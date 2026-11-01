#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 The NØR Browser authors
"""Generate the bundled NØR New-Tab backgrounds (1536x864 JPEG).

All images are ORIGINAL artworks generated procedurally by this script for
NØR Browser — no third-party photographs, no downloaded assets. License:
the generated IMAGES are dedicated CC0 1.0 / public domain by the NØR
authors (see resources/backgrounds/README.md); this script itself is
project code under GPL-3.0-or-later.

Design brief (matches the NØR identity): calm graphite/slate bases, quiet
violet accents, dark-cinematic and minimal-technical moods, one light pair
for light-scheme readability. No text, no logos, no watermarks.

Output: nor-browser/resources/backgrounds/nor-*.jpg (+ thumbs/*.jpg) and a
printed manifest (id, dark flag, avg luminance, file size) for C++.
"""
import os
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

W, H = 1536, 864
OUT = os.path.join(os.path.dirname(__file__), "..", "resources", "backgrounds")
THUMBS = os.path.join(OUT, "thumbs")
os.makedirs(THUMBS, exist_ok=True)

rng = np.random.default_rng(20261031)


# ---------------------------------------------------------------- helpers --
def value_noise(w, h, cell=64, seed=0):
    """Smooth value noise via a small random grid upscaled bicubically."""
    g = np.random.default_rng(seed)
    gw = max(2, int(np.ceil(w / cell)) + 1)
    gh = max(2, int(np.ceil(h / cell)) + 1)
    small = g.random((gh, gw)).astype(np.float32)
    img = Image.fromarray((small * 255).astype(np.uint8), "L")
    img = img.resize((w, h), Image.BICUBIC)
    return np.asarray(img).astype(np.float32) / 255.0


