#!/usr/bin/env python3
"""Exercise RIP_HEADER and the scene input lock over a real TCP connection."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path

from st_migrated_control_test import connection
from st_rip3_connected_test import pixel, query
from st_rip3_extended_window_connected_test import xy
from st_rip3_mouse_connected_test import field
from st_terminal_conformance_test import Terminal


def run(args, width, height):
    with connection(args.executable, args.reports, encoding=args.encoding,
                    options='ScreenMode=Custom\nRIP=RIPv3\n',
                    settings=f'CustomColumns={width // 8}\nCustomRows={height // 8}\n'
                             'CustomFontHeight=8\nAudioModes=0\n') as (client, root, peer, send):
        t = Terminal(client, root, peer, send)

        def values(program, expected=b''):
            return t.exchange(query(program, at_cursor=True), expected)

        def wire(command, terminator=b'\r\n'):
            return t.exchange(b'\x01|' + command + terminator)

        t.exchange(b'\x1b[!', b'RIPSCRIP030001')
        wire(b'*')
        t.case(f'rip3-header-global-and-segment-order-{width}x{height}')
        values(b'$CUR(ENV,4)$$BASEMATH(CUR,64)$$CUR(TW,3)$$CUR(PAL,7)$$CUR(PORT,8)$$CUR(STYLE,9)$$CUR(BUT,6)$')
        wire(b'h0100080150002220')
        values(b'$CUR(TW)$:$CUR(BUT)$:$CUR(STYLE)$:$CUR(PORT)$:$CUR(PAL)$:$CUR(ENV)$', b'0:0:0:0:0:2')
        # The documented base-64 example sets the world and base after SOFT.
        wire(b'h01000201502O2220')
        values(b'$CUR(ENV)$:$BASEMATH$:$COORDSIZE$:$WORLD$', b'2:36:2:1280:960')
        # Accept a continuation inside a field and then an LF delimiter.
        wire(b'h010000015004\\\r\n0220', b'\n')
        values(b'$BASEMATH$', b'64')
        values(b'$BASEMATH(CUR,36)$')

        t.case('rip3-header-typed-segments-select-their-own-tables')
        values(b'$CUR(PORT,2)$$CUR(PORT,0)$$CUR(PAL,4)$$PROT(PAL,4)$$CUR(PAL,7)$')
        # Button 7, style 6, port 2, text window 5 and palette 5.
        wire(b'h01000005002710262000223000254065')
        values(b'$CUR(TW)$:$CUR(BUT)$:$CUR(STYLE)$:$CUR(PORT)$:$CUR(PAL)$', b'5:7:6:2:5')
        values(b'$INUSE(PAL,4)$:$ISPROT(PAL,4)$:$INUSE(PAL,7)$', b'1:1:0')
        wire(b'h01000001201402')
        values(b'$TEXTXY(2)$', b'-1')
        wire(b'h01000001200G02')
        values(b'$TEXTXY(2)$', b'0:0')
        values(b'$CUR(TW,0)$$CUR(PORT,0)$')

        t.case('rip3-header-hard-acceptance-and-invalid-late-segment')
        t.exchange(b'\x1b[2;4H\x1b7\x1b[3;3H')
        values(b'$CUR(TW,3)$$PROT(TW,3)$$STW(1)$$PROT(TW,S1)$$CUR(TW,0)$')
        wire(b'h01000102700702')  # The second audio segment is invalid.
        frame = t.exchange(b'\x1b8')
        assert xy(frame) == (4, 2)
        values(b'$INUSE(TW,3)$:$ISPROT(TW,3)$:$INUSE(TW,S1)$', b'1:1:1')
        wire(b'h01000200')
        frame = t.exchange(b'\x1b8')
        assert xy(frame) == (4, 2)
        wire(b'h01000300')  # Hard wins over soft and invalidates the saved cursor.
        frame = t.exchange(b'\x1b[3;3H\x1b8')
        assert xy(frame) == (3, 3)
        values(b'$INUSE(TW,3)$:$INUSE(TW,S1)$', b'0:0')

        t.case('rip3-header-lock-suppresses-keys-and-mouse-during-receipt')
        values(f'$WORLD(CUR,{width},{height})$'.encode())
        frame = t.exchange(field((20, 20, 60, 60), b'click'))
        meta = frame.metadata

        def event(action):
            count = client.capture().metadata['events_dequeued']
            action()
            client.wait_for(lambda s: s.metadata['events_dequeued'] > count)

        def click(button):
            x = meta['destination_x'] + 40 * meta['destination_width'] // width
            y = meta['destination_y'] + 40 * meta['destination_height'] // height
            event(lambda: client.mouse(x, y, action=1, buttons=button))

        frame = wire(b'h01000400', b'\f')
        assert frame.metadata['pointer_shape'] == 6
        event(lambda: client.key('up'))
        click(1)
        click(0)
        values(b'alive-while-locked', b'alive-while-locked')
        frame = wire(b'#', b'\n')
        assert frame.metadata['pointer_shape'] == 0
        click(1)
        click(0)
        t.input(b'click')
        wire(b'h01000400')
        frame = wire(b'h01000000')
        assert frame.metadata['pointer_shape'] == 0

        t.case('rip3-header-one-numbered-save-and-physical-black')
        values(b'$SAVE(0)$$PROT(SCREEN,S0)$$SAVE(1)$$SAVE(2)$$SAVE$$SAVE(PUSH)$')
        wire(b'h0100000180E')
        values(b'$INUSE(SCREEN,BASE)$$INUSE(SCREEN,STACK)$$INUSE(SCREEN,S0)$$INUSE(SCREEN,S1)$$INUSE(SCREEN,S2)$',
               b'00101')
        wire(b'h01000001804')
        values(b'$INUSE(SCREEN,S0)$$INUSE(SCREEN,S2)$', b'10')
        values(b'$MCURSOR(2)$')
        frame = wire(b'c04|X0K0K')
        assert pixel(frame, 20, 20) == 0xaa0000
        frame = wire(b'h01000001801')
        assert pixel(frame, 20, 20) == 0
        assert frame.metadata['pointer_shape'] == 4 and frame.application['connected'] == 1


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('encoding', choices=('CP437', 'UTF-8'))
    parser.add_argument('--reports', type=Path, required=True)
    args = parser.parse_args()
    for width, height in ((640, 480), (800, 600), (1024, 768), (720, 400)):
        run(args, width, height)
