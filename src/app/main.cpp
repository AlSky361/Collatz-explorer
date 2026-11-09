#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#include "collatz/collatz_core.hpp"
#include "collatz/range_executor.hpp"
#include "collatz/summary.hpp"
#include "config.hpp"
#include "csv_writer.hpp"
#include "exporters.hpp"
#include "output_bundle.hpp"
#include "run_info.hpp"

namespace {
void compute_range(const collatz::Config& config, const collatz::DigitLayout& layout,
                   collatz::ChainMemo* memo, std::ostream& csv,
                   collatz::summary::StreamingSummary* summary, unsigned threads) {
    // Buffers outlive the executor, including unwinding after a CSV write error.
    collatz::ResultBatch batches[2];
    collatz::RangeExecutor executor(layout, memo, threads);
    if (!config.write_csv) {
        executor.summarize(1, config.n_max, *summary);
        return;
    }
    collatz::CsvWriter writer(csv);
    size_t current = 0;
    executor.start_batch(1, static_cast<size_t>(std::min(config.batch_size, config.n_max)),
                         batches[current]);
    for (uint64_t first = 1; first <= config.n_max;) {
        executor.wait();
        const auto& batch = batches[current];
        const auto count =
            static_cast<size_t>(std::min(config.batch_size, config.n_max - first + 1));
        const uint64_t next = first + count;
        if (next <= config.n_max)
            executor.start_batch(
                next, static_cast<size_t>(std::min(config.batch_size, config.n_max - next + 1)),
                batches[1 - current]);
        for (size_t i = 0; i < count; ++i) {
            const auto digits = batch.digits(i);
            writer.row(batch.metrics[i], digits);
            if (summary) summary->add(batch.metrics[i], digits);
        }
        first = next;
        current = 1 - current;
    }
    writer.flush();
}

void produce_data(const collatz::Config& config, const std::filesystem::path& staging,
                  unsigned threads) {
    const auto layout = collatz::make_digit_layout(config.bases);
    std::unique_ptr<collatz::ChainMemo> memo;
    if (config.memo_capacity > 0)
        memo = std::make_unique<collatz::ChainMemo>(config.memo_capacity, layout);
    if (memo) collatz::prepare_memo(*memo, layout);
    std::unique_ptr<collatz::summary::StreamingSummary> summary;
    if (config.summary_bins > 0)
        summary = std::make_unique<collatz::summary::StreamingSummary>(layout, config.summary_bins);
    std::ofstream csv;
    if (config.write_csv) {
        csv = collatz::open_output_file(staging / "collatz_results.csv");
        collatz::write_csv_header(csv, layout);
    }
    compute_range(config, layout, memo.get(), csv, summary.get(), threads);
    if (config.write_csv) csv.close();
    if (summary) collatz::write_summary(staging / "collatz_summary.json", *summary, layout);
    collatz::write_coral(staging, config);
}

void run(const collatz::Config& config, const std::filesystem::path& directory) {
    const auto started_at = std::chrono::system_clock::now();
    const auto start = std::chrono::steady_clock::now();
    const unsigned threads = collatz::resolve_workers(config);
    auto plan = config;
    plan.threads = threads;
    const uint64_t estimated = collatz::estimate_working_bytes(plan);
    if (estimated > config.memory_budget_mb * 1024 * 1024)
        throw std::length_error("Actual worker plan exceeds memory_budget_mb");
    std::cout << "Estimated working data: " << estimated / (1024.0 * 1024.0) << " MiB\n";
    // produce_data closes all streams before the staging owner can clean up.
    collatz::OutputBundle bundle(directory);
    const auto& staging = bundle.staging_directory();
    produce_data(config, staging, threads);
    collatz::RunDetails details;
    details.run_id = bundle.run_id();
    details.started_at = started_at;
    details.threads_used = threads;
    details.estimated_working_bytes = estimated;
    details.elapsed_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    collatz::write_run_info(staging, config, details);
    bundle.commit();
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << "Processed " << config.n_max << " numbers in " << seconds
              << " s; output: " << bundle.final_directory() << '\n';
}
}  // namespace

int main(int argc, char** argv) {
    if (argc > 3 || (argc > 1 && std::string(argv[1]) == "--help")) {
        std::cout << "Usage: collatz_run [config.json] [output_directory]\n";
        return argc > 3 ? 1 : 0;
    }
    try {
        const auto config = collatz::load_config(argc > 1 ? argv[1] : "config.json");
        run(config, argc > 2 ? argv[2] : ".");
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
