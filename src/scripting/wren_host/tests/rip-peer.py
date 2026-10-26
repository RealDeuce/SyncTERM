#!/usr/bin/env python3
"""Independent, deterministic RIP X/YMODEM peer over the shell PTY."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import binascii
import os
import select
import sys
import time


def read_byte():
    if not select.select([0], [], [], 8)[0]:
        raise RuntimeError("client response timed out")
    byte = os.read(0, 1)
    if not byte:
        raise RuntimeError("client closed")
    return byte[0]


def expect(value):
    # Initial NAK/C may be queued ahead of the first block's ACK.
    for _ in range(20):
        byte = read_byte()
        if byte == value:
            return
        if byte == 24:
            raise RuntimeError("client cancelled")
    raise RuntimeError("unexpected response")


def packet(number, data, crc):
    data = data.ljust(128, b"\x1a")
    check = binascii.crc_hqx(data, 0).to_bytes(2, "big") if crc else bytes([sum(data) & 255])
    return bytes([1, number & 255, 255 - (number & 255)]) + data + check


mode = int(sys.argv[1])
scene = b"!|*|c04|X2S2S\r\n" + b" " * 128 + b"!|c02|X2T2T\r\n"
icon = b"\0\0\0\0\x80\x80\x80\x80F"
if mode >= 6:
    os.write(1, b"!|9\x1b10000000upload.rip<>\r\n")
    if mode == 7:
        stamps = []
        for _ in range(10):
            assert read_byte() == 24
            stamps.append(time.monotonic())
        assert stamps[-1] - stamps[0] >= 0.8, stamps
    else:
        # No source bytes may be sent before the user accepts.
        assert not select.select([0], [], [], 0.2)[0]
        os.write(1, b"\x15")
        assert read_byte() == 1
        block = bytes(read_byte() for _ in range(131))
        assert block[0:2] == b"\x01\xfe" and block[2:8] == b"SECRET"
        assert (sum(block[2:130]) & 255) == block[130]
        os.write(1, b"\x06")
        assert read_byte() == 4
        os.write(1, b"\x06")
else:
    batch = mode >= 4
    name = ["scene.rip", "stored.rip", "asset.icn", "help.hlp", "scene.rip", "scene.rip"][mode]
    files = [(name, icon if mode == 2 else scene)]
    if batch:
        files = [("asset.icn", icon), ("scene.rip", scene)]
    command = b"!|9\x1b0" + (b"5" if batch else b"0") + ("%02d0000" % mode).encode()
    command += (b"" if batch else name.encode()) + b"<>\r\n"
    os.write(1, command)
    expect(ord("C") if batch else 21)
    for index, (name, data) in enumerate(files):
        if batch:
            header = (name.encode() + b"\0" + str(len(data)).encode() + b" 0\0").ljust(128, b"\0")
            wire = packet(0, header, True)
            os.write(1, wire)
            expect(6)
            expect(ord("C"))
        for number, position in enumerate(range(0, len(data), 128), 1):
            wire = packet(number, data[position:position + 128], batch)
            os.write(1, wire)
            expect(6)
        # Leave time to observe streamed drawing before this file completes.
        time.sleep(0.25)
        os.write(1, b"\x04")
        reply = read_byte()
        if reply == 21:
            os.write(1, b"\x04")
            expect(6)
        else:
            assert reply == 6, reply
        if batch:
            expect(ord("C"))
    if batch:
        os.write(1, packet(0, bytes(128), True))
        expect(6)
os.write(1, b"\r\nAFTER-RIP")
time.sleep(0.4)
