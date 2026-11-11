#!/usr/bin/env python3
"""X7 source patterns affect drawing only after an explicit A4 copy."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from array import array
import socket

from st_igs_cursor_test import DATA, fixture, initial
from syncterm_fixture import cell_at, check_pixel_planes

ROWS = (0x8000, 0x4000, 0x2000, 0x1000, 0x0800, 0x0400, 0x0200, 0x0100,
        0x0080, 0x0040, 0x0020, 0x0010, 0x0008, 0x0004, 0x0002, 0x0001)
WIRE = b"".join(b"." * row + b"x" + b"." * (15 - row) + b"@" for row in range(16))


def check(frame, resolution, status, active, regions):
    m, state = frame.metadata, frame.application
    width, height, ch = (320 if resolution == 0 else 640), (400 if resolution == 2 else 200), (16 if resolution == 2 else 8)
    mask = (15, 3, 1)[resolution]
    drawable = height - (ch if status else 0)
    assert (m["plane_width"], m["plane_height"]) == (width, height)
    assert (state["cterm.x"], state["cterm.y"]) == (5, 3)
    assert not m["cursor_visible"]
    assert tuple(frame.palette[768:784]) == tuple(initial(resolution))
    assert tuple(state[f"cterm.igs_user_fill_{i}"] for i in range(16)) == tuple(active)
    assert state["cterm.igs_writing_mode"] == 1
    expected = array("I", [768]) * (width * drawable)
    samples = set()
    assert cell_at(frame, 3, 2).glyph == ord("A")
    for y, bits in enumerate(DATA["fonts"][str(ch)]["65"]):
        for x in range(8):
            samples.add((24 + x, 2 * ch + y))
            if bits & (128 >> x):
                expected[(2 * ch + y) * width + 24 + x] = 768 + (3 & mask)
    for left, pattern in regions:
        for y in range(40, 72):
            for x in range(left, left + 32):
                if pattern[y % 16] & (0x8000 >> (x % 16)):
                    expected[y * width + x] = 768 + mask
                samples.add((x, y))
    check_pixel_planes(frame, expected, context=(resolution, status, regions))
    for x, y in samples:
        ox = m["destination_x"] + (2*x + 1) * m["destination_width"] // (2*width)
        oy = m["destination_y"] + (2*y + 1) * m["destination_height"] // (2*height)
        assert frame.rgb[oy * m["output_width"] + ox] & 0xffffff == frame.palette[expected[y * width + x]]


def run(executable, reports, resolution, encoding, status):
    screen = ("AtariST40x25", "AtariST80x25", "AtariST80x25Mono")[resolution]
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0)); listener.listen(2); listener.settimeout(15)
        entries = ("[IGS pattern load]\nConnectionType=Raw\nAddress=127.0.0.1\n"
                   f"Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\nScreenMode={screen}\n"
                   f"Font=Atari ST\nEncoding={encoding}\nNoStatus={str(not status).lower()}\n")
        settings = "AudioModes=0\nMidiModes=0\nBlockyScaling=true\n"
        with fixture(executable, entries=entries, settings=settings, reports=reports, quiet=False) as (client, root):
            client.wait_text("IGS pattern load"); client.key("enter")

            def send(peer, wire):
                with (root / "wire.bin").open("ab") as log: log.write(wire)
                peer.sendall(wire)

            def scene(peer, wire, marker, active, regions):
                send(peer, wire + f"G#X 10,{marker},777:".encode())
                frame = client.wait_for(lambda f: f.application.get("cterm.igs_draw_x") == marker)
                check(frame, resolution, status, active, regions)

            def disconnect(peer):
                peer.shutdown(socket.SHUT_RDWR)
                client.wait_text("Disconnected: Enter exits, F1 details"); client.key("enter")
                client.wait_text("Directory: Name")

            setup = b"\x1bfG#c>1,3:c 0,0:\x1bEG#p 3,2:A"
            with listener.accept()[0] as peer:
                client.wait_for(lambda f: f.application.get("connected") == 1); client.resize(1280, 960)
                scene(peer, setup + b"G#g 1:G#X 7,7," + WIRE + b"G#g 0:", 1000, (0xffff,)*16, [])
                scene(peer, b"G#A>4,7,0:Z 40,40,71,71:", 1001, ROWS, [(40, ROWS)])
                scene(peer, b"G#X 7,7," + b"................@"*16 + b"G#Z 80,40,111,71:",
                      1002, ROWS, [(40, ROWS), (80, ROWS)])
                scene(peer, b"G#A>4,7,0:Z 120,40,151,71:", 1003, (0,)*16, [(40, ROWS), (80, ROWS)])
                full = (0xffff,)*16
                scene(peer, b"G#X 7,0," + b"xxxxxxxxxxxxxxxx@"*16 + b"G#A>4,0,0:Z 120,40,151,71:",
                      1004, full, [(40, ROWS), (80, ROWS), (120, full)])
                peer.settimeout(0.03)
                try: reply = peer.recv(1)
                except socket.timeout: pass
                else: raise AssertionError(f"unexpected X7 reply: {reply!r}")
                send(peer, b"G#X 7,0,x@unfinished")
                disconnect(peer)
            client.key("enter")
            with listener.accept()[0] as peer:
                client.wait_for(lambda f: f.application.get("connected") == 1); client.resize(1280, 960)
                scene(peer, setup + b"G#A 4,7,0:", 2000, (0,)*16, [])
                disconnect(peer)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(); parser.add_argument("executable")
    parser.add_argument("--reports", default="reports/test-control"); args = parser.parse_args()
    for resolution in range(3):
        for encoding in ("ATARI-ST", "CP437", "UTF-8"):
            for status in (False, True): run(args.executable, args.reports, resolution, encoding, status)
