"""Рисунки к шагу 3 (CCD).

    python scripts/plot_step3.py data report/fig

step3_trace.png — координата снаряда относительно стенки во времени для одного
и того же запуска (2000 px/с, 60 Гц) в дискретном режиме и в режиме CCD.
"""
import csv
import os
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.ticker

BLUE, ORANGE, INK, MUTED = "#2a78d6", "#eb6834", "#333333", "#8a96a8"
comma = matplotlib.ticker.FuncFormatter(lambda v, _: f"{v:g}".replace(".", ","))


def load(path):
    with open(path) as f:
        return [(float(r["t"]) * 1000, float(r["x"]), r["kind"]) for r in csv.DictReader(f)]


def trace(data, out):
    d = load(os.path.join(data, "trace_discrete.csv"))
    c = load(os.path.join(data, "trace_ccd.csv"))
    t0, t1 = 225, 295
    d = [p for p in d if t0 <= p[0] <= t1]
    c = [p for p in c if t0 <= p[0] <= t1]
    fig, ax = plt.subplots(figsize=(7.6, 4.2), dpi=150)
    ax.axhspan(-9, 9, color="#e9edf3", zorder=0)
    ax.axhspan(-5, 5, color=MUTED, alpha=0.6, zorder=0)
    ax.text(t1 - 0.5, 0, "стенка (w = 10 px)", ha="right", va="center", fontsize=8.5, color="white")
    ax.text(t1 - 0.5, 7, "зона пересечения центра |x| < (w+s)/2 = 9 px", ha="right", va="center",
            fontsize=8, color=INK)
    for k in range(14, 18):
        ax.axvline(k * 1000 / 60, color="#dddddd", lw=0.8, zorder=0)
    ax.plot([p[0] for p in d], [p[1] for p in d], "--", color=ORANGE, lw=1.4, zorder=2)
    ax.plot([p[0] for p in d], [p[1] for p in d], "o", color=ORANGE, ms=6, mec="white", mew=1, zorder=3,
            label="дискретный режим: положения в $t_n$")
    ax.plot([p[0] for p in c], [p[1] for p in c], "-", color=BLUE, lw=2, zorder=2)
    ax.plot([p[0] for p in c if p[2] == "step"], [p[1] for p in c if p[2] == "step"], "o", color=BLUE, ms=6,
            mec="white", mew=1, zorder=3, label="CCD: положения в $t_n$")
    imp = [p for p in c if p[2] == "impact"][0]
    ax.plot([imp[0]], [imp[1]], "X", color="#d62f2f", ms=10, mec="white", mew=1, zorder=4,
            label="CCD: удар в момент $t_c$")
    ax.annotate(f"$t_c$ = $t_n$ + {imp[0] - 250:.1f} мс\nx = −9 px (передняя грань)".replace(".", ","),
                xy=(imp[0], imp[1]), xytext=(imp[0] + 9, -40), fontsize=8.5, color=INK,
                arrowprops=dict(arrowstyle="-", color=INK, lw=0.8))
    ax.set_xlim(t0, t1)
    ax.set_ylim(-75, 60)
    ax.set_xlabel("t, мс (вертикальные линии – моменты шагов $t_n$, Δt = 16,7 мс)")
    ax.set_ylabel("координата снаряда относительно стенки, px")
    ax.xaxis.set_major_formatter(comma)
    ax.yaxis.set_major_formatter(comma)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    ax.legend(fontsize=8.5, frameon=False, loc="upper left")
    fig.tight_layout()
    fig.savefig(out)


if __name__ == "__main__":
    data, fig = sys.argv[1], sys.argv[2]
    trace(data, os.path.join(fig, "step3_trace.png"))
