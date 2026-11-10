#!/usr/bin/env python3
"""IGS r and VT52 j/k share one cursor save slot."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import socket

from st_igs_console_color_test import DATA, check, fixture


# Original TOS 1.04 rows from generated/tos104-fonts.json, using the same
# pinned ROM and font export as the console color fixtures.
FONTS = {
    "8": {
        **DATA["fonts"]["8"],
        "106": (24, 0, 24, 24, 24, 24, 24, 112),
        "107": (96, 96, 102, 108, 120, 108, 102, 0),
    },
    "16": {
        **DATA["fonts"]["16"],
        "106": (0, 0, 12, 12, 0, 12, 12, 12, 12, 12, 12, 12, 12, 12, 124, 120),
        "107": (0, 0, 192, 192, 192, 204, 220, 248, 240, 248, 216, 204, 206, 198, 0, 0),
    },
}
FOREGROUND = (15 - 32) & 65535
BACKGROUND = (2 - 32) & 65535
ESC_COLOR = (27 - 32) & 65535

# Wire suffix, zero-based final cursor, raw foreground, and literal cells.
SCENES = (
    (b"G#r>0:p 7,5:\x1bk", (3, 2), FOREGROUND, {}),
    (b"\x1bjG#p>7,5:r>1:p>9,8:r 1:", (0, 0), FOREGROUND, {}),
    (b"G#r>0:p>7,5:r>9999:p>9,8:r 1:", (7, 5), FOREGROUND, {}),
    (b"\x1bG#r>0:r 1:", (0, 0), FOREGROUND, {(3, 2): (106, FOREGROUND, BACKGROUND)}),
    (b"\x1bjG#p 7,5:\x1bbG#r 1:\x1bk", (3, 2), ESC_COLOR, {(7, 5): (107, ESC_COLOR, BACKGROUND)}),
)


def run(executable, reports, resolution, encoding, status):
    screen = ("AtariST40x25", "AtariST80x25", "AtariST80x25Mono")[resolution]
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(2)
        listener.settimeout(15)
        entries = (
            "[IGS cursor save]\nConnectionType=Raw\nAddress=127.0.0.1\n"
            f"Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\nScreenMode={screen}\n"
            f"Font=Atari ST\nEncoding={encoding}\nNoStatus={str(not status).lower()}\n"
        )
        settings = "AudioModes=0\nMidiModes=0\nBlockyScaling=true\n"
        with fixture(executable, entries=entries, settings=settings, reports=reports, quiet=False) as (client, root):
            client.wait_text("IGS cursor save")
            client.key("enter")

            def send(peer, wire):
                with (root / "wire.bin").open("ab") as log:
                    log.write(wire)
                peer.sendall(wire)

            def setup():
                return (
                    b"\x1bq\x1bf\x1bwG#g>0:c>1,15:c 0,0:\x1bE"
                    b"G#c>0,2:C>3,7:E>0,9,0:M>1:W 100,60,A@G#p 3,2:"
                )

            def inspect(peer, number, wire, cursor, fg, cells):
                marker = 1000 + number
                send(peer, wire + f"G#X 10,{marker},777:".encode())
                frame = client.wait_for(lambda f: f.application.get("cterm.igs_draw_x") == marker)
                model = dict(
                    state=0, x=cursor[0], y=cursor[1], fg=fg, bg=BACKGROUND,
                    cells=cells, row_background=[0] * (24 if status else 25), scrolls=0,
                )
                check(frame, resolution, status, marker, dict(reverse=False, scene=number), model=model, fonts=FONTS)

            def disconnect(peer):
                peer.shutdown(socket.SHUT_RDWR)
                client.wait_text("Disconnected: Enter exits, F1 details")
                client.key("enter")
                client.wait_text("Directory: Name")

            with listener.accept()[0] as peer:
                client.wait_for(lambda frame: frame.application.get("connected") == 1)
                client.resize(1280, 960)
                for index, (wire, cursor, fg, cells) in enumerate(SCENES):
                    inspect(peer, index, b"\x1bk" + setup() + wire, cursor, fg, cells)
                peer.settimeout(0.03)
                try:
                    reply = peer.recv(1)
                except socket.timeout:
                    pass
                else:
                    raise AssertionError(f"unexpected r reply: {reply!r}")
                send(peer, b"\x1bjG#r 1")
                disconnect(peer)

            client.key("enter")
            with listener.accept()[0] as peer:
                client.wait_for(lambda frame: frame.application.get("connected") == 1)
                client.resize(1280, 960)
                inspect(peer, len(SCENES), setup() + b"G#r 1:", (0, 0), FOREGROUND, {})
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
