#!/usr/bin/env python3
"""? replies, interactive motion and original pointer forms in coherent frames."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from array import array
import json
from pathlib import Path
import socket
from st_igs_cursor_test import fixture, DATA, initial
from syncterm_fixture import check_pixel_planes, cell_at
from st_igs_macro_test import SCRIPT

FORMS = json.loads((Path(__file__).parent/'igs/mouse-forms.json').read_text())
CAPTURES = 0


def run(executable, reports, resolution, encoding, status):
    global CAPTURES
    width, height, cell_height = ((320,200,8),(640,200,8),(640,400,16))[resolution]
    mask = (15,3,1)[resolution]
    drawable = (24 if status else 25)*cell_height
    screen = ('AtariST40x25','AtariST80x25','AtariST80x25Mono')[resolution]
    with socket.socket() as listener:
        listener.bind(('127.0.0.1',0)); listener.listen(2); listener.settimeout(15)
        entries = ('[IGS queries]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                   f'Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\nScreenMode={screen}\n'
                   f'Font=Atari ST\nEncoding={encoding}\nNoStatus={str(not status).lower()}\n')
        with fixture(executable,entries=entries,script=SCRIPT,settings='AudioModes=0\nMidiModes=0\nBlockyScaling=true\n',
                     reports=reports,quiet=False) as (client,root):
            client.wait_text('IGS queries'); client.key('enter'); marker=1000

            def send(peer,wire):
                with (root/'wire.bin').open('ab') as log: log.write(wire)
                peer.sendall(wire)

            def read(peer,expected):
                actual=b''
                while len(actual)<len(expected):
                    part=peer.recv(len(expected)-len(actual)); assert part,actual
                    actual+=part
                assert actual==expected,(actual,expected)

            def inspect(frame,active=False,point=None,form=None,marker_point=None,cursor=(5,3)):
                global CAPTURES
                s=frame.application; m=frame.metadata
                assert bool(s['cterm.igs_query_active'])==active
                assert bool(s['cterm.igs_input_paused'])==active
                assert (s['cterm.x'],s['cterm.y'])==cursor
                assert s['cterm.igs_writing_mode']==(3 if active and (form is not None or marker_point is not None) else 2)
                assert not m['cursor_visible']
                assert tuple(frame.palette[768:784])==tuple(initial(resolution))
                expected=array('I',[768])*(width*drawable)
                for row,bits in enumerate(DATA['fonts'][str(cell_height)]['65']):
                    for col in range(8):
                        if bits&(128>>col): expected[(2*cell_height+row)*width+24+col]=768+(3&mask)
                if form is not None:
                    original=FORMS[form]; hx=original['hotspot_x']; hy=original['hotspot_y']
                    for row in range(16):
                        y=point[1]-hy+row
                        for col in range(16):
                            x=point[0]-hx+col
                            if not(0<=x<width and 0<=y<drawable): continue
                            bit=0x8000>>col
                            if original['data_words'][row]&bit: expected[y*width+x]=768+mask
                            elif original['mask_words'][row]&bit: expected[y*width+x]=768
                if marker_point is not None:
                    x,y=marker_point; offset=y*width+x
                    expected[offset]=768+((expected[offset]-768)^mask)
                check_pixel_planes(frame,expected,context=(resolution,status,active,form,point,marker_point))
                assert cell_at(frame,3,2).glyph==65
                # RGB uses the same coherent capture as cells, state and both
                # raster phases; cover every overlay pixel, including clipping.
                locations=[(24+i,2*cell_height+j) for j in range(cell_height) for i in range(8)]
                if form is not None:
                    original=FORMS[form]
                    locations += [(point[0]-original['hotspot_x']+i,point[1]-original['hotspot_y']+j)
                                  for j in range(16) for i in range(16)]
                if marker_point is not None: locations.append(marker_point)
                for x,y in locations:
                    if not(0<=x<width and 0<=y<drawable): continue
                    ox=m['destination_x']+(2*x+1)*m['destination_width']//(2*width)
                    oy=m['destination_y']+(2*y+1)*m['destination_height']//(2*height)
                    assert frame.rgb[oy*m['output_width']+ox]&0xffffff==frame.palette[expected[y*width+x]]
                CAPTURES+=1

            def scene(peer,wire=b'',reply=b''):
                nonlocal marker
                marker+=1; send(peer,wire+f'G#X 10,{marker},777:'.encode())
                frame=client.wait_for(lambda f:f.application.get('cterm.igs_draw_x')==marker)
                inspect(frame); read(peer,reply)
                return frame

            def mouse(x,y,buttons=0):
                frame=client.capture(); m=frame.metadata; before=m['events_dequeued']
                ox=m['destination_x']+(2*x+1)*m['destination_width']//(2*width)
                oy=m['destination_y']+(2*y+1)*m['destination_height']//(2*height)
                client.mouse(ox,oy,action=1,buttons=buttons)
                return client.wait_for(lambda f:f.metadata['events_dequeued']>before)

            def probe(peer,prefix=b''):
                client.key('f8'); read(peer,prefix+b'STATE 3 false false\n')

            def setup(peer):
                client.wait_for(lambda f:f.application.get('connected')==1); client.resize(1280,960)
                scene(peer,b'G#c>1,3:c 0,0:\x1bE\x1bfG#p>3,2:M 2:A')

            def disconnect(peer):
                peer.shutdown(socket.SHUT_RDWR); client.wait_text('Disconnected: Enter exits, F1 details')
                client.key('enter'); client.wait_text('Directory: Name')

            with listener.accept()[0] as peer:
                peer.settimeout(5); setup(peer)
                scene(peer,b'G#? 0:',b'2.21'); scene(peer,b'G#? 3:',f'{resolution}:'.encode())
                scene(peer,b'G#? 1,0:',bytes((36,34,32)))
                mouse(100,100); scene(peer,b'G#? 2,0:',b'100,100,0:')
                # All8 original AES forms; move once after press too, since
                # GEM remains live while the release coordinates are acquired.
                for form in range(8):
                    x,y=(1,1) if form==0 else (40+form*20,80)
                    mouse(x,y); send(peer,f'G#? 2,{form+3}:'.encode())
                    frame=client.wait_for(lambda f:f.application.get('cterm.igs_query_active')==1)
                    inspect(frame,True,(x,y),form); probe(peer)
                    frame=mouse(x+3,y+2,1); inspect(frame,True,(x+3,y+2),form)
                    frame=mouse(x+7,y+4,1); inspect(frame,True,(x+7,y+4),form)
                    mouse(x+11,y+6,0); scene(peer,reply=f'{x+11},{y+6},1:'.encode())
                # Polymarker moves only in the press loop, unlike GEM.
                mouse(100,100); send(peer,b'G#? 2,1:')
                frame=client.wait_for(lambda f:f.application.get('cterm.igs_query_active')==1)
                inspect(frame,True,marker_point=(100,100))
                frame=mouse(110,110,1); inspect(frame,True,marker_point=(110,110))
                frame=mouse(120,120,1); inspect(frame,True,marker_point=(110,110))
                mouse(130,130,0); scene(peer,reply=b'130,130,1:')
                # Character motion is one cell per direction sample; release
                # movement changes neither the cursor nor the press buttons.
                mouse(100,100); send(peer,b'G#? 1,1:')
                frame=client.wait_for(lambda f:f.application.get('cterm.igs_query_active')==1)
                inspect(frame,True)
                frame=mouse(150,150); inspect(frame,True,cursor=(6,4))
                frame=mouse(140,140,5); inspect(frame,True,cursor=(5,3))
                client.text('é'); client.wait_for(lambda f:f.application.get('cterm.igs_music_keys')==1)
                mouse(170,170,0)
                scene(peer,reply=bytes((36,34,35))+(b'\xc3\xa9' if encoding=='UTF-8' else b'\x82'))
                # Incomplete interactive work is cancelled without a reply and
                # its visible pointer is restored before session teardown.
                mouse(100,100); send(peer,b'G#? 2,4:')
                frame=client.wait_for(lambda f:f.application.get('cterm.igs_query_active')==1)
                inspect(frame,True,(100,100),1); disconnect(peer)
            client.key('enter')
            with listener.accept()[0] as peer:
                peer.settimeout(5); setup(peer); scene(peer,b'G#? 0:',b'2.21'); disconnect(peer)
            assert b'[wren' not in client.stderr,bytes(client.stderr)


if __name__=='__main__':
    parser=argparse.ArgumentParser(); parser.add_argument('executable')
    parser.add_argument('--reports',default='reports/test-control'); args=parser.parse_args()
    for resolution in range(3):
        for encoding in ('ATARI-ST','CP437','UTF-8'):
            for status in (False,True): run(args.executable,args.reports,resolution,encoding,status)
    assert CAPTURES==864,CAPTURES
    print(f'{CAPTURES} coherent query captures',flush=True)
