#!/usr/bin/env python3
"""IGS l and raw Atari VT52 erase on an indexed text/graphics scene."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from array import array
import socket

from st_igs_console_color_test import DATA, fixture, initial
from syncterm_fixture import cell_at, check_pixel_planes


def check(frame, resolution, status, marker, row, erased, cursor, visible):
    width = 320 if resolution == 0 else 640
    height, ch = (400, 16) if resolution == 2 else (200, 8)
    columns = width // 8
    rows = 24 if status else 25
    mask = (15, 3, 1)[resolution]
    fg, bg = 768 + (4 & mask), 768 + (3 & mask)
    left, right = erased
    metadata, state = frame.metadata, frame.application
    assert (metadata["plane_width"], metadata["plane_height"]) == (width, height)
    assert state["cterm.igs_resolution"] == resolution and state["cterm.graphics_mode"] == 5
    assert (state["cterm.x"], state["cterm.y"]) == (cursor + 1, row + 1)
    assert metadata["cursor_visible"] == visible
    assert state["cterm.igs_console_reverse"] == 1
    assert state["cterm.igs_console_foreground"] == (4 - 32) & 65535
    assert state["cterm.igs_console_background"] == (3 - 32) & 65535
    assert not state["cterm.igs_console_escape"] and not state["cterm.igs_console_parameters"]
    assert (state["cterm.igs_draw_x"], state["cterm.igs_draw_y"]) == (marker, 777)
    assert tuple(frame.palette[768:784]) == tuple(initial(resolution))

    letters = (1, 4, 7)
    pixels = array("I", [768]) * (width * rows * ch)
    for column in letters:
        for gy, bits in enumerate(DATA["fonts"][str(ch)]["65"]):
            for gx in range(8):
                pixels[(row * ch + gy) * width + 8 * column + gx] = fg if bits & (128 >> gx) else bg
    for stripe in (row * ch - 1, (row + 1) * ch - 1):
        offset = stripe * width
        pixels[offset:offset + width] = array("I", [768 + mask]) * width
    fill = array("I", [bg]) * ((right - left + 1) * 8)
    for y in range(row * ch, (row + 1) * ch):
        offset = y * width + left * 8
        pixels[offset:offset + len(fill)] = fill
    check_pixel_planes(frame, pixels, context=(resolution, status, row, erased, cursor))

    for y in range(rows):
        for x in range(columns):
            cell = cell_at(frame, x, y)
            blanked = y == row and left <= x <= right
            letter = y == row and x in letters and not blanked
            assert cell.glyph == (65 if letter else 32), (resolution, status, row, erased, x, y)
            assert cell.background == (bg if blanked or letter else 768)
            if letter:
                assert cell.foreground == fg
            if blanked:
                assert not cell.flags & 8  # ST_CONSOLE_CELL_PIXEL_GRAPHICS
    if status:
        assert all(not cell_at(frame, x, rows).flags & 8 for x in range(columns))

    for y in range(row * ch - 1, (row + 1) * ch):
        for x in (*range(72), width - 1):
            index = pixels[y * width + x] - 768
            if visible and y >= row * ch and x // 8 == cursor:
                index ^= mask
            ox = metadata["destination_x"] + (2 * x + 1) * metadata["destination_width"] // (2 * width)
            oy = metadata["destination_y"] + (2 * y + 1) * metadata["destination_height"] // (2 * height)
            rgb = frame.rgb[oy * metadata["output_width"] + ox] & 0xffffff
            assert rgb == frame.palette[768 + index], (resolution, status, erased, x, y, rgb)


def run(executable, reports, resolution, encoding, status):
    screen = ("AtariST40x25", "AtariST80x25", "AtariST80x25Mono")[resolution]
    width = 320 if resolution == 0 else 640
    ch = 16 if resolution == 2 else 8
    right = width // 8 - 1
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(2)
        listener.settimeout(15)
        entries = (
            "[IGS line erase]\nConnectionType=Raw\nAddress=127.0.0.1\n"
            f"Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\nScreenMode={screen}\n"
            f"Font=Atari ST\nEncoding={encoding}\nNoStatus={str(not status).lower()}\n"
        )
        settings = "AudioModes=0\nMidiModes=0\nBlockyScaling=true\n"
        with fixture(executable, entries=entries, settings=settings, reports=reports, quiet=False) as (client, root):
            client.wait_text("IGS line erase")
            client.key("enter")

            def send(peer, wire):
                with (root / "wire.bin").open("ab") as log:
                    log.write(wire)
                peer.sendall(wire)

            def inspect(peer, number, row, wire, erased, cursor, visible=False):
                marker = 1000 + number
                setup = (
                    "\x1bq\x1bwG#c>1,15:c 0,0:\x1bEG#c>1,4:c 0,3:"
                    f"G#p 1,{row}:AG#p 4,{row}:AG#p 7,{row}:A"
                    f"G#L>0,{row * ch - 1},{width - 1},{row * ch - 1}:"
                    f"L>0,{(row + 1) * ch - 1},{width - 1},{(row + 1) * ch - 1}:p 4,{row}:\x1bp"
                )
                send(peer, setup.encode() + wire + f"G#X 10,{marker},777:".encode())
                frame = client.wait_for(lambda f: f.application.get("cterm.igs_draw_x") == marker)
                check(frame, resolution, status, marker, row, erased, cursor, visible)

            def disconnect(peer):
                peer.shutdown(socket.SHUT_RDWR)
                client.wait_text("Disconnected: Enter exits, F1 details")
                client.key("enter")
                client.wait_text("Directory: Name")

            with listener.accept()[0] as peer:
                client.wait_for(lambda f: f.application.get("connected") == 1)
                client.resize(1280, 960)
                client.presentation_time(4000)
                inspect(peer, 0, 2, b"G#l 0:", (0, right), 1)
                inspect(peer, 1, 2, b"\x1bfG#l 1:", (0, 4), 4)
                inspect(peer, 2, 23 if status else 24, b"G#l 2:", (4, right), 4)
                inspect(peer, 3, 2, b"\x1beG#l 9999:", (0, right), 0, visible=True)
                peer.settimeout(0.03)
                try:
                    reply = peer.recv(1)
                except socket.timeout:
                    pass
                else:
                    raise AssertionError(f"unexpected l reply: {reply!r}")
                send(peer, b"\x1bf\x1bfG#l 2")
                disconnect(peer)

            client.key("enter")
            with listener.accept()[0] as peer:
                client.wait_for(lambda f: f.application.get("connected") == 1)
                client.resize(1280, 960)
                inspect(peer, 4, 2, b"\x1bl", (0, right), 1)
                disconnect(peer)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("executable")
    parser.add_argument("--reports", default="reports/test-control")
    args = parser.parse_args()
    for resolution in range(3):
        for encoding in ("ATARI-ST", "CP437", "UTF-8"):
            for status in (False, True):
                run(args.executable, args.reports, resolution, encoding, status)
