"""Медиа к отчёту и README: видео 26 с, GIF и скриншоты.

Все кадры пишет само приложение (physsim --record, кадр = ровно 1/60 с модельного
времени, окно не показывается), поэтому результат детерминирован и повторяем.

Использование (из корня репозитория, после сборки):
    python scripts/make_media.py build/physsim
На Windows путь к программе: build/Release/physsim.exe (или out/build/.../physsim.exe).

Нужны: ffmpeg в PATH, Python-пакеты Pillow и matplotlib.

Результат:
    media/ccd_multirate_demo.mp4      видео 1920×1080, 60 к/с
    media/tunnel_demo.gif             GIF первой части (для README)
    media/screenshots/*.png           полноразмерные кадры приложения
    report/fig/step7_app_tunnel.png   рисунок отчёта: окно приложения, дискретный режим и CCD
    report/fig/step7_main_targets.png рисунок отчёта: мишени основной сцены в трёх режимах
"""
import os
import re
import shutil
import subprocess
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MEDIA = os.path.join(ROOT, "media")
RAW = os.path.join(MEDIA, "_raw")          # временные кадры, удаляются в конце
SHOTS = os.path.join(MEDIA, "screenshots")
FIG = os.path.join(ROOT, "report", "fig")

W, H, FPS = 1920, 1080, 60
SRC_W, SRC_H = 1280, 720
PANE_W, PANE_H = 950, 534
PANE_Y = 140
PANE_X = (5, 965)

FONT_DIRS = ["/usr/share/fonts/truetype/dejavu", "C:/Windows/Fonts", "/Library/Fonts"]


def font(size, bold=False):
    names = ["DejaVuSans-Bold.ttf", "arialbd.ttf", "Arial Bold.ttf"] if bold else \
            ["DejaVuSans.ttf", "arial.ttf", "Arial.ttf"]
    for d in FONT_DIRS:
        for n in names:
            p = os.path.join(d, n)
            if os.path.exists(p):
                return ImageFont.truetype(p, size)
    return ImageFont.load_default()


F_TITLE, F_SUB = font(38, True), font(25)
F_LABEL, F_TEXT, F_SMALL = font(32, True), font(24), font(20)
F_BIG = font(64, True)

BG = (15, 17, 21)
WHITE, GREY = (235, 237, 240), (150, 156, 166)
RED, GREEN, YELLOW = (240, 90, 80), (110, 210, 120), (250, 200, 70)


# ------------------------------------------------------------------ запись кадров
def record(physsim, name, frames, *args):
    """Записать `frames` кадров в media/_raw/<name>; вернуть строки stdout."""
    d = os.path.join(RAW, name)
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    out = subprocess.run([physsim, *args, "--record", d, "--frames", str(frames)],
                         check=True, capture_output=True, text=True).stdout
    return d, out


def load_frames(d, n):
    return [Image.open(os.path.join(d, f"frame_{i:05d}.bmp")).convert("RGB") for i in range(n)]


def to_intermediate(d, mp4):
    """Длинные клипы — сразу в почти без потерь сжатое видео, BMP удаляются (≈2,7 ГБ на клип)."""
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-framerate", str(FPS),
                    "-i", os.path.join(d, "frame_%05d.bmp"), "-c:v", "libx264", "-preset", "veryfast",
                    "-crf", "8", "-pix_fmt", "yuv444p", mp4], check=True)
    shutil.rmtree(d)


def read_video(mp4):
    p = subprocess.Popen(["ffmpeg", "-loglevel", "error", "-i", mp4, "-f", "rawvideo",
                          "-pix_fmt", "rgb24", "-"], stdout=subprocess.PIPE)
    size = SRC_W * SRC_H * 3
    while True:
        buf = p.stdout.read(size)
        if len(buf) < size:
            break
        yield Image.frombytes("RGB", (SRC_W, SRC_H), buf)
    p.wait()


def counters(stdout):
    return [(int(t), int(s)) for t, s in re.findall(r"tunnels (\d+) shots (\d+)", stdout)]


# ------------------------------------------------------------------ рисование
def text_box(draw, xy, text, fnt, fill, anchor="la", pad=6, bg=(0, 0, 0, 170)):
    l, t, r, b = draw.textbbox(xy, text, font=fnt, anchor=anchor)
    draw.rectangle((l - pad, t - pad, r + pad, b + pad), fill=bg)
    draw.text(xy, text, font=fnt, fill=fill, anchor=anchor)


