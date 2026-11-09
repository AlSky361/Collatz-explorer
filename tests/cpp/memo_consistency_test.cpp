// Compare all metrics with and without memoization for n in [1, 20000].

#include <atomic>
#include <cstdint>
#include <iostream>
#include <latch>
#include <thread>
#include <vector>

#include "collatz/collatz_core.hpp"

namespace {

constexpr uint64_t kNMax = 20000;

// Проверяет, что все метрики двух CollatzResult совпадают точно.
[[nodiscard]] bool results_equal(const collatz::CollatzResult& a, const collatz::CollatzResult& b) {
    if (a.n != b.n || a.length != b.length || a.max_value != b.max_value ||
        a.odd_steps != b.odd_steps || a.even_steps != b.even_steps) {
        return false;
    }
    if (a.first_digit_counts.size() != b.first_digit_counts.size()) {
        return false;
    }
    for (size_t i = 0; i < a.first_digit_counts.size(); ++i) {
        if (a.first_digit_counts[i] != b.first_digit_counts[i]) {
            return false;
        }
    }
    return true;
}

}  // namespace

bool check_frozen_cache(const std::vector<int>& bases, uint64_t capacity) {
    const auto layout = collatz::make_digit_layout(bases);
    collatz::ChainMemo memo(capacity, layout);
    collatz::prepare_memo(memo, layout);
    collatz::prepare_memo(memo, layout);  // Repeated preparation must preserve the cache.
    collatz::CachedEntry entry;
    if (memo.try_get(2, entry)) return false;
    for (uint64_t n = 1; n <= 2000; ++n) {
        if (!results_equal(collatz::collatz_chain(n, layout),
                           collatz::collatz_chain(n, layout, &memo)))
            return false;
        const bool expected_hit = n <= capacity && n % 2 != 0;
        if (memo.try_get(n, entry) != expected_hit) return false;
        if (expected_hit) {
            auto expected = collatz::collatz_chain(n, layout);
            if (entry.length != expected.length || entry.max_value != expected.max_value ||
                entry.odd_steps != expected.odd_steps || entry.even_steps != expected.even_steps ||
                entry.first_digit_counts != expected.first_digit_counts)
                return false;
        }
    }
    return memo.is_frozen();
}

bool check_concurrent_preparation() {
    const auto layout = collatz::make_digit_layout({2, 8, 10, 16});
    collatz::ChainMemo memo(4095, layout);
    std::latch start(1);
    std::atomic<bool> valid{true};
    {
        const auto prepare = [&] {
            start.wait();
            collatz::prepare_memo(memo, layout);
        };
        std::jthread first(prepare);
        std::jthread second(prepare);
        std::jthread reader([&] {
            start.wait();
            for (uint64_t n = 1; n < 100; n += 2) {
                collatz::CachedEntry entry;
                if (!memo.try_get(n, entry)) continue;
                const auto expected = collatz::collatz_chain(n, layout);
                if (entry.length != expected.length || entry.max_value != expected.max_value ||
                    entry.first_digit_counts != expected.first_digit_counts)
                    valid.store(false, std::memory_order_relaxed);
            }
        });
        start.count_down();
    }
    return valid.load(std::memory_order_relaxed) && memo.is_frozen();
}

int main() {
    if (!check_concurrent_preparation()) return 1;
    for (const uint64_t capacity : {1ull, 2ull, 3ull, 97ull, 98ull}) {
        if (!check_frozen_cache({2, 8, 10, 16}, capacity) ||
            !check_frozen_cache({16, 10, 8, 2}, capacity) || !check_frozen_cache({2}, capacity) ||
            !check_frozen_cache({3, 7, 36}, capacity)) {
            std::cerr << "Frozen odd cache mismatch, capacity=" << capacity << '\n';
            return 1;
        }
    }
    const std::vector<int> bases = {2, 8, 10, 16};
    const collatz::DigitLayout layout = collatz::make_digit_layout(bases);

    // Эталон без memo не использует try_get и служит независимой проверкой кэширования.
    std::vector<collatz::CollatzResult> baseline;
    baseline.reserve(static_cast<size_t>(kNMax));
    for (uint64_t n = 1; n <= kNMax; ++n) {
        baseline.push_back(collatz::collatz_chain(n, layout, nullptr));
    }

    // Восходящий проход заполняет кэш и проверяет как промахи, так и попадания.
    collatz::ChainMemo memo(kNMax, layout);
    size_t mismatches = 0;
    for (uint64_t n = 1; n <= kNMax; ++n) {
        const collatz::CollatzResult with_memo = collatz::collatz_chain(n, layout, &memo);
        const collatz::CollatzResult& expected = baseline[static_cast<size_t>(n - 1)];

        if (!results_equal(with_memo, expected)) {
            ++mismatches;
            if (mismatches <= 10) {
                std::cerr << "MISMATCH at n=" << n << ": length(memo)=" << with_memo.length
                          << " length(baseline)=" << expected.length
                          << " max_value(memo)=" << with_memo.max_value
                          << " max_value(baseline)=" << expected.max_value << '\n';
                for (size_t i = 0; i < layout.total_size; ++i) {
                    if (with_memo.first_digit_counts[i] != expected.first_digit_counts[i]) {
                        std::cerr << "  digit slot " << i
                                  << ": memo=" << with_memo.first_digit_counts[i]
                                  << " baseline=" << expected.first_digit_counts[i] << '\n';
                    }
                }
            }
        }
    }

    if (mismatches > 0) {
        std::cerr << "FAILED: " << mismatches << " / " << kNMax
                  << " mismatches between memo and baseline results.\n";
        return 1;
    }

    std::cout << "OK: memo and baseline agree for all n in [1, " << kNMax << "].\n";
    return 0;
}
