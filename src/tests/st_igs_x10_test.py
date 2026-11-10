#!/usr/bin/env python3
"""X10's non-drawing position, intended X/Y scaling and subsequent Drawto.

Literal positions follow IG221.TXT's L/P-equivalence and normalized canvas.
The original binary's 0/1 reader classes are corrected, explicitly in docs.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import socket
import sys
from pathlib import Path

sys.path.insert(0,str(Path(__file__).resolve().parents[2] / 'tools' / 'testing'))
from syncterm_fixture import fixture
from st_igs_scaling_test import check as check_scene
from st_igs_line_test import check as check_line


def check(frame, actual, captured, scaling, entry, status, seeded, token, positions, mode):
    check_scene(frame,actual,captured,scaling,entry,status,seeded,token)
    state, m = frame.application, frame.metadata
    assert tuple(state[f'cterm.igs_{name}'] for name in ('draw_start_x','draw_start_y','draw_x','draw_y')) == positions
    assert state['cterm.igs_writing_mode'] == mode and state['cterm.igs_last_line'] == 1
    assert tuple(state[f'cterm.igs_{name}'] for name in ('line_type','line_width','line_begin','line_end','user_line')) == (1,1,0,0,0xffff)
    assert state['cterm.igs_random_seed'] == 1
    samples = [(64,64,768)]
    if seeded: samples.append((99,12*(16 if actual == 2 else 8)+(2 if actual == 2 else 0),768+(15,3,1)[actual]))
    for x,y,index in samples:
        ox = m['destination_x']+(2*x+1)*m['destination_width']//(2*m['plane_width'])
        oy = m['destination_y']+(2*(y+m['pixel_top'])+1)*m['destination_height']//(2*m['plane_height'])
        assert frame.rgb[oy*m['output_width']+ox] & 0xffffff == frame.palette[index], (actual,token,x,y)


def run(executable, reports, entry, encoding, status):
    screen = ('AtariST40x25','AtariST80x25','AtariST80x25Mono')[entry]
    with socket.socket() as listener:
        listener.bind(('127.0.0.1',0)); listener.listen(2); listener.settimeout(15)
        entries = ('[IGS X10]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                   f'Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\n'
                   f'ScreenMode={screen}\nFont=Atari ST\nEncoding={encoding}\n'
                   f'NoStatus={str(not status).lower()}\n')
        with fixture(executable,entries=entries,settings='AudioModes=0\nMidiModes=0\nBlockyScaling=true\n',
                     reports=reports,quiet=False) as (client,root):
            client.wait_text('IGS X10'); client.key('enter')
            with listener.accept()[0] as peer:
                client.wait_for(lambda s: s.application.get('connected') == 1)
                client.resize(960,720)
                peer.sendall(b'\x1bf\x1bE\x1bY,,ZG#g>0:M>4:L 10,20,30,40:')
                other = 1-entry if entry != 2 else 2
                half_x, half_y = (160 if entry == 0 else 320),(200 if entry == 2 else 100)
                other_x = 160 if other == 0 else 320
                other_y = 200 if other == 2 else 100
                steps = (
                    (b'G#X 10,40,60:',entry,entry,0,True,(10,20,40,60),4),
                    (b'G#g>1:X 10,5000,5000:',entry,entry,1,True,(10,20,half_x,half_y),4),
                    (b'G#X 9999:',entry,entry,1,True,(10,20,half_x,half_y),4),
                    (b'G#g>2:X 10,40,60:',entry,entry,2,True,(10,20,40,120 if entry == 2 else 60),4),
                    (b'G#I 2:',entry,entry,2,True,(10,20,40,120 if entry == 2 else 60),1),
                    (b'G#X>16:M 4:',entry,entry,2,True,(10,20,40,120 if entry == 2 else 60),4),
                    (f'G#R {other},0:'.encode(),other,entry,2,entry == 2,(10,20,40,120 if entry == 2 else 60),4),
                    (b'G#g>1:X 10,5000,5000:',other,other,1,entry == 2,(10,20,other_x,other_y),4),
                    (b'G#I 5:',entry,other,1,entry == 2,(10,20,other_x,other_y),4),
                    (b'G#X 10,9999,9999:',entry,other,1,entry == 2,
                     (10,20,319 if other == 0 else 639,399 if other == 2 else 199),4),
                    (b'G#I 1:',entry,other,1,entry == 2,
                     (10,20,319 if other == 0 else 639,399 if other == 2 else 199),4),
                )
                for number,(command,actual,captured,scaling,seeded,positions,mode) in enumerate(steps):
                    token = f'XPOS{number:02d}'
                    wire = command+b'\x1bH\x1bf'+token.encode()
                    with (root/'wire.bin').open('ab') as stream: stream.write(wire)
                    peer.sendall(wire)
                    check(client.wait_text(token),actual,captured,scaling,entry,status,seeded,token,positions,mode)
                # X10 establishes a normalized origin; g0 changes how the new
                # D operand is read, without rescaling that remembered origin.
                client.resize(1920,1440)
                ex,ey = half_x+20,half_y
                wire = (b'\x1bf\x1bE\x1bY2,ZG#g>1:M>1:C>1,10:X>10,5000,5000:g>0:'+
                        f'D {ex},{ey}:'.encode()+b'\x1bHFOLLOW')
                with (root/'wire.bin').open('ab') as stream: stream.write(wire)
                peer.sendall(wire)
                case = dict(points=((half_x,half_y),(ex,ey)),style=1,width=1,caps=(0,0),mode=1,
                            runs=[(half_y,half_x,21,9 & (15,3,1)[entry])])
                check_line(client.wait_text('FOLLOW'),entry,status,'FOLLOW','vector',case=case,drawto=True)
                peer.settimeout(0.03)
                try: reply = peer.recv(1)
                except socket.timeout: pass
                else: raise AssertionError(f'unexpected IGS reply: {reply!r}')
                peer.sendall(b'G#X 10,1,')
                peer.shutdown(socket.SHUT_RDWR)
                client.wait_text('Disconnected: Enter exits, F1 details'); client.key('enter')
            client.wait_text('Directory: Name'); client.key('enter')
            with listener.accept()[0] as peer:
                client.wait_for(lambda s: s.application.get('connected') == 1)
                client.resize(960,720)
                peer.sendall(b'\x1bf\x1bE\x1bY,,Z\x1bHFRESH')
                check(client.wait_text('FRESH'),entry,entry,0,entry,status,True,'FRESH',(0,0,0,0),1)
                peer.shutdown(socket.SHUT_RDWR)
                client.wait_text('Disconnected: Enter exits, F1 details'); client.key('enter')
            client.wait_text('Directory: Name')
            assert b'[wren' not in client.stderr,bytes(client.stderr)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('executable'); parser.add_argument('--reports',default='reports/test-control')
    args = parser.parse_args()
    for entry in range(3):
        for encoding in ('ATARI-ST','CP437','UTF-8'):
            for status in (False,True): run(args.executable,args.reports,entry,encoding,status)
