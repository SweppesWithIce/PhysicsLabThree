"""Скриншот к шагу 2: сцена туннелирования на 60 и 240 Гц с подписями дорожек.

Кадры записываются приложением:
    physsim --scene 4 --no-hud --hz 60  --record DIR60  --frames 30
    physsim --scene 4 --no-hud --hz 240 --record DIR240 --frames 30
затем кадр 24 обрезается до области у стенки (x 470…1070, y 150…550):
    python scripts/compose_tunnel_shots.py crop60.png crop240.png report/fig/step2_tunnel_shots.png
"""
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.image as mpimg
import matplotlib.pyplot as plt

# y дорожек в мире: 200, 300, 400, 500; кадр обрезан с y = 150 → 50, 150, 250, 350
LANES_60 = ["2000 px/с, u = −15: пропуск", "2000 px/с, u = +4: вытолкнут вперёд",
            "2000 px/с, u = −5: отскок", "500 px/с: отскок"]
LANES_240 = ["2000 px/с: отскок"] * 3 + ["500 px/с: отскок"]


def panel(ax, path, title, labels):
    ax.imshow(mpimg.imread(path))
    ax.set_title(title, fontsize=10, loc="left")
    for y, text in zip((50, 150, 250, 350), labels):
        ax.text(596, y - 12, text, ha="right", va="bottom", fontsize=8, color="white",
                bbox=dict(fc="#000000", alpha=0.55, ec="none", pad=1.5))
    ax.text(306, 395, "стенка w = 10 px", ha="left", va="bottom", fontsize=8, color="white")
    ax.set_xticks([])
    ax.set_yticks([])


LANES_CCD = ["2000 px/с: удар в $t_c$, отскок"] * 3 + ["500 px/с: удар в $t_c$, отскок"]


def main(c60, c240, out, kind="hz"):
    fig, axes = plt.subplots(1, 2, figsize=(11, 3.9), dpi=150)
    if kind == "ccd":
        # второй аргумент — кадр режима CCD при той же частоте 60 Гц
        panel(axes[0], c60, "а) дискретный режим, 60 Гц", LANES_60)
        panel(axes[1], c240, "б) CCD, 60 Гц (красные крестики – положения в момент удара)", LANES_CCD)
    else:
        panel(axes[0], c60, "а) 60 Гц: v·Δt = 33,3 px", LANES_60)
        panel(axes[1], c240, "б) 240 Гц: v·Δt = 8,3 px", LANES_240)
    fig.tight_layout()
    fig.savefig(out)


if __name__ == "__main__":
    # python scripts/compose_tunnel_shots.py a.png b.png out.png [hz|ccd]
    main(*sys.argv[1:5])
