#!/usr/bin/env python3
"""Verify exact RIP_FILE_QUERY replies through the TCP session."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from datetime import datetime, timezone
import os
from pathlib import Path
import struct

from st_migrated_control_test import connection
from st_rip3_connected_test import pixel, query
from st_terminal_conformance_test import Terminal


def fixture(parent, name, contents):
    parent.mkdir(parents=True, exist_ok=True)
    path = parent / name
    path.write_bytes(contents)
    stamp = datetime(1993, 1, 2, 3, 4, 30, tzinfo=timezone.utc).timestamp()
    os.utime(path, (stamp, stamp))


def run(args):
    with connection(args.executable, args.reports, encoding=args.encoding,
                    options="ScreenMode=Custom\nRIP=RIPv3\n",
                    settings="CustomColumns=80\nCustomRows=60\nCustomFontHeight=8\nAudioModes=0\n") as (
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
        check(b".", b"MiXeD.RiP", b"1", b".")
        check(b"04", b"MiXeD.RiP", b"1.MIXED.RIP:20345" + ending, b".")

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
                + wire(b"00", b"Mixed.rip", b"00.")
                + wire(b"02", b"Pixel.ICN", b"00Aa"))
        term.exchange(data, b"1.22\r")
        term.exchange(query(b"$HOSTDIR(EMPTY)$", at_cursor=True))
        check(b"02", b"Mixed.rip", b"1.10\r")
        term.exchange(query(b"$HOSTDIR(OFF)$", at_cursor=True))
        check(b"02", b"Mixed.rip", b"1.20345\r")

        def values(source, expected=b""):
            return term.exchange(query(source, at_cursor=True), expected)

        term.case("FILEEXISTS reports presence through the active cache and global fallback")
        fixture(shared, "Global.dat", b"shared")
        fixture(local / "HOST-NAMED", "Named.dat", b"selected")
        fixture(root / "cache" / "other-bbs" / "RIP", "Foreign.dat", b"outside")
        (local / "Folder.dat").mkdir()
        values(b"$FILEEXISTS(mixed.rip)$:$FILEEXISTS(Mixed)$:$FILEEXISTS(Global.dat)$:"
               b"$FILEEXISTS(Foreign.dat)$:$FILEEXISTS(Folder.dat)$", b"1:0:1:0:0")
        values(b"$FILEEXISTS(Pixel.icn)$:$FILEEXISTS(Hover.hic)$:$FILEEXISTS(Mask.msk)$:"
               b"$FILEEXISTS(a.icn.extra)$", b"1:1:1:1")
        values(b"$FILEEXISTS(C:mixed.rip)$:$FILEEXISTS(Mask.msk.icn)$:"
               b"$FILEEXISTS(Mask.icn.msk)$", b"1:1:0")
        check(b"00", b"Mask.msk.icn", b"0")
        values(b"$HOSTDIR(Named)$$FILEEXISTS(Named.dat)$:$FILEEXISTS(Mask.msk)$:"
               b"$FILEEXISTS(Global.dat)$$HOSTDIR(Off)$", b"1:0:1")
        try:
            (local / "Alias.dat").symlink_to(root / "cache" / "other-bbs" / "RIP" / "Foreign.dat")
        except (NotImplementedError, OSError):
            pass
        else:
            values(b"$FILEEXISTS(Alias.dat)$", b"0")

        term.case("FILEEXISTS recovers from malformed references and expands in graphical fields")
        term.exchange(b"\x01|1D00400FILEEXISTS:spoof\r\n")
        accent = b"\x82" if args.encoding == "CP437" else "é".encode()
        values(b"$FILEEXISTS(" + accent + b"Pixel.icn)$", b"1")
        # The leading non-filename bytes still count toward the 20-byte
        # argument limit. Global.dat is the prepared lookup name.
        argument=accent*(10//len(accent))+b"Global.dat"
        assert len(argument)==20
        values(b"$FILEEXISTS("+argument+b")$",b"1")
        for invalid in (b"$FILEEXISTS$", b"$FILEEXISTS()$", b"$FILEEXISTS(Pixel.icn,Mask.msk)$",
                        b"$FILEEXISTS(Pixel.icn" + accent + b")$"):
            values(b"a" + invalid + b"b", b"a" + invalid + b"b")
        term.exchange(query(b"$FILEEXISTS(Invalid/Name)$", at_cursor=True)
                      + query(b"$FILEEXISTS(Pixel.icn)$", at_cursor=True), b"$FILEEXISTS(Invalid/Name)$1")
        fixture(local, "1.RIP", b"!|c04|X0A0A\r\n")
        fixture(local, "0.RIP", b"!|c02|X0B0B\r\n")
        fixture(local, "A$FILEEXISTS()$B.RIP", b"!|c01|X0C0C\r\n")
        frame = term.exchange(b"\x01|*\r\n" + query(b"$WORLD(CUR,640,480)$", at_cursor=True)
                              + b"\x01|1R00000000$FILEEXISTS(Mask.msk)$.RIP\r\n"
                              b"\x01|1R00000000$FILEEXISTS(Absent)$.RIP\r\n"
                              b"\x01|1R00000000A$FILEEXISTS()$B.RIP\r\n"
                              + query(b"alive", at_cursor=True), b"alive")
        assert pixel(frame, 10, 10) == 0xaa0000
        assert pixel(frame, 11, 11) == 0x00aa00
        assert pixel(frame, 12, 12) == 0x0000aa
        assert frame.application["connected"] == 1

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
