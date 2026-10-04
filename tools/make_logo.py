#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Draws the BedrockLink logo: a pixel-art stone block linked to a green chain.

Writes icon.jpg (256x256, the homebrew menu icon), romfs/logo.bmp (96x96, the
app header) and assets/logo-512.png (README / release art). Original artwork;
deterministic (fixed random seed). Run from the repository root.
"""
import math
import random
from PIL import Image, ImageDraw, ImageFilter

S = 1024  # draw large, then scale down for clean edges
BG_TOP = (28, 36, 52)
BG_BOTTOM = (12, 15, 22)
GREEN = (74, 222, 128)
GREEN_DARK = (22, 163, 74)


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def background():
    img = Image.new("RGB", (S, S), BG_BOTTOM)
    d = ImageDraw.Draw(img)
    for y in range(S):
        d.line([(0, y), (S, y)], fill=lerp(BG_TOP, BG_BOTTOM, y / S))
    return img


def cube(img, cx, cy, size, rng):
    """Isometric block of 8x8 stone texels per face."""
    d = ImageDraw.Draw(img)
    n = 8
    w = size  # half width of the cube's top diamond
    h = size / 2
    top = (cx, cy - h * 2)
    # unit vectors along the cube's edges in screen space
    ex = (w / n, h / n)        # to the right-down
    ey = (-w / n, h / n)       # to the left-down
    ez = (0, 2 * h / n)        # down

    def quad(o, a, b):
        return [o, (o[0] + a[0], o[1] + a[1]), (o[0] + a[0] + b[0], o[1] + a[1] + b[1]), (o[0] + b[0], o[1] + b[1])]

    def stone(base):
        v = rng.choice([-26, -16, -8, 0, 0, 6, 14, 22])
        return tuple(max(0, min(255, c + v)) for c in base)

    faces = [
        (top, ex, ey, (118, 120, 128)),                                         # top
        ((top[0] - w, top[1] + h), ex, ez, (84, 86, 94)),                       # left
        ((top[0], top[1] + 2 * h), (ex[0], -ex[1]), ez, (58, 60, 68)),          # right
    ]
    for origin, a, b, base in faces:
        for i in range(n):
            for j in range(n):
                o = (origin[0] + a[0] * i + b[0] * j, origin[1] + a[1] * i + b[1] * j)
                d.polygon(quad(o, a, b), fill=stone(base))
    # crisp outline
    outline = [top, (top[0] + w, top[1] + h), (top[0] + w, top[1] + 3 * h), (top[0], top[1] + 4 * h),
               (top[0] - w, top[1] + 3 * h), (top[0] - w, top[1] + h)]
    d.line(outline + [outline[0]], fill=(20, 22, 28), width=10, joint="curve")
    d.line([(top[0], top[1] + 2 * h), (top[0], top[1] + 4 * h)], fill=(20, 22, 28), width=8)
    d.line([(top[0] - w, top[1] + h), (top[0], top[1] + 2 * h), (top[0] + w, top[1] + h)], fill=(20, 22, 28), width=8)


def link(img, cx, cy, length, thick, angle_deg):
    """Two interlocked chain links, drawn as rounded capsules."""
    layer = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    a = math.radians(angle_deg)
    ux, uy = math.cos(a), math.sin(a)

    def capsule(px, py, color, width):
        half = length / 2
        x0, y0 = px - ux * half, py - uy * half
        x1, y1 = px + ux * half, py + uy * half
        r = length * 0.28
        # capsule outline = two arcs joined by parallel lines
        nx, ny = -uy * r, ux * r
        pts = []
        for k in range(0, 181, 6):
            t = math.radians(k)
            pts.append((x1 + ux * r * math.sin(t) + nx * math.cos(t), y1 + uy * r * math.sin(t) + ny * math.cos(t)))
        for k in range(180, 361, 6):
            t = math.radians(k)
            pts.append((x0 + ux * r * math.sin(t) + nx * math.cos(t), y0 + uy * r * math.sin(t) + ny * math.cos(t)))
        d.line(pts + [pts[0]], fill=color, width=width, joint="curve")

    off = length * 0.42
    capsule(cx - ux * off, cy - uy * off, GREEN_DARK + (255,), thick + 10)
    capsule(cx - ux * off, cy - uy * off, GREEN + (255,), thick)
    capsule(cx + ux * off, cy + uy * off, GREEN_DARK + (255,), thick + 10)
    capsule(cx + ux * off, cy + uy * off, GREEN + (255,), thick)
    glow = layer.filter(ImageFilter.GaussianBlur(18))
    img.paste(Image.new("RGB", (S, S), GREEN), (0, 0), glow.split()[3].point(lambda v: int(v * 0.35)))
    img.paste(layer, (0, 0), layer)


def rounded_mask(size, radius):
    m = Image.new("L", (size, size), 0)
    ImageDraw.Draw(m).rounded_rectangle([0, 0, size - 1, size - 1], radius=radius, fill=255)
    return m


def main():
    rng = random.Random(1987)
    img = background()
    cube(img, S * 0.43, S * 0.55, S * 0.28, rng)
    link(img, S * 0.71, S * 0.72, S * 0.19, 40, -45)

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
