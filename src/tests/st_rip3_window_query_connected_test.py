#!/usr/bin/env python3
"""Literal connected cursor/font/geometry replies, independently of pixels."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path
import time

from st_migrated_control_test import connection
from st_rip3_connected_test import query
from st_rip3_extended_window_connected_test import window
from st_rip3_text_erase_connected_test import scene
from st_terminal_conformance_test import Terminal
from syncterm_fixture import receive


def run(args, width, height, ox, oy, cw, ch, bcw, bch, bx, by):
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
            fence = f"@GEO{serial}@".encode()
            send(data + query(fence, at_cursor=True))
            received = bytearray()
            deadline = time.monotonic() + 10
            while not received.endswith(fence):
                peer.settimeout(max(0.001, deadline-time.monotonic()))
                part = peer.recv(65536)
                assert part, "connection closed"
                received.extend(part)
                assert len(received) < 65536
                with (root / "replies.bin").open("ab") as stream:
                    stream.write(part)
            assert received == expected+fence, (bytes(received), expected+fence)
            return client.capture()

        def values(source, expected):
            return capture(query(source, at_cursor=True), expected)

        send(b"\x1b[!")
        receive(peer, b"RIPSCRIP030001", root / "replies.bin")
        t.case(f"rip3-window-query-root-{width}x{height}")
        before = capture(scene(width, height))
        frame = values(b"$CURX$:$CURY$:$CURSOR$:$TWW$:$TWH$",
                       f"1:1:YES:{width//8}:{height//8}".encode())
        assert frame.primary == before.primary
        # No invented MicroANSI font is reported for an unredefined client TTY.
        values(b"$TWFONT$alive", b"alive")
        t.case(f"rip3-window-query-ordinary-and-noncurrent-{width}x{height}")
        capture(b"\x01|w0A050H0A10\r\n\x1b[2;5r\x1b[?6h\x01|g0403\r\n")
        values(b"$CURX$:$CURY$:$TWFONT$:$TWW$:$TWH$", b"5:4:1:8:6")
        values(b"$TWX0$:$TWY0$:$TWX1$:$TWY1$", b"10:5:17:10")
        values(b"$TWX0(CUR,BOUND,DEVICE)$:$TWY0(CUR,TEXT,DEVICE)$:$TWX1(CUR,TEXT,DEVICE)$:$TWY1(CUR,BOUND,DEVICE)$",
               f"{ox}:{oy}:{ox+8*cw}:{oy+6*ch}".encode())
        capture(b"\x01|2T10|g0101\r\n")
        before = values(b"$CUR(TW)$", b"1")
        frame = values(b"$CUR(TW)$:$CURX(0)$:$CURY(0)$:$CURX$:$CURY$", b"1:5:4:2:2")
        assert frame.primary == before.primary
        assert (frame.application["cterm.x"], frame.application["cterm.y"]) == (2, 2)
        values(b"$COFF(0)$$CURSOR(0)$:$CURSOR$", b"NO:YES")
        for font in range(5):
            capture(b"\x01|w000003031" + bytes((ord("0")+font,)) + b"\r\n")
            values(b"$TWFONT$:$TWW$:$TWH$", f"{font+1}:4:4".encode())

        t.case(f"rip3-window-query-extended-world-{width}x{height}")
        capture(window((10, 20, 610, 460), 20, 15, 35, 9))
        values(b"$TWFONT$:$TWW$:$TWH$:$ISEXTWIN$", b"5:20:15:1")
        values(b"$TWW(CUR,BOUND,DEVICE)$:$TWH(CUR,BOUND,DEVICE)$:$TWX1(CUR,BOUND,DEVICE)$:$TWY1(CUR,BOUND,DEVICE)$",
               b"600:440:610:460")
        values(b"$TWW(CUR,TEXT,DEVICE)$:$TWH(CUR,TEXT,DEVICE)$:$TWW(CUR,CELL,DEVICE)$:$TWH(CUR,CELL,DEVICE)$",
               f"{20*bcw}:{15*bch}:{bcw}:{bch}".encode())
        values(b"$TWX0(CUR,TEXT,DEVICE)$:$TWY0(CUR,TEXT,DEVICE)$:$TWX1(CUR,TEXT,DEVICE)$:$TWY1(CUR,TEXT,DEVICE)$",
               f"{bx}:{by}:{bx+20*bcw}:{by+15*bch}".encode())
        values(b"$TWX0$:$TWY0$:$TWX1$:$TWY1$", b"-1:-1:-1:-1")
        capture(b"\x01|gZZZZ\r\nF")
        before = values(b"$CURX$:$CURY$", b"20:15")
        frame = values(f"$WORLD(CUR,{2*width},{2*height})$$TWW(CUR,BOUND)$:$TWH(CUR,BOUND)$:$TWX0(CUR,TEXT)$:$TWY0(CUR,TEXT)$".encode(),
                       f"1200:880:{2*bx}:{2*by}".encode())
        assert frame.primary == before.primary and frame.application["cterm.last_column_flag.set"] == 1
        values(b"$TWX1(CUR,TEXT)$bad-default:$TWX1(CUR,TEXT,COLS)$bad-cell", b"bad-default:bad-cell")
        values(b"$TWH(CUR,TEXT,COLS)$bad-syntax", b"bad-syntax")
        values(b"$PROT(TW,1)$$CURX$:$CURY$:$CURSOR$", b"20:15:YES")
        values(b"$UNPROT(TW,1)$$DTW(1)$$CURSOR$:$CURX$:$CURY$:$TWFONT$:$TWW$:$TWH$:$TWX0$:$TWY1(CUR,BOUND,DEVICE)$",
               b"NO:0:0:0:0:0:-1:-1")
        frame = values(b"$ATW(1)$$WORLD(CUR,0,0)$$TWW(CUR,BOUND,DEVICE)$:$CURX$", b"600:20")
        assert frame.application["cterm.last_column_flag.set"] == 1
        values(b"$TWW(CUR,BOUND,WORLD)$alive", b"alive")
        values(b"$CURX(35)$:$CURSOR(35)$:$TWW(35)$:$TWX0(35)$:$INUSE(TW,35)$", b"0:NO:0:-1:0")
        if width == 640:
            t.case("rip3-window-query-wide-offscreen")
            capture(query(b"$WORLD(CUR,1,1)$", at_cursor=True) + b"\x01|n5\r\n"
                    + window((16777216, 0, 16777217, 1), 1, 1, 4, 1, xy=5))
            frame = values(b"$CURSOR$:$CURX$:$TWW$", b"YES:1:1")
            assert not frame.metadata["pixel_cursor_visible"]
            values(b"$TWX0(CUR,BOUND,DEVICE)$:$TWX1(CUR,BOUND,DEVICE)$:$TWX0(CUR,TEXT,DEVICE)$:$TWX1(CUR,TEXT,DEVICE)$",
                   b"10737418240:10737418880:10737418552:10737418568")
            values(b"$WORLD(CUR,65535,65535)$$TWX0(CUR,TEXT,WORLD)$:$TWX1(CUR,TEXT,WORLD)$",
                   b"1099494882508:1099494884146")
        frame = values(b"$CUR(TW)$", b"1")
        assert frame.application["connected"] == 1
        assert (frame.metadata["plane_width"], frame.metadata["plane_height"],
                frame.metadata["columns"], frame.metadata["rows"]) == (width, height, width//8, height//8)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("encoding", choices=("CP437", "UTF-8"))
    parser.add_argument("--reports", type=Path, required=True)
    args = parser.parse_args()
    for case in ((640, 480, 80, 54, 8, 11, 16, 19, 150, 97),
                 (800, 600, 100, 68, 10, 13, 20, 24, 110, 60),
                 (1024, 768, 128, 87, 12, 17, 20, 24, 110, 60),
                 (720, 400, 90, 45, 9, 9, 16, 19, 150, 97)):
        run(args, *case)


if __name__ == "__main__":
    main()
