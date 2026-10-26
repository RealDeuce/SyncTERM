#!/usr/bin/env python3
"""Compare unchanged RIP files in live v1.10a and modern CTerm sessions.

The legacy binary must have RIP/vstat and debug symbols. Run it only on a
dedicated Xvfb display. A shell connection with a raw PTY and nc carries the
file bytes. Do not use the legacy raw:// URL: it selects the Telnet provider,
which interprets 0xff.
"""

# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

import argparse
import contextlib
import os
import random
import re
import shutil
import socket
import struct
import subprocess
import tempfile
import time
from pathlib import Path

WIDTH = 640
HEIGHT = 350
PIXELS = WIDTH * HEIGHT
PALETTE_ENTRIES = 65536


class ComparisonInconclusive(RuntimeError):
    """One renderer did not provide a final pixel frame."""


def snapshot(pid: int, output: Path, debugger: str,
             palette_address: int | None,
             rip_version: int) -> tuple[bytes, bytes, int, bool]:
    frame = output / "legacy.bin"
    palette = output / "palette.bin"
    if debugger == "lldb":
        if palette_address is None:
            raise RuntimeError("LLDB palette address was not resolved")
        commands = [
            "lldb", "-b",
            "-o", "settings set frame-format "
                  "'frame #${frame.index}: ${function.name} "
                  "at ${line.file.basename}:${line.number}\\n'",
            "-o", "settings set thread-format "
                  "'thread #${thread.index}: ${thread.name}\\n'",
            "-o", f"process attach --pid {pid}",
            "-o", "expr -- rip.version",
            "-o", "expr -- screena.toprow",
            "-o", "thread select 1",
            "-o", "thread backtrace -c 12",
            "-o", f"memory read -br -o {frame} -c {PIXELS * 4} "
                  "screena.rect->data",
            "-o", f"memory read -br -o {palette} -c 262144 "
                  f"0x{palette_address:x}",
            "-o", "detach",
        ]
    else:
        commands = [
            "gdb", "-q", "-batch", "-p", str(pid),
            "-ex", "p rip.version",
            "-ex", "p screena.toprow",
            "-ex", "thread apply all bt 4",
            "-ex", f"dump binary memory {frame} screena.rect->data "
                   f"screena.rect->data+{PIXELS}",
            "-ex", f"dump binary memory {palette} "
                   "&'bitmap_con.c'::palette "
                   "((char *)&'bitmap_con.c'::palette)+262144",
        ]
    for attempt in range(3):
        frame.unlink(missing_ok=True)
        palette.unlink(missing_ok=True)
        try:
            result = subprocess.run(commands, capture_output=True,
                                    text=True, timeout=30, check=False)
        except subprocess.TimeoutExpired as error:
            if attempt == 2:
                raise ComparisonInconclusive(
                    "legacy debugger did not finish capture") from error
            time.sleep(0.5)
            continue
        if (debugger != "lldb"
                or "Couldn't dematerialize a result variable" not in result.stdout
                or attempt == 2):
            break
        time.sleep(0.5)
    top_row = re.search(r"\$1 = (-?\d+)" if debugger == "lldb"
                        else r"\$2 = (-?\d+)", result.stdout)
    version_value = 1 if rip_version == 1 else 2
    version = re.search(rf"\$0 = {version_value}\b" if debugger == "lldb"
                        else rf"\$1 = {version_value}\b", result.stdout)
    if (result.returncode != 0 or (debugger == "lldb"
            and "error:" in result.stdout) or version is None
            or top_row is None or not 0 <= int(top_row.group(1)) < HEIGHT):
        raise RuntimeError("legacy RIP framebuffer capture failed:\n"
                           + result.stdout + result.stderr)
    frame_bytes = frame.read_bytes()
    palette_bytes = palette.read_bytes()
    if (len(frame_bytes) != PIXELS * 4
            or len(palette_bytes) != PALETTE_ENTRIES * 4):
        raise RuntimeError("legacy framebuffer or palette has wrong size")
    (output / "legacy.toprow").write_text(top_row.group(1) + "\n")
    (output / "legacy.debugger-state").write_text(result.stdout)
    busy = re.search(r"(?<![A-Za-z0-9_])(?:ripper|cterm)\.c:\d+",
                     result.stdout) is not None
    return frame_bytes, palette_bytes, int(top_row.group(1)), busy


def pixel_rgb(index: int, palette: tuple[int, ...]) -> int:
    if index & 0x80000000:
        return index & 0xffffff
    return palette[index & 0xffffff] & 0xffffff


