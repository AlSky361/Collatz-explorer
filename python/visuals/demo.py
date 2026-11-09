"""Open the local Collatz gallery or export its figures without a GUI."""

import argparse
from pathlib import Path

from result_paths import resolve_directory


def main() -> None:
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input-dir", type=Path, default=root / "output")
    parser.add_argument("--save-dir", type=Path, help="Save PNG figures to this directory")
    parser.add_argument("--no-show", action="store_true", help="Export without opening windows")
    args = parser.parse_args()
    if args.no_show and args.save_dir is None:
        parser.error("--no-show requires --save-dir")
    try:
        args.input_dir = resolve_directory(args.input_dir)
    except ValueError as error:
        parser.error(str(error))
    summary_path = args.input_dir / "collatz_summary.json"
    coral_path = args.input_dir / "collatz_coral.csv"
    if not summary_path.is_file():
        parser.error(f"Missing {summary_path}. Run ./build.sh config.json output first.")

    import matplotlib
    if args.no_show:
        matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from benford_digits import build_figure as build_benford
    from coral_trajectories import build_figure as build_coral, load_trajectories
    from data_source import open_data_source
    from summary_view import show_histograms

    source = open_data_source(summary_path)
    data = source.summary
    figures = {
        "chain_lengths": show_histograms(data, ("length",), distribution=True, show=False),
        "odd_even_steps": show_histograms(data, ("odd_steps", "even_steps"), show=False),
        "max_values": show_histograms(data, ("max_value",), distribution=True, show=False),
        "benford_digits": build_benford(source, show=False),
    }
    if coral_path.is_file():
        figures["coral_trajectories"] = build_coral(load_trajectories(coral_path), show=False)
    titles = {
        "chain_lengths": "Распределение длин цепочек Коллатца",
        "odd_even_steps": "Распределение числа нечётных и чётных шагов",
        "max_values": "Распределение максимумов траекторий Коллатца",
        "benford_digits": "Распределение первых цифр",
        "coral_trajectories": "Траектории Коллатца",
    }
    for name, figure in figures.items():
        figure.canvas.manager.set_window_title(f"Collatz — {titles[name]}")
        if args.save_dir is not None:
            args.save_dir.mkdir(parents=True, exist_ok=True)
            target = args.save_dir / f"{name}.png"
            figure.savefig(target, dpi=160, facecolor="white")
            print(f"Saved {target}", flush=True)
    print(f"Prepared {len(figures)} figures for {data['n_max']:,} starting values", flush=True)
    if args.no_show:
        plt.close("all")
    else:
        plt.show()


if __name__ == "__main__":
    main()
