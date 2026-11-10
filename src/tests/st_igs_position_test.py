#!/usr/bin/env python3
"""p: source-defined word conversion and persistent VT52 composition."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import json
import socket
from pathlib import Path
from st_igs_console_color_test import check,PREFIXES,fixture
from st_igs_box_test import check as check_blank

DATA=json.loads((Path(__file__).parent/'igs'/'position.json').read_text())


def expected(case,columns,rows):
    # This interpreter uses source states and arithmetic, not CTerm dispatch,
    # cursor helpers, font rendering or captured emulator output.
    state=3 if case['state']==6 else case['state'];x,y=3,2
    row=rows-1 if case['state']==6 else 1
    fg,bg=(15-32)%65536,(case['background']-32)%65536
    cells={};row_background=[0]*rows;scrolls=0
    def down():
        nonlocal y,cells,scrolls
        if y+1<rows:y+=1
        else:
            cells={(cx,cy-1):value for (cx,cy),value in cells.items() if cy>0}
            row_background.pop(0);row_background.append(bg);scrolls+=1
    for byte in (27,89,(case['row']+32)%256,(case['column']+32)%256):
        if state==1:state={89:2,98:4,99:5}.get(byte,0)
        elif state==2:row=min((byte-32)%65536,rows-1);state=3
        elif state==3:x=min((byte-32)%65536,columns-1);y=row;state=0
        elif state==4:fg=(byte-32)%65536;state=0
        elif state==5:bg=(byte-32)%65536;state=0
        elif byte==27:state=1
        elif byte==8:x=max(0,x-1)
        elif byte==9:x=min((x//8+1)*8,columns-1)
        elif byte in (10,11,12):down()
        elif byte==13:x=0
        elif byte>=32:
            cells[x,y]=(byte,bg if case['reverse'] else fg,fg if case['reverse'] else bg)
            if x+1<columns:x+=1
            elif case['wrap']:x=0;down()
    return dict(state=state,x=x,y=y,fg=fg,bg=bg,cells=cells,row_background=row_background,scrolls=scrolls)


def run(executable,reports,resolution,encoding,status):
    screen=('AtariST40x25','AtariST80x25','AtariST80x25Mono')[resolution]
    with socket.socket() as listener:
        listener.bind(('127.0.0.1',0));listener.listen(2);listener.settimeout(15)
        entries=('[IGS position]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                 f'Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\nScreenMode={screen}\n'
                 f'Font=Atari ST\nEncoding={encoding}\nNoStatus={str(not status).lower()}\n')
        with fixture(executable,entries=entries,settings='AudioModes=0\nMidiModes=0\nBlockyScaling=true\n',reports=reports,quiet=False) as (client,root):
            client.wait_text('IGS position');client.key('enter')
            with listener.accept()[0] as peer:
                client.wait_for(lambda f:f.application.get('connected')==1);client.resize(1920,1440)
                def send(wire):
                    with (root/'wire.bin').open('ab') as stream:stream.write(wire)
                    peer.sendall(wire)
                resync=b'\0\0\x1b\x1b\x1bq\x1bf\x1bw'
                for index,case in enumerate(c for c in DATA['cases'] if c['resolution']==resolution):
                    marker=1000+index
                    setup=resync+b'G#g>0:c>1,15:c 0,0:\x1bE'
                    setup+=b'G#c>0,2:C>3,7:E>0,9,0:M>1:W 100,60,A@\x1bY\x22\x23'
                    if case['reverse']:setup+=b'\x1bp'
                    if case['state']==6:setup+=b'\x1bv\x1bY'+bytes([32+(24 if status else 25)-1])
                    else:setup+=PREFIXES[case['state']]
                    # Vary g after the existing W glyph, then restore it before
                    # the non-drawing X10 fence; p itself stays unscaled.
                    send(setup+f'G#g>{index%3}:p>{case["column"]},{case["row"]}:g>0:X 10,{marker},777:'.encode())
                    frame=client.wait_for(lambda f:f.application.get('cterm.igs_draw_x')==marker)
                    check(frame,resolution,status,marker,case,model=expected(case,40 if resolution==0 else 80,24 if status else 25),fonts=DATA['fonts'])
                peer.settimeout(0.03)
                try:reply=peer.recv(1)
                except socket.timeout:pass
                else:raise AssertionError(f'unexpected p reply: {reply!r}')
                send(resync+b'G#p r,');peer.shutdown(socket.SHUT_RDWR)
                client.wait_text('Disconnected: Enter exits, F1 details');client.key('enter')
            client.wait_text('Directory: Name');client.key('enter')
            with listener.accept()[0] as peer:
                client.wait_for(lambda f:f.application.get('connected')==1);client.resize(1920,1440)
                peer.sendall(b'\x1bf\x1bE\x1bY2,Z\x1bHFRESH');frame=client.wait_text('FRESH')
                check_blank(frame,resolution,status,'FRESH',fresh=True)
                assert (frame.application['cterm.igs_console_foreground'],frame.application['cterm.igs_console_background'])==(15,0)
                assert not frame.application['cterm.igs_console_reverse'] and not frame.application['cterm.igs_console_escape']
                assert frame.application['cterm.igs_random_seed']==1
                peer.shutdown(socket.SHUT_RDWR);client.wait_text('Disconnected: Enter exits, F1 details');client.key('enter')
            client.wait_text('Directory: Name');assert b'[wren' not in client.stderr,bytes(client.stderr)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('executable');parser.add_argument('--reports',default='reports/test-control');args=parser.parse_args()
    for resolution in range(3):
        for encoding in ('ATARI-ST','CP437','UTF-8'):
            for status in (False,True):run(args.executable,args.reports,resolution,encoding,status)
