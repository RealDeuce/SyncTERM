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
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "testing"))
from syncterm_fixture import fixture, cell_at, click_text
from syncterm_control import CONTROL

PALETTE = (0, 0xaa, 0xaa00, 0xaaaa, 0xaa0000, 0xaa00aa, 0xaa5500, 0xaaaaaa,
           0x555555, 0x5555ff, 0x55ff55, 0x55ffff, 0xff5555, 0xff55ff, 0xffff55, 0xffffff)
EGA_CODES = (0, 1, 2, 3, 4, 5, 20, 7, 56, 57, 58, 59, 60, 61, 62, 63)
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


def check(frame, resolution, status, seeded, token, loop_state=None, scaling=0, steps=(12, 6), zones=None, marker=None, line=(0, 0, 1, 65535), graph_error=0, graph_point=None, marker_kind=None, line_pixels=(), writing=(0, 0), colors=(15, 15, 15, 15), graph_color=15, colored_pixels=(), fill=(1, 15), rgb_sample=None, selected_fill=1, fill_pixels=(0xffffffff,) * 8, custom0=(0x55aa55aa, 0x55aa55aa), lower_points=None, rejections=(0, 0), hollow=0, pages=(1, 0, 0, 0, 0, 0), seeded_pixels=None, backing=None, image=None, palette_codes=EGA_CODES, expected_palette=PALETTE, flow=(1, 17, 19, 1, 1), random_bounds=(0, 199), text_state=((1, 1, 1, 1), 1, (8, 8, 8)), font_state=(0, 0, 0, 255, 0), text_metrics=(0, 0), text_effective=None, text_header=(0, 0, 0, 0), text_device=(0, 0, 8, 8, 1, 1)):
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
    assert not s["cterm.big_rotation_active"]
    assert tuple(s[f"cterm.big_flow_{field}"] for field in ("mode", "on_byte", "off_byte", "on_count", "off_count")) == flow
    assert (s["cterm.big_random_from"], s["cterm.big_random_to"]) == random_bounds
    font, direction, loaded, stroke_id, stroke_direction = font_state
    assert tuple(s[f"cterm.big_font_{field}"] for field in ("registry_count", "registered_mask", "loaded", "active")) == (10, 15, loaded, int(loaded != 0))
    text_ratios, text_size, text_cache = text_state
    assert tuple(s[f"cterm.big_text_{field}"] for field in ("font", "direction", "size")) == (font, direction, text_size)
    assert tuple(s[f"cterm.big_text_{field}"] for field in ("cached_width", "cached_height", "selected_size")) == text_cache
    assert (s["cterm.big_text_scaled_width"], s["cterm.big_text_scaled_height"]) == text_metrics
    assert tuple(s[f"cterm.big_text_saved_ratio{i}"] for i in range(4)) == text_ratios
    assert tuple(s[f"cterm.big_text_effective_ratio{i}"] for i in range(4)) == (text_ratios if text_effective is None else text_effective)
    assert tuple(s[f"cterm.big_text_{field}"] for field in ("ddo_font", "ddo_direction", "stroke_active", "suppression", "body")) == (font, direction, 255 if font else 0, 65535 if font else 0, int(font != 0))
    assert (s["cterm.big_text_stroke_id"], s["cterm.big_text_stroke_direction"]) == (stroke_id, stroke_direction)
    assert tuple(s[f"cterm.big_text_{field}"] for field in ("total_width", "stroke_x", "stroke_y", "justify_x", "justify_y")) == (0, 0, 0, 0, 2)
    assert tuple(s[f"cterm.big_text_header_{field}"] for field in ("count", "first", "cap", "desc")) == text_header
    assert tuple(s[f"cterm.big_text_device_{field}"] for field in ("font", "direction", "width", "height", "mag_x", "mag_y")) == text_device
    assert (s["cterm.big_last_error"], s["cterm.big_rejected_commands"]) == rejections
    assert not s["cterm.big_reply_pending"]
    assert not s["cterm.big_loop_active"] and not s["cterm.big_setup_active"]
    assert s["cterm.big_scaling"] == scaling
    assert s["cterm.big_hollow"] == hollow
    assert tuple(s[f"cterm.big_{name}"] for name in ("memory_page", "active_page", "visual_page", "physical_active_page", "physical_visual_page", "display_start")) == pages
    if backing is not None:
        assert s["cterm.big_video_backing"] == backing
    if image is not None:
        assert tuple(s[f"cterm.big_image_{field}"] for field in ("width", "height", "mode", "size", "payload0", "payload1")) == image
    assert (s["cterm.big_step_x"], s["cterm.big_step_y"]) == steps
    assert not s["cterm.big_zone_active"]
    if zones is None:
        zones = ((0, 0, 0, 0), 0, 0, 0)
    coordinates, loopback, prefix, tail = zones
    assert tuple(s[f"cterm.big_zone0_{field}"] for field in ("x1", "y1", "x2", "y2")) == coordinates
    assert (s["cterm.big_zone_loopback"], s["cterm.big_zone0_prefix"], s["cterm.big_zone0_tail"]) == (loopback, prefix, tail)
    assert (s["cterm.big_line_style"], s["cterm.big_line_user"], s["cterm.big_line_width"], s["cterm.big_line_mask"]) == line
    assert tuple(s[f"cterm.big_ddo_line_{field}"] for field in ("style", "user", "width")) == line[:3]
    assert s["cterm.big_device_line_width"] == line[2]
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
    assert s["cterm.big_palette_size"] == 16 and s["cterm.big_bgi_background"] == 0
    assert tuple(s[f"cterm.big_palette{index}"] for index in range(16)) == palette_codes
    assert tuple(frame.palette[800:816]) == expected_palette
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
        if seeded if seeded_pixels is None else seeded_pixels:
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
    assert frame.rgb[oy * m["output_width"] + ox] & 0xffffff == expected_palette[0]
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
                        rectangle_pixels = bar_pixels + region
                        rectangle_cases = (
                            (b"I#H>256:C>1,15:C>2,3:A>1:Z 240,64,248,72:", 0, 256, (0, 0), (1, 3), 1, (0x33333333,) * 8),
                            (b"I#H>0:C>2,5:Z 240,64,248,72:", 5, 0, (0, 0), (1, 5), 1, (0x55555555,) * 8),
                            (b"I#A>12:Z 240,64,248,72:", None, 0, (0, 0), (12, 5), 12, checker),
                            (b"I#H>256:M>2:Z 240,64,248,72:", None, 256, (1, 1), (12, 5), 12, checker),
                            (b"I#H>0:M>1:A>0:Z 240,64,248,72:", 0, 0, (0, 0), (0, 5), 0, (0,) * 8),
                            (b"I#H>256:Z 248,64,240,72:", 0, 256, (0, 0), (0, 5), 0, (0,) * 8),
                        )
                        for index, (command, interior, hollow, writing, fill, selected, tile) in enumerate(rectangle_cases):
                            region = []
                            for y in range(64, 73):
                                for x in range(240, 249):
                                    edge = x in (240, 248) or y in (64, 72)
                                    corner = x in (240, 248) and y in (64, 72)
                                    pen = (0 if index == 3 and not corner else 15) if edge else ((5 if (x + y) % 2 == 0 else 0) if interior is None else interior)
                                    region.append((x, y, pen))
                            token = f"RECTANGLE{index}"
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
                                  colors=(271, 15, 3 if index == 0 else 5, 65535), graph_color=15,
                                  colored_pixels=rectangle_pixels + tuple(region), writing=writing,
                                  rgb_sample=(244, 68, 0xaa00aa) if index == 1 else (101, 61, 0xaaaa),
                                  fill=fill, selected_fill=selected, fill_pixels=tile, hollow=hollow, rejections=(-12, 1))
                        page0_pixels = rectangle_pixels + tuple(region)
                        magenta = tuple((x, y, 5) for y in range(64, 73) for x in range(280, 289))
                        cyan = tuple((x, y, 3) for y in range(64, 73) for x in range(300, 309))
                        selected = 0 if resolution == 3 else 1
                        local = 0 if resolution == 3 else 1
                        display = 0 if resolution == 3 else 0x4000 if resolution == 1 else 0x8000
                        memory3 = 3 if resolution == 1 else local
                        page0_drawn = page0_pixels + (magenta if resolution == 3 else ())
                        page1_drawn = page0_drawn if resolution == 3 else magenta
                        page_cases = (
                            (b"I#G 4,3:", (memory3, 0, 0, 0, 0, 0), page0_pixels, True, (0, 5), (0,) * 8, 5, (101, 61, 0xaaaa), b""),
                            (b"I#G>6,1:C>2,5:A>1:B 280,64,288,72,0:", (local, selected, 0, selected, 0, 0), page0_drawn, True, (1, 5), (0x55555555,) * 8, 5, (101, 61, 0xaaaa), b""),
                            (b"I#G 5,1:", (local, 0, selected, selected, selected, display), page1_drawn, resolution == 3, (1, 5), (0x55555555,) * 8, 5, (284, 68, 0xaa00aa), b""),
                            (b"I#G>6,0:C>2,3:B 300,64,308,72,0:", (local, 0, 0, 0, selected, display), page1_drawn + (cyan if resolution == 3 else ()), resolution == 3, (1, 3), (0x33333333,) * 8, 3, (284, 68, 0xaa00aa), b""),
                            (b"I#G 5,0:", (local, 0, 0, 0, 0, 0), page0_drawn + cyan, True, (1, 3), (0x33333333,) * 8, 3, (304, 68, 0xaaaa), b""),
                            (b"I#G>6,1:C>2,5:B>280,64,288,72,0:G 5,1:", (local, selected, selected, selected, selected, display), page1_drawn + (cyan if resolution == 3 else ()), resolution == 3, (1, 5), (0x55555555,) * 8, 5, (284, 68, 0xaa00aa), b""),
                            (b"I#& 0,2,1,0,G@2,4,x:", (2 if resolution == 1 else local, 0, 0, selected, selected, display), page1_drawn + (cyan if resolution == 3 else ()), resolution == 3, (1, 5), (0x55555555,) * 8, 5, (284, 68, 0xaa00aa), b"\x13\x11"),
                            (b"I#G 5,0:", (local, 0, 0, selected, 0, 0), page0_drawn + cyan, True, (1, 5), (0x55555555,) * 8, 5, (304, 68, 0xaaaa), b""),
                            (f"I#R {resolution}:".encode(), (1, 0, 0, 0, 0, 0), (), False, (1, 15), (0xffffffff,) * 8, 5, None, b""),
                            (b"I#G 5,1:", (local, 0, selected, 0, selected, display), (), False, (1, 15), (0xffffffff,) * 8, 5, None, b""),
                        )
                        for index, (command, pages, pixels, raster_seeded, fill, tile, fill_cache, rgb, flow) in enumerate(page_cases):
                            token = f"PAGE{index}"
                            wire = command + b"\x1b[H" + token.encode() + b"\x1b[6n"
                            with (root / "wire.bin").open("ab") as log: log.write(wire)
                            peer.sendall(wire)
                            expected = flow + f"\x1b[1;{len(token) + 1}R".encode()
                            reply = b""
                            while len(reply) < len(expected):
                                data = peer.recv(len(expected) - len(reply)); assert data, reply
                                reply += data
                            assert reply == expected, reply
                            with (root / "replies.bin").open("ab") as log: log.write(reply)
                            reset = index >= 8
                            check(client.wait_text(token), resolution, status, not reset, token, steps=(5, 7),
                                  zones=((0, 0, 639, 479), 0, 0x5a004d03, 0x003a3120), graph_error=-11,
                                  graph_point=(0, 0) if reset else (124, 60), lower_points=((0, 0), (0, 0)) if reset else ((208, 72), (208, 72)),
                                  colors=(271, 15, fill_cache, 65535), graph_color=15, colored_pixels=pixels,
                                  rgb_sample=rgb, fill=fill, selected_fill=0 if index == 0 else 1, fill_pixels=tile,
                                  hollow=256, rejections=(-12, 1), pages=pages, seeded_pixels=raster_seeded,
                                  backing=524288 if status or (index > 0 and resolution != 3) else 0)
                        # Original image routes retain one packed/indexed
                        # buffer across R/I, independently of physical pages.
                        def patch(x0, y0, pen):
                            return tuple((x, y, pen) for y in range(y0, y0 + 3) for x in range(x0, x0 + 9))

                        source = patch(320, 64, 5)
                        copied = patch(340, 64, 5)
                        ored = patch(360, 64, 5)
                        anded = patch(380, 64, 4)
                        inverted = patch(400, 64, 10)
                        page0_image = source + ored + anded + inverted
                        page1_image = patch(328, 66, 5)
                        staged = ((420, 64, 5),) if resolution == 3 else patch(420, 64, 5)
                        retained = ((440, 64, 5),) if resolution == 3 else patch(440, 64, 5)
                        repeated = ((460, 64, 5),) if resolution == 3 else patch(460, 64, 5)
                        solid = 0x80ff0000
                        staging = 0x00800000 if resolution == 3 else solid
                        image_cases = (
                            (f"I#R {resolution}:".encode() + b"I#C>2,5:A>1:B>320,64,328,66,0:G 0,0,320,64,328,66,340,64:",
                             source + copied, 0, 5, (1, 5), 5, solid, (344, 65, 0xaa00aa)),
                            (b"I#G 2,1,340,64:", source, 1, 5, (1, 5), 5, solid, (344, 65, 0)),
                            (b"I#G 2,2,360,64:", source + ored, 2, 5, (1, 5), 5, solid, (364, 65, 0xaa00aa)),
                            (b"I#C>2,6:B>380,64,388,66,0:G 2,3,380,64:", source + ored + anded, 3, 6, (1, 6), 6, solid, (384, 65, 0xaa0000)),
                            (b"I#G 2,4,400,64:", page0_image, 4, 6, (1, 6), 6, solid, (404, 65, 0x55ff55)),
                            (b"I#G>1,0,328,66,320,64:G 5,1:", page0_image if resolution == 3 else page1_image,
                             0, 6, (1, 6), 6, solid,
                             (324, 65, 0xaa00aa) if resolution == 3 else (332, 67, 0xaa00aa)),
                            (b"I#G 5,0:", page0_image, 0, 6, (1, 6), 6, solid, (404, 65, 0x55ff55)),
                            (b"I#G 3,0,328,66,336,68,420,64:", page0_image + staged, 0, 6, (1, 6), 6, staging,
                             (324, 65, 0xaa00aa) if resolution == 3 else (424, 65, 0xaa00aa)),
                            (f"I#R {resolution}:I#G 2,0,440,64:".encode(), retained, 0, 6, (1, 15), 15, staging,
                             (500, 65, 0) if resolution == 3 else (444, 65, 0xaa00aa)),
                            (b"I#I 1:I#G 2,0,460,64:", retained + repeated, 0, 6, (1, 15), 15, staging,
                             (500, 65, 0) if resolution == 3 else (464, 65, 0xaa00aa)),
                            # A real capture-bound failure stays in this
                            # connection; following G2/ANSI/DSR still execute.
                            (b"I#G 0,0,0,0,639,349,0,0:I#G 2,0,60,60:", retained + repeated,
                             0, 6, (1, 15), 15, 0, (500, 65, 0)),
                        )
                        for index, (command, pixels, mode, fill_cache, fill, tile_pen, payload, rgb) in enumerate(image_cases):
                            token = f"IMAGE{index}"
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
                            visual = selected if index == 5 else 0
                            pages = (local, 0, visual, 0, visual, display if index == 5 else 0)
                            check(client.wait_text(token), resolution, status, False, token, steps=(5, 7),
                                  zones=((0, 0, 639, 479), 0, 0x5a004d03, 0x003a3120), graph_error=-11,
                                  graph_point=(0, 0), lower_points=((0, 0), (0, 0)),
                                  colors=(271, 15, fill_cache, 65535), graph_color=15,
                                  colored_pixels=pixels, rgb_sample=rgb, fill=fill, selected_fill=1,
                                  fill_pixels=(int(f"{tile_pen:x}" * 8, 16),) * 8, hollow=0 if index >= 9 else 256,
                                  rejections=(-12, 2 if index == 10 else 1), pages=pages, backing=524288,
                                  image=(639, 349, mode, 0, payload, payload) if index == 10 else (8, 2, mode, 30, payload, payload))
                        # Cache bytes and physical six-bit colours are
                        # separate; image pens remain live under recolouring.
                        def swatch(x0):
                            return tuple((x, y, 5) for y in range(64, 73) for x in range(x0, x0 + 9))

                        two_swatches = swatch(320) + swatch(340)
                        three_swatches = two_swatches + swatch(360)
                        four_swatches = three_swatches + swatch(380)
                        palette_cases = (
                            (f"I#R {resolution}:".encode() + b"I#C>2,5:A>1:B>320,64,328,72,0:G 0,0,320,64,328,72,340,64:",
                             two_swatches, None, None, (344, 68, 0xaa00aa)),
                            (b"I#S 5,128:", two_swatches, (5, 128), (5, 0), (344, 68, 0)),
                            (b"I#S>5,63:G 2,0,360,64:", three_swatches, (5, 63), (5, 0xffffff), (364, 68, 0xffffff)),
                            (b"I#S 5,262:", three_swatches, (5, 6), (5, 0xaaaa00), (344, 68, 0xaaaa00)),
                            (b"I#S>5,+1:S 16,63:", three_swatches, None, None, (344, 68, 0xaaaa00)),
                            (b"I#S>0,6:G 0,0,320,64,328,72,380,64:", four_swatches, (0, 6), (0, 0xaaaa00), (384, 68, 0xaaaa00)),
                            (b"I#I 1:", four_swatches, None, None, (384, 68, 0xaa00aa)),
                        )
                        codes, physical = list(EGA_CODES), list(PALETTE)
                        for index, (command, pixels, cached, mapped, rgb) in enumerate(palette_cases):
                            if index == 6:
                                codes, physical = list(EGA_CODES), list(PALETTE)
                            if cached is not None:
                                codes[cached[0]] = cached[1]
                            if mapped is not None:
                                physical[mapped[0]] = mapped[1]
                            token = f"PALETTE{index}"
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
                            pages = (local if index in (0, 2, 5) else 1, 0, 0, 0, 0, 0)
                            check(client.wait_text(token), resolution, status, False, token, steps=(5, 7),
                                  zones=((0, 0, 639, 479), 0, 0x5a004d03, 0x003a3120), graph_error=-11,
                                  graph_point=(0, 0), lower_points=((0, 0), (0, 0)),
                                  colors=(271, 15, 5, 65535), graph_color=15, colored_pixels=pixels,
                                  rgb_sample=rgb, fill=(1, 5), selected_fill=1, fill_pixels=(0x55555555,) * 8,
                                  hollow=0, rejections=(-12, 2), pages=pages, backing=524288,
                                  image=(8, 8, 0, 78, solid, solid), palette_codes=tuple(codes), expected_palette=tuple(physical))
                        # Literal caller traces from26/55: extra endpoint
                        # setters, stale final snapshots and signed saves.
                        captured = list(EGA_CODES)
                        captured[2:5] = (7, 10, 20)
                        captured[8] = 128
                        saved = captured.copy()
                        saved[8] = 0xff80
                        rotated = captured.copy()
                        rotated[3:5] = (10, 7)
                        restored = captured.copy()
                        restored[8] = 5
                        ascending = restored.copy()
                        ascending[3:5] = (5, 20)
                        lower = ascending.copy()
                        lower[0:2] = (0, 16)
                        upper = lower.copy()
                        upper[14:16] = (0, 63)
                        settled = upper.copy()
                        settled[3:5] = (7, 7)
                        local_down = rotated.copy()
                        local_down[3] = 7
                        local_up = ascending.copy()
                        local_up[4] = 5
                        local_low = lower.copy()
                        local_low[0] = 16
                        local_high = upper.copy()
                        local_high[15] = 0
                        rotations = (
                            (b"I#X 8,0,15,0,+1:", [0] * 16, list(EGA_CODES), [0] * 16, False),
                            (b"I#I 1:I#S>2,7:S>3,10:S>4,20:S>8,128:X 8,1,1,1,1:", captured, captured, saved, False),
                            (b"I#X 8,3,4,1,0:", rotated, local_down, saved, True),
                            (b"I#S>8,5:X 8,0,0,0,0:", restored, rotated, saved, False),
                            (b"I#X 8,4,3,1,0:", ascending, local_up, saved, True),
                            (b"I#X 8,0,1,1,0:", lower, local_low, saved, True),
                            (b"I#X 8,15,14,1,0:", upper, local_high, saved, True),
                            (b"I#X 8,3,4,9999,1:", settled, settled, saved, True),
                            (b"I#I 1:", list(EGA_CODES), settled, saved, False),
                        )

                        def ega_rgb(code):
                            return (85 * (2 * ((code >> 2) & 1) + ((code >> 5) & 1)) << 16
                                    | 85 * (2 * ((code >> 1) & 1) + ((code >> 4) & 1)) << 8
                                    | 85 * (2 * (code & 1) + ((code >> 3) & 1)))

                        for index, (command, codes, local_codes, saved_codes, flow) in enumerate(rotations):
                            token = f"ROTATE{index}"
                            wire = command + b"\x1b[H" + token.encode() + b"\x1b[6n"
                            with (root / "wire.bin").open("ab") as log: log.write(wire)
                            peer.sendall(wire)
                            if index == 7:
                                client.wait_for(lambda f: f.application.get("cterm.big_rotation_active")
                                                and f.application.get("cterm.big_rotation_counter", 0) >= 3
                                                and f.application.get("cterm.big_palette3") == 7
                                                and f.application.get("cterm.big_palette4") == 7)
                                client.key(ord("A"))
                            expected = (b"\x13\x11" if flow else b"") + f"\x1b[1;{len(token) + 1}R".encode()
                            reply = b""
                            while len(reply) < len(expected):
                                data = peer.recv(len(expected) - len(reply)); assert data, reply
                                reply += data
                            assert reply == expected, reply
                            with (root / "replies.bin").open("ab") as log: log.write(reply)
                            frame = client.wait_text(token)
                            physical = tuple(ega_rgb(code) for code in codes)
                            check(frame, resolution, status, False, token, steps=(5, 7),
                                  zones=((0, 0, 639, 479), 0, 0x5a004d03, 0x003a3120), graph_error=-11,
                                  graph_point=(0, 0), lower_points=((0, 0), (0, 0)),
                                  colors=(271, 15, 5, 65535), graph_color=15, colored_pixels=four_swatches,
                                  rgb_sample=(384, 68, physical[5]), fill=(1, 5), selected_fill=1,
                                  fill_pixels=(0x55555555,) * 8, hollow=0, rejections=(-12, 2),
                                  pages=(1, 0, 0, 0, 0, 0), backing=524288,
                                  image=(8, 8, 0, 78, solid, solid), palette_codes=tuple(codes), expected_palette=physical)
                            assert frame.application["cterm.big_local_palette_size"] == 16
                            assert frame.application["cterm.big_palette_padding"] == 0
                            assert tuple(frame.application[f"cterm.big_local_palette{i}"] for i in range(16)) == tuple(local_codes)
                            assert tuple(frame.application[f"cterm.big_saved_palette{i}"] for i in range(16)) == tuple(saved_codes)
                        # X5 changes settings without transmitting them.
                        # Negative X8 count exercises flow without drawing.
                        flows = (
                            (b"I#X>5,0:X 8,3,4,+1,0:", (0, 17, 19, 1, 1), b""),
                            (b"I#X 5,2,256,2:", (2, 0, 19, 2, 1), b""),
                            (b"I#X 5,3,511,2:", (3, 0, 255, 2, 2), b""),
                            (b"I#X 8,3,4,+1,0:", (3, 0, 255, 2, 2), b"\xff\xff\0\0"),
                            (b"I#X 5,2,300,+1:", (2, 44, 255, 65535, 2), b""),
                            (b"I#X 8,3,4,+1,0:", (2, 44, 255, 65535, 2), b"\xff\xff"),
                            (b"I#X 5,+1:", (65535, 44, 255, 65535, 2), b""),
                            (b"I#X 5,4:", (4, 17, 19, 1, 1), b""),
                            (b"I#X 5,1:", (1, 17, 19, 1, 1), b""),
                        )
                        for index, (command, flow, prefix) in enumerate(flows):
                            token = f"FLOW{index}"
                            wire = command + b"\x1b[H" + token.encode() + b"\x1b[6n"
                            with (root / "wire.bin").open("ab") as log: log.write(wire)
                            peer.sendall(wire)
                            expected = prefix + f"\x1b[1;{len(token) + 1}R".encode()
                            reply = b""
                            while len(reply) < len(expected):
                                data = peer.recv(len(expected) - len(reply)); assert data, reply
                                reply += data
                            assert reply == expected, reply
                            with (root / "replies.bin").open("ab") as log: log.write(reply)
                            check(client.wait_text(token), resolution, status, False, token, steps=(5, 7),
                                  zones=((0, 0, 639, 479), 0, 0x5a004d03, 0x003a3120), graph_error=-11,
                                  graph_point=(0, 0), lower_points=((0, 0), (0, 0)),
                                  colors=(271, 15, 5, 65535), graph_color=15, colored_pixels=four_swatches,
                                  rgb_sample=(384, 68, 0xaa00aa), fill=(1, 5), selected_fill=1,
                                  fill_pixels=(0x55555555,) * 8, hollow=0, rejections=(-12, 2),
                                  pages=(1, 0, 0, 0, 0, 0), backing=524288,
                                  image=(8, 8, 0, 78, solid, solid), flow=flow)
                        ranges = (
                            (b"I#X 2,10,4:", (10, 11), True, 5),
                            (b"I#X 2,10,10:", (10, 10), True, 5),
                            (b"I#X 2,+1,+1:", (0, 1), True, 5),
                            (b"I#X 2,0,0:", (0, 0), True, 5),
                            (b"I#S 5,r:", (0, 0), False, 0),
                            (b"I#X 2,200,199:", (200, 201), True, 0),
                            (b"I#I 1:", (200, 201), False, 5),
                            (b"I#X 2,0,199:", (0, 199), True, 5),
                            (b"I#S 5,5:", (0, 199), False, 5),
                        )
                        last_random = None
                        for index, (command, bounds, reseed, color5) in enumerate(ranges):
                            token = f"RANDOM{index}"
                            wire = command + b"\x1b[H" + token.encode() + b"\x1b[6n"
                            with (root / "wire.bin").open("ab") as log: log.write(wire)
                            before_seconds = int(time.time())
                            peer.sendall(wire)
                            expected = f"\x1b[1;{len(token) + 1}R".encode()
                            reply = b""
                            while len(reply) < len(expected):
                                data = peer.recv(len(expected) - len(reply)); assert data, reply
                                reply += data
                            assert reply == expected, reply
                            with (root / "replies.bin").open("ab") as log: log.write(reply)
                            frame = client.wait_text(token)
                            after_seconds = int(time.time())
                            codes, physical = list(EGA_CODES), list(PALETTE)
                            codes[5], physical[5] = color5, PALETTE[color5]
                            check(frame, resolution, status, False, token, steps=(5, 7),
                                  zones=((0, 0, 639, 479), 0, 0x5a004d03, 0x003a3120), graph_error=-11,
                                  graph_point=(0, 0), lower_points=((0, 0), (0, 0)),
                                  colors=(271, 15, 5, 65535), graph_color=15, colored_pixels=four_swatches,
                                  rgb_sample=(384, 68, physical[5]), fill=(1, 5), selected_fill=1,
                                  fill_pixels=(0x55555555,) * 8, hollow=0, rejections=(-12, 2),
                                  pages=(1, 0, 0, 0, 0, 0), backing=524288,
                                  image=(8, 8, 0, 78, solid, solid), palette_codes=tuple(codes),
                                  expected_palette=tuple(physical), random_bounds=bounds)
                            observed = frame.application["cterm.big_random_state"]
                            if reseed:
                                assert observed in {seconds & 65535 for seconds in range(before_seconds, after_seconds + 1)}
                            else:
                                expected_state = ((last_random * 0x015a4e35 + 1) & 0xffffffff) if index == 4 else last_random
                                assert observed == expected_state, (observed, expected_state)
                            last_random = observed
                        def spray_shape(kind, x0, y0):
                            return tuple((x0 - 4 + x, y0 - 4 + y, 15)
                                         for y, row in enumerate(MARKERS[kind])
                                         for x in range(9) if row & (1 << x))

                        spray_dot = ((450, 100, 15),)
                        spray_cross = spray_shape(2, 480, 100)
                        spray_square = spray_shape(4, 510, 100)
                        sprays = (
                            (b"I#X 0,450,100,0,0,0:", 0, (0, 0), 0, 0, (450, 100), ()),
                            (b"I#X 0,450,100,0,0,+1:", 0, (0, 0), 0, 65535, (450, 100), ()),
                            (b"I#X 0,450,100,0,0,4:", 0, (0, 0), 0, 4, (450, 100), spray_dot),
                            (b"I#T>0,2,1:X 0,480,100,0,0,1:", 2, (484, 100), 0, 1, (480, 100), spray_dot + spray_cross),
                            (b"I#T>0,4,1:X 0,510,100,0,0,1:", 4, (510, 100), 0, 1, (510, 100), spray_dot + spray_cross + spray_square),
                            (b"I#M>2:T>0,2,1:X 0,480,100,0,0,1:", 2, (484, 100), 1, 1, (480, 100), spray_dot + ((480, 100, 15),) + spray_square),
                            (b"I#T>0,0,1:X 0,450,100,0,0,2:", 0, (484, 100), 1, 2, (450, 100), spray_dot + ((480, 100, 15),) + spray_square),
                            (b"I#M>0:X 0,530,100,0,0,0:", 0, (484, 100), 0, 0, (530, 100), spray_dot + ((480, 100, 15),) + spray_square),
                            (b"I#X 0,+1,+1,0,0,3:", 0, (484, 100), 0, 3, (65535, 65535), spray_dot + ((480, 100, 15),) + spray_square),
                        )
                        for index, (command, kind, point, writing, count, origin, pixels) in enumerate(sprays):
                            token = f"SPRAY{index}"
                            wire = command + b"\x1b[H" + token.encode() + b"\x1b[6n"
                            with (root / "wire.bin").open("ab") as log: log.write(wire)
                            before_seconds = int(time.time())
                            peer.sendall(wire)
                            expected = f"\x1b[1;{len(token) + 1}R".encode()
                            reply = b""
                            while len(reply) < len(expected):
                                data = peer.recv(len(expected) - len(reply)); assert data, reply
                                reply += data
                            assert reply == expected, reply
                            with (root / "replies.bin").open("ab") as log: log.write(reply)
                            frame = client.wait_text(token)
                            after_seconds = int(time.time())
                            check(frame, resolution, status, False, token, steps=(5, 7),
                                  zones=((0, 0, 639, 479), 0, 0x5a004d03, 0x003a3120), graph_error=-11,
                                  graph_point=point, marker_kind=kind, writing=(writing, writing),
                                  colors=(271, 15, 5, 65535), graph_color=15, colored_pixels=four_swatches + pixels,
                                  rgb_sample=(384, 68, 0xaa00aa), fill=(1, 5), selected_fill=1,
                                  fill_pixels=(0x55555555,) * 8, hollow=0, rejections=(-12, 2),
                                  pages=(1, 0, 0, 0, 0, 0), backing=524288,
                                  image=(8, 8, 0, 78, solid, solid))
                            attempts = count if count < 32768 else 0
                            assert tuple(frame.application[f"cterm.big_spray_{field}"] for field in
                                         ("x", "y", "width", "height", "count", "counter")) == (*origin, 0, 0, count, attempts)
                            states = set()
                            for seconds in range(before_seconds, after_seconds + 1):
                                state = seconds & 65535
                                for _ in range(2 * attempts): state = (state * 0x015a4e35 + 1) & 0xffffffff
                                states.add(state)
                            assert frame.application["cterm.big_random_state"] in states
                        ratio_cases = (
                            (b"I#X 3,2,1,3,2:", (2, 1, 3, 2), b""),
                            (b"I#X 3,0,5,+1,4:", (1, 1, 1, 4), b""),
                            (b"I#X 3,4,3,2,1:", (4, 3, 2, 1), b""),
                            (b"I#I 1:", (4, 3, 2, 1), b""),
                            (b"I#X>3,2,4,4,2:S 5,5:", (2, 4, 4, 2), b""),
                            (b"I#X 3,258,1,260,2:", (1, 1, 1, 2), b""),
                            (b"I#X 3,1,2,3,4:", (1, 2, 3, 4), b""),
                            (b"I#& 0,1,1,0,X@5,3,4,3,2,1:", (4, 3, 2, 1), b"\x13\x11"),
                            (b"I#X 3,1,1,1,1:", (1, 1, 1, 1), b""),
                        )
                        retained_random = frame.application["cterm.big_random_state"]
                        retained_pixels = four_swatches + spray_dot + ((480, 100, 15),) + spray_square
                        for index, (command, ratios, prefix) in enumerate(ratio_cases):
                            token = f"RATIO{index}"
                            wire = command + b"\x1b[H" + token.encode() + b"\x1b[6n"
                            with (root / "wire.bin").open("ab") as log: log.write(wire)
                            peer.sendall(wire)
                            expected = prefix + f"\x1b[1;{len(token) + 1}R".encode()
                            reply = b""
                            while len(reply) < len(expected):
                                data = peer.recv(len(expected) - len(reply)); assert data, reply
                                reply += data
                            assert reply == expected, reply
                            with (root / "replies.bin").open("ab") as log: log.write(reply)
                            frame = client.wait_text(token)
                            check(frame, resolution, status, False, token, steps=(5, 7),
                                  zones=((0, 0, 639, 479), 0, 0x5a004d03, 0x003a3120), graph_error=-11,
                                  graph_point=(484, 100), marker_kind=0,
                                  colors=(271, 15, 5, 65535), graph_color=15, colored_pixels=retained_pixels,
                                  rgb_sample=(384, 68, 0xaa00aa), fill=(1, 5), selected_fill=1,
                                  fill_pixels=(0x55555555,) * 8, hollow=0, rejections=(-12, 2),
                                  pages=(1, 0, 0, 0, 0, 0), backing=524288,
                                  image=(8, 8, 0, 78, solid, solid),
                                  text_state=(ratios, 0, (ratios[1], ratios[2], 0)))
                            assert frame.application["cterm.big_random_state"] == retained_random
                        # Literal E/DDO metric/cache expectations from28/30/53.
                        # Each row retains the complete preceding spray scene.
                        custom = (2, 1, 3, 2)
                        bitmap16 = (0, 1, 16, 16, 2, 2)
                        bitmap8 = (0, 1, 8, 8, 1, 1)
                        trip = (95, 32, 21, 249)
                        small = (96, 32, 6, 254)
                        sans = (126, 1, 21, 249)
                        gothic = (106, 21, 21, 249)
                        style_cases = (
                            (b"I#E 0,2,1:", (0, 1, 0, 255, 0), 2, (16, 16, 8), (0, 0), (1, 1, 1, 1), (1, 1, 1, 1), (0, 0, 0, 0), bitmap16, b""),
                            (b"I#E 1,1,0:", (1, 0, 1, 1, 0), 1, (16, 16, 1), (16, 16), (1, 1, 1, 1), (3, 5, 3, 5), trip, bitmap16, b""),
                            (b"I#E 2,2,1:", (2, 1, 2, 2, 1), 2, (5, 5, 2), (5, 5), (1, 1, 1, 1), (2, 3, 2, 3), small, bitmap16, b""),
                            (b"I#E 3,3,0:", (3, 0, 3, 3, 0), 3, (21, 21, 3), (21, 21), (1, 1, 1, 1), (3, 4, 3, 4), sans, bitmap16, b""),
                            (b"I#E 4,4,1:", (4, 1, 4, 4, 1), 4, (28, 28, 4), (28, 28), (1, 1, 1, 1), (1, 1, 1, 1), gothic, bitmap16, b""),
                            (b"I#X 3,2,1,3,2:", (4, 1, 4, 4, 1), 0, (1, 3, 0), (56, 42), custom, custom, gothic, bitmap16, b""),
                            (b"I#E 1,0,1:", (1, 1, 1, 1, 1), 0, (56, 42, 0), (56, 42), custom, custom, trip, bitmap16, b""),
                            (b"I#E 0,0,1:", (0, 1, 1, 1, 1), 0, (8, 8, 8), (56, 42), custom, custom, trip, bitmap8, b""),
                            (b"I#I 1:", (0, 1, 1, 1, 1), 0, (8, 8, 8), (56, 42), custom, custom, trip, bitmap8, b""),
                            (b"I#E 5,+1,2:", (0, 0, 1, 1, 1), 1, (8, 8, 8), (56, 42), custom, custom, trip, (0, 0, 8, 8, 1, 1), b""),
                            (b"I#E>2,0,0:S 5,5:", (2, 0, 2, 2, 0), 0, (16, 12, 0), (16, 12), custom, custom, small, (0, 0, 8, 8, 1, 1), b""),
                            (b"I#& 0,1,1,0,E@3,3,4,1:", (3, 1, 3, 3, 1), 4, (28, 28, 4), (28, 28), custom, (1, 1, 1, 1), sans, (0, 0, 8, 8, 1, 1), b"\x13\x11"),
                            (b"I#E 0,3,0:", (0, 0, 3, 3, 1), 3, (24, 24, 8), (28, 28), custom, (1, 1, 1, 1), sans, (0, 0, 24, 24, 3, 3), b""),
                        )
                        for index, (command, font_state, size, cache, metrics, saved, effective, header, device, prefix) in enumerate(style_cases):
                            token = f"STYLE{index}"
                            wire = command + b"\x1b[H" + token.encode() + b"\x1b[6n"
                            with (root / "wire.bin").open("ab") as log: log.write(wire)
                            peer.sendall(wire)
                            expected = prefix + f"\x1b[1;{len(token) + 1}R".encode()
                            reply = b""
                            while len(reply) < len(expected):
                                data = peer.recv(len(expected) - len(reply)); assert data, reply
                                reply += data
                            assert reply == expected, reply
                            with (root / "replies.bin").open("ab") as log: log.write(reply)
                            frame = client.wait_text(token)
                            check(frame, resolution, status, False, token, steps=(5, 7),
                                  zones=((0, 0, 639, 479), 0, 0x5a004d03, 0x003a3120), graph_error=-11,
                                  graph_point=(484, 100), marker_kind=0,
                                  colors=(271, 15, 5, 65535), graph_color=15, colored_pixels=retained_pixels,
                                  rgb_sample=(384, 68, 0xaa00aa), fill=(1, 5), selected_fill=1,
                                  fill_pixels=(0x55555555,) * 8, hollow=0, rejections=(-12, 2),
                                  pages=(1, 0, 0, 0, 0, 0), backing=524288,
                                  image=(8, 8, 0, 78, solid, solid), text_state=(saved, size, cache),
                                  font_state=font_state, text_metrics=metrics, text_effective=effective,
                                  text_header=header, text_device=device)
                            assert frame.application["cterm.big_random_state"] == retained_random
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
