#!/usr/bin/env python3
"""End-to-end tests of the normal SyncTERM executable's testing bitmap driver."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import binascii
import os
from pathlib import Path
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import queue
import threading

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "testing"))
from syncterm_control import ALT, SHIFT, Client, CommandError, Snapshot
from syncterm_fixture import application, environment, fixture, click_text, receive


def basic(executable):
    with fixture(executable, settings="[Unrelated]\nPreserve=yes\n") as (client, root):
        first = client.wait_text("Build Options")
        assert client.hello[1] & (1 << 12)
        assert first.application["session_active"] == 0
        if os.name == "posix":
            # Executable policy must cover process-directed signals too.
            os.kill(client.process.pid, signal.SIGPIPE)
            assert client.capture().application["session_active"] == 0
        original = bytes(first.primary)
        client.grid_resize(90, 30)
        client.wait_for(lambda snap: (snap.metadata["columns"], snap.metadata["rows"]) == (90, 30))
        client.grid_resize(80, 25)
        client.wait_for(lambda snap: (snap.metadata["columns"], snap.metadata["rows"]) == (80, 25))
        client.key("enter", ALT)
        assert client.capture().metadata["fullscreen"] == 1
        client.key("enter", ALT)
        assert client.capture().metadata["fullscreen"] == 0
        client.clipboard("private \U0001f4b0 clipboard")
        assert client.capture().clipboard == "private \U0001f4b0 clipboard"
        client.physical_key(29, True)  # focus loss must release held keys normally
        client.focus(False)
        client.focus(True)
        client.wait_for(lambda snap: snap.metadata["events_dequeued"] >= first.metadata["events_dequeued"] + 6)
        client.key("right", ALT)
        enlarged = client.wait_for(lambda s: s.metadata["scaling_millionths"] == 2000000)
        assert enlarged.metadata["output_width"] == first.metadata["output_width"] * 2
        client.resize(777, 611)
        resized = client.capture()
        assert (resized.metadata["output_width"], resized.metadata["output_height"]) == (777, 611)
        client.key("left", ALT)
        client.key("right", ALT)
        client.wait_for(lambda s: s.metadata["scaling_millionths"] == 2000000)
        assert bytes(first.primary) == original  # client capture owns storage too
        click_text(client, client.capture(), "Build Options")
        click_text(client, client.capture(), "Build Options")
        client.wait_text("SyncTERM Build Options")
        client.key("end")
        options = client.wait_text("Testing bitmap backend")
        assert any("Testing bitmap backend" in line and "√" in line for line in options.lines)
        client.key("escape")
        client.wait_text("Directory: Name")
        client.close()
        assert "ScalingFactor=2" in (root / "syncterm.ini").read_text()
        assert "[Unrelated]" in (root / "syncterm.ini").read_text()
        assert "Preserve=yes" in (root / "syncterm.ini").read_text()
        with application(executable, root) as restarted:
            frame = restarted.wait_text("Directory: Name")
            assert frame.metadata["scaling_millionths"] == 2000000


def session(executable, utf8):
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(15)
        port = listener.getsockname()[1]
        entries = ("[Capture]\nConnectionType=Raw\nAddress=127.0.0.1\n"
                   f"Port={port}\nScreenMode=80x25\nNoStatus=true\nHidePopups=true\n"
                   f"Encoding={'UTF-8' if utf8 else 'CP437'}\nRIP=Off\nForceLCF=true\n")
        with fixture(executable, entries=entries) as (client, root):
            client.wait_text("Capture")
            client.key("enter")
            with listener.accept()[0] as peer:
                peer.sendall(b"\x1b[2J\x1b[H\x1b[?7h" + b"A" * 80 + b"\x1b[0m")
                full = client.wait_for(lambda s: s.application.get("cterm.last_column_flag.set") == 1 and s.lines[0] == "A" * 80)
                assert full.application["connected"] == 1
                peer.sendall(b"\r\nNEXT\x1b[0m")
                next_frame = client.wait_text("NEXT")
                assert next_frame.application["cterm.last_column_flag.set"] == 0
                sample = "\U0001f579\ufe0f \U0001f4b0" if utf8 else "│ß"
                data = sample.encode("utf-8") if utf8 else b"\xb3\xe1"
                peer.sendall(b"\r\n" + data + b"\x1b[0m")
                seen = client.wait_text("💰" if utf8 else sample)
                assert any(seen.cell_text(cell) == ("💰" if utf8 else "│") for cell in seen.cells)
                # Private clipboard still uses the actual connection paste path.
                client.clipboard("PASTED-CONTROL")
                client.key("insert", SHIFT)
                receive(peer, b"PASTED-CONTROL")
                client.text("WIRE-INPUT")
                receive(peer, b"WIRE-INPUT")
                # Browser opening is observable through the driver's normal host op.
                url = b"https://example.invalid/control"
                peer.sendall(b"\r\n\x1b]8;;" + url + b"\x1b\\LINK\x1b]8;;\x1b\\\x1b[0m")
                link = client.wait_text("LINK")
                click_text(client, link, "LINK")
                opened = client.wait_for(lambda s: s.metadata["url_serial"] != 0)
                assert opened.url == url.decode()
                # Capture both blink phases via a driver-local timestamp override.
                peer.sendall(b"\x1b[4 q\x1b[?25h\x1b[24;1H\x1b[0m")
                solid = client.wait_for(lambda s: s.metadata["cursor_visible"] == 1 and s.metadata["cursor_blink"] == 0)
                client.presentation_time(1000)
                a = client.capture()
                client.presentation_time(1700)
                b = client.capture()
                assert a.rgb == b.rgb, "Solid cursor changed between presentation phases"
                peer.sendall(b"\x1b[3 q")
                client.wait_for(lambda snap: snap.metadata["cursor_blink"] == 1)
                client.presentation_time(1000)
                blink_a = client.capture()
                client.presentation_time(1133)
                blink_b = client.capture()
                assert blink_a.cells == blink_b.cells and blink_a.primary == blink_b.primary
                assert blink_a.alternate == blink_b.alternate and blink_a.rgb != blink_b.rgb
                client.presentation_time()
                client.key(ord('h'), ALT)
            client.wait_for(lambda s: s.application.get("session_active") == 0)
            client.wait_text("Directory: Name")
            assert full.lines[0] == "A" * 80  # valid after session teardown


def frame(serial, op, payload=b""):
    return struct.pack("<IIIII", 16 + len(payload), 1, serial, op, 0) + payload


def raw_process(executable, root, extra=()):
    return subprocess.Popen([executable, "--test-control", "--test-directory", str(root), *extra],
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, env=environment(),
                            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))


def read_exact(process, size, timeout=15):
    result = queue.Queue()
    def read():
        try:
            result.put(process.stdout.read(size))
        except BaseException as error:
            result.put(error)
    thread = threading.Thread(target=read, daemon=True)
    thread.start()
    try:
        data = result.get(timeout=timeout)
    except queue.Empty:
        process.kill()
        process.wait(timeout=timeout)
        raise TimeoutError("Raw control read timed out")
    finally:
        thread.join(timeout=timeout)
    if isinstance(data, BaseException):
        raise data
    if len(data) != size:
        raise EOFError(f"Expected {size} bytes; got {len(data)}")
    return data


def protocol(executable):
    with fixture(executable) as (client, root):
        for op, payload, error in ((999, b"", -18), (2, b"unexpected", -1),
                                   (4, struct.pack("<II", 0, 1), -12),
                                   (4, struct.pack("<II", 4096, 4096), -12),
                                   (5, b"\xff", -2),
                                   (3, struct.pack("<9I", 2, 0xd800, *([0] * 7)), -2)):
            try:
                client.request(op, payload)
            except CommandError as exception:
                assert exception.status == error
            else:
                raise AssertionError(f"Command {op} unexpectedly succeeded")
        assert isinstance(client.capture(), Snapshot)
    # Exercise transport fragmentation and shutdown while its reader is idle.
    with tempfile.TemporaryDirectory(prefix="syncterm-pipes-") as directory:
        root = Path(directory)
        (root / "syncterm.ini").write_text("[SyncTERM]\nConfirmClose=false\n")
        for malformed in (b"\x00\x00\x00\x00", struct.pack("<I", 1024 * 1024 + 1), frame(1, 1)[:-1]):
            process = raw_process(executable, root)
            try:
                process.communicate(malformed, timeout=15)
                assert process.returncode == 0
            finally:
                if process.poll() is None:
                    process.kill()
                process.wait()
        process = raw_process(executable, root)
        try:
            for byte in frame(42, 1):
                process.stdin.write(bytes([byte]))
                process.stdin.flush()
            header = read_exact(process, 4)
            size, = struct.unpack("<I", header)
            response = read_exact(process, size)
            assert struct.unpack_from("<IIII", response) == (1, 42, 1, 0)
            # Closing both control ends while a large frame is being written
            # must also close the app, rather than killing it with SIGPIPE.
            process.stdin.write(frame(43, 2))
            process.stdin.flush()
            size, = struct.unpack("<I", read_exact(process, 4))
            assert size > 1024 * 1024  # ensure the snapshot writer has started
            process.stdout.close()
            process.stdin.close()
            process.wait(timeout=15)
            assert process.returncode == 0, (process.returncode, process.stderr.read().decode())
        finally:
            if process.poll() is None:
                process.kill()
            process.wait()
            for stream in (process.stdin, process.stdout, process.stderr):
                if not stream.closed:
                    stream.close()


        # Startup must reject non-pipe input, without opening a desktop window.
        rejected = subprocess.run([executable, "--test-control", "--test-directory", str(root)],
                                  stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, env=environment(), timeout=15,
                                  creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        assert rejected.returncode != 0 and b"requires redirected pipe" in rejected.stderr
        # Natural application exit must join a writer blocked on an unread
        # snapshot, even when the parent keeps both pipe endpoints open.
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen(1)
            listener.settimeout(15)
            process = raw_process(executable, root, ["-q", f"raw://127.0.0.1:{listener.getsockname()[1]}"])
            try:
                with listener.accept()[0] as peer:
                    process.stdin.write(frame(1, 1)); process.stdin.flush()
                    size, = struct.unpack("<I", read_exact(process, 4))
                    read_exact(process, size)
                    process.stdin.write(frame(2, 2)); process.stdin.flush()
                    size, = struct.unpack("<I", read_exact(process, 4))
                    assert size > 1024 * 1024  # far larger than the pipe buffer
                process.wait(timeout=15)
                assert process.returncode == 0, process.stderr.read().decode()
            finally:
                if process.poll() is None:
                    process.kill()
                process.wait()
                for stream in (process.stdin, process.stdout, process.stderr):
                    stream.close()


def transfer(executable):
    # Real receivers wait for input: inspection/cancellation needs no timer race.
    for kind, action in (("xmodem-crc", "cancel"), ("xmodem-g", "cancel"),
                         ("xmodem-g", "success"), ("xmodem-g", "corrupt")):
        expected = "null" if action == "success" else ("IOErr.protocol" if action == "corrupt" else "IOErr.cancelled")
        check = 'result == null' if action == "success" else f'result != null && result.code == {expected}'
        script = ('import "syncterm" for Transfer, IOErr\nimport "transfer_app" for TransferApp\n'
                  'Fiber.new {\n'
                  f'  var error = Transfer.download("{kind}", "capture.bin")\n'
                  '  if (error != null) Fiber.abort(error)\n'
                  '  var app = TransferApp.new("Control transfer")\n'
                  '  app.waitTimeoutMs = 0\n  app.run()\n'
                  '  var result = Transfer.result\n'
                  f'  if (!({check})) Fiber.abort("Wrong transfer result")\n'
                  '  Transfer.endSession()\n}.call()\n')
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen(1)
            listener.settimeout(15)
            port = listener.getsockname()[1]
            entries = f"[Transfer]\nConnectionType=Raw\nAddress=127.0.0.1\nPort={port}\nHidePopups=true\nNoStatus=true\n"
            with fixture(executable, entries=entries, script=script,
                         settings="AudioModes=0\n") as (client, root):
                client.wait_text("Transfer")
                client.key("enter")
                with listener.accept()[0] as peer:
                    receive(peer, b"G" if kind == "xmodem-g" else b"C")
                    payload = b"X" * 128
                    crc = binascii.crc_hqx(payload, 0) ^ (1 if action == "corrupt" else 0)
                    peer.sendall(b"\x01\x01\xfe" + payload + struct.pack(">H", crc))
                    if action != "corrupt":
                        if kind == "xmodem-crc":
                            receive(peer, b"\x06")
                        progress = client.wait_for(lambda s: s.application.get("transfer.active") == 1
                                                   and s.application.get("transfer.bytes_current") == 128)
                        assert progress.application["transfer.protocol"] == (11 if kind == "xmodem-g" else 5)
                        assert progress.application["transfer.done"] == 0
                        assert "Control transfer" in progress.text
                        if action == "cancel":
                            client.key("escape")
                        else:
                            peer.sendall(b"\x04")
                            receive(peer, b"\x06")
                    client.wait_for(lambda s: s.application.get("transfer.active") == 0)
                    if action == "success":
                        assert (root / "downloads" / "capture.bin").read_bytes() == payload
                    peer.sendall(b"TRANSFER-HANDOFF\x1b[0m")
                    client.wait_text("TRANSFER-HANDOFF")
                    client.key(ord('h'), ALT)
                client.wait_text("Directory: Name")


if __name__ == "__main__":
    executable, variant = sys.argv[1:3]
    if variant == "basic":
        basic(executable)
    elif variant in ("cp437", "utf8"):
        session(executable, variant == "utf8")
    elif variant == "protocol":
        protocol(executable)
    elif variant == "transfer":
        transfer(executable)
    else:
        raise ValueError(variant)
