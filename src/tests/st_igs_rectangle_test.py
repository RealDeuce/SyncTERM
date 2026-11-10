#!/usr/bin/env python3
"""Z: independent rectangle scenes, fill state and coherent cells/palette/RGB."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import json
import socket
import sys
from pathlib import Path

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'/'testing'))
from syncterm_fixture import fixture, cell_at
from st_igs_init_test import Z8, Z16
from st_igs_resolution_test import initial
from st_igs_fill_test import DEFAULT

VECTORS = json.loads((Path(__file__).parent/'igs'/'rectangle-vectors.json').read_text())


def check(frame, resolution, status, token, case=None, overlay=None, edge=False, fresh=False, following=False,
          user=None, seeds=(1,0)):
    m, state = frame.metadata, frame.application
    width, height, ch, mask = (320 if resolution==0 else 640),(400 if resolution==2 else 200),(16 if resolution==2 else 8),(15,3,1)[resolution]
    drawable = height-(ch if status else 0)
    assert (m['plane_width'],m['plane_height']) == (width,height)
    assert state['cterm.graphics_mode'] == 5 and state['cterm.igs_resolution'] == resolution
    assert (state['cterm.x'],state['cterm.y']) == (len(token)+1,1)
    assert tuple(frame.palette[768:784]) == tuple(initial(resolution))
    expected = [768]*(width*drawable); samples = {(20,20)}
    assert cell_at(frame,12,18).glyph == ord('Z')
    for y,row in enumerate(Z16 if resolution==2 else Z8):
        for x in range(8):
            samples.add((96+x,18*ch+y))
            if row & (128>>x):expected[(18*ch+y)*width+96+x] = 768+mask
    mode, interior, index, perimeter, pen = 1,1,1,True,1
    positions = (0,0,0,0) if fresh else (17,23,37,47)
    line = (1,1,0,0) if fresh or following else (3,1,1,1)
    if case is not None:
        mode,interior,index,pen = case['mode'],case['interior'],case['index'],(7 if resolution==0 else 1)
        foreground = mask if mode==3 else (5 if resolution==0 else mask)
        for y,x,count,value in case['runs']:
            assert value != 0
            if not ch <= y < drawable:continue
            expected[y*width+x:y*width+x+count] = [768+foreground]*count
            samples.update((column,y) for column in range(max(0,x-1),min(width,x+count+1)))
    if user is not None:
        mode,interior,index = user,4,24
        for y in range(40,72):
            for x in range(33,64):
                bit = bool(DEFAULT[y%16] & (0x8000>>(x%16)))
                if (mode in (1,2,3) and bit) or (mode==4 and not bit):expected[y*width+x] = 768+mask
                samples.add((x,y))
    if overlay is not None:
        mode,interior,index = overlay,2,4
        rows = next(c['rows'] for c in VECTORS['cases'] if c['interior']==2 and c['index']==4)
        for y in range(18*ch,18*ch+8):
            for x in range(94,108):
                bit = bool(rows[y%len(rows)] & (0x8000>>(x%16))); offset=y*width+x
                if mode==1:expected[offset] = 768+(mask if bit else 0)
                elif mode==2 and bit:expected[offset] = 768+mask
                elif mode==3 and bit:expected[offset] = 768+((expected[offset]-768)^mask)
                elif mode==4 and not bit:expected[offset] = 768+mask
                samples.add((x,y))
        assert cell_at(frame,12,18).flags & 8
    if edge:
        if not status:
            expected[(height-1)*width:height*width] = [768+mask]*width
            samples.update((x,height-1) for x in range(width))
        assert state['cterm.igs_scaling_mode'] == 1
    if following:
        positions = (57,47,57,47)
        expected[47*width+37:47*width+58] = [768+mask]*21
        samples.update((x,47) for x in range(36,59))
    assert tuple(state[f'cterm.igs_{name}'] for name in ('draw_start_x','draw_start_y','draw_x','draw_y')) == positions
    assert tuple(state[f'cterm.igs_{name}'] for name in ('line_type','line_width','line_begin','line_end')) == line
    assert tuple(state[f'cterm.igs_fill_{name}'] for name in ('interior','index','perimeter','pen')) == (interior,index,perimeter,pen)
    assert state['cterm.igs_writing_mode'] == mode and state['cterm.igs_last_line'] == 1
    assert (state['cterm.igs_random_seed'],state['cterm.igs_pattern_seed']) == seeds
    assert tuple(state[f'cterm.igs_user_fill_{i}'] for i in range(16)) == ((0xffff,)*16 if case is not None or fresh else DEFAULT)
    for y in range(1,24 if status else 25):
        for x in range(width//8):assert cell_at(frame,x,y).glyph == (ord('Z') if (x,y)==(12,18) else 32)
    if status:assert all(not cell_at(frame,x,24).flags & 8 for x in range(width//8))
    start,end = ch*width,drawable*width
    for plane in (frame.primary,frame.alternate):
        observed = list(plane[m['pixel_top']*width+start:m['pixel_top']*width+end])
        assert observed == expected[start:end], (resolution,status,token,next((i%width,i//width+ch,a,b)
                for i,(a,b) in enumerate(zip(observed,expected[start:end])) if a!=b))
    assert m['destination_width'] >= 3*width and m['destination_height'] >= 3*height
    for x,y in samples:
        ox = m['destination_x']+(2*x+1)*m['destination_width']//(2*width)
        oy = m['destination_y']+(2*(y+m['pixel_top'])+1)*m['destination_height']//(2*height)
        assert frame.rgb[oy*m['output_width']+ox]&0xffffff == frame.palette[expected[y*width+x]],(token,x,y)


def run(executable,reports,resolution,encoding,status):
    screen = ('AtariST40x25','AtariST80x25','AtariST80x25Mono')[resolution]
    with socket.socket() as listener:
        listener.bind(('127.0.0.1',0));listener.listen(2);listener.settimeout(15)
        entries = ('[IGS rectangles]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                   f'Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\nScreenMode={screen}\n'
                   f'Font=Atari ST\nEncoding={encoding}\nNoStatus={str(not status).lower()}\n')
        with fixture(executable,entries=entries,settings='AudioModes=0\nMidiModes=0\nBlockyScaling=true\n',
                     reports=reports,quiet=False) as (client,root):
            client.wait_text('IGS rectangles');client.key('enter')
            with listener.accept()[0] as peer:
                client.wait_for(lambda s:s.application.get('connected')==1);client.resize(1920,1440)
                def send(wire):
                    with (root/'wire.bin').open('ab') as stream:stream.write(wire)
                    peer.sendall(wire)
                prefix = b'\x1bf\x1bE\x1bY2,ZG#g>0:T>2,1,1:T>2,1,0:M>4:L>17,23,37,47:T>2,3,1:T>2,3,50:'
                for number,case in enumerate(c for c in VECTORS['cases'] if c['resolution']==resolution):
                    token = f'RECT{number:02d}'; coordinates = ','.join(map(str,case['points']))
                    send(prefix+f'C>2,7:A>{case["interior"]},{case["index"]},1:M>{case["mode"]}:Z {coordinates}:'.encode()+b'\x1bH'+token.encode())
                    check(client.wait_text(token),resolution,status,token,case=case)
                seeds = ((0xbb40e62e,0xbb40e6),(0x5eb5ca17,0x5eb5ca),(0x4c45300c,0x4c4530),(0x58543a1d,0x58543a))
                for mode in range(1,5):
                    token = f'USER{mode}'
                    send(prefix+f'C>2,1:A>2,24,1:A>4,9,1:M>{mode}:Z 33,40,63,71:'.encode()+b'\x1bH'+token.encode())
                    check(client.wait_text(token),resolution,status,token,user=mode,seeds=seeds[mode-1])
                ch = 16 if resolution==2 else 8
                for mode in range(1,5):
                    token = f'OVER{mode}'
                    send(prefix+f'C>2,1:A>2,4,1:M>{mode}:Z 94,{18*ch},107,{18*ch+7}:'.encode()+b'\x1bH'+token.encode())
                    check(client.wait_text(token),resolution,status,token,overlay=mode,seeds=seeds[-1])
                send(prefix+b'C>2,1:A>1,1,1:g>1:M>1:Z 0,9999,9999,9999:\x1bHEDGE')
                check(client.wait_text('EDGE'),resolution,status,'EDGE',edge=True,seeds=seeds[-1])
                send(prefix+b'C>2,1:A>1,1,1:M>4:Z>80,80,100,100:T>2,1,1:T>2,1,0:M>1:D 57,47:\x1bHFOLLOW')
                check(client.wait_text('FOLLOW'),resolution,status,'FOLLOW',following=True,seeds=seeds[-1])
                peer.settimeout(0.03)
                try:reply = peer.recv(1)
                except socket.timeout:pass
                else:raise AssertionError(f'unexpected IGS reply: {reply!r}')
                send(b'G#Z 1,2,3,');peer.shutdown(socket.SHUT_RDWR)
                client.wait_text('Disconnected: Enter exits, F1 details');client.key('enter')
            client.wait_text('Directory: Name');client.key('enter')
            with listener.accept()[0] as peer:
                client.wait_for(lambda s:s.application.get('connected')==1);client.resize(1920,1440)
                peer.sendall(b'\x1bf\x1bE\x1bY2,Z\x1bHFRESH')
                check(client.wait_text('FRESH'),resolution,status,'FRESH',fresh=True)
                peer.shutdown(socket.SHUT_RDWR);client.wait_text('Disconnected: Enter exits, F1 details');client.key('enter')
            client.wait_text('Directory: Name');assert b'[wren' not in client.stderr,bytes(client.stderr)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('executable');parser.add_argument('--reports',default='reports/test-control')
    args=parser.parse_args()
    for resolution in range(3):
        for encoding in ('ATARI-ST','CP437','UTF-8'):
            for status in (False,True):run(args.executable,args.reports,resolution,encoding,status)
