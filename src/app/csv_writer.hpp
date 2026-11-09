#pragma once

#include <array>
#include <charconv>
#include <cstring>
#include <ostream>
#include <span>
#include <stdexcept>
#include <vector>

#include "collatz/collatz_core.hpp"

namespace collatz {
namespace detail {
struct SmallInteger {
    std::array<char, 4> text{};
    uint8_t size = 0;
};

inline constexpr auto kSmallIntegers = [] {
    std::array<SmallInteger, 1000> table{};
    for (size_t i = 0; i < table.size(); ++i) {
        auto& entry = table[i];
        entry.size = static_cast<uint8_t>(i < 10 ? 1 : i < 100 ? 2 : 3);
        size_t value = i;
        for (size_t digit = entry.size; digit != 0; --digit) {
            entry.text[digit - 1] = static_cast<char>('0' + value % 10);
            value /= 10;
        }
    }
    return table;
}();
}  // namespace detail

/// Integer-only CSV rows with bounded buffering. Owner must flush before closing
/// the stream; normal output is byte-for-byte compatible with the scalar writer.
class CsvWriter {
   public:
    explicit CsvWriter(std::ostream& out) : out_(out), buffer_(256 * 1024) {}
    void row(const ChainMetrics& result, std::span<const StepCount> digits) {
        append(result.n, ',');
        append(result.length, ',');
        append(result.max_value, ',');
        append(result.odd_steps, ',');
        append(result.even_steps, ',');
        for (size_t i = 0; i < digits.size(); ++i)
            append(digits[i], i + 1 == digits.size() ? '\n' : ',');
    }
    void flush() {
        out_.write(buffer_.data(), static_cast<std::streamsize>(used_));
        used_ = 0;
    }

   private:
    void append(StepCount value, char delimiter) {
        if (value >= detail::kSmallIntegers.size()) {
            append(static_cast<uint64_t>(value), delimiter);
            return;
        }
        if (buffer_.size() - used_ < 4) flush();
        const auto& entry = detail::kSmallIntegers[value];
        // Four bytes fit; padding is overwritten or excluded from the logical size.
        std::memcpy(buffer_.data() + used_, entry.text.data(), 4);
        buffer_[used_ + entry.size] = delimiter;
        used_ += static_cast<size_t>(entry.size) + 1;
    }
    void append(uint64_t value, char delimiter) {
        if (buffer_.size() - used_ < 21) flush();
        auto* begin = buffer_.data() + used_;
        const auto result = std::to_chars(begin, buffer_.data() + buffer_.size() - 1, value);
        if (result.ec != std::errc{}) throw std::runtime_error("Cannot format CSV integer");
        *result.ptr = delimiter;
        used_ = static_cast<size_t>(result.ptr - buffer_.data()) + 1;
    }
    std::ostream& out_;
    std::vector<char> buffer_;
    size_t used_ = 0;
};
}  // namespace collatz
