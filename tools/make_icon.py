#!/usr/bin/env python3
"""Generate the USB-Switch application icon (multi-resolution .ico).

Run:  python tools/make_icon.py
Out:  resources/usb-switch.ico  (16/24/32/48/64/128/256 px)

The icon is drawn procedurally (no external assets): a rounded blue tile with
a white "eject" glyph (triangle + bar), the classic symbol for safe removal.
"""

from __future__ import annotations

import os
import sys

from PIL import Image, ImageDraw

S = 1024  # supersample canvas, downscaled per size for crisp edges
SIZES = [(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]

TOP = (56, 116, 255)     # blue-500
BOTTOM = (23, 37, 84)    # blue-950
WHITE = (255, 255, 255, 255)


def gradient_background(size: int) -> Image.Image:
    bg = Image.new("RGBA", (size, size))
    d = ImageDraw.Draw(bg)
    for y in range(size):
        t = y / (size - 1)
        c = tuple(int(TOP[i] + (BOTTOM[i] - TOP[i]) * t) for i in range(3)) + (255,)
        d.line([(0, y), (size, y)], fill=c)
    return bg


def rounded_mask(size: int, radius: int) -> Image.Image:
    m = Image.new("L", (size, size), 0)
    ImageDraw.Draw(m).rounded_rectangle([0, 0, size - 1, size - 1], radius=radius, fill=255)
    return m


def draw_icon() -> Image.Image:
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    img.paste(gradient_background(S), (0, 0), rounded_mask(S, int(S * 0.22)))

    d = ImageDraw.Draw(img)
    cx = S // 2

    # Eject glyph: upward triangle over a bar, optically centered.
    tri_w = int(S * 0.44)
    tri_h = int(S * 0.34)
    base_y = int(S * 0.60)
    apex_y = base_y - tri_h
    d.polygon(
        [(cx, apex_y), (cx - tri_w // 2, base_y), (cx + tri_w // 2, base_y)],
        fill=WHITE,
    )

    bar_w = int(S * 0.40)
    bar_h = int(S * 0.075)
    bar_y = int(S * 0.655)
    d.rounded_rectangle(
        [cx - bar_w // 2, bar_y, cx + bar_w // 2, bar_y + bar_h],
        radius=bar_h // 2,
        fill=WHITE,
    )
    return img


def main() -> int:
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = os.path.join(root, "resources", "usb-switch.ico")
    os.makedirs(os.path.dirname(out), exist_ok=True)

    img = draw_icon()
    img.resize((256, 256), Image.LANCZOS).save(out, format="ICO", sizes=SIZES)
    print(f"wrote {out} ({os.path.getsize(out)} bytes, sizes={SIZES})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
