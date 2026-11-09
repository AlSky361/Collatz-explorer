#pragma once

#include <stdexcept>

#include "collatz/collatz_core.hpp"

namespace collatz::detail {
// A summary amortizes one dense tail-frequency array per worker over its starts.
inline uint64_t deferred_tail_slots(const DigitLayout& layout, uint64_t capacity, uint64_t count,
                                    uint64_t workers) noexcept {
    const uint64_t slots = memo_entry_count(capacity);
    return workers != 0 && !layout.counted_bases.empty() && count / workers >= slots ? slots : 0;
}

// Construction acquires the permanent frozen state once. The cache must outlive
// the view. Methods read immutable arrays, without per-step atomics or locks.
class FrozenMemoView {
   public:
    explicit FrozenMemoView(const ChainMemo& memo) : memo_(memo) {
        if (!memo.is_frozen()) throw std::logic_error("Expected a frozen cache");
    }
    [[nodiscard]] bool try_metrics(uint64_t value, ChainMetrics& out) const noexcept {
        if (value % 2 == 0 || value > memo_.capacity_) return false;
        const auto index = static_cast<size_t>(value / 2);
        if (value != 1 && memo_.length_[index] == 0) return false;
        out.n = value;
        out.length = value == 1 ? 0 : memo_.length_[index];
        out.odd_steps = value == 1 ? 0 : memo_.odd_steps_[index];
        out.even_steps = static_cast<StepCount>(out.length - out.odd_steps);
        out.max_value = value == 1 ? 1 : memo_.max_value_[index];
        return true;
    }
    // Adds weighted tails except base 2, whose full count is derived per start.
    // Rejects wrong dimensions, missing entries and uint64_t overflow.
    void add_nonbinary_tail_digits(std::span<const uint64_t> weights,
                                   std::span<uint64_t> totals) const;

   private:
    const ChainMemo& memo_;
};

struct PrefixResult {
    ChainMetrics metrics;
    uint64_t tail = 0;
};

// Adds nonbinary prefixes and full binary counts; returns unweighted metrics and tail id.
// Caller adds the tail with the same weight (1..64).
[[nodiscard]] PrefixResult summarize_prefix(uint64_t start, const DigitLayout& layout,
                                            std::span<uint64_t> totals, const FrozenMemoView& memo,
                                            uint64_t weight = 1);

// Adds prefixes preceding odd in starts odd*2^j, j=0..doublings. Each value
// odd*2^i occurs doublings-i+1 times. Throws on invalid dimensions or shifts.
void record_doubling_prefixes(uint64_t odd, unsigned doublings, const DigitLayout& layout,
                              std::span<uint64_t> totals);
}  // namespace collatz::detail
