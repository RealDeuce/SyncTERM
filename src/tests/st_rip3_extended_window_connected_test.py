#!/usr/bin/env python3
"""Connected b fitting/state/erase expectations from the pinned RIP3 contract."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path

from st_migrated_control_test import connection, square_canvas
from st_rip3_connected_test import pixel, query
from st_rip3_text_erase_connected_test import expect_region, number, scene
from st_terminal_conformance_test import Terminal
from syncterm_fixture import receive


def window(box, columns, rows, font, flags, xy=2, short=False):
    fields = b"".join(number(value, xy) for value in box)
    fields += number(columns, 2) + number(rows, 2) + number(font, 1)
    suffix = number(flags, 4)
    if short:
        suffix = suffix.lstrip(b"0") or b"0"
    return b"\x01|b" + fields + suffix + b"\r\n"


def xy(frame):
    return frame.application["cterm.x"], frame.application["cterm.y"]


def geometry(frame, width, height, x, y, cw, ch):
    m = frame.metadata
    assert (m["plane_width"], m["plane_height"], m["columns"], m["rows"]) == (width, height, width//8, height//8)
    assert (m["pixel_cursor_x"], m["pixel_cursor_y"], m["pixel_cursor_width"], m["pixel_cursor_height"]) == (x, y, cw, ch)


def run(args, width, height, cw, ch, font0_width, font0_height, columns, rows, box, origin):
    with connection(args.executable, args.reports, encoding=args.encoding,
                    options="ScreenMode=Custom\nRIP=RIPv3\n",
                    settings=square_canvas(width, height)) as (client, root, peer, send):
        t = Terminal(client, root, peer, send)
        send(b"\x1b[!")
        receive(peer, b"RIPSCRIP030001", root / "replies.bin")
        t.case(f"rip3-extended-fitting-and-repeat-{width}x{height}")
        before = t.exchange(scene(width, height) + query(b"$WORLD$", at_cursor=True), f"{width}:{height}".encode())
        command = window(box, columns, rows, 35, 8, short=True)
        frame = t.exchange(command + b"\x1b[2 q\x1b[6n", b"\x1b[1;1R")
        geometry(frame, width, height, *origin, cw, ch)
        assert frame.primary == before.primary and frame.application["cterm.ext_attr.auto_wrap"] == 0
        frame = t.exchange(b"\x01|gZZZZ\r\n\x1b[6n", f"\x1b[{rows};{columns}R".encode())
        geometry(frame, width, height, origin[0]+(columns-1)*cw, origin[1]+(rows-1)*ch, cw, ch)
        before = t.exchange(b"\x01|H\r\nF\x1b[3g\x1b[1;6H\x1bH\x1b[2;3r\x1b[?6h\x1b[2;2H\x1b[s\x1b[31;44m")
        frame = t.exchange(command + b"\x1b[6n", b"\x1b[1;1R")
        assert frame.primary == before.primary and frame.application["cterm.attr"] == 7
        assert frame.application["cterm.ext_attr.origin_mode"] == 0
        assert (frame.application["cterm.top_margin"], frame.application["cterm.bottom_margin"]) == (1, rows)
        t.exchange(b"\x1b[u\x1b[6n", b"\x1b[1;1R")
        t.exchange(b"\t\x1b[6n", b"\x1b[1;9R")
        t.exchange(query(b"$COFF$", at_cursor=True))
        frame = t.exchange(command)
        assert not frame.metadata["pixel_cursor_visible"]
        send(b"\x1b[3")
        frame = t.exchange(command + b"1m")
        assert frame.application["cterm.attr"] == 4 and xy(frame) == (1, 1)

        if (width, height) == (720, 400):
            t.case("rip3-extended-fit-without-a-matching-MAF-bank")
            # 20 mode-4 columns need 360 pixels. Mode 2 fits at 9x16.
            frame = t.exchange(query(b"$CON$", at_cursor=True)
                               + window((0, 0, 340, 300), 20, 15, 4, 8)
                               + b"\x1b[2 q" + query(b"$TWFONT$", at_cursor=True), b"3")
            geometry(frame, width, height, 80, 30, 9, 16)

        if width == 1024:
            t.case("rip3-extended-directed-height-fit")
            frame = t.exchange(query(b"$CON$", at_cursor=True)
                               + window((0, 0, 800, 500), 40, 25, 0, 8) + b"\x1b[2 q")
            geometry(frame, width, height, 160, 37, 12, 17)

        if width == 640:
            t.case("rip3-extended-window-field-and-reserved-shorthand")
            # Active XY 3. Short zero corners and flags, then a complete
            # three-digit ignored reserved field followed by surplus text.
            prefix = b"\x01|n3|b..02S02S02020"
            t.exchange(prefix + b".02Z comment|g001001\r\n\x1b[6n", b"\x1b[2;2R")
            prefix = b"\x01|b..02S02S02020"
            t.exchange(prefix + b"00010?\r\n\x1b[6n", b"\x1b[2;2R")
            t.exchange(prefix + b"0001Z\r\n\x1b[6n", b"\x1b[1;1R")
            frame = t.exchange(prefix + b"1|n2\r\n")
            assert frame.application["cterm.ext_attr.auto_wrap"] == 1

        t.case(f"rip3-extended-minimum-cell-{width}x{height}")
        frame = t.exchange(query(b"$CON$", at_cursor=True) + scene(width, height)
                           + window((20, 30, 21, 31), 1, 1, 4, 24) + b"\x1b[2 q")
        geometry(frame, width, height, 20, 30, font0_width, font0_height)
        expect_region(frame, width, height, (20, 30, font0_width, font0_height), 0xff0000)

        t.case(f"rip3-extended-clear-before-deactivation-{width}x{height}")
        send(scene(width, height) + window(box, columns, rows, 4, 30)
             + b"IGNORED\r\n"*4 + query(b"clear-off", at_cursor=True))
        receive(peer, b"clear-off", root / "replies.bin")
        frame = client.capture()
        bounding = (box[0], box[1], box[2]-box[0], box[3]-box[1])
        expect_region(frame, width, height, bounding, 0xff0000)
        assert not frame.metadata["pixel_cursor_visible"] and xy(frame) == (1, 1)
        label = b"\x82" if args.encoding == "CP437" else "é".encode()
        frame = t.exchange(query(b"$ATW$", at_cursor=True) + label)
        assert not frame.metadata["pixel_cursor_visible"] and xy(frame) == (2, 1)
        assert any(pixel(frame, x, y) == 0xa8a8a8 for y in range(origin[1], origin[1]+ch)
                   for x in range(origin[0], origin[0]+cw))
        frame = t.exchange(command)
        assert not frame.metadata["pixel_cursor_visible"] and xy(frame) == (1, 1)
        frame = t.exchange(scene(width, height) + query(b"$ETW$", at_cursor=True))
        expect_region(frame, width, height, bounding, 0xff0000)
        frame = t.exchange(b"\x01|a0009\r\n")
        expect_region(frame, width, height, bounding, 0x0000ff)

        t.case(f"rip3-extended-resource-and-offscreen-recovery-{width}x{height}")
        before = frame
        frame = t.exchange(query(b"$WORLD(CUR,1,1)$", at_cursor=True)
                           + window((0, 0, 1295, 1295), 1295, 1295, 4, 16))
        assert frame.primary == before.primary
        t.exchange(b"\x01|gZZZZ\r\n\x1b[6n", f"\x1b[{rows};{columns}R".encode())
        before = t.exchange(query(b"$CON$", at_cursor=True))
        frame = t.exchange(b"\x01|n5\r\n" + window((16777216, 0, 16777217, 1), 1, 1, 4, 0, xy=5)
                           + b"A\x01|gZZZZ\r\n")
        assert xy(frame) == (1, 1) and not frame.metadata["pixel_cursor_visible"] and frame.primary == before.primary
        frame = t.exchange(query(f"$WORLD(CUR,{width},{height})$".encode(), at_cursor=True)
                           + b"\x01|n2\r\n" + command)
        assert frame.metadata["pixel_cursor_visible"] and xy(frame) == (1, 1)
        # A protection request at TW0 diagnoses without undoing definition;
        # ordinary w can still replace it, independently of WORLD=0.
        t.exchange(window(box, 1, 1, 4, 32))
        frame = t.exchange(query(b"$WORLD(CUR,0,0)$", at_cursor=True)
                           + b"\x01|w0A050H0A10|gZZZZ\r\n\x1b[6n", b"\x1b[6;8R")
        assert xy(frame) == (8, 6) and frame.application["connected"] == 1

        if width == 640:
            t.case("Scrolling a text window moves the RIP pixels inside its edge")
            # Font 0 at 640x480 has 8x11 cells. The last pixel in this
            # three-column window is x=23. Start both marks on its second row.
            frame = t.exchange(b"\x01|*|fHSDC|w0000020210|c0C|X0N0C|c0A|X0O0C|g0002\r\n"
                               b"\x1b[?25l\r\n\x1b[6n", b"\x1b[3;1R")
            assert pixel(frame, 23, 1) == 0xff5555 and pixel(frame, 23, 12) == 0
            assert pixel(frame, 24, 1) == 0 and pixel(frame, 24, 12) == 0x55ff55


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("encoding", choices=("CP437", "UTF-8"))
    parser.add_argument("--reports", type=Path, required=True)
    args = parser.parse_args()
    for case in ((640, 480, 16, 19, 8, 11, 20, 15, (10, 20, 630, 460), (160, 97)),
                 (800, 600, 20, 24, 10, 13, 20, 15, (10, 20, 790, 580), (200, 120)),
                 (1024, 768, 20, 24, 12, 17, 40, 25, (10, 20, 1010, 620), (110, 20)),
                 (792, 592, 18, 21, 9, 12, 20, 15, (10, 20, 782, 572), (216, 138)),
                 (720, 400, 18, 16, 9, 9, 20, 15, (10, 20, 710, 380), (180, 80))):
        run(args, *case)


if __name__ == "__main__":
    main()
