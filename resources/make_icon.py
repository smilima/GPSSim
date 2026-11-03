#!/usr/bin/env python3
"""Generates GPSSim.ico. Requires Pillow.

    python resources/make_icon.py

The icon is a GPS satellite broadcasting its signal, the half of the link that
GPSSim makes. It is a sibling of SoftGpsReceiver's sky plot icon: the same
tile, the same sky-plot blue for the waves and the same colours, on an indigo
tile so the two can be told apart on the taskbar. The artwork is redrawn for
each size class rather than scaled: at 16 px the solar panel cells and the
third wave turn to mush, so they go and what is left gets bolder.
"""
import math
import struct
from io import BytesIO
from pathlib import Path

from PIL import Image, ImageDraw

SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)
UNITS = 256  # all artwork coordinates are in a 256 x 256 design space
CENTER = UNITS / 2

TILE_TOP = (96, 68, 204)
TILE_BOTTOM = (30, 18, 92)
GRID = (150, 224, 255)
WHITE = (255, 255, 255)
GOLD = (255, 190, 60)
GOLD_DARK = (196, 128, 24)
PANEL = (46, 120, 236)

DOWN = math.radians(45)  # the signal heads down and to the right, towards the receiver
TOWARDS = (math.cos(DOWN), math.sin(DOWN))
ACROSS = (math.sin(DOWN), -math.cos(DOWN))  # the solar panels' axis

# Size classes, largest first: (minimum size, artwork)
STYLES = (
    (64, dict(corner=50, rim=2.5, sat=(94, 94), body=21, band=5, gap=9, boom=5, panel=(29, 18), cells=3, cell=2.5,
              outline=4, dish=8, waves=((56, 11, 1.0), (88, 11, 0.7), (120, 11, 0.42)), span=34)),
    (32, dict(corner=46, rim=0, sat=(92, 92), body=25, band=0, gap=8, boom=8, panel=(30, 22), cells=1, cell=4,
              outline=0, dish=0, waves=((66, 18, 1.0), (108, 18, 0.65)), span=34)),
    (0, dict(corner=44, rim=0, sat=(90, 90), body=29, band=0, gap=6, boom=12, panel=(32, 27), cells=0, cell=0,
             outline=0, dish=0, waves=((72, 24, 1.0), (122, 24, 0.7)), span=33)),
)


def along(point, axis, distance):
    return point[0] + axis[0] * distance, point[1] + axis[1] * distance


