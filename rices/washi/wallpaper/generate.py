#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""washi's wallpapers: handmade paper that a cat walked across, with a hanko seal in the corner.

    python3 generate.py washi washi.jpg [--size 3840x2160] [--seed 2] [--seal-font FONT]
    python3 generate.py sumi sumi.jpg ...
    python3 generate.py logo logo.png --size 512x512

washi is cream paper by day, sumi the same sheet soaked in ink by night. The paw prints are
paw's logo, its four toe beans and its pad, stamped in caramel along a cat's walk; the seal
reads 肉球 (nikukyū, paw pads). It draws with pycairo and Pillow only, from a seed, so a command
gives the same picture every time. The seal needs a font with 肉 and 球 (the shipped pictures
use Yuji Syuku, OFL); without one it carries the paw instead.
"""
import argparse
import math
import random

import cairo
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

VARIANTS = {
    "washi": {
        "paper": (244, 240, 243),
        "mottle": 7,                      # how much the paper's brightness wanders, of 255
        "fibre_light": (255, 253, 255), "fibre_dark": (186, 180, 206),
        "fleck": (120, 128, 162), "fibre_strength": 1.0,
        "ink": [(238, 150, 183), (126, 178, 230)],  # sakura toes, a sky-blue heel
        "seal": (214, 92, 135),
        "text": (54, 60, 88),
        "glow": None,
        "vignette": 0.05,
        "grain": 9,
    },
    "sumi": {
        "paper": (22, 18, 16),
        "mottle": 6,
        "fibre_light": (58, 48, 41), "fibre_dark": (8, 6, 5),
        "fleck": (90, 70, 52), "fibre_strength": 0.5,
        "ink": [(214, 160, 110), (168, 118, 80)],  # the beans lit by a lamp
        "seal": (196, 72, 58),
        "text": (232, 220, 203),
        "glow": (200, 145, 95),
        "vignette": 0.30,
        "grain": 7,
    },
}

# paw's logo (docs/assets/logo.svg) in its 256-unit box: the four toe beans as ellipses (centre,
# radii, rotation in degrees) and the pad's outline. A print is these, the parts that touch,
# the beans a third larger than the logo's (whose fur leaves them small) so that they read as
# toes at a print's size.
BEAN_SCALE = 1.34
BEANS = [((70, 110), (14, 17), -28), ((106, 80), (15, 18), -10),
         ((150, 80), (15, 18), 10), ((186, 110), (14, 17), 28)]
PAD = [("M", 128, 128), ("C", 153, 128, 175, 151, 176, 176), ("C", 177, 195, 170, 207, 160, 207),
       ("C", 152, 207, 149, 200, 144, 200), ("C", 139, 200, 138, 210, 128, 210),
       ("C", 118, 210, 117, 200, 112, 200), ("C", 107, 200, 104, 207, 96, 207),
       ("C", 86, 207, 79, 195, 80, 176), ("C", 81, 151, 103, 128, 128, 128)]
PRINT_CENTRE = (128, 135)


def to_pil(surface):
    """A cairo ARGB32 surface as a Pillow RGBA image."""
    width, height = surface.get_width(), surface.get_height()
    return Image.frombuffer("RGBA", (width, height), bytes(surface.get_data()), "raw", "BGRa",
                            surface.get_stride(), 1)


def smooth_noise(rng, size, cells, blur):
    """Value noise: a grid of `cells` random greys stretched over `size` and blurred."""
    width, height = size
    columns = max(2, cells)
    rows = max(2, round(cells * height / width))
    grid = Image.new("L", (columns, rows))
    grid.putdata([rng.randrange(256) for _ in range(columns * rows)])
    return grid.resize(size, Image.BICUBIC).filter(ImageFilter.GaussianBlur(blur))


def fractal_noise(rng, size, octaves):
    """Several octaves of value noise, (cells, weight) each, summed around mid-grey."""
    out = Image.new("L", size, 128)
    total = 0.0
    for cells, weight in octaves:
        layer = smooth_noise(rng, size, cells, size[0] / cells / 3)
        total += weight
        out = Image.blend(out, layer, weight / total)
    return out


def stretch(image, low, high):
    """Maps the grey levels of `image` from their spread to low..high."""
    lo, hi = image.getextrema()
    span = max(1, hi - lo)
    return image.point(lambda v: round(low + (v - lo) * (high - low) / span))


def paper(rng, size, colours):
    """The sheet: its colour, wandering a little in brightness as handmade paper does."""
    clouds = stretch(fractal_noise(rng, size, [(6, 1.0), (18, 0.6), (60, 0.35)]),
                     -colours["mottle"], colours["mottle"])
    channels = [clouds.point(lambda v, base=base: max(0, min(255, base + v)))
                for base in colours["paper"]]
    return Image.merge("RGB", channels)


def fibres(rng, size, colours, scale):
    """Kozo fibres: long, thin, wandering strands, most lighter than the sheet, some darker,
    and a few flecks of bark."""
    width, height = size
    surface = cairo.ImageSurface(cairo.FORMAT_ARGB32, width, height)
    cr = cairo.Context(surface)
    cr.set_line_cap(cairo.LINE_CAP_ROUND)
    count = int(7000 * width * height / (3840 * 2160))
    for i in range(count):
        dark = rng.random() < 0.3
        colour = colours["fibre_dark" if dark else "fibre_light"]
        alpha = rng.uniform(0.08, 0.24) if dark else rng.uniform(0.2, 0.55)
        alpha *= colours["fibre_strength"]
        length = min(900, rng.lognormvariate(math.log(110), 0.6)) * scale
        angle = rng.uniform(0, math.pi)
        x, y = rng.uniform(-50, width + 50), rng.uniform(-50, height + 50)
        dx, dy = math.cos(angle) * length, math.sin(angle) * length
        bend = length * 0.35
        c1 = (x + dx / 3 + rng.uniform(-bend, bend), y + dy / 3 + rng.uniform(-bend, bend))
        c2 = (x + 2 * dx / 3 + rng.uniform(-bend, bend), y + 2 * dy / 3 + rng.uniform(-bend, bend))
        cr.move_to(x, y)
        cr.curve_to(*c1, *c2, x + dx, y + dy)
        cr.set_line_width(rng.uniform(0.5, 1.7) * scale)
        cr.set_source_rgba(*(c / 255 for c in colour), alpha)
        cr.stroke()
    for i in range(int(count / 25)):
        x, y = rng.uniform(0, width), rng.uniform(0, height)
        cr.save()
        cr.translate(x, y)
        cr.rotate(rng.uniform(0, math.pi))
        cr.scale(rng.uniform(1.0, 4.5) * scale, rng.uniform(0.5, 1.3) * scale)
        cr.arc(0, 0, 1, 0, 2 * math.pi)
        cr.restore()
        cr.set_source_rgba(*(c / 255 for c in colours["fleck"]), rng.uniform(0.15, 0.45))
        cr.fill()
    return to_pil(surface)


def print_path(cr):
    """One paw print, beans and pad, as the current path in logo units."""
    for (cx, cy), (rx, ry), rotation in BEANS:
        cr.save()
        cr.translate(cx, cy)
        cr.rotate(math.radians(rotation))
        cr.scale(rx * BEAN_SCALE, ry * BEAN_SCALE)
        cr.new_sub_path()
        cr.arc(0, 0, 1, 0, 2 * math.pi)
        cr.restore()
    for command, *points in PAD:
        if command == "M":
            cr.move_to(*points)
        else:
            cr.curve_to(*points)
    cr.close_path()


def bezier(points, t):
    """A point on the cubic Bézier through `points` at t."""
    (x0, y0), (x1, y1), (x2, y2), (x3, y3) = points
    u = 1 - t
    return (u ** 3 * x0 + 3 * u * u * t * x1 + 3 * u * t * t * x2 + t ** 3 * x3,
            u ** 3 * y0 + 3 * u * u * t * y1 + 3 * u * t * t * y2 + t ** 3 * y3)


def walk(rng, size, scale, steps_wanted):
    """Where a cat's paws land along its walk: a list of (x, y, heading, side, step)."""
    width, height = size
    # Up from the bottom-left corner and over in an arc, off the right edge.
    curve = [(0.06 * width, 1.08 * height), (0.16 * width, 0.40 * height),
             (0.62 * width, 0.14 * height), (1.06 * width, 0.40 * height)]
    samples = [bezier(curve, i / 2000) for i in range(2001)]
    lengths = [0.0]
    for (ax, ay), (bx, by) in zip(samples, samples[1:]):
        lengths.append(lengths[-1] + math.hypot(bx - ax, by - ay))
    stride = lengths[-1] / steps_wanted
    prints, index = [], 0
    for step in range(steps_wanted):
        target = (step + 0.5) * stride
        while index < len(lengths) - 2 and lengths[index + 1] < target:
            index += 1
        (ax, ay), (bx, by) = samples[index], samples[index + 1]
        heading = math.atan2(by - ay, bx - ax)
        side = 1 if step % 2 else -1
        offset = 34 * scale * side
        x = ax - math.sin(heading) * offset + rng.uniform(-6, 6) * scale
        y = ay + math.cos(heading) * offset + rng.uniform(-6, 6) * scale
        prints.append((x, y, heading + rng.uniform(-0.12, 0.12) + side * 0.08, side, step))
    return prints


