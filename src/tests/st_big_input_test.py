#!/usr/bin/env python3
"""BIG selection and live I through coherent bitmap/session captures.

Glyph rows, EGA colours and I0/I1 effects come from BIG chapters47/48/54
and the original IBM ROM banks identified in docs/BIG_IMPLEMENTATION.adoc.
Each scene observes cells, both indexed planes, palette and state together.
No expected images are produced by SyncTERM. Physical audio/MIDI are off.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from array import array
from pathlib import Path
import socket
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "testing"))
from syncterm_fixture import fixture, cell_at, click_text
from syncterm_control import CONTROL

PALETTE = (0, 0xaa, 0xaa00, 0xaaaa, 0xaa0000, 0xaa00aa, 0xaa5500, 0xaaaaaa,
           0x555555, 0x5555ff, 0x55ff55, 0x55ffff, 0xff5555, 0xff55ff, 0xffff55, 0xffffff)
A = ((0x30, 0x78, 0xcc, 0xcc, 0xfc, 0xcc, 0xcc, 0),
     (0, 0, 0x10, 0x38, 0x6c, 0xc6, 0xc6, 0xfe, 0xc6, 0xc6, 0xc6, 0, 0, 0),
     (0, 0, 0x10, 0x38, 0x6c, 0xc6, 0xc6, 0xfe, 0xc6, 0xc6, 0xc6, 0xc6, 0, 0, 0, 0))
PROFILES = (("CGA80x25", 200, 8, 25), ("EGA80x25", 350, 14, 25), ("80x30", 480, 16, 30))


def check(frame, resolution, status, seeded, token, loop_state=None):
    _, height, ch, rows = PROFILES[resolution - 1]
    m, s = frame.metadata, frame.application
    assert (m["columns"], m["cell_width"], m["cell_height"], m["plane_width"], m["plane_height"]) == (80, 8, ch, 640, height), m
    assert (s["cterm.graphics_mode"], s["cterm.skypix"], s["cterm.big_resolution"]) == (6, 0, resolution), s
    assert (s["cterm.big_bgi_width"], s["cterm.big_bgi_height"]) == (640, height)
    assert (s["cterm.x"], s["cterm.y"], s["cterm.bottom_margin"]) == (len(token) + 1, 1, rows - int(status))
    assert not m["cursor_visible"] and not s["cterm.big_pair_wait"]
    assert s["cterm.big_loop_type"] == 32 and not s["cterm.big_footer_active"]
    assert not s["cterm.big_reply_pending"]
    assert not s["cterm.big_loop_active"] and not s["cterm.big_setup_active"]
    if loop_state is not None:
        assert (s["cterm.big_loop_current"], s["cterm.big_loop_count"],
                s["cterm.big_loop_index"], s["cterm.big_no_parameters"]) == loop_state
    assert tuple(frame.palette[800:816]) == PALETTE
    assert frame.cell_text(cell_at(frame, 12, 12)) == ("A" if seeded else " ")
    assert frame.cell_text(cell_at(frame, 2, 3)) == ("\u2588" if seeded else " ")
    if seeded:
        block = cell_at(frame, 2, 3)
        assert (block.foreground, block.background) == (804, 801)
    drawable = (rows - int(status)) * ch
    # Row0 is the source fence, not part of the scene oracle. Status is UI.
    start = (m["pixel_top"] + ch) * 640
    for plane, block_index in ((frame.primary, 804), (frame.alternate, 801)):
        expected = array("I", [800]) * (640 * (drawable - ch))
        if seeded:
            for y, row in enumerate(A[resolution - 1]):
                for x in range(8):
                    if row & (128 >> x):
                        expected[(12 * ch + y - ch) * 640 + 96 + x] = 807
            for y in range(3 * ch, 4 * ch):
                offset = (y - ch) * 640 + 16
                expected[offset:offset + 8] = array("I", [block_index]) * 8
        observed = memoryview(plane)[start:start + len(expected)]
        assert observed.tobytes() == expected.tobytes(), next(
            ((i % 640, i // 640 + ch, actual, want)
             for i, (actual, want) in enumerate(zip(observed, expected)) if actual != want), "length")
    assert (m["destination_width"], m["destination_height"]) == (960, 720)
    # Flat background interiors avoid differences from fractional edge filtering.
    ox = m["destination_x"] + 500 * 960 // 640
    oy = m["destination_y"] + 2 * ch * 720 // height
    assert frame.rgb[oy * m["output_width"] + ox] & 0xffffff == 0


def run(executable, reports, resolution, encoding, status, screen=None):
    mode = PROFILES[resolution - 1][0] if screen is None else screen
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0)); listener.listen(2); listener.settimeout(15)
        entries = ("[BIG input]\nConnectionType=Raw\nAddress=127.0.0.1\n"
                   f"Port={listener.getsockname()[1]}\nGraphics=BIG (1.03)\nScreenMode={mode}\n"
                   f"Font=Codepage 437 English\nEncoding={encoding}\nNoStatus={str(not status).lower()}\n")
        with fixture(executable, entries=entries, reports=reports, quiet=False,
                     settings="BlockyScaling=true\n") as (client, root):
            menu = client.wait_text("BIG input")
            for connection in range(2):
                client.key("enter")
                with listener.accept()[0] as peer:
                    client.wait_for(lambda f: f.application.get("connected") == 1)
                    client.resize(960, 720)
                    block = b"\xdb" if encoding == "CP437" else "\u2588".encode()
                    seed = b"\x1b[?25l\x1b[0m\x1b[2J\x1b[13;13HA\x1b[4;3H\x1b[31;44;5m" + block + b"\x1b[0m"
                    for step, command in enumerate((b"I#I 1:", b"I#I 0:", b"I#I 1:")):
                        token = f"LIVE{connection}{step}"
                        loop = (b"I#& 0,3,1,0,I@1,1:" if step == 0 else
                                b"I#& 0,2,1,0,>I@@2,0,1:" if step == 2 else b"")
                        wire = (seed if step != 1 else b"") + command + loop + b"I#@>I 1:" + b"\x1b[H" + token.encode() + b"\x1b[6n"
                        with (root / "wire.bin").open("ab") as log: log.write(wire)
                        peer.sendall(wire)
                        expected = (b"\x13\x11" if loop else b"") + f"\x1b[1;{len(token) + 1}R".encode()
                        peer.settimeout(3); reply = b""
                        while len(reply) < len(expected):
                            data = peer.recv(len(expected) - len(reply)); assert data, reply
                            reply += data
                        with (root / "replies.bin").open("ab") as log: log.write(reply)
                        assert reply == expected, reply
                        loop_state = (3, 1, 0, 1) if step == 0 else (2, 2, 0, 1) if step == 2 else None
                        check(client.wait_text(token), resolution if step == 0 else 2, status, step != 1, token, loop_state)
                    if connection == 1:
                        token = "KEYCANCEL1"
                        wire = b"I#& 0,1000,1,9999,I@1,1:\x1b[H" + token.encode() + b"\x1b[6n"
                        with (root / "wire.bin").open("ab") as log: log.write(wire)
                        peer.sendall(wire)
                        peer.settimeout(3); assert peer.recv(1) == b"\x13"
                        client.wait_for(lambda f: f.application.get("cterm.big_loop_active") == 1
                                        and not f.application.get("cterm.big_setup_active"))
                        client.key(3, CONTROL)
                        expected = b"\x11" + f"\x1b[1;{len(token) + 1}R".encode()
                        reply = b""
                        while len(reply) < len(expected):
                            data = peer.recv(len(expected) - len(reply)); assert data, reply
                            reply += data
                        assert reply == expected, reply
                        with (root / "replies.bin").open("ab") as log: log.write(b"\x13" + reply)
                        check(client.wait_text(token), 2, status, True, token, (1, 1, 0, 1))
                    # Both partial command bodies and partial & tables must
                    # leave no pending source/key state in a fresh connection.
                    peer.sendall(b"I#& 7,1,1,0,I@2,1:" if connection else b"I#I 0")
                    peer.shutdown(socket.SHUT_RDWR)
                    client.wait_text("Disconnected: Enter exits, F1 details"); client.key("enter")
                restored = client.wait_text("Directory: Name")
                for key in ("columns", "rows", "plane_width", "plane_height", "cell_width", "cell_height"):
                    assert restored.metadata[key] == menu.metadata[key], key
            assert b"[wren" not in client.stderr, bytes(client.stderr)


def editor(executable, reports):
    with fixture(executable, entries="[BIG editor]\nAddress=example.invalid\nFont=Atari ST\n", reports=reports) as (client, root):
        client.wait_text("BIG editor"); client.key("f2")
        click_text(client, client.wait_text("Graphics"), "Graphics")
        client.wait_text("Enter Select"); client.key("end"); client.key("enter")
        frame = client.wait_for(lambda f: "BIG (1.03)" in f.text and "EGA 80x25" in f.text)
        assert "Codepage 437 English" in frame.text and "CP437" in frame.text
        click_text(client, frame, "Screen Mode")
        frame = client.wait_text("Enter Select")
        assert "CGA 80x25" in frame.text and "EGA 80x25" in frame.text and "80x30" in frame.text
        client.key("home"); client.key("enter")
        frame = client.wait_text("Graphics")
        click_text(client, frame, "Encoding")
        client.wait_text("Enter Select"); client.key("end"); client.key("enter")
        client.wait_for(lambda f: "UTF-8" in f.text and "CGA 80x25" in f.text and "BIG (1.03)" in f.text)
        client.key("escape"); client.wait_text("Directory: Name")
        saved = (root / "syncterm.lst").read_text()
        assert "Graphics=BIG (1.03)" in saved and "ScreenMode=CGA80x25" in saved, saved
        assert "Encoding=UTF-8" in saved, saved
        assert "Font=Codepage 437 English" in saved, saved
        assert b"[wren" not in client.stderr, bytes(client.stderr)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("executable")
    parser.add_argument("--reports", default="reports/test-control")
    args = parser.parse_args()
    editor(args.executable, args.reports)
    for resolution in (1, 2, 3):
        for encoding in ("CP437", "UTF-8"):
            for status in (False, True): run(args.executable, args.reports, resolution, encoding, status)
    run(args.executable, args.reports, 2, "CP437", False, "Current")
    run(args.executable, args.reports, 2, "CP437", False, "AtariST40x25")
