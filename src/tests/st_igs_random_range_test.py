#!/usr/bin/env python3
"""X2 changes range state while preserving the coherent terminal scene."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import socket

from st_igs_cursor_test import check, fixture


def run(executable, reports, resolution, encoding, status):
    screen = ("AtariST40x25", "AtariST80x25", "AtariST80x25Mono")[resolution]
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0)); listener.listen(2); listener.settimeout(15)
        entries = ("[IGS random range]\nConnectionType=Raw\nAddress=127.0.0.1\n"
                   f"Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\nScreenMode={screen}\n"
                   f"Font=Atari ST\nEncoding={encoding}\nNoStatus={str(not status).lower()}\n")
        settings = "AudioModes=0\nMidiModes=0\nBlockyScaling=true\n"
        with fixture(executable, entries=entries, settings=settings, reports=reports, quiet=False) as (client, root):
            client.wait_text("IGS random range"); client.key("enter")

            def send(peer, wire):
                with (root / "wire.bin").open("ab") as log: log.write(wire)
                peer.sendall(wire)

            def inspect(marker, bounds):
                frame = client.wait_for(lambda f: f.application.get("cterm.igs_draw_x") == marker)
                assert tuple(frame.application[f"cterm.igs_{name}"] for name in
                             ("r_lower", "r_upper", "R_lower", "R_upper")) == bounds
                assert frame.application["cterm.igs_random_seed"] == 1
                check(frame, resolution, status, 4, "A", False, False, False)

            def disconnect(peer):
                peer.shutdown(socket.SHUT_RDWR)
                client.wait_text("Disconnected: Enter exits, F1 details"); client.key("enter")
                client.wait_text("Directory: Name")

            setup = b"\x1bfG#c>1,3:c 0,0:\x1bEG#p 3,2:A"
            with listener.accept()[0] as peer:
                client.wait_for(lambda f: f.application.get("connected") == 1); client.resize(1280, 960)
                send(peer, setup)
                steps = (("10,20", (10, 20, 0, 199)), ("30,30,40", (30, 31, 30, 40)),
                         ("0,0,0", (0, 31, 0, 0)), ("90,80", (90, 91, 0, 0)),
                         ("5,5,3", (5, 91, 5, 6)), ("2,1", (2, 3, 5, 6)))
                for number, (arguments, bounds) in enumerate(steps):
                    marker = 1000 + number
                    send(peer, f"G#g 1:G#X 2,{arguments}:G#g>0:X 10,{marker},777:".encode())
                    inspect(marker, bounds)
                peer.settimeout(0.03)
                try: reply = peer.recv(1)
                except socket.timeout: pass
                else: raise AssertionError(f"unexpected X2 reply: {reply!r}")
                send(peer, b"G#X 2,100,100,")
                disconnect(peer)
            client.key("enter")
            with listener.accept()[0] as peer:
                client.wait_for(lambda f: f.application.get("connected") == 1); client.resize(1280, 960)
                send(peer, setup + b"G#X 10,2000,777:")
                inspect(2000, (0, 199, 0, 199))
                disconnect(peer)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(); parser.add_argument("executable")
    parser.add_argument("--reports", default="reports/test-control"); args = parser.parse_args()
    for resolution in range(3):
        for encoding in ("ATARI-ST", "CP437", "UTF-8"):
            for status in (False, True): run(args.executable, args.reports, resolution, encoding, status)
