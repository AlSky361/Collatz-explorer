"""Read pinned CSV/JSON results independently of plotting and command-line state."""

import json
from pathlib import Path
import re

import numpy as np

from result_paths import load_run_info, resolve_directory, resolve_file

DEFAULT_CSV = Path(__file__).resolve().parents[2] / "collatz_results.csv"
METRICS = ("length", "max_value", "odd_steps", "even_steps")
FD_COLUMN_RE = re.compile(r"^fd_b(\d+)_d(\d+)$")


def resolve_input(default_csv: Path = DEFAULT_CSV, supplied=None) -> Path:
    """Resolve a file/directory or prefer a fresh summary; explicit files stay pinned.
    Completed-set validation is delegated to result_paths."""
    default_csv = Path(default_csv)
    if supplied is not None:
        supplied = Path(supplied)
        if not supplied.is_dir():
            return resolve_file(supplied)
        default_csv = resolve_directory(supplied) / default_csv.name
    else:
        default_csv = resolve_directory(default_csv.parent) / default_csv.name
    summary = default_csv.with_name("collatz_summary.json")
    if (default_csv.parent / "run_info.json").exists():
        names = {entry["name"] for entry in load_run_info(default_csv.parent)["files"]}
        return summary if summary.name in names else default_csv
    if summary.exists() and (
        not default_csv.exists() or summary.stat().st_mtime_ns >= default_csv.stat().st_mtime_ns
    ):
        return summary
    return default_csv


def load_summary(path: Path) -> dict:
    path = resolve_file(path)
    with path.open(encoding="utf-8") as stream:
        data = json.load(stream)
    if data.get("schema_version") != 2:
        raise ValueError("Требуется сводка версии 2; пересоздайте её текущим приложением")
    n_max = data.get("n_max")
    if type(n_max) is not int or n_max <= 0:
        raise ValueError("В сводке отсутствует положительное целое n_max")
    for metric in METRICS:
        hist = data["histograms"][metric]
        edges = np.asarray(hist["edges"], dtype=float)
        counts = hist["counts"]
        if (
            len(edges) != len(counts) + 1
            or not counts
            or not np.all(np.isfinite(edges))
            or not np.all(np.diff(edges) > 0)
            or any(type(x) is not int or x < 0 for x in counts)
            or sum(counts) != n_max
        ):
            raise ValueError(f"Некорректная гистограмма {metric}")
    for base in data["bases"]:
        counts = data["first_digit_counts"][str(base)]
        if len(counts) != base or any(type(x) is not int or x < 0 for x in counts):
            raise ValueError(f"Некорректные счётчики цифр для основания {base}")
    return data


class PlotDataSource:
    """Pinned CSV/JSON source with lazy columns and cached digit totals.
    JSON uses ready summaries without CSV; raw samples and fitted models require CSV."""

    def __init__(self, path: Path):
        self.path = Path(path)
        self.summary = load_summary(self.path) if self.path.suffix.lower() == ".json" else None
        self._columns = {}
        self._base_columns = None
        self._digit_counts = {}

    @property
    def is_summary(self) -> bool:
        return self.summary is not None

    def columns(self, *names: str) -> tuple[np.ndarray, ...]:
        """Load only requested raw CSV columns; reuse previously loaded arrays."""
        if self.is_summary:
            raise ValueError("Сводка не содержит индивидуальные строки CSV")
        missing = [name for name in names if name not in self._columns]
        if missing:
            import pandas as pd
            frame = pd.read_csv(self.path, usecols=missing)
            self._columns.update({name: frame[name].to_numpy() for name in missing})
        return tuple(self._columns[name] for name in names)

    def _discover_bases(self):
        if self._base_columns is None:
            import pandas as pd
            groups = {}
            for name in pd.read_csv(self.path, nrows=0).columns:
                match = FD_COLUMN_RE.fullmatch(name)
                if match:
                    base, digit = map(int, match.groups())
                    groups.setdefault(base, []).append((digit, name))
            self._base_columns = {
                base: sorted(columns) for base, columns in sorted(groups.items())
            }
        return self._base_columns

    @property
    def bases(self) -> list[int]:
        return list(self.summary["bases"]) if self.is_summary else list(self._discover_bases())

    def first_digit_counts(self, base: int) -> tuple[np.ndarray, np.ndarray]:
        """Return positive leading digits and totals, cached across redraws."""
        if base not in self._digit_counts:
            if self.is_summary:
                digits = np.arange(1, base)
                counts = np.asarray(self.summary["first_digit_counts"][str(base)][1:])
            else:
                import pandas as pd
                columns = [(digit, name) for digit, name in self._discover_bases()[base]
                           if digit > 0]
                names = [name for _, name in columns]
                totals = pd.read_csv(self.path, usecols=names).sum()
                digits = np.asarray([digit for digit, _ in columns])
                counts = np.asarray([totals[name] for name in names])
            self._digit_counts[base] = digits, counts
        return self._digit_counts[base]


def open_data_source(path=None, *, default_csv: Path = DEFAULT_CSV) -> PlotDataSource:
    """Select and validate one source; an already opened source is reused."""
    if isinstance(path, PlotDataSource):
        return path
    selected = resolve_input(default_csv, path)
    if not selected.is_file():
        raise FileNotFoundError(f"Файл не найден: {selected}")
    return PlotDataSource(selected)
