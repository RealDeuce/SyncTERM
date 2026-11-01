#!/usr/bin/env python3
"""Independent geometry and byte-format expectations for the emoji generator."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import importlib.util
from pathlib import Path
import random
import sys
import unittest

try:
    import PIL
    import fontTools
except ImportError:
    print("Emoji raster generator tests require Pillow and fontTools")
    sys.exit(77)

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location("emoji_raster", Path(__file__).parents[1] / "update_emoji.py")
raster = importlib.util.module_from_spec(spec)
spec.loader.exec_module(raster)


def decode(data, width, height):
    offset = int.from_bytes(data[:2], "little")
    output = bytearray(width * height * 4)
    for start, end, first_x, span in ((2, offset, 0, width // 2),
                                     (offset, len(data), width // 2, width - width // 2)):
        pixel = 0
        while start < end:
            alpha = data[start]
            if alpha == 0:
                pixel += data[start + 1] + 1
                start += 2
            else:
                x, y = first_x + pixel % span, pixel // span
                target = (y * width + x) * 4
                output[target:target + 4] = data[start + 1:start + 4] + bytes([alpha])
                start += 4
                pixel += 1
            assert pixel <= span * height
        assert start == end and pixel == span * height
    return bytes(output)


class EmojiRasterTests(unittest.TestCase):
    def test_aspect_fit(self):
        for height, square, tall in ((8, (8, 8), (10, 8)),
                                     (14, (12, 12), (14, 12)),
                                     (16, (14, 14), (14, 12))):
            self.assertEqual(raster.raster_size(100, 100, 16, height), square)
            self.assertEqual(raster.raster_size(100, 100, 16, height, True), tall)
        self.assertEqual(raster.raster_size(200, 100, 16, 16, True), (14, 6))
        self.assertEqual(raster.raster_size(100, 200, 16, 16, True), (8, 14))

    def test_empty_and_boundary(self):
        self.assertEqual(raster.encode_color_rll(bytes(16 * 16 * 4), 16, 16),
                         bytes([4, 0, 0, 127, 0, 127]))
        self.assertEqual(raster.encode_color_rll(bytes(16 * 33 * 4), 16, 33),
                         bytes([6, 0, 0, 255, 0, 7, 0, 255, 0, 7]))

    def test_single_skip_and_partial_alpha(self):
        rgba = bytearray(16 * 16 * 4)
        rgba[4:8] = bytes([10, 20, 30, 128])
        encoded = raster.encode_color_rll(rgba, 16, 16)
        self.assertEqual(encoded, bytes([10, 0, 0, 0, 128, 10, 20, 30, 0, 125, 0, 127]))
        self.assertEqual(decode(encoded, 16, 16), rgba)

    def test_round_trip(self):
        rng = random.Random(1701)
        for width in (1, 2, 7, 8, 16):
            for height in (1, 8, 14, 16, 33):
                rgba = bytearray()
                for _ in range(width * height):
                    rgba.extend([rng.randrange(256) for _ in range(3)])
                    rgba.append(rng.choice((0, 0, 1, 128, 255)))
                expected = bytearray(rgba)
                for i in range(0, len(rgba), 4):
                    if rgba[i + 3] == 0:
                        expected[i:i + 4] = bytes(4)
                self.assertEqual(decode(raster.encode_color_rll(rgba, width, height), width, height), expected)

    def test_offset_overflow(self):
        with self.assertRaises(ValueError):
            raster.encode_color_rll(bytes([1, 2, 3, 255]) * 16 * 4096, 16, 4096)


if __name__ == "__main__":
    unittest.main()
