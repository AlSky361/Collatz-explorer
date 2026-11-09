#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "collatz/collatz_core.hpp"

namespace collatz::summary {
// Edges are in original units; the last bin includes its right edge.
struct Histogram {
    std::vector<double> edges;
    std::vector<uint64_t> counts;
    bool log_space = false;
};

// Equal-width bins over the observed range; empty input or nonpositive bins
// returns an empty histogram. Constant data gets a padded range.
[[nodiscard]] Histogram build_linear_histogram(const std::vector<uint64_t>& values, int num_bins);

// Равномерные интервалы в log10-пространстве; edges возвращаются в исходных единицах.
// Неположительные значения исключаются.
[[nodiscard]] Histogram build_log_histogram(const std::vector<uint64_t>& values, int num_bins);

// Суммирует first_digit_counts в вектор длины layout.total_size.
// Использует uint64_t, чтобы избежать переполнения StepCount при суммировании строк.
[[nodiscard]] std::vector<uint64_t> sum_first_digit_counts(
    const std::vector<CollatzResult>& results, const DigitLayout& layout);

}  // namespace collatz::summary

namespace collatz::summary {
/// Online summary with memory independent of the processed range. Step counts
/// use exact frequencies; maxima use fixed logarithmic bins over [1, 2^64].
class StreamingSummary {
   public:
    StreamingSummary(const DigitLayout& layout, int num_bins);
    void add(const CollatzResult& result);
    void add(const ChainMetrics& result, std::span<const StepCount> digits);
    /// Adds one trajectory's metrics; deferred digit counts can be added separately.
    void add_metrics(const ChainMetrics& result);
    /// Adds metrics for odd n and n*2^i (i=0..doublings), with unchanged odd steps.
    /// Validates the family, uint64_t shifts and the 60000-step limit.
    void add_doubling_family(const ChainMetrics& base, unsigned doublings);
    /// Adds deferred first-digit totals without changing trajectory frequencies.
    /// Rejects a wrong width or uint64_t overflow.
    void add_digit_totals(std::span<const uint64_t> totals);
    void merge(const StreamingSummary& other);
    [[nodiscard]] int bin_count() const noexcept { return bins_; }
    [[nodiscard]] Histogram length_histogram() const;
    [[nodiscard]] Histogram odd_histogram() const;
    [[nodiscard]] Histogram even_histogram() const;
    [[nodiscard]] const Histogram& max_histogram() const noexcept { return maxima_; }
    [[nodiscard]] const std::vector<uint64_t>& digit_totals() const noexcept { return digits_; }
    [[nodiscard]] const std::vector<uint64_t>& length_counts() const noexcept { return lengths_; }
    [[nodiscard]] uint64_t count() const noexcept { return count_; }

   private:
    [[nodiscard]] Histogram rebin(const std::vector<uint64_t>& frequency) const;
    [[nodiscard]] size_t max_bin(uint64_t value) const;
    int bins_;
    std::vector<int> bases_;
    uint64_t count_ = 0;
    std::vector<uint64_t> lengths_;
    std::vector<uint64_t> odd_;
    std::vector<uint64_t> even_;
    std::vector<uint64_t> digits_;
    Histogram maxima_;
    std::array<size_t, 65> max_search_begin_{};
    std::array<size_t, 65> max_search_end_{};
};
}  // namespace collatz::summary
