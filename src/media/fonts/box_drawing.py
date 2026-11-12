"""Match Unicode box drawing to the unchanged built-in CP437 rasters."""

# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

import re
import unicodedata


def cp437_boxes(root, height):
    """Borrow the actual project raster rather than another IBM font variant."""
    source = (root / "src/media/fonts/st_builtin_font_data.c").read_text(
        encoding="ascii")
    match = re.search(
        rf"static const uint8_t font_0_8x{height}\[\] = \{{(.*?)\}};",
        source, re.DOTALL)
    if match is None:
        raise ValueError(f"missing built-in CP437 8x{height}")
    bitmap = bytes(int(value, 16) for value in re.findall(
        r"0x([0-9a-fA-F]{2})\b", match[1]))
    if len(bitmap) != 256 * height:
        raise ValueError(f"invalid built-in CP437 8x{height}")
    return {ord(bytes([index]).decode("cp437")): bitmap[index * height:(index + 1) * height]
            for index in range(256)
            if 0x2500 <= ord(bytes([index]).decode("cp437")) < 0x2580}


def box_arms(name):
    """Decode the fixed Unicode box names into left/right/up/down weights."""
    directions = {"LEFT": "L", "RIGHT": "R", "UP": "U", "DOWN": "D",
                  "HORIZONTAL": "LR", "VERTICAL": "UD"}
    weights = {"LIGHT": 1, "HEAVY": 2}
    words = name.split()
    default = weights.get(words[0])
    arms = dict.fromkeys("LRUD", 0)
    for clause in name.split(" AND "):
        tokens = clause.split()
        weight = next((weights[token] for token in tokens if token in weights), default)
        if weight is None:
            raise ValueError(f"unrecognized box drawing weight: {name}")
        for token in tokens:
            for direction in directions.get(token, ""):
                arms[direction] = weight
    return arms


def box_bitmap(name, height, source):
    # CP437's horizontal centre is row 4 at 8x8 and row 7 at 8x16.
    # Its vertical light stroke occupies columns 3 and 4. Heavy strokes
    # expand by one pixel on each side: three rows or four columns.
    centre = 4 if height == 8 else 7
    rows = bytearray(height)

    def rectangle(left, top, right, bottom):
        mask = sum(0x80 >> x for x in range(left, right))
        for y in range(top, bottom):
            rows[y] |= mask

    if " ARC " in name:
        down = "DOWN" in name
        right = "RIGHT" in name
        step = 1 if down else -1
        for distance, mask in ((0, 0x07), (1, 0x0c)):
            if not right:
                mask = int(f"{mask:08b}"[::-1], 2)
            rows[centre + step * distance] = mask
        rectangle(3, centre + 2 if down else 0,
                  5, height if down else centre - 1)
        return bytes(rows)

    arms = box_arms(name)
    horizontal = {0: (), 1: (centre,), 2: (centre - 1, centre, centre + 1)}
    vertical = {0: (), 1: (3, 4), 2: (2, 3, 4, 5)}
    cross_x = vertical[max(arms["U"], arms["D"])]
    cross_y = horizontal[max(arms["L"], arms["R"])]
    # Extend each arm through the perpendicular stroke so that mixed-weight
    # corners and tees have a solid join, including the wider heavy stem.
    for direction in "LR":
        for y in horizontal[arms[direction]]:
            left = 0 if direction == "L" else min(cross_x, default=4)
            right = max(cross_x, default=3) + 1 if direction == "L" else 8
            rectangle(left, y, right, y + 1)
    for direction in "UD":
        top = 0 if direction == "U" else min(cross_y, default=centre)
        bottom = max(cross_y, default=centre - 1) + 1 if direction == "U" else height
        for x in vertical[arms[direction]]:
            rectangle(x, top, x + 1, bottom)

    if " DASH " in name:
        # Keep UNSCII's dash lengths, gaps and phase across cell boundaries.
        # Only the stroke's perpendicular thickness and position change.
        if "HORIZONTAL" in name:
            mask = 0
            for row in source:
                mask |= row
            for y in range(height):
                rows[y] &= mask
        else:
            for y in range(height):
                if source[y] == 0:
                    rows[y] = 0
    return bytes(rows)


def match_cp437(glyphs, root, height):
    """Override only box drawing; leave diagonal artwork and other text alone."""
    if height not in (8, 16):
        raise ValueError("box drawing overrides require height 8 or 16")
    originals = cp437_boxes(root, height)
    for codepoint in range(0x2500, 0x2580):
        name = unicodedata.name(chr(codepoint)).removeprefix("BOX DRAWINGS ")
        if codepoint in originals:
            glyphs[codepoint] = originals[codepoint]
        elif "DIAGONAL" not in name:
            glyphs[codepoint] = box_bitmap(name, height, glyphs[codepoint])
