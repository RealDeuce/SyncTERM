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


def check_drawing(t):
    def scene(bodies, expected):
        queries = [f"2aG,{x},{y}".encode() for x, y in expected]
        reply = b"".join(str(colour).encode() + b"\xff" for colour in expected.values())
        t.exchange(commands(*bodies, *queries), reply)

    scene([b"2aERASE", b"2C,9", b"2aCLIP,10,20,15,25", b"2aM,5,20", b"2aDRAW,25,20",
           b"2aMVREL,-10,0", b"2aDRAWREL,0,12", b"2aP,25,20", b"2aPX,15,20"],
          {(9,20):0,(10,20):9,(15,20):0,(20,20):9,(21,20):0,(15,25):9,(15,26):0})
    scene([b"2aERASE", b"2aRECT,2,5,1,3", b"2aCLPRCT,8,12,17,21",
           b"2aBOXLNS,2,1", b"2aDRWBOX,14,20,15,21", b"2aDRWBXX,14,20,15,21"],
          {(2,1):9,(5,3):9,(9,17):0,(10,17):9,(13,20):0,(14,15):0,(20,21):0})
    scene([b"2aDRWBOX,14,20,15,21"], {(16,16):9,(15,17):0,(14,18):9,(19,18):0})
    scene([b"2aERASE", b"2aM,8,18", b"2aDRDASH,18,18,1000100010001000",
           b"2aDRDSHRL,0,4,1000100010001000"],
          {(8,18):0,(11,18):0,(12,18):9,(16,18):9,(18,22):9,(18,21):0})
    scene([b"2aERASE", b"2aM,15,20", b"2aCIRC,3", b"2C,6", b"2aFLOOD,15,20"],
          {(15,20):6,(15,17):9,(18,20):9,(12,20):9,(15,23):9,(10,20):0})
    scene([b"2aERASE", b"2aFLOOD,15,20", b"2C,3", b"2aPAINT,1,1"],
          {(0,0):3,(9,15):3,(10,15):6,(20,25):6,(21,25):3})
    scene([b"2aERASE", b"2aELIPSF,3,2", b"2aMVREL,3,2", b"2C,4", b"2aCIRCF,1"],
          {(12,20):3,(15,18):3,(14,20):3,(15,17):0,(18,22):4})
    scene([b"2aERASE", b"2aBCURV,1,2,4,2,7,2,10,2,8", b"2aBCURVX,1,2,4,2,7,2,10,2,8",
           b"2aCLIP,0,639,0,479", b"2aDRAWREL,0,2"],
          {(1,2):0,(7,2):0,(10,2):4,(10,4):4})
    scene([b"2aERASE", b"2aM,-2147483648,7", b"2aDRAW,2147483647,7",
           b"2aDRAWREL,1,1"], {(0,7):4,(639,7):4,(639,8):0})
    scene([b"2aERASE", b"2C,2", b"2aRECT,1,3,1,2", b"2C,5", b"2aRECT,1,3,3,4",
           b"2aSCROLL,1,3,1,4,-3,0,WP"], {(1,1):5,(1,2):2,(1,3):2,(1,4):5,(0,2):0})
    scene([b"2CPYWP2VP", b"2C,7", b"2aSCROLL,1,3,1,4,2,1,VP", b"2CPYVP2WP"],
          {(1,1):7,(1,2):7,(1,3):5,(1,4):2,(4,4):0})
    scene([b"2CPYHP2WP"], {(1,1):5,(1,2):2,(1,3):2,(1,4):5})


