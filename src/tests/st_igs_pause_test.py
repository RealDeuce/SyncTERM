#!/usr/bin/env python3
"""IGS t retains the input suffix and consumes the key that ends the wait."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import socket
import time

from st_igs_cursor_test import check, fixture


def receive(peer, expected):
    data = bytearray()
    while len(data) < len(expected):
        chunk = peer.recv(len(expected) - len(data))
        assert chunk, (bytes(data), expected)
        data.extend(chunk)
    assert data == expected, (bytes(data), expected)


def run(executable, reports, resolution, encoding, status):
    screen = ("AtariST40x25", "AtariST80x25", "AtariST80x25Mono")[resolution]
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(2)
        listener.settimeout(15)
        entries = (
            "[IGS pause]\nConnectionType=Raw\nAddress=127.0.0.1\n"
            f"Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\nScreenMode={screen}\n"
            f"Font=Atari ST\nEncoding={encoding}\nNoStatus={str(not status).lower()}\n"
        )
        settings = "AudioModes=0\nMidiModes=0\nBlockyScaling=true\n"
        with fixture(executable, entries=entries, settings=settings, reports=reports, quiet=False) as (client, root):
            client.wait_text("IGS pause")
            client.key("enter")

            def send(peer, wire):
                with (root / "wire.bin").open("ab") as log:
                    log.write(wire)
                peer.sendall(wire)

            def inspect(marker, paused):
                frame = client.wait_for(lambda f: f.application.get("cterm.igs_draw_x") == marker
                                        and f.application.get("cterm.igs_timed_pause") == paused)
                check(frame, resolution, status, 4, "A", False, False, False)
                return frame

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
                send(peer, setup + b"G#X 10,1000,777:G#t 30:G#X 10,1001,777:")
                receive(peer, b"\x13")
                inspect(1000, True)
                client.key(ord("x"))
                receive(peer, b"\x11")
                inspect(1001, False)
                client.key(ord("z"))
                receive(peer, b"z")

                send(peer, b"G#t>0:X 10,1002,777:")
                receive(peer, b"\x13\x11")
                inspect(1002, False)

                if resolution == 0 and encoding == "ATARI-ST" and not status:
                    started = time.monotonic()
                    send(peer, b"G#t>1:X 10,1003,777:")
                    receive(peer, b"\x13")
                    inspect(1002, True)
                    receive(peer, b"\x11")
                    assert time.monotonic() - started >= 0.95
                    inspect(1003, False)

                send(peer, b"G#t 30:G#X 10,1999,777:")
                receive(peer, b"\x13")
                client.wait_for(lambda f: f.application.get("cterm.igs_timed_pause") == 1)
                disconnect(peer)

            client.key("enter")
            with listener.accept()[0] as peer:
                peer.settimeout(5)
                client.wait_for(lambda f: f.application.get("connected") == 1)
                client.resize(1280, 960)
                send(peer, setup + b"G#X 10,2000,777:")
                inspect(2000, False)
                peer.settimeout(0.03)
                try:
                    reply = peer.recv(1)
                except socket.timeout:
                    pass
                else:
                    raise AssertionError(f"unexpected pause reply after reconnect: {reply!r}")
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
