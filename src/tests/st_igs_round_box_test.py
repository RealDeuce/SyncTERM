#!/usr/bin/env python3
"""U: independent thin/wide outlines, shared rounded fill and text restoration."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import json
import socket
import sys
from pathlib import Path

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'/'testing'))
from syncterm_fixture import fixture, cell_at
from st_igs_line_test import END, check as check_line
from st_igs_box_test import check as check_fill, VECTORS as FILLS

OUTLINES=json.loads((Path(__file__).parent/'igs'/'round-box-vectors.json').read_text())


def expected_outline(case,resolution):
    # The console writes the complete Z cell after graphics in marked cases.
    # Remove covered native runs, then let the shared literal glyph recipe
    # supply that cell. This changes no observed capture.
    expected=dict(case,points=((17,23),(37,47)))
    if case['text_after']:
        ch=16 if resolution==2 else 8
        runs=[]
        for y,x,count,value in case['runs']:
            if 18*ch<=y<19*ch:
                for left,right in ((x,min(x+count,96)),(max(x,104),x+count)):
                    if left<right:runs.append((y,left,right-left,value))
            else:runs.append((y,x,count,value))
        expected['runs']=runs
    return expected


def run(executable,reports,resolution,encoding,status):
    screen=('AtariST40x25','AtariST80x25','AtariST80x25Mono')[resolution]
    with socket.socket() as listener:
        listener.bind(('127.0.0.1',0));listener.listen(2);listener.settimeout(15)
        entries=('[IGS rounded boxes]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                 f'Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\nScreenMode={screen}\n'
                 f'Font=Atari ST\nEncoding={encoding}\nNoStatus={str(not status).lower()}\n')
        with fixture(executable,entries=entries,settings='AudioModes=0\nMidiModes=0\nBlockyScaling=true\n',
                     reports=reports,quiet=False) as (client,root):
            client.wait_text('IGS rounded boxes');client.key('enter')
            with listener.accept()[0] as peer:
                client.wait_for(lambda s:s.application.get('connected')==1);client.resize(1920,1440)
                def send(wire):
                    with (root/'wire.bin').open('ab') as stream:stream.write(wire)
                    peer.sendall(wire)
                base=b'G#g>0:T>2,1,1:T>2,1,0:M>4:L>17,23,37,47:'
                for number,case in enumerate(c for c in OUTLINES['cases'] if c['resolution']==resolution):
                    token=f'ROUND{number:02d}';style=case['style'];begin,end=case['caps']
                    # Kind7/size33 selects the already default0xffff user mask.
                    size=33 if style==7 else case['width']
                    coordinates=','.join(map(str,case['points']))
                    prefix=b'\x1bf\x1bE'+(b'' if case['text_after'] else b'\x1bY2,Z')
                    send(prefix+base+f'C>1,10:C>2,7:A>0,1,1:T>2,{style},{size}:T>2,{style},{END[begin][end]}:M>{case["mode"]}:U {coordinates},0:'.encode()+
                         (b'\x1bY2,Z' if case['text_after'] else b'')+b'\x1bH'+token.encode())
                    frame=client.wait_text(token)
                    check_line(frame,resolution,status,token,'vector',case=expected_outline(case,resolution))
                    state=frame.application
                    assert tuple(state[f'cterm.igs_fill_{name}'] for name in ('interior','index','perimeter','pen'))==(0,1,True,7 if resolution==0 else 1)
                    assert (state['cterm.igs_random_seed'],state['cterm.igs_pattern_seed'])==(1,0)
                    if case['text_after']:assert not cell_at(frame,12,18).flags&8
                for number,case in enumerate(c for c in FILLS['cases'] if c['resolution']==resolution and c['flag']==1):
                    token=f'UFILL{number:02d}';coordinates=','.join(map(str,case['points']))
                    send(b'\x1bf\x1bE\x1bY2,Z'+base+b'T>2,3,1:T>2,3,50:'+
                         f'C>2,7:A>{case["interior"]},{case["index"]},{case["perimeter"]}:M>{case["mode"]}:U {coordinates},9999:'.encode()+b'\x1bH'+token.encode())
                    check_fill(client.wait_text(token),resolution,status,token,case=case)
                # Full native corner data is above; a degenerate horizontal
                # outline is an independent status/normalized-bottom check.
                width,height=(320 if resolution==0 else 640),(400 if resolution==2 else 200)
                edge=dict(points=((17,23),(37,47)),style=1,width=1,caps=(0,0),mode=1,
                          runs=[] if status else [(height-1,0,width,(15,3,1)[resolution])])
                send(b'\x1bf\x1bE\x1bY2,Z'+base+b'C>1,10:T>2,1,1:T>2,1,0:g>1:M>1:U 0,9999,9999,9999,0:\x1bHEDGE2')
                check_line(client.wait_text('EDGE2'),resolution,status,'EDGE2','vector',case=edge)
                # U preserves the prior L position used by the next D.
                send(b'\x1bf\x1bE\x1bY2,Z'+base+b'C>1,10:M>4:U>80,80,100,100,0:T>2,1,1:T>2,1,0:M>1:D 57,47:\x1bHFOLLOW')
                following=dict(points=((57,47),(57,47)),style=1,width=1,caps=(0,0),mode=1,
                               runs=[(47,37,21,9&(15,3,1)[resolution])])
                check_line(client.wait_text('FOLLOW'),resolution,status,'FOLLOW','vector',case=following)
                peer.settimeout(0.03)
                try:reply=peer.recv(1)
                except socket.timeout:pass
                else:raise AssertionError(f'unexpected IGS reply: {reply!r}')
                send(b'G#U 1,2,3,4,');peer.shutdown(socket.SHUT_RDWR)
                client.wait_text('Disconnected: Enter exits, F1 details');client.key('enter')
            client.wait_text('Directory: Name');client.key('enter')
            with listener.accept()[0] as peer:
                client.wait_for(lambda s:s.application.get('connected')==1);client.resize(1920,1440)
                peer.sendall(b'\x1bf\x1bE\x1bY2,Z\x1bHFRESH')
                check_line(client.wait_text('FRESH'),resolution,status,'FRESH','fresh')
                peer.shutdown(socket.SHUT_RDWR);client.wait_text('Disconnected: Enter exits, F1 details');client.key('enter')
            client.wait_text('Directory: Name');assert b'[wren' not in client.stderr,bytes(client.stderr)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('executable');parser.add_argument('--reports',default='reports/test-control')
    args=parser.parse_args()
    for resolution in range(3):
        for encoding in ('ATARI-ST','CP437','UTF-8'):
            for status in (False,True):run(args.executable,args.reports,resolution,encoding,status)
