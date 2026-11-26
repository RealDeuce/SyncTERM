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


def bold_scan(t):
    t.case('rip3-bold-scan-preserves-contours-and-text-advance')
    # Baseline (20,100), size 4. The BOLD width table gives the successive
    # origins 20, 56, 70, 83, 102, 142, 171 and the final cursor at 186.
    frame = t.exchange(b'\x01|*|fHSDC|c0F|Y0A00040W|m0K2S|TA":;%=I\r\n'
                       + query(b'$TEXTXY$', at_cursor=True), b'186:100')
    # Published colon and equals contours, projected from the 640x350 CHR
    # reference plane to this 640x480 device. Include both interiors and gaps.
    for left, right, bars in ((70, 76, ((66, 78), (89, 100))),
                              (142, 164, ((63, 70), (75, 82)))):
        for y in range(60, 103):
            for x in range(left - 1, right + 2):
                edge = left <= x <= right and any(
                    top <= y <= bottom and (x in (left, right) or y in (top, bottom))
                    for top, bottom in bars)
                assert pixel(frame, x, y) == (0xffffff if edge else 0), (x, y)
    # Both quotes, the semicolon's upper contour, the percent's final slash
    # and the ordinary glyphs on either side of the run all contribute ink.
    for x, y in ((20, 100), (57, 45), (62, 45), (89, 71), (133, 42), (171, 80)):
        assert pixel(frame, x, y) == 0xffffff, (x, y)


def skewed_curves(t):
    t.case('rip3-live-skewed-span-and-composition')

    def curve(opcode, *fields):
        return opcode + b''.join(number(value, 2) for value in fields)

    commands = [b'*', b'fHSDC', b'S0104', b'B0000GO4G', b'c02']
    for mode in range(5):
        x = 50 + 100 * mode
        commands += [b'W' + number(mode, 2),
                     curve(b']', x, 58, 10, 10, 0, 719, 0),
                     curve(b']', x + 40, 58, 10, 10, 0, 720, 0)]

    # The whole oval rotates the 20-pixel X radius onto the vertical axis.
    # Chords with these same angle spans differ in even-odd fill membership.
    commands += [b'W0', curve(b'&', 100, 108, 20, 10, 90), b'S0102', b'N0',
                 curve(b'+', 185, 110, 10, 10, 0, 719, 0),
                 curve(b'+', 235, 110, 10, 10, 0, 720, 0),
                 b'c0F', curve(b'[', 450, 114, 10, 10, 0, 720, 0),
                 b'N1', curve(b'[', 510, 114, 10, 10, 0, 720, 0)]

    # Radius X is zero. The arc goes up a straight six-pixel segment, so
    # AAAA has marks at 116,114,112,110 with gaps between them.
    commands += [b'W1', b'c02', b'k01', b'=040XPM01',
                 curve(b']', 340, 116, 0, 6, 0, 90, 0), b'=140XPM01',
                 curve(b']', 380, 116, 0, 6, 0, 90, 0)]
    frame = t.exchange(b'\x01|' + b'|'.join(commands) + b'\r\n' + query(b'alive', at_cursor=True), b'alive')
    single = (0x00aa00, 0xaa5500, 0xaa5500, 0, 0xffdbff)
    double = (0x00aa00, 0xaa0000, 0xaa5500, 0, 0xaa0000)
    for mode, (once, twice) in enumerate(zip(single, double)):
        x = 50 + 100 * mode
        assert pixel(frame, x, 48) == twice and pixel(frame, x + 40, 48) == once
        assert pixel(frame, x, 68) == twice and pixel(frame, x + 40, 68) == once
        assert pixel(frame, x, 58) == pixel(frame, x + 40, 58) == 0xaa0000
    assert pixel(frame, 100, 88) == pixel(frame, 110, 108) == 0x00aa00
    assert pixel(frame, 120, 108) == 0xaa0000
    assert [pixel(frame, x, y) for x, y in ((185, 110), (235, 110), (455, 114), (515, 114))] == [
        0xaa0000, 0x00aa00, 0x00aa00, 0xffffff,
    ]
    for x, gap in ((340, 0xaa0000), (380, 0xaa00aa)):
        assert [pixel(frame, x, y) for y in range(110, 117)] == [0xaa5500, gap] * 3 + [0xaa5500]

    t.case('rip3-rotated-oval-applies-world-scale-before-rotation')
    # At a 2:1 X scale and a 1:1 Y scale, radii (20,10) become (10,10).
    # The viewport starts at device (5,7), so angle zero rotated 90 degrees
    # puts the first point at (50+5,80+7-10).
    frame = t.exchange(b'\x01|*|fZKDC|v0A07ZKDC|c02|' + curve(b'&', 100, 80, 20, 10, 90) + b'\r\n')
    assert pixel(frame, 55, 77) == 0x00aa00 and pixel(frame, 55, 67) == 0


