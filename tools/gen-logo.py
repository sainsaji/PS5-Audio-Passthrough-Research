#!/usr/bin/env python3
"""Draws the Surround Sound Studio icon: a listener at the centre, sound rings
spreading out, and the five speakers of a 5.1 layout around them.

usage: python3 tools/gen-logo.py   (writes sample-app/sce_sys/icon0.png, 512x512)
Needs Pillow. SPDX-License-Identifier: GPL-3.0-or-later
"""
import math
import pathlib

from PIL import Image, ImageChops, ImageDraw, ImageFilter

SIZE = 512
SS = 4  # supersampling
S = SIZE * SS
C = S / 2

BG_TOP = (11, 16, 38)
BG_BOT = (20, 14, 44)
BLUE = (64, 168, 255)
CYAN = (120, 224, 255)
ORANGE = (255, 122, 72)
WHITE = (240, 246, 255)


def gradient():
    img = Image.new("RGB", (S, S))
    px = img.load()
    for y in range(S):
        t = y / (S - 1)
        row = tuple(int(BG_TOP[i] + (BG_BOT[i] - BG_TOP[i]) * t) for i in range(3))
        for x in range(S):
            px[x, y] = row
    return img


def glow(layer, radius, strength=1.0):
    blurred = layer.filter(ImageFilter.GaussianBlur(radius * SS))
    if strength != 1.0:
        a = blurred.getchannel("A").point(lambda v: min(255, int(v * strength)))
        blurred.putalpha(a)
    return blurred


def polar(angle_deg, radius):
    # 0 degrees is straight ahead (up); positive turns clockwise
    a = math.radians(angle_deg)
    return C + radius * math.sin(a), C - radius * math.cos(a)


def speaker(draw, angle, radius, colour, size=46):
    x, y = polar(angle, radius)
    w, h = size * SS, size * SS * 1.35
    box = Image.new("RGBA", (int(w * 2), int(h * 2)), (0, 0, 0, 0))
    d = ImageDraw.Draw(box)
    cx, cy = box.width / 2, box.height / 2
    d.rounded_rectangle([cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2], radius=w * 0.28, fill=colour)
    # driver cone
    d.ellipse([cx - w * 0.28, cy - w * 0.28 + h * 0.08, cx + w * 0.28, cy + w * 0.28 + h * 0.08],
              fill=(12, 18, 40, 255))
    d.ellipse([cx - w * 0.12, cy - w * 0.12 + h * 0.08, cx + w * 0.12, cy + w * 0.12 + h * 0.08],
              fill=colour)
    box = box.rotate(-angle, resample=Image.BICUBIC)  # face the listener
    return box, (int(x - box.width / 2), int(y - box.height / 2))


def main():
    img = gradient().convert("RGBA")

    # sound rings
    rings = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(rings)
    for i, (r, a, wdt) in enumerate([(84, 230, 7), (136, 170, 6), (188, 110, 5)]):
        rr = r * SS
        d.ellipse([C - rr, C - rr, C + rr, C + rr], outline=BLUE + (a,), width=wdt * SS)
    img = Image.alpha_composite(img, glow(rings, 10, 1.2))
    img = Image.alpha_composite(img, rings)

    # the listening position: warm core with a halo
    core = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(core)
    r = 40 * SS
    d.ellipse([C - r, C - r, C + r, C + r], fill=ORANGE + (255,))
    img = Image.alpha_composite(img, glow(core, 22, 1.6))
    img = Image.alpha_composite(img, core)
    hi = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(hi)
    r2 = 16 * SS
    d.ellipse([C - r2 - 8 * SS, C - r2 - 10 * SS, C + r2 - 8 * SS, C + r2 - 10 * SS], fill=(255, 214, 180, 200))
    img = Image.alpha_composite(img, hi.filter(ImageFilter.GaussianBlur(6 * SS)))

    # five speakers of a 5.1 layout (front L/R at 30 degrees, centre, surrounds at 110)
    for angle, colour in [(-30, CYAN), (30, CYAN), (0, WHITE), (-110, BLUE), (110, BLUE)]:
        box, pos = speaker(None, angle, 192 * SS, colour + (255,))
        halo = Image.new("RGBA", (S, S), (0, 0, 0, 0))
        halo.paste(box, pos, box)
        img = Image.alpha_composite(img, glow(halo, 12, 1.1))
        img = Image.alpha_composite(img, halo)

    # a subtle vignette keeps the corners calm
    vig = Image.new("L", (S, S), 0)
    d = ImageDraw.Draw(vig)
    d.ellipse([-S * 0.15, -S * 0.15, S * 1.15, S * 1.15], fill=255)
    vig = vig.filter(ImageFilter.GaussianBlur(S * 0.12))
    dark = Image.new("RGBA", (S, S), (4, 6, 16, 255))
    mask = ImageChops.invert(vig).point(lambda v: int(v * 0.55))
    img = Image.composite(dark, img, mask)

    out = img.resize((SIZE, SIZE), Image.LANCZOS).convert("RGB")
    dest = pathlib.Path(__file__).resolve().parent.parent / "sample-app" / "sce_sys" / "icon0.png"
    out.save(dest, optimize=True)
    print("wrote", dest)


if __name__ == "__main__":
    main()
