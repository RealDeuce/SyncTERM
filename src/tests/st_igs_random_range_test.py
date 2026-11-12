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

            def inspect(marker, bounds, seed=1, y=777):
                frame = client.wait_for(lambda f: f.application.get("cterm.igs_draw_x") == marker
                                        and f.application.get("cterm.igs_draw_y") == y)
                assert tuple(frame.application[f"cterm.igs_{name}"] for name in
                             ("r_lower", "r_upper", "R_lower", "R_upper")) == bounds
                assert frame.application["cterm.igs_random_seed"] == seed
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
                # References 11/19/124: construct encoded words through normal
                # X13 arithmetic. All operands used to construct them are
                # positive until the final subtract writes its destination.
                words = (b"G#X>13,3,6553,5,0:X>13,2,0,0],0:"
                         b"X>13,3,8191,4,1:X>13,2,0,1],1:"
                         b"X>13,3,8191,4,2:X>13,2,0,2],2:"
                         b"X>13,3,6553,5,3:X 13,2,0,3],3:")
                send(peer, words + b"G#X>2,0,0,255:X>2,10,11:g>2:X 3],0],1]:")
                # Independent Random recurrence from seed 1: first [10,11]
                # candidate is 10 on draw146; next is 10 after13 draws;
                # R then accepts byte41. Previewing X must consume no draws.
                inspect(10, (10, 11, 0, 255), 0x98ec2961, 41)
                send(peer, b"G#& 0,0,1,0,X,3,10,0],1]:")
                inspect(11, (10, 11, 0, 255), 0x8792c229, 194)
                send(peer, b"G#X 10,0],1]:")
                inspect(10, (10, 11, 0, 255), 0xd8ea7fd2, 127)
                peer.settimeout(2)
                flow = b""
                while len(flow) < 2: flow += peer.recv(2-len(flow))
                assert flow == b"\x13\x11", flow
                disconnect(peer)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(); parser.add_argument("executable")
    parser.add_argument("--reports", default="reports/test-control"); args = parser.parse_args()
    for resolution in range(3):
        for encoding in ("ATARI-ST", "CP437", "UTF-8"):
            for status in (False, True): run(args.executable, args.reports, resolution, encoding, status)
