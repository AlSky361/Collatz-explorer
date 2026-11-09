"""Completed-run selection, immutable paths and legacy directory compatibility."""

import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "python" / "visuals"))
from result_paths import load_run_info, resolve_directory, resolve_file


class ResultPathTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="collatz-paths-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def dataset(self, number, *, pending=False, files=None):
        run_id = f"run-{number}-0000000000000000"
        directory = self.root / "collatz_runs" / ((".pending-" if pending else "") + run_id)
        directory.mkdir(parents=True)
        names = files if files is not None else ["collatz_summary.json"]
        entries = []
        for name in names:
            (directory / name).write_text("sample data\n")
            entries.append(dict(name=name, size_bytes=(directory / name).stat().st_size))
        info = dict(schema_version=1, status="complete", run_id=run_id,
                    finished_at_utc=f"2026-11-08T12:00:{number:02d}.000000000Z", files=entries)
        (directory / "run_info.json").write_text(json.dumps(info))
        return directory

    def test_latest_complete_and_pinned_selection(self):
        first = self.dataset(1)
        second = self.dataset(2)
        pending = self.dataset(3, pending=True)
        (self.root / "collatz_runs" / "run-4-0000000000000000").mkdir()
        self.assertEqual(resolve_directory(self.root), second)
        self.assertEqual(resolve_directory(first), first)
        self.assertEqual(resolve_file(first / "collatz_summary.json"), first / "collatz_summary.json")
        self.assertEqual(resolve_file(self.root / "collatz_summary.json"), second / "collatz_summary.json")
        with self.assertRaises(ValueError):
            resolve_directory(pending)
        with self.assertRaises(ValueError):
            resolve_file(pending / "collatz_summary.json")

    def test_legacy_files_and_pending_only_directory(self):
        legacy = self.root / "collatz_summary.json"
        legacy.write_text("legacy")
        self.dataset(1, pending=True)
        self.assertEqual(resolve_directory(self.root), self.root)
        self.assertEqual(resolve_file(legacy), legacy)

    def test_disabled_outputs_do_not_come_from_previous_run(self):
        self.dataset(1, files=["collatz_results.csv", "collatz_summary.json"])
        second = self.dataset(2)
        self.assertEqual(resolve_directory(self.root), second)
        with self.assertRaises(FileNotFoundError):
            resolve_file(self.root / "collatz_results.csv")
        (second / "collatz_results.csv").write_text("unlisted stale CSV")
        with self.assertRaises(ValueError):
            resolve_file(second / "collatz_results.csv")

    def test_changed_or_missing_data_is_rejected(self):
        self.dataset(1)
        second = self.dataset(2)
        (second / "collatz_summary.json").write_text("changed to a different file length")
        with self.assertRaises(ValueError):
            resolve_directory(self.root)

    def test_invalid_passports_and_file_names(self):
        directory = self.dataset(1)
        path = directory / "run_info.json"
        original = json.loads(path.read_text())
        for replacement in [dict(schema_version=True), dict(status="pending"),
                            dict(finished_at_utc="2026-02-30T00:00:00.000000000Z"),
                            dict(files=[dict(name="../outside", size_bytes=1)]),
                            dict(files=original["files"] * 2), dict(files=[])]:
            path.write_text(json.dumps(original | replacement))
            with self.subTest(replacement=replacement), self.assertRaises(ValueError):
                load_run_info(directory)
        path.write_text("{")
        with self.assertRaises(ValueError):
            resolve_directory(self.root)


if __name__ == "__main__":
    unittest.main()
