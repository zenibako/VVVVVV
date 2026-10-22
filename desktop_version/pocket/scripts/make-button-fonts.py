#!/usr/bin/env python3
"""Make the Pocket's button fonts: the game's buttons_*.png fonts plus
Analogue Pocket button glyphs at U+EB37-U+EB3E.

The glyphs follow Butterscotch Pocket's positional prompts
(src/openfpga/platform/of_buttons.c in zenibako/Butterscotch): four circles
in a diamond with the pressed face button filled, and the shoulder pair.
In the 8 and 10 pixel fonts the face buttons are too small for outlines,
so the pressed one is bright and the others dim. Start and Select are the
row of three small buttons below the screen
(Select, the Analogue button, Start) with the pressed one bright and the
others dim. Butterscotch draws its icons at 13 pixels; here each is redrawn
for the game's 8, 10 and 12 pixel fonts. They are white and take the text
colour, as Butterscotch's do.

Usage: make-button-fonts.py <game fonts dir> <output dir>
Requires Pillow.
"""

import pathlib
import re
import sys

from PIL import Image

FIRST = 0xEB37
# Order matches ButtonGlyphs.cpp: A (right), B (bottom), X (top), Y (left),
# Start, Select, L, R.
NAMES = ["A", "B", "X", "Y", "START", "SELECT", "L", "R"]

# Per cell size: the face-button circle size and the top-left corner of the
# right, bottom, top and left circles; the three small buttons as
# (width, height, step, y); the shoulder rectangles as (width, height, y)
# with the second one right-aligned.
LAYOUTS = {
    8: {"circle": 3, "face": [(4, 2), (2, 4), (2, 0), (0, 2)],
        "small": (2, 2, 3, 3), "shoulder": (3, 3, 1)},
    10: {"circle": 3, "face": [(6, 3), (3, 6), (3, 0), (0, 3)],
         "small": (2, 2, 4, 4), "shoulder": (4, 3, 2)},
    12: {"circle": 4, "face": [(8, 4), (4, 8), (4, 0), (0, 4)],
         "small": (3, 2, 4, 5), "shoulder": (5, 4, 2)},
}

BRIGHT = 255
DIM = 96


def circle(size, filled):
    """Pixels of a circle of the given size, corners cut, as a set of (x, y).
    Outline only unless filled. A 3-pixel circle is a plus."""
    pixels = set()
    last = size - 1
    for y in range(size):
        for x in range(size):
            corner = (x in (0, last)) and (y in (0, last))
            if corner:
                continue
            edge = x in (0, last) or y in (0, last)
            if filled or edge:
                pixels.add((x, y))
    return pixels


def rect(w, h, filled):
    return {(x, y) for y in range(h) for x in range(w)
            if filled or x in (0, w - 1) or y in (0, h - 1)}


def glyph(cell, index):
    """The glyph's pixels as a dict of (x, y) to alpha."""
    layout = LAYOUTS[cell]
    pixels = {}
    if index < 4:
        size = layout["circle"]
        for i, (ox, oy) in enumerate(layout["face"]):
            pressed = i == index
            # A 3-pixel circle is too small to show as an outline, so the
            # small fonts mark the pressed button by brightness instead.
            if size == 3:
                shape, alpha = circle(size, True), BRIGHT if pressed else DIM
            else:
                shape, alpha = circle(size, pressed), BRIGHT
            for x, y in shape:
                pixels[(ox + x, oy + y)] = alpha
    elif index in (4, 5):
        # Select, the Analogue button, Start, left to right.
        w, h, step, top = layout["small"]
        pressed = 2 if index == 4 else 0
        for button in range(3):
            for x, y in rect(w, h, True):
                pixels[(button * step + x, top + y)] = BRIGHT if button == pressed else DIM
    else:
        w, h, top = layout["shoulder"]
        filled_right = index == 7
        for right in (False, True):
            ox = cell - w if right else 0
            for x, y in rect(w, h, right == filled_right):
                pixels[(ox + x, top + y)] = BRIGHT
    return pixels


def make(fonts_dir, out_dir, cell):
    name = "buttons_%dx%d" % (cell, cell)
    image = Image.open(fonts_dir / (name + ".png")).convert("RGBA")
    columns = image.width // cell
    rows = image.height // cell
    last = FIRST + len(NAMES) - 1 - 0xEB00
    if last >= columns * rows:
        raise SystemExit("%s.png has no room for the Pocket glyphs" % name)
    for i in range(len(NAMES)):
        cell_index = FIRST - 0xEB00 + i
        cx = (cell_index % columns) * cell
        cy = (cell_index // columns) * cell
        for y in range(cell):
            for x in range(cell):
                if image.getpixel((cx + x, cy + y))[3] != 0:
                    raise SystemExit("%s.png already has a glyph at U+%04X" % (name, 0xEB00 + cell_index))
        for (x, y), alpha in glyph(cell, i).items():
            image.putpixel((cx + x, cy + y), (255, 255, 255, alpha))
    image.save(out_dir / (name + ".png"), optimize=True)

    # Extend the character range; the special (own-colour) range is left as
    # it is, so the Pocket glyphs take the text colour.
    meta = (fonts_dir / (name + ".fontmeta")).read_text()
    chars = re.search(r"<chars>.*?</chars>", meta, re.S).group(0)
    new_chars, count = re.subn(r'end="0x[0-9A-Fa-f]+"', 'end="0x%04X"' % (FIRST + len(NAMES) - 1), chars, count=1)
    if count != 1:
        raise SystemExit("%s.fontmeta: no character range to extend" % name)
    (out_dir / (name + ".fontmeta")).write_text(meta.replace(chars, new_chars))


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    fonts_dir = pathlib.Path(sys.argv[1])
    out_dir = pathlib.Path(sys.argv[2])
    out_dir.mkdir(parents=True, exist_ok=True)
    for cell in sorted(LAYOUTS):
        make(fonts_dir, out_dir, cell)


if __name__ == "__main__":
    main()
