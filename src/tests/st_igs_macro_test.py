#!/usr/bin/env python3
"""X3 raw macro bytes, hold ownership, Wren routing and coherent scenes."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import socket
from st_igs_cursor_test import check, fixture

SCRIPT = r'''
import "syncterm" for Hook, Key, CTerm, Conn
Hook.onKey(Key.f8) {|event|
  Conn.sendBytes("STATE %(CTerm.mouseMode) %(CTerm.mouseDisabled) %(CTerm.mouseSgrPixels)\n")
  return true
}
'''
CAPTURES = 0


def byte_hash(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value


def run(executable, reports, resolution, encoding, status):
    global CAPTURES
    screen = ('AtariST40x25', 'AtariST80x25', 'AtariST80x25Mono')[resolution]
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0)); listener.listen(2); listener.settimeout(15)
        entries = ('[IGS macro]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                   f'Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\nScreenMode={screen}\n'
                   f'Font=Atari ST\nEncoding={encoding}\nNoStatus={str(not status).lower()}\n')
        with fixture(executable, entries=entries, settings='AudioModes=0\nMidiModes=0\nBlockyScaling=true\n',
                     script=SCRIPT, reports=reports, quiet=False) as (client, root):
            client.wait_text('IGS macro'); client.key('enter'); marker = 1000
            buffer = bytearray(80)
            values = [0, 0, 0]

            def send(peer, wire):
                with (root/'wire.bin').open('ab') as log:
                    log.write(wire)
                peer.sendall(wire)

            def inspect(frame, held=False, loading=False, received=None):
                global CAPTURES
                check(frame, resolution, status, 4, 'A', False, False, False)
                s = frame.application
                assert tuple(s[f'cterm.igs_macro_{name}'] for name in ('enabled', 'length', 'cr')) == tuple(values)
                assert s['cterm.igs_macro_buffer'] == byte_hash(buffer)
                assert s['cterm.igs_macro_held'] == held
                assert s['cterm.igs_macro_loading'] == loading
                assert s['cterm.igs_input_paused'] == held
                if received is not None:
                    assert s['cterm.igs_macro_received'] == received
                CAPTURES += 1

            def scene(peer, wire=b''):
                nonlocal marker
                marker += 1
                send(peer, wire+f'G#X 10,{marker},777:'.encode())
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

            def probe(peer, mode, disabled=False):
                client.key('f8')
                read(peer, f'STATE {mode} {str(disabled).lower()} false\n'.encode())

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
                peer.settimeout(5); setup(peer); probe(peer, 0)
                # Opaque payload including NUL/FF/attention/control bytes.
                payload = b'\0\xffG#:\r\n\x11\x13\x03\x18@'
                buffer[:len(payload)] = payload; values[:] = [1, len(payload), 1]
                scene(peer, f'G#X 3,2,1,1,{len(payload)}:'.encode()+payload+b'!')
                probe(peer, 3)
                inspect(mouse(5)); inspect(mouse(0))  # both buttons do not fire
                inspect(mouse(4), held=True); read(peer, payload+b'\r')
                inspect(mouse(4, action=0), held=True)  # held movement does not repeat
                inspect(mouse(1), held=True); inspect(mouse(5), held=True)
                # RX and keys wait for all-buttons-up, then preserve text encoding.
                marker += 1
                send(peer, f'G#X 10,{marker},777:'.encode())
                client.text('é')
                frame = client.wait_for(lambda f: f.application.get('cterm.igs_music_keys') == 1)
                inspect(frame, held=True)
                assert frame.application['cterm.igs_draw_x'] == marker-1
                mouse(0)
                frame = client.wait_for(lambda f: f.application.get('cterm.igs_draw_x') == marker)
                inspect(frame)
                read(peer, b'\xc3\xa9' if encoding == 'UTF-8' else b'\x82')
                # A short disabled load preserves the old suffix, discards any byte.
                buffer[0] = 0xff; values[:] = [0, 1, 0]
                scene(peer, b'G#X 3,2,0,0,1:\xff\0'); probe(peer, 0)
                values[:] = [1, 1, 0]; scene(peer, b'G#X 3,1,0:'); probe(peer, 3)
                inspect(mouse(4), held=True); read(peer, b'\xff'); mouse(0); scene(peer)
                # Local mouse-disable gate and status/UI clicks cannot arm a macro.
                client.command_key(24); probe(peer, 3, True)
                mouse(4); mouse(0); scene(peer)
                client.command_key(24); probe(peer, 3)
                if status:
                    mouse(4, py=399 if resolution == 2 else 199)
                    mouse(0, py=399 if resolution == 2 else 199)
                    scene(peer)
                # Wheel-up still belongs to local scrollback for protocol mode3.
                mouse(0, action=2, wheel_y=1)
                client.wait_text('Scrollback'); client.key('escape'); scene(peer)
                # Incremental raw stores are visible, EOF retains them without fill.
                values[:] = [0, 3, 7]
                send(peer, b'G#X 3,2,0,7,3:z'); buffer[0] = ord('z')
                frame = client.wait_for(lambda f: f.application.get('cterm.igs_macro_loading') == 1
                                        and f.application.get('cterm.igs_macro_received') == 1)
                inspect(frame, loading=True, received=1)
                client.text('K')
                frame = client.wait_for(lambda f: f.application.get('cterm.igs_music_keys') == 1)
                inspect(frame, loading=True, received=1)
                send(peer, b'12!'); buffer[1:3] = b'12'
                scene(peer); read(peer, b'K')
                send(peer, b'G#X 3,2,0,7,3:y'); buffer[0] = ord('y')
                frame = client.wait_for(lambda f: f.application.get('cterm.igs_macro_loading') == 1
                                        and f.application.get('cterm.igs_macro_received') == 1)
                inspect(frame, loading=True, received=1)
                disconnect(peer)
            client.key('enter'); buffer[:] = bytes(80); values[:] = [0, 0, 0]
            with listener.accept()[0] as peer:
                peer.settimeout(5); setup(peer); probe(peer, 0)
                # A zero-length initial macro can append CR; disconnect while held.
                values[:] = [1, 0, 1]; scene(peer, b'G#X 3,1,1:')
                inspect(mouse(4), held=True); read(peer, b'\r')
                disconnect(peer)
            assert b'[wren' not in client.stderr, bytes(client.stderr)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('executable')
    parser.add_argument('--reports', default='reports/test-control'); args = parser.parse_args()
    for resolution in range(3):
        for encoding in ('ATARI-ST', 'CP437', 'UTF-8'):
            for status in (False, True):
                run(args.executable, args.reports, resolution, encoding, status)
    assert CAPTURES == 423, CAPTURES
    print(f'{CAPTURES} coherent X3 captures', flush=True)
