#!/usr/bin/env python3
"""X14: independent retained-bank glyphs expose G1/G3 direction and G4 selection."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from array import array
import socket

from st_igs_cursor_test import DATA, fixture, initial
from syncterm_fixture import cell_at, check_pixel_planes


def check(frame, resolution, status, number, route):
    m, state = frame.metadata, frame.application
    width, height, ch = (320 if resolution == 0 else 640), (400 if resolution == 2 else 200), (16 if resolution == 2 else 8)
    drawable, mask = height-(ch if status else 0), (15, 3, 1)[resolution]
    assert (m["plane_width"], m["plane_height"]) == (width, height)
    assert (state["cterm.x"], state["cterm.y"]) == (10+number, 1)
    assert not m["cursor_visible"]
    assert tuple(frame.palette[768:784]) == tuple(initial(resolution))
    assert state["cterm.igs_memory_route"] == route
    assert (state["cterm.igs_draw_x"], state["cterm.igs_draw_y"]) == (19, 17)
    assert not state["cterm.igs_input_paused"] and not state["cterm.igs_copy_pending"]
    assert state["cterm.igs_random_seed"] == 1 and state["cterm.igs_double_step"] == 0
    assert state["cterm.igs_writing_mode"] == 1
    for bank in range(2):
        assert tuple(state[f"cterm.igs_memory{bank}_{field}"] for field in ("x1", "y1", "x2", "y2")) == (16, 2*ch, 31, 3*ch-1)
        screen = bank == route and route in (0, 1)
        assert bool(state[f"cterm.igs_memory{bank}_screen"]) == screen
        assert state[f"cterm.igs_memory{bank}_word_width"] == (0 if screen else 1)
        assert state[f"cterm.igs_memory{bank}_planes"] == (0 if screen else (4, 2, 1)[resolution])
    expected = array("I", [768])*(width*drawable)
    fonts = DATA["fonts"][str(ch)]

    def tile(character, dx, dy, invert=False, columns=16):
        for y, bits in enumerate(fonts[str(ord(character))]):
            for x in range(columns):
                value = 3 & mask if x < 8 and bits & (128 >> x) else 0
                if invert: value ^= mask
                expected[(dy+y)*width+dx+x] = 768+value

    # Expected bank contents are literal route-table consequences, independent
    # of the implementation's route dispatch and indexed addressing loops.
    first = {0: ("c", "b"), 1: ("A", "c"), 2: ("A", "A"), 3: ("b", "b")}.get(route, ("A", "b"))
    final = {2: ("b", "b"), 3: ("A", "A"), 4: ("c", "b"), 5: ("A", "c")}.get(route, ("A", "b"))
    visible = "A" if route == 0 else "b" if route == 1 else "c"
    tile(visible, 16, 2*ch)
    for bank, x in enumerate((80, 160)):
        tile(first[bank], x, 60)
        tile(final[bank], x, 100, invert=route == bank and route in (0, 1))
    if route in (0, 1): tile(visible, 240, 140)
    tile("A", (8+number)*8, 0, columns=8)
    for cy in range(24 if status else 25):
        for cx in range(width//8):
            character = "c" if (cx, cy) == (2, 2) else "A" if (cx, cy) == (8+number, 0) else " "
            assert cell_at(frame, cx, cy).glyph == ord(character), (number, cx, cy)
    check_pixel_planes(frame, expected, context=(resolution, status, number, route))
    samples = {(0, 0), (width-1, drawable-1)}
    samples.update((x, y) for y in range(drawable) for x in range(width) if expected[y*width+x] != 768)
    for x, y in samples:
        ox = m["destination_x"]+(2*x+1)*m["destination_width"]//(2*width)
        oy = m["destination_y"]+(2*y+1)*m["destination_height"]//(2*height)
        assert frame.rgb[oy*m["output_width"]+ox] & 0xffffff == frame.palette[expected[y*width+x]]


def run(executable, reports, resolution, encoding, status):
    screen = ("AtariST40x25", "AtariST80x25", "AtariST80x25Mono")[resolution]
    ch = 16 if resolution == 2 else 8
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0)); listener.listen(2); listener.settimeout(15)
        entries = ("[IGS routing]\nConnectionType=Raw\nAddress=127.0.0.1\n"
                   f"Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\nScreenMode={screen}\n"
                   f"Font=Atari ST\nEncoding={encoding}\nNoStatus={str(not status).lower()}\n")
        with fixture(executable, entries=entries, settings="AudioModes=0\nMidiModes=0\nBlockyScaling=true\n",
                     reports=reports, quiet=False) as (client, root):
            client.wait_text("IGS routing"); client.key("enter")

            def send(peer, wire):
                with (root / "wire.bin").open("ab") as stream: stream.write(wire)
                peer.sendall(wire)

            def banks(implicit=False):
                wire = b"G#p 2,2:A"+(b"" if implicit else b"G#X 14,0:")
                wire += f"G#G 1,3,16,{2*ch},31,{3*ch-1}:".encode()
                wire += f"G#p 2,2:bG#X>14,1:G 1,3,16,{2*ch},31,{3*ch-1}:G#p 2,2:c".encode()
                return wire

            def replay(y):
                return f"G#X>14,0:G>2,3,80,{y}:X>14,1:G 2,3,160,{y}:".encode()

            def scene(peer, number, value, fresh=False):
                route = 0 if value > 5 else value
                selector = "0]" if value < 0 else str(value)
                select = f"G#X 14,{selector}:".encode()
                wire = b"\x1bfG#c>1,3:c 0,0:\x1bEG#g>0:X>10,19,17:M 1:"
                if value < 0: wire += b"G#X 13,2,0,1,0:"
                wire += banks(implicit=fresh)+select+f"G#G 1,3,16,{2*ch},31,{3*ch-1}:".encode()+replay(60)
                wire += banks()+select+f"G#G>3,3,16,{2*ch},31,{3*ch-1},16,{2*ch}:G 4,10,16,{2*ch},31,{3*ch-1},16,{2*ch}:".encode()
                wire += replay(100)+select+f"G#G 0,3,16,{2*ch},31,{3*ch-1},240,140:".encode()+select
                wire += bytes((27, 89, 32, 40+number, 65))
                send(peer, wire)
                frame = client.wait_for(lambda f: f.application.get("cterm.x") == 10+number
                                        and f.application.get("cterm.y") == 1
                                        and cell_at(f, 8+number, 0).glyph == ord("A"))
                check(frame, resolution, status, number, route)

            def disconnect(peer):
                peer.shutdown(socket.SHUT_RDWR)
                client.wait_text("Disconnected: Enter exits, F1 details"); client.key("enter")
                client.wait_text("Directory: Name")

            with listener.accept()[0] as peer:
                frame = client.wait_for(lambda f: f.application.get("connected") == 1
                                        and f.application.get("cterm.igs_memory_route") == 0)
                assert frame.application["cterm.igs_memory_route"] == 0
                client.resize(1280, 960)
                for number, route in enumerate((0, 1, 2, 3, 4, 5, -1, 6)): scene(peer, number, route)
                peer.settimeout(0.03)
                try: reply = peer.recv(1)
                except socket.timeout: pass
                else: raise AssertionError(f"unexpected X14 reply: {reply!r}")
                send(peer, b"G#X>13,2,0,1,0:X 14,0]:")
                client.wait_for(lambda f: f.application.get("cterm.igs_memory_route") == -1)
                send(peer, b"G#X 14,"); disconnect(peer)
            client.key("enter")
            with listener.accept()[0] as peer:
                frame = client.wait_for(lambda f: f.application.get("connected") == 1
                                        and f.application.get("cterm.igs_memory_route") == 0)
                assert frame.application["cterm.igs_memory_route"] == 0
                client.resize(1280, 960); scene(peer, 8, 2, fresh=True); disconnect(peer)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(); parser.add_argument("executable")
    parser.add_argument("--reports", default="reports/test-control"); args = parser.parse_args()
    for resolution in range(3):
        for encoding in ("ATARI-ST", "CP437", "UTF-8"):
            for status in (False, True): run(args.executable, args.reports, resolution, encoding, status)
