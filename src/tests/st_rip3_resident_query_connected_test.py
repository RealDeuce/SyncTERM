#!/usr/bin/env python3
"""Stored modes 1-4 never execute on physical events; positive 0/5/6 controls."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path
from st_migrated_control_test import connection, square_canvas
from st_terminal_conformance_test import Terminal
from st_rip3_connected_test import query
from st_rip3_hover_connected_test import resident
from st_rip3_mouse_connected_test import field


def run(args,width,height):
    with connection(args.executable,args.reports,encoding=args.encoding,
                    options='ScreenMode=Custom\nRIP=RIPv3\n',
                    settings=square_canvas(width, height)) as (client,root,peer,send):
        t=Terminal(client,root,peer,send)
        def values(source,expected=b''):
            return t.exchange(query(source,at_cursor=True),expected)
        t.exchange(b'\x1b[!',b'RIPSCRIP030001')
        frame=t.exchange(b'\x01|*\r\n\x1b[?25l')
        values(f'$WORLD(CUR,{width},{height})$'.encode());m=frame.metadata
        def mouse(x,y,left=False,move=False):
            px=m['destination_x']+(2*x+1)*m['destination_width']//(2*m['plane_width'])
            py=m['destination_y']+(2*y+1)*m['destination_height']//(2*m['plane_height'])
            count=client.capture().metadata['events_dequeued']
            client.mouse(px,py,action=0 if move else 1,buttons=int(left))
            client.wait_for(lambda s:s.metadata['events_dequeued']>count)
        def click(x,y,expected=b''):
            mouse(x,y,True);mouse(x,y)
            return t.input(expected) if expected else t.exchange()
        t.case(f'rip3-stored-query-overlap-no-click-execution-{width}x{height}')
        values(b'$-=EFFECT=cold$')
        before=t.exchange()
        source=b'$-=EFFECT=bad$unexpected:$FIELDID$'
        accented=b'\x82' if args.encoding=='CP437' else 'é'.encode()
        frame=t.exchange(resident(1,source+accented,35)+resident(2,source+accented,35)
                         +resident(3,source,0)+resident(4,source,0))
        assert frame.primary==before.primary and frame.alternate==before.alternate
        for x,y in ((0,0),(1,1),(width//2,height//2),(width-1,height-1)):
            mouse(x,y,move=True);click(x,y)
        values(b'$&EFFECT$:$CUR(PORT)$:$CUR(TW)$',b'cold:0:0')
        t.case('rip3-stored-query-positive-immediate-hover-and-field-controls')
        t.exchange(resident(5,b'E$FIELDID$;')+resident(6,b'X$FIELDID$;')
                   +field((10,10,60,60),b'ID=7:field:$FIELDID$'))
        t.case('rip3-reset-query-preserves-display-hover-and-active-entries')
        before=client.capture()
        after=values(b'$RESET(QUERY,TW,0,VIEW,CUR)$', b'$RESET(QUERY,TW,0,VIEW,CUR)$')
        assert after.primary==before.primary and after.alternate==before.alternate
        after=values(b'$RESET(QUERY,ALL)$$CUR(PORT)$:$CUR(TW)$',b'0:0')
        assert after.primary==before.primary and after.alternate==before.alternate
        mouse(30,30,move=True);t.input(b'E7;');click(30,30,b'field:7')
        mouse(80,80,move=True);t.input(b'X7;')
        values(b'$&EFFECT$',b'cold')
        t.case('rip3-bookmark-names-expand-empty-in-resident-and-field-actions')
        t.exchange(resident(5,b'E$PASSWORD$:$&Username=fallback$;')
                   +field((70,10,110,50),b'ID=9:field:<<PASSWORD>>:$INUSE(TV,Username)$'))
        mouse(90,30,move=True);t.input(b'E:;');click(90,30,b'field::0')
        mouse(130,80,move=True);t.input(b'X9;')
        # Stored strings are still inert over matching numbered definitions
        # after first-use cloning/selection, protected writes and backup restore.
        t.case('rip3-stored-query-port-backup-and-window-lifetimes')
        values(b'$CUR(PORT,1)$$CUR(TW,1)$')
        t.exchange(resident(3,source,1)+resident(4,source,1))
        values(b'$COPY(PORT,TBL,BASE)$$PROT(PORT,1)$$PROT(TW,1)$')
        t.exchange(resident(3,b'$OFF$',1)+resident(4,b'$OFF$',1))
        values(b'$COPY(PORT,BASE,TBL)$$MKILL$')
        for x,y in ((30,30),(width//2,height//2)):
            mouse(x,y,move=True);click(x,y)
        values(b'$&EFFECT$',b'cold')
        t.case('rip3-stored-query-malformed-and-size-recovery')
        for mode in (1,2,3,4):
            t.exchange(resident(mode,b'')+resident(mode,b'x'*1024))
            t.exchange(resident(mode,b'$oFf$')+resident(mode,b'$OFF$ suffix'))
        t.exchange(resident(3,source,35)+resident(4,source,35)+resident(7,b'bad'))
        values(b'$INUSE(PORT,35)$:$INUSE(TW,35)$:$&EFFECT$',b'0:0:cold')
        t.case('rip3-stored-query-soft-retains-independent-port-save')
        t.exchange(b'\x01|*\r\n');values(b'$COPY(PORT,BASE,TBL)$')
        mouse(30,30,move=True);click(30,30)
        values(b'$&EFFECT$:alive',b'cold:alive')
        assert client.capture().application['connected']==1


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path)
    parser.add_argument('encoding',choices=('CP437','UTF-8'))
    parser.add_argument('--reports',type=Path,required=True)
    args=parser.parse_args()
    for width,height in ((640,480),(720,400)):
        run(args,width,height)
