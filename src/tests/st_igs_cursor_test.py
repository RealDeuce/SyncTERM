#!/usr/bin/env python3
"""IGS k visibility, palette index inversion and serial erase."""
# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause
import argparse
from array import array
import socket

from st_igs_console_color_test import DATA, fixture, initial
from syncterm_fixture import cell_at, check_pixel_planes


def check(frame, resolution, status, cursor_x, glyph, line, visible, inverted, palette=None):
    width = 320 if resolution == 0 else 640
    height = 400 if resolution == 2 else 200
    cell_height = 16 if resolution == 2 else 8
    mask = (15, 3, 1)[resolution]
    rows = 24 if status else 25
    metadata = frame.metadata
    state = frame.application
    assert (metadata["plane_width"], metadata["plane_height"]) == (width, height)
    assert (state["cterm.x"], state["cterm.y"]) == (cursor_x + 1, 3)
    assert metadata["cursor_visible"] == visible
    assert tuple(frame.palette[768:784]) == tuple(initial(resolution) if palette is None else palette)

    expected = array("I", [768]) * (width * rows * cell_height)
    top = 2 * cell_height
    if glyph == "A":
        for row, bits in enumerate(DATA["fonts"][str(cell_height)]["65"]):
            for col in range(8):
                if bits & (128 >> col):
                    expected[(top + row) * width + 24 + col] = 768 + (3 & mask)
    if line:
        start = (top + cell_height - 1) * width + 24
        expected[start:start + 8] = array("I", [768 + mask]) * 8
    else:
        cell = cell_at(frame, 3, 2)
        assert cell.glyph == ord(glyph)
        assert cell.background == 768
    check_pixel_planes(frame, expected, context=(resolution, status, cursor_x, glyph, line, visible, inverted))

    for y in range(top, top + cell_height):
        for x in range(23, 41):
            index = expected[y * width + x] - 768
            if inverted and cursor_x * 8 <= x < cursor_x * 8 + 8:
                index ^= mask
            ox = metadata["destination_x"] + (2*x + 1) * metadata["destination_width"] // (2*width)
            oy = metadata["destination_y"] + (2*y + 1) * metadata["destination_height"] // (2*height)
            assert frame.rgb[oy * metadata["output_width"] + ox] & 0xffffff == frame.palette[768 + index], (
                resolution, status, x, y, cursor_x, inverted,
            )


def run(executable, reports, resolution, encoding, status):
    screen = ("AtariST40x25", "AtariST80x25", "AtariST80x25Mono")[resolution]
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(2)
        listener.settimeout(15)
        entries = (
            "[IGS cursor]\nConnectionType=Raw\nAddress=127.0.0.1\n"
            f"Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\nScreenMode={screen}\n"
            f"Font=Atari ST\nEncoding={encoding}\nNoStatus={str(not status).lower()}\n"
        )
        settings = "AudioModes=0\nMidiModes=0\nBlockyScaling=true\n"
        with fixture(executable, entries=entries, settings=settings, reports=reports, quiet=False) as (client, root):
            client.wait_text("IGS cursor")
            client.key("enter")
            marker = 1000

            def send(peer, wire):
                with (root / "wire.bin").open("ab") as log:
                    log.write(wire)
                peer.sendall(wire)

            def scene(peer, wire, cursor_x, glyph="A", line=False, visible=False, inverted=False):
                nonlocal marker
                marker += 1
                send(peer, wire + f"G#X 10,{marker},777:".encode())
                frame = client.wait_for(lambda f: f.application.get("cterm.igs_draw_x") == marker)
                check(frame, resolution, status, cursor_x, glyph, line, visible, inverted)

            def disconnect(peer):
                peer.shutdown(socket.SHUT_RDWR)
                client.wait_text("Disconnected: Enter exits, F1 details")
                client.key("enter")
                client.wait_text("Directory: Name")

            setup = b"G#c>1,3:c 0,0:\x1bEG#p 3,2:A"
            with listener.accept()[0] as peer:
                client.wait_for(lambda frame: frame.application.get("connected") == 1)
                client.resize(1280, 960)
                client.presentation_time(4000)
                scene(peer, setup, 4)
                scene(peer, b"G#p>3,2:k>0:k 1:", 3, visible=True, inverted=True)
                client.presentation_time(4499)
                check(client.capture(), resolution, status, 3, "A", False, True, True)
                client.presentation_time(4500)
                scene(peer, b"G#k 1:", 3, visible=True)
                scene(peer, b"G#m 3,1:", 4, visible=True, inverted=True)
                row = 47 if resolution == 2 else 23
                scene(peer, f"G#p>3,2:L 24,{row},31,{row}:".encode(),
                      3, line=True, visible=True, inverted=True)
                scene(peer, b"G#k 0:", 3, line=True)
                scene(peer, b"G#p>4,2:k 2:\b", 3, glyph=" ")
                scene(peer, b"AG#k 3:\b", 3)
                peer.settimeout(0.03)
                try:
                    reply = peer.recv(1)
                except socket.timeout:
                    pass
                else:
                    raise AssertionError(f"unexpected k reply: {reply!r}")
                send(peer, b"G#k 2:G#k 3")
                disconnect(peer)

            client.key("enter")
            with listener.accept()[0] as peer:
                client.wait_for(lambda frame: frame.application.get("connected") == 1)
                client.resize(1280, 960)
                scene(peer, setup + b"\b", 3)
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
