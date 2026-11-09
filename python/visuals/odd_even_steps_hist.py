"""Гистограммы нечётных и чётных шагов из CSV/JSON на общих осях.
Для CSV доступен выбор числа интервалов."""

from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

from data_source import open_data_source
from plot_cli import run_stat_plot
from plot_style import BLUE, DARK_BLUE, FILL_ALPHA, apply_grid
from plot_ui import create_figure, slider_control, finish_figure

DEFAULT_BINS = 50
MIN_BINS = 5
MAX_BINS = 200


def load_steps(csv_path: Path) -> tuple[np.ndarray, np.ndarray]:
    return open_data_source(csv_path).columns("odd_steps", "even_steps")


def build_figure(odd_steps: np.ndarray, even_steps: np.ndarray, *, show: bool = False) -> plt.Figure:
    fig, (ax,) = create_figure("Распределение числа нечётных и чётных шагов",
                               layout=dict(left=0.12, bottom=0.25))

    state = {"bins": DEFAULT_BINS}

    def redraw() -> None:
        ax.clear()
        bins = state["bins"]
        bin_edges = np.histogram_bin_edges(np.r_[odd_steps, even_steps], bins=bins)

        ax.hist(
            odd_steps,
            bins=bin_edges,
            alpha=FILL_ALPHA,
            color=BLUE,
            edgecolor="white",
            label=r"Нечётные шаги ($3n+1$)",
        )
        ax.hist(
            even_steps,
            bins=bin_edges,
            alpha=FILL_ALPHA,
            color=DARK_BLUE,
            edgecolor="white",
            label=r"Чётные шаги ($n/2$)",
        )
        ax.set_xlabel(r"Количество шагов $x$")
        ax.set_ylabel(r"Частота $n_i$")
        ax.set_title("Распределение числа нечётных и чётных шагов")
        ax.legend(loc="upper right")
        apply_grid(ax)
        fig.canvas.draw_idle()

    def on_bins_change(val: float) -> None:
        state["bins"] = int(val)
        redraw()

    bins_slider = slider_control(fig, [0.3, 0.1, 0.6, 0.03], on_bins_change,
                                 minimum=MIN_BINS, maximum=MAX_BINS, value=DEFAULT_BINS)
    redraw()
    return finish_figure(fig, (bins_slider,), show=show)


def main() -> None:
    run_stat_plot(lambda source: build_figure(*source.columns("odd_steps", "even_steps")),
                  ("odd_steps", "even_steps"))


if __name__ == "__main__":
    main()
