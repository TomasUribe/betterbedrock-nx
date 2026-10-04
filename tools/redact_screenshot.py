#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Hides personal details in BedrockLink console screenshots before they are published.

Usage: tools/redact_screenshot.py <in.jpg> <out.jpg> [--box x0,y0,x1,y1 ...]
Without --box it covers the app's server name, address and port rows and the address
line of the "In Minecraft" box (1280x720 screenshots of the main screen). Each area is
pixelated, then blurred, so the text cannot be read back.
"""
import sys
from PIL import Image, ImageFilter

DEFAULT_BOXES = [
    (360, 374, 606, 418),   # Name value
    (360, 424, 606, 468),   # Address value
    (360, 474, 606, 518),   # Port value
    (690, 580, 1214, 610),  # "In Minecraft" address and port line
]


def redact(im, box):
    region = im.crop(box)
    w, h = region.size
    small = region.resize((max(1, w // 14), max(1, h // 14)), Image.BILINEAR)
    region = small.resize((w, h), Image.NEAREST).filter(ImageFilter.GaussianBlur(6))
    im.paste(region, box)


def main():
    src, dst = sys.argv[1], sys.argv[2]
    boxes = [tuple(int(v) for v in a.split(",")) for a in sys.argv[4::2]] if "--box" in sys.argv else DEFAULT_BOXES
    im = Image.open(src).convert("RGB")
    for box in boxes:
        redact(im, box)
    im.save(dst, quality=92)  # re-encoded: no metadata from the console is carried over


if __name__ == "__main__":
    main()
