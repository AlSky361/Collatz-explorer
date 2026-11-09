#include "collatz/summary.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace collatz::summary {

Histogram build_linear_histogram(const std::vector<uint64_t>& values, int num_bins) {
    Histogram hist;
    if (values.empty() || num_bins <= 0) {
        return hist;
    }

    const uint64_t min_value = *std::min_element(values.begin(), values.end());
    const uint64_t max_value = *std::max_element(values.begin(), values.end());

    double lo = static_cast<double>(min_value);
    double hi = static_cast<double>(max_value);
    if (hi <= lo) {
        // Для постоянных данных берём интервал шириной 1, исключая деление на ноль.
        lo -= 0.5;
        hi += 0.5;
    }

    hist.edges.resize(static_cast<size_t>(num_bins) + 1);
    hist.counts.assign(static_cast<size_t>(num_bins), 0);
    const double width = (hi - lo) / static_cast<double>(num_bins);
    for (int i = 0; i <= num_bins; ++i) {
        hist.edges[static_cast<size_t>(i)] = lo + width * static_cast<double>(i);
    }

    for (const uint64_t v : values) {
        const double fv = static_cast<double>(v);
        int bin = static_cast<int>((fv - lo) / width);
        // Последний бин включает правую границу, как в numpy.histogram.
        if (bin >= num_bins) {
            bin = num_bins - 1;
        }
        if (bin < 0) {
            bin = 0;
        }
        ++hist.counts[static_cast<size_t>(bin)];
    }

    return hist;
}

Histogram build_log_histogram(const std::vector<uint64_t>& values, int num_bins) {
    Histogram hist;
    hist.log_space = true;
    if (num_bins <= 0) {
        return hist;
    }

    std::vector<uint64_t> positive;
    positive.reserve(values.size());
    for (const uint64_t v : values) {
        if (v > 0) {
            positive.push_back(v);
        }
    }
    if (positive.empty()) {
        return hist;
    }

    const uint64_t min_value = *std::min_element(positive.begin(), positive.end());
    const uint64_t max_value = *std::max_element(positive.begin(), positive.end());

    double log_lo = std::log10(static_cast<double>(min_value));
    double log_hi = std::log10(static_cast<double>(max_value));
    if (log_hi <= log_lo) {
        log_lo -= 0.5;
        log_hi += 0.5;
    }

    hist.edges.resize(static_cast<size_t>(num_bins) + 1);
    hist.counts.assign(static_cast<size_t>(num_bins), 0);
    const double width = (log_hi - log_lo) / static_cast<double>(num_bins);
    for (int i = 0; i <= num_bins; ++i) {
        // Возвращаем границы в исходных единицах для прямой передачи в Python.
        hist.edges[static_cast<size_t>(i)] =
            std::pow(10.0, log_lo + width * static_cast<double>(i));
    }

    for (const uint64_t v : positive) {
        const double log_v = std::log10(static_cast<double>(v));
        int bin = static_cast<int>((log_v - log_lo) / width);
        if (bin >= num_bins) {
            bin = num_bins - 1;
        }
        if (bin < 0) {
            bin = 0;
        }
        ++hist.counts[static_cast<size_t>(bin)];
    }

    return hist;
}

std::vector<uint64_t> sum_first_digit_counts(const std::vector<CollatzResult>& results,
                                             const DigitLayout& layout) {
    std::vector<uint64_t> totals(layout.total_size, 0);
    for (const CollatzResult& r : results) {
        for (size_t i = 0; i < layout.total_size; ++i) {
            totals[i] += r.first_digit_counts[i];
        }
    }
    return totals;
}

}  // namespace collatz::summary

