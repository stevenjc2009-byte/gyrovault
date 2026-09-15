#!/usr/bin/env python3
"""Generate Gyrovault LiveArea art procedurally (Pillow).

Writes 8-bit indexed (mode 'P') PNGs, which the Vita LiveArea requires:
  sce_sys/icon0.png                        128x128  chrome ball in a steel maze square
  sce_sys/livearea/contents/bg.png         840x500  dark steel with a maze motif
  sce_sys/livearea/contents/startup.png    280x158  "Gyrovault" plate

Re-runnable: overwrites the files each time, then re-opens and verifies mode and size.
"""
from pathlib import Path
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parent.parent
ICON = ROOT / "sce_sys" / "icon0.png"
BG = ROOT / "sce_sys" / "livearea" / "contents" / "bg.png"
STARTUP = ROOT / "sce_sys" / "livearea" / "contents" / "startup.png"

SS = 4  # supersample factor for smooth edges


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def vertical_gradient(w, h, top, bottom):
    img = Image.new("RGB", (w, h))
    d = ImageDraw.Draw(img)
    for y in range(h):
        d.line([(0, y), (w, y)], fill=lerp(top, bottom, y / max(1, h - 1)))
    return img


def brushed_block(d, x0, y0, x1, y1, s):
    """Steel wall block with light top/left bevel and dark bottom/right edge."""
    d.rectangle([x0, y0, x1, y1], fill=(150, 156, 166))
    for i, yy in enumerate(range(int(y0), int(y1), max(1, 2 * s))):
        shade = 140 + (i * 37) % 22
        d.line([(x0, yy), (x1, yy)], fill=(shade, shade + 5, shade + 12))
    b = max(1, 2 * s)
    d.rectangle([x0, y0, x1, y0 + b], fill=(214, 220, 228))
    d.rectangle([x0, y0, x0 + b, y1], fill=(196, 202, 212))
    d.rectangle([x0, y1 - b, x1, y1], fill=(62, 66, 74))
    d.rectangle([x1 - b, y0, x1, y1], fill=(78, 82, 90))


def chrome_ball(img, cx, cy, r):
    """Chrome sphere with drop shadow, concentric shading and a specular highlight."""
    shadow = Image.new("L", img.size, 0)
    ImageDraw.Draw(shadow).ellipse(
        [cx - r + r * 0.25, cy - r + r * 0.35, cx + r + r * 0.25, cy + r + r * 0.35], fill=150)
    shadow = shadow.filter(ImageFilter.GaussianBlur(r * 0.25))
    img.paste((10, 10, 14), (0, 0), shadow)

    d = ImageDraw.Draw(img)
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(28, 30, 36))
    steps = 24
    for i in range(steps):
        t = i / (steps - 1)
        rr = (r - r * 0.06) * (1 - t * 0.92)
        ox = -r * 0.28 * t
        oy = -r * 0.30 * t
        col = lerp((70, 76, 88), (235, 240, 248), t ** 1.3)
        d.ellipse([cx + ox - rr, cy + oy - rr, cx + ox + rr, cy + oy + rr], fill=col)
    hr = r * 0.16
    hx, hy = cx - r * 0.38, cy - r * 0.40
    d.ellipse([hx - hr, hy - hr, hx + hr, hy + hr], fill=(255, 255, 255))


