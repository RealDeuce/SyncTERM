#!/usr/bin/env python3
"""RIP_SCROLL through the real TCP parser and shared bitmap console."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from dataclasses import replace
from pathlib import Path

from st_migrated_control_test import connection
from st_rip3_button_styles_connected_test import number
from st_rip3_connected_test import pixel, query
from st_terminal_conformance_test import Terminal


COLORS = (0xaa0000, 0x00aa00, 0x0000aa, 0x00aaaa, 0xaa00aa, 0xffffff)


def coords(*values, width=2, radix=36):
    return b"".join(number(v, width, radix) for v in values)


def scroll(x0, y0, x1, y1, mode, y, *, width=2, radix=36, reserved=0):
    return (b"1G" + coords(x0, y0, x1, y1, width=width, radix=radix)
            + number(mode, 1, radix) + number(reserved, 1, radix)
            + number(y, width, radix))


def plane_pixel(frame, x, y, phase):
    m = frame.metadata
    value = getattr(frame, phase)[(y + m["pixel_top"]) * m["plane_width"] + x]
    return (value if value & 0x80000000 else frame.palette[value]) & 0xffffff


def addresses(frame, x, y, samples):
    for offset, expected in enumerate(samples):
        for phase in ("primary", "alternate"):
            row = [plane_pixel(frame, x + dx, y + offset, phase) for dx in range(3)]
            assert row == [expected]*3, (x, y+offset, phase, row, expected)


def run(args, width, height):
    with connection(args.executable, args.reports, encoding=args.encoding,
                    options="ScreenMode=Custom\nRIP=RIPv3\n",
                    settings=f"CustomColumns={width // 8}\nCustomRows={height // 8}\n"
                             "CustomFontHeight=8\nAudioModes=0\n") as (client, root, peer, send):
        t = Terminal(client, root, peer, send)

        def wire(source, tail=b""):
            return t.exchange(b"\x01|" + source + b"\r\n" + tail)

        def values(source, expect=b""):
            return t.exchange(query(source, at_cursor=True), expect)

        def paint_rows(y=0, x=3):
            marks = [b"S01" + number(ink, 2) + b"|B" + coords(x, y+i, x+2, y+i)
                     for i, ink in enumerate((4, 2, 1, 3, 5, 15))]
            return b"W0|" + b"|".join(marks)

        def fresh():
            wire(b"*", b"\x1b[?25l")
            values(f"$WORLD(CUR,{width},{height})$".encode())

        t.exchange(b"\x1b[!", b"RIPSCRIP030001")
        fresh()
        t.case(f"rip3-scroll-overlap-and-reversed-one-row-{width}x{height}")
        a, b, c, d, e, f = COLORS
        frame = wire(paint_rows()+b"|a0009|W4|" + scroll(3, 2, 5, 4, 5, 1))
        addresses(frame, 3, 0, (a, c, d, e, 0, f))
        assert pixel(frame, 2, 1) == pixel(frame, 6, 1) == 0
        frame = wire(paint_rows()+b"|W2|" + scroll(3, 4, 5, 2, 0, 3))
        addresses(frame, 3, 0, (a, b, c, c, d, e))
        frame = wire(paint_rows()+b"|" + scroll(5, 2, 3, 2, 5, 1))
        addresses(frame, 3, 0, (a, c, 0, d, e, f))
        frame = wire(paint_rows()+b"|" + scroll(3, 2, 5, 4, 6, 3))
        addresses(frame, 3, 0, (a, b, c, c, d, e))

        t.case("rip3-scroll-current-port-clipping-and-original-corner")
        for mode in (0, 5, 6):
            fresh()
            wire(paint_rows()+b"|2P1"+coords(3, 2, 6, 6)+b"0|2s100|v04030403")
            values(b"$PROT(PORT,1)$")
            frame = wire(scroll(3, 1, 5, 4, mode, 3))
            addresses(frame, 3, 0, (a, b, c if mode==0 else 0, d if mode==0 else 0, c, d))
            values(b"$CUR(PORT)$:$ISPROT(PORT,1)$", b"1:1")
        frame = wire(b"c04|" + scroll(3, 2, 5, 4, 1, 6)+b"|c0F|X0304|X0403")
        # The out-of-port destination still exposes the source, while the
        # following X can only draw within the stored one-pixel viewport.
        assert pixel(frame, 3, 4) == a and pixel(frame, 4, 3) == f
        assert pixel(frame, 3, 5) == d and pixel(frame, 2, 4) == 0
        before = frame
        frame = wire(scroll(3, 1, 5, 1, 5, 3))
        assert frame.primary == before.primary and frame.alternate == before.alternate
        values(b"$UNPROT(PORT,1)$$DVP(1)$")
        frame = wire(scroll(3, 2, 5, 4, 5, 6))
        assert frame.primary == before.primary and frame.alternate == before.alternate

        t.case("rip3-scroll-solid-and-mask-exposure")
        for mode, expected in ((1, a), (2, b), (3, c)):
            fresh()
            frame = wire(paint_rows(8, 7) + b"|c04|k02|S0001|"
                         + scroll(7, 8, 9, 8, mode, 16))
            addresses(frame, 7, 8, (expected,))
            addresses(frame, 7, 16, (a,))
        mask = number(128, 2) + b"00"*7
        frame = wire(b"k02|s" + mask + b"01|v0A0A0B0B|" + scroll(7, 8, 9, 8, 4, 17))
        for phase in ("primary", "alternate"):
            assert [plane_pixel(frame, x, 8, phase) for x in range(7, 10)] == [b, c, b]

        t.case("rip3-scroll-xy-width-radix-and-preclip-rejection")
        fresh()
        before = wire(paint_rows())
        values(f"$WORLD(CUR,{2*width},{2*height})$$BASEMATH(CUR,64)$$COORDSIZE(CUR,3)$".encode())
        frame = wire(scroll(6, 4, 10, 8, 5, 5, width=3, radix=64))
        assert frame.primary == before.primary and frame.alternate == before.alternate
        for malformed in (scroll(6, 4, 10, 8, 7, 2, width=3, radix=64),
                          scroll(6, 4, 10, 8, 5, 2, width=3, radix=64, reserved=1),
                          scroll(6, 4, 10, 8, 5, 2, width=3, radix=64)[:-1]):
            frame = wire(malformed)
            assert frame.primary == before.primary and frame.alternate == before.alternate
        frame = wire(scroll(6, 4, 10, 8, 5, 2, width=3, radix=64))
        addresses(frame, 3, 0, (a, c, d, e, 0, f))

        t.case("rip3-scroll-offscreen-until-explicit-port-copy")
        fresh()
        original = client.capture()
        wire(b"2P2"+coords(0, 0, 8, 8)+b"1|2s200|"+paint_rows())
        frame = wire(scroll(3, 2, 5, 4, 5, 1))
        assert frame.primary == original.primary and frame.alternate == original.alternate
        frame = wire(b"2s000|2C2"+coords(3, 0, 6, 6)+b"0"+coords(30, 30, 33, 36)+b"0")
        addresses(frame, 30, 30, (a, c, d, e, 0, f))

        t.case("rip3-scroll-physical-ansi-with-separate-blink-planes")
        fresh()
        glyph = b"\xdb" if args.encoding == "CP437" else "█".encode()
        before = wire(b"w0000070310", b"\x1b[?25l\x1b[5;32;44m"+glyph)
        colors = (plane_pixel(before, 2, 2, "primary"), plane_pixel(before, 2, 2, "alternate"))
        assert colors in ((0x00a800, 0x0000a8), (0x0000a8, 0x00a800)), colors
        frame = wire(scroll(2, 2, 3, 3, 0, 20))
        for phase, color in zip(("primary", "alternate"), colors):
            assert plane_pixel(frame, 2, 20, phase) == color
        frame = wire(scroll(2, 20, 3, 21, 6, 10))
        for phase, color in zip(("primary", "alternate"), colors):
            assert plane_pixel(frame, 2, 10, phase) == plane_pixel(frame, 2, 21, phase) == color
        # The renderer marks graphics-owned cells after physical writes.
        # Every logical cell value and the ANSI state must remain intact.
        assert [replace(cell, flags=cell.flags & ~8) for cell in frame.cells] == [
            replace(cell, flags=cell.flags & ~8) for cell in before.cells]
        for key in ("cterm.x", "cterm.y", "cterm.attr", "cterm.top_margin", "cterm.bottom_margin"):
            assert frame.application[key] == before.application[key]
        values(b"alive", b"alive")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("encoding", choices=("CP437", "UTF-8"))
    parser.add_argument("--reports", type=Path, required=True)
    args = parser.parse_args()
    for width, height in ((640, 480), (800, 600)):
        run(args, width, height)
