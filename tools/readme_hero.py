#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Makes docs/assets/rices.webp, the picture at the top of the README, from the rices'
screenshots: the first one large, the other three stacked beside it, a little apart and with
rounded corners, on a transparent background so that it sits on a light or a dark page alike.

    tools/readme_hero.py [OUTPUT]

Run it from the top of the source tree after retaking one of the screenshots below. Needs Pillow."""
import sys

from PIL import Image, ImageDraw

SHOTS = [
    "rices/washi/screenshots/washi.jpg",
    "rices/washi/screenshots/sumi-firefox.jpg",
    "rices/mochi/screenshots/mochi.jpg",
    "rices/hibana/screenshots/hibana.jpg",
]
HEIGHT = 1080  # of the large picture; the small ones share it, gaps included
GAP = 24
RADIUS = 18
OUTLINE = (128, 128, 128, 90)  # so that washi's pale paper keeps its edge on a white page
QUALITY = 88


def rounded(image, radius):
    """The image with its corners cut round, transparent outside them, and a faint outline."""
    size = (image.width * 4, image.height * 4)
    box = (0, 0, size[0] - 1, size[1] - 1)
    mask = Image.new("L", size, 0)
    ImageDraw.Draw(mask).rounded_rectangle(box, radius * 4, fill=255)
    outline = Image.new("RGBA", size, (0, 0, 0, 0))
    ImageDraw.Draw(outline).rounded_rectangle(box, radius * 4, outline=OUTLINE, width=8)
    image = image.convert("RGBA")
    image.putalpha(mask.resize(image.size, Image.LANCZOS))
    image.alpha_composite(outline.resize(image.size, Image.LANCZOS))
    return image


def main():
    if len(sys.argv) > 2:
        sys.exit(__doc__)
    output = sys.argv[1] if len(sys.argv) == 2 else "docs/assets/rices.webp"
    shots = [Image.open(path).convert("RGB") for path in SHOTS]
    aspect = shots[0].width / shots[0].height
    large = (round(HEIGHT * aspect), HEIGHT)
    small_height = (HEIGHT - 2 * GAP) // 3
    small = (round(small_height * aspect), small_height)
    canvas = Image.new("RGBA", (large[0] + GAP + small[0], HEIGHT), (0, 0, 0, 0))
    canvas.alpha_composite(rounded(shots[0].resize(large, Image.LANCZOS), RADIUS))
    for row, shot in enumerate(shots[1:]):
        tile = rounded(shot.resize(small, Image.LANCZOS), RADIUS)
        canvas.alpha_composite(tile, (large[0] + GAP, row * (small_height + GAP)))
    canvas.save(output, quality=QUALITY, method=6)


if __name__ == "__main__":
    main()
