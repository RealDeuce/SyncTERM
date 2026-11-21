#!/usr/bin/env python3
"""PR126 graphical text through the TCP/parser/bitmap presentation path."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path
from st_migrated_control_test import connection
from st_terminal_conformance_test import Terminal
from st_rip3_connected_test import query,pixel
from st_rip3_mouse_connected_test import field


def run(args):
    with connection(args.executable,args.reports,encoding=args.encoding,
                    options='ScreenMode=Custom\nRIP=RIPv3\n',
                    settings='CustomColumns=80\nCustomRows=60\nCustomFontHeight=8\n') as (client,root,peer,send):
        t=Terminal(client,root,peer,send)
        def values(source,expected=b''):
            return t.exchange(query(source,at_cursor=True),expected)
        t.exchange(b'\x1b[!',b'RIPSCRIP030001')
        t.exchange(b'\x01|*\r\n\x1b[?25l')
        values(b'$WORLD(CUR,640,480)$')
        t.exchange(b'\x01|Y00000100|c0F\r\n')
        t.case('rip3-graphical-builtins-and-ink')
        frame=t.exchange(b'\x01|m0A1E|T$RESX$\r\n'+query(b'$TEXTXY$',at_cursor=True),b'34:50')
        assert any(pixel(frame,x,y) for y in range(35,75) for x in range(10,34))
        assert not any(pixel(frame,x,y) for y in range(35,75) for x in range(34,60))
        t.case('rip3-graphical-literal-disabled-and-quoted-references')
        for source,expected in ((b'\\$V$',b'$V$'),(b'^M``',b'^M``'),
                                (b'$?3$',b'$?3$'),(b'$>SCENE.RIP$',b'$>SCENE.RIP$'),
                                (b'$WORLD(CUR,1,2)$',b'$WORLD(CUR,1,2)$'),
                                (b'$MKILL$',b'$MKILL$'),(b'$RESX(1)$',b'$RESX(1)$')):
            t.exchange(b'\x01|m0000|T'+source+b'\r\n'+query(b'$TEXTXY$',at_cursor=True),
                       f'{len(expected)*8}:0'.encode())
        values(b'$WORLD$',b'640:480')
        t.case('rip3-graphical-angle-template-and-single-ordinary-pass')
        values(b'[3:]$RESX$')
        values(b'[4:]$?3$')
        for source,expected in ((b'<<?3>>',b'640'),(b'<<?4>>',b'$?3$')):
            t.exchange(b'\x01|m0000|T'+source+b'\r\n'+query(b'$TEXTXY$',at_cursor=True),
                       f'{len(expected)*8}:0'.encode())
        t.case('rip3-graphical-position-prefix-survives-setter')
        t.exchange(b'\x01|@0A1E$COORDSIZE(CUR,5)$A\r\n'
                   +query(b'$TEXTXY$:$COORDSIZE$',at_cursor=True),b'10:50:5')
        values(b'$COORDSIZE(CUR,2)$')
        t.case('rip3-graphical-working-prefix-discards-provider-suffix')
        t.exchange(b'\x01|m0000|T'+b'A'*1022+b'$COORDSIZE(CUR,5)$\r\n'
                   +query(b'$TEXTXY$:$COORDSIZE$',at_cursor=True),b'8176:0:2')
        t.case('rip3-graphical-original-encoding-byte-budget')
        encoded=b'\x82'*1022 if args.encoding=='CP437' else b'\xc3\xa9'*511
        glyphs=1022 if args.encoding=='CP437' else 511
        t.exchange(b'\x01|m0000|T'+encoded+b'$COORDSIZE(CUR,5)$\r\n'
                   +query(b'$TEXTXY$:$COORDSIZE$',at_cursor=True),f'{glyphs*8}:0:2'.encode())
        t.case('rip3-graphical-failure-has-no-partial-ink-or-field-side-effects')
        before=t.exchange(field((20,20,60,60),b'click'))
        frame=t.exchange(b'\x01|Tbefore<<MKILL>>suffix\r\n'+query(b'alive',at_cursor=True),b'alive')
        assert frame.primary==before.primary and frame.alternate==before.alternate
        m=frame.metadata
        def mouse(left):
            x=m['destination_x']+81*m['destination_width']//(2*m['plane_width'])
            y=m['destination_y']+81*m['destination_height']//(2*m['plane_height'])
            count=client.capture().metadata['events_dequeued']
            client.mouse(x,y,action=1,buttons=int(left))
            client.wait_for(lambda s:s.metadata['events_dequeued']>count)
        mouse(True);mouse(False);t.input(b'click')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path)
    parser.add_argument('encoding',choices=('CP437','UTF-8'))
    parser.add_argument('--reports',type=Path,required=True)
    run(parser.parse_args())
