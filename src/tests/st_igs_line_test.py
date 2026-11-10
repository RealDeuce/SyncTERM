#!/usr/bin/env python3
"""L through transport: independent source-derived scenes and coherent RGB.

Native geometry provenance is recorded with the checked-in line vectors.
Console overlay rows and border-line expectations below are literal facts.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import json
import socket
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'testing'))
from syncterm_fixture import fixture, cell_at
from st_igs_init_test import Z8, Z16
from st_igs_resolution_test import initial

VECTORS = json.loads((Path(__file__).parent / 'igs' / 'line-vectors.json').read_text())
END = ((0,52,62),(51,50,53),(61,54,60))


def expected_console_replacement(case,resolution):
    # The console writes the complete Z cell after graphics in marked cases.
    # Remove covered native runs, then let the shared literal glyph recipe
    # supply that cell. This changes no observed capture.
    expected=dict(case)
    if case.get('text_after',False):
        ch=16 if resolution==2 else 8
        runs=[]
        for y,x,count,value in case['runs']:
            if 18*ch<=y<19*ch:
                for left,right in ((x,min(x+count,96)),(max(x,104),x+count)):
                    if left<right:runs.append((y,left,right-left,value))
            else:runs.append((y,x,count,value))
        expected['runs']=runs
    return expected



def expected_retained_outline(case,resolution):
    return dict(expected_console_replacement(case,resolution),points=((17,23),(37,47)))


def check(frame, resolution, status, token, scenario, case=None, mode=1, drawto=False):
    m, state = frame.metadata, frame.application
    width, height, ch = (320 if resolution == 0 else 640), (400 if resolution == 2 else 200), (16 if resolution == 2 else 8)
    mask = (15,3,1)[resolution]
    drawable = height - (ch if status else 0)
    assert (m['plane_width'], m['plane_height']) == (width, height)
    assert state['cterm.igs_resolution'] == resolution and state['cterm.graphics_mode'] == 5
    assert (state['cterm.x'], state['cterm.y']) == (len(token)+1, 1)
    assert tuple(frame.palette[768:784]) == tuple(initial(resolution))
    expected = [768]*(width*drawable)
    samples = {(20,20)}
    assert cell_at(frame, 12, 18).glyph == ord('Z')
    for y, row in enumerate(Z16 if resolution == 2 else Z8):
        for x in range(8):
            samples.add((96+x,18*ch+y))
            if row & (128 >> x): expected[(18*ch+y)*width+96+x] = 768+mask
    positions = (0,0,0,0)
    attributes = (1,1,0,0)
    if scenario == 'vector':
        assert case is not None
        mode = case['mode']
        foreground = (9 if resolution == 0 else mask) if mode != 3 else mask
        positions = (*case['points'][0], *case['points'][1])
        attributes = (case['style'],case['width'],*case['caps'])
        for y, x, count, value in case['runs']:
            assert value != 0 and ch <= y < drawable
            expected[y*width+x:y*width+x+count] = [768+foreground]*count
            samples.update((column,y) for column in range(x-1,x+count+1) if 0 <= column < width)
        assert state['cterm.igs_line_pen'] == (10 if resolution == 0 else 1)
    elif scenario == 'overlay':
        positions = (94,18*ch+2,106,18*ch+2)
        attributes = (3,1,0,0)
        y = positions[1]
        for x in range(94,107):
            offset = y*width+x
            pattern = bool(0xc0c0 & (0x8000 >> (x % 16)))
            if mode == 1: expected[offset] = 768 + (mask if pattern else 0)
            elif mode == 2 and pattern: expected[offset] = 768+mask
            elif mode == 3 and pattern: expected[offset] = 768 + ((expected[offset]-768) ^ mask)
            elif mode == 4 and not pattern: expected[offset] = 768+mask
            samples.add((x,y))
        assert cell_at(frame,12,18).flags & 8
    elif scenario == 'edge':
        positions = (0,height-1,width-1,height-1)
        if not status:
            expected[(height-1)*width:height*width] = [768+mask]*width
            samples.update((x,height-1) for x in range(width))
        assert state['cterm.igs_scaling_mode'] == 1
    elif scenario != 'fresh':
        raise AssertionError(scenario)
    if drawto:
        positions = positions[2:]+positions[2:]
    assert tuple(state[f'cterm.igs_{name}'] for name in ('draw_start_x','draw_start_y','draw_x','draw_y')) == positions
    assert tuple(state[f'cterm.igs_{name}'] for name in ('line_type','line_width','line_begin','line_end')) == attributes
    assert state['cterm.igs_user_line'] == 0xffff
    assert state['cterm.igs_writing_mode'] == mode and state['cterm.igs_last_line'] == 1
    assert (state['cterm.igs_marker_type'], state['cterm.igs_marker_height']) == (1,11)
    for y in range(1,24 if status else 25):
        for x in range(width//8):
            assert cell_at(frame,x,y).glyph == (ord('Z') if (x,y) == (12,18) else 32)
    start, end = ch*width, drawable*width
    for plane in (frame.primary,frame.alternate):
        observed = list(plane[m['pixel_top']*width+start:m['pixel_top']*width+end])
        assert observed == expected[start:end], (resolution,status,scenario,mode,
            next((i%width,i//width+ch,a,b) for i,(a,b) in enumerate(zip(observed,expected[start:end])) if a != b))
    if status:
        assert all(not cell_at(frame,x,24).flags & 8 for x in range(width//8))
    # Repeated pixel centres avoid interpolation at fractional edges. All
    # line pixels, their adjacent background, and the console glyph use the
    # same capture as the complete raw scene/state/cell checks above.
    assert m['destination_width'] >= 3*width and m['destination_height'] >= 3*height
    for x,y in samples:
        ox = m['destination_x']+(2*x+1)*m['destination_width']//(2*width)
        oy = m['destination_y']+(2*(y+m['pixel_top'])+1)*m['destination_height']//(2*height)
        assert frame.rgb[oy*m['output_width']+ox] & 0xffffff == frame.palette[expected[y*width+x]], (scenario,x,y)


def run(executable, reports, resolution, encoding, status):
    screen = ('AtariST40x25','AtariST80x25','AtariST80x25Mono')[resolution]
    with socket.socket() as listener:
        listener.bind(('127.0.0.1',0)); listener.listen(2); listener.settimeout(15)
        entries = ('[IGS lines]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                   f'Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\n'
                   f'ScreenMode={screen}\nFont=Atari ST\nEncoding={encoding}\n'
                   f'NoStatus={str(not status).lower()}\n')
        with fixture(executable,entries=entries,settings='AudioModes=0\nMidiModes=0\nBlockyScaling=true\n',
                     reports=reports,quiet=False) as (client,root):
            client.wait_text('IGS lines'); client.key('enter')
            with listener.accept()[0] as peer:
                client.wait_for(lambda s: s.application.get('connected') == 1)
                client.resize(1920,1440)
                for number,case in enumerate(c for c in VECTORS['cases'] if c['resolution'] == resolution):
                    token = f'LINE{number:02d}'
                    x1,y1 = case['points'][0]; x2,y2 = case['points'][1]
                    begin,end = case['caps']; style = case['style']
                    wire = (b'\x1bf\x1bE\x1bY2,Z' +
                            f'G#g>0:C>1,10:T>2,{style},{case["width"]}:T>2,{style},{END[begin][end]}:'.encode() +
                            f'M>4:P>9999,9999:M>{case["mode"]}:L {x1},{y1},{x2},{y2}:'.encode() +
                            b'\x1bH' + token.encode())
                    with (root/'wire.bin').open('ab') as stream: stream.write(wire)
                    peer.sendall(wire)
                    check(client.wait_text(token),resolution,status,token,'vector',case=case)
                ch = 16 if resolution == 2 else 8
                for mode in range(1,5):
                    token = f'OVER{mode}'
                    peer.sendall(b'\x1bf\x1bE\x1bY2,Z' +
                                 f'G#g>0:C>1,1:T>2,3,1:T>2,3,0:M>{mode}:L 94,{18*ch+2},106,{18*ch+2}:'.encode() +
                                 b'\x1bH' + token.encode())
                    check(client.wait_text(token),resolution,status,token,'overlay',mode=mode)
                peer.sendall(b'\x1bf\x1bE\x1bY2,ZG#g>1:T>2,1,1:T>2,1,0:M>1:L 0,9999,9999,9999:\x1bHEDGE')
                check(client.wait_text('EDGE'),resolution,status,'EDGE','edge')
                peer.settimeout(0.03)
                try: reply = peer.recv(1)
                except socket.timeout: pass
                else: raise AssertionError(f'unexpected IGS reply: {reply!r}')
                peer.sendall(b'G#L 1,2,3,')
                peer.shutdown(socket.SHUT_RDWR)
                client.wait_text('Disconnected: Enter exits, F1 details'); client.key('enter')
            client.wait_text('Directory: Name'); client.key('enter')
            with listener.accept()[0] as peer:
                client.wait_for(lambda s: s.application.get('connected') == 1)
                client.resize(1920,1440)
                peer.sendall(b'\x1bf\x1bE\x1bY2,Z\x1bHFRESH')
                check(client.wait_text('FRESH'),resolution,status,'FRESH','fresh')
                peer.shutdown(socket.SHUT_RDWR)
                client.wait_text('Disconnected: Enter exits, F1 details'); client.key('enter')
            client.wait_text('Directory: Name')
            assert b'[wren' not in client.stderr,bytes(client.stderr)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('executable'); parser.add_argument('--reports',default='reports/test-control')
    args = parser.parse_args()
    for resolution in range(3):
        for encoding in ('ATARI-ST','CP437','UTF-8'):
            for status in (False,True): run(args.executable,args.reports,resolution,encoding,status)
