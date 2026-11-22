#!/usr/bin/env python3
"""Viewport-only RESET through TCP: geometry/activation without lost content."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path
from st_migrated_control_test import connection
from st_terminal_conformance_test import Terminal
from st_rip3_connected_test import query,pixel
from st_rip3_button_styles_connected_test import number


def run(args,width,height):
    with connection(args.executable,args.reports,encoding=args.encoding,
                    options='ScreenMode=Custom\nRIP=RIPv3\n',
                    settings=f'CustomColumns={width//8}\nCustomRows={height//8}\nCustomFontHeight=8\n') as (client,root,peer,send):
        t=Terminal(client,root,peer,send)
        def values(source,expected=b''):
            return t.exchange(query(source,at_cursor=True),expected)
        def wire(source):
            return t.exchange(b'\x01|'+source+b'\r\n')
        def coords(area):
            return b''.join(number(n,2) for n in area)
        def define(n,area,flags=0):
            return wire(b'2P'+number(n,1)+coords(area)+number(flags,1))
        def same(a,b):
            assert a.primary==b.primary and a.alternate==b.alternate
        def ordinary(frame):
            assert (frame.application['cterm.x'],frame.application['cterm.y'],frame.application['cterm.attr'])==(4,2,0x16)
            assert (frame.metadata['plane_width'],frame.metadata['plane_height'])==(width,height)
            values(b'$CUR(ENV)$:$CUR(PAL)$:$CUR(STYLE)$:$CUR(BUT)$:$CUR(TW)$',b'5:17:7:6:3')
        t.exchange(b'\x1b[!',b'RIPSCRIP030001');wire(b'*')
        values(b'$CUR(ENV,5)$'+f'$WORLD(CUR,{width},{height})$'.encode()+b'$CUR(PAL,17)$$CUR(STYLE,7)$$CUR(BUT,6)$$CUR(TW,3)$')
        accented=b'\x82' if args.encoding=='CP437' else 'é'.encode()
        t.exchange(b'\x1b[?25l\x1b[33;44m\x1b[2;3H'+accented)
        define(1,(100,80,140,110));define(2,(0,0,4,3),1)
        wire(b'S0104|B'+coords((300,220,301,220)))
        wire(b'2C0'+coords((300,220,302,221))+b'2'+coords((1,1,3,2))+b'0')
        values(b'$CUR(PORT,2)$');wire(b'v'+coords((1,1,3,3))+b'|m0V0W')
        # m adds the existing (1,1) viewport origin to its (31,32) anchor.
        values(b'$TEXTXY(2)$',b'32:33')
        values(b'$DVP(2)$')
        t.case(f'rip3-view-reset-activates-current-without-paint-{width}x{height}')
        before=client.capture();frame=values(b'$MVP$');same(before,frame)
        values(b'$TEXTXY(2)$',b'-1')
        # Reset uses existing port geometry, independent of zero WORLD and radix.
        values(b'$WORLD(CUR,0,0)$$BASEMATH(CUR,64)$')
        before=client.capture();frame=values(b'$RESET(vIeW,,)$');same(before,frame);ordinary(frame)
        values(b'$CUR(PORT)$:$TEXTXY(2)$',b'2:32:33')
        values(b'$BASEMATH(CUR,36)$'+f'$WORLD(CUR,{width},{height})$'.encode())
        t.case('rip3-view-reset-retains-copy-pair-and-offscreen-pixels')
        values(b'$CUR(PORT,0)$');wire(b'W0|S0100|B'+coords((300,220,301,220)))
        frame=values(b'$PCB(2)$');assert pixel(frame,300,220)==0xaa0000 and pixel(frame,301,220)==0xaa0000
        assert pixel(frame,302,220)==0 and pixel(frame,300,221)==0
        ordinary(frame)
        t.case('rip3-view-reset-rejects-invalid-late-selectors-before-changes')
        values(b'$DVP(2)$')
        for selector in (b'36',b'BASE',b'ALL',b'S0',b'STACK',b'BOGUS',b'-1'):
            before=client.capture();frame=values(b'$RESET(VIEW,2,'+selector+b')$');same(before,frame)
            values(b'$TEXTXY(2)$',b'-1')
        before=client.capture();frame=values(b'$RESET(VIEW,0002,CUR,2,35)$');same(before,frame)
        values(b'$INUSE(PORT,35)$:$TEXTXY(2)$:$CUR(PORT)$',b'0:32:33:0')
        t.case('rip3-view-reset-table-skips-protected-inactive-and-preserves-port-origin')
        values(b'$CUR(PORT,1)$');wire(b'v'+coords((110,90,120,100)))
        values(b'$DVP(1)$$PROT(PORT,1)$$CUR(PORT,0)$$DVP(2)$')
        before=client.capture();frame=values(b'$RESET(VIEW,TBL)$');same(before,frame);ordinary(frame)
        values(b'$TEXTXY(1)$:$TEXTXY(2)$:$ISPROT(PORT,1)$:$CUR(PORT)$',b'-1:32:33:1:0')
        values(b'$UNPROT(PORT,1)$$RESET(VIEW,1)$$CUR(PORT,1)$')
        values(b'$TEXTXY(1)$',b'0:0')
        # X uses context-absolute coordinates; the reset must expose exactly
        # this port's (100,80)..(139,109) physical bounds.
        frame=wire(b'c02|X'+coords((100,80))+b'|X'+coords((139,109))+b'|X'+coords((140,110)))
        assert pixel(frame,100,80)==0x00aa00 and pixel(frame,139,109)==0x00aa00
        assert pixel(frame,140,110)==0
        ordinary(frame)
        values(b'$CUR(PORT)$:after-view-reset',b'1:after-view-reset')
        assert frame.application['connected']==1


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path)
    parser.add_argument('encoding',choices=('CP437','UTF-8'))
    parser.add_argument('--reports',type=Path,required=True)
    args=parser.parse_args()
    for width,height in ((640,480),(800,600),(1024,768),(720,400)):run(args,width,height)
