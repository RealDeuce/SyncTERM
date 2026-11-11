#!/usr/bin/env python3
"""IGS X1/X12 recolor retained indexed pixels through physical registers."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import socket

from st_igs_cursor_test import check, fixture
from st_igs_console_color_test import initial


def run(executable, reports, resolution, encoding, status, groups=False):
    screen = ("AtariST40x25", "AtariST80x25", "AtariST80x25Mono")[resolution]
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(2)
        listener.settimeout(15)
        entries = (
            "[IGS raw palette]\nConnectionType=Raw\nAddress=127.0.0.1\n"
            f"Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\nScreenMode={screen}\n"
            f"Font=Atari ST\nEncoding={encoding}\nNoStatus={str(not status).lower()}\n"
        )
        settings = "AudioModes=0\nMidiModes=0\nBlockyScaling=true\n"
        with fixture(executable, entries=entries, settings=settings, reports=reports, quiet=False) as (client, root):
            client.wait_text("IGS raw palette")
            client.key("enter")
            palette = list(initial(resolution))

            def send(peer, wire):
                with (root / "wire.bin").open("ab") as log:
                    log.write(wire)
                peer.sendall(wire)

            def inspect(marker):
                frame = client.wait_for(lambda f: f.application.get("cterm.igs_draw_x") == marker)
                check(frame, resolution, status, 4, "A", False, False, False, palette)

            def disconnect(peer):
                peer.shutdown(socket.SHUT_RDWR)
                client.wait_text("Disconnected: Enter exits, F1 details")
                client.key("enter")
                client.wait_text("Directory: Name")

            setup = b"\x1bfG#c>1,3:c 0,0:\x1bEG#p 3,2:A"
            with listener.accept()[0] as peer:
                client.wait_for(lambda f: f.application.get("connected") == 1)
                client.resize(1280, 960)
                send(peer, setup)
                # Literal register words and original-ST DAC results. The
                # high bits of 0x0888 are retained but produce black RGB.
                selector = 12 if groups else 1
                if groups:
                    steps = ((0, (1792, 112, 7, 1911), (0xff0000, 0x00ff00, 0x0000ff, 0xffffff)),
                             (1, (1, 2184, 1911, 0), (0x000024, 0, 0xffffff, 0)),
                             (2, (1911, 1792, 112, 7), (0xffffff, 0xff0000, 0x00ff00, 0x0000ff)),
                             (3, (0, 1, 2184, 1911), (0, 0x000024, 0, 0xffffff)),
                             (9999, (1792, 112, 7, 0), (0xff0000, 0x00ff00, 0x0000ff, 0)),
                             (0, (1, 0, 7, 1792), (0x000024, 0, 0x0000ff, 0xff0000)))
                else:
                    steps = ((0, (1792,), (0xff0000,)), (3, (7,), (0x0000ff,)),
                             (1, (112,), (0x00ff00,)), (0, (1,), (0x000024,)),
                             (9999, (2184,), (0,)), (3, (1911,), (0xffffff,)))
                for number, (index, words, rgbs) in enumerate(steps):
                    first = min(index, 3) * 4 if groups else min(index, 15)
                    for offset, (word, rgb) in enumerate(zip(words, rgbs)):
                        physical = first + offset
                        if resolution != 2:
                            palette[physical] = rgb
                        elif physical == 0:
                            palette[0] = 0xffffff if word & 1 else 0
                            palette[1] = palette[0] ^ 0xffffff
                    marker = 1000 + number
                    arguments = ",".join(str(value) for value in (selector, index, *words))
                    send(peer, f"G#g 1:G#X {arguments}:G#g>0:X 10,{marker},777:".encode())
                    inspect(marker)
                peer.settimeout(0.03)
                try:
                    reply = peer.recv(1)
                except socket.timeout:
                    pass
                else:
                    raise AssertionError(f"unexpected X{selector} reply: {reply!r}")
                send(peer, b"G#X 12,0,1792,112,7," if groups else b"G#X 1,0,")
                disconnect(peer)

            client.key("enter")
            with listener.accept()[0] as peer:
                client.wait_for(lambda f: f.application.get("connected") == 1)
                client.resize(1280, 960)
                palette = list(initial(resolution))
                send(peer, setup + b"G#X 10,2000,777:")
                inspect(2000)
                disconnect(peer)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("executable")
    parser.add_argument("--reports", default="reports/test-control")
    parser.add_argument("--groups", action="store_true")
    args = parser.parse_args()
    for resolution in range(3):
        for encoding in ("ATARI-ST", "CP437", "UTF-8"):
            for status in (False, True):
                run(args.executable, args.reports, resolution, encoding, status, args.groups)
