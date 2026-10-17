#!/usr/bin/env python3
"""Compare self-authored, unchanged APC streams with live SyncTERM v1.10a.

Uses an explicitly supplied private Xvfb, a raw shell PTY (not raw://),
and read-only debugger captures. No legacy source or artwork is copied.
Synth patches are inspected before playback; this test makes no sound.
"""
# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import re
import socket
import struct
import subprocess
import tempfile

from compare_legacy_rip_live import rgb24, pixel_rgb


def apc(text: str) -> bytes:
    return b"\x1b_" + text.encode("ascii") + b"\x1b\\"


def blob(prefix: str, data: bytes) -> bytes:
    return apc(prefix + ";" + base64.b64encode(data).decode("ascii"))


def cases():
    ppm = b"P6\n3 2\n255\n" + bytes([0, 0, 0, 255, 255, 255, 0, 0, 0,
                                           255, 255, 255, 0, 0, 0, 255, 255, 255])
    pbm = b"P4\n3 2\n" + bytes([0xa0, 0x40])
    yield "cache", (blob("SyncTERM:C;S;apc-reference.dat", b"abc")
                    + apc("SyncTERM:C;L;apc-reference.dat"))
    # Test default listing before creating a subdirectory: v1.10a hangs
    # in while (!feof(f)) after fread fails on an opened directory. Do not
    # reinterpret that hang as an image/state mismatch or alter one replay.
    yield "cache-patterns", (apc("SyncTERM:C;L")
                             + blob("SyncTERM:C;S;nested/z.dat", b"z")
                             + blob("SyncTERM:C;S;nested/a.dat", b"")
                             + blob("SyncTERM:C;S;.hidden.dat", b"hidden")
                             + apc("SyncTERM:C;L;nested/[az].dat")
                             + apc("SyncTERM:C;L;*.missing")
                             + apc("SyncTERM:C;L;*.dat"))
    yield "images", (blob("SyncTERM:C;LoadPPMBlob;B=0", ppm)
                     + apc("SyncTERM:P;Paste;DX=8;DY=9;ZX=3;ZY=2;FX;FY")
                     + apc("SyncTERM:P;Copy;B=1;X=7;Y=8;W=12;H=8"))
    yield "buffered-mask", (blob("SyncTERM:C;LoadPBMBlob", pbm)
                            + apc("SyncTERM:P;Paste;DX=25;DY=20;MBUF"))
    yield "inline-mask", blob("SyncTERM:C;DrawPPMBlob;DX=-1;DY=2;MW=3;MH=2;MASK=qA==", ppm)
    yield "file-images", (blob("SyncTERM:C;S;reference.ppm", ppm)
                          + blob("SyncTERM:C;S;reference.pbm", pbm)
                          + apc("SyncTERM:C;DrawPPM;DX=30;DY=40;MFILE=reference.pbm;reference.ppm")
                          + apc("SyncTERM:C;LoadPPM;B=1;reference.ppm")
                          + apc("SyncTERM:C;LoadPBM;reference.pbm"))
    yield "clipping-and-masks", (apc("SyncTERM:P;Paste;B=1;SX=1;SY=0;SW=2;SH=2;DX=638;DY=348;ZX=3;ZY=2;FX;FY")
                                 + apc("SyncTERM:P;Paste;B=1;DX=-2;DY=-1;ZX=2;ZY=3;MBUF")
                                 + apc("SyncTERM:P;Paste;B=1;DX=70;DY=10;MW=3;MH=2;MASK=qA==")
                                 + apc("SyncTERM:P;Paste;B=1;SX=1;SW=2;DX=80;DY=10;MX=1;MBUF")
                                 + apc("SyncTERM:P;Paste;B=1;DX=90;DY=10;MFILE=reference.pbm"))
    # Self-authored lossless 1x1 #123456 image, also used by pixel_image's
    # decoder test. It is not an external artwork/corpus fixture.
    jxl = bytes.fromhex(
        "ff0a001010505c080802010098024b189b9c71840338800338204ac03905"
        "01002044800810012240e4ff917bfa1e5a67575555552549921050777777"
        "7777ffffffbf556f66666606fedfbfe7bf87c69c73aeb5cfbd4992240454"
        "5555555555ffffffcfbdafbbbbbb1bfedfbfe7bf87c69c73aeb5cfbd4992"
        "2404545555555555ffffffcfbdafbbbbbb1bfedfbfe7bf87c69c73aeb5cf"
        "bd49922404545555555555ffffffcfbdafbbbbbbfb022100b81237c28d30")
    yield "jxl", (apc("SyncTERM:Q;JXL")
                  + blob("SyncTERM:C;LoadJXLBlob;B=0", jxl)
                  + blob("SyncTERM:C;DrawJXLBlob;DX=45;DY=25;ZX=4;ZY=3", jxl)
                  + blob("SyncTERM:C;S;reference.jxl", jxl)
                  + apc("SyncTERM:C;LoadJXL;B=1;reference.jxl")
                  + apc("SyncTERM:C;DrawJXL;DX=50;DY=25;reference.jxl"))
    wave_samples = struct.pack("<3h", -1000, 1000, 3000)
    wave = (b"RIFF" + struct.pack("<I", 36 + len(wave_samples)) + b"WAVEfmt "
            + struct.pack("<IHHIIHH", 16, 1, 1, 22050, 44100, 2, 16)
            + b"data" + struct.pack("<I", len(wave_samples)) + wave_samples)
    yield "audio-load", (apc("SyncTERM:Q;libsndfile")
                         + apc("SyncTERM:Q;libsndfileFormat;1;2")
                         + blob("SyncTERM:A;LoadBlob;S=0", wave)
                         + blob("SyncTERM:C;S;reference.wav", wave)
                         + apc("SyncTERM:A;Load;S=1;reference.wav"))
    shapes = ("SIN", "SAW", "SQ", "SINE_HARM", "SINE_SAW", "SINE_SAW_CHORD", "SINE_SAW_HARM")
    yield "synth", b"".join(apc(f"SyncTERM:A;Synth;S={i};W={shape};F=440;T=600f")
                             for i, shape in enumerate(shapes))
    yield "synth-boundaries", (apc("SyncTERM:A;Synth;S=7;W=SAW;F=1000;T=1000f")
                               + apc("SyncTERM:A;Synth;S=8;W=SQ;F=30000;T=600f")
                               + apc("SyncTERM:A;Synth;S=9;W=SILENCE;T=5ms"))
    yield "synth-periods", (apc("SyncTERM:A;Synth;S=10;W=SIN;F=440;T=2p")
                            + apc("SyncTERM:A;Synth;S=10;W=SIN;T=2p;F=440")
                            + apc("SyncTERM:A;Copy;S=10;D=11"))
    downloaded_font = bytes([0x80]) * 3584
    cached_font = bytes([0x40]) * 3584
    yield "downloaded-font", (b"\x1bPCTerm:Font:46:" + base64.b64encode(downloaded_font) + b"\x1b\\"
                               + b"\x1b[0;46 D\x1b[20;5HABC xyz")
    yield "cached-font", (blob("SyncTERM:C;S;reference.fnt", cached_font)
                          + apc("SyncTERM:C;SetFont;47;reference.fnt")
                          + b"\x1b[0;0 D")
    yield "cached-font-use", b"\x1b[0;46 D\x1b[21;5HNEW\x1b[0;0 D"


