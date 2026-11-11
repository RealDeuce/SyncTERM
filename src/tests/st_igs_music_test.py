#!/usr/bin/env python3
"""N: independent retained bytes/notes, opaque loads and coherent scenes."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import socket
from st_igs_cursor_test import check, fixture
from st_igs_effect_test import ORIGINAL, fingerprint
from st_igs_note_test import CUSTOM

CAPTURES = 0


def byte_hash(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte)*16777619) & 0xffffffff
    return value


def run(executable, reports, resolution, encoding, status):
    global CAPTURES
    screen = ('AtariST40x25', 'AtariST80x25', 'AtariST80x25Mono')[resolution]
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0)); listener.listen(2); listener.settimeout(15)
        entries = ('[IGS buffered music]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                   f'Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\nScreenMode={screen}\n'
                   f'Font=Atari ST\nEncoding={encoding}\nNoStatus={str(not status).lower()}\n')
        with fixture(executable, entries=entries, settings='AudioModes=0\nMidiModes=0\nBlockyScaling=true\n',
                     reports=reports, quiet=False) as (client, root):
            client.wait_text('IGS buffered music'); client.key('enter'); marker = 1000
            buffer = bytearray(10001); count = 0; words = [row[:] for row in ORIGINAL]

            def send(peer, wire):
                with (root/'wire.bin').open('ab') as log:
                    log.write(wire)
                peer.sendall(wire)

            def inspect(frame, mask=0, active=False, loading=False, note=False):
                global CAPTURES
                check(frame, resolution, status, 4, 'A', False, False, False)
                s = frame.application
                assert s['cterm.igs_music_buffer'] == byte_hash(buffer)
                assert s['cterm.igs_music_count'] == count
                assert s['cterm.igs_script_length'] == 0 and s['cterm.igs_midi_enabled'] == 1
                assert s['cterm.igs_music_loading'] == loading and s['cterm.igs_music_playing'] == active
                assert s['cterm.igs_input_paused'] == active and s['cterm.igs_note_active'] == note
                assert s['cterm.igs_sound_voices'] == mask and not s['cterm.igs_sound_output']
                assert s['cterm.igs_sound_patches'] == fingerprint(words)
                assert s['cterm.igs_sound_saved'] == fingerprint(ORIGINAL)
                CAPTURES += 1

            def scene(peer, wire, mask=0, flow=False):
                nonlocal marker
                marker += 1
                send(peer, wire+f'G#X 10,{marker},777:'.encode())
                frame = client.wait_for(lambda f: f.application.get('cterm.igs_draw_x') == marker
                                        and not f.application.get('cterm.igs_music_playing'))
                inspect(frame, mask)
                if flow:
                    peer.settimeout(3)
                    reply = b''
                    while len(reply) < 2:
                        reply += peer.recv(2-len(reply))
                    assert reply == b'\x13\x11', reply
                return frame

            def load(peer, op, payload, mask=0, flow=False):
                nonlocal count
                buffer[:len(payload)] = payload; count = len(payload)
                return scene(peer, f'G#N {op},{count}:'.encode()+payload, mask, flow)

            def setup(peer):
                client.wait_for(lambda f: f.application.get('connected') == 1)
                client.resize(1280, 960)
                scene(peer, b'G#c>1,3:c 0,0:\x1bE\x1bfG#p 3,2:AG#b 23,5:')

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
                words[0] = CUSTOM[:]; scene(peer, ''.join(edits).encode())
                # This payload must neither clear the screen nor change modes.
                load(peer, 0, b'\x00\x03\x18\x1bEG#R 2,0:\r\n\xff>||}')
                records = bytes((0,0,15,60,0,0, 0,1,10,64,0,0))
                load(peer, 4, records, 3, True)
                scene(peer, b'G#N 5:', 3, True)
                load(peer, 4, b'\x00', 3, True)  # Five retained operands.
                scene(peer, b'G#N 5:', 3)  # Count <=5 skips N5.
                load(peer, 0, bytes((125,0,0,0,0,0)), 3)
                scene(peer, b'G#N 6,1,2:', 3, True)  # Retained second record.
                scene(peer, b'G#N 6,0,1666:', 3)
                load(peer, 1, bytes((0xf8,0,0xfa,0,0x7f)), 3, True)  # Odd tail not sent.
                scene(peer, b'G#N 2:', 3, True)
                # Positive chip wait owns only its inner key. The opaque queued
                # input resumes after the complete record helper and N loop.
                payload = bytes((0,2,5,67,255,2, 125,0,0,0,0,0))
                buffer[:len(payload)] = payload; count = len(payload); previous = marker; marker += 1
                send(peer, f'G#N 4,{count}:'.encode()+payload+f'G#X 10,{marker},777:'.encode())
                frame = client.wait_for(lambda f: f.application.get('cterm.igs_music_playing') == 1
                                        and f.application.get('cterm.igs_note_active') == 1
                                        and f.application.get('cterm.igs_draw_x') == previous)
                inspect(frame, 7, True, note=True)
                assert frame.application['cterm.igs_music_index'] == 6
                client.key(ord('+'))
                frame = client.wait_for(lambda f: f.application.get('cterm.igs_draw_x') == marker)
                inspect(frame, 3)
                peer.settimeout(3); reply = b''
                while len(reply) < 2:
                    reply += peer.recv(2-len(reply))
                assert reply == b'\x13\x11', reply
                client.key(ord('a')); assert peer.recv(1) == b'a'
                # The load abort stores its first byte, clears only the count,
                # and leaves the immediately following graphics command intact.
                count = 2; send(peer, b'G#N 0,2:')
                frame = client.wait_for(lambda f: f.application.get('cterm.igs_music_loading') == 1)
                inspect(frame, 3, loading=True)
                client.key(3)
                client.wait_for(lambda f: f.application.get('cterm.igs_music_keys') == 1)
                buffer[0] = 24; count = 0
                scene(peer, b'\x18', 3)
                # An incomplete load retains accepted data until disconnect;
                # a new session starts from original storage and patch banks.
                count = 8; buffer[:2] = b'xy'; send(peer, b'G#N 0,8:xy')
                frame = client.wait_for(lambda f: f.application.get('cterm.igs_music_received') == 2
                                        and f.application.get('cterm.igs_music_loading') == 1)
                inspect(frame, 3, loading=True)
                disconnect(peer)

            client.key('enter'); buffer = bytearray(10001); count = 0; words = [row[:] for row in ORIGINAL]
            with listener.accept()[0] as peer:
                setup(peer)
                load(peer, 4, b'', 0, True)
                disconnect(peer)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('executable')
    parser.add_argument('--reports', default='reports/test-control'); args = parser.parse_args()
    for resolution in range(3):
        for encoding in ('ATARI-ST', 'CP437', 'UTF-8'):
            for status in (False, True):
                run(args.executable, args.reports, resolution, encoding, status)
    assert CAPTURES == 342, CAPTURES
