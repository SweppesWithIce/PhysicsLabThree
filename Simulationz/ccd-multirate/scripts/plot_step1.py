"""Рисунки к шагу 1: отскок (h'/h от e) и проседание стека (warm start вкл/выкл).

    python scripts/plot_step1.py data report/fig
"""
import csv
import os
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.ticker

BLUE, ORANGE, AQUA, INK = "#2a78d6", "#eb6834", "#1baf7a", "#333333"
comma = matplotlib.ticker.FuncFormatter(lambda v, _: f"{v:g}".replace(".", ","))


def rows(path):
    with open(path) as f:
        return [{k: float(v) for k, v in r.items()} for r in csv.DictReader(f)]


def style(ax):
    ax.grid(color="#e5e5e5", lw=0.8)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    ax.xaxis.set_major_formatter(comma)
    ax.yaxis.set_major_formatter(comma)


def bounce(data, out):
    r60 = rows(os.path.join(data, "bounce_sweep_60.csv"))
    r240 = rows(os.path.join(data, "bounce_sweep_240.csv"))
    fig, ax = plt.subplots(figsize=(7.0, 4.2), dpi=150)
    es = [i / 100 for i in range(101)]
    ax.plot(es, [e * e for e in es], color=INK, lw=2, label="теория h'/h = e²")
    ax.plot([r["e"] for r in r60], [r["apex_over_h"] for r in r60], "o", ms=7, color=BLUE,
            markeredgecolor="white", markeredgewidth=1.2, label="программа, 60 Гц")
    ax.plot([r["e"] for r in r240], [r["apex_over_h"] for r in r240], "s", ms=6, color=ORANGE,
            markeredgecolor="white", markeredgewidth=1.2, label="программа, 240 Гц")
    for r, dy in ((r60[-1], 6), (r240[-1], -6)):
        ax.annotate(f"{r['apex_over_h']:.3f}".replace(".", ","), xy=(r["e"], r["apex_over_h"]),
                    xytext=(9, dy), textcoords="offset points", va="center", fontsize=9, color=INK)
    ax.set_xlabel("коэффициент восстановления e")
    ax.set_ylabel("h' / h")
    ax.set_xlim(-0.02, 1.13)
    ax.set_ylim(-0.03, 1.1)
    style(ax)
    ax.legend(fontsize=9, frameon=False, loc="upper left")
    fig.tight_layout()
    fig.savefig(out)


def stack(data, out):
    w1 = rows(os.path.join(data, "stack_warm1.csv"))
    w0 = rows(os.path.join(data, "stack_warm0.csv"))
    fig, ax = plt.subplots(figsize=(7.0, 3.8), dpi=150)
    ax.plot([r["t"] for r in w0], [r["sink_top"] for r in w0], color=ORANGE, lw=1.6, label="без warm start")
    ax.plot([r["t"] for r in w1], [r["sink_top"] for r in w1], color=BLUE, lw=2, label="с warm start")
    ax.hlines(5.0, 0, 10, color=INK, lw=1, ls="--")
    ax.text(10.15, 5.0, "10 · slop = 5 px", va="center", fontsize=9, color=INK)
    ax.set_xlabel("t, с")
    ax.set_ylabel("просадка верхнего блока, px")
    ax.set_xlim(0, 11.6)
    ax.set_ylim(bottom=0)
    style(ax)
    ax.legend(fontsize=9, frameon=False, loc="upper left")
    fig.tight_layout()
    fig.savefig(out)


if __name__ == "__main__":
    data, fig = sys.argv[1], sys.argv[2]
    bounce(data, os.path.join(fig, "step1_bounce.png"))
    stack(data, os.path.join(fig, "step1_stack_sink.png"))
