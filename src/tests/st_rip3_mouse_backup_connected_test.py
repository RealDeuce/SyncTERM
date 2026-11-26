#!/usr/bin/env python3
"""Mouse definition backups over TCP; independent bytes, pixels and input boundaries."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path
from st_migrated_control_test import connection
from st_terminal_conformance_test import Terminal
from st_rip3_connected_test import query,pixel
from st_rip3_mouse_connected_test import field
from st_rip3_hover_connected_test import resident
from st_rip3_buttons_connected_test import style,button
from st_rip3_icon_buttons_connected_test import bitmap
from syncterm_fixture import receive


def run(args,width,height):
    with connection(args.executable,args.reports,encoding=args.encoding,
                    options='ScreenMode=Custom\nRIP=RIPv3\n',
                    settings=f'CustomColumns={width//8}\nCustomRows={height//8}\nCustomFontHeight=8\n') as (client,root,peer,send):
        t=Terminal(client,root,peer,send)
        def values(source,expected=b''):
            return t.exchange(query(source,at_cursor=True),expected)
        t.exchange(b'\x1b[!',b'RIPSCRIP030001')
        t.exchange(b'\x01|*\r\n\x1b[?25l')
        frame=values(f'$WORLD(CUR,{width},{height})$'.encode());m=frame.metadata
        def mouse(x,y,left=False,move=False):
            px=m['destination_x']+(2*x+1)*m['destination_width']//(2*m['plane_width'])
            py=m['destination_y']+(2*y+1)*m['destination_height']//(2*m['plane_height'])
            count=client.capture().metadata['events_dequeued']
            client.mouse(px,py,action=0 if move else 1,buttons=int(left))
            client.wait_for(lambda s:s.metadata['events_dequeued']>count)
        def movement(x,y,expected=b''):
            mouse(x,y,move=True);return t.exchange(expected=expected)
        def click(x,y,expected=b''):
            mouse(x,y,True);mouse(x,y)
            return t.input(expected) if expected else t.exchange()
        def same(a,b):
            assert a.primary==b.primary and a.alternate==b.alternate
        t.case(f'rip3-mouse-backup-held-save-and-applying-restore-{width}x{height}')
        t.exchange(resident(5,b'E$FIELDID$;')+resident(6,b'X$FIELDID$;')
                   +field((10,10,60,60),b'ID=7:A$FIELDID$',invert=True)
                   +field((20,20,80,80),b'ID=8:B$FIELDID$',invert=True))
        movement(30,30,b'E8;E7;');before=t.exchange()
        mouse(30,30,True);pressed=t.exchange()
        assert pressed.primary!=before.primary
        frame=values(b'$SMF$$SMF(3)$');same(frame,pressed)
        values(b'$INUSE(MOUSE,BASE)$$INUSE(MOUSE,S3)$',b'11')
        t.exchange(resident(5,b'N$FIELDID$;')+resident(6,b'Y$FIELDID$;'))
        frame=values(b'$RMF$');same(frame,before)
        mouse(30,30);t.exchange()  # Release of the old capture cannot click.
        movement(30,30,b'E8;E7;');click(30,30,b'B8')
        movement(90,90,b'X8;X7;')
        t.case('rip3-mouse-backup-rejected-request-retains-capture')
        mouse(30,30,True);t.exchange()
        values(b'$RMF(9)$');values(b'$SMF(10)$', b'$SMF(10)$')
        mouse(30,30);t.input(b'B8')
        t.case('rip3-mouse-backup-slots-stack-copy-and-protection')
        values(b'$PROT(MOUSE,S3)$$RMF(3)$')
        values(b'$INUSE(MOUSE,S3)$$ISPROT(MOUSE,S3)$',b'11')
        values(b'$UNPROT(MOUSE,S3)$$RMF(3)$')
        values(b'$INUSE(MOUSE,S3)$',b'0')
        values(b'$COPY(MOUSE,TBL,S1,PUSH)$$MKILL$')
        values(b'$RMF(POP)$');click(30,30,b'B8')
        values(b'$INUSE(MOUSE,S1)$$INUSE(MOUSE,STACK)$',b'10')
        t.case('rip3-mouse-backup-saved-handler-absence-and-empty-definitions')
        t.exchange(resident(5,b'$OFF$')+resident(6,b'$OFF$'))
        values(b'$SMF(4)$')
        t.exchange(resident(5,b'never'))
        values(b'$RMF(4)$');movement(30,30)
        values(b'$MKILL$$SMF(5)$$RMF(1)$');click(30,30,b'B8')
        values(b'$RMF(5)$');click(30,30)
        t.case('rip3-mouse-backup-soft-reset-retains-base-and-restores-handlers')
        t.exchange(b'\x01|*\r\n');values(f'$WORLD(CUR,{width},{height})$'.encode())
        values(b'$RMF$');movement(30,30,b'E8;E7;');click(30,30,b'B8')
        values(b'$MKILL$');t.exchange(resident(5,b'$OFF$')+resident(6,b'$OFF$'))
        t.case('rip3-mouse-backup-captured-plain-button-definition-without-repaint')
        accented=b'\x82' if args.encoding=='CP437' else 'é'.encode()
        t.exchange(style(flags=256|1024|2,surface=3,bevel=0)
                   +button((100,120,160,145),b'ID=7:<>$-=DRAW=ran$B<>A:$FIELDID$:'+accented,hotkey=ord('a')))
        values(b'$-=DRAW=cold$$SMF$')
        values(b'$MKILL$')
        before=t.exchange(style(surface=12)+button((100,120,160,145)))
        frame=values(b'$RMF$');same(frame,before)
        values(b'$&DRAW$',b'cold')
        client.text('a');frame=t.input(b'A:7:'+accented)
        assert pixel(frame,158,143)==0x00aaaa
        values(b'$&DRAW$',b'ran')
        t.case('rip3-mouse-backup-owned-source-through-self-replacing-prompt')
        values(b'$MKILL$')
        t.exchange(style(flags=256|1024,surface=3,bevel=0)
                   +button((100,120,160,145),b'ID=7:<><>$RMF$before:$FIELDID$:$=RESTORED,8@Restored$;after:$FIELDID$',hotkey=ord('b')))
        mouse(120,130,True);mouse(120,130);client.wait_text('Restored')
        send(b'\x1b[5n');client.text('kept');client.key('enter')
        receive(peer,b'before:7:kept;after:7\x1b[0n',root/'replies.bin')
        client.text('a');t.input(b'A:7:'+accented)
        t.case('rip3-mouse-backup-ordinary-clipboard-and-hot-icon-resources')
        values(b'$MKILL$')
        t.exchange(b'\x01|S0103|BDW0KDZ0L|1CDW0KDZ0L0\r\n'
                   +style(flags=1|1024|2,bevel=0)
                   +button((400,300,0,0),b'ID=8:<><>clipboard:$FIELDID$',hotkey=ord('c')))
        t.exchange(b'\x01|2P6000001011|2W6000000000000Seed.bmp\r\n')
        paths=list(root.rglob('Seed.bmp'));assert len(paths)==1;assets=paths[0].parent
        (assets/'Face.BMP').write_bytes(bitmap(30,20,0x00aa00))
        (assets/'Face.BMH').write_bytes(bitmap(30,20,0xaa0000))
        t.exchange(style(flags=128|4096|1024,bevel=0)
                   +button((200,120,0,0),b'ID=9:Face<><>icon:$FIELDID$',hotkey=ord('i')))
        values(b'$SMF$');values(b'$MKILL$');before=t.exchange()
        # Restore must neither load files nor retain old feedback resources.
        (assets/'Face.BMP').unlink();(assets/'Face.BMH').unlink()
        frame=values(b'$RMF$');same(frame,before)
        (assets/'Face.BMP').write_bytes(bitmap(30,20,0x00aaaa))
        (assets/'Face.BMH').write_bytes(bitmap(30,20,0xaa00aa))
        mouse(210,130,True);frame=t.exchange();assert pixel(frame,210,130)==0xaa00aa
        mouse(250,160);frame=t.exchange();assert pixel(frame,210,130)==0x00aaaa
        click(210,130,b'icon:9');client.text('c');t.input(b'clipboard:8')
        # The BASE snapshot owns independent descriptors/resources after these
        # live redraws and deletion; a second restore still reads its own data.
        values(b'$MKILL$$RMF$');click(210,130,b'icon:9')
        t.case('rip3-mouse-backup-grouped-capture-restores-ordinary-and-grouped-fields')
        t.exchange(style(flags=256|1024,flags2=1,group=2,bevel=0)
                   +button((300,120,360,145),b'<><>[]group',hotkey=ord('g')))
        values(b'$SMF$');values(b'$MKILL$$RMF$')
        client.text('g');t.input(b'group')
        click(210,130,b'icon:9')
        t.case('rip3-mouse-backup-reset-selectors-and-handler-ownership')
        values(b'$SMF(2)$$SMF(PUSH)$$PROT(MOUSE,S2)$')
        values(b'$RESET(MOUSE,BASE,CUR)$', b'$RESET(MOUSE,BASE,CUR)$')  # All selectors validate before mutation.
        values(b'$INUSE(MOUSE,BASE)$',b'1')
        t.exchange(resident(5,b'E$FIELDID$;'))
        before=values(b'$RESET(MOUSE)$')
        frame=t.exchange(field((40,200,80,240),b'ID=6:new'))
        same(frame,before)  # Definition/reset add no scene painting.
        movement(50,210,b'E6;')  # Field-only reset retained the shared handler.
        t.exchange(resident(5,b'$OFF$'))
        values(b'$RESET(MOUSE,ALL)$')
        values(b'$INUSE(MOUSE,BASE)$$INUSE(MOUSE,S2)$$INUSE(MOUSE,STACK)$',b'010')
        values(b'$RMF(2)$');click(210,130,b'icon:9')
        values(b'$UNPROT(MOUSE,S2)$$RESET(MOUSE,BACKUP)$')
        values(b'$INUSE(MOUSE,S2)$',b'0')
        click(210,130,b'icon:9')  # Backup clearing does not delete live fields.
        t.case('rip3-group-backup-published-literal-state-vector')
        values(b'$MKILL$[2:][3:]')
        # Literal independent PR137 vector: WORLD=DEVICE, base36/coordsize2.
        t.exchange(b'\x01|1B00000200ZK000F000F080702010F07000000\r\n'
                   b'\x01|1U0A0A0U0K1T00ID=1:<>C1<>[2:]A[3:]X\r\n'
                   b'\x01|1U0A0U0U141U00ID=2:<>C2<>[2:]B[3:]Y\r\n')
        click(20,15);click(20,35);values(b'$?2$:$?3$',b'AB:XY')
        before=t.exchange();same(values(b'$SMF$$COPY(MOUSE,BASE,S7)$'),before)
        click(20,15);values(b'[2:]Z');values(b'$?2$:$?3$',b'Z:Y')
        before=t.exchange();same(values(b'$RMF$'),before)
        values(b'$?2$:$?3$',b'Z:XY')
        click(20,15);values(b'$?2$:$?3$',b'B:Y')
        values(b'$RMF$');values(b'$?2$:$?3$',b'AB:XY')
        before=t.exchange();same(values(b'$MKILL$'),before)
        values(b'$?2$:$?3$',b'Z:')
        values(b'$RMF$');values(b'$?2$:$?3$',b'Z:')
        click(20,15);values(b'$?2$:$?3$',b'B:Y')
        values(b'$RMF(7)$');values(b'$?2$:$?3$',b'AB:XY')
        t.case('rip3-group-backup-current-other-publisher-and-kind-change')
        values(b'$MKILL$[3:]baseline')
        t.exchange(style(flags=256|1024,flags2=1,group=4,bevel=0)
                   +button((100,10,150,30),b'<>H<>[3:]H[]h',hotkey=ord('h')))
        click(120,20,b'h');values(b'$?3$',b'H')
        values(b'$RMF$');values(b'$?3$',b'baseline')  # H absent; G cannot steal.
        values(b'$MKILL$')
        t.exchange(style(flags=256|1024|16384,group=2,bevel=0)
                   +button((100,10,150,30),b'<>R<>[3:]radio[]r'))
        click(120,20,b'r');values(b'$?3$',b'radio')
        values(b'$RMF$');values(b'$?3$',b'baseline')  # radio2 != checkbox2.
        t.case('rip3-group-backup-held-preview-captures-committed-state-no-paint')
        values(b'$MKILL$[2:]')
        accented=b'\x82' if args.encoding=='CP437' else 'é'.encode()
        t.exchange(style(flags=256|1024|2,flags2=1,group=2,surface=3,bevel=0)
                   +button((10,100,60,130),b'ID=1:<>$-=DRAW=ran$<>[2:]A[]'+accented))
        click(30,115,accented);before=t.exchange();assert pixel(before,58,128)==0xff5555
        mouse(30,115,True);pressed=t.exchange();assert pixel(pressed,58,128)==0x00aaaa
        values(b'$-=DRAW=cold$$SMF$');same(t.exchange(),pressed)
        frame=values(b'$RMF$');assert pixel(frame,58,128)==0xff5555  # Cancel preview only.
        values(b'$&DRAW$:$?2$',b'cold:A');mouse(30,115);t.exchange()
        # A real activation toggles the restored ON bit to OFF and runs label.
        click(30,115,accented);values(b'$&DRAW$:$?2$',b'ran:')
        values(b'$-=DRAW=cold$');before=t.exchange();same(values(b'$RMF$'),before)
        values(b'$&DRAW$:$?2$',b'cold:A')  # Pixels stay OFF despite logical ON.
        values(b'$HKEYOFF$');before=t.exchange();same(values(b'$SMF$$RMF$'),before)
        client.text('a');t.input(b'a');values(b'$HKEYON$')
        t.case('rip3-group-backup-off-map-provider-bytes-and-soft-publication')
        values(b'$MKILL$[2:]')
        t.exchange(style(flags=256|1024,flags2=1,group=2,bevel=0)
                   +button((10,100,60,130),b'<>raw<>[2:]$-=EFFECT=ran$BODY[]defined'))
        click(30,115,b'defined');click(30,115,b'defined')  # OFF with retained A.
        values(b'$-=EFFECT=cold$$SMF$$RMF$');values(b'$&EFFECT$:$?2$',b'cold:')
        click(30,115,b'defined');values(b'$?2$',b'BODY');values(b'$&EFFECT$',b'ran')
        values(b'$-=EFFECT=cold$');t.exchange(b'\x01|*\r\n')
        values(f'$WORLD(CUR,{width},{height})$'.encode())
        values(b'$RMF$');values(b'$?2$',b'BODY');values(b'$&EFFECT$',b'ran')
        # The SOFT ordinary publication survives RMF of saved OFF owners.
        t.case('rip3-group-backup-selected-empty-and-unselected-radio')
        values(b'$MKILL$[4:]')
        t.exchange(style(flags=256|1024|16384,group=4,bevel=0)
                   +button((100,100,150,130),b'ID=7:<>R<>[4:]never[]r',flags=1))
        values(b'$SMF$$RMF$');values(b'$?4$after',b'after')
        values(b'$MKILL$')
        t.exchange(style(flags=256|1024|16384,group=4,bevel=0)
                   +button((100,100,150,130),b'ID=7:<>R<>[4:]never[]r'))
        values(b'$SMF$$RMF$');values(b'$?4$after')  # No radio selection aborts later lookup.
        values(b'$MKILL$')
        t.case('rip3-group-backup-stale-associated-continuation-and-fieldid')
        t.exchange(style(flags=256|1024,flags2=1,group=2,bevel=0)
                   +button((10,100,60,130),b'ID=7:<>saved<>[2:]saved[]fresh',hotkey=ord('s')))
        values(b'$SMF$')
        t.exchange(button((100,100,150,130),b'ID=7:<>old<>[]$RMF$before:$FIELDID$:$=GROUPWAIT,8@Grouped$;[2:]stale[]after:$FIELDID$'))
        mouse(120,115,True);mouse(120,115);client.wait_text('Grouped')
        send(b'\x1b[5n');client.text('kept');client.key('enter')
        receive(peer,b'before:7:kept;after:7\x1b[0n',root/'replies.bin')
        values(b'$?2$',b'BODY')  # Ordinary SOFT bytes, not stale/restored associations.
        client.text('s');t.input(b'fresh');values(b'$?2$',b'saved')
        t.case('rip3-group-backup-uncapped-owner-bodies-remain-independent')
        values(b'$MKILL$[2:]')
        t.exchange(style(flags=256|1024,flags2=1,group=2,bevel=0)
                   +button((10,100,60,130),b'<>A<>[2:]'+b'A'*3000)
                   +button((100,100,150,130),b'<>B<>[2:]'+b'B'*2000))
        click(30,115);click(120,115);values(b'$SMF$$RMF$')
        values(b'$?2$',b'A'*3000+b'B'*1096)
        click(30,115);values(b'$?2$',b'B'*2000)
        values(b'$RMF$');values(b'$?2$',b'A'*3000+b'B'*1096)
        values(b'alive',b'alive');assert client.capture().application['connected']==1


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path)
    parser.add_argument('encoding',choices=('CP437','UTF-8'))
    parser.add_argument('--reports',type=Path,required=True)
    args=parser.parse_args()
    for width,height in ((640,480),(720,400)):
        run(args,width,height)
