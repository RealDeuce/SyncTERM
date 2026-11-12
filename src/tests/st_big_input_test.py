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

# Literal masks from BIG22/58: x=anchorX-4..+4, y=anchorY-4..+8.
# Bit0 is the leftmost pixel. These are authored expectations, not captures.
MARKERS = (
    (0, 0, 0, 0, 0x10, 0, 0, 0, 0, 0, 0, 0, 0),
    (0, 0, 0, 0, 0x10, 0x38, 0x54, 0x92, 0x111, 0x10, 0x10, 0x10, 0x10),
    (0, 0x10, 0x10, 0x10, 0x1ff, 0x10, 0x10, 0x10, 0),
    (0x10, 0x92, 0x54, 0x38, 0x1ff, 0x38, 0x54, 0x92, 0x10),
    (0x1ff, 0x101, 0x101, 0x101, 0x101, 0x101, 0x101, 0x101, 0x1ff),
    (0x101, 0x82, 0x44, 0x28, 0x10, 0x28, 0x44, 0x82, 0x101),
    (0x10, 0x28, 0x44, 0x82, 0x101, 0x82, 0x44, 0x28, 0x10),
    (0x1ff, 0x183, 0x145, 0x129, 0x111, 0x129, 0x145, 0x183, 0x1ff),
)


