#!/usr/bin/env python3
"""E: literal attribute selections and unchanged coherent console pixels/state."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import socket
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'/'testing'))
from syncterm_fixture import fixture
from st_igs_box_test import check as check_fill
from st_igs_line_test import check as check_line

# Literal native source/doubled point/top values from reference30.
SIZES=((8,4),(9,6),(10,13),(16,9),(18,13),(20,27))
CASES=[]
for index,(point,top) in enumerate(SIZES):
    for rotation in range(5):
        CASES.append((5*index+rotation,point,rotation,5*index+rotation,point,top,900*rotation))
CASES += [(30,0,5,30,8,4,0),(31,9999,9999,31,20,27,0)]
for effects,request,point,top,rotation in ((63,7,8,4,1),(9999,11,10,13,2),(2,15,10,13,3),
        (4,17,16,9,4),(8,19,18,13,0),(16,21,20,27,1),(1,9999,20,27,2),(0,0,8,4,3)):
    CASES.append((effects,request,rotation,effects%32,point,top,rotation*900))
assert len(CASES)==40


def check_text(frame,effects,points,top,rotation,point_mode=True,pen=1):
    state=frame.application
    assert tuple(state[f'cterm.igs_text_{name}'] for name in ('effects','points','height','rotation','point_mode','face','align_h','align_v','pen')) == (effects,points,top,rotation,point_mode,1,0,0,pen)


def run(executable,reports,resolution,encoding,status):
    screen=('AtariST40x25','AtariST80x25','AtariST80x25Mono')[resolution]
    with socket.socket() as listener:
        listener.bind(('127.0.0.1',0));listener.listen(2);listener.settimeout(15)
        entries=('[IGS text attributes]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                 f'Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\nScreenMode={screen}\n'
                 f'Font=Atari ST\nEncoding={encoding}\nNoStatus={str(not status).lower()}\n')
        with fixture(executable,entries=entries,settings='AudioModes=0\nMidiModes=0\nBlockyScaling=true\n',reports=reports,quiet=False) as (client,root):
            client.wait_text('IGS text attributes');client.key('enter')
            with listener.accept()[0] as peer:
                client.wait_for(lambda s:s.application.get('connected')==1);client.resize(1920,1440)
                def send(wire):
                    with (root/'wire.bin').open('ab') as stream:stream.write(wire)
                    peer.sendall(wire)
                prefix=b'\x1bf\x1bE\x1bY2,Z'
                for index,case in enumerate(CASES):
                    effects,request,rotation,effective,point,top,angle=case;token=f'STYLE{index:02d}'
                    send(prefix+f'G#g>{index%3}:E {effects},{request},{rotation}:'.encode()+b'\x1bH'+token.encode())
                    frame=client.wait_text(token);check_fill(frame,resolution,status,token,fresh=True)
                    check_text(frame,effective,point,top,angle)
                    assert frame.application['cterm.igs_scaling_mode']==index%3
                send(prefix+b'G#E>31,18,4:C>3,0:I 1:\x1bHPRESERVE')
                frame=client.wait_text('PRESERVE');check_fill(frame,resolution,status,'PRESERVE',fresh=True);check_text(frame,31,18,13,3600,pen=0)
                default_point,default_top=(10,13) if resolution==2 else (9,6)
                for operation in (0,2):
                    token=f'RESTORE{operation}'
                    send(prefix+f'G#E>31,20,4:I {operation}:'.encode()+b'\x1bH'+token.encode())
                    frame=client.wait_text(token);check_fill(frame,resolution,status,token,fresh=True)
                    check_text(frame,0,default_point,default_top,0,point_mode=False)
                # Text attributes neither draw nor move the remembered L/D origin.
                send(prefix+b'G#g>0:C>1,10:T>2,1,1:T>2,1,0:M>4:L>17,23,37,47:M>1:E>31,20,4:D 57,47:\x1bHFOLLOW')
                following=dict(points=((57,47),(57,47)),style=1,width=1,caps=(0,0),mode=1,runs=[(47,37,21,9&(15,3,1)[resolution])])
                frame=client.wait_text('FOLLOW');check_line(frame,resolution,status,'FOLLOW','vector',case=following);check_text(frame,31,20,27,3600)
                peer.settimeout(0.03)
                try:reply=peer.recv(1)
                except socket.timeout:pass
                else:raise AssertionError(f'unexpected E reply: {reply!r}')
                send(b'G#E 1,2,');peer.shutdown(socket.SHUT_RDWR)
                client.wait_text('Disconnected: Enter exits, F1 details');client.key('enter')
            client.wait_text('Directory: Name');client.key('enter')
            with listener.accept()[0] as peer:
                client.wait_for(lambda s:s.application.get('connected')==1);client.resize(1920,1440)
                peer.sendall(prefix+b'\x1bHFRESH');frame=client.wait_text('FRESH')
                check_fill(frame,resolution,status,'FRESH',fresh=True);check_text(frame,0,default_point,default_top,0,point_mode=False)
                peer.shutdown(socket.SHUT_RDWR);client.wait_text('Disconnected: Enter exits, F1 details');client.key('enter')
            client.wait_text('Directory: Name');assert b'[wren' not in client.stderr,bytes(client.stderr)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('executable');parser.add_argument('--reports',default='reports/test-control');args=parser.parse_args()
    for resolution in range(3):
        for encoding in ('ATARI-ST','CP437','UTF-8'):
            for status in (False,True):run(args.executable,args.reports,resolution,encoding,status)
