"""Shared statistical-plot palette and grid settings."""

BLUE = "#1f77b4"
DARK_BLUE = "#124b73"
FILL_ALPHA = 0.35

def apply_grid(ax):
    """Grid only the data axes; widget axes and coral geometry stay uncluttered."""
    ax.set_axisbelow(True)
    ax.grid(True, alpha=0.25)
