#!/usr/bin/env python3
"""Independent glyph and placement vectors in connected RIPv3 text regions."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path
import re
import socket

from st_migrated_control_test import connection
from st_rip3_button_styles_connected_test import number
from st_rip3_connected_test import query
from st_rip3_mouse_connected_test import field
from st_rip3_scroll_connected_test import plane_pixel
from st_terminal_conformance_test import Terminal


# Independent pixel-center samples of the bundled DEFAULT.RFF contours at
# 11 points, 72 DPI, 140% X scale, baseline eight pixels below the line top.
# The input à selects the outline in raw byte slot 0x85.
CAPITAL_A = bytes.fromhex("00 30 6c c6 fe c6 c6 c6 00 00 00")
ACCENT = bytes.fromhex("36 36 36 36 36 00 00 00 00 00 00")
DOLLAR = bytes.fromhex("18 fc c2 c0 7e 06 86 fc 18 00 00")
AMPERSAND = bytes.fromhex("00 78 6c 78 fe cc cc fe 00 00 00")
CAPITAL_V = bytes.fromhex("00 c6 c6 c6 c6 c6 6c 38 00 00 00")
BACKSLASH = bytes.fromhex("00 80 c0 e0 78 3c 0e 06 00 00 00")
PIPE = bytes.fromhex("00 18 18 18 00 18 18 18 00 00 00")
PERIOD = bytes.fromhex("00 00 00 00 00 00 00 18 00 00 00")
RED = 0xAA0000
GREEN = 0x00AA00


def region(area, column=0, article=1):
    return b"1T" + b"".join(number(value, 2) for value in area) + number(column, 1) + number(article, 1)


def extended(area, column, article, flags, color=0, words=b"", *, reserved=b"00000000"):
    return b"1e" + region(area, column, article)[2:] + number(flags, 4) + number(color, 2) + reserved + words


def select(article, reserved=b"0000"):
    return b"1A" + number(article, 2) + reserved


def commands(*parts):
    return b"\x01|" + b"|".join(parts) + b"\r\n"


def check_pixels(frame, bounds, placements):
    expected = {}
    for x, y, mask, color in placements:
        for dy, row in enumerate(mask):
            for dx in range(8):
                if row & (0x80 >> dx):
                    expected[x + dx, y + dy] = color
    x0, y0, x1, y1 = bounds
    for phase in ("primary", "alternate"):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                actual = plane_pixel(frame, x, y, phase)
                assert actual == expected.get((x, y), 0), (phase, x, y, actual, expected.get((x, y), 0))


def check_default_outline_columns(args):
    with connection(args.executable, args.reports, encoding=args.encoding,
                    options="ScreenMode=Custom\nRIP=RIPv3\n",
                    settings="CustomColumns=128\nCustomRows=96\nCustomFontHeight=8\nAudioModes=0\n") as (
                        client, root, peer, send):
        terminal = Terminal(client, root, peer, send)
        terminal.exchange(b"\x1b[!", b"RIPSCRIP030001")
        terminal.case("DEFAULT EX prewrapped justification and inclusive bottom fit")
        # At identity scale, Q=11, P=109: seven pixels per byte, A=8,
        # D=2 and L=10. Width 22 leaves one extra pixel before the second A.
        setup = commands(b"*", b"fSGLC", b"c02")
        column = region((100, 100, 121, 120))
        frame = terminal.exchange(setup + commands(column, b"1t1A A", b"1t0", b"1t0A", b"1E"))
        check_pixels(frame, (98, 98, 125, 123), [(100, 100, CAPITAL_A, GREEN), (115, 100, CAPITAL_A, GREEN)])

        terminal.case("Column fields follow active XY and accept zero shorthand before literal text")
        frame = terminal.exchange(setup + commands(b"n3", b"1T.02S00L03C.1 note", b"1t.A", b"1E note", b"n2"))
        check_pixels(frame, (0, 98, 24, 123), [(0, 100, CAPITAL_A, GREEN)])

        terminal.case("DEFAULT EX raw width 22 fits and width 21 wraps as the next glyph arrives")
        frame = terminal.exchange(setup + commands(column) + b"A A\r\n" + commands(b"1E flush"))
        check_pixels(frame, (98, 98, 125, 123), [(100, 100, CAPITAL_A, GREEN), (114, 100, CAPITAL_A, GREEN)])
        terminal.exchange(setup)
        terminal.send(commands(region((100, 100, 120, 120))) + b"A ")
        accent = b"\x85" if args.encoding == "CP437" else "à".encode("utf-8")
        for byte in accent:
            terminal.send(bytes([byte]))
        frame = client.wait_for(lambda f: plane_pixel(f, 102, 101, "primary") == GREEN)
        check_pixels(frame, (98, 98, 125, 123), [(100, 100, CAPITAL_A, GREEN)])
        frame = terminal.exchange(commands(b"1E"))
        check_pixels(frame, (98, 98, 125, 123), [(100, 100, CAPITAL_A, GREEN), (100, 110, ACCENT, GREEN)])

        terminal.case("DEFAULT EX raw punctuation contributes two measured spaces")
        frame = terminal.exchange(setup + commands(region((100, 100, 135, 120)))
                                  + b"A. A\r\n" + commands(b"1E"))
        check_pixels(frame, (98, 98, 138, 123), [(100, 100, CAPITAL_A, GREEN),
                     (107, 100, PERIOD, GREEN), (128, 100, CAPITAL_A, GREEN)])
        terminal.exchange(setup)
        terminal.send(commands(region((100, 100, 127, 120))) + b"A. ")
        frame = client.wait_for(lambda f: plane_pixel(f, 102, 101, "primary") == GREEN)
        check_pixels(frame, (98, 98, 132, 123), [(100, 100, CAPITAL_A, GREEN), (107, 100, PERIOD, GREEN)])
        frame = terminal.exchange(b"A\r\n" + commands(b"1E"))
        check_pixels(frame, (98, 98, 132, 123), [(100, 100, CAPITAL_A, GREEN),
                     (107, 100, PERIOD, GREEN), (100, 110, CAPITAL_A, GREEN)])

        terminal.case("DEFAULT EX raw paragraph skips a blank at a disjoint continuation")
        frame = terminal.exchange(setup + commands(region((100, 100, 121, 110)),
                                                    region((140, 100, 161, 120), column=1))
                                  + b"A\r\n\r\nA\r\n" + commands(b"1E"))
        check_pixels(frame, (98, 98, 164, 123), [(100, 100, CAPITAL_A, GREEN), (140, 100, CAPITAL_A, GREEN)])

        terminal.case("Extended columns retain eight search words and reject invalid replacements")
        words = b" AAAAAAAAAAAAAAAA B C D E F G a V "
        column = extended((100, 100, 260, 180), 0, 1, 2, 4, words)
        other = (270, 100, 430, 180)
        replacements = (
            extended(other, 0, 2, 4),
            extended(other, 0, 2, 2, 256, b"A"),
            extended(other, 0, 2, 2, 4),
            extended(other, 0, 2, 2, 4, b"  "),
            extended(other, 0, 2, 2, 4, b"A" * 17),
        )
        frame = terminal.exchange(setup + commands(column, b"1t0" + b"A" * 16, b"1t0A", b"1t0V",
                                  *replacements, b"1t0A", b"1E"))
        check_pixels(frame, (98, 98, 432, 143),
                     [(100 + 7 * i, 100, CAPITAL_A, RED) for i in range(16)]
                     + [(100, 110, CAPITAL_A, RED), (100, 120, CAPITAL_V, GREEN),
                        (100, 130, CAPITAL_A, RED)])

        terminal.case("Extended column protection and ignored highlight operands")
        port = b"2P10000" + number(1023, 2) + number(767, 2) + b"0"
        frame = terminal.exchange(setup + commands(column, port, b"2s100")
                                  + query(b"$PROT(PORT,1)$", at_cursor=True)
                                  + commands(extended(other, 0, 2, 0, 256, b"A"), b"2s000",
                                             b"1t0A", b"1E"))
        check_pixels(frame, (98, 98, 432, 111), [(100, 100, CAPITAL_A, RED)])
        frame = terminal.exchange(commands(extended(other, 0, 2, 0, 256, b"A"), b"1t0A", b"1E"))
        check_pixels(frame, (268, 98, 432, 111), [(270, 100, CAPITAL_A, GREEN)])


def check_overflow(terminal, root, accent):
    owner, = (root / "cache").glob("*/syncterm-cache-name.txt")
    cache = owner.parent / "RIP"
    cache.mkdir(exist_ok=True)

    def values(program, expected=b""):
        return terminal.exchange(query(program, at_cursor=True), expected)

    def column(x, article=13, flags=1):
        return extended((x, 300, x + 7, 310), article, article, flags)

    terminal.case("Overflow retains the original source and first undisplayed word")
    (cache / "PAGING.TXT").write_bytes(b"A\r\n\r\n" + accent + b" A")
    first = extended((30, 300, 37, 310), 13, 13, 1, reserved=b"1234ZAZZ")
    frame = terminal.exchange(commands(b"c02", first, b"1R00000000PAGING.TXT", b"1E"))
    check_pixels(frame, (29, 299, 38, 311), [(30, 300, CAPITAL_A, GREEN)])
    values(b"$OVERFLOW(13,PAGE)$:$OVERFLOW(13,CUR)$:$OVERFLOW(13,NEXT,QUERY)$:$OVERFLOW(13,PREV,QUERY)$",
           re.compile(rb"1:(\d{3,5})D0000\.OVR:\1D0001\.OVR:\1D0001\.OVR"))

    terminal.case("A pager field runs an HCL continuation in the live connection")
    (cache / "SECOND.COL").write_bytes(commands(column(50), b"1R00000000$OVERFLOW(13)$", b"1E"))
    frame = terminal.exchange(field((30, 340, 60, 365),
                                    b"$OVERFLOW(13,NEXT,SET)$$>SECOND.COL$$OVERFLOW(13,PAGE)$"))
    metadata = frame.metadata
    x = metadata["destination_x"] + 45 * metadata["destination_width"] // metadata["plane_width"]
    y = metadata["destination_y"] + 350 * metadata["destination_height"] // metadata["plane_height"]
    terminal.client.mouse(x, y, action=1, buttons=1)
    terminal.client.mouse(x, y, action=1, buttons=0)
    frame = terminal.input(b"2")
    check_pixels(frame, (49, 299, 58, 311), [(50, 300, ACCENT, GREEN)])
    values(b"$OVERFLOW(13,NEXT,)$$OVERFLOW()$", re.compile(rb"\d{3,5}D0002\.OVR"))
    filename = re.search(rb"\d{3,5}D0002\.OVR", (root / "replies.bin").read_bytes()).group()
    frame = terminal.exchange(commands(column(70)) + query(b"$>" + filename + b"$", at_cursor=True)
                              + commands(b"1E"))
    check_pixels(frame, (69, 299, 78, 311), [(70, 300, CAPITAL_A, GREEN)])
    values(b"$OVERFLOW(13,PAGE,QUERY)$:$OVERFLOW(13,NEXT,QUERY)$", b"3:<NONE>")

    terminal.case("A boundary discards the evaluator's reply and skips the following scene")
    (cache / "UNREACHED.COL").write_bytes(commands(column(90), b"1t0A", b"1E"))
    frame = values(b"discard$OVERFLOW(13,NEXT,SETVERBOSE)$$>UNREACHED.COL$discard")
    check_pixels(frame, (89, 299, 98, 311), [])
    values(b"$OVERFLOW(13,PAGE)$:alive", b"3:alive")
    values(b"$OVERFLOW(13,0)$")
    values(b"discard$OVERFLOW(13,PREV)$discard")
    frame = terminal.exchange(commands(column(110), b"1R00000000$OVERFLOW(13,CUR,QUERY)$", b"1E"))
    check_pixels(frame, (109, 299, 118, 311), [(110, 300, CAPITAL_A, GREEN)])
    values(b"$OVERFLOW(13,2)$$OVERFLOW(13,PAGE)$", b"1")
    values(b"$OVERFLOW(13,NEXT)$$OVERFLOW(13,PAGE)$", b"2")

    terminal.case("Article, drawing and overflow resets preserve their distinct state")
    terminal.exchange(commands(column(130, 14)) + b"A A\r\n" + commands(b"#"))
    values(b"$OVERFLOW(13,RESET)$$OVERFLOW(13,PAGE)$:$OVERFLOW(13)$:$OVERFLOW(14,NEXT,QUERY)$",
           re.compile(rb"1:<NONE>:\d{3,5}E0001\.OVR"))
    values(b"$OVERFLOW(14,NEXT)$")
    terminal.exchange(commands(b"*"))
    values(b"$OVERFLOW(14,PAGE)$:$OVERFLOW(14)$", b"2:<NONE>")
    values(b"$RESET(OVERFLOW)$$OVERFLOW(14,PAGE)$:$OVERFLOW()$", b"1:<NONE>")
    values(b"$WORLD(CUR,640,480)$")
    terminal.exchange(commands(column(150, 14)) + b"A A\r\n" + commands(b"1E"))
    values(b"$OVERFLOW(14,NEXT)$$RESET(HARD)$$OVERFLOW(14,PAGE)$:$OVERFLOW()$", b"1:<NONE>")
    assert list(cache.glob(".rip3-overflow-*.part"))
    return cache


def check_selection(terminal, accent):
    terminal.case("Selecting another article flushes prose and preserves both cursors")
    frame = terminal.exchange(
        commands(b"c02", region((195, 310, 211, 332), 15, 15),
                 region((220, 310, 236, 332), 16, 16), select(15, b"ZZZZ"))
        + b"A\r\n" + commands(select(16, b""))
        + accent + b"\r\n" + commands(select(15, b"0001 note"))
        + b"A\r\n" + commands(select(15, b"01"))
        + accent + b"\r\n" + commands(select(16, b"."))
        + b"A\r\n" + commands(b"1E"))
    check_pixels(frame, (192, 309, 229, 335),
                 [(195, 310, CAPITAL_A, GREEN), (195, 320, ACCENT, GREEN),
                  (220, 310, ACCENT, GREEN), (220, 320, CAPITAL_A, GREEN)])

    terminal.case("Article zero accepts prewrapped text and ordinary bytes after end")
    frame = terminal.exchange(
        commands(region((245, 310, 252, 342), 0, 0), b"1t0A", b"1E",
                 b"1t0" + accent, b"#", select(35), b"1t0" + accent)
        + b"\x1b[4;2HNO COL\x1b[6n\r\n" + commands(b"1A.", b"1t.A")
        + b"\x1b[3;2HOK", b"\x1b[4;8R")
    check_pixels(frame, (242, 309, 254, 344),
                 [(245, 310, CAPITAL_A, GREEN), (245, 320, ACCENT, GREEN), (245, 330, CAPITAL_A, GREEN)])
    assert frame.lines[2][1:3] == "OK"

    terminal.case("Invalid selectors retain the current selection and allow the next command")
    terminal.exchange(commands(extended((270, 310, 277, 320), 17, 17, 1), b"1E")
                      + query(b"$BASEMATH(CUR,64)$", at_cursor=True))
    reply = re.compile(rb"\d{3,5}H0000\.OVR:alive")
    terminal.exchange(
        commands(select(17, b"00#&"), b"1A0a0000", b"1A000.", b"1A00!!!!", b"1E")
        + query(b"$OVERFLOW()$:alive$BASEMATH(CUR,36)$", at_cursor=True), reply)

    terminal.case("Switching articles keeps each original overflow source open for more text")
    left = extended((310, 350, 317, 360), 18, 18, 1)
    right = extended((340, 350, 347, 360), 19, 19, 1)
    terminal.exchange(
        commands(left, right, select(18)) + b"A\r\n"
        + commands(select(19)) + accent + b"\r\n"
        + commands(select(18)) + accent + b"\r\n"
        + commands(select(19)) + b"A\r\n" + commands(b"1E", select(18), b"1E"))
    terminal.exchange(query(b"$OVERFLOW(18,NEXT,QUERY)$:$OVERFLOW(19,NEXT,QUERY)$", at_cursor=True),
                      re.compile(rb"(\d{3,5})I0001\.OVR:\1J0001\.OVR"))
    frame = terminal.exchange(commands(
        extended((310, 370, 334, 380), 18, 18, 1), b"1R00000000$OVERFLOW(18)$", b"1E",
        extended((340, 370, 364, 380), 19, 19, 1), b"1R00000000$OVERFLOW(19)$", b"1E"))
    check_pixels(frame, (309, 369, 364, 381),
                 [(310, 370, CAPITAL_A, GREEN), (326, 370, ACCENT, GREEN),
                  (340, 370, ACCENT, GREEN), (356, 370, CAPITAL_A, GREEN)])


def check_prewrapped(terminal, accent):
    terminal.case("Prewrapped justification preserves leading spaces and distributes signed padding")
    frame = terminal.exchange(commands(
        b"c02", region((370, 30, 410, 62), 20, 20),
        b"1t1A A A", b"1t1 A " + accent + b"  ", b"1t1" + accent,
        region((428, 30, 441, 40), 21, 21), b"1t1A A", b"1E"))
    check_pixels(frame, (367, 29, 444, 64),
                 [(370, 30, CAPITAL_A, GREEN), (386, 30, CAPITAL_A, GREEN),
                  (403, 30, CAPITAL_A, GREEN), (378, 40, CAPITAL_A, GREEN),
                  (394, 40, ACCENT, GREEN), (370, 50, ACCENT, GREEN),
                  (428, 30, CAPITAL_A, GREEN), (434, 30, CAPITAL_A, GREEN)])

    terminal.case("Prewrapped lines leave pending raw prose for the article end")
    frame = terminal.exchange(commands(region((370, 72, 393, 93), 22, 22)) + b"A\r\n"
                              + commands(b"1t0" + accent, b"1E"))
    check_pixels(frame, (368, 71, 395, 94), [(370, 72, ACCENT, GREEN), (370, 82, CAPITAL_A, GREEN)])

    terminal.case("Empty prewrapped lines advance except at the top of a disjoint continuation column")
    frame = terminal.exchange(commands(
        region((370, 105, 377, 115), 23, 23), region((390, 105, 397, 126), 24, 23),
        b"1t0A", b"1t0 ", b"1t0" + accent, b"1t0A", b"1E",
        region((420, 105, 427, 115), 25, 24), region((425, 110, 432, 131), 26, 24),
        b"1t0A", b"1t0", b"1t0" + accent, b"1E"))
    check_pixels(frame, (367, 103, 434, 132),
                 [(370, 105, CAPITAL_A, GREEN), (390, 105, ACCENT, GREEN),
                  (390, 115, CAPITAL_A, GREEN), (420, 105, CAPITAL_A, GREEN),
                  (425, 120, ACCENT, GREEN)])

    terminal.case("A prewrapped line advances to the next slot once when it will not fit")
    frame = terminal.exchange(commands(
        region((370, 145, 377, 151), 27, 25), region((390, 151, 397, 157), 28, 25),
        region((410, 170, 417, 191), 29, 25),
        b"1t0A", b"1t0" + accent, b"1E"))
    check_pixels(frame, (368, 144, 420, 192),
                 [(390, 151, CAPITAL_A, GREEN), (410, 170, ACCENT, GREEN)])

    terminal.case("A changed font supplies the next prewrapped line advance")
    frame = terminal.exchange(commands(
        region((370, 205, 401, 260), 30, 26), b"1t0A",
        b"Y00000200", b"1t0A", b"Y00000100", b"1t0" + accent, b"1E"))
    check_pixels(frame, (368, 236, 403, 251), [(370, 236, ACCENT, GREEN)])

    terminal.case("A protected graphics port suppresses a prewrapped line and leaves the cursor in place")
    frame = terminal.exchange(
        commands(b"2P10000" + number(639, 2) + number(479, 2) + b"0", b"2s100",
                 region((450, 30, 457, 51), 31, 27))
        + query(b"$PROT(PORT,1)$", at_cursor=True) + commands(b"1t0A")
        + query(b"$UNPROT(PORT,1)$", at_cursor=True)
        + commands(b"1t0" + accent, b"1E", b"2s000"))
    check_pixels(frame, (448, 29, 460, 53), [(450, 30, ACCENT, GREEN)])

    terminal.case("Prewrapped search compares the rendered string or justified runs")
    frame = terminal.exchange(commands(
        extended((460, 80, 499, 112), 32, 28, 2, 4, b"A"),
        b"1t0A A", b"1t1A A", b"1t0A", b"1E"))
    check_pixels(frame, (458, 79, 502, 114),
                 [(460, 80, CAPITAL_A, GREEN), (476, 80, CAPITAL_A, GREEN),
                  (460, 90, CAPITAL_A, RED), (492, 90, CAPITAL_A, GREEN),
                  (460, 100, CAPITAL_A, RED)])

    terminal.case("Outline columns use separate baseline, bottom fit and line advance")
    dixon = b"y0000000M000000001a1a000000Dixon"
    rotated = dixon[:11] + b"E4E4" + dixon[15:]
    frame = terminal.exchange(commands(
        rotated, region((430, 230, 461, 309), 33, 29), b"1t0F",
        b"Y00000100", b"1t0A", b"1E",
        dixon, region((490, 230, 521, 251), 34, 30), b"1t0F", b"1E",
        b"@" + number(550, 2) + number(246, 2) + b"F", b"Y00000100"))
    for phase in ("primary", "alternate"):
        expected = [plane_pixel(frame, 550+dx, y, phase)
                    for y in range(225, 257) for dx in range(-2, 26)]
        assert GREEN in expected
        for x in (430, 490):
            actual = [plane_pixel(frame, x+dx, y, phase)
                      for y in range(225, 257) for dx in range(-2, 26)]
            assert actual == expected, (phase, x)
    check_pixels(frame, (428, 282, 440, 298), [(430, 285, CAPITAL_A, GREEN)])


def check_prewrapped_contract(terminal, accent, encoding):
    terminal.case("Prewrapped text is literal after framing and strips backslashes once")
    terminal.exchange(query(b"$-=V=A$", at_cursor=True))
    frame = terminal.exchange(commands(
        b"c02", region((500, 28, 619, 60), 35, 31), b"1t0A$&V$" + accent,
        b"1t0A\\A A\\\\A A\\|A \t", b"1E"))
    check_pixels(frame, (499, 27, 620, 61),
                 [(500, 28, CAPITAL_A, GREEN), (508, 28, DOLLAR, GREEN),
                  (516, 28, AMPERSAND, GREEN), (524, 28, CAPITAL_V, GREEN),
                  (532, 28, DOLLAR, GREEN), (540, 28, ACCENT, GREEN),
                  (500, 38, CAPITAL_A, GREEN), (508, 38, CAPITAL_A, GREEN),
                  (524, 38, CAPITAL_A, GREEN), (532, 38, BACKSLASH, GREEN),
                  (540, 38, CAPITAL_A, GREEN), (556, 38, CAPITAL_A, GREEN),
                  (564, 38, PIPE, GREEN), (572, 38, CAPITAL_A, GREEN)])

    terminal.case("Inactive viewport and invalid or unsupported justification leave placement intact")
    invalid_encoding = (b"1t0\xc3",) if encoding == "UTF-8" else ()
    frame = terminal.exchange(commands(region((500, 85, 539, 117), 35, 32), b"v00000000", b"1t0A",
                                       b"v0000" + number(639, 2) + number(479, 2))
                              + query(b"$BASEMATH(CUR,64)$", at_cursor=True)
                              + commands(b"1t2A", b"1t#A", b"1t&A", b"1t?A", *invalid_encoding,
                                       b"1t0" + accent, b"1t0\\ \t", b"1t0A", b"1E")
                              + query(b"alive$BASEMATH(CUR,36)$", at_cursor=True), b"alive")
    check_pixels(frame, (499, 84, 540, 118),
                 [(500, 85, ACCENT, GREEN), (500, 105, CAPITAL_A, GREEN)])

    terminal.case("Classic stroke top adjustment, inclusive fit, and line step are independent")
    # Original LITT.CHR has cap=7, descent=-2 and a pipe from (0,6) to (0,0).
    # Size 2 is 2/3: A=6, D=1, K=2, L=7. The first two fitted references
    # are 134 and 141. At a column top of 130 the fit thresholds are 135 and 142.
    frame = terminal.exchange(commands(
        b"Y02010200", region((510, 130, 519, 142), 34, 33), region((535, 130, 544, 141), 35, 33),
        b"1t0\\|", b"1t0\\|", b"1t0\\|", b"1t0\\|", b"1E", b"Y00000100"))
    stroke = b"\x80" * 5
    check_pixels(frame, (508, 128, 548, 148),
                 [(510, 130, stroke, GREEN), (510, 137, stroke, GREEN), (535, 130, stroke, GREEN)])

    terminal.case("Changing font preserves a continuation column's recorded first line")
    frame = terminal.exchange(commands(
        region((510, 174, 519, 184), 34, 34), region((535, 174, 544, 192), 35, 34),
        b"1t0A", b"Y02000200", b"1t0\\|", b"1E", b"Y00000100"))
    check_pixels(frame, (508, 172, 548, 193), [(510, 174, CAPITAL_A, GREEN), (535, 176, stroke, GREEN)])

    terminal.case("Classic strokes project WORLD metrics and justify in the device column")
    # Half scale gives A=3, D=0, K=1, L=3, with a two-pixel stroke above each
    # reference. The three glyph advances measure four device pixels in a
    # ten-pixel column, so the second pipe shifts six pixels to the right.
    frame = terminal.exchange(query(b"$WORLD(CUR,1280,960)$", at_cursor=True)
                              + commands(b"Y02000200", region((1020, 420, 1038, 430), 35, 35),
                                         b"1t1\\| \\|", b"1t1\\| \\|", b"1t0\\|", b"1E", b"Y00000100")
                              + query(b"$WORLD(CUR,640,480)$", at_cursor=True))
    stroke = b"\x80" * 3
    check_pixels(frame, (508, 208, 522, 218),
                 [(510, 210, stroke, GREEN), (519, 210, stroke, GREEN),
                  (510, 213, stroke, GREEN), (519, 213, stroke, GREEN)])

    terminal.case("Prewrapped exhaustion leaves overflow available for subsequent prose")
    frame = terminal.exchange(commands(extended((595, 175, 602, 185), 35, 35, 1),
                                       b"1t1AA", b"1t0A", b"1E")
                              + query(b"$OVERFLOW(35,NEXT,QUERY)$", at_cursor=True), b"<NONE>")
    check_pixels(frame, (594, 174, 611, 186), [(595, 175, CAPITAL_A, GREEN), (603, 175, CAPITAL_A, GREEN)])
    frame = terminal.exchange(commands(region((595, 190, 602, 200), 35, 35)) + b"A A\r\n!|1E\r\n"
                              + query(b"$OVERFLOW(35,NEXT,QUERY)$", at_cursor=True),
                              re.compile(rb"\d{3,5}Z0001\.OVR"))
    check_pixels(frame, (594, 189, 611, 201), [(595, 190, CAPITAL_A, GREEN)])


def run(args):
    with connection(args.executable, args.reports, encoding=args.encoding,
                    options="ScreenMode=Custom\nRIP=RIPv3\n",
                    settings="CustomColumns=80\nCustomRows=60\nCustomFontHeight=8\nAudioModes=0\n") as (
                        client, root, peer, send):
        terminal = Terminal(client, root, peer, send)

        def wire(*parts):
            return terminal.exchange(commands(*parts))

        accent = b"\x85" if args.encoding == "CP437" else "à".encode("utf-8")
        terminal.exchange(b"\x1b[!", b"RIPSCRIP030001")
        terminal.exchange(query(b"$WORLD(CUR,640,480)$", at_cursor=True))

        terminal.case("RIPv3 region normalization, full justification and end")
        frame = wire(b"c04", region((59, 61, 20, 40)), b"1t1A " + accent,
                     b"c02", b"1t0" + accent, b"1t0A", b"1E", b"1t0A")
        check_pixels(frame, (17, 36, 63, 75),
                     [(20, 40, CAPITAL_A, RED), (52, 40, ACCENT, RED), (20, 50, ACCENT, GREEN)])

        terminal.case("RIPv3 columns visit numeric slots and skip holes")
        frame = wire(region((70, 110, 77, 120), 17, 2), region((20, 110, 27, 120), 3, 2),
                     b"c04", b"1t0A", b"c02", b"1t0" + accent, b"1t0A", b"1E")
        check_pixels(frame, (16, 105, 81, 134), [(20, 110, CAPITAL_A, RED), (70, 110, ACCENT, GREEN)])

        terminal.case("RIPv3 region syntax recovery and scene end")
        frame = wire(region((20, 145, 27, 155), 4, 3), b"1T00", b"1t2A", b"1t0A", b"#", b"1t0A")
        check_pixels(frame, (16, 141, 32, 168), [(20, 145, CAPITAL_A, GREEN)])
        terminal.exchange(query(b"alive", at_cursor=True), b"alive")

        terminal.case("Prewrapped ink uses absolute column anchors and the active viewport clip")
        viewport = b"v" + b"".join(number(value, 2) for value in (23, 160, 94, 194))
        marker = b"X" + number(23, 2) + number(160, 2)
        frame = wire(viewport, region((20, 170, 25, 180), 5, 4), b"1t0AAAA", b"1E", b"c04", marker)
        check_pixels(frame, (17, 167, 54, 184),
                     [(20, 170, bytes(row & 0x1F for row in CAPITAL_A), GREEN),
                      (28, 170, CAPITAL_A, GREEN), (36, 170, CAPITAL_A, GREEN), (44, 170, CAPITAL_A, GREEN)])
        assert plane_pixel(frame, 23, 160, "primary") == RED

        full_view = b"v0000" + number(639, 2) + number(479, 2)
        terminal.case("Raw article text wraps across slots and returns to the terminal at end")
        frame = terminal.exchange(commands(full_view, region((170, 40, 194, 50), 31, 6),
                                           region((130, 40, 154, 50), 27, 6), b"c02")
                                  + b"A\r\nA " + accent + b"\r\n!|1E\r\n\x1b[3;2HOK")
        check_pixels(frame, (126, 39, 196, 54),
                     [(130, 40, CAPITAL_A, GREEN), (146, 40, CAPITAL_A, GREEN), (170, 40, ACCENT, GREEN)])
        assert frame.lines[2][1:3] == "OK"

        terminal.case("Raw CRLF paragraphs and same-article redefinition")
        paragraph = region((130, 75, 177, 107), 0, 7)
        frame = terminal.exchange(commands(paragraph) + b"A\r\n" + commands(paragraph)
                                  + accent + b"\r\n\r\nA\r\n!|#\r\n")
        check_pixels(frame, (126, 72, 181, 110), [(130, 75, ACCENT, GREEN), (130, 95, CAPITAL_A, GREEN)])

        terminal.case("Changing articles flushes the old article's pending text")
        frame = terminal.exchange(commands(region((130, 130, 153, 140), 1, 10)) + b"A\r\n"
                                  + commands(region((161, 130, 184, 140), 2, 11)) + accent + b"\r\n!|1E\r\n")
        check_pixels(frame, (126, 128, 187, 142), [(130, 130, CAPITAL_A, GREEN), (161, 130, ACCENT, GREEN)])

        terminal.case("A cached TXT scene executes RIP before routing ordinary bytes into its article")
        shared = root / "RIP"
        shared.mkdir(exist_ok=True)
        (shared / "Story.txt").write_bytes(b"!|c04\r\nA")
        frame = wire(extended((195, 100, 250, 121), 0, 8, 0), b"1R00000000STORY.TXT", b"1E",
                     b"X" + number(193, 2) + number(97, 2))
        assert plane_pixel(frame, 193, 97, "primary") == RED
        check_pixels(frame, (194, 99, 251, 122), [(195, 100, CAPITAL_A, RED)])

        terminal.case("The scene macro uses the same framing and leaves the article open")
        (shared / "Accent.txt").write_bytes(b"!|c02\n" + accent)
        frame = terminal.exchange(commands(extended((195, 150, 250, 171), 0, 8, 0))
                                  + query(b"$>Accent.TXT$ready", at_cursor=True)
                                  + commands(b"1E", b"X" + number(189, 2) + number(147, 2)), b"ready")
        assert plane_pixel(frame, 189, 147, "primary") == GREEN
        check_pixels(frame, (191, 146, 254, 174), [(195, 150, ACCENT, GREEN)])

        terminal.case("Extended columns expand search words and highlight complete tokens")
        terminal.exchange(query(b"$-=FIND=a$", at_cursor=True))
        actual = extended((300, 120, 379, 141), 5, 9, 2, 4, b"  $&FIND$  ", reserved=b".")
        malformed = extended((300, 170, 379, 191), 5, 9, 2, 4, b"A", reserved=b"0000.")
        invalid = extended((300, 170, 379, 191), 5, 9, 2, 4, b"ABCDEFGHIJKLMNOPQ")
        frame = terminal.exchange(commands(actual, malformed) + b"A A" + accent + b" "
                                  + commands(invalid) + b"A\r\n!|1E\r\n")
        check_pixels(frame, (297, 118, 381, 144),
                     [(300, 120, CAPITAL_A, RED), (316, 120, CAPITAL_A, GREEN),
                      (324, 120, ACCENT, GREEN), (340, 120, CAPITAL_A, RED)])
        check_pixels(frame, (297, 168, 381, 194), [])

        terminal.case("Legacy definitions retain the slot's search list")
        frame = terminal.exchange(commands(region((300, 200, 379, 210), 5, 12)) + b"A\r\n!|1E\r\n")
        check_pixels(frame, (297, 197, 382, 213), [(300, 200, CAPITAL_A, RED)])

        cache = check_overflow(terminal, root, accent)
        terminal.exchange(query(b"$WORLD(CUR,640,480)$", at_cursor=True))
        check_selection(terminal, accent)
        check_prewrapped(terminal, accent)
        check_prewrapped_contract(terminal, accent, args.encoding)

        terminal.case("RIPv3 regions use the selected outline font and preserve the drawing cursor")
        wire(full_view, b"m8C8C", b"c04",
             b"y00000W0M000000001a1a000000Dixon", region((110, 200, 190, 279), 5, 5),
             b"1t0F", b"1E")
        frame = terminal.exchange(query(b"$TEXTXY$", at_cursor=True), b"300:300")
        assert any(plane_pixel(frame, x, y, "primary") == RED
                   for y in range(200, 280) for x in range(110, 191))
        for phase in ("primary", "alternate"):
            assert all(plane_pixel(frame, x, y, phase) == 0 for x, y in ((109, 200), (191, 200), (110, 199)))

        terminal.case("Disconnect removes only the session's overflow files")
        peer.shutdown(socket.SHUT_WR)
        client.wait_for(lambda s: s.application.get("session_active") == 0)
        assert not list(cache.glob(".rip3-overflow-*.part"))
        assert (cache / "PAGING.TXT").exists()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("encoding", choices=("CP437", "UTF-8"))
    parser.add_argument("--reports", type=Path, required=True)
    args = parser.parse_args()
    check_default_outline_columns(args)
    run(args)
