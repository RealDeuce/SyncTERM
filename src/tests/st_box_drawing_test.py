#!/usr/bin/env python3
"""Check CP437 and Unicode box rasters through coherent control captures."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path

from st_migrated_control_test import connection
from st_terminal_conformance_test import Terminal, tile, colour, cup, ESC
from syncterm_fixture import cell_at


SCENE = (
    'Box drawing: CP437 + Unicode fallback',
    '',
    'Light              Heavy / light      Rounded',
    '┌────────────┐     ┏━━━━━━━━━━━━┓     ╭────────────╮',
    '│            │     ┃            ┃     │            │',
    '├────────────┤     ┠────────────┨     │            │',
    '│            │     ┃            ┃     │            │',
    '└────────────┘     ┗━━━━━━━━━━━━┛     ╰────────────╯',
    '',
    'Mixed weights      Dashed             Double',
    '┌─────┰──────┐     ┌┄┄┄┄┄┄┄┄┄┄┄┄┐     ╔════════════╗',
    '│     ┃      │     ┆            ┆     ║            ║',
    '┝━━━━━╋━━━━━━┥     ┆            ┆     ╠════════════╣',
    '│     ┃      │     ┆            ┆     ║            ║',
    '└─────┸──────┘     └┄┄┄┄┄┄┄┄┄┄┄┄┘     ╚════════════╝',
    '',
    'Mixed arms: ─╼━╾─   │╽┃╿│     Dashes: ┄┅┈┉╌╍',
)

# Explicit reference rows, independent of both the generator and renderer.
EXPECTED = {
    8: {
        '─': '00000000ff000000', '│': '1818181818181818',
        '┌': '000000001f181818', '┼': '18181818ff181818',
        '═': '0000ff00ff000000', '║': '3636363636363636',
        '━': '000000ffffff0000', '┃': '3c3c3c3c3c3c3c3c',
        '┍': '0000001f1f1f1818', '┎': '000000003f3c3c3c',
        '╭': '00000000070c1818', '╼': '0000000fff0f0000',
        '┄': '00000000db000000',
    },
    16: {
        '─': '00000000000000ff0000000000000000',
        '│': '18181818181818181818181818181818',
        '┌': '000000000000001f1818181818181818',
        '┼': '18181818181818ff1818181818181818',
        '═': '0000000000ff00ff0000000000000000',
        '║': '36363636363636363636363636363636',
        '━': '000000000000ffffff00000000000000',
        '┃': '3c3c3c3c3c3c3c3c3c3c3c3c3c3c3c3c',
        '┍': '0000000000001f1f1f18181818181818',
        '┎': '000000000000003f3c3c3c3c3c3c3c3c',
        '╭': '00000000000000070c18181818181818',
        '╼': '0000000000000fff0f00000000000000',
        '┄': '00000000000000db0000000000000000',
    },
}


def run(executable, encoding, reports, preview_dir=None, preview_only=False):
    for height in (8, 16):
        settings = (f'AudioModes=0\nCustomRows=25\nCustomColumns=80\n'
                    f'CustomFontHeight={height}\n')
        with connection(executable, reports, encoding=encoding, settings=settings,
                        options='ScreenMode=Custom\n') as (client, root, peer, send):
            terminal = Terminal(client, root, peer, send)
            terminal.case(f'box-drawing-{encoding}-{height}')
            expected = {char: bytes.fromhex(bitmap) for char, bitmap in EXPECTED[height].items()
                        if encoding == 'UTF-8' or char in '─│┌┼═║'}
            samples = ''.join(expected)
            scene = b''.join(cup(1, y) + line.encode(encoding, errors='replace')
                             for y, line in enumerate(SCENE, 1))
            payload = ESC + b'[?25l' + ESC + b'[0;37;40m' + scene + cup(1, 21) + samples.encode(encoding)
            for repetition in range(2):
                frame = terminal.exchange(payload, reset=True)
                assert frame.metadata['cell_width'] == 8
                assert frame.metadata['cell_height'] == height
                assert (frame.application['cterm.x'], frame.application['cterm.y']) == (len(samples) + 1, 21)
                if not preview_only:
                    for x, (char, bitmap) in enumerate(expected.items(), 1):
                        cell = cell_at(frame, x - 1, 20)
                        assert frame.cell_text(cell) == char
                        foreground, background = colour(frame, cell), colour(frame, cell, True)
                        pixels = tuple(foreground if row & (0x80 >> bit) else background
                                       for row in bitmap for bit in range(8))
                        for alternate in (False, True):
                            observed = tuple((p if p & 0x80000000 else frame.palette[p]) & 0xffffff
                                             for p in tile(frame, x, 21, alternate))
                            assert observed == pixels, (encoding, height, char, repetition, alternate)
                if preview_dir is not None and repetition == 0:
                    preview_dir.mkdir(parents=True, exist_ok=True)
                    frame.save_image(preview_dir / f'boxes-{encoding}-{height}.ppm')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('encoding', choices=('CP437', 'UTF-8'))
    parser.add_argument('--reports', type=Path, required=True)
    parser.add_argument('--preview-dir', type=Path)
    parser.add_argument('--preview-only', action='store_true')
    args = parser.parse_args()
    run(args.executable, args.encoding, args.reports, args.preview_dir, args.preview_only)
