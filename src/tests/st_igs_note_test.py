#!/usr/bin/env python3
"""n note ownership, key/deadline completion and coherent unchanged scenes."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import socket

from st_igs_cursor_test import check, fixture
from st_igs_effect_test import ORIGINAL, fingerprint

CAPTURES = 0
CUSTOM = [0]*56
CUSTOM[0:6] = [2, 125, 65535, 15, 1, 15]
CUSTOM[9] = 15  # Sustain at 15; zero release delta keeps a released voice observable.


def run(executable, reports, resolution, encoding, status):
    global CAPTURES
    screen = ('AtariST40x25', 'AtariST80x25', 'AtariST80x25Mono')[resolution]
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0)); listener.listen(2); listener.settimeout(15)
        entries = ('[IGS notes]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                   f'Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\nScreenMode={screen}\n'
                   f'Font=Atari ST\nEncoding={encoding}\nNoStatus={str(not status).lower()}\n')
        with fixture(executable, entries=entries, settings='AudioModes=0\nMidiModes=0\nBlockyScaling=true\n',
                     reports=reports, quiet=False) as (client, root):
            client.wait_text('IGS notes'); client.key('enter'); marker = 1000
            words = [row[:] for row in ORIGINAL]

            def send(peer, wire):
                with (root/'wire.bin').open('ab') as log:
                    log.write(wire)
                peer.sendall(wire)

            def matches(frame, mask, profiles):
                s = frame.application
                return s.get('cterm.igs_sound_voices') == mask and all(
                    s.get(f'cterm.igs_sound_voice{voice}_{field}') == value
                    for voice, profile in profiles.items() for field, value in profile.items())

            def inspect(frame, mask, profiles, active=False, voice=0, stop=0):
                global CAPTURES
                check(frame, resolution, status, 4, 'A', False, False, False)
                s = frame.application
                assert matches(frame, mask, profiles)
                assert s['cterm.igs_sound_patches'] == fingerprint(words)
                assert s['cterm.igs_sound_saved'] == fingerprint(ORIGINAL)
                assert not s['cterm.igs_sound_output']
                assert s['cterm.igs_note_active'] == s['cterm.igs_input_paused'] == active
                assert s['cterm.igs_note_voice'] == voice and s['cterm.igs_note_stop'] == stop
                assert not s['cterm.igs_note_key']
                CAPTURES += 1

            def capture(mask=0, profiles=None):
                profiles = profiles or {}
                frame = client.wait_for(lambda f: f.application.get('cterm.igs_draw_x') == marker
                                        and not f.application.get('cterm.igs_note_active')
                                        and matches(f, mask, profiles))
                inspect(frame, mask, profiles)

            def scene(peer, wire, mask=0, profiles=None):
                nonlocal marker
                marker += 1
                send(peer, wire+f'G#X 10,{marker},777:'.encode())
                capture(mask, profiles)

            def setup(peer):
                client.wait_for(lambda f: f.application.get('connected') == 1)
                client.resize(1280, 960)
                scene(peer, b'G#c>1,3:c 0,0:\x1bE\x1bfG#p 3,2:AG#b 23,5:')

            def waiting(peer, stop, scaling=False):
                nonlocal marker
                previous = marker; marker += 1
                wire = (f'G#g>2:n>0,1,10,60,9999,{stop}:g 0:' if scaling
                        else f'G#n 0,0,15,60,9999,{stop}:').encode()
                send(peer, wire+f'G#X 10,{marker},777:'.encode())
                voice = 1 if scaling else 0; volume = 10 if scaling else 15
                profiles = {voice: dict(priority=7, control=60, volume=volume, duration=2)}
                frame = client.wait_for(lambda f: f.application.get('cterm.igs_note_active') == 1
                                        and f.application.get('cterm.igs_draw_x') == previous
                                        and matches(f, 1 << voice, profiles))
                inspect(frame, 1 << voice, profiles, True, voice, stop)

            def disconnect(peer):
                peer.shutdown(socket.SHUT_RDWR)
                client.wait_text('Disconnected: Enter exits, F1 details')
                client.key('enter'); client.wait_text('Directory: Name')

            with listener.accept()[0] as peer:
                setup(peer)
                edits = []
                for i, value in enumerate(CUSTOM):
                    negative = value >= 32768; magnitude = 65536-value if negative else value
                    edits.append(f'G#b 20,0,0,{i},{int(negative)},0,{magnitude}:')
                words[0] = CUSTOM[:]
                scene(peer, ''.join(edits).encode())
                held = {0: dict(priority=7, control=60, volume=10, duration=2)}
                scene(peer, b'G#n 0,0,10,60,0,0:', 1, held)
                scene(peer, b'G#n 125,0,15,70,2,0:', 1, held)
                scene(peer, b'G#n 0,0,15,0,0,0:', 1, held)
                released = {0: dict(priority=0, control=-1, volume=10, phase=4)}
                scene(peer, b'G#n 125,0,0,0,1,1:', 1, released)
                scene(peer, b'G#n 125,0,0,0,0,2:')
                all_voices = {i: dict(priority=7, control=pitch, volume=volume, duration=2)
                              for i, volume, pitch in ((0,15,84),(1,10,60),(2,5,36))}
                scene(peer, b'G#n>0,0,15,84,0,0:n>0,1,10,60,0,0:n 0,2,5,36,0,0:', 7, all_voices)
                scene(peer, b'G#n 125,1,0,0,1,3:', 7,
                      {i: dict(priority=0, control=-1, phase=4) for i in range(3)})
                scene(peer, b'G#n 125,1,0,0,0,4:')
                scene(peer, b'G#n 126,9,99,300,0,4:', profiles={2: dict(priority=0, control=255, volume=15, duration=0)})
                waiting(peer, 2, True)
                client.key(ord('+')); capture()
                peer.settimeout(0.03)
                try:
                    reply = peer.recv(1)
                except socket.timeout:
                    pass
                else:
                    raise AssertionError(f'n cancellation leaked a key/flow reply: {reply!r}')
                scene(peer, b'G#n 0,0,15,60,1,2:')
                client.key(ord('a')); peer.settimeout(3); assert peer.recv(1) == b'a'
                waiting(peer, 0)
                disconnect(peer)

            client.key('enter'); words = [row[:] for row in ORIGINAL]
            with listener.accept()[0] as peer:
                setup(peer)
                scene(peer, b'G#n 0,0,15,60,0,4:')
                disconnect(peer)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('executable')
    parser.add_argument('--reports', default='reports/test-control'); args = parser.parse_args()
    for resolution in range(3):
        for encoding in ('ATARI-ST', 'CP437', 'UTF-8'):
            for status in (False, True):
                run(args.executable, args.reports, resolution, encoding, status)
    assert CAPTURES == 306, CAPTURES
