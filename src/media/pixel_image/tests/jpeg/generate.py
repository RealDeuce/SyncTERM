#!/usr/bin/env python3
"""Construct baseline JPEG fixtures directly from T.81 marker/block syntax.

Each hand-authored baseline block contains only its
DC coefficient: quantization = 1, inverse DCT yields (DC/8 + 128).
The grayscale fixture changes from 64 to 192 at row eight.  Color fixtures
have Y=80, Cb=96, Cr=192, approximately RGB=(170,45,23).
jpegtran losslessly repacks two of these coefficient sets into progressive scans.
Run this script with jpegtran on PATH to reproduce the checked-in fixtures.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
from pathlib import Path
from struct import pack
from subprocess import run


def marker(code, payload):
    return bytes((255, code)) + pack('>H', len(payload) + 2) + payload


def jpeg(horizontal, vertical, gray=False, cmyk=False):
    components = [(1, 1, 1)] if gray else [(1, horizontal, vertical), (2, 1, 1), (3, 1, 1)]
    if cmyk:
        components = [(n, 1, 1) for n in range(1, 5)]
    width, height = 17, 9
    result = b'\xff\xd8'
    if not cmyk:
        result += marker(0xe0, b'JFIF\0\x01\x00\0\0\x01\0\x01\0\0')
    result += marker(0xdb, b'\0' + bytes([1]) * 64)
    frame = bytes((8,)) + pack('>HHB', height, width, len(components))
    result += marker(0xc0, frame + b''.join(bytes((n, h << 4 | v, 0)) for n, h, v in components))
    # DC categories 0..11 have four-bit canonical Huffman codes 0000..1011.
    result += marker(0xc4, b'\0' + bytes((0, 0, 0, 12) + (0,) * 12) + bytes(range(12)))
    # One AC symbol: EOB, encoded by the single bit 0.
    result += marker(0xc4, bytes((0x10, 1)) + bytes(15) + b'\0')
    result += marker(0xda, bytes((len(components),)) + b''.join(bytes((n, 0)) for n, _, _ in components) + bytes((0, 63, 0)))
    bits, previous = '', {n: 0 for n, _, _ in components}
    for row in range((height + 8 * vertical - 1) // (8 * vertical)):
        for _ in range((width + 8 * horizontal - 1) // (8 * horizontal)):
            for n, h, v in components:
                for _ in range(h * v):
                    sample = (64 if row == 0 else 192) if gray else {1: 80, 2: 96, 3: 192, 4: 0}[n]
                    dc = (sample - 128) * 8
                    difference = dc - previous[n]
                    previous[n] = dc
                    size = abs(difference).bit_length()
                    bits += f'{size:04b}'
                    if size:
                        amplitude = difference if difference > 0 else difference + (1 << size) - 1
                        bits += f'{amplitude:0{size}b}'
                    bits += '0'
    bits += '1' * (-len(bits) % 8)
    entropy = bytes(int(bits[i:i + 8], 2) for i in range(0, len(bits), 8))
    return result + entropy.replace(b'\xff', b'\xff\0') + b'\xff\xd9'


if __name__ == '__main__':
    root = Path(__file__).parent
    for name, h, v, gray in [('gray', 1, 1, True), ('444', 1, 1, False), ('422', 2, 1, False), ('420', 2, 2, False)]:
        (root / (name + '.jpg')).write_bytes(jpeg(h, v, gray))

    (root / "cmyk.jpg").write_bytes(jpeg(1, 1, cmyk=True))

    for name in ("gray", "420"):
        run(["jpegtran", "-progressive", "-outfile", root / f"{name}-progressive.jpg",
             root / f"{name}.jpg"], check=True)