def stamp(rng, mask, scale, roughness=1.0, heaviest=175):
    """Ink pressed through paper: rough edges where the paper's tooth caught it, specks where
    it did not take and patches where it took heavier. `mask` is the shape at full strength."""
    size = mask.size
    soft = mask.filter(ImageFilter.GaussianBlur(2.2 * scale))
    tooth = Image.effect_noise(size, 70).filter(ImageFilter.GaussianBlur(0.9 * scale))
    edged = ImageChops.add(soft, tooth, scale=1.0, offset=-128)
    low = 104 - 10 * roughness
    edged = edged.point(lambda v: 0 if v < low else 255 if v > low + 40
                        else round((v - low) * 255 / 40))
    specks = stretch(Image.effect_noise(size, 60).filter(ImageFilter.GaussianBlur(1.6 * scale)),
                     0, 255)
    specks = specks.point(lambda v: 255 if v < 172 else max(0, 255 - (v - 172) * 12))
    patches = stretch(smooth_noise(rng, size, 70, 4 * scale), heaviest, 255)
    return ImageChops.multiply(ImageChops.multiply(edged, specks), patches)


def paw_prints(rng, size, colours, scale):
    """The cat's walk in caramel, the ink running out as it goes."""
    width, height = size
    steps = 17
    prints = walk(rng, size, scale, steps)
    print_height = 168 * scale
    unit = print_height / 160  # the print spans about 50..210 in logo units
    shape = cairo.ImageSurface(cairo.FORMAT_ARGB32, width, height)
    ink = cairo.ImageSurface(cairo.FORMAT_ARGB32, width, height)
    for surface, solid in ((shape, True), (ink, False)):
        cr = cairo.Context(surface)
        for x, y, heading, side, step in prints:
            cr.save()
            cr.translate(x, y)
            cr.rotate(heading + math.pi / 2)
            cr.scale(unit * (1 if side > 0 else -1), unit)
            cr.translate(-PRINT_CENTRE[0], -PRINT_CENTRE[1])
            print_path(cr)
            if solid:
                cr.set_source_rgba(1, 1, 1, 1)
                cr.fill()
            else:
                strength = max(0.32, 0.97 - 0.6 * step / (steps - 1)) * rng.uniform(0.9, 1.0)
                top, bottom = colours["ink"]
                gradient = cairo.LinearGradient(0, 60, 0, 210)
                gradient.add_color_stop_rgba(0, *(c / 255 for c in top), strength)
                gradient.add_color_stop_rgba(1, *(c / 255 for c in bottom), strength)
                cr.set_source(gradient)
                cr.fill_preserve()
                cr.set_line_width(10)  # past the shape, so stamping alone draws the edge
                cr.stroke()
            cr.restore()
    mask = to_pil(shape).getchannel("A")
    coloured = to_pil(ink)
    alpha = ImageChops.multiply(coloured.getchannel("A"), stamp(rng, mask, scale))
    coloured.putalpha(alpha)
    return coloured, prints


