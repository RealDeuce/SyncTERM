#!/usr/bin/env python3
"""RIPv3 host pick lists over TCP with a real positioned bitmap dialog."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path
import select

from st_migrated_control_test import connection, square_canvas
from st_rip3_buttons_connected_test import button as rip_button, style
from st_rip3_connected_test import pixel, query
from st_rip3_mouse_connected_test import field
from st_terminal_conformance_test import Terminal


QUESTION = 0x29395b
FOCUS = 0x2354a1
HOTKEY = 0xffdc79


def run(args, width, height):
    with connection(args.executable, args.reports, encoding=args.encoding,
                    options="ScreenMode=Custom\nRIP=RIPv3\n",
                    settings=square_canvas(width, height)) as (client, root, peer, send):
        t = Terminal(client, root, peer, send)

        def packet(source):
            return query(source, at_cursor=True)

        def quiet():
            assert not select.select([peer], [], [], 0.05)[0], "response before selection"

        def modal(x, y, color=QUESTION):
            return client.wait_for(lambda frame: pixel(frame, x, y) == color)

        def button(x, y, buttons):
            px = m["destination_x"] + (2*x + 1)*m["destination_width"] // (2*m["plane_width"])
            py = m["destination_y"] + (2*y + 1)*m["destination_height"] // (2*m["plane_height"])
            event(lambda: client.mouse(px, py, action=1, buttons=int(buttons)))

        def event(post):
            before = client.capture().metadata["events_dequeued"]
            post()
            return client.wait_for(lambda frame: frame.metadata["events_dequeued"] > before)

        t.exchange(b"\x1b[!", b"RIPSCRIP030001")
        before = t.exchange(packet(f"$WORLD(CUR,{width * 2},{height * 2})$".encode())
                            + b"\x1b[5mNative blink\x1b[0m")
        m = before.metadata
        t.case(f"rip3-pick-list-world-position-and-raster-restore-{width}x{height}")
        send(packet(b"pre((3,5:Q::A@First,B@_S_econd))post"))
        frame = modal(1, 2)
        assert pixel(frame, 0, 2) == pixel(before, 0, 2)
        assert pixel(frame, 1, 1) == pixel(before, 1, 1)
        assert frame.cells == before.cells
        assert any(pixel(frame, x, y) == HOTKEY for x in range(9, 17) for y in range(34, 50))
        quiet()
        button(3, 36, True)
        button(3, 36, False)
        after = t.input(b"preBpost")
        assert (after.primary, after.alternate, after.cells) == (before.primary, before.alternate, before.cells)
        assert after.metadata["pixel_cursor_visible"] == before.metadata["pixel_cursor_visible"]

        # Center the omitted X axis and clamp the supplied Y independently.
        send(packet(b"((,65535:Q::A))"))
        modal((width - 24)//2, height - 32)
        client.key("enter")
        t.input(b"A")

        if width != 640:
            return

        t.case("rip3-pick-list-button-chords-preserve-the-original-press")
        send(packet(b"pre((0,0:Q::A,B))post"))
        modal(0, 0)
        button(3, 20, 1)  # Left press on A.
        button(3, 35, 5)  # Right press on B while left is held.
        button(3, 35, 1)  # Release right, leaving left held.
        button(3, 35, 0)  # Release left over a different choice.
        quiet()
        modal(0, 16, FOCUS)
        button(3, 35, 1)
        button(3, 35, 0)
        t.input(b"preBpost")

        t.case("rip3-pick-list-required-and-optional-during-scene-lock")
        send(b"\x01|h01000400\r\n" + packet(b"pre((*0,0:Q::A,B@_S_))post")
             + b"\x01|#\r\n" + packet(b"next"))
        modal(0, 0)
        event(lambda: client.key("escape"))
        quiet()
        assert pixel(client.capture(), 0, 0) == QUESTION
        client.text("s")
        t.input(b"preBpostnext")
        send(packet(b"pre((0,0:Q::A,B))post"))
        modal(0, 0)
        client.key("escape")
        t.input(b"prepost")

        t.case("rip3-pick-list-pass-boundaries-and-malformed-recovery")
        t.exchange(packet(b"[1:]((0,0:Q::^m))"))
        send(packet(b"$?1$"))
        modal(0, 0)
        client.key("enter")
        t.input(b"^m")
        send(packet(b"((0,0:Q::^m))"))
        modal(0, 0)
        client.key("enter")
        t.input(b"\r")
        t.exchange(packet(b"((Q::A@D@E,B))((::))pre((A^m") + packet(b"alive"),
                   b"pre((A\ralive")

        t.case("rip3-pick-list-refresh-fields-and-buttons")
        t.exchange(b"\x01|2R0000((0,0:Q::ready))\r\n")
        client.key("f5", 4)
        modal(0, 0)
        client.key("enter")
        t.input(b"ready")
        send(packet(b"pre$REFRESH$post"))
        modal(0, 0)
        client.key("enter")
        t.input(b"readyprepost")
        t.exchange(field((20, 60, 60, 90), b"((0,0:Q::field))"))
        button(12, 32, True)
        button(12, 32, False)
        modal(0, 0)
        client.key("enter")
        t.input(b"field")
        t.exchange(style(flags=256 | 1024) + rip_button((100, 50, 160, 80),
                   b"ID=7:<>Open<>((0,0:Q::$FIELDID$))", hotkey=ord("O")))
        client.text("o")
        modal(0, 0)
        client.key("enter")
        t.input(b"7")

        t.case("rip3-pick-list-64-entries-and-local-hotkey-encoding")
        entries = b",".join(f"{i}@item {i:02}".encode() for i in range(1, 66))
        send(packet(b"((65535,65535:Q::" + entries + b"))"))
        x, y = width - 72, height - 19*16
        modal(x, y)
        event(lambda: client.key("end"))
        modal(x, height - 16, FOCUS)
        button(x + 1, height - 8, True)
        button(x + 1, height - 8, False)
        t.input(b"64")
        accent = "é".encode("cp437" if args.encoding == "CP437" else "utf-8")
        send(packet(b"((0,0:Q::chosen@_" + accent + b"_,other))"))
        modal(0, 0)
        client.text("É")
        t.input(b"chosen")

        t.case("rip3-pick-list-zero-world-axis-and-wire-byte-limits")
        t.exchange(packet(f"$WORLD(CUR,0,{height * 2})$".encode()))
        send(packet(b"((23,41:Q::A))"))
        modal((width - 24)//2, 20)
        client.key("enter")
        t.input(b"A")
        # CP437 encodes a full block as one wire byte and three UTF-8 bytes
        # after normalization. The response and the entire list have distinct
        # limits even when the internal UTF-8 form is longer than either.
        symbol = "█".encode("cp437") if args.encoding == "CP437" else b"T"
        send(packet(b"((0,0:Q::" + symbol * 1024 + b"@one," + symbol * 1024 + b"@two))"))
        modal((width - 40)//2, 0)
        client.key("enter")
        t.input(symbol * 1023)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("encoding", choices=("CP437", "UTF-8"))
    parser.add_argument("--reports", type=Path, required=True)
    args = parser.parse_args()
    for width, height in ((640, 480), (800, 600), (1024, 768), (720, 400)):
        run(args, width, height)


if __name__ == "__main__":
    main()
