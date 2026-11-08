"""Рисунок к шагу 4: доля проскоков в multi-rate в зависимости от скорости снаряда
для частот быстрых тел 60·N Гц (обычные тела — 60 Гц).

    python scripts/plot_step4.py data report/fig
"""
import csv
import os
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.ticker

COLORS = {60: "#2a78d6", 120: "#eb6834", 180: "#1baf7a", 240: "#d55181"}
INK = "#333333"
comma = matplotlib.ticker.FuncFormatter(lambda v, _: f"{v:g}".replace(".", ","))


def main(data, figdir):
    with open(os.path.join(data, "multirate_sweep.csv")) as f:
        rows = [{k: float(v) for k, v in r.items()} for r in csv.DictReader(f)]
    fig, ax = plt.subplots(figsize=(7.4, 4.2), dpi=150)
    vs = [x * 10 for x in range(1, 621)]
    for fh, c in COLORS.items():
        rr = [r for r in rows if r["fast_hz"] == fh]
        ax.plot(vs, [max(0, 1 - 9 / (x / fh)) for x in vs], color=c, lw=2, label=f"быстрые {fh} Гц (N = {fh // 60})")
        ax.plot([r["v"] for r in rr], [r["p_pass"] for r in rr], "o", ms=5.5, color=c, mec="white", mew=1)
    ax.axvline(2000, color="#999999", lw=1, ls=":")
    ax.text(2050, 0.95, "2000 px/с", fontsize=8.5, color=INK)
    ax.set_xlabel("скорость снаряда v, px/с")
    ax.set_ylabel("доля проскоков из 1000 запусков")
    ax.set_xlim(0, 6200)
    ax.set_ylim(-0.02, 1.0)
    ax.grid(color="#e5e5e5", lw=0.8)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    ax.xaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(lambda v, _: f"{v:.0f}"))
    ax.yaxis.set_major_formatter(comma)
    ax.legend(fontsize=8.5, frameon=False, loc="upper left")
    fig.tight_layout()
    fig.savefig(os.path.join(figdir, "step4_multirate_probability.png"))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