def check_fades(t):
    t.case("foreground palette fades and page preservation")
    t.exchange(commands(b"2C,17", b"2aFILL", b"2CPYWP2VP", b"2C,18", b"2aFILL", b"2CPYWP2BP",
                        b"2aSRGB,17,63,0,0", b"2aSRGB,18,0,63,0", b"2aSRGB,19,0,0,63"))
    t.send(commands(b"2aFPALOUTA,18,17,18"))
    deadline = time.monotonic() + 3
    while True:
        frame = t.client.capture()
        green = (frame.palette[17] >> 8) & 255
        if 0 < green < 255:
            assert frame.primary[-1] == 17 and frame.palette[19] == 255
            break
        assert time.monotonic() < deadline, "advanced fade never showed an intermediate palette"
    frame = t.exchange(commands(b"2aGRGB,17", b"2aGRGB,19", b"2aFPALIN", b"2aGRGB,17"),
                       b"0,63,0\xff0,0,63\xff63,0,0\xff")
    assert frame.primary[-1] == 17 and frame.palette[17] == 0xff0000
    frame = t.exchange(commands(b"2aFPALOUT", b"2aGRGB,17", b"2aG,639,479", b"2CPYHP2WP",
                                b"2aG,639,479", b"2CPYBP2WP", b"2aG,639,479"),
                       b"0,0,0\xff18\xff0\xff18\xff")
    assert frame.primary[-1] == frame.alternate[-1] == 0
    assert all(rgb == 0 for rgb in frame.palette[:256])
    frame = t.exchange(commands(b"2CPYWP2VP", b"2aFPALIN", b"2aGRGB,18"), b"0,63,0\xff")
    assert frame.primary[-1] == 18 and frame.palette[18] == 0x00ff00


def check_sprites(t):
    t.case("sprite capture and drawing across the four graphics pages")
    # A 3x2 sprite with distinguishable rows and a transparent top-left pixel.
    rows = [[0, 11, 12], [13, 14, 15]]
    wire = [b"2aCLIP,0,639,0,479", b"2aERASE"]
    for y, row in enumerate(rows):
        for x, index in enumerate(row):
            wire += [f"2C,{index}".encode(), f"2aP,{x},{y}".encode()]
    wire += [b"2CPYWP2VP", b"2CPYWP2BP", b"2S,0,2,0,1,work", b"2SH,0,2,0,1,hid",
             b"2SV,0,2,0,1,vis", b"2SB,0,2,0,1,back", b"2C,77", b"2aFILL",
             b"2CPYWP2VP", b"2CPYWP2BP", b"2CCOL,0,3"]
    expected = bytearray()
    for suffix, name, page in [(b"", b"WoRk", b"WP"), (b"H", b"Hid", b"HP"),
                               (b"B", b"Back", b"BP"), (b"V", b"Vis", b"VP")]:
        for i, op in enumerate(b"DCRFP"):
            x = 10 + 3*i
            wire.append(b"2s" + bytes([op]) + suffix + f",{x},20,".encode() + name)
        if page != b"WP":
            wire.append(b"2CPY" + page + b"2WP")
        for i, op in enumerate(b"DCRFP"):
            for dy, row in enumerate(rows):
                for dx, pixel in enumerate(row[::-1] if op in b"RF" else row):
                    wire.append(f"2aG,{10+3*i+dx},{19+dy}".encode())
                    expected.extend(str(pixel if pixel or op == ord("P") else 77).encode() + b"\xff")
    frame = t.exchange(commands(*wire), bytes(expected))
    assert list(frame.primary[19*640+10:19*640+13]) == [77, 11, 12]

    t.exchange(commands(b"2aERASE", b"2aCLIP,0,1,0,1", b"2sD,-1,1,work", b"2aG,0,0",
                        b"2sF,-1,1,work", b"2aG,0,0", b"2aG,1,1", b"2sI,work", b"2sP,3,4,work",
                        b"2aG,3,3", b"2sI,work", b"2sP,3,4,work", b"2aG,3,3",
                        b"2sC,2147483647,-2147483648,work", b"2S,0,0,0,0,work", b"2sP,4,8,work", b"2aG,4,8",
                        b"2sFF", b"2sP,4,8,vis", b"2aG,4,8", b"2aCLIP,0,639,0,479"),
               b"0\xff11\xff13\xff13\xff0\xff0\xff0\xff")

    # Definitions leave the screen alone until DSS. Static sprites are
    # composited after animation frames; HP remains a separate page.
    t.case("overlapping simple sprites and demand-driven animation")
    t.exchange(commands(b"2C,3", b"2aFILL", b"2CPYWP2BP", b"2CPYWP2VP",
                        b"2C,4", b"2aFILL", b"2S,0,1,0,1,A", b"2C,5", b"2aFILL", b"2S,0,1,0,1,B",
                        b"2C,6", b"2aFILL", b"2S,0,1,0,1,C", b"2aERASE", b"2C,9", b"2aP,0,0",
                        b"2S,0,1,0,1,Dot", b"2SSA,20,50,50,1,A,C,2,1", b"2SS,50,50,2,Dot,2",
                        b"2CPYVP2WP", b"2aG,50,50"), b"3\xff")
    for index in (4, 5, 6, 5):
        frame = t.exchange(commands(b"2DSS", b"2aG,50,50", b"2aG,51,49"),
                           str(index).encode()+b"\xff9\xff")
        assert list(frame.primary[49*640+50:49*640+52]) == [index, 9]
    frame = t.exchange(commands(b"2CSS", b"2SS,52,50,2,Dot,1", b"2DSS",
                                b"2CPYHP2WP", b"2aG,51,49"), b"3\xff")
    assert list(frame.primary[49*640+50:49*640+54]) == [4, 4, 9, 3]
    t.exchange(commands(b"2SSA,20,50,50,1,NONE,C,1,1", b"2DSS", b"2aG,51,49",
                        b"2ACSS", b"2C,12", b"2aFILL", b"2DSS", b"2aG,51,49"), b"3\xff12\xff")