def read_barrier(connection):
    result = bytearray()
    while True:
        data = connection.recv(65536)
        if not data:
            raise RuntimeError("legacy disconnected before APC version barrier")
        result += data
        match = re.search(rb"\x1b_SyncTERM:VER;([^\x1b]*)\x1b\\", result)
        if match is not None:
            return bytes(result), match[1].decode("ascii")


def capture(pid: int, prefix: Path):
    commands = ["gdb", "-q", "-batch", "-p", str(pid),
                "-ex", "set pagination off", "-ex", "set print thread-events off",
                "-ex", 'printf "APC_TOP=%d\\n", screena.toprow',
                "-ex", f"dump binary memory {prefix}.frame32 screena.rect->data "
                       "((char *)screena.rect->data)+896000",
                "-ex", f"dump binary memory {prefix}.palette32 &'bitmap_con.c'::palette "
                       "((char *)&'bitmap_con.c'::palette)+262144"]
    for i in range(2):
        image = f"'term.c'::pixmap_buffer[{i}]"
        commands += ["-ex", f"if {image} != 0",
                     "-ex", f'printf "APC_IMAGE{i}=%u,%u\\n", {image}->width, {image}->height',
                     "-ex", f"dump binary memory {prefix}.image{i}.native {image}->pixels "
                            f"((char *){image}->pixels)+{image}->width*{image}->height*4",
                     "-ex", "end"]
    mask = "'term.c'::mask_buffer"
    commands += ["-ex", f"if {mask} != 0",
                 "-ex", f'printf "APC_MASK=%u,%u\\n", {mask}->width, {mask}->height',
                 "-ex", f"dump binary memory {prefix}.mask {mask}->bits "
                        f"((char *){mask}->bits)+({mask}->width*{mask}->height+7)/8",
                 "-ex", "end"]
    for i in range(12):
        patch = f"'audio_apc.c'::patches[{i}]"
        commands += ["-ex", f'printf "APC_PATCH{i}=%zu\\n", {patch}.nframes',
                     "-ex", f"if {patch}.frames != 0",
                     "-ex", f"dump binary memory {prefix}.pcm{i} {patch}.frames "
                            f"((char *){patch}.frames)+{patch}.nframes*4",
                     "-ex", "end"]
    commands += ["-ex", "detach"]
    script = Path(str(prefix) + ".gdb")
    script.write_text("\n".join(commands[i + 1] for i, value in enumerate(commands) if value == "-ex") + "\n")
    result = subprocess.run(commands[:5] + ["-x", str(script)],
                            capture_output=True, text=True, timeout=60)
    Path(str(prefix) + ".debugger").write_text(result.stdout + result.stderr)
    if result.returncode != 0 or re.search(r"(?:No symbol|Cannot access|syntax error)", result.stdout + result.stderr):
        raise RuntimeError("legacy APC capture failed: " + result.stdout + result.stderr)
    top = re.search(r"APC_TOP=(\d+)", result.stdout)
    if top is None:
        raise RuntimeError("legacy APC capture has no screen origin")
    palette_bytes = Path(str(prefix) + ".palette32").read_bytes()
    frame = Path(str(prefix) + ".frame32").read_bytes()
    Path(str(prefix) + ".rgb24").write_bytes(rgb24((frame, palette_bytes, int(top[1]), False)))
    palette = struct.unpack("=65536I", palette_bytes)
    for i in range(2):
        native = Path(str(prefix) + f".image{i}.native")
        if native.exists():
            values = struct.unpack(f"={native.stat().st_size // 4}I", native.read_bytes())
            Path(str(prefix) + f".image{i}.rgb24").write_bytes(
                b"".join(pixel_rgb(value, palette).to_bytes(3, "big") for value in values))
    return result.stdout


