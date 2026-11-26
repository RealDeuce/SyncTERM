#!/usr/bin/env python3
"""Full TW definitions/ANSI state over TCP, without restoring displayed contents."""
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
            serial+=1;fence=f'@TWB{serial}@'.encode()
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
        def same(a,b):
            assert a.primary==b.primary and a.alternate==b.alternate
        capture(b'\x1b[!',b'RIPSCRIP030001')
        t.case(f'rip3-tw-backup-entry-font-and-raster-{width}x{height}')
        before=values(b'$STW(8)$')
        capture(b'\x01|*\r\n');frame=values(b'$RTW(8)$')
        assert (frame.metadata['plane_width'],frame.metadata['plane_height'])==(width,height)
        assert frame.metadata['pixel_cursor_width']==8
        assert (frame.application['cterm.right_margin'],frame.application['cterm.bottom_margin'])==(width//8,height//8)
        values(b'$INUSE(TW,S8)$',b'0')
        t.case('rip3-tw-slot-zero-protection-skips-to-later-selectors')
        values(b'$CUR(TW,1)$$CUR(TW,0)$$STW(8)$')
        values(b'$PROT(TW,CUR,1,S8)$$ISPROT(TW,CUR)$:$ISPROT(TW,1)$:$ISPROT(TW,S8)$',b'0:1:1')
        values(b'$UNPROT(TW,CUR,1,S8)$$ISPROT(TW,CUR)$:$ISPROT(TW,1)$:$ISPROT(TW,S8)$',b'0:0:0')
        capture(b'\x01|*\r\n');values(f'$WORLD(CUR,{width},{height})$'.encode())
        t.case('rip3-tw-backup-all-definitions-attributes-cursors-margins')
        capture(b'\x01|w0A050T0C10\r\n'
                   b'\x1b[3g\x1b[1;6H\x1bH\x1b[2;7r\x1b[?6h\x1b[3;4H\x1b[s\x1b[32;44m\x1b[?7l\x1b[?25l')
        capture(resident(4,b'$-=EFFECT=bad$zero',0))
        values(b'$CUR(TW,1)$')
        capture(window((200,100,500,300),10,6,2,1)
                   +b'\x1b[33;41m\x1b[38;2;12;34;56m\x1b[48;2;65;43;21m\x1b[4;2H\x1b[s\x1b[?25h\x1b[2 q')
        capture(resident(4,b'$-=EFFECT=bad$one',1)+resident(2,b'$-=EFFECT=bad$floating'))
        values(b'$-=EFFECT=cold$$PROT(TW,1)$')
        before=capture();saved=values(b'$STW$$STW(1)$$STW(PUSH)$$COPY(TW,BASE,S7)$');same(saved,before)
        values(b'$CUR(TW,0)$')
        capture(b'\x1b[0m\x1b[r\x1b[?6l\x1b[?7h\x1b[?25h\x1b[Hchanged')
        capture(resident(4,b'changed',0))
        values(b'$UNPROT(TW,1)$$CUR(TW,1)$')
        capture(window((10,10,100,80),3,2,0,1)+b'\x1b[0mchanged')
        values(b'$CUR(TW,3)$$CUR(PAL,17)$$CUR(STYLE,7)$$CUR(PORT,1)$$WORLD(CUR,320,240)$$RESET(SCREEN)$')
        t.case('rip3-tw-backup-no-pixels-and-independent-families')
        before=capture();frame=values(b'$RTW$');same(frame,before)
        values(b'$CUR(TW)$:$INUSE(TW,3)$:$ISPROT(TW,1)$:$CUR(PAL)$:$CUR(STYLE)$:$CUR(PORT)$:$WORLD$',b'1:0:1:17:7:1:320:240')
        assert xy(frame)==(2,4) and frame.application['cterm.attr']==0x46 and frame.metadata['pixel_cursor_visible']
        assert frame.application['cterm.right_margin']==10 and frame.application['cterm.bottom_margin']==6
        t.case('rip3-tw-backup-font-and-ansi-following-restore')
        cw,ch=(8,19) if width in (640,720) else (10,24) if width==800 else (12,30)
        assert (frame.metadata['pixel_cursor_width'],frame.metadata['pixel_cursor_height'])==(cw,ch)
        x=frame.metadata['pixel_cursor_x'];y=frame.metadata['pixel_cursor_y']
        accented=b'\x82' if args.encoding=='CP437' else 'é'.encode()
        frame=capture(b'F'+accented)
        colors={pixel(frame,xx,yy) for yy in range(y,y+ch) for xx in range(x,x+cw)}
        assert 0x0c2238 in colors and 0x412b15 in colors
        frame=values(b'$CUR(TW,0)$')
        assert xy(frame)==(4,4) and frame.application['cterm.attr']==0x12
        assert (frame.application['cterm.top_margin'],frame.application['cterm.bottom_margin'])==(2,7)
        assert frame.application['cterm.ext_attr.origin_mode']==1 and frame.application['cterm.ext_attr.auto_wrap']==0
        assert not frame.metadata['pixel_cursor_visible']
        frame=capture(b'\x1b[?6l\x1b[H\t');assert xy(frame)==(6,1)
        frame=capture(b'\x1b[?6h\x1b[u');assert xy(frame)==(4,4)
        t.case('rip3-tw-backup-live-partial-ansi-not-saved-parser')
        # Ordinary source is saved independently; the current pending CSI must
        # finish on the restored current TW instead of reverting to old input.
        capture(b'\x1b[3'+query(b'$RTW$',at_cursor=True))
        frame=capture(b'1mF');assert frame.application['cterm.attr']==0x44
        t.case('rip3-tw-backup-slots-stack-protection-and-reusable-base')
        values(b'$PROT(TW,S1)$$RTW(1)$$INUSE(TW,S1)$',b'1')
        values(b'$UNPROT(TW,S1)$$RTW(1)$$INUSE(TW,S1)$',b'0')
        values(b'$RTW(POP)$$INUSE(TW,STACK)$',b'0')
        values(b'$COPY(TW,S7,TBL)$$INUSE(TW,S7)$',b'0')
        values(b'$&EFFECT$',b'cold')
        t.case('rip3-tw-backup-inactive-current-and-soft-lifetime')
        values(b'$UNPROT(TW,1)$$DTW$$STW(2)$$ATW$$RTW(2)$')
        frame=capture(b'ignored');assert xy(frame)==(2,4) and not frame.metadata['pixel_cursor_visible']
        values(b'$ATW$')
        capture(b'\x01|*\r\n');values(b'$RTW$')
        values(b'$CUR(TW)$:$ISPROT(TW,1)$:$INUSE(TW,3)$:$&EFFECT$',b'1:1:0:cold')
        t.case('rip3-tw-backup-deferred-wrap-and-native-scroll')
        values(b'$CUR(TW,0)$$CUR(PAL,0)$')
        capture(b'\x1b[?6l\x1b[r\x1b[?7h\x1b[32;44m\x1b[48;2;0;0;170m\x1b[8;20HA')
        values(b'$STW(5)$')
        before=capture(b'\x1b[48;2;170;0;0m\x1b[2J')
        frame=values(b'$RTW(5)$');same(frame,before)
        frame=capture(b'B');assert xy(frame)==(2,8)
        ox=80*width//640;oy=40*height//350;cw=width//80;ch=height//43
        assert pixel(frame,ox+10*cw,oy+ch//2)==0xaa0000
        assert pixel(frame,ox+10*cw,oy+6*ch+ch//2)==0xaa0000
        assert pixel(frame,ox+10*cw,oy+7*ch+ch//2)==0x0000aa
        values(b'$RTW$')
        t.case('rip3-tw-backup-reset-selectors-preserve-live-and-protected-saves')
        values(b'$RESET(TW,BASE,BOGUS)$', b'$RESET(TW,BASE,BOGUS)$');values(b'$INUSE(TW,BASE)$',b'1')
        values(b'$STW(3)$$PROT(TW,S3)$$STW(PUSH)$')
        before=capture();same(values(b'$RESET(TW,BACKUP)$'),before)
        values(b'$INUSE(TW,BASE)$$INUSE(TW,S3)$$INUSE(TW,STACK)$$CUR(TW)$',b'0101')
        values(b'$UNPROT(TW,S3)$$RESET(TW,S3)$$INUSE(TW,S3)$',b'0')
        t.case('rip3-tw-backup-invalid-and-missing-nonfatal-recovery')
        values(b'$STW(10)$', b'$STW(10)$');values(b'$RTW(9)$');values(b'$RTW(PUSH)$', b'$RTW(PUSH)$')
        values(b'$CUR(TW)$:alive',b'1:alive')
        assert client.capture().application['connected']==1
        if width == 640:
            t.case('rip3-copy-text-window-table-between-base-slots-and-stack')
            values(b'$CUR(TW,3)$$COPY(TW, TBL, S3, BASE, S5)$'
                   b'$INUSE(TW,S3)$$INUSE(TW,BASE)$$INUSE(TW,S5)$', b'111')
            values(b'$CUR(TW,9)$$COPY(TW, TBL, PUSH)$$COPY(TW, BASE, TBL)$'
                   b'$CUR(TW)$:$INUSE(TW,9)$:$INUSE(TW,BASE)$', b'3:0:1')
            values(b'$COPY(TW, POP, TBL)$$CUR(TW)$:$INUSE(TW,STACK)$', b'9:0')
            values(b'$COPY(TW, TBL, BASE)$$COPY(TW, S3, BASE)$$COPY(TW, BASE, S2)$'
                   b'$COPY(TW, TBL, S3)$$COPY(TW, S3, S5)$$COPY(TW, S2, TBL)$'
                   b'$CUR(TW)$:$INUSE(TW,S2)$:$INUSE(TW,S3)$', b'3:0:1')
            frame = values(b'$COPY(TW, S5, TBL)$$CUR(TW)$:$INUSE(TW,S5)$', b'9:0')
            assert frame.application['connected'] == 1


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path)
    parser.add_argument('encoding',choices=('CP437','UTF-8'))
    parser.add_argument('--reports',type=Path,required=True)
    args=parser.parse_args()
    for width,height in ((640,480),(800,600),(1024,768),(720,400)):
        run(args,width,height)
