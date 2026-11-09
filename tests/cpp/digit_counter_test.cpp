#include "internal/digit_counter.hpp"

#include <bit>
#include <iostream>
#include <random>

int main() {
    std::mt19937_64 random(42);
    size_t checks = 0;
    for (int base = 2; base <= 36; ++base) {
        const auto layout = collatz::make_digit_layout({base});
        const auto check = [&](uint64_t value) {
            if (value == 0) return true;
            uint64_t expected = value;
            while (expected >= static_cast<unsigned>(base)) expected /= static_cast<unsigned>(base);
            ++checks;
            const int actual = collatz::detail::leading_digit(
                value, static_cast<unsigned>(std::bit_width(value)), layout, 0);
            if (actual == static_cast<int>(expected)) return true;
            std::cerr << "Wrong digit: base=" << base << " value=" << value << '\n';
            return false;
        };
        for (uint64_t power = 1;;) {
            if (!check(power - 1) || !check(power) || !check(power + 1)) return 1;
            for (unsigned digit = 1; digit < static_cast<unsigned>(base); ++digit) {
                if (power > UINT64_MAX / digit) break;
                const uint64_t boundary = power * digit;
                if (!check(boundary - 1) || !check(boundary) || !check(boundary + 1)) return 1;
            }
            if (power > UINT64_MAX / static_cast<unsigned>(base)) break;
            power *= static_cast<unsigned>(base);
        }
        for (unsigned bit = 0; bit < 64; ++bit) {
            const auto value = uint64_t{1} << bit;
            if (!check(value - 1) || !check(value) || !check(value + 1)) return 1;
        }
        if (!check(UINT64_MAX)) return 1;
        for (unsigned i = 0; i < 5000; ++i)
            if (!check(random())) return 1;
    }
    std::cout << checks << " boundary/random digit checks passed\n";
}
