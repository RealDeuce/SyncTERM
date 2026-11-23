#!/usr/bin/env python3
"""Independent MicroANSI cell masks in connected RIPv3 text regions."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path

from st_migrated_control_test import connection
from st_rip3_button_styles_connected_test import number
from st_rip3_connected_test import query
from st_rip3_scroll_connected_test import plane_pixel
from st_terminal_conformance_test import Terminal


# RIPtel MicroANSI 640x480 80x43 bank, glyph codes 0x41 and 0x82.
CAPITAL_A = bytes.fromhex("38 6c c6 c6 fe fe c6 c6 c6 00 00")
ACCENT = bytes.fromhex("04 08 10 00 78 cc fc c0 78 00 00")
RED = 0xAA0000
GREEN = 0x00AA00


def region(area, column=0, article=1):
    return b"1T" + b"".join(number(value, 2) for value in area) + number(column, 1) + number(article, 1)


def extended(area, column, article, flags, color=0, words=b""):
    return b"1e" + region(area, column, article)[2:] + number(flags, 4) + number(color, 2) + b"0" * 8 + words


def commands(*parts):
    return b"\x01|" + b"|".join(parts) + b"\r\n"


def check_pixels(frame, bounds, placements):
    expected = {}
    for x, y, mask, color in placements:
        for dy, row in enumerate(mask):
            for dx in range(8):
                if row & (0x80 >> dx):
                    expected[x + dx, y + dy] = color
    x0, y0, x1, y1 = bounds
    for phase in ("primary", "alternate"):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                actual = plane_pixel(frame, x, y, phase)
                assert actual == expected.get((x, y), 0), (phase, x, y, actual, expected.get((x, y), 0))


def run(args):
    with connection(args.executable, args.reports, encoding=args.encoding,
                    options="ScreenMode=Custom\nRIP=RIPv3\n",
                    settings="CustomColumns=80\nCustomRows=60\nCustomFontHeight=8\nAudioModes=0\n") as (
                        client, root, peer, send):
        terminal = Terminal(client, root, peer, send)

        def wire(*parts):
            return terminal.exchange(commands(*parts))

        accent = b"\x82" if args.encoding == "CP437" else "é".encode("utf-8")
        terminal.exchange(b"\x1b[!", b"RIPSCRIP030001")
        terminal.exchange(query(b"$WORLD(CUR,640,480)$", at_cursor=True))

        terminal.case("RIPv3 region normalization, full justification and end")
        frame = wire(b"c04", region((59, 61, 20, 40)), b"1t1A " + accent,
                     b"c02", b"1t0" + accent, b"1t0A", b"1E", b"1t0A")
        check_pixels(frame, (17, 36, 63, 75),
                     [(20, 40, CAPITAL_A, RED), (52, 40, ACCENT, RED), (20, 51, ACCENT, GREEN)])

        terminal.case("RIPv3 columns visit numeric slots and skip holes")
        frame = wire(region((70, 110, 77, 120), 17, 2), region((20, 110, 27, 120), 3, 2),
                     b"c04", b"1t0A", b"c02", b"1t0" + accent, b"1t0A", b"1E")
        check_pixels(frame, (16, 105, 81, 134), [(20, 110, CAPITAL_A, RED), (70, 110, ACCENT, GREEN)])

        terminal.case("RIPv3 region syntax recovery and scene end")
        frame = wire(region((20, 145, 27, 155), 4, 3), b"1T00", b"1t2A", b"1t0A", b"#", b"1t0A")
        check_pixels(frame, (16, 141, 32, 168), [(20, 145, CAPITAL_A, GREEN)])
        terminal.exchange(query(b"alive", at_cursor=True), b"alive")

        terminal.case("RIPv3 region ink is clipped by the column and viewport")
        viewport = b"v" + b"".join(number(value, 2) for value in (23, 160, 94, 194))
        marker = b"X" + number(23, 2) + number(160, 2)
        frame = wire(viewport, region((20, 170, 25, 180), 5, 4), b"1t0AAAA", b"1E", b"c04", marker)
        check_pixels(frame, (17, 167, 33, 184), [(20, 170, bytes(row & 0x1C for row in CAPITAL_A), GREEN)])
        assert plane_pixel(frame, 23, 160, "primary") == RED

        full_view = b"v0000" + number(639, 2) + number(479, 2)
        terminal.case("Raw article text wraps across slots and returns to the terminal at end")
        frame = terminal.exchange(commands(full_view, region((170, 40, 193, 50), 31, 6),
                                           region((130, 40, 153, 50), 27, 6), b"c02")
                                  + b"A\r\nA " + accent + b"\r\n!|1E\r\n\x1b[3;2HOK")
        check_pixels(frame, (126, 39, 196, 54),
                     [(130, 40, CAPITAL_A, GREEN), (146, 40, CAPITAL_A, GREEN), (170, 40, ACCENT, GREEN)])
        assert frame.lines[2][1:3] == "OK"

        terminal.case("Raw CRLF paragraphs and same-article redefinition")
        paragraph = region((130, 75, 177, 107), 0, 7)
        frame = terminal.exchange(commands(paragraph) + b"A\r\n" + commands(paragraph)
                                  + accent + b"\r\n\r\nA\r\n!|#\r\n")
        check_pixels(frame, (126, 72, 181, 110), [(130, 75, ACCENT, GREEN), (130, 97, CAPITAL_A, GREEN)])

        terminal.case("Changing articles flushes the old article's pending text")
        frame = terminal.exchange(commands(region((130, 130, 153, 140), 1, 10)) + b"A\r\n"
                                  + commands(region((161, 130, 184, 140), 2, 11)) + accent + b"\r\n!|1E\r\n")
        check_pixels(frame, (126, 128, 187, 142), [(130, 130, CAPITAL_A, GREEN), (161, 130, ACCENT, GREEN)])

        terminal.case("An open article flows cached TXT content as literal prose")
        shared = root / "RIP"
        shared.mkdir(exist_ok=True)
        (shared / "Story.txt").write_bytes(b"!|c04\r\nA")
        frame = wire(extended((195, 100, 250, 121), 0, 8, 0), b"1R00000000STORY.TXT", b"1E",
                     b"X" + number(193, 2) + number(97, 2))
        assert plane_pixel(frame, 193, 97, "primary") == GREEN
        assert any(plane_pixel(frame, x, y, "primary") == GREEN
                   for x in range(195, 251) for y in range(100, 111))

        terminal.case("The scene macro also feeds cached prose into an open article")
        (shared / "Accent.txt").write_bytes(accent)
        frame = terminal.exchange(commands(extended((195, 150, 250, 171), 0, 8, 0))
                                  + query(b"$>Accent.TXT$ready", at_cursor=True) + commands(b"1E"), b"ready")
        check_pixels(frame, (191, 146, 254, 174), [(195, 150, ACCENT, GREEN)])

        terminal.case("Extended columns expand search words and highlight complete tokens")
        terminal.exchange(query(b"$-=FIND=a$", at_cursor=True))
        actual = extended((300, 120, 379, 141), 5, 9, 2, 4, b"  $&FIND$  ")
        invalid = extended((300, 170, 379, 191), 5, 9, 2, 4, b"ABCDEFGHIJKLMNOPQ")
        frame = terminal.exchange(commands(actual) + b"A A" + accent + b" " + commands(invalid) + b"A\r\n!|1E\r\n")
        check_pixels(frame, (297, 118, 381, 144),
                     [(300, 120, CAPITAL_A, RED), (316, 120, CAPITAL_A, GREEN),
                      (324, 120, ACCENT, GREEN), (340, 120, CAPITAL_A, RED)])
        check_pixels(frame, (297, 168, 381, 194), [])

        terminal.case("Legacy definitions retain the slot's search list")
        frame = terminal.exchange(commands(region((300, 200, 379, 210), 5, 12)) + b"A\r\n!|1E\r\n")
        check_pixels(frame, (297, 197, 382, 213), [(300, 200, CAPITAL_A, RED)])

        terminal.case("RIPv3 regions use the selected outline font and preserve the drawing cursor")
        wire(full_view, b"m8C8C", b"c04",
             b"y00000W0M000000001a1a000000Dixon", region((110, 200, 190, 279), 5, 5),
             b"1t0F", b"1E")
        frame = terminal.exchange(query(b"$TEXTXY$", at_cursor=True), b"300:300")
        assert any(plane_pixel(frame, x, y, "primary") == RED
                   for y in range(200, 280) for x in range(110, 191))
        for phase in ("primary", "alternate"):
            assert all(plane_pixel(frame, x, y, phase) == 0 for x, y in ((109, 200), (191, 200), (110, 199)))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("encoding", choices=("CP437", "UTF-8"))
    parser.add_argument("--reports", type=Path, required=True)
    run(parser.parse_args())
