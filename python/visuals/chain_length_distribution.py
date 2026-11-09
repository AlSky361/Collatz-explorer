"""Нормированная гистограмма длин n_i/(N*h_i) и выборочная F*_X(x)=#{x_k<x}/N.
CSV позволяет менять интервалы и показывать эвристическую модель, подогнанную по этим же данным."""

from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

import collatz_theory as ct

from data_source import open_data_source
from plot_cli import run_stat_plot
from empirical import draw_empirical_cdf
from plot_style import BLUE, DARK_BLUE, FILL_ALPHA, apply_grid
from plot_ui import create_figure, slider_control, radio_selector, check_toggle, finish_figure

MIN_BINS = 5
MAX_BINS = 300  # Keep the number of intervals readable at normal window sizes.


def load_n_and_lengths(csv_path: Path) -> tuple[np.ndarray, np.ndarray]:
    return open_data_source(csv_path).columns("n", "length")


def build_figure(n_values: np.ndarray, lengths: np.ndarray, *, show: bool = False) -> plt.Figure:
    fig, (ax_pdf, ax_cdf) = create_figure(
        "Распределение длин цепочек Коллатца", rows=2, figsize=(9, 9),
        layout=dict(left=0.3, right=0.96, top=0.87, bottom=0.16, hspace=0.52),
        heading=True, heading_y=0.98)

    default_bins = ct.freedman_diaconis_bins(lengths, MIN_BINS, MAX_BINS)
    state = {
        "bins": default_bins,
        "yscale": "linear",
        "show_theory": True,
    }

    # Калибровка и теоретическая смесь считаются один раз — не зависят от
    # bins/scale виджетов, только от самих данных.
    model = ct.calibrate_length_model(n_values, lengths)
    theory_x = np.linspace(max(lengths.min(), 1e-6), lengths.max(), 500)
    theory_pdf = ct.theoretical_length_pdf(theory_x, n_values, model)
    theory_cdf = ct.theoretical_length_cdf(theory_x, n_values, model)

    def redraw() -> None:
        ax_pdf.clear()
        ax_cdf.clear()
        bins = state["bins"]

        bin_edges = bins

        # --- PDF ---
        ax_pdf.hist(
            lengths,
            bins=bin_edges,
            density=True,
            color=BLUE,
            edgecolor="white",
            alpha=FILL_ALPHA,
            label="Гистограмма относительных частот",
        )
        if state["show_theory"]:
            ax_pdf.plot(
                theory_x, theory_pdf, color=DARK_BLUE, linewidth=2, linestyle="--",
                label="Калиброванная эвристическая смесь",
            )
        ax_pdf.set_yscale(state["yscale"])
        ax_pdf.set_xlabel(r"Длина цепочки $x$ (число шагов до 1)")
        ax_pdf.set_ylabel(r"$n_i/(N h_i)$, $f_X(x)$")
        ax_pdf.set_title("Гистограмма относительных частот и модель", fontsize=11)
        ax_pdf.legend(loc="upper right", fontsize=8)
        apply_grid(ax_pdf)

        # --- CDF ---
        draw_empirical_cdf(ax_cdf, lengths, color=BLUE, label=r"$F_X^*(x)$")
        if state["show_theory"]:
            ax_cdf.plot(
                theory_x, theory_cdf, color=DARK_BLUE, linewidth=2,
                linestyle="--", label=r"Модель $F_X(x)$ (эвристика)",
            )
        ax_cdf.set_xlabel(r"Длина цепочки $x$ (число шагов до 1)")
        ax_cdf.set_ylabel(r"$F_X^*(x)$, $F_X(x)$")
        ax_cdf.set_title("Выборочная функция распределения длин", fontsize=11)
        ax_cdf.legend(loc="lower right", fontsize=8)
        apply_grid(ax_cdf)

        fig.canvas.draw_idle()

    def on_bins_change(val: float) -> None:
        state["bins"] = int(val)
        redraw()

    def on_yscale_change(label: str) -> None:
        state["yscale"] = label
        redraw()

    def on_theory_toggle(_label: str) -> None:
        state["show_theory"] = not state["show_theory"]
        redraw()

    bins_slider = slider_control(fig, [0.04, 0.79, 0.16, 0.025], on_bins_change,
                                 minimum=MIN_BINS, maximum=MAX_BINS, value=default_bins,
                                 label="", title="Интервалы", reference=True)
    radio_y = radio_selector(fig, [0.02, 0.52, 0.2, 0.15], ("linear", "log"),
                             on_yscale_change, title="Ось Y (гистограмма)")
    check_theory = check_toggle(fig, [0.02, 0.32, 0.2, 0.1], "Модель", True, on_theory_toggle)
    redraw()
    return finish_figure(fig, (bins_slider, radio_y, check_theory), show=show)


def main() -> None:
    run_stat_plot(lambda source: build_figure(*source.columns("n", "length")),
                  ("length",), distribution=True)


if __name__ == "__main__":
    main()
