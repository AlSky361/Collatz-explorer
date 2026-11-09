#pragma once

#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>

#include "collatz/collatz_core.hpp"
#include "collatz/summary.hpp"

namespace collatz {
/// Reusable flat counters; no per-row allocation. Rows remain valid until resize.
class ResultBatch {
   public:
    void resize(size_t rows, size_t width);
    [[nodiscard]] std::span<StepCount> digits(size_t row);
    [[nodiscard]] std::span<const StepCount> digits(size_t row) const;
    std::vector<ChainMetrics> metrics;

   private:
    size_t width_ = 0;
    std::vector<StepCount> digits_;
};

/// Persistent workers with private buffers; owner serializes calls. Layout/cache must outlive this.
/// Frozen cache reads need no locks.
class RangeExecutor {
   public:
    RangeExecutor(const DigitLayout& layout, ChainMemo* memo, unsigned thread_count);
    ~RangeExecutor();
    RangeExecutor(const RangeExecutor&) = delete;
    RangeExecutor& operator=(const RangeExecutor&) = delete;
    void compute_batch(uint64_t first, size_t count, ResultBatch& batch);
    /// Starts one asynchronous batch. Until wait() or destruction returns, keep
    /// batch alive and do not read, resize or write it. A pending job rejects starts.
    void start_batch(uint64_t first, size_t count, ResultBatch& batch);
    /// Joins the pending job and rethrows worker errors; the executor remains reusable.
    void wait();
    /// Aggregates worker-local summaries; no batch of individual results exists.
    void summarize(uint64_t first, uint64_t last, summary::StreamingSummary& target);

   private:
    struct Worker {
        ChainScratch scratch;
        std::vector<StepCount> digits;
        std::vector<uint64_t> tail_weights;
        std::vector<uint64_t> digit_totals;
        std::unique_ptr<summary::StreamingSummary> summary;
    };
    void start_dispatch(uint64_t first, uint64_t count, ResultBatch* batch, int bins);
    void worker_loop(size_t index, std::stop_token stop);
    void execute(size_t index);
    void execute_range(Worker& worker, uint64_t begin, uint64_t end,
                       const detail::FrozenMemoView* memo);
    void summarize_family(Worker& worker, uint64_t odd, const detail::FrozenMemoView& memo);
    [[nodiscard]] bool claim_range(uint64_t& begin, uint64_t& end);
    const DigitLayout& layout_;
    ChainMemo* memo_;
    std::vector<Worker> workers_;
    std::mutex mutex_;
    std::condition_variable_any work_ready_;
    std::condition_variable done_;
    uint64_t generation_ = 0;
    uint64_t first_ = 0;
    uint64_t count_ = 0;
    uint64_t work_count_ = 0;
    bool group_doublings_ = false;
    uint64_t chunk_size_ = 1;
    std::atomic<uint64_t> next_offset_{0};
    size_t remaining_ = 0;
    bool pending_ = false;
    ResultBatch* batch_ = nullptr;
    int summary_bins_ = 0;
    std::exception_ptr failure_;
    std::atomic<bool> failed_{false};
    // Declared last so construction failure joins workers before shared state dies.
    std::vector<std::jthread> threads_;
};
}  // namespace collatz
