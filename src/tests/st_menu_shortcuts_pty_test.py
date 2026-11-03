#!/usr/bin/env python3
"""Drive the real ANSI/curses BBS menu through an isolated pseudo-terminal."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import errno
import fcntl
import os
from pathlib import Path
import pty
import re
import select
import signal
import struct
import sys
import tempfile
import termios
import time

ESCAPES = re.compile(rb"\x1b(?:\[[0-?]*[ -/]*[@-~]|\][^\x07\x1b]*(?:\x07|\x1b\\)|.)")


def main():
    assert ESCAPES.sub(b"", b"\x1b]4;1;rgb:00/00/aa\x1b\\Directory: Name"
                       b"\x1b]4;2;rgb:aa/00/00\x1b\\") == b"Directory: Name"
    executable, backend = str(Path(sys.argv[1]).resolve()), sys.argv[2]
    with tempfile.TemporaryDirectory(prefix="syncterm-shortcut-pty-") as directory:
        root = Path(directory)
        env = dict(os.environ)
        for name in ("DISPLAY", "WAYLAND_DISPLAY", "XAUTHORITY", "XDG_RUNTIME_DIR"):
            env.pop(name, None)
        for name, leaf in (("XDG_CONFIG_HOME", "config"), ("XDG_DATA_HOME", "data"),
                           ("XDG_CACHE_HOME", "cache"), ("XDG_DOWNLOAD_DIR", "downloads")):
            env[name] = str(root / leaf)
        env["TERM"] = "xterm-256color"
        config = root / "config" / "syncterm"
        config.mkdir(parents=True)
        (config / "syncterm.ini").write_text("[SyncTERM]\nAudioModes=0\nConfirmClose=false\n")
        (config / "syncterm.lst").write_text("[Alpha]\n[Target One]\n[Target Two]\n")
        scripts = root / "data" / "syncterm" / "scripts" / "auto" / "menu"
        scripts.mkdir(parents=True)
        (scripts / "zzz_shortcut.wren").write_text('''
import "syncterm" for Host, Mod
import "main_menu" for MainMenu
MainMenu.registerCommand(121, Mod.control, "Ctrl-Y", "Capture selection", Fn.new {|app, connected|
  Host.print("PTY-SELECTION " + app.root.children[1].children[0].selectedItem)
})
''')
        pid, master = pty.fork()
        if pid == 0:
            fcntl.ioctl(0, termios.TIOCSWINSZ, struct.pack("HHHH", 25, 80, 0, 0))
            os.execve(executable, [executable, "-q", "-i" + backend], env)
        pending = bytearray()
        transcript = bytearray()
        reaped = False

        def wait(text):
            needle = text.encode()
            deadline = time.monotonic() + 10
            while needle not in ESCAPES.sub(b"", pending):
                remaining = deadline - time.monotonic()
                if remaining <= 0 or not select.select([master], [], [], remaining)[0]:
                    raise AssertionError(f"{backend}: waiting for {text!r}\n{transcript[-6000:]!r}")
                try:
                    data = os.read(master, 65536)
                except OSError as error:
                    if error.errno != errno.EIO:
                        raise
                    data = b""
                if not data:
                    raise AssertionError(f"{backend}: exited waiting for {text!r}\n{transcript[-6000:]!r}")
                pending.extend(data)
                transcript.extend(data)
            pending.clear()

        def send(data):
            pending.clear()
            assert os.write(master, data) == len(data)

        try:
            wait("Directory: Name")
            send(b"\x06")
            wait("Find")
            send(b"Target\r")
            send(b"\x19")
            wait("PTY-SELECTION Target One")
            send(b"\x07\x19")
            wait("PTY-SELECTION Target Two")
            send(b"\x07\x19")
            wait("PTY-SELECTION Target One")
            send(b"\x13")
            wait("Sort Profiles")
            send(b"\x1b")
            wait("Directory: Name")
            send(b"\x04")
            wait("Quick Connect")
            send(b"\x1b")
            wait("Directory: Name")
            # Byte-stream terminals have no distinct Ctrl+` physical event.
            # The shipped console shortcut in these backends is Ctrl+Q.
            send(b"\x11")
            wait("(main_menu) wren>")
            send(b"\x1b")
            wait("Directory: Name")
            send(b"\x1b")
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                child, status = os.waitpid(pid, os.WNOHANG)
                if child:
                    reaped = True
                    assert os.waitstatus_to_exitcode(status) == 0, status
                    break
                if select.select([master], [], [], 0.05)[0]:
                    try:
                        transcript.extend(os.read(master, 65536))
                    except OSError as error:
                        if error.errno != errno.EIO:
                            raise
            assert reaped, f"menu did not exit\n{transcript[-6000:]!r}"
        finally:
            if not reaped:
                os.kill(pid, signal.SIGKILL)
                os.waitpid(pid, 0)
            os.close(master)
    print(f"{backend}: Ctrl+F/G/S/D and Ctrl+Q console fallback passed")


if __name__ == "__main__":
    main()