def canvas(title, subtitle):
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)
    d.text((30, 26), title, font=F_TITLE, fill=WHITE)
    d.text((30, 80), subtitle, font=F_SUB, fill=GREY)
    return img


def pane(img, k, frame):
    img.paste(frame.resize((PANE_W, PANE_H), Image.LANCZOS), (PANE_X[k], PANE_Y))


def pane_caption(img, k, label, result, color):
    d = ImageDraw.Draw(img)
    x = PANE_X[k] + 10
    d.text((x, PANE_Y + PANE_H + 22), label, font=F_LABEL, fill=WHITE)
    d.text((x, PANE_Y + PANE_H + 70), result, font=F_TEXT, fill=color)


def lane_labels(img, k, labels):
    """Подписи у дорожек сцены туннелирования (y мира 200, 300, 400, 500)."""
    d = ImageDraw.Draw(img, "RGBA")
    s = PANE_H / SRC_H
    for y, (text, color) in zip((200, 300, 400, 500), labels):
        text_box(d, (PANE_X[k] + PANE_W - 14, PANE_Y + y * s - 16), text, F_SMALL, color, anchor="rb")


def footer(img, text):
    ImageDraw.Draw(img).text((30, H - 52), text, font=F_SMALL, fill=GREY)


# ------------------------------------------------------------------ части видео
def title_card():
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)
    d.text((W // 2, 380), "Туннелирование, CCD и multi-rate", font=F_BIG, fill=WHITE, anchor="mm")
    d.text((W // 2, 470), "2D rigid body, AABB, фиксированный шаг · ПЗ 3, «Моделирование физических процессов»",
           font=F_SUB, fill=GREY, anchor="mm")
    d.text((W // 2, 560), "1) снаряд против тонкой стенки: дискретный шаг vs CCD", font=F_TEXT, fill=WHITE, anchor="mm")
    d.text((W // 2, 600), "2) то же: 60 Гц vs multi-rate (быстрые тела 240 Гц)", font=F_TEXT, fill=WHITE, anchor="mm")
    d.text((W // 2, 640), "3) основная сцена: 9 статичных, 50 обычных, 5 быстрых тел", font=F_TEXT, fill=WHITE, anchor="mm")
    return img


LANES_D60 = [("пропуск: шаг 33 px > w + s = 18 px", RED), ("перекрытие u = +4 → вытолкнут вперёд", RED),
             ("перекрытие u = −5 → отскок", GREEN), ("500 px/с, шаг 8 px → отскок", GREEN)]
LANES_CCD = [("удар в момент t_c → отскок", GREEN)] * 3 + [("500 px/с → отскок", GREEN)]
LANES_MR = [("шаг 8,3 px < 18 px → отскок", GREEN)] * 3 + [("500 px/с → отскок", GREEN)]


def tunnel_part(left, right, title, subtitle, cap_l, cap_r, lanes_l, lanes_r, hold_note, slow=3, hold=150):
    """40 кадров сцены туннелирования, замедленно в `slow` раз, затем стоп-кадр с подписями."""
    n = len(left)
    for i in range(n * slow + hold):
        j = min(i // slow, n - 1)
        img = canvas(title, subtitle)
        pane(img, 0, left[j])
        pane(img, 1, right[j])
        pane_caption(img, 0, *cap_l)
        pane_caption(img, 1, *cap_r)
        if i >= n * slow:
            lane_labels(img, 0, lanes_l)
            lane_labels(img, 1, lanes_r)
            footer(img, hold_note)
        else:
            footer(img, f"Замедлено в {slow} раза. Жёлтые точки — положения тел на каждом шаге физики.")
        yield img


def main_part(left_mp4, right_mp4, cnt_l, cnt_r, n=720):
    title = "3. Основная сцена: залп из 5 снарядов по 4000 px/с каждые 1,5 с"
    subtitle = "9 статичных тел (пол, стены, мишени, полки), 50 обычных (из них стек 10 блоков), 5 быстрых; реальное время"
    for i, (a, b) in enumerate(zip(read_video(left_mp4), read_video(right_mp4))):
        if i >= n:
            break
        img = canvas(title, subtitle)
        pane(img, 0, a)
        pane(img, 1, b)
        for k, (cnt, label) in enumerate(((cnt_l[i], "Дискретный, 60 Гц"),
                                          (cnt_r[i], "Multi-rate: обычные 60 Гц, быстрые 240 Гц"))):
            t, s = cnt
            pane_caption(img, k, label, f"Прошли сквозь препятствия: {t} из {s} выстрелов",
                         RED if t > 0 else GREEN)
        d = ImageDraw.Draw(img)
        d.text((30, 860), "Жёлтые точки — шаги текущего залпа. Слева точки снаряда стоят по обе стороны мишени: за шаг он перескакивает её.",
               font=F_TEXT, fill=WHITE)
        d.text((30, 900), "Справа снаряды делают 4 подшага за кадр, их точки упираются в грань мишени, и снаряды отскакивают.",
               font=F_TEXT, fill=WHITE)
        footer(img, "Счётчик считает выстрел, если центр снаряда за один шаг перескочил центр мишени или полки.")
        yield img


def summary_card():
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)
    d.text((W // 2, 200), "Итог: 60 с основной сцены, 205 выстрелов", font=F_TITLE, fill=WHITE, anchor="mm")
    rows = [("Режим", "Проскоки", "Время кадра"),
            ("Дискретный, 60 Гц", "61 %", "×1,0"),
            ("CCD, 60 Гц", "0", "×2,1"),
            ("Multi-rate 60 / 240 Гц", "0", "×1,3"),
            ("Дискретный, 240 Гц (всё)", "1,5 %", "×3,8")]
    xs = (520, 1150, 1450)
    for r, row in enumerate(rows):
        y = 320 + r * 80
        for c, cell in enumerate(row):
            color = GREY if r == 0 else (RED if cell in ("61 %", "1,5 %") else GREEN if cell == "0" else WHITE)
            d.text((xs[c], y), cell, font=F_LABEL if r else F_TEXT, fill=color, anchor="lm" if c == 0 else "mm")
        if r == 0:
            d.line((480, y + 35, 1560, y + 35), fill=GREY, width=2)
    d.text((W // 2, 800), "Время кадра — при 50 обычных телах, относительно дискретного режима 60 Гц.",
           font=F_TEXT, fill=GREY, anchor="mm")
    return img


# ------------------------------------------------------------------ рисунки отчёта
def report_figures(t_d60, t_ccd, targets):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, axes = plt.subplots(1, 2, figsize=(12, 3.6), dpi=170)
    for ax, im, title in zip(axes, (t_d60, t_ccd),
                             ("а) дискретный режим, 60 Гц: два снаряда 2000 px/с прошли сквозь стенку",
                              "б) CCD, 60 Гц: все снаряды отскочили, крест — точка удара")):
        ax.imshow(im)
        ax.set_title(title, fontsize=9, loc="left")
        ax.set_xticks([]); ax.set_yticks([])
    fig.tight_layout()
    fig.savefig(os.path.join(FIG, "step7_app_tunnel.png"))
    plt.close(fig)

    fig, axes = plt.subplots(1, 3, figsize=(12, 3.9), dpi=170)
    titles = ("а) дискретный, 60 Гц: 4 из 5 насквозь", "б) CCD, 60 Гц: удар в t_c у грани",
              "в) multi-rate 60/240 Гц: шаг 16,7 px")
    for ax, im, title in zip(axes, targets, titles):
        ax.imshow(im, interpolation="nearest")
        ax.set_title(title, fontsize=9, loc="left")
        ax.set_xticks([]); ax.set_yticks([])
    fig.tight_layout()
    fig.savefig(os.path.join(FIG, "step7_main_targets.png"))
    plt.close(fig)


# ------------------------------------------------------------------ main
def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    physsim = os.path.abspath(sys.argv[1])
    os.makedirs(RAW, exist_ok=True)
    os.makedirs(SHOTS, exist_ok=True)

    print("запись: сцена туннелирования")
    tun = {}
    for key, mode in (("d60", "discrete"), ("ccd", "ccd"), ("mr", "multirate")):
        d, _ = record(physsim, "t_" + key, 40, "--scene", "4", "--mode", mode, "--hz", "60")
        tun[key] = load_frames(d, 40)

    print("запись: основная сцена (по 12 с)")
    cnt = {}
    for key, mode in (("d60", "discrete"), ("mr", "multirate")):
        d, out = record(physsim, "m_" + key, 720, "--scene", "6", "--mode", mode, "--hz", "60", "--dots")
        cnt[key] = counters(out)
        to_intermediate(d, os.path.join(RAW, f"m_{key}.mp4"))

    print("запись: кадры у мишеней (кадр 19 — первый залп только что прошёл)")
    targets = []
    for key, mode in (("d60", "discrete"), ("ccd", "ccd"), ("mr", "multirate")):
        d, _ = record(physsim, "g_" + key, 20, "--scene", "6", "--mode", mode, "--hz", "60", "--dots")
        full = Image.open(os.path.join(d, "frame_00019.bmp")).convert("RGB")
        full.save(os.path.join(SHOTS, f"main_{key}.png"))
        crop = full.crop((820, 160, 1240, 540))
        targets.append(crop.resize((crop.width * 2, crop.height * 2), Image.NEAREST))

    for key in tun:
        tun[key][-1].save(os.path.join(SHOTS, f"tunnel_{key}.png"))

    print("сборка видео")
    out_mp4 = os.path.join(MEDIA, "ccd_multirate_demo.mp4")
    enc = subprocess.Popen(["ffmpeg", "-loglevel", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24",
                            "-s", f"{W}x{H}", "-r", str(FPS), "-i", "-", "-c:v", "libx264", "-preset", "veryfast",
                            "-crf", "20", "-pix_fmt", "yuv420p", "-movflags", "+faststart", out_mp4],
                           stdin=subprocess.PIPE)
    n_frames = 0

    def emit(img, times=1):
        nonlocal n_frames
        buf = img.tobytes()
        for _ in range(times):
            enc.stdin.write(buf)
            n_frames += 1

    emit(title_card(), 150)
    seg1_start = n_frames
    for img in tunnel_part(tun["d60"], tun["ccd"],
                           "1. Снаряд 2000 px/с против стенки 10 px: дискретный шаг vs CCD",
                           "Δt = 1/60 с → за шаг снаряд проходит 33 px, а «пробить» стенку можно, если шаг больше w + s = 18 px",
                           ("Дискретный, 60 Гц", "2 из 3 быстрых снарядов прошли сквозь стенку", RED),
                           ("CCD, 60 Гц", "все снаряды отскочили от передней грани", GREEN),
                           LANES_D60, LANES_CCD,
                           "Стоп-кадр. Жёлтые точки — положения на каждом шаге физики, красный крест — момент удара, найденный CCD."):
        emit(img)
    seg1_end = n_frames
    for img in tunnel_part(tun["d60"], tun["mr"],
                           "2. То же: 60 Гц vs multi-rate (быстрые тела шагают с частотой 240 Гц)",
                           "При 240 Гц шаг снаряда 8,3 px < 18 px: стенку уже нельзя перепрыгнуть или пересечь больше чем наполовину",
                           ("Дискретный, 60 Гц", "2 из 3 быстрых снарядов прошли сквозь стенку", RED),
                           ("Multi-rate: 60 Гц / быстрые 240 Гц", "все снаряды отскочили", GREEN),
                           LANES_D60, LANES_MR,
                           "Стоп-кадр. Справа точек в 4 раза больше: быстрые тела делают 4 подшага за один шаг обычных тел."):
        emit(img)
    for img in main_part(os.path.join(RAW, "m_d60.mp4"), os.path.join(RAW, "m_mr.mp4"), cnt["d60"], cnt["mr"]):
        emit(img)
    emit(summary_card(), 180)
    enc.stdin.close()
    enc.wait()
    print(f"  {out_mp4}: {n_frames} кадров, {n_frames / FPS:.1f} с")

    print("GIF первой части")
    gif = os.path.join(MEDIA, "tunnel_demo.gif")
    t0, t1 = seg1_start / FPS, seg1_end / FPS
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-ss", f"{t0:.3f}", "-t", f"{t1 - t0:.3f}", "-i", out_mp4,
                    "-vf", "fps=20,scale=960:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=96[p];"
                           "[b][p]paletteuse=dither=none", gif], check=True)

    print("рисунки отчёта")
    report_figures(tun["d60"][-1], tun["ccd"][-1], targets)

    shutil.rmtree(RAW)
    print("готово")


if __name__ == "__main__":
    main()
