#!/usr/bin/env python3
"""RoF text placement checked against independently decoded native Times/15."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import hashlib
import socket
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'testing'))
from syncterm_fixture import fixture

# Only these public welcome lines are retained from the user's 2026-11-08
# /tmp/rof.pcap. Both sessions send identical artwork. No login data is used.
# Expected masks come from amiga_font.py's load_font/text_mask on the native
# FS-UAE Fonts/times/15, SHA256 d00caceb09f0ad46052b37ee5409040e56cf3c99125f867bf6ebec4d791a309a.
# Baseline = command-19 Y + 15 - 1. Hash input: one byte per pixel, row-major
# 640x200, 1 for a glyph bit and 0 otherwise. Each byte is a separate Text
# call: Kick 1.3 clips its trailing overhang; Kick 3.1 retains it.
# The per-byte first-line hashes were also checked by executing Text in the
# pinned 1.3 and 3.1 ROMs via ~/SkyPix/tools/rom_graphics.py. Pens 4 and 11
# and the sixteen-colour request below are the captured wire values.
LINES = (
    (4, 85, 95, 'JoinLink - Messages - Games - Warez - ROF FTN - irc.rofbbs.com',
     ('4ee6aa59a25a1e0b3eea6cdd9052f85cf3ba93225ce5bcd9d8f8be7b7c29e7e4',
      'b050d0d47125ee9a3c51fb4c1c9b365bbfb7676e7ab4b50d197f67df692174e3')),
    (11, 75, 127, 'CommodoreNet 64:500/1  *  DiscipleNet 12:316/1  *  FidoNet 1:154/50',
     ('c8277130050e195aefa2eaaa0f39d4c00e4f956d6a4b08028a2a96a913a5986c',)*2),
)


def run(executable, reports, kick, encoding, status):
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        listener.listen(1)
        listener.settimeout(15)
        entries = ('[RoF welcome]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                   f'Port={listener.getsockname()[1]}\nGraphics=SkyPix (Kick {kick})\n'
                   f'Encoding={encoding}\nNoStatus={str(not status).lower()}\n'
                   'AmigaFont=Pearl.font\nAmigaFontSize=8\nScreenMode=Amiga640x200\n')
        with fixture(executable, entries=entries, reports=reports,
                     settings='AudioModes=0\n', quiet=False) as (client, root):
            client.wait_text('RoF welcome')
            client.key('enter')
            with listener.accept()[0] as peer:
                peer.settimeout(10)
                client.wait_for(lambda s: s.application.get('connected') == 1)
                # Reset and replay twice so a font reset and parser recovery
                # cannot retain stale glyph revision state or affect artwork.
                for repeat in range(2):
                    wire = b'\x1bc\x1b[17;2!\x1b[10;15!Times.font!\x1b[?25l'
                    wire += b''.join(f'\x1b[15;{pen}!\x1b[19;{x};{y}!{text}'.encode('ascii')
                                     for pen, x, y, text, digests in LINES)
                    (root / 'wire.bin').write_bytes(wire)
                    # Split inside a printable run; placement must not change.
                    peer.sendall(wire[:91])
                    peer.sendall(wire[91:] + b'\x1b[5n')
                    reply = b''
                    while len(reply) < 4:
                        part = peer.recv(4 - len(reply))
                        assert part, reply
                        reply += part
                    assert reply == b'\x1b[0n', reply
                    frame = client.capture()
                    m = frame.metadata
                    assert (m['columns'], m['cell_width'], m['cell_height']) == (80, 8, 8), m
                    assert m['rows'] == 25, m
                    # Named-font output has no bitmap cursor or overlay. The
                    # logical terminal position still tracks the pixel anchor.
                    assert m['pixel_cursor_enabled'] == 0, m
                    assert (m['cursor_x'], m['cursor_y']) == (60, 16), m
                    for pen, x, y, text, digests in LINES:
                        digest = digests[0 if kick == '1.3' else 1]
                        for plane in (frame.primary, frame.alternate):
                            mask = bytes(plane[(py + m['pixel_top']) * m['plane_width'] + px] == 512 + pen
                                         for py in range(200) for px in range(640))
                            actual = hashlib.sha256(mask).hexdigest()
                            assert actual == digest, (kick, encoding, status, repeat, pen, actual)
                        assert frame.palette[512 + pen] == (0x00ff11 if pen == 4 else 0x0077cc)
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
    for kick in ('1.3', '3.1'):
        for encoding in ('CP437', 'UTF-8'):
            for status in (False, True):
                run(args.executable, args.reports, kick, encoding, status)
