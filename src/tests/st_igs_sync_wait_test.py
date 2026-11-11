#!/usr/bin/env python3
"""IGS q sync-unit waits retain double-copy control and the input suffix."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import socket
import time

from st_igs_cursor_test import check, fixture
from st_igs_pause_test import receive


def run(executable, reports, resolution, encoding, status):
    screen = ("AtariST40x25", "AtariST80x25", "AtariST80x25Mono")[resolution]
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(2)
        listener.settimeout(15)
        entries = (
            "[IGS sync wait]\nConnectionType=Raw\nAddress=127.0.0.1\n"
            f"Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\nScreenMode={screen}\n"
            f"Font=Atari ST\nEncoding={encoding}\nNoStatus={str(not status).lower()}\n"
        )
        settings = "AudioModes=0\nMidiModes=0\nBlockyScaling=true\n"
        with fixture(executable, entries=entries, settings=settings, reports=reports, quiet=False) as (client, root):
            client.wait_text("IGS sync wait")
            client.key("enter")

            def send(peer, wire):
                with (root / "wire.bin").open("ab") as log:
                    log.write(wire)
                peer.sendall(wire)

            def inspect(marker, control, paused=False):
                frame = client.wait_for(lambda f: f.application.get("cterm.igs_draw_x") == marker
                                        and f.application.get("cterm.igs_input_paused") == paused)
                assert frame.application["cterm.igs_double_step"] == control
                assert not frame.application["cterm.igs_timed_pause"]
                check(frame, resolution, status, 4, "A", False, False, False)

            def disconnect(peer):
                peer.shutdown(socket.SHUT_RDWR)
                client.wait_text("Disconnected: Enter exits, F1 details")
                client.key("enter")
                client.wait_text("Directory: Name")

            setup = b"\x1bfG#c>1,3:c 0,0:\x1bEG#p 3,2:A"
            with listener.accept()[0] as peer:
                peer.settimeout(5)
                client.wait_for(lambda f: f.application.get("connected") == 1)
                client.resize(1280, 960)
                send(peer, setup)
                steps = ((9995, 4), (1, 4), (9996, 3), (9997, 2), (9998, 1), (9999, 0))
                for number, (word, control) in enumerate(steps):
                    send(peer, f"G#q>{word}:X 10,{1000 + number},777:".encode())
                    inspect(1000 + number, control)
                marker = 1005
                if resolution == 0 and encoding == "ATARI-ST" and not status:
                    started = time.monotonic()
                    send(peer, b"G#q>0:X 10,1006,777:")
                    inspect(1006, 0)
                    assert time.monotonic() - started >= 0.030
                    marker = 1006
                peer.settimeout(0.03)
                try:
                    reply = peer.recv(1)
                except socket.timeout:
                    pass
                else:
                    raise AssertionError(f"unexpected q reply: {reply!r}")
                peer.settimeout(5)
                send(peer, b"G#q 180:G#X 10,1999,777:")
                client.wait_for(lambda f: f.application.get("cterm.igs_input_paused") == 1)
                client.key(ord("z"))
                receive(peer, b"z")
                inspect(marker, 0, True)
                disconnect(peer)

            client.key("enter")
            with listener.accept()[0] as peer:
                peer.settimeout(5)
                client.wait_for(lambda f: f.application.get("connected") == 1)
                client.resize(1280, 960)
                send(peer, setup + b"G#q>0:X 10,2000,777:")
                inspect(2000, 0)
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
