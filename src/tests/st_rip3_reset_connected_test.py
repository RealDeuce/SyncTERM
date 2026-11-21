#!/usr/bin/env python3
"""Independent outer SOFT reset vectors through a live TCP/bitmap session."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path
import time

from st_migrated_control_test import connection
from st_rip3_connected_test import pixel, query
from st_rip3_text_erase_connected_test import number, scene
from st_terminal_conformance_test import Terminal
from syncterm_fixture import receive


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
            fence = f"@RESET{serial}@".encode()
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

        def values(text, expected=b""):
            return capture(query(text, at_cursor=True), expected)

        def black(frame):
            assert all((p & 0xffffff if p & 0x80000000 else frame.palette[p]) == 0
                       for p in frame.primary)

        send(b"\x1b[!")
        receive(peer, b"RIPSCRIP030001", root / "replies.bin")
        t.case(f"rip3-reset-protected-current-{width}x{height}")
        capture(scene(width, height) + b"\x01|w0000270O12\r\n"
                + b"\x1b[3g\x1b[1;6H\x1bH\x01|2T50|w00000J0900\r\n"
                + b"\x1b[2;8r\x1b[?6h\x1b[?25l\x1b[3;4H\x1b[s\x1b[32;44m")
        values(b"$PROT(TW,5)$")
        capture(b"\x01|2T60\r\n\x1b[4;5H\x1b[s\x1b[31;45m\x1b[?25h")
        values(b"$PROT(TW,6)$$CUR(TW,7)$$CUR(TW,5)$")
        # A protected clipboard and offscreen ports survive; an unprotected
        # live port and window must disappear. Their backups remain independent.
        far = number(width, 2) + number(height, 2)
        capture(b"\x01|v0000" + far + b"|1C00000303\r\n")
        values(b"$PROT(PORT,1)$$CUR(PAL,5)$")
        capture(b"\x01|a0010|2P500000G0G1|2s500|S0102|B00000F0F"
                b"|2P600000G0G1|2s600|S0101|B00000F0F|2P700000G0G1\r\n")
        values(b"$IMGSTYLE(5,1,1,5,5,ASPECT)$$IMGSTYLE(6,1,1,5,5,ASPECT)$"
               b"$PROT(PORT,5)$$PROT(PORT,6)$$CUR(PORT,5)$$PROT(PAL,5)$"
               b"$-=KEPT=value$")
        values(b"[5:]held")
        capture(b"\x01|2Y60|c03|2Y50|k01|W1|S000F|N0\r\n")
        values(b"$PROT(STYLE,5)$$COPY(PAL,TBL,BASE,S0,PUSH)$"
               b"$COPY(PORT,TBL,BASE)$$COPY(STYLE,TBL,BASE)$$COPY(ENV,TBL,BASE)$"
               b"$WORLD(CUR,0,0)$$BASEMATH(CUR,64)$$COORDSIZE(CUR,5)$$COLORMODE(CUR,RGB,8)$")
        # Pending ordinary ANSI syntax survives SOFT and completes in TW0.
        frame = capture(b"\x1b[3\x01|*\r\n")
        black(frame)
        assert (frame.application["cterm.x"], frame.application["cterm.y"], frame.application["cterm.attr"]) == (1, 1, 7)
        assert (frame.application["cterm.top_margin"], frame.application["cterm.bottom_margin"]) == (1, 43)
        assert frame.metadata["pixel_cursor_visible"]
        values(b"$CUR(TW)$:$CUR(STYLE)$:$CUR(PORT)$:$CUR(ENV)$:$CUR(PAL)$:$WORLD$:$BASEMATH$:$COORDSIZE$:$COLORMODE$:$TEXTXY$",
               b"0:0:0:0:5:640:350:36:2:0:0:0")
        values(b"$INUSE(TW,5)$$INUSE(TW,6)$$INUSE(TW,7)$$INUSE(STYLE,5)$$INUSE(STYLE,6)$"
               b"$INUSE(PORT,1)$$INUSE(PORT,5)$$INUSE(PORT,6)$$INUSE(PORT,7)$$INUSE(PAL,5)$", b"1101011101")
        values(b"$&KEPT$:$INUSE(PAL,BASE)$$INUSE(PAL,S0)$$INUSE(PAL,STACK)$$INUSE(PORT,BASE)$"
               b"$INUSE(STYLE,BASE)$$INUSE(ENV,BASE)$", b"value:111111")
        values(b"[5]", b"held")
        frame = capture(b"1m\t")
        assert frame.application["cterm.attr"] == 4 and frame.application["cterm.x"] == 6
        frame = values(b"$CUR(TW,5)$")
        assert (frame.application["cterm.x"], frame.application["cterm.y"], frame.application["cterm.attr"]) == (1, 1, 7)
        assert frame.application["cterm.ext_attr.origin_mode"] == 1
        assert frame.application["cterm.ext_attr.auto_wrap"] == 0 and not frame.metadata["pixel_cursor_visible"]
        frame = capture(b"\t")
        assert frame.application["cterm.x"] == 9
        frame = values(b"$CUR(TW,6)$")
        assert (frame.application["cterm.x"], frame.application["cterm.y"], frame.application["cterm.attr"]) == (5, 5, 0x54)
        assert (frame.application["cterm.top_margin"], frame.application["cterm.bottom_margin"]) == (2, 8)
        assert frame.metadata["pixel_cursor_visible"]
        values(b"$CUR(TW,0)$" + f"$WORLD(CUR,{width},{height})$".encode())
        values(b"$IMGSTYLE(5)$:$IMGSTYLE(6)$", b"-1:-1:-1:-1:0:1:1:6:6:1")
        frame = capture(b"\x01|X0101|X0101|B0A0A0C0C|c00|X0202|1P2S2S0\r\n")
        assert pixel(frame, 1, 1) == 0xffffff and pixel(frame, 2, 2) == 0xff0000
        assert all(pixel(frame, x, y) == 0xffffff for y in range(10, 13) for x in range(10, 13))
        assert pixel(frame, 100, 100) == 0x00aa00  # protected clipboard pixels
        assert pixel(frame, width-1, height-1) == 0

        t.case(f"rip3-reset-protected-inactive-{width}x{height}")
        values(b"$CUR(TW,5)$$TWGOTO(CUR,3,2)$$UNPROT(TW,5)$$DTW(5)$$PROT(TW,5)$")
        black(capture(b"\x01|*\r\n"))
        frame = values(b"$CUR(TW,5)$$TWIN$", b"NO")
        assert (frame.application["cterm.x"], frame.application["cterm.y"], frame.application["cterm.attr"]) == (4, 3, 7)
        assert not frame.metadata["pixel_cursor_visible"]
        values(b"$CUR(TW,0)$$UNPROT(PORT,1)$$UNPROT(PAL,5)$")
        t.case(f"rip3-reset-macro-and-clipboard-deletion-{width}x{height}")
        frame = values(b"$RESET(SOFT)$$CUR(PAL)$:$INUSE(PAL,5)$alive", b"5:0alive")
        black(frame)
        # Defining the old clipboard number must not recreate its assignment.
        frame = capture(b"\x01|2P100000G0G1|2s100|S0101|B00000505|2s000|1P2S2S0\r\n")
        black(frame)
        # A new palette upload must not recolor the literal reset clear.
        values(b"$CUR(PAL,0)$")
        frame = capture(b"\x01|a0010|*bad\r\n" + query(b"alive", at_cursor=True), b"alive")
        black(frame)
        assert frame.application["connected"] == 1
        assert (frame.metadata["plane_width"], frame.metadata["plane_height"],
                frame.metadata["columns"], frame.metadata["rows"]) == (width, height, width//8, height//8)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("encoding", choices=("CP437", "UTF-8"))
    parser.add_argument("--reports", type=Path, required=True)
    args = parser.parse_args()
    for width, height in ((640, 480), (800, 600), (1024, 768), (720, 400)):
        run(args, width, height)


if __name__ == "__main__":
    main()