def mixed_shape_composition(t):
    t.case('rip3-shape-fills-borders-and-retraced-strokes-over-truecolor')

    def shape(opcode, *fields):
        return opcode + b''.join(number(value, 2) for value in fields)

    # Draw into truecolor text cells. Palette indices 4 and 2 resolve
    # to red and green before the Boolean operation with the visible RGB.
    background = 0x112233
    fill = background ^ 0xaa0000
    stroke = background ^ 0x00aa00
    border = 0x00aa00
    t.exchange(b'\x01|*|fHSDC\r\n\x1b[?25l\x1b[0;48;2;17;34;51m'
               + b''.join(f'\x1b[{row};1H'.encode() + b' ' * 40 for row in (1, 2, 3)))
    commands = [
        b'W1', b'c02', b'S0104', b'N1',
        shape(b'{', 12, 11, 18, 11, 12, 6), shape(b'.', 30, 11, 36, 11, 30, 6),
        shape(b'I', 45, 11, 0, 360, 3), shape(b'i', 55, 11, 0, 1, 3, 3),
        shape(b'_', 65, 17, 0, 90, 10, 10),
        shape(b'K', 80, 6, 90, 16), shape(b'u', 94, 6, 104, 16, 2),
        shape(b'U', 110, 6, 120, 16, 2), shape(b'G', 130, 11, 5),
        shape(b'o', 145, 11, 6, 4),
        shape(b'P03', 155, 6, 165, 6, 165, 16),
        shape(b'l03', 170, 11, 180, 11, 170, 11),
        b'N0', shape(b'I', 190, 11, 0, 360, 3), shape(b'i', 200, 11, 0, 1, 3, 3),
        shape(b'<0204', 210, 4, 230, 4, 230, 18, 210, 18)
        + shape(b'04', 215, 8, 225, 8, 225, 14, 215, 14),
        # Only the last bit of this opaque pen is set. The first ten
        # pixels of each polygon's top edge therefore copy Back Color.
        b'N1', b'k01', b'=14000101',
        shape(b'p04', 235, 6, 245, 6, 245, 16, 235, 16),
        b'x0400' + b''.join(shape(b'0', x, y) for x, y in
                             ((250, 6), (260, 6), (260, 16), (250, 16))),
        shape(b'K', 265, 6, 275, 16),
    ]
    frame = t.exchange(b'\x01|' + b'|'.join(commands) + b'\r\n'
                       + query(b'alive', at_cursor=True), b'alive')
    samples = {
        fill: [(12, 11), (54, 10), (71, 11), (85, 11), (99, 11), (130, 11),
               (145, 11), (201, 11), (212, 11), (240, 11), (255, 11), (270, 11)],
        border: [(47, 11), (56, 11), (70, 12), (85, 6), (99, 6), (130, 6),
                 (145, 7), (270, 6)],
        stroke: [(36, 11), (115, 6), (160, 6)],
        background: [(42, 11), (45, 10), (65, 17), (160, 8), (175, 11),
                     (192, 11), (220, 11)],
        0x0000aa: [(240, 6), (255, 6)],
    }
    for expected, points in samples.items():
        for x, y in points:
            actual = pixel(frame, x, y)
            assert actual == expected, (x, y, hex(actual), hex(expected))


