#!/usr/bin/env python3
"""A fill/pattern state with coherent independent literal scene expectations.

Random rows derive from reference 124's seed-1 stream and reference 130's
sixteen row updates. No expected data is produced by SyncTERM.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import socket
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'testing'))
from syncterm_fixture import fixture
from st_igs_scaling_test import check as check_scene

DEFAULT = (0,0,0x00c0,0x01e0,0x03b0,0x07b8,0x0fbc,0x0e0c,
           0x1f5e,0x1ffe,0x1f8e,0x1e06,0x1c06,0x1802,0x1000,0)
FIRST = (0xfa4f,0x6e42,0xcb83,0xd43e,0x6197,0x01da,0xf18b,0xee16,
         0xa85f,0xb7f2,0xeb13,0x666e,0x96a7,0xa88a,0x401b,0x9546)
THIRD = (0xb2dd,0xcd7c,0x0d61,0xf4e8,0xac45,0xe674,0xb889,0xd520,
         0x332d,0xbbec,0x3531,0x1dd8,0x2f95,0xc5e4,0x2b59,0x8710)


def check(frame, actual, entry, status, triple, rows, seeds, seeded, token):
    check_scene(frame, actual, entry, 2, entry, status, seeded, token)
    state = frame.application
    assert (state['cterm.igs_fill_interior'], state['cterm.igs_fill_index'],
            state['cterm.igs_fill_perimeter']) == triple
    assert state['cterm.igs_writing_mode'] == 4 and state['cterm.igs_fill_pen'] == 1
    assert tuple(state[f'cterm.igs_user_fill_{i}'] for i in range(16)) == rows
    assert (state['cterm.igs_random_seed'], state['cterm.igs_pattern_seed']) == seeds


def run(executable, reports, entry, encoding, status):
    screen = ('AtariST40x25', 'AtariST80x25', 'AtariST80x25Mono')[entry]
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        listener.listen(2)
        listener.settimeout(15)
        entries = ('[IGS fill]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                   f'Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\n'
                   f'ScreenMode={screen}\nFont=Atari ST\nEncoding={encoding}\n'
                   f'NoStatus={str(not status).lower()}\n')
        with fixture(executable, entries=entries, settings='AudioModes=0\n',
                     reports=reports, quiet=False) as (client, root):
            client.wait_text('IGS fill')
            client.key('enter')
            with listener.accept()[0] as peer:
                client.wait_for(lambda s: s.application.get('connected') == 1)
                client.resize(960, 720)
                peer.sendall(b'G#g>2:M 4:')
                other = 1 - entry if entry != 2 else 2
                first_seed, second_seed, third_seed = (0xbb40e62e,0x762e9546), (0x5eb5ca17,0x5eb5ca), (0x4c45300c,0x5f9b8710)
                steps = (
                    (b'G#A 2,24,1:', entry, (2,24,True), (0xffff,) * 16, (1,0), True),
                    (b'G#A 3,13,0:', entry, (3,1,False), (0xffff,) * 16, (1,0), True),
                    (b'G#A 2,24,1:', entry, (2,24,True), (0xffff,) * 16, (1,0), True),
                    (b'G#A 4,7,0:', entry, (4,24,False), (0,) * 16, (1,0), True),
                    (b'G#A 4,0,1:', entry, (4,24,True), DEFAULT, (1,0), True),
                    (b'G#A 4,8,0:', entry, (4,24,False), FIRST, first_seed, True),
                    (b'G#I>2:M 4:', entry, (1,1,False), FIRST, first_seed, True),
                    (b'G#A 4,0,1:', entry, (4,1,True), DEFAULT, first_seed, True),
                    (b'G#A 4,9,1:', entry, (4,1,True), DEFAULT, second_seed, True),
                    (b'G#A 4,8,0:', entry, (4,1,False), THIRD, third_seed, True),
                    (f'G#R {other},0:'.encode(), other, (4,1,False), THIRD, third_seed, entry == 2),
                    (b'G#I 5:', entry, (4,1,False), THIRD, third_seed, entry == 2),
                    (b'G#I>0:M 4:', entry, (1,1,False), THIRD, third_seed, True),
                    (b'G#A 4,0,1:', entry, (4,1,True), DEFAULT, third_seed, True),
                    (b'G#A 9999,9999,0:', entry, (0,1,False), DEFAULT, third_seed, True),
                )
                for number, (command, actual, triple, rows, seeds, seeded) in enumerate(steps):
                    token = f'FILL{number:02d}'
                    wire = b'\x1bf\x1bE\x1bY,,Z' + command + b'\x1bH\x1bf' + token.encode()
                    with (root / 'wire.bin').open('ab') as stream:
                        stream.write(wire)
                    peer.sendall(wire)
                    check(client.wait_text(token), actual, entry, status, triple, rows, seeds, seeded, token)
                peer.settimeout(0.03)
                try:
                    reply = peer.recv(1)
                except socket.timeout:
                    pass
                else:
                    raise AssertionError(f'unexpected IGS reply: {reply!r}')
                peer.sendall(b'G#A 4,9,')
                peer.shutdown(socket.SHUT_RDWR)
                client.wait_text('Disconnected: Enter exits, F1 details')
                client.key('enter')
            client.wait_text('Directory: Name')
            client.key('enter')
            with listener.accept()[0] as peer:
                client.wait_for(lambda s: s.application.get('connected') == 1)
                client.resize(960, 720)
                peer.sendall(b'G#g>2:M 4:\x1bf\x1bE\x1bY,,Z\x1bHFRESH')
                check(client.wait_text('FRESH'), entry, entry, status, (1,1,True), (0xffff,) * 16, (1,0), True, 'FRESH')
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