def seal(rng, size, colours, scale, font_path, position):
    """A hanko in vermilion with 肉球 cut out of it (白文), pressed a little askew."""
    width, height = size
    seal_w, seal_h = round(118 * scale), round(212 * scale)
    pad = round(40 * scale)
    block = Image.new("L", (seal_w + 2 * pad, seal_h + 2 * pad), 0)
    draw = ImageDraw.Draw(block)
    draw.rounded_rectangle((pad, pad, pad + seal_w, pad + seal_h), radius=round(10 * scale),
                           fill=255)
    font = None
    if font_path:
        try:
            font = ImageFont.truetype(font_path, round(92 * scale))
        except OSError:
            font = None
    if font and font.getmask("肉球").getbbox():
        for i, character in enumerate("肉球"):
            centre = (pad + seal_w / 2, pad + seal_h * (0.29 + 0.44 * i))
            draw.text(centre, character, font=font, fill=0, anchor="mm")
    else:
        surface = cairo.ImageSurface(cairo.FORMAT_ARGB32, block.width, block.height)
        cr = cairo.Context(surface)
        unit = seal_w * 0.75 / 151
        cr.translate(block.width / 2, block.height / 2)
        cr.scale(unit, unit)
        cr.translate(-PRINT_CENTRE[0], -PRINT_CENTRE[1])
        print_path(cr)
        cr.set_source_rgba(1, 1, 1, 1)
        cr.fill()
        block = ImageChops.subtract(block, to_pil(surface).getchannel("A"))
    block = block.rotate(rng.uniform(-5, -2), resample=Image.BICUBIC, expand=True)
    mask = Image.new("L", size, 0)
    mask.paste(block, (round(position[0] - block.width / 2), round(position[1] - block.height / 2)))
    layer = Image.new("RGBA", size, colours["seal"] + (0,))
    layer.putalpha(ImageChops.multiply(stamp(rng, mask, scale, roughness=1.6),
                                       mask.point(lambda v: 232 if v else 0)))
    return layer


