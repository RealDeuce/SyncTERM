#!/usr/bin/env python3
"""Synthetic regressions for SpitfireTN/syncterm-skypix report 647972f.

Literal geometry and Pearl/8 rows are independent expectations, not ACIS
capture pixels. Native CTerm tests separately exercise every input split.
The extra-parameter cases also cover the 2026-11-10 RoF menu capture.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import socket
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'testing'))
from syncterm_fixture import fixture,check_pixel_planes

ROWS = ((0x10, 0x38, 0x6c, 0xc6, 0xfe, 0xc6, 0xc6, 0),
        (0xfc, 0xc6, 0xc6, 0xfc, 0xc6, 0xc6, 0xfc, 0),
        (0xfc, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0xfc, 0))
GLYPHS = ((8, 9, 0), (40, 25, 1), (72, 41, 0),
          (80, 8, 0), (88, 8, 1), (96, 8, 2))
WIRE = (b'\x1bc\x1b[?25l\x1b[!\x1b[15;3!'
        b'\x1b[8;100;70!\x1b[2;550;70!\x1b[2;550;170!'
        b'\x1b[2;100;170!\x1b[2;100;70!\x1b[15;4!\x1b[3;0;101;71!'
        b'\x1b[15;2!\x1b[10;8!Pearl.font!\x1b[19;8;8!A'
        b'\x1b[19;40;24!B\x1b[19;72;40!A'
        b'\x1b[10;20!Diamond.font!\x1b[10;0!\x1b[2;11HABC\b\x1b[KD')


def expected_scene(height):
    scene = [512] * (640 * height)
    for y in range(70, 171):
        for x in range(100, 551):
            scene[y * 640 + x] = 515 if x in (100, 550) or y in (70, 170) else 516
    for gx, gy, glyph in GLYPHS:
        for y, row in enumerate(ROWS[glyph]):
            for x in range(8):
                if row & (128 >> x):
                    scene[(gy + y) * 640 + gx + x] = 514
    return scene


def font_parameters(peer, client, kick, height):
    # Native Opal/9 A/B rows independently decoded with amitools from the
    # pinned Workbench inputs. A advances 10, B advances 8; bearings are zero.
    opal = ((8, (0x0f, 0x11, 0x21, 0x41, 0x83, 0x85, 0x79, 0, 0)),
            (7, (0x7e, 0x41, 0x41, 0x7e, 0x41, 0x41, 0x7e, 0, 0)))
    expected = [512] * (640 * height)

    def glyph(x, y, width, rows):
        for dy, row in enumerate(rows):
            for dx in range(width):
                if row & (1 << (width - 1 - dx)):
                    expected[(y + dy) * 640 + x + dx] = 514

    for index, letter in enumerate((0, 1, 0, 1, 0)):
        glyph(600 + index * 8, 17, 8, ROWS[letter])
    for x, letter in ((600, 0), (610, 1), (618, 0)):
        glyph(x, 41 if kick == '1.3' else 42, *opal[letter])
    glyph(24, height - 15, 8, ROWS[0])
    # Exercise the captured trailing empty field, explicit extra arguments,
    # and the maximum numeric set. Reset has no string argument.
    for extra in (';', ';7;8', ';0' * 15):
        data = (b'\x1bc\x1b[?25l\x1b[10;20!Diamond.font!\f'
                + f'\x1b[10;8{extra}!Pearl.font!\x1b[19;600;16!ABABA'.encode()
                + f'\x1b[10;9{extra}!Opal.font!\x1b[19;600;40!ABA'.encode()
                + f'\x1b[10;0;7!\x1b[19;24;{height-16}!A\x1b[5n'.encode())
        peer.sendall(data)
        reply = b''
        while len(reply) < 4:
            part = peer.recv(4 - len(reply))
            assert part, reply
            reply += part
        assert reply == b'\x1b[0n', reply
        frame = client.capture()
        assert frame.application['cterm.graphics_mode'] == (3 if kick == '1.3' else 4)
        assert (frame.palette[512], frame.palette[514]) == (0, 0xdddddd)
        assert frame.metadata['cursor_visible'] == 0
        start = frame.metadata['pixel_top'] * 640
        check_pixel_planes(frame, expected, start=start, context=(kick, extra))


def run(executable, reports, kick, encoding, status, mode_height):
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        listener.listen(1)
        listener.settimeout(15)
        entries = ('[SkyPix report]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                   f'Port={listener.getsockname()[1]}\nGraphics=SkyPix (Kick {kick})\n'
                   f'Encoding={encoding}\nNoStatus={str(not status).lower()}\n'
                   'AmigaFont=Pearl.font\nAmigaFontSize=8\n'
                   f'ScreenMode=Amiga640x{mode_height}\n')
        with fixture(executable, entries=entries, reports=reports,
                     settings='AudioModes=0\n', quiet=False) as (client, root):
            client.wait_text('SkyPix report')
            client.key('enter')
            with listener.accept()[0] as peer:
                peer.settimeout(10)
                client.wait_for(lambda s: s.application.get('connected') == 1)
                (root / 'wire.bin').write_bytes(WIRE)
                for repeat in range(2):
                    peer.sendall(WIRE + b'\x1b[5n')
                    reply = b''
                    while len(reply) < 4:
                        part = peer.recv(4 - len(reply))
                        assert part, reply
                        reply += part
                    # SkyPix must not answer the RIP version query in WIRE.
                    assert reply == b'\x1b[0n', reply
                    frame = client.capture()
                    m = frame.metadata
                    assert (m['plane_width'], m['plane_height']) == (640, mode_height), m
                    assert frame.application['cterm.skypix'] == 1
                    assert frame.application['cterm.graphics_mode'] == (3 if kick == '1.3' else 4)
                    height = mode_height - (8 if status else 0)
                    expected = expected_scene(height)
                    start = m['pixel_top'] * 640
                    check_pixel_planes(frame, expected, start=start,
                                       context=(kick, encoding, status, repeat))
                    assert tuple(frame.palette[512:517]) == (0, 0x1111ff, 0xdddddd, 0xff0000, 0x00ff11)
                # Literal native Topaz/11 A rows from the two original files.
                # This checks profile selection through the real application,
                # rather than selecting a built-in directly in a unit fixture.
                topaz = ((0x18, 0x3c, 0x3c, 0x66, 0x66, 0x7e, 0xc3, 0xc3, 0xc3, 0, 0)
                         if kick == '1.3' else
                         (0x7c, 0xc6, 0xc6, 0xc6, 0xfe, 0xc6, 0xc6, 0xc6, 0xc6, 0, 0))
                peer.sendall(b'\x1bc\x1b[?25l\x1b[10;11!Topaz.font!\x1b[19;8;8!A\x1b[5n')
                reply = b''
                while len(reply) < 4:
                    part = peer.recv(4 - len(reply))
                    assert part, reply
                    reply += part
                assert reply == b'\x1b[0n', reply
                frame = client.capture()
                expected = [512] * (640 * height)
                for y, row in enumerate(topaz):
                    for x in range(8):
                        if row & (128 >> x):
                            expected[(10 + y) * 640 + 8 + x] = 514
                start = frame.metadata['pixel_top'] * 640
                check_pixel_planes(frame, expected, start=start, context=(kick, encoding, status))
                assert frame.application['cterm.graphics_mode'] == (3 if kick == '1.3' else 4)
                font_parameters(peer, client, kick, height)
                peer.shutdown(socket.SHUT_RDWR)
                client.wait_text('Disconnected: Enter exits, F1 details')
                client.key('enter')
            client.wait_text('Directory: Name')
            assert b'[wren' not in client.stderr, bytes(client.stderr)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('executable')
    parser.add_argument('--reports', default='reports/test-control')
    args = parser.parse_args()
    for kick in ('1.3', '3.1'):
        for encoding in ('CP437', 'UTF-8'):
            for status in (False, True):
                for mode_height in (200, 400):
                    run(args.executable, args.reports, kick, encoding, status, mode_height)
