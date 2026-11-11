#!/usr/bin/env python3
"""G: independent Boolean truth tables, retained captures and live second copies."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from array import array
import socket

from st_igs_cursor_test import DATA, fixture, initial
from syncterm_fixture import cell_at, check_pixel_planes


def truth(logic, source, destination, mask):
    return sum((1 << bit) for bit in range(mask.bit_length())
               if logic & (1 << (2*int(not (source & (1 << bit))) + int(not (destination & (1 << bit))))))


def check(frame, resolution, status, number):
    m, state = frame.metadata, frame.application
    width, height, ch = (320 if resolution == 0 else 640), (400 if resolution == 2 else 200), (16 if resolution == 2 else 8)
    drawable, mask = height-(ch if status else 0), (15, 3, 1)[resolution]
    assert (m["plane_width"], m["plane_height"]) == (width, height)
    assert (state["cterm.x"], state["cterm.y"]) == (10+number, 1)
    assert not m["cursor_visible"]
    assert tuple(frame.palette[768:784]) == tuple(initial(resolution))
    assert (state["cterm.igs_draw_x"], state["cterm.igs_draw_y"]) == (19, 17)
    assert not state["cterm.igs_input_paused"] and not state["cterm.igs_copy_pending"]
    assert state["cterm.igs_random_seed"] == 1
    assert state["cterm.igs_text_counter"] == state["cterm.igs_text_deadline"] == 0
    assert state["cterm.igs_double_step"] == (4 if number == 4 else 0)
    saved = (24, 2*ch, 39, 3*ch-1) if 1 <= number <= 5 else (0, 0, 0, 0)
    assert tuple(state[f"cterm.igs_memory0_{field}"] for field in ("x1", "y1", "x2", "y2")) == saved
    screen = number in (0, 4, 5)
    assert bool(state["cterm.igs_memory0_screen"]) == screen
    assert state["cterm.igs_memory0_word_width"] == (0 if screen else 1)
    assert state["cterm.igs_memory0_planes"] == (0 if screen else (4, 2, 1)[resolution])
    expected = array("I", [768])*(width*drawable)
    rows = DATA["fonts"][str(ch)]["65"]
    cells = {(3, 2): "A", (8+number, 0): "A"}
    for cx, cy in cells:
        for y, bits in enumerate(rows):
            for x in range(8):
                expected[(ch*cy+y)*width+8*cx+x] = 768 + (3 & mask if bits & (128 >> x) else 0)

    def glyph(x, y, col):
        return (3 & mask) if col < 8 and rows[y] & (128 >> col) else 0

    def block(dx, dy, columns, lines, operation, background=0):
        for y in range(lines):
            for x in range(columns):
                if dx+x < width and dy+y < drawable:
                    expected[(dy+y)*width+dx+x] = 768+operation(x, y, background)

    if number == 0:
        for logic in range(16):
            dx, dy = 80+(logic % 4)*48, 40+(logic // 4)*2*ch
            block(dx, dy, 16, ch, lambda x, y, d: truth(logic, glyph(x, y, x), d, mask), 1)
    elif number == 1:
        block(24, 2*ch, 16, ch, lambda x, y, d: 0)
        block(width//2, height//2, 16, ch, lambda x, y, d: glyph(x, y, x))
    elif number == 2:
        block(100, 80, 8, ch, lambda x, y, d: glyph(x, y, x) | d, 1)
    elif number == 3:
        block(120, 80, 16, ch, lambda x, y, d: glyph(x, y, x % 8))
    elif number == 5:
        block(width-4, drawable-2, 4, 2, lambda x, y, d: glyph(x, y, x))
    for cy in range(24 if status else 25):
        for cx in range(width//8):
            assert cell_at(frame, cx, cy).glyph == ord(cells.get((cx, cy), " ")), (number, cx, cy)
    check_pixel_planes(frame, expected, context=(resolution, status, number))
    for y in range(drawable):
        for x in range(width):
            if expected[y*width+x] == 768: continue
            ox = m["destination_x"]+(2*x+1)*m["destination_width"]//(2*width)
            oy = m["destination_y"]+(2*y+1)*m["destination_height"]//(2*height)
            assert frame.rgb[oy*m["output_width"]+ox] & 0xffffff == frame.palette[expected[y*width+x]]


def run(executable, reports, resolution, encoding, status):
    screen = ("AtariST40x25", "AtariST80x25", "AtariST80x25Mono")[resolution]
    width, height, ch = (320 if resolution == 0 else 640), (400 if resolution == 2 else 200), (16 if resolution == 2 else 8)
    drawable = height-(ch if status else 0)
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0)); listener.listen(2); listener.settimeout(15)
        entries = ("[IGS grab]\nConnectionType=Raw\nAddress=127.0.0.1\n"
                   f"Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\nScreenMode={screen}\n"
                   f"Font=Atari ST\nEncoding={encoding}\nNoStatus={str(not status).lower()}\n")
        with fixture(executable, entries=entries, settings="AudioModes=0\nMidiModes=0\nBlockyScaling=true\n",
                     reports=reports, quiet=False) as (client, root):
            client.wait_text("IGS grab"); client.key("enter")

            def send(peer, wire):
                with (root / "wire.bin").open("ab") as stream: stream.write(wire)
                peer.sendall(wire)

            def scene(peer, number):
                wire = b"\x1bfG#c>1,3:c 0,0:\x1bEG#p 3,2:AG#g>0:X>10,19,17:A>1,1,0:C>2,2:M 1:"
                if number == 0:
                    for logic in range(16):
                        x, y = 80+(logic % 4)*48, 40+(logic // 4)*2*ch
                        wire += f"G#Z>{x},{y},{x+15},{y+ch-1}:G 0,{logic},24,{2*ch},39,{3*ch-1},{x},{y}:".encode()
                elif number == 1:
                    wire += (f"G#G>1,3,24,{2*ch},39,{3*ch-1}:C>2,0:Z>24,{2*ch},39,{3*ch-1}:"
                             "g>1:G 2,3,5000,5000:").encode()
                elif number == 2:
                    wire += f"G#Z>100,80,107,{80+ch-1}:G 3,7,24,{2*ch},31,{3*ch-1},100,80:".encode()
                elif number == 3:
                    wire += f"G#G>4,3,24,{2*ch},31,{3*ch-1},32,{2*ch}:G 2,3,120,80:".encode()
                elif number == 4:
                    wire += f"G#q>9995:G 0,6,24,{2*ch},31,{3*ch-1},80,80:".encode()
                elif number == 5:
                    wire += f"G#q>9999:G 0,3,24,{2*ch},39,{3*ch-1},{width-4},{drawable-2}:".encode()
                else:
                    wire += b"G#G 2,3,80,80:"
                wire += bytes((27, 89, 32, 40+number, 65))
                send(peer, wire)
                frame = client.wait_for(lambda f: f.application.get("cterm.x") == 10+number
                                        and f.application.get("cterm.y") == 1
                                        and cell_at(f, 8+number, 0).glyph == ord("A"))
                check(frame, resolution, status, number)

            def disconnect(peer):
                peer.shutdown(socket.SHUT_RDWR)
                client.wait_text("Disconnected: Enter exits, F1 details"); client.key("enter")
                client.wait_text("Directory: Name")

            with listener.accept()[0] as peer:
                client.wait_for(lambda f: f.application.get("connected") == 1); client.resize(1280, 960)
                for number in range(6): scene(peer, number)
                peer.settimeout(0.03)
                try: reply = peer.recv(1)
                except socket.timeout: pass
                else: raise AssertionError(f"unexpected G reply: {reply!r}")
                send(peer, b"G#G 1,3,0,0,15,"); disconnect(peer)
            client.key("enter")
            with listener.accept()[0] as peer:
                client.wait_for(lambda f: f.application.get("connected") == 1); client.resize(1280, 960)
                scene(peer, 6); disconnect(peer)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(); parser.add_argument("executable")
    parser.add_argument("--reports", default="reports/test-control"); args = parser.parse_args()
    for resolution in range(3):
        for encoding in ("ATARI-ST", "CP437", "UTF-8"):
            for status in (False, True): run(args.executable, args.reports, resolution, encoding, status)
