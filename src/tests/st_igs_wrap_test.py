#!/usr/bin/env python3
"""IGS w and VT52 immediate wrap through the application bitmap."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import socket

from st_igs_console_color_test import DATA, check, fixture


# Original TOS 1.04 rows from the same pinned font export as the color fixtures.
FONTS = {
    "8": {
        **DATA["fonts"]["8"],
        "118": (0, 0, 102, 102, 102, 60, 24, 0),
        "119": (0, 0, 198, 198, 214, 124, 108, 0),
    },
    "16": {
        **DATA["fonts"]["16"],
        "118": (0, 0, 0, 0, 0, 102, 102, 102, 102, 102, 60, 60, 24, 24, 0, 0),
        "119": (0, 0, 0, 0, 0, 198, 198, 214, 214, 254, 254, 238, 198, 130, 0, 0),
    },
}
FG = (3 - 32) & 65535
BG = (2 - 32) & 65535
ESC_COLOR = (27 - 32) & 65535


def run(executable, reports, resolution, encoding, status):
    screen = ("AtariST40x25", "AtariST80x25", "AtariST80x25Mono")[resolution]
    right = 39 if resolution == 0 else 79
    bottom = 23 if status else 24
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(2)
        listener.settimeout(15)
        entries = (
            "[IGS wrapping]\nConnectionType=Raw\nAddress=127.0.0.1\n"
            f"Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\nScreenMode={screen}\n"
            f"Font=Atari ST\nEncoding={encoding}\nNoStatus={str(not status).lower()}\n"
        )
        settings = "AudioModes=0\nMidiModes=0\nBlockyScaling=true\n"
        with fixture(executable, entries=entries, settings=settings, reports=reports, quiet=False) as (client, root):
            client.wait_text("IGS wrapping")
            client.key("enter")

            def send(peer, wire):
                with (root / "wire.bin").open("ab") as log:
                    log.write(wire)
                peer.sendall(wire)

            def inspect(peer, number, wire, cursor, wrap, cells, *, bg=BG, reverse=False, scroll=False):
                marker = 1000 + number
                setup = (
                    b"\x1bq\x1bfG#c>1,15:c 0,0:\x1bE"
                    b"G#c>1,3:c>0,2:C>3,7:E>0,9,0:M>1:W 100,60,A@"
                )
                send(peer, setup + wire + f"G#X 10,{marker},777:".encode())
                frame = client.wait_for(lambda f: f.application.get("cterm.igs_draw_x") == marker)
                assert frame.application["cterm.ext_attr.auto_wrap"] == wrap
                assert not frame.application["cterm.last_column_flag.set"]
                backgrounds = [0] * (bottom + 1)
                if scroll:
                    backgrounds[-1] = bg
                model = dict(
                    state=0, x=cursor[0], y=cursor[1], fg=FG, bg=bg,
                    cells=cells, row_background=backgrounds, scrolls=int(scroll),
                )
                check(frame, resolution, status, marker, dict(reverse=reverse, scene=number), model, FONTS)

            def disconnect(peer):
                peer.shutdown(socket.SHUT_RDWR)
                client.wait_text("Disconnected: Enter exits, F1 details")
                client.key("enter")
                client.wait_text("Directory: Name")

            with listener.accept()[0] as peer:
                client.wait_for(lambda f: f.application.get("connected") == 1)
                client.resize(1280, 960)
                inspect(peer, 0, f"\x1bwG#p>{right},2:w 1:A".encode(),
                        (0, 3), True, {(right, 2): (65, FG, BG)})
                inspect(peer, 1, f"G#p>{right},{bottom}:w 9999:Ac".encode(),
                        (right, bottom), False, {(right, bottom): (99, FG, BG)})
                inspect(peer, 2, f"G#p {right - 1},2:\x1bG#w 1:\x1bcG#w 0:".encode(),
                        (right, 2), False, {
                            (right - 1, 2): (118, FG, BG), (right, 2): (119, FG, ESC_COLOR),
                        }, bg=ESC_COLOR)
                inspect(peer, 3, f"G#v>1:p>{right},{bottom}:w 1:A".encode(),
                        (0, bottom), True, {(right, bottom - 1): (65, BG, FG)},
                        reverse=True, scroll=True)
                peer.settimeout(0.03)
                try:
                    reply = peer.recv(1)
                except socket.timeout:
                    pass
                else:
                    raise AssertionError(f"unexpected w reply: {reply!r}")
                send(peer, b"G#w 0")
                disconnect(peer)

            client.key("enter")
            with listener.accept()[0] as peer:
                client.wait_for(lambda f: f.application.get("connected") == 1)
                client.resize(1280, 960)
                inspect(peer, 4, f"G#p {right},2:A".encode(),
                        (right, 2), False, {(right, 2): (65, FG, BG)})
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