def load_font(size):
    for name in ("DejaVuSans-Bold.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                 "LiberationSans-Bold.ttf", "arialbd.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    return ImageFont.load_default(size=size)


def maze_motif(d, w, h, cell, s, offset=(0, 0)):
    """A fixed little maze pattern of steel blocks."""
    pattern = [
        "##########",
        "#....#...#",
        "#.##.#.#.#",
        "#.#....#.#",
        "#.#.####.#",
        "#...#....#",
        "###.#.##.#",
        "#.....#..#",
        "##########",
    ]
    ox, oy = offset
    for gy, row in enumerate(pattern):
        for gx, ch in enumerate(row):
            if ch == "#":
                x0, y0 = ox + gx * cell, oy + gy * cell
                if x0 < w and y0 < h:
                    brushed_block(d, x0, y0, x0 + cell - 1, y0 + cell - 1, s)


def make_icon():
    size = 128 * SS
    img = vertical_gradient(size, size, (56, 44, 34), (34, 26, 20))  # dark wood floor
    d = ImageDraw.Draw(img)
    cell = size // 8
    wall = [
        "########",
        "#......#",
        "#.####.#",
        "#.#....#",
        "#.#.##.#",
        "#......#",
        "#.#.#..#",
        "########",
    ]
    for gy, row in enumerate(wall):
        for gx, ch in enumerate(row):
            if ch == "#":
                brushed_block(d, gx * cell, gy * cell, (gx + 1) * cell - 1, (gy + 1) * cell - 1, SS)
    # goal hole with green glow
    gx, gy, gr = 5.5 * cell, 3.5 * cell, cell * 0.38
    for i in range(8, 0, -1):
        rr = gr + i * SS * 1.2
        d.ellipse([gx - rr, gy - rr, gx + rr, gy + rr], fill=lerp((34, 26, 20), (60, 220, 110), (8 - i) / 10))
    d.ellipse([gx - gr, gy - gr, gx + gr, gy + gr], fill=(6, 8, 6))
    chrome_ball(img, 2.9 * cell, 5.1 * cell, cell * 0.72)
    return img.resize((128, 128), Image.LANCZOS)


def make_bg():
    w, h = 840 * 2, 500 * 2
    img = vertical_gradient(w, h, (40, 44, 52), (14, 16, 20))
    d = ImageDraw.Draw(img)
    maze_motif(d, w, h, 90, 2, offset=(840, 120))
    # darken the maze towards the left so text/gate stays readable
    fade = Image.new("L", (w, h))
    fd = ImageDraw.Draw(fade)
    for x in range(w):
        fd.line([(x, 0), (x, h)], fill=int(255 * max(0.0, 1 - x / (w * 0.75))))
    dark = vertical_gradient(w, h, (40, 44, 52), (14, 16, 20))
    img.paste(dark, (0, 0), fade)
    chrome_ball(img, 1180, 580, 70)
    font = load_font(150)
    d = ImageDraw.Draw(img)
    d.text((84, 104), "GYROVAULT", font=font, fill=(0, 0, 0))
    d.text((76, 96), "GYROVAULT", font=font, fill=(222, 228, 236))
    d.text((80, 280), "Tilt to roll. Find the vault.", font=load_font(56), fill=(150, 200, 160))
    return img.resize((840, 500), Image.LANCZOS)


def make_startup():
    w, h = 280 * SS, 158 * SS
    img = Image.new("RGB", (w, h), (20, 22, 26))
    d = ImageDraw.Draw(img)
    # steel plate with bevel
    pad = 6 * SS
    d.rectangle([pad, pad, w - pad, h - pad], fill=(120, 126, 136))
    for i, yy in enumerate(range(pad, h - pad, 2 * SS)):
        shade = 112 + (i * 29) % 20
        d.line([(pad, yy), (w - pad, yy)], fill=(shade, shade + 5, shade + 12))
    b = 3 * SS
    d.rectangle([pad, pad, w - pad, pad + b], fill=(210, 216, 224))
    d.rectangle([pad, pad, pad + b, h - pad], fill=(190, 196, 206))
    d.rectangle([pad, h - pad - b, w - pad, h - pad], fill=(50, 54, 60))
    d.rectangle([w - pad - b, pad, w - pad, h - pad], fill=(64, 68, 76))
    # rivets
    for rx, ry in ((pad + 12 * SS, pad + 12 * SS), (w - pad - 12 * SS, pad + 12 * SS),
                   (pad + 12 * SS, h - pad - 12 * SS), (w - pad - 12 * SS, h - pad - 12 * SS)):
        r = 4 * SS
        d.ellipse([rx - r, ry - r, rx + r, ry + r], fill=(70, 74, 82))
        d.ellipse([rx - r + SS, ry - r + SS, rx + r - 2 * SS, ry + r - 2 * SS], fill=(200, 206, 214))
    font = load_font(46 * SS)
    text = "Gyrovault"
    tw, th = d.textbbox((0, 0), text, font=font)[2:]
    tx, ty = (w - tw) // 2, (h - th) // 2 - 6 * SS
    d.text((tx + 2 * SS, ty + 2 * SS), text, font=font, fill=(30, 32, 38))
    d.text((tx, ty), text, font=font, fill=(245, 248, 252))
    return img.resize((280, 158), Image.LANCZOS)


def save_indexed(img, path, size):
    path.parent.mkdir(parents=True, exist_ok=True)
    q = img.convert("RGB").quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    q.save(path, format="PNG", optimize=True)
    with Image.open(path) as check:
        if check.mode != "P" or check.size != size:
            raise RuntimeError(f"{path}: got mode={check.mode} size={check.size}, want P {size}")
        print(f"{path.relative_to(ROOT)}: mode={check.mode} size={check.size} bytes={path.stat().st_size}")


def main():
    save_indexed(make_icon(), ICON, (128, 128))
    save_indexed(make_bg(), BG, (840, 500))
    save_indexed(make_startup(), STARTUP, (280, 158))
    return 0


if __name__ == "__main__":
    sys.exit(main())