def compare(prefix: Path, modern: Path, label: str):
    failures = []
    reference_state = Path(str(prefix) + ".debugger").read_text()
    modern_state = dict(line.split("=", 1) for line in Path(str(modern) + ".state").read_text().splitlines())
    for key in ("image0", "image1", "mask"):
        match = re.search("APC_" + key.upper() + r"=(\d+,\d+)", reference_state)
        expected = match[1] if match is not None else "0,0"
        if modern_state[key] != expected:
            failures.append(f"{key}: dimensions {expected}/{modern_state[key]}")
    for i in range(12):
        match = re.search(f"APC_PATCH{i}=" + r"(\d+)\n", reference_state)
        if match is None:
            raise RuntimeError(f"reference capture has no patch {i} frame count")
        if modern_state[f"patch{i}"] != match[1]:
            failures.append(f"patch{i}: frames {match[1]}/{modern_state[f'patch{i}']}")
    for suffix in [".rgb24", ".wire", ".mask", ".image0.rgb24", ".image1.rgb24"] + [f".pcm{i}" for i in range(12)]:
        reference_file = Path(str(prefix) + suffix)
        modern_file = Path(str(modern) + suffix)
        if reference_file.exists() != modern_file.exists():
            failures.append(f"{suffix}: presence differs")
        elif reference_file.exists():
            expected, actual = reference_file.read_bytes(), modern_file.read_bytes()
            if expected != actual:
                first = next((i for i, pair in enumerate(zip(expected, actual)) if pair[0] != pair[1]), min(len(expected), len(actual)))
                failures.append(f"{suffix}: first differing byte {first}, sizes {len(expected)}/{len(actual)}")
    for failure in failures:
        print(f"{label}: REGRESSION {failure}", flush=True)
    if not failures:
        print(f"{label}: exact", flush=True)
    return len(failures)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--legacy", type=Path, required=True)
    parser.add_argument("--modern", type=Path, required=True)
    parser.add_argument("--display", required=True, help="already-running private Xvfb display")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--chunk-size", type=int, default=4096,
                        help="modern incremental write size (use 1 for every-byte splits)")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix="run-", dir=args.output.resolve()))
    print(f"APC captures: {output}", flush=True)
    (output / "reference.json").write_text(json.dumps({
        "binary": str(args.legacy.resolve()),
        "sha256": hashlib.sha256(args.legacy.read_bytes()).hexdigest(),
        "display": args.display,
    }, indent=2) + "\n")
    with tempfile.TemporaryDirectory(prefix="session-", dir=output) as temporary, socket.socket() as listener:
        root = Path(temporary)
        environment = os.environ.copy()
        environment["DISPLAY"] = args.display
        environment.pop("WAYLAND_DISPLAY", None)
        for variable in ("XDG_CACHE_HOME", "XDG_CONFIG_HOME", "XDG_DATA_HOME"):
            directory = root / variable.lower()
            directory.mkdir()
            environment[variable] = str(directory)
        ini, listing = root / "legacy.ini", root / "legacy.list"
        ini.write_text("[SyncTERM]\nScalingFactor=1\nScreenMode=EGA80x25\n")
        listing.write_text("RIP=Off\n")
        listener.bind(("127.0.0.1", 0)); listener.listen(1); listener.settimeout(60)
        command = [str(args.legacy.resolve()), "-iXW", "-n" + str(ini), "-b" + str(listing), "-c", "-q",
                   f"shell:stty raw -echo;exec nc 127.0.0.1 {listener.getsockname()[1]}"]
        with (output / "legacy.stderr").open("wb") as stderr:
            process = subprocess.Popen(command, env=environment, stdout=subprocess.DEVNULL, stderr=stderr)
        mismatches = 0
        try:
            connection, _ = listener.accept()
            with connection:
                connection.settimeout(60)
                initial = b"\x1b[0;37;40m\x1b[2J\x1b[H\x1b[?25l" + apc("SyncTERM:VER")
                connection.sendall(initial)
                wire, version = read_barrier(connection)
                if not version.startswith("SyncTERM 1.10a"):
                    raise RuntimeError("reference is not SyncTERM v1.10a: " + version)
                cumulative = initial
                for index, (label, payload) in enumerate(cases()):
                    cumulative += payload + apc("SyncTERM:VER")
                    connection.sendall(payload + apc("SyncTERM:VER"))
                    replies, _ = read_barrier(connection)
                    wire += replies
                    prefix = output / f"{index:02d}-{label}-legacy"
                    modern = output / f"{index:02d}-{label}-modern"
                    stream = output / f"{index:02d}-{label}.stream"
                    stream.write_bytes(cumulative)
                    Path(str(prefix) + ".wire").write_bytes(wire)
                    capture(process.pid, prefix)
                    modern_directory = root / f"modern-{index}"
                    modern_directory.mkdir()
                    result = subprocess.run([str(args.modern.resolve()), str(modern_directory), version,
                                             str(stream), str(modern), str(args.chunk_size)],
                                            timeout=120, capture_output=True, text=True)
                    if result.returncode != 0:
                        raise RuntimeError("modern APC replay failed: " + result.stdout + result.stderr)
                    mismatches += compare(prefix, modern, label)
                print(f"APC reference comparison: {mismatches} regressions", flush=True)
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill(); process.wait()
    return 1 if mismatches else 0


if __name__ == "__main__":
    raise SystemExit(main())
