#pragma once

#include "collatz/collatz_core.hpp"

namespace collatz::detail {
// For value>0 and width=bit_width(value), use the scale at the interval's lower endpoint.
// A non-power-of-two base crosses at most one extra power within that interval.
inline int leading_digit(uint64_t value, unsigned width, const DigitLayout& layout, size_t b) {
    const auto scale = layout.digit_scales[b][width];
    if (layout.bit_shifts[b] != 0) return static_cast<int>(value >> scale);
    const auto& powers = layout.powers[b];
    const size_t index =
        scale + (static_cast<size_t>(scale) + 1 < powers.size() && value >= powers[scale + 1]);
    return static_cast<int>(value / powers[index]);
}
}  // namespace collatz::detail
