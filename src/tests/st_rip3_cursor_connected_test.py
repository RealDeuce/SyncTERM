#!/usr/bin/env python3
"""Independent RIP3 cursor-control acceptance through connected sessions."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path

from st_migrated_control_test import connection
from st_rip3_connected_test import pixel, query
from st_terminal_conformance_test import Terminal
from syncterm_fixture import receive


def visible(frame):
    key = "pixel_cursor_visible" if frame.metadata["pixel_cursor_enabled"] else "cursor_visible"
    return frame.metadata[key]


def xy(frame):
    return frame.application["cterm.x"], frame.application["cterm.y"]


def controls(t, columns, rows, ox, oy, cw, ch):
    # The selected and saved ANSI positions lie within both margin axes.
    modes = (f"\x1b[2;{rows-1}r\x1b[?69h\x1b[3;{columns-2}s"
             "\x1b[?6h\x1b[2;2H\x1b7\x1b[2 q\x1b[0;37m").encode()
    before = t.exchange(modes)
    assert xy(before) == (4, 3) and visible(before)
    frame = t.exchange(b"\x01|g012\r\n")
    assert xy(frame) == (2, 3) and frame.primary == before.primary
    assert frame.application["cterm.ext_attr.origin_mode"] == 1
    assert (frame.application["cterm.top_margin"], frame.application["cterm.bottom_margin"]) == (2, rows-1)
    assert (frame.application["cterm.left_margin"], frame.application["cterm.right_margin"]) == (3, columns-2)
    frame = t.exchange(b"\x01|g02|g00?\r\n")
    assert xy(frame) == (2, 3) and frame.primary == before.primary
    frame = t.exchange(b"\x01|g000\r\n")
    assert xy(frame) == (1, 1) and frame.primary == before.primary
    frame = t.exchange(b"\x1b8")
    assert xy(frame) == (4, 3) and frame.application["connected"] == 1
    frame = t.exchange(query(b"$COFF$", at_cursor=True))
    assert not visible(frame) and xy(frame) == (4, 3) and frame.primary == before.primary
    frame = t.exchange(query(b"$CON(CUR,36)$$TWGOTO(CUR,0,NO)$alive", at_cursor=True), b"alive")
    assert not visible(frame) and xy(frame) == (4, 3) and frame.primary == before.primary
    frame = t.exchange(query(b"$TWGOTO(CUR,99999999999999999999,99999999999999999999)$$CON$", at_cursor=True))
    assert xy(frame) == (columns, rows) and visible(frame) and frame.primary == before.primary
    assert frame.application["cterm.ext_attr.origin_mode"] == 1
    assert (frame.application["cterm.top_margin"], frame.application["cterm.bottom_margin"]) == (2, rows-1)
    assert (frame.application["cterm.left_margin"], frame.application["cterm.right_margin"]) == (3, columns-2)
    # Macro data is decimal even with UltraNum command radix selected.
    frame = t.exchange(b"\x01|J1S\r\n" + query(
        b"$TWGOTO(CUR,10)$$TWGOTO(CUR,CUR,1)$$TWGOTO$$TWGOTO(CUR)$", at_cursor=True))
    assert xy(frame) == (min(11, columns), 2) and frame.primary == before.primary
    frame = t.exchange(query(b"$TWHOME$", at_cursor=True))
    assert xy(frame) == (1, 1) and frame.primary == before.primary
    # ANSI saved coordinates and origin mode survive all native RIP moves.
    frame = t.exchange(b"\x1b8")
    assert xy(frame) == (4, 3) and frame.application["cterm.ext_attr.origin_mode"] == 1

    # Inactive TTY discards ANSI reply probes, so use a RIP host reply fence.
    t.send(query(b"$DTW$$COFF$$TWGOTO(CUR,3,4)$$TWHOME$inactive", at_cursor=True))
    receive(t.peer, b"inactive", t.root / "replies.bin")
    frame = t.client.capture()
    assert not visible(frame) and xy(frame) == (4, 3)
    frame = t.exchange(query(b"$ATW$", at_cursor=True))
    assert visible(frame) and xy(frame) == (4, 3)
    t.send(query(b"$DTW$inactive-again", at_cursor=True))
    receive(t.peer, b"inactive-again", t.root / "replies.bin")
    frame = t.client.capture()
    assert not visible(frame)
    frame = t.exchange(query(b"$ATW$$COFF$", at_cursor=True))
    assert not visible(frame) and xy(frame) == (4, 3)
    frame = t.exchange(query(
        b"$DTW$$CON$$TWGOTO(CUR,3,4)$$ATW$$CON(1,35)$$TWHOME(1)$", at_cursor=True))
    assert not visible(frame) and xy(frame) == (4, 3)
    frame = t.exchange(query(b"$CON(CUR,0,0)$$TWGOTO(CUR,10,3)$", at_cursor=True)
                       + b"Q" + query(b"rendered", at_cursor=True), b"rendered")
    assert visible(frame) and xy(frame) == (min(12, columns), 4)
    assert any(pixel(frame, x, y) == 0xa8a8a8
               for y in range(oy+3*ch, oy+4*ch)
               for x in range(ox+(min(11, columns)-1)*cw, ox+min(11, columns)*cw))
    assert frame.application["connected"] == 1
    end = t.exchange(b"\x01|gZZZZ|J10\r\n")
    assert xy(end) == (columns, rows) and end.primary == frame.primary


def run(args, width, height):
    with connection(args.executable, args.reports, encoding=args.encoding,
                    options="ScreenMode=Custom\nRIP=RIPv3\n",
                    settings=f"CustomColumns={width//8}\nCustomRows={height//8}\n"
                             "CustomFontHeight=8\nCustomAspectWidth=4\nCustomAspectHeight=3\n"
                             "AudioModes=0\n") as (client, root, peer, send):
        t = Terminal(client, root, peer, send)
        send(b"\x1b[!")
        receive(peer, b"RIPSCRIP030001", root / "replies.bin")
        t.case(f"rip3-cursor-variables-entry-{width}x{height}")
        controls(t, width//8, height//8, 0, 0, 8, 8)
        t.case(f"rip3-cursor-variables-positioned-{width}x{height}")
        t.exchange(b"\x01|w0A050H0A10\r\n")
        controls(t, 8, 6, 80*width//640, 40*height//350, width//80, height//43)

        t.case(f"rip3-cursor-and-window-wire-widths-{width}x{height}")
        t.exchange(b"\x01|n3|w..030310 comment|g001002tail\r\n\x1b[6n", b"\x1b[3;2R")
        # With XY 3, g0102 has X=36 and shortened Y=2 in MegaNum.
        t.exchange(b"\x01|g0102\r\n\x1b[6n", b"\x1b[3;4R")
        # Missing font in w and an invalid byte in a pending Y preserve state.
        t.exchange(b"\x01|w000009091|g01022|g0010?\r\n\x1b[6n", b"\x1b[4;4R")
        t.exchange(b"\x01|g..1\r\n\x1b[6n", b"\x1b[1;1R")
        frame = t.exchange(b"\x01|g.02\r\n\x1b[6n", b"\x1b[3;1R")
        assert xy(frame) == (1, 3) and frame.application["connected"] == 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("encoding", choices=("CP437", "UTF-8"))
    parser.add_argument("--reports", type=Path, required=True)
    args = parser.parse_args()
    for width, height in ((640, 480), (800, 600), (1024, 768)):
        run(args, width, height)


if __name__ == "__main__":
    main()
