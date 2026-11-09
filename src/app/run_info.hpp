#pragma once

#include <chrono>
#include <filesystem>
#include <string>

#include "config.hpp"

namespace collatz {
struct RunDetails {
    std::string run_id;
    std::chrono::system_clock::time_point started_at;
    unsigned threads_used = 1;
    uint64_t estimated_working_bytes = 0;
    double elapsed_seconds = 0;
};

/// Writes config, workers, build, UTC times, export duration and sizes after closing data files.
/// Explicitly closes the passport and propagates I/O errors.
void write_run_info(const std::filesystem::path& directory, const Config& config,
                    const RunDetails& details);
}  // namespace collatz
