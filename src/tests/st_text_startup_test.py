#!/usr/bin/env python3
# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

"""Exercise application startup and shutdown in an oversized private PTY."""

import errno
import fcntl
import os
from pathlib import Path
import pty
import select
import socket
import struct
import subprocess
import sys
import tempfile
import termios
import time


def main():
    executable, mode = sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix="syncterm-text-") as directory:
        root = Path(directory)
        env = dict(os.environ)
        for key in ("DISPLAY", "WAYLAND_DISPLAY", "XAUTHORITY", "XDG_RUNTIME_DIR"):
            env.pop(key, None)
        for key in ("XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_DOWNLOAD_DIR"):
            env[key] = directory
        env["TERM"] = "xterm-256color"
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen(1)
            port = listener.getsockname()[1]
            master, slave = pty.openpty()
            # Both dimensions exceed the initial 80x25 pixel-backed mode.
            fcntl.ioctl(slave, termios.TIOCSWINSZ,
                        struct.pack("HHHH", 37, 91, 0, 0))
            process = subprocess.Popen(
                [executable, "-i" + mode, "-s", "-q",
                 "-n", str(root / "syncterm.ini"),
                 "-b", str(root / "syncterm.lst"),
                 "raw://127.0.0.1:" + str(port)],
                stdin=slave, stdout=slave, stderr=slave,
                env=env, start_new_session=True)
            os.close(slave)
            output = bytearray()
            connected = False
            deadline = time.monotonic() + 15
            try:
                while time.monotonic() < deadline:
                    readers = [master] if connected else [master, listener]
                    ready, _, _ = select.select(readers, [], [], 0.1)
                    if listener in ready:
                        peer, _ = listener.accept()
                        peer.close()
                        connected = True
                    if master in ready:
                        try:
                            data = os.read(master, 65536)
                        except OSError as error:
                            if error.errno != errno.EIO:
                                raise
                            break
                        if not data:
                            break
                        output.extend(data)
                    if process.poll() is not None:
                        break
                status = process.wait(timeout=max(0.1, deadline - time.monotonic()))
                if not connected or status != 0:
                    raise RuntimeError(
                        f"-i{mode}: connected={connected}, exit={status}, "
                        f"terminal output={bytes(output)!r}")
            except subprocess.TimeoutExpired as error:
                raise RuntimeError(
                    f"-i{mode}: startup/shutdown timed out; "
                    f"terminal output={bytes(output)!r}") from error
            finally:
                if process.poll() is None:
                    process.kill()
                process.wait()
                os.close(master)


if __name__ == "__main__":
    main()
