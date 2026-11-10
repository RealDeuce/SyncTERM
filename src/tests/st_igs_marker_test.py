#!/usr/bin/env python3
"""P transport, literal marker pixels, text overlays and coherent RGB captures.

Size-one rows are derived from reference 43/44/52, independent of SyncTERM.
Row bit zero is the left pixel; no generated reference images are used.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import socket
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'testing'))
from syncterm_fixture import fixture, cell_at
from st_igs_init_test import Z8, Z16
from st_igs_resolution_test import initial

ROWS = (
    (0, 0, 0, 0x10, 0, 0, 0),
    (0x10, 0x10, 0x10, 0x1ff, 0x10, 0x10, 0x10),
    (0x10, 0x92, 0x7c, 0x10, 0x7c, 0x92, 0x10),
    (0x1ff, 0x101, 0x101, 0x101, 0x101, 0x101, 0x1ff),
    (0x101, 0xc2, 0x2c, 0x10, 0x2c, 0xc2, 0x101),
    (0x10, 0x2c, 0xc2, 0x101, 0xc2, 0x2c, 0x10),
)
XOR_ROWS = (ROWS[0], (0x10, 0x10, 0x10, 0x1ef, 0x10, 0x10, 0x10),
            ROWS[2], (0x1fe,) + ROWS[3][1:],
            ROWS[4][:3] + (0,) + ROWS[4][4:],
            (0x10, 0x2c, 0xc2, 0, 0xc2, 0x2c, 0))


def check(frame, actual, entry, status, mode, token, scenario, foreground=None):
    m, state = frame.metadata, frame.application
    width, height, ch = (320 if actual == 0 else 640), (400 if actual == 2 else 200), (16 if actual == 2 else 8)
    mask = (15, 3, 1)[actual]
    foreground = mask if foreground is None else foreground
    drawable = height - (ch if status else 0)
    assert (m['plane_width'], m['plane_height']) == (width, height)
    assert state['cterm.igs_resolution'] == actual and state['cterm.graphics_mode'] == 5
    assert (state['cterm.x'], state['cterm.y']) == (len(token) + 1, 1)
    assert tuple(frame.palette[768:784]) == tuple(initial(entry))
    assert cell_at(frame, 12, 12).glyph == ord('Z')
    expected = [768] * (width * drawable)
    for y, row in enumerate(Z16 if actual == 2 else Z8):
        for x in range(8):
            if row & (128 >> x):
                expected[(12*ch+y)*width+96+x] = 768+mask

    def put(x, y, xor=False):
        if 0 <= x < width and ch <= y < drawable:
            offset = y*width+x
            expected[offset] = 768 + ((expected[offset]-768) ^ mask) if xor else 768+foreground

    if scenario in ('shapes', 'retained'):
        if mode != 4:
            for shape, rows in enumerate(XOR_ROWS if mode == 3 else ROWS):
                for y, row in enumerate(rows):
                    for x in range(9):
                        if row & (1 << x):
                            put(32+shape*32+x-4, 60+y-3, mode == 3)
            # Plus crosses an existing console glyph without changing its cell.
            for y in range(-3, 4):
                put(100, 12*ch+3+y, mode == 3)
            for x in range(-4, 5):
                # XOR visits the centre twice, so leave its original pixel.
                put(100+x, 12*ch+3, mode == 3)
        assert (state['cterm.igs_draw_x'], state['cterm.igs_draw_y']) == (100, 12*ch+3)
        assert state['cterm.igs_marker_type'] == 2 and state['cterm.igs_marker_height'] == 11
    elif scenario == 'boundary':
        for y in range(height-25, height):
            put(0, y)
        for x in range(33):
            put(x, height-1)
        put(width-1, height-1)
        assert (state['cterm.igs_draw_x'], state['cterm.igs_draw_y']) == (width-1, height-1)
        assert state['cterm.igs_marker_type'] == 1 and state['cterm.igs_scaling_mode'] == 1
    elif scenario == 'fresh':
        assert (state['cterm.igs_draw_x'], state['cterm.igs_draw_y']) == (0, 0)
        assert state['cterm.igs_marker_type'] == 1 and state['cterm.igs_marker_height'] == 11
    else:
        raise AssertionError(scenario)

    assert state['cterm.igs_writing_mode'] == mode
    assert tuple(state[f'cterm.igs_{key}'] for key in ('line_type', 'line_width', 'line_begin', 'line_end', 'user_line')) == (
        (1, 1, 0, 0, 0xffff) if scenario == 'fresh' else (7, 1, 2, 2, 0xaaaa))
    start, end = ch*width, drawable*width
    for plane in (frame.primary, frame.alternate):
        observed = list(plane[m['pixel_top']*width+start:m['pixel_top']*width+end])
        assert observed == expected[start:end], (entry, actual, status, scenario, mode,
            next((i % width, i // width+ch, a, b) for i, (a, b) in
                 enumerate(zip(observed, expected[start:end])) if a != b))
    if status:
        assert all(not cell_at(frame, x, 24).flags & 8 for x in range(width//8))
    # At least three repeated output pixels per source pixel keep these
    # centres clear of the presenter's fractional edge interpolation.
    assert m['destination_width'] >= 3*width and m['destination_height'] >= 3*height
    # Check the composed output at every scene pixel centre in the same frame.
    for y in range(ch, drawable):
        oy = m['destination_y'] + (2*(y+m['pixel_top'])+1)*m['destination_height']//(2*height)
        for x in range(width):
            ox = m['destination_x'] + (2*x+1)*m['destination_width']//(2*width)
            wanted = frame.palette[expected[y*width+x]]
            assert frame.rgb[oy*m['output_width']+ox] & 0xffffff == wanted, (scenario, mode, x, y)


def shapes(ch):
    result = b''.join(f'G#T>1,{i+1},1:P {32+i*32},60:'.encode() for i in range(6))
    return result + f'G#T>1,2,1:P 100,{12*ch+3}:'.encode()


def run(executable, reports, entry, encoding, status):
    screen = ('AtariST40x25', 'AtariST80x25', 'AtariST80x25Mono')[entry]
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0)); listener.listen(2); listener.settimeout(15)
        entries = ('[IGS markers]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                   f'Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\n'
                   f'ScreenMode={screen}\nFont=Atari ST\nEncoding={encoding}\n'
                   f'NoStatus={str(not status).lower()}\n')
        with fixture(executable, entries=entries, settings='AudioModes=0\nMidiModes=0\nBlockyScaling=true\n',
                     reports=reports, quiet=False) as (client, root):
            client.wait_text('IGS markers'); client.key('enter')
            with listener.accept()[0] as peer:
                client.wait_for(lambda s: s.application.get('connected') == 1)
                client.resize(1920, 1440)
                for mode in range(1, 5):
                    ch = 16 if entry == 2 else 8
                    token = f'MARK{mode}'
                    # Marker drawing must ignore line pattern, width and ends.
                    wire = (b'\x1bf\x1bE\x1bY,,ZG#g>0:C>0,1:T>2,1,39:T>2,7,1:T 2,7,60:'
                            + f'G#M {mode}:'.encode() + shapes(ch) + b'\x1bH' + token.encode())
                    with (root / 'wire.bin').open('ab') as stream:
                        stream.write(wire)
                    peer.sendall(wire)
                    check(client.wait_text(token), entry, entry, status, mode, token, 'shapes')
                height = 400 if entry == 2 else 200
                token = 'EDGES'
                peer.sendall(b'\x1bf\x1bE\x1bY,,ZG#M>1:g>0:T 1,2,8:'
                             + f'G#P 0,{height-1}:'.encode()
                             + b'G#g>1:T>1,1,1:P 9999,9999:\x1bH' + token.encode())
                check(client.wait_text(token), entry, entry, status, 1, token, 'boundary')
                other = 1-entry if entry != 2 else 2
                ch = 16 if other == 2 else 8
                token = 'RETAIN'
                peer.sendall(f'G#C>0,15:R {other},0:'.encode()
                             + b'\x1bf\x1bE\x1bY,,ZG#g 0:' + shapes(ch) + b'\x1bH' + token.encode())
                check(client.wait_text(token), other, entry, status, 1, token, 'retained',
                      (13 if entry == 0 else 15) & (15, 3, 1)[other])
                peer.settimeout(0.03)
                try:
                    reply = peer.recv(1)
                except socket.timeout:
                    pass
                else:
                    raise AssertionError(f'unexpected IGS reply: {reply!r}')
                peer.sendall(b'G#P 10,')
                peer.shutdown(socket.SHUT_RDWR)
                client.wait_text('Disconnected: Enter exits, F1 details'); client.key('enter')
            client.wait_text('Directory: Name'); client.key('enter')
            with listener.accept()[0] as peer:
                client.wait_for(lambda s: s.application.get('connected') == 1)
                client.resize(1920, 1440)
                peer.sendall(b'\x1bf\x1bE\x1bY,,Z\x1bHFRESH')
                check(client.wait_text('FRESH'), entry, entry, status, 1, 'FRESH', 'fresh')
                peer.shutdown(socket.SHUT_RDWR)
                client.wait_text('Disconnected: Enter exits, F1 details'); client.key('enter')
            client.wait_text('Directory: Name')
            assert b'[wren' not in client.stderr, bytes(client.stderr)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('executable')
    parser.add_argument('--reports', default='reports/test-control')
    args = parser.parse_args()
    for entry in range(3):
        for encoding in ('ATARI-ST', 'CP437', 'UTF-8'):
            for status in (False, True):
                run(args.executable, args.reports, entry, encoding, status)
