#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Draws the BetterBedrock NX logo: a pixel-art grass block with a green wrench.

Writes icon.jpg (256x256, the homebrew menu icon), romfs/logo.bmp (96x96, the
app header) and assets/logo-512.png (README / release art). Original artwork
(procedural texels, fixed random seed). Run from the repository root.
"""
import math
import random
from PIL import Image, ImageDraw, ImageFilter

S = 1024  # draw large, then scale down for clean edges
BG_TOP = (28, 36, 52)
BG_BOTTOM = (12, 15, 22)
GREEN = (74, 222, 128)
GREEN_DARK = (22, 163, 74)
OUTLINE = (20, 22, 28)


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def background():
    img = Image.new("RGB", (S, S), BG_BOTTOM)
    d = ImageDraw.Draw(img)
    for y in range(S):
        d.line([(0, y), (S, y)], fill=lerp(BG_TOP, BG_BOTTOM, y / S))
    return img


def jitter(rng, base, spread):
    v = rng.choice(spread)
    return tuple(max(0, min(255, c + v)) for c in base)


def cube(img, cx, cy, size, rng):
    """Isometric grass block: 8x8 texels per face, grass on top and a grass fringe."""
    d = ImageDraw.Draw(img)
    n = 8
    w, h = size, size / 2
    top = (cx, cy - h * 2)
    ex = (w / n, h / n)        # to the right-down
    ey = (-w / n, h / n)       # to the left-down
    ez = (0, 2 * h / n)        # down

    def quad(o, a, b):
        return [o, (o[0] + a[0], o[1] + a[1]), (o[0] + a[0] + b[0], o[1] + a[1] + b[1]), (o[0] + b[0], o[1] + b[1])]

    grass_top = (88, 168, 64)
    shades = {"left": 0.78, "right": 0.6}
    dirt = (134, 96, 66)
    grass_side = (78, 150, 56)
    fringe = [rng.choice([1, 2, 2, 3]) for _ in range(n)]  # grass rows hanging over each column

    for i in range(n):
        for j in range(n):
            o = (top[0] + ex[0] * i + ey[0] * j, top[1] + ex[1] * i + ey[1] * j)
            d.polygon(quad(o, ex, ey), fill=jitter(rng, grass_top, [-22, -12, -6, 0, 0, 8, 16]))
    for name, origin, a in (("left", (top[0] - w, top[1] + h), ex), ("right", (top[0], top[1] + 2 * h), (ex[0], -ex[1]))):
        k = shades[name]
        for i in range(n):
            for j in range(n):
                base = grass_side if j < fringe[i] else dirt
                if j >= fringe[i] and rng.random() < 0.08:
                    base = (112, 112, 110)  # a pebble
                c = jitter(rng, base, [-18, -10, -4, 0, 0, 6, 12])
                c = tuple(int(v * k) for v in c)
                o = (origin[0] + a[0] * i + ez[0] * j, origin[1] + a[1] * i + ez[1] * j)
                d.polygon(quad(o, a, ez), fill=c)
    outline = [top, (top[0] + w, top[1] + h), (top[0] + w, top[1] + 3 * h), (top[0], top[1] + 4 * h),
               (top[0] - w, top[1] + 3 * h), (top[0] - w, top[1] + h)]
    d.line(outline + [outline[0]], fill=OUTLINE, width=10, joint="curve")
    d.line([(top[0], top[1] + 2 * h), (top[0], top[1] + 4 * h)], fill=OUTLINE, width=8)
    d.line([(top[0] - w, top[1] + h), (top[0], top[1] + 2 * h), (top[0] + w, top[1] + h)], fill=OUTLINE, width=8)


def wrench(img, cx, cy, length, thick, angle_deg):
    """An open-ended wrench: a handle with a ring head and a notch, glowing green."""
    layer = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    a = math.radians(angle_deg)
    ux, uy = math.cos(a), math.sin(a)
    x0, y0 = cx - ux * length / 2, cy - uy * length / 2    # handle end
    x1, y1 = cx + ux * length / 2, cy + uy * length / 2    # head centre
    r = thick * 1.45

    def shape(color, grow):
        d.line([(x0, y0), (x1, y1)], fill=color, width=int(thick + 2 * grow))
        d.ellipse([x0 - thick / 2 - grow, y0 - thick / 2 - grow, x0 + thick / 2 + grow, y0 + thick / 2 + grow], fill=color)
        d.ellipse([x1 - r - grow, y1 - r - grow, x1 + r + grow, y1 + r + grow], fill=color)

    shape(GREEN_DARK + (255,), 10)
    shape(GREEN + (255,), 0)
    # the jaw: a notch cut from the head along the handle's direction
    jw = thick * 0.62
    px, py = -uy, ux
    notch = [(x1 + px * jw / 2, y1 + py * jw / 2), (x1 + ux * r * 1.6 + px * jw / 2, y1 + uy * r * 1.6 + py * jw / 2),
             (x1 + ux * r * 1.6 - px * jw / 2, y1 + uy * r * 1.6 - py * jw / 2), (x1 - px * jw / 2, y1 - py * jw / 2)]
    d.polygon(notch, fill=(0, 0, 0, 0))
    d.ellipse([x1 - jw / 2, y1 - jw / 2, x1 + jw / 2, y1 + jw / 2], fill=(0, 0, 0, 0))
    glow = layer.filter(ImageFilter.GaussianBlur(18))
    img.paste(Image.new("RGB", (S, S), GREEN), (0, 0), glow.split()[3].point(lambda v: int(v * 0.35)))
    img.paste(layer, (0, 0), layer)


def rounded_mask(size, radius):
    m = Image.new("L", (size, size), 0)
    ImageDraw.Draw(m).rounded_rectangle([0, 0, size - 1, size - 1], radius=radius, fill=255)
    return m


def main():
    rng = random.Random(2026)
    img = background()
    cube(img, S * 0.43, S * 0.55, S * 0.28, rng)
    wrench(img, S * 0.66, S * 0.69, S * 0.40, 60, -45)

    full = img.resize((512, 512), Image.LANCZOS)
    rgba = full.convert("RGBA")
    rgba.putalpha(rounded_mask(512, 96))
    rgba.save("assets/logo-512.png")
    img.resize((256, 256), Image.LANCZOS).save("icon.jpg", quality=95)
    # The app header draws the logo on its own background, so a square BMP is enough.
    img.resize((96, 96), Image.LANCZOS).save("romfs/logo.bmp")
    print("wrote icon.jpg, romfs/logo.bmp, assets/logo-512.png")


if __name__ == "__main__":
    main()
