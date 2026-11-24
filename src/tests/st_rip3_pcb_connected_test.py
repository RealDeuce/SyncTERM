#!/usr/bin/env python3
"""Connected PCB producers, DEVICE reversal, overlap and PORT backup ownership.

Expected rectangles, pixel indices and state follow the portable contract;
no RIPtel raster or emulator-generated expected image is used.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path
from st_migrated_control_test import connection
from st_terminal_conformance_test import Terminal
from st_rip3_connected_test import query,pixel
from st_rip3_button_styles_connected_test import number
from st_rip3_buttons_connected_test import style,button
from st_rip3_scroll_connected_test import plane_pixel


def run(args,width,height):
    with connection(args.executable,args.reports,encoding=args.encoding,
                    options='ScreenMode=Custom\nRIP=RIPv3\n',
                    settings=f'CustomColumns={width//8}\nCustomRows={height//8}\nCustomFontHeight=8\n') as (client,root,peer,send):
        t=Terminal(client,root,peer,send)
        def values(source,expected=b''):
            return t.exchange(query(source,at_cursor=True),expected)
        def wire(source):
            return t.exchange(b'\x01|'+source+b'\r\n')
        def coords(area):
            return b''.join(number(v,2) for v in area)
        def define(n,area,flags=1):
            return wire(b'2P'+number(n,1)+coords(area)+number(flags,1))
        def copy(source,area,destination,target,mode=0):
            return wire(b'2C'+number(source,1)+coords(area)+number(destination,1)+coords(target)+number(mode,1))
        def bar(color,area):
            return wire(b'W0|S01'+number(color,2)+b'|B'+coords(area))
        def same(before,after):
            assert before.primary==after.primary and before.alternate==after.alternate
        def region(frame,area,color):
            x0,y0,x1,y1=area
            assert all(pixel(frame,x,y)==color for y in range(y0,y1) for x in range(x0,x1))
        def selections(frame):
            assert (frame.metadata['plane_width'],frame.metadata['plane_height'])==(width,height)
            assert (frame.application['cterm.x'],frame.application['cterm.y'],frame.application['cterm.attr'])==(4,2,0x16)
            values(b'$CUR(ENV)$:$CUR(PAL)$:$CUR(STYLE)$:$CUR(BUT)$:$CUR(TW)$:$CUR(PORT)$:$TEXTXY$',b'5:17:7:6:3:0:50:51')
        t.exchange(b'\x1b[!',b'RIPSCRIP030001')
        wire(b'*')
        values(b'$CUR(ENV,5)$'+f'$WORLD(CUR,{width},{height})$'.encode()+b'$CUR(PAL,17)$$CUR(STYLE,7)$$CUR(BUT,6)$$CUR(TW,3)$')
        accented=b'\x82' if args.encoding=='CP437' else 'é'.encode()
        t.exchange(b'\x1b[?25l\x1b[33;44m\x1b[2;3H'+accented)
        wire(b'm1E1F')

        t.case(f'rip3-pcb-explicit-source-copy-mode-and-device-state-{width}x{height}')
        define(5,(0,0,4,4));define(10,(0,0,4,4))
        bar(4,(100,100,103,103));copy(0,(100,100,104,104),5,(0,0,4,4))
        bar(2,(120,100,123,103));copy(0,(120,100,124,104),10,(0,0,4,4))
        values(b'$PROT(PORT,5,10)$')
        bar(0,(100,100,123,103));wire(b'W4')
        # PCB has DEVICE geometry and decimal selectors, independent of zero
        # WORLD, base64 and the current graphics destination-NOT write mode.
        values(b'$WORLD(CUR,0,0)$$BASEMATH(CUR,64)$')
        before=client.capture();frame=values(b'$PCB(CUR)$$PCB$');same(before,frame)
        frame=values(b'$PCB(0005)$');region(frame,(100,100,104,104),0xaa0000)
        frame=values(b'$PCB(10)$');region(frame,(120,100,124,104),0x00aa00)
        selections(frame)
        values(b'$WORLD$:$BASEMATH$',b'0:0:64')
        values(b'$BASEMATH(CUR,36)$'+f'$WORLD(CUR,{width},{height})$'.encode())

        t.case('rip3-pcb-no-pair-inactive-and-invalid-reference-recovery')
        define(7,(0,0,4,4))
        before=client.capture();frame=values(b'$PCB(7)$$PCB(8)$');same(before,frame)
        values(b'$INUSE(PORT,8)$',b'0')
        bar(0,(100,100,103,103))
        values(b'$UNPROT(PORT,5)$$DVP(5)$$PROT(PORT,5)$')
        before=client.capture();frame=values(b'$PCB(5)$');same(before,frame)
        values(b'$UNPROT(PORT,5)$$AVP(5)$$PROT(PORT,5)$')
        for source in (b'$PCB( )$',b'$PCB(-1)$',b'$PCB(+1)$',b'$PCB(0x5)$',b'$PCB(ALL)$',b'$PCB(36)$',b'$PCB(5,10)$'):
            before=client.capture();frame=values(source);same(before,frame)
        frame=values(b'$PCB(, 0005 ,,)$');region(frame,(100,100,104,104),0xaa0000)
        selections(frame)

        t.case('rip3-pcb-moved-viewport-reanchors-device-offsets')
        wire(b'v'+coords((10,20,width,height)))
        frame=values(b'$PCB(5)$');region(frame,(110,120,114,124),0xaa0000)
        assert pixel(frame,109,120)==0
        wire(b'v'+coords((0,0,width,height)))
        selections(client.capture())

        t.case('rip3-pcb-repeated-same-screen-overlap')
        copy(0,(200,200,203,201),0,(201,200,204,201),1)
        for x,color in enumerate((1,2,3,4),200):
            wire(b'c'+number(color,2)+b'|X'+number(x,2)+number(200,2))
        frame=values(b'$PCB( cUr )$')
        assert [pixel(frame,x,200) for x in range(200,204)]==[0x00aa00,0x00aaaa,0xaa0000,0xaa0000]
        frame=values(b'$PCB(CUR)$')
        assert [pixel(frame,x,200) for x in range(200,204)]==[0x00aa00,0x00aa00,0x00aaaa,0xaa0000]
        selections(frame)

        t.case('rip3-pcb-get-image-default-pointer-and-put-image-producer')
        bar(4,(300,200,302,202));wire(b'1C'+coords((300,200,302,202)))
        values(b'$INUSE(PORT,1)$',b'1')
        bar(0,(300,200,302,202))
        frame=values(b'$PCB(CUR)$');region(frame,(300,200,303,203),0)
        frame=values(b'$PCB()$');region(frame,(300,200,303,203),0xaa0000)
        assert pixel(frame,303,200)==0 and pixel(frame,300,203)==0
        wire(b'1P'+number(350,2)+number(200,2)+b'0')
        bar(2,(350,200,353,203))
        frame=values(b'$PCB(CUR)$');region(frame,(0,0,4,4),0x00aa00)
        selections(frame)

        t.case('rip3-pcb-port-backup-owns-pair-and-pointer')
        values(b'$PROT(PORT,1)$$SCB$')
        values(b'$UNPROT(PORT,1)$')
        bar(1,(400,200,402,202));wire(b'1C'+coords((400,200,402,202)))
        values(b'$RCB$$ISPROT(PORT,1)$',b'1')
        bar(0,(300,200,302,202))
        frame=values(b'$PCB$');region(frame,(300,200,303,203),0xaa0000)
        region(frame,(400,200,403,203),0x0000aa)
        selections(frame)
        t.case('rip3-pcb-initial-button-auto-stamp-producer')
        values(b'$UNPROT(PORT,1)$')
        frame=t.exchange(style(flags=256|64,surface=3,bevel=0)+button((80,250,110,270)))
        region(frame,(80,250,110,270),0x00aaaa)
        bar(0,(80,250,109,269))
        frame=values(b'$PCB$');region(frame,(80,250,110,270),0x00aaaa)
        assert pixel(frame,110,250)==0 and pixel(frame,80,270)==0
        selections(frame)
        t.case('rip3-pcb-captures-current-ordinary-ansi-pixels')
        frame=t.exchange(b'\x1b[1;1H\x1b[48;2;17;34;51m ')
        assert pixel(frame,0,0)==0x112233
        copy(0,(0,0,1,1),5,(0,0,1,1))
        bar(0,(0,0,0,0))
        frame=values(b'$PCB(5)$');assert pixel(frame,0,0)==0x112233

        t.case('rip3-put-image-inverts-all-rgb-channels')
        wire(b'1C'+coords((0,0,0,0)))
        frame=wire(b'1P'+coords((400,280))+b'040')
        region(frame,(400,280,402,282),0xeeddcc)
        assert pixel(frame,402,280)==0 and pixel(frame,400,282)==0
        frame=wire(b'1P'+coords((430,280))+b'001')
        region(frame,(430,280,432,282),0x112233)
        values(b'$CUR(PORT)$',b'0')

        t.case('rip3-put-image-composes-with-the-live-rgb-destination')
        frame=t.exchange(b'\x1b[1;61H\x1b[48;2;160;176;192m   ')
        cell_width=frame.metadata['pixel_cursor_width']
        end=frame.metadata['pixel_cursor_x']
        start=end-3*cell_width
        region(frame,(start,0,end,4),0xa0b0c0)
        modes=((b'010',0xb192f3),(b'020',0xb1b2f3),(b'030',0x002000))
        frame=wire(b'|'.join(b'1P'+coords((start+i*cell_width,0))+mode for i,(mode,_) in enumerate(modes)))
        for i,(_,expected) in enumerate(modes):
            x=start+i*cell_width
            region(frame,(x,0,x+2,2),expected)
            region(frame,(x+2,0,x+cell_width,4),0xa0b0c0)

        t.case('rip3-put-image-composes-each-native-blink-plane')
        glyph=b'\xdb' if args.encoding=='CP437' else '█'.encode()
        before=t.exchange(b'\x1b[1;65H\x1b[5;38;2;160;176;192;48;2;204;136;68m'+glyph)
        x=before.metadata['pixel_cursor_x']-cell_width
        phases=('primary','alternate')
        original=tuple(plane_pixel(before,x+1,3,phase) for phase in phases)
        expected={0xa0b0c0:0xb192f3,0xcc8844:0xddaa77}
        assert set(original)==expected.keys(),original
        frame=wire(b'1P'+coords((x,2))+b'010')
        assert tuple(plane_pixel(frame,x+1,3,phase) for phase in phases)==tuple(expected[color] for color in original)
        assert tuple(plane_pixel(frame,x+2,3,phase) for phase in phases)==original

        t.case('rip3-port-copy-scales-rgb-before-composing-with-live-ansi')
        define(9,(0,0,4,1))
        copy(0,(0,0,1,1),9,(0,0,2,1))
        copy(0,(start+3,0,start+4,1),9,(2,0,4,1))
        x=start+3
        expected_colors=((0xb192f3,0),(0xb1b2f3,0xa0b0c0),(0x002000,0xa0b0c0),(0xeeddcc,0x5f4f3f))
        for y,expected in enumerate(expected_colors):
            frame=copy(9,(0,0,4,1),0,(x,y,x+2,y+1),y+1)
            actual=(pixel(frame,x,y),pixel(frame,x+1,y))
            assert actual==expected,(y,actual)
            assert pixel(frame,x+2,y)==0xa0b0c0

        t.case('rip3-port-copy-uses-original-rgb-pixels-on-screen-overlap')
        frame=t.exchange(b'\x1b[1;61H\x1b[48;2;17;34;51m \x1b[48;2;160;176;192m ')
        x=start+cell_width-1
        assert [pixel(frame,i,0) for i in range(x,x+3)]==[0x112233,0xa0b0c0,0xa0b0c0]
        frame=copy(0,(x,0,x+2,1),0,(x+1,0,x+3,1),1)
        assert [pixel(frame,i,0) for i in range(x,x+4)]==[0x112233,0xb192f3,0,0xa0b0c0]

        t.case('rip3-put-image-reads-inactive-clipboard-viewport')
        wire(b'1C010103030|2s100')
        # Put the known samples in the captured tile independently of capture scaling.
        bar(7,(0,0,3,3))
        bar(4,(1,1,1,1));bar(2,(2,1,2,1));bar(1,(1,2,1,2));bar(3,(2,2,2,2))
        wire(b'v01010202|v00000000|2s000')
        frame=wire(b'1P0A0K00Z')
        colors=[0xaa0000,0x00aa00,0x0000aa,0x00aaaa]
        samples=[pixel(frame,x,y) for y in (20,21) for x in (10,11)]
        assert samples==colors,samples
        assert all(pixel(frame,x,y)==0 for x,y in ((9,20),(12,20),(10,19),(10,22)))
        same(frame,values(b'$PCB(1)$'))
        wire(b'v00000000')
        values(b'$WORLD(CUR,0,0)$')
        same(frame,wire(b'1P0U0K000'))
        values(b'$AVP(0)$')
        frame=values(b'$PCB(CUR)$')
        assert [pixel(frame,x,y) for y in (0,1) for x in (0,1)]==colors
        values(b'$CUR(PORT)$',b'0')
        values(b'after-pcb',b'after-pcb')
        assert client.capture().application['connected']==1


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path)
    parser.add_argument('encoding',choices=('CP437','UTF-8'))
    parser.add_argument('--reports',type=Path,required=True)
    args=parser.parse_args()
    for width,height in ((640,480),(800,600),(1024,768),(720,400)):
        run(args,width,height)
