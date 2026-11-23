#!/usr/bin/env python3
"""Check received Atari ST graphics against the original TOS glyphs."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path
import socket
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "testing"))
from syncterm_fixture import cell_at, check_pixel_planes, fixture

GRAPHICS = bytes([*range(1, 7), *range(14, 27), *range(28, 32)])
PIECES = (14, 15, 28, 29, 30, 31)
# Rows of the joined Fuji logo from the TOS 1.04 font export pinned in igs/console-color.json.
FUJI = {
    8: (0x05a0, 0x05a0, 0x05a0, 0x0db0, 0x0db0, 0x1998, 0x799e, 0x718e),
    16: (0x0000, 0x05a0, 0x05a0, 0x05a0, 0x05a0, 0x0db0, 0x0db0, 0x1db8,
         0x399c, 0x799e, 0x718e, 0x718e, 0x6186, 0x4182, 0x0000, 0x0000),
}


def run(executable, reports, mode, encoding, igs):
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen()
        listener.settimeout(15)
        entries = (f"[VT52 C0]\nConnectionType=Raw\nAddress=127.0.0.1\n"
                   f"Port={listener.getsockname()[1]}\nScreenMode={mode}\n"
                   f"Graphics={'IGS (2.21)' if igs else 'Off'}\n"
                   f"Font=Atari ST\nEncoding={encoding}\nNoStatus=true\n")
        with fixture(executable, entries=entries, reports=reports) as (client, _):
            client.wait_text("VT52 C0")
            client.key("enter")
            with listener.accept()[0] as peer:
                client.wait_for(lambda f: f.application.get("connected") == 1)
                # A raw C0 colour operand, all graphic bytes, a NUL pad and BS.
                wire = b"\x1bf\x1bE\x1bb\x03A" + GRAPHICS + b"\0Z\b!"
                if encoding == "UTF-8":
                    wire += "".join(chr(0xffcfa + i) for i in range(6)).encode()
                peer.sendall(wire + b"DONE")
                frame = client.wait_text("DONE")
                glyphs = b"A" + GRAPHICS + b"!" + (bytes(PIECES) if encoding == "UTF-8" else b"") + b"DONE"
                assert frame.application["cterm.graphics_mode"] == (5 if igs else 0)
                assert (frame.application["cterm.x"], frame.application["cterm.y"]) == (len(glyphs) + 1, 1)
                for x, glyph in enumerate(glyphs):
                    cell = cell_at(frame, x, 0)
                    assert (cell.glyph, cell.glyph_font) == (glyph, cell.font), (mode, encoding, igs, x, cell)
                    if glyph in PIECES:
                        assert frame.cell_text(cell) == chr(0xffcfa + PIECES.index(glyph))
                x = glyphs.index(14)
                reference = cell_at(frame, 0, 0)
                assert reference.foreground != reference.background
                for y, bits in enumerate(FUJI[frame.metadata["cell_height"]]):
                    pixels = [reference.foreground if bits & (0x8000 >> bit) else reference.background
                              for bit in range(16)]
                    check_pixel_planes(frame, pixels, start=y * frame.metadata["plane_width"] + x * 8,
                                       context=(mode, encoding, igs, y))
                peer.shutdown(socket.SHUT_RDWR)
            client.wait_text("Directory: Name")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("executable")
    parser.add_argument("--reports", required=True)
    args = parser.parse_args()
    for mode in ("AtariST40x25", "AtariST80x25", "AtariST80x25Mono"):
        for encoding in ("ATARI-ST", "CP437", "UTF-8"):
            for igs in (False, True):
                run(args.executable, args.reports, mode, encoding, igs)
