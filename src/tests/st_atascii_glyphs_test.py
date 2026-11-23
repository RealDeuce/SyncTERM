#!/usr/bin/env python3
"""Check Atari 8-bit wire characters, their polarity and the screen-editor controls."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path
import socket
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "testing"))
from syncterm_fixture import cell_at, check_pixel_planes, fixture

CELL_REVERSE = 1 << 18
CONTROLS = {*range(0x1b, 0x20), *range(0x7d, 0x80), *range(0x9b, 0xa0), *range(0xfd, 0x100)}
# A space, a right-quarter block and a bottom-quarter block in ATASCII order.
MASKS = {0x20: [0] * 8, 0x02: [0x03] * 8, 0x0e: [0] * 6 + [0xff] * 2}


def run(executable, reports, mode):
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen()
        listener.settimeout(15)
        entries = (f"[ATASCII glyphs]\nConnectionType=Raw\nAddress=127.0.0.1\n"
                   f"Port={listener.getsockname()[1]}\nScreenMode={mode}\n"
                   "Font=Atari\nEncoding=ATASCII\nNoStatus=true\n")
        with fixture(executable, entries=entries, reports=reports) as (client, _):
            client.wait_text("ATASCII glyphs")
            client.key("enter")
            with listener.accept()[0] as peer:
                client.wait_for(lambda f: f.application.get("connected") == 1)
                wire = bytearray([0x7d])
                for row in range(16):
                    for byte in range(16 * row, 16 * row + 16):
                        if byte == 0x9b:
                            wire.append(0x20)  # keep the grid aligned
                            continue
                        if byte in CONTROLS:
                            wire.append(0x1b)
                        wire.append(byte)
                    wire.append(0x9b)
                peer.sendall(wire + b"A\x1b\xc1\x1b\x9bDONE")
                frame = client.wait_text("DONE")
                assert (frame.application["cterm.x"], frame.application["cterm.y"]) == (5, 18)
                height = frame.metadata["cell_height"]
                stride = frame.metadata["plane_width"]
                reference = cell_at(frame, 1, 4)  # A
                fg, bg = reference.foreground, reference.background
                assert fg != bg
                if mode == "Atari":
                    assert (frame.palette[fg], frame.palette[bg]) == (0x60b7e7, 0x005181)
                else:
                    assert frame.palette[fg] != frame.palette[bg]
                assert frame.lines[16].strip() == "AA"
                assert cell_at(frame, 1, 16).flags & CELL_REVERSE
                for byte in range(256):
                    if byte == 0x9b:
                        continue
                    x, y = byte % 16, byte // 16
                    base = byte & 0x7f
                    cell = cell_at(frame, x, y)
                    assert (cell.glyph, cell.glyph_font) == (base, cell.font), (mode, byte, cell)
                    assert bool(cell.flags & CELL_REVERSE) == (byte >= 128), (mode, byte, cell)
                    assert (cell.foreground, cell.background) == (fg, bg)
                    for line in range(height):
                        offset = (y * height + line) * stride + x * 8
                        if base in MASKS:
                            mask = MASKS[base][line * 8 // height] ^ (0xff if byte >= 128 else 0)
                            pixels = [fg if mask & (0x80 >> bit) else bg for bit in range(8)]
                        elif byte >= 128:
                            normal = offset - 8 * height * stride
                            pixels = [bg if p == fg else fg for p in frame.primary[normal:normal + 8]]
                        else:
                            continue
                        check_pixel_planes(frame, pixels, start=offset, context=(mode, byte, line))
                peer.shutdown(socket.SHUT_RDWR)
            client.wait_text("Directory: Name")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("executable")
    parser.add_argument("--reports", required=True)
    args = parser.parse_args()
    for mode in ("Atari", "Atari-XEP80"):
        run(args.executable, args.reports, mode)
