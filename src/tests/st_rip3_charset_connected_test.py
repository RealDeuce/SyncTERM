#!/usr/bin/env python3
"""RIP3 ANSI designation state and literal CP437 bank output through TCP.

The glyph oracle reads immutable MicroANSI asset rows, independently of the
emulator. No VT-102 mode or UK/special display translation is requested.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path
import time
from st_migrated_control_test import connection, square_canvas
from st_terminal_conformance_test import Terminal
from st_rip3_connected_test import query,check_default_text
from st_rip3_extended_window_connected_test import window,xy


def run(args,width,height):
    with connection(args.executable,args.reports,encoding=args.encoding,
                    options='ScreenMode=Custom\nRIP=RIPv3\n',
                    settings=square_canvas(width, height)) as (client,root,peer,send):
        t=Terminal(client,root,peer,send);serial=0
        def capture(data=b'',expected=b''):
            nonlocal serial
            serial+=1;fence=f'@CHR{serial}@'.encode();send(data+query(fence,at_cursor=True))
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
        def sets(frame,g0,g1):
            assert frame.application['cterm.rip3_g0']==ord(g0) and frame.application['cterm.rip3_g1']==ord(g1)
            assert frame.application['cterm.emulation']==0
            assert (frame.metadata['plane_width'],frame.metadata['plane_height'])==(width,height)
        def same(a,b):
            assert a.primary==b.primary and a.alternate==b.alternate
        capture(b'\x1b[!',b'RIPSCRIP030001');capture(b'\x01|*\r\n')
        t.case(f'rip3-ansi-literal-designation-si-so-bank-vector-{width}x{height}')
        box=b'\xb3' if args.encoding=='CP437' else '│'.encode()
        frame=capture(b'\x1b(A#\x0eq\x0f#'+box);sets(frame,'A','B');assert xy(frame)==(7,1)
        check_default_text(frame,[0x23,0x0e,0x71,0x0f,0x23,0xb3])
        before=capture();frame=capture(b'\x1b)0\x1b(H\x1b)x');same(before,frame);sets(frame,'A','0');assert xy(frame)==(7,1)
        frame=capture(b'\x1b[s\x1b(B\x1b[u');sets(frame,'B','0')
        frame=capture(b'\x1b(A\x1b' b'c');sets(frame,'A','0')
        t.case('rip3-charset-ordinary-definition-identity-and-activation')
        before=capture();frame=capture(b'\x01|w0101070510\r\n');same(before,frame);sets(frame,'A','0')
        frame=capture(b'\x01|w0101070500\r\n');sets(frame,'A','0')
        before=capture();frame=values(b'$DTW$');same(before,frame);sets(frame,'A','0')
        before=capture();frame=capture(b'\x1b(Bignored');same(before,frame);sets(frame,'A','0')
        frame=values(b'$ATW$');sets(frame,'A','0')
        t.case('rip3-charset-extended-and-live-reset-use-fresh-sets')
        values(f'$WORLD(CUR,{width},{height})$'.encode())
        before=capture();frame=capture(window((30,40,400,250),8,5,2,1));same(before,frame);sets(frame,'B','B')
        capture(b'\x1b(A\x1b)0');frame=capture(window((30,40,400,250),8,5,2,1));sets(frame,'B','B')
        capture(b'\x1b(A\x1b)0');before=capture();frame=values(b'$RESET(TW,0)$');same(before,frame);sets(frame,'B','B')
        t.case('rip3-charset-cur-first-use-wire-clone-and-entry-copy')
        capture(b'\x1b(A\x1b)0');frame=values(b'$CUR(TW,5)$');sets(frame,'B','B')
        capture(b'\x1b(0\x1b)A');frame=values(b'$CUR(TW,0)$');sets(frame,'A','0')
        frame=capture(b'\x01|2T60\r\n');sets(frame,'A','0')
        frame=values(b'$COPY(TW,0,5)$$CUR(TW,5)$');sets(frame,'A','0')
        t.case('rip3-charset-owned-tw-backup-and-soft-protected-survival')
        values(b'$STW$');capture(b'\x1b(B\x1b)B');before=capture();frame=values(b'$RTW$');same(before,frame);sets(frame,'A','0')
        capture(b'\x1b(0\x1b)A');values(b'$PROT(TW,5)$');frame=capture(b'\x01|*\r\n');sets(frame,'A','0')
        frame=values(b'$CUR(TW,5)$');sets(frame,'0','A')
        before=capture();frame=values(b'$RESET(TW,TBL)$');same(before,frame);sets(frame,'0','A')
        frame=values(b'$CUR(TW,0)$');sets(frame,'B','B')
        t.case('rip3-charset-split-designation-across-ordinary-definition')
        capture(b'\x1b('+b'\x01|w0101070510\r\n');frame=capture(b'A');sets(frame,'A','B')
        assert xy(frame)==(1,1)
        values(b'after-charset',b'after-charset');assert frame.application['connected']==1


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path)
    parser.add_argument('encoding',choices=('CP437','UTF-8'))
    parser.add_argument('--reports',type=Path,required=True)
    args=parser.parse_args()
    for width,height in ((640,480),(800,600),(1024,768),(720,400)):run(args,width,height)
