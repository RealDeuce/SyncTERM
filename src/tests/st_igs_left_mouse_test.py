#!/usr/bin/env python3
"""X6 release bytes, X3 handoff and input ownership in coherent captures."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import socket
from st_igs_cursor_test import check, fixture
from st_igs_macro_test import SCRIPT, byte_hash

CAPTURES = 0


def run(executable, reports, resolution, encoding, status):
    global CAPTURES
    screen = ('AtariST40x25', 'AtariST80x25', 'AtariST80x25Mono')[resolution]
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0)); listener.listen(2); listener.settimeout(15)
        entries = ('[IGS left mouse]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                   f'Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\nScreenMode={screen}\n'
                   f'Font=Atari ST\nEncoding={encoding}\nNoStatus={str(not status).lower()}\n')
        with fixture(executable, entries=entries, settings='AudioModes=0\nMidiModes=0\nBlockyScaling=true\n',
                     script=SCRIPT, reports=reports, quiet=False) as (client, root):
            client.wait_text('IGS left mouse'); client.key('enter'); marker = 1000
            left_mode = 0
            macro = [0, 0, 0]
            macro_bytes = bytearray(80)

            def send(peer, wire):
                with (root/'wire.bin').open('ab') as log:
                    log.write(wire)
                peer.sendall(wire)

            def inspect(frame, left=False, right=False):
                global CAPTURES
                check(frame, resolution, status, 4, 'A', False, False, False)
                s = frame.application
                assert s['cterm.igs_left_mode'] == left_mode
                assert s['cterm.igs_left_held'] == left
                assert s['cterm.igs_macro_held'] == right
                assert s['cterm.igs_input_paused'] == (left or right)
                assert tuple(s[f'cterm.igs_macro_{name}'] for name in ('enabled', 'length', 'cr')) == tuple(macro)
                assert s['cterm.igs_macro_buffer'] == byte_hash(macro_bytes)
                CAPTURES += 1

            def scene(peer, wire=b''):
                nonlocal marker
                marker += 1; send(peer, wire+f'G#X 10,{marker},777:'.encode())
                frame = client.wait_for(lambda f: f.application.get('cterm.igs_draw_x') == marker)
                inspect(frame)
                return frame

            def read(peer, expected):
                actual = b''
                while len(actual) < len(expected):
                    part = peer.recv(len(expected)-len(actual))
                    assert part, actual
                    actual += part
                assert actual == expected, (actual, expected)

            def probe(peer, prefix=b'', disabled=False):
                # The explicit Wren hook is an output fence even during an
                # emulator-owned hold, proving no premature/duplicate bytes.
                mode = 3 if left_mode or macro[0] else 0
                client.key('f8')
                read(peer, prefix+f'STATE {mode} {str(disabled).lower()} false\n'.encode())

            def mouse(buttons, action=1, px=100, py=100, wheel_y=0):
                frame = client.capture(); m = frame.metadata
                x = m['destination_x']+(2*px+1)*m['destination_width']//(2*m['plane_width'])
                y = m['destination_y']+(2*py+1)*m['destination_height']//(2*m['plane_height'])
                before = m['events_dequeued']
                client.mouse(x, y, action=action, buttons=buttons, wheel_y=wheel_y)
                return client.wait_for(lambda f: f.metadata['events_dequeued'] > before)

            def setup(peer):
                client.wait_for(lambda f: f.application.get('connected') == 1)
                client.resize(1280, 960)
                scene(peer, b'G#c>1,3:c 0,0:\x1bE\x1bfG#p 3,2:A')

            def disconnect(peer):
                peer.shutdown(socket.SHUT_RDWR)
                client.wait_text('Disconnected: Enter exits, F1 details')
                client.key('enter'); client.wait_text('Directory: Name')

            with listener.accept()[0] as peer:
                peer.settimeout(5); setup(peer); probe(peer)
                for value in (1, 2, 255, 256, 258):
                    left_mode = value & 255
                    scene(peer, f'G#X 6,{value}:'.encode())
                    inspect(mouse(1), left=bool(left_mode)); probe(peer)
                    mouse(1, action=0); probe(peer)
                    inspect(mouse(0))
                    probe(peer, b'\r\n' if left_mode == 2 else b'\r' if left_mode else b'')
                left_mode = 2; scene(peer, b'G#X 6,2:')
                inspect(mouse(5)); probe(peer); mouse(0)  # initial both does nothing
                inspect(mouse(1), left=True)
                inspect(mouse(5)); probe(peer, b'\r\n'); mouse(0)  # both ends X6 hold
                # A final right-only sample immediately enters separate X3.
                macro[:] = [1, 4, 1]; macro_bytes[:4] = b'\0\xffG#'
                scene(peer, b'G#X 3,2,1,1,4:\0\xffG#!')
                inspect(mouse(1), left=True); probe(peer)
                inspect(mouse(4), right=True); probe(peer, b'\r\n\0\xffG#\r')
                mouse(4, action=0); inspect(mouse(1), right=True); probe(peer)
                inspect(mouse(0)); probe(peer)
                # Serial suffix and original Unicode keys wait until release.
                inspect(mouse(1), left=True)
                marker += 1; send(peer, f'G#X 10,{marker},777:'.encode())
                client.text('é')
                frame = client.wait_for(lambda f: f.application.get('cterm.igs_music_keys') == 1)
                inspect(frame, left=True)
                assert frame.application['cterm.igs_draw_x'] == marker-1
                mouse(0)
                frame = client.wait_for(lambda f: f.application.get('cterm.igs_draw_x') == marker)
                inspect(frame); probe(peer, b'\r\n'+(b'\xc3\xa9' if encoding == 'UTF-8' else b'\x82'))
                # Disable from the remote end is likewise unread during a hold.
                inspect(mouse(1), left=True)
                marker += 1; send(peer, f'G#X 6,0:G#X 10,{marker},777:'.encode()); probe(peer)
                mouse(0); left_mode = 0
                frame = client.wait_for(lambda f: f.application.get('cterm.igs_draw_x') == marker)
                inspect(frame); probe(peer, b'\r\n')
                left_mode = 2; scene(peer, b'G#X 6,2:')
                client.command_key(24); probe(peer, disabled=True)
                inspect(mouse(1)); inspect(mouse(0)); probe(peer, disabled=True)
                client.command_key(24); probe(peer)
                if status:
                    py = 399 if resolution == 2 else 199
                    mouse(1, py=py); mouse(0, py=py); scene(peer); probe(peer)
                mouse(0, action=2, wheel_y=1)
                client.wait_text('Scrollback'); client.key('escape'); scene(peer); probe(peer)
                # EOF cancels a pending click without synthesizing a release CR.
                inspect(mouse(1), left=True); probe(peer); disconnect(peer)
            client.key('enter'); left_mode = 0; macro[:] = [0, 0, 0]; macro_bytes[:] = bytes(80)
            with listener.accept()[0] as peer:
                peer.settimeout(5); setup(peer); probe(peer)
                send(peer, b'G#X 6,')
                frame = client.wait_for(lambda f: f.application.get('cterm.igs_left_configuring') == 1)
                inspect(frame); disconnect(peer)
            assert b'[wren' not in client.stderr, bytes(client.stderr)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('executable')
    parser.add_argument('--reports', default='reports/test-control'); args = parser.parse_args()
    for resolution in range(3):
        for encoding in ('ATARI-ST', 'CP437', 'UTF-8'):
            for status in (False, True):
                run(args.executable, args.reports, resolution, encoding, status)
    assert CAPTURES == 675, CAPTURES
    print(f'{CAPTURES} coherent X6 captures', flush=True)
