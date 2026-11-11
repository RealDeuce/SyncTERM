#!/usr/bin/env python3
"""X11: opaque native payloads, indexed display, retained banks and recovery."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from array import array
import socket

from st_igs_cursor_test import DATA, fixture, initial
from syncterm_fixture import cell_at, check_pixel_planes


def payload(count):
    data = bytearray((i*37 ^ (i >> 3) ^ 0xa5) & 255 for i in range(count))
    data[:8] = b"\r\0\n\x1b\xff\x80\xc0\0"
    data[123:143] = b"G#X>14,5:\x1b[31m\0\r\n\xff@!"
    data[-2:] = b"G#"
    assert len(data) == count
    return data


def random_bytes(count):
    state, data = 1, bytearray()
    for _ in range(count):
        state = (state*0xbb40e62d+1) & 0xffffffff
        data.append((state >> 8) & 255)
    assert list(data[:8]) == [230, 202, 48, 58, 69, 129, 216, 45]
    assert state == 0xa7054a61
    return data


def check(frame, resolution, status, number, data, section, display=False, memory_tile=False, fresh=False):
    m, state = frame.metadata, frame.application
    width, height, ch = (320 if resolution == 0 else 640), (400 if resolution == 2 else 200), (16 if resolution == 2 else 8)
    drawable, planes = height-(ch if status else 0), (4, 2, 1)[resolution]
    band, mask = (50 if resolution == 2 else 25), (1 << planes)-1
    first, last = (0, height-1) if section == 0 else ((section-1)*band, section*band-1)
    assert (m["plane_width"], m["plane_height"]) == (width, height)
    assert (state["cterm.x"], state["cterm.y"]) == (10+number, 1)
    assert not m["cursor_visible"] and not state["cterm.igs_input_paused"]
    assert not state["cterm.igs_copy_pending"] and not state["cterm.igs_bitmap_loading"]
    assert state["cterm.igs_bitmap_errors"] == state["cterm.igs_bitmap_deadline"] == 0
    assert state["cterm.igs_bitmap_received"] == state["cterm.igs_bitmap_size"] == 0
    assert state["cterm.igs_text_counter"] == state["cterm.igs_text_deadline"] == 0
    assert state["cterm.igs_memory_route"] == (0 if fresh else 5)
    assert state["cterm.igs_double_step"] == (4 if number == 3 else 0)
    assert state["cterm.igs_scaling_mode"] == (1 if number == 4 else 0)
    assert state["cterm.igs_random_seed"] == (1 if fresh or number < 8 else 0xa7054a61)
    assert (state["cterm.igs_draw_x"], state["cterm.igs_draw_y"]) == (19, 17)
    assert tuple(frame.palette[768:784]) == tuple(initial(resolution))
    assert tuple(state[f"cterm.igs_memory0_{field}"] for field in ("x1", "y1", "x2", "y2")) == (0, first, width-1, last)
    assert state["cterm.igs_memory0_word_width"] == width//16
    assert state["cterm.igs_memory0_planes"] == planes and not state["cterm.igs_memory0_screen"]
    assert tuple(state[f"cterm.igs_memory1_{field}"] for field in ("x1", "y1", "x2", "y2")) == ((0, 0, 0, 0) if fresh else (24, 2*ch, 31, 3*ch-1))
    expected = array("I", [768])*(width*drawable)
    if display:
        for y in range(max(0, first), min(drawable, last+1)):
            for x in range(width):
                index = y*width+x
                value = 0
                for plane in range(planes):
                    word = 2*((index//16)*planes+plane)
                    bits = (data[word] << 8) | data[word+1]
                    value |= ((bits >> (15-index%16)) & 1) << plane
                expected[index] += value
    rows = DATA["fonts"][str(ch)]["65"]

    def glyph(cx, cy, overwritten=False):
        for y, bits in enumerate(rows):
            py = cy+y
            if overwritten and display and first <= py <= last: continue
            for x in range(8): expected[py*width+cx+x] = 768 + (3 & mask if bits & (128 >> x) else 0)

    glyph(24, 2*ch, overwritten=True)
    if memory_tile: glyph(80, 80)
    glyph(8*(8+number), 0)
    for cy in range(24 if status else 25):
        for cx in range(width//8):
            assert cell_at(frame, cx, cy).glyph == (65 if (cx, cy) in ((3, 2), (8+number, 0)) else 32), (number, cx, cy)
    check_pixel_planes(frame, expected, context=(resolution, status, number))
    samples = {(0, 0), (width-1, drawable-1), (24, 2*ch)}
    colors = set()
    for i, value in enumerate(expected):
        x, y = i % width, i // width
        if value not in colors or (x % 32 == 0 and y % 25 == 0):
            samples.add((x, y)); colors.add(value)
    for x, y in samples:
        ox = m["destination_x"]+(2*x+1)*m["destination_width"]//(2*width)
        oy = m["destination_y"]+(2*y+1)*m["destination_height"]//(2*height)
        assert frame.rgb[oy*m["output_width"]+ox] & 0xffffff == frame.palette[expected[y*width+x]]


def only_telnet_controls(data):
    while data:
        assert data[0] == 255 and len(data) >= 3, f"unexpected X11 reply: {data!r}"
        if data[1] in (251, 252, 253, 254): data = data[3:]
        else:
            assert data[1] == 250
            end = data.find(b"\xff\xf0", 2)
            assert end >= 0
            data = data[end+2:]


def run(executable, reports, resolution, encoding, status, telnet=False):
    screen = ("AtariST40x25", "AtariST80x25", "AtariST80x25Mono")[resolution]
    ch, band = (16 if resolution == 2 else 8), (50 if resolution == 2 else 25)
    title = "IGS bitmap Telnet" if telnet else "IGS bitmap"
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0)); listener.listen(2); listener.settimeout(15)
        entries = (f"[{title}]\nConnectionType={'Telnet' if telnet else 'Raw'}\nAddress=127.0.0.1\n"
                   f"Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\nScreenMode={screen}\nBPSRate=0\n"
                   f"Font=Atari ST\nEncoding={encoding}\nNoStatus={str(not status).lower()}\n")
        with fixture(executable, entries=entries, settings="AudioModes=0\nMidiModes=0\nBlockyScaling=true\n",
                     reports=reports, quiet=False) as (client, root):
            client.wait_text(title); client.key("enter")
            model = bytearray(32000)

            def send(peer, wire):
                transport = wire.replace(b"\xff", b"\xff\xff") if telnet else wire
                with (root / "wire.bin").open("ab") as stream: stream.write(transport)
                peer.sendall(transport)

            def ready(peer):
                if telnet: peer.sendall(b"\xff\xfb\0\xff\xfd\0")
                client.wait_for(lambda f: f.application.get("connected") == 1
                                and f.application.get("cterm.igs_bitmap_loading") == 0)
                client.resize(1280, 960)

            def scene(peer, number, fresh=False):
                section, display, memory_tile = 4, False, False
                wire = b"\x1bfG#c>1,3:c 0,0:\x1bEG#p 3,2:AG#g>0:X>10,19,17:M 1:"
                if not fresh: wire += b"G#X 14,5:"
                if number == 0:
                    wire += b"G#X>11,0,0,0:X 11,0,4,128:"; model[:] = bytes(32000); model[12000:16000] = bytes([128])*4000
                elif number == 1:
                    data = payload(4000); wire += b"G#X 11,2,4:"+data; model[12000:16000] = data
                elif number == 2:
                    wire += f"G#X>14,0:G>2,3,0,{3*band}:X 14,5:".encode(); display = True
                elif number == 3:
                    data = payload(32000); wire += b"G#q>9995:X 11,1,0:"+data; model[:] = data; section, display = 0, True
                elif number == 4:
                    data = bytes([128])*4000; wire += b"G#q>9999:g>1:X 11,1,9999:"+data; model[28000:] = data; section, display = 8, True
                elif number == 5:
                    wire += b"G#X>13,2,0,1,0:X>11,9,0]:M 1:"; section = -1
                elif number == 6:
                    wire += b"G#X 11,1,0]:"; section = -1
                elif number == 7:
                    wire += b"G#X>14,1:G>2,3,80,80:X 14,5:"; section, memory_tile = -1, True
                elif number == 8:
                    wire += f"G#X>11,0,4,256:X>14,0:G>2,3,0,{3*band}:X>14,1:G>2,3,80,80:X 14,5:".encode()
                    model[12000:16000] = random_bytes(4000); display = memory_tile = True
                elif number == 9:
                    wire += b"G#X>11,0,0,0:X 11,1,1:"; model[:] = bytes(32000); send(peer, wire)
                    client.wait_for(lambda f: f.application.get("cterm.igs_bitmap_loading") == 1
                                    and f.application.get("cterm.igs_bitmap_received") == 0)
                    client.wait_for(lambda f: f.application.get("cterm.igs_bitmap_received") == 1
                                    and f.application.get("cterm.igs_bitmap_errors") == 1, timeout=8)
                    wire = bytes(3999); model[0] = 42; section, display = 1, True
                else:
                    model[:] = bytes(32000); wire += b"G#X>11,9,0:G 2,3,0,0:"; section, display = 0, True
                wire += bytes((27, 89, 32, 40+number, 65)); send(peer, wire)
                frame = client.wait_for(lambda f: f.application.get("cterm.x") == 10+number
                                        and f.application.get("cterm.y") == 1
                                        and cell_at(f, 8+number, 0).glyph == ord("A"))
                check(frame, resolution, status, number, model, section, display, memory_tile, fresh)

            def disconnect(peer):
                peer.shutdown(socket.SHUT_RDWR)
                client.wait_text("Disconnected: Enter exits, F1 details"); client.key("enter")
                client.wait_text("Directory: Name")

            with listener.accept()[0] as peer:
                ready(peer)
                send(peer, f"\x1bfG#c>1,3:c 0,0:\x1bEG#p 3,2:AG#X>14,1:G 1,3,24,{2*ch},31,{3*ch-1}:".encode())
                for number in range(9): scene(peer, number)
                if resolution == 0 and encoding == "ATARI-ST" and not status and not telnet: scene(peer, 9)
                peer.settimeout(0.03); reply = bytearray()
                while True:
                    try: part = peer.recv(4096)
                    except socket.timeout: break
                    assert part, "unexpected disconnect"; reply.extend(part)
                if telnet: only_telnet_controls(reply)
                else: assert not reply, f"unexpected X11 reply: {reply!r}"
                send(peer, b"G#X 11,1,2:G#\0\x1b\xff"); disconnect(peer)
            client.key("enter")
            with listener.accept()[0] as peer:
                ready(peer); scene(peer, 10, fresh=True); disconnect(peer)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(); parser.add_argument("executable")
    parser.add_argument("--reports", default="reports/test-control"); args = parser.parse_args()
    for resolution in range(3):
        for encoding in ("ATARI-ST", "CP437", "UTF-8"):
            for status in (False, True): run(args.executable, args.reports, resolution, encoding, status)
    run(args.executable, args.reports, 0, "ATARI-ST", False, telnet=True)
