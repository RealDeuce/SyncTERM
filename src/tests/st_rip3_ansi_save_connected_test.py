#!/usr/bin/env python3
"""Interpreter-owned ESC7/8, independently asserted over a live RIP3 connection."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path
import time
from st_migrated_control_test import connection, square_canvas
from st_terminal_conformance_test import Terminal
from st_rip3_connected_test import query
from st_rip3_extended_window_connected_test import window,xy


def run(args,width,height):
    with connection(args.executable,args.reports,encoding=args.encoding,
                    options='ScreenMode=Custom\nRIP=RIPv3\n',
                    settings=square_canvas(width, height)) as (client,root,peer,send):
        t=Terminal(client,root,peer,send);serial=0
        def capture(data=b'',expected=b''):
            nonlocal serial
            serial+=1;fence=f'@AS{serial}@'.encode();send(data+query(fence,at_cursor=True))
            received=bytearray();deadline=time.monotonic()+10
            while not received.endswith(fence):
                peer.settimeout(max(0.001,deadline-time.monotonic()))
                part=peer.recv(4096);assert part,'connection closed';received.extend(part)
                assert len(received)<65536
            with (root/'replies.bin').open('ab') as stream:stream.write(received)
            assert received==expected+fence,(bytes(received),expected+fence)
            return client.capture()
        def values(text,expected=b''):
            return capture(query(text,at_cursor=True),expected)
        def same(a,b):assert a.primary==b.primary and a.alternate==b.alternate
        def cursor(frame,x,y,attr=None):
            assert xy(frame)==(x,y),xy(frame)
            if attr is not None:assert frame.application['cterm.attr']==attr,(frame.application['cterm.attr'],attr)
            assert frame.application['connected']==1
            assert (frame.metadata['plane_width'],frame.metadata['plane_height'])==(width,height)
        capture(b'\x1b[!',b'RIPSCRIP030001')
        t.case(f'rip3-ansi-save-absent-and-startup-font-{width}x{height}')
        before=capture(b'\x1b[2;4H');frame=capture(b'\x1b8');same(before,frame);cursor(frame,4,2)
        capture(b'\x1b7');values(b'$CUR(TW,5)$')
        before=capture();frame=capture(b'\x1b8');same(before,frame);cursor(frame,4,2)
        assert frame.metadata['pixel_cursor_width']==width//80
        values(b'$CUR(TW,0)$');capture(b'\x01|*\r\n')
        t.case('rip3-ansi-save-ordinary-font-two-and-requested-cursor')
        capture(b'\x01|w0000270O12\r\n\x1b[H\x1b[s\x1b[2;4H\x1b[?25l\x1b7')
        values(b'$CUR(TW,5)$');before=capture();frame=capture(b'\x1b8');same(before,frame)
        cursor(frame,4,2);assert not frame.metadata['pixel_cursor_visible']
        values(b'$ISEXTWIN$:$TWW$:$TWH$:$TWFONT$',b'0:80:25:3')
        frame=capture(b'\x1b[H\x1b[u');cursor(frame,4,2)
        values(b'$CUR(TW,0)$');frame=capture(b'\x1b[u');cursor(frame,1,1)
        capture(b'\x1b[?25h')
        t.case('rip3-ansi-save-live-versus-csi-cursor-and-same-chunk-following-text')
        values(f'$WORLD(CUR,{width},{height})$'.encode())
        capture(window((30,40,400,250),8,5,2,1))
        setup=b'\x1b[H\x1b[s\x1b[3g\x1b[1;6H\x1bH\x1b[2;4H\x1b(A\x1b)0\x1b[31;44m'
        capture(setup+b'\x1b7\x1b[32m')
        frame=capture(b'\x1b[u');cursor(frame,1,1,0x12)  # ESC7 did not replace source CSI save.
        values(b'$CUR(TW,5)$');capture(window((450,300,600,380),4,3,4,1))
        values(b'$WORLD(CUR,160,100)$')
        before=capture();frame=capture(b'\x1b8');same(before,frame);cursor(frame,4,2,0x14)
        assert frame.application['cterm.rip3_g0']==ord('A') and frame.application['cterm.rip3_g1']==ord('0')
        values(b'$CUR(TW)$:$TWW$:$TWH$:$TWFONT$',b'5:8:5:3')
        frame=capture(b'\x1b[H\x1b[u');cursor(frame,4,2,0x14)
        frame=capture(b'\x1b[H\t');cursor(frame,6,1,0x14)
        word=b'\x82' if args.encoding=='CP437' else 'é'.encode()
        frame=capture(b'\x1b[32m\x1b8'+word);cursor(frame,5,2,0x14)
        before=capture();frame=capture(b'\x1b8');same(before,frame);cursor(frame,4,2,0x14)
        t.case('rip3-ansi-save-source-replacement-and-independent-whole-window-backup')
        values(b'$STW$$CUR(TW,0)$$RESET(TW,0)$$CUR(TW,5)$')
        before=capture();frame=capture(b'\x1b8');same(before,frame);cursor(frame,4,2,0x14)
        # Save a later state; restoring an earlier whole-table backup cannot rewind S.
        capture(b'\x1b[3;2H\x1b[33m\x1b7');values(b'$RTW$')
        frame=capture(b'\x1b8');cursor(frame,2,3,0x16)
        t.case('rip3-ansi-save-protected-source-protected-target-and-zero-normalization')
        values(b'$PROT(TW,5)$');capture(b'\x1b[35m\x1b7');values(b'$CUR(TW,3)$')
        frame=capture(b'\x1b8');cursor(frame,2,3,0x15);values(b'$ISPROT(TW,3)$',b'1')
        capture(b'\x1b[32m\x1b[H');frame=capture(b'\x1b8');cursor(frame,1,1,0x12)
        values(b'$CUR(TW,0)$');frame=capture(b'\x1b8');cursor(frame,2,3,0x15)
        values(b'$ISPROT(TW,CUR)$',b'0')
        t.case('rip3-ansi-save-soft-reset-delete-and-inactive-gate')
        # Save survives reset, including deletion/redefinition of its source.
        capture(b'\x01|*\r\n');before=capture();frame=capture(b'\x1b8');same(before,frame);cursor(frame,2,3,0x15)
        values(b'$CUR(TW,3)$$UNPROT(TW,3)$$RESET(TW,3)$')
        values(b'$RESET(TW,ALL)$')
        frame=capture(b'\x1b8');cursor(frame,2,3,0x15)
        values(b'$DTW$');before=capture();frame=capture(b'\x1b7\x1b8ignored');same(before,frame)
        values(b'$ATW$');frame=capture(b'\x1b8');cursor(frame,2,3,0x15)
        capture(b'\x1b' b'c');before=capture();frame=capture(b'\x1b8');same(before,frame);cursor(frame,2,3,0x15)
        values(b'$COPY(TW,0,7)$$CUR(TW,7)$');capture(b'\x1b[H\x1b[32m')
        before=capture();frame=capture(b'\x1b8');same(before,frame);cursor(frame,2,3,0x15)
        # Split input, one connection snapshot, followed by live text in that same write.
        capture(b'\x1b');frame=capture(b'8'+word);cursor(frame,3,3,0x15)
        t.case('rip3-ansi-save-nonzero-source-deletion-does-not-recreate-it')
        values(b'$CUR(TW,3)$');capture(b'\x1b[2;4H\x1b7')
        values(b'$RESET(TW,3)$$UNPROT(TW,5)$$CUR(TW,5)$')
        before=capture();frame=capture(b'\x1b8');same(before,frame);cursor(frame,4,2)
        values(b'$CUR(TW)$:$INUSE(TW,3)$',b'5:0')
        values(b'after-ansi-save',b'after-ansi-save')


        t.case('rip3-mtw-whole-list-validation-and-independent-window-state')
        capture(b'\x01|h01000100\r\n')
        values(f'$WORLD(CUR,{width},{height})$'.encode())
        capture(b'\x01|S0102|B0A0A0D0D\r\n')
        capture(window((100, 90, 320, 260), 8, 6, 4, 1)
                + b'\x1b[2;3H\x1b[s\x1b[3g\x1b[1;6H\x1bH'
                  b'\x1b[2;5r\x1b[?69h\x1b[3;7s\x1b[4;5H'
                  b'\x1b[31;44m\x1b(A\x1b)0\x1b[?25l\x1b7')
        values(b'$CUR(TW,5)$')
        capture(b'\x01|w0A050H0A12\r\n\x1b[4;5H')
        values(b'$PROT(TW,5)$$CUR(TW,7)$')
        capture(b'\x01|w0A050H0A11\r\n\x1b[3;6H')
        values(b'$DTW(7)$$CUR(TW,5)$')
        before = capture()
        for invalid in (b'36', b'BASE', b'ALL,BOGUS'):
            source = b'$MTW(0,CUR,' + invalid + b')$alive'
            frame = values(source, source)
            same(before, frame)
            cursor(frame, 5, 4)
            values(b'$TWW(0)$:$TWH(0)$:$CURX(0)$:$CURY(0)$', b'8:6:5:4')

        frame = values(b'$MTW(0,cUr,0,35)$')
        same(before, frame)
        cursor(frame, 5, 4)
        values(b'$CUR(TW)$:$INUSE(TW,35)$:$TWIN(7)$:$ISEXTWIN(0)$:$TWFONT(0)$:$TWW(0)$:$TWH(0)$',
               b'5:0:NO:1:5:40:25')
        expected = {640: (640, 475), 800: (800, 600), 1024: (800, 600), 720: (720, 400)}
        w, h = expected[width]
        values(b'$TWX0(0,BOUND,DEVICE)$:$TWY0(0,TEXT,DEVICE)$:$TWX1(0,TEXT,DEVICE)$:$TWY1(0,BOUND,DEVICE)$',
               f'0:0:{w}:{h}'.encode())
        frame = values(b'$CUR(TW,0)$')
        cursor(frame, 1, 1, 0x14)
        assert not frame.metadata['pixel_cursor_visible']
        assert frame.application['cterm.rip3_g0'] == ord('A')
        assert frame.application['cterm.rip3_g1'] == ord('0')
        app = frame.application
        assert (app['cterm.top_margin'], app['cterm.bottom_margin']) == (1, 25)
        assert (app['cterm.left_margin'], app['cterm.right_margin']) == (1, 40)
        frame = capture(b'\x1b[u')
        cursor(frame, 3, 2, 0x14)
        cursor(capture(b'\t'), 6, 2, 0x14)

        t.case('rip3-mtw-retains-esc-snapshot-and-partial-ansi-input')
        frame = capture(b'\x1b8')
        same(before, frame)
        cursor(frame, 5, 4, 0x14)
        app = frame.application
        assert (app['cterm.top_margin'], app['cterm.bottom_margin']) == (2, 5)
        assert (app['cterm.left_margin'], app['cterm.right_margin']) == (3, 7)
        values(b'$TWW$:$TWH$:$ISEXTWIN$', b'8:6:1')
        frame = capture(b'\x1b[3' + query(b'$MTW$', at_cursor=True) + b'2m')
        same(before, frame)
        cursor(frame, 1, 1, 0x12)
        values(b'$TWW$:$TWH$', b'40:25')

        t.case('rip3-mtw-all-skips-inactive-and-maximizes-ordinary-window')
        values(b'$UNPROT(TW,5)$$MTW(ALL)$')
        frame = values(b'$CUR(TW,5)$')
        cursor(frame, 1, 1)
        values(b'$TWX0$:$TWY0$:$TWX1$:$TWY1$:$TWFONT$:$ISEXTWIN$', b'0:0:79:24:3:0')
        values(b'$TWIN(7)$', b'NO')
        frame = values(b'$ATW(7)$$CUR(TW,7)$')
        cursor(frame, 6, 3)
        values(b'$TWW$:$TWH$:after-mtw', b'8:6:after-mtw')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path)
    parser.add_argument('encoding',choices=('CP437','UTF-8'))
    parser.add_argument('--reports',type=Path,required=True)
    args=parser.parse_args()
    for width,height in ((640,480),(800,600),(1024,768),(720,400)):run(args,width,height)
