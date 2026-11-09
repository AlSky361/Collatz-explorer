#include "config.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>

#include "collatz/collatz_core.hpp"
#include "internal/frozen_memo.hpp"

static_assert(NLOHMANN_JSON_VERSION_MAJOR == 3 && NLOHMANN_JSON_VERSION_MINOR == 12 &&
                  NLOHMANN_JSON_VERSION_PATCH == 0,
              "Expected vendored nlohmann/json 3.12.0");

namespace collatz {
namespace {
using Json = nlohmann::json;

uint64_t integer(const Json& value, const std::string& name, uint64_t lo, uint64_t hi) {
    if (!value.is_number_integer() ||
        (value.is_number_integer() && !value.is_number_unsigned() && value.get<int64_t>() < 0)) {
        throw std::invalid_argument(name + ": expected a non-negative integer");
    }
    const uint64_t result = value.get<uint64_t>();
    if (result < lo || result > hi) {
        throw std::invalid_argument(name + ": expected integer in [" + std::to_string(lo) + ", " +
                                    std::to_string(hi) + "]");
    }
    return result;
}

uint64_t option(const Json& j, const char* key, uint64_t fallback, uint64_t lo, uint64_t hi) {
    return j.contains(key) ? integer(j.at(key), key, lo, hi) : fallback;
}

void read_arrays(const Json& j, Config& config) {
    if (!j.contains("bases") || !j.at("bases").is_array()) {
        throw std::invalid_argument("bases: expected an array");
    }
    for (const auto& base : j.at("bases")) {
        config.bases.push_back(static_cast<int>(integer(base, "bases[]", 2, 36)));
    }
    (void)make_digit_layout(config.bases);
    if (!j.contains("coral_n_list")) return;
    const auto& values = j.at("coral_n_list");
    if (!values.is_array() || values.size() > 10000) {
        throw std::invalid_argument("coral_n_list: expected an array of at most 10000 entries");
    }
    for (const auto& n : values) {
        config.coral_n_list.push_back(integer(n, "coral_n_list[]", 1, UINT64_MAX));
    }
}

uint64_t checked_product(uint64_t left, uint64_t right) {
    if (left > UINT64_MAX / right) throw std::length_error("Memory size overflows uint64_t");
    return left * right;
}

void add_bytes(uint64_t& total, uint64_t count, uint64_t width) {
    const uint64_t extra = checked_product(count, width);
    if (total > UINT64_MAX - extra) throw std::length_error("Memory size overflows uint64_t");
    total += extra;
}

}  // namespace

unsigned resolve_workers(const Config& config) noexcept {
    const uint64_t available =
        config.write_csv ? std::min(config.batch_size, config.n_max) : config.n_max;
    return static_cast<unsigned>(
        std::min<uint64_t>(resolve_thread_count(config.threads), available));
}

uint64_t estimate_working_bytes(const Config& config) {
    const auto layout = make_digit_layout(config.bases);
    uint64_t total = 8 * 1024 * 1024;  // streams, histogram edges and small auxiliary data
    const uint64_t digits = layout.total_size * sizeof(StepCount);
    add_bytes(total, memo_entry_count(config.memo_capacity), memo_bytes_per_entry(layout));
    if (config.write_csv)
        add_bytes(total, 2 * std::min(config.batch_size, config.n_max),
                  sizeof(ChainMetrics) + digits);
    const auto workers = resolve_workers(config);
    // Worst-case scratch and dynamically grown frequency capacities, not typical RSS.
    add_bytes(total, workers, 2 * (kMaxChainSteps + 1ull) * sizeof(uint64_t) + 4 * digits);
    if (config.summary_bins > 0) {
        const uint64_t copies = config.write_csv ? 1 : workers + 1;
        add_bytes(total, copies, 6 * (kMaxChainSteps + 1ull) * sizeof(uint64_t));
        add_bytes(total, copies,
                  static_cast<uint64_t>(config.summary_bins) * 16 + layout.total_size * 8);
    }
    if (!config.write_csv) {
        add_bytes(total, workers,
                  checked_product(detail::deferred_tail_slots(layout, config.memo_capacity,
                                                              config.n_max, workers),
                                  sizeof(uint64_t)) +
                      layout.total_size * sizeof(uint64_t));
    }
    return total;
}

Config load_config(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Cannot open configuration: " + path);
    const Json j = Json::parse(input);
    if (!j.is_object()) throw std::invalid_argument("Configuration must be a JSON object");
    const std::set<std::string> known{
        "n_max",      "bases",         "coral_n_count", "coral_n_list",     "summary_bins",
        "batch_size", "memo_capacity", "threads",       "memory_budget_mb", "write_csv"};
    for (const auto& item : j.items()) {
        if (!known.contains(item.key()))
            throw std::invalid_argument("Unknown option: " + item.key());
    }
    if (!j.contains("n_max")) throw std::invalid_argument("Missing n_max");
    Config config;
    config.n_max = integer(j.at("n_max"), "n_max", 1, 1000000000000ull);
    config.batch_size = option(j, "batch_size", config.batch_size, 1, 10000000);
    config.memo_capacity = option(j, "memo_capacity", config.memo_capacity, 0, 1000000000000ull);
    config.memo_capacity = std::min(config.memo_capacity, config.n_max);
    config.threads = static_cast<unsigned>(option(j, "threads", 0, 0, 256));
    config.memory_budget_mb = option(j, "memory_budget_mb", 256, 16, 1048576);
    config.summary_bins = static_cast<int>(option(j, "summary_bins", 100, 0, 10000));
    config.coral_n_count = option(j, "coral_n_count", 0, 0, 10000);
    read_arrays(j, config);
    if (j.contains("write_csv")) {
        if (!j.at("write_csv").is_boolean())
            throw std::invalid_argument("write_csv must be boolean");
        config.write_csv = j.at("write_csv").get<bool>();
    }
    if (!config.write_csv && config.summary_bins == 0) {
        throw std::invalid_argument("Enable write_csv or summary_bins for range results");
    }
    const uint64_t estimated = estimate_working_bytes(config);
    if (estimated > checked_product(config.memory_budget_mb, 1024 * 1024)) {
        throw std::length_error(
            "Estimated working data exceeds memory_budget_mb; reduce batch_size, memo_capacity or "
            "threads");
    }
    return config;
}
}  // namespace collatz
