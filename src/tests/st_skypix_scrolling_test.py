#!/usr/bin/env python3
"""Literal pixel stripes through fractional scrolling and both history views."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import socket
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'testing'))
from syncterm_fixture import fixture

COLOURS = (0, 0x0000ff, 0xffffff, 0xff0000, 0x00ff00, 0xffff00, 0x00ffff, 0xff00ff)


def run_box(executable, reports, kick, encoding, status, mode_height):
    """Literal box crosses the old font-row boundary, then fonts change.

    SkyTERM_SCROLLING.adoc's ordinary ScrollRaster copy rule supplies the
    expected raster operation: shift the entire drawable rectangle, then fill
    its exposed bottom strip. This is synthetic geometry, not a RoF golden.
    """
    canvas = mode_height - (8 if status else 0)
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        listener.listen(1)
        listener.settimeout(15)
        entries = ('[Box scrolling]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                   f'Port={listener.getsockname()[1]}\nGraphics=SkyPix (Kick {kick})\n'
                   f'Encoding={encoding}\nNoStatus={str(not status).lower()}\n'
                   f'AmigaFont=Pearl.font\nAmigaFontSize=8\nScreenMode=Amiga640x{mode_height}\n')
        with fixture(executable, entries=entries, settings='AudioModes=0\n',
                     reports=reports, quiet=False) as (client, root):
            client.wait_text('Box scrolling')
            client.key('enter')
            with listener.accept()[0] as peer:
                peer.settimeout(10)
                client.wait_for(lambda s: s.application.get('connected') == 1)

                def check(data, shifted):
                    with (root / 'wire.bin').open('ab') as wire:
                        wire.write(data)
                    peer.sendall(data + b'\x1b[5n')
                    reply = b''
                    while len(reply) < 4:
                        part = peer.recv(4 - len(reply))
                        assert part, reply
                        reply += part
                    assert reply == b'\x1b[0n', reply
                    frame = client.capture()
                    m = frame.metadata
                    assert (m['columns'], m['rows'], m['plane_width'], m['plane_height']) == (80, mode_height // 8, 640, mode_height), m
                    for y in range(canvas):
                        source_y = y + shifted
                        for x in range(640):
                            outline = (20 <= x <= 620 and 12 <= source_y <= canvas - 4
                                       and (x in (20, 620) or source_y in (12, canvas - 4)))
                            expected = 515 if outline else 512
                            at = (y + m['pixel_top']) * m['plane_width'] + x
                            assert frame.primary[at] == expected and frame.alternate[at] == expected, (
                                kick, encoding, status, mode_height, shifted, x, y,
                                frame.primary[at], frame.alternate[at], expected)
                    assert frame.palette[515] == 0xff0000

                # Times/15 has a four-pixel descent. Its nominal last anchor
                # determines when to scroll, independently of the raster extent.
                anchor = (canvas // 15 - 1) * 15 - 4
                check((f'\x1bc\x1b[?25l\x1b[10;15!Times.font!\x1b[19;0;{anchor}!'
                       f'\x1b[15;3!\x1b[8;20;12!\x1b[2;620;12!\x1b[2;620;{canvas-4}!'
                       f'\x1b[2;20;{canvas-4}!\x1b[2;20;12!').encode(), 0)
                check(b'\r\n', 15)
                check(f'\x1b[10;8!Ruby.font!\x1b[19;0;{canvas-9}!\r\n'.encode(), 23)
                check(b'\r\n', 31)
                # Return to grid-aligned default Pearl and exercise the normal
                # cell scroll too; the same raster must remain continuous.
                check(f'\x1b[10;0!\x1b[19;0;{canvas-8}!\x1b[10;0!\r\n'.encode(), 39)
                peer.shutdown(socket.SHUT_RDWR)
                client.wait_text('Disconnected: Enter exits, F1 details')
                client.key('enter')
            client.wait_text('Directory: Name')


def run(executable, reports, kick, encoding, status):
    print(kick, encoding, 'status', status, flush=True)
    canvas_height = 192 if status else 200
    text_height = canvas_height // 20 * 20
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        listener.listen(1)
        listener.settimeout(15)
        entries = ('[Pixel scrolling]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                   f'Port={listener.getsockname()[1]}\nGraphics=SkyPix (Kick {kick})\n'
                   f'Encoding={encoding}\nNoStatus={str(not status).lower()}\n'
                   'AmigaFont=Diamond.font\nAmigaFontSize=20\nScreenMode=Amiga640x200\n')
        with fixture(executable, entries=entries, settings='AudioModes=0\nScrollBackLines=100\n',
                     reports=reports, quiet=False) as (client, root):
            client.wait_text('Pixel scrolling')
            client.key('enter')
            with listener.accept()[0] as peer:
                peer.settimeout(10)
                client.wait_for(lambda s: s.application.get('connected') == 1)

                def frame(data=b''):
                    peer.sendall(data + b'\x1b[5n')
                    reply = b''
                    while len(reply) < 4:
                        part = peer.recv(4-len(reply))
                        assert part, reply
                        reply += part
                    assert reply == b'\x1b[0n', reply
                    return client.capture()

                def pixel(s, y, alternate=False):
                    m = s.metadata
                    plane = s.alternate if alternate else s.primary
                    value = plane[(y+m['pixel_top'])*m['plane_width']+80]
                    return (value if value & 0x80000000 else s.palette[value]) & 0xffffff

                def expected(y):
                    return COLOURS[(y//4) % 7 + 1] if 0 <= y < canvas_height else 0

                def check(s, start):
                    assert (s.metadata['columns'], s.metadata['cell_width'], s.metadata['cell_height']) == (80, 8, 8)
                    for y in range(160):
                        for alternate in (False, True):
                            assert pixel(s, y, alternate) == expected(start+y), (kick, encoding, status, start, y, alternate)

                def check_live(s, start):
                    check(s,start)
                    for y in range(160,canvas_height):
                        # SkyTERM's scroll rectangle reaches the drawable
                        # bottom even when the font height does not divide it.
                        colour=expected(start+y)
                        for alternate in (False,True):
                            assert pixel(s,y,alternate)==colour,(kick,encoding,status,start,y,alternate)

                def painted(start):
                    s = client.wait_for(lambda s: all(pixel(s, y) == expected(start+y) for y in range(16)))
                    check(s, start)
                    return s

                # Whole 20-pixel text rows determine the last baseline, while
                # the raster scroll uses the entire drawable area.
                initial = frame(b'\r\n'*(text_height//20-1))
                assert initial.metadata['cursor_y'] == (text_height-20)//8
                palette = b'\x1b[11;0;15;4095;3840;240;4080;255;3855;0;0;0;0;0;0;0;0!'
                stripe = b''.join(f'\x1b[15;{(y//4)%7+1}!\x1b[4;80;{y};87;{y+3}!'.encode()
                                  for y in range(0, canvas_height, 4))
                check_live(frame(palette+stripe), 0)
                for scroll in range(1, 4):
                    live = frame(b'\r\n')
                    assert live.metadata['cursor_y'] == (text_height-20)//8, live.metadata
                    check_live(live, scroll*20)
                    client.command_key(48)
                    client.wait_text('Scrollback')
                    painted(scroll*20)
                    client.key('up')
                    # The newest eight history pixels meet live row zero,
                    # including the four pending pixels on odd scrolls.
                    painted(scroll*20-8)
                    client.key('escape')
                    check_live(frame(), scroll*20)
                peer.shutdown(socket.SHUT_RDWR)
                client.wait_text('Disconnected: Enter exits, F1 details')
                # The session publishes its retained screen before making
                # room for the notice. Both later viewers use that snapshot.
                retired = 60
                client.command_key(48)
                client.wait_text('Scrollback')
                painted(retired)
                client.key('up')
                painted(retired-8)
                client.key('escape')
                client.wait_text('Disconnected: Enter exits, F1 details')
                client.key('enter')
            client.wait_text('Directory: Name')
            client.command_key(48)
            client.wait_text('Scrollback')
            # The offline viewer uses all 25 mode rows; the connected
            # viewport excludes status and has only 24 when status is on.
            offline_retired = retired - (8 if status else 0)
            painted(offline_retired)
            client.key('up')
            painted(offline_retired-8)
            client.key('escape')
            client.wait_text('Directory: Name')
            assert b'[wren' not in client.stderr, bytes(client.stderr)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('executable')
    parser.add_argument('--reports', default='reports/test-control')
    args = parser.parse_args()
    for kick in ('1.3', '3.1'):
        for encoding in ('CP437', 'UTF-8'):
            for status in (False, True):
                run(args.executable, args.reports, kick, encoding, status)
                for height in (200, 400):
                    run_box(args.executable, args.reports, kick, encoding, status, height)
