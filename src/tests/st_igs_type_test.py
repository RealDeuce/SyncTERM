#!/usr/bin/env python3
"""T's conditional reads, marker/line state and unchanged coherent scenes.

Reference 15/29/52/128. Primitive raster use is tested with later drawing steps.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import socket
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'testing'))
from syncterm_fixture import fixture
from st_igs_scaling_test import check as check_scene


def check(frame, actual, entry, status, marker, line, seeded, token):
    check_scene(frame, actual, entry, 2, entry, status, seeded, token)
    state = frame.application
    assert (state['cterm.igs_marker_type'], state['cterm.igs_marker_height']) == marker
    assert tuple(state[f'cterm.igs_{key}'] for key in
                 ('line_type', 'line_width', 'line_begin', 'line_end', 'user_line')) == line
    assert state['cterm.igs_writing_mode'] == 4


def run(executable, reports, entry, encoding, status):
    screen = ('AtariST40x25', 'AtariST80x25', 'AtariST80x25Mono')[entry]
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        listener.listen(2)
        listener.settimeout(15)
        entries = ('[IGS types]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                   f'Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\n'
                   f'ScreenMode={screen}\nFont=Atari ST\nEncoding={encoding}\n'
                   f'NoStatus={str(not status).lower()}\n')
        with fixture(executable, entries=entries, settings='AudioModes=0\n',
                     reports=reports, quiet=False) as (client, root):
            client.wait_text('IGS types')
            client.key('enter')
            with listener.accept()[0] as peer:
                client.wait_for(lambda s: s.application.get('connected') == 1)
                client.resize(960, 720)
                peer.sendall(b'G#g>2:M 4:')
                other = 1 - entry if entry != 2 else 2
                steps = (
                    (b'G#T 1,7,8:', entry, (3,88), (1,1,0,0,0xffff), True),
                    (b'G#T 1,2,9:', entry, (2,11), (1,1,0,0,0xffff), True),
                    (b'G#T 2,1,40:', entry, (2,11), (1,39,0,0,0xffff), True),
                    (b'G#T 2,2,0:', entry, (2,11), (2,39,0,0,0xffff), True),
                    (b'G#T 2,1,52:', entry, (2,11), (1,39,0,1,0xffff), True),
                    (b'G#T 2,7,50:', entry, (2,11), (1,39,1,1,0xffff), True),
                    (b'G#T 2,7,1:', entry, (2,11), (7,1,1,1,0xaaaa), True),
                    (b'G#T 2,7,17:', entry, (2,11), (7,1,1,1,0x5555), True),
                    (b'G#T 2,7,33:', entry, (2,11), (7,1,1,1,0x5555), True),
                    (b'G#T 2,1,41:', entry, (2,11), (1,39,1,1,0x5555), True),
                    (b'G#T 2,5,64:', entry, (2,11), (5,39,1,2,0x5555), True),
                    (b'G#T 0:', entry, (2,11), (5,39,1,2,0x5555), True),
                    (b'G#T>9:M 4:', entry, (2,11), (5,39,1,2,0x5555), True),
                    (b'G#I>2:M 4:', entry, (1,11), (1,1,0,0,0x5555), True),
                    (b'G#T 2,7,33:', entry, (1,11), (7,1,0,0,0x5555), True),
                    (f'G#R {other},0:'.encode(), other, (1,11), (7,1,0,0,0x5555), entry == 2),
                    (b'G#I 5:', entry, (1,11), (7,1,0,0,0x5555), entry == 2),
                    (b'G#I>0:M 4:', entry, (1,11), (1,1,0,0,0x5555), True),
                )
                for number, (command, actual, marker, line, seeded) in enumerate(steps):
                    token = f'TYPE{number:02d}'
                    wire = b'\x1bf\x1bE\x1bY,,Z' + command + b'\x1bH\x1bf' + token.encode()
                    with (root / 'wire.bin').open('ab') as stream:
                        stream.write(wire)
                    peer.sendall(wire)
                    check(client.wait_text(token), actual, entry, status, marker, line, seeded, token)
                peer.settimeout(0.03)
                try:
                    reply = peer.recv(1)
                except socket.timeout:
                    pass
                else:
                    raise AssertionError(f'unexpected IGS reply: {reply!r}')
                peer.sendall(b'G#T 1,7,')
                peer.shutdown(socket.SHUT_RDWR)
                client.wait_text('Disconnected: Enter exits, F1 details')
                client.key('enter')
            client.wait_text('Directory: Name')
            client.key('enter')
            with listener.accept()[0] as peer:
                client.wait_for(lambda s: s.application.get('connected') == 1)
                client.resize(960, 720)
                peer.sendall(b'G#g>2:M 4:\x1bf\x1bE\x1bY,,Z\x1bHFRESH')
                check(client.wait_text('FRESH'), entry, entry, status, (1,11), (1,1,0,0,0xffff), True, 'FRESH')
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