namespace collatz::summary {
namespace {
size_t locate_bin(const Histogram& hist, double value) {
    const auto it = std::upper_bound(hist.edges.begin(), hist.edges.end(), value);
    const auto position = static_cast<size_t>(it - hist.edges.begin());
    return std::min(position == 0 ? 0 : position - 1, hist.counts.size() - 1);
}
}  // namespace

StreamingSummary::StreamingSummary(const DigitLayout& layout, int num_bins)
    : bins_(num_bins),
      bases_(layout.bases),
      lengths_(1, 0),
      odd_(1, 0),
      even_(1, 0),
      digits_(layout.total_size, 0),
      maxima_(build_log_histogram({1, UINT64_MAX}, num_bins)) {
    if (num_bins <= 0 || num_bins > 10000) throw std::invalid_argument("Invalid summary bin count");
    std::fill(maxima_.counts.begin(), maxima_.counts.end(), 0);
    // A w-bit integer, converted to double, stays in [2^(w-1), 2^w].
    // Include the upper endpoint: large integers may round up to it.
    for (int width = 1; width <= 64; ++width) {
        const auto begin = std::upper_bound(maxima_.edges.begin(), maxima_.edges.end(),
                                            std::ldexp(1.0, width - 1));
        const auto end =
            std::upper_bound(maxima_.edges.begin(), maxima_.edges.end(), std::ldexp(1.0, width));
        max_search_begin_[static_cast<size_t>(width)] =
            static_cast<size_t>(begin - maxima_.edges.begin());
        max_search_end_[static_cast<size_t>(width)] =
            static_cast<size_t>(end - maxima_.edges.begin());
    }
}

void StreamingSummary::add(const CollatzResult& result) {
    add(result, result.first_digit_counts);
}

void StreamingSummary::add(const ChainMetrics& result, std::span<const StepCount> digits) {
    if (digits.size() != digits_.size()) throw std::invalid_argument("Invalid summary digit width");
    add_metrics(result);
    for (size_t i = 0; i < digits_.size(); ++i) digits_[i] += digits[i];
}

void StreamingSummary::add_metrics(const ChainMetrics& result) {
    if (result.length > kMaxChainSteps || result.odd_steps + result.even_steps != result.length ||
        result.max_value == 0) {
        throw std::invalid_argument("Invalid result for summary");
    }
    const auto increment = [](std::vector<uint64_t>& frequency, size_t value) {
        if (frequency.size() <= value) frequency.resize(value + 1, 0);
        ++frequency[value];
    };
    increment(lengths_, result.length);
    increment(odd_, result.odd_steps);
    increment(even_, result.even_steps);
    ++maxima_.counts[max_bin(result.max_value)];
    ++count_;
}

size_t StreamingSummary::max_bin(uint64_t value) const {
    const auto width = static_cast<size_t>(std::bit_width(value));
    const auto begin =
        maxima_.edges.begin() + static_cast<std::ptrdiff_t>(max_search_begin_[width]);
    const auto end = maxima_.edges.begin() + static_cast<std::ptrdiff_t>(max_search_end_[width]);
    const auto found = std::upper_bound(begin, end, static_cast<double>(value));
    const auto position = static_cast<size_t>(found - maxima_.edges.begin());
    return std::min(position == 0 ? 0 : position - 1, maxima_.counts.size() - 1);
}

void StreamingSummary::add_doubling_family(const ChainMetrics& base, unsigned doublings) {
    if (base.n % 2 == 0 || doublings >= 64 || base.n > (UINT64_MAX >> doublings) ||
        base.max_value == 0 || base.odd_steps + base.even_steps != base.length)
        throw std::invalid_argument("Invalid doubling family metrics");
    if (base.length + doublings > kMaxChainSteps)
        throw std::runtime_error("Trajectory exceeds the 60000-step resource limit");
    const auto ensure = [](std::vector<uint64_t>& frequency, size_t last) {
        if (frequency.size() <= last) frequency.resize(last + 1, 0);
    };
    ensure(lengths_, base.length + doublings);
    ensure(even_, base.even_steps + doublings);
    ensure(odd_, base.odd_steps);
    const uint64_t weight = static_cast<uint64_t>(doublings) + 1;
    odd_[base.odd_steps] += weight;
    for (unsigned i = 0; i <= doublings; ++i) {
        ++lengths_[base.length + i];
        ++even_[base.even_steps + i];
    }
    if (base.max_value >= (base.n << doublings)) {
        maxima_.counts[max_bin(base.max_value)] += weight;
    } else {
        for (unsigned i = 0; i <= doublings; ++i)
            ++maxima_.counts[max_bin(std::max(base.max_value, base.n << i))];
    }
    count_ += weight;
}

void StreamingSummary::add_digit_totals(std::span<const uint64_t> totals) {
    if (totals.size() != digits_.size()) throw std::invalid_argument("Invalid summary digit width");
    for (size_t i = 0; i < digits_.size(); ++i)
        if (totals[i] > UINT64_MAX - digits_[i])
            throw std::overflow_error("Summary digit total exceeds uint64_t");
    for (size_t i = 0; i < digits_.size(); ++i) digits_[i] += totals[i];
}

void StreamingSummary::merge(const StreamingSummary& other) {
    if (bins_ != other.bins_ || bases_ != other.bases_)
        throw std::invalid_argument("Incompatible summaries");
    const auto sum = [](std::vector<uint64_t>& into, const std::vector<uint64_t>& from) {
        if (into.size() < from.size()) into.resize(from.size(), 0);
        for (size_t i = 0; i < from.size(); ++i) into[i] += from[i];
    };
    sum(lengths_, other.lengths_);
    sum(odd_, other.odd_);
    sum(even_, other.even_);
    sum(digits_, other.digits_);
    sum(maxima_.counts, other.maxima_.counts);
    count_ += other.count_;
}

Histogram StreamingSummary::rebin(const std::vector<uint64_t>& frequency) const {
    if (count_ == 0) return {};
    const auto first =
        std::find_if(frequency.begin(), frequency.end(), [](uint64_t x) { return x != 0; });
    const auto last =
        std::find_if(frequency.rbegin(), frequency.rend(), [](uint64_t x) { return x != 0; });
    const auto lo = static_cast<uint64_t>(first - frequency.begin());
    const auto hi = static_cast<uint64_t>(frequency.rend() - last - 1);
    auto hist = build_linear_histogram({lo, hi}, bins_);
    std::fill(hist.counts.begin(), hist.counts.end(), 0);
    for (uint64_t value = lo; value <= hi; ++value) {
        hist.counts[locate_bin(hist, static_cast<double>(value))] +=
            frequency[static_cast<size_t>(value)];
    }
    return hist;
}

Histogram StreamingSummary::length_histogram() const {
    return rebin(lengths_);
}
Histogram StreamingSummary::odd_histogram() const {
    return rebin(odd_);
}
Histogram StreamingSummary::even_histogram() const {
    return rebin(even_);
}
}  // namespace collatz::summary
