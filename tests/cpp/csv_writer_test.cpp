#include "app/csv_writer.hpp"

#include <iostream>
#include <sstream>

bool check_count_range() {
    std::ostringstream output, expected;
    collatz::CsvWriter writer(output);
    for (unsigned value = 0; value <= 1100; ++value) {
        collatz::ChainMetrics row;
        row.n = value + 10000;
        row.max_value = UINT64_MAX;
        row.length = static_cast<collatz::StepCount>(value);
        row.odd_steps = static_cast<collatz::StepCount>(value / 2);
        row.even_steps = static_cast<collatz::StepCount>(value - value / 2);
        const std::vector<collatz::StepCount> digits{row.length, 999, 1000, 9999, 10000, 65535};
        writer.row(row, digits);
        expected << row.n << ',' << row.length << ',' << row.max_value << ',' << row.odd_steps
                 << ',' << row.even_steps;
        for (const auto digit : digits) expected << ',' << digit;
        expected << '\n';
    }
    writer.flush();
    return output.str() == expected.str();
}

int main() {
    if (!check_count_range()) return 2;
    collatz::ChainMetrics result;
    result.n = UINT64_MAX;
    result.max_value = UINT64_MAX;
    result.length = 60000;
    result.odd_steps = 20000;
    result.even_steps = 40000;
    const std::vector<collatz::StepCount> digits{0, 60001, 0, 1, 9, 10, 99, 100};
    const std::string row =
        "18446744073709551615,60000,18446744073709551615,20000,40000,0,60001,0,1,9,10,99,100\n";
    std::ostringstream output;
    collatz::CsvWriter writer(output);
    std::string expected;
    for (unsigned i = 0; i < 10000; ++i) {
        writer.row(result, digits);
        expected += row;
    }
    writer.flush();
    writer.flush();
    if (output.str() != expected) return 1;
    std::cout << "CSV count range, maximum integers and buffer flush boundaries passed\n";
}
