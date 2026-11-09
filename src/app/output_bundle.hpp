#pragma once

#include <filesystem>
#include <string>

namespace collatz {
/// Owns a staging directory under output/collatz_runs; destruction removes it unless committed.
/// Published directories are never overwritten or removed by subsequent runs.
class OutputBundle {
   public:
    explicit OutputBundle(const std::filesystem::path& output);
    ~OutputBundle();
    OutputBundle(const OutputBundle&) = delete;
    OutputBundle& operator=(const OutputBundle&) = delete;
    [[nodiscard]] const std::filesystem::path& staging_directory() const noexcept {
        return staging_;
    }
    [[nodiscard]] const std::filesystem::path& final_directory() const noexcept { return final_; }
    [[nodiscard]] const std::string& run_id() const noexcept { return id_; }
    /// Requires closed data files and run_info.json. Publishes by one same-parent
    /// directory rename. Throws on a missing manifest, collision or filesystem error.
    void commit();

   private:
    std::string id_;
    std::filesystem::path staging_;
    std::filesystem::path final_;
    bool committed_ = false;
};
}  // namespace collatz