def check(frame, resolution, status, seeded, token, loop_state=None, scaling=0, steps=(12, 6), zones=None, marker=None, line=(0, 0, 1, 65535), graph_error=0, graph_point=None, marker_kind=None, line_pixels=(), writing=(0, 0), colors=(15, 15, 15, 15), graph_color=15, colored_pixels=(), fill=(1, 15), rgb_sample=None, selected_fill=1, fill_pixels=(0xffffffff,) * 8, custom0=(0x55aa55aa, 0x55aa55aa), lower_points=None, rejections=(0, 0), hollow=0):
    _, height, ch, rows = PROFILES[resolution - 1]
    m, s = frame.metadata, frame.application
    assert (m["columns"], m["cell_width"], m["cell_height"], m["plane_width"], m["plane_height"]) == (80, 8, ch, 640, height), m
    assert (s["cterm.graphics_mode"], s["cterm.skypix"], s["cterm.big_resolution"]) == (6, 0, resolution), s
    assert (s["cterm.big_bgi_width"], s["cterm.big_bgi_height"]) == (640, height)
    assert (s["cterm.x"], s["cterm.y"], s["cterm.bottom_margin"]) == (len(token) + 1, 1, rows - int(status))
    assert not m["cursor_visible"] and not s["cterm.big_pair_wait"]
    assert s["cterm.big_loop_type"] == 32 and not s["cterm.big_footer_active"]
    assert not s["cterm.big_collect_active"] and not s["cterm.big_pointer_active"]
    assert not s["cterm.big_pattern_active"]
    assert not s["cterm.big_bar_active"]
    assert not s["cterm.big_flood_active"]
    assert (s["cterm.big_last_error"], s["cterm.big_rejected_commands"]) == rejections
    assert not s["cterm.big_reply_pending"]
    assert not s["cterm.big_loop_active"] and not s["cterm.big_setup_active"]
    assert s["cterm.big_scaling"] == scaling
    assert s["cterm.big_hollow"] == hollow
    assert (s["cterm.big_step_x"], s["cterm.big_step_y"]) == steps
    assert not s["cterm.big_zone_active"]
    if zones is None:
        zones = ((0, 0, 0, 0), 0, 0, 0)
    coordinates, loopback, prefix, tail = zones
    assert tuple(s[f"cterm.big_zone0_{field}"] for field in ("x1", "y1", "x2", "y2")) == coordinates
    assert (s["cterm.big_zone_loopback"], s["cterm.big_zone0_prefix"], s["cterm.big_zone0_tail"]) == (loopback, prefix, tail)
    assert (s["cterm.big_line_style"], s["cterm.big_line_user"], s["cterm.big_line_width"], s["cterm.big_line_mask"]) == line
    assert s["cterm.big_bgi_error"] == graph_error
    assert (s["cterm.big_write_mode"], s["cterm.big_cached_write_mode"]) == writing
    point = ((0, 0) if marker is None else (120, 64)) if graph_point is None else graph_point
    for domain in ("point", "ddo", "device"):
        expected_point = point if domain == "point" or lower_points is None else lower_points[0 if domain == "ddo" else 1]
        assert (s[f"cterm.big_{domain}_x"], s[f"cterm.big_{domain}_y"]) == expected_point
    kind = (0 if marker is None else marker[0]) if marker_kind is None else marker_kind
    assert (s["cterm.big_marker"], s["cterm.big_marker_size"]) == (kind, 1)
    assert s["cterm.big_color"] == graph_color and s["cterm.big_device_color"] == graph_color & 15
    assert tuple(s[f"cterm.big_{name}_color"] for name in ("marker", "line", "fill", "text")) == colors
    assert (s["cterm.big_bgi_fill_style"], s["cterm.big_bgi_fill_color"]) == fill
    assert s["cterm.big_fill_selection"] == selected_fill
    assert (s["cterm.big_custom0_prefix"], s["cterm.big_custom0_tail"]) == custom0
    assert (s["cterm.big_fill_byte"], s["cterm.big_device_fill_color"], s["cterm.big_device_fill_style"]) == (fill[1] & 255, fill[1] & 15, fill[0])
    assert tuple(s[f"cterm.big_fill_row{row}"] for row in range(8)) == fill_pixels
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
        if marker is not None:
            for y, row in enumerate(marker[1]):
                for x in range(9):
                    if row & (1 << x):
                        expected[(60 + y - ch) * 640 + 116 + x] = 815
        for x, y in line_pixels:
            expected[(y - ch) * 640 + x] = 815
        for x, y, pen in colored_pixels:
            expected[(y - ch) * 640 + x] = 800 + pen
        observed = memoryview(plane)[start:start + len(expected)]
        assert observed.tobytes() == expected.tobytes(), next(
            ((i % 640, i // 640 + ch, actual, want)
             for i, (actual, want) in enumerate(zip(observed, expected)) if actual != want), "length")
    assert (m["destination_width"], m["destination_height"]) == (960, 720)
    # Flat background interiors avoid differences from fractional edge filtering.
    ox = m["destination_x"] + 500 * 960 // 640
    oy = m["destination_y"] + 2 * ch * 720 // height
    assert frame.rgb[oy * m["output_width"] + ox] & 0xffffff == 0
    if rgb_sample is not None:
        # Interior of an independently specified solid swatch; all its
        # neighbours agree, avoiding fractional edge-filter differences.
        x, y, color = rgb_sample
        ox = m["destination_x"] + (2 * x + 1) * m["destination_width"] // 1280
        oy = m["destination_y"] + (2 * y + 1) * m["destination_height"] // (2 * height)
        assert frame.rgb[oy * m["output_width"] + ox] & 0xffffff == color


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
                        for index, (request, target) in enumerate(((1, 1), (3, 3), (2, 2), (2, 2), (258, 1))):
                            token = f"RES{index}_{target}"
                            # The old coloured sentinel must disappear because
                            # R clears, including a repeated same-mode request.
                            # No ED follows R; new A uses its default attribute.
                            wire = (b"\x1b[8;41H\x1b[35;44;5mX" + f"I#R {request}:".encode()
                                    + b"\x1b[13;13HA\x1b[4;3H\x1b[31;44;5m" + block
                                    + b"\x1b[0m\x1b[H" + token.encode() + b"\x1b[6n")
                            with (root / "wire.bin").open("ab") as log: log.write(wire)
                            peer.sendall(wire)
                            expected = f"\x1b[1;{len(token) + 1}R".encode()
                            reply = b""
                            while len(reply) < len(expected):
                                data = peer.recv(len(expected) - len(reply)); assert data, reply
                                reply += data
                            assert reply == expected, reply
                            with (root / "replies.bin").open("ab") as log: log.write(reply)
                            check(client.wait_text(token), target, status, True, token)
                        for index, (request, scaling) in enumerate(((b"2:", 2), (b"3:", 3),
                                                                   (b"258:", 258), (b"+1:", 65535), (b"0:", 0))):
                            token = f"SCALE{index}"
                            # I1 and a same-mode R preserve g. R's clear is
                            # followed by fresh scene glyphs without ED.
                            after = b"I#I 1:" if index == 0 else (
                                b"I#R 1:\x1b[13;13HA\x1b[4;3H\x1b[31;44;5m" + block + b"\x1b[0m"
                                if index == 1 else b"")
                            wire = b"I#g " + request + after + b"\x1b[H" + token.encode() + b"\x1b[6n"
                            with (root / "wire.bin").open("ab") as log: log.write(wire)
                            peer.sendall(wire)
                            expected = f"\x1b[1;{len(token) + 1}R".encode()
                            reply = b""
                            while len(reply) < len(expected):
                                data = peer.recv(len(expected) - len(reply)); assert data, reply
                                reply += data
                            assert reply == expected, reply
                            with (root / "replies.bin").open("ab") as log: log.write(reply)
                            check(client.wait_text(token), 1, status, True, token, scaling=scaling)
                        stamp = b"\x1b[13;13HA\x1b[4;3H\x1b[31;44;5m" + block + b"\x1b[0m"
                        extensions = (
                            (b"I#R 2:I#g 2:" + stamp + b"I#X 6,5,5:", 2, 2, (5, 8)),
                            (b"I#R 3:I#g 3:" + stamp + b"I#X 6,5,5:", 3, 3, (5, 11)),
                            (b"I#X 6,0,20:", 3, 3, (1, 15)),
                            (b"I#X 6,+1,+1:", 3, 3, (1, 1)),
                            (b"I#I 1:", 3, 3, (1, 1)),
                        )
                        for index, (command, target, scaling, steps) in enumerate(extensions):
                            token = f"STEPS{index}"
                            wire = command + b"\x1b[H" + token.encode() + b"\x1b[6n"
                            with (root / "wire.bin").open("ab") as log: log.write(wire)
                            peer.sendall(wire)
                            expected = f"\x1b[1;{len(token) + 1}R".encode()
                            reply = b""
                            while len(reply) < len(expected):
                                data = peer.recv(len(expected) - len(reply)); assert data, reply
                                reply += data
                            assert reply == expected, reply
                            with (root / "replies.bin").open("ab") as log: log.write(reply)
                            check(client.wait_text(token), target, status, True, token,
                                  scaling=scaling, steps=steps)
                        # Counted payloads include fake commands, NUL and ff;
                        # the extra helper result is discarded without validation.
                        zones = (
                            (b"I#X 4,0,10,20,30,40,6:I#R 1:!", (10, 47, 30, 95), 0, 0x52234906, 0x003a3120),
                            (b"I#X 4,0,1,2,3,4,2:\0\xff@", (1, 4, 3, 9), 0, 0x52ff0002, 0x003a3120),
                            (b"I#X 4,9998:", (1, 4, 3, 9), 1, 0x52ff0002, 0x003a3120),
                            (b"I#R 3:" + stamp, (1, 4, 3, 9), 1, 0x52ff0002, 0x003a3120),
                            (b"I#X 4,9999:", (65535, 65535, 65535, 65535), 1, 0x52ff0002, 0x003a3120),
                            (b"I#X 4,9997:", (65535, 65535, 65535, 65535), 0, 0x52ff0002, 0x003a3120),
                        )
                        for index, (command, coordinates, loopback, prefix, tail) in enumerate(zones):
                            token = f"ZONES{index}"
                            wire = command + b"\x1b[H" + token.encode() + b"\x1b[6n"
                            with (root / "wire.bin").open("ab") as log: log.write(wire)
                            peer.sendall(wire)
                            expected = f"\x1b[1;{len(token) + 1}R".encode()
                            reply = b""
                            while len(reply) < len(expected):
                                data = peer.recv(len(expected) - len(reply)); assert data, reply
                                reply += data
                            assert reply == expected, reply
                            with (root / "replies.bin").open("ab") as log: log.write(reply)
                            check(client.wait_text(token), 3, status, True, token, scaling=3, steps=(1, 1),
                                  zones=(coordinates, loopback, prefix, tail))
                        # Every marker type in every physical mode, then line
                        # phase/rejection/zero-mask/width2 composition.
                        zone_state = ((65535, 65535, 65535, 65535), 0, 0x52ff0002, 0x003a3120)
                        marker_cases = []
                        for target in (1, 2, 3):
                            for kind in range(8):
                                command = f"I#R {target}:I#g 0:".encode() + stamp + f"I#T>1,{kind},1:P 120,64:".encode()
                                marker_cases.append((command, target, kind, MARKERS[kind], (0, 0, 1, 65535), 0))
                        patterned = (0x66, 0, 0x101, 0x101, 0, 0, 0x101, 0x101, 0xcc)
                        for command, kind, rows, line, error in (
                                (b"I#T>2,4,204,257,0:T>1,4,1:P 120,64:", 4, patterned, (4, 0xcccc, 1, 0xcccc), 0),
                                (b"I#T>2,4,1,1,9:T>1,4,1:P 120,64:", 4, MARKERS[4], (0, 0, 1, 65535), -11),
                                (b"I#T>2,4,0,1,0:T>1,1,1:P 120,64:", 1, (0,) * 13, (4, 0, 1, 0), -11),
                                (b"I#T>2,0,1,1,2:T>1,2,1:P 120,64:", 2, MARKERS[2], (0, 1, 2, 65535), -11)):
                            marker_cases.append((b"I#R 3:" + stamp + command, 3, kind, rows, line, error))
                        for index, (command, target, kind, rows, line, error) in enumerate(marker_cases):
                            token = f"MARKER{index}"
                            wire = command + b"\x1b[H" + token.encode() + b"\x1b[6n"
                            with (root / "wire.bin").open("ab") as log: log.write(wire)
                            peer.sendall(wire)
                            expected = f"\x1b[1;{len(token) + 1}R".encode()
                            reply = b""
                            while len(reply) < len(expected):
                                data = peer.recv(len(expected) - len(reply)); assert data, reply
                                reply += data
                            assert reply == expected, reply
                            with (root / "replies.bin").open("ab") as log: log.write(reply)
                            check(client.wait_text(token), target, status, True, token, scaling=0, steps=(1, 1),
                                  zones=zone_state, marker=(kind, rows), line=line, graph_error=error)
                        # Wire tails remain buffered until local acquisition
                        # completes; replies precede their DSR fence exactly.
                        for index, (command, text_reply) in enumerate(((b"I#< 1,0,0:", b"A\r"),
                                                                      (b"\x1b[13;13HI#< 1,1,1:", b"\r"))):
                            token = f"INPUT{index}"
                            wire = command + b"\x1b[H" + token.encode() + b"\x1b[6n"
                            with (root / "wire.bin").open("ab") as log: log.write(wire)
                            peer.sendall(wire)
                            client.wait_for(lambda f: f.application.get("cterm.big_collect_active") == 1)
                            client.text("A")
                            if index:
                                client.key("backspace")
                                client.key("enter")
                            expected = text_reply + f"\x1b[1;{len(token) + 1}R".encode()
                            reply = b""
                            while len(reply) < len(expected):
                                data = peer.recv(len(expected) - len(reply)); assert data, reply
                                reply += data
                            assert reply == expected, reply
                            with (root / "replies.bin").open("ab") as log: log.write(reply)
                            check(client.wait_text(token), 3, status, True, token, scaling=0, steps=(1, 1), zones=zone_state,
                                  marker=(2, MARKERS[2]), line=(0, 1, 2, 65535), graph_error=-11)
                        token = "HYBRID"
                        wire = (f"I#R {resolution}:I#g 0:".encode() + stamp +
                                b"I#X 4,0,0,0,639,479,3:M\0Z!I#X 4,9997:I#T 1,2,1:I#X 6,5,7:I#< 0,2,0:" +
                                b"\x1b[H" + token.encode() + b"\x1b[6n")
                        with (root / "wire.bin").open("ab") as log: log.write(wire)
                        peer.sendall(wire)
                        active = client.wait_for(lambda f: f.application.get("cterm.big_pointer_active") == 1)
                        assert (active.application["cterm.big_pointer_x"], active.application["cterm.big_pointer_y"]) == (318, 98)
                        def move(action, buttons):
                            m = client.capture().metadata
                            ox = m["destination_x"] + 161 * m["destination_width"] // 1280
                            oy = m["destination_y"] + 121 * m["destination_height"] // (2 * PROFILES[resolution - 1][1])
                            client.mouse(ox, oy, action=action, buttons=buttons)
                        move(0, 0)
                        client.wait_for(lambda f: f.application.get("cterm.big_pointer_x") == 80)
                        client.key("right")
                        client.wait_for(lambda f: f.application.get("cterm.big_pointer_x") == 85)
                        move(1, 1)
                        armed = client.wait_for(lambda f: f.application.get("cterm.big_pointer_armed") == 1)
                        assert armed.application["cterm.big_pointer_x"] == 85
                        client.key("down")
                        client.wait_for(lambda f: f.application.get("cterm.big_pointer_y") == 67)
                        move(1, 0)
                        expected = b"M\0Z" + f"\x1b[1;{len(token) + 1}R".encode()
                        reply = b""
                        while len(reply) < len(expected):
                            data = peer.recv(len(expected) - len(reply)); assert data, reply
                            reply += data
                        assert reply == expected, reply
                        with (root / "replies.bin").open("ab") as log: log.write(reply)
                        check(client.wait_text(token), resolution, status, True, token, scaling=0, steps=(5, 7),
                              zones=((0, 0, 639, 479), 0, 0x5a004d03, 0x003a3120), graph_error=-11,
                              graph_point=(89, 67), marker_kind=2)
                        # Literal chapter57 candidates distinguish L/D endpoint
                        # order and d's Y-sign defect. R clears only the scene;
                        # the prior -11 BGI error must not suppress drawing.
                        line_cases = (
                            (b"I#T>2,1,1,1,0:L 100,60,107,60:", (107, 60), (1, 1, 1, 0xcccc),
                             ((102, 60), (103, 60), (106, 60), (107, 60))),
                            (b"I#P>100,60:T>2,1,1,1,0:D 107,60:", (107, 60), (1, 1, 1, 0xcccc),
                             ((100, 60), (101, 60), (104, 60), (105, 60))),
                            (b"I#P>120,60:d 1,4,257,2:", (116, 62), (0, 0, 1, 65535),
                             ((120, 60), (119, 61), (118, 61), (117, 62), (116, 62))),
                            (b"I#T>2,0,1,1,1:L 100,60,103,60:", (103, 60), (0, 1, 3, 65535),
                             ((100, 59), (101, 59), (102, 59), (103, 59),
                              (100, 60), (101, 60), (102, 60), (103, 60),
                              (100, 61), (101, 61), (102, 61), (103, 61))),
                            (b"I#T>2,4,0,1,0:L 100,60,103,60:", (103, 60), (4, 0, 1, 0), ()),
                        )
                        for index, (command, point, line, pixels) in enumerate(line_cases):
                            token = f"LINE{index}"
                            wire = (f"I#R {resolution}:".encode() + stamp + b"I#T 1,0,1:" + command +
                                    b"\x1b[H" + token.encode() + b"\x1b[6n")
                            with (root / "wire.bin").open("ab") as log: log.write(wire)
                            peer.sendall(wire)
                            expected = f"\x1b[1;{len(token) + 1}R".encode()
                            reply = b""
                            while len(reply) < len(expected):
                                data = peer.recv(len(expected) - len(reply)); assert data, reply
                                reply += data
                            assert reply == expected, reply
                            with (root / "replies.bin").open("ab") as log: log.write(reply)
                            check(client.wait_text(token), resolution, status, True, token, steps=(5, 7),
                                  zones=((0, 0, 639, 479), 0, 0x5a004d03, 0x003a3120), graph_error=-11,
                                  graph_point=point, line=line, line_pixels=pixels)
                        # Chapter51 m has no raster effect. A later D starts
                        # at the moved point without leaving a P marker dot.
                        move_cases = (
                            (f"I#R {resolution}:".encode() + stamp + b"I#m 256,120,60:", (120, 60), ()),
                            (b"I#m 1,4,1,2:", (116, 62), ()),
                            (b"I#D 120,62:", (120, 62),
                             ((116, 62), (117, 62), (118, 62), (119, 62), (120, 62))),
                        )
                        for index, (command, point, pixels) in enumerate(move_cases):
                            token = f"MOVE{index}"
                            wire = command + b"\x1b[H" + token.encode() + b"\x1b[6n"
                            with (root / "wire.bin").open("ab") as log: log.write(wire)
                            peer.sendall(wire)
                            expected = f"\x1b[1;{len(token) + 1}R".encode()
                            reply = b""
                            while len(reply) < len(expected):
                                data = peer.recv(len(expected) - len(reply)); assert data, reply
                                reply += data
                            assert reply == expected, reply
                            with (root / "replies.bin").open("ab") as log: log.write(reply)
                            check(client.wait_text(token), resolution, status, True, token, steps=(5, 7),
                                  zones=((0, 0, 639, 479), 0, 0x5a004d03, 0x003a3120), graph_error=-11,
                                  graph_point=point, line_pixels=pixels)
                        # M affects line consumers; standalone P dots still
                        # copy. R clears the installed mode but retains cache.
                        writing_cases = (
                            (f"I#R {resolution}:".encode() + stamp +
                             b"I#M>2:L>100,60,102,60:L 100,60,102,60:", (102, 60), (1, 1), ()),
                            (b"I#M>3:P>120,60:P 120,60:", (120, 60), (1, 1), ((120, 60),)),
                            (f"I#R {resolution}:".encode() + stamp +
                             b"I#L>100,60,102,60:L 100,60,102,60:", (102, 60), (0, 1),
                             ((100, 60), (101, 60), (102, 60))),
                            (b"I#M>258:L>100,60,102,60:L 100,60,102,60:", (102, 60), (0, 0),
                             ((100, 60), (101, 60), (102, 60))),
                        )
                        for index, (command, point, writing, pixels) in enumerate(writing_cases):
                            token = f"WRITING{index}"
                            wire = command + b"\x1b[H" + token.encode() + b"\x1b[6n"
                            with (root / "wire.bin").open("ab") as log: log.write(wire)
                            peer.sendall(wire)
                            expected = f"\x1b[1;{len(token) + 1}R".encode()
                            reply = b""
                            while len(reply) < len(expected):
                                data = peer.recv(len(expected) - len(reply)); assert data, reply
                                reply += data
                            assert reply == expected, reply
                            with (root / "replies.bin").open("ab") as log: log.write(reply)
                            check(client.wait_text(token), resolution, status, True, token, steps=(5, 7),
                                  zones=((0, 0, 639, 479), 0, 0x5a004d03, 0x003a3120), graph_error=-11,
                                  graph_point=point, line_pixels=pixels, writing=writing)
                        # C stores full application WORDs. Only later consumers
                        # reduce draw bytes/pens; fill/text C stores no pixels.
                        color_cases = (
                            (f"I#R {resolution}:".encode() + stamp +
                             b"I#C>1,291:L>100,60,102,60:L>100,61,102,61:L 100,62,102,62:",
                             (102, 62), (15, 291, 15, 15), 0x23,
                             ((100, 60, 3), (101, 60, 3), (102, 60, 3),
                              (100, 61, 3), (101, 61, 3), (102, 61, 3), (100, 62, 3), (101, 62, 3), (102, 62, 3))),
                            (b"I#C>0,261:P 120,60:", (120, 60), (261, 291, 15, 15), 5,
                             ((100, 60, 3), (101, 60, 3), (102, 60, 3),
                              (100, 61, 3), (101, 61, 3), (102, 61, 3), (100, 62, 3), (101, 62, 3), (102, 62, 3), (120, 60, 5))),
                            (b"I#C>2,31:C 3,+1:", (120, 60), (261, 291, 31, 65535), 5,
                             ((100, 60, 3), (101, 60, 3), (102, 60, 3),
                              (100, 61, 3), (101, 61, 3), (102, 61, 3), (100, 62, 3), (101, 62, 3), (102, 62, 3), (120, 60, 5))),
                            (b"I#C>258,7:P 124,60:", (124, 60), (7, 291, 31, 65535), 7,
                             ((100, 60, 3), (101, 60, 3), (102, 60, 3),
                              (100, 61, 3), (101, 61, 3), (102, 61, 3), (100, 62, 3), (101, 62, 3), (102, 62, 3), (120, 60, 5), (124, 60, 7))),
                        )
                        for index, (command, point, colors, graph_color, pixels) in enumerate(color_cases):
                            token = f"COLOR{index}"
                            wire = command + b"\x1b[H" + token.encode() + b"\x1b[6n"
                            with (root / "wire.bin").open("ab") as log: log.write(wire)
                            peer.sendall(wire)
                            expected = f"\x1b[1;{len(token) + 1}R".encode()
                            reply = b""
                            while len(reply) < len(expected):
                                data = peer.recv(len(expected) - len(reply)); assert data, reply
                                reply += data
                            assert reply == expected, reply
                            with (root / "replies.bin").open("ab") as log: log.write(reply)
                            check(client.wait_text(token), resolution, status, True, token, steps=(5, 7),
                                  zones=((0, 0, 639, 479), 0, 0x5a004d03, 0x003a3120), graph_error=-11,
                                  graph_point=point, colors=colors, graph_color=graph_color, colored_pixels=pixels,
                                  rgb_sample=(101, 61, 0xaaaa))
                        # A installs attributes and its indexed cache without
                        # repainting. The colour31 custom submission rejects,
                        # preserving the earlier built-in; built-ins accept256.
                        fill_cases = (
                            (b"I#A 3:", (7, 291, 31, 65535), 3, (3, 31),
                             (0xf0000000, 0x0f000000, 0x00f00000, 0x000f0000,
                              0x0000f000, 0x00000f00, 0x000000f0, 0x0000000f)),
                            (b"I#A 12:", (7, 291, 31, 65535), 12, (3, 31),
                             (0xf0000000, 0x0f000000, 0x00f00000, 0x000f0000,
                              0x0000f000, 0x00000f00, 0x000000f0, 0x0000000f)),
                            (b"I#C>2,5:A 12:", (7, 291, 5, 65535), 12, (12, 5),
                             (0x05050505, 0x50505050, 0x05050505, 0x50505050,
                              0x05050505, 0x50505050, 0x05050505, 0x50505050)),
                            (b"I#C>2,256:A 258:", (7, 291, 256, 65535), 1, (1, 256), (0,) * 8),
                        )
                        for index, (command, colors, selected, fill, tile) in enumerate(fill_cases):
                            token = f"FILL{index}"
                            wire = command + b"\x1b[H" + token.encode() + b"\x1b[6n"
                            with (root / "wire.bin").open("ab") as log: log.write(wire)
                            peer.sendall(wire)
                            expected = f"\x1b[1;{len(token) + 1}R".encode()
                            reply = b""
                            while len(reply) < len(expected):
                                data = peer.recv(len(expected) - len(reply)); assert data, reply
                                reply += data
                            assert reply == expected, reply
                            with (root / "replies.bin").open("ab") as log: log.write(reply)
                            check(client.wait_text(token), resolution, status, True, token, steps=(5, 7),
                                  zones=((0, 0, 639, 479), 0, 0x5a004d03, 0x003a3120), graph_error=-11,
                                  graph_point=(124, 60), colors=colors, graph_color=7, colored_pixels=pixels,
                                  rgb_sample=(101, 61, 0xaaaa), fill=fill, selected_fill=selected, fill_pixels=tile)
                        # Editing/restoring a retained slot does not install it;
                        # each following A explicitly resubmits the new rows.
                        edge = (5, 0, 0, 0, 0, 0, 0, 0x50000000)
                        checker = (0x05050505, 0x50505050, 0x05050505, 0x50505050,
                                   0x05050505, 0x50505050, 0x05050505, 0x50505050)
                        pattern_cases = (
                            (b"I#X 1,0,1,128,0,0,0,0,0,0,1:", (7, 291, 256, 65535), 1, (1, 256), (0,) * 8, (128, 0x01000000)),
                            (b"I#C>2,5:A 12:", (7, 291, 5, 65535), 12, (12, 5), edge, (128, 0x01000000)),
                            (b"I#X 1,12,0:", (7, 291, 5, 65535), 12, (12, 5), edge, (0x55aa55aa, 0x55aa55aa)),
                            (b"I#A 12:", (7, 291, 5, 65535), 12, (12, 5), checker, (0x55aa55aa, 0x55aa55aa)),
                        )
                        for index, (command, colors, selected, fill, tile, custom) in enumerate(pattern_cases):
                            token = f"PATTERN{index}"
                            wire = command + b"\x1b[H" + token.encode() + b"\x1b[6n"
                            with (root / "wire.bin").open("ab") as log: log.write(wire)
                            peer.sendall(wire)
                            expected = f"\x1b[1;{len(token) + 1}R".encode()
                            reply = b""
                            while len(reply) < len(expected):
                                data = peer.recv(len(expected) - len(reply)); assert data, reply
                                reply += data
                            assert reply == expected, reply
                            with (root / "replies.bin").open("ab") as log: log.write(reply)
                            check(client.wait_text(token), resolution, status, True, token, steps=(5, 7),
                                  zones=((0, 0, 639, 479), 0, 0x5a004d03, 0x003a3120), graph_error=-11,
                                  graph_point=(124, 60), colors=colors, graph_color=7, colored_pixels=pixels,
                                  rgb_sample=(101, 61, 0xaaaa), fill=fill, selected_fill=selected, fill_pixels=tile, custom0=custom)
                        # BIG23/59 authored bar/box pixels. These scenes retain
                        # the preceding console/RGB swatch and separate CPs.
                        region = {}
                        side = ((172, 69), (171, 70), (170, 71), (169, 71), (168, 72),
                                (172, 61), (172, 62), (172, 63), (172, 64), (172, 65),
                                (172, 66), (172, 67), (172, 68), (172, 69))
                        top = (tuple((x, 61) for x in range(164, 173))
                               + ((164, 61), (163, 62), (162, 63), (161, 63), (160, 64),
                                  (172, 61), (171, 62), (170, 63), (169, 63), (168, 64)))
                        bar_cases = (
                            (b"I#B 160,64,168,72,+1:", (0, 0), None, (12, 5), 12, checker),
                            (b"I#A>1:M>2:B 160,64,168,72,0:", (1, 1), None, (1, 5), 1, (0x55555555,) * 8),
                            (b"I#B 160,64,168,72,1:", (1, 1), ((168, 72), (168, 72)), (1, 5), 1, (0x55555555,) * 8),
                            (b"I#M>1:B 160,64,168,72,4,256:", (0, 0), ((172, 61), (168, 72)), (1, 5), 1, (0x55555555,) * 8),
                            (b"I#B 160,64,168,72,4,257:", (0, 0), ((172, 61), (168, 64)), (1, 5), 1, (0x55555555,) * 8),
                            (b"I#A>12:B 160,64,168,72,0:", (0, 0), ((172, 61), (168, 64)), (12, 5), 12, checker),
                        )
                        for index, (command, writing, lower, fill, selected, tile) in enumerate(bar_cases):
                            if index == 1:
                                region.update(((x, y), 5) for y in range(64, 73) for x in range(160, 169))
                            if index == 2:
                                for y in range(64, 73):
                                    for x in range(160, 169):
                                        if x in (160, 168) or y in (64, 72):
                                            region[x, y] = 5 if x in (160, 168) and y in (64, 72) else 6
                            if index == 3:
                                for y in range(64, 73):
                                    for x in range(160, 169):
                                        region[x, y] = 3 if x in (160, 168) or y in (64, 72) else 5
                                region.update((point, 3) for point in side)
                            if index == 4:
                                region.update((point, 3) for point in top)
                            if index == 5:
                                region.update(((x, y), 5 if (x + y) % 2 == 0 else 0)
                                              for y in range(64, 73) for x in range(160, 169))
                            bar_pixels = pixels + tuple((x, y, pen) for (x, y), pen in region.items())
                            token = f"BAR{index}"
                            wire = command + b"\x1b[H" + token.encode() + b"\x1b[6n"
                            with (root / "wire.bin").open("ab") as log: log.write(wire)
                            peer.sendall(wire)
                            expected = f"\x1b[1;{len(token) + 1}R".encode()
                            reply = b""
                            while len(reply) < len(expected):
                                data = peer.recv(len(expected) - len(reply)); assert data, reply
                                reply += data
                            assert reply == expected, reply
                            with (root / "replies.bin").open("ab") as log: log.write(reply)
                            check(client.wait_text(token), resolution, status, True, token, steps=(5, 7),
                                  zones=((0, 0, 639, 479), 0, 0x5a004d03, 0x003a3120), graph_error=-11,
                                  graph_point=(124, 60), lower_points=lower, writing=writing,
                                  colors=(7, 291, 5, 65535), graph_color=35, colored_pixels=bar_pixels,
                                  rgb_sample=(164, 68, 0xaa00aa) if 1 <= index <= 4 else (101, 61, 0xaaaa),
                                  fill=fill, selected_fill=selected, fill_pixels=tile)
                        # BIG24/59 authored boundary/interior results, retaining
                        # the complete preceding scene and independent CPs.
                        flood_cases = (
                            (b"I#C>1,15:C>2,3:A>1:B>200,64,208,72,1:C>0,271:C>2,5:C 1,291:", 3, (1, 3), 1, (0x33333333,) * 8),
                            (b"I#F 204,68,0:", 5, (1, 5), 1, (0x55555555,) * 8),
                            (b"I#A>12:F 204,68,0:", None, (12, 5), 12, checker),
                            (b"I#A>0:F 204,68,0:", 0, (0, 5), 0, (0,) * 8),
                            (b"I#X 4,+1,9,TAILI#F 204,68,0:", 0, (0, 5), 0, (0,) * 8),
                        )
                        for index, (command, interior, fill, selected, tile) in enumerate(flood_cases):
                            region = tuple((x, y, 15 if x in (200, 208) or y in (64, 72)
                                            else (5 if (x + y) % 2 == 0 else 0) if interior is None else interior)
                                           for y in range(64, 73) for x in range(200, 209))
                            token = f"FLOOD{index}"
                            wire = command + b"\x1b[H" + token.encode() + b"\x1b[6n"
                            with (root / "wire.bin").open("ab") as log: log.write(wire)
                            peer.sendall(wire)
                            expected = (b"\x13\x11" if index else b"") + f"\x1b[1;{len(token) + 1}R".encode()
                            reply = b""
                            while len(reply) < len(expected):
                                data = peer.recv(len(expected) - len(reply)); assert data, reply
                                reply += data
                            assert reply == expected, reply
                            with (root / "replies.bin").open("ab") as log: log.write(reply)
                            check(client.wait_text(token), resolution, status, True, token, steps=(5, 7),
                                  zones=((0, 0, 639, 479), 0, 0x5a004d03, 0x003a3120), graph_error=-11,
                                  graph_point=(124, 60), lower_points=((208, 72), (208, 72)),
                                  colors=(271, 291, 5, 65535), graph_color=15, colored_pixels=bar_pixels + region,
                                  rgb_sample=(204, 68, 0xaa00aa) if index == 1 else (101, 61, 0xaaaa),
                                  fill=fill, selected_fill=selected, fill_pixels=tile,
                                  rejections=(-12, 1) if index == 4 else (0, 0))
                        for index, (command, hollow) in enumerate(((b"I#H 256:", 256), (b"I#H +1:", 0))):
                            token = f"HOLLOW{index}"
                            wire = command + b"\x1b[H" + token.encode() + b"\x1b[6n"
                            with (root / "wire.bin").open("ab") as log: log.write(wire)
                            peer.sendall(wire)
                            expected = f"\x1b[1;{len(token) + 1}R".encode()
                            reply = b""
                            while len(reply) < len(expected):
                                data = peer.recv(len(expected) - len(reply)); assert data, reply
                                reply += data
                            assert reply == expected, reply
                            with (root / "replies.bin").open("ab") as log: log.write(reply)
                            check(client.wait_text(token), resolution, status, True, token, steps=(5, 7),
                                  zones=((0, 0, 639, 479), 0, 0x5a004d03, 0x003a3120), graph_error=-11,
                                  graph_point=(124, 60), lower_points=((208, 72), (208, 72)),
                                  colors=(271, 291, 5, 65535), graph_color=15, colored_pixels=bar_pixels + region,
                                  rgb_sample=(101, 61, 0xaaaa), fill=(0, 5), selected_fill=0, fill_pixels=(0,) * 8,
                                  rejections=(-12, 1), hollow=hollow)
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
