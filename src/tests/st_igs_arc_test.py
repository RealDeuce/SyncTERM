#!/usr/bin/env python3
"""K: independent native arcs, caps/joins and coherent console composition."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import json
import socket
import sys
from pathlib import Path

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'/'testing'))
from syncterm_fixture import fixture, cell_at
from st_igs_line_test import END, check as check_line, expected_retained_outline

VECTORS=json.loads((Path(__file__).parent/'igs'/'arc-vectors.json').read_text())


def run(executable,reports,resolution,encoding,status):
    screen=('AtariST40x25','AtariST80x25','AtariST80x25Mono')[resolution]
    ch=16 if resolution==2 else 8
    with socket.socket() as listener:
        listener.bind(('127.0.0.1',0));listener.listen(2);listener.settimeout(15)
        entries=('[IGS arcs]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                 f'Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\nScreenMode={screen}\n'
                 f'Font=Atari ST\nEncoding={encoding}\nNoStatus={str(not status).lower()}\n')
        with fixture(executable,entries=entries,settings='AudioModes=0\nMidiModes=0\nBlockyScaling=true\n',
                     reports=reports,quiet=False) as (client,root):
            client.wait_text('IGS arcs');client.key('enter')
            with listener.accept()[0] as peer:
                client.wait_for(lambda s:s.application.get('connected')==1);client.resize(1920,1440)
                def send(wire):
                    with (root/'wire.bin').open('ab') as stream:stream.write(wire)
                    peer.sendall(wire)
                base=b'G#g>0:T>2,1,1:T>2,1,0:M>4:L>17,23,37,47:'
                prefix=b'\x1bf\x1bE\x1bY2,Z'+base
                for number,case in enumerate(c for c in VECTORS['cases'] if c['resolution']==resolution):
                    assert case['last_line']==1
                    token=f'ARC{number:02d}';style=case['style'];begin,end=case['caps'];x,y=case['center'];a,b=case['angles']
                    size=33 if style==7 else case['width']
                    wire=b'\x1bf\x1bE'+(b'' if case['text_after'] else b'\x1bY2,Z')+base
                    # A hidden, perimeter-free degenerate fill establishes the
                    # source's incoming false LSTLIN before wide cap traversal.
                    if not case['last']:wire+=b'A>1,1,0:B>0,0,0,0,1:'
                    wire+=f'C>1,10:C>2,7:A>0,1,1:T>2,{style},{size}:T>2,{style},{END[begin][end]}:M>{case["mode"]}:K {x},{y},{case["radius"]},{a},{b}:'.encode()
                    send(wire+(b'\x1bY2,Z' if case['text_after'] else b'')+b'\x1bH'+token.encode())
                    frame=client.wait_text(token)
                    check_line(frame,resolution,status,token,'vector',case=expected_retained_outline(case,resolution))
                    assert tuple(frame.application[f'cterm.igs_fill_{name}'] for name in ('interior','index','perimeter','pen'))==(0,1,True,7 if resolution==0 else 1)
                    assert (frame.application['cterm.igs_random_seed'],frame.application['cterm.igs_pattern_seed'])==(1,0)
                    if case['text_after']:assert not cell_at(frame,12,18).flags&8
                for mode in range(1,5):
                    token=f'DOT{mode}'
                    expected=dict(points=((17,23),(37,47)),style=1,width=1,caps=(0,0),mode=mode,
                                  runs=[(18*ch+2,103,1,1)] if mode in (1,2) else [])
                    send(prefix+f'C>1,10:T>2,1,1:T>2,1,0:M>{mode}:K 103,{18*ch+2},0,0,90:'.encode()+b'\x1bH'+token.encode())
                    frame=client.wait_text(token);check_line(frame,resolution,status,token,'vector',case=expected)
                    if mode!=4:assert cell_at(frame,12,18).flags&8
                height=400 if resolution==2 else 200
                edge=dict(points=((17,23),(37,47)),style=1,width=1,caps=(0,0),mode=1,
                          runs=[] if status else [(height-1,0,1,(15,3,1)[resolution])])
                send(prefix+b'C>1,10:T>2,1,1:T>2,1,0:g>1:M>1:K 0,9999,0,0,90:\x1bHEDGE')
                frame=client.wait_text('EDGE');check_line(frame,resolution,status,'EDGE','vector',case=edge)
                assert frame.application['cterm.igs_scaling_mode']==1
                send(prefix+b'C>1,10:T>2,1,1:T>2,1,0:M>4:K>80,80,17,0,90:M>1:D 57,47:\x1bHFOLLOW')
                following=dict(points=((57,47),(57,47)),style=1,width=1,caps=(0,0),mode=1,
                               runs=[(47,37,21,9&(15,3,1)[resolution])])
                check_line(client.wait_text('FOLLOW'),resolution,status,'FOLLOW','vector',case=following)
                peer.settimeout(0.03)
                try:reply=peer.recv(1)
                except socket.timeout:pass
                else:raise AssertionError(f'unexpected IGS reply: {reply!r}')
                send(b'G#K 1,2,3,4,');peer.shutdown(socket.SHUT_RDWR)
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
