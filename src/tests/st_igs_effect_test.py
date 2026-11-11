#!/usr/bin/env python3
"""b patch ownership and audio-disabled session integration, coherent captures."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import json
from pathlib import Path
import socket

from st_igs_cursor_test import check, fixture

DATA = json.loads((Path(__file__).parent/'igs'/'effect-vectors.json').read_text())
ORIGINAL = [[int(row[i:i+4], 16) for i in range(0, len(row), 4)] for row in DATA['patches_hex']]
CAPTURES = 0


def fingerprint(bank):
    result = 2166136261
    for row in bank:
        for word in row:
            for byte in (word >> 8, word & 255):
                result = ((result ^ byte)*16777619) & 0xffffffff
    return result


def run(executable, reports, resolution, encoding, status):
    global CAPTURES
    screen = ('AtariST40x25', 'AtariST80x25', 'AtariST80x25Mono')[resolution]
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0)); listener.listen(2); listener.settimeout(15)
        entries = ('[IGS effects]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                   f'Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\nScreenMode={screen}\n'
                   f'Font=Atari ST\nEncoding={encoding}\nNoStatus={str(not status).lower()}\n')
        with fixture(executable, entries=entries, settings='AudioModes=0\nMidiModes=0\nBlockyScaling=true\n',
                     reports=reports, quiet=False) as (client, root):
            client.wait_text('IGS effects'); client.key('enter'); marker = 1000
            words = [row[:] for row in ORIGINAL]

            def send(peer, wire):
                with (root/'wire.bin').open('ab') as log:
                    log.write(wire)
                peer.sendall(wire)

            def scene(peer, wire, multiplier, voices=0):
                nonlocal marker
                global CAPTURES
                marker += 1
                send(peer, wire+f'G#X 10,{marker},777:'.encode())
                frame = client.wait_for(lambda f: f.application.get('cterm.igs_draw_x') == marker
                                        and f.application.get('cterm.igs_sound_voices') == voices)
                check(frame, resolution, status, 4, 'A', False, False, False)
                state = frame.application
                assert state['cterm.igs_sound_present'] == 1
                assert state['cterm.igs_sound_multiplier'] == multiplier
                assert state['cterm.igs_sound_patches'] == fingerprint(words)
                assert state['cterm.igs_sound_saved'] == fingerprint(ORIGINAL)
                assert not state['cterm.igs_sound_output'] and not state['cterm.igs_input_paused']
                CAPTURES += 1

            def setup(peer):
                client.wait_for(lambda f: f.application.get('connected') == 1)
                client.resize(1280, 960)
                scene(peer, b'G#c>1,3:c 0,0:\x1bE\x1bfG#p 3,2:AG#b 23,5:', 5)

            def disconnect(peer):
                peer.shutdown(socket.SHUT_RDWR)
                client.wait_text('Disconnected: Enter exits, F1 details')
                client.key('enter'); client.wait_text('Directory: Name')

            with listener.accept()[0] as peer:
                setup(peer)
                words[3][3] = 1
                scene(peer, b'G#b>23,0:b 20,0,3,3,0,0,1:', 0)
                scene(peer, b'G#b 22,11:', 0)  # Identical original patch owns a different record.
                words[19][55] = 41999
                scene(peer, b'G#b 20,0,20,56,0,33,9999:', 0)
                words[3] = ORIGINAL[3][:]
                scene(peer, b'G#b 22,3:', 0)
                words[5][0] = 2; words[5][4] = 0; words[5][1] = 125
                scene(peer, b'G#b>20,0,5,0,0,0,2:b>20,0,5,4,0,0,0:b 20,1,5,1,0,0,125:', 0)
                scene(peer, b'G#I 0:', 0)  # Graphics initialization preserves sound sources.
                words[5] = ORIGINAL[5][:]
                scene(peer, b'G#b>22,5:b>21:b 23,17:', 5)
                peer.settimeout(0.03)
                try:
                    reply = peer.recv(1)
                except socket.timeout:
                    pass
                else:
                    raise AssertionError(f'unexpected b reply: {reply!r}')
                words[5][0] = 9999; words[5][4] = 0
                scene(peer, b'G#b>20,0,5,4,0,0,0:b 20,1,5,0,0,9,999:', 5, voices=1)
                disconnect(peer)

            client.key('enter'); words = [row[:] for row in ORIGINAL]
            with listener.accept()[0] as peer:
                setup(peer)  # No edited/saved patch or active voice leaks to the new session.
                scene(peer, b'G#b 21:', 5)
                disconnect(peer)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('executable')
    parser.add_argument('--reports', default='reports/test-control'); args = parser.parse_args()
    for resolution in range(3):
        for encoding in ('ATARI-ST', 'CP437', 'UTF-8'):
            for status in (False, True):
                run(args.executable, args.reports, resolution, encoding, status)
    assert CAPTURES == 198, CAPTURES
