#!/usr/bin/env python3
"""H's three attributes and lifetime, with coherent literal scene checks.

H itself draws nothing. Primitive-specific hollow/perimeter raster behavior
belongs to the drawing command tests when those steps are implemented.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import socket
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'testing'))
from syncterm_fixture import fixture
from st_igs_scaling_test import check as check_scene


def check(frame, actual, entry, status, triple, seeded, token):
    check_scene(frame, actual, entry, 2, entry, status, seeded, token)
    state = frame.application
    assert (state['cterm.igs_fill_interior'], state['cterm.igs_writing_mode'],
            state['cterm.igs_fill_perimeter']) == triple


def run(executable, reports, entry, encoding, status):
    screen = ('AtariST40x25', 'AtariST80x25', 'AtariST80x25Mono')[entry]
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        listener.listen(2)
        listener.settimeout(15)
        entries = ('[IGS hollow]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                   f'Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\n'
                   f'ScreenMode={screen}\nFont=Atari ST\nEncoding={encoding}\n'
                   f'NoStatus={str(not status).lower()}\n')
        with fixture(executable, entries=entries, settings='AudioModes=0\n',
                     reports=reports, quiet=False) as (client, root):
            client.wait_text('IGS hollow')
            client.key('enter')
            with listener.accept()[0] as peer:
                client.wait_for(lambda s: s.application.get('connected') == 1)
                client.resize(960, 720)
                peer.sendall(b'G#g>2:M 3:')
                other = 1 - entry if entry != 2 else 2
                steps = (
                    (b'G#H 1:', entry, (0,2,True), True),
                    (b'G#M 4:', entry, (0,4,True), True),
                    (b'G#H 0:', entry, (1,1,False), True),
                    (b'G#M 3:', entry, (1,3,False), True),
                    (b'G#H 9999:', entry, (0,2,True), True),
                    (b'G#I 2:', entry, (1,1,True), True),
                    (b'G#H>0:I 2:', entry, (1,1,False), True),
                    (b'G#H>1:I 0:', entry, (1,1,True), True),
                    (b'G#H>1:I 5:', entry, (0,2,True), entry == 2),
                    (f'G#R {other},0:'.encode(), other, (0,2,True), entry == 2),
                    (b'G#H 0:', other, (1,1,False), True),
                )
                for number, (command, actual, triple, seeded) in enumerate(steps):
                    token = f'HOLLOW{number:02d}'
                    wire = b'\x1bf\x1bE\x1bY,,Z' + command + b'\x1bH\x1bf' + token.encode()
                    with (root / 'wire.bin').open('ab') as stream:
                        stream.write(wire)
                    peer.sendall(wire)
                    check(client.wait_text(token), actual, entry, status, triple, seeded, token)
                peer.settimeout(0.03)
                try:
                    reply = peer.recv(1)
                except socket.timeout:
                    pass
                else:
                    raise AssertionError(f'unexpected IGS reply: {reply!r}')
                peer.sendall(b'G#H 1')
                peer.shutdown(socket.SHUT_RDWR)
                client.wait_text('Disconnected: Enter exits, F1 details')
                client.key('enter')
            client.wait_text('Directory: Name')
            client.key('enter')
            with listener.accept()[0] as peer:
                client.wait_for(lambda s: s.application.get('connected') == 1)
                client.resize(960, 720)
                peer.sendall(b'G#g 2:\x1bf\x1bE\x1bY,,Z\x1bHFRESH')
                check(client.wait_text('FRESH'), entry, entry, status, (1,1,True), True, 'FRESH')
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
