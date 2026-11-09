// Diagnostic pass: compute, aggregate and write serially to time each phase.
#include <algorithm>
#include <charconv>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>

#include "app/csv_writer.hpp"
#include "collatz/range_executor.hpp"

namespace {
using Clock = std::chrono::steady_clock;
double seconds(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

uint64_t argument(const char* text, uint64_t limit) {
    const std::string_view input(text);
    uint64_t value = 0;
    const auto [end, error] = std::from_chars(input.data(), input.data() + input.size(), value);
    if (error != std::errc{} || end != input.data() + input.size() || value > limit)
        throw std::invalid_argument("Invalid benchmark argument");
    return value;
}

struct SerialTimings {
    double compute = 0;
    double aggregate = 0;
    double write = 0;
};

SerialTimings serial_pass(uint64_t count, const char* output, collatz::RangeExecutor& executor,
                          collatz::summary::StreamingSummary& serial) {
    std::ofstream csv;
    csv.exceptions(std::ios::failbit | std::ios::badbit);
    csv.open(output, std::ios::binary);
    collatz::CsvWriter writer(csv);
    collatz::ResultBatch batch;
    SerialTimings timings;
    for (uint64_t first = 1; first <= count; first += batch.metrics.size()) {
        auto start = Clock::now();
        executor.compute_batch(
            first, static_cast<size_t>(std::min<uint64_t>(10000, count - first + 1)), batch);
        timings.compute += seconds(start);
        start = Clock::now();
        for (size_t i = 0; i < batch.metrics.size(); ++i)
            serial.add(batch.metrics[i], batch.digits(i));
        timings.aggregate += seconds(start);
        start = Clock::now();
        for (size_t i = 0; i < batch.metrics.size(); ++i)
            writer.row(batch.metrics[i], batch.digits(i));
        timings.write += seconds(start);
    }
    const auto start = Clock::now();
    writer.flush();
    csv.close();
    timings.write += seconds(start);
    return timings;
}

void profile(uint64_t count, unsigned threads, const char* output) {
    const auto layout = collatz::make_digit_layout({2, 8, 10, 16});
    auto start = Clock::now();
    collatz::ChainMemo memo(std::min<uint64_t>(100000, count), layout);
    collatz::prepare_memo(memo, layout);
    const double prepare = seconds(start);
    collatz::RangeExecutor executor(layout, &memo, threads);
    collatz::summary::StreamingSummary reference(layout, 100), serial(layout, 100);
    start = Clock::now();
    executor.summarize(1, count, reference);
    const double summary = seconds(start);
    const auto timings = serial_pass(count, output, executor, serial);
    if (reference.digit_totals() != serial.digit_totals() ||
        reference.length_counts() != serial.length_counts() ||
        reference.max_histogram().counts != serial.max_histogram().counts)
        throw std::runtime_error("Summary/serial profiling passes disagree");
    std::cout << "{\"prepare_seconds\":" << prepare << ",\"summary_seconds\":" << summary
              << ",\"compute_seconds\":" << timings.compute
              << ",\"aggregate_seconds\":" << timings.aggregate
              << ",\"write_seconds\":" << timings.write << "}\n";
}
}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 4)
            throw std::invalid_argument("Usage: collatz_profile n_max threads output.csv");
        const uint64_t count = argument(argv[1], 10000000);
        if (count == 0) throw std::invalid_argument("n_max must be positive");
        profile(count, static_cast<unsigned>(argument(argv[2], 256)), argv[3]);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
