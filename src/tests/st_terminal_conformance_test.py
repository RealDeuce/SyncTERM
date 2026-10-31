#!/usr/bin/env python3
"""Independent ANSI-BBS conformance checks through the real connection and renderer.

The checked-in termtest manifest documents correspondence, not an oracle. Expected
text, coordinates, colours and replies below come from docs/cterm.adoc. A status
reply fences remote parsing before one coherent cell/pixel/state capture; it does
not move the cursor or consume pending wrap. No installed BBS is required.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import binascii
import json
from pathlib import Path
import random
import re
import struct
import time

from st_migrated_control_test import connection
from syncterm_control import SHIFT, CONTROL, ALT
from syncterm_fixture import cell_at, click_cell

ESC = b"\x1b"
ST = ESC + b"\\"
FENCE = ESC + b"[0n"
RESET = ESC + b"c" + ESC + b"[0$~" + ESC + b"[2J" + ESC + b"[H"
MANIFEST = Path(__file__).with_name("terminal_conformance.json")


def cup(x, y):
    return f"\x1b[{y};{x}H".encode()


def colour(frame, cell, background=False):
    value = cell.background if background else cell.foreground
    return (value if value & 0x80000000 else frame.palette[value]) & 0xffffff


def tile(frame, x=1, y=1, alternate=False):
    m = frame.metadata
    plane = frame.alternate if alternate else frame.primary
    left, top = (x - 1) * m["cell_width"], (y - 1) * m["cell_height"] + m["pixel_top"]
    return tuple(plane[(top + row) * m["plane_width"] + left + col]
                 for row in range(m["cell_height"]) for col in range(m["cell_width"]))


class Terminal:
    def __init__(self, client, root, peer, send):
        self.client, self.root, self.peer, self.send = client, root, peer, send
        self.completed = set()
        self.current = "startup"

    def case(self, name):
        self.current = name
        print(name, flush=True)
        with (self.root / "cases.txt").open("a", encoding="utf-8") as stream:
            stream.write(name + "\n")
        self.completed.add(name)

    def exchange(self, data=b"", expected=b"", *, reset=False):
        self.send((RESET if reset else b"") + data + ESC + b"[5n")
        # The terminator is a complete response. Compare *all* bytes, retaining
        # unexpected/duplicate replies rather than searching for a substring.
        received = bytearray()
        deadline = time.monotonic() + 10
        fences = 1 + (expected.count(FENCE) if isinstance(expected, bytes) else 0)
        while not received.endswith(FENCE) or received.count(FENCE) < fences:
            self.peer.settimeout(max(0.001, deadline - time.monotonic()))
            try:
                part = self.peer.recv(65536)
            except TimeoutError as error:
                raise AssertionError((self.current, "reply timeout", bytes(received), data.hex())) from error
            assert part, (self.current, "connection closed", received)
            received.extend(part)
            with (self.root / "replies.bin").open("ab") as stream:
                stream.write(part)
            assert len(received) < 65536, (self.current, "unbounded reply")
        actual = bytes(received[:-len(FENCE)])
        if isinstance(expected, re.Pattern):
            assert expected.fullmatch(actual), (self.current, actual, expected.pattern)
        else:
            assert actual == expected, (self.current, actual, expected)
        return self.client.capture()

    def check(self, name, data, xy=None, lines=(), *, reset=True, expected=b""):
        self.case(name)
        frame = self.exchange(data, expected, reset=reset)
        if xy:
            actual = frame.application["cterm.x"], frame.application["cterm.y"]
            assert actual == xy, (name, actual, xy)
        for x, y, text in lines:
            actual = "".join(frame.cell_text(cell_at(frame, col - 1, y - 1))
                             for col in range(x, x + len(text)))
            assert actual == text, (name, x, y, actual, text)
        return frame

    def input(self, expected):
        # Input acceptance queues an event; it does not acknowledge transmission.
        # Wait for its complete wire output before sending a parsing fence, so
        # connection input cannot overtake a queued keyboard/mouse event.
        received = bytearray()
        deadline = time.monotonic() + 10
        while len(received) < len(expected):
            self.peer.settimeout(max(0.001, deadline - time.monotonic()))
            try:
                part = self.peer.recv(len(expected) - len(received))
            except TimeoutError as error:
                raise AssertionError((self.current, "input timeout", bytes(received), expected)) from error
            assert part, (self.current, "connection closed", received)
            received.extend(part)
            with (self.root / "replies.bin").open("ab") as stream:
                stream.write(part)
        assert bytes(received) == expected, (self.current, bytes(received), expected)
        return self.exchange()


def movement(t):
    cases = [
        ("NUL", cup(3, 4) + b"\0", (3, 4)),
        ("BS", cup(3, 4) + b"\b", (2, 4)),
        ("BS.2", b"\b", (1, 1)),
        ("HT", b"\t", (9, 1)),
        ("HT.2", b"\t\t", (17, 1)),
        ("LF", cup(5, 3) + b"\n", (5, 4)),
        ("LF.2", cup(5, 25) + b"\n", (5, 25)),
        ("CR", cup(10, 5) + b"\r", (1, 5)),
        ("CR.2", cup(1, 5) + b"\r\r", (1, 5)),
        ("NEL", cup(10, 3) + ESC + b"E", (1, 4)),
        ("NEL.2", cup(10, 25) + ESC + b"E", (1, 25)),
        ("RI", cup(5, 5) + ESC + b"M", (5, 4)),
        ("RI.2", ESC + b"M", (1, 1)),
        ("HTS", ESC + b"[3g" + cup(5, 1) + ESC + b"H" + cup(20, 1) + ESC + b"H" + cup(1, 1) + b"\t\t", (20, 1)),
        ("HTS.2", ESC + b"[3g" + cup(5, 1) + ESC + b"H" + cup(20, 1) + ESC + b"H" + ESC + b"[2$w", (20, 1)),
        ("CHT", ESC + b"[2I", (17, 1)),
        ("CBT", cup(30, 1) + ESC + b"[2Z", (17, 1)),
        ("CVT", cup(3, 1) + ESC + b"[Y", (3, 25)),
        ("CVTwithVTS", cup(3, 10) + ESC + b"J" + cup(3, 1) + ESC + b"[Y", (3, 10)),
        ("TBC", cup(9, 1) + ESC + b"[g" + cup(1, 1) + b"\t", (17, 1)),
        ("TSR", ESC + b"[9 d\t", (17, 1)),
        ("SCOSC", cup(2, 2) + ESC + b"[s" + cup(5, 5) + ESC + b"[u", (2, 2)),
        ("SCORC", cup(3, 4) + ESC + b"[s" + cup(8, 9) + ESC + b"[u", (3, 4)),
        ("DECSC", cup(2, 2) + ESC + b"7" + cup(5, 5) + ESC + b"8", (2, 2)),
        ("DECRC", cup(3, 4) + ESC + b"7" + cup(8, 9) + ESC + b"8", (3, 4)),
        ("HVP", ESC + b"[5;10f", (10, 5)),
    ]
    for name, stream, xy in cases:
        expected = ESC + b"P2$u5/20" + ST if name == "HTS.2" else b""
        t.check(name, stream, xy, expected=expected)
    for name, final, target in (("CUU", "A", (5, 3)), ("CUD", "B", (5, 7)),
                                ("CUF", "C", (7, 5)), ("CUB", "D", (3, 5)),
                                ("CNL", "E", (1, 7)), ("CPL", "F", (1, 3)),
                                ("HPR", "a", (7, 5)), ("VPR", "e", (5, 7)),
                                ("HPB", "j", (3, 5)), ("VPB", "k", (5, 3))):
        t.check(name, cup(5, 5) + f"\x1b[2{final}".encode(), target)
        # An omitted parameter means one.
        unit = (1 if target[0] == 1 else 5 + (target[0] - 5) // 2,
                5 + (target[1] - 5) // 2)
        t.check(name, cup(5, 5) + ESC + b"[" + final.encode(), unit)
    for name, final, xy in (("CUUClamp", "A", (5, 1)), ("CUDClamp", "B", (5, 25)),
                             ("CUFClamp", "C", (80, 5)), ("CUBClamp", "D", (1, 5)),
                             ("HPBClamp", "j", (1, 5)), ("VPBClamp", "k", (5, 1))):
        t.check(name, cup(5, 5) + f"\x1b[999{final}".encode(), xy)
    for name, final, xy in (("CHA", "G", (10, 5)), ("HPA", "`", (10, 5)), ("VPA", "d", (5, 10))):
        t.check(name, cup(5, 5) + f"\x1b[10{final}".encode(), xy)
    for name, final in (("CUP", b"H"), ("CUP.2", b"f")):
        for params, xy in ((b"", (1, 1)), (b";", (1, 1)), (b"2", (1, 2)),
                           (b"2;", (1, 2)), (b";2", (2, 1)), (b"2;2", (2, 2))):
            t.check(name, cup(5, 5) + ESC + b"[" + params + final, xy)
    t.check("HTS-after-clear", ESC + b"[3g" + cup(5, 1) + ESC + b"H"
            + ESC + b"[3g" + ESC + b"H" + cup(1, 1) + b"\t", (5, 1))
    t.check("default-tab-report", ESC + b"[2$w", (1, 1),
            expected=ESC + b"P2$u1/9/17/25/33/41/49/57/65/73" + ST)
    t.check("HT-last-stop", cup(73, 1) + b"\t", (1, 2))
    t.check("HT-last-column", cup(80, 1) + b"\t", (1, 2))
    t.check("HT-bottom-scroll", cup(1, 25) + b"LAST" + cup(73, 25) + b"\t",
            (1, 25), ((1, 24, "LAST"), (1, 25, "    ")))
    t.check("CBT-last-column", cup(80, 1) + ESC + b"[Z", (73, 1))
    t.check("RIS-restores-tabs", ESC + b"[3g" + ESC + b"c\t", (9, 1))
    t.check("REP", b"X" + ESC + b"[3b", (5, 1), ((1, 1, "XXXX"),))
    t.check("RIS", cup(10, 10) + ESC + b"[31m" + ESC + b"c", (1, 1))
    # The ASCII control cases above run in both encodings. Also exercise actual
    # non-ASCII decoding and a repeated graphic across the shared cell path.
    text = "é│"
    t.check("encoded-text", text.encode("utf-8" if t.encoding == "UTF-8" else "cp437")
            + ESC + b"[2b", (5, 1), ((1, 1, "é│││"),))


def editing(t):
    for name, stream, text in (
        ("ICH", b"[2@", "AB  CDEFGHIJ"), ("DCH", b"[2P", "ABEFGHIJ  "),
        ("ECH", b"[3X", "AB   FGHIJ"), ("EL", b"[K", "AB        "),
        ("SL", b"[2 @", "CDEFGHIJ  "), ("SR", b"[2 A", "  ABCDEFGHIJ")):
        t.check(name, b"ABCDEFGHIJ" + cup(3, 1) + ESC + stream, (3, 1), ((1, 1, text),))
    for name, stream, text in (
        ("ICH", b"[@", "AB CDEFGHIJ"), ("DCH", b"[P", "ABDEFGHIJ "),
        ("ECH", b"[X", "AB DEFGHIJ"), ("SL", b"[ @", "BCDEFGHIJ "),
        ("SR", b"[ A", " ABCDEFGHIJ")):
        t.check(name, b"ABCDEFGHIJ" + cup(3, 1) + ESC + stream, (3, 1), ((1, 1, text),))
    for mode, text in ((0, "AB        "), (1, "   DEFGHIJ"), (2, "          ")):
        t.check("EL", b"ABCDEFGHIJ" + cup(3, 1) + f"\x1b[{mode}K".encode(), (3, 1), ((1, 1, text),))
        lines = ((1, 1, "ABCDEFGHIJ"), (1, 2, "AB        "), (1, 3, "          ")) if mode == 0 else (
            ((1, 1, "          "), (1, 2, "   DEFGHIJ"), (1, 3, "ABCDEFGHIJ")) if mode == 1 else
            ((1, 1, "          "), (1, 2, "          "), (1, 3, "          ")))
        t.check("ED", b"ABCDEFGHIJ\r\nABCDEFGHIJ\r\nABCDEFGHIJ" + cup(3, 2) + f"\x1b[{mode}J".encode(), (1, 1) if mode == 2 else (3, 2), lines)
    for name, cmd, lines in (
        ("IL", b"[L", ((1, 1, "ONE"), (1, 2, "     "), (1, 3, "TWO"), (1, 4, "THREE"))),
        ("DL", b"[M", ((1, 1, "ONE"), (1, 2, "THREE"), (1, 3, "     "))),
        ("SU", b"[S", ((1, 1, "TWO"), (1, 2, "THREE"), (1, 3, "     "))),
        ("SD", b"[T", ((1, 1, "     "), (1, 2, "ONE"), (1, 3, "TWO")))):
        t.check(name, b"ONE\r\nTWO\r\nTHREE" + cup(3, 2) + ESC + cmd, (3, 2), lines)
    t.check("RIScroll", b"TopLine" + cup(1, 1) + ESC + b"M", (1, 1), ((1, 1, "       "), (1, 2, "TopLine")))
    t.check("LFScroll", b"Gone\r\nNext" + cup(1, 25) + b"\n", (1, 25), ((1, 1, "Next"), (1, 25, "    ")))
    base = b"ABCDEFGHIJ\r\nKLMNOPQRST"
    t.check("DECERA", base + ESC + b"[1;3;2;8$z", (11, 2), ((1, 1, "AB      IJ"), (1, 2, "KL      ST")))
    t.check("DECFRA", base + ESC + b"[35;1;3;1;7$x", (11, 2), ((1, 1, "AB#####HIJ"), (1, 2, "KLMNOPQRST")))
    t.check("DECCRA", b"HELLO" + ESC + b"[1;1;1;5;1;3;10;1$v", (6, 1), ((10, 3, "HELLO"), (1, 1, "HELLO")))
    for name, cmd, text in (("DECIC", b"[2'}", "AB  CDEFGHIJKLMNOPQ"), ("DECDC", b"[2'~", "ABEFGHIJ          ")):
        word = b"ABCDEFGHIJKLMNOPQ" if name == "DECIC" else b"ABCDEFGHIJ"
        t.check(name, ESC + b"[?69h" + ESC + b"[1;20s" + word + cup(3, 1) + ESC + cmd,
                (3, 1), ((1, 1, text),))
    # The source has this historical regression commented out. Keep it active.
    t.check("Regressions", b"KEEP" + ESC + b"[2;24r" + cup(1, 2) + ESC + b"[24M",
            (1, 2), ((1, 1, "KEEP"), (1, 2, "    ")))


def margins(t):
    t.check("DECSTBM", ESC + b"[10;11r" + cup(3, 11) + b"\n", (3, 11))
    t.check("DECSLRM", ESC + b"[?69h" + ESC + b"[10;11s" + cup(10, 10) + b"\b\b" + ESC + b"[10C", (11, 10))
    t.check("DECSTBMScroll", b"StayHere" + ESC + b"[3;6r" + cup(1, 3) + b"Line3\r\nLine4\r\nLine5\r\nLine6\r\n",
            (1, 6), ((1, 1, "StayHere"), (1, 3, "Line4"), (1, 4, "Line5"), (1, 5, "Line6"), (1, 6, "     ")))
    for name, operation, result in (("DECSLRMScroll", b"[@", " LeftRight!"),
                                    ("SLMargins", b"[2 @", "ftRight!   "),
                                    ("SRMargins", b"[2 A", "  LeftRight")):
        t.check(name, b"OUT!" + ESC + b"[?69h" + ESC + b"[5;15s" + cup(5, 1) + b"LeftRight!" + cup(5, 1) + ESC + operation,
                (5, 1), ((1, 1, "OUT!"), (5, 1, result)))
    for name, xy, dsr in (("OriginMode", (5, 7), b"\x1b[3;5R"), ("OriginMode.2", (80, 10), b"\x1b[6;80R")):
        move = cup(5, 3) if name == "OriginMode" else cup(999, 999)
        t.check(name, ESC + b"[5;10r" + ESC + b"[?6h" + move + ESC + b"[6n", xy, expected=dsr)
    t.check("AutoWrap", ESC + b"[?7l" + cup(80, 1) + b"ABCDEF", (80, 1), ((80, 1, "F"),))
    for name, enabled, after in (("CTELCF", True, (2, 2)), ("CTDLCF", False, (2, 2))):
        stream = ESC + (b"[=4h" if enabled else b"[=4l") + cup(80, 1) + b"XY"
        t.check(name, stream, after, ((80, 1, "X"), (1, 2, "Y")))
    for enabled, xy in ((True, (1, 2)), (False, (1, 3))):
        t.check("LCFFullLine", ESC + (b"[=4h" if enabled else b"[=4l") + b"A" * 80 + b"\r\n", xy)
    t.check("LCFDSR", ESC + b"[=4h" + cup(79, 1) + b"AB" + ESC + b"[6nY",
            (2, 2), ((79, 1, "AB"), (1, 2, "Y")), expected=ESC + b"[1;80R")
    t.check("CTSMS", ESC + b"[?7s" + ESC + b"[?7l" + ESC + b"[?7$p", expected=ESC + b"[?7;2$y")
    t.check("CTRMS", ESC + b"[?7s" + ESC + b"[?7l" + ESC + b"[?7u" + ESC + b"[?7$p", expected=ESC + b"[?7;1$y")
    t.check("CTSMS_CTRMS", ESC + b"[?s" + ESC + b"[?7l" + ESC + b"[?u" + ESC + b"[?7$p", expected=ESC + b"[?7;1$y")
    # Save-all restores several independent settings, not just autowrap.
    t.check("CTRMS", ESC + b"[?6;33;69h" + ESC + b"[?s" + ESC + b"[?6;33;69l"
            + ESC + b"[?u" + ESC + b"[?6$p" + ESC + b"[?33$p" + ESC + b"[?69$p",
            expected=ESC + b"[?6;1$y" + ESC + b"[?33;1$y" + ESC + b"[?69;1$y")
    t.check("DECRST", ESC + b"[?7l" + cup(80, 1) + b"XY", (80, 1), ((80, 1, "Y"),))


def attributes(t):
    # SyncTERM's default VGA DAC palette uses 0xA8/0x54 channel values.
    # Assert logical colours via the cell mapping, never physical bank indices.
    for name, sgr, fg, bg in (
        ("SGR", "0;31;44", 0xa80000, 0x0000a8),
        ("SGR256FG", "38;5;196", 0xff0000, 0),
        ("SGR256BG", "48;5;21", 0xa8a8a8, 0x0000ff),
        ("SGRRGBFG", "38;2;12;123;234", 0x0c7bea, 0),
        ("SGRRGBBG", "48;2;23;134;245", 0xa8a8a8, 0x1786f5),
        ("SGRBright", "91;104", 0xff5454, 0x5454ff),
        ("SGRNeg", "31;44;7", 0x0000a8, 0xa80000),
        ("SGRConceal", "31;44;8", 0x0000a8, 0x0000a8),
        ("SGRDim", "1;2", 0xa8a8a8, 0),
        ("SGRNormInt", "1;22", 0xa8a8a8, 0),
        ("SGRDefFG", "31;39", 0xa8a8a8, 0),
        ("SGRDefBG", "44;49", 0xa8a8a8, 0),
        ("SGRBrightBG", "101", 0xa8a8a8, 0xff5454),
        ("CT24BC", "0", 0xff8000, 0x123456),
        ("CT24BC.2", "0", 0xff8000, 0x123456)):
        stream = ESC + b"[?33h" + f"\x1b[{sgr}m".encode()
        if name.startswith("CT24BC"):
            stream += ESC + b"[1;255;128;0t" + ESC + b"[0;18;52;86t"
        frame = t.check(name, stream + b"X", (2, 1), ((1, 1, "X"),))
        cell = cell_at(frame, 0, 0)
        assert (colour(frame, cell), colour(frame, cell, True)) == (fg, bg), (name, cell, hex(colour(frame, cell)), hex(colour(frame, cell, True)))
        pixels = set(tile(frame))
        if name == "SGRConceal":
            assert len(pixels) == 1, (name, pixels)
        else:
            assert len(pixels) == 2, (name, pixels)
    frame = t.check("SGRNoBlink", ESC + b"[5;25mX")
    assert not cell_at(frame, 0, 0).flags & 2
    assert tile(frame) == tile(frame, alternate=True)
    for name, seq in (("DECCARA", b"$r"), ("DECRARA", b"$t")):
        frame = t.check(name, b"ABCDE" + ESC + b"[1;2;1;4;1" + seq, lines=((1, 1, "ABCDE"),))
        assert [colour(frame, cell_at(frame, x, 0)) for x in range(5)] == [0xa8a8a8, 0xffffff, 0xffffff, 0xffffff, 0xa8a8a8]
        undo = b"1" if name == "DECRARA" else b"22"
        frame = t.exchange(ESC + b"[1;2;1;4;" + undo + seq)
        assert all(colour(frame, cell_at(frame, x, 0)) == 0xa8a8a8 for x in range(5))
    for mode in (1, 2):
        frame = t.check("DECSACE", ESC + f"[{mode}*x".encode() + b"LINE1TEXT." + cup(1, 2) + b"LINE2TEXT." + ESC + b"[1;6;2;5;1$r")
        assert colour(frame, cell_at(frame, 0, 0)) == 0xa8a8a8
        assert colour(frame, cell_at(frame, 5, 0)) == (0xffffff if mode == 1 else 0xa8a8a8)
        assert colour(frame, cell_at(frame, 4, 1)) == (0xffffff if mode == 1 else 0xa8a8a8)
    t.case("OSC4")
    frame = t.exchange(ESC + b"]4;1;rgb:00/ff/00" + ST + ESC + b"[31mX", reset=True)
    assert colour(frame, cell_at(frame, 0, 0)) == 0x00ff00
    frame = t.exchange(ESC + b"]104;1" + ST)
    assert colour(frame, cell_at(frame, 0, 0)) == 0xa80000
    t.case("OSC104All")
    frame = t.exchange(ESC + b"]4;4;rgb:ff/ff/00" + ST + ESC + b"[34mX", reset=True)
    assert colour(frame, cell_at(frame, 0, 0)) == 0xffff00
    frame = t.exchange(ESC + b"]104" + ST)
    assert colour(frame, cell_at(frame, 0, 0)) == 0x0000a8
    for style in range(1, 5):
        frame = t.check("DECSCUSR", f"\x1b[{style} q".encode())
        m = frame.metadata
        assert (m["cursor_start"], m["cursor_end"], m["cursor_blink"]) == (0 if style <= 2 else 14, 15, style % 2)
    frame = t.check("DECSETBlinkBG", ESC + b"[?33h" + ESC + b"[0;5;41m ")
    assert colour(frame, cell_at(frame, 0, 0), True) == 0xff5454
    assert tile(frame) == tile(frame, alternate=True)
    frame = t.exchange(ESC + b"[?33l" + cup(1, 1) + ESC + b"[0;5;41m ")
    assert colour(frame, cell_at(frame, 0, 0), True) == 0xa80000


def queries(t):
    for name, data, expected in (
        ("DSR", b"[5n", FENCE), ("BCDSR", b"[255n", ESC + b"[25;80R"),
        ("DA", b"[c", re.compile(rb"\x1b\[=67;84;101;114;109;[0-9]+;[0-9]+c")),
        ("CTDA", b"[<0c", re.compile(rb"\x1b\[<0(?:;[0-9]+)+c")),
        ("CTSMRR", b"[=3n", ESC + b"[=3;16;8n"),
        ("DECDSR", b"[?62n", ESC + b"[32767*{"),
        ("DECMSR", b"[?62n", ESC + b"[32767*{"),
        ("DECCKSR", b"[?63;17n", ESC + b"P17!~0000" + ST),
        ("DECTABSR", b"[2$w", ESC + b"P2$u1/9/17/25/33/41/49/57/65/73" + ST),
        ("CTSMRR_4", b"[=4h" + ESC + b"[=4n", ESC + b"[=4;1n"),
        ("CTSMRR_5", b"[=5n", ESC + b"[=5;0n"),
        ("DECRQSS_m", b"[0;31m" + ESC + b"P$qm" + ST, ESC + b"P1$r0;31m" + ST),
        ("DECRQSS_r", b"[5;15r" + ESC + b"P$qr" + ST, ESC + b"P1$r5;15r" + ST),
        ("DECRQSS_s", b"[?69h" + ESC + b"[5;75s" + ESC + b"P$qs" + ST, ESC + b"P1$r5;75s" + ST),
        ("DECRQSS_*x", b"[1*x" + ESC + b"P$q*x" + ST, ESC + b"P1$r1*x" + ST),
        ("DECRQSS_*r", b"P$q*r" + ST, ESC + b"P1$r;0*r" + ST),
        ("DECSCS", b"[0;1*r" + ESC + b"P$q*r" + ST + ESC + b"[0;0*r", ESC + b"P1$r;1*r" + ST),
        ("FETM", b"[14h" + ESC + b"[14$p" + ESC + b"[14l" + ESC + b"[14$p", ESC + b"[14;1$y" + ESC + b"[14;2$y"),
        ("TTM", b"[16h" + ESC + b"[16$p" + ESC + b"[16l" + ESC + b"[16$p", ESC + b"[16;1$y" + ESC + b"[16;2$y"),
        ("CTSV", b"_SyncTERM:VER" + ST, re.compile(rb"\x1b_SyncTERM:VER;SyncTERM [^\x1b]+\x1b\\")),
        ("CTQJS", b"_SyncTERM:Q;JXL" + ST, re.compile(rb"\x1b\[=1;[01]-n")),
    ):
        t.check(name, ESC + data, expected=expected)
    t.check("CTSMRR", ESC + b"[0;0 D" + ESC + b"[=1n",
            expected=re.compile(rb"\x1b\[=1;[0-9]+;0;0;0;0;0n"))
    # Explicitly establish every reported mode; compare the complete report.
    t.check("CTSMRR", ESC + b"[?6;9;25;31;32;33;34;35;69;80;1000;1001;1002;1003;1004;1005;1006;1007;1015;1016l"
            + ESC + b"[?7;33;69h" + ESC + b"[=2n", expected=ESC + b"[=2;7;33;69n")
    t.check("CTSMRR", ESC + b"[?6;9;25;31;32;33;34;35;69;1000;1001;1002;1003;1004;1005;1006;1007;1015;1016l"
            + ESC + b"[?7;80h" + ESC + b"[=2n" + ESC + b"[?80$p",
            expected=ESC + b"[=2;7;80n" + ESC + b"[?80;1$y")
    t.check("DECDSR", ESC + b"[?63n" + ESC + b"[?63;2n",
            expected=ESC + b"P1!~0000" + ST + ESC + b"P2!~0000" + ST)
    # Positive checksum oracle, including the defined macro and empty slots.
    crc = binascii.crc_hqx(b"\0\0ABC\0" + b"\0" * 61, 0)
    t.check("DECCKSR", ESC + b"P2;0;0!zABC" + ST + ESC + b"[?63;23n",
            expected=f"\x1bP23!~{crc:04X}\x1b\\".encode())
    t.case("XTSRGA")
    frame = t.exchange(reset=True)
    t.exchange(ESC + b"[?2;1S", f'\x1b[?2;0;{frame.metadata["plane_width"]};{frame.metadata["plane_height"]}S'.encode())
    # CTerm's documented checksum uses modern cells, not the DEC glyph sum.
    # Compute the CRC independently over the capture's serialized cell ABI.
    t.case("DECRQCRA")
    frame = t.exchange(b"X", reset=True)
    c = cell_at(frame, 0, 0)
    raw = struct.pack("=IIIIHHHH", c.glyph, c.foreground, c.background, c.flags,
                      c.font, c.glyph_font, c.hyperlink, c.reserved)
    crc = binascii.crc_hqx(raw, 0)
    t.exchange(ESC + b"[17;1;1;1;1;1*y", f"\x1bP17!~{crc:04X}\x1b\\".encode())


def extensions(t):
    for name, introducer in (("APC", b"_"), ("DCS", b"P"), ("PM", b"^"), ("OSC", b"]"), ("SOS", b"X")):
        t.check(name, b"A" + ESC + introducer + b"ignored text" + ST + b"B", (3, 1), ((1, 1, "AB"),))
    for name, number, move in (("DECINVM", 1, b"1B5B313B3148"), ("DECDMAC", 2, b"1B5B333B3348")):
        t.check(name, ESC + f"P{number};0;1!z".encode() + move + ST + cup(10, 10) + f"\x1b[{number}*z".encode(), (1, 1) if number == 1 else (3, 3))
    # Audio execution/audibility has separate mixer tests. These assertions only
    # cover sequence consumption and return to normal terminal parsing.
    t.check("BEL", b"A\aB", (3, 1), ((1, 1, "AB"),))
    t.check("CTSAM", ESC + b"[=0M" + b"AB", (3, 1), ((1, 1, "AB"),))
    t.check("BCAM", ESC + b"[N" + b"T255L64C\x0e" + b"AB", (3, 1), ((1, 1, "AB"),))
    t.check("BCDM", ESC + b"[=255h\0\x1b", (2, 1), ((1, 1, "←"),))
    t.check("BCRST", ESC + b"[=255h" + ESC + b"[=255l" + ESC + b"[2;3HX", (4, 2), ((3, 2, "X"),))
    t.case("CopyPaste")
    frame = t.exchange(ESC + b"[?25l" + ESC + b"[1;33mCopyTest123", reset=True)
    expected = frame.primary[:]
    frame = t.exchange(ESC + b"_SyncTERM:P;Copy" + ST + ESC + b"[2J")
    assert frame.primary != expected
    frame = t.exchange(ESC + b"_SyncTERM:P;Paste" + ST)
    assert frame.primary == expected
    assert frame.lines[0].strip() == "", "pixel paste must not manufacture text cells"
    t.case("FNT")
    frame = t.exchange(b"X", reset=True)
    original = tile(frame)
    frame = t.exchange(ESC + b"[0;1 D" + cup(1, 1) + b"X" + ESC + b"[=1n",
                       re.compile(rb"\x1b\[=1;[0-9]+;0;1;[0-9]+;[0-9]+;[0-9]+n"))
    assert tile(frame) != original
    t.case("CTOSF")
    # Real font-download payloads include controls and are not UTF-8 text.
    data = b"".join(ESC + f"[=255;{size}{{".encode() + b"\x10" * length
                    for size, length in ((0, 4096), (1, 3584), (2, 2048)))
    frame = t.exchange(data + ESC + b"[0;255 D" + cup(1, 1) + b"X" + ESC + b"[=1n",
                       re.compile(rb"\x1b\[=1;[0-9]+;0;255;[0-9]+;[0-9]+;[0-9]+n"), reset=True)
    pixels = tile(frame)
    assert len(pixels) == 128
    assert all((pixels[row * 8 + col] != pixels[0]) == (col == 3)
               for row in range(16) for col in range(8)), pixels
    # Font downloads survive RIS, select the built-in font for subsequent cases.
    t.exchange(ESC + b"[0;0 D", reset=True)
    frame = t.check("DECSSDT_hide", ESC + b"[0$~" + cup(1, 25) + b"LAST", (5, 25), ((1, 25, "LAST"),))
    assert frame.application["cterm.status_display"] == 0
    frame = t.check("DECSSDT_host", ESC + b"[2$~" + ESC + b"[1$}STATUS" + ESC + b"[0$}MAIN",
                    (5, 1), ((1, 1, "MAIN"), (1, 25, "STATUS")))
    assert frame.application["cterm.status_display"] == 2
    frame = t.check("OSC8", ESC + b"]8;id=example;https://example.org/a?b=c" + ST + b"LINK" + ESC + b"]8;;" + ST + b"plain")
    assert all(cell_at(frame, x, 0).hyperlink for x in range(4))
    assert all(not cell_at(frame, x, 0).hyperlink for x in range(4, 9))
    click_cell(t.client, frame, 1, 0)
    t.client.wait_for(lambda s: s.url == "https://example.org/a?b=c")
    t.case("DECSET")
    frame = t.exchange(ESC + b"[?9h", reset=True)
    # X10 reports button-down only, at 1-based cell coordinates.
    click_cell(t.client, frame, 4, 5)
    t.input(ESC + b"[M" + bytes((32, 37, 38)))
    t.exchange(ESC + b"[?9l")


def keyboard(t):
    t.exchange(reset=True)
    for name, key, modifiers, expected in (
        ("IN:Up", "up", 0, b"\x1b[A"), ("IN:Down", "down", 0, b"\x1b[B"),
        ("IN:Right", "right", 0, b"\x1b[C"), ("IN:Left", "left", 0, b"\x1b[D"),
        ("IN:Home", "home", 0, b"\x1b[H"), ("IN:End", "end", 0, b"\x1b[K"),
        ("IN:PgUp", "page_up", 0, b"\x1b[V"), ("IN:PgDn", "page_down", 0, b"\x1b[U"),
        ("IN:Ins", "insert", 0, b"\x1b[@"), ("IN:Del", "delete", 0, b"\x7f"),
        ("IN:BkSp", "backspace", 0, b"\x08"), ("IN:S-Tab", "tab", SHIFT, b"\x1b[Z")):
        t.case(name)
        t.client.key(key, modifiers)
        t.input(expected)
    # Fixed termtest/ANSI wire values; never ask the encoder for an oracle.
    for prefix, modifier, suffix in (("", 0, ""), ("S-", SHIFT, ";2"),
                                      ("C-", CONTROL, ";5"), ("A-", ALT, ";3")):
        for n, parameter in enumerate((11, 12, 13, 14, 15, 17, 18, 19, 20, 21, 23, 24), 1):
            if prefix == "A-" and n == 4:
                continue  # Alt-F4 is an application/OS close command.
            t.case(f"IN:{prefix}F{n}")
            t.client.key(f"f{n}", modifier)
            t.input(f"\x1b[{parameter}{suffix}~".encode())
    t.case("keyboard-backarrow")
    t.exchange(ESC + b"[?67l")
    t.client.key("backspace")
    t.input(b"\x7f")
    t.exchange(ESC + b"[?67h")
    t.client.key("backspace")
    t.input(b"\x08")
    t.case("keyboard-paste")
    t.exchange(ESC + b"[?2004h")
    t.client.clipboard("paste é")
    t.client.key("insert", SHIFT)
    # Encoding translation is part of the real connection's paste path.
    payload = "paste é".encode("utf-8" if t.encoding == "UTF-8" else "cp437")
    t.input(ESC + b"[200~" + payload + ESC + b"[201~")

    # Numeric entry uses physical keypad positions; top-row digits are distinct.
    numeric = ALT | 32  # Num Lock
    for digits, cp437, utf8 in (("130", b"\x82", b"\xc2\x82"),
                                 ("0233", b"\x82", b"\xc3\xa9"),
                                 ("179", b"\xb3", b"\xc2\xb3"),
                                 ("0128377", b"", b"\xf0\x9f\x95\xb9"),
                                 ("255", b"\xff", b"\xc3\xbf"),
                                 ("0255", b"\x98", b"\xc3\xbf"),
                                 ("256", b"", b""), ("055296", b"", b""),
                                 ("01114112", b"", b""), ("0", b"", b"")):
        t.case("alt-keypad-" + digits)
        numeric_entry(t.client, digits)
        t.client.text("~")
        t.input((utf8 if t.encoding == "UTF-8" else cp437) + b"~")
    t.case("alt-keypad-focus-cancel")
    t.client.physical_key(56, True, numeric)
    t.client.physical_key(79, True, numeric)
    t.client.physical_key(79, False, numeric)
    t.client.event(6, 0)  # Focus loss cancels composition and releases held keys.
    t.client.event(6, 1)
    t.client.physical_key(56, False, 32)
    t.client.text("~")
    t.input(b"~")


def numeric_entry(client, digits):
    keypad = (82, 79, 80, 81, 75, 76, 77, 71, 72, 73)
    numeric = ALT | 32  # Num Lock
    client.physical_key(56, True, numeric)
    for digit in digits:
        client.physical_key(keypad[int(digit)], True, numeric)
        # Some native backends also receive translated text for the press.
        client.text(digit, numeric)
        client.physical_key(keypad[int(digit)], False, numeric)
    client.physical_key(56, False, 32)


def codepage_numeric(executable, reports):
    # These values distinguish the active codepage from CP437. Entering the
    # byte and its Unicode scalar must produce the same byte on the wire.
    for encoding, byte, scalar in (("CP1251", 192, 0x0410),
                                    ("KOI8-R", 192, 0x044e),
                                    ("CP850", 155, 0x00f8),
                                    ("ISO-8859-1", 224, 0x00e0)):
        with connection(executable, reports, encoding=encoding,
                        settings="AudioModes=0\n") as (client, root, peer, send):
            t = Terminal(client, root, peer, send)
            t.exchange(reset=True)
            for digits in (str(byte), "0" + str(scalar)):
                t.case(f"alt-keypad-{encoding}-{digits}")
                numeric_entry(client, digits)
                client.text("~")
                t.input(bytes((byte,)) + b"~")
    # Fixed mapping expectations; supplementary characters must reach the
    # actual wire byte, not merely survive the console's Unicode event queue.
    for encoding, scalar, expected in (("ATASCII", 0x1f8b0, b"\x7d"),
                                        ("PETSCIIU", 0x1fb8c, b"\x7c"),
                                        ("PRESTEL", 0x1fb00, b"\xa1")):
        with connection(executable, reports, encoding=encoding,
                        settings="AudioModes=0\n") as (client, root, peer, send):
            t = Terminal(client, root, peer, send)
            t.case(f"alt-keypad-{encoding}-U+{scalar:X}")
            t.exchange(reset=True)
            numeric_entry(client, "0" + str(scalar))
            client.text(" ")
            t.input(expected + b" ")


def recovery(t):
    seed = 0x43544552
    rng = random.Random(seed)
    samples = [ESC + b"[" + b"9" * 1000 + b"H", ESC + b"[" + b"1;" * 256 + b"m",
               ESC + b"[1;0;99999;0$z", ESC + b"[0;0;0;0;0;0*y",
               ESC + b"P1;0;1!zGGnothex" + ST, ESC + b"]4;999999;rgb:gg/ff/00" + ST,
               ESC + b"[", ESC + b"Punfinished", ESC + b"]unfinished", b"\xf0\x80\x80\x80"]
    for _ in range(38):
        samples.append(rng.choice((ESC + b"[", ESC + b"]", ESC + b"P", b""))
                       + bytes(rng.randrange(256) for _ in range(rng.randrange(1, 257))))
    for index, data in enumerate(samples):
        t.case(f"recovery-{seed:08x}-{index:02d}")
        # CAN/SUB cancel partial sequences; ST exits strings. RIS then establishes
        # a known baseline. Recovery checks exact cells, coordinates and replies.
        # Disable macro/music/doorway before random data can absorb the fence.
        frame = t.exchange(b"\x18\x1a" + ST + ST + RESET + ESC + b"[=0M" + data
                           + b"\x18\x1a" + ST + ST + RESET + ESC + b"[0;0 D" + b"RECOVERED")
        assert frame.lines == ["RECOVERED" + " " * 71] + [" " * 80] * 24, (t.current, data.hex(), frame.lines)
        assert (frame.application["cterm.x"], frame.application["cterm.y"]) == (10, 1)
        assert (frame.application["cterm.top_margin"], frame.application["cterm.bottom_margin"]) == (1, 25)


SUITES = dict(movement=movement, editing=editing, margins=margins, attributes=attributes,
              queries=queries, extensions=extensions, keyboard=keyboard, recovery=recovery)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("suite", choices=SUITES)
    parser.add_argument("encoding", choices=("CP437", "UTF-8"))
    parser.add_argument("--reports", type=Path, default=Path("reports/test-control"))
    args = parser.parse_args()
    with connection(args.executable, args.reports, encoding=args.encoding,
                    options="ForceLCF=false\nLFExpand=false\n",
                    settings="AudioModes=0\n") as (client, root, peer, send):
        t = Terminal(client, root, peer, send)
        t.encoding = args.encoding
        SUITES[args.suite](t)
        manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
        identifiers = [row["id"] for row in manifest["cases"]]
        assert len(identifiers) == len(set(identifiers)), "Duplicate manifest case"
        assert {row["suite"] for row in manifest["cases"]} <= SUITES.keys(), "Unknown manifest group"
        required = {row["id"] for row in manifest["cases"] if row["suite"] == args.suite}
        assert required <= t.completed, ("Missing coverage", required - t.completed)
        print(f"Passed {len(t.completed)} cases ({args.suite}, {args.encoding})", flush=True)
    if args.suite == "keyboard" and args.encoding == "CP437":
        codepage_numeric(args.executable, args.reports)


if __name__ == "__main__":
    main()
