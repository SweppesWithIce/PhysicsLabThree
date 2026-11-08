"""Рисунок к шагу 0: ошибка полунеявного Эйлера при свободном падении.

Линии — теория err(t) = g·dt·t/2, маркеры — значения из physbench (каждый
k-й шаг, чтобы точки не сливались). Запуск из корня репозитория:
    python scripts/plot_freefall.py data report/fig/step0_freefall_error.png
"""
import csv
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.ticker

COLORS = {60: "#2a78d6", 120: "#eb6834", 240: "#1baf7a"}  # категориальные слоты 1–3


def load(path):
    with open(path) as f:
        rows = list(csv.DictReader(f))
    return [(float(r["t"]), float(r["err"]), float(r["err_pred"])) for r in rows]


def main(data_dir, out):
    fig, ax = plt.subplots(figsize=(7.0, 4.0), dpi=150)
    for hz, color in COLORS.items():
        rows = load(f"{data_dir}/freefall_{hz}.csv")
        t = [r[0] for r in rows]
        ax.plot(t, [r[2] for r in rows], color=color, lw=2, label=f"{hz} Гц — теория g·Δt·t/2")
        k = max(1, hz // 10)  # 10 точек на секунду для любой частоты
        ax.plot(t[k - 1::k], [r[1] for r in rows][k - 1::k], "o", ms=6, color=color,
                markeredgecolor="white", markeredgewidth=1.2, label=f"{hz} Гц — программа")
        ax.annotate(f"{hz} Гц: {rows[-1][1]:.3f} px".replace(".", ","), xy=(t[-1], rows[-1][1]), xytext=(6, 0),
                    textcoords="offset points", va="center", fontsize=9, color="#333333")
    comma = matplotlib.ticker.FuncFormatter(lambda v, _: f"{v:g}".replace(".", ","))
    ax.xaxis.set_major_formatter(comma)
    ax.yaxis.set_major_formatter(comma)
    ax.set_xlabel("t, с")
    ax.set_ylabel("y_числ − y_точн, px")
    ax.set_xlim(0, 1.22)
    ax.set_ylim(bottom=0)
    ax.grid(color="#e5e5e5", lw=0.8)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    ax.legend(fontsize=8, frameon=False, ncol=2, loc="upper left")
    fig.tight_layout()
    fig.savefig(out)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