def logo(rng, side, colours):
    """A square seal with the paw cut out of it, filling a `side` by `side` picture on nothing:
    the mark for fastfetch and the like."""
    scale = side / 512
    margin = round(28 * scale)
    block = Image.new("L", (side, side), 0)
    ImageDraw.Draw(block).rounded_rectangle((margin, margin, side - margin, side - margin),
                                            radius=round(46 * scale), fill=255)
    surface = cairo.ImageSurface(cairo.FORMAT_ARGB32, side, side)
    cr = cairo.Context(surface)
    unit = (side - 2 * margin) * 0.66 / 160
    cr.translate(side / 2, side / 2 + 6 * scale)
    cr.scale(unit, unit)
    cr.translate(-PRINT_CENTRE[0], -PRINT_CENTRE[1])
    print_path(cr)
    cr.set_source_rgba(1, 1, 1, 1)
    cr.fill()
    mask = ImageChops.subtract(block, to_pil(surface).getchannel("A"))
    layer = Image.new("RGBA", (side, side), colours["seal"] + (0,))
    # Pressed harder than the wallpaper's seal: a mark small on screen must hold its colour.
    layer.putalpha(ImageChops.multiply(stamp(rng, mask, scale * 2, roughness=1.4, heaviest=225),
                                       mask.point(lambda v: 252 if v else 0)))
    return layer


def lighting(size, colours):
    """A vignette, and for sumi a lamp's warmth from the top-left corner."""
    width, height = size
    surface = cairo.ImageSurface(cairo.FORMAT_ARGB32, width, height)
    cr = cairo.Context(surface)
    reach = math.hypot(width, height) / 2
    vignette = cairo.RadialGradient(width / 2, height / 2, reach * 0.45,
                                    width / 2, height / 2, reach * 1.05)
    vignette.add_color_stop_rgba(0, 0, 0, 0, 0)
    vignette.add_color_stop_rgba(1, 0, 0, 0, colours["vignette"])
    cr.set_source(vignette)
    cr.paint()
    if colours["glow"]:
        glow = cairo.RadialGradient(width * 0.12, -height * 0.1, 0,
                                    width * 0.12, -height * 0.1, reach * 1.3)
        r, g, b = (c / 255 for c in colours["glow"])
        glow.add_color_stop_rgba(0, r, g, b, 0.16)
        glow.add_color_stop_rgba(0.5, r, g, b, 0.05)
        glow.add_color_stop_rgba(1, r, g, b, 0)
        cr.set_source(glow)
        cr.paint()
    return to_pil(surface)


def grain(image, amount):
    """Fine grain over everything, as a print has."""
    noise = Image.effect_noise(image.size, amount).convert("RGB")
    return ImageChops.soft_light(image, noise)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("variant", choices=sorted(VARIANTS) + ["logo"],
                        help="logo: the seal with the paw cut out, square, on nothing")
    parser.add_argument("output")
    parser.add_argument("--size", default="3840x2160")
    parser.add_argument("--seed", type=int, default=2)
    parser.add_argument("--seal-font", help="a font with 肉 and 球, for the seal")
    args = parser.parse_args()
    size = tuple(int(n) for n in args.size.lower().split("x"))
    scale = size[1] / 2160
    if args.variant == "logo":
        logo(random.Random(args.seed), min(size), VARIANTS["washi"]).save(args.output,
                                                                          optimize=True)
        return
    colours = VARIANTS[args.variant]
    # One generator per layer, so changing one layer's draws leaves the others as they were.
    seeds = random.Random(args.seed)
    rng = {name: random.Random(seeds.getrandbits(64))
           for name in ("paper", "fibres", "prints", "seal")}

    image = paper(rng["paper"], size, colours).convert("RGBA")
    image.alpha_composite(fibres(rng["fibres"], size, colours, scale))
    prints, steps = paw_prints(rng["prints"], size, colours, scale)
    image.alpha_composite(prints)
    image.alpha_composite(seal(rng["seal"], size, colours, scale, args.seal_font,
                               (size[0] * 0.905, size[1] * 0.74)))
    image.alpha_composite(lighting(size, colours))
    out = grain(image.convert("RGB"), colours["grain"])
    if args.output.lower().endswith((".jpg", ".jpeg")):
        out.save(args.output, quality=93, subsampling=0, optimize=True)
    else:
        out.save(args.output, optimize=True)


if __name__ == "__main__":
    main()
