#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <vector>

namespace collatz {
namespace detail {
class FrozenMemoView;
}
using StepCount = uint16_t;
inline constexpr StepCount kMaxChainSteps = 60000;

struct ChainMetrics {
    uint64_t n = 0;
    StepCount length = 0;
    uint64_t max_value = 0;
    StepCount odd_steps = 0;
    StepCount even_steps = 0;
};

struct CollatzResult : ChainMetrics {
    // Every value of the full trajectory, including start and terminal 1.
    std::vector<StepCount> first_digit_counts;
};

struct DigitLayout {
    std::vector<int> bases;
    std::vector<size_t> offsets;
    size_t total_size = 0;
    std::vector<unsigned> bit_shifts;
    std::vector<std::array<uint8_t, 65>> digit_scales;
    std::vector<std::vector<uint64_t>> powers;
    std::vector<size_t> counted_bases;
    size_t binary_offset = SIZE_MAX;
    bool standard_bases = false;
};

/// Validates unique bases in [2, 36]; derived tables must remain unchanged.
[[nodiscard]] DigitLayout make_digit_layout(const std::vector<int>& bases);
[[nodiscard]] uint64_t memo_bytes_per_entry(const DigitLayout& layout) noexcept;
/// Number of stored odd values within [1, capacity], without overflow.
[[nodiscard]] constexpr uint64_t memo_entry_count(uint64_t capacity) noexcept {
    return capacity / 2 + capacity % 2;
}

struct CachedEntry : ChainMetrics {
    bool has_value = false;
    std::vector<StepCount> first_digit_counts;
};

/// Odd keys in [1, capacity]; even reads miss, writes are ignored. Reconstructs the digit layout.
/// Mutable access locks. Quiesce writers before freeze(); then reads need no lock, writes throw.
class ChainMemo {
   public:
    ChainMemo(uint64_t capacity, const DigitLayout& layout);
    [[nodiscard]] bool try_get(uint64_t value, CachedEntry& out) const;
    [[nodiscard]] bool try_get_into(uint64_t value, ChainMetrics& out,
                                    std::span<StepCount> digits) const;
    void set(uint64_t value, StepCount length, StepCount odd_steps, StepCount even_steps,
             uint64_t max_value, std::span<const StepCount> digits);
    void freeze();
    [[nodiscard]] bool is_frozen() const noexcept {
        return frozen_.load(std::memory_order_acquire);
    }
    [[nodiscard]] uint64_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] bool matches(const DigitLayout& layout) const noexcept;

   private:
    friend class detail::FrozenMemoView;
    friend void prepare_memo(ChainMemo& memo, const DigitLayout& layout);
    [[nodiscard]] bool read_entry(uint64_t value, ChainMetrics& out,
                                  std::span<StepCount> digits) const;
    void write_entry(uint64_t value, StepCount length, StepCount odd_steps, uint64_t max_value,
                     std::span<const StepCount> digits);
    uint64_t capacity_;
    DigitLayout layout_;
    std::vector<size_t> stored_indices_;
    // Index=value/2 for odd values; length=0 means unfilled. n=1 is reconstructed.
    std::vector<StepCount> length_;
    std::vector<StepCount> odd_steps_;
    std::vector<uint64_t> max_value_;
    std::vector<StepCount> digits_flat_;
    std::atomic<bool> frozen_{false};
    mutable std::mutex mutex_;
};

/// One instance per worker. Buffers are reused across chains; never share it.
struct ChainScratch {
    std::vector<uint64_t> path;
    std::vector<StepCount> tail_digits;
};

/// Reuses caller-owned counters and scratch; throws on zero, uint64 overflow or
/// more than kMaxChainSteps. Layout must come from make_digit_layout().
[[nodiscard]] ChainMetrics collatz_chain_into(uint64_t start_n, const DigitLayout& layout,
                                              std::span<StepCount> digits, ChainScratch& scratch,
                                              ChainMemo* memo = nullptr);
[[nodiscard]] CollatzResult collatz_chain(uint64_t start_n, const DigitLayout& layout,
                                          ChainMemo* memo = nullptr);
/// Sequentially fills the bounded cache and freezes it before workers start.
void prepare_memo(ChainMemo& memo, const DigitLayout& layout);

/// Convenience APIs for small ranges. The CLI uses a persistent RangeExecutor.
[[nodiscard]] std::vector<CollatzResult> collatz_batch_parallel(uint64_t first, uint64_t last,
                                                                const DigitLayout& layout,
                                                                ChainMemo* memo = nullptr,
                                                                unsigned thread_count = 0);
[[nodiscard]] std::vector<CollatzResult> collatz_range_parallel(uint64_t n_max,
                                                                const DigitLayout& layout,
                                                                ChainMemo* memo = nullptr);
[[nodiscard]] unsigned resolve_thread_count(unsigned requested = 0) noexcept;
[[nodiscard]] std::vector<uint64_t> collatz_trajectory(uint64_t start_n);
}  // namespace collatz
