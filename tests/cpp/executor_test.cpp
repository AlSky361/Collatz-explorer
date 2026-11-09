#include <algorithm>
#include <iostream>
#include <stdexcept>

#include "collatz/range_executor.hpp"

bool check_async(const collatz::DigitLayout& layout, collatz::ChainMemo& memo) {
    collatz::ResultBatch batches[2];
    collatz::RangeExecutor executor(layout, &memo, 4);
    executor.start_batch(1, 31, batches[0]);
    bool rejects_overlap = false;
    try {
        executor.start_batch(1, 1, batches[0]);  // Must reject before resizing an active buffer.
    } catch (const std::logic_error&) {
        rejects_overlap = true;
    }
    if (!rejects_overlap) return false;
    for (uint64_t first = 1, current = 0; first < 1000; first += 31, current = 1 - current) {
        executor.wait();
        executor.start_batch(first + 31, 31, batches[1 - current]);
        for (size_t i = 0; i < 31; ++i) {
            const auto expected = collatz::collatz_chain(first + i, layout);
            const auto& batch = batches[current];
            if (batch.metrics[i].n != expected.n || batch.metrics[i].length != expected.length ||
                !std::equal(batch.digits(i).begin(), batch.digits(i).end(),
                            expected.first_digit_counts.begin()))
                return false;
        }
    }
    executor.wait();
    try {
        executor.wait();
        return false;
    } catch (const std::logic_error&) {
    }
    executor.start_batch(6148914691236517205ull, 7, batches[0]);
    try {
        executor.wait();
        return false;
    } catch (const std::overflow_error&) {
    }
    executor.compute_batch(3, 1, batches[0]);
    if (batches[0].metrics[0].length != 7) return false;
    // Destruction with outstanding work also occurs when a caller's writer throws.
    executor.start_batch(1, 1000, batches[1]);
    return true;
}

int main() {
    const auto layout = collatz::make_digit_layout({2, 3, 10, 16, 36});
    collatz::ChainMemo memo(97, layout);
    collatz::prepare_memo(memo, layout);
    if (!check_async(layout, memo)) return 9;
    if (!memo.is_frozen()) return 1;
    bool rejects_write = false;
    const auto sample = collatz::collatz_chain(3, layout);
    try {
        memo.set(3, sample.length, sample.odd_steps, sample.even_steps, sample.max_value,
                 sample.first_digit_counts);
    } catch (const std::logic_error&) {
        rejects_write = true;
    }
    if (!rejects_write) return 2;
    collatz::RangeExecutor executor(layout, &memo, 4);
    collatz::ResultBatch batch;
    collatz::summary::StreamingSummary sequential(layout, 17);
    for (uint64_t first = 1; first <= 2000; first += 23) {
        const size_t count = static_cast<size_t>(std::min<uint64_t>(23, 2001 - first));
        executor.compute_batch(first, count, batch);
        for (size_t i = 0; i < count; ++i) {
            const auto expected = collatz::collatz_chain(first + i, layout);
            const auto& actual = batch.metrics[i];
            const auto digits = batch.digits(i);
            if (actual.n != expected.n || actual.length != expected.length ||
                actual.max_value != expected.max_value || actual.odd_steps != expected.odd_steps ||
                actual.even_steps != expected.even_steps ||
                !std::equal(digits.begin(), digits.end(), expected.first_digit_counts.begin()))
                return 3;
            sequential.add(expected);
        }
    }
    bool throws = false;
    try {
        executor.compute_batch(6148914691236517205ull, 7, batch);
    } catch (const std::overflow_error&) {
        throws = true;
    }
    if (!throws) return 4;
    executor.compute_batch(1, 1, batch);
    if (batch.metrics[0].length != 0 || batch.digits(0)[1] != 1) return 5;
    collatz::summary::StreamingSummary parallel(layout, 17);
    executor.summarize(1, 2000, parallel);
    executor.summarize(1, 2000, parallel);
    sequential.merge(sequential);
    if (parallel.count() != sequential.count() ||
        parallel.digit_totals() != sequential.digit_totals() ||
        parallel.length_counts() != sequential.length_counts() ||
        parallel.length_histogram().counts != sequential.length_histogram().counts ||
        parallel.odd_histogram().counts != sequential.odd_histogram().counts ||
        parallel.even_histogram().counts != sequential.even_histogram().counts ||
        parallel.max_histogram().counts != sequential.max_histogram().counts)
        return 6;
    collatz::summary::StreamingSummary recovered(layout, 17);
    bool summary_overflow = false;
    try {
        executor.summarize(6148914691236517205ull, 6148914691236518205ull, recovered);
    } catch (const std::overflow_error&) {
        summary_overflow = true;
    }
    if (!summary_overflow || recovered.count() != 0) return 10;
    executor.summarize(998, 2501, recovered);
    collatz::summary::StreamingSummary expected_slice(layout, 17);
    for (uint64_t n = 998; n <= 2501; ++n) expected_slice.add(collatz::collatz_chain(n, layout));
    if (recovered.count() != expected_slice.count() ||
        recovered.digit_totals() != expected_slice.digit_totals() ||
        recovered.length_counts() != expected_slice.length_counts())
        return 11;
    const auto binary = collatz::make_digit_layout({2});
    collatz::ChainMemo tiny(1, binary);
    collatz::prepare_memo(tiny, binary);
    if (collatz::memo_bytes_per_entry(binary) != 12) return 7;
    const auto binary_result = collatz::collatz_chain(27, binary, &tiny);
    if (binary_result.first_digit_counts[1] != 112) return 8;
    std::cout << "Frozen cache, worker reuse/recovery and summary merging passed\n";
}
