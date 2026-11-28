#!/usr/bin/env python3
"""Independent ANSI-BBS conformance checks through the real connection and renderer.

The checked-in termtest manifest documents correspondence, not an oracle. Expected
text, coordinates, colours and replies below come from docs/cterm.adoc. A status
reply fences remote parsing before one coherent cell/pixel/state capture; it does
not move the cursor or consume pending wrap. No installed BBS is required.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import array
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
    # Capture planes are already in logical row order.
    left, top = (x - 1) * m["cell_width"], (y - 1) * m["cell_height"]
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

    def input(self, expected, timeout=10):
        # Input acceptance queues an event; it does not acknowledge transmission.
        # Wait for its complete wire output before sending a parsing fence, so
        # connection input cannot overtake a queued keyboard/mouse event.
        received = bytearray()
        deadline = time.monotonic() + timeout
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
    frame = t.check("CSI-parameter-NUL", b"A" + ESC + b"[2;1\x002H" + ESC + b"[1;3\x001mB" + ESC + b"[6n",
                    (13, 2), [(1, 1, "A"), (12, 2, "B")], expected=ESC + b"[2;13R")
    assert colour(frame, cell_at(frame, 11, 1)) == 0xff5454
    t.check("ESC-intermediate-finals", cup(4, 3) + ESC + b"(%M" + ESC + b"%[31mX",
            (8, 3), [(4, 3, "31mX")])
    t.check("CSI-multiple-intermediates", b"ABC" + cup(2, 1) + ESC + b"[1! @",
            (2, 1), [(1, 1, "ABC")])
    t.check("BS", cup(2, 4) + b"AB\b", (3, 4), [(2, 4, "AB")])
    cases = [
        ("NUL", cup(3, 4) + b"\0", (3, 4)),
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

    if t.encoding == "UTF-8":
        keycap = "1\ufe0f\u20e3".encode()
        # Write both halves of separate wide keycaps and retain the following sentinels.
        data = b"e\xcc\x81" + keycap + b"!" + keycap + b"?" + cup(2, 1) + b"A" + cup(6, 1) + b"B"
        frame = t.check("ascii-wide-overwrite", data + ESC + b"[6n", (7, 1),
                        ((1, 1, "éA ! B?"),), expected=ESC + b"[1;7R")
        for col in (3, 5):
            blank = cell_at(frame, col - 1, 0)
            assert set(tile(frame, col, 1)) == {blank.background}


def editing(t):
    insert, replace, query = b"\x1b[4h", b"\x1b[4l", b"\x1b[4$p"
    on, off = b"\x1b[4;1$y", b"\x1b[4;2$y"
    frame = t.check("IRM", query + b"ABCDE" + cup(2, 1) + insert + query + b"X"
                    + replace + query + b"Y", (4, 1), ((1, 1, "AXYCDE"),), expected=off + on + off)
    assert frame.application["cterm.ext_attr.insert_mode"] == 0
    # ICH opens its own blanks and REP inserts each repeated graphic.
    frame = t.check("IRM", b"ABCD" + cup(2, 1) + insert + b"\x1b[2@X\x1b[2b",
                    (5, 1), ((1, 1, "AXXX  BCD"),))
    assert frame.application["cterm.ext_attr.insert_mode"] == 1
    for lcf in (b"\x1b[=4l", b"\x1b[=4h"):
        t.check("IRM", b"12345!\r\nabcde?" + b"\x1b[?69h\x1b[2;5s" + lcf + cup(4, 1)
                + insert + b"XYZ", (3, 2), ((1, 1, "123XY!"), (1, 2, "aZbcd?")))
        t.check("IRM", lcf + cup(1, 25) + b"bottom" + cup(80, 25) + insert + b"XY",
                (2, 25), ((1, 24, "bottom"), (80, 24, "X"), (1, 25, "Y ")))
    t.check("IRM", b"\x1b[?7l" + cup(79, 1) + b"AB" + cup(79, 1) + insert + b"XYZ",
            (80, 1), ((79, 1, "XZ"), (1, 2, " ")))
    t.check("IRM", insert + ESC + b"c" + query, expected=off)
    # Changing IRM while wrap is pending preserves that pending wrap.
    t.check("IRM", b"NEXT" + cup(1, 2) + b"ABCD" + b"\x1b[=4h" + cup(80, 1)
            + b"!" + insert + query + b"X", (2, 2), ((80, 1, "!"), (1, 2, "XABCD")), expected=on)
    frame = t.check("IRM", b"\x1b[41m \x1b[42m \x1b[0m" + cup(1, 1) + insert + b"X")
    for col, background in ((2, 0xa80000), (3, 0x00a800)):
        assert colour(frame, cell_at(frame, col - 1, 0), True) == background
        background_pixel = cell_at(frame, col - 1, 0).background
        assert set(tile(frame, col)) == set(tile(frame, col, alternate=True)) == {background_pixel}
    if t.encoding == "UTF-8":
        # A combining mark revises one inserted cell. A keycap grows from one cell to two.
        frame = t.check("IRM", b"ABCDEF" + cup(2, 1) + insert + "e\u03011\u20e3界".encode(), (7, 1))
        assert [frame.cell_text(cell_at(frame, x, 0)) for x in (0, 1, 2, 4, 6, 7)] == [
            "A", "é", "1\u20e3", "界", "B", "C"]
        # Widening at the last column restores its old glyph, then inserts on row two.
        frame = t.check("IRM", cup(79, 1) + "界".encode() + cup(1, 2) + b"NEXT" + cup(80, 1)
                        + insert + "1\u20e3".encode(), (3, 2), ((3, 2, "NEXT"),))
        assert frame.cell_text(cell_at(frame, 78, 0)) == "界"
        assert frame.cell_text(cell_at(frame, 0, 1)) == "1\u20e3"
        # A keycap followed by an unmapped mark narrows back to a single cell.
        # The original wide glyph now fits at the right edge again.
        frame = t.check("IRM", cup(77, 1) + "界AB".encode() + cup(74, 1) + insert
                        + "1\u20e3\u0301".encode(), (75, 1))
        assert frame.cell_text(cell_at(frame, 77, 0)) == "界"
        assert frame.cell_text(cell_at(frame, 79, 0)) == "A"
        frame = t.check("IRM", cup(78, 1) + "界A".encode() + cup(74, 1) + insert
                        + "1\u20e3\u0301".encode(), (75, 1))
        assert frame.cell_text(cell_at(frame, 78, 0)) == "界"
        assert cell_at(frame, 79, 0).flags & (1 << 19)
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


def region_scrolling(t):
    # Large regions exercise the row ring; small ones use the copy path. Each
    # row has a distinct character and solid RGB raster, independently expected.
    width, height = 80, 25
    for top, bottom in ((1, 20), (3, 23), (5, 10), (1, 25)):
        for count in (1, 2, 4, bottom - top + 2):
            for operation in ("SU", "SD", "DL", "IL", "LF", "RI"):
                t.case("RegionScroll")
                seed = b"".join(cup(1, row) +
                    f"\x1b[38;2;{row};{2*row};{3*row}m\x1b[48;2;{row};{2*row};{3*row}m".encode() +
                    bytes([64 + row]) * width for row in range(1, height + 1))
                region = f"\x1b[{top};{bottom}r".encode()
                start = top if operation in ("RI", "IL", "DL") else bottom
                command = {"SU": f"\x1b[{count}S".encode(), "SD": f"\x1b[{count}T".encode(),
                           "DL": f"\x1b[{count}M".encode(), "IL": f"\x1b[{count}L".encode(),
                           "LF": b"\n" * count, "RI": b"\x1bM" * count}[operation]
                frame = t.exchange(ESC + b"[?7l" + seed + region + cup(1, start) +
                                   ESC + b"[38;2;90;80;70m\x1b[48;2;90;80;70m" + command, reset=True)
                down = operation in ("SD", "IL", "RI")
                shift = min(count, bottom - top + 1)
                for row in range(1, height + 1):
                    source = row
                    if top <= row <= bottom:
                        source = row - shift if down else row + shift
                        if not top <= source <= bottom:
                            source = None
                    expected_text = chr(64 + source) if source else " "
                    expected_rgb = (source << 16 | 2*source << 8 | 3*source) if source else 0x5a5046
                    for x in range(1, width + 1):
                        cell = cell_at(frame, x - 1, row - 1)
                        context = (top, bottom, count, operation, x, row)
                        assert frame.cell_text(cell) == expected_text, context
                        assert colour(frame, cell, background=True) == expected_rgb, context
                    # Compare complete solid rows in C rather than creating a
                    # Python tuple for every tile in this 96-scene matrix.
                    span = frame.metadata["plane_width"] * frame.metadata["cell_height"]
                    offset = (row - 1) * span
                    pixels = array.array("I", [0x80000000 | expected_rgb]) * span
                    for plane in (frame.primary, frame.alternate):
                        assert plane[offset:offset + span] == pixels, (top, bottom, count, operation, row)
                assert (frame.application["cterm.x"], frame.application["cterm.y"]) == (1, start), (operation, frame.application)
    t.check("RegionScrollRecovery", b"RECOVERED", (10, 1), ((1, 1, "RECOVERED"),))


def region_full_width_wrap(t):
    # A 79-column run only reaches column 80. Writing the final cell, even
    # as a space, scrolls immediately with LCF off and autowrap enabled.
    for top, origin in ((1, False), (3, False), (1, True), (3, True)):
        for policy in ("immediate", "deferred", "disabled"):
            for written in (79, 80):
                t.case("RegionFullWidthWrap")
                mode = ESC + (b"[=4h" if policy == "deferred" else b"[=4l")
                mode += ESC + (b"[?7l" if policy == "disabled" else b"[?7h")
                setup = mode + f"\x1b[{top};23r".encode() + (ESC + b"[?6h" if origin else b"")
                before = t.exchange(setup, reset=True)
                data = ESC + b"[38;2;24;80;120m\x1b[48;2;24;80;120m" + cup(1, 23)
                for index, letter in enumerate(b"ABCDE"):
                    text = bytes([letter]) * 79
                    if written == 80:
                        text += b" " if index % 2 == 0 else bytes([letter])
                    # Send LF from the column left by the preceding write,
                    # including column 80 after writing just 79 characters.
                    data += b"\n" + cup(1, 23) + text
                frame = t.exchange(data)
                double = written == 80 and policy == "immediate"
                for row in range(1, 26):
                    expected = " " * 80
                    for index, letter in enumerate("ABCDE"):
                        target = 14 + 2 * index if double else 19 + index
                        if row == target:
                            last = " " if written == 79 or index % 2 == 0 else letter
                            expected = letter * 79 + last
                    context = (top, origin, policy, written, row)
                    assert frame.lines[row - 1] == expected, (context, frame.lines)
                    if expected.strip():
                        for x in range(1, 81):
                            cell = cell_at(frame, x - 1, row - 1)
                            assert colour(frame, cell, background=True) == 0x185078, context
                        span = frame.metadata["plane_width"] * frame.metadata["cell_height"]
                        offset = (row - 1) * span
                        pixels = array.array("I", [0x80185078]) * span
                        for plane in (frame.primary, frame.alternate):
                            assert plane[offset:offset + span] == pixels, context
                app = frame.application
                assert (app["cterm.x"], app["cterm.y"]) == ((1, 23) if double else (80, 23))
                assert app["cterm.last_column_flag.set"] == int(written == 80 and policy == "deferred")
                assert (app["cterm.scrollback_pos"] - before.application["cterm.scrollback_pos"]) % app["cterm.scrollback_lines"] == (10 if double else 5)
    t.check("RegionWrapRecovery", b"RECOVERED", (10, 1), ((1, 1, "RECOVERED"),))


def region_clear(t):
    for origin in (False, True):
        for command in (ESC + b"[2J", b"\f"):
            t.case("RegionClearScrollback")
            seed = b"".join(cup(1, row) + bytes([64 + row]) * 79 for row in range(1, 26))
            before = t.exchange(seed + ESC + b"[5;16r" + (ESC + b"[?6h" if origin else b""), reset=True)
            frame = t.exchange(command)
            first, last = (5, 16) if origin else (1, 25)
            for row in range(1, 26):
                cleared = first <= row <= last
                assert frame.lines[row - 1] == (" " * 80 if cleared else chr(64 + row) * 79 + " "), (origin, command, row)
                if cleared:
                    for x in range(1, 81):
                        cell = cell_at(frame, x - 1, row - 1)
                        assert colour(frame, cell, background=True) == 0
                        assert all((pixel & 0xffffff) == 0 if pixel & 0x80000000 else frame.palette[pixel] == 0
                                   for pixel in tile(frame, x, row))
            app = frame.application
            assert (app["cterm.x"], app["cterm.y"]) == (1, first)
            assert (app["cterm.scrollback_pos"] - before.application["cterm.scrollback_pos"]) % app["cterm.scrollback_lines"] == last - first + 1
    t.check("RegionClearRecovery", b"RECOVERED", (10, 1), ((1, 1, "RECOVERED"),))


def margins(t):
    region_clear(t)
    region_scrolling(t)
    region_full_width_wrap(t)
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
    # Saving modes, restoring an unsaved mode, and restoring the same origin preserve the cursor.
    for save, restore in ((b"", b"[?6u"), (b"[?6s", b"[?6u"), (b"[?s", b"[?u")):
        t.check("CTSMS_CTRMS", cup(5, 3) + (ESC + save if save else b"") + ESC + restore + b"X",
                (6, 3), ((5, 3, "X"),))
    t.check("OriginMode", ESC + b"[5;10r" + cup(8, 8) + ESC + b"[?6hA" + cup(3, 2)
            + ESC + b"[?6h" + ESC + b"[6n", (1, 5), ((1, 5, "A"),), expected=ESC + b"[1;1R")
    # Origin changes home at the scrolling margins or at the screen origin.
    for save, restore in ((b"[?6s", b"[?6u"), (b"[?s", b"[?u")):
        t.check("OriginMode", ESC + b"[5;10r" + cup(5, 7) + ESC + b"[?6hA" + ESC + save
                + ESC + b"[?6lB" + cup(8, 8) + ESC + restore + b"C" + ESC + b"[6n",
                (2, 5), ((1, 1, "B"), (1, 5, "C")), expected=ESC + b"[1;2R")
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
        ("SGRConceal", "31;44;8", 0xa80000, 0x0000a8),
        ("SGRDim", "1;2", 0x545454, 0),
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
    bright_backgrounds = (0x545454, 0xff5454, 0x54ff54, 0xffff54,
                          0x5454ff, 0xff54ff, 0x54ffff, 0xffffff)
    normal_colours = (0, 0xa80000, 0x00a800, 0xa85400, 0x0000a8, 0xa800a8, 0x00a8a8, 0xa8a8a8)
    for mode in (b"\x1b[?32l", b"\x1b[?32h"):
        stream = bytearray(mode)
        for sgr in range(90, 98):
            stream.extend(f"\x1b[0;{sgr}mX\x1b[1mX\x1b[2mX\x1b[2;{sgr}mX\x1b[22mX".encode())
        frame = t.check("SGRBright", stream)
        for x in range(40):
            cell = cell_at(frame, x, 0)
            bank = normal_colours if x % 5 in (2, 3) else bright_backgrounds
            assert colour(frame, cell) == bank[x // 5], (mode, x, cell)
            assert bool(cell.flags & (1 << 22)) == (x % 5 == 1)
            assert set(tile(frame, x + 1)) == {cell.foreground, cell.background}
    t.exchange(b"\x1b[?32l")
    query = ESC + b"P$qm" + ST
    t.check("SGRBright", b"\x1b[2;91m" + query + b"\x1b[22m" + query,
            expected=ESC + b"P1$r0;2;91m" + ST + ESC + b"P1$r0;91m" + ST)
    for mode in (b"\x1b[?33l", b"\x1b[?33h"):
        stream = bytearray(mode)
        for sgr in range(100, 108):
            stream.extend(f"\x1b[0;{sgr}mX\x1b[5mX\x1b[6;{sgr}mX\x1b[25mX".encode())
        frame = t.check("SGRBrightBG", stream)
        for x in range(32):
            cell = cell_at(frame, x, 0)
            assert colour(frame, cell, True) == bright_backgrounds[x // 4], (mode, x, cell)
            assert colour(frame, cell) == 0xa8a8a8
            assert bool(cell.flags & 2) == (x % 4 in (1, 2))
            assert bool(cell.flags & (1 << 25)) == (x % 4 == 2)
            assert set(tile(frame, x + 1)) == {cell.foreground, cell.background}
            if x % 4 in (0, 3):
                assert tile(frame, x + 1) == tile(frame, x + 1, alternate=True)
    frame = t.check("SGRBrightBG", b"\x1b[?33h\x1b[5;101m\x1b[?33s\x1b[?33lX"
                    b"\x1b[?33u\x1b[25mX\x1b[7mX\x1b[27m" + ESC + b"P$qm" + ST
                    + b"\x1b[41mX\x1b[102;49mX\x1b[103;0mX",
                    expected=ESC + b"P1$r0;101m" + ST)
    assert [colour(frame, cell_at(frame, x, 0), True) for x in range(6)] == [
        0xff5454, 0xff5454, 0xa8a8a8, 0xa80000, 0, 0]
    assert colour(frame, cell_at(frame, 2, 0)) == 0xff5454
    # Rectangular blink edits must distinguish two visually identical backgrounds.
    frame = t.check("SGRBrightBG", b"\x1b[?33h\x1b[5;101mA\x1b[41mB\x1b[101mC"
                    b"\x1b[1;1;1;2;25$r\x1b[1;3;1;3;5$t")
    assert [colour(frame, cell_at(frame, x, 0), True) for x in range(3)] == [0xff5454, 0xa80000, 0xff5454]
    assert all(not (cell_at(frame, x, 0).flags & 2) for x in range(3))
    frame = t.check("SGRBrightBG", b"\x1b[102m\x1b[2K" + ESC + b"]4;10;rgb:12/34/56" + ST)
    assert all(colour(frame, cell_at(frame, x, 0), True) == 0x123456 for x in (0, 79))
    assert set(tile(frame)) == set(tile(frame, 80)) == {cell_at(frame, 0, 0).background}
    t.exchange(ESC + b"]104;10" + ST)
    t.check("SGRBrightBG", b"\x1bF\x1b[91;101mA\x1b[31;41mB\x1bS",
            expected=ESC + b"XCTerm:STS:0:\x1b[91;101mA\x1b[31;41mB" + ST)
    frame = t.check("SGRBright", b"\x1b[2;91mAB\x1b[1;1;1;1;22$r\x1b[1;2;1;2;1$t")
    assert [colour(frame, cell_at(frame, x, 0)) for x in (0, 1)] == [0xff5454, 0xff5454]
    assert [bool(cell_at(frame, x, 0).flags & (1 << 22)) for x in (0, 1)] == [False, True]
    t.check("SGRBright", b"\x1bF\x1b[2;91mA\x1b[22mB\x1bS",
            expected=ESC + b"XCTerm:STS:0:\x1b[2;91mA\x1b[0;91;40mB" + ST)
    frame = t.check("SGRBright", b"\x1b[2;91;104;7mAB\x1b[1;1;1;1;22$r")
    assert [colour(frame, cell_at(frame, x, 0)) for x in (0, 1)] == [0x5454ff] * 2
    assert [colour(frame, cell_at(frame, x, 0), True) for x in (0, 1)] == [0xff5454, 0xa80000]
    # Only the intensity and blink attributes select the legacy alternate fonts.
    frame = t.check("SGRBright", b"\x1b[0;0 D\x1b[1;1 D\x1b[2;2 D\x1b[?31;34h"
                    b"A\x1b[91;101mB\x1b[1mC\x1b[92mD\x1b[22;5mE\x1b[102mF\x1b[25mG")
    fonts = [cell_at(frame, x, 0).font for x in range(7)]
    assert len({fonts[0], fonts[2], fonts[4]}) == 3
    assert fonts == [fonts[0], fonts[0], fonts[2], fonts[2], fonts[4], fonts[4], fonts[0]]
    t.exchange(b"\x1b[?31;34l")
    hidden_text = "界".encode() if t.encoding == "UTF-8" else b"XY"
    query = ESC + b"P$qm" + ST
    frame = t.check("SGRConceal", b"L\x1b[8m" + query + b"X\x1b[32;44;7;4;6m" + hidden_text
                    + b"\x1b[38:2::17:34:51mY\x1b[28mZ\x1b[8;0mR" + query, (8, 1),
                    expected=ESC + b"P1$r0;8m" + ST + ESC + b"P1$r0m" + ST)
    for x in range(7):
        cell = cell_at(frame, x, 0)
        assert bool(cell.flags & 4) == (1 <= x <= 4)
        if 1 <= x <= 4:
            assert set(tile(frame, x + 1)) == set(tile(frame, x + 1, alternate=True)) == {cell.background}
    assert [colour(frame, cell_at(frame, x, 0), True) for x in (2, 3, 4, 5)] == [
        0x00a800, 0x00a800, 0x112233, 0x112233]
    assert colour(frame, cell_at(frame, 5, 0)) == 0x5454ff and len(set(tile(frame, 6))) == 2
    transmitted = "界1\u20e3🫱🏻‍🫲🏼".encode() if t.encoding == "UTF-8" else b"Q\x82"
    selected_text = b"\x1bFA\x1b[8m" + transmitted + b"\x1b[28mB\x1bS"
    t.check("SGRConceal", selected_text,
            expected=ESC + b"XCTerm:STS:0:A\x1b[8m" + transmitted + b"\x1b[28mB" + ST)
    t.check("SGRConceal", b"\x1b[14h" + selected_text,
            expected=ESC + b"XCTerm:STS:1:A" + transmitted + b"B" + ST)
    if t.encoding == "UTF-8":
        # A clipped wide glyph occupies one blank cell in the selected rectangle.
        text = b"\x1b[8m" + "界".encode() + b"\x1b[28mB"
        for mode, prefix, blank in ((b"14l", b"0:", b"\x1b[8m "), (b"14h", b"1:", b" ")):
            t.check("SGRConceal", ESC + b"[" + mode + text + cup(1, 1) + ESC + b"F" + cup(2, 1) + ESC + b"S",
                    expected=ESC + b"XCTerm:STS:" + prefix + blank + ST)
            suffix = b"\x1b[28mB" if mode == b"14l" else b"B"
            t.check("SGRConceal", ESC + b"[" + mode + text + cup(2, 1) + ESC + b"F" + cup(4, 1) + ESC + b"S",
                    expected=ESC + b"XCTerm:STS:" + prefix + blank + suffix + ST)
    else:
        # DoorWay permits a literal ESC glyph; its byte must stay framed in the reply.
        literal = b"\x1b[=255h\x00\x1b\x1b[=255l"
        t.check("SGRConceal", ESC + b"F\x1b[8m" + literal + b"X\x1bS",
                expected=ESC + b"XCTerm:STS:0:\x1b[8m" + literal + b"X" + ST)
        t.check("SGRConceal", b"\x1b[14h\x1bF\x1b[8m" + literal + b"X\x1bS",
                expected=ESC + b"XCTerm:STS:1: X" + ST)
    frame = t.check("SGRConceal", b"AB\r\x1b[8;44m\x1b[2K\x1b[28mC")
    assert not (cell_at(frame, 0, 0).flags & 4)
    assert all(cell_at(frame, x, 0).flags & 4 for x in range(1, 80))
    link = ESC + b"]8;;https://example.org/hidden" + ST
    t.check("SGRConceal", ESC + b"F" + link + b"\x1b[8mQ" + ESC + b"]8;;" + ST + ESC + b"S",
            expected=ESC + b"XCTerm:STS:0:\x1b[8m\x1b]8;;https://example.org/hidden\x1b:\\Q" + ST)
    frame = t.check("SGRConceal", hidden_text + b"\x1b[1;1;1;2;8$r")
    for x in (1, 2):
        assert set(tile(frame, x)) == {cell_at(frame, x-1, 0).background}
    restored = t.exchange(b"\x1b[1;1;1;2;28$t")
    assert restored.cell_text(cell_at(restored, 0, 0)) == ("界" if t.encoding == "UTF-8" else "X")
    assert not (cell_at(restored, 0, 0).flags & 4) and len(set(tile(restored))) == 2
    frame = t.check("compound-sgr-colour",
                    b"\x1b[38:2::18:52:86;48:2:7:154:188:222;4m " + ESC + b"P$qm" + ST
                    + b"\x1b[m ", (3, 1),
                    expected=ESC + b"P1$r0;4;38:2::18:52:86;48:2:7:154:188:222m" + ST)
    cell = cell_at(frame, 0, 0)
    assert (colour(frame, cell), colour(frame, cell, True)) == (0x123456, 0x9abcde)
    expected = tuple(cell.foreground if y == 13 else cell.background for y in range(16) for _ in range(8))
    assert tile(frame) == tile(frame, alternate=True) == expected
    reset_cell = cell_at(frame, 1, 0)
    assert (colour(frame, reset_cell), colour(frame, reset_cell, True)) == (0xa8a8a8, 0)
    assert set(tile(frame, 2)) == {reset_cell.background}
    link = ESC + b"]8;;https://example.org/" + ST
    unlink = ESC + b"]8;;" + ST
    frame = t.check("sgr-underline", b"\x1b[4m " + link + b" \x1b[24m " + unlink
                    + b"\x1b[4;5m \x1b[m \x1b[4m" + ESC + b"P$qm" + ST, (6, 1),
                    expected=ESC + b"P1$r0;4m" + ST)
    assert (frame.metadata["cell_width"], frame.metadata["cell_height"]) == (8, 16)
    for x in range(5):
        cell = cell_at(frame, x, 0)
        underlined, linked = x in (0, 1, 3), x in (1, 2)
        assert bool(cell.flags & (1 << 16)) == underlined and bool(cell.hyperlink) == linked
        for alternate in (False, True):
            expected = tuple(cell.foreground if ((underlined and y == 13) or
                             (linked and y == 15 and i % 4 == 0)) and not (x == 3 and alternate)
                             else cell.background for y in range(16) for i in range(8))
            assert tile(frame, x+1, 1, alternate) == expected, (x, alternate)
    if t.encoding == "UTF-8":
        wide = "中".encode()
        frame = t.check("sgr-underline", wide + ESC + b"[4m" + wide, (5, 1))
        for half in (0, 1):
            cell = cell_at(frame, 2 + half, 0)
            assert cell.flags & (1 << 16)
            for alternate in (False, True):
                reference = list(tile(frame, 1 + half, 1, alternate))
                reference[13 * 8:14 * 8] = [cell.foreground] * 8
                assert tile(frame, 3 + half, 1, alternate) == tuple(reference)
    t.check("sgr-underline", b"\x1bF\x1b[4mA\x1b[24mB\x1bS",
                    expected=ESC + b"XCTerm:STS:0:\x1b[4mA\x1b[24mB" + ST)
    frame = t.check("sgr-underline", b"\x1b[4mAAA\r\x1b[2K\x1b[24m ", (2, 1))
    for x in range(80):
        cell = cell_at(frame, x, 0)
        assert bool(cell.flags & (1 << 16)) == (x != 0)
        expected = tuple(cell.foreground if x != 0 and y == 13 else cell.background
                         for y in range(16) for _ in range(8))
        assert tile(frame, x+1) == tile(frame, x+1, alternate=True) == expected, x
    frame = t.check("sgr-underline", b"ABCD\x1b[1;1;1;3;4$r\x1b[1;2;1;2;4$t\x1b[1;3;1;3;24$r")
    assert [bool(cell_at(frame, x, 0).flags & (1 << 16)) for x in range(4)] == [
        True, False, False, False]
    # SGR 21 replaces the font's single underline with two fixed pixel rows.
    frame = t.check("sgr-underline", b"\x1b[4m \x1b[21m \x1b[4m \x1b[21;6m \x1b[24m \x1b[21;0m "
                    + ESC + b"[21m" + ESC + b"P$qm" + ST, (7, 1), expected=ESC + b"P1$r0;21m" + ST)
    for x, rows in enumerate(((13,), (13, 15), (13,), (13, 15), (), ())):
        cell = cell_at(frame, x, 0)
        for alternate in (False, True):
            expected = tuple(cell.foreground if y in rows and not (alternate and x == 3)
                             else cell.background for y in range(16) for _ in range(8))
            assert tile(frame, x + 1, 1, alternate) == expected, (x, alternate)
    t.check("sgr-underline", b"\x1b[4;21m" + ESC + b"P$qm" + ST + b"\x1b[21;4m" + ESC + b"P$qm" + ST,
            expected=ESC + b"P1$r0;21m" + ST + ESC + b"P1$r0;4m" + ST)
    if t.encoding == "UTF-8":
        frame = t.check("sgr-underline", wide + b"\x1b[21m" + wide, (5, 1))
        for half in (0, 1):
            cell = cell_at(frame, 2 + half, 0)
            expected = list(tile(frame, 1 + half))
            for y in (13, 15):
                expected[y * 8:(y + 1) * 8] = [cell.foreground] * 8
            assert tile(frame, 3 + half) == tuple(expected)
    t.check("sgr-underline", b"\x1bF\x1b[4mA\x1b[21mB\x1b[4mC\x1b[21mD\x1b[24mE\x1bS",
            expected=ESC + b"XCTerm:STS:0:\x1b[4mA\x1b[21mB\x1b[4mC\x1b[21mD\x1b[24mE" + ST)
    frame = t.check("sgr-underline", b"ABC\r\x1b[21m\x1b[2K\x1b[24m \x1b[1;2;1;2;4$r\x1b[1;3;1;3;24$t")
    for x in range(80):
        cell = cell_at(frame, x, 0)
        rows = () if x in (0, 2) else (13,) if x == 1 else (13, 15)
        assert tile(frame, x + 1) == tuple(cell.foreground if y in rows else cell.background
                                            for y in range(16) for _ in range(8))
    frame = t.check("sgr-strike", b"\x1b[9m \x1b[4m \x1b[29m \x1b[24;6;9m \x1b[m \x1b[9m"
                    + ESC + b"P$qm" + ST, (6, 1), expected=ESC + b"P1$r0;9m" + ST)
    for x in range(5):
        cell = cell_at(frame, x, 0)
        strike, underline = x in (0, 1, 3), x in (1, 2)
        assert bool(cell.flags & (1 << 26)) == strike
        for alternate in (False, True):
            expected = tuple(cell.foreground if ((strike and y == 8) or (underline and y == 13))
                             and not (alternate and x == 3) else cell.background
                             for y in range(16) for _ in range(8))
            assert tile(frame, x+1, 1, alternate) == expected, (x, alternate)
    frame = t.check("sgr-strike", b"\x1b[9mAAA\r\x1b[2K\x1b[29m ", (2, 1))
    for x in range(80):
        cell = cell_at(frame, x, 0)
        assert bool(cell.flags & (1 << 26)) == (x != 0)
        assert tile(frame, x+1) == tuple(cell.foreground if x and y == 8 else cell.background
                                        for y in range(16) for _ in range(8))
    t.check("sgr-strike", b"\x1bF\x1b[9mA\x1b[29mB\x1bS",
            expected=ESC + b"XCTerm:STS:0:\x1b[9mA\x1b[29mB" + ST)
    frame = t.check("sgr-strike", b"ABCD\x1b[1;1;1;3;9$r\x1b[1;2;1;2;9$t\x1b[1;3;1;3;29$r")
    assert [bool(cell_at(frame, x, 0).flags & (1 << 26)) for x in range(4)] == [True, False, False, False]
    if t.encoding == "UTF-8":
        frame = t.check("sgr-strike", wide + ESC + b"[9m" + wide, (5, 1))
        for half in (0, 1):
            expected = list(tile(frame, 1 + half))
            expected[64:72] = [cell_at(frame, 2 + half, 0).foreground] * 8
            assert tile(frame, 3 + half) == tuple(expected)
    frame = t.check("sgr-overline", b"\x1b[53m \x1b[4m \x1b[21m \x1b[55m \x1b[24;53;6m \x1b[m "
                    + b"\x1b[53m" + ESC + b"P$qm" + ST, (7, 1), expected=ESC + b"P1$r0;53m" + ST)
    for x, rows in enumerate(((0,), (0, 13), (0, 13, 15), (13, 15), (0,), ())):
        cell = cell_at(frame, x, 0)
        for alternate in (False, True):
            expected = tuple(cell.foreground if y in rows and not (alternate and x == 4)
                             else cell.background for y in range(16) for _ in range(8))
            assert tile(frame, x + 1, 1, alternate) == expected, (x, alternate)
    t.check("sgr-overline", b"\x1bF\x1b[5;53mA\x1b[25mB\x1b[55mC\x1bS",
            expected=ESC + b"XCTerm:STS:0:\x1b[5;53m\x1b[48;5;8mA\x1b[0;53;37;40mB\x1b[55mC" + ST)
    frame = t.check("sgr-overline", b"ABCD\r\x1b[53m\x1b[2K\x1b[55m "
                    b"\x1b[1;2;1;4;55$r\x1b[1;2;1;2;53$r\x1b[1;4;1;5;55$t")
    for x in range(80):
        cell = cell_at(frame, x, 0)
        expected = tuple(cell.foreground if (x in (1, 3) or x >= 5) and y == 0 else cell.background
                         for y in range(16) for _ in range(8))
        assert tile(frame, x + 1) == expected
    if t.encoding == "UTF-8":
        frame = t.check("sgr-overline", wide + b"\x1b[53m" + wide, (5, 1))
        for half in (0, 1):
            expected = list(tile(frame, 1 + half))
            expected[:8] = [cell_at(frame, 2 + half, 0).foreground] * 8
            assert tile(frame, 3 + half) == tuple(expected)
    # Faint colours are independent, programmable ANSI-order registers.
    stream = b"".join(f"\x1b[{30+i};2mX".encode() for i in range(8))
    stream += (b"\x1b[2;33mA\x1b[1mB\x1b[22mC\x1b[2;7mD\x1b[27;93mE"
               b"\x1b[2;38;5;196mF\x1b[22mG\x1b[38;2;12;34;56;2mH"
               b"\x1b[22mI\x1b[2;32mJ")
    frame = t.check("SGRDim", stream + ESC + b"P$qm" + ST, (19, 1),
                    ((1, 1, "XXXXXXXXABCDEFGHIJ"),), expected=ESC + b"P1$r0;2;32m" + ST)
    faint = [0, 0x540000, 0x005400, 0x542a00, 0x000054, 0x540054, 0x005454, 0x545454]
    for x, fg in enumerate(faint + [0x542a00, 0xffff54, 0xa85400, 0, 0xa85400,
                                    0xff0000, 0xff0000, 0x0c2238, 0x0c2238, 0x005400]):
        cell = cell_at(frame, x, 0)
        assert colour(frame, cell) == fg and colour(frame, cell, True) == (0x542a00 if x == 11 else 0), (x, hex(fg), hex(colour(frame, cell)), hex(colour(frame, cell, True)))
        assert set(tile(frame, x+1, 1)) == {cell.foreground, cell.background}
    frame = t.exchange(ESC + b"]4;285;rgb:12/34/56" + ST + ESC + b"]4;285;?" + ST,
                       expected=ESC + b"]4;285;rgb:12/34/56" + ST)
    assert [colour(frame, cell_at(frame, x, 0)) for x in (3, 8, 9, 10)] == [
        0x123456, 0x123456, 0xffff54, 0xa85400]
    frame = t.exchange(ESC + b"]104;285" + ST)
    assert colour(frame, cell_at(frame, 3, 0)) == 0x542a00
    frame = t.check("SGRDim", b"\x1b[34mA\x1b[38;5;196mB\x1b[2;32mC\x1b[1;1;1;2;2$rD",
                    (5, 1), ((1, 1, "ABCD"),))
    assert [colour(frame, cell_at(frame, x, 0)) for x in range(4)] == [
        0x000054, 0xff0000, 0x005400, 0x005400]
    selection = b"\x1bF\x1b[2;31mA\x1b[34mB\x1b[22mC\x1bS"
    attributed = b"\x1b[2;31mA\x1b[34mB\x1b[0;34;40mC"
    frame = t.check("SGRDim", selection, (4, 1), ((1, 1, "ABC"),),
                    expected=ESC + b"XCTerm:STS:0:" + attributed + ST)
    replay = t.exchange(attributed, reset=True)
    for x in range(3):
        assert tile(replay, x+1, 1) == tile(frame, x+1, 1)
    frame = t.check("plain-text-wrap-rendition", cup(79, 1) + ESC + b"[31;44mABCD"
                    + ESC + b"[0mE" + ESC + b"[6n", (4, 2), ((79, 1, "AB"), (1, 2, "CDE")),
                    expected=ESC + b"[2;4R")
    for x, y in ((79, 1), (80, 1), (1, 2), (2, 2), (3, 2)):
        cell = cell_at(frame, x - 1, y - 1)
        expected = (0xa8a8a8, 0) if (x, y) == (3, 2) else (0xa80000, 0x0000a8)
        assert (colour(frame, cell), colour(frame, cell, True)) == expected
        assert set(tile(frame, x, y)) == {cell.foreground, cell.background}
    # Ticket 305. The first shortcut starts from reset, the other two from reverse blue.
    shortcuts = (b"\x1b[H\x1b[J\x1b[s\x1b[1;1H\x1b[47m\x1b[K\x1b[49m\x1b[7;44mZeile\x1b[m"
                 b"\x1b[1;49H\x1b[1;7;43mCTRL-Q\x1b[m\x1b[7;44m=Beenden   \x1b[1;7;43mCTRL-K"
                 b"\x1b[m\x1b[7;44m+\x1b[1;7;43m?\x1b[m\x1b[7;44m=Hilfe\x1b[m\x1b[1;7H\x1b[7;44m1/1      \x1b[m\x1b[u\n")
    transitions = (b"\x1b[7;43;1mA\x1b[22mB\x1b[1;27mC\x1b[mD"
                   b"\x1b[1;7;48;2;12;34;56m\x1b[?32s\x1b[?32hE\x1b[?32uF\x1b[27mG")
    frame = t.check("SGRNeg", shortcuts + transitions,
                    lines=((49, 1, "CTRL-Q=Beenden   CTRL-K+?=Hilfe"), (1, 2, "ABCDEFG")))
    brown, white, grey = 0xa85400, 0xffffff, 0xa8a8a8
    for x, y, count, fg, bg in ((49, 1, 6, brown, white), (66, 1, 6, brown, white),
                                (73, 1, 1, brown, white), (1, 2, 1, brown, white),
                                (2, 2, 1, brown, grey), (3, 2, 1, white, brown),
                                (4, 2, 1, grey, 0), (5, 2, 1, 0x0c2238, grey),
                                (6, 2, 1, 0x0c2238, white), (7, 2, 1, white, 0x0c2238)):
        for column in range(x, x + count):
            cell = cell_at(frame, column - 1, y - 1)
            actual = colour(frame, cell), colour(frame, cell, True)
            assert actual == (fg, bg), (column, y, actual, (fg, bg))
            assert set(tile(frame, column, y)) == {cell.foreground, cell.background}, (column, y)

    frame = t.check("SGRNoBlink", ESC + b"[5;25mX")
    assert not cell_at(frame, 0, 0).flags & 2
    assert tile(frame) == tile(frame, alternate=True)
    frame = t.check("sgr-fast-blink", b"\x1b[?33l\x1b[5mX\x1b[6mX\x1b[25mX\x1b[6;5mX\x1b[6;0mX\x1b[6m"
                    + ESC + b"P$qm" + ST, (6, 1), expected=ESC + b"P1$r0;6m" + ST)
    for x, rate in enumerate((5, 6, 0, 5, 0)):
        cell = cell_at(frame, x, 0)
        assert bool(cell.flags & 2) == bool(rate)
        assert bool(cell.flags & (1 << 25)) == (rate == 6)
        start = x * frame.metadata["cell_width"]
        for col in range(start, start + frame.metadata["cell_width"]):
            assert bool(frame.fast_blink[col // 8] & (0x80 >> (col % 8))) == (rate == 6)
        visible, hidden = tile(frame, x+1), tile(frame, x+1, alternate=True)
        assert set(visible) == {cell.foreground, cell.background}
        assert hidden == ((cell.background,) * len(visible) if rate else visible)
    # Inspect both cells in each composed frame, with the presenter clock frozen.
    for elapsed, visible in ((0, (True, True)), (178, (True, False)),
                             (266, (False, False)), (355, (False, True))):
        t.client.presentation_time(1000 + elapsed)
        phase = t.client.capture()
        m = phase.metadata
        width = m["destination_width"] * m["cell_width"] // m["plane_width"]
        height = m["destination_height"] * m["cell_height"] // m["plane_height"]
        for x in range(2):
            pixels = [phase.rgb[(m["destination_y"] + y) * m["output_width"]
                                + m["destination_x"] + x * width + col]
                      for y in range(height) for col in range(width)]
            assert any(pixels) == visible[x], (elapsed, x, set(pixels))
    t.client.presentation_time()
    t.check("sgr-fast-blink", b"\x1b[?33l\x1bF\x1b[5mA\x1b[6mB\x1b[5mC\x1bS",
            expected=ESC + b"XCTerm:STS:0:\x1b[5mA\x1b[6mB\x1b[5mC" + ST)
    frame = t.check("sgr-fast-blink", b"ABCD\x1b[1;1;1;3;6$r\x1b[1;2;1;2;5$r\x1b[1;3;1;3;25$r")
    assert [cell_at(frame, x, 0).flags & (2 | (1 << 25)) for x in range(4)] == [
        2 | (1 << 25), 2, 0, 0]
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
    t.case("DECSCUSR")
    def check_cursor(commands, style, visible):
        reports = b"\x1b[?25$p\x1bP$q q" + ST
        expected = f"\x1b[?25;{1 if visible else 2}$y\x1bP1$r{style} q".encode() + ST
        m = t.exchange(commands + reports, expected).metadata
        assert m["cursor_visible"] == visible
        if visible:
            assert (m["cursor_start"], m["cursor_end"], m["cursor_blink"]) == (
                0 if style <= 2 else 14, 15, style % 2)

    for style in range(1, 5):
        select = f"\x1b[{style} q".encode()
        check_cursor(RESET + select, style, True)
        # Both the individual and the all-modes save/restore forms save visibility.
        mode = b"25" if style % 2 else b""
        save, restore = (b"\x1b[?" + mode + op for op in (b"s", b"u"))
        check_cursor(save + b"\x1b[?25l\x1b[?25l", style, False)
        check_cursor(restore, style, True)
        check_cursor(b"\x1b[?25l" + save + b"\x1b[?25h" + restore, style, False)
        new_style = 5 - style
        check_cursor(f"\x1b[{new_style} q".encode(), new_style, False)
        check_cursor(b"\x1b[?25h\x1b[?25h", new_style, True)
    frame = t.check("DECSETBlinkBG", ESC + b"[?33h" + ESC + b"[0;5;41m ")
    assert colour(frame, cell_at(frame, 0, 0), True) == 0xff5454
    assert tile(frame) == tile(frame, alternate=True)
    frame = t.exchange(ESC + b"[?33l" + cup(1, 1) + ESC + b"[0;5;41m ")
    assert colour(frame, cell_at(frame, 0, 0), True) == 0xa80000


def queries(t):
    sizes = b"\x1b[15t\x1b[16t\x1b[18t\x1b[19t"
    screen = b"\x1b[5;400;640t\x1b[6;16;8t"
    t.check("XTWINOPS", sizes, expected=screen + b"\x1b[8;25;80t\x1b[9;25;80t")
    # Reserve the host status line, then set smaller scrolling margins.
    t.exchange(b"\x1b[2$~\x1b[4;9r\x1b[?6h" + sizes,
               screen + b"\x1b[8;24;80t\x1b[9;25;80t")
    # Interleave colours, valid queries, malformed requests and echoed replies.
    frame = t.check("XTWINOPS", b"\x1b[1;18;52;86t\x1b[016t\x1b[0;171;205;239t"
                    b"\x1b[15;2t\x1b[18;t\x1b[16:2t\x1b[18446744073709551631t"
                    b"\x1b[19;0;0;0t\x1b[8;24;80tX",
                    (2, 1), ((1, 1, "X"),), expected=b"\x1b[6;16;8t")
    cell = cell_at(frame, 0, 0)
    assert (colour(frame, cell), colour(frame, cell, True)) == (0x123456, 0xabcdef)
    t.case("synchronized-output")
    begin, end, query = (ESC + b"[?2026" + suffix for suffix in (b"h", b"l", b"$p"))
    on, off = (ESC + b"[?2026;" + value + b"$y" for value in (b"1", b"2"))
    t.exchange(query, off, reset=True)
    # The red background and cursor hide precede BSU in the same write.
    # The second BSU must keep that frame, even though the live cell is green.
    held = t.exchange(ESC + b"[?25l" + ESC + b"[41m " + begin + b"\r" + ESC + b"[42m "
                      + begin + query + ESC + b"[6n", on + ESC + b"[1;2R")
    def first_pixel(frame):
        m = frame.metadata
        return frame.rgb[m["destination_y"] * m["output_width"] + m["destination_x"]]
    assert colour(held, cell_at(held, 0, 0), True) == 0x00a800
    assert first_pixel(held) == 0xa80000
    released = t.exchange(end + end + query, off)
    assert first_pixel(released) == 0x00a800
    t.exchange(begin + ESC + b"[=2n" + end, re.compile(rb"\x1b\[=2(?:;[0-9]+)*;2026n"))
    t.exchange(begin + ESC + b"c" + query, off)
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
        ("CTSV", b"_SyncTERM:VER" + ST, re.compile(rb"\x1b_SyncTERM:VER;SyncTERM [\x08-\x0d\x20-\x7e]+\x1b\\")),
        ("CTQJS", b"_SyncTERM:Q;JXL" + ST, re.compile(rb"\x1b\[=1;[01]-n")),
    ):
        t.check(name, ESC + data, expected=expected)
    t.check("CTSMRR", ESC + b"[0;0 D" + ESC + b"[=1n",
            expected=re.compile(rb"\x1b\[=1;[0-9]+;0;0;0;0;0n"))
    assignments = b"".join(f"\x1b[{i};{i} D".encode() for i in range(10))
    report = ESC + b"[=8;0;1;2;3;4;5;6;7;8;9n"
    t.check("CTSMRR", assignments + ESC + b"[=8;1n" + ESC + b"[=8n" + ESC + b"[=1n",
            expected=report + ESC + b"[=1;46;0;0;1;2;3n")
    for selector in (250, 256, 4294967296):
        t.exchange(f"\x1b[9;{selector} D".encode() + ESC + b"[=1n" + ESC + b"[=8n",
                   ESC + b"[=1;46;1;0;1;2;3n" + report)
    t.exchange(ESC + b"[10;1 D" + ESC + b"[=8n", report)
    t.exchange(ESC + b"[=8n", ESC + b"[=8;0;0;0;0;0;0;0;0;0;0n", reset=True)
    # Explicitly establish every reported mode; compare the complete report.
    t.check("CTSMRR", ESC + b"[?6;9;25;31;32;33;34;35;69;80;1000;1001;1002;1003;1004;1005;1006;1007;1015;1016l"
            + ESC + b"[?7;33;69h" + ESC + b"[=2n", expected=ESC + b"[=2;7;33;69n")
    t.exchange(b"\x1b[2 q\x1b[?25h\x1b[=2n", ESC + b"[=2;7;25;33;69n")
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
    for text in (b"X", b"\x1b[2;91;104mX"):
        frame = t.exchange(text, reset=True)
        c = cell_at(frame, 0, 0)
        raw = struct.pack("=IIIIHHHBB", c.glyph, c.foreground, c.background, c.flags,
                          c.font, c.glyph_font, c.hyperlink, c.reserved, c.colour_rendition)
        crc = binascii.crc_hqx(raw, 0)
        t.exchange(ESC + b"[17;1;1;1;1;1*y", f"\x1bP17!~{crc:04X}\x1b\\".encode())


def sixel_bounds(t):
    cases = (
        ("SixelScaledBackground", b'7;0q"2;3;4;9#0;2;100;0;0!2~', (6, 12), (12, 18)),
        ("SixelTransparent", b'7;1q"2;3;4;9#0;2;100;0;0!2~', (6, 12), (0, 0)),
        ("SixelBackgroundOnly", b'7;0q"2;3;4;9', (0, 0), (12, 18)),
        ("SixelOnePixel", b'7;1q#0;2;100;0;0@', (1, 1), (0, 0)),
        ("SixelPaletteOverflow", b'7;1q#0;2;100;0;0#4294967296;2;0;0;100@', (1, 1), (0, 0)),
        ("SixelRepeatOverflow", b'7;1q#0;2;100;0;0!18446744073709551616~', (640, 6), (0, 0)),
        ("SixelRasterOverflow", b'7;0q"1;2;2147483647;2147483647#0;2;100;0;0~~-?', (4, 6), (640, 400)),
    )
    for name, payload, red, background in cases:
        # Known background and palette-defined sixel red are independent
        # expectations. Fence once, then inspect both planes and cursor.
        prefix = ESC + b"[?25l" + ESC + b"[?80h" + ESC + b"[48;2;18;52;86m" + ESC + b"[2J" + ESC + b"[40m"
        frame = t.check(name, prefix + ESC + b"P" + payload + ST, (1, 1))
        m = frame.metadata
        for plane in (frame.primary, frame.alternate):
            for y in range(400):
                for x in range(640):
                    expected = (0xff0000 if x < red[0] and y < red[1] else
                                0 if x < background[0] and y < background[1] else 0x123456)
                    value = plane[(m["pixel_top"] + y) * m["plane_width"] + x]
                    actual = (value if value & 0x80000000 else frame.palette[value]) & 0xffffff
                    assert actual == expected, (name, x, y, actual, expected)
    t.check("SixelEmptyCursor", ESC + b"[?80l" + ESC + b"P2q" + ST, (1, 2))


def extensions(t):
    for name, introducer in (("APC", b"_"), ("DCS", b"P"), ("PM", b"^"), ("OSC", b"]"), ("SOS", b"X")):
        t.check(name, b"A" + ESC + introducer + b"ignored text" + ST + b"B", (3, 1), ((1, 1, "AB"),))
    for name, number, move in (("DECINVM", 1, b"1B5B313B3148"), ("DECDMAC", 2, b"1B5B333B3348")):
        t.check(name, ESC + f"P{number};0;1!z".encode() + move + ST + cup(10, 10) + f"\x1b[{number}*z".encode(), (1, 1) if number == 1 else (3, 3))
    for name, parameters in (("NumericMacroSlot", b"4294967296;0;1"),
                             ("NumericMacroDelete", b"0;4294967296;1"),
                             ("NumericMacroEncoding", b"0;0;4294967297")):
        # Oversized fields must not wrap into a valid definition of macro 0.
        t.check(name, ESC + b"P0;0;1!z1B5B333B3348" + ST
                + ESC + b"P" + parameters + b"!z1B5B393B3948" + ST
                + ESC + b"[0*z", (3, 3))
    sixel_bounds(t)
    # Audio execution/audibility has separate mixer tests. These assertions only
    # cover sequence consumption and return to normal terminal parsing.
    t.check("BEL", b"A\aB", (3, 1), ((1, 1, "AB"),))
    t.check("CTSAM", ESC + b"[=0M" + b"AB", (3, 1), ((1, 1, "AB"),))
    t.check("CTQueueModule", ESC + b"_SyncTERM:Q;module" + ST
            + ESC + b"_SyncTERM:A;QueueModule;../../outside.xm" + ST
            + ESC + b"[=7;0nAB", (3, 1), ((1, 1, "AB"),),
            expected=ESC + b"[=7;103;0n" + ESC + b"[=7;0;0n")
    t.check("CTMIDIUnavailable", ESC + b"_SyncTERM:Q;midi" + ST,
            (1, 1), expected=ESC + b"[=7;102;0n")
    t.check("CTMIDIQueueRecovery", ESC + b"_SyncTERM:A;QueueMIDI;../../outside.mid" + ST
            + ESC + b"_SyncTERM:A;Flush;C=0" + ST + b"AB", (3, 1), ((1, 1, "AB"),))
    t.check("CTANSISevenOctaves", ESC + b"[|MB O0 C N0 O6 B N83 N84\x0e" + b"AB",
            (3, 1), ((1, 1, "AB"),))
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
    # The main display changes font while the status display keeps its own selection.
    frame = t.exchange(ESC + b"[2$~" + cup(1, 1) + ESC + b"[0;0 DX" + ESC + b"[0;1 DX"
                       + ESC + b"[1$}" + ESC + b"[0;0 DX" + ESC + b"[0$}XX")
    main = [cell_at(frame, x, 0) for x in range(4)]
    status = cell_at(frame, 0, 24)
    assert all(frame.cell_text(cell) == "X" for cell in [*main, status])
    assert status.font == main[0].font != main[1].font
    assert main[1].font == main[2].font == main[3].font
    assert tuple(p == status.foreground for p in tile(frame, 1, 25)) == tuple(
        p == main[0].foreground for p in tile(frame, 1, 1))
    assert tile(frame, 1, 1) != tile(frame, 2, 1) == tile(frame, 3, 1) == tile(frame, 4, 1)
    # Resolve each selector through the primary position for a reference row.
    references = b"".join(f"\x1b[0;{i} DX".encode() for i in range(10))
    assignments = b"".join(f"\x1b[{i};{i} D".encode() for i in range(10))
    explicit = b"".join(f"\x1b[{10+i}mX".encode() for i in range(1, 10))
    legacy = b"\x1b[0mX\x1b[1mX\x1b[22;5mX\x1b[1mX"
    frame = t.exchange(references + assignments + cup(1, 2) + ESC + b"[?31;34h"
                       + legacy + explicit + ESC + b"[10mX" + ESC + b"[0mX"
                       + ESC + b"[?31;34l" + ESC + b"[1;5mX" + ESC + b"[19mX"
                       + ESC + b"P$qm" + ST, ESC + b"P1$r0;1;5;19m" + ST, reset=True)
    fonts = [cell_at(frame, i, 0).font for i in range(10)]
    assert len(set(fonts)) == 10, fonts
    for x, position in enumerate([0, 1, 2, 3, *range(1, 10), 3, 0, 0, 9]):
        assert cell_at(frame, x, 1).font == fonts[position], (x, position, fonts)
    # Two positions assigned the same font still have distinct SGR renditions.
    t.exchange(b"\x1bF\x1b[19mA\x1b[11mB\x1b[10mC\x1bS",
               ESC + b"XCTerm:STS:0:\x1b[19mA\x1b[11mB\x1b[10mC" + ST, reset=True)
    # A retained cell keeps its original font after a reassignment. The
    # attributed selection must assign each captured font to the replay slot.
    payload = b"\x1b[19m\x1b[9;1 DAB\x1b[9;2 DC\x1b[10mD"
    source = t.exchange(b"\x1bF\x1b[9;1 D\x1b[19mAB\x1b[9;2 DC\x1b[10mD\x1bS",
                        ESC + b"XCTerm:STS:0:" + payload + ST, reset=True)
    replay = t.exchange(payload, reset=True)
    for x in range(4):
        assert tile(source, x + 1) == tile(replay, x + 1)
    frame = t.exchange(b"ABCD\x1b[1;1;1;2;19$r\x1b[1;1;1;1;1$t", reset=True)
    assert [cell_at(frame, x, 0).flags >> 27 for x in range(4)] == [9, 9, 0, 0]
    # Copy across concealed text after position 9 is reassigned.
    first, second = ((b"\xc0", b"\xc0") if t.encoding == "CP437"
                     else ("└".encode(), "А".encode()))
    concealed = "h界den" if t.encoding == "UTF-8" else "hidden"
    frame = t.exchange(b"\x1b[?25l\x1b[9;1 D" + first + b"\x1b[8m" + concealed.encode() + b"\x1b[28;19m" + second
                       + b"\x1b[9;0 D\x1b[10m", reset=True)
    cells = [cell_at(frame, x, 0) for x in (0, 7)]
    assert [frame.cell_text(cell) for cell in cells] == ["└", "А"]
    assert all(cell.glyph == 0xc0 and cell.glyph_font == cell.font for cell in cells)
    assert cells[0].font != cells[1].font
    m = frame.metadata
    x0 = m["destination_x"] + m["cell_width"] * m["destination_width"] // (2 * m["plane_width"])
    x1 = m["destination_x"] + 15 * m["cell_width"] * m["destination_width"] // (2 * m["plane_width"])
    y = m["destination_y"] + (m["pixel_top"] + m["cell_height"] // 2) * m["destination_height"] // m["plane_height"]
    t.client.mouse(x0, y, action=1, buttons=1)
    t.client.mouse(x1, y, buttons=1)
    selected = t.client.wait_for(lambda s: cell_at(s, 7, 0).background != cells[1].background)
    for x in range(1, 7):
        cell = cell_at(selected, x, 0)
        assert set(tile(frame, x + 1)) == {cell_at(frame, x, 0).background}
        assert set(tile(selected, x + 1)) == {cell.foreground, cell.background}
        assert cell.foreground != cell.background
    t.client.mouse(x1, y, action=1)
    restored = t.client.wait_for(lambda s: s.clipboard == "└" + concealed + "А")
    assert restored.primary == frame.primary and restored.alternate == frame.alternate
    assert all(cell_at(restored, x, 0).flags & 4 for x in range(1, 7))
    # Change the primary assignment and select the same font through bright.
    # A UTF-8 box corner retains its Unicode meaning under either assignment.
    char = b"\xc0" if t.encoding == "CP437" else "└".encode()
    frame = t.exchange(b"\x1b[0;1 D" + char + b"\x1b[0;0 D\x1b[1;1 D\x1b[?31h\x1b[1m"
                       + char + b"\x1b[22m" + char, reset=True)
    assert [frame.cell_text(cell_at(frame, x, 0)) for x in range(3)] == (
        ["А", "А", "└"] if t.encoding == "CP437" else ["└"] * 3)
    if t.encoding == "CP437":
        t.exchange(b"\x1bF\x1b[9;1 D\x1b[19m\xc0\x1b[10m\xc0\x1bS",
                   ESC + b"XCTerm:STS:0:\x1b[19m\x1b[9;1 D\xc0\x1b[10m\xc0" + ST, reset=True)
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
    t.check("KAM-permanently-reset", ESC + b"[2h" + ESC + b"[2$p", expected=ESC + b"[2;4$y")
    t.client.key(ord("q"))
    t.input(b"q")
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
    t.exchange(b"\x1b[9;1 D\x1b[19m\x1b[?2004h")
    t.client.text("é")
    t.input(b"\x82" if t.encoding == "CP437" else b"\xc3\xa9")
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
