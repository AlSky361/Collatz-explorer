#include "collatz/range_executor.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <optional>
#include <stdexcept>

#include "internal/frozen_memo.hpp"

namespace collatz {
void ResultBatch::resize(size_t rows, size_t width) {
    if (width == 0 || rows > digits_.max_size() / width || rows > metrics.max_size()) {
        throw std::length_error("Batch dimensions exceed vector limits");
    }
    metrics.resize(rows);
    digits_.resize(rows * width);
    width_ = width;
}

std::span<StepCount> ResultBatch::digits(size_t row) {
    if (row >= metrics.size()) throw std::out_of_range("Invalid batch row");
    return std::span(digits_).subspan(row * width_, width_);
}

std::span<const StepCount> ResultBatch::digits(size_t row) const {
    if (row >= metrics.size()) throw std::out_of_range("Invalid batch row");
    return std::span(digits_).subspan(row * width_, width_);
}

RangeExecutor::RangeExecutor(const DigitLayout& layout, ChainMemo* memo, unsigned thread_count)
    : layout_(layout), memo_(memo), workers_(resolve_thread_count(thread_count)) {
    if (memo_ != nullptr && !memo_->matches(layout_))
        throw std::invalid_argument("Cache/layout mismatch");
    for (auto& worker : workers_) worker.digits.resize(layout_.total_size);
    threads_.reserve(workers_.size());
    for (size_t i = 0; i < workers_.size(); ++i) {
        threads_.emplace_back([this, i](std::stop_token stop) { worker_loop(i, stop); });
    }
}

RangeExecutor::~RangeExecutor() {
    for (auto& thread : threads_) thread.request_stop();
    work_ready_.notify_all();
    threads_.clear();
}

void RangeExecutor::start_dispatch(uint64_t first, uint64_t count, ResultBatch* batch, int bins) {
    if (first == 0 || count == 0 || count - 1 > UINT64_MAX - first)
        throw std::invalid_argument("Invalid range");
    const std::lock_guard lock(mutex_);
    if (pending_) throw std::logic_error("Wait for the pending job before starting another");
    if (batch != nullptr) batch->resize(static_cast<size_t>(count), layout_.total_size);
    first_ = first;
    count_ = count;
    group_doublings_ =
        batch == nullptr && first == 1 && memo_ != nullptr && memo_->is_frozen() &&
        detail::deferred_tail_slots(layout_, memo_->capacity(), count, workers_.size()) != 0;
    work_count_ = group_doublings_ ? memo_entry_count(count) : count;
    chunk_size_ =
        std::max<uint64_t>(1, std::min<uint64_t>(4096, work_count_ / workers_.size() / 4));
    next_offset_.store(0, std::memory_order_relaxed);
    batch_ = batch;
    summary_bins_ = bins;
    failure_ = nullptr;
    failed_.store(false, std::memory_order_relaxed);
    remaining_ = workers_.size();
    pending_ = true;
    ++generation_;
    work_ready_.notify_all();
}

void RangeExecutor::wait() {
    std::unique_lock lock(mutex_);
    if (!pending_) throw std::logic_error("No pending job");
    done_.wait(lock, [this] { return remaining_ == 0; });
    pending_ = false;
    if (failure_) std::rethrow_exception(failure_);
}

void RangeExecutor::compute_batch(uint64_t first, size_t count, ResultBatch& batch) {
    start_batch(first, count, batch);
    wait();
}

void RangeExecutor::start_batch(uint64_t first, size_t count, ResultBatch& batch) {
    start_dispatch(first, count, &batch, 0);
}

void RangeExecutor::summarize(uint64_t first, uint64_t last, summary::StreamingSummary& target) {
    if (first == 0 || last < first) throw std::invalid_argument("Invalid range");
    start_dispatch(first, last - first + 1, nullptr, target.bin_count());
    wait();
    for (const auto& worker : workers_) target.merge(*worker.summary);
}

void RangeExecutor::worker_loop(size_t index, std::stop_token stop) {
    uint64_t observed = 0;
    while (true) {
        std::unique_lock lock(mutex_);
        work_ready_.wait(lock, stop, [&] { return generation_ != observed; });
        if (stop.stop_requested()) return;
        observed = generation_;
        lock.unlock();
        try {
            execute(index);
        } catch (...) {
            const std::lock_guard failure_lock(mutex_);
            if (!failure_) failure_ = std::current_exception();
            failed_.store(true, std::memory_order_relaxed);
        }
        lock.lock();
        if (--remaining_ == 0) done_.notify_one();
    }
}

void RangeExecutor::execute(size_t index) {
    auto& worker = workers_[index];
    if (batch_ == nullptr)
        worker.summary = std::make_unique<summary::StreamingSummary>(layout_, summary_bins_);
    const uint64_t slots =
        batch_ == nullptr && memo_ != nullptr && memo_->is_frozen()
            ? detail::deferred_tail_slots(layout_, memo_->capacity(), count_, workers_.size())
            : 0;
    std::optional<detail::FrozenMemoView> view;
    if (slots != 0) {
        worker.tail_weights.assign(static_cast<size_t>(slots), 0);
        worker.digit_totals.assign(layout_.total_size, 0);
        view.emplace(*memo_);
    }
    uint64_t begin = 0, end = 0;
    if (batch_ != nullptr || workers_.size() == 1) {
        const uint64_t chunk = 1 + (work_count_ - 1) / workers_.size();
        begin = std::min<uint64_t>(index * chunk, work_count_);
        end = begin + std::min(chunk, work_count_ - begin);
        execute_range(worker, begin, end, view ? &*view : nullptr);
    } else {
        while (claim_range(begin, end)) execute_range(worker, begin, end, view ? &*view : nullptr);
    }
    if (view) {
        view->add_nonbinary_tail_digits(worker.tail_weights, worker.digit_totals);
        worker.summary->add_digit_totals(worker.digit_totals);
    }
}

bool RangeExecutor::claim_range(uint64_t& begin, uint64_t& end) {
    begin = next_offset_.load(std::memory_order_relaxed);
    while (begin < work_count_ && !failed_.load(std::memory_order_relaxed)) {
        end = begin + std::min(chunk_size_, work_count_ - begin);
        if (next_offset_.compare_exchange_weak(begin, end, std::memory_order_relaxed)) return true;
    }
    return false;
}

void RangeExecutor::execute_range(Worker& worker, uint64_t begin, uint64_t end,
                                  const detail::FrozenMemoView* memo) {
    for (uint64_t offset = begin; offset < end && !failed_.load(std::memory_order_relaxed);
         ++offset) {
        if (memo != nullptr) {
            if (group_doublings_) {
                summarize_family(worker, 2 * offset + 1, *memo);
                continue;
            }
            const auto result =
                detail::summarize_prefix(first_ + offset, layout_, worker.digit_totals, *memo);
            worker.summary->add_metrics(result.metrics);
            if (result.tail != 0) ++worker.tail_weights[static_cast<size_t>(result.tail / 2)];
            continue;
        }
        auto digits = batch_ != nullptr ? batch_->digits(static_cast<size_t>(offset))
                                        : std::span(worker.digits);
        const auto result =
            collatz_chain_into(first_ + offset, layout_, digits, worker.scratch, memo_);
        if (batch_ != nullptr)
            batch_->metrics[static_cast<size_t>(offset)] = result;
        else
            worker.summary->add(result, digits);
    }
}

void RangeExecutor::summarize_family(Worker& worker, uint64_t odd,
                                     const detail::FrozenMemoView& memo) {
    const auto doublings = static_cast<unsigned>(std::bit_width(count_ / odd) - 1);
    const uint64_t weight = static_cast<uint64_t>(doublings) + 1;
    const auto base = detail::summarize_prefix(odd, layout_, worker.digit_totals, memo, weight);
    worker.summary->add_doubling_family(base.metrics, doublings);
    detail::record_doubling_prefixes(odd, doublings, layout_, worker.digit_totals);
    if (base.tail != 0) worker.tail_weights[static_cast<size_t>(base.tail / 2)] += weight;
}
}  // namespace collatz
