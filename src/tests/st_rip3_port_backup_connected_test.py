#!/usr/bin/env python3
"""PORT backups and clipboard selection through actual RIP3 connections."""
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
        def define(n,area,flags=0):
            return wire(b'2P'+number(n,1)+b''.join(number(v,2) for v in area)+number(flags,1))
        t.exchange(b'\x1b[!',b'RIPSCRIP030001')
        t.exchange(b'\x01|*\r\n\x1b[?25l')
        values(f'$WORLD(CUR,{width},{height})$'.encode())
        t.case(f'rip3-port-backup-clipboard-and-protected-survivors-{width}x{height}')
        define(1,(80,80,180,160))
        wire(b'S0104|B0K0K0P0P|1C0K0K0P0P0')  # First unused offscreen port is 2.
        values(b'$CUR(PORT,2)$$PROT(PORT,2)$')
        before=values(b'$SCB$$SCB(0)$$SCB(PUSH)$$COPY(PORT,BASE,S7)$')
        assert pixel(before,20,20)==0xaa0000
        values(b'$UNPROT(PORT,2)$')
        t.exchange(b'\x01|*\r\n\x1b[?25l')  # Deletes 2 and clears its selected pointer.
        values(f'$WORLD(CUR,{width},{height})$'.encode())
        define(1,(80,80,180,160))
        define(2,(0,0,3,3),1)
        wire(b'2s200|S0102|B00000202')
        values(b'$PROT(PORT,2)$$CUR(PORT,1)$')
        wire(b'v00000505')
        values(b'$PROT(PORT,1)$')
        define(3,(300,200,330,230),8)  # Absent in saved table, protected live survivor.
        define(4,(350,250,380,280))
        values(b'$CUR(PORT,0)$')
        wire(b'S0101|B14141919|1C141419190')  # First unused is now 5; pointer differs.
        wire(b'S0105|B0000'+number(width-1,2)+number(height-1,2))
        values(b'$CUR(PORT,4)$$CUR(PAL,17)$$CUR(STYLE,7)$')
        before=client.capture();after=values(b'$RCB$')
        assert after.primary==before.primary and after.alternate==before.alternate
        values(b'$CUR(PORT)$:$ISPROT(PORT,2)$:$INUSE(PORT,3)$:$ISPROT(PORT,3)$:$INUSE(PORT,4)$:$INUSE(PORT,5)$',
               b'2:1:1:1:0:0')
        values(b'$PORTX0(1)$:$PORTY0(1)$:$PORTW(1)$:$PORTH(1)$:$PORTW(3)$:$CUR(PAL)$:$CUR(STYLE)$',
               b'80:80:100:80:30:17:7')
        values(b'$CUR(PORT,0)$')
        frame=wire(b'1P2S2S0')  # Restored pointer selects saved red contents, not blue 5.
        assert pixel(frame,100,100)==0xaa0000 and pixel(frame,20,20)==0xaa00aa
        t.case('rip3-port-backup-reusable-base-numbered-stack-and-generic-copy')
        values(b'$RCB$$RCB(0)$$INUSE(PORT,S0)$',b'0')
        values(b'$RCB(POP)$$INUSE(PORT,STACK)$',b'0')
        values(b'$COPY(PORT,S7,TBL)$$INUSE(PORT,S7)$',b'0')
        values(b'$SCB(3)$$PROT(PORT,S3)$$RCB(3)$$INUSE(PORT,S3)$',b'1')
        values(b'$UNPROT(PORT,S3)$$RCB(3)$$INUSE(PORT,S3)$',b'0')
        t.case('rip3-port-backup-empty-selection-is-restored-with-port-definitions')
        # A fresh scene has no clipboard. SCB still captures its PORT table.
        values(b'$UNPROT(PORT,1)$$UNPROT(PORT,2)$$UNPROT(PORT,3)$')
        t.exchange(b'\x01|*\r\n\x1b[?25l')
        values(f'$WORLD(CUR,{width},{height})$'.encode())
        define(8,(10,10,30,30))
        values(b'$SCB(4)$')
        wire(b'S0104|B0K0K0P0P|1C0K0K0P0P0')
        values(b'$RCB(4)$$INUSE(PORT,1)$$INUSE(PORT,8)$',b'01')
        before=client.capture();after=wire(b'1P5K5K0')
        assert before.primary==after.primary and before.alternate==after.alternate
        t.case('rip3-port-backup-soft-and-nonfatal-recovery')
        values(b'$SCB(5)$')
        t.exchange(b'\x01|*\r\n\x1b[?25l')
        values(b'$RCB(5)$$INUSE(PORT,8)$',b'1')
        values(b'$SCB(10)$', b'$SCB(10)$');values(b'$RCB(PUSH)$', b'$RCB(PUSH)$');values(b'$RCB(9)$')
        values(b'$CUR(PORT)$:alive',b'0:alive')
        t.case('rip3-port-reset-orders-live-targets-and-preserves-independent-saves')
        values(f'$WORLD(CUR,{width},{height})$'.encode())
        wire(b'S0104|B0K0K0P0P|v00000505|k02')
        define(6,(10,10,50,50))
        define(7,(10,10,50,50),8)
        define(9,(0,0,3,3),1)
        values(b'$CUR(PORT,6)$$SCB$$SCB(3)$$PROT(PORT,S3)$$SCB(PUSH)$')
        before=client.capture()
        after=values(b'$RESET(PORT,BASE,6,36)$', b'$RESET(PORT,BASE,6,36)$')
        assert after.primary==before.primary and after.alternate==before.alternate
        values(b'$CUR(PORT)$:$INUSE(PORT,6)$:$INUSE(PORT,BASE)$',b'6:1:1')
        after=values(b'$RESET(PORT,7,34,6,CUR)$')
        assert pixel(after,20,20)==0x00aa00 and pixel(after,width-1,height-1)==0x00aa00
        values(b'$CUR(PORT)$:$INUSE(PORT,6)$:$ISPROT(PORT,7)$:$INUSE(PORT,9)$:$INUSE(PORT,BASE)$',
               b'0:0:1:1:1')
        values(b'$CUR(PORT,9)$$RESET(PORT)$$CUR(PORT)$:$INUSE(PORT,9)$',b'0:0')
        values(b'$RESET(PORT,ALL)$$INUSE(PORT,8)$:$ISPROT(PORT,7)$:$INUSE(PORT,BASE)$:'
               b'$INUSE(PORT,STACK)$:$INUSE(PORT,S3)$',b'0:1:0:0:1')
        assert client.capture().application['connected']==1


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path)
    parser.add_argument('encoding',choices=('CP437','UTF-8'))
    parser.add_argument('--reports',type=Path,required=True)
    args=parser.parse_args()
    for width,height in ((640,480),(800,600),(1024,768),(720,400)):
        run(args,width,height)
