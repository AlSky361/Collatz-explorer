"""Histogram and empirical CDF of trajectory maxima; X uses a logarithmic scale."""

from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

import collatz_theory as ct
from data_source import open_data_source
from plot_cli import run_stat_plot
from summary_view import log_edges, show_maxima



def load_max_values(csv_path: Path) -> np.ndarray:
    return open_data_source(csv_path).columns("max_value")[0]


def build_figure(max_values: np.ndarray, *, show: bool = False) -> plt.Figure:
    bins = ct.freedman_diaconis_bins(max_values, 5, 300, log_space=True)
    edges = log_edges(max_values[max_values > 0], bins)
    counts, _ = np.histogram(max_values, bins=edges)
    return show_maxima(edges, counts, len(max_values), samples=max_values, show=show)


def main() -> None:
    run_stat_plot(lambda source: build_figure(source.columns("max_value")[0]),
                  ("max_value",), distribution=True)


if __name__ == "__main__":
    main()
