#!/usr/bin/env python3
"""Exercise RIP_HEADER and the scene input lock over a real TCP connection."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path

from st_migrated_control_test import connection
from st_rip3_connected_test import pixel, query
from st_rip3_extended_window_connected_test import window, xy
from st_rip3_mouse_connected_test import field
from st_rip3_text_erase_connected_test import number, scene
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
    # Current PAL index 0 is bright blue, independent of the ANSI
    # background, STYLE background, inactive viewport and selected port.
    t.exchange(b'\x01|a0009|S0004\r\n\x1b[41m')
    values(b'$CUR(PORT,2)$$DVP$')
    frame = t.exchange(text_header(4096))
    assert pixel(frame, 33, 31) == 0x0000ff and pixel(frame, 321, 211) == 0xaa0000
    assert xy(frame) == (1, 1)
    frame = t.exchange(b'\x1b[u')
    assert xy(frame) == (4, 4)

    # Activate-all enables 5 without defining unused slots. Current erase
    # then clears only 5, including its bounding margin, and homes its cursor.
    t.exchange(text_header(256) + text_header(2, 5))
    values(b'$PROT(TW,5)$')
    frame = t.exchange(b'\x01|h01000001302000\r\n')
    assert xy(frame) == (1, 1)
    for point in ((320, 210), (321, 211), (599, 379)):
        assert pixel(frame, *point) == 0x0000ff
    for point in ((319, 210), (320, 209), (600, 379), (599, 380)):
        assert pixel(frame, *point) == 0
    values(b'$CUR(TW)$:$TWIN(0)$:$TWIN(35)$:$ISPROT(TW,3)$:$CUR(PORT)$:$TEXTXY(2)$',
           b'5:YES:NO:1:2:-1')
    values(b'$UNPROT(TW,3)$$UNPROT(TW,5)$')
    t.exchange(b'\x01|*\r\n')


def geometry_actions(t, values, width, height):
    t.case('rip3-header-geometry-noncurrent-retains-font-and-ansi-state')
    t.exchange(scene(width, height))
    # The scene helper leaves a narrow inactive viewport. Neither it nor
    # the later WORLD change applies to physical text-window geometry.
    t.exchange(window((100, 90, 240, 210), 5, 4, 4, 1)
               + b'\x1b[3g\x1b[1;5H\x1bH\x1b[2;3r\x1b[2;2H\x1b[s'
                 b'\x1b[3;4H\x1b[31;44m\x1b[?25l')
    values(b'$CUR(TW,5)$')
    t.exchange(b'\x1b[7;8H')
    values(b'$WORLD(CUR,730,470)$')
    before = t.exchange()
    frame = t.exchange(b'\x01|h01000001300040\r\n')
    assert xy(frame) == (8, 7)
    assert frame.primary == before.primary and frame.alternate == before.alternate
    expected = {640: (640, 475), 800: (800, 600), 1024: (800, 600), 720: (640, 475)}
    w, h = expected[width]
    values(b'$CUR(TW)$:$ISEXTWIN(0)$:$TWFONT(0)$:$TWW(0)$:$TWH(0)$:$CURX(0)$:$CURY(0)$',
           b'5:1:5:40:25:1:1')
    values(b'$TWX0(0,BOUND,DEVICE)$:$TWY0(0,TEXT,DEVICE)$:$TWX1(0,BOUND,DEVICE)$:$TWY1(0,TEXT,DEVICE)$',
           f'0:0:{w}:{h}'.encode())
    frame = t.exchange(text_header(2))
    assert xy(frame) == (1, 1) and not frame.metadata['pixel_cursor_visible']
    assert frame.application['cterm.attr'] == 0x14
    assert (frame.application['cterm.top_margin'], frame.application['cterm.bottom_margin']) == (1, 25)
    assert xy(t.exchange(b'\x1b[u')) == (2, 2)
    assert xy(t.exchange(b'\t')) == (5, 2)

    t.case('rip3-header-geometry-active-and-protection-gates')
    t.exchange(b'\x01|w0A050H0A11\r\n\x1b[4;5H')
    values(b'$CUR(TW,5)$$PROT(TW,5)$')
    before = t.exchange(text_header(2048))
    assert xy(before) == (8, 7)
    # Return to the protected active window before the ANSI reply fence.
    t.exchange(text_header(64) + text_header(2) + text_header(2048) + text_header(2, 5))
    values(b'$TWIN(0)$', b'NO')
    assert xy(t.exchange(text_header(2 | 512))) == (5, 4)
    values(b'$TWW$:$TWH$', b'8:6')
    # Activation precedes the geometry gate in the same segment.
    frame = t.exchange(text_header(128) + b'\x01|h01000001300e00\r\n')
    assert xy(frame) == (1, 1)
    assert frame.primary == before.primary and frame.alternate == before.alternate
    values(b'$TWX0$:$TWY0$:$TWX1$:$TWY1$:$TWFONT$:$TWIN$', b'0:0:90:42:2:YES')
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
        if width == 720:
            t.case('rip3-header-geometry-before-first-microansi-definition')
            label = b'\x82' if args.encoding == 'CP437' else 'é'.encode()
            before = t.exchange(b'\x1b[4;4H' + label)
            frame = t.exchange(b'\x01|h01000001300W00\r\n')
            assert xy(frame) == (1, 1)
            assert frame.primary == before.primary and frame.alternate == before.alternate
            values(b'$TWW$:$TWH$:$TWFONT$entry', b'80:43:entry')
            assert frame.metadata['plane_width'] == width and frame.metadata['plane_height'] == height
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
        geometry_actions(t, values, width, height)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('encoding', choices=('CP437', 'UTF-8'))
    parser.add_argument('--reports', type=Path, required=True)
    args = parser.parse_args()
    for width, height in ((640, 480), (800, 600), (1024, 768), (720, 400)):
        run(args, width, height)
