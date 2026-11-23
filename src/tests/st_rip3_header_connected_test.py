#!/usr/bin/env python3
"""Exercise RIP_HEADER and the scene input lock over a real TCP connection."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path

from st_migrated_control_test import connection
from st_rip3_connected_test import pixel, query
from st_rip3_extended_window_connected_test import window, xy
from st_rip3_mouse_connected_test import field
from st_rip3_text_erase_connected_test import number
from st_terminal_conformance_test import Terminal


def text_header(flags, slot=0):
    alphabet = b'0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz#&'
    digits = bytes(alphabet[(flags >> shift) & 63] for shift in (18, 12, 6, 0))
    return b'\x01|h010000013' + digits + bytes([alphabet[slot]]) + b'\r\n'


def text_window_actions(t, values, width, height):
    t.case('rip3-header-text-window-activation-and-caret')
    t.exchange(b'\x01|*\r\n')
    values(f'$WORLD(CUR,{width},{height})$'.encode())
    values(b'$CUR(TW,3)$')
    t.exchange(window((32, 30, 260, 200), 8, 5, 0, 1) + b'\x1b[4;4H\x1b[s\x1b[2;3H')
    values(b'$PROT(TW,3)$$CUR(TW,5)$')
    t.exchange(window((320, 210, 600, 380), 8, 5, 0, 1) + b'\x1b[3;4H\x1b[?25l')
    values(b'$CUR(TW,0)$')

    # The protected target stays active while all unprotected windows stop
    # accepting raw text. Select the protected window to fence the capture.
    frame = t.exchange(text_header(64) + b'ignored' + text_header(2, 3))
    assert xy(frame) == (3, 2)
    values(b'$TWIN(0)$:$TWIN(3)$:$TWIN(5)$:$TWIN(35)$', b'NO:YES:NO:NO')
    before = t.exchange()
    frame = t.exchange(text_header(32768))
    assert not frame.metadata['pixel_cursor_visible']
    assert frame.primary == before.primary and frame.alternate == before.alternate
    frame = t.exchange(text_header(262144))
    assert frame.metadata['pixel_cursor_visible'] and xy(frame) == (3, 2)
    # Inactive 5 ignores the show-all request and keeps its earlier hidden caret.
    t.exchange(text_header(131072))
    frame = t.exchange(text_header(2 | 512, 5))
    assert not frame.metadata['pixel_cursor_visible'] and xy(frame) == (4, 3)
    frame = t.exchange(text_header(262144))
    assert frame.metadata['pixel_cursor_visible'] and xy(frame) == (4, 3)
    t.exchange(text_header(128) + text_header(2, 3))
    values(b'$TWIN(5)$', b'NO')

    t.case('rip3-header-text-window-erase-bounds-and-live-cursor')
    coords = b''.join(number(v, 2) for v in (33, 31, 321, 211))
    frame = t.exchange(b'\x01|c04|X' + coords[:4] + b'|X' + coords[4:] + b'\r\n')
    assert pixel(frame, 33, 31) == 0xaa0000 and pixel(frame, 321, 211) == 0xaa0000
    values(b'$DVP$')
    # Both candidate color sources are black. The portable color choice is
    # tracked separately upstream. Here only scope, gates and homing matter.
    frame = t.exchange(text_header(4096))
    assert pixel(frame, 33, 31) == 0 and pixel(frame, 321, 211) == 0xaa0000
    assert xy(frame) == (1, 1)
    frame = t.exchange(b'\x1b[u')
    assert xy(frame) == (4, 4)

    # Activate-all enables 5 without defining unused slots. Current erase
    # then clears only 5, including its bounding margin, and homes its cursor.
    frame = t.exchange(text_header(256) + text_header(2, 5) + text_header(8192))
    assert pixel(frame, 321, 211) == 0 and xy(frame) == (1, 1)
    values(b'$CUR(TW)$:$TWIN(0)$:$TWIN(35)$:$ISPROT(TW,3)$', b'5:YES:NO:1')
    t.exchange(b'\x01|*\r\n')


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
        t.case('rip3-header-bounds-reset-skips-inactive-viewport')
        values(f'$WORLD(CUR,{width},{height})$'.encode())
        wire(b'v' + b''.join(number(v, 2) for v in (40, 30, 60, 60)))
        wire(b'h01000001201402')  # Deactivate current, then attempt bounds reset.
        values(b'$TEXTXY(2)$', b'-1')
        frame = wire(b'h01000001200G02|c02|X0K0K|X1E1E')
        assert pixel(frame, 20, 20) == 0 and pixel(frame, 50, 50) == 0x00aa00
        frame = wire(b'h01000001201002|X0K0K')  # Reset the now-active current viewport.
        assert pixel(frame, 20, 20) == 0x00aa00
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
        text_window_actions(t, values, width, height)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('encoding', choices=('CP437', 'UTF-8'))
    parser.add_argument('--reports', type=Path, required=True)
    args = parser.parse_args()
    for width, height in ((640, 480), (800, 600), (1024, 768), (720, 400)):
        run(args, width, height)
