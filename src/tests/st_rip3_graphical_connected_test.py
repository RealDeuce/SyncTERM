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

        t.case("rip3-wire-quote-decoding-precedes-host-expansion")
        values(b"A\\|B\\\x01C\\\x02D\\$SHIFT\\$", b"A|B\x01C\x02D$SHIFT$")
        values(b"\\$SHIFT$:after", b"0:after")
        # The framing quote does not occupy one of the 4096 host bytes.
        values(b"A" * 4094 + b"\\|Z", b"A" * 4094 + b"|Z")

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
        def marker(x, glyph, rotation, flags, *, flag_width=2):
            fields = ((2*x, 4), (100, 4), (glyph, 2), (40, 4), (28, 4), (rotation, 2), (flags, flag_width))
            return b';' + b''.join(number(value, width) for value, width in fields)

        t.exchange(b'\x01|*|n4000|f00ZK00DC|c0C|S0102|N00\r\n')
        scene = b'\x01|' + b'|'.join(marker(70 + i*40, 21, 0, i, flag_width=1) for i in range(4)) + b'\r\n'
        t.send(scene[:18])
        frame = t.exchange(scene[18:])
        assert [pixel(frame, x, 100) for x in (70, 110, 150, 190)] == [0, 0x00aa00, 0, 0x00aa00]
        assert [pixel(frame, x, 86) for x in (70, 110, 150, 190)] == [0xff5555]*3 + [0x00aa00]
        frame = t.exchange(b'\x01|' + b'|'.join((marker(250, 36, 0, 1), marker(290, 21, 360, 1),
                            marker(330, 21, 0, 4), marker(370, 14, 90, 0))) + b'\r\n'
                            + query(b'alive', at_cursor=True), b'alive')
        assert all(pixel(frame, x, 100) == 0 for x in (250, 290, 330))
        assert pixel(frame, 356, 100) == 0xff5555

        t.case('PolyMarker composes the pen and brush with XOR on a connected port')
        frame = t.exchange(b'\x01|W1|N00|=00000003|' + marker(430, 21, 0, 1)
                           + b'|N01|=10000003|' + marker(470, 21, 0, 1) + b'\r\n')
        assert [pixel(frame, x, 100) for x in (430, 470)] == [0x00aa00, 0x00aa00]
        assert [pixel(frame, x, 85) for x in (430, 470)] == [0xff5555, 0xff5555]
        assert [pixel(frame, x, 87) for x in (430, 470)] == [0xffff55, 0xff5555]
        t.exchange(b'\x01|W0|N00|=00000001\r\n')

        t.case('Affine ellipse commands share geometry and choose the correct closure')
        def affine(opcode, x, *, fill=b'', short=False):
            center = (x, 200)
            right, top = (x + 20, 200), (x, 188)
            points = (center, right, top)
            if opcode in (b',', b':', b'`'):
                points += (right, top)
            fields = [number(value, 4) for px, py in points for value in (2*px, py)]
            if short:
                fields[-1] = fields[-1].lstrip(b'0') or b'0'
            return opcode + b''.join(fields) + fill

        frame = t.exchange(b'\x01|' + b'|'.join((affine(b',', 70, short=True), affine(b'.', 130) + b' oval',
                            affine(b':', 190, fill=b'1'), affine(b'`', 250, fill=b'Z'),
                            affine(b'{', 310, short=True), affine(b':', 370, fill=b'.') + b' unfilled pie')) + b'\r\n')
        assert pixel(frame, 90, 200) == 0xff5555 and pixel(frame, 50, 200) == 0
        assert pixel(frame, 110, 200) == 0xff5555 and pixel(frame, 130, 200) == 0
        assert pixel(frame, 195, 195) == 0x00aa00 and pixel(frame, 185, 204) == 0
        assert pixel(frame, 263, 194) == 0x00aa00 and pixel(frame, 255, 195) == 0
        assert pixel(frame, 310, 200) == 0x00aa00
        assert pixel(frame, 380, 200) == 0xff5555 and pixel(frame, 375, 195) == 0
        before = frame
        frame = t.exchange(b'\x01|' + affine(b'`', 450, fill=b'?') + b'|' + affine(b'.', 510)[:-4]
                           + b'\r\n' + query(b'alive', at_cursor=True), b'alive')
        assert frame.primary == before.primary and frame.alternate == before.alternate

        t.case('Affine ellipse coordinates stay in port space under viewport clipping')
        view = b'v' + b''.join(number(value, 4) for value in (1100, 170, 1200, 240))
        frame = t.exchange(b'\x01|' + view + b'|' + affine(b'.', 560, short=True) + b'\r\n')
        assert pixel(frame, 580, 200) == 0xff5555
        assert pixel(frame, 540, 200) == 0

        t.case('Affine ellipses use screen coordinates on positioned screen ports')
        values(b'$CLS$')
        bounds = b''.join(number(value, 4) for value in (600, 120, 1240, 330))
        smaller_view = b''.join(number(value, 4) for value in (640, 155, 1220, 315))
        frame = t.exchange(b'\x01|2P1' + bounds + b'0|2s100|'
                           + affine(b'.', 520) + b'|v' + smaller_view + b'|'
                           + affine(b'{', 330) + b'\r\n')
        values(b'$CUR(PORT)$', b'1')
        assert pixel(frame, 500, 200) == pixel(frame, 540, 200) == 0xff5555
        assert pixel(frame, 330, 200) == 0x00aa00
        assert pixel(frame, 315, 200) == 0 and pixel(frame, 520, 200) == 0

        t.case('Extended font names expand before the selected face is drawn')
        t.exchange(b'\x01|*|c0F\r\n\x1b[?25l')
        values(b'$WORLD(CUR,640,480)$$-=FONT_NAME=dIxOn$[3:]Dixon')

        def font(name, size=22, spacing=100, flags=32, baseline=0):
            fields = ((0, 1), (0, 1), (flags, 4), (size, 2), (0, 2), (0, 2),
                      (0, 2), (baseline, 2), (spacing, 2), (100, 2), (0, 6))
            return b'y' + b''.join(number(value, width, 64) for value, width in fields) + name

        def text(x):
            return b'@' + number(x, 2) + number(100, 2) + b'F'

        def line(*commands):
            return b'\x01|' + b'|'.join(commands) + b'\r\n'

        # Dixon F has 610/1000 em advance. At 22 points and 72 DPI
        # the advance is 13 and the centered glyph fits these regions.
        def glyph(frame, x):
            return {(dx, y) for dx in range(-8, 14) for y in range(79, 113)
                    if pixel(frame, x + dx, y) == 0xffffff}

        frame = t.exchange(line(font(b'Dixon'), text(30), font(b'Marin', 90),
                                font(b'$&FONT_NAME$'), text(75), font(b'Marin', 90),
                                font(b'<<?3>>'), text(120))
                           + query(b'$TEXTXY$', at_cursor=True), b'133:100')
        reference = glyph(frame, 30)
        assert reference and glyph(frame, 75) == reference and glyph(frame, 120) == reference

        t.case('Invalid and protected font commands skip evaluation and preserve the selected face')
        setter = b'$COORDSIZE(CUR,5)$Marin'
        t.exchange(line(font(setter, spacing=0), font(b'')))
        values(b'$COORDSIZE$', b'2')
        values(b'$CUR(STYLE,7)$')
        t.exchange(line(font(b'Dixon')))
        values(b'$PROT(STYLE,7)$')
        frame = t.exchange(line(font(setter), text(170))
                           + query(b'$TEXTXY$:$COORDSIZE$', at_cursor=True), b'183:100:2')
        assert glyph(frame, 170) == reference
        values(b'$UNPROT(STYLE,7)$')

        t.case('Empty expanded names defer face failure and font prompts resume in wire order')
        frame = t.exchange(line(font(b'Marin', 90), font(b'$NULL$'), text(210))
                           + query(b'$TEXTXY$', at_cursor=True), b'223:100')
        assert not glyph(frame, 210)
        send(line(font(b'<<FACEPROMPT>>'), text(260)) + query(b'$TEXTXY$', at_cursor=True))
        client.wait_text('FACEPROMPT')
        client.text('Dixon')
        client.key('enter')
        frame = t.input(b'273:100')
        assert glyph(frame, 260) == reference and frame.application['connected'] == 1

        t.case('Outline font prefixes precede expansion and bound the name working buffer')
        t.exchange(line(font(b'$WORLDH(0,960)$Dixon', flags=0x220, baseline=8)))
        values(b'$WORLD$', b'640:960')
        values(b'$WORLD(CUR,640,480)$')
        frame = t.exchange(line(text(310)) + query(b'$TEXTXY$', at_cursor=True), b'323:100')
        assert glyph(frame, 310) == {(x, y+8) for x, y in reference}
        t.exchange(line(font(b'Dixon' + b' '*4090 + setter)))
        values(b'$COORDSIZE$', b'2')

        t.case("rip3-fixed-width-geometry-ignores-trailing-commentary")

        def geometry(opcode, *fields):
            return opcode + b"".join(number(value, 2) for value in fields)

        commands = (
            b"*", b"fHSDCextra", b"c0C", geometry(b"X", 5, 5), b"E erase the marker",
            geometry(b"L", 10, 10, 14, 10) + b" line",
            geometry(b"R", 20, 10, 24, 14) + b"outline",
            b"S0102", geometry(b"B", 10, 20, 14, 24) + b"001",
            b"N00", geometry(b"K", 20, 20, 24, 24) + b" filled rectangle",
            geometry(b"X", 30, 10) + b" pixel",
            geometry(b"j", 32, 10) + b"point",
            geometry(b"C", 40, 12, 3) + b" circle",
            geometry(b"U", 50, 10, 60, 20, 2) + b"rounded",
            geometry(b"u", 65, 10, 75, 20, 2)[:-2] + b"2",
            geometry(b"X", 80, 10)[:-1] + b" incomplete",
            geometry(b"X", 82, 10),
        )
        frame = t.exchange(line(*commands) + query(b"$WORLD$:alive", at_cursor=True), b"640:480:alive")
        for x, y in ((12, 10), (22, 10), (30, 10), (32, 10), (40, 9), (55, 10), (82, 10)):
            assert pixel(frame, x, y) == 0xFF5555, (x, y, pixel(frame, x, y))
        for x, y in ((12, 22), (22, 22), (70, 15)):
            assert pixel(frame, x, y) == 0x00AA00, (x, y, pixel(frame, x, y))
        assert pixel(frame, 5, 5) == pixel(frame, 80, 0) == 0

        t.case("rip3-fixed-width-drawing-state-and-zero-fields")
        frame = t.exchange(line(
            b"*", b"J10 base", b"n2000 size", b"M08 palette", b"fHSDC",
            b"a0C10 custom red", b"k00 background", b"c0C123", b"W01 XOR",
            geometry(b"X", 10, 10), b"W0.", geometry(b"X", 10, 10),
            geometry(b"X", 11, 10), b"W.2", geometry(b"X", 11, 10),
            b"=00.01 pen", b"Y... sizes", b"Y..0109 font",
            b"N. no border", b"S0102 solid", geometry(b"K", 20, 20, 24, 24),
            b"3D0000 no delay", b"L0A.0E. line at y=0",
            b"c.", geometry(b"X", 12, 10),
        ) + query(b"$BASEMATH$:$COORDSIZE$:$COLORMODE$:alive", at_cursor=True), b"36:2:0:alive")
        assert pixel(frame, 10, 10) == pixel(frame, 12, 10) == 0
        assert pixel(frame, 11, 10) == pixel(frame, 12, 0) == 0xff0000
        assert pixel(frame, 22, 22) == 0x00aa00
        assert pixel(frame, 13, 10) == 0

        frame = t.exchange(query(b"$COLORMODE(CUR,RGB,1)$", at_cursor=True)
                           + line(b"c0002 color", geometry(b"X", 12, 10),
                                  b"c2 incomplete", geometry(b"X", 13, 10), b"M1.")
                           + query(b"$COLORMODE$:alive", at_cursor=True), b"1:alive")
        assert pixel(frame, 12, 10) == pixel(frame, 13, 10) == 0x00ff00

        t.case("rip3-curve-and-counted-path-field-boundaries")
        oval = geometry(b"&", 40, 40, 8, 6)
        bezier = b"Z." + geometry(b"", 68, 5, 68, 10, 68, 15, 68, 2)
        polygon = (b"P04." + geometry(b"", 95, 12, 95, 12, 106)
                   + b"." + number(106, 2))
        contour = b"<0104." + geometry(b"", 160, 12, 160, 12, 172) + b"." + number(172, 2)
        y = number(130, 2)
        polyline = b"t02.0." + y + b"0" + number(14, 2) + y
        frame = t.exchange(line(
            b"*", b"fHSDC", b"c0C", b"S0102", b"N00",
            oval + b". ellipse", bezier + b" standalone",
            polygon, polyline, contour,
        ) + query(b"alive", at_cursor=True), b"alive")
        for x, y in ((48, 40), (9, 68), (6, 95), (12, 101), (7, 130)):
            assert pixel(frame, x, y) == 0xff5555, (x, y, pixel(frame, x, y))
        assert pixel(frame, 6, 165) == 0x00aa00

        before = frame
        frame = t.exchange(line(b"c0E", oval + b"0 incomplete",
                                bezier[:-1] + b". malformed",
                                polygon + b" commentary", polyline + b"0",
                                polyline[:4] + b"." + polyline[5:],
                                contour + b" ignored nowhere")
                           + query(b"alive", at_cursor=True), b"alive")
        assert frame.primary == before.primary and frame.alternate == before.alternate


        t.case("rip3-meganum-wire-alphabet-and-ultranum-case")
        frame = t.exchange(line(
            b"*", b"fHSDC", b"c0C", b"c0a", b"X1408",
            b"c--", b"X1508", b"X0e08", b"2s10a", b"J1s", b"c0A lowercase suffix", b"X1608",
        ) + query(b"$CUR(PORT)$:$BASEMATH$:alive", at_cursor=True), b"0:36:alive")
        assert pixel(frame, 40, 8) == pixel(frame, 41, 8) == 0xff5555
        assert pixel(frame, 14, 8) == 0 and pixel(frame, 42, 8) == 0x55ff55

        frame = t.exchange(line(b"J1S", b"X0a08", b"X0A08", b"J1s")
                           + query(b"$BASEMATH$:alive", at_cursor=True), b"64:alive")
        assert pixel(frame, 36, 8) == pixel(frame, 10, 8) == 0x55ff55


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path)
    parser.add_argument('encoding',choices=('CP437','UTF-8'))
    parser.add_argument('--reports',type=Path,required=True)
    run(parser.parse_args())
