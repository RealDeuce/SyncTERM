"""Exercise batch RealPlay requests against a pull-only session mixer."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
from binascii import crc_hqx
import io
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zipfile


def header(kind, position=0):
    body = bytes([kind]) + position.to_bytes(4, "little")
    return b"**\x18B" + (body + crc_hqx(body, 0).to_bytes(2, "big")).hex().encode() + b"\r\n"

def subpacket(data, end):
    def escape(raw):
        return b"".join(bytes((24, b ^ 64)) if not b & 96 else bytes([b]) for b in raw)
    return escape(data) + b"\x18" + end + escape(crc_hqx(data + end, 0).to_bytes(2, "big"))

def package(members, compression):
    data = io.BytesIO()
    with zipfile.ZipFile(data, "w", compression) as archive:
        for name, contents in members:
            member = zipfile.ZipInfo(name, date_time=(2024, 2, 29, 12, 34, 18))
            member.compress_type = compression
            archive.writestr(member, contents)
    return data.getvalue()


def check_autoplay(executable):
    # The first package selects a four-sample ramp at the mixer rate.
    # The second is installed but its request must not replace the ramp.
    wire = bytearray(header(0))
    for n, sample in enumerate((bytes((1, 2, 0xff, 0xfe)), bytes((32, 32, 32, 32)))):
        filename = f"TONE{n}.RLP"
        archive = package([(filename, sample), ("DDOFX.INI", f"[REALPLAY]={filename},44100".encode())],
                          zipfile.ZIP_DEFLATED)
        info = f"$LI$R00{n}.ZIP".encode() + b"\x00" + str(len(archive)).encode() + b"\x00"
        wire.extend(header(4) + subpacket(info, b"k") + header(10) + subpacket(archive, b"h")
                    + header(11, len(archive)))

    for complete in (True, False):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "downloads").mkdir()
            arguments = [executable, root / "DDOFX", root / "downloads"]
            if complete:
                ending = header(8) + b"OO"
            else:
                arguments.append("cancelled")
                ending = b""  # The receiver cancels after both per-file completion events.
            subprocess.run(arguments, input=wire + ending, check=True)
            if complete:
                assert {p.name for p in (root / "DDOFX" / "BBSTHEME").iterdir()} == {"TONE0.RLP", "TONE1.RLP"}
                assert not any((root / "downloads").iterdir())
            else:
                assert not (root / "DDOFX").exists()
                assert {p.name for p in (root / "downloads").iterdir()} == {"$LI$R000.ZIP", "$LI$R001.ZIP"}


def check_theme(executable, samples):
    directory = bytearray(33 * 24)
    data = bytearray()
    for sample in range(1, 21):
        name = f"SAM{sample}.SAM".encode()
        struct.pack_into("<12sII", directory, (sample - 1) * 24, name, len(directory) + len(data), 1)
        data.append(sample)
    with tempfile.TemporaryDirectory() as directory_name:
        root = Path(directory_name)
        if samples:
            (root / "BBSTHEME").mkdir()
            (root / "BBSTHEME" / "SLIDES.THE").write_bytes(directory + data)
            subprocess.run([executable, root, "samples"], check=True)
            return
        (root / "THEMES").mkdir()
        (root / "THEMES" / "SLIDES.THE").write_bytes(directory + data)
        subprocess.run([executable, root, "select-theme"], check=True)
        assert (root / "terminal-theme").read_bytes() == b"SLIDES.THE"
        subprocess.run([executable, root, "reuse-theme"], check=True)


if __name__ == "__main__":
    if sys.argv[2:] in (["--theme"], ["--samples"]):
        check_theme(sys.argv[1], sys.argv[2] == "--samples")
    else:
        check_autoplay(sys.argv[1])
