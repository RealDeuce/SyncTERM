#!/usr/bin/env python3
"""Independent PR134 selection/publication vectors through actual TCP sessions."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path
from st_migrated_control_test import connection
from st_terminal_conformance_test import Terminal
from st_rip3_connected_test import query,pixel
from st_rip3_buttons_connected_test import style,button,remove_fields
from syncterm_fixture import receive


def run(args,width,height):
    with connection(args.executable,args.reports,encoding=args.encoding,
                    options='ScreenMode=Custom\nRIP=RIPv3\n',
                    settings=f'CustomColumns={width//8}\nCustomRows={height//8}\nCustomFontHeight=8\n') as (client,root,peer,send):
        t=Terminal(client,root,peer,send)
        def values(source,expected=b''):
            return t.exchange(query(source,at_cursor=True),expected)
        t.exchange(b'\x1b[!',b'RIPSCRIP030001')
        t.exchange(b'\x01|*|Y00000100\r\n\x1b[?25l')
        frame=values(f'$WORLD(CUR,{width},{height})$'.encode());m=frame.metadata
        def mouse(x,y,left=False):
            px=m['destination_x']+(2*x+1)*m['destination_width']//(2*m['plane_width'])
            py=m['destination_y']+(2*y+1)*m['destination_height']//(2*m['plane_height'])
            count=client.capture().metadata['events_dequeued']
            client.mouse(px,py,action=1,buttons=int(left))
            client.wait_for(lambda s:s.metadata['events_dequeued']>count)
        def click(x,y,expected=b''):
            mouse(x,y,True);mouse(x,y)
            return t.input(expected) if expected else t.exchange()
        if width == 640:
            t.case('rip3-group-empty-expanded-label-does-not-require-a-font')
            t.exchange(style(flags=256|1024|2,flags2=1,group=8,surface=3))
            # At this scale the outline font's device DPI truncates to zero,
            # while the transmitted corners still form a visible 80x60 face.
            values(b'$COORDSIZE(CUR,3)$$WORLD(CUR,65535,65535)$')
            definition = button((4096,4096,12288,12288),
                                b'<>$-=LABEL_STAGE=ran$$NULL$<>empty-label',xy=3)
            frame = t.exchange(definition + query(b'$&LABEL_STAGE$:$INUSE(MOUSE,ANY)$', at_cursor=True), b'ran:1')
            assert pixel(frame,50,40) == 0x00aaaa
            frame = click(50,40,b'empty-label')
            assert pixel(frame,50,40) == 0xff5555
            values(f'$MKILL$$COORDSIZE(CUR,2)$$WORLD(CUR,{width},{height})$'.encode())

            t.case('rip3-patterned-buttons-resolve-only-colors-used-by-their-brush')
            # The unused color came from RGB mode and is not a valid palette
            # index. Both buttons still have a complete one-color brush.
            patterned = style(flags=256|1024,flags2=64|1,group=8,surface=3)
            bg = b'\x01|k01|M18|S00&&&&|M08|W4\r\n'
            fg = b'\x01|M18|k&&&&|M08|S0102\r\n'
            frame = t.exchange(patterned + bg + button((300,100,340,130), b'<><>pattern-background')
                               + fg + button((350,100,390,130), b'<><>pattern-foreground'))
            assert pixel(frame,310,110) == 0x0000aa and pixel(frame,360,110) == 0x00aa00
            click(310,110,b'pattern-background')
            click(360,110,b'pattern-foreground')

            before = t.exchange()
            frame = t.exchange(bg + style(flags=256|1024,flags2=32|64|1,group=8)
                               + button((300,100,340,130), b'<><>no-fill'))
            assert frame.primary == before.primary and frame.alternate == before.alternate
            click(310,110,b'no-fill')
            values(b'$MKILL$')
            t.exchange(b'\x01|W0|k00|S0107\r\n')
        def checkboxes():
            return (style(flags=256|1024|2,flags2=1,group=2,surface=3,bevel=0)
                    +button((10,10,60,30),b'ID=1:<>C1<>[2:]A[3:]X[]one',hotkey=ord('a'))
                    +button((10,40,60,60),b'ID=2:<>C2<>[2:]B[2:]C[3:]Y[]two',hotkey=ord('b'),flags=1))
        t.case('rip3-ungrouped-initial-selection-is-ignored')
        frame=t.exchange(style(flags=256|1024|2,surface=3,bevel=0)
                         +button((10,10,60,30),b'<><>ordinary',flags=1))
        assert pixel(frame,58,28)==0x00aaaa
        mouse(30,20,True);frame=t.exchange();assert pixel(frame,58,28)==0xff5555
        mouse(30,20);frame=t.input(b'ordinary');assert pixel(frame,58,28)==0x00aaaa
        values(b'$MKILL$')
        t.case(f'rip3-groups-checkbox-selection-and-owned-slots-{width}x{height}')
        frame=t.exchange(checkboxes())
        assert pixel(frame,58,28)==0x00aaaa and pixel(frame,58,58)==0xff5555
        values(b'$?2$:$?3$',b':')  # Initial selection never parses host source.
        click(30,20,b'one');values(b'$?2$:$?3$',b'A:X')
        frame=click(30,50,b'two');values(b'$?2$:$?3$',b'A:X')
        assert pixel(frame,58,58)==0x00aaaa
        frame=click(30,50,b'two');values(b'$?2$:$?3$',b'AC:XY')
        assert pixel(frame,58,58)==0xff5555
        click(30,20,b'one');values(b'$?2$:$?3$',b'C:Y')
        values(b'[2:]Z')
        t.exchange(remove_fields((10,40,60,60),1));values(b'$?2$:$?3$',b'Z:')
        click(30,20,b'one');values(b'$?2$:$?3$',b'A:X')
        t.exchange(remove_fields((10,10,60,30),1));values(b'$?2$:$?3$',b'Z:')
        t.case('rip3-groups-soft-preserves-effective-values-without-owners')
        t.exchange(checkboxes())
        click(30,20,b'one');click(30,50,b'two');click(30,50,b'two')
        values(b'$?2$:$?3$',b'AC:XY')
        t.exchange(b'\x01|*\r\n');values(b'$?2$:$?3$',b'AC:XY')
        values(f'$WORLD(CUR,{width},{height})$'.encode())
        t.case('rip3-groups-radio-exclusivity-required-selection-and-preflight')
        frame=t.exchange(style(flags=256|1024|2|16384,group=4,surface=3,bevel=0)
                         +button((100,10,150,30),b'ID=7:<>R1<>[4:]R1[]r1',hotkey=ord('c'),flags=1)
                         +button((100,40,150,60),b'ID=7:<>R2<>[4:]R2[]r2',hotkey=ord('d')))
        values(b'$?4$')  # Selected, but its associated map is still empty.
        assert pixel(frame,148,28)==0xff5555 and pixel(frame,148,58)==0x00aaaa
        # A newly admitted selected radio changes peers' committed bits, but
        # the older held member still previews its proposed selected artwork.
        mouse(120,20,True);t.exchange()
        frame=t.exchange(button((170,10,220,30),b'ID=7:<>R3<>[4:]R3[]r3',flags=1))
        assert pixel(frame,148,28)==0xff5555 and pixel(frame,218,28)==0xff5555
        mouse(240,80);frame=t.exchange()
        assert pixel(frame,148,28)==0x00aaaa and pixel(frame,218,28)==0xff5555
        t.exchange(remove_fields((170,10,220,30),1))
        click(120,20,b'r1')
        mouse(120,50,True);mouse(200,80);frame=t.exchange()
        assert pixel(frame,148,28)==0xff5555 and pixel(frame,148,58)==0x00aaaa
        frame=click(120,50,b'r2');values(b'$?4$',b'R2')
        assert pixel(frame,148,28)==0x00aaaa and pixel(frame,148,58)==0xff5555
        click(120,50,b'r2');client.text('d');t.input(b'r2')
        values(b'$HKEYOFF$');client.text('d');t.input(b'd')
        values(b'$?4$',b'R2');values(b'$HKEYON$')
        before=t.exchange()
        frame=t.exchange(style(flags=256|1024|2,flags2=1,group=4,surface=12)
                         +button((200,100,250,130),b'ID=9:<>$COORDSIZE(CUR,5)$<>never',flags=1))
        assert frame.primary==before.primary and frame.alternate==before.alternate
        frame=t.exchange(style(flags=256|1024|2|16384,flags2=1,group=4,surface=12)
                         +button((200,100,250,130),b'<>$COORDSIZE(CUR,5)$<>both-kinds'))
        assert frame.primary==before.primary and frame.alternate==before.alternate
        values(b'$COORDSIZE$:$?4$',b'2:R2')
        values(b'[4:]Z')
        t.exchange(remove_fields((100,40,150,60),1))
        values(b'$?4$after')  # Live radio group without a selected member rejects.
        click(120,20,b'r1');values(b'$?4$',b'R1')
        t.exchange(remove_fields((100,10,150,30),1));values(b'$?4$',b'Z')
        t.case('rip3-groups-post-angle-bodies-and-stale-owner-after-prompt')
        t.exchange(style(flags=256|1024|2|16384,group=4,surface=3,bevel=0)
                   +button((100,10,150,30),b'ID=7:<>old<>[]$MKILL$before:$FIELDID$:$=WAIT,8@Group$;[4:]stale[]after:$FIELDID$',hotkey=ord('e')))
        mouse(120,20,True);mouse(120,20);client.wait_text('Group')
        send(b'\x1b[5n');client.text('kept');client.key('enter')
        receive(peer,b'before:7:kept;after:7\x1b[0n',root/'replies.bin')
        values(b'$?4$',b'Z')  # Deleted association never becomes an ordinary write.
        t.case('rip3-groups-angle-once-ordinary-body-late-without-rebuild-effects')
        values(b'$-=PREFIX=pre$$-=BODY=old$$-=EFFECT=cold$')
        t.exchange(style(flags=256|1024,flags2=1,group=2,surface=3,bevel=0)
                   +button((10,10,60,30),b'<>raw<>[2:]<<PREFIX>>$&BODY$$-=EFFECT=ran$[]defined'))
        click(30,20,b'defined')
        values(b'$&EFFECT$',b'cold')  # Definition/composition never runs its body.
        values(b'$-=PREFIX=changed$$-=BODY=new$')
        values(b'$?2$',b'prenew');values(b'$&EFFECT$',b'ran')
        click(30,20,b'defined')  # OFF still evaluates its own angle/host source.
        values(b'$-=EFFECT=cold$');values(b'$?2$')
        values(b'$&EFFECT$',b'cold')
        values(b'$MKILL$')
        t.case('rip3-groups-creation-order-assembly-bound')
        t.exchange(style(flags=256|1024,flags2=1,group=2,surface=3,bevel=0)
                   +button((10,10,60,30),b'<>A<>[2:]'+b'A'*3000)
                   +button((10,40,60,60),b'<>B<>[2:]'+b'B'*2000))
        click(30,20);click(30,50)
        values(b'$?2$',b'A'*3000+b'B'*1096)
        values(b'alive',b'alive')
        assert client.capture().application['connected']==1


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path)
    parser.add_argument('encoding',choices=('CP437','UTF-8'))
    parser.add_argument('--reports',type=Path,required=True)
    args=parser.parse_args()
    for width,height in ((640,480),(720,400)):
        run(args,width,height)