def check_pcx(t, root):
    t.case("indexed PCX resource loading and page snapshots")
    [marker] = root.rglob("syncterm-cache-name.txt")
    directory = marker.parent / "DDOFX" / "BBSTHEME"
    header = bytearray(128)
    header[:4] = bytes((10,5,1,8))
    struct.pack_into("<4H", header, 4, 12,18,14,19)
    header[65] = 1
    struct.pack_into("<H", header, 66, 4)
    palette = bytearray(768)
    palette[3*193:3*194] = bytes((192,149,63))
    tail = b"\x0c"+palette
    members = [(b"ART.PCX", 1, header+b"\xc2\xc1\x03\x09\x05\xc1\xff\x06\x09"+tail),
               (b"BAD.PCX", 1, header+b"\xc0\x04"+tail),
               (b"OTHER.PCX", 0, header+b"\xc4\x01\xc4\x02"+tail)]
    entry = struct.Struct("<12sIIHH")
    data = bytearray(entry.pack(b"\xff",0,0,0,0)*33)
    for slot, (name, kind, payload) in enumerate(members):
        entry.pack_into(data, slot*entry.size, name,len(data),len(payload),kind,0)
        data.extend(payload)
    (directory/"Art.S").write_bytes(data)
    t.exchange(commands(b"2fArt.", b"2C,90", b"2aFILL", b"2CPYWP2VP", b"2CPYWP2BP",
                        b"2wart.pcx", b"2aG,0,0", b"2aG,1,0", b"2aG,2,0", b"2aG,0,1", b"2aG,1,1",
                        b"2aG,2,1", b"2aG,3,1", b"2aGRGB,193", b"2WP2PCX"),
               b"193\xff193\xff3\xff5\xff255\xff6\xff90\xff48,37,15\xff")
    frame = t.exchange(commands(b"2wBAD.PCX", b"2wOTHER.PCX", b"2aG,1,1", b"2C,13", b"2aFILL", b"2CPYWP2HP",
                                b"2vART.PCX", b"2VP2PCX", b"2CPYHP2WP", b"2aG,1,1", b"2aG,639,479"),
                       b"255\xff255\xff90\xff")
    indices = list(frame.primary[640:644])
    assert indices == [5, 255, 6, 90], indices
    t.exchange(commands(b"2bART.PCX", b"2BP2PCX", b"2fmissing.", b"2C,8", b"2aFILL", b"2CPYWP2VP",
                        b"2aZEROPAL", b"2LWPPCX,BP", b"2CPYBP2WP", b"2aG,1,0", b"2aGRGB,193",
                        b"2LVPPCX,WP", b"2aG,1,1", b"2LBPPCX,VP", b"2CPYVP2WP", b"2aG,2,1"),
               b"193\xff48,37,15\xff255\xff6\xff")


