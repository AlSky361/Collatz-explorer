"""Time diagnostic phases; CSV stages are serial, unlike the production pipeline."""

import argparse
import hashlib
import json
from pathlib import Path
import platform
import statistics
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binaries", nargs="+", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--repeats", default=3, type=int)
    args = parser.parse_args()
    if args.repeats < 1:
        parser.error("--repeats must be positive")
    binaries = [binary.resolve() for binary in args.binaries]
    project_root = Path(__file__).resolve().parents[1]
    report = dict(platform=platform.platform(), threads=4, cases=[],
                  settings=dict(bases=[2, 8, 10, 16], memo_capacity=100000, batch_size=10000,
                                summary_bins=100, diagnostic_csv_pipeline="serial"),
                  executables=[dict(path=binary.relative_to(project_root).as_posix()
                                    if binary.is_relative_to(project_root) else binary.name,
                                    sha256=hashlib.sha256(binary.read_bytes()).hexdigest())
                               for binary in binaries])
    with tempfile.TemporaryDirectory(prefix="collatz-phases-") as temp:
        for count in (1000000, 10000000):
            samples = [[] for _ in binaries]
            for repeat in range(args.repeats):
                order = range(len(binaries)) if repeat % 2 == 0 else reversed(range(len(binaries)))
                for index in order:
                    result = subprocess.run([str(binaries[index]), str(count), "4", str(Path(temp) / "data.csv")],
                                            check=True, capture_output=True, text=True, timeout=180)
                    samples[index].append(json.loads(result.stdout))
            case = dict(n_max=count, samples=samples,
                        medians=[{key: statistics.median(sample[key] for sample in group)
                                  for key in group[0]} for group in samples])
            report["cases"].append(case)
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(report, indent=2) + "\n")
            print(json.dumps(case | dict(samples="see report")), flush=True)


if __name__ == "__main__":
    main()
