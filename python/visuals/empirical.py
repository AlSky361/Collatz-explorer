"""Empirical CDF in the lecture convention F*_X(x) = #{x_k < x}/N."""

import numpy as np


def empirical_cdf_points(values, counts=None):
    """Return distinct values and CDF levels at/beyond jumps, excluding ties at the jump.
    Optional counts are exact frequencies, not grouped histogram bins."""
    values = np.asarray(values)
    if values.ndim != 1 or not values.size or not np.all(np.isfinite(values)):
        raise ValueError("Expected nonempty finite observations")
    if counts is None:
        support, frequency = np.unique(values, return_counts=True)
    else:
        frequency = np.asarray(counts)
        if (frequency.shape != values.shape or not np.all(np.isfinite(frequency))
                or np.any(frequency < 0) or np.any(frequency != np.floor(frequency))
                or np.any(values[1:] <= values[:-1])):
            raise ValueError("Expected ordered values and nonnegative integer frequencies")
        occupied = frequency > 0
        support, frequency = values[occupied], frequency[occupied]
        if not support.size:
            raise ValueError("Expected positive total frequency")
    cumulative = np.cumsum(frequency, dtype=np.float64)
    before = np.r_[0.0, cumulative[:-1]] / cumulative[-1]
    after = cumulative / cumulative[-1]
    return support, before, after


def draw_empirical_cdf(ax, values, counts=None, *, color, label=None, log_x=False, alpha=1.0):
    """Draw left-continuous empirical steps without point markers."""
    support, before, _ = empirical_cdf_points(values, counts)
    if log_x:
        left, right = float(support[0]) / 1.1, float(support[-1]) * 1.1
    else:
        padding = max(1.0, float(support[-1] - support[0]) * 0.03)
        left, right = float(support[0]) - padding, float(support[-1]) + padding
    line, = ax.step(np.r_[left, support, right], np.r_[0.0, before, 1.0],
                    where="pre", color=color, linewidth=1.8, label=label, alpha=alpha)
    return line
