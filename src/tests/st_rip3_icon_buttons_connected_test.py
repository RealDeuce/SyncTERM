#!/usr/bin/env python3
"""Independent BMP fixtures and ICON/HOT button geometry through TCP."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path
import struct
from st_migrated_control_test import connection
from st_terminal_conformance_test import Terminal
from st_rip3_buttons_connected_test import button,style,remove_fields
from st_rip3_button_styles_connected_test import number
from st_rip3_connected_test import query,pixel


def bitmap(width,height,rgb):
    stride=(width+3)&~3
    palette=bytes((rgb&255,(rgb>>8)&255,(rgb>>16)&255,0))+bytes(255*4)
    offset=14+40+len(palette)
    return (struct.pack('<2sIHHI',b'BM',offset+stride*height,0,0,offset)
            +struct.pack('<IiiHHIIiiII',40,width,height,1,8,0,stride*height,0,0,256,0)
            +palette+bytes(stride*height))


def run(args,width,height):
    with connection(args.executable,args.reports,encoding=args.encoding,
                    options='ScreenMode=Custom\nRIP=RIPv3\n',
                    settings=f'CustomColumns={width//8}\nCustomRows={height//8}\nCustomFontHeight=8\n'
                             'CustomAspectWidth=4\nCustomAspectHeight=3\n') as (client,root,peer,send):
        t=Terminal(client,root,peer,send)
        def values(program,expected=b''):
            return t.exchange(query(program,at_cursor=True),expected)
        t.exchange(b'\x1b[!',b'RIPSCRIP030001')
        t.exchange(b'\x01|*\r\n\x1b[?25l')
        values(f'$WORLD(CUR,{width},{height})$'.encode())
        # Export only locates the actual trusted per-connection asset directory.
        # Every image under test is independently encoded above.
        frame=t.exchange(b'\x01|2P6000001011|2W6000000000000Seed.bmp\r\n')
        paths=list(root.rglob('Seed.bmp'));assert len(paths)==1
        assets=paths[0].parent
        for name,w,h,rgb in [('Face.BMP',60,30,0x00aa00),('Face.BMH',100,50,0xaa0000),
                             ('Fallback.BMP',20,10,0x0000aa),('BadHot.BMP',20,10,0x0000aa),
                             ('OnlyHot.BMH',100,50,0xaa0000),('Tiny.BMP',1,1,0x00aa00)]:
            (assets/name).write_bytes(bitmap(w,h,rgb))
        (assets/'BadHot.BMH').write_bytes(b'not a bitmap')
        m=frame.metadata
        def mouse(x,y,left=False,move=False):
            px=m['destination_x']+(2*x+1)*m['destination_width']//(2*m['plane_width'])
            py=m['destination_y']+(2*y+1)*m['destination_height']//(2*m['plane_height'])
            before=client.capture().metadata['events_dequeued']
            client.mouse(px,py,action=0 if move else 1,buttons=int(left))
            client.wait_for(lambda s:s.metadata['events_dequeued']>before)
        def click(x,y,expected=b''):
            mouse(x,y,True);mouse(x,y)
            return t.input(expected) if expected else t.exchange()
        def world(scale):
            values(f'$WORLD(CUR,{width*scale},{height*scale})$'.encode())
            # WORLD may exceed the two-digit range; the macro changes it without
            # changing coordinate width/base, keeping the fixture prefixes literal.
        world(2)
        t.case(f'rip3-icon-natural-palette-copy-{width}x{height}')
        before=t.exchange(style(flags=128|1024))
        frame=t.exchange(button((200,100,0,0),b'<><>')
                         +button((200,100,0,0),b'bad/name<><>')
                         +button((200,100,0,0),b'Tiny<><>'))
        assert frame.primary==before.primary and frame.alternate==before.alternate
        frame=t.exchange(b'\x01|W3\r\n'+style(flags=128|1024,width=50,height=40,flags2=128)
                         +button((200,100,1,1),b'ID=1:fAcE<><>normal'))
        assert pixel(frame,100,50)==0x00aa00 and pixel(frame,129,64)==0x00aa00
        assert pixel(frame,130,50)==0 and pixel(frame,100,65)==0
        click(129,64,b'normal');click(130,64)

        t.case(f'rip3-hot-captured-world-viewport-{width}x{height}')
        frame=t.exchange(style(flags=128|4096|1024,width=50,height=40,flags2=128)
                         +button((400,100,0,0),b'ID=2:Face<><>hot',hotkey=ord('H')))
        assert pixel(frame,200,50)==0x00aa00 and pixel(frame,229,64)==0x00aa00
        mouse(210,55,True);frame=t.exchange()
        assert pixel(frame,200,50)==0xaa0000 and pixel(frame,229,64)==0xaa0000
        assert pixel(frame,230,50)==0 and pixel(frame,200,65)==0
        world(1)
        t.exchange(b'\x01|vB45KDW8C|v00000000|2s600\r\n')
        mouse(250,80,True,True);frame=t.exchange()
        assert pixel(frame,200,50)==0x00aa00
        mouse(210,55,True,True);frame=t.exchange()
        assert pixel(frame,200,50)==0xaa0000
        mouse(210,55);frame=t.input(b'hot')
        assert pixel(frame,229,64)==0x00aa00
        values(b'$CUR(PORT)$',b'6')
        client.text('h');frame=t.input(b'hot')
        assert pixel(frame,200,50)==0x00aa00
        values(b'$CUR(PORT,0)$');world(1)
        far=number(width-1,2)+number(height-1,2)
        t.exchange(b'\x01|v0000'+far+b'|W0\r\n')
        # A new instance under WORLD=DEVICE is 60x30; the first stays 30x15.
        frame=t.exchange(button((300,50,1,1),b'Face<><>new-world'))
        assert pixel(frame,359,79)==0x00aa00 and pixel(frame,360,79)==0
        click(359,79,b'new-world')
        world(2)

        t.case(f'rip3-hot-initial-size-and-label-{width}x{height}')
        frame=t.exchange(style(flags=128|4096|1024,foreground=15)
                         +button((200,200,0,0),b'ID=3:Face<>X<>selected',flags=1))
        assert pixel(frame,100,100)==0xaa0000 and pixel(frame,149,124)==0xaa0000
        white={(x,y) for y in range(100,125) for x in range(100,150) if pixel(frame,x,y)==0xffffff}
        assert white
        # Changing the current style/font cannot change the captured label.
        t.exchange(style(flags=256,foreground=1)+b'\x01|Y01000100\r\n')
        frame=click(149,124,b'selected')
        assert pixel(frame,149,124)==0x00aa00 and pixel(frame,150,124)==0
        assert white=={(x,y) for y in range(100,125) for x in range(100,150) if pixel(frame,x,y)==0xffffff}
        click(150,124)

        t.case(f'rip3-hot-fallback-is-field-local-{width}x{height}')
        t.exchange(style(flags=128|4096|1024)+button((400,200,0,0),b'ID=4:Fallback<><>fallback'))
        mouse(205,102,True);frame=t.exchange()
        assert pixel(frame,200,100)==0xffff55 and pixel(frame,209,104)==0xffff55
        mouse(205,102);frame=t.input(b'fallback')
        assert pixel(frame,200,100)==0x0000aa
        # Once fallback disabled HOT in that field, adding a hot file does not
        # change it; a new field still gets HOT from the unchanged style.
        (assets/'Fallback.BMH').write_bytes(bitmap(40,20,0xaa0000))
        mouse(205,102,True);frame=t.exchange();assert pixel(frame,200,100)==0xffff55
        mouse(205,102);t.input(b'fallback')
        frame=t.exchange(button((440,200,0,0),b'Fallback<><>fresh'))
        mouse(225,102,True);frame=t.exchange();assert pixel(frame,220,100)==0xaa0000
        mouse(225,102);t.input(b'fresh')
        t.exchange(button((480,200,0,0),b'BadHot<><>bad-hot'))
        mouse(245,102,True);frame=t.exchange();assert pixel(frame,240,100)==0xffff55
        mouse(245,102);t.input(b'bad-hot')
        frame=t.exchange(style(flags=128|2|1024)
                         +button((560,200,0,0),b'Fallback<><>initial-inverse',flags=1))
        assert pixel(frame,280,100)==0xffff55 and pixel(frame,289,104)==0xffff55
        frame=click(285,102,b'initial-inverse')
        assert pixel(frame,280,100)==0x0000aa

        t.case(f'rip3-icon-auto-stamp-before-label-{width}x{height}')
        frame=t.exchange(style(flags=128|4096|64|512|1024,foreground=12,bevel=6)
                         +button((200,400,0,0),b'Face<>X<>auto-icon',flags=1)
                         +b'\x01|1W0AutoIcon.bmp\r\n')
        paths=list(root.rglob('AutoIcon.bmp'));assert len(paths)==1
        data=paths[0].read_bytes();offset=int.from_bytes(data[10:14],'little')
        assert struct.unpack_from('<ii',data,18)==(57,32)
        colors={value for y in range(32) for value in data[offset+y*60:offset+y*60+57]}
        # Hot image=4, dark=8, bright=15, and this fixture's explicit corner=7;
        # the later label foreground=12 must not enter the captured bitmap.
        assert colors <= {4,7,8,15} and 12 not in colors, colors
        frame=t.exchange(button((400,400,0,0),b'<><>captured-icon'))
        assert pixel(frame,200,200)==0xffffff and pixel(frame,256,231)==0x555555
        click(256,231,b'captured-icon')

        t.case(f'rip3-hot-redraw-excludes-initial-recess-{width}x{height}')
        frame=t.exchange(style(flags=128|4096|512|16|1024,bevel=6)
                         +button((600,500,0,0),b'Face<><>recess'))
        frame=t.exchange(b'\x01|c02|XGEE6\r\n')
        # WORLD (590,510) -> DEVICE (295,255), on the outer recess only.
        assert pixel(frame,295,255)==0x00aa00
        mouse(310,255,True);frame=t.exchange()
        assert pixel(frame,310,255)==0xaa0000 and pixel(frame,295,255)==0x00aa00
        mouse(310,255);frame=t.input(b'recess')
        assert pixel(frame,310,255)==0x00aa00 and pixel(frame,295,255)==0x00aa00

        t.case(f'rip3-hot-delete-and-asset-failure-{width}x{height}')
        t.exchange(style(flags=128|4096|1024)+button((400,300,0,0),b'ID=5:Face<><>deleted'))
        mouse(210,155,True);frame=t.exchange();assert pixel(frame,200,150)==0xaa0000
        frame=t.exchange(remove_fields((400,300,460,330),1|32))
        assert pixel(frame,200,150)==0 and pixel(frame,229,164)==0
        mouse(210,155);t.exchange()
        # A selected hot image establishes geometry without loading an unused
        # missing normal image. Its later normal-load failure is nonfatal.
        frame=t.exchange(button((600,300,0,0),b'OnlyHot<><>still-alive',flags=1))
        assert pixel(frame,300,150)==0xaa0000 and pixel(frame,349,174)==0xaa0000
        click(349,174,b'still-alive')
        frame=values(b'alive',b'alive')
        assert frame.application['connected']==1


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path)
    parser.add_argument('encoding',choices=('CP437','UTF-8'))
    parser.add_argument('--reports',type=Path,required=True)
    args=parser.parse_args()
    for width,height in ((640,480),(800,600),(1024,768),(720,400)):
        run(args,width,height)
