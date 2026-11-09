#include "app/output_bundle.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <stdexcept>
#include <system_error>

#include "app/run_info.hpp"

namespace {
namespace fs = std::filesystem;

struct TestDirectory {
    fs::path path;
    TestDirectory() {
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() / ("collatz-output-test-" + std::to_string(ticks) + "-" +
                                            std::to_string(std::random_device{}()));
        if (!fs::create_directory(path)) throw std::runtime_error("Test directory already exists");
    }
    ~TestDirectory() {
        std::error_code error;
        fs::remove_all(path, error);
    }
};

void write_file(const fs::path& path, const std::string& text) {
    std::ofstream out;
    out.exceptions(std::ios::failbit | std::ios::badbit);
    out.open(path, std::ios::binary);
    out << text;
    out.close();
}

std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot read test file");
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void passport(const collatz::OutputBundle& bundle) {
    collatz::Config config;
    config.n_max = config.batch_size = config.memo_capacity = 1;
    config.bases = {2};
    config.threads = 1;
    config.write_csv = false;
    collatz::RunDetails details;
    details.run_id = bundle.run_id();
    details.started_at = std::chrono::system_clock::now();
    details.estimated_working_bytes = collatz::estimate_working_bytes(config);
    collatz::write_run_info(bundle.staging_directory(), config, details);
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

bool check_publication(const fs::path& root) {
    fs::path first;
    {
        collatz::OutputBundle bundle(root);
        write_file(bundle.staging_directory() / "collatz_summary.json", "original summary\n");
        passport(bundle);
        first = bundle.final_directory();
        if (fs::exists(first)) return false;
        bundle.commit();
        if (fs::exists(bundle.staging_directory()) || !fs::is_directory(first)) return false;
        if (!rejects<std::logic_error>([&] { bundle.commit(); })) return false;
    }
    const auto original = read_file(first / "run_info.json");
    fs::path staging;
    {
        collatz::OutputBundle bundle(root);
        staging = bundle.staging_directory();
        write_file(staging / "collatz_summary.json", "incomplete\n");
        if (!rejects<std::runtime_error>([&] { bundle.commit(); })) return false;
    }
    if (fs::exists(staging)) return false;
    fs::path collision;
    {
        collatz::OutputBundle bundle(root);
        staging = bundle.staging_directory();
        write_file(staging / "collatz_summary.json", "new summary\n");
        passport(bundle);
        collision = bundle.final_directory();
        write_file(collision, "unrelated file");
        if (!rejects<std::runtime_error>([&] { bundle.commit(); })) return false;
    }
    return !fs::exists(staging) && read_file(collision) == "unrelated file" &&
           read_file(first / "run_info.json") == original &&
           read_file(first / "collatz_summary.json") == "original summary\n";
}

bool check_write_failure(const fs::path& root) {
    fs::path staging;
    {
        collatz::OutputBundle bundle(root);
        staging = bundle.staging_directory();
        write_file(staging / "collatz_summary.json", "new summary\n");
        fs::create_directory(staging / "run_info.json");
        if (!rejects<std::ios_base::failure>([&] { passport(bundle); })) return false;
    }
    return !fs::exists(staging);
}
}  // namespace

int main() {
    try {
        TestDirectory root;
        if (!check_publication(root.path) || !check_write_failure(root.path)) return 1;
        std::cout << "Publication, rollback, collision and passport write errors passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