class Canvas:
    """Supersampled canvas; every shape is a mask painted through a fill."""

    def __init__(self, size):
        self.size = size
        self.px = size * (8 if size <= 64 else 4)
        self.scale = self.px / UNITS
        self.image = Image.new("RGBA", (self.px, self.px), (0, 0, 0, 0))

    def paint(self, fill, shape, alpha=1.0):
        mask = Image.new("L", (self.px, self.px), 0)
        shape(ImageDraw.Draw(mask))
        if alpha < 1.0:
            mask = mask.point(lambda v: round(v * alpha))
        layer = fill.copy() if isinstance(fill, Image.Image) else Image.new("RGBA", mask.size, fill)
        layer.putalpha(mask)
        self.image.alpha_composite(layer)

    def box(self, cx, cy, r):
        s = self.scale
        return [(cx - r) * s, (cy - r) * s, (cx + r) * s, (cy + r) * s]

    def disc(self, cx, cy, r):
        return lambda d: d.ellipse(self.box(cx, cy, r), fill=255)

    def rect(self, centre, length, width):
        """Rectangle turned to lie along ACROSS: half-length along it, half-width along TOWARDS."""
        s = self.scale
        corners = [along(along(centre, ACROSS, a * length), TOWARDS, b * width)
                   for a, b in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
        return lambda d: d.polygon([(x * s, y * s) for x, y in corners], fill=255)

    def wave(self, cx, cy, r, width, span):
        """Arc of radius r facing TOWARDS, with round ends."""
        s = self.scale
        mid = math.degrees(DOWN)
        ends = [(cx + r * math.cos(math.radians(a)), cy + r * math.sin(math.radians(a)))
                for a in (mid - span, mid + span)]

        def shape(d):
            d.arc(self.box(cx, cy, r + width / 2), mid - span, mid + span, fill=255, width=round(width * s))
            for x, y in ends:
                d.ellipse(self.box(x, y, width / 2), fill=255)
        return shape

    def tile(self, inset, corner, width=0):
        s = self.scale
        box = [inset * s, inset * s, (UNITS - inset) * s, (UNITS - inset) * s]
        if width:
            return lambda d: d.rounded_rectangle(box, radius=corner * s, outline=255, width=round(width * s))
        return lambda d: d.rounded_rectangle(box, radius=corner * s, fill=255)

    def tile_fill(self):
        """Vertical gradient with a soft glow in the middle, as SoftGpsReceiver's tile has."""
        t = Image.linear_gradient("L").resize((self.px, self.px), Image.BILINEAR)
        rgb = [t.point(lambda v, a=a, b=b: round(a + (b - a) * v / 255)) for a, b in zip(TILE_TOP, TILE_BOTTOM)]
        r = Image.radial_gradient("L").resize((self.px, self.px), Image.BILINEAR)  # distance from the middle
        glow = r.point(lambda v: round(255 * max(0.0, 1 - v / 256 / 0.7) ** 2 * 0.16))
        white = Image.new("L", (self.px, self.px), 255)
        rgb = [Image.composite(white, channel, glow.point(lambda v, k=k: round(v * k)))
               for channel, k in zip(rgb, (0.45, 0.7, 1.0))]
        return Image.merge("RGBA", (*rgb, white))


def render(size):
    style = next(st for minimum, st in STYLES if size >= minimum)
    c = Canvas(size)
    inset = (size // 32) * UNITS / size  # a whole number of pixels, so the tile edge stays sharp

    c.paint(c.tile_fill(), c.tile(inset, style["corner"]))
    if style["rim"]:  # keeps the edge of the tile visible on a dark taskbar
        c.paint(WHITE + (255,), c.tile(inset, style["corner"], style["rim"]), 0.18)

    sat, body = style["sat"], style["body"]
    face = along(sat, TOWARDS, body)  # the side that looks at the Earth, where the signal leaves
    for radius, width, alpha in style["waves"]:
        c.paint(GRID + (255,), c.wave(*face, radius, width, style["span"]), alpha)

    length, width = style["panel"]
    reach = body + style["gap"] + length  # from the middle of the body to the middle of a panel
    for side in (-1, 1):
        centre = along(sat, ACROSS, side * reach)
        c.paint(WHITE + (255,), c.rect(along(sat, ACROSS, side * (body + style["gap"] / 2)),
                                       style["gap"] / 2 + 1, style["boom"] / 2))
        if style["outline"]:
            c.paint(TILE_BOTTOM + (255,), c.rect(centre, length + style["outline"], width + style["outline"]))
        c.paint(PANEL + (255,), c.rect(centre, length, width))
        for k in range(1, style["cells"] + 1):  # the gaps between the rows of cells
            row = along(centre, ACROSS, length * (2 * k / (style["cells"] + 1) - 1))
            c.paint(GRID + (255,), c.rect(row, style["cell"] / 2, width), 0.55)

    if style["outline"]:
        c.paint(TILE_BOTTOM + (255,), c.rect(sat, body + style["outline"], body + style["outline"]))
    c.paint(GOLD + (255,), c.rect(sat, body, body))
    if style["band"]:  # the shadowed side of the body's gold foil
        c.paint(GOLD_DARK + (255,), c.rect(along(sat, TOWARDS, -body + style["band"]), body, style["band"]))
    if style["dish"]:
        c.paint(WHITE + (255,), c.disc(*along(face, TOWARDS, -style["dish"] - 3), style["dish"]))

    return c.image.resize((size, size), Image.BOX)


def dib(image):
    """Uncompressed 32-bit icon frame: BGRA pixels, bottom-up, then the 1-bit AND mask."""
    w, h = image.size
    flipped = image.transpose(Image.FLIP_TOP_BOTTOM)
    header = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    stride = (w + 31) // 32 * 4
    mask = flipped.getchannel("A").point(lambda v: 255 if v < 128 else 0).convert("1", dither=Image.NONE).tobytes()
    row = (w + 7) // 8
    mask = b"".join(mask[y * row:(y + 1) * row].ljust(stride, b"\0") for y in range(h))
    return header + flipped.tobytes("raw", "BGRA") + mask


def png(image):
    out = BytesIO()
    image.save(out, "PNG", optimize=True)
    return out.getvalue()


def write_ico(path, images):
    """Small frames stay uncompressed for compatibility; only 256 px is PNG."""
    frames = [png(im) if im.width >= 256 else dib(im) for im in images]
    offset = 6 + 16 * len(frames)
    out = struct.pack("<HHH", 0, 1, len(frames))
    for im, frame in zip(images, frames):
        side = im.width % 256  # 0 means 256
        out += struct.pack("<BBBBHHII", side, side, 0, 0, 1, 32, len(frame), offset)
        offset += len(frame)
    path.write_bytes(out + b"".join(frames))


if __name__ == "__main__":
    target = Path(__file__).with_name("GPSSim.ico")
    write_ico(target, [render(size) for size in SIZES])
    print(f"wrote {target} ({target.stat().st_size} bytes)")
