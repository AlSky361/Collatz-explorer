// Histogram and digit aggregation checks. Run with CTest.

#include "collatz/summary.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "collatz/collatz_core.hpp"

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const std::string& description) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::cerr << "FAIL: " << description << '\n';
    }
}

template <typename T>
void check_eq(const T& actual, const T& expected, const std::string& description) {
    ++g_checks;
    if (actual != expected) {
        ++g_failures;
        std::cerr << "FAIL: " << description << " (actual=" << actual << ", expected=" << expected
                  << ")\n";
    }
}

void check_near(double actual, double expected, double tol, const std::string& description) {
    ++g_checks;
    if (std::abs(actual - expected) > tol) {
        ++g_failures;
        std::cerr << "FAIL: " << description << " (actual=" << actual << ", expected=" << expected
                  << ", tol=" << tol << ")\n";
    }
}

// --- build_linear_histogram ------------------------------------------------

void test_linear_histogram_basic_counts() {
    // Для значений 0..9 проверяем пять равномерных бинов с двумя значениями в каждом.
    const std::vector<uint64_t> values = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    const collatz::summary::Histogram hist = collatz::summary::build_linear_histogram(values, 5);

    check_eq<size_t>(hist.edges.size(), 6, "linear hist: edges.size() == bins+1");
    check_eq<size_t>(hist.counts.size(), 5, "linear hist: counts.size() == bins");
    check(!hist.log_space, "linear hist: log_space == false");

    check_near(hist.edges.front(), 0.0, 1e-9, "linear hist: first edge == min");
    check_near(hist.edges.back(), 9.0, 1e-9, "linear hist: last edge == max");

    const std::vector<uint64_t> expected_counts = {2, 2, 2, 2, 2};
    for (size_t i = 0; i < expected_counts.size(); ++i) {
        check_eq(hist.counts[i], expected_counts[i],
                 "linear hist: bin " + std::to_string(i) + " count");
    }

    uint64_t total = 0;
    for (const uint64_t c : hist.counts) {
        total += c;
    }
    check_eq<uint64_t>(total, values.size(),
                       "linear hist: sum(counts) == number of input values "
                       "(no value dropped)");
}

void test_linear_histogram_degenerate_single_value() {
    // Все значения совпадают (n_max=1 сценарий) — не должно быть деления
    // на 0 / NaN, и всё должно попасть в один (любой) бин без потерь.
    const std::vector<uint64_t> values = {42, 42, 42};
    const collatz::summary::Histogram hist = collatz::summary::build_linear_histogram(values, 5);

    check_eq<size_t>(hist.counts.size(), 5, "degenerate hist: still 5 bins");
    uint64_t total = 0;
    bool any_nan = false;
    for (const double e : hist.edges) {
        if (std::isnan(e) || std::isinf(e)) {
            any_nan = true;
        }
    }
    for (const uint64_t c : hist.counts) {
        total += c;
    }
    check(!any_nan, "degenerate hist: no NaN/inf edges");
    check_eq<uint64_t>(total, 3, "degenerate hist: all 3 values counted");
}

void test_linear_histogram_empty_input() {
    const collatz::summary::Histogram hist = collatz::summary::build_linear_histogram({}, 5);
    check(hist.edges.empty(), "empty-input hist: no edges");
    check(hist.counts.empty(), "empty-input hist: no counts");
}

void test_linear_histogram_zero_bins() {
    const std::vector<uint64_t> values = {1, 2, 3};
    const collatz::summary::Histogram hist = collatz::summary::build_linear_histogram(values, 0);
    check(hist.edges.empty(), "zero-bins hist: no edges");
    check(hist.counts.empty(), "zero-bins hist: no counts");
}

// --- build_log_histogram ----------------------------------------------------

void test_log_histogram_basic_counts_and_bounds() {
    // Проверяем четыре log-бина на значениях 1, 10, 100, 1000, 10000.
    const std::vector<uint64_t> values = {1, 10, 100, 1000, 10000};
    const collatz::summary::Histogram hist = collatz::summary::build_log_histogram(values, 4);

    check(hist.log_space, "log hist: log_space == true");
    check_eq<size_t>(hist.edges.size(), 5, "log hist: edges.size() == bins+1");

    check_near(hist.edges.front(), 1.0, 1e-6, "log hist: first edge == min");
    check_near(hist.edges.back(), 10000.0, 1e-6, "log hist: last edge == max");

    // Обратное преобразование log-сетки должно сохранять строгий рост границ.
    bool strictly_increasing = true;
    for (size_t i = 1; i < hist.edges.size(); ++i) {
        if (hist.edges[i] <= hist.edges[i - 1]) {
            strictly_increasing = false;
            break;
        }
    }
    check(strictly_increasing, "log hist: edges strictly increasing");

    uint64_t total = 0;
    for (const uint64_t c : hist.counts) {
        total += c;
    }
    check_eq<uint64_t>(total, values.size(), "log hist: sum(counts) == number of input values");
}

void test_log_histogram_excludes_non_positive_values() {
    // Нули исключаются из log-бинов и общего числа учтённых значений.
    const std::vector<uint64_t> values = {0, 1, 10, 100};
    const collatz::summary::Histogram hist = collatz::summary::build_log_histogram(values, 3);

    uint64_t total = 0;
    for (const uint64_t c : hist.counts) {
        total += c;
    }
    check_eq<uint64_t>(total, 3,
                       "log hist: zero value excluded, only 3 positive "
                       "values counted");
}

