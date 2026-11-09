"""End-to-end checks against an independent, full-trajectory Python reference."""

import bisect
import csv
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "python" / "visuals"))
from result_paths import load_run_info, resolve_directory

BINARY = Path(sys.argv.pop(1)).resolve()


def trajectory(n):
    result = [n]
    while n != 1:
        n = 3 * n + 1 if n % 2 else n // 2
        result.append(n)
    return result


def reference(n, bases):
    values = trajectory(n)
    odd = sum(value % 2 for value in values[:-1])
    row = dict(n=n, length=len(values) - 1, max_value=max(values), odd_steps=odd,
               even_steps=len(values) - 1 - odd)
    for base in bases:
        counts = [0] * base
        for value in values:
            digits = []
            while value:
                value, digit = divmod(value, base)
                digits.append(digit)
            counts[digits[-1]] += 1
        row.update({f"fd_b{base}_d{d}": count for d, count in enumerate(counts)})
    return row


class IntegrationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="collatz-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.serial = 0

    def run_app(self, output_directory=None, **options):
        self.serial += 1
        config = dict(n_max=300, bases=[2, 8, 10, 16], batch_size=37,
                      memo_capacity=127, threads=3, summary_bins=13)
        config.update(options)
        path = self.root / f"config{self.serial}.json"
        path.write_text(json.dumps(config), encoding="utf-8")
        output = output_directory if output_directory is not None else self.root / f"run{self.serial}"
        result = subprocess.run([str(BINARY), str(path), str(output)], capture_output=True,
                                text=True, timeout=60)
        return result, output, config

    def require_success(self, **options):
        result, output, config = self.run_app(**options)
        self.assertEqual(result.returncode, 0, result.stderr)
        return resolve_directory(output), config

    def read_rows(self, output):
        with (output / "collatz_results.csv").open() as stream:
            return [{key: int(value) for key, value in row.items()} for row in csv.DictReader(stream)]

    def test_all_metrics_against_independent_reference(self):
        output, config = self.require_success(coral_n_count=3, coral_n_list=[3, 27, 27])
        rows = self.read_rows(output)
        expected = [reference(n, config["bases"]) for n in range(1, config["n_max"] + 1)]
        self.assertEqual(rows, expected)
        summary = json.loads((output / "collatz_summary.json").read_text())
        self.assertEqual(summary["schema_version"], 2)
        for metric, hist in summary["histograms"].items():
            counts = [0] * len(hist["counts"])
            for row in expected:
                index = min(max(0, bisect.bisect_right(hist["edges"], row[metric]) - 1), len(counts) - 1)
                counts[index] += 1
            self.assertEqual(hist["counts"], counts, metric)
        for base in config["bases"]:
            totals = [sum(row[f"fd_b{base}_d{digit}"] for row in expected) for digit in range(base)]
            self.assertEqual(summary["first_digit_counts"][str(base)], totals)
        self.assertEqual(sum(summary["length_counts"]), config["n_max"])
        with (output / "collatz_coral.csv").open() as stream:
            coral = list(csv.DictReader(stream))
        for n in [1, 2, 3, 27]:
            self.assertEqual([int(r["value"]) for r in coral if int(r["n"]) == n], trajectory(n))

    def test_all_supported_bases_and_summary_mode(self):
        output, _ = self.require_success(n_max=200, bases=list(range(2, 37)), memo_capacity=199)
        rows = self.read_rows(output)
        self.assertEqual(rows, [reference(n, list(range(2, 37))) for n in range(1, 201)])
        first, _ = self.require_success(n_max=2000, threads=1)
        second, _ = self.require_success(n_max=2000, write_csv=False, threads=7, batch_size=1)
        self.assertEqual((first / "collatz_summary.json").read_bytes(), (second / "collatz_summary.json").read_bytes())

    def test_batch_cache_thread_independence(self):
        first, _ = self.require_success(batch_size=1, memo_capacity=0, threads=1)
        second, _ = self.require_success(batch_size=29, memo_capacity=47, threads=8)
        for name in ["collatz_results.csv", "collatz_summary.json"]:
            self.assertEqual((first / name).read_bytes(), (second / name).read_bytes(), name)

    def test_deferred_digits_against_independent_reference(self):
        for bases in [[2, 8, 10, 16], [16, 10, 8, 2], list(range(2, 37))]:
            with self.subTest(bases=bases):
                output, config = self.require_success(n_max=500, bases=bases, memo_capacity=47,
                                                      threads=4, write_csv=False)
                summary = json.loads((output / "collatz_summary.json").read_text())
                expected = [reference(n, bases) for n in range(1, 501)]
                for base in bases:
                    totals = [sum(row[f"fd_b{base}_d{digit}"] for row in expected)
                              for digit in range(base)]
                    self.assertEqual(summary["first_digit_counts"][str(base)], totals)
                for metric, hist in summary["histograms"].items():
                    counts = [0] * len(hist["counts"])
                    for row in expected:
                        index = min(max(0, bisect.bisect_right(hist["edges"], row[metric]) - 1), len(counts) - 1)
                        counts[index] += 1
                    self.assertEqual(hist["counts"], counts, metric)

    def test_summary_only_and_singleton(self):
        output, _ = self.require_success(n_max=1, write_csv=False)
        self.assertFalse((output / "collatz_results.csv").exists())
        summary = json.loads((output / "collatz_summary.json").read_text())
        self.assertEqual(summary["length_counts"], [1])
        for hist in summary["histograms"].values():
            self.assertEqual(sum(hist["counts"]), 1)
            self.assertTrue(all(a < b for a, b in zip(hist["edges"], hist["edges"][1:])))
        output, _ = self.require_success(summary_bins=0)
        self.assertFalse((output / "collatz_summary.json").exists())

    def test_rejects_invalid_configuration_before_output(self):
        invalid = [dict(n_max=x) for x in [0, -1, 3.5, True, "3", 10**30]]
        invalid += [dict(bases=x) for x in [[], [10, 10], [1], [37], [10.5], [True], "10"]]
        invalid += [dict(batch_size=0), dict(batch_size=1.5), dict(threads=257), dict(threads=True),
                    dict(summary_bins=10001), dict(summary_bins=-1), dict(coral_n_count=10001),
                    dict(coral_n_list=[0]), dict(coral_n_list=[3.5]), dict(coral_n_list=[2**64]),
                    dict(write_csv=1), dict(write_csv=False, summary_bins=0), dict(typo=1),
                    dict(n_max=1000000, memo_capacity=1000000, memory_budget_mb=16),
                    dict(n_max=10**12, memo_capacity=10**12)]
        for options in invalid:
            with self.subTest(options=options):
                result, output, _ = self.run_app(**options)
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertFalse(output.exists())

    def test_configuration_requires_one_complete_json_document(self):
        document = json.dumps(dict(n_max=1, bases=[2]))
        for index, suffix in enumerate(["", "\n \t", "\nINVALID", "\n{}"]):
            with self.subTest(suffix=suffix):
                path = self.root / f"document{index}.json"
                path.write_text(document + suffix, encoding="utf-8")
                output = self.root / f"document{index}"
                result = subprocess.run([str(BINARY), str(path), str(output)],
                                        capture_output=True, text=True, timeout=60)
                if suffix.strip():
                    self.assertEqual(result.returncode, 1, result.stderr)
                    self.assertFalse(output.exists())
                else:
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertTrue(output.exists())

    def test_overflow_reports_error_instead_of_aborting(self):
        result, _, _ = self.run_app(n_max=1, coral_n_list=[6148914691236517205])
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("3n+1", result.stderr)

    def test_memory_estimate_independent_of_range(self):
        first, _, _ = self.run_app(n_max=100, batch_size=7, memo_capacity=19, write_csv=False)
        second, _, _ = self.run_app(n_max=1000, batch_size=7, memo_capacity=19, write_csv=False)
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertEqual(second.returncode, 0, second.stderr)
        self.assertEqual(first.stdout.splitlines()[0], second.stdout.splitlines()[0])

    def test_deferred_arrays_in_memory_estimate(self):
        first, _, _ = self.run_app(n_max=1000, memo_capacity=1000, threads=4, write_csv=False)
        second, _, _ = self.run_app(n_max=2000, memo_capacity=1000, threads=4, write_csv=False)
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertEqual(second.returncode, 0, second.stderr)
        before = float(first.stdout.splitlines()[0].split()[3])
        after = float(second.stdout.splitlines()[0].split()[3])
        self.assertAlmostEqual(after - before, 4 * 500 * 8 / 1024**2, delta=0.0002)

    def test_run_passport(self):
        output, _ = self.require_success(n_max=17, memo_capacity=999999, batch_size=1,
                                          threads=8, coral_n_list=[3])
        info = load_run_info(output)
        self.assertEqual(info["run_id"], output.name)
        self.assertEqual(info["config"]["n_max"], 17)
        self.assertEqual(info["config"]["memo_capacity"], 17)
        self.assertEqual(info["config"]["threads"], 8)
        self.assertEqual(info["execution"]["threads_used"], 1)
        self.assertGreater(info["execution"]["estimated_working_bytes"], 0)
        self.assertGreaterEqual(info["execution"]["hardware_threads"], 0)
        self.assertGreaterEqual(info["timing"]["elapsed_seconds"], 0)
        self.assertLessEqual(info["started_at_utc"], info["finished_at_utc"])
        self.assertEqual(info["program"]["cpp_standard"], 20)
        self.assertRegex(info["program"]["version"], r"^\d+\.\d+\.\d+")
        self.assertTrue(info["build"]["configuration"])
        self.assertTrue(info["build"]["compiler"]["id"])
        self.assertTrue(info["build"]["compiler"]["version"])
        self.assertEqual({entry["name"] for entry in info["files"]},
                         {"collatz_results.csv", "collatz_summary.json", "collatz_coral.csv"})

    def test_failed_run_preserves_previous_set(self):
        destination = self.root / "shared"
        first, _ = self.require_success(output_directory=destination, coral_n_list=[3])
        snapshot = {path.name: path.read_bytes() for path in first.iterdir()}
        failed, _, _ = self.run_app(output_directory=destination, n_max=500,
                                    coral_n_list=[6148914691236517205])
        self.assertEqual(failed.returncode, 1)
        self.assertIn("3n+1", failed.stderr)
        self.assertNotIn("Processed", failed.stdout)
        self.assertEqual(resolve_directory(destination), first)
        self.assertEqual({path.name: path.read_bytes() for path in first.iterdir()}, snapshot)
        self.assertEqual(list((destination / "collatz_runs").iterdir()), [first])
        second, _ = self.require_success(output_directory=destination, n_max=200, write_csv=False)
        self.assertNotEqual(first, second)
        self.assertEqual(resolve_directory(destination), second)
        self.assertFalse((second / "collatz_results.csv").exists())
        self.assertFalse((second / "collatz_coral.csv").exists())
        self.assertEqual({path.name: path.read_bytes() for path in first.iterdir()}, snapshot)

    def test_output_path_error_preserves_existing_file(self):
        destination = self.root / "ordinary-file"
        destination.write_text("preserve this")
        failed, _, _ = self.run_app(output_directory=destination)
        self.assertEqual(failed.returncode, 1)
        self.assertNotIn("Processed", failed.stdout)
        self.assertEqual(destination.read_text(), "preserve this")

    def test_concurrent_runs_publish_separate_sets(self):
        destination = self.root / "shared"
        processes = []
        try:
            for n in [71, 103]:
                config = self.root / f"parallel-{n}.json"
                config.write_text(json.dumps(dict(n_max=n, bases=[2, 10], threads=2,
                                                  write_csv=False, coral_n_list=[3])))
                processes.append(subprocess.Popen([str(BINARY), str(config), str(destination)],
                                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True))
            for process in processes:
                stdout, stderr = process.communicate(timeout=60)
                self.assertEqual(process.returncode, 0, stderr)
                self.assertIn("Processed", stdout)
        finally:
            for process in processes:
                if process.poll() is None:
                    process.kill()
                    process.communicate()
        runs = list((destination / "collatz_runs").iterdir())
        self.assertEqual(len(runs), 2)
        observed = set()
        for run in runs:
            info = load_run_info(run)
            summary = json.loads((run / "collatz_summary.json").read_text())
            self.assertEqual(summary["n_max"], info["config"]["n_max"])
            observed.add(summary["n_max"])
        self.assertEqual(observed, {71, 103})


if __name__ == "__main__":
    unittest.main()
