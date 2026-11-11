#!/usr/bin/env python3
"""X5 state and exact raw flow replies with coherent unchanged scenes."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import socket
from st_igs_cursor_test import check, fixture

CAPTURES = 0


def run(executable, reports, resolution, encoding, status):
    global CAPTURES
    screen = ('AtariST40x25', 'AtariST80x25', 'AtariST80x25Mono')[resolution]
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0)); listener.listen(2); listener.settimeout(15)
        entries = ('[IGS flow control]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                   f'Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\nScreenMode={screen}\n'
                   f'Font=Atari ST\nEncoding={encoding}\nNoStatus={str(not status).lower()}\n')
        with fixture(executable, entries=entries, settings='AudioModes=0\nMidiModes=0\nBlockyScaling=true\n',
                     reports=reports, quiet=False) as (client, root):
            client.wait_text('IGS flow control'); client.key('enter'); marker = 1000

            def send(peer, wire):
                with (root/'wire.bin').open('ab') as log:
                    log.write(wire)
                peer.sendall(wire)

            def inspect(frame, values):
                global CAPTURES
                check(frame, resolution, status, 4, 'A', False, False, False)
                s = frame.application
                assert tuple(s[f'cterm.igs_flow_{name}'] for name in
                             ('mode', 'on', 'off', 'on_count', 'off_count')) == values
                assert not s['cterm.igs_input_paused']
                CAPTURES += 1

            def scene(peer, wire, values, reply=b''):
                nonlocal marker
                marker += 1
                send(peer, wire+f'G#X 10,{marker},777:'.encode())
                frame = client.wait_for(lambda f: f.application.get('cterm.igs_draw_x') == marker)
                inspect(frame, values)
                if reply:
                    peer.settimeout(3); actual = b''
                    while len(actual) < len(reply):
                        actual += peer.recv(len(reply)-len(actual))
                    assert actual == reply, actual
                return frame

            def setup(peer):
                client.wait_for(lambda f: f.application.get('connected') == 1)
                client.resize(1280, 960)
                scene(peer, b'G#c>1,3:c 0,0:\x1bE\x1bfG#p 3,2:A', (1,17,19,1,1))

            def disconnect(peer):
                peer.shutdown(socket.SHUT_RDWR)
                client.wait_text('Disconnected: Enter exits, F1 details')
                client.key('enter'); client.wait_text('Directory: Name')

            with listener.accept()[0] as peer:
                setup(peer)
                scene(peer, b'G#X 5,2,511,2:', (2,255,19,2,1))
                scene(peer, b'G#X 5,3,256,3:', (3,255,0,2,3))
                # Reply bytes stay raw under every wire encoding, including NUL/FF.
                scene(peer, b'G#N 4,0:', (3,255,0,2,3), b'\x00\x00\x00\xff\xff')
                scene(peer, b'G#X>5,0:N 4,0:', (0,255,0,2,3))
                scene(peer, b'G#X>5,1:N 4,0:', (1,255,0,2,3), b'\x00\x00\x00\xff\xff')
                scene(peer, b'G#X 5,9999:', (9999,17,19,1,1))
                scene(peer, b'G#N 4,0:', (9999,17,19,1,1), b'\x13\x11')
                scene(peer, b'G#X 5,2,17,0:', (2,17,19,0,1))
                scene(peer, b'G#X>5,3,19,0:N 4,0:', (3,17,19,0,0))
                peer.settimeout(0.03)
                try:
                    extra = peer.recv(1)
                except socket.timeout:
                    pass
                else:
                    raise AssertionError(f'unexpected flow output: {extra!r}')
                # Exercise the real response transport beyond a small queue,
                # using an independently known signed-word product.
                scene(peer, b'G#X 13,3,181,181,0:', (3,17,19,0,0))
                scene(peer, b'G#X 5,2,17,0]:', (2,17,19,32761,0))
                scene(peer, b'G#N 4,0:', (2,17,19,32761,0), b'\x11'*32761)
                # Partial custom-character/count acquisition survives EOF; the
                # new terminal starts with the normal default configuration.
                send(peer, b'G#X 5,2,33,')
                frame = client.wait_for(lambda f: f.application.get('cterm.igs_flow_on') == 33)
                inspect(frame, (2,33,19,32761,0))
                disconnect(peer)
            client.key('enter')
            with listener.accept()[0] as peer:
                setup(peer)
                scene(peer, b'G#N 4,0:', (1,17,19,1,1), b'\x13\x11')
                disconnect(peer)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('executable')
    parser.add_argument('--reports', default='reports/test-control'); args = parser.parse_args()
    for resolution in range(3):
        for encoding in ('ATARI-ST', 'CP437', 'UTF-8'):
            for status in (False, True):
                run(args.executable, args.reports, resolution, encoding, status)
    assert CAPTURES == 288, CAPTURES
