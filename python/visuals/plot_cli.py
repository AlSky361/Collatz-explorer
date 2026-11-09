"""Command-line boundary for statistical plots; builders only return figures."""

import argparse

import matplotlib.pyplot as plt

from data_source import open_data_source
from summary_view import show_histograms


def run_plot(builder):
    parser = argparse.ArgumentParser()
    parser.add_argument("input", nargs="?", help="Completed output directory, CSV or summary JSON")
    args = parser.parse_args()
    try:
        source = open_data_source(args.input)
        builder(source)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    plt.show()


def run_stat_plot(csv_builder, metrics, *, distribution=False):
    def build(source):
        if source.is_summary:
            return show_histograms(source.summary, metrics, distribution=distribution)
        return csv_builder(source)
    run_plot(build)
