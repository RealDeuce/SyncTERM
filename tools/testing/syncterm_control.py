#!/usr/bin/env python3
"""Drive the regular SyncTERM executable built with ST_CONSOLE_TEST=ON.

The wire is little endian. Each message is a u32 byte length followed by
(version, request_id, opcode, status), all u32. Requests have zero status.
Snapshot payloads contain (tag, byte_length, bytes) sections. Unknown sections
can be skipped; cells retain the documented st_console_cell field meanings.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import array
import dataclasses
import json
import pathlib
import queue
import struct
import subprocess
import sys
import threading
import time

VERSION = 1
MAX_MESSAGE = 128 * 1024 * 1024
KEYS = dict(zip(("up", "down", "left", "right", "insert", "delete", "home", "end",
                 "page_up", "page_down", *(f"f{i}" for i in range(1, 13))), range(256, 278)))
KEYS.update(enter=13, escape=27, tab=9, backspace=8)
SHIFT, CONTROL, ALT, GUI = 1, 2, 4, 8
COMMAND = GUI if sys.platform == "darwin" else ALT


class ProtocolError(RuntimeError):
    pass


class CommandError(ProtocolError):
    def __init__(self, opcode, status):
        self.opcode, self.status = opcode, status
        super().__init__(f"SyncTERM control command {opcode} failed: {status}")


class Reader:
    def __init__(self, data):
        self.data, self.offset = memoryview(data), 0

    def take(self, size):
        if size < 0 or size > len(self.data) - self.offset:
            raise ProtocolError("Truncated control data")
        result = self.data[self.offset:self.offset + size]
        self.offset += size
        return result

    def u32(self):
        return struct.unpack("<I", self.take(4))[0]

    def string(self):
        return bytes(self.take(self.u32())).decode("utf-8")

    def fields(self):
        result = {}
        for _ in range(self.u32()):
            name, kind = self.string(), self.u32()
            if kind not in (0, 1):
                raise ProtocolError("Unknown field type")
            result[name] = self.string() if kind else struct.unpack("<q", self.take(8))[0]
        self.end()
        return result

    def end(self):
        if self.offset != len(self.data):
            raise ProtocolError("Unexpected trailing control data")


@dataclasses.dataclass(frozen=True)
class Cell:
    glyph: int
    foreground: int
    background: int
    flags: int
    font: int
    glyph_font: int
    hyperlink: int
    reserved: int


def words(data):
    if len(data) % 4:
        raise ProtocolError("Misaligned pixel section")
    result = array.array("I")
    result.frombytes(data)
    if sys.byteorder != "little":
        result.byteswap()
    return result


class Snapshot:
    def __init__(self, data):
        reader, sections = Reader(data), {}
        while reader.offset < len(reader.data):
            tag, size = reader.u32(), reader.u32()
            if tag in sections:
                raise ProtocolError("Duplicate snapshot section")
            sections[tag] = reader.take(size)
        if not set(range(1, 10)).issubset(sections):
            raise ProtocolError("Incomplete snapshot")
        self.metadata = Reader(sections[1]).fields()
        self.application = Reader(sections[8]).fields()
        self.cells = tuple(Cell(*values) for values in struct.iter_unpack("<IIIIHHHH", sections[2]))
        self.palette, self.primary, self.alternate, self.rgb = (words(sections[i]) for i in (3, 4, 5, 6))
        glyphs = Reader(sections[7])
        self.glyphs = {}
        for _ in range(glyphs.u32()):
            font, glyph = glyphs.u32(), glyphs.u32()
            self.glyphs[font, glyph] = glyphs.string()
        glyphs.end()
        host = Reader(sections[9])
        self.title, self.clipboard, self.url = host.string(), host.string(), host.string()
        host.end()
        m = self.metadata
        if (len(self.cells) != m["columns"] * m["rows"] or len(self.palette) != 65536
                or len(self.primary) != m["plane_width"] * m["plane_height"]
                or len(self.alternate) != len(self.primary)
                or len(self.rgb) != m["output_width"] * m["output_height"]):
            raise ProtocolError("Snapshot dimensions disagree with its buffers")

    def cell_text(self, cell):
        return self.glyphs.get((cell.glyph_font, cell.glyph), "")

    @property
    def lines(self):
        width = self.metadata["columns"]
        return ["".join(self.cell_text(c) for c in self.cells[row:row + width] if not c.flags & (1 << 19))
                for row in range(0, len(self.cells), width)]

    @property
    def text(self):
        return "\n".join(self.lines)

    def save_image(self, path):
        """Write a portable PPM of the exact composed drawable."""
        m = self.metadata
        rgb = bytes(component for pixel in self.rgb for component in
                    ((pixel >> 16) & 255, (pixel >> 8) & 255, pixel & 255))
        pathlib.Path(path).write_bytes(f'P6\n{m["output_width"]} {m["output_height"]}\n255\n'.encode() + rgb)

    def diagnostic(self):
        return dict(metadata=self.metadata, application=self.application, title=self.title,
                    clipboard=self.clipboard, url=self.url, lines=self.lines)


class Client:
    """One request at a time; transport readers work on Windows and POSIX pipes."""
    def __init__(self, executable, arguments=(), *, env=None, timeout=10):
        self.timeout, self.serial, self.last_snapshot = timeout, 0, None
        self.responses, self.stderr = queue.Queue(), bytearray()
        self.writes = queue.Queue()
        self.process = subprocess.Popen([str(executable), "--test-control", *map(str, arguments)],
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE, env=env, bufsize=0,
                                        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        self.threads = [threading.Thread(target=self._write_requests, daemon=True),
                        threading.Thread(target=self._read_responses, daemon=True),
                        threading.Thread(target=self._read_stderr, daemon=True)]
        for thread in self.threads:
            thread.start()
        try:
            self.hello = struct.unpack("<III", self.request(1))
        except BaseException:
            self.terminate()
            raise

    def _write_requests(self):
        while True:
            item = self.writes.get()
            if item is None:
                return
            data, completed = item
            try:
                view = memoryview(data)
                while view:
                    count = self.process.stdin.write(view)
                    if not count:
                        raise EOFError("SyncTERM input pipe closed")
                    view = view[count:]
                completed.put(None)
            except BaseException as error:
                completed.put(error)

    def _read_exact(self, size):
        # Large snapshots arrive in short pipe reads. Fill their final buffer
        # directly instead of allocating and extending for every fragment.
        result = bytearray(size)
        view, offset = memoryview(result), 0
        while offset < size:
            count = self.process.stdout.readinto(view[offset:])
            if not count:
                raise EOFError("SyncTERM control pipe closed")
            offset += count
        return result

    def _read_responses(self):
        try:
            while True:
                size, = struct.unpack("<I", self._read_exact(4))
                if size < 16 or size > MAX_MESSAGE:
                    raise ProtocolError(f"Invalid response length {size}")
                self.responses.put(bytes(self._read_exact(size)))
        except BaseException as error:
            self.responses.put(error)

    def _read_stderr(self):
        while True:
            data = self.process.stderr.read(4096)
            if not data:
                return
            self.stderr.extend(data)
            if len(self.stderr) > 65536:
                del self.stderr[:-65536]

    def diagnostics(self):
        result = self.stderr.decode("utf-8", "replace")
        if self.last_snapshot is not None:
            result += "\n" + json.dumps(self.last_snapshot.diagnostic(), ensure_ascii=False)
        return result

    def request(self, opcode, payload=b"", *, timeout=None):
        if len(payload) > 1024 * 1024 - 16:
            raise ValueError("Control request exceeds 1 MiB")
        deadline = time.monotonic() + (self.timeout if timeout is None else timeout)
        self.serial += 1
        data = struct.pack("<IIIII", 16 + len(payload), VERSION, self.serial, opcode, 0) + payload
        completed = queue.Queue(maxsize=1)
        self.writes.put((data, completed))
        try:
            error = completed.get(timeout=max(0, deadline - time.monotonic()))
            if error is not None:
                raise ProtocolError(f"Control write failed: {error}\n{self.diagnostics()}") from error
            response = self.responses.get(timeout=max(0, deadline - time.monotonic()))
        except queue.Empty as error:
            self.terminate()
            raise TimeoutError(f"SyncTERM command {opcode} timed out\n{self.diagnostics()}") from error
        if isinstance(response, BaseException):
            raise ProtocolError(f"{response}\n{self.diagnostics()}") from response
        version, serial, received_op, status = struct.unpack_from("<IIIi", response)
        if (version, serial, received_op) != (VERSION, self.serial, opcode):
            raise ProtocolError("Mismatched response header")
        if status:
            raise CommandError(opcode, status)
        return response[16:]

    def capture(self, *, timeout=None):
        self.last_snapshot = Snapshot(self.request(2, timeout=timeout))
        return self.last_snapshot

    def wait_for(self, predicate, *, timeout=None):
        deadline = time.monotonic() + (self.timeout if timeout is None else timeout)
        while time.monotonic() < deadline:
            snapshot = self.capture(timeout=max(0.001, deadline - time.monotonic()))
            if predicate(snapshot):
                return snapshot
        raise TimeoutError(f"SyncTERM snapshot predicate timed out\n{self.diagnostics()}")

    def wait_text(self, text, **kwargs):
        return self.wait_for(lambda snapshot: text in snapshot.text, **kwargs)

    def event(self, kind, *values):
        if len(values) > 8:
            raise ValueError("An event has at most eight data words")
        return self.request(3, struct.pack("<9I", kind, *(v & 0xffffffff for v in values), *([0] * (8 - len(values)))))

    def key(self, key, modifiers=0):
        return self.event(0, KEYS[key] if isinstance(key, str) else key, modifiers)

    def physical_key(self, evdev, pressed, modifiers=0, repeat=False):
        return self.event(1, evdev, modifiers, int(pressed), int(repeat))

    def command_key(self, evdev):
        modifier_key = 125 if COMMAND == GUI else 56
        self.physical_key(modifier_key, True, COMMAND)
        self.physical_key(evdev, True, COMMAND)
        self.physical_key(evdev, False, COMMAND)
        self.physical_key(modifier_key, False)

    def text(self, text, modifiers=0):
        for character in text:
            self.event(2, ord(character), modifiers)

    def mouse(self, x, y, *, action=0, buttons=0, modifiers=0, wheel_x=0, wheel_y=0):
        return self.event(4, action, x, y, buttons, modifiers, wheel_x, wheel_y)

    def resize(self, width, height):
        return self.request(4, struct.pack("<II", width, height))

    def grid_resize(self, columns, rows):
        return self.event(5, columns, rows)

    def focus(self, focused):
        return self.event(6, int(focused))

    def clipboard(self, text):
        return self.request(5, text.encode("utf-8"))

    def presentation_time(self, milliseconds=None):
        return self.request(6, struct.pack("<II", int(milliseconds is not None), milliseconds or 0))

    def close(self):
        if self.process.poll() is None:
            try:
                self.event(7)
            except (ProtocolError, BrokenPipeError):
                pass  # A normal close can finish before its queue acknowledgment.
        self.process.wait(timeout=self.timeout)
        self._finish_readers()

    def _finish_readers(self):
        self.writes.put(None)
        for thread in self.threads:
            thread.join(timeout=self.timeout)
        for pipe in (self.process.stdin, self.process.stdout, self.process.stderr):
            pipe.close()

    def terminate(self):
        if self.process.poll() is None:
            self.process.kill()
        self.process.wait(timeout=self.timeout)
        self._finish_readers()

    def __enter__(self):
        return self

    def __exit__(self, kind, value, traceback):
        if kind is None:
            try:
                self.close()
            except BaseException:
                self.terminate()
                raise
        else:
            self.terminate()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", help="Write the composed drawable as PPM")
    parser.add_argument("--wait-text", help="Wait until this text is visible")
    parser.add_argument("executable")
    parser.add_argument("arguments", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    with Client(args.executable, args.arguments) as client:
        snapshot = client.wait_text(args.wait_text) if args.wait_text else client.capture()
        if args.image:
            snapshot.save_image(args.image)
        print(json.dumps(snapshot.diagnostic(), ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
