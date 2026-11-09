#include "run_info.hpp"

#include <ctime>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
#include <thread>

#include "collatz_build_info.hpp"
#include "exporters.hpp"

namespace collatz {
namespace {
using Json = nlohmann::json;

std::string utc_timestamp(std::chrono::system_clock::time_point point) {
    const auto seconds = std::chrono::floor<std::chrono::seconds>(point);
    const auto fraction =
        std::chrono::duration_cast<std::chrono::nanoseconds>(point - seconds).count();
    const std::time_t time = std::chrono::system_clock::to_time_t(seconds);
    std::tm utc{};
#ifdef _WIN32
    if (gmtime_s(&utc, &time) != 0) throw std::runtime_error("Cannot format UTC time");
#else
    if (gmtime_r(&time, &utc) == nullptr) throw std::runtime_error("Cannot format UTC time");
#endif
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.' << std::setfill('0') << std::setw(9)
        << fraction << 'Z';
    return out.str();
}

Json config_json(const Config& config) {
    return {{"n_max", config.n_max},
            {"bases", config.bases},
            {"batch_size", config.batch_size},
            {"memo_capacity", config.memo_capacity},
            {"threads", config.threads},
            {"memory_budget_mb", config.memory_budget_mb},
            {"write_csv", config.write_csv},
            {"summary_bins", config.summary_bins},
            {"coral_n_count", config.coral_n_count},
            {"coral_n_list", config.coral_n_list}};
}

Json data_files(const std::filesystem::path& directory, const Config& config) {
    Json files = Json::array();
    const auto add = [&](const char* name) {
        files.push_back(
            {{"name", name}, {"size_bytes", std::filesystem::file_size(directory / name)}});
    };
    if (config.write_csv) add("collatz_results.csv");
    if (config.summary_bins > 0) add("collatz_summary.json");
    if (config.coral_n_count > 0 || !config.coral_n_list.empty()) add("collatz_coral.csv");
    return files;
}
}  // namespace

void write_run_info(const std::filesystem::path& directory, const Config& config,
                    const RunDetails& details) {
    const std::string_view configuration = COLLATZ_BUILD_CONFIGURATION;
    const Json info{
        {"schema_version", 1},
        {"status", "complete"},
        {"run_id", details.run_id},
        {"started_at_utc", utc_timestamp(details.started_at)},
        {"finished_at_utc", utc_timestamp(std::chrono::system_clock::now())},
        {"program",
         {{"name", "Collatz Explorer"}, {"version", build_info::kVersion}, {"cpp_standard", 20}}},
        {"build",
         {{"configuration", configuration.empty() ? "unspecified" : configuration},
          {"system", build_info::kSystem},
          {"architecture", build_info::kArchitecture},
          {"compiler",
           {{"id", build_info::kCompilerId}, {"version", build_info::kCompilerVersion}}}}},
        {"config", config_json(config)},
        {"execution",
         {{"threads_used", details.threads_used},
          {"hardware_threads", std::thread::hardware_concurrency()},
          {"estimated_working_bytes", details.estimated_working_bytes}}},
        {"timing",
         {{"elapsed_seconds", details.elapsed_seconds},
          {"scope", "run start through closed data files; excludes passport and publication"}}},
        {"files", data_files(directory, config)}};
    auto out = open_output_file(directory / "run_info.json");
    out << info.dump(2) << '\n';
    out.close();
}
}  // namespace collatz
