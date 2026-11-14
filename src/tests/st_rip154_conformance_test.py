#!/usr/bin/env python3
"""RIPterm 1.54 historical pixel oracles and live wire/interaction regressions."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import hashlib
import json
from pathlib import Path
import zlib

from st_migrated_control_test import connection, click_text
from st_terminal_conformance_test import Terminal, ESC, colour
from syncterm_fixture import cell_at

DATA = Path(__file__).with_name("rip154")


def rgb6(frame, phase="primary"):
    m = frame.metadata
    assert m["plane_width"] == 640 and m["plane_height"] >= 350, m
    # Resolve palette entries from the SAME checkpoint as the pixel plane.
    rgb = (pixel if pixel & 0x80000000 else frame.palette[pixel]
           for pixel in getattr(frame, phase)[:640 * 350])
    return bytes((pixel >> shift) & 63 for pixel in rgb for shift in (18, 10, 2))


def capture_cases(args):
    manifest = json.loads((DATA / "captures.json").read_text(encoding="utf-8"))
    failures = []
    for row in manifest["cases"]:
        if row["group"] != args.group:
            continue
        try:
            with connection(args.executable, args.reports, encoding=args.encoding,
                            options="ScreenMode=80x43\nRIP=RIPv1\n", settings="AudioModes=0\n") as (client, root, peer, send):
                t = Terminal(client, root, peer, send)
                t.case(row["id"])
                wire = (DATA / (row["id"] + ".rip")).read_bytes()
                expected = zlib.decompress((DATA / (row["id"] + ".rgb6.z")).read_bytes())
                assert hashlib.sha256(wire).hexdigest() == row["input_sha256"]
                assert len(expected) == 640 * 350 * 3
                assert hashlib.sha256(expected).hexdigest() == row["rgb6_sha256"]
                # These text fixtures contain one command per source line.
                # CR terminates RIP wire commands; LF alone is tested separately.
                wire = wire.replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")
                frame = t.exchange(b"\r!|*\r\n" + wire + b"\r\n!|#\r\n")
                capture_ui = {y * 640 + x for left, top, width, height in row["capture_ui_rectangles"]
                              for y in range(top, top + height) for x in range(left, left + width)}
                for phase in ("primary", "alternate"):
                    actual = rgb6(frame, phase)
                    differences = [i for i in range(640 * 350) if i not in capture_ui
                                   and actual[i*3:i*3+3] != expected[i*3:i*3+3]]
                    if differences:
                        first = differences[0]
                        raise AssertionError((row["id"], phase, len(differences), "pixel differences; first",
                                              first % 640, first // 640,
                                              actual[first*3:first*3+3].hex(), expected[first*3:first*3+3].hex()))
                print("PASS " + row["id"], flush=True)
        except Exception as error:
            failures.append(str(error))
    assert not failures, "\n".join(failures)


def mega(value, width=2):
    digits = b"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    result = bytearray(width)
    for index in range(width - 1, -1, -1):
        result[index] = digits[value % 36]
        value //= 36
    assert value == 0
    return bytes(result)


def point(x, y):
    return mega(x) + mega(y)


def scene(t, commands=b""):
    # RIP_RESET intentionally preserves drawing styles. Set the baseline
    # explicitly when a case is not testing that preservation.
    return t.exchange(b"\r!|*\r\n!|c0F|S0102|W0|=00000001|Y00000100\r\n" + commands)


def pixel(frame, x, y, phase="primary"):
    value = getattr(frame, phase)[y * frame.metadata["plane_width"] + x]
    return (value if value & 0x80000000 else frame.palette[value]) & 0xffffff


def copy_cases(t, version):
    # Independent indexed scene. Expected copies use these specified pixels,
    # never observations of the emulator or its alignment implementation.
    indexes = (16, 20, 18, 22, 17, 21, 19, 23, 24, 28, 26, 30, 25, 29, 27, 31)
    seed = [16] * (640 * 350)
    drawing = []
    for y in (*range(10, 14), *range(340, 350)):
        for band in (*range(6), 79):
            ink = (y + band) % 15 + 1
            x = band * 8
            drawing.append(b"S01" + mega(ink) + b"|B" + point(x, y) + point(x + 7, y))
            seed[y * 640 + x:y * 640 + x + 8] = [indexes[ink]] * 8
    paint = b"!|" + b"|".join(drawing) + b"\r\n!|1C0A0A0B0B0\r\n"
    # Rectangles are x, source Y, width, inclusive-row count, destination Y.
    cases = (
        ("up", b"080A0O0C0002", (8, 10, 16, 3, 2), (8, 10, 16, 3, 2)),
        ("down-overlap", b"080A0O0D000B", (8, 10, 16, 4, 11), (8, 10, 16, 4, 11)),
        ("up-overlap", b"080A0O0D0009", (8, 10, 16, 4, 9), (8, 10, 16, 4, 9)),
        ("reversed", b"0O0D080A0002", (8, 10, 16, 4, 2), (8, 10, 16, 4, 2)),
        ("same-line", b"080A0O0C000A", None, None),
        ("one-row", b"080A0O0A0002", None, (8, 10, 16, 1, 2)),
        ("unaligned", b"0E0A120C0002", (8, 10, 24, 3, 2), (8, 10, 32, 3, 2)),
        ("reserved-positive", b"0E0A120C0702", (0, 10, 32, 3, 2), (8, 10, 32, 3, 2)),
        ("reserved-negative", b"0E0A120C-702", (16, 10, 16, 3, 2), (8, 10, 32, 3, 2)),
        ("negative-left", b"-A0A0O0C0002", (0, 10, 24, 3, 2), (0, 10, 24, 3, 2)),
        ("pre-clamp-height", b"089G0OB4008C", None, (8, 340, 16, 10, 300)),
        ("source-bottom-clamp", b"089G0OB40000", (8, 340, 16, 10, 0), (8, 340, 16, 10, 0)),
        ("large-copy", b"0000HR9O0001", (0, 0, 632, 349, 1), (0, 0, 640, 349, 1)),
        ("full-width", b"0000HR9O0101", (0, 0, 640, 349, 1), (0, 0, 640, 349, 1)),
        ("loose-bottom", b"080A0O0B009P", (8, 10, 16, 1, 349), None),
        ("destination-overflow", b"080A0O0D009O", None, None),
        ("missing-field", b"080A0O0C00", None, None),
        ("invalid-reserved", b"080A0O0C?002", None, None),
        ("resort-adjusted-down", b"080A090C-70K", (8, 10, 8, 3, 20), (8, 10, 8, 3, 20)),
        ("short-final-field", b"080A0O0C002", (8, 10, 16, 3, 2), (8, 10, 16, 3, 2)),
    )
    for number, (name, arguments, v1, v3) in enumerate(cases):
        scene(t, paint)
        viewport = b"v00000000" if number % 2 else b"v8C8C8M8M"
        frame = t.check(f"copy-v{version}-" + name,
                        b"!|W1|=04000003|c03|S0205|m0V15|" + viewport + b"|1G" + arguments
                        + b"|W0|v0000HR9P|1PGO96000|c0F|XH39E\r\n",
                        (1, 1), ((1, 1, " "),), reset=False)
        expected = seed.copy()
        rectangle = v1 if version == 1 else v3
        if rectangle:
            x, source_y, width, height, destination_y = rectangle
            for row in range(height):
                start = (destination_y + row) * 640 + x
                source = (source_y + row) * 640 + x
                expected[start:start + width] = seed[source:source + width]
        # The unrelated clipboard survives, and following commands execute
        # after valid, rejected and malformed copy requests alike.
        for row in range(2):
            expected[(330 + row) * 640 + 600:(330 + row) * 640 + 602] = seed[(10 + row) * 640 + 10:(10 + row) * 640 + 12]
        expected[338 * 640 + 615] = 31
        for phase in ("primary", "alternate"):
            actual = list(getattr(frame, phase)[:640 * 350])
            if actual != expected:
                first = next(i for i, (a, b) in enumerate(zip(actual, expected)) if a != b)
                raise AssertionError((name, version, phase, first % 640, first // 640,
                                      actual[first], expected[first]))


def numeric_cases(t):
    # Independent WORD/byte expectations; no rendered output is used to
    # generate an oracle. Every related cell/pixel/state uses this capture.
    for name, commands, y, left, right, rgb in (
        ("numeric-negative-pattern", b"=04-00101|L2S2S322S", 100, 100, 110, 0xffffff),
        ("numeric-unsigned-pattern", b"=041EKF01|L2S2S322S", 100, 100, 110, 0xffffff),
        ("numeric-word-wrap", b"=041EKG01|L2S2S322S", 100, 100, 110, 0),
        ("numeric-invalid-style", b"=-1000001|L2S2S322S", 100, 100, 110, 0xffffff),
        ("numeric-invalid-thickness", b"=040000-1|L2S2S322S", 100, 100, 110, 0xffffff),
        ("numeric-negative-colour", b"c-1|X2S2S", 100, 100, 100, 0xffffff),
        ("numeric-colour-byte-wrap", b"c74|X2S2S", 100, 100, 100, 0),
        ("numeric-negative-fill", b"S01-1|B2S2S322U", 101, 100, 110, 0xffffff),
        ("numeric-fill-bytes", b"s-1-1-1-1-1-1-1-10F|B2S2S322U", 101, 100, 110, 0xffffff),
        ("numeric-invalid-fill-colour", b"s0000000000000000-1|B2S2S322U", 101, 100, 110, 0x00aa00),
        ("numeric-signed-line", b"L-12S0A2S", 100, 0, 10, 0xffffff),
        ("numeric-signed-polyline", b"l02-12S0A2S-1-1", 100, 0, 10, 0xffffff),
        ("numeric-invalid-vertex-recovery", b"P-1-12S0A2S|L2S2S322S", 100, 100, 110, 0xffffff),
        ("numeric-invalid-viewport-recovery", b"v-1000A0A|L2S2S322S", 100, 100, 110, 0xffffff),
        ("numeric-negative-palette-index", b"a-10F|X2S2S", 100, 100, 100, 0xffffff),
        ("numeric-palette-byte", b"a01-1|c01|X2S2S", 100, 100, 100, 0xffffff),
        ("numeric-all-palette-bytes", b"Q" + b"-1" * 16 + b"|c01|X2S2S", 100, 100, 100, 0xffffff),
    ):
        scene(t)
        frame = t.check(name, b"!|" + commands + b"\r\n", reset=False)
        for phase in ("primary", "alternate"):
            assert all(pixel(frame, x, y, phase) == rgb for x in range(left, right + 1)), (name, phase)
    for name, command in (
        ("numeric-short-host-field", b"g001"),
        ("numeric-internal-minus-host", b"g0-01"),
        ("numeric-cursor-byte-wrap", b"g7401"),
    ):
        scene(t)
        t.check(name, b"!|" + command + b"\r\nZ", (2, 2), ((1, 2, "Z"),), reset=False)
    scene(t)
    # The byte becomes -1, whose one-based position is invalid. The
    # existing console setter rejects the entire move before changing Y.
    t.check("numeric-negative-cursor-byte", b"!|g-101\r\nZ",
            (2, 1), ((1, 1, "Z"),), reset=False)
    scene(t)
    t.check("numeric-host-mode-minus", b"!|1\x1b-000OK\r\n", expected=b"OK", reset=False)
    t.check("numeric-query-reserved-minus", b"!|1\x1b0-01OK\r\n", expected=b"OK", reset=False)
    t.check("numeric-file-query-reserved-minus", b"!|1F0--001MISSING.ICN\r\n", expected=b"0", reset=False)
    t.check("numeric-file-query-negative-mode", b"!|1F-10000MISSING.ICN\r\n", reset=False)
    frame = t.check("numeric-window-minus-style", b"!|w0000271612\r\n!|w000027161-\r\nZ",
                    (2, 1), ((1, 1, "Z"),), reset=False)
    assert frame.metadata["cell_height"] == 8
    frame = t.check("numeric-window-byte-wrap", b"\r!|w0074271610\r\nZ", (2, 1), ((1, 1, "Z"),), reset=False)
    assert frame.application["cterm.top_margin"] == 1


def wire_cases(t):
    copy_cases(t, 1)
    numeric_cases(t)
    t.check("rip-version", b"\x1b[!", expected=b"RIPSCRIP015410", reset=False)
    t.check("rip-version-zero", b"\x1b[0!", expected=b"RIPSCRIP015410", reset=False)
    # An unfinished ANSI prefix survives intervening RIP commands. Verify
    # replies, cells/colours, cursor and both pixel phases in one capture.
    # RIP text uses its EGA palette (aa channel steps).
    for name, data, reply, foreground in (
        ("interleaved-soh", b"\x1b[31\x01|1\x1b0000OK|c0F|X2S2S\r\nmZ", b"OK", 0xaa0000),
        ("interleaved-stx", b"\x1b[31\x02|1\x1b0000OK|c0F|X2S2S\r\nmZ", b"OK", 0xaa0000),
        ("interleaved-after-esc", b"\x1b\x01|c0F|X2S2S\r\n[31mZ", b"", 0xaa0000),
        ("interleaved-completion", b"\x1b[3\x01|c0F|X2S2S\rX1mZ", b"", 0xaa0000),
        ("interleaved-twice", b"\x1b[3\x01|1\x1b0000Q|c0F\r\n1\x02|1\x1b0000R|X2S2S\r\nmZ", b"QR", 0xaa0000),
        ("interleaved-argument-esc", b"\x1b[31\x01|1\x1b0000\x1b[!|c0F|X2S2S\r\nmZ", b"\x1b[!", 0xaa0000),
        ("interleaved-autosense", b"\x1b[0\x01|c0F|X2S2S\r\n!Z", b"RIPSCRIP015410", 0xaaaaaa),
    ):
        scene(t)
        frame = t.check(name, b"\x1b[0mA" + data, (3, 1), ((1, 1, "AZ"),),
                        expected=reply, reset=False)
        assert colour(frame, cell_at(frame, 0, 0)) == 0xaaaaaa, name
        assert colour(frame, cell_at(frame, 1, 0)) == foreground, name
        for phase in (frame.primary, frame.alternate):
            value = phase[100 * frame.metadata["plane_width"] + 100]
            rgb = (value if value & 0x80000000 else frame.palette[value]) & 0xffffff
            assert rgb == 0xffffff, (name, hex(rgb))
    scene(t)
    t.check("completion-before-new-ansi", b"!|#\r\x1b[31mZ", (6, 1),
            ((1, 1, "[31mZ"),), reset=False)
    scene(t)
    frame = t.check("consumed-byte-introducer-history",
                    b"ABC\x01|#\r!|c0F|X2S2S\r\nZ", (5, 1),
                    ((1, 1, "ABCZ"),), reset=False)
    assert pixel(frame, 100, 100) == 0xffffff
    for name, data, xy, text in (
        ("intro-midcolumn", b"ABC!|X2S2S\r\n", (1, 2), "ABC!|X2S2S"),
        ("double-bang-literal", b"!!|c0F\r\n", (1, 2), "!!|c0F"),
        ("lone-bang-literal", b"!\r\n", (1, 2), "!"),
        ("escaped-bang-literal", b"\\!|c0F\r\n", (1, 2), "\\!|c0F"),
        ("unknown-command-ebc", b"!|~ignored\r\nNEXT", (5, 1), "NEXT"),
        ("unknown-level-ebc", b"!|8qignored\r\nNEXT", (5, 1), "NEXT"),
        ("completion-vt", b"!|c0F\r\vNEXT", (5, 2), ""),
        ("completion-second-lf", b"!|c0F\r\n\nNEXT", (5, 2), ""),
        ("no-more-repeat", b"!|#|#|#\r\nNEXT", (5, 1), "NEXT"),
        ("no-more-lf-buffered", b"!|#\nDISCARDED\r\nNEXT", (5, 1), "NEXT"),
    ):
        scene(t)
        frame = t.check(name, data, xy, reset=False)
        assert frame.lines[0].startswith(text), (name, frame.lines[0], text)
        if not text:
            assert frame.lines[1].startswith("NEXT"), frame.lines[:2]
    for name, data in (
        ("lf-in-fixed-argument", b"!|c0\nF|X2S2S\r\n"),
        ("odd-slash-continuation", b"!|c0\\\r\nF|X2S2S\r\n"),
        ("cursor-column-introducer", b"ABC\x1b[1G!|X2S2S\r\n"),
        ("soh-midcolumn-introducer", b"ABC\x01|X2S2S\r\n"),
        ("stx-midcolumn-introducer", b"ABC\x02|X2S2S\r\n"),
        ("scene-after-no-more", b"!|#|#!|X2S2S\r\n"),
    ):
        scene(t)
        frame = t.check(name, data, reset=False)
        assert pixel(frame, 100, 100) == 0xffffff, (name, hex(pixel(frame, 100, 100)))
    for name, encoded, decoded in (
        ("query-escaped-pipe", b"A\\|B", b"A|B"),
        ("query-literal-slash", b"A\\\\B", b"A\\B"),
        ("query-triple-slash", b"A\\\\\\\r\nB", b"A\\B"),
        ("query-caret-escapes", b"A^m^j^@^z", b"A\r\n\0\x1a"),
        ("query-backquote-escapes", b"A`m`j", b"A\r\n"),
        ("query-version-variable", b"$RIPVER$", b"RIPSCRIP015410"),
        ("query-cp437-byte", b"\x82", b"\x82"),
    ):
        scene(t)
        t.check(name, b"!|1\x1b0000" + encoded + b"\r\n", expected=decoded, reset=False)
    t.case("reset-preserves-styles")
    scene(t, b"!|c04|S0102|W1|=00000003\r\n!|*\r\n!|L2S2S322S|B3C2S3E2U\r\n")
    frame = t.client.capture()
    assert all(pixel(frame, x, y) == 0xaa0000 for y in range(99, 102) for x in range(100, 111))
    assert pixel(frame, 120, 100) == 0x00aa00
    # Drawing the same retained XOR line a second time restores black.
    frame = t.exchange(b"!|L2S2S322S\r\n")
    assert all(pixel(frame, x, y) == 0 for y in range(99, 102) for x in range(100, 111))
    t.case("viewport-origin-clipping")
    frame = scene(t, b"!|v0A0A0K0K|L00000U00\r\n")
    assert pixel(frame, 9, 10) == 0 and pixel(frame, 21, 10) == 0
    assert all(pixel(frame, x, 10) == 0xffffff for x in range(10, 21))
    t.case("full-canvas-last-pixel")
    frame = scene(t, b"!|S0102|B0000HR9P\r\n")
    # This pixel is excluded only in the two historical mouse-contaminated
    # captures. Check the actual last drawable pixel independently here.
    assert pixel(frame, 639, 349) == 0x00aa00
    t.case("world-coordinate-mapping")
    frame = scene(t, b"!|fM8JG|X2S2S\r\n")
    assert pixel(frame, 80, 50) == 0xffffff
    assert pixel(frame, 100, 100) == 0
    t.case("packed-raster-edits")
    # BGI colour 4 now has the same visible RGB as colour 0. XOR must
    # still use its indexed bits, with repeated writes cancelling exactly.
    commands = (b"!|a0400|v" + point(100, 40) + point(109, 49)
                + b"|S0102|B" + point(0, 0) + point(9, 9)
                + b"|c04|W1|L" + point(0, 5) + point(9, 5)
                + b"|L" + point(2, 5) + point(7, 5)
                + b"|c01|L" + point(0, 9) + point(12, 9)
                + b"|1C" + point(0, 0) + point(1, 1) + b"0"
                + b"|v0000HR9P")
    for x, mode in ((200, 1), (204, 4), (208, 3), (212, 2)):
        commands += b"|1P" + point(x, 40) + mega(mode) + b"0"
    commands += (b"|v" + point(300, 40) + point(309, 49) + b"|c03|W0|R"
                 + point(0, 0) + point(9, 9) + b"|S0106|F010103|v0000HR9P")
    commands += b"|S010A|B" + point(638, 348) + point(639, 349) + b"\r\n"
    frame = scene(t, commands)
    m = frame.metadata
    assert (m["columns"], m["rows"], m["cell_width"], m["cell_height"]) == (80, 43, 8, 8)
    assert frame.palette[16] == frame.palette[17] == 0
    assert frame.palette[19] == 0xaa5500
    for phase in ("primary", "alternate"):
        plane = getattr(frame, phase)
        for y in range(32, 64):
            for x in range(90, 320):
                expected = 16
                if 100 <= x <= 109 and 40 <= y <= 49:
                    expected = 22 if y == 49 else 19 if y == 45 and x in (100, 101, 108, 109) else 18
                elif 300 <= x <= 309 and 40 <= y <= 49:
                    expected = 22 if x in (300, 309) or y in (40, 49) else 19
                elif y in (40, 41):
                    if x in (200, 201, 212, 213): expected = 18
                    elif x in (204, 205): expected = 29
                assert plane[y * 640 + x] == expected, (phase, x, y, plane[y * 640 + x], expected)
        for y in (348, 349):
            for x in range(640):
                assert plane[y * 640 + x] == (26 if x >= 638 else 16), (x, y)
    # The pinned executable loads the explicit 0.005 fallback, rather
    # than the residual 0x3fc asserted in the older research prose.
    # Literal samples of this cubic distinguish 200 from 1020 steps.
    curve = point(100, 100) + point(100, 140) + point(140, 140) + point(140, 100)
    for name, count in (("zero", b"00"), ("negative", b"-1"), ("explicit", b"5K")):
        t.case("bezier-default-count-" + name)
        frame = scene(t, b"!|Z" + curve + count + b"\r\n")
        for phase in ("primary", "alternate"):
            for x, y, rgb in ((100, 100, 0xffffff), (140, 100, 0xffffff),
                              (120, 130, 0xffffff), (119, 130, 0), (100, 110, 0)):
                assert pixel(frame, x, y, phase) == rgb, (name, phase, x, y)
    for style, columns, rows, width, height in (
        (0, 80, 43, 8, 8), (1, 91, 43, 7, 8), (2, 80, 25, 8, 14),
        (3, 91, 25, 7, 14), (4, 40, 25, 16, 14)):
        t.case(f"text-window-font-{style}")
        window = point(0, 0) + point(columns - 1, rows - 1) + b"1" + mega(style, 1)
        frame = scene(t, b"!|X2S2S\r\n!|w" + window + b"\r\nA")
        m = frame.metadata
        assert (m["columns"], m["rows"], m["cell_width"], m["cell_height"]) == (columns, rows, width, height)
        assert frame.lines[0].startswith("A") and pixel(frame, 100, 100) == 0xffffff
    t.case("text-window-disabled")
    frame = scene(t, b"!|w0000000000\r\nHIDDEN\r\n!|X2S2S\r\n")
    assert "HIDDEN" not in frame.text and pixel(frame, 100, 100) == 0xffffff
    t.case("invalid-polygon-count")
    before = scene(t)
    after = t.exchange(b"!|p052S2S322S3232\r\n")
    assert before.primary == after.primary
    t.case("invalid-font-reserved")
    frame = scene(t, b"!|Y010101|@2S2SA\r\n")
    rows = (56, 108, 198, 198, 254, 198, 198, 0)
    for y, bits in enumerate(rows):
        for x in range(8):
            assert pixel(frame, 100 + x, 100 + y) == (0xffffff if bits & (0x80 >> x) else 0)
    t.case("rip-font-encoding")
    text = "é".encode("utf-8" if t.encoding == "UTF-8" else "cp437")
    frame = scene(t, b"!|@2S2S" + text + b"\r\n")
    # Runtime RIPterm glyph extraction in rip_test/ripterm_8x8_font.py.
    rows = (28, 0, 120, 204, 252, 192, 120, 0)
    for y, bits in enumerate(rows):
        for x in range(8):
            assert pixel(frame, 100 + x, 100 + y) == (0xffffff if bits & (0x80 >> x) else 0)


def click_pixel(t, frame, x, y):
    m = frame.metadata
    px = m["destination_x"] + x * m["destination_width"] // m["plane_width"]
    py = m["destination_y"] + y * m["destination_height"] // m["plane_height"]
    t.client.mouse(px, py, action=1, buttons=1)
    t.client.mouse(px, py, action=1, buttons=0)


def interaction_cases(t):
    from syncterm_control import SHIFT
    # Issue #2: legacy terminal replacement lost both keyboard and RIP host
    # strings. Exercise repeated resets and every RIP 1.54 font on the socket.
    for name, command in [("reset", b"\r\n!|*\r\n")] + [
            (f"font-{style}", b"\r\n!|w00001B0M1" + mega(style, 1) + b"\r\n")
            for style in range(5)]:
        scene(t)
        t.client.text("a")
        t.input(b"a")
        for repeat in range(2):
            t.case(f"input-after-{name}-{repeat}")
            frame = t.exchange(command + b"\x1b[!", expected=b"RIPSCRIP015410")
            t.client.text("b")
            t.input(b"b")
            frame = t.exchange(
                b"\r!|1B140A0200ZK0300080F080700000707000000"
                b"|1U1O1E00002G00<>EXIT<>^M\r\n"
                b"!|1M002S2S2U2U1000000^N\r\n")
            click_pixel(t, frame, 61, 51)
            t.input(b"\r")
            click_pixel(t, frame, 101, 101)
            t.input(b"\x0e")
    t.case("mouse-invert-and-command")
    frame = scene(t, b"!|1M002S2S2U2U1000000^M\r\n")
    before = frame.primary[:]
    click_pixel(t, frame, 101, 101)
    frame = t.input(b"\r")
    assert frame.primary == before
    t.case("button-hotkey-case-folding")
    frame = scene(t, b"!|1B140A0200ZK0300080F080700000707000000|1U1O1E00002G00<>EXIT<>^M\r\n")
    for letter in ("x", "X"):
        t.client.text(letter)
        t.input(b"\r")
    t.case("button-font-retention")
    before = frame.primary[:]
    # 9c50d54228: drawing a button stores its font; later font changes must
    # not affect its pressed/released redraw or leak the button's saved font.
    frame = t.exchange(b"!|Y01000400\r\n")
    click_pixel(t, frame, 61, 51)
    frame = t.input(b"\r")
    assert frame.primary == before
    t.case("button-font-restore")
    frame = t.exchange(b"!|@8C2SA\r\n!|Y01000400|@CI2SA\r\n")
    width = frame.metadata["plane_width"]
    for y in range(100, 250):
        assert frame.primary[y * width + 300:y * width + 428] == frame.primary[y * width + 450:y * width + 578]
    t.case("mouse-save-kill-restore")
    t.exchange(b"!|$SMF$\r\n!|$MKILL$\r\n!|$RMF$\r\n")
    click_pixel(t, frame, 61, 51)
    t.input(b"\r")
    t.case("tab-forward-backward")
    frame = scene(t, b"!|1M002S2S2U2U1000000^M\r\n!|1M003C3C3E3E1000000^N\r\n")
    t.client.key("tab")
    t.client.key("enter")
    t.input(b"\r")
    t.client.key("tab", SHIFT)
    t.client.key("enter")
    t.input(b"\x0e")
    t.case("checkbox-toggle")
    frame = scene(t, b"!|1B140A0200ZK0300080F080700010707000000|1U7S2800000000<>C<>^M\r\n")
    off = frame.primary[:]
    click_pixel(t, frame, 281, 81)
    frame = t.input(b"\r")
    assert frame.primary != off
    click_pixel(t, frame, 281, 81)
    frame = t.input(b"\r")
    assert frame.primary == off
    t.case("radio-group-exclusion")
    frame = scene(t, b"!|1B140A020DMO0300080F080701000707000000"
                  b"|1U4G2800000000<>A<>^M|1U642800000000<>B<>^N\r\n")
    off_a, off_b = pixel(frame, 161, 81), pixel(frame, 221, 81)
    click_pixel(t, frame, 161, 81)
    frame = t.input(b"\r")
    assert pixel(frame, 161, 81) != off_a and pixel(frame, 221, 81) == off_b
    click_pixel(t, frame, 221, 81)
    frame = t.input(b"\x0e")
    assert pixel(frame, 161, 81) == off_a and pixel(frame, 221, 81) != off_b


def host_command_cases(t):
    scene(t)
    for name, command, expected in (
        ("template-define", b"[0:]S FORUM1 $?$^m", b""),
        ("template-use", b"[0]HELLO", b"S FORUM1 HELLO\r"),
        ("template-chain-define", b"[1:]<$?$>", b""),
        ("template-repeat", b"[11]X", b"<<X>>"),
        ("template-null", b"[]literal", b"literal"),
        ("template-undefined", b"[Z]PASS", b"PASS"),
    ):
        t.check(name, b"!|1\x1b0000" + command + b"\r\n", expected=expected, reset=False)
    t.case("playback-in-host-command")
    markers = list(t.root.rglob("syncterm-cache-name.txt"))
    assert len(markers) == 1, markers
    (markers[0].parent / "host-scene.rip").write_bytes(b"!|c04|X2S2S|1\x1b0000INNER\r\n")
    frame = t.exchange(b"!|1\x1b0000L$>host-scene.rip$R^m\r\n", expected=b"LINNERR\r")
    assert pixel(frame, 100, 100) == 0xaa0000
    t.check("template-playback-define", b"!|1\x1b0000[2:]$>host-scene.rip$$?$\r\n", reset=False)
    t.check("template-playback-use", b"!|1\x1b0000[2]END\r\n", expected=b"INNEREND", reset=False)
    t.case("checkbox-group-host-commands")
    frame = scene(t, b"!|1B140A0200ZK0300080F080702010707000000"
                  b"|1U2S2S00001T10<>A<>[2:]A|1U3M2S00001U10<>B<>[2:]B\r\n")
    t.exchange(b"!|1\x1b0000$?2$\r\n", expected=b"AB")
    selected = pixel(frame, 101, 101)
    t.client.text("a")
    t.client.wait_for(lambda f: pixel(f, 101, 101) != selected)
    t.exchange(b"!|1\x1b0000$?2$\r\n", expected=b"B")
    # Popup UI runs in the menu VM. Related pixels/state are read together
    # only after the selected host response and parsing fence have completed.
    before = scene(t, b"!|c04|X2S2S\r\n")
    for name, command, key, expected in (
        ("popup-hotkey", b"A((Pick recipient::Sysop@_H_ead Honcho,Joe@~B~rother))Z", "b", b"AJoeZ"),
        ("popup-cancel", b"A((Pick recipient::Sysop@_H_ead Honcho,Joe@~B~rother))Z", "escape", b"AZ"),
        ("popup-required", b"((*Pick recipient::Sysop@_H_ead Honcho,Joe@~B~rother))", "b", b"Joe"),
        ("popup-template-response", b"((Pick recipient::[0]HI@_H_ead Honcho,Joe@~B~rother))", "h", b"S FORUM1 HI\r"),
    ):
        t.case(name)
        t.send(b"\r!|1\x1b0000" + command + b"\r\n")
        t.client.wait_text("Pick recipient")
        if name == "popup-required":
            t.client.key("escape")
        if key == "escape":
            t.client.key(key)
        else:
            t.client.text(key)
        frame = t.input(expected)
        assert frame.primary == before.primary and frame.palette == before.palette, name
    t.case("popup-mouse")
    t.send(b"\r!|1\x1b0000((Pick recipient::Sysop@_H_ead Honcho,Joe@~B~rother))\r\n")
    popup = t.client.wait_text("Pick recipient")
    click_text(t.client, popup, "Brother [B]")
    frame = t.input(b"Joe")
    assert frame.primary == before.primary and frame.palette == before.palette
    t.case("popup-unicode")
    label = b"\x82" if t.encoding == "CP437" else "é".encode()
    t.send(b"\r!|1\x1b0000((Caf" + label + b"::OK@_" + label + b"_))\r\n")
    t.client.wait_text("Café")
    t.client.text("é")
    frame = t.input(b"OK")
    assert frame.primary == before.primary and frame.palette == before.palette


def live_cases(args):
    with connection(args.executable, args.reports, encoding=args.encoding,
                    options="ScreenMode=80x43\nRIP=RIPv1\n", settings="AudioModes=0\n") as (client, root, peer, send):
        t = Terminal(client, root, peer, send)
        t.encoding = args.encoding
        (wire_cases if args.group == "wire" else interaction_cases)(t)
        if args.group == "interaction":
            host_command_cases(t)
        print(f"Passed {len(t.completed)} live {args.group} cases", flush=True)
    if args.group == "wire":
        with connection(args.executable, args.reports, encoding=args.encoding,
                        options="ScreenMode=80x43\nRIP=RIPv3\n", settings="AudioModes=0\n") as (client, root, peer, send):
            t = Terminal(client, root, peer, send)
            copy_cases(t, 3)
            scene(t)
            frame = t.check("v3-escape-priority", b"\x1b[31\x01|c0F|X2S2S\r\nZ", reset=False)
            assert frame.application["cterm.graphics_mode"] == 2  # ST_CTERM_GRAPHICS_RIP_3
            assert pixel(frame, 100, 100) == 0
            frame = t.check("v3-interleaving-recovery", b"\r!|c0F|X2S2S\r\n", reset=False)
            assert pixel(frame, 100, 100) == 0xffffff


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("group", choices=("ellipses", "raster", "buttons", "wire", "interaction"))
    parser.add_argument("encoding", choices=("CP437", "UTF-8"))
    parser.add_argument("--reports", type=Path, default=Path("reports/test-control"))
    args = parser.parse_args()
    (live_cases if args.group in ("wire", "interaction") else capture_cases)(args)


if __name__ == "__main__":
    main()
