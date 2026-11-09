"""Compare executables on identical inputs without Git; report timings and child-process RSS."""

import argparse
import hashlib
import json
from pathlib import Path
import platform
import statistics
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python" / "visuals"))
from result_paths import resolve_directory


def digest_outputs(directory):
    hashes = {}
    for path in sorted(resolve_directory(directory).glob("collatz_*")):
        if not path.is_file():
            continue
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(block)
        hashes[path.name] = digest.hexdigest()
    if not hashes:
        raise AssertionError(f"No data files produced in {directory}")
    return hashes


def measure_child(binary, config, output):
    import resource
    start = time.perf_counter()
    result = subprocess.run([binary, config, output], capture_output=True, text=True, timeout=180)
    seconds = time.perf_counter() - start
    if result.returncode:
        raise RuntimeError(result.stderr)
    rss = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
    return dict(seconds=seconds, peak_rss_mib=rss / (1024**2 if sys.platform == "darwin" else 1024))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--n-max", type=int, default=1000000)
    parser.add_argument("--no-csv", action="store_true")
    parser.add_argument("--cases", nargs="+", choices=["one_thread", "four_threads", "auto_threads", "large_cache", "no_cache", "csv"],
                        help="Run only the selected scenarios")
    args = parser.parse_args()
    if args.repeats < 1:
        parser.error("--repeats must be positive")
    binaries = dict(baseline=args.baseline.resolve(), candidate=args.candidate.resolve())
    project_root = Path(__file__).resolve().parents[1]
    report = dict(platform=platform.platform(), python=platform.python_version(),
                  executables={key: dict(path=path.relative_to(project_root).as_posix()
                                         if path.is_relative_to(project_root) else path.name,
                                         sha256=hashlib.sha256(path.read_bytes()).hexdigest())
                               for key, path in binaries.items()}, cases=[])
    base = dict(n_max=args.n_max, bases=[2, 8, 10, 16], batch_size=10000,
                memo_capacity=min(100000, args.n_max), threads=1, write_csv=False,
                summary_bins=100, memory_budget_mb=256)
    cases = [("one_thread", {}), ("four_threads", dict(threads=4)),
             ("auto_threads", dict(threads=0)),
             ("large_cache", dict(memo_capacity=args.n_max)),
             ("no_cache", dict(memo_capacity=0, threads=4, n_max=min(args.n_max, 100000)))]
    if not args.no_csv:
        cases.append(("csv", dict(write_csv=True, threads=4)))
    if args.cases:
        cases = [(name, overrides) for name, overrides in cases if name in args.cases]
    if not cases:
        parser.error("No scenarios selected")
    with tempfile.TemporaryDirectory(prefix="collatz-benchmark-") as temp:
        root = Path(temp)
        for name, overrides in cases:
            config = base | overrides
            cfg = root / "config.json"
            cfg.write_text(json.dumps(config))
            measurements = {key: [] for key in binaries}
            outputs = {}
            for repeat in range(args.repeats):
                order = list(binaries) if repeat % 2 == 0 else list(reversed(binaries))
                for key in order:
                    output = root / f"{name}-{key}"
                    command = [sys.executable, str(Path(__file__).resolve()), "--child",
                               str(binaries[key]), str(cfg), str(output)]
                    result = subprocess.run(command, capture_output=True, text=True, check=True, timeout=200)
                    measurements[key].append(json.loads(result.stdout))
                    hashes = digest_outputs(output)
                    if key in outputs and outputs[key] != hashes:
                        raise AssertionError(f"Nondeterministic output: {name}, {key}, repeat {repeat}")
                    outputs[key] = hashes
            if outputs["baseline"] != outputs["candidate"]:
                raise AssertionError(f"Output mismatch: {name}")
            case = dict(name=name, config=config, output_sha256=outputs["candidate"], measurements=measurements)
            for key, samples in measurements.items():
                case[key] = {metric: statistics.median(sample[metric] for sample in samples)
                             for metric in ("seconds", "peak_rss_mib")}
            report["cases"].append(case)
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(report, indent=2) + "\n")
            print(json.dumps({k: case[k] for k in ("name", "baseline", "candidate")}), flush=True)


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--child":
        print(json.dumps(measure_child(*sys.argv[2:])))
    else:
        main()
