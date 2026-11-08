"""Рисунок к шагу 2: доля проскоков стенки от скорости снаряда (дискретный режим).

    python scripts/plot_step2.py data report/fig
"""
import csv
import os
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.ticker

COLORS = {60: "#2a78d6", 120: "#eb6834", 240: "#1baf7a"}
INK = "#333333"
comma = matplotlib.ticker.FuncFormatter(lambda v, _: f"{v:g}".replace(".", ","))


def main(data, figdir):
    with open(os.path.join(data, "tunnel_sweep.csv")) as f:
        rows = [{k: float(v) for k, v in r.items()} for r in csv.DictReader(f)]
    fig, axes = plt.subplots(1, 2, figsize=(9.6, 4.0), dpi=150, sharey=True)
    ws = 18.0  # w + s, px
    panels = [
        (axes[0], "p_miss", 1.0, "а) пересечение не найдено ни разу\nтеория: 1 − (w+s)/(vΔt)"),
        (axes[1], "p_pass", 0.5, "б) снаряд оказался за стенкой\nтеория: 1 − (w+s)/(2vΔt)"),
    ]
    vs = [x * 10 for x in range(1, 621)]
    for ax, key, k, title in panels:
        for hz, c in COLORS.items():
            rr = [r for r in rows if r["hz"] == hz]
            ax.plot(vs, [max(0, 1 - ws * k / (x / hz)) for x in vs], color=c, lw=2, label=f"{hz} Гц")
            ax.plot([r["v"] for r in rr], [r[key] for r in rr], "o", ms=5.5, color=c,
                    markeredgecolor="white", markeredgewidth=1)
        ax.axvline(2000, color="#999999", lw=1, ls=":")
        ax.set_title(title, fontsize=9.5, loc="left", color=INK)
        ax.set_xlabel("скорость снаряда v, px/с")
        ax.set_xlim(0, 6200)
        ax.set_ylim(-0.02, 1.0)
        ax.grid(color="#e5e5e5", lw=0.8)
        for sp in ("top", "right"):
            ax.spines[sp].set_visible(False)
        ax.xaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(lambda v, _: f"{v:.0f}"))
        ax.yaxis.set_major_formatter(comma)
    axes[0].set_ylabel("доля из 1000 запусков")
    axes[0].legend(fontsize=9, frameon=False, loc="upper left")
    axes[1].text(2060, 0.93, "2000 px/с", fontsize=8.5, color=INK)
    fig.tight_layout()
    fig.savefig(os.path.join(figdir, "step2_tunnel_probability.png"))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
