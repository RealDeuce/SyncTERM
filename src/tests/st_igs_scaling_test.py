#!/usr/bin/env python3
"""g state/persistence and absence of immediate scene or display effects.

Coherent bitmap/cell/palette/state captures. Coordinate-reader math has
independent native tests; no unimplemented drawing command is claimed.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import socket
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'testing'))
from syncterm_fixture import fixture, cell_at
from st_igs_init_test import Z8, Z16
from st_igs_resolution_test import initial


def check(frame, actual, captured, scaling, entry, status, seeded, token):
    m, state = frame.metadata, frame.application
    width = 320 if actual == 0 else 640
    height, ch = (400, 16) if actual == 2 else (200, 8)
    assert (m['plane_width'], m['plane_height']) == (width, height)
    assert (m['output_width'], m['output_height']) == (960, 720)
    assert (m['destination_width'], m['destination_height']) == (960, 720)
    assert state['cterm.igs_resolution'] == actual
    assert state['cterm.igs_scaling_mode'] == scaling
    assert state['cterm.igs_scaling_resolution'] == captured
    assert state['cterm.graphics_mode'] == 5 and state['cterm.skypix'] == 0
    assert (state['cterm.x'], state['cterm.y']) == (len(token) + 1, 1)
    assert tuple(frame.palette[768:784]) == tuple(initial(entry))
    assert cell_at(frame, 12, 12).glyph == (ord('Z') if seeded else 32)
    drawable = height - (ch if status else 0)
    expected = [768] * (width * (drawable - ch))
    if seeded:
        mask = (15, 3, 1)[actual]
        for y, row in enumerate(Z16 if actual == 2 else Z8):
            for x in range(8):
                if row & (128 >> x):
                    expected[(12 * ch + y - ch) * width + 96 + x] = 768 + mask
    start = (m['pixel_top'] + ch) * width
    for plane in (frame.primary, frame.alternate):
        image = list(plane[start:start + len(expected)])
        assert image == expected, (entry, actual, captured, scaling, status,
            next((i % width, i // width + ch, a, e)
                 for i, (a, e) in enumerate(zip(image, expected)) if a != e))


def run(executable, reports, entry, encoding, status):
    mode = ('AtariST40x25', 'AtariST80x25', 'AtariST80x25Mono')[entry]
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        listener.listen(2)
        listener.settimeout(15)
        entries = ('[IGS scaling]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                   f'Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\n'
                   f'ScreenMode={mode}\nFont=Atari ST\nEncoding={encoding}\n'
                   f'NoStatus={str(not status).lower()}\n')
        with fixture(executable, entries=entries, settings='AudioModes=0\n',
                     reports=reports, quiet=False) as (client, root):
            client.wait_text('IGS scaling')
            client.key('enter')
            with listener.accept()[0] as peer:
                client.wait_for(lambda s: s.application.get('connected') == 1)
                client.resize(960, 720)
                other = 1 - entry if entry != 2 else 2
                steps = (
                    (b'G#g 1:', entry, entry, 1, True),
                    (b'G#I 2:', entry, entry, 1, True),
                    (b'G#g 2:', entry, entry, 2, True),
                    (b'G#I 0:', entry, entry, 2, True),
                    (f'G#R {other},0:'.encode(), other, entry, 2, entry == 2),
                    (b'G#g 0:', other, other, 0, True),
                    (b'G#g 1:', other, other, 1, True),
                    (b'G#I 5:', entry, other, 1, entry == 2),
                    (b'G#g 9999:', entry, entry, 0, True),
                    (b'G#g 2:', entry, entry, 2, True),
                )
                for number, (command, actual, captured, scaling, seeded) in enumerate(steps):
                    token = f'SCALE{number:02d}'
                    wire = b'\x1bf\x1bE\x1bY,,Z' + command + b'\x1bH\x1bf' + token.encode()
                    with (root / 'wire.bin').open('ab') as stream:
                        stream.write(wire)
                    peer.sendall(wire)
                    check(client.wait_text(token), actual, captured, scaling, entry, status, seeded, token)
                peer.sendall(b'G#g 1')
                peer.shutdown(socket.SHUT_RDWR)
                client.wait_text('Disconnected: Enter exits, F1 details')
                client.key('enter')
            client.wait_text('Directory: Name')
            client.key('enter')
            with listener.accept()[0] as peer:
                client.wait_for(lambda s: s.application.get('connected') == 1)
                client.resize(960, 720)
                peer.sendall(b'\x1bf\x1bE\x1bY,,Z\x1bHFRESH')
                check(client.wait_text('FRESH'), entry, entry, 0, entry, status, True, 'FRESH')
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
    for entry in range(3):
        for encoding in ('ATARI-ST', 'CP437', 'UTF-8'):
            for status in (False, True):
                run(args.executable, args.reports, entry, encoding, status)
