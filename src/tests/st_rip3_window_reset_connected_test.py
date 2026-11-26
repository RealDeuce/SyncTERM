#!/usr/bin/env python3
"""Live TW RESET: independent defaults/selection/parser and unchanged raster."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path
import time
from st_migrated_control_test import connection
from st_terminal_conformance_test import Terminal
from st_rip3_connected_test import query,pixel
from st_rip3_extended_window_connected_test import window,xy
from st_rip3_hover_connected_test import resident


def run(args,width,height):
    with connection(args.executable,args.reports,encoding=args.encoding,
                    options='ScreenMode=Custom\nRIP=RIPv3\n',
                    settings=f'CustomColumns={width//8}\nCustomRows={height//8}\nCustomFontHeight=8\n') as (client,root,peer,send):
        t=Terminal(client,root,peer,send)
        serial=0
        def capture(data=b'',expected=b''):
            nonlocal serial
            serial+=1;fence=f'@TWR{serial}@'.encode()
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
        def fresh(frame):
            assert xy(frame)==(1,1) and frame.application['cterm.attr']==7
            assert (frame.application['cterm.top_margin'],frame.application['cterm.bottom_margin'])==(1,43)
            assert frame.application['cterm.right_margin']==80
            assert frame.application['cterm.ext_attr.origin_mode']==0 and frame.application['cterm.ext_attr.auto_wrap']==1
            assert frame.metadata['pixel_cursor_visible']
            assert (frame.metadata['plane_width'],frame.metadata['plane_height'])==(width,height)
            assert (frame.metadata['pixel_cursor_width'],frame.metadata['pixel_cursor_height'])==(width//80,1)
            values(b'$CUR(TW)$:$ISEXTWIN$:$TWW$:$TWH$:$TWFONT$:$TWIN$',b'0:0:80:43:1:YES')
        capture(b'\x1b[!',b'RIPSCRIP030001')
        capture(b'\x01|*\r\n');values(f'$WORLD(CUR,{width},{height})$'.encode())
        t.case(f'rip3-live-tw-reset-forced-fresh-zero-no-paint-{width}x{height}')
        # An extended inactive zero cannot use w identity, MTW or SOFT.
        capture(window((50,50,400,300),12,7,2,1)
                +b'\x1b[32;44m\x1b[38;2;17;34;51;48;2;65;43;21m\x1b[2;6r\x1b[?6h\x1b[3;4H\x1b[s\x1bH\x1b[?7l\x1b[2 q\x1b[?25l')
        values(b'$DTW$')
        capture(resident(4,b'$-=EFFECT=bad$Q0',0)+resident(2,b'$-=EFFECT=bad$F'))
        values(b'$-=EFFECT=cold$$CUR(ENV,5)$$CUR(PAL,17)$$CUR(STYLE,7)$$CUR(BUT,6)$$CUR(PORT,4)$')
        before=capture();frame=values(b'$RESET(TW,0)$');same(before,frame);fresh(frame)
        values(b'$CUR(ENV)$:$CUR(PAL)$:$CUR(STYLE)$:$CUR(BUT)$:$CUR(PORT)$:$&EFFECT$',b'5:17:7:6:4:cold')
        frame=capture(b'\x1b[u');assert xy(frame)==(1,1)
        frame=capture(b'\t');assert xy(frame)==(1,2)  # empty explicit tab map
        t.case('rip3-live-tw-reset-identical-geometry-still-forces-defaults')
        capture(b'\x1b[3;4H\x1b[s\x1b[1;4;5;7;31;44m\x1b[?25l')
        before=capture();frame=values(b'$RESET(TW)$');same(before,frame);fresh(frame)
        accented=b'\x82' if args.encoding=='CP437' else 'é'.encode()
        frame=capture(b'F'+accented);assert xy(frame)==(3,1)
        colors={pixel(frame,x,y) for y in range(height//43) for x in range(width//80)}
        assert frame.application['cterm.fg_color']==23 and frame.application['cterm.bg_color']==16
        assert 0xa8a8a8 in colors and 0 in colors

        t.case('rip3-live-tw-reset-noncurrent-zero-and-protected-current-table')
        values(b'$CUR(TW,3)$$CUR(TW,5)$')
        capture(window((200,100,500,300),10,6,2,1)+b'\x1b[33;41m\x1b[4;3H\x1b[?25l')
        capture(resident(4,b'$-=EFFECT=bad$Q5',5))
        values(b'$DTW$$PROT(TW,5)$$STW$$STW(3)$$PROT(TW,S3)$')
        before=capture();frame=values(b'$RESET(TW,0)$');same(before,frame)
        assert xy(frame)==(3,4) and frame.application['cterm.attr']==0x46
        values(b'$CUR(TW)$:$TWIN$:$ISPROT(TW,5)$',b'5:NO:1')
        before=capture();frame=values(b'$RESET(TW,TBL)$');same(before,frame)
        values(b'$CUR(TW)$:$TWIN$:$INUSE(TW,3)$:$INUSE(TW,BASE)$',b'5:NO:0:1')
        before=capture();frame=values(b'$RESET(TW,5,CUR,ALL)$');same(before,frame)
        values(b'$CUR(TW)$:$TWIN$:$INUSE(TW,BASE)$:$INUSE(TW,S3)$:$&EFFECT$',b'5:NO:0:1:cold')
        assert xy(frame)==(3,4) and frame.application['cterm.attr']==0x46

        t.case('rip3-live-tw-reset-delete-current-selects-existing-inactive-zero')
        values(b'$CUR(TW,0)$')
        capture(window((30,40,400,250),8,5,2,1)+b'\x1b[34;42m\x1b[3;2H\x1b[?25l')
        values(b'$DTW$$CUR(TW,5)$$UNPROT(TW,5)$')
        before=capture();frame=values(b'$RESET(TW,5)$');same(before,frame)
        values(b'$CUR(TW)$:$TWIN$:$INUSE(TW,5)$',b'0:NO:0')
        assert (frame.application['cterm.right_margin'],frame.application['cterm.bottom_margin'])==(8,5)
        values(b'$ATW$$ISEXTWIN$:$TWW$:$TWH$$DTW$',b'1:8:5')
        assert xy(frame)==(2,3) and frame.application['cterm.attr']==0x21 and not frame.metadata['pixel_cursor_visible']
        before=capture();frame=capture(b'ignored');same(before,frame);assert xy(frame)==(2,3)
        t.case('rip3-live-tw-reset-cursor-selectors-resolve-left-to-right')
        values(b'$CUR(TW,5)$')
        before=capture();frame=values(b'$RESET(TW,5,CUR)$');same(before,frame);fresh(frame)
        t.case('rip3-live-tw-reset-late-invalid-selector-keeps-earlier-state')
        values(b'$CUR(TW,5)$$STW$')
        before=capture();frame=values(b'$RESET(TW,BASE,5,BOGUS)$', b'$RESET(TW,BASE,5,BOGUS)$');same(before,frame)
        values(b'$CUR(TW)$:$INUSE(TW,5)$:$INUSE(TW,BASE)$',b'5:1:1')
        t.case('rip3-live-tw-reset-retains-connection-partial-ansi')
        values(b'$CUR(TW,0)$')
        capture(b'\x1b[3'+query(b'$RESET(TW,0)$',at_cursor=True))
        frame=capture(b'1mF');assert frame.application['cterm.attr']==4 and xy(frame)==(2,1)
        values(b'after-live-reset',b'after-live-reset')
        assert frame.application['connected']==1


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path)
    parser.add_argument('encoding',choices=('CP437','UTF-8'))
    parser.add_argument('--reports',type=Path,required=True)
    args=parser.parse_args()
    for width,height in ((640,480),(800,600),(1024,768),(720,400)):
        run(args,width,height)
