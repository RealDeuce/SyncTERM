#!/usr/bin/env python3
"""X15: independent anchored-line compositions and coherent retained state."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from array import array
import json
from pathlib import Path
import socket

from st_igs_cursor_test import DATA, fixture, initial
from syncterm_fixture import cell_at, check_pixel_planes

VECTORS = json.loads((Path(__file__).parent/'igs'/'sweep-vectors.json').read_text())
ENDS = ((0,52,62),(51,50,53),(61,54,60))
CAPTURES = 0


def inspect(frame,resolution,status,number,position,mode=1,style=1,width=1,caps=(0,0),
            runs=(),overlay=False,following=False,text_after=False,user_line=0xa531,
            scaling=0,active=False,fresh=False,partial=False):
    global CAPTURES
    m,state = frame.metadata,frame.application
    w,h,ch = (320 if resolution==0 else 640),(400 if resolution==2 else 200),(16 if resolution==2 else 8)
    drawable,mask = h-(ch if status else 0),(15,3,1)[resolution]
    assert (m['plane_width'],m['plane_height']) == (w,h)
    assert not m['cursor_visible']
    assert tuple(frame.palette[768:784]) == tuple(initial(resolution))
    assert tuple(state['cterm.igs_'+n] for n in ('draw_start_x','draw_start_y','draw_x','draw_y')) == tuple(position)
    assert tuple(state['cterm.igs_'+n] for n in ('line_type','line_width','line_begin','line_end')) == (style,width,*caps)
    assert state['cterm.igs_user_line'] == user_line
    assert state['cterm.igs_scaling_mode'] == scaling and state['cterm.igs_writing_mode'] == mode
    assert state['cterm.igs_sweep_active'] == state['cterm.igs_input_paused'] == active
    assert state['cterm.igs_sweep_wait'] == (60 if active else 0)
    assert not state['cterm.igs_copy_pending'] and not state['cterm.igs_palette_rotation_active']
    assert state['cterm.igs_memory_route'] == (0 if fresh else 5)
    assert state['cterm.igs_double_step'] == (0 if fresh else 4)
    assert state['cterm.igs_shared_text_size'] == (0 if fresh else 16)
    assert state['cterm.igs_text_counter'] == state['cterm.igs_text_deadline'] == 0
    assert state['cterm.igs_random_seed'] == 1
    expected = array('I',[768])*(w*drawable)
    glyph_rows = DATA['fonts'][str(ch)]['65']
    def glyph(cx,cy):
        for y,bits in enumerate(glyph_rows):
            for x in range(8): expected[(cy+y)*w+cx+x] = 768+(3 & mask if bits & (128>>x) else 0)
    glyph(24,2*ch)
    ink = (9 if resolution==0 else mask) if mode!=3 else mask
    for y,x,count,value in runs:
        assert value and 0 <= y < drawable
        expected[y*w+x:y*w+x+count] = array('I',[768+ink])*count
    if overlay:
        y = 2*ch+2
        for end in (22,26,30):
            for x in range(22,end+1):
                bit = bool(0xc0c0 & (0x8000>>(x%16)))
                offset = y*w+x
                if mode==1: expected[offset] = 768+(mask if bit else 0)
                elif mode==2 and bit: expected[offset] = 768+mask
                elif mode==3 and bit: expected[offset] = 768+((expected[offset]-768)^mask)
                elif mode==4 and not bit: expected[offset] = 768+mask
        if following:
            for x in range(30,35):
                bit = bool(0xc0c0 & (0x8000>>(x%16))); offset = y*w+x
                if mode==1: expected[offset] = 768+(mask if bit else 0)
                elif mode==2 and bit: expected[offset] = 768+mask
                elif mode==3 and bit: expected[offset] = 768+((expected[offset]-768)^mask)
                elif mode==4 and not bit: expected[offset] = 768+mask
        if text_after: glyph(24,2*ch)
    if partial: expected[60*w+24] = 768+ink
    fence = None if active else (8+number,0)
    if fence: glyph(fence[0]*8,0)
    assert (state['cterm.x'],state['cterm.y']) == ((5,3) if active else (10+number,1))
    for y in range(24 if status else 25):
        for x in range(w//8):
            assert cell_at(frame,x,y).glyph == (65 if (x,y)==(3,2) or (x,y)==fence else 32)
    check_pixel_planes(frame,expected,context=(resolution,status,number,mode,style,width,active))
    samples = {(0,0),(w-1,drawable-1)}
    for i,color in enumerate(expected):
        x,y = i%w,i//w
        if color!=768 or (x%64==0 and y%25==0):samples.add((x,y))
    for x,y in samples:
        ox=m['destination_x']+(2*x+1)*m['destination_width']//(2*w)
        oy=m['destination_y']+(2*y+1)*m['destination_height']//(2*h)
        assert frame.rgb[oy*m['output_width']+ox]&0xffffff == frame.palette[expected[y*w+x]]
    CAPTURES += 1


def run(executable,reports,resolution,encoding,status):
    screen=('AtariST40x25','AtariST80x25','AtariST80x25Mono')[resolution]
    ch=16 if resolution==2 else 8
    with socket.socket() as listener:
        listener.bind(('127.0.0.1',0));listener.listen(2);listener.settimeout(15)
        entries=('[IGS sweeps]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                 f'Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\nScreenMode={screen}\n'
                 f'Font=Atari ST\nEncoding={encoding}\nNoStatus={str(not status).lower()}\n')
        with fixture(executable,entries=entries,settings='AudioModes=0\nMidiModes=0\nBlockyScaling=true\n',
                     reports=reports,quiet=False) as (client,root):
            client.wait_text('IGS sweeps');client.key('enter')
            number,user_line=0,0xffff
            def send(peer,wire):
                with (root/'wire.bin').open('ab') as log:log.write(wire)
                peer.sendall(wire)
            def ready():
                f=client.wait_for(lambda f:f.application.get('connected')==1)
                assert not f.application['cterm.igs_sweep_active']
                assert not f.application['cterm.igs_double_step'] and not f.application['cterm.igs_memory_route']
                assert all(f.application['cterm.igs_'+n]==0 for n in ('draw_start_x','draw_start_y','draw_x','draw_y'))
                client.resize(1280,960)
            def base(style=1,width=1,caps=(0,0),mode=1,scaling=0,pen=10):
                return (b'\x1bfG#c>1,3:c 0,0:\x1bEG#p 3,2:A'+
                        f'G#g>{scaling}:C>1,{pen}:T>2,{style},{width}:T>2,{style},{ENDS[caps[0]][caps[1]]}:M {mode}:'.encode())
            def fence():return bytes((27,89,32,40+number,65))
            def capture():
                return client.wait_for(lambda f:f.application.get('cterm.x')==10+number
                                       and f.application.get('cterm.y')==1 and cell_at(f,8+number,0).glyph==65)
            def scene(peer,wire,position,**expected):
                nonlocal number
                send(peer,wire+fence());inspect(capture(),resolution,status,number,position,user_line=user_line,**expected);number+=1
            def disconnect(peer):
                peer.shutdown(socket.SHUT_RDWR)
                client.wait_text('Disconnected: Enter exits, F1 details');client.key('enter');client.wait_text('Directory: Name')
            with listener.accept()[0] as peer:
                ready()
                pattern=''.join('X' if 0xa531&(0x8000>>i) else '-' for i in range(16))
                send(peer,b'G#q>9995:X>14,5:X 7,6:'+pattern.encode()+b'@'+(b'----------------@'*15))
                for case in (c for c in VECTORS['cases'] if c['resolution']==resolution):
                    if case['style']==7:user_line=0xa531
                    mode,style,width,caps=case['mode'],case['style'],case['width'],tuple(case['caps'])
                    wire=base(style,width,caps,mode,number%3)+b'G#X 15,'+','.join(map(str,case['args'])).encode()+b':'
                    scene(peer,wire,(*case['points'][0],*case['points'][1]),mode=mode,style=style,width=width,
                          caps=caps,runs=case['runs'],scaling=number%3)
                for following in (False,True):
                    for mode in range(1,5):
                        y=2*ch+2;wire=base(3,mode=mode,pen=1)+f'G#X 15,22,{y},34,{y},0,22,1,4,0:'.encode()
                        pos=(22,y,30,y)
                        if following:wire+=f'G#D 34,{y}:'.encode();pos=(34,y,34,y)
                        text_after=following and mode==3
                        if text_after:wire+=b'G#p 3,2:A'
                        scene(peer,wire,pos,mode=mode,style=3,overlay=True,following=following,text_after=text_after)
                for axis,start,add in ((2,22,1),(0,30,1),(1,20,0)):
                    scene(peer,base()+f'G#X 15,22,40,30,40,{axis},{start},{add},4,60:'.encode(),(22,40,30,40))
                # A real local wait verifies initial drawing, deferred next line,
                # queued-command order and forwarding rather than key abort.
                special=resolution==0 and encoding=='ATARI-ST' and not status
                wait=60 if special else 1
                wire=base(scaling=1)+f'G#X 15,24,60,40,60,0,24,1,8,{wait}:'.encode()
                wire+=b'G#M 2:' + fence()
                send(peer,wire)
                if special:
                    frame=client.wait_for(lambda f:f.application.get('cterm.igs_sweep_active')==1
                                          and f.application.get('cterm.igs_draw_x')==24)
                    inspect(frame,resolution,status,number,(24,60,24,60),active=True,scaling=1,partial=True,user_line=user_line)
                    client.key(ord('a'));peer.settimeout(3);assert peer.recv(1)==b'a'
                inspect(capture(),resolution,status,number,(24,60,32,60),mode=2,scaling=1,
                        runs=((60,24,9,9),),user_line=user_line);number+=1
                peer.settimeout(0.03)
                try:reply=peer.recv(1)
                except socket.timeout:pass
                else:raise AssertionError(f'unexpected X15 reply: {reply!r}')
                send(peer,b'G#X 15,24,60,40,60,0,24,1,8,60:')
                client.wait_for(lambda f:f.application.get('cterm.igs_sweep_active')==1)
                disconnect(peer)
            client.key('enter')
            with listener.accept()[0] as peer:
                ready();user_line=0xffff
                scene(peer,base()+b'G#X 15,22,40,30,40,2,22,1,4,60:',(22,40,30,40),fresh=True)
                disconnect(peer)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('executable')
    parser.add_argument('--reports',default='reports/test-control');args=parser.parse_args()
    for resolution in range(3):
        for encoding in ('ATARI-ST','CP437','UTF-8'):
            for status in (False,True):run(args.executable,args.reports,resolution,encoding,status)
    assert CAPTURES==451,CAPTURES
