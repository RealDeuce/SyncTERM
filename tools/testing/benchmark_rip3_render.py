#!/usr/bin/env python3
"""Measure silent text streams through the native X11 app and a real shell PTY.

Each run owns a private Xvfb, configuration, BBS list, shell and cache. Replies
fence parser progress, not X server presentation. No bell/audio commands or
external art corpus are sent. Supply two executables for paired comparisons.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import json
import os
from pathlib import Path
import select
import shlex
import statistics
import subprocess
import sys
import tempfile
import time


PEER = r'''
import json, os, select, statistics, termios, time, tty
from pathlib import Path
root = Path(__file__).parent
tty.setraw(0)
def fence(data=b"", expected=b""):
    os.write(1, data + b"\x1b[5n")
    reply = bytearray()
    deadline = time.monotonic() + 20
    while not reply.endswith(b"\x1b[0n"):
        left = deadline-time.monotonic()
        if left <= 0 or not select.select([0], [], [], left)[0]:
            raise RuntimeError("DSR timeout: " + repr(reply))
        reply.extend(os.read(0, 4096))
    if reply != expected+b"\x1b[0n":
        raise RuntimeError("unexpected reply: " + repr(reply))
def measure(lines=500):
    samples=[]
    for i in range(50):
        start=time.monotonic_ns()
        fence(b"a")
        samples.append((time.monotonic_ns()-start)/1e6)
    start=time.monotonic_ns()
    fence((b"\x1b[32m0123456789 filename.rip \x82\x1b[m\r\n")*lines)
    return {"cell_median_ms": statistics.median(samples),
            "stream_lines": lines, "stream_ms": (time.monotonic_ns()-start)/1e6}
version={"Off": b"", "RIPv1": b"RIPSCRIP015410", "RIPv3": b"RIPSCRIP030001"}[MODE]
fence(b"\x1b[!", version)
fence(b"\x1b[2J\x1b[H")
results={}
if GRAPHICS_ONLY:
    digits=b"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    def point(x,y):
        return bytes((digits[x//36],digits[x%36],digits[y//36],digits[y%36]))
    data=b"\x01|c0F"+b"".join(b"|L"+point(i%320,i%200)+point((i*7)%320,(i*11)%200)
                                 for i in range(200))+b"\r\n"
    start=time.monotonic_ns()
    fence(data)
    results["graphics"]={"200_lines_ms": (time.monotonic_ns()-start)/1e6}
else:
    results["entry"]=measure()
if MODE == "RIPv3" and not GRAPHICS_ONLY:
    # Exact script prologue, with CRLF supplied explicitly because this
    # helper's own PTY is raw. Follow with ordinary filename text.
    fence(b"\x1b[m\r\n!|*\r\nfilename.rip\r\n")
    # Explicit w also makes the positioned comparison usable on pre-* builds.
    fence(b"\x01|w0000270O00\r\n")
    fence(b"\x1b[25;1H")
    results["microansi"] = measure(1)
(root/"results.json").write_text(json.dumps(results))
# Remain connected until the driver has collected results and stops this app.
while os.read(0, 4096):
    pass
'''


def stop(process):
    if process is None or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def run(executable, mode, work, graphics_only):
    with tempfile.TemporaryDirectory(prefix="r3perf-", dir=work) as temporary:
        root = Path(temporary)
        env = os.environ.copy()
        for name in ("DISPLAY", "WAYLAND_DISPLAY", "XAUTHORITY", "XDG_RUNTIME_DIR"):
            env.pop(name, None)
        for name, child in (("XDG_CONFIG_HOME", "config"), ("XDG_CACHE_HOME", "cache"),
                            ("XDG_DATA_HOME", "data"), ("XDG_DOWNLOAD_DIR", "downloads")):
            env[name] = str(root/child)
        read_fd, write_fd = os.pipe()
        display = app = None
        try:
            with (root/"xvfb.log").open("wb") as log:
                display = subprocess.Popen(["Xvfb", "-displayfd", str(write_fd), "-screen", "0",
                                            "1280x1024x24", "-nolisten", "tcp"],
                                           pass_fds=(write_fd,), env=env, stdout=log, stderr=log)
            os.close(write_fd)
            write_fd = -1
            if not select.select([read_fd], [], [], 10)[0]:
                raise RuntimeError("private Xvfb did not start")
            env["DISPLAY"] = ":" + os.read(read_fd, 32).decode().strip()
            peer = root/"p.py"
            peer.write_text("#!" + sys.executable + "\nMODE=" + repr(mode)
                            + "\nGRAPHICS_ONLY=" + repr(graphics_only)
                            + "\ntry:\n exec(" + repr(PEER) + ")\n"
                              "except BaseException:\n import traceback\n from pathlib import Path\n"
                              " (Path(__file__).parent/'error.txt').write_text(traceback.format_exc())\n")
            peer.chmod(0o700)
            command = shlex.quote(str(peer))
            if len(command.encode()) > 64:
                raise RuntimeError("temporary peer command exceeds profile address capacity")
            (root/"syncterm.ini").write_text(
                "[SyncTERM]\nConfirmClose=false\nScalingFactor=1\nOutputMode=X11\n"
                "AudioModes=0\nMidiModes=0\n")
            (root/"syncterm.lst").write_text(
                "[Perf]\nConnectionType=Shell\nAddress=" + command
                + "\nScreenMode=80x43\nNoStatus=false\nHidePopups=true\n"
                  "BPSRate=0\nEncoding=CP437\nRIP=" + mode + "\n")
            with (root/"app.log").open("wb") as log:
                app = subprocess.Popen([str(executable), "-iXW", "-q", "-n", str(root/"syncterm.ini"),
                                        "-b", str(root/"syncterm.lst")], env=env,
                                       stdin=subprocess.DEVNULL, stdout=log, stderr=log)
            window = subprocess.check_output(["xdotool", "search", "--sync", "--onlyvisible",
                                              "--name", "SyncTERM"], env=env, timeout=10).splitlines()[0]
            # Window creation precedes the menu VM's initial draw.
            time.sleep(0.3)
            subprocess.run(["xdotool", "key", "--window", window.decode(), "Return"],
                           env=env, timeout=5, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            deadline = time.monotonic()+90
            while not (root/"results.json").exists():
                if (root/"error.txt").exists():
                    raise RuntimeError((root/"error.txt").read_text())
                if app.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError("native rendering benchmark failed: "
                                       + (root/"app.log").read_text(errors="replace")[-4000:])
                time.sleep(0.02)
            return json.loads((root/"results.json").read_text())
        finally:
            stop(app)
            stop(display)
            os.close(read_fd)
            if write_fd >= 0:
                os.close(write_fd)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executables", type=Path, nargs="+")
    parser.add_argument("--work", type=Path, default=Path("build"))
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--graphics-only", action="store_true")
    args = parser.parse_args()
    executables = [path.resolve() for path in args.executables]
    results = {str(path): {} for path in executables}
    for iteration in range(args.runs):
        for executable in executables if iteration % 2 == 0 else reversed(executables):
            for mode in (("RIPv1", "RIPv3") if args.graphics_only else ("Off", "RIPv1", "RIPv3")):
                result = run(executable, mode, args.work.resolve(), args.graphics_only)
                results[str(executable)].setdefault(mode, []).append(result)
                print(json.dumps({"executable": str(executable), "mode": mode,
                                  "run": iteration+1, **result}), flush=True)
    print(json.dumps({"medians": {
        executable: {mode: {phase: {metric: statistics.median(row[phase][metric] for row in runs)
                                    for metric in runs[0][phase]}
                             for phase in runs[0]}
                     for mode, runs in modes.items()}
        for executable, modes in results.items()}}, indent=2))


if __name__ == "__main__":
    main()
