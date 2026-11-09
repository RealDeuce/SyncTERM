#!/usr/bin/env python3
"""SkyTERM cursor shape/colours with clean saved-background ownership.

The native/ROM reference is SKYTERM_PROTOCOL.adoc, cursor section. Native
tests additionally check non-default widths, clipping, brushes and history.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import socket
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'testing'))
from syncterm_fixture import fixture

GLYPHS = {'A': (0x10, 0x38, 0x6c, 0xc6, 0xfe, 0xc6, 0xc6, 0),
          'B': (0xfc, 0xc6, 0xc6, 0xfc, 0xc6, 0xc6, 0xfc, 0),
          'é': (0x0c, 0x10, 0x3c, 0x66, 0x7e, 0x60, 0x3c, 0)}


def scene(height, text='', cursor=None, mask=15):
    expected = [512] * (640 * height)
    for i, character in enumerate(text):
        for y, bits in enumerate(GLYPHS[character]):
            for x in range(8):
                if bits & (128 >> x):
                    expected[y * 640 + i * 8 + x] = 514
    if cursor is not None:
        for x in range(cursor, cursor + 8):
            at = 6 * 640 + x
            expected[at] = 512 + ((expected[at] - 512) ^ mask)
    return expected


def run(executable, reports, kick, encoding, status, mode_height):
    drawable = mode_height - (8 if status else 0)
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        listener.listen(1)
        listener.settimeout(15)
        entries = ('[Bitmap cursor]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                   f'Port={listener.getsockname()[1]}\nGraphics=SkyPix (Kick {kick})\n'
                   f'Encoding={encoding}\nNoStatus={str(not status).lower()}\n'
                   f'AmigaFont=Pearl.font\nAmigaFontSize=8\nScreenMode=Amiga640x{mode_height}\n')
        with fixture(executable, entries=entries, settings='AudioModes=0\n',
                     reports=reports, quiet=False) as (client, root):
            client.wait_text('Bitmap cursor')
            client.key('enter')
            with listener.accept()[0] as peer:
                peer.settimeout(10)
                client.wait_for(lambda s: s.application.get('connected') == 1)

                def frame(data=b''):
                    with (root / 'wire.bin').open('ab') as wire:
                        wire.write(data)
                    peer.sendall(data + b'\x1b[5n')
                    reply = b''
                    while len(reply) < 4:
                        part = peer.recv(4 - len(reply))
                        assert part, reply
                        reply += part
                    assert reply == b'\x1b[0n', reply
                    return client.capture()

                def check(shown, expected):
                    m = shown.metadata
                    assert (m['plane_width'], m['plane_height'], m['rows']) == (640, mode_height, mode_height // 8), m
                    assert m['pixel_cursor_enabled'] == 0 and m['cursor_visible'] == 0, m
                    start = m['pixel_top'] * 640
                    for plane in (shown.primary, shown.alternate):
                        actual = list(plane[start:start + len(expected)])
                        assert actual == expected, (kick, encoding, status, mode_height,
                            next((i % 640, i // 640, a, e)
                                 for i, (a, e) in enumerate(zip(actual, expected)) if a != e))

                check(frame(b'\f'), scene(drawable, cursor=0))
                check(frame(b'A'), scene(drawable, 'A', 8))
                time.sleep(0.4)  # More than two old JR-Comm blink intervals.
                check(frame(), scene(drawable, 'A', 8))
                # SkyTERM can abandon its underline during save/restore and
                # horizontal movement. Our intentional clean-cursor policy
                # always restores the previous location and draws just one.
                check(frame(b'\x1b[sB\x1b[u'), scene(drawable, 'AB', 8))
                check(frame(b'\x1b[2C'), scene(drawable, 'AB', 24))
                check(frame(b'\x1b[2D'), scene(drawable, 'AB', 8))
                check(frame(b'\x1b[H'), scene(drawable, 'AB', 0))
                check(frame(b'\x1b[2JA'), scene(drawable, 'A', 8))
                check(frame(b'\x1b[65535!B'), scene(drawable, 'AB'))
                check(frame(b'\x1b[10;0!A'), scene(drawable, 'ABA', 24))
                check(frame(b'\x1b[10;20!Diamond.font!\f'), scene(drawable))
                check(frame(b'\x1b[10;0!\f'), scene(drawable, cursor=0))
                # Byte-mode Amiga fonts keep native glyph indexes: native
                # 0xe9 selects this strike's e-acute, independently of CP437's
                # Unicode label for that byte. UTF-8 maps Latin-1 glyphs.
                accent = b'\xe9' if encoding == 'CP437' else b'e\xcc\x81'
                peer.sendall(accent[:1])
                check(frame(accent[1:]), scene(drawable, 'é', 8))
                check(frame(b'\x1b[17;1!\x1b[10;0!\f'), scene(drawable, cursor=0, mask=7))
                # Cursor pixels use the active native palette/depth. Changing
                # colour 15 makes the four-plane underline red, not fixed RGB.
                palette = (b'\x1b[11;0;287;3549;3840;241;943;4082;3086;'
                           b'182;221;175;124;15;1807;3086;3840!')
                shown = frame(b'\x1b[17;2!' + palette + b'\x1b[10;0!\f')
                check(shown, scene(drawable, cursor=0, mask=15))
                assert shown.palette[527] == 0xff0000
                client.command_key(44)
                menu = client.wait_text('SyncTERM Online Menu')
                assert menu.metadata['pixel_cursor_enabled'] == 0
                client.key('escape')
                check(frame(), scene(drawable, cursor=0, mask=15))
                peer.shutdown(socket.SHUT_RDWR)
                client.wait_text('Disconnected: Enter exits, F1 details')
                client.key('enter')
            client.wait_text('Directory: Name')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('executable')
    parser.add_argument('--reports', default='reports/test-control')
    args = parser.parse_args()
    for kick in ('1.3', '3.1'):
        for encoding in ('CP437', 'UTF-8'):
            for status in (False, True):
                for height in (200, 400):
                    run(args.executable, args.reports, kick, encoding, status, height)
