#!/usr/bin/env python3
"""IGS I through profiles, transport and the control bitmap backend.

Palette words are literal reference-28/table expectations. Z rows are literal
bundled Atari ST font rows, independent of the renderer under test. Each
checkpoint observes cells, both pixel planes, palette and state in one frame.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import socket
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'testing'))
from syncterm_fixture import fixture, cell_at, click_text

IGS = (0x777, 0x700, 0x070, 0, 0x007, 0x770, 0x077, 0x546,
       0x532, 0x743, 0x043, 0x030, 0x333, 0x210, 0x134, 0)
VDI = (0x777, 0x700, 0x070, 0x770, 0x007, 0x707, 0x077, 0x555,
       0x333, 0x733, 0x373, 0x773, 0x337, 0x737, 0x377, 0)
DEFAULT = (0xffffff, 0xff0000, 0x00ff00, 0xffff00,
           0x0000ff, 0xff00ff, 0x00ffff, 0xb6b6b6,
           0x6d6d6d, 0xff6d6d, 0x6dff6d, 0xffff6d,
           0x6d6dff, 0xff6dff, 0x6dffff, 0)
Z8 = (0x7e, 0x06, 0x0c, 0x18, 0x30, 0x60, 0x7e, 0)
Z16 = (0, 0, 0x7e, 0x7e, 0x0c, 0x0c, 0x18, 0x18,
       0x30, 0x30, 0x60, 0x60, 0x7e, 0x7e, 0, 0)


def rgb(word):
    # Reference ST DAC: three bits per channel, nearest 8-bit expansion.
    return sum((((word >> shift) & 7) * 255 + 3) // 7 << output
               for shift, output in ((8, 16), (4, 8), (0, 0)))


def run(executable, reports, resolution, encoding, status):
    mode = ('AtariST40x25', 'AtariST80x25', 'AtariST80x25Mono')[resolution]
    width, height, ch = (320 if resolution == 0 else 640), (400 if resolution == 2 else 200), (16 if resolution == 2 else 8)
    mask = (15, 3, 1)[resolution]
    default = DEFAULT if resolution == 0 else ((0xffffff, 0xff0000, 0x00ff00, 0) if resolution == 1 else (0xffffff, 0))
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        listener.listen(1)
        listener.settimeout(15)
        entries = ('[IGS init]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                   f'Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\n'
                   f'ScreenMode={mode}\nFont=Atari ST\nEncoding={encoding}\n'
                   f'NoStatus={str(not status).lower()}\n')
        with fixture(executable, entries=entries, reports=reports,
                     settings='AudioModes=0\n', quiet=False) as (client, root):
            menu = client.wait_text('IGS init')
            client.key('enter')
            with listener.accept()[0] as peer:
                client.wait_for(lambda s: s.application.get('connected') == 1)
                # Seed ordinary ST text away from the fence. It must survive
                # every I operation except a colour-mode I5.
                peer.sendall(b'\x1bf\x1bE\x1bY,,Z')
                for step, op in enumerate((3, 2, 4, 1, 3, 0, 3, 6, 5, None)):
                    token = f'INIT{step:02d}'
                    wire = (f'G#I {op}:'.encode() if op is not None else b'\x1bY,,Z') + b'\x1bH\x1bf' + token.encode()
                    with (root / 'wire.bin').open('ab') as stream:
                        stream.write(wire)
                    peer.sendall(wire)
                    frame = client.wait_text(token)
                    m = frame.metadata
                    assert (m['plane_width'], m['plane_height'], m['cell_width'], m['cell_height']) == (width, height, 8, ch), m
                    assert frame.application['cterm.graphics_mode'] == 5
                    assert frame.application['cterm.skypix'] == 0
                    if op in (3, 2):
                        palette = tuple(map(rgb, IGS[:mask + 1])) if resolution != 2 else (0xffffff, 0)
                    elif op == 4:
                        palette = tuple(map(rgb, VDI)) if resolution == 0 else ((0xffffff, 0xff0000, 0x00ff00, 0) if resolution == 1 else (0xffffff, 0))
                    else:
                        palette = default
                    assert tuple(frame.palette[768:769 + mask]) == palette, (resolution, op, frame.palette[768:769 + mask], palette)
                    seeded = op != 5 or resolution == 2
                    assert cell_at(frame, 12, 12).glyph == (ord('Z') if seeded else 32)
                    drawable = height - (ch if status else 0)
                    expected = [768] * (width * (drawable - ch))
                    if seeded:
                        for y, row in enumerate(Z16 if resolution == 2 else Z8):
                            for x in range(8):
                                if row & (128 >> x):
                                    expected[(12 * ch + y - ch) * width + 96 + x] = 768 + mask
                    start = (m['pixel_top'] + ch) * width
                    for plane in (frame.primary, frame.alternate):
                        actual = list(plane[start:start + len(expected)])
                        assert actual == expected, (resolution, encoding, status, op,
                            next((i % width, i // width + ch, a, e)
                                 for i, (a, e) in enumerate(zip(actual, expected)) if a != e))
                peer.shutdown(socket.SHUT_RDWR)
                client.wait_text('Disconnected: Enter exits, F1 details')
                client.key('enter')
            restored = client.wait_text('Directory: Name')
            assert restored.metadata['plane_width'] == menu.metadata['plane_width']
            assert restored.metadata['plane_height'] == menu.metadata['plane_height']
            assert b'[wren' not in client.stderr, bytes(client.stderr)


def editor(executable, reports):
    with fixture(executable, entries='[IGS editor]\nAddress=example.invalid\n',
                 reports=reports) as (client, root):
        client.wait_text('IGS editor')
        client.key('f2')
        screen = client.wait_text('Graphics')
        click_text(client, screen, 'Graphics')
        client.wait_text('Enter Select')
        client.key('end')
        client.wait_text('IGS (2.21)')
        client.key('enter')
        screen = client.wait_for(lambda s: 'Atari ST 40x25' in s.text and 'IGS (2.21)' in s.text)
        click_text(client, screen, 'Screen Mode')
        client.wait_text('Enter Select')
        client.key('end')
        client.wait_text('Atari ST 80x25 Mono')
        client.key('enter')
        screen = client.wait_text('Graphics')
        click_text(client, screen, 'Encoding')
        client.wait_text('Enter Select')
        client.key('end')
        client.wait_text('UTF-8')
        client.key('enter')
        client.wait_for(lambda s: 'UTF-8' in s.text and 'Atari ST 80x25 Mono' in s.text)
        client.key('escape')
        client.wait_text('Directory: Name')
        saved = (root / 'syncterm.lst').read_text()
        assert 'Graphics=IGS (2.21)' in saved and 'ScreenMode=AtariST80x25Mono' in saved, saved
        assert 'Encoding=UTF-8' in saved and 'Font=Atari ST' in saved, saved
        assert b'[wren' not in client.stderr, bytes(client.stderr)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('executable')
    parser.add_argument('--reports', default='reports/test-control')
    args = parser.parse_args()
    editor(args.executable, args.reports)
    for resolution in range(3):
        for encoding in ('ATARI-ST', 'UTF-8'):
            for status in (False, True):
                run(args.executable, args.reports, resolution, encoding, status)
