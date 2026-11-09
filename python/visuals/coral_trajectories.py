"""Coral-визуализация траекторий из collatz_coral.csv (n,step,value).
Доступны угол поворота, число траекторий и цвет по стартовому значению."""

import sys
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

from result_paths import resolve_directory, resolve_file
from plot_style import BLUE
from plot_ui import create_figure, slider_control, check_toggle, finish_figure

DEFAULT_CSV = Path(__file__).resolve().parents[2] / "collatz_coral.csv"
DEFAULT_ANGLE_DEG = 8.0
MIN_ANGLE_DEG = 1.0
MAX_ANGLE_DEG = 30.0


def load_trajectories(csv_path: Path) -> dict[int, np.ndarray]:
    """Возвращает {n: массив значений шагов} для всех n в CSV."""
    csv_path = resolve_directory(csv_path) / "collatz_coral.csv" if csv_path.is_dir() else resolve_file(csv_path)
    df = pd.read_csv(csv_path)
    trajectories: dict[int, np.ndarray] = {}
    for n, group in df.groupby("n"):
        trajectories[int(n)] = group.sort_values("step")["value"].to_numpy()
    return trajectories


def compute_path(
    values: np.ndarray, angle_deg: float
) -> tuple[np.ndarray, np.ndarray]:
    """Строит путь с единичными отрезками и поворотом на каждом переходе.
    Чётный шаг меняет угол на -angle_deg, нечётный — на +angle_deg."""
    angle_rad = np.radians(angle_deg)
    heading = np.pi / 2  # начинаем "смотря вверх"
    x, y = 0.0, 0.0
    xs = [x]
    ys = [y]

    for i in range(1, len(values)):
        is_even_step = values[i] < values[i - 1]  # переход был делением на 2
        heading += -angle_rad if is_even_step else angle_rad
        x += np.cos(heading)
        y += np.sin(heading)
        xs.append(x)
        ys.append(y)

    return np.array(xs), np.array(ys)


def build_figure(trajectories: dict[int, np.ndarray], *, show: bool = False) -> plt.Figure:
    all_n = sorted(trajectories.keys())
    max_n = max(all_n)

    fig, (ax,) = create_figure("Траектории Коллатца", figsize=(9, 8),
                               layout=dict(left=0.28, bottom=0.22))

    state = {
        "angle": DEFAULT_ANGLE_DEG,
        "show_count": max_n,
        "gradient_color": True,
    }

    cmap = plt.get_cmap("viridis")

    def redraw() -> None:
        ax.clear()
        shown_n = [n for n in all_n if n <= state["show_count"]]

        for idx, n in enumerate(shown_n):
            xs, ys = compute_path(trajectories[n], state["angle"])
            if state["gradient_color"]:
                color = cmap(idx / max(1, len(shown_n) - 1))
            else:
                color = BLUE
            ax.plot(xs, ys, color=color, linewidth=0.8, alpha=0.7)

        ax.set_aspect("equal", adjustable="datalim")
        ax.set_title(
            f"Траектории Коллатца: {len(shown_n)} шт. "
            f"(угол={state['angle']:.1f}°)"
        )
        ax.set_xticks([])
        ax.set_yticks([])
        fig.canvas.draw_idle()

    def on_angle_change(val: float) -> None:
        state["angle"] = val
        redraw()

    def on_count_change(val: float) -> None:
        state["show_count"] = int(val)
        redraw()

    def on_color_toggle(_label: str) -> None:
        state["gradient_color"] = not state["gradient_color"]
        redraw()

    angle_slider = slider_control(fig, [0.28, 0.13, 0.6, 0.03], on_angle_change,
                                  minimum=MIN_ANGLE_DEG, maximum=MAX_ANGLE_DEG,
                                  value=DEFAULT_ANGLE_DEG, label="Угол (°)", step=None)
    count_slider = slider_control(fig, [0.28, 0.05, 0.6, 0.03], on_count_change,
                                  minimum=1, maximum=max_n, value=max_n, label="Макс. n")
    check_color = check_toggle(fig, [0.02, 0.5, 0.2, 0.1], "Градиент по n", True, on_color_toggle)
    redraw()
    return finish_figure(fig, (angle_slider, count_slider, check_color), show=show)


def main() -> None:
    csv_path = Path(sys.argv[1]) if len(sys.argv) > 1 else resolve_directory(DEFAULT_CSV.parent) / DEFAULT_CSV.name
    csv_path = resolve_directory(csv_path) / "collatz_coral.csv" if csv_path.is_dir() else resolve_file(csv_path)
    if not csv_path.exists():
        print(f"Файл не найден: {csv_path}")
        print(
            "Сначала запусти C++ приложение с coral_n_count > 0 в "
            "config.json, чтобы сгенерировать collatz_coral.csv."
        )
        sys.exit(1)

    trajectories = load_trajectories(csv_path)
    build_figure(trajectories, show=True)


if __name__ == "__main__":
    main()
