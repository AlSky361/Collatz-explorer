// Independent reference trajectories, full-value digit sampling and range checks.

#include "collatz/collatz_core.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

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

// --- make_digit_layout ---------------------------------------------------

void test_digit_layout_offsets_and_total_size() {
    const collatz::DigitLayout layout = collatz::make_digit_layout({2, 8, 10, 16});

    check_eq<size_t>(layout.offsets.size(), 4, "layout: 4 offsets for 4 bases");
    check_eq<size_t>(layout.offsets[0], 0, "layout: base 2 offset");
    check_eq<size_t>(layout.offsets[1], 2, "layout: base 8 offset (после base 2)");
    check_eq<size_t>(layout.offsets[2], 10, "layout: base 10 offset (после base 2+8)");
    check_eq<size_t>(layout.offsets[3], 20, "layout: base 16 offset (после base 2+8+10)");
    check_eq<size_t>(layout.total_size, 36, "layout: total_size = 2+8+10+16");
}

void test_digit_layout_single_base() {
    const collatz::DigitLayout layout = collatz::make_digit_layout({10});
    check_eq<size_t>(layout.offsets.size(), 1, "single-base layout: 1 offset");
    check_eq<size_t>(layout.offsets[0], 0, "single-base layout: offset 0");
    check_eq<size_t>(layout.total_size, 10, "single-base layout: total_size");
}

// collatz_chain references include every value, including intermediate 3n+1 values.

struct Reference {
    uint64_t n;
    collatz::StepCount length;
    uint64_t max_value;
    collatz::StepCount odd_steps;
    collatz::StepCount even_steps;
    std::vector<collatz::StepCount> digits;
};