void test_log_histogram_empty_input() {
    const collatz::summary::Histogram hist = collatz::summary::build_log_histogram({}, 5);
    check(hist.edges.empty(), "empty-input log hist: no edges");
    check(hist.counts.empty(), "empty-input log hist: no counts");
}

void test_streaming_maxima_boundaries() {
    const auto layout = collatz::make_digit_layout({2});
    for (const int bins : {1, 3, 64, 100, 10000}) {
        collatz::summary::StreamingSummary summary(layout, bins);
        const auto& edges = summary.max_histogram().edges;
        std::vector<uint64_t> expected(static_cast<size_t>(bins), 0);
        const std::vector<collatz::StepCount> digits{0, 1};
        const auto add = [&](uint64_t value) {
            if (value == 0) return;
            collatz::ChainMetrics row;
            row.n = 1;
            row.max_value = value;  // Synthetic maxima isolate bin assignment.
            summary.add(row, digits);
            const auto found =
                std::upper_bound(edges.begin(), edges.end(), static_cast<double>(value));
            const auto position = static_cast<size_t>(found - edges.begin());
            ++expected[std::min(position == 0 ? 0 : position - 1, expected.size() - 1)];
        };
        for (unsigned bit = 0; bit < 64; ++bit) {
            const uint64_t power = uint64_t{1} << bit;
            add(power - 1);
            add(power);
            add(power + 1);
        }
        for (const double edge : edges) {
            if (edge < 1 || edge >= std::ldexp(1.0, 64)) continue;
            const auto value = static_cast<uint64_t>(edge);
            add(value - 1);
            add(value);
            add(value + 1);
        }
        std::mt19937_64 random(42);
        for (int i = 0; i < 2048; ++i) add(random());
        add(UINT64_MAX);
        check(summary.max_histogram().counts == expected,
              "streaming maxima match full-range search, bins=" + std::to_string(bins));
    }
}

// --- sum_first_digit_counts -------------------------------------------------

void test_sum_first_digit_counts_matches_manual_sum() {
    const collatz::DigitLayout layout = collatz::make_digit_layout({2, 10});
    // layout.total_size == 2 + 10 == 12.

    std::vector<collatz::CollatzResult> results(3);
    for (collatz::CollatzResult& r : results) {
        r.first_digit_counts.assign(layout.total_size, 0);
    }
    // Синтетические счётчики — не настоящий выхлоп collatz_chain, здесь
    // важна только правильность суммирования, не смысл чисел.
    results[0].first_digit_counts = {1, 2, 0, 1, 0, 0, 0, 0, 0, 0, 0, 3};
    results[1].first_digit_counts = {0, 5, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1};
    results[2].first_digit_counts = {2, 0, 0, 0, 0, 2, 2, 0, 0, 0, 0, 0};

    const std::vector<uint64_t> totals = collatz::summary::sum_first_digit_counts(results, layout);

    check_eq<size_t>(totals.size(), layout.total_size,
                     "digit sum: result size == layout.total_size");

    const std::vector<uint64_t> expected = {3, 7, 1, 2, 1, 2, 2, 0, 0, 0, 0, 4};
    for (size_t i = 0; i < expected.size(); ++i) {
        check_eq(totals[i], expected[i], "digit sum: slot " + std::to_string(i));
    }
}

void test_sum_first_digit_counts_empty_results() {
    const collatz::DigitLayout layout = collatz::make_digit_layout({10});
    const std::vector<uint64_t> totals = collatz::summary::sum_first_digit_counts({}, layout);
    check_eq<size_t>(totals.size(), layout.total_size,
                     "digit sum (empty results): size still == "
                     "layout.total_size, all zero");
    bool all_zero = true;
    for (const uint64_t v : totals) {
        if (v != 0) {
            all_zero = false;
        }
    }
    check(all_zero, "digit sum (empty results): all totals are 0");
}

// Сквозная проверка суммы first_digit_counts из collatz_range_parallel(kN).
// Суммы для digit==0 должны оставаться нулевыми во всех основаниях.
void test_sum_first_digit_counts_zero_digit_always_empty() {
    const collatz::DigitLayout layout = collatz::make_digit_layout({2, 8, 10, 16});
    constexpr uint64_t kN = 2000;
    const std::vector<collatz::CollatzResult> results =
        collatz::collatz_range_parallel(kN, layout, nullptr);

    const std::vector<uint64_t> totals = collatz::summary::sum_first_digit_counts(results, layout);

    bool all_zero_digit_slots_empty = true;
    for (size_t b = 0; b < layout.bases.size(); ++b) {
        const size_t offset = layout.offsets[b];
        if (totals[offset] != 0) {  // digit == 0 слот для этого base
            all_zero_digit_slots_empty = false;
        }
    }
    check(all_zero_digit_slots_empty,
          "digit sum (real data): digit-0 slot is 0 for every base "
          "(first digit is never 0)");
}

}  // namespace

int main() {
    test_linear_histogram_basic_counts();
    test_linear_histogram_degenerate_single_value();
    test_linear_histogram_empty_input();
    test_linear_histogram_zero_bins();
    test_log_histogram_basic_counts_and_bounds();
    test_log_histogram_excludes_non_positive_values();
    test_log_histogram_empty_input();
    test_streaming_maxima_boundaries();
    test_sum_first_digit_counts_matches_manual_sum();
    test_sum_first_digit_counts_empty_results();
    test_sum_first_digit_counts_zero_digit_always_empty();

    std::cout << g_checks << " checks, " << g_failures << " failures\n";
    if (g_failures > 0) {
        std::cerr << "FAILED\n";
        return 1;
    }
    std::cout << "OK\n";
    return 0;
}