def check_graphics(t, root):
    lite = t.check("Lite screen before graphics", b"Before", (7, 1), [(1, 1, "Before")])
    enter = commands(b"LIGMODE", b"LIGMODE")
    t.exchange(commands(b"dVCARD", b"LIGMODE", b"dVIDMODE",
                        b"4DEFM,1,1,\x1d(LIGMODE)\x1d(LIGMODE)", b"4RUNM,1", b"dVIDMODE"),
               b"S\xff0\xff0\xff")
    frame = t.exchange(enter + commands(b"dVIDMODE", b"2C,37", b"2aP,639,479", b"2aG,639,479"),
                       b"1\xff37\xff")
    assert (frame.metadata["plane_width"], frame.metadata["plane_height"]) == (640, 480)
    assert frame.primary[-1] == 0
    # The work, background and hidden pages are offscreen. The second
    # entry pair leaves the current scene intact. G uses decimal indexes.
    frame = t.exchange(enter + commands(b"2CPYWP2BP", b"2aERASE", b"2CPYBP2VP", b"2CPYHP2WP",
                                        b"2aG,639,479", b"2aPX,639,479", b"2aG,639,479"),
                       b"37\xff0\xff")
    assert frame.primary[-1] == frame.alternate[-1] == 37
    t.exchange(commands(b"2C,8", b"2aFILL", b"2CPYWP2HP", b"2C,3", b"2aFILL", b"2aSRGB,3,63,21,0",
                        b"2aSTOREPAL", b"2aZEROPAL", b"2aGRGB,3", b"2aRESTOREPAL", b"2aGRGB,3",
                        b"2CCOL,3,1", b"2CUTWP2HPT,0,1,0,1,638,479", b"2CPYHP2BP"),
               b"0,0,0\xff63,21,0\xff")
    frame = t.exchange(commands(b"2CCOL,3,0", b"2CUTWP2HPT,0,1,0,1,638,479", b"2CPYHP2VP"))
    for plane in (frame.primary, frame.alternate):
        assert list(plane[-3:]) == [8, 3, 3]
        assert list(plane[-643:-640]) == [8, 3, 3]
    assert frame.palette[3] == 0xff5500
    t.exchange(commands(b"2CPYBP2WP", b"2aG,639,479", b"2CPYVP2WP", b"2aG,639,479",
                        b"2CUTBP2WP,0,1,0,1,639,479", b"2aG,639,479"), b"8\xff3\xff3\xff")
    frame = t.exchange(b"\x1b[H\x1b[41m " + commands(b"2CPYVP2WP", b"2aG,3,4",
                                                       b"2aSRGB,4,0,63,63"), b"4\xff")
    assert frame.primary[4 * 640 + 3] == 4 and frame.palette[4] == 0x00ffff
    check_drawing(t)
    check_sprites(t)
    check_pcx(t, root)
    t.exchange(b"\x1b[75;45H" + b"\x1b[6n", b"\x1b[75;45R")
    restored = t.check("Lite display and cursor restored", commands(b"2GEXIT", b"dVIDMODE") + b"!",
                       (8, 1), [(1, 1, "Before!")], reset=False, expected=b"0\xff")
    assert restored.metadata["plane_height"] == lite.metadata["plane_height"]
    assert restored.palette[3] == lite.palette[3]
    t.exchange(enter + commands(b"2aGRGB,3", b"2aG,639,479", b"2LWPPCX,WP", b"2aG,1,0"),
               b"63,21,0\xff0\xff193\xff")
    check_fades(t)
    t.exchange(commands(b"2GEXIT"))


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
        check_graphics(t, fixture[1])
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
