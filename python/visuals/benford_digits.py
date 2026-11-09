"""Распределение первых цифр из CSV/JSON и модель Бенфорда p_d=log_b(1+1/d), d>0.
Основания берутся из данных; доступны переключатели основания, модели и масштаба Y."""

from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

from data_source import PlotDataSource, open_data_source
from plot_cli import run_plot
from plot_ui import create_figure, radio_selector, check_toggle, finish_figure
from plot_style import BLUE, DARK_BLUE, FILL_ALPHA, apply_grid

def benford_curve(base: int, max_digit: int) -> tuple[np.ndarray, np.ndarray]:
    """Теоретическая кривая Бенфорда для digit в [1, max_digit]."""
    digits = np.arange(1, max_digit + 1)
    probs = np.log(1 + 1 / digits) / np.log(base)
    return digits, probs


def build_figure(csv_path: Path | PlotDataSource, *, show: bool = False) -> plt.Figure:
    source = open_data_source(csv_path)
    base_list = source.bases
    if not base_list:
        raise ValueError("В CSV не найдено колонок вида fd_bX_dY")
    fig, (ax,) = create_figure("Распределение первых цифр", layout=dict(left=0.3, bottom=0.13))

    initial_base = 10 if 10 in base_list else base_list[0]
    state = {"base": initial_base, "yscale": "linear", "show_benford": True}

    def redraw() -> None:
        ax.clear()
        base = state["base"]
        digits, counts = source.first_digit_counts(base)
        observed_probs = counts / counts.sum()

        ax.bar(
            digits,
            observed_probs,
            color=BLUE,
            edgecolor="white",
            alpha=FILL_ALPHA,
            label="Гистограмма относительных частот",
        )

        # Полигон соединяет наблюдаемые частоты для сравнения с моделью Бенфорда.
        ax.plot(
            digits, observed_probs, color=BLUE, marker="s",
            linewidth=1.5, markersize=5,
            label="Полигон относительных частот",
        )

        if state["show_benford"]:
            bx, by = benford_curve(base, max(digits))
            ax.plot(
                bx, by, color=DARK_BLUE, marker="o", linewidth=2, linestyle="--",
                label="Закон Бенфорда",
            )

        ax.set_yscale(state["yscale"])
        ax.set_xlabel(rf"Первая цифра $d$ (основание $b={base}$)")
        ax.set_ylabel(r"Относительная частота $n_d/N$; вероятность $p_d$")
        ax.set_title(
            f"Распределение первых цифр в цепочках Коллатца — "
            f"основание {base}"
        )
        ax.set_xticks(digits)
        apply_grid(ax)
        ax.legend(loc="upper right")
        fig.canvas.draw_idle()

    def on_base_change(label: str) -> None:
        state["base"] = int(label)
        redraw()

    def on_yscale_change(label: str) -> None:
        state["yscale"] = label
        redraw()

    def on_benford_toggle(_label: str) -> None:
        state["show_benford"] = not state["show_benford"]
        redraw()

    radio_base = radio_selector(fig, [0.02, 0.55, 0.2, 0.3], [str(b) for b in base_list],
                                on_base_change, title="Основание СС", active=base_list.index(initial_base))
    radio_y = radio_selector(fig, [0.02, 0.35, 0.2, 0.15], ("linear", "log"),
                             on_yscale_change, title="Ось Y")
    check_benford = check_toggle(fig, [0.02, 0.2, 0.2, 0.1], "Закон Бенфорда", True, on_benford_toggle)
    redraw()
    return finish_figure(fig, (radio_base, radio_y, check_benford), show=show)


def main() -> None:
    run_plot(build_figure)

if __name__ == "__main__":
    main()
