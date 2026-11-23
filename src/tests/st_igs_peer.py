"""Wire assertions for connected IGS tests."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause


def expect_igs_version(peer):
    """Fence the preceding commands and catch any unexpected flow bytes."""
    peer.sendall(b'G#? 0:')
    reply = bytearray()
    peer.settimeout(5)
    while len(reply) < 4:
        data = peer.recv(4 - len(reply))
        assert data, bytes(reply)
        reply.extend(data)
    assert reply == b'2.21', bytes(reply)
