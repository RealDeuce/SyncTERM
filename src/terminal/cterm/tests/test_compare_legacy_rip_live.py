#!/usr/bin/env python3
"""Focused tests for live RIP framebuffer settling."""

# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

import unittest

from compare_legacy_rip_live import HEIGHT, PIXELS, WIDTH, compare, settled_frames


def frame(value: int) -> tuple[bytes, bytes, int, bool]:
    return (bytes([value]), b"palette", 0, False)


class SettledFramesTest(unittest.TestCase):
    def test_static_frame(self) -> None:
        a = frame(1)
        self.assertEqual(settled_frames([a]), None)
        self.assertEqual(settled_frames([a, a]), [a])

    def test_blinking_cursor_keeps_both_full_frames(self) -> None:
        a = frame(1)
        b = frame(2)
        self.assertEqual(settled_frames([a, b, a]), None)
        self.assertEqual(settled_frames([a, b, a, b]), [b, a])

    def test_changing_scene_does_not_settle(self) -> None:
        a = frame(1)
        b = frame(2)
        c = frame(3)
        self.assertEqual(settled_frames([a, b, c, a]), None)

    def test_changing_status_row_does_not_hide_pixels(self) -> None:
        pixels = bytearray(PIXELS * 4)
        palette = bytearray(65536 * 4)
        palette[4:8] = (0xFF0000).to_bytes(4, "little")
        frames = []
        for x in (4, 5, 6):
            changed = pixels.copy()
            offset = ((HEIGHT - 1) * WIDTH + x) * 4
            changed[offset] = 1
            frames.append((bytes(changed), bytes(palette), 0, False))
        self.assertEqual(settled_frames(frames), frames)
        modern = bytes(PIXELS * 3)
        self.assertEqual(compare(frames[-1], modern)[0], 1)

    def test_changing_rip_area_does_not_settle(self) -> None:
        pixels = bytearray(PIXELS * 4)
        palette = bytes(65536 * 4)
        frames = []
        for x in (4, 5, 6):
            changed = pixels.copy()
            offset = ((HEIGHT - 15) * WIDTH + x) * 4
            changed[offset] = 1
            frames.append((bytes(changed), palette, 0, False))
        self.assertIsNone(settled_frames(frames))


if __name__ == "__main__":
    unittest.main()
