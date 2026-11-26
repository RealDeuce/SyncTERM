#!/usr/bin/env python3
"""Live TW entry COPY through TCP: independent native definitions, no pixels.

Expected state, geometry and colors follow the contract; native definition
copies preserve the connection interpreter while owning independent metadata.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path
import time
from st_migrated_control_test import connection, square_canvas
from st_terminal_conformance_test import Terminal
from st_rip3_connected_test import query,pixel
from st_rip3_extended_window_connected_test import window,xy
from st_rip3_hover_connected_test import resident


def run(args,width,height):
    with connection(args.executable,args.reports,encoding=args.encoding,
                    options='ScreenMode=Custom\nRIP=RIPv3\n',
                    settings=square_canvas(width, height)) as (client,root,peer,send):
        t=Terminal(client,root,peer,send)
        serial=0
        def capture(data=b'',expected=b''):
            nonlocal serial
            serial+=1;fence=f'@TWC{serial}@'.encode()
            send(data+query(fence,at_cursor=True))
            received=bytearray();deadline=time.monotonic()+10
            while not received.endswith(fence):
                peer.settimeout(max(0.001,deadline-time.monotonic()))
                part=peer.recv(4096);assert part,'connection closed';received.extend(part)
                assert len(received)<65536
            with (root/'replies.bin').open('ab') as stream:stream.write(received)
            assert received==expected+fence,(bytes(received),expected+fence)
            return client.capture()
        def values(source,expected=b''):
            return capture(query(source,at_cursor=True),expected)
        def same(before,after):
            assert before.primary==after.primary and before.alternate==after.alternate
        def ordinary(frame):
            assert xy(frame)==(4,4) and frame.application['cterm.attr']==0x12
            assert (frame.application['cterm.top_margin'],frame.application['cterm.bottom_margin'])==(2,7)
            assert frame.application['cterm.right_margin']==20
            assert frame.application['cterm.ext_attr.origin_mode']==1 and frame.application['cterm.ext_attr.auto_wrap']==0
            assert not frame.metadata['pixel_cursor_visible']
            assert (frame.metadata['pixel_cursor_width'],frame.metadata['pixel_cursor_height'])==(width//80,height//43)
        capture(b'\x1b[!',b'RIPSCRIP030001')

        t.case(f'rip3-tw-entry-copy-entry-console-{width}x{height}')
        before=capture(b'\x1b[?25lF');frame=values(b'$COPY(TW,0,3)$$CUR(TW,3)$');same(before,frame)
        assert xy(frame)==(2,1) and frame.metadata['pixel_cursor_width']==width//80
        values(b'$TWW$:$TWH$:$TWFONT$',b'80:42:1')
        capture(b'F')
        frame=capture()
        assert all(pixel(frame,x,y)==pixel(frame,x+width//80,y) for y in range(height//43) for x in range(width//80))
        before=capture();frame=values(b'$COPY(TW,3,5)$$CUR(TW)$',b'3');same(before,frame)
        frame=values(b'$CUR(TW,5)$');assert xy(frame)==(3,1) and frame.metadata['pixel_cursor_width']==width//80
        capture(b'\x01|*\r\n');values(f'$WORLD(CUR,{width},{height})$'.encode())

        t.case('rip3-tw-entry-copy-active-destination-and-protection')
        values(b'$CUR(TW,1)$')
        capture(b'\x01|w0A050T0C10\r\n'
                b'\x1b[3g\x1b[1;6H\x1bH\x1b[2;7r\x1b[?6h\x1b[3;4H\x1b[s\x1b[32;44m\x1b[38;2;0;170;0;48;2;0;0;170m\x1b[?7l\x1b[2 q\x1b[?25l')
        capture(resident(4,b'$-=EFFECT=bad$source',1)+resident(2,b'$-=EFFECT=bad$floating'))
        values(b'$-=EFFECT=cold$$PROT(TW,1)$$CUR(TW,2)$')
        capture(window((200,100,500,300),10,6,2,1)+b'\x1b[33;41m\x1b[4;2H\x1b[?25l')
        values(b'$CUR(ENV,5)$$WORLD(CUR,160,100)$$CUR(PAL,17)$$CUR(STYLE,7)$$CUR(BUT,6)$$CUR(PORT,4)$')
        before=capture();frame=values(b'$COPY(TW,1,CUR,0,35,35)$');same(before,frame);ordinary(frame)
        values(b'$CUR(TW)$:$TWW$:$TWH$:$TWFONT$:$ISPROT(TW,2)$:$ISPROT(TW,35)$',b'2:20:8:1:1:1')
        values(b'$CUR(ENV)$:$WORLD$:$CUR(PAL)$:$CUR(STYLE)$:$CUR(BUT)$:$CUR(PORT)$:$&EFFECT$',b'5:160:100:17:7:6:4:cold')
        t.case('rip3-tw-entry-copy-preflight-has-no-partial-definitions')
        for source, rejected in (
            (b'$COPY(TW,1,7,2,8)$', False),
            (b'$COPY(TW,1,7,1,8)$', True),
            (b'$COPY(TW,1,7,36,8)$', True),
            (b'$COPY(TW,1,7,BASE,8)$', True),
            (b'$COPY(TW,34,7)$', False),
        ):
            before=capture();frame=values(source, source if rejected else b'');same(before,frame);ordinary(frame)
            values(b'$INUSE(TW,7)$$INUSE(TW,8)$$INUSE(TW,34)$',b'000')
        t.case('rip3-tw-entry-copy-independent-cursors-and-native-font-output')
        accented=b'\x82' if args.encoding=='CP437' else 'é'.encode()
        x=80*width//640+3*(width//80);y=40*height//350+3*(height//43)
        frame=capture(b'F'+accented)
        colors={pixel(frame,xx,yy) for yy in range(y,y+height//43) for xx in range(x,x+width//80)}
        assert 0x00aa00 in colors and 0x0000aa in colors and xy(frame)==(6,4)
        values(b'$UNPROT(TW,2)$$CURXY(6,5)$')
        capture(b'\x1b[31m'+resident(4,b'changed',2))
        frame=values(b'$CUR(TW,35)$');ordinary(frame)
        frame=capture(b'\x1b[?6l\x1b[H\t');assert xy(frame)==(6,1)
        frame=capture(b'\x1b[?6h\x1b[u');assert xy(frame)==(4,4)
        frame=values(b'$CUR(TW,1)$$COPY(TW,CUR,5,7,9)$$COPY(TW,CUR,6,8)$');ordinary(frame)
        values(b'$CUR(TW)$:$ISPROT(TW,6)$:$ISPROT(TW,8)$',b'1:1:1')
        values(b'$ISPROT(TW,5)$$ISPROT(TW,7)$$ISPROT(TW,9)$',b'111')
        frame=values(b'$CUR(TW,0)$$ISPROT(TW,CUR)$',b'0');ordinary(frame)

        t.case('rip3-tw-entry-copy-inactive-state-and-live-ansi-continuation')
        values(b'$UNPROT(TW,1)$$DTW(1)$$PROT(TW,1)$$CUR(TW,2)$')
        before=capture();frame=values(b'$COPY(TW,1,CUR)$');same(before,frame);ordinary(frame)
        values(b'$CUR(TW)$:$TWIN$',b'2:NO')
        before=capture();frame=capture(b'ignored');same(before,frame);assert xy(frame)==(4,4)
        values(b'$UNPROT(TW,2)$$ATW(2)$$CUR(TW,0)$')
        # Copying into current keeps this incomplete connection CSI, rather
        # than adopting old parser state from the noncurrent source window.
        frame=capture(b'\x1b[3'+query(b'$COPY(TW,1,CUR)$',at_cursor=True))
        values(b'$TWIN$',b'NO')
        values(b'$ATW$')
        frame=capture(b'1mF');assert frame.application['cterm.attr']==0x14 and xy(frame)==(5,4)

        t.case('rip3-tw-entry-copy-extended-kind-and-retained-layout')
        values(f'$WORLD(CUR,{width},{height})$'.encode()+b'$UNPROT(TW,35)$$CUR(TW,35)$')
        capture(window((200,100,500,300),10,6,2,1)+b'\x1b[33;41m\x1b[4;2H\x1b[2 q\x1b[?25l')
        values(b'$PROT(TW,35)$$CUR(TW,0)$')
        before=capture();frame=values(b'$COPY(TW,35,CUR)$');same(before,frame)
        values(b'$CUR(TW)$:$ISEXTWIN$:$TWW$:$TWH$:$TWFONT$:$ISPROT(TW,CUR)$',b'0:1:10:6:3:0')
        assert xy(frame)==(2,4) and frame.application['cterm.attr']==0x46
        cell={640: (8,19), 720: (9,16), 800: (10,24), 1024: (12,30)}[width]
        assert (frame.metadata['pixel_cursor_width'],frame.metadata['pixel_cursor_height'])==cell
        values(b'$&EFFECT$',b'cold')
        assert frame.application['connected']==1
        assert (frame.metadata['plane_width'],frame.metadata['plane_height'])==(width,height)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path)
    parser.add_argument('encoding',choices=('CP437','UTF-8'))
    parser.add_argument('--reports',type=Path,required=True)
    args=parser.parse_args()
    for width,height in ((640,480),(800,600),(1024,768),(720,400)):
        run(args,width,height)
