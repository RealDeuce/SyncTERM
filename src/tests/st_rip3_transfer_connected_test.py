#!/usr/bin/env python3
"""Connected RIP3 block mode through SyncTERM's actual XMODEM worker."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from binascii import crc_hqx
from pathlib import Path

from st_migrated_control_test import connection
from st_rip3_connected_test import query
from st_terminal_conformance_test import Terminal
from syncterm_fixture import receive


def run(args):
    with connection(args.executable, args.reports, encoding=args.encoding,
                    options="RIP=RIPv3\n") as (client, root, peer, send):
        terminal = Terminal(client, root, peer, send)
        terminal.exchange(b"\x1b[!", b"RIPSCRIP030001")
        terminal.exchange(query(b"$HOSTDIR(ART)$", at_cursor=True))

        def packet(number, data):
            return bytes((1, number, 255 - number)) + data + crc_hqx(data, 0).to_bytes(2, "big")

        payload = (b"!|c0F|X0000\r\n\x1b[2J").ljust(128, b"\x1a")

        def stored(filename, data):
            matches = list(root.rglob(filename))
            assert len(matches) == 1 and matches[0].parent.name == "HOST-ART", matches
            assert matches[0].read_bytes() == data

        def download(command, filename, prefix=b""):
            send(command)
            receive(peer, b"C", root / "replies.bin")
            # CR was already consumed by the RIP parser. The delayed LF
            # and the first binary block can arrive in the same read.
            send(prefix + packet(1, payload))
            receive(peer, b"\x06", root / "replies.bin")
            send(b"\x04")
            receive(peer, b"\x15", root / "replies.bin")
            send(b"\x04")
            receive(peer, b"\x06", root / "replies.bin")
            client.wait_for(lambda frame: not frame.application.get("transfer.active"))
            frame = terminal.exchange(query(b"ready", at_cursor=True), b"ready")
            assert frame.application["connected"] == 1
            stored(filename, payload)

        terminal.case("rip3-wire-block-mode-owns-split-crlf-and-binary-payload")
        download(b"!|9\x1b01020000data.bmp<>\r", "data.bmp", b"\n")
        terminal.case("rip3-xfer-shares-the-live-transfer-worker")
        download(b"!|1\x1b0000$XFER(receive,XMODEMCRC,none,macro.bin)$\r",
                 "macro.bin", b"\n")

        terminal.case("rip3-composite-batch-stores-an-unlisted-extension")
        send(b"!|9\x1b05040000batch.dat<>\r\n")
        receive(peer, b"C", root / "replies.bin")
        send(packet(0, b"other.dat\x004\x00".ljust(128, b"\x00")))
        receive(peer, b"\x06C", root / "replies.bin")
        send(packet(1, b"data".ljust(128, b"\x1a")))
        receive(peer, b"\x06", root / "replies.bin")
        send(b"\x04")
        receive(peer, b"\x15", root / "replies.bin")
        send(b"\x04")
        receive(peer, b"\x06C", root / "replies.bin")
        send(packet(0, bytes(128)))
        receive(peer, b"\x06", root / "replies.bin")
        client.wait_for(lambda frame: not frame.application.get("transfer.active"))
        stored("other.dat", b"data")

        terminal.case("rip3-cached-upload-requires-user-confirmation")
        send(b"!|1\x1b0000$XFER(SEND,XMODEMCRC,NONE,macro.bin)$\r\n")
        client.wait_text("The host requests an upload of cached RIP file macro.bin.")
        client.key("escape")
        receive(peer, b"\x18" * 10, root / "replies.bin")
        terminal.exchange(query(b"declined", at_cursor=True), b"declined")

        terminal.case("rip3-confirmed-upload-sends-the-cached-file")
        send(b"!|9\x1b11010000macro.bin<>\r\n")
        client.wait_text("The host requests an upload of cached RIP file macro.bin.")
        client.text("y")
        client.wait_for(lambda frame: frame.application.get("transfer.active") == 1)
        send(b"C")
        receive(peer, packet(1, payload), root / "replies.bin")
        send(b"\x06")
        receive(peer, b"\x04", root / "replies.bin")
        send(b"\x15")
        receive(peer, b"\x04", root / "replies.bin")
        send(b"\x06")
        client.wait_for(lambda frame: not frame.application.get("transfer.active"))
        terminal.exchange(query(b"uploaded", at_cursor=True), b"uploaded")

        terminal.case("rip3-block-refusal-and-malformed-wire-recover-nonfatally")
        terminal.exchange(b"!|9\x1b04010000kermit.bin<>\r\n"
                          + query(b"unsupported", at_cursor=True), b"\x18" * 10 + b"unsupported")
        terminal.exchange(b"!|9\x1b01010000bad.bin<>|c0F\r\n"
                          + query(b"alive", at_cursor=True), b"alive")
        terminal.exchange(b"!|9\x1b01010000../escape.bin<>\r\n"
                          + query(b"confined", at_cursor=True), b"\x18" * 10 + b"confined")
        terminal.exchange(b"!|9U000000\r\n!|9U010000\r\n!|9U020000\r\n"
                          + query(b"$XFER(RECEIVE,XMODEMCRC,NONE)$after", at_cursor=True), b"after")
        assert not list(root.rglob("escape.bin"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("encoding", choices=("CP437", "UTF-8"))
    parser.add_argument("--reports", type=Path, required=True)
    run(parser.parse_args())
