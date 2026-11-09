#include "exporters.hpp"

#include <nlohmann/json.hpp>
#include <set>

#include "collatz/collatz_core.hpp"
#include "collatz/summary.hpp"
#include "config.hpp"

namespace collatz {
namespace {
using Json = nlohmann::json;

Json histogram_json(const collatz::summary::Histogram& hist) {
    return {{"edges", hist.edges}, {"counts", hist.counts}, {"log_space", hist.log_space}};
}

}  // namespace

std::ofstream open_output_file(const std::filesystem::path& path) {
    std::ofstream out;
    out.exceptions(std::ios::failbit | std::ios::badbit);
    out.open(path);
    return out;
}

void write_csv_header(std::ostream& out, const collatz::DigitLayout& layout) {
    out << "n,length,max_value,odd_steps,even_steps";
    for (const int base : layout.bases) {
        for (int digit = 0; digit < base; ++digit) out << ",fd_b" << base << "_d" << digit;
    }
    out << '\n';
}

void write_summary(const std::filesystem::path& path,
                   const collatz::summary::StreamingSummary& summary,
                   const collatz::DigitLayout& layout) {
    Json j{{"schema_version", 2},
           {"n_max", summary.count()},
           {"bases", layout.bases},
           {"first_digit_sampling", "full_trajectory_including_start_and_terminal_1"},
           {"max_value_bin_domain", "fixed logarithmic [1, 2^64]"}};
    j["histograms"]["length"] = histogram_json(summary.length_histogram());
    j["histograms"]["odd_steps"] = histogram_json(summary.odd_histogram());
    j["histograms"]["even_steps"] = histogram_json(summary.even_histogram());
    j["histograms"]["max_value"] = histogram_json(summary.max_histogram());
    auto lengths = summary.length_counts();
    while (lengths.size() > 1 && lengths.back() == 0) lengths.pop_back();
    j["length_counts"] = lengths;
    for (size_t b = 0; b < layout.bases.size(); ++b) {
        const auto begin =
            summary.digit_totals().begin() + static_cast<std::ptrdiff_t>(layout.offsets[b]);
        j["first_digit_counts"][std::to_string(layout.bases[b])] =
            std::vector<uint64_t>(begin, begin + layout.bases[b]);
    }
    auto out = open_output_file(path);
    out << j.dump(2) << '\n';
    out.close();
}

void write_coral(const std::filesystem::path& directory, const collatz::Config& config) {
    std::set<uint64_t> values(config.coral_n_list.begin(), config.coral_n_list.end());
    for (uint64_t n = 1; n <= config.coral_n_count; ++n) values.insert(n);
    if (values.empty()) return;
    auto out = open_output_file(directory / "collatz_coral.csv");
    out << "n,step,value\n";
    for (const uint64_t n : values) {
        const auto trajectory = collatz::collatz_trajectory(n);
        for (size_t i = 0; i < trajectory.size(); ++i)
            out << n << ',' << i << ',' << trajectory[i] << '\n';
    }
    out.close();
}

}  // namespace collatz
