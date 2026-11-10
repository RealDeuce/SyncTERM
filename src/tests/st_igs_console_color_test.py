#!/usr/bin/env python3
"""c: source-defined console state composition and coherent glyph pixels."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import json
import socket
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'/'testing'))
from syncterm_fixture import fixture,cell_at
from st_igs_resolution_test import initial
from st_igs_text_attributes_test import check_text
from st_igs_box_test import check as check_blank
DATA=json.loads((Path(__file__).parent/'igs'/'console-color.json').read_text())
PREFIXES=(b'',b'\x1b',b'\x1bY',b'\x1bY!',b'\x1bb',b'\x1bc')


def expected(case,columns,rows):
    # Source states: Normal, ESC, Y-row, Y-column, foreground, background.
    state=3 if case['state']==6 else case['state'];x,y=3,2
    row=rows-1 if case['state']==6 else 1
    fg,bg=(15-32)&65535,(case['background']-32)&65535
    cells={};row_background=[0]*rows;scrolls=0
    def scroll():
        nonlocal cells,scrolls
        cells={(cx,cy-1):value for (cx,cy),value in cells.items() if cy>0}
        row_background.pop(0);row_background.append(bg);scrolls+=1
    output=[27] if case['selector']<0 else [27,98 if case['selector']==1 else 99,case['color']&255]
    for byte in output:
        if state==1:state={98:4,99:5}.get(byte,0)
        elif state==2:row=min((byte-32)&65535,rows-1);state=3
        elif state==3:x=min((byte-32)&65535,columns-1);y=row;state=0
        elif state==4:fg=(byte-32)&65535;state=0
        elif state==5:bg=(byte-32)&65535;state=0
        elif byte==27:state=1
        elif byte==8:x=max(0,x-1)
        elif byte==9:x=min((x&~7)+8,columns-1)
        elif byte in (10,11,12):
            if y+1<rows:y+=1
            else:scroll()
        elif byte==13:x=0
        elif byte>=32:
            cells[x,y]=(byte,bg if case['reverse'] else fg,fg if case['reverse'] else bg)
            if x+1<columns:x+=1
            elif case.get('wrap',False):
                x=0
                if y+1<rows:y+=1
                else:scroll()
    return dict(state=state,x=x,y=y,fg=fg,bg=bg,cells=cells,row_background=row_background,scrolls=scrolls)


def check(frame,resolution,status,marker,case):
    m,s=frame.metadata,frame.application
    width=320 if resolution==0 else 640;height=400 if resolution==2 else 200;ch=16 if resolution==2 else 8
    rows=24 if status else 25;mask=(15,3,1)[resolution];model=expected(case,width//8,rows)
    assert (m['plane_width'],m['plane_height'])==(width,height)
    assert s['cterm.graphics_mode']==5 and s['cterm.igs_resolution']==resolution
    assert (s['cterm.x'],s['cterm.y'])==(model['x']+1,model['y']+1)
    assert tuple(frame.palette[768:784])==tuple(initial(resolution))
    assert tuple(s[f'cterm.igs_console_{name}'] for name in ('foreground','background','reverse','escape','parameters'))==(model['fg'],model['bg'],case['reverse'],model['state']==1,1 if model['state']==3 else 0)
    assert tuple(s[f'cterm.igs_{name}'] for name in ('draw_start_x','draw_start_y','draw_x','draw_y'))==(0,0,marker,777)
    assert s['cterm.igs_last_line']==1 and s['cterm.igs_writing_mode']==1
    assert (s['cterm.igs_random_seed'],s['cterm.igs_pattern_seed'])==(1,0)
    assert tuple(s[f'cterm.igs_fill_{name}'] for name in ('interior','index','perimeter','pen'))==(1,1,True,1)
    check_text(frame,0,9,6,0,pen=7 if resolution==0 else 1)
    assert s['cterm.igs_shared_text_size']==1 and s['cterm.igs_text_counter']==0
    expected_pixels=[];samples={(20,20)}
    for background in model['row_background']:expected_pixels.extend([768+(background&mask)]*(width*ch))
    graphic_top=54-model['scrolls']*ch
    for y,bits in enumerate(DATA['fonts']['8']['65']):
        for x in range(8):
            expected_pixels[(graphic_top+y)*width+100+x]=768+((5 if resolution==0 else mask) if bits&(128>>x) else 0)
            samples.add((100+x,graphic_top+y))
    for (cx,cy),(code,fg,bg) in model['cells'].items():
        for y,bits in enumerate(DATA['fonts'][str(ch)][str(code)]):
            for x in range(8):
                expected_pixels[(cy*ch+y)*width+cx*8+x]=768+((fg if bits&(128>>x) else bg)&mask)
                samples.add((cx*8+x,cy*ch+y))
    for cy in range(rows):
        for cx in range(width//8):
            cell=cell_at(frame,cx,cy);value=model['cells'].get((cx,cy))
            assert cell.glyph==(value[0] if value else 32),(resolution,status,case,cx,cy,cell.glyph,value)
            if value:
                assert (cell.foreground,cell.background)==(768+(value[1]&mask),768+(value[2]&mask))
                assert not cell.flags&2 # ST_CONSOLE_CELL_BLINK
            else:assert cell.background==768+(model['row_background'][cy]&mask)
    if status:assert all(not cell_at(frame,x,24).flags&8 for x in range(width//8))
    # Capture planes are already copied in logical row order. pixel_top is
    # retained storage-ring metadata, not another offset into this snapshot.
    start=0
    for plane in (frame.primary,frame.alternate):
        observed=list(plane[start:start+len(expected_pixels)])
        assert observed==expected_pixels,(resolution,status,case,next((i%width,i//width,a,b)
            for i,(a,b) in enumerate(zip(observed,expected_pixels)) if a!=b))
    for x,y in samples:
        ox=m['destination_x']+(2*x+1)*m['destination_width']//(2*width)
        oy=m['destination_y']+(2*y+1)*m['destination_height']//(2*height)
        assert frame.rgb[oy*m['output_width']+ox]&0xffffff==frame.palette[expected_pixels[y*width+x]],(case,x,y)


def run(executable,reports,resolution,encoding,status,del_only=False):
    screen=('AtariST40x25','AtariST80x25','AtariST80x25Mono')[resolution]
    with socket.socket() as listener:
        listener.bind(('127.0.0.1',0));listener.listen(2);listener.settimeout(15)
        entries=('[IGS console color]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                 f'Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\nScreenMode={screen}\n'
                 f'Font=Atari ST\nEncoding={encoding}\nNoStatus={str(not status).lower()}\n')
        with fixture(executable,entries=entries,settings='AudioModes=0\nMidiModes=0\nBlockyScaling=true\n',reports=reports,quiet=False) as (client,root):
            client.wait_text('IGS console color');client.key('enter')
            with listener.accept()[0] as peer:
                client.wait_for(lambda f:f.application.get('connected')==1);client.resize(1920,1440)
                def send(wire):
                    with (root/'wire.bin').open('ab') as stream:stream.write(wire)
                    peer.sendall(wire)
                # Two NULs finish any raw parameters; ESC ESC returns to Normal.
                resync=b'\0\0\x1b\x1b\x1bq\x1bf\x1bw'
                for index,case in enumerate(c for c in DATA['del_cases' if del_only else 'cases'] if c['resolution']==resolution):
                    marker=1000+index
                    setup=resync+b'G#g>0:c>1,15:c 0,0:\x1bE'
                    setup+=f'G#c>0,{case["background"]}:C>3,7:E>0,9,0:M>1:W 100,60,A@'.encode()+b'\x1bY\x22\x23'
                    if case['reverse']:setup+=b'\x1bp'
                    if case['state']==6:
                        setup+=b'\x1bv\x1bY'+bytes([32+(24 if status else 25)-1])
                    else:setup+=PREFIXES[case['state']]
                    send(setup+f'G#c {case["selector"]},{case["color"]}:G#X 10,{marker},777:'.encode())
                    # A unique non-drawing graphics fence preserves pending console
                    # state; an ordinary caption/ESC H could consume that state.
                    frame=client.wait_for(lambda f:f.application.get('cterm.igs_draw_x')==marker)
                    check(frame,resolution,status,marker,case)
                peer.settimeout(0.03)
                try:reply=peer.recv(1)
                except socket.timeout:pass
                else:raise AssertionError(f'unexpected c reply: {reply!r}')
                send(resync+b'G#c 1,');peer.shutdown(socket.SHUT_RDWR)
                client.wait_text('Disconnected: Enter exits, F1 details');client.key('enter')
            client.wait_text('Directory: Name');client.key('enter')
            with listener.accept()[0] as peer:
                client.wait_for(lambda f:f.application.get('connected')==1);client.resize(1920,1440)
                peer.sendall(b'\x1bf\x1bE\x1bY2,Z\x1bHFRESH');frame=client.wait_text('FRESH')
                check_blank(frame,resolution,status,'FRESH',fresh=True)
                assert (frame.application['cterm.igs_console_foreground'],frame.application['cterm.igs_console_background'])==(15,0)
                assert not frame.application['cterm.igs_console_reverse'] and not frame.application['cterm.igs_console_escape']
                peer.shutdown(socket.SHUT_RDWR);client.wait_text('Disconnected: Enter exits, F1 details');client.key('enter')
            client.wait_text('Directory: Name');assert b'[wren' not in client.stderr,bytes(client.stderr)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('executable');parser.add_argument('--reports',default='reports/test-control');parser.add_argument('--del-only',action='store_true');args=parser.parse_args()
    for resolution in range(3):
        for encoding in ('ATARI-ST','CP437','UTF-8'):
            for status in (False,True):run(args.executable,args.reports,resolution,encoding,status,args.del_only)