def fbm(w, h, cell=64, octaves=4, seed=0):
    """Fractal Brownian motion (sum of value noise octaves)."""
    acc = np.zeros((h, w), np.float32)
    amp, total = 1.0, 0.0
    c = cell
    for o in range(octaves):
        acc += amp * value_noise(w, h, cell=c, seed=seed + 17 * o)
        total += amp
        amp *= 0.55
        c = max(4, c // 2)
    return acc / total


def to_img(arr):
    return Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGB")


def add_grain(im, sigma=2.2, seed=0):
    """Subtle photographic grain (keeps big flat areas from banding)."""
    g = np.random.default_rng(seed)
    a = np.asarray(im).astype(np.float32)
    noise = g.normal(0.0, sigma, a.shape[:2])[:, :, None]
    return to_img(a + noise)


def vignette(im, strength=0.22):
    a = np.asarray(im).astype(np.float32)
    y, x = np.mgrid[0 : H, 0 : W]
    cx, cy = W / 2, H / 2
    d = np.sqrt(((x - cx) / (W / 2)) ** 2 + ((y - cy) / (H / 2)) ** 2)
    m = 1.0 - strength * np.clip(d - 0.55, 0, 1) ** 1.6
    return to_img(a * m[:, :, None])


def vertical_gradient(top, bottom):
    t = np.linspace(0.0, 1.0, H)[:, None, None]
    top = np.array(top, np.float32)
    bottom = np.array(bottom, np.float32)
    return np.repeat(top[None, None, :] + (bottom - top)[None, None, :] * t, W, axis=1)


def save(im, name, quality=80):
    path = os.path.join(OUT, name + ".jpg")
    im = im.filter(ImageFilter.GaussianBlur(0.4))  # kill single-pixel harshness
    im.save(path, "JPEG", quality=quality, optimize=True, progressive=True)
    # thumbnail: center crop to 16:9 then downscale
    th = im.copy()
    tw, thh = 240, 135
    ratio = max(tw / im.width, thh / im.height)
    nw, nh = int(im.width * ratio + 0.5), int(im.height * ratio + 0.5)
    th = th.resize((nw, nh), Image.LANCZOS).crop(((nw - tw) // 2, (nh - thh) // 2,
                                                  (nw - tw) // 2 + tw, (nh - thh) // 2 + thh))
    th.save(os.path.join(THUMBS, name + ".jpg"), "JPEG", quality=70, optimize=True)
    a = np.asarray(im.convert("L")).astype(np.float32)
    lum = float(a.mean())
    kb = os.path.getsize(path) / 1024
    print(f"{name:16s} dark={lum < 110!s:5s} lum={lum:6.1f}  {kb:7.1f} KB")
    return lum


def draw_glow(layer, xy, radius, color, alpha):
    d = ImageDraw.Draw(layer)
    x, y = xy
    d.ellipse([x - radius, y - radius, x + radius, y + radius],
              fill=color + (alpha,))


# ---------------------------------------------------------------- backgrounds --
def bg_aurora():
    """Deep graphite night with quiet violet aurora curtains + faint stars."""
    base = vertical_gradient((11, 13, 20), (23, 25, 34))
    im = to_img(base)
    glow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(glow)
    # aurora curtains: sine-modulated bands, additive violet/indigo
    for band, (y0, amp, freq, phase, col, a, wdt) in enumerate([
        (250, 70, 3.1, 0.4, (139, 92, 246), 66, 26),   # NØR violet
        (330, 55, 2.3, 2.2, (109, 84, 210), 48, 34),
        (430, 80, 2.7, 4.0, (86, 104, 190), 34, 44),
        (560, 46, 1.9, 1.1, (139, 92, 246), 24, 60),
    ]):
        xs = np.arange(W)
        ys = y0 + amp * np.sin(xs / W * freq * np.pi * 2 + phase) \
                  + amp * 0.35 * np.sin(xs / W * freq * 5.1 * np.pi + phase * 2)
        pts = list(zip(xs.tolist(), ys.tolist()))
        d.line(pts, fill=col + (a,), width=wdt)
    glow = glow.filter(ImageFilter.GaussianBlur(46))
    im = Image.alpha_composite(im.convert("RGBA"), glow)
    # stars, upper half
    st = ImageDraw.Draw(im)
    g = np.random.default_rng(7)
    for _ in range(150):
        x, y = int(g.random() * W), int(g.random() * H * 0.62)
        b = g.random()
        if b > 0.93:
            st.point((x, y), fill=(230, 233, 245, 235))
            st.point((x + 1, y), fill=(230, 233, 245, 90))
        else:
            st.point((x, y), fill=(200, 205, 220, 70 + int(90 * b)))
    return vignette(add_grain(im.convert("RGB"), 1.8, 3), 0.20)


def bg_nebula():
    """Deep-space nebula: layered slate-blue/violet clouds over a starfield."""
    base = vertical_gradient((7, 9, 15), (13, 15, 23))
    cloud1 = fbm(W, H, cell=220, octaves=5, seed=11)
    cloud2 = fbm(W, H, cell=140, octaves=5, seed=29)
    r = base[:, :, 0] + 34 * np.clip(cloud1 - 0.35, 0, 1) ** 1.3
    g = base[:, :, 1] + 30 * np.clip(cloud1 - 0.35, 0, 1) ** 1.3
    b = base[:, :, 2] + 46 * np.clip(cloud1 - 0.35, 0, 1) ** 1.2 \
        + 30 * np.clip(cloud2 - 0.45, 0, 1) ** 1.5
    # faint violet breath in the middle-left region
    mask = np.clip(1.2 - np.sqrt(((np.mgrid[0:H, 0:W][1] - W * 0.38) / (W * 0.42)) ** 2
                                 + ((np.mgrid[0:H, 0:W][0] - H * 0.42) / (H * 0.5)) ** 2), 0, 1)
    g += 20 * mask * cloud2
    b += 34 * mask * cloud2
    im = to_img(np.stack([r, g, b], axis=2)).filter(ImageFilter.GaussianBlur(2))
    # starfield: power-law brightness, few with diffraction spikes
    st = ImageDraw.Draw(im)
    g2 = np.random.default_rng(5)
    n = 1500
    xs = g2.random(n) * W
    ys = g2.random(n) * H
    bs = g2.random(n) ** 3.0
    for x, y, b in zip(xs, ys, bs):
        v = int(90 + 160 * b)
        st.point((int(x), int(y)), fill=(v, v, min(255, v + 12)))
    for _ in range(9):  # bright ones
        x, y = g2.random() * W, g2.random() * H
        v = 235
        st.point((int(x), int(y)), fill=(v, v, 250))
        st.line([(x - 7, y), (x + 7, y)], fill=(150, 155, 190, 120), width=1)
        st.line([(x, y - 7), (x, y + 7)], fill=(150, 155, 190, 120), width=1)
    return vignette(add_grain(im, 1.6, 8), 0.24)


def bg_ridge():
    """Dark cinematic mountain ridges at violet dusk, thin moon."""
    sky = vertical_gradient((26, 28, 38), (40, 42, 54))
    # Break 8-bit gradient banding: large-scale organic drift + grain dither.
    drift = (fbm(W, H, cell=260, octaves=3, seed=37) - 0.5) * 5.0
    sky = sky + drift[:, :, None]
    im = to_img(sky)
    im = add_grain(im, 2.8, 12)
    glow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    draw_glow(glow, (W * 0.62, H * 0.78), 300, (124, 92, 214), 34)   # horizon violet
    draw_glow(glow, (W * 0.62, H * 0.80), 130, (150, 120, 230), 30)
    draw_glow(glow, (W * 0.80, H * 0.16), 46, (235, 238, 248), 90)   # moon core
    glow = glow.filter(ImageFilter.GaussianBlur(60))
    im = Image.alpha_composite(im.convert("RGBA"), glow).convert("RGB")
    # ridge layers: far (lighter, atmospheric) -> near (darkest)
    xs = np.arange(W)
    for i, (base_y, amp, seed, tone) in enumerate([
        (H * 0.52, 90, 41, (44, 48, 60)),
        (H * 0.62, 110, 43, (34, 37, 47)),
        (H * 0.72, 130, 47, (25, 27, 35)),
        (H * 0.84, 150, 53, (16, 17, 23)),
    ]):
        n = fbm(W, 8, cell=90, octaves=4, seed=seed)[0]
        ys = base_y + amp * (n - 0.5)
        d = ImageDraw.Draw(im)
        pts = [(0, H)] + list(zip(xs.tolist(), ys.tolist())) + [(W, H)]
        d.polygon(pts, fill=tone)
        im = im.filter(ImageFilter.GaussianBlur(0.6))
    return vignette(add_grain(im, 1.0, 13), 0.18)


def bg_zenith():
    """Calm light dawn sky — the light-scheme counterpart. Soft, airy."""
    base = vertical_gradient((233, 236, 244), (246, 243, 237))
    c = fbm(W, H, cell=300, octaves=4, seed=71)
    w = np.clip(c - 0.42, 0, 1) ** 1.2
    a = base + np.stack([10 * w, 10 * w, 12 * w], axis=2)  # faint cloud lift
    im = to_img(a)
    glow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    draw_glow(glow, (W * 0.82, H * 0.18), 260, (255, 252, 244), 95)  # soft sun
    glow = glow.filter(ImageFilter.GaussianBlur(90))
    im = Image.alpha_composite(im.convert("RGBA"), glow).convert("RGB")
    # two long cloud wisps
    d = ImageDraw.Draw(im, "RGBA")
    xs = np.arange(W)
    for y0, seed, alpha in [(H * 0.30, 91, 20), (H * 0.52, 97, 15), (H * 0.70, 101, 12)]:
        n = fbm(W, 4, cell=160, octaves=3, seed=seed)[0]
        ys = y0 + 26 * (n - 0.5)
        pts = list(zip(xs.tolist(), ys.tolist()))
        d.line(pts, fill=(255, 255, 255, alpha), width=16)
    im = im.filter(ImageFilter.GaussianBlur(18))
    return add_grain(im, 0.9, 17)


def bg_mesh():
    """Minimal technical mesh on light gray — nodes + hairlines, top-right."""
    base = np.zeros((H, W, 3), np.float32)
    base[:, :] = (239, 241, 245)
    im = to_img(base)
    g = np.random.default_rng(131)
    layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    nodes = []
    for _ in range(95):
        x = g.random() * W
        y = g.random() * H
        # density increases toward top-right corner
        keep = (x / W * 0.5 + (1 - y / H) * 0.5 + 0.25) > g.random() * 0.9
        if keep:
            nodes.append((x, y))
    for i, (x1, y1) in enumerate(nodes):
        for x2, y2 in nodes[i + 1:]:
            dist = ((x1 - x2) ** 2 + (y1 - y2) ** 2) ** 0.5
            if dist < 190:
                fade = 1.0 - dist / 190.0
                a = int(46 * fade)
                d.line([(x1, y1), (x2, y2)], fill=(72, 82, 98, a), width=1)
    for x, y in nodes:
        d.ellipse([x - 2.2, y - 2.2, x + 2.2, y + 2.2], fill=(96, 106, 124, 110))
    # one quiet violet node cluster accent
    ax, ay = W * 0.74, H * 0.26
    for x, y in nodes:
        if (x - ax) ** 2 + (y - ay) ** 2 < 130 ** 2:
            d.ellipse([x - 2.8, y - 2.8, x + 2.8, y + 2.8], fill=(139, 92, 246, 150))
    layer = layer.filter(ImageFilter.GaussianBlur(0.4))
    im = Image.alpha_composite(im.convert("RGBA"), layer).convert("RGB")
    return add_grain(im, 1.0, 19)


def bg_slate():
    """Abstract professional: soft slate/graphite planes + one violet tide."""
    base = np.zeros((H, W, 3), np.float32)
    base[:, :] = (35, 38, 44)
    im = to_img(base)
    layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    # large rotated soft planes
    for (cx, cy, wd, ht, ang, col, a) in [
        (W * 0.22, H * 0.30, 900, 520, 24, (54, 59, 69), 255),
        (W * 0.78, H * 0.24, 820, 460, -18, (28, 30, 36), 235),
        (W * 0.55, H * 0.78, 1000, 520, 8, (44, 49, 58), 205),
        (W * 0.86, H * 0.62, 560, 700, 32, (74, 64, 118), 120),  # violet tide
    ]:
        box = [(-wd / 2, -ht / 2), (wd / 2, ht / 2)]
        poly = [(cx + x * np.cos(ang) - y * np.sin(ang),
                 cy + x * np.sin(ang) + y * np.cos(ang)) for x, y in box]
        d.polygon(poly, fill=col + (a,))
    layer = layer.filter(ImageFilter.GaussianBlur(120))
    im = Image.alpha_composite(im.convert("RGBA"), layer).convert("RGB")
    # fine texture lift so planes read as material, not smudge
    tex = fbm(W, H, cell=8, octaves=2, seed=211)
    a = np.asarray(im).astype(np.float32)
    a += (tex[:, :, None] - 0.5) * 3.5
    return vignette(add_grain(to_img(a), 0.7, 23), 0.16)


def bg_dune():
    """Warm dark dunes at twilight — calm, natural, low-detail."""
    base = vertical_gradient((30, 25, 19), (48, 41, 30))
    drift = (fbm(W, H, cell=300, octaves=3, seed=303) - 0.5) * 5.0
    base = base + drift[:, :, None]
    im = add_grain(to_img(base), 2.9, 28)
    # last light on the horizon
    glow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    draw_glow(glow, (W * 0.30, H * 0.26), 260, (150, 116, 82), 46)
    glow = glow.filter(ImageFilter.GaussianBlur(120))
    im = Image.alpha_composite(im.convert("RGBA"), glow).convert("RGB")
    xs = np.arange(W)
    d = ImageDraw.Draw(im)
    for i, (base_y, amp, seed, tone) in enumerate([
        (H * 0.42, 60, 307, (58, 49, 36)),
        (H * 0.58, 78, 311, (48, 40, 29)),
        (H * 0.74, 92, 313, (39, 32, 23)),
        (H * 0.90, 84, 317, (29, 24, 17)),
    ]):
        n = fbm(W, 8, cell=130, octaves=3, seed=seed)[0]
        ys = base_y + amp * (n - 0.5) + 14 * np.sin(xs / W * (1.3 + i) * np.pi)
        pts = [(0, H)] + list(zip(xs.tolist(), ys.tolist())) + [(W, H)]
        d.polygon(pts, fill=tone)
        im = im.filter(ImageFilter.GaussianBlur(1.2))
    # ridge highlights: soft crest light on each dune
    hl = ImageDraw.Draw(im, "RGBA")
    for i, (base_y, amp, seed) in enumerate([
        (H * 0.42, 60, 307), (H * 0.58, 78, 311), (H * 0.74, 92, 313),
    ]):
        n = fbm(W, 8, cell=130, octaves=3, seed=seed)[0]
        ys = base_y + amp * (n - 0.5) + 14 * np.sin(xs / W * (1.3 + i) * np.pi)
        pts = list(zip(xs.tolist(), ys.tolist()))
        hl.line(pts, fill=(126, 102, 74, 40), width=3)
    im = im.filter(ImageFilter.GaussianBlur(2.5))
    return vignette(add_grain(im, 1.8, 29), 0.20)


def bg_graphite():
    """Identity piece: graphite field with quiet topographic contours."""
    base = np.zeros((H, W, 3), np.float32)
    base[:, :] = (23, 25, 30)
    im = to_img(base)
    layer = ImageDraw.Draw(im, "RGBA")
    field = fbm(W, H, cell=180, octaves=4, seed=401)
    # horizontal contour polylines displaced by the field (topographic feel)
    accent_row = 14
    for row, y in enumerate(np.linspace(40, H - 40, 26)):
        disp = (field[int(np.clip(y, 0, H - 1)), :] - 0.5) * 120
        pts = list(zip(np.arange(W).tolist(), (y + disp).tolist()))
        if row == accent_row:
            layer.line(pts, fill=(139, 92, 246, 26), width=8)   # soft under-glow
            layer.line(pts, fill=(139, 92, 246, 70), width=4)   # smooth core
        else:
            a = 12 + int(10 * (row % 3 == 0))
            layer.line(pts, fill=(70, 76, 88, a), width=2)
    im = im.filter(ImageFilter.GaussianBlur(1.4))
    # soft corner depth
    glow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    draw_glow(glow, (W * 0.12, H * 0.88), 320, (16, 17, 22), 120)
    draw_glow(glow, (W * 0.90, H * 0.10), 260, (30, 33, 40), 90)
    glow = glow.filter(ImageFilter.GaussianBlur(130))
    im = Image.alpha_composite(im.convert("RGBA"), glow).convert("RGB")
    return add_grain(im, 1.2, 31)


# ------------------------------------------------------------------ run --
manifest = []
for name, fn, q in [
    ("nor-aurora", bg_aurora, 84),
    ("nor-nebula", bg_nebula, 82),
    ("nor-ridge", bg_ridge, 86),
    ("nor-zenith", bg_zenith, 90),
    ("nor-mesh", bg_mesh, 88),
    ("nor-slate", bg_slate, 84),
    ("nor-dune", bg_dune, 86),
    ("nor-graphite", bg_graphite, 88),
]:
    lum = save(fn(), name, quality=q)
    manifest.append((name, lum < 110.0, round(lum, 1)))

print("\nmanifest:", manifest)
