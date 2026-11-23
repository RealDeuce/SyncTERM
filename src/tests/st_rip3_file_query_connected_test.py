#!/usr/bin/env python3
"""Verify exact RIP_FILE_QUERY replies through the TCP session."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from datetime import datetime, timezone
import os
from pathlib import Path
import struct

from st_migrated_control_test import connection
from st_rip3_connected_test import query
from st_terminal_conformance_test import Terminal


def fixture(parent, name, contents):
    parent.mkdir(parents=True, exist_ok=True)
    path = parent / name
    path.write_bytes(contents)
    stamp = datetime(1993, 1, 2, 3, 4, 30, tzinfo=timezone.utc).timestamp()
    os.utime(path, (stamp, stamp))


def run(args):
    with connection(args.executable, args.reports, encoding=args.encoding,
                    options="RIP=RIPv3\n", settings="AudioModes=0\n") as (
                        client, root, peer, send):
        term = Terminal(client, root, peer, send)
        term.exchange(b"\x1b[!", b"RIPSCRIP030001")
        owner, = (path for path in (root / "cache").glob("*/syncterm-cache-name.txt")
                  if path.read_text(encoding="utf-8").strip() == "Capture")
        local = owner.parent / "RIP"
        shared = root / "RIP"
        fixture(local, "Mixed.rip", bytes(20345))
        fixture(shared, "MIXED.RIP", b"other tier")
        bmp = (struct.pack("<2sIHHI", b"BM", 70, 0, 0, 62)
               + struct.pack("<IiiHHIIiiII", 40, 9, 2, 1, 1, 0, 8, 0, 0, 0, 0)
               + bytes(16))
        assert len(bmp) == 70
        for name in ("Pixel.BMP", "Hover.BMH", "Mask.BMM", "Tail.BMP", "A.BMP"):
            fixture(local, name, bmp)
        fixture(local, "Short.BMP", bmp[:20])
        fixture(local, "Other.BMP", bytes(70))
        fixture(local, "Empty", b"")
        fixture(local, "$NAME$", b"text")

        def wire(mode, filename, reserved=b"0000"):
            return b"\x01|1F" + mode + reserved + filename + b"\r\n"

        def check(mode, name, expected, reserved=b"0000"):
            return term.exchange(wire(mode, name, reserved), expected)

        term.case("RIPv3 file query exact replies for all five modes and both lookup outcomes")
        ending = b".01/02/93.03:04:30\r"
        for mode, hit, miss in ((b"00", b"1", b"0"), (b"01", b"1\r", b"0\r"),
                                (b"02", b"1.20345\r", b"0\r"),
                                (b"03", b"1.20345" + ending, b"0\r"),
                                (b"04", b"1.MIXED.RIP:20345" + ending,
                                 b"0.MISSING:0.00/00/00.00:00:00\r")):
            check(mode, b"MiXeD.RiP", hit)
            check(mode, b"Missing", miss)
        check(b"02", b"Empty", b"1.0\r")
        check(b"00", b"Mixed", b"0")

        term.case("RIPv3 file query reports the prepared name and converts only aliases")
        for name, reply in ((b"PiXeL.iCn", b"1.PIXEL.ICN:22"),
                            (b"Pixel.BmP", b"1.PIXEL.BMP:70"),
                            (b"Hover.Hic", b"1.HOVER.HIC:22"),
                            (b"Mask.Msk", b"1.MASK.MSK:22"),
                            (b"A.Icn.Extra", b"1.A.ICN.EXTRA:22"),
                            (b"Ab*tail.icn", b"1.TAIL.ICN:22"),
                            (b"c:\\outside\\pixel.icn", b"1.C:\\OUTSIDE\\PIXEL.ICN:22"),
                            (b"Short.ICN", b"1.SHORT.ICN:20"),
                            (b"Other.ICN", b"1.OTHER.ICN:70"),
                            (b"$Name$", b"1.$NAME$:4")):
            check(b"04", name, reply + ending)

        term.case("RIPv3 file query preserves command order and recovers from malformed requests")
        term.exchange(query(b"$DVP(CUR)$$BASEMATH(CUR,64)$", at_cursor=True))
        data = (wire(b"05", b"Mixed.rip") + wire(b"00", b"Mixed.rip*")
                + wire(b"00", b"") + wire(b"00", b"Mixed.rip", b"00??")
                + wire(b"02", b"Pixel.ICN", b"00Aa"))
        term.exchange(data, b"1.22\r")
        term.exchange(query(b"$HOSTDIR(EMPTY)$", at_cursor=True))
        check(b"02", b"Mixed.rip", b"1.10\r")
        term.exchange(query(b"$HOSTDIR(OFF)$", at_cursor=True))
        check(b"02", b"Mixed.rip", b"1.20345\r")

    with connection(args.executable, args.reports, encoding=args.encoding,
                    options="RIP=Off\n", settings="AudioModes=0\n") as (
                        client, root, peer, send):
        term = Terminal(client, root, peer, send)
        term.case("RIPv3 file query emits no reply when connection graphics is off")
        term.exchange(b"\x01|1F000000Missing\r\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("encoding", choices=("CP437", "UTF-8"))
    parser.add_argument("--reports", type=Path, required=True)
    run(parser.parse_args())
