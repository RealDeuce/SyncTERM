#!/usr/bin/env python3
"""PR126 graphical text through the TCP/parser/bitmap presentation path."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path
from st_migrated_control_test import connection
from st_terminal_conformance_test import Terminal
from st_rip3_connected_test import query,pixel
from st_rip3_button_styles_connected_test import number
from st_rip3_mouse_connected_test import field


def run(args):
    with connection(args.executable,args.reports,encoding=args.encoding,
                    options='ScreenMode=Custom\nRIP=RIPv3\n',
                    settings='CustomColumns=80\nCustomRows=60\nCustomFontHeight=8\n') as (client,root,peer,send):
        t=Terminal(client,root,peer,send)
        def values(source,expected=b''):
            return t.exchange(query(source,at_cursor=True),expected)

        def default_glyph():
            t.exchange(b'\x01|k00|E|c0F\r\n')
            values(b'$WORLD(CUR,1280,960)$')
            frame = t.exchange(b'\x01|@1414A\r\n' + query(b'$TEXTXY$', at_cursor=True), b'40:40')
            points = {(x, y) for y in range(15, 44) for x in range(15, 44)
                      if pixel(frame, x, y) == 0xffffff}
            assert points and all(19 <= x <= 24 and 19 <= y <= 26 for x, y in points)
            return points

        t.exchange(b'\x1b[!',b'RIPSCRIP030001')
        t.case('classic font zero draws DEFAULT EX on a fresh connection')
        glyph = default_glyph()
        t.exchange(b'\x01|*\r\n\x1b[?25l')
        values(b'$WORLD(CUR,640,480)$')
        t.exchange(b'\x01|Y00000100|c0F\r\n')
        t.case('rip3-graphical-builtins-and-ink')
        frame=t.exchange(b'\x01|m0A1E|T$RESX$\r\n'+query(b'$TEXTXY$',at_cursor=True),b'34:50')
        assert any(pixel(frame,x,y) for y in range(35,75) for x in range(10,34))
        assert not any(pixel(frame,x,y) for y in range(35,75) for x in range(34,60))
        t.case('rip3-graphical-literal-disabled-and-quoted-references')
        for source,expected in ((b'\\$V$',b'$V$'),(b'^M``',b'^M``'),
                                (b'$?3$',b'$?3$'),(b'$>SCENE.RIP$',b'$>SCENE.RIP$'),
                                (b'$WORLD(CUR,1,2)$',b'$WORLD(CUR,1,2)$'),
                                (b'$MKILL$',b'$MKILL$'),(b'$RESX(1)$',b'$RESX(1)$')):
            t.exchange(b'\x01|m0000|T'+source+b'\r\n'+query(b'$TEXTXY$',at_cursor=True),
                       f'{len(expected)*8}:0'.encode())
        values(b'$WORLD$',b'640:480')
        t.case('rip3-graphical-angle-template-and-single-ordinary-pass')
        values(b'[3:]$RESX$')
        values(b'[4:]$?3$')
        for source,expected in ((b'<<?3>>',b'640'),(b'<<?4>>',b'$?3$')):
            t.exchange(b'\x01|m0000|T'+source+b'\r\n'+query(b'$TEXTXY$',at_cursor=True),
                       f'{len(expected)*8}:0'.encode())
        t.case('rip3-graphical-position-prefix-survives-setter')
        t.exchange(b'\x01|@0A1E$COORDSIZE(CUR,5)$A\r\n'
                   +query(b'$TEXTXY$:$COORDSIZE$',at_cursor=True),b'10:50:5')
        values(b'$COORDSIZE(CUR,2)$')
        t.case('rip3-graphical-working-prefix-discards-provider-suffix')
        t.exchange(b'\x01|m0000|T'+b'A'*1022+b'$COORDSIZE(CUR,5)$\r\n'
                   +query(b'$TEXTXY$:$COORDSIZE$',at_cursor=True),b'8176:0:2')
        t.case('rip3-graphical-original-encoding-byte-budget')
        encoded=b'\x82'*1022 if args.encoding=='CP437' else b'\xc3\xa9'*511
        glyphs=1022 if args.encoding=='CP437' else 511
        t.exchange(b'\x01|m0000|T'+encoded+b'$COORDSIZE(CUR,5)$\r\n'
                   +query(b'$TEXTXY$:$COORDSIZE$',at_cursor=True),f'{glyphs*8}:0:2'.encode())
        t.case('rip3-graphical-failure-has-no-partial-ink-or-field-side-effects')
        before=t.exchange(field((20,20,60,60),b'click'))
        frame=t.exchange(b'\x01|Tbefore<<MKILL>>suffix\r\n'+query(b'alive',at_cursor=True),b'alive')
        assert frame.primary==before.primary and frame.alternate==before.alternate
        m=frame.metadata
        def mouse(left):
            x=m['destination_x']+81*m['destination_width']//(2*m['plane_width'])
            y=m['destination_y']+81*m['destination_height']//(2*m['plane_height'])
            count=client.capture().metadata['events_dequeued']
            client.mouse(x,y,action=1,buttons=int(left))
            client.wait_for(lambda s:s.metadata['events_dequeued']>count)
        mouse(True);mouse(False);t.input(b'click')

        t.case('classic Y resolves the three published flag words')
        values(b'$WORLD(CUR,1280,960)$')
        region = ((x, y) for y in range(385, 480) for x in range(430, 640))
        points = tuple(region)

        def signature(flags):
            return t.exchange(b'\x01|k00|E|c0F|k08|Y000002' + flags
                              + b'|@R4OOB.Pommenville 95-2\r\n')

        def ink(frame, color):
            return {(x, y) for x, y in points if pixel(frame, x, y) == color}

        top = ink(signature(b'09'), 0xffffff)
        assert top and min(x for x, _ in top) >= 488 and min(y for _, y in top) >= 444
        assert max(x for x, _ in top) <= 615
        middle = ink(signature(b'1T'), 0xffffff)
        shift = min(y for _, y in middle) - min(y for _, y in top)
        assert shift < 0 and middle == {(x, y + shift) for x, y in top}
        shadowed = signature(b'3T')
        assert ink(shadowed, 0xffffff) == top
        shadow = {(x + 1, y + 1) for x, y in top if x < 639 and y < 479} - top
        assert shadow and ink(shadowed, 0x555555) == shadow
        rejected = signature(b'3T|Y01000303|Y0100030O')
        assert rejected.primary == shadowed.primary and rejected.alternate == shadowed.alternate

        t.case('classic font zero uses DEFAULT EX on default style paths')

        t.exchange(b'\x01|*\r\n')
        assert default_glyph() == glyph
        values(b'$SAVEALL$')
        t.exchange(b'\x01|Y01000309|2Y100\r\n')
        assert default_glyph() == glyph
        values(b'$RESTOREALL$')
        assert default_glyph() == glyph
        values(b'$RESET(HARD)$')
        assert default_glyph() == glyph

        t.case('PolyMarker projects full dimensions and selects the flag-defined pen and brush')
        # Marker 21 is a square spanning signed 50% of each full extent.
        # At a 2:1 X scale, WORLD width 40 becomes device width 20. The
        # device height is 28, so the top edge projects to y=86.
        def marker(x, glyph, rotation, flags):
            fields = ((2*x, 4), (100, 4), (glyph, 2), (40, 4), (28, 4), (rotation, 2), (flags, 2))
            return b';' + b''.join(number(value, width) for value, width in fields)

        t.exchange(b'\x01|*|n4000|f00ZK00DC|c0C|S0102|N00\r\n')
        scene = b'\x01|' + b'|'.join(marker(70 + i*40, 21, 0, i) for i in range(4)) + b'\r\n'
        t.send(scene[:18])
        frame = t.exchange(scene[18:])
        assert [pixel(frame, x, 100) for x in (70, 110, 150, 190)] == [0, 0x00aa00, 0, 0x00aa00]
        assert [pixel(frame, x, 86) for x in (70, 110, 150, 190)] == [0xff5555]*3 + [0x00aa00]
        frame = t.exchange(b'\x01|' + b'|'.join((marker(250, 36, 0, 1), marker(290, 21, 360, 1),
                            marker(330, 21, 0, 4), marker(370, 14, 90, 0))) + b'\r\n'
                            + query(b'alive', at_cursor=True), b'alive')
        assert all(pixel(frame, x, 100) == 0 for x in (250, 290, 330))
        assert pixel(frame, 356, 100) == 0xff5555

        t.case('Affine ellipse commands share geometry and choose the correct closure')
        def affine(opcode, x, *, fill=b''):
            center = (x, 200)
            right, top = (x + 20, 200), (x, 188)
            points = (center, right, top)
            if opcode in (b',', b':', b'`'):
                points += (right, top)
            return opcode + b''.join(number(2*px, 4) + number(py, 4) for px, py in points) + fill

        frame = t.exchange(b'\x01|' + b'|'.join((affine(b',', 70), affine(b'.', 130),
                            affine(b':', 190, fill=b'1'), affine(b'`', 250, fill=b'Z'),
                            affine(b'{', 310), affine(b':', 370, fill=b'0'))) + b'\r\n')
        assert pixel(frame, 90, 200) == 0xff5555 and pixel(frame, 50, 200) == 0
        assert pixel(frame, 110, 200) == 0xff5555 and pixel(frame, 130, 200) == 0
        assert pixel(frame, 195, 195) == 0x00aa00 and pixel(frame, 185, 204) == 0
        assert pixel(frame, 263, 194) == 0x00aa00 and pixel(frame, 255, 195) == 0
        assert pixel(frame, 310, 200) == 0x00aa00
        assert pixel(frame, 380, 200) == 0xff5555 and pixel(frame, 375, 195) == 0
        before = frame
        frame = t.exchange(b'\x01|' + affine(b'`', 450, fill=b'?') + b'|' + affine(b'.', 510)[:-2]
                           + b'\r\n' + query(b'alive', at_cursor=True), b'alive')
        assert frame.primary == before.primary and frame.alternate == before.alternate


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path)
    parser.add_argument('encoding',choices=('CP437','UTF-8'))
    parser.add_argument('--reports',type=Path,required=True)
    run(parser.parse_args())
