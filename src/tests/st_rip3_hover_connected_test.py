#!/usr/bin/env python3
"""Resident hover queries through TCP, physical motion and the real menu editor."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path
from st_migrated_control_test import connection
from st_terminal_conformance_test import Terminal
from st_rip3_connected_test import query
from st_rip3_mouse_connected_test import field
from st_rip3_buttons_connected_test import style,button
from st_rip3_button_styles_connected_test import number
from syncterm_fixture import receive


def resident(mode,source,window=0):
    return b'\x01|1\x1b'+number(mode,1)+number(window,1)+b'00'+source+b'\r\n'


def run(args,width,height):
    with connection(args.executable,args.reports,encoding=args.encoding,
                    options='ScreenMode=Custom\nRIP=RIPv3\n',
                    settings=f'CustomColumns={width//8}\nCustomRows={height//8}\nCustomFontHeight=8\n') as (client,root,peer,send):
        t=Terminal(client,root,peer,send)
        def values(source,expected=b''):
            return t.exchange(query(source,at_cursor=True),expected)
        t.exchange(b'\x1b[!',b'RIPSCRIP030001')
        frame=t.exchange(b'\x01|*\r\n\x1b[?25l')
        values(f'$WORLD(CUR,{width},{height})$'.encode())
        m=frame.metadata
        def mouse(x,y,left=False,move=True):
            px=m['destination_x']+(2*x+1)*m['destination_width']//(2*m['plane_width'])
            py=m['destination_y']+(2*y+1)*m['destination_height']//(2*m['plane_height'])
            count=client.capture().metadata['events_dequeued']
            client.mouse(px,py,action=0 if move else 1,buttons=int(left))
            client.wait_for(lambda s:s.metadata['events_dequeued']>count)
        def move(x,y,expected=b'',left=False):
            mouse(x,y,left);return t.exchange(expected=expected)
        def fields():
            return (field((10,10,60,60),b'ID=7:A$FIELDID$')
                    +field((20,20,80,80),b'ID=8:B$FIELDID$'))
        t.case(f'rip3-hover-overlap-identity-and-pointer-{width}x{height}')
        before=t.exchange(resident(5,b'E$FIELDID$;$MCURSOR(4)$',35)
                          +resident(6,b'X$FIELDID$;$MCURSOR(0)$',26)+fields())
        frame=move(30,30,b'E8;E7;')
        assert frame.metadata['pointer_shape']==2
        assert frame.primary==before.primary and frame.alternate==before.alternate
        move(30,30)
        frame=move(90,90,b'X8;X7;');assert frame.metadata['pointer_shape']==0
        move(15,15,b'E7;')
        move(70,70,b'X7;E8;')  # exits precede even a newer entry
        move(80,30,b'X8;')
        move(60,30,b'E8;');move(59,30,b'E7;')  # half-open far edge
        t.case('rip3-hover-preserves-held-click-owner')
        mouse(30,30,True,False);t.exchange()
        move(90,90,b'X8;X7;',left=True)
        move(30,30,b'E8;E7;',left=True)
        mouse(30,30,False,False);t.input(b'B8')
        t.case('rip3-hover-definition-off-validation-and-no-identity')
        t.exchange(resident(5,b'$oFf$'))
        move(90,90,b'X8;X7;');move(30,30)
        t.exchange(resident(5,b'E$FIELDID$;'))
        move(30,30)  # restoring a handler does not erase membership
        move(90,90,b'X8;X7;');move(30,30,b'E8;E7;')
        t.exchange(b'\x01|1\x1b5Z01E$FIELDID$;|1\x1b7000bad\r\n')
        move(90,90,b'X8;X7;')
        t.exchange(field((100,10,140,50),b'anonymous'))
        move(110,30);move(111,30)  # unnumbered fields have no shared hover
        t.case('rip3-hover-focus-definition-bound-and-membership')
        move(30,30,b'E8;E7;')
        client.focus(False);client.focus(True);t.exchange()
        move(30,30,b'E8;E7;')  # fresh interaction; no movement synthesized on focus
        t.exchange(resident(5,b''))  # rejected empty replacement retains E
        move(90,90,b'X8;X7;');move(30,30,b'E8;E7;')
        t.exchange(resident(5,b'A'*1023)+resident(5,b'B'*1024))
        move(90,90,b'X8;X7;');move(30,30,b'A'*2046)
        t.exchange(resident(5,b'E$FIELDID$;'))
        move(90,90,b'X8;X7;')
        t.case('rip3-hover-definition-uses-first-nul')
        t.exchange(resident(5,b'N$FIELDID$;\0'+b'A'*1024)
                   +resident(6,b'\0replacement'))
        move(30,30,b'N8;N7;')
        move(90,90,b'X8;X7;')
        # Exact sentinel before the NUL clears the entry expression. The
        # following exit definition still dispatches on the same RIP line.
        t.exchange(b'\x01|1\x1b5000$oFf$\0ignored|1\x1b6000Q$FIELDID$;\0ignored\r\n')
        move(30,30)
        move(90,90,b'Q8;Q7;')
        t.exchange(b'\x01|1\x1b0000skip\0ignored|1\x1b0000alive\r\n',b'alive')
        t.exchange(resident(6,b'X$FIELDID$;'))
        t.case('rip3-hover-handler-failure-stays-inside-parser')
        t.exchange(resident(5,b'$RAND("bad")$'))
        move(30,30,b'$RAND("bad")$' * 2)  # Both overlapping fields run the stored query.
        move(90,90,b'X8;X7;')
        t.exchange(resident(5,b'E$FIELDID$;'))
        t.case('rip3-hover-button-redraw-and-held-gesture')
        t.exchange(style(flags=256|1024|2)
                   +button((150,100,190,130),b'ID=9:<><>button:$FIELDID$'))
        move(170,110,b'E9;')
        mouse(170,110,True,False);t.exchange()
        move(200,140,b'X9;',left=True)
        move(170,110,b'E9;',left=True)
        mouse(170,110,False,False);t.input(b'button:9')
        move(90,90,b'X9;')
        t.case('rip3-hover-self-deletion-and-handler-lifetime')
        t.exchange(resident(5,b'$MKILL$kept:$FIELDID$'))
        move(30,30,b'kept:8')  # older snapshot token was deleted before its turn
        move(90,90)
        # 1K clears fields/membership, not the resident expression. TELKILL.FN
        # clears its two query strings explicitly after issuing 1K.
        t.exchange(b'\x01|1K\r\n'+resident(5,b'E$FIELDID$;')+fields())
        move(30,30,b'E8;E7;')
        t.exchange(b'\x01|1K\r\n'+fields())
        move(30,30,b'E8;E7;')
        t.case('rip3-hover-real-prompts-keep-fieldid-and-wire-order')
        move(90,90,b'X8;X7;')
        t.exchange(resident(5,b'E$FIELDID$:$=LIVE,8@Entry <<FIELDID>>$;'))
        mouse(30,30);client.wait_text('Entry 8')
        send(b'\x1b[5n')
        def answer(value):
            client.key('home')
            for _ in range(8):client.key('delete')
            client.text(value);client.key('enter')
        answer('Ready');client.wait_text('Entry 7');answer('Done')
        receive(peer,b'E8:Ready;E7:Done;\x1b[0n',root/'replies.bin')
        t.case('rip3-hover-focus-during-real-prompt')
        move(90,90,b'X8;X7;')
        t.exchange(resident(5,b'E$FIELDID$:$=FOCUSED,8@Focus <<FIELDID>>$;'))
        mouse(30,30);client.wait_text('Focus 8')
        client.focus(False);client.focus(True)
        send(b'\x1b[5n');answer('kept')
        # The committed callback finishes, while old uncommitted A is skipped.
        receive(peer,b'E8:kept;\x1b[0n',root/'replies.bin')
        t.exchange(resident(5,b'E$FIELDID$;'))
        move(30,30,b'E8;E7;')  # boundary left both stable instances outside
        t.case('rip3-hover-full-reset-clears-definitions')
        move(90,90,b'X8;X7;')
        t.exchange(resident(5,b'$RESET(SOFT)$kept:$FIELDID$'))
        move(30,30,b'kept:8')
        # SOFT removes the old fields and both handlers. Definition/reentry
        # without reinstalling handlers must not run the old reset callback.
        values(f'$WORLD(CUR,{width},{height})$'.encode())
        t.exchange(fields())
        move(30,30);move(90,90)
        values(b'$FIELDID$')  # unavailable between callback scopes
        values(b'$&LIVE$',b'Done')
        values(b'alive',b'alive')
        assert client.capture().application['connected']==1


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path)
    parser.add_argument('encoding',choices=('CP437','UTF-8'))
    parser.add_argument('--reports',type=Path,required=True)
    args=parser.parse_args()
    for width,height in ((640,480),(720,400)):
        run(args,width,height)
