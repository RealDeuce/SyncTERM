#!/usr/bin/env python3
"""Eight-family RIP3 backup composition through real TCP sessions."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path
from st_migrated_control_test import connection
from st_terminal_conformance_test import Terminal
from st_rip3_connected_test import query,pixel
from st_rip3_button_styles_connected_test import number
from st_rip3_buttons_connected_test import style,button
from st_rip3_extended_window_connected_test import window


def colors(frame,plane):
    return [v&0xffffff if v&0x80000000 else frame.palette[v] for v in plane]


def run(args,width,height):
    with connection(args.executable,args.reports,encoding=args.encoding,
                    options='ScreenMode=Custom\nRIP=RIPv3\n',
                    settings=f'CustomColumns={width//8}\nCustomRows={height//8}\nCustomFontHeight=8\n') as (client,root,peer,send):
        t=Terminal(client,root,peer,send)
        def values(source,expected=b''):
            return t.exchange(query(source,at_cursor=True),expected)
        def wire(source):
            return t.exchange(b'\x01|'+source+b'\r\n')
        def reset():
            t.exchange(b'\x01|*\r\n\x1b[?25l')
            values(f'$WORLD(CUR,{width},{height})$'.encode())
        def used(place,expected):
            source=b''.join(f'$INUSE({family},{place})$'.encode() for family in
                            ('TW','PORT','MOUSE','SCREEN','PAL','BUT','STYLE','ENV'))
            values(source,expected)
        t.exchange(b'\x1b[!',b'RIPSCRIP030001')
        reset()
        t.case(f'rip3-sgs-rgs-shipping-current-index-and-senv-ownership-{width}x{height}')
        values(b'$CUR(STYLE,7)$');wire(b'c04')
        values(b'$CUR(STYLE,5)$$SGS$$CUR(STYLE,7)$');wire(b'c0A')
        values(b'$RGS$$CUR(STYLE)$',b'7')
        assert pixel(wire(b'X0505'),5,5)==0xaa0000
        values(b'$MCURSOR(2)$$BAUDEMUL(1200)$$SENV$$SENV(1)$$SENV(PUSH)$')
        values(b'$WORLD(CUR,320,240)$$BASEMATH(CUR,64)$$COORDSIZE(CUR,4)$'
               b'$COLORMODE(CUR,RGB,8)$$MCURSOR(6)$$BAUDEMUL(2400)$'
               b'$CUR(STYLE,9)$$CUR(BUT,8)$$CUR(PORT,6)$$CUR(TW,3)$$CUR(PAL,17)$')
        values(b'$RENV$$WORLD$:$BASEMATH$:$COORDSIZE$:$COLORMODE$:$BAUDEMUL$',
               f'{width}:{height}:36:2:0:1200'.encode())
        values(b'$CUR(STYLE)$:$CUR(BUT)$:$CUR(PORT)$:$CUR(TW)$:$CUR(PAL)$',b'9:8:6:3:17')
        values(b'$RENV(1)$$INUSE(ENV,S1)$$RENV(POP)$$INUSE(ENV,STACK)$',b'00')
        reset()
        t.case('rip3-aggregate-all-eight-families-and-live-selection-defects')
        values(b'$CUR(PAL,17)$');wire(b'a0401')  # Saved inactive palette has blue at 4.
        values(b'$CUR(PAL,5)$');wire(b'a0404')
        values(b'$CUR(STYLE,7)$');wire(b'c04')
        values(b'$CUR(STYLE,5)$');wire(b'c02|S0103|B0K0K0U0U')
        values(b'$CUR(BUT,5)$')
        t.exchange(style(flags=256|1024,surface=3,bevel=0)+button((100,120,160,145),b'ID=7:<><>saved:$FIELDID$',hotkey=ord('a')))
        client.text('a');t.input(b'saved:7')  # Positive control before the snapshot.
        values(b'$CUR(TW,1)$')
        t.exchange(window((200,80,500,220),12,5,0,1)+b'\x1b[?25l\x1b[32;44m\x1b[2;3H')
        wire(b'2P2000008081|2s200|S0106|B00000707')
        values(b'$MCURSOR(2)$$BAUDEMUL(1200)$')
        before=values(b'$SAVEALL$$SAVEALL(2)$$SAVEALL(PUSH)$')
        expected=(colors(before,before.primary),colors(before,before.alternate))
        used('BASE',b'11111111')
        values(b'$CUR(PORT,0)$$CUR(TW,0)$$CUR(PAL,17)$$CUR(BUT,9)$$CUR(STYLE,7)$')
        wire(b'c0A|S0105|B0000'+number(width-1,2)+number(height-1,2)+b'|a0402|2P2000002021|2s200|S0101|B00000101')
        values(b'$WORLD(CUR,320,240)$$MCURSOR(6)$$BAUDEMUL(2400)$$MKILL$')
        after=values(b'$RESTOREALL$')
        assert (colors(after,after.primary),colors(after,after.alternate))==expected
        values(b'$CUR(TW)$:$CUR(PORT)$:$CUR(PAL)$:$CUR(BUT)$:$CUR(STYLE)$:$WORLD$:$BAUDEMUL$',
               f'1:2:5:5:7:{width}:{height}:1200'.encode())
        assert (after.application['cterm.x'],after.application['cterm.y'],after.application['cterm.attr'])==(3,2,0x12)
        values(b'$PALENTRY(17,4)$',b'6;0:0:42')
        values(b'$CUR(PORT,0)$')
        frame=wire(b'2C20000080805K5K60600')  # Saved brown offscreen pixels copied to screen.
        assert pixel(frame,200,200)==0xaa5500
        assert pixel(wire(b'X0505'),5,5)==0xaa0000  # RGS keeps live index 7 but restores its red record.
        client.text('a');t.input(b'saved:7')
        accented=b'\x82' if args.encoding=='CP437' else 'é'.encode()
        frame=t.exchange(accented)
        assert (frame.application['cterm.x'],frame.application['cterm.y'])==(4,2)
        t.case('rip3-aggregate-numbered-protection-and-independent-stacks')
        values(b'$PROT(PORT,S2)$$PROT(STYLE,S2)$$RESTOREALL(2)$')
        used('S2',b'01000010')
        values(b'$RESTOREALL(POP)$');used('STACK',b'00000000')
        values(b'$RESTOREALL$');used('BASE',b'11111111')
        t.case('rip3-aggregate-missing-palette-continues-through-later-env')
        values(b'$RESET(PAL,BASE)$$CUR(PAL,17)$')
        wire(b'a0402')
        values(b'$WORLD(CUR,320,240)$$BAUDEMUL(2400)$$RESTOREALL$')
        values(b'$CUR(PAL)$:$PALENTRY(17,4)$:$CUR(BUT)$:$WORLD$:$BAUDEMUL$',
               f'17:6;42:0:0:5:{width}:{height}:1200'.encode())
        t.case('rip3-aggregate-independent-save-failure-and-invalid-recovery')
        # An occupied protected slot blocks only that family's save. Its older
        # BASE/slot/stack state and every other family remain independently usable.
        values(b'$SAVEALL(2)$');used('S2',b'11111111')
        values(b'$SAVEALL(3)$$RESTOREALL(3)$');used('S3',b'00000000')
        values(b'$SAVEALL(10)$');values(b'$RESTOREALL(PUSH)$')
        values(b'$RESTOREALL(9)$');values(b'$WORLD$:alive',f'{width}:{height}:alive'.encode())
        assert client.capture().application['connected']==1


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path)
    parser.add_argument('encoding',choices=('CP437','UTF-8'))
    parser.add_argument('--reports',type=Path,required=True)
    args=parser.parse_args()
    for width,height in ((640,480),(800,600),(1024,768),(720,400)):
        run(args,width,height)
