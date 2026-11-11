#!/usr/bin/env python3
"""IGS d and raw VT52 M move the indexed text and graphics together."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from array import array
import socket

from st_igs_console_color_test import DATA, fixture, initial
from syncterm_fixture import cell_at, check_pixel_planes


# Literal M glyph from the same original TOS font export as the A fixtures.
M_ROWS = {
    8: (198, 238, 254, 214, 198, 198, 198, 0),
    16: (0, 0, 198, 198, 238, 238, 254, 214, 214, 198, 198, 198, 198, 198, 0, 0),
}


def landmarks(bottom, ch):
    cells = ((1, 1), (2, 2), (3, 3), (6, 4), (5, bottom))
    stripes = tuple(y * ch + i for i, (_, y) in enumerate(cells))
    return cells, stripes


def check(frame, resolution, status, marker, top, amount, visible):
    width = 320 if resolution == 0 else 640
    height, ch = (400, 16) if resolution == 2 else (200, 8)
    rows = 24 if status else 25
    columns = width // 8
    mask = (15, 3, 1)[resolution]
    fg, bg = 768 + (4 & mask), 768 + (3 & mask)
    cursor = 0 if amount else 5
    metadata, state = frame.metadata, frame.application
    assert (metadata["plane_width"], metadata["plane_height"]) == (width, height)
    assert state["cterm.igs_resolution"] == resolution and state["cterm.graphics_mode"] == 5
    assert (state["cterm.x"], state["cterm.y"]) == (cursor + 1, top + 1)
    assert metadata["cursor_visible"] == visible
    assert state["cterm.igs_console_reverse"] == 1
    assert state["cterm.igs_console_foreground"] == (4 - 32) & 65535
    assert state["cterm.igs_console_background"] == (3 - 32) & 65535
    assert not state["cterm.igs_console_escape"] and not state["cterm.igs_console_parameters"]
    assert (state["cterm.igs_draw_x"], state["cterm.igs_draw_y"]) == (marker, 777)
    assert tuple(frame.palette[768:784]) == tuple(initial(resolution))

    letters, stripes = landmarks(rows - 1, ch)
    original = array("I", [768]) * (width * rows * ch)
    for x, y in letters:
        for gy, bits in enumerate(DATA["fonts"][str(ch)]["65"]):
            for gx in range(8):
                original[(y * ch + gy) * width + 8 * x + gx] = fg if bits & (128 >> gx) else bg
    for y in stripes:
        original[y * width:(y + 1) * width] = array("I", [768 + mask]) * width
    if not amount:
        for gy, bits in enumerate(M_ROWS[ch]):
            for gx in range(8):
                original[(top * ch + gy) * width + 32 + gx] = bg if bits & (128 >> gx) else fg

    row_size = width * ch
    blank_row = array("I", [bg]) * row_size
    pixels = array("I")
    for y in range(rows):
        source = y + amount if y >= top else y
        pixels.extend(original[source * row_size:(source + 1) * row_size] if source < rows else blank_row)
        for x in range(columns):
            cell = cell_at(frame, x, y)
            glyph, background = 32, bg if source >= rows else 768
            if (x, source) in letters:
                glyph, background = 65, bg
            elif not amount and (x, y) == (4, top):
                glyph, background = 77, fg
            assert (cell.glyph, cell.background) == (glyph, background), (resolution, status, top, amount, x, y)
            if source >= rows:
                assert not cell.flags & 8  # ST_CONSOLE_CELL_PIXEL_GRAPHICS
    if status:
        assert all(not cell_at(frame, x, rows).flags & 8 for x in range(columns))
    check_pixel_planes(frame, pixels, context=(resolution, status, top, amount))

    for row in {0, 1, 2, 3, 4, top, rows - 1}:
        for gy in range(ch):
            y = row * ch + gy
            for x in (*range(56), width - 1):
                index = pixels[y * width + x] - 768
                if visible and row == top and x // 8 == cursor:
                    index ^= mask
                ox = metadata["destination_x"] + (2 * x + 1) * metadata["destination_width"] // (2 * width)
                oy = metadata["destination_y"] + (2 * y + 1) * metadata["destination_height"] // (2 * height)
                rgb = frame.rgb[oy * metadata["output_width"] + ox] & 0xffffff
                assert rgb == frame.palette[768 + index], (resolution, status, top, amount, x, y)


def run(executable, reports, resolution, encoding, status):
    screen = ("AtariST40x25", "AtariST80x25", "AtariST80x25Mono")[resolution]
    width = 320 if resolution == 0 else 640
    ch = 16 if resolution == 2 else 8
    bottom = 23 if status else 24
    letters, stripes = landmarks(bottom, ch)
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(2)
        listener.settimeout(15)
        entries = (
            "[IGS delete lines]\nConnectionType=Raw\nAddress=127.0.0.1\n"
            f"Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\nScreenMode={screen}\n"
            f"Font=Atari ST\nEncoding={encoding}\nNoStatus={str(not status).lower()}\n"
        )
        settings = "AudioModes=0\nMidiModes=0\nBlockyScaling=true\n"
        with fixture(executable, entries=entries, settings=settings, reports=reports, quiet=False) as (client, root):
            client.wait_text("IGS delete lines")
            client.key("enter")

            def send(peer, wire):
                with (root / "wire.bin").open("ab") as log:
                    log.write(wire)
                peer.sendall(wire)

            def inspect(peer, number, top, wire, amount, visible=False):
                marker = 1000 + number
                setup = (
                    "\x1bq\x1bw\x1bfG#c>1,15:c 0,0:\x1bEG#c>1,4:c 0,3:"
                    + "".join(f"G#p {x},{y}:A" for x, y in letters)
                    + "".join(f"G#L 0,{y},{width - 1},{y}:" for y in stripes)
                    + f"G#p 4,{top}:\x1bp"
                )
                send(peer, setup.encode() + wire + f"G#X 10,{marker},777:".encode())
                frame = client.wait_for(lambda f: f.application.get("cterm.igs_draw_x") == marker)
                check(frame, resolution, status, marker, top, amount, visible)

            def disconnect(peer):
                peer.shutdown(socket.SHUT_RDWR)
                client.wait_text("Disconnected: Enter exits, F1 details")
                client.key("enter")
                client.wait_text("Directory: Name")

            with listener.accept()[0] as peer:
                client.wait_for(lambda f: f.application.get("connected") == 1)
                client.resize(1280, 960)
                client.presentation_time(4000)
                inspect(peer, 0, 0, b"G#d 0:", 1)
                inspect(peer, 1, 2, b"G#d 2:", 2)
                inspect(peer, 2, bottom, b"\x1beG#d 1:", 1, visible=True)
                inspect(peer, 3, 2, b"\x1bG#d 1:", 0)
                peer.settimeout(0.03)
                try:
                    reply = peer.recv(1)
                except socket.timeout:
                    pass
                else:
                    raise AssertionError(f"unexpected d reply: {reply!r}")
                send(peer, b"G#d 2")
                disconnect(peer)

            client.key("enter")
            with listener.accept()[0] as peer:
                client.wait_for(lambda f: f.application.get("connected") == 1)
                client.resize(1280, 960)
                inspect(peer, 4, 2, b"\x1bM", 1)
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
