#!/usr/bin/env python3
"""X4 retained rectangles/payloads with independent coherent screen checks."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import socket
from st_igs_cursor_test import check, fixture
from st_igs_macro_test import byte_hash

CAPTURES = 0


def run(executable, reports, resolution, encoding, status):
    global CAPTURES
    screen = ('AtariST40x25', 'AtariST80x25', 'AtariST80x25Mono')[resolution]
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0)); listener.listen(2); listener.settimeout(15)
        entries = ('[IGS zones]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                   f'Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\nScreenMode={screen}\n'
                   f'Font=Atari ST\nEncoding={encoding}\nNoStatus={str(not status).lower()}\n')
        with fixture(executable, entries=entries, settings='AudioModes=0\nMidiModes=0\nBlockyScaling=true\n',
                     reports=reports, quiet=False) as (client, root):
            client.wait_text('IGS zones'); client.key('enter'); marker = 1000
            rectangles = [[0]*4 for _ in range(48)]
            payloads = [bytearray(81) for _ in range(48)]
            loopback = 0
            macro = [0, 0, 0]
            macro_bytes = bytearray(80)

            def send(peer, wire):
                with (root/'wire.bin').open('ab') as log:
                    log.write(wire)
                peer.sendall(wire)

            def inspect(frame, *, loading=False, received=None, held=False):
                global CAPTURES
                check(frame, resolution, status, 4, 'A', False, False, False)
                s = frame.application
                words = b''.join((word & 65535).to_bytes(2, 'big') for rect in rectangles for word in rect)
                assert s['cterm.igs_zone_rectangles'] == byte_hash(words)
                assert s['cterm.igs_zone_payloads'] == byte_hash(b''.join(payloads))
                assert s['cterm.igs_zone_loopback'] == loopback
                assert s['cterm.igs_zone_loading'] == loading
                assert s['cterm.igs_input_paused'] == held
                if received is not None:
                    assert s['cterm.igs_zone_received'] == received
                assert tuple(s[f'cterm.igs_macro_{name}'] for name in ('enabled', 'length', 'cr')) == tuple(macro)
                assert s['cterm.igs_macro_buffer'] == byte_hash(macro_bytes)
                assert not s['cterm.igs_macro_loading']
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

            def define_wire(zone, rect, payload, length=None, extra=b'!'):
                stored = min(zone, 47)
                rectangles[stored][:] = rect
                payloads[stored][0] = len(payload)
                payloads[stored][1:len(payload)+1] = payload
                # '-' is an IGS loop expression, not a decimal sign. Produce
                # negative signed words through actual X13 subtraction/lookup.
                preamble = bytearray()
                values = []
                for i, value in enumerate([zone, *rect, len(payload) if length is None else length]):
                    if value < 0:
                        slot = 120+i
                        preamble.extend(f'G#X 13,2,0,{-value},{slot}:'.encode())
                        values.append(f'{slot}]')
                    else:
                        values.append(str(value))
                return bytes(preamble)+f'G#X 4,{",".join(values)}:'.encode()+payload+extra

            def setup(peer):
                client.wait_for(lambda f: f.application.get('connected') == 1)
                client.resize(1280, 960)
                scene(peer, b'G#c>1,3:c 0,0:\x1bE\x1bfG#p 3,2:A')

            def disconnect(peer):
                peer.shutdown(socket.SHUT_RDWR)
                client.wait_text('Disconnected: Enter exits, F1 details')
                client.key('enter'); client.wait_text('Directory: Name')

            with listener.accept()[0] as peer:
                peer.settimeout(5); setup(peer)
                macro[:] = [0, 1, 1]; macro_bytes[0] = ord('Z')
                scene(peer, b'G#X 3,2,0,1,1:Z!')
                # All48 slots have distinct retained bounds and opaque bytes.
                batch = bytearray()
                for zone in range(48):
                    batch.extend(define_wire(zone, [zone, zone+1, 100+zone, 200+zone],
                                             bytes((zone, 0, 255, 58, zone ^ 255)),
                                             extra=b'G' if zone % 2 else b'\0'))
                scene(peer, bytes(batch))
                for base in range(0, 256, 64):
                    payload = bytes((base+i) & 255 for i in range(80))
                    scene(peer, define_wire(7, [9999, -1, 0, -1], payload))
                scene(peer, define_wire(7, [4, 3, 2, 1], b'\xff', extra=b'\0'))
                # Signed WORD clamp:255 means80,0/-200 mean1.
                scene(peer, define_wire(13, [1, 2, 3, 4], bytes(range(80)), length=255))
                scene(peer, define_wire(13, [1, 2, 3, 4], b'A', length=0))
                scene(peer, define_wire(13, [1, 2, 3, 4], b'B', length=-200))
                scene(peer, define_wire(9996, [8, 7, 6, 5], b'G#:\0'))
                loopback = 1; scene(peer, b'G#X 4,9998:')
                for rect in rectangles:
                    rect[:] = [-1]*4
                scene(peer, b'G#X 4,9999:')
                loopback = 0; scene(peer, b'G#X 4,9997:')
                scene(peer, b'G#I 2:')
                # An accepted corner remains visible even with no next operand.
                rectangles[4][0] = 19
                send(peer, b'G#X 4,4,19,')
                frame = client.wait_for(lambda f: f.application.get('cterm.igs_zone_rectangles') == byte_hash(
                    b''.join((word & 65535).to_bytes(2, 'big') for rect in rectangles for word in rect)))
                inspect(frame)
                rectangles[4][1:] = [20, 21, 22]; payloads[4][0:3] = b'\x02\0\xff'
                scene(peer, b'20,21,22,2:\0\xff!')
                # Local keys wait through raw input, not an abort or early send.
                rectangles[8][:] = [11, 12, 13, 14]; payloads[8][0] = 3; payloads[8][1] = ord('z')
                send(peer, b'G#X 4,8,11,12,13,14,3:z')
                frame = client.wait_for(lambda f: f.application.get('cterm.igs_zone_loading') == 1
                                        and f.application.get('cterm.igs_zone_received') == 1)
                inspect(frame, loading=True, received=1)
                client.text('é')
                frame = client.wait_for(lambda f: f.application.get('cterm.igs_music_keys') == 1)
                inspect(frame, loading=True, received=1)
                payloads[8][2:4] = b'12'; scene(peer, b'12!')
                read(peer, b'\xc3\xa9' if encoding == 'UTF-8' else b'\x82')
                # Partial serial EOF does not fill unreceived bytes or clear slots.
                payloads[8][0] = 4; payloads[8][1] = ord('y')
                send(peer, b'G#X 4,8,11,12,13,14,4:y')
                frame = client.wait_for(lambda f: f.application.get('cterm.igs_zone_loading') == 1
                                        and f.application.get('cterm.igs_zone_received') == 1)
                inspect(frame, loading=True, received=1)
                disconnect(peer)
            client.key('enter'); rectangles[:] = [[0]*4 for _ in range(48)]
            payloads[:] = [bytearray(81) for _ in range(48)]; loopback = 0
            macro[:] = [0, 0, 0]; macro_bytes[:] = bytes(80)
            with listener.accept()[0] as peer:
                peer.settimeout(5); setup(peer)
                loopback = 1; scene(peer, b'G#X 4,9998:')
                disconnect(peer)
            assert b'[wren' not in client.stderr, bytes(client.stderr)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('executable')
    parser.add_argument('--reports', default='reports/test-control'); args = parser.parse_args()
    for resolution in range(3):
        for encoding in ('ATARI-ST', 'CP437', 'UTF-8'):
            for status in (False, True):
                run(args.executable, args.reports, resolution, encoding, status)
    assert CAPTURES == 432, CAPTURES
    print(f'{CAPTURES} coherent X4 captures', flush=True)
