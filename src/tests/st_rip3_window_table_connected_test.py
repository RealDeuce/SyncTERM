#!/usr/bin/env python3
"""Independent connected text-window selection, protection and target effects."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path
import time

from st_migrated_control_test import connection
from st_rip3_connected_test import pixel, query
from st_rip3_extended_window_connected_test import window
from st_rip3_text_erase_connected_test import expect_region, scene
from st_terminal_conformance_test import Terminal
from syncterm_fixture import receive


def first_use_transition(t, capture, values, width):
    t.case("rip3-window-first-use-from-styled-ordinary-window")
    values(b"$RESET(HARD)$$CUR(PORT,5)$$CUR(STYLE,2)$$CUR(ENV,3)$")
    # Font 2, twenty columns by ten rows. CUP addresses relative to
    # the top margin after origin mode is set.
    source = capture(b"\x01|w00000J0912\r\n"
                     b"\x1b[3g\x1b[1;5H\x1bH\x1b[1;11H\x1bH\x1b[2;7r\x1b[?6h"
                     b"\x1b[1;3H\x1b[s\x1b[4;8H\x1b[1;32;44m\x1b[2 q\x1b[?25l"
                     b"\x1b[38;2;12;34;56m\x1b[48;2;65;43;21m")
    def source_state(frame):
        app = frame.application
        assert (app["cterm.x"], app["cterm.y"]) == (8, 5)
        assert (app["cterm.top_margin"], app["cterm.bottom_margin"]) == (2, 7)
        assert app["cterm.ext_attr.origin_mode"] and app["cterm.ext_attr.auto_wrap"]
        assert not frame.metadata["pixel_cursor_visible"]

    source_state(source)
    clone = capture(b"\x01|2T30\r\n")
    source_state(clone)
    assert source.primary == clone.primary and source.alternate == clone.alternate
    values(b"$TWW$:$TWH$:$TWFONT$:$ISPROT(TW,3)$:$CUR(PORT)$:$CUR(STYLE)$:$CUR(ENV)$",
           b"20:10:3:0:5:2:3")
    frame = capture(b"\x1b[u" + query(b"$CON$", at_cursor=True))
    assert (frame.application["cterm.x"], frame.application["cterm.y"]) == (3, 2)
    m = frame.metadata
    cw, ch = {640: (8, 19), 800: (10, 24), 1024: (12, 30)}[width]
    assert m["pixel_cursor_visible"] and (m["pixel_cursor_width"], m["pixel_cursor_height"]) == (cw, ch)
    x, y = m["pixel_cursor_x"], m["pixel_cursor_y"]
    frame = capture(b"F\t")
    assert {pixel(frame, xx, yy) for yy in range(y, y+ch) for xx in range(x, x+cw)} == {0x0c2238, 0x412b15}
    assert frame.application["cterm.x"] == 5
    frame = capture(b"\t")
    assert frame.application["cterm.x"] == 11
    capture(b"\x1b[0;94m\x1b[3g\x1b[2;3H\x1bH\x1b[2;2H" + query(b"$PROT(TW,3)$", at_cursor=True))
    source_state(capture(b"\x01|2T00\r\n"))
    frame = capture(b"\x1b[u\t")
    assert (frame.application["cterm.x"], frame.application["cterm.y"]) == (5, 2)
    frame = capture(b"\x1b[3" + query(b"$CUR(TW,3)$", at_cursor=True) + b"1m\t")
    assert (frame.application["cterm.x"], frame.application["cterm.y"]) == (3, 3)
    assert frame.application["cterm.attr"] & 7 == 4  # The split SGR selected red in TW 3.
    frame = values(b"$CUR(TW,4)$$ISPROT(TW,3)$:$TWIN$:$TWW$:$TWH$:$TWFONT$:$CURSOR$",
                   b"1:YES:80:42:1:YES")
    assert (frame.application["cterm.x"], frame.application["cterm.y"], frame.application["cterm.attr"]) == (1, 1, 7)
    assert (frame.metadata["pixel_cursor_width"], frame.metadata["pixel_cursor_height"]) == (width//80, 1)
    assert frame.application["cterm.top_margin"] == 1 and frame.application["cterm.bottom_margin"] == 42


def run(args, width, height):
    with connection(args.executable, args.reports, encoding=args.encoding,
                    options="ScreenMode=Custom\nRIP=RIPv3\n",
                    settings=f"CustomColumns={width//8}\nCustomRows={height//8}\n"
                             "CustomFontHeight=8\nCustomAspectWidth=4\nCustomAspectHeight=3\n"
                             "AudioModes=0\n") as (client, root, peer, send):
        t = Terminal(client, root, peer, send)
        serial = 0

        def capture(data=b"", expected=b""):
            nonlocal serial
            serial += 1
            fence = f"@TW{serial}@".encode()
            send(data + query(fence, at_cursor=True))
            received = bytearray()
            deadline = time.monotonic() + 10
            while not received.endswith(fence):
                peer.settimeout(max(0.001, deadline-time.monotonic()))
                part = peer.recv(65536)
                assert part, "connection closed"
                received.extend(part)
                with (root / "replies.bin").open("ab") as stream:
                    stream.write(part)
            assert received == expected+fence, (bytes(received), expected+fence)
            return client.capture()

        def variables(text, expected=b""):
            return capture(query(text, at_cursor=True), expected)

        send(b"\x1b[!")
        receive(peer, b"RIPSCRIP030001", root / "replies.bin")
        t.case(f"rip3-window-root-copy-{width}x{height}")
        before = capture(scene(width, height) + b"F")
        frame = capture(b"\x01|2T10\r\n" + query(b"$CUR(TW)$:$INUSE(TW,1)$", at_cursor=True), b"1:1")
        assert all((a if a & 0x80000000 else frame.palette[a]) & 0xffffff
                   == (b if b & 0x80000000 else before.palette[b]) & 0xffffff
                   for a, b in zip(frame.primary, before.primary))
        assert (frame.application["cterm.x"], frame.application["cterm.y"]) == (2, 1)
        frame = capture(b"F")
        # Both glyphs are observed in one capture, after the source root has
        # stopped receiving text; a cloned font must retain the same artwork.
        assert all(pixel(frame, x, y) == pixel(frame, x+width//80, y) for y in range(height//43) for x in range(width//80))
        cw, ch = width//80, height//43
        ox, oy = 80*width//640, 40*height//350
        capture(b"\x01|w0A050H0A10\r\n\x1b[31;41m\x1b[2;2H\x1b7")
        before = variables(b"$CUR(TW)$", b"1")
        frame = capture(b"\x01|2T20\r\n")
        assert all((a if a & 0x80000000 else frame.palette[a]) & 0xffffff
                   == (b if b & 0x80000000 else before.palette[b]) & 0xffffff
                   for a, b in zip(frame.primary, before.primary)) and frame.application["cterm.attr"] == 0x44
        capture(window((400, 100, 600, 300), 10, 8, 0, 1) + b"\x1b[32;44m\x01|g0201\r\n")
        frame = variables(b"$CUR(TW,1)$$CUR(TW)$", b"1")
        assert (frame.application["cterm.x"], frame.application["cterm.y"]) == (2, 2)
        assert frame.application["cterm.attr"] == 0x44
        frame = variables(b"$CUR(TW,2)$$TWIN$:$ISEXTWIN$", b"YES:1")
        assert (frame.application["cterm.x"], frame.application["cterm.y"]) == (3, 2)
        assert frame.application["cterm.attr"] == 0x12

        t.case(f"rip3-window-noncurrent-effects-{width}x{height}")
        before = capture(scene(width, height))
        frame = variables(b"$TWGOTO(1,3,2)$$COFF(1)$$TWERASEEOL(1)$")
        expect_region(frame, width, height, (ox+3*cw, oy+2*ch, 5*cw, ch), 0xa80000)
        assert (frame.application["cterm.x"], frame.application["cterm.y"]) == (3, 2)
        assert frame.metadata["pixel_cursor_visible"] == before.metadata["pixel_cursor_visible"]
        assert (frame.metadata["pixel_cursor_x"], frame.metadata["pixel_cursor_y"]) == (
            before.metadata["pixel_cursor_x"], before.metadata["pixel_cursor_y"])
        frame = variables(b"$ETW(1)$")
        expect_region(frame, width, height, (ox, oy, 8*cw, 6*ch), 0xff0000)
        assert (frame.application["cterm.x"], frame.application["cterm.y"]) == (3, 2)
        assert frame.application["cterm.attr"] == 0x12

        t.case(f"rip3-window-protection-and-activation-{width}x{height}")
        before = capture(scene(width, height) + query(b"$PROT(TW,1)$$CUR(TW,1)$$ISPROT(TW,1)$", at_cursor=True), b"1")
        frame = capture(b"\x01|w0000000000|w0000030312\r\n"
                        + window((0, 0, 100, 100), 3, 3, 0, 16))
        assert all((a if a & 0x80000000 else frame.palette[a]) & 0xffffff
                   == (b if b & 0x80000000 else before.palette[b]) & 0xffffff
                   for a, b in zip(frame.primary, before.primary)) and frame.application["cterm.attr"] == 0x44
        # Wire first use can read a protected source; the clone is unprotected.
        frame = capture(b"\x01|2T30\r\n"
                        + query(b"$CUR(TW)$:$ISPROT(TW,CUR)$:$ISPROT(TW,1)$", at_cursor=True), b"3:0:1")
        assert (frame.application["cterm.x"], frame.application["cterm.y"], frame.application["cterm.attr"]) == (1, 1, 0x44)
        capture(b"\x01|2T505 note\r\n")
        variables(b"$CUR(TW)$:$ISPROT(TW,3)$:$ISPROT(TW,5)$", b"5:1:1")
        capture(b"\x01|2T30Fignored\r\n")
        variables(b"$CUR(TW)$:$ISPROT(TW,3)$:$ISPROT(TW,5)$", b"3:0:0")
        capture(b"\x01|2T|2T5|2T50.|2T3. note\r\n")
        variables(b"$CUR(TW)$:$ISPROT(TW,3)$", b"3:0")
        variables(b"$CUR(TW,1)$")
        before = capture(scene(width, height))
        frame = variables(b"$CUR(TW,4)$$TWW$:$TWH$:$TWFONT$:$CURX$:$CURY$:$CURSOR$:$ISPROT(TW,CUR)$",
                          b"80:42:1:1:1:YES:0")
        assert frame.application["cterm.attr"] == 7
        assert all(pixel(frame, x, y) == pixel(before, x, y)
                   for y in range(height) for x in range(width))
        # CUR's fresh window has no explicit tab stops, unlike a wire clone.
        frame = capture(b"\t")
        assert (frame.application["cterm.x"], frame.application["cterm.y"]) == (1, 2)
        capture(b"\x01|w0000030312\r\n")  # release the large default plane
        variables(b"$CUR(TW,1)$")
        variables(b"$DTW(1)$$TWIN(1)$", b"YES")
        variables(b"$UNPROT(TW,1)$$DTW(1)$$PROT(TW,1)$$ATW(1)$$TWIN(1)$", b"NO")
        variables(b"$CUR(TW,2)$$INUSE(TW,1)$:$TWIN(1)$:$ISEXTWIN(1)$", b"1:NO:-1")
        frame = variables(b"$CUR(TW,1)$")
        assert not frame.metadata["pixel_cursor_visible"]
        before = frame
        label = b"\x82" if args.encoding == "CP437" else "é".encode()
        frame = capture(label + query(b"$ETW(1)$$TWERASEEOL(1)$", at_cursor=True))
        assert all((a if a & 0x80000000 else frame.palette[a]) & 0xffffff
                   == (b if b & 0x80000000 else before.palette[b]) & 0xffffff
                   for a, b in zip(frame.primary, before.primary))
        frame = variables(b"$UNPROT(TW,1)$$ATW(1)$")
        assert not frame.metadata["pixel_cursor_visible"]
        frame = variables(b"$CON(1)$$UNPROT(TW,1)$")
        assert frame.metadata["pixel_cursor_visible"]
        frame = capture(b"\x01|w0000030312\r\n")
        assert (frame.application["cterm.x"], frame.application["cterm.y"]) == (1, 1)
        # One connection-owned CSI completes in the selected destination.
        frame = capture(b"\x1b[3\x01|2T00\r\n" + query(b"$CUR(TW)$", at_cursor=True), b"0")
        frame = capture(b"1m")
        assert frame.application["cterm.attr"] == 4
        variables(b"$CUR(TW,0)$$CUR(TW)$", b"0")
        frame = variables(b"$CUR(TW,1)$")
        assert frame.application["cterm.attr"] == 7
        frame = capture(query(b"$DTW(1)$", at_cursor=True) + b"\x01|2T60\r\n"
                        + query(b"$TWIN$:$INUSE(TW,6)$", at_cursor=True), b"NO:1")
        assert not frame.metadata["pixel_cursor_visible"]
        frame = variables(b"$ATW(6)$$CUR(TW,1)$$TWIN$", b"NO")
        assert not frame.metadata["pixel_cursor_visible"]
        variables(b"$ATW(1)$")
        if width == 640 and args.encoding == "CP437":
            t.case("rip3-window-aggregate-resource-budget")
            capture(b"\x01|n3\r\n" + window((0, 0, 2560, 1440), 320, 120, 0, 1, xy=3))
            capture(b"\x01|2T80|2T90\r\n" + query(b"$CUR(TW)$", at_cursor=True), b"9")
            capture(b"\x01|2TA0\r\n" + query(b"$CUR(TW)$:$INUSE(TW,10)$", at_cursor=True), b"9:0")
            # Release each large plane by a real definition replacement.
            for number in (9, 8, 1):
                capture(query(f"$CUR(TW,{number})$".encode(), at_cursor=True)
                        + b"\x01|n2|w0000030312\r\n")
        t.case(f"rip3-window-all-slots-{width}x{height}")
        for number in range(3, 36):
            variables(f"$CUR(TW,{number})$$CUR(TW)$".encode(), str(number).encode())
        variables(b"$INUSE(TW,ALL)$:$ISPROT(TW,35)$", b"1:0")
        variables(b"$PROT(TW,0)$$CUR(TW)$", b"$PROT(TW,0)$35")
        variables(b"$CUR(TW,0)$$PROT(TW,CUR)$$ISPROT(TW,CUR)$$CUR(TW,35)$", b"0")
        frame = capture(query(b"$BASEMATH(CUR,64)$", at_cursor=True)
                        + b"\x01|2T?0|2Ta0|2TZ0\r\n"
                        + query(b"$CUR(TW)$$BASEMATH(CUR,36)$", at_cursor=True), b"35")
        frame = capture(scene(width, height) + window((20, 20, 100, 100), 3, 3, 0, 49)
                        + query(b"$ISPROT(TW,35)$", at_cursor=True), b"1")
        expect_region(frame, width, height, (20, 20, 80, 80), 0xff0000)
        frame = capture(scene(width, height) + query(b"$CUR(TW,0)$$ETW(35)$", at_cursor=True))
        expect_region(frame, width, height, (20, 20, 80, 80), 0xff0000)
        frame = capture(scene(width, height) + query(b"$ETW(ALL)$", at_cursor=True))
        expect_region(frame, width, height, (0, 0, width//80*80, height//43*42), 0xff0000)
        assert (frame.application["cterm.x"], frame.application["cterm.y"]) == (1, 1)
        frame = variables(b"$CUR(TW,35)$$ISPROT(TW,CUR)$", b"1")
        assert (frame.application["cterm.x"], frame.application["cterm.y"]) == (1, 1)
        assert frame.application["connected"] == 1
        assert (frame.metadata["plane_width"], frame.metadata["plane_height"],
                frame.metadata["columns"], frame.metadata["rows"]) == (width, height, width//8, height//8)

        t.case(f"rip3-window-repeated-text-over-graphics-{width}x{height}")
        variables(b"$CUR(TW,0)$$CUR(TW)$", b"0")
        capture(b"\x01|w0000050200\r\n\x1b[48;2;18;52;86m \x1b[1;4H ")
        # Replace the entire raster, then explicitly repeat the two blanks.
        # Every pixel of the two intervening cells must retain the green art.
        frame = capture(scene(width, height) + b"\x1b[H \x1b[1;4H ")
        cw, ch = {640: (8, 11), 800: (10, 13), 1024: (12, 17)}[width]
        for plane in (frame.primary, frame.alternate):
            for y in range(ch):
                offset = ((frame.metadata["pixel_top"] + y) % height) * width
                for x in range(4 * cw):
                    word = plane[offset + x]
                    rgb = word if word & 0x80000000 else frame.palette[word]
                    expected = 0x123456 if x // cw in (0, 3) else 0x00aa00
                    assert rgb & 0xffffff == expected, (x, y, hex(rgb), hex(expected))
        first_use_transition(t, capture, variables, width)


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
