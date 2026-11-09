"""Load bounded summaries and draw precomputed histograms without reading CSV."""


import matplotlib.pyplot as plt
import numpy as np

from empirical import draw_empirical_cdf
from plot_style import BLUE, DARK_BLUE, FILL_ALPHA, apply_grid
from plot_ui import create_figure, radio_selector, check_toggle, finish_figure

LABELS = {
    "length": "Длина цепочки",
    "max_value": "Максимальное значение",
    "odd_steps": "Нечётные шаги",
    "even_steps": "Чётные шаги",
}


def show_maxima(
    edges: np.ndarray, counts: np.ndarray, n_max: int, *,
    samples: np.ndarray | None = None,
    show: bool = False,
) -> plt.Figure:
    """Interval relative frequencies and their cumulative sums, or exact ECDF."""
    edges = np.asarray(edges, dtype=float)
    counts = np.asarray(counts, dtype=float)
    occupied = np.flatnonzero(counts)
    lo, hi = edges[occupied[0]], edges[occupied[-1] + 1]
    title = "Распределение максимумов траекторий Коллатца"
    fig, (ax, cdf_ax) = create_figure(title, rows=2, figsize=(10, 8), heading=True,
                                     layout=dict(left=0.12, right=0.96, top=0.87,
                                                 bottom=0.10, hspace=0.52))
    ax.stairs(counts / n_max, edges, fill=True, color=BLUE, alpha=FILL_ALPHA)
    ax.set_title("Гистограмма относительных частот максимумов", fontsize=11)
    ax.set_ylabel(r"Относительная частота $n_i/N$")
    ax.set_xlabel(r"Максимум траектории $x$")
    if samples is None:
        # Grouped data determine boundary totals, not individual jumps inside bins.
        cdf_ax.plot(edges, np.r_[0, np.cumsum(counts) / n_max],
                    color=BLUE, alpha=0.85, linestyle="--", linewidth=1.8)
        cumulative_title = "Приближённая функция распределения максимумов"
    else:
        draw_empirical_cdf(cdf_ax, samples, color=BLUE, log_x=True, alpha=0.85)
        cumulative_title = "Выборочная функция распределения максимумов"
    cdf_ax.set_title(cumulative_title, fontsize=11)
    cdf_ax.set_ylabel(r"$F_X^*(x)$")
    cdf_ax.set_xlabel(r"Максимум траектории $x$")
    cdf_ax.set_ylim(0, 1.03)
    for axis in (ax, cdf_ax):
        axis.set_xscale("log")
        axis.set_xlim(lo, hi)
        apply_grid(axis)
    return finish_figure(fig, show=show)


def show_histograms(
    data: dict, metrics: tuple[str, ...], distribution: bool = False, *, show: bool = False
) -> plt.Figure:
    if metrics == ("max_value",):
        hist = data["histograms"]["max_value"]
        return show_maxima(hist["edges"], hist["counts"], data["n_max"], show=show)
    rows = 2 if distribution else 1
    is_steps = metrics == ("odd_steps", "even_steps")
    title = ("Распределение числа нечётных и чётных шагов"
             if is_steps else "Распределение длин цепочек Коллатца")
    fig, axes = create_figure(title, rows=rows, figsize=(10, 8 if distribution else 6), heading=True,
                              layout=dict(left=0.29, right=0.96, top=0.87, bottom=0.12, hspace=0.52))
    ax = axes[0]
    state = {"density": distribution, "yscale": "linear"}

    def redraw() -> None:
        ax.clear()
        visible_lo, visible_hi = float("inf"), float("-inf")
        for index, metric in enumerate(metrics):
            hist = data["histograms"][metric]
            edges = np.asarray(hist["edges"], dtype=float)
            counts = np.asarray(hist["counts"], dtype=float)
            heights = counts / (data["n_max"] * np.diff(edges)) if state["density"] else counts
            ax.stairs(heights, edges, label=LABELS[metric], linewidth=1.8,
                      color=BLUE if index == 0 else DARK_BLUE,
                      linestyle="-" if index == 0 else "--", fill=not is_steps,
                      alpha=FILL_ALPHA if not is_steps else 0.85)
            occupied = np.flatnonzero(counts)
            visible_lo = min(visible_lo, edges[occupied[0]])
            visible_hi = max(visible_hi, edges[occupied[-1] + 1])
        ax.set_xlim(visible_lo, visible_hi)
        ax.set_yscale(state["yscale"])
        ax.set_xlabel((LABELS[metrics[0]] if len(metrics) == 1 else "Количество шагов") + r" $x$")
        ax.set_ylabel(r"$n_i/(N h_i)$" if state["density"] else r"Частота $n_i$")
        ax.set_title("Гистограмма относительных частот" if state["density"] else "Гистограмма частот")
        apply_grid(ax)
        ax.legend()
        if distribution:
            cdf_ax = axes[1]
            cdf_ax.clear()
            frequency = np.asarray(data["length_counts"], dtype=float)
            draw_empirical_cdf(cdf_ax, np.arange(len(frequency)), frequency,
                               color=BLUE)
            cdf_ax.set_title("Выборочная функция распределения длин", fontsize=11)
            cdf_ax.set_xlabel(r"Длина цепочки $x$")
            cdf_ax.set_ylabel(r"$F_X^*(x)$")
            cdf_ax.set_ylim(0, 1.02)
            apply_grid(cdf_ax)
        fig.canvas.draw_idle()

    widgets = []
    if not is_steps:
        def change_y(label: str) -> None:
            state["yscale"] = label
            redraw()

        radio_y = radio_selector(fig, [0.02, 0.63, 0.19, 0.13], ("linear", "log"),
                                 change_y, title="Ось Y")
        widgets.append(radio_y)
    def change_density(_label: str) -> None:
        state["density"] = not state["density"]
        redraw()

    density = check_toggle(fig, [0.02, 0.43, 0.19, 0.1], "Нормировка", state["density"], change_density)
    widgets.append(density)
    redraw()
    return finish_figure(fig, widgets, show=show)


def log_edges(positive: np.ndarray, bins: int) -> np.ndarray:
    """Non-degenerate logarithmic edges for small or constant CSV datasets."""
    if positive.size == 0:
        return np.geomspace(0.5, 1.5, bins + 1)
    lo, hi = float(positive.min()), float(positive.max())
    if hi <= lo:
        lo, hi = lo / 2, hi * 2
    return np.geomspace(lo, hi, bins + 1)
