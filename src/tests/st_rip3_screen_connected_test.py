#!/usr/bin/env python3
"""Physical screen clear and screen/palette saves through real RIP3 TCP sessions."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import struct
from pathlib import Path
from st_migrated_control_test import connection
from st_terminal_conformance_test import Terminal
from st_rip3_connected_test import query,pixel
from st_rip3_buttons_connected_test import style,button
from st_rip3_button_styles_connected_test import number


def colors(frame,plane):
    return [v&0xffffff if v&0x80000000 else frame.palette[v] for v in plane]


def check_clear(t, width, height):
    def values(text, expected=b""):
        return t.exchange(query(text, at_cursor=True), expected)

    t.case(f"rip3-cls-physical-black-without-reset-{width}x{height}")
    t.exchange(b"\x01|*\r\n\x1b[?25l")
    values(f"$WORLD(CUR,{width},{height})$$CUR(PAL,0)$".encode())
    far = number(width-1, 2) + number(height-1, 2)
    t.exchange(b"\x01|a0004|k03|c00|X1010|c0F|X" + far + b"\r\n"
               + style(flags=256|1024, bevel=0)
               + button((60, 70, 100, 90), b"ID=8:<><>still-live:$FIELDID$", hotkey=ord("k"))
               + b"\x1b[1;1H\x1b[32;45;5mX")
    t.exchange(b"\x01|v0A0A0K0K|2P6000002021|2s600|c02|X0000|v00000000\r\n")
    before = values(b"$PROT(PORT,6)$$SAVE(4)$$PALENTRY(0,0)$", b"6;42:0:0")
    assert pixel(before, 36, 36) == 0xa80000
    assert pixel(before, width-1, height-1) == 0xffffff
    assert before.primary != before.alternate

    rejected = values(b"$CLS(1)$alive", b"alive")
    assert before.primary == rejected.primary and before.alternate == rejected.alternate
    frame = values(b"$CLS$$CUR(PORT)$:$CUR(PAL)$:$ISPROT(PORT,6)$:$INUSE(SCREEN,S4)$:$TWIN$:"
                   b"$PALENTRY(0,0)$", b"6:0:1:1:YES:6;42:0:0")
    assert all(value == 0 for value in colors(frame, frame.primary))
    assert frame.primary == frame.alternate
    for key in ("cterm.x", "cterm.y", "cterm.attr", "cterm.top_margin", "cterm.bottom_margin"):
        assert frame.application[key] == before.application[key]
    t.client.text("k")
    t.input(b"still-live:8")

    values(b"$UNPROT(PORT,6)$$AVP(6)$")
    t.exchange(b"\x01|2W6000001010000Offscreen.bmp\r\n")
    data = next(t.root.rglob("Offscreen.bmp")).read_bytes()
    offset, = struct.unpack_from("<I", data, 10)
    assert struct.unpack_from("<iiHH", data, 18) == (2, 2, 1, 8)
    assert data[offset + 4] == 2  # Top-left pixel after one padded bottom row.

    frame = values(b"$RESTORE(4)$")
    assert (colors(frame, frame.primary), colors(frame, frame.alternate)) == (
        colors(before, before.primary), colors(before, before.alternate))


def run(args,width,height):
    with connection(args.executable,args.reports,encoding=args.encoding,
                    options='ScreenMode=Custom\nRIP=RIPv3\n',
                    settings=f'CustomColumns={width//8}\nCustomRows={height//8}\nCustomFontHeight=8\n') as (client,root,peer,send):
        t=Terminal(client,root,peer,send)
        def values(source,expected=b''):
            return t.exchange(query(source,at_cursor=True),expected)
        t.exchange(b'\x1b[!',b'RIPSCRIP030001')
        t.exchange(b'\x01|*\r\n\x1b[?25l')
        values(f'$WORLD(CUR,{width},{height})$'.encode())
        t.case(f'rip3-screen-physical-native-blink-and-current-palette-{width}x{height}')
        values(b'$CUR(PAL,5)$')
        accented=b'\x82' if args.encoding=='CP437' else 'é'.encode()
        t.exchange(b'\x01|a0F04|c0F|B1E1E1Y1Y\r\n'
                   +b'\x1b[H\x1b[41m \x1b[5;32;44mX'+accented+b'\x1b[0m')
        t.case('rip3-port-write-exports-live-ansi-and-rip-pixels')
        t.exchange(b'\x01|c02|X0100|2W0000002010000Live.bmp\r\n')
        live, = root.rglob('Live.bmp')
        data = live.read_bytes()
        assert struct.unpack_from('<iiHH',data,18)==(2,1,1,8)
        offset, = struct.unpack_from('<I',data,10)
        assert data[offset:]==bytes((15,2,0,0)), data[offset:]
        # Save from an inactive/protected offscreen current port, with PORT0
        # viewport excluding both the native text and graphics source pixels.
        t.exchange(b'\x01|v0A0A0K0K|2P6000002021|2s600|v00000000\r\n')
        values(b'$PROT(PORT,6)$')
        before=values(b'$SAVE$')
        assert pixel(before,50,50)==0xa80000 and pixel(before,0,0)==0xa80000
        assert before.primary!=before.alternate
        expected=(colors(before,before.primary),colors(before,before.alternate))
        values(b'$CUR(PAL,17)$')
        t.exchange(b'\x01|a0F02\r\n')
        values(b'$PROT(PAL,17)$$RESET(SCREEN)$')
        t.exchange(b'\x1b[Hchanged')
        frame=values(b'$RESTORE$')
        assert (colors(frame,frame.primary),colors(frame,frame.alternate))==expected
        assert pixel(frame,50,50)==0xa80000 and pixel(frame,0,0)==0xa80000
        values(b'$CUR(PAL)$:$ISPROT(PAL,17)$:$CUR(PORT)$:$ISPROT(PORT,6)$',b'17:1:6:1')
        values(b'$PALENTRY(17,15)$',b'6;42:0:0')
        # Indexed bitmap consumers must match restored native RGB under the
        # explicit RIP palette, rather than truncating an RGB word to an index.
        values(b'$CUR(PORT,0)$')
        t.exchange(b'\x01|v0000'+number(width-1,2)+number(height-1,2)
                   +b'|2W0000001010000Native.bmp\r\n')
        paths=list(root.rglob('Native.bmp'));assert len(paths)==1;data=paths[0].read_bytes()
        assert struct.unpack_from('<ii',data,18)==(1,1) and struct.unpack_from('<H',data,28)==(8,)
        offset=struct.unpack_from('<I',data,10)[0];assert data[offset]==15
        assert data[54+15*4:54+15*4+3]==bytes((0,0,168))
        values(b'$CUR(PORT,6)$')
        # Raw graphics indexes retain later palette recoloring; captured native
        # ANSI RGB and literal reset black retain their independent colors.
        values(b'$UNPROT(PAL,17)$')
        frame=t.exchange(b'\x01|a0F02|a0001\r\n')
        assert pixel(frame,50,50)==0x00a800 and pixel(frame,0,0)==0xa80000
        assert pixel(frame,width-1,height-1)==0
        values(b'$RESTORE$')
        t.case('rip3-screen-native-cell-writes-and-circular-scroll-after-restore')
        frame=t.exchange(b'\x1b[1;2r\x1b[1;1H\x1b[43m \x1b[2;1H\x1b[46m \x1b[0m\n\x1b[r')
        assert pixel(frame,0,0)==0x00a8a8  # Second native row scrolled into the first.
        cursor=(frame.metadata['pixel_cursor_x'],frame.metadata['pixel_cursor_y'])
        frame=values(b'$RESTORE$')
        assert (colors(frame,frame.primary),colors(frame,frame.alternate))==expected
        assert (frame.metadata['pixel_cursor_x'],frame.metadata['pixel_cursor_y'])==cursor
        t.case('rip3-screen-retains-live-mouse-group-style-and-viewport-state')
        values(b'$UNPROT(PORT,6)$$CUR(PORT,0)$$CUR(PAL,0)$')
        t.exchange(b'\x01|*\r\n\x1b[?25l')
        values(f'$WORLD(CUR,{width},{height})$'.encode())
        t.exchange(style(flags=256|1024|2,flags2=1,group=2,surface=3,bevel=0)
                   +button((100,120,160,145),b'<><>[2:]A[]group',hotkey=ord('g')))
        values(b'$SAVE(1)$')
        client.text('g');t.input(b'group');values(b'$?2$',b'A')
        t.exchange(style(flags=256|1024,surface=12,bevel=0)
                   +button((200,120,260,145),b'ID=7:<><>new:$FIELDID$',hotkey=ord('b')))
        values(b'$RESTORE(1)$');values(b'$?2$',b'A')
        client.text('b');t.input(b'new:7')  # Screen pixels do not replace fields.
        client.text('g');t.input(b'group');values(b'$?2$')  # Logical selection stayed ON.
        values(b'$MKILL$')
        t.case('rip3-screen-numbered-aliases-protection-stack-and-actions')
        # Each alias addresses the same numbered family as its parameter form.
        for n in range(10):
            values(f'$SAVE{n}$'.encode());values(f'$RESTORE({n})$'.encode())
            values(f'$INUSE(SCREEN,S{n})$'.encode(),b'0')
        values(b'$SAVE(3)$$PROT(SCREEN,S3)$$RESTORE3$')
        values(b'$INUSE(SCREEN,S3)$$ISPROT(SCREEN,S3)$',b'11')
        values(b'$SAVE(PUSH)$$RESTORE(POP)$$INUSE(SCREEN,STACK)$',b'0')
        values(b'before:$RESTORE9$after',b'before:after')  # Valid failed action is not a variable failure.
        values(b'$SAVE0(1)$bad',b'bad')
        values(b'$RESTORE0(1)$bad',b'bad')
        values(b'$SAVE(10)$bad',b'bad')
        values(b'$INUSE(SCREEN,S0)$',b'0')
        values(b'alive',b'alive')
        values(b'$UNPROT(SCREEN,S3)$$RESET(SCREEN,BACKUP)$')
        values(b'$INUSE(SCREEN,BASE)$$INUSE(SCREEN,S3)$$INUSE(SCREEN,STACK)$',b'000')
        t.case('rip3-screen-whole-copy-and-soft-backup-lifetime')
        values(b'$CUR(PAL,5)$')
        t.exchange(b'\x01|c0C|X1E1E\r\n')
        before=values(b'$COPY(SCREEN,TBL,BASE,S2,PUSH)$')
        expected=(colors(before,before.primary),colors(before,before.alternate))
        t.exchange(b'\x01|*\r\n\x1b[?25l')
        frame=values(b'$COPY(SCREEN,POP,TBL)$')
        assert (colors(frame,frame.primary),colors(frame,frame.alternate))==expected
        values(b'$INUSE(SCREEN,BASE)$$INUSE(SCREEN,S2)$$INUSE(SCREEN,STACK)$',b'110')
        values(b'$COPY(SCREEN,S2,TBL)$$INUSE(SCREEN,S2)$',b'0')
        values(b'$RESET(SCREEN,ALL)$')
        values(b'$INUSE(SCREEN,BASE)$',b'0')
        frame=t.exchange()
        assert all(v==frame.palette[0] for v in colors(frame,frame.primary))
        assert frame.application['connected']==1
        check_clear(t, width, height)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path)
    parser.add_argument('encoding',choices=('CP437','UTF-8'))
    parser.add_argument('--reports',type=Path,required=True)
    args=parser.parse_args()
    for width,height in ((640,480),(720,400)):
        run(args,width,height)