def run(args):
    with connection(args.executable,args.reports,encoding=args.encoding,
                    options='ScreenMode=Custom\nRIP=RIPv3\n',
                    settings='CustomColumns=80\nCustomRows=60\nCustomFontHeight=8\nAudioModes=0\n') as (client,root,peer,send):
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
        t.case('rip3-erase-clears-full-protected-port-at-zero-world')
        values(b'$WORLD(CUR,640,480)$')
        frame = t.exchange(b'\x01|c02|X0A0A|X0D0D|X0H0H|2P50A0A0G0G2|v0A0A0C0C|k02|f..\r\n'
                           + query(b'$PROT(PORT,5)$', at_cursor=True) + b'\x01|E surplus\r\n'
                           + query(b'$WORLD$', at_cursor=True), b'0:0')
        assert pixel(frame, 10, 10) == 0
        assert pixel(frame, 13, 13) == 0
        assert pixel(frame, 17, 17) == 0x00aa00
        frame = t.exchange(query(b'$WORLD(CUR,640,480)$', at_cursor=True)
                           + b'\x01|a0004|c02|X0A0A|X0D0D\r\n')
        assert pixel(frame, 10, 10) == 0x00aa00 and pixel(frame, 13, 13) == 0
        before = values(b'$CUR(PORT,0)$$EGW(5,BAD)$', b'$EGW(5,BAD)$')
        assert pixel(before, 10, 10) == 0x00aa00
        erased = values(b'$EGW(5)$$CUR(PORT)$', b'0')
        assert pixel(erased, 10, 10) == 0 and pixel(erased, 17, 17) == 0x00aa00
        assert erased.application['connected'] == 1
        t.exchange(b'\x01|*\r\n\x1b[?25l')
        values(b'$WORLD(CUR,640,480)$')
        t.case('rip3-legacy-fill-descriptor-discards-suffix-without-drawing')
        before = t.exchange(b'\x01|c0F|R0A0A1E1E|S010C\r\n')
        frame = t.exchange(b'\x01|F0K0K0F|F|Ffree form?\x1b[!\r\n')
        assert frame.primary == before.primary and frame.alternate == before.alternate
        frame = t.exchange(b'\x01|F\\|c02|X0K0K|F\\\\\r\nignored|c04|X0L0L\r\n'
                           + query(b'alive', at_cursor=True), b'alive')
        assert pixel(frame, 20, 20) == 0x00aa00 and pixel(frame, 21, 21) == 0xaa0000
        assert pixel(frame, 22, 22) == 0 and pixel(frame, 10, 10) == 0xffffff
        assert frame.application['connected'] == 1
        t.case('rip3-zero-argument-erase-and-home-delimiters')
        frame = t.exchange(b'\x01|Eignored\\|c02|X0K0K|Hignored\\|g0203\r\n\x1b[6n', b'\x1b[4;3R')
        assert pixel(frame, 10, 10) == 0 and pixel(frame, 20, 20) == 0x00aa00
        assert frame.application['connected'] == 1
        t.case('rip3-fixed-numeric-suffix-preserves-pipe-with-live-field-widths')
        frame = t.exchange(b'\x01|n3|f0HS0DC|c04|L010010012010\0ignored\\|c02|X011011\r\n')
        assert pixel(frame, 36, 36) == 0xaa0000 and pixel(frame, 37, 37) == 0x00aa00
        frame = t.exchange(b'\x01|n2|M18|c0Fy0suffix\\|X1313|M08|c04|X1414\r\n')
        assert pixel(frame, 39, 39) == 0x00ff00 and pixel(frame, 40, 40) == 0xaa0000
        assert frame.application['connected'] == 1
        t.case('rip3-malformed-numeric-prefix-preserves-following-drawing')
        frame = t.exchange(b'\x01|1p0a00bad\\|c02|X1616|P010000 \x1b[!\\|c0C|X1717\r\n'
                           + query(b'alive', at_cursor=True), b'alive')
        assert pixel(frame, 42, 42) == 0x00aa00 and pixel(frame, 43, 43) == 0xff5555
        assert frame.application['connected'] == 1
        t.case('rip3-command-starts-between-raw-text-bytes')
        frame = t.exchange(b'\x1b[12;5HX\0!|n3\r\n' + query(b'$COORDSIZE$', at_cursor=True)
                           + b'X\\' + query(b'SOH', at_cursor=True)
                           + b'Y\\\x02|1\x1b0000STX\r\n', b'2SOHSTX')
        assert frame.application['connected'] == 1
        t.case('rip3-deep-key-and-corrupt-line-recover-at-chaining-pipe')
        frame = t.exchange(b'\x01|123L00000000|c02|X1818|1234567891X1919|L00\x1b[!|c0C|X1A1A\r\n'
                           + query(b'alive', at_cursor=True), b'alive')
        assert pixel(frame, 44, 44) == 0x00aa00 and pixel(frame, 46, 46) == 0xff5555
        assert pixel(frame, 45, 45) == 0 and frame.application['connected'] == 1
        t.case('rip3-reserved-opcodes-skip-their-payload-and-resume-drawing')
        frame = t.exchange(b'\x01|1N|1O|1S|\x1b[!|$COORDSIZE(CUR,3)$|c03|X1B1B\r\n'
                           + query(b'$COORDSIZE$:alive', at_cursor=True), b'2:alive')
        assert pixel(frame, 47, 47) == 0x00aaaa and frame.application['connected'] == 1
        t.case('rip3-early-numeric-terminator-with-live-coordinate-width')
        frame = t.exchange(b'\x01|n3|c2<|L18<19<1B<19<|X19<1A<|n2\r\n'
                           + query(b'$COORDSIZE$:alive', at_cursor=True), b'2:alive')
        assert all(pixel(frame, x, 45) == 0x00aa00 for x in range(44, 48))
        assert pixel(frame, 43, 45) == 0 and pixel(frame, 45, 46) == 0x00aa00
        t.exchange(b'\x01|E|Y00000100|c0F\r\n')
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

        t.case('rip3-host-and-graphical-conditions-use-wire-byte-order')
        # CP437 orders ü (81) before é (82). Their UTF-8 byte order is reversed.
        low,high=(b'\x81',b'\x82') if args.encoding=='CP437' else ('ü'.encode(),'é'.encode())
        values(b'[Q:]a'+low)
        comparison=b'<<IF ?Q<"A'+high+b'">>Y<<ELSE>>NO<<ENDIF>>'
        expected=b'Y' if args.encoding=='CP437' else b'NO'
        values(comparison,expected)
        t.exchange(b'\x01|m0000|T'+comparison+b'\r\n'+query(b'$TEXTXY$',at_cursor=True),
                   f'{8*len(expected)}:0'.encode())

        t.case('rip3-conditional-and-angle-wire-byte-limits')
        wire_glyph = b'\xb3' if args.encoding == 'CP437' else '│'.encode()
        bound = wire_glyph * (255 // len(wire_glyph))
        values(b'[Q:]' + bound)
        literal = b'"' + bound[:-len(wire_glyph)] + b'\\' + wire_glyph + b'"'
        values(b'<<IF ?Q=' + literal + b'>>equal<<ELSE>>different<<ENDIF>>', b'equal')
        # A lookup failure is a false comparison even with the != operator.
        values(b'[Q:]' + bound + b'X')
        values(b'<<IF ?Q!=' + literal + b'>>different<<ELSE>>false<<ENDIF>>', b'false')
        values(b'<<IF ?Q="' + bound + b'X">>too long<<ENDIF>>')
        values(b'$-=OPERAND=old$')
        reference = b'OPERAND@' + wire_glyph * (247 // len(wire_glyph))
        values(b'<<IF $' + reference + b'$=old>>left<<ENDIF>>:<<' + reference + b'>>', b'left:old')
        values(b'<<' + reference + b'XXXX>>')
        text = b'<<IF $' + reference + b'$=old>>A<<ENDIF>>'
        t.exchange(b'\x01|m0000|T' + text + b'\r\n' + query(b'$TEXTXY$', at_cursor=True), b'8:0')

        t.case('rip3-graphical-position-prefix-survives-setter')
        t.exchange(b'\x01|@.1E$COORDSIZE(CUR,5)$A\r\n'
                   +query(b'$TEXTXY$:$COORDSIZE$',at_cursor=True),b'0:50:5')
        values(b'$COORDSIZE(CUR,2)$')
        t.case('rip3-graphics-move-ignores-surplus-after-complete-fields')
        t.exchange(b'\x01|m0A. ignored|m.1-|@.1-$COORDSIZE(CUR,5)$A\r\n'
                   +query(b'$TEXTXY$:$COORDSIZE$',at_cursor=True),b'10:0:2')
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

        mixed_shape_composition(t)
        bold_scan(t)
        skewed_curves(t)
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

        def font(name, size=22, spacing=100, flags=32, baseline=0, short=False):
            fields = ((0, 1), (0, 1), (flags, 4), (size, 2), (0, 2), (0, 2),
                      (0, 2), (baseline, 2), (spacing, 2), (100, 2), (0, 6))
            return b'y' + b''.join(b'.' if short and value == 0 else number(value, width, 64)
                                  for value, width in fields) + name

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
                                font(b'$&FONT_NAME$', short=True), text(75), font(b'Marin', 90),
                                font(b'<<?3>>'), text(120))
                           + query(b'$TEXTXY$', at_cursor=True), b'133:100')
        reference = glyph(frame, 30)
        assert reference and glyph(frame, 75) == reference and glyph(frame, 120) == reference

        t.case('Bookmark variables remain unavailable in queries, text and font names')
        frame = t.exchange(line(b'1D00500Password:foreign', b'1D00000USERNAME:?Enter credentials',
                                font(b'Marin', 90), font(b'Dix$USERNAME$on'),
                                b'@' + number(145, 2) + b'2SF<<PASSWORD>>')
                           + query(b'$-+=USERNAME=foreign$$RESET(TV,Password)$'
                                   b'$INUSE(TV,username)$:$INUSE(TV,PASSWORD)$:'
                                   b'$&Password=fallback$:alive:$TEXTXY$', at_cursor=True),
                           b'0:0::alive:158:100')
        assert glyph(frame, 145) == reference and frame.application['connected'] == 1

        t.case('Invalid and protected font commands skip evaluation and preserve the selected face')
        setter = b'$COORDSIZE(CUR,5)$Marin'
        t.exchange(line(font(setter, spacing=0), font(b'', short=True),
                        b'y..000W0M....1a.00.000' + setter))
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
        send(line(font(b'<<FACEPROMPT>>', short=True), text(260)) + query(b'$TEXTXY$', at_cursor=True))
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

        t.case("rip3-palette-and-style-switch-field-boundaries")
        frame = t.exchange(line(b"*", b"fHSDC", b"2A1. note", b"a0C10", b"c0C",
                                geometry(b"X", 10, 10), b"2Y1. note", b"c02",
                                geometry(b"X", 11, 10), b"2Y..", geometry(b"X", 12, 10))
                           + query(b"$CUR(PAL)$:$CUR(STYLE)$", at_cursor=True), b"1:0")
        assert [pixel(frame, x, 10) for x in range(10, 13)] == [0xff0000, 0x00aa00, 0xff0000]
        frame = t.exchange(line(b"2A00. invalid", b"2A000 note")
                           + query(b"$CUR(PAL)$:alive", at_cursor=True), b"0:alive")
        assert [pixel(frame, x, 10) for x in range(10, 13)] == [0xff5555, 0x00aa00, 0xff5555]

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

        t.case("rip3-counted-polygon-continuation-keeps-the-declared-vertex-count")
        frame = t.exchange(line(b"E", b"S0102", b"N00", b"p040A0A2S0A2S0M0A\\\r\n0M",
                                b"S0104", b"p04000A000A0M0M2S0M2S000A00\\\r\n0A",
                                b"c0F", b"X2T0G") + query(b"alive", at_cursor=True), b"alive")
        assert pixel(frame, 50, 16) == 0x00aa00
        assert pixel(frame, 101, 16) == 0xffffff
        assert all(pixel(frame, x, y) != 0xaa0000 for y in range(24) for x in range(102))

        t.case("rip3-typed-bezier-continuation-and-recovery")
        frame = t.exchange(line(b"c02", b"t02.\\\r\n0." + number(131, 2) + b"0"
                                + number(14, 2) + number(131, 2) + b"\\",
                                b"z0200.010100606\\", geometry(b"X", 16, 131))
                           + query(b"alive", at_cursor=True), b"alive")
        assert pixel(frame, 7, 131) == pixel(frame, 16, 131) == 0x00aa00
        assert pixel(frame, 15, 131) == 0 and frame.application['connected'] == 1

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
