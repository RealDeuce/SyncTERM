#!/usr/bin/env python3
"""z: independent connected-line scenes, coherent cells/RGB and serial flow."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import json
import socket
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'testing'))
from syncterm_fixture import fixture
from st_igs_line_test import END, check

VECTORS = json.loads((Path(__file__).parent/'igs'/'polyline-vectors.json').read_text())


def run(executable, reports, resolution, encoding, status):
    screen = ('AtariST40x25','AtariST80x25','AtariST80x25Mono')[resolution]
    with socket.socket() as listener:
        listener.bind(('127.0.0.1',0)); listener.listen(2); listener.settimeout(15)
        entries = ('[IGS polyline]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                   f'Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\n'
                   f'ScreenMode={screen}\nFont=Atari ST\nEncoding={encoding}\n'
                   f'NoStatus={str(not status).lower()}\n')
        with fixture(executable,entries=entries,settings='AudioModes=0\nMidiModes=0\nBlockyScaling=true\n',
                     reports=reports,quiet=False) as (client,root):
            client.wait_text('IGS polyline'); client.key('enter')
            with listener.accept()[0] as peer:
                client.wait_for(lambda s: s.application.get('connected') == 1)
                client.resize(1920,1440); peer.settimeout(15)
                def send(wire):
                    with (root/'wire.bin').open('ab') as stream: stream.write(wire)
                    peer.sendall(wire)
                def flow():
                    reply = b''
                    while len(reply) < 2:
                        part = peer.recv(2-len(reply))
                        assert part, 'disconnected during flow reply'
                        reply += part
                    assert reply == b'\x13\x11', reply
                for number,case in enumerate(c for c in VECTORS['cases'] if c['resolution'] == resolution):
                    token = f'POLY{number:02d}'
                    begin,end = case['caps']; style = case['style']
                    coordinates = ','.join(str(v) for pair in case['points'] for v in pair)
                    send(b'\x1bf\x1bE\x1bY2,ZG#g>0:T>2,1,1:T>2,1,0:M>4:L>17,23,37,47:' +
                         f'C>1,10:T>2,{style},{case["width"]}:T>2,{style},{END[begin][end]}:'.encode() +
                         f'M>{case["mode"]}:z {len(case["points"])},{coordinates}:'.encode() +
                         b'\x1bH' + token.encode())
                    expected = dict(case,points=((17,23),(37,47)))
                    check(client.wait_text(token),resolution,status,token,'vector',case=expected)
                    flow()
                # D still starts at the retained L endpoint after z.
                case = dict(points=((57,47),(57,47)),style=1,width=1,caps=(0,0),mode=1,
                            runs=[(47,37,21,9 & (15,3,1)[resolution])])
                send(b'\x1bf\x1bE\x1bY2,ZG#g>0:C>1,10:T>2,1,1:T>2,1,0:M>4:L>17,23,37,47:z>3,80,80,90,90,100,80:M>1:D 57,47:\x1bHRETAIN')
                check(client.wait_text('RETAIN'),resolution,status,'RETAIN','vector',case=case)
                flow()
                ch = 16 if resolution == 2 else 8
                for mode in range(1,5):
                    token = f'OVER{mode}'
                    send(b'\x1bf\x1bE\x1bY2,Z' +
                         f'G#g>0:C>1,1:T>2,1,1:T>2,1,0:M>4:L>94,{18*ch+2},106,{18*ch+2}:T>2,3,1:M>{mode}:z 2,94,{18*ch+2},106,{18*ch+2}:'.encode() +
                         b'\x1bH' + token.encode())
                    check(client.wait_text(token),resolution,status,token,'overlay',mode=mode)
                    flow()
                width, height = (320 if resolution == 0 else 640), (400 if resolution == 2 else 200)
                send(b'\x1bf\x1bE\x1bY2,Z' +
                     f'G#g>0:T>2,1,1:T>2,1,0:M>4:L>0,{height-1},{width-1},{height-1}:g>1:M>1:z 3,0,9999,5000,9999,9999,9999:'.encode() + b'\x1bHEDGE')
                check(client.wait_text('EDGE'),resolution,status,'EDGE','edge'); flow()
                peer.settimeout(0.03)
                try: reply = peer.recv(1)
                except socket.timeout: pass
                else: raise AssertionError(f'unexpected IGS reply: {reply!r}')
                send(b'G#z 128,1,2,')
                peer.shutdown(socket.SHUT_RDWR)
                client.wait_text('Disconnected: Enter exits, F1 details'); client.key('enter')
            client.wait_text('Directory: Name'); client.key('enter')
            with listener.accept()[0] as peer:
                client.wait_for(lambda s: s.application.get('connected') == 1)
                client.resize(1920,1440)
                peer.sendall(b'\x1bf\x1bE\x1bY2,Z\x1bHFRESH')
                check(client.wait_text('FRESH'),resolution,status,'FRESH','fresh')
                peer.sendall(b'G#z 2,40,60,80,60:')
                peer.settimeout(15); reply = b''
                while len(reply) < 2:
                    part = peer.recv(2-len(reply)); assert part; reply += part
                assert reply == b'\x13\x11', reply
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