def initial_colour(frame: tuple[bytes, bytes, int, bool]) -> int:
    pixels = struct.unpack(f"<{PIXELS}I", frame[0])
    palette = struct.unpack(f"<{PALETTE_ENTRIES}I", frame[1])
    colour = pixel_rgb(pixels[0], palette)
    if any(pixel_rgb(pixel, palette) != colour for pixel in pixels):
        raise RuntimeError("legacy starting canvas is not a single colour")
    if colour not in (0x000000, 0x0000a8):
        raise RuntimeError(f"unexpected legacy starting colour: {colour:#08x}")
    return colour


def rgb24(frame: tuple[bytes, bytes, int, bool]) -> bytes:
    pixels = struct.unpack(f"<{PIXELS}I", frame[0])
    palette = struct.unpack(f"<{PALETTE_ENTRIES}I", frame[1])
    output = bytearray(PIXELS * 3)
    for y in range(HEIGHT):
        source = ((y + frame[2]) % HEIGHT) * WIDTH
        for x in range(WIDTH):
            colour = pixel_rgb(pixels[source + x], palette)
            offset = (y * WIDTH + x) * 3
            output[offset:offset + 3] = colour.to_bytes(3, "big")
    return bytes(output)


def compare(frame: tuple[bytes, bytes, int, bool], modern: bytes) -> tuple[int, tuple[int, int, int, int] | None]:
    if len(modern) != PIXELS * 3:
        raise RuntimeError("modern RGB24 capture has wrong size")
    pixels = struct.unpack(f"<{PIXELS}I", frame[0])
    palette = struct.unpack(f"<{PALETTE_ENTRIES}I", frame[1])
    differences = 0
    first = None
    for y in range(HEIGHT):
        source = ((y + frame[2]) % HEIGHT) * WIDTH
        for x in range(WIDTH):
            index = y * WIDTH + x
            old = pixel_rgb(pixels[source + x], palette)
            position = index * 3
            new = int.from_bytes(modern[position:position + 3], "big")
            if old != new:
                differences += 1
                if first is None:
                    first = (x, y, old, new)
    return differences, first


def settled_frames(history: list[tuple[bytes, bytes, int, bool]]) -> list[tuple[bytes, bytes, int, bool]] | None:
    """Return full frames once RIP drawing has stopped changing.

    An application-owned status row may update indefinitely. Use the upper
    24 text rows only to decide when to capture; compare() still checks every
    pixel of each returned frame, including that changing bottom row.
    """
    if len(history) >= 2 and history[-1][:3] == history[-2][:3]:
        return [history[-1]]
    if (len(history) >= 4
            and history[-1][:3] == history[-3][:3]
            and history[-2][:3] == history[-4][:3]):
        return [history[-1], history[-2]]
    if len(history) >= 3 and all(len(frame[0]) == PIXELS * 4
                                  for frame in history[-3:]):
        recent = history[-3:]
        if all(frame[1] == recent[-1][1] for frame in recent):
            visible = []
            for frame in recent:
                pixels, _, top_row, _ = frame
                visible.append(b"".join(
                    pixels[((y + top_row) % HEIGHT) * WIDTH * 4:
                           ((y + top_row) % HEIGHT + 1) * WIDTH * 4]
                    for y in range(HEIGHT - 14)))
            if visible[0] == visible[1] == visible[2]:
                return recent
    return None


