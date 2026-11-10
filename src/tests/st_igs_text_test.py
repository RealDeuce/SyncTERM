#!/usr/bin/env python3
"""W: independent full scenes, opaque bytes, console replacement and recovery."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import json
import socket
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'/'testing'))
from syncterm_fixture import fixture,cell_at
from st_igs_resolution_test import initial
from st_igs_text_attributes_test import check_text,SIZES
from st_igs_box_test import check as check_blank
from st_igs_line_test import check as check_line
VECTORS=json.loads((Path(__file__).parent/'igs'/'text-vectors.json').read_text())


def check(frame,resolution,status,token,case):
    m,state=frame.metadata,frame.application
    width=320 if resolution==0 else 640;height=400 if resolution==2 else 200;ch=16 if resolution==2 else 8
    drawable=height-(ch if status else 0)
    assert (m['plane_width'],m['plane_height'])==(width,height)
    assert state['cterm.graphics_mode']==5 and state['cterm.igs_resolution']==resolution
    assert (state['cterm.x'],state['cterm.y'])==(len(token)+1,1)
    assert tuple(frame.palette[768:784])==tuple(initial(resolution))
    expected=[768]*(width*drawable);samples={(20,20)}
    for y,x,count,value in case['runs']:
        if not ch<=y<drawable:continue
        expected[y*width+x:y*width+x+count]=[768+value]*count
        samples.add((x,y));samples.add((min(width-1,x+count),y))
    assert tuple(state[f'cterm.igs_{name}'] for name in ('draw_start_x','draw_start_y','draw_x','draw_y'))==(17,23,37,47)
    assert tuple(state[f'cterm.igs_{name}'] for name in ('line_type','line_width','line_begin','line_end'))==(3,1,1,1)
    assert tuple(state[f'cterm.igs_fill_{name}'] for name in ('interior','index','perimeter','pen'))==(1,1,True,1)
    assert state['cterm.igs_writing_mode']==case['mode'] and state['cterm.igs_last_line']==case['last_line']
    assert (state['cterm.igs_random_seed'],state['cterm.igs_pattern_seed'])==(1,0)
    assert tuple(state[f'cterm.igs_user_fill_{i}'] for i in range(16))==(0xffff,)*16
    check_text(frame,case['style'],case['points'],dict(SIZES)[case['points']],case['rotation']*900,pen=7 if resolution==0 else 1)
    assert state['cterm.igs_shared_text_size']==case['collected_size']
    assert state['cterm.igs_text_counter']==0 and state['cterm.igs_text_deadline']==0
    for y in range(1,24 if status else 25):
        for x in range(width//8):assert cell_at(frame,x,y).glyph==(ord('Z') if (x,y)==(12,18) else 32)
    if status:assert all(not cell_at(frame,x,24).flags&8 for x in range(width//8))
    start,end=ch*width,drawable*width
    for plane in (frame.primary,frame.alternate):
        observed=list(plane[m['pixel_top']*width+start:m['pixel_top']*width+end])
        assert observed==expected[start:end],(resolution,status,token,next((i%width,i//width+ch,a,b)
            for i,(a,b) in enumerate(zip(observed,expected[start:end])) if a!=b))
    for x,y in samples:
        ox=m['destination_x']+(2*x+1)*m['destination_width']//(2*width)
        oy=m['destination_y']+(2*(y+m['pixel_top'])+1)*m['destination_height']//(2*height)
        assert frame.rgb[oy*m['output_width']+ox]&0xffffff==frame.palette[expected[y*width+x]],(token,x,y)


def run(executable,reports,resolution,encoding,status):
    screen=('AtariST40x25','AtariST80x25','AtariST80x25Mono')[resolution]
    with socket.socket() as listener:
        listener.bind(('127.0.0.1',0));listener.listen(2);listener.settimeout(15)
        entries=('[IGS graphical text]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                 f'Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\nScreenMode={screen}\n'
                 f'Font=Atari ST\nEncoding={encoding}\nNoStatus={str(not status).lower()}\n')
        with fixture(executable,entries=entries,settings='AudioModes=0\nMidiModes=0\nBlockyScaling=true\n',reports=reports,quiet=False) as (client,root):
            client.wait_text('IGS graphical text');client.key('enter')
            with listener.accept()[0] as peer:
                client.wait_for(lambda s:s.application.get('connected')==1);client.resize(1920,1440)
                def send(wire):
                    with (root/'wire.bin').open('ab') as stream:stream.write(wire)
                    peer.sendall(wire)
                for index,case in enumerate(c for c in VECTORS['cases'] if c['resolution']==resolution):
                    token=f'GLYPH{index:02d}';payload=bytes(case['payload']);x,y=case['origin']
                    prefix=b'\x1bf\x1bE'+(b'' if case['text_after'] else b'\x1bY2,Z')
                    prefix+=b'G#g>0:T>2,1,1:T>2,1,0:M>4:L>17,23,37,47:T>2,3,1:T>2,3,50:'
                    # A mode4 solid fill is visually inert and leaves LastLine false.
                    if not case['last_line']:prefix+=b'A>1,1,0:M>4:B>0,0,0,0,1:A>1,1,1:'
                    prefix+=f'C>3,7:E>{case["style"]},{case["points"]},{case["rotation"]}:M>{case["mode"]}:W {x},{y},'.encode()
                    send(prefix+payload+(b'@' if len(payload)<128 else b'')+(b'\x1bY2,Z' if case['text_after'] else b'')+b'\x1bH'+token.encode())
                    check(client.wait_text(token),resolution,status,token,case)
                send(b'\x1bf\x1bE\x1bY2,ZG#g>0:C>1,10:T>2,1,1:T>2,1,0:M>4:L>17,23,37,47:C>3,0:E>0,9,0:M>2:W>80,80,A@M>1:D 57,47:\x1bHFOLLOW')
                following=dict(points=((57,47),(57,47)),style=1,width=1,caps=(0,0),mode=1,runs=[(47,37,21,9&(15,3,1)[resolution])])
                check_line(client.wait_text('FOLLOW'),resolution,status,'FOLLOW','vector',case=following)
                peer.settimeout(0.03)
                try:reply=peer.recv(1)
                except socket.timeout:pass
                else:raise AssertionError(f'unexpected ordinary W reply: {reply!r}')
                send(b'G#W 80,80,PARTIAL');peer.shutdown(socket.SHUT_RDWR)
                client.wait_text('Disconnected: Enter exits, F1 details');client.key('enter')
            client.wait_text('Directory: Name');client.key('enter')
            with listener.accept()[0] as peer:
                client.wait_for(lambda s:s.application.get('connected')==1);client.resize(1920,1440)
                peer.sendall(b'\x1bf\x1bE\x1bY2,Z\x1bHFRESH');frame=client.wait_text('FRESH')
                check_blank(frame,resolution,status,'FRESH',fresh=True)
                point,top=(10,13) if resolution==2 else (9,6);check_text(frame,0,point,top,0,point_mode=False)
                assert frame.application['cterm.igs_shared_text_size']==0 and frame.application['cterm.igs_text_counter']==0
                peer.shutdown(socket.SHUT_RDWR);client.wait_text('Disconnected: Enter exits, F1 details');client.key('enter')
            client.wait_text('Directory: Name');assert b'[wren' not in client.stderr,bytes(client.stderr)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('executable');parser.add_argument('--reports',default='reports/test-control');args=parser.parse_args()
    for resolution in range(3):
        for encoding in ('ATARI-ST','CP437','UTF-8'):
            for status in (False,True):run(args.executable,args.reports,resolution,encoding,status)
