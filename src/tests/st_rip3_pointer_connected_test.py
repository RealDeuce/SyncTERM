#!/usr/bin/env python3
"""RIP3 native pointer metadata, independent of pixels and input capture."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path
from st_migrated_control_test import connection
from st_terminal_conformance_test import Terminal
from st_rip3_connected_test import query
from st_rip3_mouse_connected_test import field
from st_rip3_extended_window_connected_test import window


def run(args):
    with connection(args.executable,args.reports,encoding=args.encoding,
                    options='ScreenMode=Custom\nRIP=RIPv3\n',
                    settings='CustomColumns=80\nCustomRows=60\nCustomFontHeight=8\n') as (client,root,peer,send):
        t=Terminal(client,root,peer,send)
        def values(program,expected=b''):
            return t.exchange(query(program,at_cursor=True),expected)
        t.exchange(b'\x1b[!',b'RIPSCRIP030001')
        before=t.exchange(b'\x01|*\r\n\x1b[?25l')
        expected=(0,3,4,1,2,5,6,7)  # RIP numbers -> public native pointer enum.
        t.case('rip3-wire-pointer-shapes-and-no-raster-effects')
        for number,shape in enumerate(expected):
            frame=t.exchange(f'\x01|1c{number:02d}\r\n'.encode())
            assert frame.metadata['pointer_shape']==shape,(number,frame.metadata)
            assert frame.primary==before.primary and frame.alternate==before.alternate
        frame=t.exchange(b'\x01|1c020000\r\n')
        assert frame.metadata['pointer_shape']==4
        t.case('rip3-pointer-variables-style-protection-and-recovery')
        values(b'$CUR(STYLE,5)$$PROT(STYLE,5)$$WORLD(CUR,0,0)$')
        frame=t.exchange(b'\x01|1c06\r\n')
        assert frame.metadata['pointer_shape']==6
        for number,shape in enumerate(expected):
            frame=values(f'$MCURSOR({number})$'.encode())
            assert frame.metadata['pointer_shape']==shape
        frame=t.exchange(b'\x01|1c08|1cZZ|1c060001|1c0\r\n'+query(b'$MCURSOR(8)$',at_cursor=True))
        assert frame.metadata['pointer_shape']==7 and frame.application['connected']==1
        frame=values(b'$MCURSOR(7)$')
        assert frame.metadata['pointer_shape']==7
        frame=values(b'$MCURSOR$')
        assert frame.metadata['pointer_shape']==0
        values(b'$UNPROT(STYLE,5)$$CUR(STYLE,0)$$WORLD(CUR,640,480)$$COORDSIZE(CUR,5)$$BASEMATH(CUR,64)$')
        frame=t.exchange(b'\x01|1c030000\r\n')
        assert frame.metadata['pointer_shape']==1
        values(b'$COORDSIZE(CUR,2)$$BASEMATH(CUR,36)$')
        t.case('rip3-pointer-shape-does-not-disable-mouse-input')
        frame=t.exchange(field((20,20,60,60),b'click')+b'\x01|1c06\r\n')
        m=frame.metadata
        def mouse(x,y,left=False,move=False):
            px=m['destination_x']+(2*x+1)*m['destination_width']//(2*m['plane_width'])
            py=m['destination_y']+(2*y+1)*m['destination_height']//(2*m['plane_height'])
            before_count=client.capture().metadata['events_dequeued']
            client.mouse(px,py,action=0 if move else 1,buttons=int(left))
            client.wait_for(lambda s:s.metadata['events_dequeued']>before_count)
        mouse(40,40,True)
        frame=t.exchange(b'\x01|1c07\r\n')
        assert frame.metadata['pointer_shape']==7 and frame.metadata['pointer_visible']
        mouse(40,40)
        frame=t.input(b'click');assert frame.metadata['pointer_shape']==7
        t.case('rip3-link-hover-restores-protocol-pointer')
        frame=t.exchange(b'\x1b[2;2H\x1b]8;;https://example.invalid/\x1b\\Link\x1b]8;;\x1b\\')
        mouse(10,13,move=True);frame=t.exchange()
        assert frame.metadata['pointer_shape']==2
        mouse(150,100,move=True);frame=t.exchange()
        assert frame.metadata['pointer_shape']==7
        # An unaligned text view must use actual pixels and its own cells,
        # including when adjacent inside/outside pixels share a display cell.
        frame=t.exchange(window((19,23,99,56),10,3,0,0)
                         +b'\x1b[H\x1b]8;;https://example.invalid/view\x1b\\Link\x1b]8;;\x1b\\')
        mouse(20,24,move=True);frame=t.exchange()
        assert frame.metadata['pointer_shape']==2
        mouse(18,24,move=True);frame=t.exchange()
        assert frame.metadata['pointer_shape']==7
        mouse(20,22,move=True);frame=t.exchange()
        assert frame.metadata['pointer_shape']==7
        t.case('rip3-soft-reset-pointer-default')
        frame=t.exchange(b'\x01|*\r\n')
        assert frame.metadata['pointer_shape']==0
        values(b'alive',b'alive')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path)
    parser.add_argument('encoding',choices=('CP437','UTF-8'))
    parser.add_argument('--reports',type=Path,required=True)
    run(parser.parse_args())