def run_one(args: argparse.Namespace, path: Path, output: Path,
            ini: Path, bbs_list: Path,
            palette_address: int | None) -> tuple[int, tuple[int, int, int, int] | None]:
    payload = path.read_bytes()
    if args.snapshot_at is not None:
        if not 0 < args.snapshot_at <= len(payload):
            raise ValueError("snapshot offset is outside the file")
        payload = payload[:args.snapshot_at]
    if not payload:
        raise ValueError("cannot capture an empty RIP file")
    with tempfile.TemporaryDirectory(prefix="assets-", dir=output) as assets, \
            socket.socket() as listener:
        asset_root = Path(assets)
        legacy_cache = asset_root / "legacy-cache"
        modern_directory = asset_root / "modern-rip"
        legacy_cache.mkdir()
        modern_directory.mkdir()
        if args.asset:
            # v1.10a truncates the quick shell profile name to 31 bytes.
            legacy_directory = (legacy_cache / "syncterm" /
                                "stty raw -echo;exec nc 127.0.0" / "RIP")
            legacy_directory.mkdir(parents=True)
            for name, source in args.asset:
                shutil.copyfile(source, legacy_directory / name)
                shutil.copyfile(source, modern_directory / name)
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(20)
        port = listener.getsockname()[1]
        environment = os.environ.copy()
        environment["DISPLAY"] = args.display
        for variable, dirname in (("HOME", "home"),
                                  ("XDG_CACHE_HOME", "legacy-cache"),
                                  ("XDG_CONFIG_HOME", "config"),
                                  ("XDG_DATA_HOME", "data")):
            directory = asset_root / dirname
            directory.mkdir(exist_ok=True)
            environment[variable] = str(directory)
        modern_environment = os.environ.copy()
        for variable, dirname in (("HOME", "home"),
                                  ("XDG_CACHE_HOME", "cache"),
                                  ("XDG_CONFIG_HOME", "config"),
                                  ("XDG_DATA_HOME", "data")):
            directory = asset_root / ("modern-" + dirname)
            directory.mkdir()
            modern_environment[variable] = str(directory)
        modern_environment["ST_RIP_TEST_DIRECTORY"] = str(modern_directory)
        modern_environment["ST_RIP_TEST_VERSION"] = str(args.rip_version)
        command = [
            str(args.legacy), "-iXW", f"-n{ini}", f"-b{bbs_list}",
            "-c", "-q", f"shell:stty raw -echo;exec nc 127.0.0.1 {port}",
        ]
        legacy_stderr_path = output / "legacy.stderr"
        with legacy_stderr_path.open("wb") as legacy_stderr:
            process = subprocess.Popen(command, env=environment,
                                       stdout=subprocess.DEVNULL,
                                       stderr=legacy_stderr)
        try:
            connection, _ = listener.accept()
            with connection:
                time.sleep(1)
                colour = initial_colour(snapshot(process.pid, output,
                                                  args.debugger, palette_address,
                                                  args.rip_version))
                if args.split_at is not None:
                    if args.split_at >= len(payload):
                        raise ValueError("split offset is outside the file")
                    connection.sendall(payload[:args.split_at])
                    time.sleep(2)
                    connection.sendall(payload[args.split_at:])
                else:
                    connection.sendall(payload)
                history = []
                candidates = None
                deadline = time.monotonic() + args.settle_timeout
                time.sleep(args.initial_wait)
                while time.monotonic() < deadline:
                    if process.poll() is not None:
                        diagnostic = legacy_stderr_path.read_text(
                            errors="replace").strip()
                        raise ComparisonInconclusive(
                            f"legacy exited with status {process.returncode} "
                            "before capture"
                            + (f": {diagnostic[-1000:]}" if diagnostic
                               else ""))
                    frame = snapshot(process.pid, output, args.debugger,
                                     palette_address, args.rip_version)
                    if frame[3]:
                        history.clear()
                    else:
                        history.append(frame)
                        history = history[-4:]
                        candidates = settled_frames(history)
                        if candidates is not None:
                            break
                    time.sleep(args.poll_interval)
                else:
                    raise ComparisonInconclusive(
                        "legacy framebuffer did not settle before timeout")
                option = ("--rgb24-at" if colour == 0
                          else "--legacy-initial-rgb24-at")
                modern_path = output / "modern.rgb24"
                try:
                    modern = subprocess.run(
                        [str(args.modern), option, str(modern_path),
                         str(len(payload)), str(path)],
                        capture_output=True, text=True,
                        env=modern_environment,
                        timeout=args.modern_timeout,
                        check=False)
                except subprocess.TimeoutExpired as error:
                    raise ComparisonInconclusive(
                        "modern replay did not finish before timeout") from error
                if modern.returncode != 0:
                    raise RuntimeError(
                        f"modern replay exited with status {modern.returncode}:\n"
                        + modern.stdout + modern.stderr)
                modern_bytes = modern_path.read_bytes()
                comparison, closest = min(
                    ((compare(candidate, modern_bytes), candidate)
                     for candidate in candidates),
                    key=lambda result: result[0][0])
                if args.output is not None:
                    (output / "legacy.rgb24").write_bytes(rgb24(closest))
                return comparison
        finally:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--legacy", type=Path, required=True)
    parser.add_argument("--modern", type=Path, required=True)
    parser.add_argument("--display", required=True,
                        help="dedicated, already-running Xvfb display")
    parser.add_argument("--debugger", choices=("gdb", "lldb"), default="gdb")
    parser.add_argument("--rip-version", type=int, choices=(1, 3), default=1)
    parser.add_argument("--corpus", type=Path)
    parser.add_argument("--sample", type=int)
    parser.add_argument("--seed", type=int, default=154)
    parser.add_argument("--max-bytes", type=int)
    directory_filter = parser.add_mutually_exclusive_group()
    directory_filter.add_argument("--exclude-broken", action="store_true")
    directory_filter.add_argument("--only-broken", action="store_true")
    parser.add_argument("--lowercase-extension-only", action="store_true",
                        help="reproduce earlier samples that omitted .RIP files")
    parser.add_argument("--start", type=int, default=0)
    parser.add_argument("--limit-files", type=int)
    parser.add_argument("--keep-going", action="store_true",
                        help="report every differing file in the selected slice")
    parser.add_argument("--snapshot-at", type=int)
    parser.add_argument("--split-at", type=int)
    parser.add_argument("--quiet-zero", action="store_true",
                        help="print only mismatches and the final count")
    parser.add_argument("--settle-timeout", type=float, default=180,
                        help="seconds to wait for legacy pixels to settle")
    parser.add_argument("--initial-wait", type=float, default=2,
                        help="seconds to wait after sending fixture bytes")
    parser.add_argument("--poll-interval", type=float, default=0.75,
                        help="seconds between legacy framebuffer captures")
    parser.add_argument("--modern-timeout", type=float, default=300,
                        help="seconds allowed for modern replay")
    parser.add_argument("--output", type=Path,
                        help="retain last capture for investigating a mismatch")
    parser.add_argument("--asset", nargs=2, action="append", default=[],
                        metavar=("NAME", "SOURCE"),
                        help="seed the same named RIP asset in both fresh caches")
    parser.add_argument("files", nargs="*", type=Path)
    args = parser.parse_args()
    palette_address = None
    if args.debugger == "lldb":
        symbols = subprocess.run(["nm", "-an", str(args.legacy)],
                                 capture_output=True, text=True, check=True)
        matches = re.findall(r"^([0-9a-fA-F]+) b palette$",
                             symbols.stdout, re.MULTILINE)
        if len(matches) != 1:
            parser.error("expected one static legacy bitmap palette symbol")
        palette_address = int(matches[0], 16)
    if args.settle_timeout <= 0 or args.modern_timeout <= 0 \
            or args.poll_interval <= 0 \
            or args.initial_wait < 0:
        parser.error("timeouts and poll interval must be positive; "
                     "initial wait must be nonnegative")
    if args.split_at is not None and args.split_at <= 0:
        parser.error("split offset must be positive")
    for name, source in args.asset:
        if name in ("", ".", "..") or "/" in name or "\\" in name:
            parser.error(f"invalid asset name: {name}")
        if not Path(source).is_file():
            parser.error(f"asset source is not a file: {source}")
    if args.corpus is not None:
        if args.files:
            parser.error("--corpus cannot be combined with file arguments")
        files = sorted(path for path in args.corpus.rglob("*")
                       if path.is_file() and path.suffix.lower() == ".rip"
                       and (not args.lowercase_extension_only
                            or path.suffix == ".rip"))
    else:
        files = args.files
    if args.exclude_broken:
        files = [path for path in files if "broken" not in path.parts]
    if args.only_broken:
        files = [path for path in files if "broken" in path.parts]
    if args.max_bytes is not None:
        if args.max_bytes <= 0:
            parser.error("--max-bytes must be positive")
        files = [path for path in files if path.stat().st_size <= args.max_bytes]
    if args.sample is not None:
        if args.sample <= 0 or args.sample > len(files):
            parser.error("sample size is outside the available file count")
        files = random.Random(args.seed).sample(files, args.sample)
    if (args.start < 0 or args.start >= len(files)
            or (args.limit_files is not None and args.limit_files <= 0)):
        parser.error("invalid start or limit")
    files = files[args.start:]
    if args.limit_files is not None:
        files = files[:args.limit_files]
    if not files:
        parser.error("no RIP files selected")
    if args.output is None:
        directory = tempfile.TemporaryDirectory(prefix="st-rip-v154-")
    else:
        args.output.mkdir(parents=True, exist_ok=True)
        directory = contextlib.nullcontext(str(args.output))
    with directory as temporary:
        output = Path(temporary).resolve()
        ini = output / "legacy.ini"
        bbs_list = output / "legacy.list"
        ini.write_text("[SyncTERM]\nScalingFactor=1\nScreenMode=EGA80x25\n")
        bbs_list.write_text(f"RIP=RIPv{args.rip_version}\n")
        mismatches = 0
        inconclusive = 0
        errors = 0
        for index, path in enumerate(files, args.start + 1):
            try:
                differences, first = run_one(args, path, output, ini,
                                             bbs_list, palette_address)
            except ComparisonInconclusive as error:
                inconclusive += 1
                print(f"{index}: {path}: INCONCLUSIVE ({error})",
                      flush=True)
                if not args.keep_going:
                    return 1
                continue
            except RuntimeError as error:
                errors += 1
                print(f"{index}: {path}: ERROR ({error})", flush=True)
                if not args.keep_going:
                    raise
                continue
            if differences or not args.quiet_zero:
                print(f"{index}: {path}: {differences} differing pixels"
                      + (f" first={first}" if first is not None else ""),
                      flush=True)
            if differences:
                mismatches += 1
                payload = path.read_bytes()
                print(f"  line endings: CR={payload.count(bytes([13]))} "
                      f"LF={payload.count(bytes([10]))}", flush=True)
                if not args.keep_going:
                    return 1
        print(f"{len(files)} files checked, {mismatches} with differing pixels, "
              f"{inconclusive} inconclusive, {errors} errors", flush=True)
    return 1 if mismatches or inconclusive or errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
