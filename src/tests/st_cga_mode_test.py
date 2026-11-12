#!/usr/bin/env python3
"""CGA geometry and literal full-block rasters, with coherent captures.

U+2588/CP437 DB is eight solid rows, independent of the renderer. The bitmap
backend's remote ANSI bank is16..31; red,green,blue are17,18,20. Blink
replaces the first block with its blue background in the alternate plane.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from array import array
from pathlib import Path
import socket
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "testing"))
from syncterm_fixture import fixture, cell_at


def geometry(frame):
    m = frame.metadata
    assert (m["columns"], m["cell_width"], m["cell_height"]) == (80, 8, 8)
    assert (m["plane_width"], m["plane_height"]) == (640, 200)


def check(frame, status):
    m, state = frame.metadata, frame.application
    geometry(frame)
    assert (m["destination_width"], m["destination_height"]) == (960, 720)
    assert (m["cursor_start"], m["cursor_end"], m["cursor_range"]) == (6, 7, 8)
    assert not m["cursor_visible"]
    assert (state["cterm.x"], state["cterm.y"]) == (6, 4)
    assert state["cterm.graphics_mode"] == 0
    assert frame.palette[16] == 0 and frame.palette[17] == 0xa80000
    assert frame.palette[18] == 0x00a800 and frame.palette[20] == 0x0000a8
    for x in (2, 4): assert frame.cell_text(cell_at(frame, x, 3)) == "\u2588"
    rows = 24 if status else 25
    start = m["pixel_top"] * 640
    for phase, first in ((frame.primary, 17), (frame.alternate, 20)):
        expected = array("I", [16]) * (640 * rows * 8)
        for y in range(24, 32):
            expected[y*640+16:y*640+24] = array("I", [first])*8
            expected[y*640+32:y*640+40] = array("I", [18])*8
        observed = memoryview(phase)[start:start+len(expected)]
        assert observed.tobytes() == expected.tobytes(), next(
            ((i % 640, i // 640, a, e) for i, (a, e) in enumerate(zip(observed, expected)) if a != e), "length")
    # Sample flat interiors: fractional aspect scaling filters boundary pixels.
    # The complete native indexed rasters above remain exact in both phases.
    for x, y, index in ((36, 28, 18), (48, 28, 16), (0, 0, 16)):
        ox = m["destination_x"] + (2*x+1)*960//(2*640)
        oy = m["destination_y"] + (2*y+1)*720//(2*200)
        assert frame.rgb[oy*m["output_width"]+ox] & 0xffffff == frame.palette[index], (
            x, y, ox, oy, hex(frame.rgb[oy*m["output_width"]+ox]), hex(frame.palette[index]))


def run(executable, reports, encoding, status):
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0)); listener.listen(2); listener.settimeout(15)
        entries = ("[CGA mode]\nConnectionType=Raw\nAddress=127.0.0.1\n"
                   f"Port={listener.getsockname()[1]}\nScreenMode=CGA80x25\n"
                   f"Font=Codepage 437 English\nEncoding={encoding}\nNoStatus={str(not status).lower()}\n")
        with fixture(executable, entries=entries, reports=reports, quiet=False,
                     settings="ScreenMode=CGA80x25\nBlockyScaling=true\n") as (client, root):
            geometry(client.wait_text("CGA mode"))
            for connection in range(2):
                client.key("enter")
                with listener.accept()[0] as peer:
                    client.wait_for(lambda f: f.application.get("connected") == 1)
                    client.resize(960, 720)
                    block = b"\xdb" if encoding == "CP437" else "\u2588".encode()
                    wire = (b"\x1b[?25l\x1b[0m\x1b[2J\x1b[4;3H\x1b[31;44;5m" + block
                            + b"\x1b[4;5H\x1b[32;44;25m" + block + b"\x1b[6n")
                    with (root / "wire.bin").open("ab") as log: log.write(wire)
                    peer.sendall(wire)
                    peer.settimeout(3)
                    reply = b""
                    while len(reply) < 6:
                        data = peer.recv(6-len(reply))
                        assert data, reply
                        reply += data
                    assert reply == b"\x1b[4;6R", reply
                    frame = client.capture()
                    check(frame, status)
                    peer.shutdown(socket.SHUT_RDWR)
                    client.wait_text("Disconnected: Enter exits, F1 details"); client.key("enter")
                geometry(client.wait_text("Directory: Name"))
            assert b"[wren" not in client.stderr, bytes(client.stderr)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("executable")
    parser.add_argument("--reports", default="reports/test-control")
    args = parser.parse_args()
    for encoding in ("CP437", "UTF-8"):
        for status in (False, True): run(args.executable, args.reports, encoding, status)
