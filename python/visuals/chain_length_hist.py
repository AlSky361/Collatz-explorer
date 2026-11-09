"""Гистограмма длин цепочек из CSV или JSON; CSV позволяет менять число интервалов."""

from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

from data_source import open_data_source
from plot_cli import run_stat_plot
from plot_style import BLUE, FILL_ALPHA, apply_grid
from plot_ui import create_figure, slider_control, radio_selector, finish_figure

DEFAULT_BINS = 50
MIN_BINS = 5
MAX_BINS = 200


def load_lengths(csv_path: Path) -> np.ndarray:
    return open_data_source(csv_path).columns("length")[0]


def build_figure(lengths: np.ndarray, *, show: bool = False) -> plt.Figure:
    fig, (ax,) = create_figure("Распределение длин цепочек Коллатца",
                               layout=dict(left=0.3, bottom=0.25))

    state = {"bins": DEFAULT_BINS, "yscale": "linear"}

    def redraw() -> None:
        ax.clear()
        bins = state["bins"]
        bin_edges = bins

        ax.hist(lengths, bins=bin_edges, color=BLUE, alpha=FILL_ALPHA, edgecolor="white")
        ax.set_yscale(state["yscale"])
        ax.set_xlabel(r"Длина цепочки $x$ (число шагов до 1)")
        ax.set_ylabel(r"Частота $n_i$")
        ax.set_title("Гистограмма частот длин цепочек")
        apply_grid(ax)
        fig.canvas.draw_idle()

    def on_bins_change(val: float) -> None:
        state["bins"] = int(val)
        redraw()

    def on_yscale_change(label: str) -> None:
        state["yscale"] = label
        redraw()

    bins_slider = slider_control(fig, [0.3, 0.1, 0.6, 0.03], on_bins_change,
                                 minimum=MIN_BINS, maximum=MAX_BINS, value=DEFAULT_BINS)
    radio_y = radio_selector(fig, [0.02, 0.45, 0.18, 0.15], ("linear", "log"),
                             on_yscale_change, title="Ось Y")
    redraw()
    return finish_figure(fig, (bins_slider, radio_y), show=show)


def main() -> None:
    run_stat_plot(lambda source: build_figure(source.columns("length")[0]), ("length",))


if __name__ == "__main__":
    main()
