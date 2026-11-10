#!/usr/bin/env python3
"""C independent pens and validation, with coherent literal scene checks.

The attribute command draws nothing. Primitive pen-to-pixel composition is
tested with the drawing commands when their implementation steps complete.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import socket
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'testing'))
from syncterm_fixture import fixture
from st_igs_scaling_test import check as check_scene


def check(frame, actual, entry, status, pens, cache, seeded, token):
    check_scene(frame, actual, entry, 1, entry, status, seeded, token)
    state = frame.application
    assert state['cterm.igs_writing_mode'] == 4
    assert tuple(state[f'cterm.igs_{kind}_pen'] for kind in ('marker', 'line', 'fill', 'text')) == pens
    assert state['cterm.igs_marker_pen_cache'] == cache


def run(executable, reports, entry, encoding, status):
    screen = ('AtariST40x25', 'AtariST80x25', 'AtariST80x25Mono')[entry]
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        listener.listen(2)
        listener.settimeout(15)
        entries = ('[IGS pens]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                   f'Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\n'
                   f'ScreenMode={screen}\nFont=Atari ST\nEncoding={encoding}\n'
                   f'NoStatus={str(not status).lower()}\n')
        with fixture(executable, entries=entries, settings='AudioModes=0\n',
                     reports=reports, quiet=False) as (client, root):
            client.wait_text('IGS pens')
            client.key('enter')
            with listener.accept()[0] as peer:
                client.wait_for(lambda s: s.application.get('connected') == 1)
                client.resize(960, 720)
                peer.sendall(b'G#g>1:M 4:')
                other = 1 - entry if entry != 2 else 2
                chosen = ((15,3,2,0), (1,3,2,0), (1,1,1,0))[entry]
                steps = (
                    (b'G#C 0,15:', entry, (chosen[0],1,1,1), 15, True),
                    (b'G#C 1,3:', entry, (chosen[0],chosen[1],1,1), 15, True),
                    (b'G#C 2,2:', entry, (chosen[0],chosen[1],chosen[2],1), 15, True),
                    (b'G#C 3,0:', entry, chosen, 15, True),
                    (b'G#C 9,16:', entry, (0,chosen[1],chosen[2],0), 0, True),
                    (b'G#C 2,15:', entry, ((0,3,15,0), (0,3,1,0), (0,1,1,0))[entry], 0, True),
                    (b'G#I 1:', entry, ((0,3,15,0), (0,3,1,0), (0,1,1,0))[entry], 0, True),
                    (b'G#I>2:M 4:', entry, (1,1,1,1), 1, True),
                    (b'G#C>0,15:C>1,3:C>2,2:C 3,0:', entry, chosen, 15, True),
                    (b'G#I 5:', entry, chosen, 15, entry == 2),
                    (f'G#R {other},0:'.encode(), other, chosen, 15, entry == 2),
                    (b'G#C 0,15:', other, ((1,15,1)[entry], *chosen[1:]), 15, True),
                )
                for number, (command, actual, pens, cache, seeded) in enumerate(steps):
                    token = f'PENS{number:02d}'
                    wire = b'\x1bf\x1bE\x1bY,,Z' + command + b'\x1bH\x1bf' + token.encode()
                    with (root / 'wire.bin').open('ab') as stream:
                        stream.write(wire)
                    peer.sendall(wire)
                    check(client.wait_text(token), actual, entry, status, pens, cache, seeded, token)
                peer.settimeout(0.03)
                try:
                    reply = peer.recv(1)
                except socket.timeout:
                    pass
                else:
                    raise AssertionError(f'unexpected IGS reply: {reply!r}')
                peer.sendall(b'G#C 0,')
                peer.shutdown(socket.SHUT_RDWR)
                client.wait_text('Disconnected: Enter exits, F1 details')
                client.key('enter')
            client.wait_text('Directory: Name')
            client.key('enter')
            with listener.accept()[0] as peer:
                client.wait_for(lambda s: s.application.get('connected') == 1)
                client.resize(960, 720)
                peer.sendall(b'G#g>1:M 4:\x1bf\x1bE\x1bY,,Z\x1bHFRESH')
                check(client.wait_text('FRESH'), entry, entry, status, (1,1,1,1), 1, True, 'FRESH')
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
