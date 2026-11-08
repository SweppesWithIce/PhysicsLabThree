"""Рисунок «Схема модели»: оси, тела, обозначения w, s, v·Δt, g.

    python scripts/plot_scheme.py report/fig/scheme.png
"""
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyArrowPatch, Rectangle

INK = "#222222"
MUTED = "#777777"
STATIC = "#8a96a8"
NORMAL = "#e0a93b"
FAST = "#2a78d6"


def arrow(ax, p, q, color=INK, lw=1.4, style="-|>", ms=12):
    ax.add_patch(FancyArrowPatch(p, q, arrowstyle=style, mutation_scale=ms, lw=lw, color=color))


def dim(ax, x0, x1, y, text, above=True):
    arrow(ax, (x0, y), (x1, y), style="<|-|>", ms=6, lw=1.0)
    ax.plot([x0, x0], [y - 6, y + 6], color=INK, lw=0.8); ax.plot([x1, x1], [y - 6, y + 6], color=INK, lw=0.8)
    ax.text((x0 + x1) / 2, y + (-20 if above else 18), text, ha="center", va="center", fontsize=11,
            bbox=dict(fc="white", ec="none", pad=1.5))


def main(out):
    fig, ax = plt.subplots(figsize=(8.0, 4.2), dpi=150)
    ax.set_xlim(-40, 760)
    ax.set_ylim(400, -40)  # y вниз, как в экранных координатах
    ax.set_aspect("equal")
    ax.axis("off")

    # Оси мира
    arrow(ax, (0, 0), (110, 0))
    arrow(ax, (0, 0), (0, 110))
    ax.text(116, 0, "x", va="center", fontsize=11, style="italic")
    ax.text(0, 124, "y", ha="center", fontsize=11, style="italic")
    ax.text(-6, -10, "O", ha="right", fontsize=10)
    arrow(ax, (60, 160), (60, 230), color=MUTED)
    ax.text(70, 200, "g", fontsize=11, style="italic", color=MUTED)

    # Пол и блоки обычной группы
    ax.add_patch(Rectangle((0, 330), 720, 40, color=STATIC))
    ax.text(360, 352, "неподвижное тело (Static)", ha="center", va="center", fontsize=9, color="white")
    ax.add_patch(Rectangle((140, 300), 60, 30, fc=NORMAL, ec=INK, lw=0.8))
    ax.add_patch(Rectangle((148, 270), 44, 30, fc=NORMAL, ec=INK, lw=0.8))
    ax.add_patch(Rectangle((154, 240), 32, 30, fc=NORMAL, ec=INK, lw=0.8))
    ax.text(210, 285, "стек обычных тел\n(Normal, 60 Гц)", ha="left", va="center", fontsize=9)

    # Стенка и снаряд
    wall_x, wall_w = 470, 30          # стенка толщиной w (масштаб схемы условный)
    ax.add_patch(Rectangle((wall_x, 40), wall_w, 290, color=STATIC))
    ax.text(wall_x + wall_w + 8, 70, "стенка", ha="left", va="center", fontsize=9)
    dim(ax, wall_x, wall_x + wall_w, 28, "w")

    s = 22
    y0 = 160
    xa, xb = 380, 560               # позиции снаряда в t_n и t_n + Δt
    ax.add_patch(Rectangle((xa - s / 2, y0 - s / 2), s, s, fc=FAST, ec=INK, lw=0.8))
    ax.add_patch(Rectangle((xb - s / 2, y0 - s / 2), s, s, fc="none", ec=FAST, lw=1.2, ls="--"))
    ax.text(xa, y0 + 24, r"$t_n$", ha="center", fontsize=10)
    ax.text(xb, y0 + 24, r"$t_n+\Delta t$", ha="center", fontsize=10)
    dim(ax, xa - s / 2, xa + s / 2, y0 - 26, "s")
    arrow(ax, (xa + s / 2 + 2, y0), (xa + 60, y0), color=FAST, lw=2)
    ax.text(xa + 44, y0 - 12, r"$\vec v$", fontsize=11, color=FAST)
    dim(ax, xa, xb, y0 + 50, r"$v\,\Delta t$", above=False)
    ax.text(xb + 18, y0, "снаряд (Fast, 240 Гц)", va="center", fontsize=9)
    ax.text(xb - 20, y0 + 95, "при $v\\,\\Delta t > w + s$\nпересечение в моменты\n$t_n$ и $t_n+\\Delta t$ может\nне обнаружиться",
            fontsize=8.5, color=MUTED, ha="left", va="top")

    fig.tight_layout()
    fig.savefig(out, bbox_inches="tight")


if __name__ == "__main__":
    main(sys.argv[1])
