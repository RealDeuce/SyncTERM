#!/usr/bin/env python3
# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

"""Exercise application startup and shutdown in an oversized private PTY."""

import errno
import fcntl
import os
from pathlib import Path
import pty
import re
import select
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import termios
import time


def main():
    executable, mode = sys.argv[1:3]
    layout = sys.argv[3] if len(sys.argv) > 3 else None
    with tempfile.TemporaryDirectory(prefix="syncterm-text-") as directory:
        root = Path(directory)
        env = dict(os.environ)
        for key in ("DISPLAY", "WAYLAND_DISPLAY", "XAUTHORITY", "XDG_RUNTIME_DIR"):
            env.pop(key, None)
        for key in ("XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_DOWNLOAD_DIR"):
            env[key] = directory
        env["TERM"] = "xterm-256color"
        env["COLORTERM"] = ""
        if layout is not None:
            env["COLORTERM"] = "truecolor"
            (root / "syncterm.ini").write_text(
                "[SyncTERM]\nConfirmClose=false\nLeftJustify="
                + str(layout == "left") + "\n")
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen(1)
            port = listener.getsockname()[1]
            if layout is not None:
                (root / "syncterm.lst").write_text(
                    "[Layout]\nConnectionType=Raw\nAddress=127.0.0.1\n"
                    f"Port={port}\nScreenMode=80x25\nNoStatus=true\nHidePopups=true\n")
            master, slave = pty.openpty()
            # Both dimensions exceed the initial 80x25 pixel-backed mode.
            fcntl.ioctl(slave, termios.TIOCSWINSZ,
                        struct.pack("HHHH", 37, 91, 0, 0))
            process = subprocess.Popen(
                [executable, "-i" + mode, "-s", "-q",
                 "-n", str(root / "syncterm.ini"),
                 "-b", str(root / "syncterm.lst")]
                + (["raw://127.0.0.1:" + str(port)] if layout is None else []),
                stdin=slave, stdout=slave, stderr=slave,
                env=env, start_new_session=True)
            os.close(slave)
            output = bytearray()
            connected = False
            peer = None
            checked_layout = layout is None
            selected = False
            exiting = False
            resized = False
            deadline = time.monotonic() + 15
            try:
                while time.monotonic() < deadline:
                    readers = [master] if connected else [master, listener]
                    ready, _, _ = select.select(readers, [], [], 0.1)
                    if listener in ready:
                        peer, _ = listener.accept()
                        connected = True
                        if layout is None:
                            peer.close()
                        else:
                            peer.sendall(b"\033[2J\033[HCTERMCENTRE")
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
                        if layout is not None and b"Layout" in output:
                            if not selected:
                                os.write(master, b"\r")
                                selected = True
                            elif checked_layout and not exiting:
                                # Layout cases finish after restoring the
                                # directory. URL cases verify normal exit.
                                process.terminate()
                                exiting = True
                        if not checked_layout and b"CTERMCENTRE" in output:
                            before = output[:output.index(b"CTERMCENTRE")]
                            positions = re.findall(rb"\033\[(\d+);(\d+)H", before)
                            expected = ((b"10" if resized else b"7"),
                                        b"1" if layout == "left" else
                                        (b"11" if resized else b"6"))
                            if not positions or positions[-1] != expected:
                                raise RuntimeError(
                                    f"-i{mode}: expected origin {expected}; "
                                    f"terminal output={bytes(output[-4096:])!r}")
                            if layout == "center" and b"48;2;0;0;168m" not in before:
                                raise RuntimeError(f"-i{mode}: missing blue surround")
                            if not resized:
                                fcntl.ioctl(master, termios.TIOCSWINSZ,
                                            struct.pack("HHHH", 43, 101, 0, 0))
                                process.send_signal(signal.SIGWINCH)
                                resized = True
                                output.clear()
                                continue
                            checked_layout = True
                            peer.close()
                            output.clear()
                    if process.poll() is not None:
                        break
                status = process.wait(timeout=max(0.1, deadline - time.monotonic()))
                if not connected or not checked_layout or (status != 0 and not exiting):
                    raise RuntimeError(
                        f"-i{mode}: connected={connected}, exit={status}, "
                        f"terminal output={bytes(output[-4096:])!r}")
            except subprocess.TimeoutExpired as error:
                raise RuntimeError(
                    f"-i{mode}: startup/shutdown timed out; "
                    f"terminal output={bytes(output[-4096:])!r}") from error
            finally:
                if peer is not None:
                    peer.close()
                if process.poll() is None:
                    process.kill()
                process.wait()
                os.close(master)


if __name__ == "__main__":
    main()
