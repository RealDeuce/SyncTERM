#!/usr/bin/env python3
"""Musicomm local activation, raw transport, opaque notes and coherent recovery."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from array import array
from pathlib import Path
import socket
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'testing'))
from syncterm_fixture import fixture, click_text, cell_at, check_pixel_planes


def run(executable, reports, encoding, graphics):
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0)); listener.listen(1); listener.settimeout(15)
        entries = ('[Musicomm]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                   f'Port={listener.getsockname()[1]}\nEncoding={encoding}\nNoStatus=true\n'
                   + ('ScreenMode=AtariST80x25\nFont=Atari ST\nGraphics=IGS (2.21)\n' if graphics else ''))
        with fixture(executable, entries=entries, reports=reports, quiet=False) as (client, root):
            client.wait_text('Musicomm'); client.key('enter')
            with listener.accept()[0] as peer:
                peer.settimeout(5)

                def send(wire):
                    with (root / 'wire.bin').open('ab') as stream:
                        stream.write(wire)
                    peer.sendall(wire)

                def receive(expected):
                    got = b''
                    while len(got) < len(expected):
                        data = peer.recv(len(expected)-len(got))
                        assert data, (expected, got)
                        got += data
                    assert got == expected, (expected, got)

                def arm():
                    client.command_key(44)  # Alt-Z online menu.
                    menu = client.wait_text('SyncTERM Online Menu')
                    click_text(client, menu, 'Start Musicomm')
                    client.wait_for(lambda f: f.application.get('cterm.musicomm') == 1)

                def scene(text, x):
                    frame = client.wait_for(lambda f: f.application.get('cterm.musicomm') == 0
                                            and f.application.get('cterm.x') == x)
                    assert frame.lines[0].rstrip() == text
                    assert frame.application['cterm.y'] == 1
                    assert frame.application['cterm.graphics_mode'] == (5 if graphics else 0)
                    for i, char in enumerate(text):
                        assert cell_at(frame, i, 0).glyph == ord(char)
                    # The entire remaining terminal is independently blank in
                    # both phases. Check cells/pixels/cursor in this one capture.
                    width, height = frame.metadata['plane_width'], frame.metadata['plane_height']
                    ch = frame.metadata['cell_height']
                    background = cell_at(frame, 0, 1).background
                    check_pixel_planes(frame, array('I', [background]) * (width*(height-ch)), start=width*ch)
                    assert all(c.glyph == 32 for c in frame.cells[len(text):])
                    return frame

                client.wait_for(lambda f: f.application.get('connected') == 1)
                send(b'\x1b' + (b'E' if graphics else b'[2J\x1b[H') + b'A')
                scene('A', 2)
                arm()
                send(b'junk\xff\x02\x3c\x40')
                receive(b'\x02')
                client.wait_for(lambda f: f.application.get('cterm.musicomm') == 3)
                # These include every low byte: NUL, CR/LF, XON/XOFF,
                # escape, IGS/OOII introducers, and every MIDI pitch.
                send(bytes(range(128)) * 2 + b'\x1bEG#b 1:!|BOSS')
                send(b'\xffDISCARDED')
                receive(b'\xff\xff\xff')
                client.wait_for(lambda f: f.application.get('cterm.musicomm') == 0)
                send(b'Z')
                scene('AZ', 3)
                arm()
                client.text('x')  # Consumed locally, including before handshake.
                receive(b'\xff\xff\xff')
                client.wait_for(lambda f: f.application.get('cterm.musicomm') == 0)
                send(b'B')
                scene('AZB', 4)
                arm()
                send(b'\x02'); receive(b'\x02')
                client.wait_for(lambda f: f.application.get('cterm.musicomm') == 3)
                send(b'\x3c')
                peer.shutdown(socket.SHUT_RDWR)
                client.wait_text('Disconnected: Enter exits, F1 details')
                client.key('enter'); client.wait_text('Directory: Name')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('executable'); parser.add_argument('--reports', default='reports/test-control')
    args = parser.parse_args()
    for encoding in ('CP437', 'UTF-8'):
        for graphics in (False, True):
            run(args.executable, args.reports, encoding, graphics)


if __name__ == '__main__':
    main()
