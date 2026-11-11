#!/usr/bin/env python3
"""IGS v through persistent VT52 state and the application bitmap."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import socket

from st_igs_console_color_test import DATA, check, fixture


# Original TOS 1.04 rows from generated/tos104-fonts.json, using the same
# pinned ROM and font export as the console color fixtures.
FONTS = {
    "8": {
        **DATA["fonts"]["8"],
        "112": (0, 0, 124, 102, 102, 102, 124, 96),
        "113": (0, 0, 62, 102, 102, 102, 62, 6),
    },
    "16": {
        **DATA["fonts"]["16"],
        "112": (0, 0, 0, 0, 0, 124, 126, 102, 102, 102, 102, 102, 126, 124, 96, 96),
        "113": (0, 0, 0, 0, 0, 62, 126, 102, 102, 102, 102, 102, 126, 62, 6, 6),
    },
}
FG = (3 - 32) & 65535
BG = (2 - 32) & 65535
NEW_FG = (4 - 32) & 65535
NEW_BG = (7 - 32) & 65535
ESC_COLOR = (27 - 32) & 65535


def run(executable, reports, resolution, encoding, status):
    screen = ("AtariST40x25", "AtariST80x25", "AtariST80x25Mono")[resolution]
    rows = 24 if status else 25
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(2)
        listener.settimeout(15)
        entries = (
            "[IGS reverse video]\nConnectionType=Raw\nAddress=127.0.0.1\n"
            f"Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\nScreenMode={screen}\n"
            f"Font=Atari ST\nEncoding={encoding}\nNoStatus={str(not status).lower()}\n"
        )
        settings = "AudioModes=0\nMidiModes=0\nBlockyScaling=true\n"
        with fixture(executable, entries=entries, settings=settings, reports=reports, quiet=False) as (client, root):
            client.wait_text("IGS reverse video")
            client.key("enter")

            def send(peer, wire):
                with (root / "wire.bin").open("ab") as log:
                    log.write(wire)
                peer.sendall(wire)

            def inspect(peer, number, wire, cursor, fg, bg, reverse, cells, scroll=False, fresh=False):
                marker = 1000 + number
                setup = (
                    b"\x1bf\x1bwG#c>1,15:c 0,0:\x1bE"
                    b"G#c>1,3:c>0,2:C>3,7:E>0,9,0:M>1:W 100,60,A@G#p 3,2:"
                )
                if not fresh:
                    setup = b"\x1bq" + setup
                send(peer, setup + wire + f"G#X 10,{marker},777:".encode())
                frame = client.wait_for(lambda f: f.application.get("cterm.igs_draw_x") == marker)
                backgrounds = [0] * rows
                if scroll:
                    backgrounds[-1] = bg
                model = dict(
                    state=0, x=cursor[0], y=cursor[1], fg=fg, bg=bg,
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
                inspect(peer, 0, b"AG#v 1:AG#c>1,4:c 0,7:AG#v 9999:A",
                        (7, 2), NEW_FG, NEW_BG, False, {
                            (3, 2): (65, FG, BG), (4, 2): (65, BG, FG),
                            (5, 2): (65, NEW_BG, NEW_FG), (6, 2): (65, NEW_FG, NEW_BG),
                        })
                inspect(peer, 1, b"G#v 1:\x1bG#v 0:\x1bbG#v 1:",
                        (5, 2), ESC_COLOR, BG, True, {
                            (3, 2): (113, BG, FG), (4, 2): (112, BG, ESC_COLOR),
                        })
                inspect(peer, 2, f"G#v>1:p 3,{rows - 1}:A\n".encode(),
                        (4, rows - 1), FG, BG, True,
                        {(3, rows - 2): (65, BG, FG)}, scroll=True)
                peer.settimeout(0.03)
                try:
                    reply = peer.recv(1)
                except socket.timeout:
                    pass
                else:
                    raise AssertionError(f"unexpected v reply: {reply!r}")
                send(peer, b"G#v 0")
                disconnect(peer)

            client.key("enter")
            with listener.accept()[0] as peer:
                client.wait_for(lambda f: f.application.get("connected") == 1)
                client.resize(1280, 960)
                inspect(peer, 3, b"A", (4, 2), FG, BG, False,
                        {(3, 2): (65, FG, BG)}, fresh=True)
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
