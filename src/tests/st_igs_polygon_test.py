#!/usr/bin/env python3
"""f: independent polygon scenes, coherent overlays, serial flow and lifecycle."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import json
import socket
import sys
from pathlib import Path

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'/'testing'))
from syncterm_fixture import fixture, cell_at
from st_igs_box_test import check as check_fill
from st_igs_line_test import check as check_line, expected_console_replacement
from st_igs_init_test import Z8, Z16


def run(executable,reports,resolution,encoding,status):
    vectors=json.loads((Path(__file__).parent/'igs'/'polygon-vectors.json').read_text())
    def shape(x,y,xr=0,yr=0):
        return f'f 1,{x},{y}:'
    screen=('AtariST40x25','AtariST80x25','AtariST80x25Mono')[resolution]
    ch=16 if resolution==2 else 8
    with socket.socket() as listener:
        listener.bind(('127.0.0.1',0));listener.listen(2);listener.settimeout(15)
        entries=('[IGS polygons]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                 f'Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\nScreenMode={screen}\n'
                 f'Font=Atari ST\nEncoding={encoding}\nNoStatus={str(not status).lower()}\n')
        with fixture(executable,entries=entries,settings='AudioModes=0\nMidiModes=0\nBlockyScaling=true\n',
                     reports=reports,quiet=False) as (client,root):
            client.wait_text('IGS polygons');client.key('enter')
            with listener.accept()[0] as peer:
                client.wait_for(lambda s:s.application.get('connected')==1);client.resize(1920,1440)
                def send(wire,complete=True):
                    with (root/'wire.bin').open('ab') as stream:stream.write(wire)
                    peer.sendall(wire)
                    if complete:
                        peer.settimeout(15);reply=b''
                        while len(reply)<2:
                            part=peer.recv(2-len(reply));assert part,'disconnected during polygon flow'
                            reply+=part
                        assert reply==b'\x13\x11',reply
                base=b'G#g>0:T>2,1,1:T>2,1,0:M>4:L>17,23,37,47:T>2,3,1:T>2,3,50:'
                prefix=b'\x1bf\x1bE\x1bY2,Z'+base
                for number,case in enumerate(c for c in vectors['cases'] if c['resolution']==resolution):
                    token=f'POLYGON{number:02d}'
                    points=','.join(str(v) for pair in case['points'] for v in pair)
                    polygon=f'f {len(case["points"])},{points}:'
                    text_after=case.get('text_after',False)
                    wire=b'\x1bf\x1bE'+(b'' if text_after else b'\x1bY2,Z')+base
                    wire+=f'C>2,7:A>{case["interior"]},{case["index"]},{case["perimeter"]}:M>{case["mode"]}:{polygon}'.encode()
                    send(wire+(b'\x1bY2,Z' if text_after else b'')+b'\x1bH'+token.encode())
                    frame=client.wait_text(token)
                    check_fill(frame,resolution,status,token,case=expected_console_replacement(case,resolution))
                    if text_after:assert not cell_at(frame,12,18).flags&8
                # One-point closure emits one solid border visit, including XOR.
                assert not (Z16 if resolution==2 else Z8)[2]&1
                for mode in range(1,5):
                    token=f'DOT{mode}'
                    case=dict(mode=mode,interior=1,index=1,perimeter=True,last_line=1,
                              runs=[] if mode==4 else [(18*ch+2,103,1,1)])
                    send(prefix+f'C>2,7:A>1,1,1:M>{mode}:{shape(103,18*ch+2,0,0)}'.encode()+b'\x1bH'+token.encode())
                    frame=client.wait_text(token);check_fill(frame,resolution,status,token,case=case)
                    if mode!=4:assert cell_at(frame,12,18).flags&8
                # A later console glyph replaces its entire cell, including
                # the polygon dot in a blank bit of the independent Z raster.
                send(b'\x1bf\x1bE'+base+f'C>2,7:A>1,1,1:M>1:{shape(103,18*ch+2,0,0)}'.encode()+b'\x1bY2,Z\x1bHTEXT')
                frame=client.wait_text('TEXT')
                check_fill(frame,resolution,status,'TEXT',case=dict(mode=1,interior=1,index=1,perimeter=True,last_line=1,runs=[]))
                assert not cell_at(frame,12,18).flags&8
                height=400 if resolution==2 else 200
                edge=dict(mode=1,interior=1,index=1,perimeter=True,last_line=1,
                          runs=[] if status else [(height-1,0,1,1)])
                send(prefix+b'C>2,7:A>1,1,1:g>1:M>1:'+shape(0,9999,0,0).encode()+b'\x1bHEDGE')
                frame=client.wait_text('EDGE');check_fill(frame,resolution,status,'EDGE',case=edge)
                assert frame.application['cterm.igs_scaling_mode']==1
                # Polygon execution retains the preceding L endpoint for D.
                send(prefix+b'C>1,10:C>2,7:A>1,1,0:M>4:'+shape(80,80,17,9).replace(' ','>',1).encode()+b'T>2,1,1:T>2,1,0:M>1:D 57,47:\x1bHFOLLOW')
                following=dict(points=((57,47),(57,47)),style=1,width=1,caps=(0,0),mode=1,
                               runs=[(47,37,21,9&(15,3,1)[resolution])])
                frame=client.wait_text('FOLLOW');check_line(frame,resolution,status,'FOLLOW','vector',case=following)
                assert tuple(frame.application[f'cterm.igs_fill_{name}'] for name in ('interior','index','perimeter','pen'))==(1,1,False,7 if resolution==0 else 1)
                peer.settimeout(0.03)
                try:reply=peer.recv(1)
                except socket.timeout:pass
                else:raise AssertionError(f'unexpected IGS reply: {reply!r}')
                send(b'G#f 128,1,2,',complete=False);peer.shutdown(socket.SHUT_RDWR)
                client.wait_text('Disconnected: Enter exits, F1 details');client.key('enter')
            client.wait_text('Directory: Name');client.key('enter')
            with listener.accept()[0] as peer:
                client.wait_for(lambda s:s.application.get('connected')==1);client.resize(1920,1440)
                peer.sendall(b'\x1bf\x1bE\x1bY2,Z\x1bHFRESH')
                check_fill(client.wait_text('FRESH'),resolution,status,'FRESH',fresh=True)
                peer.sendall(b'G#f 1,40,60:');peer.settimeout(15);reply=b''
                while len(reply)<2:
                    part=peer.recv(2-len(reply));assert part;reply+=part
                assert reply==b'\x13\x11',reply
                peer.shutdown(socket.SHUT_RDWR);client.wait_text('Disconnected: Enter exits, F1 details');client.key('enter')
            client.wait_text('Directory: Name');assert b'[wren' not in client.stderr,bytes(client.stderr)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('executable');parser.add_argument('--reports',default='reports/test-control')
    args=parser.parse_args()
    for resolution in range(3):
        for encoding in ('ATARI-ST','CP437','UTF-8'):
            for status in (False,True):run(args.executable,args.reports,resolution,encoding,status)
