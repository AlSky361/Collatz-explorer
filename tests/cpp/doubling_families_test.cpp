#include <algorithm>
#include <bit>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

#include "collatz/range_executor.hpp"
#include "internal/frozen_memo.hpp"

namespace {
bool compare_family(uint64_t odd, unsigned doublings, const std::vector<int>& bases) {
    const auto layout = collatz::make_digit_layout(bases);
    collatz::ChainMemo memo(47, layout);
    collatz::prepare_memo(memo, layout);
    const collatz::detail::FrozenMemoView view(memo);
    std::vector<uint64_t> totals(layout.total_size), expected(layout.total_size), weights(24);
    const uint64_t weight = static_cast<uint64_t>(doublings) + 1;
    const auto base = collatz::detail::summarize_prefix(odd, layout, totals, view, weight);
    collatz::summary::StreamingSummary grouped(layout, 17), serial(layout, 17);
    grouped.add_doubling_family(base.metrics, doublings);
    collatz::detail::record_doubling_prefixes(odd, doublings, layout, totals);
    if (base.tail != 0) weights[static_cast<size_t>(base.tail / 2)] += weight;
    view.add_nonbinary_tail_digits(weights, totals);
    for (unsigned i = 0; i <= doublings; ++i) {
        const auto row = collatz::collatz_chain(odd << i, layout);
        serial.add(row);
        if (row.length != base.metrics.length + i || row.odd_steps != base.metrics.odd_steps ||
            row.even_steps != base.metrics.even_steps + i ||
            row.max_value != std::max(base.metrics.max_value, odd << i))
            return false;
        for (size_t j = 0; j < totals.size(); ++j) expected[j] += row.first_digit_counts[j];
    }
    return totals == expected && grouped.count() == serial.count() &&
           grouped.length_counts() == serial.length_counts() &&
           grouped.odd_histogram().counts == serial.odd_histogram().counts &&
           grouped.even_histogram().counts == serial.even_histogram().counts &&
           grouped.max_histogram().counts == serial.max_histogram().counts;
}

bool compare_ranges(unsigned threads) {
    const auto layout = collatz::make_digit_layout({2, 3, 10, 16, 36});
    collatz::ChainMemo memo(1, layout);
    collatz::prepare_memo(memo, layout);
    collatz::RangeExecutor executor(layout, &memo, threads);
    for (const uint64_t count : {1ull, 2ull, 3ull, 7ull, 8ull, 9ull, 31ull, 32ull, 33ull, 63ull,
                                 64ull, 65ull, 255ull, 256ull, 257ull, 4095ull, 4096ull, 4097ull}) {
        collatz::summary::StreamingSummary actual(layout, 17), expected(layout, 17);
        executor.summarize(1, count, actual);
        for (uint64_t n = 1; n <= count; ++n) expected.add(collatz::collatz_chain(n, layout));
        if (actual.count() != count || actual.digit_totals() != expected.digit_totals() ||
            actual.length_counts() != expected.length_counts() ||
            actual.odd_histogram().counts != expected.odd_histogram().counts ||
            actual.even_histogram().counts != expected.even_histogram().counts ||
            actual.max_histogram().counts != expected.max_histogram().counts)
            return false;
    }
    return true;
}

template <typename Error, typename Action>
bool rejects(Action action) {
    try {
        action();
    } catch (const Error&) {
        return true;
    }
    return false;
}

bool check_invalid() {
    const auto layout = collatz::make_digit_layout({2, 10});
    std::vector<uint64_t> totals(layout.total_size);
    for (const auto& [odd, doublings] : std::vector<std::pair<uint64_t, unsigned>>{
             {0, 0}, {2, 1}, {1, 64}, {3, 63}, {UINT64_MAX, 1}})
        if (!rejects<std::invalid_argument>(
                [&] { collatz::detail::record_doubling_prefixes(odd, doublings, layout, totals); }))
            return false;
    if (!rejects<std::invalid_argument>(
            [&] { collatz::detail::record_doubling_prefixes(1, 1, layout, {}); }))
        return false;
    collatz::ChainMemo memo(1, layout);
    memo.freeze();
    const collatz::detail::FrozenMemoView view(memo);
    for (const uint64_t weight : {0ull, 65ull})
        if (!rejects<std::invalid_argument>(
                [&] { (void)collatz::detail::summarize_prefix(3, layout, totals, view, weight); }))
            return false;
    collatz::summary::StreamingSummary summary(layout, 17);
    const auto row = collatz::collatz_chain(3, layout);
    if (!rejects<std::invalid_argument>([&] { summary.add_doubling_family(row, 63); }) ||
        !rejects<std::invalid_argument>([&] { summary.add_doubling_family(row, 64); }) ||
        !rejects<std::invalid_argument>(
            [&] { summary.add_doubling_family(collatz::collatz_chain(2, layout), 1); }))
        return false;
    return true;
}

bool check_step_limit(collatz::StepCount length) {
    const auto layout = collatz::make_digit_layout({10});
    collatz::ChainMemo memo(3, layout);
    std::vector<collatz::StepCount> digits(layout.total_size);
    memo.set(3, length, 1, static_cast<collatz::StepCount>(length - 1), 16, digits);
    memo.freeze();
    collatz::RangeExecutor executor(layout, &memo, 1);
    collatz::summary::StreamingSummary summary(layout, 17);
    if (length == collatz::kMaxChainSteps)
        return rejects<std::runtime_error>([&] { executor.summarize(1, 6, summary); }) &&
               summary.count() == 0;
    executor.summarize(1, 6, summary);
    return summary.count() == 6 && summary.length_counts()[collatz::kMaxChainSteps] == 1;
}
}  // namespace

int main() {
    for (const auto& bases :
         std::vector<std::vector<int>>{{2, 8, 10, 16}, {16, 10, 8, 2}, {3, 7, 36}, {2}})
        for (const uint64_t odd : {1ull, 3ull, 5ull, 7ull, 27ull, 97ull, 703ull})
            for (const unsigned doublings :
                 {0u, 1u, 2u, 5u, 12u, static_cast<unsigned>(std::countl_zero(odd))})
                if (!compare_family(odd, doublings, bases)) return 1;
    if (!compare_ranges(1) || !compare_ranges(4) || !check_invalid() ||
        !check_step_limit(collatz::kMaxChainSteps) ||
        !check_step_limit(static_cast<collatz::StepCount>(collatz::kMaxChainSteps - 1)))
        return 2;
    std::cout << "Doubling families match full trajectories, boundaries and resource limits\n";
}
