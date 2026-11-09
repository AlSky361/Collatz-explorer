"""Headless plotting tests; run after installing requirements.txt."""

import importlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import shutil
import unittest
from unittest.mock import patch

os.environ.setdefault("MPLBACKEND", "Agg")
os.environ.setdefault("MPLCONFIGDIR", str(Path(tempfile.gettempdir()) / "collatz-matplotlib"))
os.environ.setdefault("XDG_CACHE_HOME", str(Path(tempfile.gettempdir()) / "collatz-fontcache"))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "python" / "visuals"))
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from result_paths import resolve_directory

BINARY = Path(sys.argv.pop(1)).resolve()
MODULES = ["chain_length_hist", "odd_even_steps_hist", "max_value_hist",
           "chain_length_distribution", "benford_digits"]


class VisualizationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="collatz-visuals-")
        self.addCleanup(self.temp.cleanup)
        self.addCleanup(plt.close, "all")
        self.root = Path(self.temp.name)

    def dataset(self, n_max):
        cfg = self.root / "config.json"
        cfg.write_text(json.dumps(dict(n_max=n_max, bases=[2, 8, 10, 16],
                                       batch_size=11, threads=2, summary_bins=13,
                                       coral_n_list=[3, 27])))
        subprocess.run([str(BINARY), str(cfg), str(self.root)], check=True, capture_output=True)
        self.data_root = resolve_directory(self.root)

    def run_figure(self, name, path):
        module = importlib.import_module(name)
        with patch.object(sys, "argv", [name, str(path)]), patch.object(plt, "show"):
            module.main()
        for number in plt.get_fignums():
            fig = plt.figure(number)
            fig.canvas.draw()
            for widget in getattr(fig, "_collatz_widgets", ()):
                if hasattr(widget, "set_val"):
                    widget.set_val((widget.valmin + widget.valmax) / 2)
                    fig.canvas.draw()
                elif hasattr(widget, "set_active"):
                    widget.set_active(1 if hasattr(widget, "value_selected") else 0)
                    fig.canvas.draw()
        plt.close("all")

    def test_summary_figures_never_read_csv(self):
        for n_max in [1, 100]:
            self.dataset(n_max)
            with patch.object(pd, "read_csv", side_effect=AssertionError("Summary path read CSV")):
                for name in MODULES:
                    with self.subTest(n=n_max, module=name):
                        self.run_figure(name, self.data_root / "collatz_summary.json")

    def test_csv_and_coral_figures(self):
        self.dataset(100)
        for name in MODULES:
            with self.subTest(module=name):
                self.run_figure(name, self.data_root / "collatz_results.csv")
        self.run_figure("coral_trajectories", self.data_root / "collatz_coral.csv")

    def test_default_prefers_only_fresh_summary(self):
        from data_source import resolve_input
        self.dataset(10)
        legacy = self.root / "legacy"
        legacy.mkdir()
        for name in ["collatz_results.csv", "collatz_summary.json"]:
            shutil.copy2(self.data_root / name, legacy / name)
        csv = legacy / "collatz_results.csv"
        summary = legacy / "collatz_summary.json"
        with patch.object(sys, "argv", ["plot", "unrelated.csv"]):
            self.assertEqual(resolve_input(csv), summary)
            self.assertEqual(resolve_input(csv, supplied=csv), csv)
            timestamp = summary.stat().st_mtime_ns + 1000000000
            os.utime(csv, ns=(timestamp, timestamp))
            self.assertEqual(resolve_input(csv), csv)

    def test_directory_selects_a_complete_set(self):
        self.dataset(10)
        for name in MODULES:
            with self.subTest(module=name):
                self.run_figure(name, self.root)
        self.run_figure("coral_trajectories", self.root)

    def test_lecture_cdf_excludes_all_ties_at_a_jump(self):
        from empirical import empirical_cdf_points, draw_empirical_cdf
        sample = np.array([3, 1, 0, 1])
        support, before, after = empirical_cdf_points(sample)
        np.testing.assert_array_equal(support, [0, 1, 3])
        np.testing.assert_array_equal(before, [0, 0.25, 0.75])
        np.testing.assert_array_equal(after, [0.25, 0.75, 1])
        grouped = empirical_cdf_points([0, 1, 2, 3], [1, 2, 0, 1])
        for observed, expected in zip(grouped, (support, before, after)):
            np.testing.assert_array_equal(observed, expected)
        fig, ax = plt.subplots()
        line = draw_empirical_cdf(ax, sample, color="blue")
        self.assertEqual(line.get_drawstyle(), "steps-pre")
        # At x=1 the CDF is 1/4, not 3/4; both repeated ones jump together.
        np.testing.assert_array_equal(line.get_ydata()[1:-1], before)
        self.assertEqual(len(ax.lines), 1)
        for value in [0, 7]:
            point, at, beyond = empirical_cdf_points([value, value])
            np.testing.assert_array_equal(at, [0])
            np.testing.assert_array_equal(beyond, [1])

    def test_normalized_histogram_has_unit_area(self):
        from data_source import load_summary
        from summary_view import show_histograms
        self.dataset(100)
        data = load_summary(self.data_root / "collatz_summary.json")
        fig = show_histograms(data, ("length",), distribution=True, show=False)
        stairs = fig.axes[0].patches[0].get_data()
        self.assertAlmostEqual(float(np.dot(stairs.values, np.diff(stairs.edges))), 1.0)

    def test_data_source_preserves_counts_and_caches_csv_reads(self):
        from data_source import open_data_source
        self.dataset(100)
        csv = open_data_source(self.data_root / "collatz_results.csv")
        summary = open_data_source(self.data_root / "collatz_summary.json")
        with patch.object(pd, "read_csv", wraps=pd.read_csv) as reader:
            for base in [2, 8, 10, 16]:
                for actual, expected in zip(csv.first_digit_counts(base), summary.first_digit_counts(base)):
                    np.testing.assert_array_equal(actual, expected)
            reads = reader.call_count
            csv.first_digit_counts(10)
            self.assertEqual(reader.call_count, reads)
            n, lengths = csv.columns("n", "length")
            reads = reader.call_count
            again_lengths, again_n = csv.columns("length", "n")
            self.assertIs(n, again_n)
            self.assertIs(lengths, again_lengths)
            self.assertEqual(reader.call_count, reads)
        with self.assertRaises(ValueError):
            summary.columns("length")

    def test_figure_builders_return_figures_without_opening_windows(self):
        from data_source import open_data_source
        from summary_view import show_histograms
        self.dataset(100)
        source = open_data_source(self.data_root / "collatz_results.csv")
        summary = open_data_source(self.data_root / "collatz_summary.json")
        n, length, odd, even, maximum = source.columns("n", "length", "odd_steps", "even_steps", "max_value")
        builders = [
            ("chain_length_hist", (length,)),
            ("chain_length_distribution", (n, length)),
            ("odd_even_steps_hist", (odd, even)),
            ("max_value_hist", (maximum,)),
            ("benford_digits", (summary,)),
        ]
        with patch.object(plt, "show", side_effect=AssertionError("Builder opened a window")):
            for name, args in builders:
                with self.subTest(module=name):
                    fig = importlib.import_module(name).build_figure(*args)
                    self.assertIsInstance(fig, plt.Figure)
                    plt.close(fig)
            fig = show_histograms(summary.summary, ("length",), distribution=True)
            self.assertIsInstance(fig, plt.Figure)
            plt.close(fig)
        with patch.object(plt, "show") as display:
            fig = importlib.import_module("chain_length_hist").build_figure(length, show=True)
            display.assert_called_once_with()
            plt.close(fig)


if __name__ == "__main__":
    unittest.main()
