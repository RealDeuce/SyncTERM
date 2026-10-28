#!/usr/bin/env python3
"""A completed transfer must leave its connection alive until the CLI peer exits."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading


def check_shutdown(driver, root, close_peer):
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(10)
        # An empty raw upload completes without a protocol-specific handshake.
        source = root / "empty.bin"
        source.write_bytes(b"")
        process = subprocess.Popen(
            [driver, str(listener.getsockname()[1]), "send", "8", str(root), str(source)],
            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        complete, log = threading.Event(), bytearray()

        def read_log():
            for line in process.stderr:
                log.extend(line)
                if line.startswith(b"transfer result="):
                    complete.set()

        reader = threading.Thread(target=read_log)
        reader.start()
        try:
            with listener.accept()[0] as peer:
                peer.settimeout(10)
                assert complete.wait(10), "Transfer did not complete: " + log.decode(errors="replace")
                try:
                    code = process.wait(timeout=0.25)
                except subprocess.TimeoutExpired:
                    pass
                else:
                    raise AssertionError(f"Driver exited ({code}) before the peer closed")
                if close_peer:
                    # CLI receivers can still write terminal restoration data
                    # after acknowledging the transfer. Drain it until FIN.
                    peer.sendall(b"receiver trailer\r\n" * 4096)
                    peer.shutdown(socket.SHUT_WR)
                code = process.wait(timeout=10)
                reader.join(timeout=10)
                assert not reader.is_alive()
                if close_peer:
                    assert code == 0, log.decode(errors="replace")
                else:
                    assert code != 0 and b"peer shutdown failed (-20)" in log, log.decode(errors="replace")
        finally:
            if process.poll() is None:
                process.kill()
            process.wait(timeout=10)
            reader.join(timeout=10)
            process.stderr.close()


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="st-transfer-shutdown-") as directory:
        check_shutdown(sys.argv[1], Path(directory), True)
        check_shutdown(sys.argv[1], Path(directory), False)
