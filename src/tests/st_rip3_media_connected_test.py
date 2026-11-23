#!/usr/bin/env python3
"""Exercise local HCL image playback through the application and TCP."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path

from st_migrated_control_test import connection
from st_rip3_connected_test import pixel, query
from st_rip3_mouse_connected_test import field
from st_terminal_conformance_test import Terminal
from syncterm_control import SHIFT


def run(args):
    with connection(args.executable, args.reports, encoding=args.encoding,
                    options="ScreenMode=Custom\nRIP=RIPv3\n",
                    settings="CustomColumns=80\nCustomRows=60\nCustomFontHeight=8\nAudioModes=0\n") as (
                        client, root, peer, send):
        t = Terminal(client, root, peer, send)

        def values(program, expected=b""):
            return t.exchange(query(program, at_cursor=True), expected)

        t.exchange(b"\x1b[!", b"RIPSCRIP030001")
        owner, = (p for p in (root / "cache").glob("*/syncterm-cache-name.txt")
                  if p.read_text(encoding="utf-8").strip() == "Capture")
        assets = owner.parent / "RIP"
        assets.mkdir(exist_ok=True)
        fixture = Path(__file__).resolve().parents[1] / "media/pixel_image/tests/jpeg/gray.jpg"
        contents = fixture.read_bytes()
        (assets / "Gray.jpg").write_bytes(contents)
        (assets / "G$RAY.jpg").write_bytes(contents)
        (assets / "Broken.jpg").write_bytes(b"invalid JPEG")

        t.case("HCL JPEG uses current style and cached asset")
        # The asset's first eight source rows are gray 64, its last is gray 192.
        # The default indexed palette maps those to 69 and 186 respectively.
        t.exchange(b"\x01|*\r\n")
        values(b"$WORLD(CUR,640,480)$$-=PICTURE=gray$")
        send(query(b"$IFS(JPEG)$", at_cursor=True))
        supported = peer.recv(1)
        assert supported in (b"0", b"1"), supported
        frame = values(b"$IMGSTYLE(CUR,20,30,39,49,DELETE)$begin$(<<PICTURE>>$end", b"beginend")
        assert pixel(frame, 20, 30) == (0x454545 if supported == b"1" else 0)
        assert pixel(frame, 38, 48) == (0xbababa if supported == b"1" else 0)
        assert pixel(frame, 39, 30) == pixel(frame, 20, 49) == 0
        assert (assets / "Gray.jpg").read_bytes() == contents

        t.case("Playback consumes only its own source")
        baseline = frame
        frame = values(b"a$(Missing$bc$(Broken.jpg$def", b"abcdef")
        assert frame.primary == baseline.primary and frame.alternate == baseline.alternate
        values(b"$(123456789012345678901$ok", b"$(123456789012345678901$ok")
        values(b"before$(unclosed", b"before$(unclosed")
        values(b"alive", b"alive")

        t.case("Refresh also expands playback with a quoted filename dollar")
        values(b"$IMGSTYLE(CUR,50,60,69,79)$")
        t.exchange(b"\x01|2R0000pre$(G\\$RAY$post\r\n")
        frame = values(b"$REFRESH$done", b"prepostdone")
        assert pixel(frame, 50, 60) == (0x454545 if supported == b"1" else 0)
        assert pixel(frame, 68, 78) == (0xbababa if supported == b"1" else 0)
        assert frame.application["connected"] == 1

        t.case("Playback substitutes the current field, capture and modifiers")
        for name in ("F010.jpg", "F110.jpg", "F7010.jpg"):
            (assets / name).write_bytes(contents)
        client.physical_key(42, True, SHIFT)
        frame = values(b"$IMGSTYLE(CUR,80,90,99,109)$$(f*?[]$outside", b"outside")
        assert pixel(frame, 80, 90) == (0x454545 if supported == b"1" else 0)

        t.exchange(field((12, 20, 40, 40), b"ID=7:$MKILL$$(f*?[]$release"))
        m = frame.metadata

        def mouse(x, y, buttons, action=1):
            dx = m["destination_x"] + (2*x+1)*m["destination_width"]//(2*m["plane_width"])
            dy = m["destination_y"] + (2*y+1)*m["destination_height"]//(2*m["plane_height"])
            before = client.capture().metadata["events_dequeued"]
            client.mouse(dx, dy, action=action, buttons=buttons)
            client.wait_for(lambda s: s.metadata["events_dequeued"] > before)

        mouse(20, 24, 1)
        mouse(55, 52, 1, action=0)
        frame = values(b"$IMGSTYLE(CUR,110,120,129,139)$$(f*?[]$held", b"held")
        assert pixel(frame, 110, 120) == (0x454545 if supported == b"1" else 0)
        mouse(20, 24, 1, action=0)
        values(b"$IMGSTYLE(CUR,140,150,159,169)$")
        mouse(20, 24, 0)
        frame = t.input(b"release")
        assert pixel(frame, 140, 150) == (0x454545 if supported == b"1" else 0)
        client.physical_key(42, False)

        t.case("Playback bounds count decoded wire bytes rather than normalized storage")
        label = "é" * (19 if args.encoding == "CP437" else 9)
        label += "" if args.encoding == "CP437" else "A"
        (assets / (label + "$.jpg")).write_bytes(contents)
        encoded = label.encode("cp437" if args.encoding == "CP437" else "utf-8")
        assert len(encoded) == 19
        frame = values(b"$IMGSTYLE(CUR,170,180,189,199)$$( " + encoded + b"\\$$raw",
                       b"$( " + encoded + b"\\$$raw")
        assert pixel(frame, 170, 180) == 0
        frame = values(b"$(" + encoded + b"\\$$good", b"good")
        assert pixel(frame, 170, 180) == (0x454545 if supported == b"1" else 0)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("encoding", choices=("CP437", "UTF-8"))
    parser.add_argument("--reports", type=Path, required=True)
    run(parser.parse_args())
