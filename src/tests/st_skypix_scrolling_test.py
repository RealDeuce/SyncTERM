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


def run(executable, reports, kick, encoding, status):
    print(kick, encoding, 'status', status, flush=True)
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
                    return COLOURS[(y//4) % 7 + 1] if 0 <= y < 200 else 0

                def check(s, start):
                    assert (s.metadata['columns'], s.metadata['cell_width'], s.metadata['cell_height']) == (80, 8, 8)
                    for y in range(160):
                        for alternate in (False, True):
                            assert pixel(s, y, alternate) == expected(start+y), (kick, encoding, status, start, y, alternate)

                def painted(start):
                    s = client.wait_for(lambda s: all(pixel(s, y) == expected(start+y) for y in range(16)))
                    check(s, start)
                    return s

                # Nine ordinary LFs reach Diamond's last baseline. Neither
                # drawing the stripe nor history-row height changes that Y.
                initial = frame(b'\r\n'*9)
                assert initial.metadata['pixel_cursor_y'] == 195
                palette = b'\x1b[11;0;15;4095;3840;240;4080;255;3855;0;0;0;0;0;0;0;0!'
                stripe = b''.join(f'\x1b[15;{(y//4)%7+1}!\x1b[4;80;{y};87;{y+3}!'.encode()
                                  for y in range(0, 200, 4))
                check(frame(palette+stripe), 0)
                for scroll in range(1, 4):
                    live = frame(b'\r\n')
                    assert live.metadata['pixel_cursor_y'] == 195, live.metadata
                    check(live, scroll*20)
                    client.command_key(48)
                    client.wait_text('Scrollback')
                    painted(scroll*20)
                    client.key('up')
                    # The newest eight history pixels meet live row zero,
                    # including the four pending pixels on odd scrolls.
                    painted(scroll*20-8)
                    client.key('escape')
                    check(frame(), scroll*20)
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
            # The menu's ordinary 26-row viewport includes one additional
            # source row when the saved session has a separate status row.
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
