#!/usr/bin/env python3
"""RIP3 graphics-color text erasure through real connected sessions."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path

from st_migrated_control_test import connection
from st_rip3_connected_test import pixel, query
from st_terminal_conformance_test import Terminal
from syncterm_fixture import receive


def number(value, width):
    digits = b"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    result = bytearray(b"0" * width)
    for at in range(width-1, -1, -1):
        value, digit = divmod(value, 36)
        result[at] = digits[digit]
    assert value == 0
    return bytes(result)


def scene(width, height):
    far = number(width, 2) + number(height, 2)
    return (b"\x01|M00|n2|f" + number(width, 2) + number(height, 2)
            + b"|a0010|a0101|v0000" + far + b"|W0|c02|S0102|B0000" + far
            + b"|k01|W1|S000F|v00000000\r\n")


def expect_region(frame, width, height, rectangle, colour):
    ox, oy, rw, rh = rectangle
    for y in range(oy, min(oy+rh, height)):
        for x in range(ox, min(ox+rw, width)):
            assert pixel(frame, x, y) == colour, (x, y, hex(pixel(frame, x, y)), hex(colour))
    # Full edge strips and distant corners must retain the green scene.
    for y in (oy-1, oy+rh):
        if 0 <= y < height:
            assert all(pixel(frame, x, y) == 0x00aa00 for x in range(max(0, ox-1), min(ox+rw+1, width)))
    for x in (ox-1, ox+rw):
        if 0 <= x < width:
            assert all(pixel(frame, x, y) == 0x00aa00 for y in range(max(0, oy-1), min(oy+rh+1, height)))
    for x, y in ((0, 0), (width-1, 0), (0, height-1), (width-1, height-1)):
        if not (ox <= x < ox+rw and oy <= y < oy+rh):
            assert pixel(frame, x, y) == 0x00aa00


def controls(t, width, height, columns, rows, ox, oy, cw, ch, encoding):
    modes = (f"\x1b[2;{rows-1}r\x1b[?69h\x1b[3;{columns-2}s"
             "\x1b[?6h\x1b[2;2H\x1b7\x1b[31;41;4m").encode()
    before = t.exchange(scene(width, height) + modes)
    expect_region(before, width, height, (0, 0, width, height), 0x00aa00)
    # Inactive graphics viewport, XOR, empty fill and red ANSI background
    # must not alter STYLE-blue COPY erasure through the full display row.
    eol = (ox+3*cw, oy+2*ch, (columns-3)*cw, ch)
    frame = t.exchange(b"\x01|> erase to end\r\n")
    expect_region(frame, width, height, eol, 0x0000a8)
    assert (frame.application["cterm.x"], frame.application["cterm.y"]) == (4, 3)
    assert frame.application["cterm.attr"] == 0x44
    frame = t.exchange(b"\x01|a0109\r\n")
    expect_region(frame, width, height, eol, 0x0000ff)
    # Repeating the same index-plane paint still overwrites intervening ink.
    label = b"\x82" if encoding == "CP437" else "é".encode()
    frame = t.exchange(label + query(b"$TWGOTO(CUR,3,2)$", at_cursor=True) + b"\x01|>\r\n")
    expect_region(frame, width, height, eol, 0x0000ff)

    whole = (ox, oy, columns*cw, rows*ch)
    frame = t.exchange(b"\x01|e erase window\r\n")
    expect_region(frame, width, height, whole, 0xff0000)
    assert (frame.application["cterm.x"], frame.application["cterm.y"]) == (1, 1)
    assert frame.application["cterm.ext_attr.origin_mode"] == 1 and frame.application["cterm.attr"] == 0x44
    assert (frame.application["cterm.top_margin"], frame.application["cterm.bottom_margin"]) == (2, rows-1)
    assert (frame.application["cterm.left_margin"], frame.application["cterm.right_margin"]) == (3, columns-2)
    frame = t.exchange(b"\x01|a0009\r\n")
    expect_region(frame, width, height, whole, 0x0000ff)
    before = t.exchange(b"\x1b8" + scene(width, height))
    assert (before.application["cterm.x"], before.application["cterm.y"]) == (4, 3)
    frame = t.exchange(query(b"$ETW(CUR,36)$alive", at_cursor=True), b"$ETW(CUR,36)$alive")
    assert frame.primary == before.primary
    assert (frame.application["cterm.x"], frame.application["cterm.y"]) == (4, 3)
    # Whole erase is always palette index 0, even in RGB mode and with a
    # zero WORLD that cannot be used by geometric drawing commands.
    frame = t.exchange(query(b"$COLORMODE(CUR,RGB,1)$$WORLD(CUR,0,0)$$ETW(ALL)$", at_cursor=True))
    expect_region(frame, width, height, whole, 0xff0000)
    assert (frame.application["cterm.x"], frame.application["cterm.y"]) == (1, 1)
    t.send(query(b"$DTW$off", at_cursor=True))
    receive(t.peer, b"off", t.root / "replies.bin")
    before = t.client.capture()
    t.send(b"\x01|e|>|e0|>0\r\n" + query(b"$ETW(ALL)$ignored", at_cursor=True))
    receive(t.peer, b"ignored", t.root / "replies.bin")
    frame = t.client.capture()
    assert frame.primary == before.primary
    assert (frame.application["cterm.x"], frame.application["cterm.y"]) == (1, 1)
    frame = t.exchange(query(f"$ATW$$WORLD(CUR,{width},{height})$on".encode(), at_cursor=True), b"on")
    assert frame.primary == before.primary and frame.application["connected"] == 1


def ansi_controls(t, width, height, columns, rows, ox, oy, cw, ch, encoding):
    ansi_red = 0xa80000
    modes = (f"\x1b[2;{rows-1}r\x1b[?69h\x1b[3;{columns-2}s"
             "\x1b[?6h\x1b[2;2H\x1b7\x1b[31;41;4m").encode()
    before = t.exchange(scene(width, height) + modes)
    frame = t.exchange(query(b"$TWERASEEOL(CUR,36)$alive", at_cursor=True), b"$TWERASEEOL(CUR,36)$alive")
    assert frame.primary == before.primary
    eol = (ox+3*cw, oy+2*ch, (columns-3)*cw, ch)
    frame = t.exchange(query(b"$tweraseeol( all,cur,0,0 )$", at_cursor=True))
    # Both Custom mode and private MicroANSI windows use the PC ANSI palette.
    expect_region(frame, width, height, eol, ansi_red)
    assert (frame.application["cterm.x"], frame.application["cterm.y"]) == (4, 3)
    assert frame.application["cterm.attr"] == 0x44 and frame.application["cterm.ext_attr.origin_mode"] == 1
    assert (frame.application["cterm.top_margin"], frame.application["cterm.bottom_margin"]) == (2, rows-1)
    assert (frame.application["cterm.left_margin"], frame.application["cterm.right_margin"]) == (3, columns-2)
    frame = t.exchange(query(b"$TWERASEEOL(1,35)$", at_cursor=True))
    expect_region(frame, width, height, eol, ansi_red)
    # Omitted selector defaults to current, including direct ANSI RGB.
    t.exchange(scene(width, height) + b"\x1b[48;2;18;52;86m")
    frame = t.exchange(query(b"$TWERASEEOL$", at_cursor=True))
    expect_region(frame, width, height, eol, 0x123456)
    # An unfinished ANSI CSI survives the command-variable expansion.
    t.send(scene(width, height) + b"\x1b[4")
    frame = t.exchange(query(b"$TWERASEEOL$", at_cursor=True) + b"1m")
    expect_region(frame, width, height, eol, 0x123456)
    assert frame.application["cterm.attr"] == 0x44
    frame = t.exchange(b"\x1b8\x1b[6n", b"\x1b[2;2R")
    assert (frame.application["cterm.x"], frame.application["cterm.y"]) == (4, 3)
    label = b"\x82" if encoding == "CP437" else "é".encode()
    # Repeating ANSI-background paint erases subsequent non-ASCII ink too.
    # ESC 8 restored the rendition saved before the test selected red.
    frame = t.exchange(scene(width, height) + b"\x1b[41m" + label
                       + query(b"$TWGOTO(CUR,3,2)$$COFF$$TWERASEEOL$", at_cursor=True))
    expect_region(frame, width, height, eol, ansi_red)
    assert not frame.metadata["pixel_cursor_visible"]
    last_cell = (ox+(columns-1)*cw, oy+(rows-1)*ch, cw, ch)
    frame = t.exchange(scene(width, height)
                       + query(f"$TWGOTO(CUR,{columns-1},{rows-1})$".encode(), at_cursor=True)
                       + label + query(b"$TWERASEEOL$", at_cursor=True))
    expect_region(frame, width, height, last_cell, ansi_red)
    assert (frame.application["cterm.x"], frame.application["cterm.y"]) == (columns, rows)
    assert frame.application["cterm.last_column_flag.set"] == 1
    t.send(query(b"$DTW$off", at_cursor=True))
    receive(t.peer, b"off", t.root / "replies.bin")
    before = t.client.capture()
    t.send(query(b"$TWERASEEOL(ALL)$ignored", at_cursor=True))
    receive(t.peer, b"ignored", t.root / "replies.bin")
    frame = t.client.capture()
    assert frame.primary == before.primary
    frame = t.exchange(query(b"$ATW$$CON$on", at_cursor=True), b"on")
    assert frame.application["connected"] == 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("encoding", choices=("CP437", "UTF-8"))
    parser.add_argument("--reports", type=Path, required=True)
    args = parser.parse_args()
    for width, height in ((640, 480), (800, 600), (1024, 768)):
        with connection(args.executable, args.reports, encoding=args.encoding,
                        options="ScreenMode=Custom\nRIP=RIPv3\n",
                        settings=f"CustomColumns={width//8}\nCustomRows={height//8}\n"
                                 "CustomFontHeight=8\nCustomAspectWidth=4\nCustomAspectHeight=3\n"
                                 "AudioModes=0\n") as (client, root, peer, send):
            t = Terminal(client, root, peer, send)
            send(b"\x1b[!")
            receive(peer, b"RIPSCRIP030001", root / "replies.bin")
            t.case(f"rip3-text-erase-entry-{width}x{height}")
            controls(t, width, height, 80, 42, 0, 0, width//80, height//43, args.encoding)
            t.case(f"rip3-ansi-eol-entry-{width}x{height}")
            ansi_controls(t, width, height, 80, 42, 0, 0, width//80, height//43, args.encoding)
            t.case(f"rip3-text-erase-positioned-{width}x{height}")
            t.exchange(b"\x01|w0A050H0A10\r\n")
            controls(t, width, height, 8, 6, 80*width//640, 40*height//350, width//80, height//43, args.encoding)
            t.case(f"rip3-ansi-eol-positioned-{width}x{height}")
            ansi_controls(t, width, height, 8, 6, 80*width//640, 40*height//350, width//80, height//43, args.encoding)
            t.case(f"rip3-ansi-eol-extended-{width}x{height}")
            # An extended box has real margins surrounding its centered cells.
            # EOL clears only the display row, leaving all four margins intact.
            cw, ch = {640: (8, 11), 800: (10, 13), 1024: (12, 17)}[width]
            extended = (b"\x01|b" + b"".join(number(n, 2) for n in (100, 50, 400, 250, 20, 10))
                        + b"00001\r\n")
            t.exchange(scene(width, height) + extended)
            ansi_controls(t, width, height, 20, 10, 100+(300-20*cw)//2, 50+(200-10*ch)//2,
                          cw, ch, args.encoding)


if __name__ == "__main__":
    main()
