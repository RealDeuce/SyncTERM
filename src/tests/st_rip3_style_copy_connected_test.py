#!/usr/bin/env python3
"""Live STYLE/BUT copy preflight and consumers over actual RIP3 TCP sessions.

Expectations use the documented table rules and independent geometry/colors;
no expected rasters are produced by the emulator under test.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path
from st_migrated_control_test import connection, square_canvas
from st_terminal_conformance_test import Terminal
from st_rip3_connected_test import query,pixel
from st_rip3_buttons_connected_test import style,button
from st_rip3_scroll_connected_test import plane_pixel


def run(args,width,height):
    with connection(args.executable,args.reports,encoding=args.encoding,
                    options='ScreenMode=Custom\nRIP=RIPv3\n',
                    settings=square_canvas(width, height)) as (client,root,peer,send):
        t=Terminal(client,root,peer,send)
        def values(source,expected=b''):
            return t.exchange(query(source,at_cursor=True),expected)
        def wire(source):
            return t.exchange(b'\x01|'+source+b'\r\n')
        def independent_state(frame):
            assert (frame.metadata['plane_width'],frame.metadata['plane_height'])==(width,height)
            assert (frame.application['cterm.x'],frame.application['cterm.y'],frame.application['cterm.attr'])==(4,2,0x16)
            values(b'$CUR(ENV)$:$CUR(PAL)$:$CUR(PORT)$:$CUR(TW)$:$TEXTXY$',b'5:17:4:3:50:51')

        t.exchange(b'\x1b[!',b'RIPSCRIP030001')
        wire(b'*')
        values(b'$CUR(ENV,5)$'+f'$WORLD(CUR,{width},{height})$'.encode()+
               b'$CUR(PAL,17)$$CUR(PORT,4)$$CUR(TW,3)$')
        accented=b'\x82' if args.encoding=='CP437' else 'é'.encode()
        t.exchange(b'\x1b[?25l\x1b[33;44m\x1b[2;3H'+accented)

        t.case(f'rip3-style-copy-active-consumer-{width}x{height}')
        values(b'$CUR(STYLE,5)$')
        wire(b'c04|k01|S0103|W0')
        values(b'$PROT(STYLE,5)$$CUR(STYLE,6)$')
        wire(b'c02|X0A0A|m1E1F')
        before=client.capture();after=values(b'$COPY(STYLE,5,CUR)$')
        assert after.primary==before.primary and after.alternate==before.alternate
        independent_state(after)
        values(b'$CUR(STYLE)$:$ISPROT(STYLE,6)$:$CUR(BUT)$',b'6:1:0')
        frame=wire(b'X0B0A|K0K0K0O0O')
        assert pixel(frame,10,10)==0x00aa00 and pixel(frame,11,10)==0xaa0000
        assert pixel(frame,20,20)==0xaa0000 and pixel(frame,21,21)==0x00aaaa
        t.case('rip3-style-copy-full-preflight-and-zero')
        for source, rejected in (
            (b'$COPY(STYLE,5,7,6,8)$', False),
            (b'$COPY(STYLE,5,7,5,8)$', True),
            (b'$COPY(STYLE,5,7,36,8)$', True),
            (b'$COPY(STYLE,34,7)$', False),
        ):
            values(source, source if rejected else b'');values(b'$INUSE(STYLE,7)$$INUSE(STYLE,8)$$INUSE(STYLE,34)$',b'000')
        values(b'$COPY(STYLE,CUR,0,35,35)$')
        values(b'$CUR(STYLE)$:$ISPROT(STYLE,35)$',b'6:1')
        values(b'$CUR(STYLE,0)$$ISPROT(STYLE,CUR)$',b'0')
        frame=wire(b'X0C0A')
        assert pixel(frame,12,10)==0xaa0000
        independent_state(frame)

        t.case(f'rip3-but-copy-button-consumer-and-protection-{width}x{height}')
        values(b'$CUR(BUT,5)$')
        t.exchange(style(flags=256|1024,width=23,height=13,surface=3,bevel=0))
        values(b'$PROT(BUT,5)$$CUR(BUT,6)$')
        t.exchange(style(width=7,height=5,surface=2,bevel=0))
        before=client.capture();after=values(b'$COPY(BUT,5,CUR)$')
        assert after.primary==before.primary and after.alternate==before.alternate
        independent_state(after)
        values(b'$CUR(BUT)$:$ISPROT(BUT,5)$:$ISPROT(BUT,6)$:$CUR(STYLE)$',b'6:1:0:0')
        frame=t.exchange(button((100,120,0,0),b'ID=7:<><>copy:$FIELDID$',hotkey=ord('a')))
        assert pixel(frame,100,120)==0x00aaaa and pixel(frame,122,132)==0x00aaaa
        assert pixel(frame,123,120)==0
        client.text('a');t.input(b'copy:7')
        # No UNPROT: a destination copied from a protected BUT is editable.
        frame=t.exchange(style(width=17,height=9,surface=4,bevel=0)+button((200,120,0,0)))
        assert pixel(frame,200,120)==0xaa0000 and pixel(frame,216,128)==0xaa0000
        assert pixel(frame,217,120)==0 and pixel(frame,100,120)==0x00aaaa
        for source, rejected in (
            (b'$COPY(BUT,6,7,5,8)$', False),
            (b'$COPY(BUT,6,7,6,8)$', True),
            (b'$COPY(BUT,5,7,36,8)$', True),
            (b'$COPY(BUT,34,7)$', False),
        ):
            values(source, source if rejected else b'');values(b'$INUSE(BUT,7)$$INUSE(BUT,8)$$INUSE(BUT,34)$',b'000')
        values(b'$COPY(BUT,5,0,35,35)$')
        values(b'$CUR(BUT)$:$ISPROT(BUT,35)$',b'6:0')
        values(b'$CUR(BUT,0)$$ISPROT(BUT,CUR)$',b'0')
        frame=t.exchange(button((300,120,0,0)))
        assert pixel(frame,322,132)==0x00aaaa and pixel(frame,323,120)==0
        # Numeric reset leaves an undefined current entry. CUR as a COPY
        # source must reject it without invoking first-use defaulting.
        values(b'$CUR(BUT,9)$$RESET(BUT,9)$$COPY(BUT,CUR,10)$')
        values(b'$CUR(BUT)$:$INUSE(BUT,9)$$INUSE(BUT,10)$',b'9:00')
        independent_state(client.capture())
        values(b'$COPY(STYLE,5)$', b'$COPY(STYLE,5)$');values(b'$COPY(BUT,5,CUR,36)$', b'$COPY(BUT,5,CUR,36)$')

        t.case(f'rip3-style-reset-live-record-and-defined-flag-{width}x{height}')
        values(b'$CUR(STYLE,9)$')
        wire(b'c02')
        values(b'$RESET(STYLE,5,9)$$CUR(STYLE)$:$INUSE(STYLE,9)$:$ISPROT(STYLE,5)$', b'9:0:1')
        frame = wire(b'X1414|c02|X1514')
        assert pixel(frame, 40, 40) == 0xffffff and pixel(frame, 41, 40) == 0x00aa00
        values(b'$INUSE(STYLE,9)$', b'0')
        values(b'$RESET(STYLE,CUR)$$CUR(STYLE)$:$INUSE(STYLE,9)$', b'9:1')
        frame = wire(b'X1614')
        assert pixel(frame, 42, 40) == 0xffffff
        independent_state(frame)
        t.case('rip3-style-patterns-through-selection-reset-and-restore')
        # Same DEVICE coordinates for every sample. A nonblack background
        # distinguishes opaque line gaps from pixels left by a previous draw.
        stamp = b'B4G4G4N4N|L4G4O4V4O'  # 8x8 at (160,160), then a 16-pixel line
        def masks(frame, rows, ink, pen, stroke, background):
            for phase in ('primary', 'alternate'):
                for y, row in enumerate(rows):
                    actual = [plane_pixel(frame, 160 + x, 160 + y, phase) for x in range(8)]
                    expected = [ink if row & (128 >> x) else background for x in range(8)]
                    assert actual == expected, (phase, y, actual, expected)
                actual = [plane_pixel(frame, 160 + x, 168, phase) for x in range(16)]
                expected = [stroke if pen & (0x8000 >> x) else background for x in range(16)]
                assert actual == expected, (phase, actual, expected)

        values(b'$CUR(STYLE,12)$')
        masks(wire(stamp), [255] * 8, 0xffffff, 0xffff, 0xffffff, 0)
        # Each custom row has one foreground pixel. The two ink colors differ.
        custom = [128, 64, 32, 16, 8, 4, 2, 1]
        wire(b'k01|c04|s3K1S0W0G0804020102|=140XPM01')
        values(b'$SGS(2)$$COPY(STYLE,12,13)$')
        masks(wire(stamp), custom, 0x00aa00, 0xaaaa, 0xaa0000, 0x0000aa)
        # For built-in lines the custom mask field is ignored.
        masks(wire(b'S0203|=110XPM01|' + stamp), [255,255,0,0] * 2,
              0x00aaaa, 0x3333, 0xaa0000, 0x0000aa)
        values(b'$RESET(STYLE,CUR)$')
        masks(wire(stamp), [255] * 8, 0xffffff, 0xffff, 0xffffff, 0)
        values(b'$CUR(STYLE,13)$')
        masks(wire(stamp), custom, 0x00aa00, 0xaaaa, 0xaa0000, 0x0000aa)
        values(b'$CUR(STYLE,12)$$RGS(2)$')
        masks(wire(stamp), custom, 0x00aa00, 0xaaaa, 0xaa0000, 0x0000aa)
        values(b'alive',b'alive')
        assert client.capture().application['connected']==1


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path)
    parser.add_argument('encoding',choices=('CP437','UTF-8'))
    parser.add_argument('--reports',type=Path,required=True)
    args=parser.parse_args()
    for width,height in ((640,480),(800,600),(1024,768),(720,400)):
        run(args,width,height)
