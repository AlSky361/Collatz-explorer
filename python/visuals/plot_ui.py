"""Small factories for figures and controls shared by standalone plots."""

import matplotlib.pyplot as plt
from matplotlib.widgets import CheckButtons, RadioButtons, Slider

from plot_style import DARK_BLUE


def create_figure(title, *, rows=1, figsize=(9, 6), layout=None, heading=False, heading_y=0.97):
    fig, axes = plt.subplots(rows, 1, figsize=figsize, squeeze=False)
    if layout:
        fig.subplots_adjust(**layout)
    fig.canvas.manager.set_window_title(title)
    if heading:
        fig.suptitle(title, fontsize=14, y=heading_y)
    return fig, axes[:, 0]


def radio_selector(fig, bounds, labels, on_change, *, title, active=0):
    control = RadioButtons(fig.add_axes(bounds), labels, active=active)
    control.ax.set_title(title, fontsize=9)
    control.on_clicked(on_change)
    return control


def slider_control(fig, bounds, on_change, *, minimum, maximum, value,
                   label="Интервалы", title=None, step=1, reference=False):
    ax = fig.add_axes(bounds)
    if title:
        ax.set_title(title, fontsize=9)
    control = Slider(ax, label, minimum, maximum, valinit=value, valstep=step)
    if reference:
        ax.axvline(value, color=DARK_BLUE, linewidth=2, ymin=0, ymax=1)
    control.on_changed(on_change)
    return control


def check_toggle(fig, bounds, label, checked, on_change):
    control = CheckButtons(fig.add_axes(bounds), [label], [checked])
    control.on_clicked(on_change)
    return control


def finish_figure(fig, widgets=(), *, show=False):
    """Keep controls alive and return a figure; display is explicitly requested."""
    fig._collatz_widgets = tuple(widgets)
    if show:
        plt.show()
    return fig
