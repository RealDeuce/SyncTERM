#!/usr/bin/env python3
"""Independent reset and palette realization vectors through a live TCP/bitmap session."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path
import time

from st_migrated_control_test import connection
from st_rip3_connected_test import pixel, query
from st_rip3_extended_window_connected_test import window, xy
from st_rip3_hover_connected_test import resident
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
        t.case('rip3-header-palette-uploads-on-the-indexed-renderer')
        values(f'$WORLD(CUR,{width},{height})$$CUR(PAL,5)$'.encode())
        before = capture(b'\x01|a0409|c04|X1414\r\n\x1b[1;1H\x1b[48;2;19;37;53m ')
        assert pixel(before, 40, 40) == 0x0000ff and pixel(before, 0, 0) == 0x132535
        values(b'$CUR(PAL,6)$')
        capture(b'\x01|a0402\r\n')
        values(b'$CUR(PAL,5)$')
        for command in (b'h010000014106', b'h010000014206', b'h010000014306'):
            before = capture()
            frame = capture(b'\x01|' + command + b'\r\n')
            assert frame.primary == before.primary and frame.alternate == before.alternate
            values(b'$CUR(PAL)$:$PALENTRY(5,4)$:$PALENTRY(6,4)$', b'5:6;0:0:63:6;0:42:0')
        values(b'$PROT(PAL,5)$')
        before = capture()
        frame = capture(b'\x01|h010000014306\r\n')
        assert frame.primary == before.primary and frame.alternate == before.alternate
        values(b'$ISPROT(PAL,5)$', b'1')
        # Selection in the same segment realizes the new palette before upload.
        frame = capture(b'\x01|h010000014326\r\n')
        assert pixel(frame, 40, 40) == 0x00aa00 and pixel(frame, 0, 0) == 0x132535
        values(b'$RESET(SOFT)$')
        capture(b'\x01|h010000014305\r\n')
        values(b'$CUR(PAL)$:$INUSE(PAL,6)$:$ISPROT(PAL,5)$', b'6:0:1')

        live_families = ('TW', 'BUT', 'STYLE', 'PORT', 'PAL', 'ENV')
        saved_families = (*live_families, 'MOUSE', 'SCREEN')
        current = ':'.join(f'$CUR({family})$' for family in live_families).encode()
        protected = ''.join(f'$CUR({family},5)$$PROT({family},5)$' for family in live_families)
        live_entries = ''.join(f'$INUSE({family},5)$' for family in live_families).encode()
        saves = ''.join(f'$INUSE({family},{place})$' for place in ('BASE', 'S2', 'STACK')
                        for family in saved_families).encode()
        protections = ''.join(f'$ISPROT({family},S2)$' for family in saved_families).encode()
        variables = b'$?3$:$&N$:$&P$:$&EFFECT$'
        preserved = b':MEM:DISK:cold'
        for name, operation, expected in (
                ('header', b'\x01|h01000100\r\n', b''),
                ('macro', query(b'$RESET(HARD)$after' + variables, at_cursor=True), b'after' + preserved)):
            t.case('rip3-hard-shared-profile-' + name)
            values(b'$CUR(PAL,0)$$WORLD(CUR,640,350)$')
            capture(scene(width, height) + window((30,40,400,250),8,5,2,1)
                    + b'\x1b[31;46m\x1b[2;3H\x1b[s\x1bH\x1b(A\x1b)0\x1b[?25lANSI'
                    + b'\x01|v0000' + far + b'\r\n')
            values(b'$DTW$$IMGSTYLE(0,1,1,5,5,ASPECT)$')
            capture(resident(4,b'$-=EFFECT=bad$fixed') + resident(2,b'$-=EFFECT=bad$floating')
                    + b'\x01|v00000000|2R0000$-=EFFECT=bad$refresh\r\n')
            values(b'[3:]OLD')
            values(b'$-+=P=DISK$$-+=N=SHADOW$$-=N=MEM$$-=EFFECT=cold$')
            values(protected.encode() + f'$WORLD(CUR,{width},{height})$'.encode())
            values(b'$UNPROT(TW,5)$$ATW(5)$$PROT(TW,5)$$TWIN$', b'YES')
            capture(b'\x01|1C0A0A0C0C\r\n')
            values(b'$OFFSCREEN(CLIP)$', b'1')
            values(b'$SAVEALL$$SAVEALL(2)$$SAVEALL(PUSH)$'
                   + ''.join(f'$PROT({family},S2)$' for family in saved_families).encode())
            values(current, b'5:5:5:5:5:5')
            values(saves + protections + live_entries + b'$MCURSOR(7)$', b'1' * 38)
            capture(b'\x1b[31;44m\x1b[3;5H\x1b[s\x1bH\x1b(A\x1b)0\x1b[?7l\x1b7')
            frame = capture(b'\x1b[31' + operation, expected)
            black(frame)
            assert frame.primary == frame.alternate
            assert xy(frame) == (1,1) and frame.application['cterm.attr'] == 7
            assert frame.application['cterm.right_margin'] == 80 and frame.application['cterm.bottom_margin'] == 43
            assert frame.application['cterm.ext_attr.auto_wrap'] == 1 and frame.metadata['pixel_cursor_visible']
            assert frame.application['cterm.rip3_g0'] == frame.application['cterm.rip3_g1'] == ord('B')
            assert frame.metadata['pointer_shape'] == 0
            values(current, b'0:0:0:0:0:0')
            values(live_entries + saves, b'0' * 30)
            values(b'$PALENTRY(0,4)$:$OFFSCREEN(CLIP)$:continued', b'6;42:0:0:-1:continued')
            values(b'$REFRESH$' + variables, preserved)
            values(b'$WORLD$:$BASEMATH$:$COORDSIZE$:$COLORMODE$:$TEXTXY$:$IMGSTYLE(0)$',
                   b'640:350:36:2:0:0:0:-1:-1:-1:-1:0')
            values(b'$ISEXTWIN$:$TWFONT$:$TWIN$:$PORTW(0,VIEW,DEVICE)$:$PORTH(0,VIEW,DEVICE)$',
                   f'0:1:YES:{width}:{height}'.encode())
            assert frame.application['connected'] == 1
            assert (frame.metadata['plane_width'], frame.metadata['plane_height']) == (width, height)
            black(capture(b'\x01|a0010\r\n'))
            frame = capture(b'mZ\x1b8')
            assert xy(frame) == (2,1) and frame.application['cterm.attr'] == 4
            frame = capture(b'\x1b[u\t')
            assert xy(frame) == (1,2)  # Fresh CSI save and empty explicit tabs.


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