const std::vector<Reference>& reference_table() {
    static const std::vector<Reference> table = {
        {1, 0, 1, 0, 0, {0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
                         0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
        {2, 1, 2, 0, 1, {0, 2, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0,
                         0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
        {3, 7, 16, 2, 5, {0, 8, 0, 3, 2, 1, 1, 1, 0, 0, 0, 3, 1, 1, 1, 1, 0, 0,
                          1, 0, 0, 2, 1, 1, 1, 1, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0}},
        {4, 2, 4, 0, 2, {0, 3, 0, 1, 1, 0, 1, 0, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0,
                         0, 0, 0, 1, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
        {5, 5, 16, 1, 4, {0, 6, 0, 2, 2, 0, 1, 1, 0, 0, 0, 2, 1, 0, 1, 1, 0, 0,
                          1, 0, 0, 2, 1, 0, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0}},
        {6, 8, 16, 2, 6, {0, 9, 0, 3, 2, 1, 1, 1, 1, 0, 0, 3, 1, 1, 1, 1, 1, 0,
                          1, 0, 0, 2, 1, 1, 1, 1, 1, 0, 1, 0, 1, 0, 0, 0, 0, 0}},
        {7, 16, 52, 5, 11, {0, 17, 0, 5, 5, 1, 2, 2, 1, 1, 0, 6, 4, 1, 2, 2, 0, 1,
                            1, 0,  0, 6, 3, 1, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 0, 0}},
        {8, 3, 8, 0, 3, {0, 4, 0, 2, 1, 0, 1, 0, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0,
                         1, 0, 0, 1, 1, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0}},
        {9, 19, 52, 6, 13, {0, 20, 0, 7, 5, 2, 2, 2, 1, 1, 0, 7, 5, 1, 2, 2, 0, 1,
                            1, 1,  0, 7, 3, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 0}},
        {10, 6, 16, 1, 5, {0, 7, 0, 3, 2, 0, 1, 1, 0, 0, 0, 3, 1, 0, 1, 1, 0, 0,
                           1, 0, 0, 2, 1, 0, 1, 1, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0}},
        {27, 111, 9232, 41, 70, {0, 112, 0, 38, 23, 12, 11, 12, 8, 8, 0, 31, 18, 14, 16, 7, 6, 7,
                                 6, 7,   0, 32, 17, 10, 8,  10, 6, 4, 5, 3,  6,  2,  3,  1, 2, 3}},
    };
    return table;
}

void test_collatz_chain_reference_values_no_memo() {
    const collatz::DigitLayout layout = collatz::make_digit_layout({2, 8, 10, 16});

    for (const Reference& ref : reference_table()) {
        const collatz::CollatzResult result = collatz::collatz_chain(ref.n, layout, nullptr);

        const std::string tag = "n=" + std::to_string(ref.n);
        check_eq(result.n, ref.n, tag + ": n echoed back");
        check_eq(result.length, ref.length, tag + ": length");
        check_eq(result.max_value, ref.max_value, tag + ": max_value");
        check_eq(result.odd_steps, ref.odd_steps, tag + ": odd_steps");
        check_eq(result.even_steps, ref.even_steps, tag + ": even_steps");
        check_eq<size_t>(result.first_digit_counts.size(), ref.digits.size(),
                         tag + ": digits vector size");

        bool digits_match = (result.first_digit_counts.size() == ref.digits.size());
        if (digits_match) {
            for (size_t i = 0; i < ref.digits.size(); ++i) {
                if (result.first_digit_counts[i] != ref.digits[i]) {
                    digits_match = false;
                    break;
                }
            }
        }
        check(digits_match, tag + ": first_digit_counts matches reference");
    }
}

// Эталоны проверяют memo-путь и ошибки слияния try_get независимо от memo_consistency_test.
void test_collatz_chain_reference_values_with_memo() {
    const collatz::DigitLayout layout = collatz::make_digit_layout({2, 8, 10, 16});
    collatz::ChainMemo memo(1000, layout);

    // Прогреваем кэш проходом 1..1000 перед проверкой эталонных значений.
    for (uint64_t n = 1; n <= 1000; ++n) {
        const collatz::CollatzResult warmup = collatz::collatz_chain(n, layout, &memo);
        (void)warmup;  // прогрев кэша — сам результат здесь не нужен
    }

    for (const Reference& ref : reference_table()) {
        const collatz::CollatzResult result = collatz::collatz_chain(ref.n, layout, &memo);

        const std::string tag = "n=" + std::to_string(ref.n) + " (memo)";
        check_eq(result.length, ref.length, tag + ": length");
        check_eq(result.max_value, ref.max_value, tag + ": max_value");
        check_eq(result.odd_steps, ref.odd_steps, tag + ": odd_steps");
        check_eq(result.even_steps, ref.even_steps, tag + ": even_steps");

        bool digits_match = (result.first_digit_counts.size() == ref.digits.size());
        if (digits_match) {
            for (size_t i = 0; i < ref.digits.size(); ++i) {
                if (result.first_digit_counts[i] != ref.digits[i]) {
                    digits_match = false;
                    break;
                }
            }
        }
        check(digits_match, tag + ": first_digit_counts matches reference");
    }
}

// --- collatz_trajectory ---------------------------------------------------

void test_trajectory_boundaries() {
    {
        const std::vector<uint64_t> traj = collatz::collatz_trajectory(1);
        check_eq<size_t>(traj.size(), 1, "trajectory(1): single element");
        check_eq<uint64_t>(traj.front(), 1, "trajectory(1): first == 1");
        check_eq<uint64_t>(traj.back(), 1, "trajectory(1): last == 1");
    }
    {
        const std::vector<uint64_t> traj = collatz::collatz_trajectory(27);
        check_eq<uint64_t>(traj.front(), 27, "trajectory(27): first == start_n");
        check_eq<uint64_t>(traj.back(), 1, "trajectory(27): last == 1");
        // Both APIs count every ordinary Collatz operation.
        const collatz::DigitLayout layout = collatz::make_digit_layout({10});
        const collatz::CollatzResult result = collatz::collatz_chain(27, layout, nullptr);
        check_eq<uint64_t>(traj.size() - 1, result.length,
                           "trajectory(27): raw step count equals "
                           "chain length (both count real "
                           "operations, just grouped differently)");
    }
    {
        // Проверяем каждый переход независимой реализации collatz_trajectory: n/2 или 3n+1.
        const std::vector<uint64_t> traj = collatz::collatz_trajectory(9);
        bool all_steps_valid = true;
        for (size_t i = 1; i < traj.size(); ++i) {
            const bool is_half = (traj[i] * 2 == traj[i - 1]);
            const bool is_3np1 = (traj[i] == 3 * traj[i - 1] + 1);
            if (!is_half && !is_3np1) {
                all_steps_valid = false;
                break;
            }
        }
        check(all_steps_valid,
              "trajectory(9): every consecutive pair is a valid n/2 or "
              "3n+1 step");
    }
}

// --- collatz_range_parallel ------------------------------------------------

void test_range_parallel_matches_sequential() {
    const collatz::DigitLayout layout = collatz::make_digit_layout({2, 8, 10, 16});
    constexpr uint64_t kN = 5000;

    const std::vector<collatz::CollatzResult> parallel_results =
        collatz::collatz_range_parallel(kN, layout, nullptr);

    check_eq<size_t>(parallel_results.size(), static_cast<size_t>(kN),
                     "range_parallel: result count == n_max");

    bool all_match = true;
    for (uint64_t n = 1; n <= kN; ++n) {
        const collatz::CollatzResult sequential = collatz::collatz_chain(n, layout, nullptr);
        const collatz::CollatzResult& parallel = parallel_results[static_cast<size_t>(n - 1)];
        if (parallel.n != sequential.n || parallel.length != sequential.length ||
            parallel.max_value != sequential.max_value) {
            all_match = false;
            std::cerr << "  mismatch at n=" << n << '\n';
            break;
        }
    }
    check(all_match,
          "range_parallel: matches sequential collatz_chain for n in "
          "[1, 5000]");
}

void test_error_boundaries() {
    const auto layout = collatz::make_digit_layout({10});
    bool overflow = false;
    try {
        (void)collatz::collatz_batch_parallel(6148914691236517205ull, 6148914691236517210ull,
                                              layout, nullptr, 3);
    } catch (const std::overflow_error&) {
        overflow = true;
    }
    check(overflow, "worker overflow is rethrown by calling thread");

    collatz::ChainMemo memo(10, layout);
    bool mismatch = false;
    try {
        (void)collatz::collatz_chain(3, collatz::make_digit_layout({2, 8}), &memo);
    } catch (const std::invalid_argument&) {
        mismatch = true;
    }
    check(mismatch, "different layouts of equal width cannot share a cache");

    std::vector<collatz::StepCount> digits(layout.total_size, 0);
    memo.set(3, collatz::kMaxChainSteps, 1, collatz::kMaxChainSteps - 1, 16, digits);
    bool step_limit = false;
    try {
        (void)collatz::collatz_chain(6, layout, &memo);
    } catch (const std::runtime_error&) {
        step_limit = true;
    }
    check(step_limit, "step limit is enforced when extending a cached tail");

    bool dimensions = false;
    try {
        collatz::ChainMemo too_large(UINT64_MAX, layout);
    } catch (const std::length_error&) {
        dimensions = true;
    }
    check(dimensions, "cache dimensions are checked before allocating");
}

}  // namespace

int main() {
    test_error_boundaries();
    test_digit_layout_offsets_and_total_size();
    test_digit_layout_single_base();
    test_collatz_chain_reference_values_no_memo();
    test_collatz_chain_reference_values_with_memo();
    test_trajectory_boundaries();
    test_range_parallel_matches_sequential();

    std::cout << g_checks << " checks, " << g_failures << " failures\n";
    if (g_failures > 0) {
        std::cerr << "FAILED\n";
        return 1;
    }
    std::cout << "OK\n";
    return 0;
}
