#!/usr/bin/env python3
"""S through transport: existing indexed pixels recolour in one capture.

Expected ST DAC colours/polarities and Z glyph rows are independent literals.
Raw planes, cells, palette and composed RGB belong to the same observation.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import socket
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'testing'))
from syncterm_fixture import fixture
from st_igs_resolution_test import check as check_scene, initial


def check(frame, actual, entry, status, registers, seeded, token):
    check_scene(frame, actual, status, registers, seeded, token)
    m, state = frame.metadata, frame.application
    assert (m['output_width'], m['output_height']) == (960, 720)
    assert (m['destination_width'], m['destination_height']) == (960, 720)
    assert state['cterm.igs_resolution'] == actual
    assert state['cterm.igs_scaling_mode'] == 2 and state['cterm.igs_scaling_resolution'] == entry
    # BlockyScaling makes samples at source-pixel centres exact, even at
    # fractional display scales. The old Z is never redrawn during S steps.
    samples = [(64,64,registers[0])]
    if seeded:
        samples.append((99,12 * (16 if actual == 2 else 8) + (2 if actual == 2 else 0),
                        registers[(15,3,1)[actual]]))
    for x, y, expected in samples:
        ox = m['destination_x'] + (2 * x + 1) * m['destination_width'] // (2 * m['plane_width'])
        oy = m['destination_y'] + (2 * (y + m['pixel_top']) + 1) * m['destination_height'] // (2 * m['plane_height'])
        assert frame.rgb[oy * m['output_width'] + ox] & 0xffffff == expected, (actual, token, x, y, expected)


def run(executable, reports, entry, encoding, status):
    screen = ('AtariST40x25', 'AtariST80x25', 'AtariST80x25Mono')[entry]
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        listener.listen(2)
        listener.settimeout(15)
        entries = ('[IGS palette]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                   f'Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\n'
                   f'ScreenMode={screen}\nFont=Atari ST\nEncoding={encoding}\n'
                   f'NoStatus={str(not status).lower()}\n')
        with fixture(executable, entries=entries, settings='AudioModes=0\nBlockyScaling=true\n',
                     reports=reports, quiet=False) as (client, root):
            client.wait_text('IGS palette')
            client.key('enter')
            with listener.accept()[0] as peer:
                client.wait_for(lambda s: s.application.get('connected') == 1)
                client.resize(960, 720)
                peer.sendall(b'G#g 2:\x1bf\x1bE\x1bY,,Z')
                other = 1 - entry if entry != 2 else 2
                fg = (15,3,1)[entry]
                changed_fg = (15,3,1)[other]
                black = {0:0, 1:0xffffff} if entry == 2 else {0:0}
                registers = initial(entry)
                steps = (
                    (b'G#S 1,7,0,7:', entry, True, {fg:0xff00ff} if entry != 2 else {}),
                    (b'G#S 0,0,0,0:', entry, True, black),
                    (b'G#S 1,7,7,7:', entry, True, {fg:0xffffff} if entry != 2 else black),
                    (b'G#S 1,3,3,3:', entry, True, {fg:0x6d6d6d} if entry != 2 else {}),
                    (b'G#S 2,7,0,0:', entry, True, {1:0xff0000} if entry != 2 else {}),
                    (b'G#S 9999,0,7,0:', entry, True, {0:0x00ff00} if entry != 2 else {}),
                    (b'G#S 0,9,8,9999:', entry, True, black),
                    (b'G#I 2:', entry, True, {}),
                    (f'G#R {other},0:'.encode(), other, entry == 2, {}),
                    (b'\x1bY,,ZG#S 1,0,0,7:', other, True, {changed_fg:0x0000ff} if entry != 2 else {}),
                    (b'G#I 5:', entry, entry == 2, {}),
                    (b'G#I 1:', entry, entry == 2, dict(enumerate(initial(entry)[:fg + 1]))),
                )
                for number, (command, actual, seeded, changes) in enumerate(steps):
                    for index, value in changes.items():
                        registers[index] = value
                    token = f'PAL{number:02d}'
                    wire = command + b'\x1bH\x1bf' + token.encode()
                    with (root / 'wire.bin').open('ab') as stream:
                        stream.write(wire)
                    peer.sendall(wire)
                    check(client.wait_text(token), actual, entry, status, registers, seeded, token)
                peer.settimeout(0.03)
                try:
                    reply = peer.recv(1)
                except socket.timeout:
                    pass
                else:
                    raise AssertionError(f'unexpected IGS reply: {reply!r}')
                peer.sendall(b'G#S 0,7,7,')
                peer.shutdown(socket.SHUT_RDWR)
                client.wait_text('Disconnected: Enter exits, F1 details')
                client.key('enter')
            client.wait_text('Directory: Name')
            client.key('enter')
            with listener.accept()[0] as peer:
                client.wait_for(lambda s: s.application.get('connected') == 1)
                client.resize(960, 720)
                peer.sendall(b'G#g 2:\x1bf\x1bE\x1bY,,Z\x1bHFRESH')
                check(client.wait_text('FRESH'), entry, entry, status, initial(entry), True, 'FRESH')
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
