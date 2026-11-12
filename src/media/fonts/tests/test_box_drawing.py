#!/usr/bin/env python3
"""Independent expected rasters for the CP437-compatible Unicode boxes."""

# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "src/media/fonts"))
from box_drawing import match_cp437


# Recorded from the pinned UNSCII hex sources; retain their dash cadence.
UPSTREAM_DASHES = {
    8: {
        0x2504: '000000dddd000000', 0x2505: '0000dddddddd0000',
        0x2506: '1818001818180018', 0x2507: '3c3c003c3c3c003c',
        0x2508: '000000aaaa000000', 0x2509: '000000aaaa000000',
        0x250a: '1800180018001800', 0x250b: '3c003c003c003c00',
        0x254c: '0000009999000000', 0x254d: '0000999999990000',
        0x254e: '1800001818000018', 0x254f: '3c00003c3c00003c',
    },
    16: {
        0x2504: '00000000000000dddd00000000000000',
        0x2505: '000000000000dddddddd000000000000',
        0x2506: '18180018181800181818001818180018',
        0x2507: '3c3c003c3c3c003c3c3c003c3c3c003c',
        0x2508: '00000000000000aaaa00000000000000',
        0x2509: '000000000000aaaaaaaa000000000000',
        0x250a: '18000018180000181800001818000018',
        0x250b: '3c00003c3c00003c3c00003c3c00003c',
        0x254c: '00000000000000999900000000000000',
        0x254d: '00000000000099999999000000000000',
        0x254e: '18180000000018181818000000001818',
        0x254f: '3c3c000000003c3c3c3c000000003c3c',
    },
}


class BoxDrawingTest(unittest.TestCase):
    def font(self, height):
        # Distinct sentinels prove that the override leaves unrelated glyphs
        # and the three original diagonal glyphs untouched.
        glyphs = {code: bytes([code & 255]) * height
                  for code in range(0x24ff, 0x2581)}
        glyphs.update({code: bytes.fromhex(bitmap)
                       for code, bitmap in UPSTREAM_DASHES[height].items()})
        match_cp437(glyphs, ROOT, height)
        return glyphs

    def test_light_and_heavy(self):
        for height, centre in ((8, 4), (16, 7)):
            with self.subTest(height=height):
                font = self.font(height)
                self.assertEqual(font[0x2500], bytes(centre) + b'\xff' + bytes(height - centre - 1))
                self.assertEqual(font[0x2502], b'\x18' * height)
                self.assertEqual(font[0x2501], bytes(centre - 1) + b'\xff' * 3 + bytes(height - centre - 2))
                self.assertEqual(font[0x2503], b'\x3c' * height)

    def test_junctions(self):
        font = self.font(16)
        expected = {
            0x250d: '0000000000001f1f1f18181818181818',  # down light, right heavy
            0x250e: '000000000000003f3c3c3c3c3c3c3c3c',  # down heavy, right light
            0x253f: '181818181818ffffff18181818181818',  # vertical light, horizontal heavy
            0x2542: '3c3c3c3c3c3c3cff3c3c3c3c3c3c3c3c',  # vertical heavy, horizontal light
            0x254b: '3c3c3c3c3c3cffffff3c3c3c3c3c3c3c',  # heavy cross
            0x257c: '0000000000000fff0f00000000000000',  # left light, right heavy
            0x257d: '181818181818183c3c3c3c3c3c3c3c3c',  # up light, down heavy
        }
        for code, bitmap in expected.items():
            with self.subTest(code=hex(code)):
                self.assertEqual(font[code], bytes.fromhex(bitmap))

    def test_double_and_rounded(self):
        for height, centre in ((8, 4), (16, 7)):
            with self.subTest(height=height):
                font = self.font(height)
                self.assertEqual(font[0x2550], bytes(centre - 2) + b'\xff\0\xff' + bytes(height - centre - 1))
                self.assertEqual(font[0x2551], b'\x36' * height)
                self.assertEqual(font[0x256d], bytes(centre) + b'\x07\x0c' + b'\x18' * (height - centre - 2))
                self.assertEqual(font[0x256e], bytes(centre) + b'\xe0\x30' + b'\x18' * (height - centre - 2))
                self.assertEqual(font[0x256f], b'\x18' * (centre - 1) + b'\x30\xe0' + bytes(height - centre - 1))
                self.assertEqual(font[0x2570], b'\x18' * (centre - 1) + b'\x0c\x07' + bytes(height - centre - 1))

    def test_dashes_and_half_strokes(self):
        font = self.font(16)
        self.assertEqual(font[0x2504], bytes(7) + b'\xdd' + bytes(8))
        self.assertEqual(font[0x2506], bytes.fromhex('18180018181800181818001818180018'))
        self.assertEqual(font[0x2574], bytes(7) + b'\xf0' + bytes(8))
        self.assertEqual(font[0x2576], bytes(7) + b'\x0f' + bytes(8))
        self.assertEqual(font[0x2575], b'\x18' * 7 + bytes(9))
        self.assertEqual(font[0x2577], bytes(7) + b'\x18' * 9)

    def test_all_dash_patterns(self):
        # Check every dash weight/count and both directions at both sizes.
        # Preserving phase keeps joins between repeated cells unchanged.
        for height in (8, 16):
            font = self.font(height)
            centre = 4 if height == 8 else 7
            for code, original in UPSTREAM_DASHES[height].items():
                source = bytes.fromhex(original)
                with self.subTest(height=height, code=hex(code)):
                    if code in (0x2504, 0x2505, 0x2508, 0x2509, 0x254c, 0x254d):
                        # Each of three repeated cells must retain precisely
                        # the original on/off columns, including the join.
                        self.assertEqual(font[code][centre:centre + 1] * 3,
                                         bytes([max(source)]) * 3)
                        occupied = [y for y, row in enumerate(font[code]) if row]
                        expected = ([centre] if code in (0x2504, 0x2508, 0x254c)
                                    else [centre - 1, centre, centre + 1])
                        self.assertEqual(occupied, expected)
                    else:
                        self.assertEqual([bool(row) for row in font[code] * 3],
                                         [bool(row) for row in source * 3])
                        expected = 0x18 if code in (0x2506, 0x250a, 0x254e) else 0x3c
                        self.assertEqual({row for row in font[code] if row}, {expected})

    def test_scope_and_dimensions(self):
        for height in (8, 16):
            font = self.font(height)
            self.assertEqual(len(font), 130)
            for code, bitmap in font.items():
                self.assertEqual(len(bitmap), height)
                if code in (0x24ff, 0x2571, 0x2572, 0x2573, 0x2580):
                    self.assertEqual(bitmap, bytes([code & 255]) * height)
                else:
                    self.assertTrue(any(bitmap), hex(code))
        with self.assertRaises(ValueError):
            match_cp437({}, ROOT, 14)


if __name__ == '__main__':
    unittest.main()
