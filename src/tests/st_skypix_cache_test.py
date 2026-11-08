#!/usr/bin/env python3
"""SkyPix command-16 cache uploads, with literal wire/file/pixel expectations."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import binascii
import socket
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'testing'))
from syncterm_fixture import fixture

CACHE_REPLY = b'\x18'*10 + b'\x08'*10 + b'\r'


def run(executable, reports, kick, encoding, status):
    print(kick, encoding, 'status', status, flush=True)
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        listener.listen(1)
        listener.settimeout(15)
        entries = ('[SkyPix cache]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                   f'Port={listener.getsockname()[1]}\nGraphics=SkyPix (Kick {kick})\n'
                   f'Encoding={encoding}\nNoStatus={str(not status).lower()}\n'
                   'ScreenMode=Amiga640x200\n')
        with fixture(executable, entries=entries, settings='AudioModes=0\n',
                     reports=reports) as (client, root):
            client.wait_text('SkyPix cache')
            client.key('enter')
            with listener.accept()[0] as peer:
                peer.settimeout(10)
                client.wait_for(lambda s: s.application.get('connected') == 1)

                def read(size):
                    result = bytearray()
                    while len(result) < size:
                        part = peer.recv(size-len(result))
                        assert part, result
                        result.extend(part)
                    return bytes(result)

                def reply():
                    value = read(1)
                    while value == b'C':
                        value = read(1)
                    return value

                def frame(data=b''):
                    peer.sendall(data + b'\x1b[5n')
                    assert read(4) == b'\x1b[0n'
                    return client.capture()

                def request(mode, name):
                    # Dimensions do not size, truncate or decode cached files.
                    peer.sendall(f'\x1b[16;{mode};999;888!'.encode() + name + b'!')

                def idle():
                    client.wait_for(lambda s: s.application.get('transfer.active') == 0
                                    and s.application.get('transfer.reserved') == 0)

                def block(number, data, corrupt=False):
                    crc = binascii.crc_hqx(data, 0) ^ int(corrupt)
                    return bytes((1 if len(data) == 128 else 2, number, 255-number)) + data + crc.to_bytes(2, 'big')

                def upload(payload, block_size):
                    padded = bytearray()
                    for number, offset in enumerate(range(0, len(payload), block_size), 1):
                        data = payload[offset:offset+block_size].ljust(block_size, b'\x1a')
                        if number == 2:
                            # After negotiation, a bad CRC requests this
                            # block again with NAK rather than the initial C.
                            peer.sendall(block(number, data, corrupt=True))
                            assert reply() == b'\x15'
                        peer.sendall(block(number, data))
                        assert reply() == b'\x06'
                        if number == 1:
                            peer.sendall(block(number, data))
                            assert reply() == b'\x06'
                        padded.extend(data)
                    peer.sendall(b'\x04')
                    value = reply()
                    if value == b'\x15':
                        peer.sendall(b'\x04\x1b[5n')
                        value = reply()
                    else:
                        peer.sendall(b'\x1b[5n')
                    assert value == b'\x06'
                    assert read(4) == b'\x1b[0n'
                    idle()
                    return bytes(padded)

                def cached_file(name):
                    files = list(root.rglob(name))
                    assert len(files) == 1 and files[0].parent.name == 'skypix', files
                    return files[0]

                def clean(name):
                    assert not list(root.rglob(name)), name
                    assert not list(root.rglob('*.part'))

                # Capture a two-pixel red brush from a literal rectangle.
                # Uploads of opaque sound files must not replace that brush.
                initial = frame(b'\f\x1b[15;3!\x1b[4;10;10;11;10!\x1b[6;10;9;2;1!')
                palette = tuple(initial.palette[512:528])

                def unchanged():
                    shown = frame(b'\x1b[7;0;0;40;40;2;1;192;255!')
                    assert tuple(shown.palette[512:528]) == palette
                    m = shown.metadata
                    for plane in (shown.primary, shown.alternate):
                        for y in (10, 40):
                            x = 10 if y == 10 else 40
                            assert tuple(plane[(y+m['pixel_top'])*m['plane_width']+x:
                                               (y+m['pixel_top'])*m['plane_width']+x+2]) == (515, 515)

                completed = {}
                for mode, name, size in ((2, 'sample.8svx', 128), (3, 'sample.raw', 1024)):
                    request(mode, name.encode())
                    assert read(1) == b'C'
                    # Deliberately not valid audio: caching is independent of
                    # audio formats. Include LF, NUL and every byte value.
                    payload = (b'\n\0opaque sample\0' + bytes(range(256))*5)[:size+257]
                    completed[name] = upload(payload, size)
                    path = cached_file(name)
                    assert path.read_bytes() == completed[name]
                    unchanged()
                    # All types share SkyTERM's BRU: namespace. A different
                    # sample type or type 20 must also hit this same file.
                    for repeated in (mode, 5-mode, 20):
                        request(repeated, name.encode())
                        assert read(len(CACHE_REPLY)) == CACHE_REPLY
                        assert path.read_bytes() == completed[name]
                        unchanged()
                    for bad in (b'../escape', b'folder/file', b'folder\\file', b'DEV:file', b'.hidden'):
                        request(mode, bad)
                        assert read(2) == b'\x18\x18'
                        frame()
                    # Cancel after a block has reached the partial file.
                    cancelled = f'cancel-{mode}.bin'
                    request(mode, cancelled.encode())
                    assert read(1) == b'C'
                    peer.sendall(block(1, b'X'*128))
                    assert reply() == b'\x06'
                    peer.sendall(b'\x18\x18')
                    assert reply() + read(15) == b'\x18'*8 + b'\x08'*8
                    idle()
                    frame()
                    clean(cancelled)
                    unchanged()

                # Sound loading is separate from successful opaque caching.
                # No device is opened: the following play commands must not
                # delay the reply fence, even for zero/large loop counts.
                assert frame().application['cterm.skypix_sample_size'] == 0
                playable = struct.pack('>IH', 256, 8000) + bytes(range(256))
                request(3, b'playable.raw')
                assert read(1) == b'C'
                completed['playable.raw'] = upload(playable, 128)

                def sample_state(size, period):
                    shown = frame(b'\x1b[9;99;40;99;0!\x1b[9;0;0;0;65535!\x1b[15;4!\x1b[1;70;20!')
                    assert shown.application['cterm.skypix_sample_size'] == size, shown.application
                    assert shown.application['cterm.skypix_sample_period'] == period
                    assert shown.application['cterm.skypix_playing'] == 0
                    m = shown.metadata
                    for plane in (shown.primary, shown.alternate):
                        assert plane[(20+m['pixel_top'])*m['plane_width']+70] == 516
                    unchanged()

                sample_state(256, 447)
                request(3, b'odd.raw')
                assert read(1) == b'C'
                completed['odd.raw'] = upload(struct.pack('>IH', 3, 11025)+b'\x80\x00\x7f', 128)
                sample_state(3, 324)
                request(3, b'playable.raw')
                assert read(len(CACHE_REPLY)) == CACHE_REPLY
                sample_state(256, 447)
                # Type 2/20 do not load samples; a malformed cached type 3
                # retains the old sample without altering the cache reply.
                for mode, name in ((2, b'odd.raw'), (20, b'odd.raw'), (3, b'sample.raw')):
                    request(mode, name)
                    assert read(len(CACHE_REPLY)) == CACHE_REPLY
                    sample_state(256, 447)
                request(3, b'bad-header.raw')
                assert read(1) == b'C'
                completed['bad-header.raw'] = upload(b'\0'*6, 128)
                sample_state(256, 447)
                request(3, b'cancel-loaded.raw')
                assert read(1) == b'C'
                peer.sendall(block(1, playable[:128]))
                assert reply() == b'\x06'
                peer.sendall(b'\x18\x18')
                assert reply() + read(15) == b'\x18'*8 + b'\x08'*8
                idle()
                sample_state(256, 447)
                clean('cancel-loaded.raw')
                frame(b'\x1b[99!\x1b[17;2!')
                sample_state(256, 447)
                frame(b'\x1bc')
                # Reset releases the sample without changing cached files.
                reset = frame(b'\x1b[9;0;0;0;1!')
                assert reset.application['cterm.skypix_sample_size'] == 0
                assert reset.application['cterm.skypix_sample_period'] == 0
                assert reset.application['cterm.skypix_playing'] == 0

                request(4, b'unsupported.bin')
                assert read(2) == b'\x18\x18'
                frame()
                clean('unsupported.bin')
                # Exercise each new type at EOF across the status matrix.
                request(3 if status else 2, b'disconnected.bin')
                assert read(1) == b'C'
                peer.sendall(block(1, b'Y'*128))
                assert reply() == b'\x06'
                peer.shutdown(socket.SHUT_RDWR)
            client.wait_text('Directory: Name')
            clean('disconnected.bin')
            for name, expected in completed.items():
                assert cached_file(name).read_bytes() == expected
            assert b'[wren' not in client.stderr, bytes(client.stderr)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('executable')
    parser.add_argument('--reports', default='reports/test-control')
    args = parser.parse_args()
    for kick in ('1.3', '3.1'):
        for encoding in ('CP437', 'UTF-8'):
            for status in (False, True):
                run(args.executable, args.reports, kick, encoding, status)
