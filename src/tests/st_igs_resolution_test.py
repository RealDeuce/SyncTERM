#!/usr/bin/env python3
"""IGS R over a real connection: native mode/palette/pixel and lifecycle checks.

Reference 16/28 and the literal font/palette expectations shared with I.
Every checkpoint observes the complete scene/state/palette in one capture.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import socket
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'testing'))
from syncterm_fixture import fixture, cell_at
from st_igs_init_test import DEFAULT, IGS, VDI, Z8, Z16, rgb


def initial(entry):
    if entry == 0:
        return list(DEFAULT)
    return list((0xffffff, 0xff0000, 0x00ff00, 0) * 4 if entry == 1 else (0xffffff, 0) * 8)


def table(resolution, words):
    if resolution == 2:
        return [0xffffff, 0]
    return list(map(rgb, words)) if resolution == 0 else [0xffffff, 0xff0000, 0x00ff00, 0]


def check(frame, resolution, status, registers, seeded, token):
    width = 320 if resolution == 0 else 640
    height, ch = (400, 16) if resolution == 2 else (200, 8)
    m, state = frame.metadata, frame.application
    assert (m['columns'], m['rows'], m['plane_width'], m['plane_height']) == (width // 8, 25, width, height), m
    assert state['cterm.graphics_mode'] == 5 and state['cterm.skypix'] == 0
    assert state['cterm.scrollback_width'] == width // 8
    assert (state['cterm.x'], state['cterm.y']) == (len(token) + 1, 1), state
    assert tuple(frame.palette[768:784]) == tuple(registers), (resolution, registers, frame.palette[768:784])
    assert cell_at(frame, 12, 12).glyph == (ord('Z') if seeded else 32)
    drawable = height - (ch if status else 0)
    expected = [768] * (width * (drawable - ch))
    if seeded:
        mask = (15, 3, 1)[resolution]
        for y, row in enumerate(Z16 if resolution == 2 else Z8):
            for x in range(8):
                if row & (128 >> x):
                    expected[(12 * ch + y - ch) * width + 96 + x] = 768 + mask
    start = (m['pixel_top'] + ch) * width
    for plane in (frame.primary, frame.alternate):
        actual = list(plane[start:start + len(expected)])
        assert actual == expected, (resolution, status, token,
            next((i % width, i // width + ch, a, e)
                 for i, (a, e) in enumerate(zip(actual, expected)) if a != e))


def run(executable, reports, entry, encoding, status):
    mode = ('AtariST40x25', 'AtariST80x25', 'AtariST80x25Mono')[entry]
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        listener.listen(2)
        listener.settimeout(15)
        entries = ('[IGS resolution]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                   f'Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\n'
                   f'ScreenMode={mode}\nFont=Atari ST\nEncoding={encoding}\n'
                   f'NoStatus={str(not status).lower()}\n')
        with fixture(executable, entries=entries, settings='AudioModes=0\n',
                     reports=reports, quiet=False) as (client, root):
            menu = client.wait_text('IGS resolution')
            client.key('enter')
            with listener.accept()[0] as peer:
                client.wait_for(lambda s: s.application.get('connected') == 1)
                # Include integer-to-fractional and sub-native medium scales.
                window = (320, 240) if entry == 0 and encoding == 'ATARI-ST' else (960, 720)
                client.resize(*window)
                client.wait_for(lambda s: (s.metadata['output_width'], s.metadata['output_height']) == window)
                current, registers = entry, initial(entry)
                steps = ((0, 0), (1, 2), (1, 3), (2, 2), (0, 3),
                         (9999, 4), (1, 1), (0, 1), (1, 9999),
                         (0, 0), (1 if entry == 0 else 0, 3))
                for step, (requested, palette) in enumerate(steps):
                    token = f'RES{step:02d}'
                    # I3 establishes a known palette; ordinary VT52 seeds
                    # a glyph and enables wrap. Only a changed colour mode
                    # may erase it or select medium no-wrap.
                    wire = (b'G#I 3:\x1bf\x1bE\x1bv\x1bY,,Z'
                            + f'G#R {requested},{palette}:'.encode()
                            + b'\x1bH\x1bf' + token.encode())
                    baseline = table(current, IGS)
                    registers[:len(baseline)] = baseline
                    target = requested if current != 2 and requested in (0, 1) else current
                    changed = target != current
                    if palette == 1:
                        saved = initial(entry)
                        count = (16, 4, 2)[target]
                        restored = saved[:count] if target == 0 else initial(target)[:count]
                        registers[:count] = restored
                    elif palette in (2, 3):
                        selected = table(target, IGS if palette == 2 else VDI)
                        registers[:len(selected)] = selected
                    with (root / 'wire.bin').open('ab') as stream:
                        stream.write(wire)
                    peer.sendall(wire)
                    frame = client.wait_text(token)
                    assert (frame.metadata['output_width'], frame.metadata['output_height']) == window
                    assert (frame.metadata['destination_width'], frame.metadata['destination_height']) == window
                    check(frame, target, status, registers, not changed, token)
                    assert frame.application['cterm.ext_attr.auto_wrap'] == (target == 0 if changed else True)
                    current = target
                peer.shutdown(socket.SHUT_RDWR)
                client.wait_text('Disconnected: Enter exits, F1 details')
                client.key('enter')
            restored = client.wait_text('Directory: Name')
            for key in ('columns', 'rows', 'cell_width', 'cell_height', 'plane_width', 'plane_height'):
                assert restored.metadata[key] == menu.metadata[key], (entry, key)
            # Reconnect after ending in another colour mode and palette.
            # The profile's entry mode and desktop palette must be fresh.
            client.key('enter')
            with listener.accept()[0] as peer:
                client.wait_for(lambda s: s.application.get('connected') == 1)
                peer.sendall(b'\x1bf\x1bE\x1bY,,Z\x1bHFRESH')
                check(client.wait_text('FRESH'), entry, status, initial(entry), True, 'FRESH')
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
