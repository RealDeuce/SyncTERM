#!/usr/bin/env python3
"""F: independent components, coherent text/pixel/state captures and serial flow."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import json
import socket
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'/'testing'))
from syncterm_fixture import fixture,cell_at
from st_igs_box_test import check as check_fill
from st_igs_line_test import check as check_line,expected_console_replacement
from st_igs_init_test import Z8,Z16


def grid_wire(grid,origin):
    ox,oy=origin;wire='C>2,2:A>1,1,0:M>1:'
    for y,row in enumerate(grid):
        x=0
        while x<len(row):
            end=x+1
            while end<len(row) and row[end]==row[x]:end+=1
            if row[x]=='1':wire+=f'Z>{ox+x},{oy+y},{ox+end-1},{oy+y}:'
            x=end
    return wire


def run(executable,reports,resolution,encoding,status):
    vectors=json.loads((Path(__file__).parent/'igs'/'flood-vectors.json').read_text())
    screen=('AtariST40x25','AtariST80x25','AtariST80x25Mono')[resolution];ch=16 if resolution==2 else 8
    with socket.socket() as listener:
        listener.bind(('127.0.0.1',0));listener.listen(2);listener.settimeout(15)
        entries=('[IGS floods]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                 f'Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\nScreenMode={screen}\n'
                 f'Font=Atari ST\nEncoding={encoding}\nNoStatus={str(not status).lower()}\n')
        with fixture(executable,entries=entries,settings='AudioModes=0\nMidiModes=0\nBlockyScaling=true\n',reports=reports,quiet=False) as (client,root):
            client.wait_text('IGS floods');client.key('enter')
            with listener.accept()[0] as peer:
                client.wait_for(lambda s:s.application.get('connected')==1);client.resize(1920,1440)
                def send(wire,complete=True):
                    with (root/'wire.bin').open('ab') as stream:stream.write(wire)
                    peer.sendall(wire)
                    if complete:
                        peer.settimeout(15);reply=b''
                        while len(reply)<2:
                            part=peer.recv(2-len(reply));assert part,'disconnected during flood flow';reply+=part
                        assert reply==b'\x13\x11',reply
                base=b'G#g>0:T>2,1,1:T>2,1,0:M>4:L>17,23,37,47:T>2,3,1:T>2,3,50:'
                prefix=b'\x1bf\x1bE\x1bY2,Z'+base
                for number,case in enumerate(c for c in vectors['cases'] if c['resolution']==resolution):
                    token=f'FLOOD{number:02d}';text_after=case['text_after'];ox,oy=case['origin'];sx,sy=case['seed']
                    wire=b'\x1bf\x1bE'+(b'' if text_after else b'\x1bY2,Z')+base
                    wire+=(grid_wire(case['grid'],case['origin'])+f'C>2,7:A>{case["interior"]},{case["index"]},{case["perimeter"]}:M>{case["mode"]}:F {ox+sx},{oy+sy}:').encode()
                    send(wire+(b'\x1bY2,Z' if text_after else b'')+b'\x1bH'+token.encode())
                    frame=client.wait_text(token);check_fill(frame,resolution,status,token,case=expected_console_replacement(case,resolution))
                    if text_after:assert not cell_at(frame,12,18).flags&8
                assert not (Z16 if resolution==2 else Z8)[2]&1
                for mode in range(1,5):
                    token=f'DOT{mode}';oy=18*ch+1
                    runs=[(oy,102,3,1),(oy+1,102,1,1),(oy+1,104,1,1),(oy+2,102,3,1)]
                    if mode!=4:runs.append((oy+1,103,1,5 if resolution==0 and mode!=3 else (15,3,1)[resolution]))
                    case=dict(mode=mode,interior=1,index=1,perimeter=True,last_line=0,physical_runs=True,runs=runs)
                    send(prefix+(grid_wire(('111','101','111'),(102,oy))+f'C>2,7:A>1,1,1:M>{mode}:F 103,{oy+1}:').encode()+b'\x1bH'+token.encode())
                    frame=client.wait_text(token);check_fill(frame,resolution,status,token,case=case);assert cell_at(frame,12,18).flags&8
                width=320 if resolution==0 else 640;height=400 if resolution==2 else 200
                edge=dict(mode=1,interior=1,index=1,perimeter=True,last_line=int(status),physical_runs=True,
                          runs=[] if status else [(height-2,0,width,1),(height-1,0,width,5 if resolution==0 else (3,1)[resolution-1])])
                send(prefix+f'C>2,2:A>1,1,0:M>1:Z>0,{height-2},{width-1},{height-2}:C>2,7:A>1,1,1:g>1:F 0,9999:'.encode()+b'\x1bHEDGE')
                check_fill(client.wait_text('EDGE'),resolution,status,'EDGE',case=edge)
                send(prefix+b'C>1,10:C>2,7:A>1,1,0:M>4:F>80,80:T>2,1,1:T>2,1,0:M>1:D 57,47:\x1bHFOLLOW')
                following=dict(points=((57,47),(57,47)),style=1,width=1,caps=(0,0),mode=1,runs=[(47,37,21,9&(15,3,1)[resolution])])
                check_line(client.wait_text('FOLLOW'),resolution,status,'FOLLOW','vector',case=following)
                peer.settimeout(0.03)
                try:reply=peer.recv(1)
                except socket.timeout:pass
                else:raise AssertionError(f'unexpected reply: {reply!r}')
                send(b'G#F 1,',complete=False);peer.shutdown(socket.SHUT_RDWR)
                client.wait_text('Disconnected: Enter exits, F1 details');client.key('enter')
            client.wait_text('Directory: Name');client.key('enter')
            with listener.accept()[0] as peer:
                client.wait_for(lambda s:s.application.get('connected')==1);client.resize(1920,1440)
                peer.sendall(b'\x1bf\x1bE\x1bY2,Z\x1bHFRESH');check_fill(client.wait_text('FRESH'),resolution,status,'FRESH',fresh=True)
                peer.sendall(b'G#F 40,60:');peer.settimeout(15);reply=b''
                while len(reply)<2:
                    part=peer.recv(2-len(reply));assert part;reply+=part
                assert reply==b'\x13\x11',reply
                peer.shutdown(socket.SHUT_RDWR);client.wait_text('Disconnected: Enter exits, F1 details');client.key('enter')
            client.wait_text('Directory: Name');assert b'[wren' not in client.stderr,bytes(client.stderr)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('executable');parser.add_argument('--reports',default='reports/test-control');args=parser.parse_args()
    for resolution in range(3):
        for encoding in ('ATARI-ST','CP437','UTF-8'):
            for status in (False,True):run(args.executable,args.reports,resolution,encoding,status)
