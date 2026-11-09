"""Resolve completed, immutable output sets; also accept legacy data directories."""

from datetime import datetime
import json
from pathlib import Path
import re

RUN_ID = re.compile(r"run-\d+-[0-9a-f]{16}\Z")
FINISHED_AT = re.compile(r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{9}Z\Z")
DATA_FILES = {"collatz_results.csv", "collatz_summary.json", "collatz_coral.csv"}


def _reject_pending(directory: Path) -> None:
    if directory.name.startswith(".pending-") and RUN_ID.fullmatch(directory.name[9:]):
        raise ValueError(f"Расчёт ещё не опубликован: {directory}")


def load_run_info(directory: Path) -> dict:
    """Validate a completed passport and the names/sizes of its data files."""
    directory = Path(directory)
    _reject_pending(directory)
    path = directory / "run_info.json"
    try:
        info = json.loads(path.read_text(encoding="utf-8"))
        if (not isinstance(info, dict) or type(info.get("schema_version")) is not int
                or info["schema_version"] != 1 or info.get("status") != "complete"):
            raise ValueError("требуется завершённый запуск версии 1")
        if not isinstance(info.get("run_id"), str) or not RUN_ID.fullmatch(info["run_id"]):
            raise ValueError("некорректный run_id")
        timestamp = info.get("finished_at_utc")
        if not isinstance(timestamp, str) or not FINISHED_AT.fullmatch(timestamp):
            raise ValueError("некорректное время завершения")
        datetime.strptime(timestamp[:19], "%Y-%m-%dT%H:%M:%S")
        files = info.get("files")
        if not isinstance(files, list) or not files:
            raise ValueError("отсутствует список результатов")
        names = set()
        for entry in files:
            if not isinstance(entry, dict) or entry.get("name") not in DATA_FILES:
                raise ValueError("недопустимое имя файла")
            name, size = entry["name"], entry.get("size_bytes")
            if name in names or type(size) is not int or size <= 0:
                raise ValueError("повторное имя или некорректный размер файла")
            names.add(name)
            if not (directory / name).is_file() or (directory / name).stat().st_size != size:
                raise ValueError(f"файл {name} отсутствует или его размер изменён")
        return info
    except (OSError, ValueError, TypeError) as error:
        raise ValueError(f"Некорректный набор результатов {path}: {error}") from error


def resolve_directory(directory: Path) -> Path:
    """Pin the latest completed set, an explicitly selected run, or a legacy dir."""
    directory = Path(directory)
    _reject_pending(directory)
    if (directory / "run_info.json").exists():
        load_run_info(directory)
        return directory
    container = directory / "collatz_runs"
    if not container.exists():
        return directory
    if not container.is_dir():
        raise ValueError(f"Ожидался каталог результатов: {container}")
    completed = []
    for candidate in container.iterdir():
        if not RUN_ID.fullmatch(candidate.name) or not candidate.is_dir():
            continue
        if not (candidate / "run_info.json").is_file():
            continue
        info = load_run_info(candidate)
        if info["run_id"] != candidate.name:
            raise ValueError(f"run_id не соответствует каталогу {candidate}")
        completed.append((info["finished_at_utc"], info["run_id"], candidate))
    return max(completed)[2] if completed else directory


def resolve_file(path: Path) -> Path:
    """Keep explicit existing files pinned; resolve missing logical output paths."""
    path = Path(path)
    _reject_pending(path.parent)
    if path.is_file():
        if (path.parent / "run_info.json").exists():
            info = load_run_info(path.parent)
            if path.name in DATA_FILES and path.name not in {entry["name"] for entry in info["files"]}:
                raise ValueError(f"Файл не входит в этот запуск: {path}")
        return path
    directory = resolve_directory(path.parent)
    if (directory / "run_info.json").exists():
        info = load_run_info(directory)
        if path.name in DATA_FILES and path.name not in {entry["name"] for entry in info["files"]}:
            raise FileNotFoundError(f"Этот запуск не создавал {path.name}: {directory}")
    return directory / path.name
