#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Draws the Homebrew App Store art from assets/logo-512.png.

Writes icon.png (256x150), screen.png (848x208 banner) and screen1.png..screen3.png
(1280x720, from docs/images) into <out_dir>.
Run from the repository root: tools/make_store_art.py <out_dir>
"""
import sys
from PIL import Image, ImageDraw, ImageFont

BG_TOP = (28, 36, 52)
BG_BOTTOM = (12, 15, 22)
TEXT = (236, 240, 245)
MUTED = (148, 160, 178)
GREEN = (74, 222, 128)
FONT_B = "/usr/share/fonts/truetype/ubuntu/Ubuntu-B.ttf"
FONT_R = "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf"
SS = 4  # draw large, then scale down for clean edges


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def canvas(w, h):
    img = Image.new("RGB", (w * SS, h * SS))
    d = ImageDraw.Draw(img)
    for y in range(h * SS):
        d.line([(0, y), (w * SS, y)], fill=lerp(BG_TOP, BG_BOTTOM, y / (h * SS)))
    return img


def paste_logo(img, x, y, size):
    logo = Image.open("assets/logo-512.png").resize((size * SS, size * SS), Image.LANCZOS)
    img.paste(logo, (x * SS, y * SS), logo)


def title(d, x, y, px):
    """'BetterBedrock' in white and ' NX' in green; returns the width."""
    f = ImageFont.truetype(FONT_B, px * SS)
    d.text((x * SS, y * SS), "BetterBedrock", font=f, fill=TEXT)
    w = d.textlength("BetterBedrock", font=f)
    d.text((x * SS + w, y * SS), " NX", font=f, fill=GREEN)
    return (w + d.textlength(" NX", font=f)) / SS


def done(img, w, h, path):
    img.resize((w, h), Image.LANCZOS).save(path, optimize=True)


def icon(out):
    w, h = 256, 150
    img = canvas(w, h)
    paste_logo(img, (w - 104) // 2, 6, 104)
    d = ImageDraw.Draw(img)
    f = ImageFont.truetype(FONT_B, 26 * SS)
    tw = (d.textlength("BetterBedrock NX", font=f)) / SS
    title(d, (w - tw) / 2, 110, 26)
    done(img, w, h, f"{out}/icon.png")


def banner(out):
    w, h = 848, 208
    img = canvas(w, h)
    paste_logo(img, 28, 14, 180)
    d = ImageDraw.Draw(img)
    title(d, 226, 40, 58)
    f = ImageFont.truetype(FONT_R, 25 * SS)
    d.text((230 * SS, 118 * SS), "A Minecraft (Bedrock) toolkit for a modded Switch:", font=f, fill=MUTED)
    d.text((230 * SS, 150 * SS), "online play, your own server, Vibrant Visuals", font=f, fill=MUTED)
    done(img, w, h, f"{out}/screen.png")


def screenshots(out):
    for i, name in enumerate(["vibrant-visuals.jpg", "app-graphics.png", "app-online.png"], 1):
        Image.open(f"docs/images/{name}").convert("RGB").resize((1280, 720)).save(f"{out}/screen{i}.png", optimize=True)


if __name__ == "__main__":
    out = sys.argv[1]
    icon(out)
    banner(out)
    screenshots(out)
