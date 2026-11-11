#!/usr/bin/env python3
"""X0: literal sampled points and marker masks in coherent application scenes."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from array import array
import socket

from st_igs_cursor_test import DATA, fixture, initial
from st_igs_marker_test import ROWS, XOR_ROWS
from syncterm_fixture import cell_at, check_pixel_planes

PENS = (0, 15, 1, 2, 4, 6, 3, 5, 7, 8, 9, 10, 12, 14, 11, 13)
BROAD = ((230, 202), (48, 58), (69, 129), (216, 45))
# Subsequent literals continue the same source stream; scenes do not reseed.
CYCLE_POINTS = ((58, 61), (47, 19), (45, 60), (62, 27))
STICKY_POINTS = ((0, 0), (31, 9), (22, 58), (16, 42))


def check(frame, resolution, status, number, shape, mode, base, points, pens, active, cache, cycle, seed):
    m, state = frame.metadata, frame.application
    width, height, ch = (320 if resolution == 0 else 640), (400 if resolution == 2 else 200), (16 if resolution == 2 else 8)
    mask = (15, 3, 1)[resolution]
    drawable = height - (ch if status else 0)
    assert (m["plane_width"], m["plane_height"]) == (width, height)
    assert (state["cterm.x"], state["cterm.y"]) == (10 + number, 1)
    assert not m["cursor_visible"]
    assert tuple(frame.palette[768:784]) == tuple(initial(resolution))
    assert (state["cterm.igs_draw_x"], state["cterm.igs_draw_y"]) == (19, 17)
    assert state["cterm.igs_marker_type"] == shape + 1 and state["cterm.igs_marker_height"] == 11
    assert state["cterm.igs_writing_mode"] == mode
    assert (state["cterm.igs_marker_pen"], state["cterm.igs_marker_pen_cache"]) == (active, cache)
    assert (state["cterm.igs_marker_cycle"], state["cterm.igs_marker_cycle_restart"]) == cycle
    assert state["cterm.igs_random_seed"] == seed
    expected = array("I", [768]) * (width * drawable)
    samples = set()
    for cx, cy in ((3, 2), (8 + number, 0)):
        assert cell_at(frame, cx, cy).glyph == ord("A")
        for y, bits in enumerate(DATA["fonts"][str(ch)]["65"]):
            for x in range(8):
                px, py = 8*cx + x, ch*cy + y
                samples.add((px, py))
                if bits & (128 >> x): expected[py*width + px] = 768 + (3 & mask)
    if mode != 4:
        for (dx, dy), pen in zip(points, pens):
            for y, row in enumerate((XOR_ROWS if mode == 3 else ROWS)[shape]):
                for x in range(9):
                    px, py = base[0] + dx + x - 4, base[1] + dy + y - 3
                    if not (0 <= px < width and 0 <= py < drawable): continue
                    samples.add((px, py))
                    if row & (1 << x):
                        offset = py*width + px
                        expected[offset] = 768 + (((expected[offset]-768)^mask) if mode == 3 else PENS[pen] & mask)
    check_pixel_planes(frame, expected, context=(resolution, status, number))
    for x, y in samples:
        ox = m["destination_x"] + (2*x + 1) * m["destination_width"] // (2*width)
        oy = m["destination_y"] + (2*y + 1) * m["destination_height"] // (2*height)
        assert frame.rgb[oy*m["output_width"] + ox] & 0xffffff == frame.palette[expected[y*width+x]]


def run(executable, reports, resolution, encoding, status):
    screen = ("AtariST40x25", "AtariST80x25", "AtariST80x25Mono")[resolution]
    ch = 16 if resolution == 2 else 8
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0)); listener.listen(2); listener.settimeout(15)
        entries = ("[IGS scatter]\nConnectionType=Raw\nAddress=127.0.0.1\n"
                   f"Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\nScreenMode={screen}\n"
                   f"Font=Atari ST\nEncoding={encoding}\nNoStatus={str(not status).lower()}\n")
        settings = "AudioModes=0\nMidiModes=0\nBlockyScaling=true\n"
        with fixture(executable, entries=entries, settings=settings, reports=reports, quiet=False) as (client, root):
            client.wait_text("IGS scatter"); client.key("enter")

            def send(peer, wire):
                with (root / "wire.bin").open("ab") as log: log.write(wire)
                peer.sendall(wire)

            def scene(peer, number, shape, mode, base, extents, points, seed, rotating=False, cache=3, normalized=False, fresh=False):
                wire = b"\x1bfG#c>1,3:c 0,0:\x1bEG#p 3,2:AG#g>0:X 10,19,17:"
                if not fresh:
                    wire += f"G#T>1,{shape+1},1:M>{mode}:C 0,{cache}:G#X 0,0,0,0,0,0:".encode()
                    if rotating: wire += b"G#X 0,1,0,0,0,0:"
                    wire += (b"G#g 1:" if normalized else b"")
                    arguments = (5000,5000,1000,1000,1) if normalized else (*base,*extents,len(points))
                    wire += ("G#X 0," + ",".join(str(v) for v in arguments) + ":").encode()
                wire += bytes((27,89,32,40+number,65))
                send(peer, wire)
                frame = client.wait_for(lambda f: f.application.get("cterm.x") == 10 + number
                                        and f.application.get("cterm.y") == 1 and cell_at(f, 8 + number, 0).glyph == ord("A"))
                effective = cache if cache <= (15,3,1)[resolution] else 1
                colors = ((2,3,4,5) if resolution == 0 else (2,3,1,2)) if rotating and resolution != 2 else (effective,)*len(points)
                current = (5 if resolution == 0 else 2) if rotating and resolution != 2 else 0
                cycle = (current, 1 if rotating and resolution != 2 else 0)
                active = effective if cache else (colors[-1] if colors else 0)
                if fresh: active, cache, cycle = 1, 1, (0,0)
                check(frame, resolution, status, number, shape, mode, base, points, colors, active, cache, cycle, seed)

            def disconnect(peer):
                peer.shutdown(socket.SHUT_RDWR)
                client.wait_text("Disconnected: Enter exits, F1 details"); client.key("enter")
                client.wait_text("Directory: Name")

            with listener.accept()[0] as peer:
                client.wait_for(lambda f: f.application.get("connected") == 1); client.resize(1280, 960)
                scene(peer, 0, 0, 1, (40,40), (255,255), BROAD, 0x37d52df9)
                scene(peer, 1, 1, 3, (24,2*ch+3), (1,255), ((0,0),)*3, 0xbcbc8b26)
                scene(peer, 2, 2, 2, (40,40), (63,63), CYCLE_POINTS, 0x63823eee, rotating=True)
                scene(peer, 3, 3, 1, (40,40), (63,63), STICKY_POINTS, 0x37531020, rotating=True, cache=0)
                scene(peer, 4, 4, 4, (40,40), (256,9999), ((149,243),(216,130),(105,100),(186,140)),
                      0x23e18cf8, rotating=True)
                scene(peer, 5, 5, 1, ((160 if resolution == 0 else 320), (200 if resolution == 2 else 100)),
                      (0,0), (((0,0),), ((59,6),), ((60,27),))[resolution],
                      (0x0c9f13ad,0xc6620652,0x60493c84)[resolution], normalized=True)
                peer.settimeout(0.03)
                try: reply = peer.recv(1)
                except socket.timeout: pass
                else: raise AssertionError(f"unexpected X0 reply: {reply!r}")
                send(peer, b"G#X 0,40,40,63,"); disconnect(peer)
            client.key("enter")
            with listener.accept()[0] as peer:
                client.wait_for(lambda f: f.application.get("connected") == 1); client.resize(1280, 960)
                scene(peer, 6, 0, 1, (0,0), (0,0), (), 1, fresh=True); disconnect(peer)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(); parser.add_argument("executable")
    parser.add_argument("--reports", default="reports/test-control"); args = parser.parse_args()
    for resolution in range(3):
        for encoding in ("ATARI-ST", "CP437", "UTF-8"):
            for status in (False, True): run(args.executable, args.reports, resolution, encoding, status)
