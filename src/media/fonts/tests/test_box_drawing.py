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


class BoxDrawingTest(unittest.TestCase):
    def font(self, height):
        # Distinct sentinels prove that the override leaves unrelated glyphs
        # and the three original diagonal glyphs untouched.
        glyphs = {code: bytes([code & 255]) * height
                  for code in range(0x24ff, 0x2581)}
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
        self.assertEqual(font[0x2504], bytes(7) + b'\xdb' + bytes(8))
        self.assertEqual(font[0x2506], b'\x18' * 5 + b'\0' + b'\x18' * 4 + b'\0' + b'\x18' * 5)
        self.assertEqual(font[0x2574], bytes(7) + b'\xf0' + bytes(8))
        self.assertEqual(font[0x2576], bytes(7) + b'\x0f' + bytes(8))
        self.assertEqual(font[0x2575], b'\x18' * 7 + bytes(9))
        self.assertEqual(font[0x2577], bytes(7) + b'\x18' * 9)

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
