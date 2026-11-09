#include "collatz/collatz_core.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cassert>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>

#include "internal/digit_counter.hpp"
#include "internal/frozen_memo.hpp"

namespace collatz {
namespace {

uint64_t next_value(uint64_t value) {
    if (value % 2 == 0) return value / 2;
    if (value > (UINT64_MAX - 1) / 3) {
        throw std::overflow_error("3n+1 exceeds uint64_t at n=" + std::to_string(value));
    }
    return 3 * value + 1;
}

template <typename Count>
void record_digits(uint64_t value, const DigitLayout& layout, std::span<Count> digits,
                   Count weight = 1) {
    const auto width = static_cast<unsigned>(std::bit_width(value));
    if (layout.standard_bases) {
        digits[2 + static_cast<size_t>(value >> (((width - 1) / 3) * 3))] += weight;
        digits[10 + static_cast<size_t>(detail::leading_digit(value, width, layout, 2))] += weight;
        digits[20 + static_cast<size_t>(value >> ((width - 1) & ~3u))] += weight;
        return;
    }
    for (const size_t b : layout.counted_bases) {
        const int digit = detail::leading_digit(value, width, layout, b);
        digits[layout.offsets[b] + static_cast<size_t>(digit)] += weight;
    }
}

void count_step(ChainMetrics& result, uint64_t value) {
    if (result.length >= kMaxChainSteps) {
        throw std::runtime_error("Trajectory exceeds the 60000-step resource limit");
    }
    ++result.length;
    if (value % 2 == 0)
        ++result.even_steps;
    else
        ++result.odd_steps;
}

// Only even values precede the returned odd value; they cannot hit the odd cache.
// Record every value while updating the monotone segment's metrics just once.
uint64_t record_even_run(uint64_t value, ChainMetrics& result, const DigitLayout& layout,
                         std::span<uint64_t> totals, uint64_t weight = 1) {
    const auto count = static_cast<unsigned>(std::countr_zero(value));
    if (result.length + count > kMaxChainSteps)
        throw std::runtime_error("Trajectory exceeds the 60000-step resource limit");
    result.max_value = std::max(result.max_value, value);
    for (unsigned i = 0; i < count; ++i) {
        record_digits(value, layout, totals, weight);
        value >>= 1;
    }
    result.length = static_cast<StepCount>(result.length + count);
    result.even_steps = static_cast<StepCount>(result.even_steps + count);
    return value;
}

ChainMetrics compute_direct(uint64_t start, const DigitLayout& layout,
                            std::span<StepCount> digits) {
    ChainMetrics result;
    result.n = start;
    std::fill(digits.begin(), digits.end(), 0);
    uint64_t value = start;
    while (true) {
        result.max_value = std::max(result.max_value, value);
        record_digits(value, layout, digits);
        if (value == 1) break;
        count_step(result, value);
        value = next_value(value);
    }
    return result;
}

template <typename Lookup, typename Store>
ChainMetrics compute_mutable(uint64_t start, const DigitLayout& layout, std::span<StepCount> digits,
                             ChainScratch& scratch, Lookup lookup, Store store) {
    auto& path = scratch.path;
    path.clear();
    ChainMetrics result;
    uint64_t value = start;
    while (value != 1 && !lookup(value, result, digits)) {
        if (path.size() >= kMaxChainSteps)
            throw std::runtime_error("Trajectory exceeds the 60000-step resource limit");
        path.push_back(value);
        value = next_value(value);
    }
    if (value == 1) {
        result.max_value = 1;
        std::fill(digits.begin(), digits.end(), 0);
        record_digits(1, layout, digits);
    }
    result.n = start;
    for (auto it = path.rbegin(); it != path.rend(); ++it) {
        count_step(result, *it);
        result.max_value = std::max(result.max_value, *it);
        record_digits(*it, layout, digits);
        store(*it, result, digits);
    }
    return result;
}

void merge_tail(ChainMetrics& result, std::span<StepCount> digits, const ChainMetrics& tail,
                std::span<const StepCount> tail_digits) {
    const unsigned length = result.length + tail.length;
    if (length > kMaxChainSteps)
        throw std::runtime_error("Trajectory exceeds the 60000-step resource limit");
    result.length = static_cast<StepCount>(length);
    result.odd_steps = static_cast<StepCount>(result.odd_steps + tail.odd_steps);
    result.even_steps = static_cast<StepCount>(result.even_steps + tail.even_steps);
    result.max_value = std::max(result.max_value, tail.max_value);
    for (size_t i = 0; i < digits.size(); ++i)
        digits[i] = static_cast<StepCount>(digits[i] + tail_digits[i]);
}

ChainMetrics compute_readonly(uint64_t start, const DigitLayout& layout,
                              std::span<StepCount> digits, ChainScratch& scratch,
                              const ChainMemo& memo) {
    ChainMetrics result;
    if (memo.try_get_into(start, result, digits)) return result;
    result.n = start;
    std::fill(digits.begin(), digits.end(), 0);
    scratch.tail_digits.resize(layout.total_size);
    uint64_t value = start;
    while (true) {
        ChainMetrics tail;
        if (memo.try_get_into(value, tail, scratch.tail_digits)) {
            merge_tail(result, digits, tail, scratch.tail_digits);
            return result;
        }
        result.max_value = std::max(result.max_value, value);
        record_digits(value, layout, digits);
        if (value == 1) return result;
        count_step(result, value);
        value = next_value(value);
    }
}

std::vector<size_t> stored_indices(const DigitLayout& layout) {
    std::vector<size_t> indices;
    for (size_t b = 0; b < layout.bases.size(); ++b) {
        if (layout.bases[b] == 2) continue;
        for (int d = 1; d < layout.bases[b]; ++d)
            indices.push_back(layout.offsets[b] + static_cast<size_t>(d));
    }
    return indices;
}

size_t cache_elements(uint64_t capacity, size_t width) {
    if (capacity == 0 || capacity > std::vector<uint64_t>().max_size() ||
        (width != 0 && capacity > std::vector<StepCount>().max_size() / width)) {
        throw std::length_error("Cache dimensions exceed vector limits");
    }
    return static_cast<size_t>(capacity) * width;
}

size_t cache_capacity(uint64_t capacity, size_t width) {
    (void)cache_elements(capacity, width);
    return static_cast<size_t>(capacity);
}

}  // namespace

detail::PrefixResult detail::summarize_prefix(uint64_t start, const DigitLayout& layout,
                                              std::span<uint64_t> totals,
                                              const FrozenMemoView& memo, uint64_t weight) {
    if (start == 0 || totals.size() != layout.total_size || weight == 0 || weight > 64)
        throw std::invalid_argument("Invalid summary prefix input");
    PrefixResult result;
    result.metrics.n = start;
    uint64_t value = start;
    while (true) {
        value = record_even_run(value, result.metrics, layout, totals, weight);
        ChainMetrics tail;
        if (memo.try_metrics(value, tail)) {
            const unsigned length = result.metrics.length + tail.length;
            if (length > kMaxChainSteps)
                throw std::runtime_error("Trajectory exceeds the 60000-step resource limit");
            result.metrics.length = static_cast<StepCount>(length);
            result.metrics.odd_steps =
                static_cast<StepCount>(result.metrics.odd_steps + tail.odd_steps);
            result.metrics.even_steps =
                static_cast<StepCount>(result.metrics.even_steps + tail.even_steps);
            result.metrics.max_value = std::max(result.metrics.max_value, tail.max_value);
            result.tail = value;
            break;
        }
        record_digits(value, layout, totals, weight);
        if (value == 1) break;
        count_step(result.metrics, value);
        value = next_value(value);
    }
    if (layout.binary_offset != SIZE_MAX)
        totals[layout.binary_offset + 1] +=
            (static_cast<uint64_t>(result.metrics.length) + 1) * weight;
    return result;
}

void detail::record_doubling_prefixes(uint64_t odd, unsigned doublings, const DigitLayout& layout,
                                      std::span<uint64_t> totals) {
    if (odd % 2 == 0 || doublings >= 64 || odd > (UINT64_MAX >> doublings) ||
        totals.size() != layout.total_size)
        throw std::invalid_argument("Invalid doubling family");
    uint64_t value = odd;
    for (unsigned i = 1; i <= doublings; ++i) {
        value <<= 1;
        record_digits(value, layout, totals, static_cast<uint64_t>(doublings - i + 1));
    }
    if (layout.binary_offset != SIZE_MAX)
        totals[layout.binary_offset + 1] += static_cast<uint64_t>(doublings) * (doublings + 1) / 2;
}

void detail::FrozenMemoView::add_nonbinary_tail_digits(std::span<const uint64_t> weights,
                                                       std::span<uint64_t> totals) const {
    if (weights.size() != memo_entry_count(memo_.capacity_) ||
        totals.size() != memo_.layout_.total_size)
        throw std::invalid_argument("Invalid weighted tail dimensions");
    const auto add = [&](size_t slot, uint64_t weight, uint64_t digit_count) {
        if (digit_count != 0 && weight > (UINT64_MAX - totals[slot]) / digit_count)
            throw std::overflow_error("Weighted digit total exceeds uint64_t");
        totals[slot] += weight * digit_count;
    };
    for (size_t index = 0; index < weights.size(); ++index) {
        const uint64_t weight = weights[index];
        if (weight == 0) continue;
        if (index == 0) {
            for (const size_t b : memo_.layout_.counted_bases)
                add(memo_.layout_.offsets[b] + 1, weight, 1);
            continue;
        }
        if (memo_.length_[index] == 0) throw std::logic_error("Weighted tail is not cached");
        const size_t offset = index * memo_.stored_indices_.size();
        for (size_t i = 0; i < memo_.stored_indices_.size(); ++i)
            add(memo_.stored_indices_[i], weight, memo_.digits_flat_[offset + i]);
    }
}

DigitLayout make_digit_layout(const std::vector<int>& bases) {
    if (bases.empty()) throw std::invalid_argument("bases must not be empty");
    DigitLayout layout;
    layout.bases = bases;
    bool seen[37]{};
    for (const int base : bases) {
        if (base < 2 || base > 36 || seen[base]) {
            throw std::invalid_argument("bases must be unique integers in [2, 36]");
        }
        seen[base] = true;
        if (base == 2)
            layout.binary_offset = layout.total_size;
        else
            layout.counted_bases.push_back(layout.offsets.size());
        layout.offsets.push_back(layout.total_size);
        layout.total_size += static_cast<size_t>(base);
        const auto unsigned_base = static_cast<unsigned>(base);
        const unsigned bits = std::has_single_bit(unsigned_base)
                                  ? static_cast<unsigned>(std::bit_width(unsigned_base) - 1)
                                  : 0u;
        layout.bit_shifts.push_back(bits);
        std::vector<uint64_t> powers{1};
        if (bits == 0) {
            while (powers.back() <= UINT64_MAX / unsigned_base)
                powers.push_back(powers.back() * unsigned_base);
        }
        std::array<uint8_t, 65> scales{};
        for (unsigned width = 1; width <= 64; ++width) {
            if (bits != 0)
                scales[width] = static_cast<uint8_t>(((width - 1) / bits) * bits);
            else {
                const uint64_t lower = uint64_t{1} << (width - 1);
                const auto it = std::upper_bound(powers.begin(), powers.end(), lower);
                scales[width] = static_cast<uint8_t>(it - powers.begin() - 1);
            }
        }
        layout.digit_scales.push_back(scales);
        layout.powers.push_back(std::move(powers));
    }
    layout.standard_bases = bases == std::vector<int>({2, 8, 10, 16});
    return layout;
}

uint64_t memo_bytes_per_entry(const DigitLayout& layout) noexcept {
    uint64_t stored = 0;
    for (const int base : layout.bases)
        if (base != 2) stored += static_cast<unsigned>(base - 1);
    return 2 * sizeof(StepCount) + sizeof(uint64_t) + stored * sizeof(StepCount);
}

ChainMemo::ChainMemo(uint64_t capacity, const DigitLayout& layout)
    : capacity_(capacity),
      layout_(make_digit_layout(layout.bases)),
      stored_indices_(stored_indices(layout_)),
      length_(cache_capacity(memo_entry_count(capacity), stored_indices_.size()), 0),
      odd_steps_(static_cast<size_t>(memo_entry_count(capacity)), 0),
      max_value_(static_cast<size_t>(memo_entry_count(capacity)), 0),
      digits_flat_(cache_elements(memo_entry_count(capacity), stored_indices_.size()), 0) {
    if (!matches(layout)) throw std::invalid_argument("Invalid digit layout");
}

bool ChainMemo::matches(const DigitLayout& layout) const noexcept {
    return layout.bases == layout_.bases && layout.offsets == layout_.offsets &&
           layout.total_size == layout_.total_size;
}

bool ChainMemo::read_entry(uint64_t value, ChainMetrics& out, std::span<StepCount> digits) const {
    const auto idx = static_cast<size_t>(value / 2);
    if (value != 1 && length_[idx] == 0) return false;
    out.n = value;
    out.length = value == 1 ? 0 : length_[idx];
    out.odd_steps = value == 1 ? 0 : odd_steps_[idx];
    out.even_steps = static_cast<StepCount>(out.length - out.odd_steps);
    out.max_value = value == 1 ? 1 : max_value_[idx];
    std::fill(digits.begin(), digits.end(), 0);
    if (value != 1) {
        const size_t offset = idx * stored_indices_.size();
        if (layout_.standard_bases) {
            const auto* source = digits_flat_.data() + offset;
            std::copy_n(source, 7, digits.data() + 3);
            std::copy_n(source + 7, 9, digits.data() + 11);
            std::copy_n(source + 16, 15, digits.data() + 21);
        } else {
            for (size_t i = 0; i < stored_indices_.size(); ++i)
                digits[stored_indices_[i]] = digits_flat_[offset + i];
        }
    }
    for (size_t b = 0; b < layout_.bases.size(); ++b) {
        if (value == 1)
            digits[layout_.offsets[b] + 1] = 1;
        else if (layout_.bases[b] == 2)
            digits[layout_.offsets[b] + 1] = static_cast<StepCount>(out.length + 1);
    }
    return true;
}

bool ChainMemo::try_get_into(uint64_t value, ChainMetrics& out, std::span<StepCount> digits) const {
    if (value % 2 == 0 || value > capacity_) return false;
    if (digits.size() != layout_.total_size)
        throw std::invalid_argument("Cache counter buffer has wrong size");
    if (is_frozen()) return read_entry(value, out, digits);
    const std::lock_guard lock(mutex_);
    return read_entry(value, out, digits);
}

bool ChainMemo::try_get(uint64_t value, CachedEntry& out) const {
    out.first_digit_counts.resize(layout_.total_size);
    out.has_value = try_get_into(value, out, out.first_digit_counts);
    return out.has_value;
}

void ChainMemo::set(uint64_t value, StepCount length, StepCount odd_steps, StepCount even_steps,
                    uint64_t max_value, std::span<const StepCount> digits) {
    if (value % 2 == 0 || value > capacity_) return;
    if (digits.size() != layout_.total_size || length > kMaxChainSteps ||
        odd_steps + even_steps != length || (value != 1 && length == 0)) {
        throw std::invalid_argument("Invalid cache entry");
    }
    const std::lock_guard lock(mutex_);
    if (is_frozen()) throw std::logic_error("Cannot modify a frozen cache");
    write_entry(value, length, odd_steps, max_value, digits);
}

void ChainMemo::write_entry(uint64_t value, StepCount length, StepCount odd_steps,
                            uint64_t max_value, std::span<const StepCount> digits) {
    if (value == 1) return;
    const auto idx = static_cast<size_t>(value / 2);
    if (length_[idx] != 0) return;
    odd_steps_[idx] = odd_steps;
    max_value_[idx] = max_value;
    const size_t offset = idx * stored_indices_.size();
    if (layout_.standard_bases) {
        auto* destination = digits_flat_.data() + offset;
        std::copy_n(digits.data() + 3, 7, destination);
        std::copy_n(digits.data() + 11, 9, destination + 7);
        std::copy_n(digits.data() + 21, 15, destination + 16);
    } else {
        for (size_t i = 0; i < stored_indices_.size(); ++i)
            digits_flat_[offset + i] = digits[stored_indices_[i]];
    }
    length_[idx] = length;
}

void ChainMemo::freeze() {
    const std::lock_guard lock(mutex_);
    frozen_.store(true, std::memory_order_release);
}

ChainMetrics collatz_chain_into(uint64_t start_n, const DigitLayout& layout,
                                std::span<StepCount> digits, ChainScratch& scratch,
                                ChainMemo* memo) {
    if (start_n == 0) throw std::invalid_argument("start_n must be positive");
    if (digits.size() != layout.total_size)
        throw std::invalid_argument("Counter buffer has wrong size");
    if (memo != nullptr && !memo->matches(layout))
        throw std::invalid_argument("Cache and digit layout do not match");
    const auto compute = [&] {
        if (memo == nullptr) return compute_direct(start_n, layout, digits);
        if (memo->is_frozen()) return compute_readonly(start_n, layout, digits, scratch, *memo);
        return compute_mutable(
            start_n, layout, digits, scratch,
            [&](uint64_t value, ChainMetrics& out, std::span<StepCount> counters) {
                return memo->try_get_into(value, out, counters);
            },
            [&](uint64_t value, const ChainMetrics& out, std::span<const StepCount> counters) {
                memo->set(value, out.length, out.odd_steps, out.even_steps, out.max_value,
                          counters);
            });
    };
    const auto result = compute();
    if (layout.binary_offset != SIZE_MAX)
        digits[layout.binary_offset + 1] = static_cast<StepCount>(result.length + 1);
    return result;
}

CollatzResult collatz_chain(uint64_t start_n, const DigitLayout& layout, ChainMemo* memo) {
    CollatzResult result;
    result.first_digit_counts.resize(layout.total_size);
    ChainScratch scratch;
    static_cast<ChainMetrics&>(result) =
        collatz_chain_into(start_n, layout, result.first_digit_counts, scratch, memo);
    return result;
}

void prepare_memo(ChainMemo& memo, const DigitLayout& layout) {
    if (!memo.matches(layout)) throw std::invalid_argument("Cache and digit layout do not match");
    const std::lock_guard lock(memo.mutex_);
    if (memo.is_frozen()) return;
    ChainScratch scratch;
    std::vector<StepCount> digits(layout.total_size);
    for (uint64_t n = 3; n <= memo.capacity(); n += 2)
        (void)compute_mutable(
            n, layout, digits, scratch,
            [&](uint64_t value, ChainMetrics& out, std::span<StepCount> counters) {
                return value <= memo.capacity_ && value % 2 != 0 &&
                       memo.read_entry(value, out, counters);
            },
            [&](uint64_t value, const ChainMetrics& out, std::span<const StepCount> counters) {
                if (value <= memo.capacity_ && value % 2 != 0)
                    memo.write_entry(value, out.length, out.odd_steps, out.max_value, counters);
            });
    memo.frozen_.store(true, std::memory_order_release);
}

unsigned resolve_thread_count(unsigned requested) noexcept {
    if (requested != 0) return requested;
    return std::clamp(std::thread::hardware_concurrency(), 1u, 32u);
}

std::vector<CollatzResult> collatz_batch_parallel(uint64_t first, uint64_t last,
                                                  const DigitLayout& layout, ChainMemo* memo,
                                                  unsigned thread_count) {
    if (first == 0 || last < first) throw std::invalid_argument("Invalid batch interval");
    const uint64_t count = last - first + 1;
    if (count > std::vector<CollatzResult>().max_size()) throw std::length_error("Batch too large");
    std::vector<CollatzResult> results(static_cast<size_t>(count));
    const auto threads =
        static_cast<unsigned>(std::min<uint64_t>(resolve_thread_count(thread_count), count));
    const uint64_t chunk = 1 + (count - 1) / threads;
    std::exception_ptr failure;
    std::mutex failure_mutex;
    std::atomic<bool> failed{false};
    std::vector<std::jthread> workers;
    for (unsigned t = 0; t < threads; ++t) {
        const uint64_t begin = t * chunk;
        if (begin >= count) break;
        const uint64_t end = begin + std::min(chunk, count - begin);
        workers.emplace_back([&, begin, end] {
            try {
                for (uint64_t i = begin; i < end && !failed.load(); ++i) {
                    results[static_cast<size_t>(i)] = collatz_chain(first + i, layout, memo);
                }
            } catch (...) {
                const std::lock_guard lock(failure_mutex);
                if (!failure) failure = std::current_exception();
                failed.store(true);
            }
        });
    }
    workers.clear();
    if (failure) std::rethrow_exception(failure);
    return results;
}

std::vector<CollatzResult> collatz_range_parallel(uint64_t n_max, const DigitLayout& layout,
                                                  ChainMemo* memo) {
    return collatz_batch_parallel(1, n_max, layout, memo);
}

std::vector<uint64_t> collatz_trajectory(uint64_t start_n) {
    if (start_n == 0) throw std::invalid_argument("start_n must be positive");
    std::vector<uint64_t> result{start_n};
    while (result.back() != 1) {
        if (result.size() > kMaxChainSteps) {
            throw std::runtime_error("Trajectory exceeds the 60000-step resource limit");
        }
        result.push_back(next_value(result.back()));
    }
    return result;
}

}  // namespace collatz
