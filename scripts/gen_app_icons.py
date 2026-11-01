#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 The NØR Browser authors
"""Renders the NØR Browser application icon to PNG sizes for Linux
(hicolor) and a Windows .ico. Run with a Python that has Pillow:

    python3 scripts/gen_app_icons.py
"""
import os

from PIL import Image, ImageDraw

ROOT = os.path.join(os.path.dirname(__file__), "..")
HICOLOR = os.path.join(ROOT, "packaging", "linux", "icons", "hicolor")
WINDOWS = os.path.join(ROOT, "packaging", "windows")

BG = (22, 18, 31, 255)          # #16121F
BORDER = (139, 92, 246, 255)    # #8B5CF6
TEXT = (232, 228, 240, 255)     # #E8E4F0


def draw_icon(size: int) -> Image.Image:
    s = size * 8  # supersample for smooth curves
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    radius = s * 0.21
    d.rounded_rectangle([0, 0, s - 1, s - 1], radius=radius, fill=BG)
    # border ring
    bw = max(1, int(s * 0.055))
    d.rounded_rectangle([bw // 2, bw // 2, s - 1 - bw // 2, s - 1 - bw // 2],
                        radius=radius - bw // 2, outline=BORDER, width=bw)

    # N letterform
    nw = s * 0.115
    x0 = s * 0.24
    x1 = x0 + nw
    y0, y1 = s * 0.30, s * 0.70
    d.line([(x0, y1), (x0, y0), (x1, y1), (x1, y0)], fill=TEXT, width=int(nw * 0.72),
           joint="curve")

    # O with a slash (Ø)
    cx, cy = s * 0.66, s * 0.5
    r = s * 0.20
    lw = max(2, int(s * 0.075))
    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=BORDER, width=lw)
    d.line([(cx - r * 0.72, cy + r * 0.72), (cx + r * 0.72, cy - r * 0.72)],
           fill=BORDER, width=lw)

    return img.resize((size, size), Image.LANCZOS)


def main() -> None:
    sizes = [16, 22, 24, 32, 48, 64, 128, 256]
    images = {}
    for size in sizes:
        img = draw_icon(size)
        images[size] = img
        out_dir = os.path.join(HICOLOR, f"{size}x{size}", "apps")
        os.makedirs(out_dir, exist_ok=True)
        out = os.path.join(out_dir, "nor-browser.png")
        img.save(out)
        print("wrote", out)

    os.makedirs(WINDOWS, exist_ok=True)
    ico_path = os.path.join(WINDOWS, "nor-browser.ico")
    images[256].save(ico_path, format="ICO",
                     sizes=[(s, s) for s in (16, 24, 32, 48, 64, 128, 256)])
    print("wrote", ico_path)


if __name__ == "__main__":
    main()
