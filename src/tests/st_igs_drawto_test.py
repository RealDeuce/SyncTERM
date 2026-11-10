#!/usr/bin/env python3
"""D uses the independent line scenes while publishing the new end to both pairs.

P establishes each requested origin. Additional literal chains and L/P
interactions verify position ownership independently of raster geometry.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from collections import Counter
import socket
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'testing'))
from syncterm_fixture import fixture
from st_igs_line_test import VECTORS, END, check as check_line


def check(*args, **kwargs):
    check_line(*args, **kwargs, drawto=True)


def corner(resolution, mode):
    visits = Counter([(x,60) for x in range(40,81)]+[(80,y) for y in range(60,81)])
    ink = (15,3,1)[resolution] if mode == 3 else 9 & (15,3,1)[resolution]
    runs = [(y,x,1,ink) for (x,y),count in visits.items()
            if mode != 4 and (mode != 3 or count % 2)]
    return dict(points=((40,60),(80,80)),style=1,width=1,caps=(0,0),mode=mode,runs=runs)


def run(executable, reports, resolution, encoding, status):
    screen = ('AtariST40x25','AtariST80x25','AtariST80x25Mono')[resolution]
    with socket.socket() as listener:
        listener.bind(('127.0.0.1',0)); listener.listen(2); listener.settimeout(15)
        entries = ('[IGS Drawto]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                   f'Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\n'
                   f'ScreenMode={screen}\nFont=Atari ST\nEncoding={encoding}\n'
                   f'NoStatus={str(not status).lower()}\n')
        with fixture(executable,entries=entries,settings='AudioModes=0\nMidiModes=0\nBlockyScaling=true\n',
                     reports=reports,quiet=False) as (client,root):
            client.wait_text('IGS Drawto'); client.key('enter')
            with listener.accept()[0] as peer:
                client.wait_for(lambda s: s.application.get('connected') == 1)
                client.resize(1920,1440)
                def send(wire):
                    with (root/'wire.bin').open('ab') as stream: stream.write(wire)
                    peer.sendall(wire)
                for number,case in enumerate(c for c in VECTORS['cases'] if c['resolution'] == resolution):
                    token = f'LINE{number:02d}'
                    x1,y1 = case['points'][0]; x2,y2 = case['points'][1]
                    begin,end = case['caps']; style = case['style']
                    wire = (b'\x1bf\x1bE\x1bY2,Z' +
                            f'G#g>0:C>1,10:T>2,{style},{case["width"]}:T>2,{style},{END[begin][end]}:'.encode() +
                            f'M>4:P>{x1},{y1}:M>{case["mode"]}:D {x2},{y2}:'.encode() +
                            b'\x1bH' + token.encode())
                    send(wire)
                    check(client.wait_text(token),resolution,status,token,'vector',case=case)
                for mode in range(1,5):
                    token = f'CHAIN{mode}'
                    send(b'\x1bf\x1bE\x1bY2,Z' +
                         f'G#g>0:C>1,10:T>2,1,1:T>2,1,0:M>4:P>40,60:M>{mode}:D>80,60:D 80,80:'.encode()+
                         b'\x1bH'+token.encode())
                    check(client.wait_text(token),resolution,status,token,'vector',case=corner(resolution,mode))
                # The eight-unit L has arrow-shortened private points. D
                # must still begin at its original requested end, (48,60).
                source = next(c for c in VECTORS['cases'] if c['resolution'] == resolution
                              and c['mode'] == 1 and c['width'] == 1 and c['caps'] == [1,1])
                case = dict(source,points=((40,60),(48,80)),caps=(0,0),
                            runs=source['runs']+[(y,48,1,9 & (15,3,1)[resolution]) for y in range(60,81)])
                send(b'\x1bf\x1bE\x1bY2,ZG#g>0:C>1,10:T>2,1,1:T>2,1,50:M>1:L>40,60,48,60:T>2,1,0:D 48,80:\x1bHFROML')
                check(client.wait_text('FROML'),resolution,status,'FROML','vector',case=case)
                # A visible P dot replaces the remembered end of the earlier
                # invisible L; the following horizontal D starts at that dot.
                case = dict(points=((100,60),(120,60)),style=1,width=1,caps=(0,0),mode=1,
                            runs=[(60,100,21,9 & (15,3,1)[resolution])])
                send(b'\x1bf\x1bE\x1bY2,ZG#g>0:C>1,10:C>0,10:T>2,1,1:T>2,1,0:M>4:L>20,40,40,40:M>1:P>100,60:D 120,60:\x1bHFROMP')
                check(client.wait_text('FROMP'),resolution,status,'FROMP','vector',case=case)
                ch = 16 if resolution == 2 else 8
                for mode in range(1,5):
                    token = f'OVER{mode}'
                    send(b'\x1bf\x1bE\x1bY2,Z' +
                                 f'G#g>0:C>1,1:T>2,3,1:T>2,3,0:M>4:P>94,{18*ch+2}:M>{mode}:D 106,{18*ch+2}:'.encode() +
                                 b'\x1bH' + token.encode())
                    check(client.wait_text(token),resolution,status,token,'overlay',mode=mode)
                send(b'\x1bf\x1bE\x1bY2,ZG#g>1:T>2,1,1:T>2,1,0:M>4:P>0,9999:M>1:D 9999,9999:\x1bHEDGE')
                check(client.wait_text('EDGE'),resolution,status,'EDGE','edge')
                peer.settimeout(0.03)
                try: reply = peer.recv(1)
                except socket.timeout: pass
                else: raise AssertionError(f'unexpected IGS reply: {reply!r}')
                send(b'G#D 1,')
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
