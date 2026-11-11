#!/usr/bin/env python3
"""X13: signed decimal console/graphical output in coherent application frames."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from array import array
import json
from pathlib import Path
import socket

from st_igs_cursor_test import fixture, initial
from syncterm_fixture import cell_at, check_pixel_planes

DATA = json.loads((Path(__file__).parent / "igs" / "math-vectors.json").read_text())


def check(frame, resolution, status, case):
    m, state = frame.metadata, frame.application
    width, height, ch = (320 if resolution == 0 else 640), (400 if resolution == 2 else 200), (16 if resolution == 2 else 8)
    drawable = height - (ch if status else 0)
    number, decimal = case["number"], case["decimal"]
    assert (m["plane_width"], m["plane_height"]) == (width, height)
    assert not m["cursor_visible"]
    assert tuple(frame.palette[768:784]) == tuple(initial(resolution))
    assert (state["cterm.x"], state["cterm.y"]) == (10 + number, 1)
    assert (state["cterm.igs_draw_x"], state["cterm.igs_draw_y"]) == (19, 17)
    assert state["cterm.igs_shared_text_size"] == len(decimal)
    assert state["cterm.igs_text_counter"] == state["cterm.igs_text_deadline"] == 0
    assert state["cterm.igs_random_seed"] == 1
    assert state["cterm.igs_writing_mode"] == case["mode"]
    assert state["cterm.igs_text_effects"] == case["style"]
    expected = array("I", [768]) * (width * drawable)
    for y, x, count, value in case["runs"]:
        if y < drawable: expected[y*width+x:y*width+x+count] = array("I", [768+value])*count
    cells = {(8+number, 0): "A", (12, 18): "Z"}
    cells.update({(3+i, 2): c for i, c in enumerate(decimal)})
    mask = (15, 3, 1)[resolution]
    for (cx, cy), character in cells.items():
        for y, bits in enumerate(DATA["fonts"][str(ch)][character]):
            for x in range(8):
                expected[(ch*cy+y)*width+8*cx+x] = 768 + (3 & mask if bits & (128 >> x) else 0)
    for cy in range(24 if status else 25):
        for cx in range(width//8):
            assert cell_at(frame, cx, cy).glyph == ord(cells.get((cx, cy), " ")), (number, cx, cy)
    check_pixel_planes(frame, expected, context=(resolution, status, number))
    # Check composed RGB at every affected cell and graphical run's first pixel.
    samples = {(8*cx+x, ch*cy+y) for cx, cy in cells for x in range(8) for y in range(ch)}
    samples.update((x, y) for y, x, _, _ in case["runs"] if y < drawable)
    for x, y in samples:
        ox = m["destination_x"] + (2*x+1)*m["destination_width"]//(2*width)
        oy = m["destination_y"] + (2*y+1)*m["destination_height"]//(2*height)
        assert frame.rgb[oy*m["output_width"]+ox] & 0xffffff == frame.palette[expected[y*width+x]]


def run(executable, reports, resolution, encoding, status):
    screen = ("AtariST40x25", "AtariST80x25", "AtariST80x25Mono")[resolution]
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0)); listener.listen(2); listener.settimeout(15)
        entries = ("[IGS math]\nConnectionType=Raw\nAddress=127.0.0.1\n"
                   f"Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\nScreenMode={screen}\n"
                   f"Font=Atari ST\nEncoding={encoding}\nNoStatus={str(not status).lower()}\n")
        with fixture(executable, entries=entries, settings="AudioModes=0\nMidiModes=0\nBlockyScaling=true\n",
                     reports=reports, quiet=False) as (client, root):
            client.wait_text("IGS math"); client.key("enter")

            def send(peer, wire):
                with (root / "wire.bin").open("ab") as stream: stream.write(wire)
                peer.sendall(wire)

            def scene(peer, case):
                number = case["number"]
                wire = (b"\x1bfG#c>1,3:c 0,0:\x1bE\x1bY2,ZG#g>0:X>10,19,17:"
                        + case["wire"].encode() + f"p>3,2:X>13,7,0,{case['index']}:".encode())
                wire += (f"g>1:C>3,7:E>{case['style']},{case['points']},{case['rotation']}:"
                         f"M>{case['mode']}:X 13,7,1,{case['index']},5000,5000:").encode()
                wire += bytes((27, 89, 32, 40+number, 65))
                send(peer, wire)
                frame = client.wait_for(lambda f: f.application.get("cterm.x") == 10+number
                                        and f.application.get("cterm.y") == 1
                                        and cell_at(f, 8+number, 0).glyph == ord("A"))
                check(frame, resolution, status, case)

            def disconnect(peer):
                peer.shutdown(socket.SHUT_RDWR)
                client.wait_text("Disconnected: Enter exits, F1 details"); client.key("enter")
                client.wait_text("Directory: Name")

            cases = [c for c in DATA["cases"] if c["resolution"] == resolution]
            with listener.accept()[0] as peer:
                client.wait_for(lambda f: f.application.get("connected") == 1); client.resize(1280, 960)
                for case in cases[:-1]: scene(peer, case)
                peer.settimeout(0.03)
                try: reply = peer.recv(1)
                except socket.timeout: pass
                else: raise AssertionError(f"unexpected X13 reply: {reply!r}")
                send(peer, b"G#X 13,7,1,0,40,"); disconnect(peer)
            client.key("enter")
            with listener.accept()[0] as peer:
                client.wait_for(lambda f: f.application.get("connected") == 1); client.resize(1280, 960)
                scene(peer, cases[-1]); disconnect(peer)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(); parser.add_argument("executable")
    parser.add_argument("--reports", default="reports/test-control"); args = parser.parse_args()
    for resolution in range(3):
        for encoding in ("ATARI-ST", "CP437", "UTF-8"):
            for status in (False, True): run(args.executable, args.reports, resolution, encoding, status)
