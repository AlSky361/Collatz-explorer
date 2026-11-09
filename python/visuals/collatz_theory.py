"""Fitted heuristic: length|n ~ N(C*ln(n)+b, sqrt(sigma2*ln(n))); counts ordinary steps.
Mixtures approximate the uniform range 2..max(n), not sparse samples or a proven law."""

from dataclasses import dataclass

import numpy as np

# Support both old and new NumPy integration function names.
_trapezoid = getattr(np, "trapezoid", None) or np.trapz


def freedman_diaconis_bins(data: np.ndarray, min_bins: int = 5,
                            max_bins: int = 500, log_space: bool = False) -> int:
    """Число интервалов по ширине 2*IQR/n^(1/3), ограниченное [min_bins, max_bins].
    При log_space=True правило применяется к log10(data); вырожденные данные обрабатываются отдельно."""
    data = np.asarray(data)
    if log_space:
        data = np.log10(data[data > 0])
    n = data.size
    if n < 2:
        return min_bins

    q75, q25 = np.percentile(data, [75, 25])
    iqr = q75 - q25
    if iqr <= 0:
        # Вырожденный случай (данные почти константны) — Sturges как fallback.
        return int(np.clip(np.ceil(np.log2(n)) + 1, min_bins, max_bins))

    bin_width = 2 * iqr / (n ** (1 / 3))
    data_range = data.max() - data.min()
    if bin_width <= 0 or data_range <= 0:
        return min_bins

    bins = int(np.ceil(data_range / bin_width))
    return int(np.clip(bins, min_bins, max_bins))


@dataclass
class LengthModel:
    """Mean C*ln(n)+b and variance sigma2*ln(n); N(m, sigma) uses std."""
    C: float
    b: float
    sigma2: float


def calibrate_length_model(n: np.ndarray, length: np.ndarray) -> LengthModel:
    """Оценивает (C, b, sigma2) взвешенным МНК с весами 1/ln(n), исключая n=1.
    Модель: length|n ~ N(C*ln(n)+b, sqrt(sigma2*ln(n))); второй параметр — стандартное отклонение."""
    n = np.asarray(n, dtype=np.float64)
    length = np.asarray(length, dtype=np.float64)
    mask = n > 1
    n = n[mask]
    length = length[mask]

    logn = np.log(n)
    design = np.column_stack([logn, np.ones_like(logn)])

    weights = 1.0 / logn
    sqrt_w = np.sqrt(weights)
    design_w = design * sqrt_w[:, None]
    length_w = length * sqrt_w

    (C, b), *_ = np.linalg.lstsq(design_w, length_w, rcond=None)

    resid = length - (C * logn + b)
    sigma2 = float(np.mean(resid ** 2 / logn))
    if not np.isfinite(sigma2) or sigma2 <= 0:
        # При нехватке данных положительная дисперсия защищает от NaN; модель неинформативна.
        sigma2 = 1e-6

    return LengthModel(C=float(C), b=float(b), sigma2=sigma2)


def _mixture_grid(n_values: np.ndarray, grid_points: int = 400):
    """Возвращает сетку y=ln(n) и веса exp(y) для смеси равномерного диапазона стартов."""
    n_max = float(np.asarray(n_values).max())
    y_max = np.log(n_max)
    # Исключаем окрестность n=1, где дисперсия sigma2*ln(n) вырождается.
    y_min = np.log(2.0)
    if y_max <= y_min:
        # Расширяем вырожденный диапазон для защиты от NaN; модель здесь неинформативна.
        y_max = y_min + 1e-3
    y_grid = np.linspace(y_min, y_max, grid_points)
    weights = np.exp(y_grid)  # плотность ln(n) для n~Uniform[1,n_max]
    return y_grid, weights


def theoretical_length_pdf(x_grid: np.ndarray, n_values: np.ndarray,
                            model: LengthModel) -> np.ndarray:
    """Эвристическая смесь плотностей N(C*ln(n)+b, sqrt(sigma2*ln(n))) для 2..max(n).
    Интегрирование приближает полный равномерный диапазон, а не отфильтрованную выборку."""
    y_grid, weights = _mixture_grid(n_values)
    means = model.C * y_grid + model.b
    stds = np.sqrt(model.sigma2 * y_grid)

    # phi(x; mean, std) для каждой пары (x_grid, y_grid) -> матрица
    # [len(x_grid), len(y_grid)], затем интегрируем по y (trapz) с весами.
    x = np.asarray(x_grid)[:, None]
    z = (x - means[None, :]) / stds[None, :]
    phi = np.exp(-0.5 * z ** 2) / (stds[None, :] * np.sqrt(2 * np.pi))

    integrand = phi * weights[None, :]
    numerator = _trapezoid(integrand, y_grid, axis=1)
    denominator = _trapezoid(weights, y_grid)
    return numerator / denominator


def theoretical_length_cdf(x_grid: np.ndarray, n_values: np.ndarray,
                            model: LengthModel) -> np.ndarray:
    """Эвристическая F_X(x)=P{X<x}: смесь нормальных функций распределения.
    Использует веса theoretical_length_pdf и прямой расчёт CDF без интегрирования плотности."""
    from scipy.special import erf

    y_grid, weights = _mixture_grid(n_values)
    means = model.C * y_grid + model.b
    stds = np.sqrt(model.sigma2 * y_grid)

    x = np.asarray(x_grid)[:, None]
    z = (x - means[None, :]) / (stds[None, :] * np.sqrt(2))
    # scipy.special.erf вычисляет массив без Python-цикла по элементам.
    cdf_vals = 0.5 * (1 + erf(z))

    integrand = cdf_vals * weights[None, :]
    numerator = _trapezoid(integrand, y_grid, axis=1)
    denominator = _trapezoid(weights, y_grid)
    return numerator / denominator
