#include "output_bundle.hpp"

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace collatz {
namespace {
std::string unique_id() {
    std::random_device random;
    const uint64_t nonce = (static_cast<uint64_t>(random()) << 32) | random();
    const auto ticks = std::chrono::duration_cast<std::chrono::nanoseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    std::ostringstream out;
    out << "run-" << ticks << '-' << std::hex << std::setfill('0') << std::setw(16) << nonce;
    return out.str();
}
}  // namespace

OutputBundle::OutputBundle(const std::filesystem::path& output) {
    const auto parent = output / "collatz_runs";
    std::filesystem::create_directories(parent);
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
        id_ = unique_id();
        final_ = parent / id_;
        staging_ = parent / (".pending-" + id_);
        if (!std::filesystem::exists(final_) && std::filesystem::create_directory(staging_)) return;
    }
    throw std::runtime_error("Cannot allocate a unique output directory");
}

OutputBundle::~OutputBundle() {
    if (!committed_) {
        try {
            std::error_code error;
            std::filesystem::remove_all(staging_, error);
        } catch (...) {
            // A failed cleanup must not replace the original calculation/I/O error.
        }
    }
}

void OutputBundle::commit() {
    if (committed_) throw std::logic_error("Output bundle is already committed");
    if (!std::filesystem::is_regular_file(staging_ / "run_info.json"))
        throw std::runtime_error("Cannot publish results without run_info.json");
    if (std::filesystem::exists(final_))
        throw std::runtime_error("Output run already exists: " + final_.string());
    std::filesystem::rename(staging_, final_);
    committed_ = true;
}
}  // namespace collatz
