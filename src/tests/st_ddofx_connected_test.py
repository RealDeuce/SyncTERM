"""DDO F/X over a live connection with ordinary ANSI before and after commands."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import os
import struct
import time
from pathlib import Path

from st_migrated_control_test import connection
from st_terminal_conformance_test import Terminal


def check_cache(t, root):
    [marker] = root.rglob("syncterm-cache-name.txt")
    base = marker.parent / "DDOFX"
    timestamp = time.mktime((2024, 2, 29, 12, 34, 19, 0, 0, -1))
    cases = [(b"dm", "MUSIC"), (b"df", "FONTS"), (b"dt", "BBSTHEME"), (b"dr", "BBSTHEME"),
             (b"8CFILE,slot,", "TERMS/Slot")]
    for prefix, category in cases:
        directory = base / category
        directory.mkdir(parents=True, exist_ok=True)
        asset = directory / "Sample.bin"
        asset.write_bytes(b"sample")
        os.utime(asset, (timestamp, timestamp))
        t.exchange(b"\x1d{" + prefix + b"SAMPLE.BIN}", b"6,2024,2,29,12,34,18\xff")
        missing = b"N\xff" if prefix.startswith(b"8") else b"NO\xff"
        t.exchange(b"\x1d{" + prefix + b"../Sample.bin}", missing)
        t.exchange(b"\x1d{" + prefix + b"ABSENT}", missing)
    t.exchange(b"\x1d{8CDIR,slot}\x1d{8CDIR,../MUSIC}\x1d{8CDIR,ABSENT}", b"Y\xffN\xffN\xff")
    t.check("external client command", b"A\x1d{8EXEC,slot}B", (3, 1), [(1, 1, "AB")])
    if os.name == "posix":
        (base / "TERMS" / "outside").symlink_to(root, target_is_directory=True)
        (base / "MUSIC" / "link.bin").symlink_to(marker)
        t.exchange(b"\x1d{8CDIR,outside}\x1d{8CFILE,outside,wire.bin}\x1d{dmlink.bin}",
                   b"N\xffN\xffNO\xff")


def commands(*bodies):
    return b"".join(b"\x1d{" + body + b"}" for body in bodies)


def check_resources(t, root):
    [marker] = root.rglob("syncterm-cache-name.txt")
    directory = marker.parent / "DDOFX" / "BBSTHEME"
    record = struct.Struct("<12sIIHH")
    block_size = 33 * record.size
    unused = record.pack(b"\xff", 0, 0, 0, 0)
    data = bytearray(unused * 33)
    outer = b"A" + commands(b"*Inner.fx") + b"C" + commands(b"4DEFM,1,1,\x82")
    data[:24] = record.pack(b"OUTER.FX", len(data), len(outer), 0, 100)
    data.extend(outer)
    link = len(data)
    data[32*24:33*24] = record.pack(b"\xff", link, 0, 0, 0)
    data.extend(unused * 33)
    limit = b"\x1d{" + b"x" * 39996 + b"}L"
    assert len(limit) == 40000
    for slot, (name, version, payload) in enumerate([
        (b"INNER.FX", 2, b"\x1b[32m\x82"), (b"INNER.FX", 3, b"DUP"),
        (b"LIMIT.FX", 0, limit), (b"LARGE.FX", 0, limit + b"!"),
        (b"REPLACE.FX", 4, commands(b"2fmissing."))]):
        start = link + slot * 24
        data[start:start+24] = record.pack(name, len(data), len(payload), 0, version)
        data.extend(payload)
    (directory / "Pack.M").write_bytes(data + b"\xe5\xff\xe6")
    t.exchange(commands(b"dRESOURCE,pack.m,inner.fx,3", b"dRESOURCE,pack.m,inner.fx,42",
                        b"dRESOURCE,BBSTHEME\\PACK.M,inner.fx,2",
                        b"7DEFRET,1,Y", b"~IFVAR,RESOURCE|pack.m|outer.fx,=,100,\x1d(7SRET,1)",
                        b"~IFVAR,RESOURCE|pack.m|absent,<,0,\x1d(7SRET,1)"), b"YES\xffNO\xffYES\xffYY")
    t.check("nested DOS resource scripts and a retained macro", commands(
            b"2fPACK.", b"2FOther.", b"2fTOO-LONG-NAME", b"*outer.fx", b"4EXCM,1,1",
            b"*large.fx", b"*limit.fx", b"*replace.fx", b"*outer.fx") + b"Z",
            (7, 1), [(1, 1, "AéCéLZ")])
    # A malformed block link and a truncated member must leave the next
    # online command available. The latter contains a valid-looking script.
    data[link+32*24:link+33*24] = record.pack(b"\xff", link, 0, 0, 0)
    data[:24] = record.pack(b"BAD.FX", len(data) - 1, 900, 0, 0)
    (directory / "Bad.M").write_bytes(data)
    t.check("damaged resource", commands(b"2fbad.", b"*bad.fx", b"*absent", b"dRESOURCE,bad.m,absent,0") + b"K",
            (2, 1), [(1, 1, "K")], expected=b"NO\xff")


def check_scripts(t):
    def read_variable(slot):
        return commands(b"7DEFRET,300,DEFVAR" + str(slot).encode(), b"7SRET,300")

    t.exchange(commands(b"7DEFRET,300,A~,B^@^[^m^^^?", b"7SRET,300"), b"A,,B\0\x1b\r\x1e\xff")
    t.exchange(commands(b"7DEFRET,0", b"7SRET,300"))
    t.exchange(commands(b"~DEFVAR,1000,1,a,b", b"7DEFRET,2,DEFVAR1000", b"~CAPVAR,1000",
                        b"7SRET,2") + read_variable(1000), b"a,bA,B")
    t.exchange(commands(b"~DEFVAR,1,1,xyz", b"~VARINS,1,1000,2", b"~VARDEL,1000,4,2",
                        b"~VARCPY,1000,2,2,3", b"~PADVARL,2,5,.", b"~PADVARR,2,6,!",
                        b"~VARPOS,2,3,y", b"~VARLEN,2,4") + read_variable(2) +
               read_variable(3) + read_variable(4), b"..xyB!46")
    t.exchange(commands(b"~VAREQU,1,4", b"~VARTYP,1,2", b"~VARADD,1,7", b"~VARMLT,1,-3",
                        b"~VARSUB,1,2", b"~VARDIV,1,2") + read_variable(1), b"-20")
    t.exchange(commands(b"~RNDVAR,1,1") + read_variable(1), b"1")
    t.exchange(commands(b"~VARCAP,1000", b"MGVOL", b"7SRET,2", b"~VARCAP,0", b"~INCVAR,1000",
                        b"MSVOL,DEFVAR1000", b"MGVOL"), b"a,b16\xff")
    t.exchange(commands(b"~VARCAP,3", b"dLITERM", b"~VARCAP,0", b"~DECVAR,3") + read_variable(3),
               b"LITERM Ver. 2.0")
    # The running line stays intact while it replaces itself and the next line.
    t.check("live macro definitions", commands(
        b"4DEFM,32,1,A\x1d(4DEFM,32,1,Z)B\x1d(4DEFM,32,10,C\x1d[7SRET,2])",
        b"4DEFM,32,10,OLD", b"4RUNM,32", b"4EXCM,32,1", b"4CLRM,32", b"4RUNM,32") + b"D",
        (6, 1), [(1, 1, "ABCZD")], expected=b"a,b")
    t.exchange(commands(b"~DEFVAR,1,2,0", b"~REPMAC,320,\x1d(~INCVAR,1)",
                        b"~REPVAR,DEFVAR1,>=,324,\x1d(~INCVAR,1)",
                        b"~IFVAR,DEFVAR1,=,324,\x1d(7SRET,2)",
                        b"~IFVAR,LITERM,=,literm ver. 2.00,\x1d(7SRET,2)") + read_variable(1), b"a,ba,b324")
    # Clearing a whole bank and rejecting out-of-range selectors preserve framing.
    t.check("script limits and recovery", commands(b"~DEFVAR,0", b"4DEFM,1,1,Q", b"4DEFM,0",
            b"4RUNM,1", b"4RUNM,33", b"7SRET,301") + read_variable(1000) + b"R", (2, 1), [(1, 1, "R")])
    start = time.monotonic()
    t.exchange(commands(b"4DEFM,1,1,\x1d(w3)\x1d(7SRET,2)", b"4RUNM,1", b"dLITERM"),
               b"a,bLITERM Ver. 2.00\xff")
    assert time.monotonic() - start >= 0.15


def check_events(t):
    # A repeating event counts to two and then deactivates itself. No more
    # bytes arrive from the BBS while the callbacks run.
    t.exchange(commands(b"~DEFVAR,1,2,0", b"7DEFRET,1,M",
                        b"7CEVENT,200,0,20,0,0,0,0,0,0,1,R~^M,"
                        b"\x1d(~INCVAR,1)\x1d(7SRET,1)\x1d(~IFVAR,DEFVAR1,=,2,\x1d[7DEVENT,200])"))
    t.send(commands(b"7AEVENT,200"))
    t.input(b"MR,\rMR,\r")

    # Calendar events use local time and are one-shot. The first callback
    # clears all definitions while running and still sends its own return.
    t.exchange(commands(b"~PAUSEBAK", b"7CEVENT,2,1,0,1,1,2000,0,0,0,1,D,\x1d(7CEVENT,0)",
                        b"7AEVENT,2", b"7AEVENT,200", b"dLITERM"), b"LITERM Ver. 2.00\xff")
    t.send(commands(b"~UNPAUSEBAK"))
    t.input(b"D")

    # An armed seconds timer interrupts a foreground wait. Its own shorter
    # wait completes before its reply, then the original wire stream resumes.
    t.case("seconds timer during a foreground wait")
    start = time.monotonic()
    t.send(commands(b"7CEVENT,1,2,1,0,0,0,0,0,0,0,E,\x1d(w1)\x1d(7SRET,1)",
                    b"7AEVENT,1", b"w30", b"7SRET,1"))
    t.input(b"MEM")
    assert time.monotonic() - start >= 1.6

    # Redefinition discards the old arming. A later activation uses only
    # the replacement record. The following query also detects stray replies.
    t.exchange(commands(b"~PAUSEBAK", b"7AEVENT,1",
                        b"7CEVENT,1,1,0,1,1,2000,0,0,0,0,N,",
                        b"7CEVENT,3,1,0,1,1,2099,0,0,0,0,F,", b"7AEVENT,3", b"~UNPAUSEBAK"))
    t.send(commands(b"7AEVENT,1"))
    t.input(b"N")
    t.exchange(commands(b"7CEVENT,0", b"7AEVENT,1", b"dLITERM"), b"LITERM Ver. 2.00\xff")


def run(args):
    ident = b"\x1d{dLITERM}"
    reply = b"LITERM Ver. 2.00\xff"
    with connection(args.executable, args.reports, encoding=args.encoding,
                    options="Graphics=DDO F/X 2.0\n", settings="AudioModes=0\n") as fixture:
        t = Terminal(*fixture)
        for split in range(len(ident) + 1):
            t.send(ident[:split])
            t.exchange(ident[split:], reply)
        t.exchange(b"\x1d{dliterm}\x1d{DLITERM}", reply)
        t.exchange(b"\x1d{dMUSICON}\x1d{mMISSING.MOD}\x1d{dGETMUSIC}", b"NO\xffNONE\xff")
        t.exchange(b"\x1d{MGVOL}\x1d{MSVOL,20}\x1d{MMVOL,-5}\x1d{MGVOL}", b"64\xff15\xff")
        t.exchange(commands(b"dSOUNDON", b"tABSENT.THE", b"dGETTHEME", b"6CHECKBS,150", b"6CHECKBS,151",
                            b"~VARCAP,10", b"6CHECKBS,1", b"~VARCAP,0", b"7DEFRET,1,DEFVAR10", b"7SRET,1"),
                   b"NO\xffNONE\xffN/A\xffN/A")
        check_cache(t, fixture[1])
        check_resources(t, fixture[1])
        check_scripts(t)
        check_events(t)
        glyph = "é".encode("utf-8" if args.encoding == "UTF-8" else "cp437")
        t.check("framing amid ANSI", b"\x1b[2;" + ident + b"3H" + glyph + ident + ident + b"B",
                (5, 2), [(3, 2, "éB")], expected=reply * 3)
        t.check("ignore and hear", b"A\x1d{~IGNORCMD}" + ident + b"B\x1d{~HEARCMD}" + ident,
                (3, 1), [(1, 1, "AB")], expected=reply)
        t.check("recover and continue", b"A\x1d{" + b"x" * 251 + b"}B\x1dX" + ident,
                (4, 1), [(1, 1, "ABX")], expected=reply)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("encoding", choices=("CP437", "UTF-8"))
    parser.add_argument("--reports", type=Path, required=True)
    run(parser.parse_args())
