#!/usr/bin/env python3
"""< local keyboard/zone input, chain-deferred bytes and coherent pointer scenes."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from array import array
import socket
from st_igs_cursor_test import fixture, DATA, initial
from st_igs_query_test import FORMS
from st_igs_macro_test import SCRIPT
from syncterm_fixture import check_pixel_planes, cell_at

CAPTURES = 0


def run(executable, reports, resolution, encoding, status):
    width,height,ch=((320,200,8),(640,200,8),(640,400,16))[resolution]
    drawable=(24 if status else 25)*ch; mask=(15,3,1)[resolution]
    screen=('AtariST40x25','AtariST80x25','AtariST80x25Mono')[resolution]
    with socket.socket() as listener:
        listener.bind(('127.0.0.1',0));listener.listen(2);listener.settimeout(15)
        entries=('[IGS collection]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                 f'Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\nScreenMode={screen}\n'
                 f'Font=Atari ST\nEncoding={encoding}\nNoStatus={str(not status).lower()}\n')
        with fixture(executable,entries=entries,script=SCRIPT,settings='AudioModes=0\nMidiModes=0\nBlockyScaling=true\n',
                     reports=reports,quiet=False) as (client,root):
            client.wait_text('IGS collection');client.key('enter');marker=1000
            cells={(3,2):65};cursor=(5,3)

            def send(peer,wire):
                with (root/'wire.bin').open('ab') as log:log.write(wire)
                peer.sendall(wire)

            def read(peer,expected):
                actual=b''
                while len(actual)<len(expected):
                    part=peer.recv(len(expected)-len(actual));assert part,actual
                    actual+=part
                assert actual==expected,(actual,expected)

            def inspect(frame,active=False,pending=False,count=None,form=None,point=None,marker_point=None):
                global CAPTURES
                s=frame.application;m=frame.metadata
                assert bool(s['cterm.igs_collect_active'])==active
                assert bool(s['cterm.igs_collect_pending'])==pending
                assert bool(s['cterm.igs_input_paused'])==active
                if count is not None:assert s['cterm.igs_collect_count']==count
                assert (s['cterm.x'],s['cterm.y'])==cursor
                assert s['cterm.igs_writing_mode']==(3 if active and (form is not None or marker_point is not None) else 2)
                assert not m['cursor_visible']
                assert tuple(frame.palette[768:784])==tuple(initial(resolution))
                expected=array('I',[768])*(width*drawable)
                locations=[]
                for (cx,cy),glyph in cells.items():
                    assert cell_at(frame,cx,cy).glyph==glyph
                    for row,bits in enumerate(DATA['fonts'][str(ch)][str(glyph)]):
                        for col in range(8):
                            x,y=cx*8+col,cy*ch+row
                            locations.append((x,y))
                            if bits&(128>>col):expected[y*width+x]=768+(3&mask)
                if form is not None:
                    f=FORMS[form]
                    for row in range(16):
                        for col in range(16):
                            x,y=point[0]-f['hotspot_x']+col,point[1]-f['hotspot_y']+row
                            if not(0<=x<width and 0<=y<drawable):continue
                            locations.append((x,y));bit=0x8000>>col
                            if f['data_words'][row]&bit:expected[y*width+x]=768+mask
                            elif f['mask_words'][row]&bit:expected[y*width+x]=768
                if marker_point is not None:
                    x,y=marker_point;expected[y*width+x]=768+((expected[y*width+x]-768)^mask);locations.append((x,y))
                check_pixel_planes(frame,expected,context=(resolution,encoding,status,active,pending,form,point))
                for x,y in locations:
                    ox=m['destination_x']+(2*x+1)*m['destination_width']//(2*width)
                    oy=m['destination_y']+(2*y+1)*m['destination_height']//(2*height)
                    assert frame.rgb[oy*m['output_width']+ox]&0xffffff==frame.palette[expected[y*width+x]]
                CAPTURES+=1

            def scene(peer,wire=b'',reply=b''):
                nonlocal marker
                marker+=1;send(peer,wire+f'G#X 10,{marker},777:'.encode())
                frame=client.wait_for(lambda f:f.application.get('cterm.igs_draw_x')==marker)
                inspect(frame);read(peer,reply);return frame

            def probe(peer,mode=0,prefix=b'',disabled=False):
                client.key('f8');read(peer,prefix+f'STATE {mode} {str(disabled).lower()} false\n'.encode())

            def mouse(x,y,buttons=0):
                frame=client.capture();m=frame.metadata;before=m['events_dequeued']
                ox=m['destination_x']+(2*x+1)*m['destination_width']//(2*width)
                oy=m['destination_y']+(2*y+1)*m['destination_height']//(2*height)
                client.mouse(ox,oy,action=1,buttons=buttons)
                return client.wait_for(lambda f:f.metadata['events_dequeued']>before)

            def active(peer,wire,**kwargs):
                send(peer,wire);frame=client.wait_for(lambda f:f.application.get('cterm.igs_collect_active')==1)
                inspect(frame,active=True,**kwargs);return frame

            def setup(peer):
                nonlocal cursor
                client.wait_for(lambda f:f.application.get('connected')==1);client.resize(1280,960)
                cursor=(5,3);cells.clear();cells[(3,2)]=65
                scene(peer,b'G#c>1,3:c 0,0:\x1bE\x1bfG#p>3,2:M 2:A')

            def disconnect(peer):
                peer.shutdown(socket.SHUT_RDWR);client.wait_text('Disconnected: Enter exits, F1 details')
                client.key('enter');client.wait_text('Directory: Name')

            with listener.accept()[0] as peer:
                peer.settimeout(5);setup(peer)
                # Reply waits for the final chain command, with explicit fences
                # proving no premature bytes and later collection replacement.
                active(peer,b'G#<>1,0,0:',pending=True);probe(peer)
                client.text('A');frame=client.wait_for(lambda f:f.application.get('cterm.igs_collect_active')==0)
                inspect(frame,pending=True,count=1);probe(peer)
                active(peer,b'<>0,0,0:',pending=True);client.text('b')
                frame=client.wait_for(lambda f:f.application.get('cterm.igs_collect_active')==0)
                inspect(frame,pending=True,count=1);probe(peer)
                scene(peer,b'M 2:',b'b')
                # Exact echo/delete behavior for all four option branches.
                for option in range(4):
                    cells.pop((10,4),None);cells.pop((11,4),None);cursor=(11,5)
                    scene(peer,b'G#p 10,4:\x1bK')
                    active(peer,f'G#< 1,1,{option}:'.encode(),pending=option<2);probe(peer)
                    client.text('Ab');frame=client.wait_for(lambda f:f.application.get('cterm.igs_collect_count')==2)
                    if option in (1,2):cells[(10,4)]=65;cells[(11,4)]=98;cursor=(13,5)
                    inspect(frame,active=True,pending=option<2,count=2)
                    client.key('delete' if option in (1,2) else 'backspace');client.wait_for(lambda f:f.application.get('cterm.igs_collect_count')==1)
                    if option in (1,2):cells.pop((11,4),None);cursor=(12,5)
                    else:cursor=(10,5) # Backspace always emits ESC D,space,ESC D.
                    client.text('c');frame=client.wait_for(lambda f:f.application.get('cterm.igs_collect_count')==2)
                    if option in (1,2):cells[(11,4)]=99;cursor=(13,5)
                    inspect(frame,active=True,pending=option<2,count=2);probe(peer)
                    client.key('enter');client.wait_for(lambda f:f.application.get('cterm.igs_collect_active')==0)
                    scene(peer,reply=b'Ac\r' if option<2 else b'')
                # Return to one known glyph, then load binary zone payloads.
                cursor=(5,3);cells.clear();cells[(3,2)]=65
                scene(peer,b'\x1bEG#p 3,2:A')
                scene(peer,b'G#X>4,9999:X>4,0,10,20,30,40,3:A\0b!X>4,1,10,20,30,40,1:c!X 4,47,100,100,110,110,1:b!')
                for form in range(8):
                    x,y=(1,1) if form==0 else (50+form*15,80)
                    mouse(x,y);active(peer,f'G#< 1,{form+3},0:'.encode(),pending=True,form=form,point=(x,y));probe(peer,3)
                    frame=mouse(30,40,5);inspect(frame,True,True,form=form,point=(30,40))
                    frame=mouse(100,100,1);inspect(frame,True,True,form=form,point=(100,100))
                    mouse(101,101,0);scene(peer,reply=b'A\0b\r')
                # Type>10 normalizes to the current polymarker. Release motion
                # changes neither its position nor the selected press zone.
                mouse(50,60);active(peer,b'G#< 0,11,0:',pending=True,marker_point=(50,60))
                frame=mouse(30,40,1);inspect(frame,True,True,marker_point=(30,40))
                frame=mouse(100,100,1);inspect(frame,True,True,marker_point=(30,40))
                mouse(101,101,0);scene(peer,reply=b'A\0b')
                # Default miss selects47; strict miss retries, retaining the
                # queued Unicode key until successful collection completes.
                mouse(50,60);active(peer,b'G#< 0,2,0:',pending=True,marker_point=(50,60))
                mouse(70,70,1);mouse(70,70,0);scene(peer,reply=b'b')
                scene(peer,b'G#X 4,9998:');mouse(50,60)
                active(peer,b'G#< 0,3,0:',pending=True,form=0,point=(50,60))
                mouse(70,70,1);frame=mouse(70,70,0);inspect(frame,True,True,form=0,point=(70,70));probe(peer,3)
                client.text('é');client.wait_for(lambda f:f.application.get('cterm.igs_music_keys')==1)
                mouse(10,20,4);mouse(200,180,0)
                scene(peer,reply=b'A\0b'+(b'\xc3\xa9' if encoding=='UTF-8' else b'\x82'))
                # A press claimed while forwarding is disabled stays suppressed
                # after re-enabling; held motion cannot select a zone.
                mouse(50,60);active(peer,b'G#< 0,3,0:',pending=True,form=0,point=(50,60))
                client.command_key(24);probe(peer,3,disabled=True)
                frame=mouse(20,30,1);inspect(frame,True,True,form=0,point=(50,60))
                client.command_key(24);probe(peer,3)
                frame=mouse(21,31,1);inspect(frame,True,True,form=0,point=(21,31))
                assert not frame.application['cterm.igs_collect_release']
                mouse(21,31,0);mouse(10,20,1);mouse(200,180,0);scene(peer,reply=b'A\0b')
                # Held software pointer cancellation and clean reconnect.
                mouse(50,60);active(peer,b'G#< 1,4,0:',pending=True,form=1,point=(50,60));disconnect(peer)
            client.key('enter')
            with listener.accept()[0] as peer:
                peer.settimeout(5);setup(peer);active(peer,b'G#< 1,0,0:',pending=True)
                client.text('A');client.wait_for(lambda f:f.application.get('cterm.igs_collect_active')==0)
                scene(peer,reply=b'A\r');disconnect(peer)
            assert b'[wren' not in client.stderr,bytes(client.stderr)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('executable');p.add_argument('--reports',default='reports/test-control');args=p.parse_args()
    for resolution in range(3):
        for encoding in ('ATARI-ST','CP437','UTF-8'):
            for status in (False,True):run(args.executable,args.reports,resolution,encoding,status)
    assert CAPTURES==1404,CAPTURES
    print(f'{CAPTURES} coherent collection captures',flush=True)
