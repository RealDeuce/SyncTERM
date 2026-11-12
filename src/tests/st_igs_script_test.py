#!/usr/bin/env python3
"""X9 live script execution through transport and coherent bitmap captures."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from array import array
import json
from pathlib import Path
import socket
from st_igs_cursor_test import fixture, DATA, initial
from st_igs_macro_test import SCRIPT
from syncterm_fixture import cell_at, check_pixel_planes

# Literal points/replies and US TOS glyph rows; chapters 11/13/21/173.
# No reference image is produced by SyncTERM or a historical emulator.
A_ROWS=(0x18,0x3c,0x66,0x66,0x7e,0x66,0x66,0)
VECTORS=json.loads((Path(__file__).parent/'igs/script-command-vectors.json').read_text())
CASES=tuple((v['name'],v['wire'].encode('latin1'),v['points'],v['glyphs'],v['mode'])
            for v in VECTORS['raster_cases'])
CAPTURES=0


def run(executable,reports,resolution,encoding,status):
    width,height,ch=((320,200,8),(640,200,8),(640,400,16))[resolution]
    drawable=(24 if status else 25)*ch;mask=(15,3,1)[resolution]
    screen=('AtariST40x25','AtariST80x25','AtariST80x25Mono')[resolution]
    with socket.socket() as listener:
        listener.bind(('127.0.0.1',0));listener.listen(2);listener.settimeout(15)
        entries=('[IGS scripts]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                 f'Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\nScreenMode={screen}\n'
                 f'Font=Atari ST\nEncoding={encoding}\nNoStatus={str(not status).lower()}\n')
        with fixture(executable,entries=entries,script=SCRIPT,
                     settings='AudioModes=0\nMidiModes=0\nBlockyScaling=true\n',
                     reports=reports,quiet=False) as (client,root):
            client.wait_text('IGS scripts');client.key('enter');marker=1000

            def send(peer,wire):
                with (root/'wire.bin').open('ab') as log:log.write(wire)
                peer.sendall(wire)

            def read(peer,expected):
                actual=b''
                while len(actual)<len(expected):
                    part=peer.recv(len(expected)-len(actual));assert part,actual
                    actual+=part
                assert actual==expected,(actual,expected)

            def inspect(frame,case=None,active=False):
                global CAPTURES
                state=frame.application;meta=frame.metadata
                assert bool(state['cterm.igs_script_active'])==active
                assert not state['cterm.igs_loop_run'] and state['cterm.igs_loop_mode']==0
                assert not state['cterm.igs_collect_active'] and not state['cterm.igs_collect_pending']
                assert (state['cterm.x'],state['cterm.y'])==(5,3)
                assert not meta['cursor_visible']
                assert state['cterm.igs_writing_mode']==(case[4] if case else 1)
                assert tuple(frame.palette[768:784])==tuple(initial(resolution))
                expected=array('I',[768])*(width*drawable)
                samples={(24+x,2*ch+y) for y in range(ch) for x in range(8)}
                for y,bits in enumerate(DATA['fonts'][str(ch)]['65']):
                    for x in range(8):
                        if bits&(128>>x):expected[(2*ch+y)*width+24+x]=768+(3&mask)
                if case:
                    for x,y in case[2]:
                        expected[y*width+x]=768+mask;samples.add((x,y))
                    for origin in case[3]:
                        for y,bits in enumerate(A_ROWS):
                            for x in range(8):
                                if bits&(128>>x):expected[(94+y)*width+origin+x]=768+mask
                                samples.add((origin+x,94+y))
                check_pixel_planes(frame,expected,context=(resolution,encoding,status,case and case[0],active))
                assert cell_at(frame,3,2).glyph==65
                for x,y in samples:
                    ox=meta['destination_x']+(2*x+1)*meta['destination_width']//(2*width)
                    oy=meta['destination_y']+(2*y+1)*meta['destination_height']//(2*height)
                    assert frame.rgb[oy*meta['output_width']+ox]&0xffffff==frame.palette[expected[y*width+x]]
                CAPTURES+=1

            def scene(peer,wire=b'',reply=b'',case=None):
                nonlocal marker
                marker+=1;send(peer,wire+f'G#g>0:X 10,{marker},777:'.encode())
                frame=client.wait_for(lambda f:f.application.get('cterm.igs_draw_x')==marker)
                inspect(frame,case);read(peer,reply);return frame

            def baseline(peer):
                scene(peer,b'G#X>3,0:g>0:E>0,9,0:M 1:\x1bEG#p 3,2:A')

            def script(data):
                # The final excluded payload position is an explicit pad.
                return b'G#X>9,0:'+data+b'!||}X 9,1:'

            def setup(peer):
                client.wait_for(lambda f:f.application.get('connected')==1);client.resize(1280,960)
                assert client.capture().application['cterm.igs_script_length']==0
                send(peer,b'G#c>1,3:c 0,0:\x1bf');baseline(peer)

            def disconnect(peer):
                peer.shutdown(socket.SHUT_RDWR);client.wait_text('Disconnected: Enter exits, F1 details')
                client.key('enter');client.wait_text('Directory: Name')

            with listener.accept()[0] as peer:
                peer.settimeout(5);setup(peer)
                frame=scene(peer,b'G#X 9,0:G#P 40,80:!||}')
                assert frame.application['cterm.igs_script_length']==len(b'G#P 40,80:')
                for case in CASES:
                    baseline(peer);scene(peer,script(case[1]),case=case)
                # A held final G emits nothing on each call. Many short
                # sources yield across their enclosing serial loop and
                # retain the caller's single XOFF/XON lifetime and suffix.
                baseline(peer)
                scene(peer,b'G#X>9,0:G!||}& 0,130,1,0,X,2,9,1:',reply=b'\x13\x11')
                # Local input ends an inner pause; the source and real caller
                # chain then continue without forwarding that local key.
                baseline(peer);send(peer,script(b'G#t>30:P 40,80:'))
                frame=client.wait_for(lambda f:f.application.get('cterm.igs_script_active')==1)
                inspect(frame,active=True);read(peer,b'\x13')
                client.text('x');scene(peer,reply=b'\x11',case=CASES[0])
                baseline(peer);send(peer,script(b'G#t>30:P 40,80:'))
                frame=client.wait_for(lambda f:f.application.get('cterm.igs_script_active')==1)
                inspect(frame,active=True);read(peer,b'\x13');disconnect(peer)
            client.key('enter')
            with listener.accept()[0] as peer:
                peer.settimeout(5);setup(peer)
                scene(peer,script(b'G#?>0:P 40,80:'),reply=b'2.21',case=CASES[0]);disconnect(peer)
            assert b'[wren' not in client.stderr,bytes(client.stderr)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('executable')
    parser.add_argument('--reports',default='reports/test-control');args=parser.parse_args()
    for resolution in range(3):
        for encoding in ('ATARI-ST','CP437','UTF-8'):
            for status in (False,True):run(args.executable,args.reports,resolution,encoding,status)
    assert CAPTURES==666,CAPTURES
    print(f'{CAPTURES} coherent script captures',flush=True)
