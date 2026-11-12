#!/usr/bin/env python3
"""& source-derived loops through real transport and coherent bitmap captures."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from array import array
import json
from pathlib import Path
import socket
from st_igs_cursor_test import fixture, DATA, initial
from st_igs_macro_test import SCRIPT
from syncterm_fixture import cell_at, check_pixel_planes
VECTORS=json.loads((Path(__file__).parent/'igs/loop-command-vectors.json').read_text())
A_ROWS=(0x18,0x3c,0x66,0x66,0x7e,0x66,0x66,0)
CAPTURES=0


def run(executable,reports,resolution,encoding,status):
    width,height,ch=((320,200,8),(640,200,8),(640,400,16))[resolution]
    drawable=(24 if status else 25)*ch;mask=(15,3,1)[resolution]
    screen=('AtariST40x25','AtariST80x25','AtariST80x25Mono')[resolution]
    with socket.socket() as listener:
        listener.bind(('127.0.0.1',0));listener.listen(2);listener.settimeout(15)
        entries=('[IGS loops]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                 f'Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\nScreenMode={screen}\n'
                 f'Font=Atari ST\nEncoding={encoding}\nNoStatus={str(not status).lower()}\n')
        with fixture(executable,entries=entries,script=SCRIPT,settings='AudioModes=0\nMidiModes=0\nBlockyScaling=true\n',reports=reports,quiet=False) as (client,root):
            client.wait_text('IGS loops');client.key('enter');marker=1000

            def send(peer,wire):
                with (root/'wire.bin').open('ab') as log:log.write(wire)
                peer.sendall(wire)

            def read(peer,expected):
                actual=b''
                while len(actual)<len(expected):
                    part=peer.recv(len(expected)-len(actual));assert part,actual
                    actual+=part
                assert actual==expected,(actual,expected)

            def inspect(frame,case=None,active=False,collection=False):
                global CAPTURES
                s=frame.application;m=frame.metadata
                assert bool(s['cterm.igs_loop_run'])==active
                assert bool(s['cterm.igs_collect_active'])==collection
                assert bool(s['cterm.igs_collect_pending'])==collection
                if not active:assert s['cterm.igs_loop_mode']==0
                assert (s['cterm.x'],s['cterm.y'])==(5,3)
                assert not m['cursor_visible']
                assert s['cterm.igs_writing_mode']==(case['mode'] if case else 1)
                assert tuple(frame.palette[768:784])==tuple(initial(resolution))
                expected=array('I',[768])*(width*drawable)
                for y,bits in enumerate(DATA['fonts'][str(ch)]['65']):
                    for x in range(8):
                        if bits&(128>>x):expected[(2*ch+y)*width+24+x]=768+(3&mask)
                samples={(24+x,2*ch+y) for y in range(ch) for x in range(8)}
                if case:
                    for x,y in case['points']:
                        expected[y*width+x]=768+mask;samples.add((x,y))
                    for origin in case['glyphs']:
                        for y,bits in enumerate(A_ROWS):
                            for x in range(8):
                                if bits&(128>>x):expected[(94+y)*width+origin+x]=768+mask
                                samples.add((origin+x,94+y))
                check_pixel_planes(frame,expected,context=(resolution,encoding,status,case and case['name'],active,collection))
                assert cell_at(frame,3,2).glyph==65
                for x,y in samples:
                    ox=m['destination_x']+(2*x+1)*m['destination_width']//(2*width)
                    oy=m['destination_y']+(2*y+1)*m['destination_height']//(2*height)
                    assert frame.rgb[oy*m['output_width']+ox]&0xffffff==frame.palette[expected[y*width+x]]
                CAPTURES+=1

            def scene(peer,wire=b'',reply=b'',case=None):
                nonlocal marker
                marker+=1;send(peer,wire+f'G#g>0:X 10,{marker},777:'.encode())
                frame=client.wait_for(lambda f:f.application.get('cterm.igs_draw_x')==marker)
                inspect(frame,case);read(peer,reply);return frame

            def baseline(peer):
                scene(peer,b'G#X>3,0:g>0:E>0,9,0:M 1:\x1bEG#p 3,2:A')

            def setup(peer):
                client.wait_for(lambda f:f.application.get('connected')==1);client.resize(1280,960)
                send(peer,b'G#c>1,3:c 0,0:\x1bf');baseline(peer)

            def probe(peer,prefix=b''):
                client.key('f8');read(peer,prefix+b'STATE 0 false false\n')

            def disconnect(peer):
                peer.shutdown(socket.SHUT_RDWR);client.wait_text('Disconnected: Enter exits, F1 details')
                client.key('enter');client.wait_text('Directory: Name')

            with listener.accept()[0] as peer:
                peer.settimeout(5);setup(peer)
                for case in VECTORS['raster_cases']:
                    baseline(peer);scene(peer,case['wire'].encode('latin1'),case['reply'].encode('latin1'),case)
                # Repeated input collections replace the pending reply; neither
                # a per-iteration reply nor queued graphics may escape early.
                baseline(peer);send(peer,b'G#& 0,2,1,0,<,3,1,0,0:')
                frame=client.wait_for(lambda f:f.application.get('cterm.igs_collect_active')==1)
                inspect(frame,active=True,collection=True);probe(peer,b'\x13')
                client.text('A');frame=client.wait_for(lambda f:f.application.get('cterm.igs_collect_active')==1 and f.application.get('cterm.igs_loop_current')==2)
                inspect(frame,active=True,collection=True);probe(peer)
                client.text('b');client.wait_for(lambda f:f.application.get('cterm.igs_loop_run')==0)
                scene(peer,reply=b'\x11b\r')
                # A local key wakes a numeric delay and Ctrl-C aborts at that
                # actual operand call, without being forwarded to the host.
                baseline(peer);send(peer,b'G#& 0,9999,1,100,M,1,0:')
                frame=client.wait_for(lambda f:f.application.get('cterm.igs_loop_waiting')==1)
                inspect(frame,active=True);probe(peer,b'\x13')
                client.key(3);client.wait_for(lambda f:f.application.get('cterm.igs_loop_run')==0)
                scene(peer,reply=b'\x11')
                # Disconnect owns cancellation even while no serial bytes can
                # advance the active source; a fresh session has no old loop.
                send(peer,b'G#& 0,9999,1,100,M,1,0:')
                frame=client.wait_for(lambda f:f.application.get('cterm.igs_loop_waiting')==1)
                inspect(frame,active=True);disconnect(peer)
            client.key('enter')
            with listener.accept()[0] as peer:
                peer.settimeout(5);setup(peer);case=VECTORS['raster_cases'][0]
                scene(peer,case['wire'].encode(),case['reply'].encode('latin1'),case);disconnect(peer)
            assert b'[wren' not in client.stderr,bytes(client.stderr)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('executable');p.add_argument('--reports',default='reports/test-control');args=p.parse_args()
    for resolution in range(3):
        for encoding in ('ATARI-ST','CP437','UTF-8'):
            for status in (False,True):run(args.executable,args.reports,resolution,encoding,status)
    assert CAPTURES==846,CAPTURES
    print(f'{CAPTURES} coherent loop captures',flush=True)
