#!/usr/bin/env python3
"""Synthetic regressions for SpitfireTN/syncterm-skypix report 647972f.

Literal geometry and Pearl/8 rows are independent expectations, not ACIS
capture pixels. Native CTerm tests separately exercise every input split.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import socket
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'testing'))
from syncterm_fixture import fixture

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


def run(executable, reports, kick, encoding, status):
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        listener.listen(1)
        listener.settimeout(15)
        entries = ('[SkyPix report]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                   f'Port={listener.getsockname()[1]}\nGraphics=SkyPix (Kick {kick})\n'
                   f'Encoding={encoding}\nNoStatus={str(not status).lower()}\n'
                   'AmigaFont=Pearl.font\nAmigaFontSize=8\nScreenMode=Amiga640x200\n')
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
                    assert (m['plane_width'], m['plane_height']) == (640, 200), m
                    assert frame.application['cterm.skypix'] == 1
                    assert frame.application['cterm.graphics_mode'] == (3 if kick == '1.3' else 4)
                    height = 192 if status else 200
                    expected = expected_scene(height)
                    start = m['pixel_top'] * 640
                    for plane in (frame.primary, frame.alternate):
                        actual = plane[start:start + len(expected)]
                        assert list(actual) == expected, (kick, encoding, status, repeat,
                            next((i % 640, i // 640, a, e)
                                 for i, (a, e) in enumerate(zip(actual, expected)) if a != e))
                    assert tuple(frame.palette[512:517]) == (0, 0x1111ff, 0xdddddd, 0xff0000, 0x00ff11)
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
                run(args.executable, args.reports, kick, encoding, status)
