#!/usr/bin/env python3
"""Coherent screen, console and palette expectations for IGS s."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from array import array
import socket

from st_igs_console_color_test import DATA, fixture, initial
from syncterm_fixture import cell_at, check_pixel_planes


def check(frame, resolution, status, marker, selector, reverse):
    width = 320 if resolution == 0 else 640
    height, ch = (400, 16) if resolution == 2 else (200, 8)
    columns = width // 8
    rows = 24 if status else 25
    mask = (15, 3, 1)[resolution]
    fg, bg = 768 + (4 & mask), 768 + (3 & mask)
    erase_color = bg if selector < 3 else 768
    metadata, state = frame.metadata, frame.application
    assert (metadata["plane_width"], metadata["plane_height"]) == (width, height)
    assert state["cterm.igs_resolution"] == resolution and state["cterm.graphics_mode"] == 5
    cursor = (5, 3) if selector in (1, 2, 3) else (1, 1)
    assert (state["cterm.x"], state["cterm.y"]) == cursor
    assert state["cterm.igs_console_reverse"] == reverse
    raw_fg, raw_bg = (4 - 32) & 65535, (3 - 32) & 65535
    if selector == 5:
        raw_fg, raw_bg = (31 if resolution == 0 else 19), 16
    assert state["cterm.igs_console_foreground"] == raw_fg
    assert state["cterm.igs_console_background"] == raw_bg
    assert not state["cterm.igs_console_escape"]
    assert state["cterm.igs_console_parameters"] == (2 if selector == 3 else 0)
    assert (state["cterm.igs_draw_x"], state["cterm.igs_draw_y"]) == (marker, 777)
    palette = initial(resolution)
    palette[0] = 0
    if resolution == 2:
        palette[1] = 0xffffff
    assert tuple(frame.palette[768:784]) == tuple(palette)

    pixels = array("I", [erase_color]) * (width * rows * ch)
    letters = {(1, 1), (1, 2), (4, 2), (7, 2), (1, 3)}
    stripes = (2 * ch - 1, 3 * ch - 1, 4 * ch - 1)
    if selector in (1, 2):
        original = array("I", [768]) * len(pixels)
        for x, y in letters:
            for gy, bits in enumerate(DATA["fonts"][str(ch)]["65"]):
                for gx in range(8):
                    original[(y * ch + gy) * width + 8 * x + gx] = fg if bits & (128 >> gx) else bg
        for stripe in stripes:
            offset = stripe * width
            original[offset:offset + width] = array("I", [768 + mask]) * width
        for y in range(rows * ch):
            if selector == 1 and y >= 2 * ch:
                left = 5 * 8 if y < 3 * ch else 0
                pixels[y * width + left:(y + 1) * width] = original[y * width + left:(y + 1) * width]
            elif selector == 2 and y < 3 * ch:
                right = width if y < 2 * ch else 4 * 8
                pixels[y * width:y * width + right] = original[y * width:y * width + right]
    check_pixel_planes(frame, pixels, context=(resolution, status, selector))

    for y in range(rows):
        for x in range(columns):
            cell = cell_at(frame, x, y)
            retained = (selector == 1 and (y, x) > (2, 4)) or (selector == 2 and (y, x) < (2, 4))
            letter = retained and (x, y) in letters
            assert cell.glyph == (65 if letter else 32), (resolution, status, selector, x, y)
            assert cell.background == (bg if letter else 768 if retained else erase_color)
            if not retained:
                assert not cell.flags & 8  # ST_CONSOLE_CELL_PIXEL_GRAPHICS
    if status:
        assert all(not cell_at(frame, x, rows).flags & 8 for x in range(columns))

    for y in (0, ch, 2 * ch - 1, 2 * ch + 3, 3 * ch - 1, 4 * ch - 1, rows * ch - 1):
        for x in (0, 7, 8, 33, 39, 40, 57, width - 1):
            index = pixels[y * width + x]
            ox = metadata["destination_x"] + (2 * x + 1) * metadata["destination_width"] // (2 * width)
            oy = metadata["destination_y"] + (2 * y + 1) * metadata["destination_height"] // (2 * height)
            rgb = frame.rgb[oy * metadata["output_width"] + ox] & 0xffffff
            assert rgb == frame.palette[index], (resolution, status, selector, x, y, rgb)


def run(executable, reports, resolution, encoding, status):
    screen = ("AtariST40x25", "AtariST80x25", "AtariST80x25Mono")[resolution]
    width = 320 if resolution == 0 else 640
    ch = 16 if resolution == 2 else 8
    stripes = (2 * ch - 1, 3 * ch - 1, 4 * ch - 1)
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(2)
        listener.settimeout(15)
        entries = (
            "[IGS screen erase]\nConnectionType=Raw\nAddress=127.0.0.1\n"
            f"Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\nScreenMode={screen}\n"
            f"Font=Atari ST\nEncoding={encoding}\nNoStatus={str(not status).lower()}\n"
        )
        settings = "AudioModes=0\nMidiModes=0\nBlockyScaling=true\n"
        with fixture(executable, entries=entries, settings=settings, reports=reports, quiet=False) as (client, root):
            client.wait_text("IGS screen erase")
            client.key("enter")

            def send(peer, wire):
                with (root / "wire.bin").open("ab") as log:
                    log.write(wire)
                peer.sendall(wire)

            def inspect(peer, number, selector, *, pending=b"", prefix=b"", reverse=True):
                marker = 1000 + number
                setup = (
                    "\x1bq\x1bw\x1bfG#c>1,15:c 0,0:\x1bE"
                    "G#c>1,4:c>0,3:S 0,0,0,0:"
                    "G#p 1,1:AG#p 1,2:AG#p 4,2:AG#p 7,2:AG#p 1,3:A"
                    + "".join(f"G#L 0,{y},{width - 1},{y}:" for y in stripes)
                    + "G#p 4,2:\x1bp"
                )
                send(peer, prefix + setup.encode() + pending + f"G#s {selector}:G#X 10,{marker},777:".encode())
                frame = client.wait_for(lambda f: f.application.get("cterm.igs_draw_x") == marker)
                check(frame, resolution, status, marker, selector, reverse)

            def disconnect(peer):
                peer.shutdown(socket.SHUT_RDWR)
                client.wait_text("Disconnected: Enter exits, F1 details")
                client.key("enter")
                client.wait_text("Directory: Name")

            with listener.accept()[0] as peer:
                client.wait_for(lambda f: f.application.get("connected") == 1)
                client.resize(1280, 960)
                for selector in range(6):
                    pending = b"\x1bY" if selector == 3 else b"\x1bvG#p 255,255:" if selector == 5 else b""
                    prefix = b"\x01\x01" if selector == 4 else b""
                    inspect(peer, selector, selector, pending=pending, prefix=prefix)
                peer.settimeout(0.03)
                try:
                    reply = peer.recv(1)
                except socket.timeout:
                    pass
                else:
                    raise AssertionError(f"unexpected s reply: {reply!r}")
                send(peer, b"G#s 3")
                disconnect(peer)

            client.key("enter")
            with listener.accept()[0] as peer:
                client.wait_for(lambda f: f.application.get("connected") == 1)
                client.resize(1280, 960)
                inspect(peer, 6, 5, pending=b"\x1b", reverse=False)
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
