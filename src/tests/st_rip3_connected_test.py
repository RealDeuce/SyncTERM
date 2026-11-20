#!/usr/bin/env python3
"""RIP3 acceptance through a real TCP session and the control bitmap backend.

Expected coordinates and advances are specified independently: Dixon F has
610/1000 em advance; 22 points rounds to 268 logical units before device DPI
truncation. Pixel checks assert ink, clipping and colours, not RIPtel raster
identity. Each checkpoint fences the wire before taking one coherent capture.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path
import time

from st_migrated_control_test import connection
from st_terminal_conformance_test import Terminal
from syncterm_fixture import receive


def query(source, *, at_cursor=False):
    # SOH permits a command without first moving the ordinary TTY cursor.
    return (b"\x01|1\x1b0000" if at_cursor else b"\r!|1\x1b0000") + source + b"\r\n"


def pixel(frame, x, y):
    m = frame.metadata
    word = frame.primary[(y + m["pixel_top"]) * m["plane_width"] + x]
    return (word if word & 0x80000000 else frame.palette[word]) & 0xffffff


def run(args, width, height, advance):
    script = r'''
import "syncterm" for CTerm, Conn, Hook, Key
Hook.onKey(Key.f12) {|event|
  Conn.sendText("WINDOW:%(CTerm.width):%(CTerm.height):%(CTerm.x):%(CTerm.y)")
  return true
}
Hook.onKey(Key.f11) {|event|
  CTerm.write("\u001b[1;1H\u001b[97mW")
  Conn.sendText("LOCAL:%(CTerm.x):%(CTerm.y)")
  return true
}
Hook.onKey(Key.f10) {|event|
  CTerm.reset()
  Conn.sendText("RESET:%(CTerm.width):%(CTerm.height):%(CTerm.x):%(CTerm.y)")
  return true
}
Hook.onKey(Key.f9) {|event|
  Hook.onInput(65) {|scalar|
    Conn.sendText("INPUT:%(CTerm.x):%(CTerm.y)")
    return false
  }
  Conn.sendText("WATCHING")
  return true
}
'''
    with connection(args.executable, args.reports, encoding=args.encoding,
                    options="ScreenMode=Custom\nRIP=RIPv3\n",
                    settings=f"CustomColumns={width // 8}\nCustomRows={height // 8}\n"
                             "CustomFontHeight=8\nCustomAspectWidth=4\nCustomAspectHeight=3\n"
                             "AudioModes=0\n", script=script) as (client, root, peer, send):
        t = Terminal(client, root, peer, send)
        for probe in (b"\x1b[!", b"\x1b[0!"):
            send(probe)
            receive(peer, b"RIPSCRIP030001", root / "replies.bin")
        t.case("rip3-text-cursor-on-entry-grid")
        t.exchange(b"\x01|g0509\r\n\x1b[6n", b"\x1b[10;6R")
        t.exchange(b"\x01|H\r\n\x1b[6n", b"\x1b[1;1R")
        t.case("rip3-default-text-activation-variables-and-cursor")
        before = t.exchange(b"\x1b[?25h")
        send(query(b"$DTW$") + b"HIDDEN\r\n" * (height // 8 + 10)
             + b"\x1b[2J\x1b[?25l" + query(b"root-off"))
        receive(peer, b"root-off", root / "replies.bin")
        hidden = client.capture()
        assert hidden.primary == before.primary and hidden.metadata["cursor_visible"] == 0
        frame = t.exchange(query(b"$ATW(CUR)$root-on"), b"root-on")
        assert frame.primary == before.primary
        assert frame.metadata["cursor_visible"] == before.metadata["cursor_visible"] == 1
        before = t.exchange(b"\x1b[?25l")
        send(query(b"$DTW$cursor-already-off"))
        receive(peer, b"cursor-already-off", root / "replies.bin")
        frame = t.exchange(query(b"$ATW$cursor-still-off"), b"cursor-still-off")
        assert frame.primary == before.primary and frame.metadata["cursor_visible"] == 0
        t.exchange(b"\x1b[?25h")
        t.case(f"rip3-connected-dixon-{width}-{args.encoding}")
        selected = t.exchange(b"!|y00000W0M000000001a1a000000Dixon\r\n")
        assert selected.application["cterm.graphics_mode"] == 2
        assert (selected.metadata["plane_width"], selected.metadata["plane_height"]) == (width, height)
        assert (selected.metadata["columns"], selected.metadata["rows"]) == (width // 8, height // 8)
        x, y = 100 * width // 640, 100 * height // 350
        frame = t.exchange(b"\r!|@2S2SF|TF\r\n" + query(b"$TEXTXY$"),
                           f"{x + 2 * advance}:{y}".encode())
        assert any(pixel(frame, px, py) == 0xffffff
                   for py in range(y - 60, y + 40) for px in range(x - 40, x + 90))
        assert all(pixel(selected, px, py) == 0
                   for py in range(y - 60, y + 40) for px in range(x - 40, x + 90))
        # Solid bold changes ink, preserving the same origin and advance.
        t.exchange(b"\r!|q01|@2S2SFF\r\n" + query(b"$TEXTXY$"),
                   f"{x + 2 * advance}:{y}".encode())

        t.case("rip3-connected-parser-recovery-and-live-tables")
        # Invalid coordinate, unknown command, then valid drawing and live
        # WORLD changes. Neither rejection may close the TCP connection.
        frame = t.exchange(b"\r!|X??00|9?|f0A0A|c0C|X0505\r\n"
                           + query(b"$WORLDW$:$WORLDH$:$RESX$:$RESY$"),
                           f"10:10:{width}:{height}".encode())
        assert pixel(frame, width // 2, height // 2) == 0xff5555
        t.case("rip3-command-crlf-does-not-scroll-art")
        # More command-only CRLF lines than the terminal has rows. The ANSI
        # cursor and the entire graphic remain fixed, including split CR/LF.
        before = frame
        frame = t.exchange(b"!|!comment\r\n" * (height // 8 + 10))
        assert frame.primary == before.primary
        assert (frame.application["cterm.x"], frame.application["cterm.y"]) == (
            before.application["cterm.x"], before.application["cterm.y"])
        send(b"!|!split\r")
        frame = t.exchange(b"\n")
        assert frame.primary == before.primary
        assert frame.application["cterm.y"] == before.application["cterm.y"]
        t.case("rip3-connected-angles-templates-and-empty-input-continuation")
        frame = t.exchange(query(b"<<IF RESX=" + str(width).encode()
                                 + b">>[3:]yes<<ELSE>>[3:]no<<ENDIF>>[]$?3$"), b"yes")
        frame = t.exchange(query(b"^m" * 130), b"\r" * 130)
        t.exchange(query(b"$-=VALUE=kept$$&VALUE$"), b"kept")
        t.exchange(query(b"$COLORS$:$SHIFT$:$CTRL$"), b"256:0:0")
        if width == 640:
            t.case("rip3-connected-prompt")
            send(query(b"$*=PROMPT,20@RIP3 validation$") + b"\x1b[5n")
            client.wait_text("RIP3 validation")
            client.text("Ada")
            client.key("enter")
            receive(peer, b"Ada\x1b[0n", root / "replies.bin")
            assert client.capture().application["connected"] == 1

        t.case("rip3-connected-offscreen-port-and-bitmap-export")
        # Explicit device-sized WORLD, then a 2x2 offscreen port filled red.
        # Selection, drawing, returning to screen and export all use the same
        # owner and trusted per-BBS resource directory.
        t.exchange(b"\r!|f" + mega(width) + mega(height)
                   + b"|2P1000002021|2s100|S010C|B00000101|2W1000000000000Tile.bmp|2s000\r\n")
        files = list(root.rglob("Tile.bmp"))
        assert len(files) == 1, files
        data = files[0].read_bytes()
        assert data[:2] == b"BM" and int.from_bytes(data[18:22], "little") == 2
        assert int.from_bytes(data[22:26], "little") == 2
        offset = int.from_bytes(data[10:14], "little")
        assert data[offset:] == bytes((12, 12, 0, 0, 12, 12, 0, 0))
        frame = t.exchange(b"\r!|1b2S2S0000" + b"00" + b"0000" + b"0000Tile.bmp"
                           + b"|1C2S2S2T2T|1P3C2S0\r\n")
        assert all(pixel(frame, px, py) == 0xff5555
                   for py in (100, 101) for px in (100, 101, 120, 121))
        bitmap_width = {800: 10, 1024: 12}.get(width, 8)
        t.exchange(b"\r!|c0F|Y00000100|m5K5K|TFF\r\n" + query(b"$TEXTXY$"),
                   f"{200 + 2 * bitmap_width}:200".encode())
        t.exchange(b"\r!|f0A0A\r\n")

        t.case("rip3-connected-delay-and-queued-recovery")
        start = time.monotonic()
        frame = t.exchange(query(b"$D(3)$ready") + b"\r!|c0A|X0606\r\n", b"ready")
        assert time.monotonic() - start >= 0.045
        assert pixel(frame, width * 6 // 10, height * 6 // 10) == 0x55ff55

        t.case("rip3-connected-ansi-and-repeated-graphics-paint")
        # Restore one-to-one geometry and paint a small black rectangle over
        # later ANSI ink. Repeating COPY black must still publish damage even
        # when the engine's indexed pixels were already black.
        t.exchange(b"\r!|f" + mega(width) + mega(height) + b"|c00|S0100|B00000707\r\n")
        before = t.exchange(b"\x1b[H\x1b[97mA")
        assert any(pixel(before, px, py) != 0 for py in range(8) for px in range(8))
        frame = t.exchange(b"\r!|B00000707\r\n")
        assert all(pixel(frame, px, py) == 0 for py in range(8) for px in range(8))
        assert frame.application["connected"] == 1

        t.case("rip3-deactivated-text-window-keeps-rip-live")
        before = frame
        # Use a RIP reply as the fence: deactivated TTY must discard ANSI
        # controls as well as visible text and line feeds.
        send(b"\r!|w0000000000\r\n" + b"HIDDEN\r\n" * (height // 8 + 10)
             + b"\x1b[2J\x1b[6n" + query(b"deactivated"))
        receive(peer, b"deactivated", root / "replies.bin")
        frame = client.capture()
        assert frame.primary == before.primary
        assert (frame.application["cterm.x"], frame.application["cterm.y"]) == (
            before.application["cterm.x"], before.application["cterm.y"])
        send(b"\x1b[!")
        receive(peer, b"RIPSCRIP030001", root / "replies.bin")
        send(b"\r!|c0C|X0808\r\n" + query(b"still-live"))
        receive(peer, b"still-live", root / "replies.bin")
        frame = client.capture()
        assert pixel(frame, 8, 8) == 0xff5555
        assert frame.application["connected"] == 1

        t.case("rip3-normal-text-window-layout-wrap-and-reactivation")
        before = frame
        # Character origin (10,5) scales from reference (80,40) in 640x350.
        # Font zero cells divide this device into an 80x43 nominal grid.
        ox, oy = 80 * width // 640, 40 * height // 350
        cw, ch = width // 80, height // 43
        frame = t.exchange(b"\r!|w0A050C0700\r\n\x1b[97mABCDEF\x1b[6n", b"\x1b[1;3R")
        assert (frame.application["cterm.x"], frame.application["cterm.y"]) == (3, 1)
        assert (frame.metadata["plane_width"], frame.metadata["plane_height"]) == (width, height)
        assert (frame.metadata["columns"], frame.metadata["rows"]) == (width // 8, height // 8)
        assert any(pixel(frame, px, py) == 0xffffff
                   for py in range(oy, oy + ch) for px in range(ox, ox + 3 * cw))
        assert all(pixel(frame, px, py) == pixel(before, px, py)
                   for py in range(height) for px in range(width)
                   if not (ox <= px < ox + 3 * cw and oy <= py < oy + 3 * ch))
        # SOH introduces RIP without moving the TTY cursor with a leading CR.
        t.exchange(b"\x01|w0A050C0710\r\n\x1b[6n", b"\x1b[1;3R")
        frame = t.exchange(b"ZQ\x1b[6n", b"\x1b[2;2R")
        assert frame.application["cterm.y"] == 2
        send(b"\x01|w0000000000\r\nIGNORED" + query(b"off"))
        receive(peer, b"off", root / "replies.bin")
        frame = t.exchange(b"\x01|w0A050C0710\r\n\x1b[6n", b"\x1b[2;2R")
        assert frame.application["cterm.x"] == 2 and frame.application["cterm.y"] == 2
        # Malformed policy retains the live window, cursor and connection.
        t.exchange(b"\x01|w0A050C0790\r\n\x1b[6n", b"\x1b[2;2R")
        label = b"\x82" if args.encoding == "CP437" else "é".encode()
        frame = t.exchange(b"\x1b[3;1H" + label + b"\x1b[6n", b"\x1b[3;2R")
        assert any(pixel(frame, px, py) == 0xffffff
                   for py in range(oy + 2 * ch, oy + 3 * ch) for px in range(ox, ox + cw))
        client.key("f12")
        receive(peer, b"WINDOW:3:3:2:3", root / "replies.bin")
        # ANSI-BBS forwards Up as CSI A from the selected virtual TTY.
        client.key("up")
        receive(peer, b"\x1b[A", root / "replies.bin")
        client.key("f11")
        receive(peer, b"LOCAL:2:1", root / "replies.bin")
        frame = client.capture()
        assert any(pixel(frame, px, py) == 0xffffff
                   for py in range(oy, oy + ch) for px in range(ox, ox + cw))
        client.key("f10")
        receive(peer, b"RESET:3:3:1:1", root / "replies.bin")
        client.key("f9")
        receive(peer, b"WATCHING", root / "replies.bin")
        t.exchange(b"\x1b[97mA", b"INPUT:1:1")
        t.case("rip3-normal-text-window-scroll-is-clipped")
        before = client.capture()
        frame = t.exchange(b"\x1b[41m\x1b[2J\x1b[H\x1b[42m\x1b[2K\r\n"
                           b"\x1b[44m\x1b[2K\r\n\x1b[45m\x1b[2K\r\n\x1b[6n",
                           b"\x1b[3;1R")
        # The first row scrolls away; only this 3x3 window can move/clear.
        # ANSI's default VGA DAC channels are A8, independently of RIP's PAL.
        for row, colour in enumerate((0x0000a8, 0xa800a8, 0xa800a8)):
            assert all(pixel(frame, px, py) == colour
                       for py in range(oy + row * ch, oy + (row + 1) * ch)
                       for px in range(ox, ox + 3 * cw))
        assert all(pixel(frame, px, py) == pixel(before, px, py)
                   for py in range(height) for px in range(width)
                   if not (ox <= px < ox + 3 * cw and oy <= py < oy + 3 * ch))
        frame = t.exchange(b"\x1b[46m    \x1b[6n", b"\x1b[3;2R")
        # A fourth space wraps past the bottom right and scrolls again.
        for row, colour in enumerate((0xa800a8, 0x00a8a8, 0x00a8a8)):
            assert all(pixel(frame, px, py) == colour
                       for py in range(oy + row * ch, oy + (row + 1) * ch)
                       for px in range(ox, ox + 3 * cw))
        assert all(pixel(frame, px, py) == pixel(before, px, py)
                   for py in range(height) for px in range(width)
                   if not (ox <= px < ox + 3 * cw and oy <= py < oy + 3 * ch))
        # RIP painting under the window survives later boundaries/reactivation;
        # the ANSI view must import actual artwork instead of replaying old pixels.
        px, py = ox + cw, oy + ch
        frame = t.exchange(b"\x01|c0C|X" + mega(px) + mega(py) + b"|!boundary\r\n")
        assert pixel(frame, px, py) == 0xff5555
        send(b"\x01|w0000000000\r\n\x01|c0A|X" + mega(px) + mega(py)
             + b"\r\n" + query(b"off-painted"))
        receive(peer, b"off-painted", root / "replies.bin")
        frame = t.exchange(b"\x01|w0A050C0710\r\n")
        assert pixel(frame, px, py) == 0x55ff55
        for font, nominal_cols, nominal_rows, ref_cw, ref_ch in (
                (1, 91, 43, 7, 8), (2, 80, 25, 8, 14),
                (3, 91, 25, 7, 14), (4, 40, 25, 16, 14)):
            frame = t.exchange(b"\x01|w0A050C071" + str(font).encode()
                               + b"\r\n\x1b[97mF\x1b[6n", b"\x1b[1;2R")
            fx, fy = 10 * ref_cw * width // 640, 5 * ref_ch * height // 350
            fw, fh = width // nominal_cols, height // nominal_rows
            assert any(pixel(frame, cx, cy) == 0xffffff
                       for cy in range(fy, fy + fh) for cx in range(fx, fx + fw))
            assert (frame.metadata["plane_width"], frame.metadata["plane_height"]) == (width, height)

        t.case("rip3-text-cursor-home-goto-and-recovery")
        before = t.exchange(b"\x01|w0A050C0710\r\n")
        frame = t.exchange(b"\x01|g0102\r\n\x1b[6n", b"\x1b[3;2R")
        assert frame.primary == before.primary
        frame = t.exchange(b"\x01|H\r\n\x1b[6n", b"\x1b[1;1R")
        assert frame.primary == before.primary
        frame = t.exchange(b"\x01|gZZZZ\r\n\x1b[6n", b"\x1b[3;3R")
        assert frame.primary == before.primary
        # g has fixed two-digit fields even when XY width is five; base-64
        # 0l means column 47, which clamps to the window's right edge.
        t.exchange(b"\x01|J1S|n5|g0l01\r\n\x1b[6n", b"\x1b[2;3R")
        t.exchange(b"\x01|g??00|g000|H0|J10|n2\r\n\x1b[6n", b"\x1b[2;3R")
        send(b"\x01|w0000000000|g0000|H\r\n" + query(b"cursor-off"))
        receive(peer, b"cursor-off", root / "replies.bin")
        frame = t.exchange(b"\x01|w0A050C0710\r\n\x1b[6n", b"\x1b[2;3R")
        assert frame.primary == before.primary and frame.application["connected"] == 1
        # Positioning clears a pending right-margin wrap before the next text.
        t.exchange(b"\x01|g0200\r\nX\x01|g0001\r\nB\x1b[6n", b"\x1b[2;2R")

        t.case("rip3-positioned-text-activation-preserves-artwork-and-state")
        before = t.exchange(b"\x1b[?25h")
        px, py = ox + 2 * cw, oy + 2 * ch
        send(query(b"$DTW(ALL)$", at_cursor=True) + b"HIDDEN\r\n" * (height // 8 + 10)
             + b"\x1b[2J\x1b[?25l\x01|c0A|X" + mega(px) + mega(py)
             + b"\r\n" + query(b"window-off"))
        receive(peer, b"window-off", root / "replies.bin")
        hidden = client.capture()
        assert hidden.metadata["pixel_cursor_visible"] == 0
        assert (hidden.application["cterm.x"], hidden.application["cterm.y"]) == (2, 2)
        assert pixel(hidden, px, py) == 0x55ff55
        send(query(b"$ATW(1,35)$$ATW(CUR,36)$still-off"))
        receive(peer, b"still-off", root / "replies.bin")
        invalid = client.capture()
        assert invalid.metadata["pixel_cursor_visible"] == 0 and invalid.primary == hidden.primary
        frame = t.exchange(query(b"$ATW(CUR)$window-on") + b"\x1b[6n", b"window-on\x1b[2;2R")
        assert frame.primary == hidden.primary and frame.metadata["pixel_cursor_visible"] == 1
        assert pixel(frame, px, py) == 0x55ff55
        frame = t.exchange(query(b"$DTW(ALL,36)$still-on", at_cursor=True) + b"\x1b[6n", b"still-on\x1b[2;2R")
        assert frame.primary == hidden.primary and frame.application["connected"] == 1


def mega(number):
    digits = b"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    return bytes((digits[number // 36], digits[number % 36]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("encoding", choices=("CP437", "UTF-8"))
    parser.add_argument("--reports", type=Path, default=Path("reports/test-control"))
    args = parser.parse_args()
    for canvas in ((640, 480, 13), (800, 600, 16), (1024, 768, 21), (1280, 800, 26), (720, 400, 15)):
        run(args, *canvas)


if __name__ == "__main__":
    main()
