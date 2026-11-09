#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace collatz {
struct Config {
    uint64_t n_max = 0;
    std::vector<int> bases;
    uint64_t coral_n_count = 0;
    std::vector<uint64_t> coral_n_list;
    int summary_bins = 100;
    uint64_t batch_size = 10000;
    uint64_t memo_capacity = 100000;
    unsigned threads = 0;
    uint64_t memory_budget_mb = 256;
    bool write_csv = true;
};

/// Strict JSON validation. Dimensions and a conservative working-data estimate
/// are checked before computation. memory_budget_mb is not an OS/RSS limit.
[[nodiscard]] Config load_config(const std::string& path);
[[nodiscard]] uint64_t estimate_working_bytes(const Config& config);
/// Actual worker count for a validated configuration, bounded by available rows.
[[nodiscard]] unsigned resolve_workers(const Config& config) noexcept;
}  // namespace collatz
