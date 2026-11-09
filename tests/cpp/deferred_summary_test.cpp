#include <algorithm>
#include <iostream>
#include <stdexcept>

#include "collatz/summary.hpp"
#include "internal/frozen_memo.hpp"

namespace {
bool same_metrics(const collatz::ChainMetrics& a, const collatz::ChainMetrics& b) {
    return a.n == b.n && a.length == b.length && a.odd_steps == b.odd_steps &&
           a.even_steps == b.even_steps && a.max_value == b.max_value;
}

bool compare(const std::vector<int>& bases, uint64_t capacity) {
    const auto layout = collatz::make_digit_layout(bases);
    collatz::ChainMemo memo(capacity, layout);
    collatz::prepare_memo(memo, layout);
    const collatz::detail::FrozenMemoView view(memo);
    std::vector<uint64_t> totals(layout.total_size), expected(layout.total_size);
    std::vector<uint64_t> weights(static_cast<size_t>(collatz::memo_entry_count(capacity)));
    const auto check = [&](uint64_t n) {
        const auto actual = collatz::detail::summarize_prefix(n, layout, totals, view);
        const auto reference = collatz::collatz_chain(n, layout);
        if (!same_metrics(actual.metrics, reference)) return false;
        if (actual.tail != 0) ++weights[static_cast<size_t>(actual.tail / 2)];
        for (size_t i = 0; i < expected.size(); ++i) expected[i] += reference.first_digit_counts[i];
        return true;
    };
    for (uint64_t n = 1; n <= 2000; ++n)
        if (!check(n)) return false;
    for (unsigned bit = 1; bit < 64; ++bit)
        if (!check(uint64_t{1} << bit)) return false;
    view.add_nonbinary_tail_digits(weights, totals);
    return totals == expected;
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

bool boundaries() {
    const auto layout = collatz::make_digit_layout({2, 10});
    collatz::ChainMemo memo(5, layout);
    if (!rejects<std::logic_error>([&] { collatz::detail::FrozenMemoView view(memo); }))
        return false;
    memo.freeze();  // An incompletely filled frozen cache is also valid.
    const collatz::detail::FrozenMemoView view(memo);
    collatz::ChainMetrics metrics;
    if (view.try_metrics(0, metrics) || view.try_metrics(2, metrics) ||
        view.try_metrics(7, metrics) || view.try_metrics(3, metrics) ||
        !view.try_metrics(1, metrics))
        return false;
    std::vector<uint64_t> totals(layout.total_size), weights(3);
    const auto prefix = collatz::detail::summarize_prefix(3, layout, totals, view);
    if (prefix.tail != 1 || prefix.metrics.length != 7) return false;
    weights[1] = 1;
    if (!rejects<std::logic_error>([&] { view.add_nonbinary_tail_digits(weights, totals); }))
        return false;
    weights = {UINT64_MAX, 0, 0};
    totals[layout.offsets[1] + 1] = 1;
    if (!rejects<std::overflow_error>([&] { view.add_nonbinary_tail_digits(weights, totals); }))
        return false;
    if (!rejects<std::invalid_argument>([&] { view.add_nonbinary_tail_digits({}, totals); }))
        return false;
    if (!rejects<std::overflow_error>(
            [&] { (void)collatz::detail::summarize_prefix(UINT64_MAX, layout, totals, view); }))
        return false;
    return rejects<std::invalid_argument>(
        [&] { (void)collatz::detail::summarize_prefix(0, layout, totals, view); });
}

bool step_limit() {
    const auto layout = collatz::make_digit_layout({10});
    collatz::ChainMemo memo(3, layout);
    std::vector<collatz::StepCount> digits(layout.total_size);
    memo.set(3, collatz::kMaxChainSteps, 1,
             static_cast<collatz::StepCount>(collatz::kMaxChainSteps - 1), 16, digits);
    memo.freeze();
    const collatz::detail::FrozenMemoView view(memo);
    std::vector<uint64_t> totals(layout.total_size);
    return rejects<std::runtime_error>(
        [&] { (void)collatz::detail::summarize_prefix(6, layout, totals, view); });
}

bool totals_overflow() {
    const auto layout = collatz::make_digit_layout({10});
    collatz::summary::StreamingSummary summary(layout, 7);
    std::vector<uint64_t> totals(layout.total_size);
    totals[1] = UINT64_MAX;
    summary.add_digit_totals(totals);
    totals[0] = totals[1] = 1;
    if (!rejects<std::overflow_error>([&] { summary.add_digit_totals(totals); })) return false;
    return summary.count() == 0 && summary.digit_totals()[0] == 0 &&
           summary.digit_totals()[1] == UINT64_MAX;
}
}  // namespace

int main() {
    for (const uint64_t capacity : {1ull, 2ull, 47ull, 48ull, 5000ull})
        for (const auto& bases :
             std::vector<std::vector<int>>{{2, 8, 10, 16}, {16, 10, 8, 2}, {3, 7, 36}, {2}})
            if (!compare(bases, capacity)) return 1;
    if (!boundaries() || !step_limit() || !totals_overflow()) return 2;
    const auto layout = collatz::make_digit_layout({10});
    if (collatz::detail::deferred_tail_slots(layout, 47, 95, 4) != 0 ||
        collatz::detail::deferred_tail_slots(layout, 47, 96, 4) != 24 ||
        collatz::detail::deferred_tail_slots(layout, 47, 96, 0) != 0 ||
        collatz::detail::deferred_tail_slots(collatz::make_digit_layout({2}), 47, 96, 4) != 0)
        return 3;
    std::cout << "Deferred tails match full trajectories; bounds and overflow checks passed\n";
}
