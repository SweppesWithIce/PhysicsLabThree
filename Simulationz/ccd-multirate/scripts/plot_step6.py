"""Рисунки к шагу 6.

    python scripts/plot_step6.py data report/fig

step6_scaling.png — время физики на кадр в зависимости от числа обычных тел
для всех режимов (основная сцена, минимум из 15 повторов).
"""
import csv
import os
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.ticker

SERIES = [
    ("discrete60", "дискретный 60 Гц", "#2a78d6", "o"),
    ("multirate120", "multi-rate 60/120 Гц", "#1baf7a", "D"),
    ("multirate240", "multi-rate 60/240 Гц", "#eb6834", "s"),
    ("discrete240", "дискретный 240 Гц (все тела)", "#e87ba4", "v"),
    ("ccd60", "CCD 60 Гц", "#eda100", "^"),
]
INK = "#333333"
comma = matplotlib.ticker.FuncFormatter(lambda v, _: f"{v:g}".replace(".", ","))


def main(data, figdir):
    with open(os.path.join(data, "main_timing.csv")) as f:
        rows = list(csv.DictReader(f))
    fig, ax = plt.subplots(figsize=(7.6, 4.4), dpi=150)
    for key, label, color, marker in SERIES:
        rr = sorted((r for r in rows if r["case"] == key), key=lambda r: int(r["n_normal"]))
        xs = [int(r["n_normal"]) for r in rr]
        ys = [float(r["ms_per_frame"]) for r in rr]
        ax.plot(xs, ys, "-", color=color, lw=2, marker=marker, ms=6, mec="white", mew=1, label=label)
        dy = {"multirate120": 7, "multirate240": -7}.get(key, 0)  # две близкие кривые — подписи врозь
        ax.annotate(f"{ys[-1]:.3f}".replace(".", ","), xy=(xs[-1], ys[-1]), xytext=(7, dy),
                    textcoords="offset points", va="center", fontsize=8.5, color=INK)
    ax.axvline(50, color="#999999", lw=1, ls=":")
    ax.text(51, 0.152, "основная сцена", fontsize=8.5, color=INK, va="top")
    ax.set_xlabel("число обычных тел (включая 10 блоков стека); + 9 неподвижных и 5 снарядов")
    ax.set_ylabel("время физики, мс на кадр (1/60 с)")
    ax.set_xlim(15, 112)
    ax.set_ylim(0, 0.16)
    ax.set_xticks([20, 50, 70, 100])
    ax.grid(color="#e5e5e5", lw=0.8)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    ax.yaxis.set_major_formatter(comma)
    ax.legend(fontsize=8.5, frameon=False, loc="upper left")
    fig.tight_layout()
    fig.savefig(os.path.join(figdir, "step6_scaling.png"))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
