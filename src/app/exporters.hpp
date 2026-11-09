#pragma once

#include <filesystem>
#include <fstream>
#include <ostream>

namespace collatz {
struct Config;
struct DigitLayout;
namespace summary {
class StreamingSummary;
}

/// Opens a data file with exceptions on open/write/close errors. Caller closes it.
[[nodiscard]] std::ofstream open_output_file(const std::filesystem::path& path);
/// Writes the columns matching CsvWriter::row to an exception-enabled stream.
void write_csv_header(std::ostream& out, const DigitLayout& layout);
/// Serializes summary schema 2 and explicitly closes the file; propagates I/O errors.
void write_summary(const std::filesystem::path& path, const summary::StreamingSummary& summary,
                   const DigitLayout& layout);
/// Writes enabled trajectories in ascending start order and closes the file.
/// Propagates trajectory/I/O errors; creates no file if no starts are selected.
void write_coral(const std::filesystem::path& directory, const Config& config);
}  // namespace collatz
