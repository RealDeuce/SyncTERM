#!/usr/bin/env python3
"""M attribute selection and lifetime, with independent coherent scenes.

M itself draws nothing. Reuse the literal VT52 Z scene assertion; pixel
compositing belongs to the drawing command tests as they are implemented.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import socket
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'testing'))
from syncterm_fixture import fixture
from st_igs_scaling_test import check as check_scene


def check(frame, actual, entry, status, mode, seeded, token):
    check_scene(frame, actual, entry, 2, entry, status, seeded, token)
    assert frame.application['cterm.igs_writing_mode'] == mode


def run(executable, reports, entry, encoding, status):
    screen = ('AtariST40x25', 'AtariST80x25', 'AtariST80x25Mono')[entry]
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        listener.listen(2)
        listener.settimeout(15)
        entries = ('[IGS writing mode]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                   f'Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\n'
                   f'ScreenMode={screen}\nFont=Atari ST\nEncoding={encoding}\n'
                   f'NoStatus={str(not status).lower()}\n')
        with fixture(executable, entries=entries, settings='AudioModes=0\n',
                     reports=reports, quiet=False) as (client, root):
            client.wait_text('IGS writing mode')
            client.key('enter')
            with listener.accept()[0] as peer:
                client.wait_for(lambda s: s.application.get('connected') == 1)
                client.resize(960, 720)
                peer.sendall(b'G#g 2:')
                other = 1 - entry if entry != 2 else 2
                steps = (
                    (b'G#M 1:', entry, 1, True),
                    (b'G#M 2:', entry, 2, True),
                    (b'G#M 3:', entry, 3, True),
                    (b'G#M 4:', entry, 4, True),
                    (b'G#M 0:', entry, 1, True),
                    (b'G#M 5:', entry, 1, True),
                    (b'G#M 9999:', entry, 1, True),
                    (b'G#M>4:I 1:', entry, 4, True),
                    (b'G#I 2:', entry, 1, True),
                    (b'G#M>3:I 0:', entry, 1, True),
                    (b'G#M>2:I 5:', entry, 2, entry == 2),
                    (f'G#R>{other},0:M 4:'.encode(), other, 4, entry == 2),
                )
                for number, (command, actual, mode, seeded) in enumerate(steps):
                    token = f'MODE{number:02d}'
                    wire = b'\x1bf\x1bE\x1bY,,Z' + command + b'\x1bH\x1bf' + token.encode()
                    with (root / 'wire.bin').open('ab') as stream:
                        stream.write(wire)
                    peer.sendall(wire)
                    check(client.wait_text(token), actual, entry, status, mode, seeded, token)
                peer.settimeout(0.03)
                try:
                    reply = peer.recv(1)
                except socket.timeout:
                    pass
                else:
                    raise AssertionError(f'unexpected IGS reply: {reply!r}')
                peer.sendall(b'G#M 2')
                peer.shutdown(socket.SHUT_RDWR)
                client.wait_text('Disconnected: Enter exits, F1 details')
                client.key('enter')
            client.wait_text('Directory: Name')
            client.key('enter')
            with listener.accept()[0] as peer:
                client.wait_for(lambda s: s.application.get('connected') == 1)
                client.resize(960, 720)
                peer.sendall(b'G#g 2:\x1bf\x1bE\x1bY,,Z\x1bHFRESH')
                check(client.wait_text('FRESH'), entry, entry, status, 1, True, 'FRESH')
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
