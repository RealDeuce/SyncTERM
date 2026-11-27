"""DDO F/X over a live connection with ordinary ANSI before and after commands."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from binascii import crc_hqx
import os
import struct
import time
import zipfile
from pathlib import Path

from st_ddofx_package_test import header, subpacket, package
from st_migrated_control_test import connection
from st_terminal_conformance_test import Terminal


def receive_header(t, kind):
    # A recipient may advertise ZRINIT both at startup and in reply to
    # ZRQINIT. Read one frame at a time even when TCP combines them.
    while True:
        data = bytearray()
        while len(data) < 20:
            part = t.peer.recv(20 - len(data))
            assert part, data
            data.extend(part)
        assert data[:4] == b"**\x18B" and data[18:] == b"\r\n", data
        fields = bytes.fromhex(data[4:18].decode("ascii"))
        assert crc_hqx(fields[:5], 0) == int.from_bytes(fields[5:], "big")
        if fields[0] != 8:
            assert t.peer.recv(1) == b"\x11"
        if fields[0] == kind:
            return
        assert fields[0] == 1 and kind in (5, 8, 9), data


def check_package_downloads(t):
    t.case("ZMODEM command packages install before the next DDO query")

    def transfer(name, contents):
        info = name + b"\x00" + str(len(contents)).encode() + b"\x00"
        t.send(header(4) + subpacket(info, b"k"))
        receive_header(t, 9)
        t.send(header(10) + subpacket(contents, b"h") + header(11, len(contents)))
        receive_header(t, 1)

    # A damaged member must leave the earlier complete asset in place.
    broken = package([("TUNE.MOD", b"damaged replacement")], zipfile.ZIP_STORED)
    broken = broken.replace(b"damaged replacement", b"invalid replacement")
    t.send(header(0))
    receive_header(t, 1)
    music = package([("nested/TUNE.MOD", b"first"), ("DRUM.S3M", b"second")], zipfile.ZIP_DEFLATED)
    transfer(b"$li$mA-0.zip", music)
    record = struct.Struct("<12sIIHH")
    base = bytearray(record.pack(b"\xff", 0, 0, 0, 0) * 33)
    base[:24] = record.pack(b"OLD.FX", len(base), 1, 0, 13)
    transfer(b"$LI$T013.ZIP", package([("MARK.THE", b"theme"), ("Art.M", base+b"X")], zipfile.ZIP_STORED))
    instructions = (b"[RESOURCE]=ART.M\r\n[DEL]=old.fx\r\n[ADD]=SIGN.FX,324\r\n"
                    b"[RESOURCE]=NONE\r\n[ADD]=DROP.BIN")
    transfer(b"$LI$T014.ZIP", package([("DDOFX.INI",instructions), ("sign.fx", commands(b"7SRET,33")),
                                       ("drop.bin",b"obsolete")], zipfile.ZIP_DEFLATED))
    transfer(b"$LI$Mbad.ZIP", broken)
    transfer(b"$LI$R001.ZIP", package([("TONE.RLP", b"tone"), ("ddofx.ini", b"[REALPLAY]=tone.rlp,8000")],
                                       zipfile.ZIP_DEFLATED))
    for kind, name in [(b"F", "PROMPT.FNT"), (b"S", "ENTRY.SCR"), (b"P", "LOGIN.THE")]:
        transfer(b"$LI$" + kind + b"001.ZIP", package([(name, b"asset")], zipfile.ZIP_STORED))
    transfer(b"$LI$Q013.ZIP", music)
    transfer(b"$LI$F013.ZIP", b"incomplete archive")
    assert not list((t.root / "cache").glob("*/DDOFX/MUSIC/TUNE.MOD"))
    t.send(header(8))
    receive_header(t, 8)
    t.send(b"OO")
    t.client.wait_for(lambda frame: not frame.application.get("transfer.active"))
    t.exchange(commands(b"dmTUNE.MOD", b"dmDRUM.S3M", b"dtMARK.THE"),
               b"5,2024,2,29,12,34,18\xff6,2024,2,29,12,34,18\xff5,2024,2,29,12,34,18\xff")
    cache, = (t.root / "cache").glob("*/DDOFX")
    assert (cache / "MUSIC" / "TUNE.MOD").read_bytes() == b"first"
    t.exchange(commands(b"dRESOURCE,ART.M,OLD.FX,13", b"dRESOURCE,ART.M,SIGN.FX,324",
                        b"2fArt.", b"7DEFRET,33,installed", b"*SIGN.FX"), b"NO\xffYES\xffinstalled")
    theme = cache / "BBSTHEME"
    assert {p.name for p in theme.iterdir()} == {"MARK.THE", "Art.M", "TONE.RLP"}
    assert (theme / "Art.M").read_bytes().endswith(b"\xe5\xff\xe6")
    assert int((theme / "Art.M").stat().st_mtime) == int((theme / "MARK.THE").stat().st_mtime)
    for path in ("FONTS/PROMPT.FNT", "SCRIPTS/ENTRY.SCR", "THEMES/LOGIN.THE"):
        assert (cache / path).read_bytes() == b"asset"
    downloads = t.root / "downloads"
    assert {p.name for p in downloads.iterdir()} == {"$LI$Q013.ZIP", "$LI$F013.ZIP", "$LI$Mbad.ZIP"}


def check_background_download(t):
    t.case("background ZMODEM keeps the scene interactive and owns the wire")
    frame = t.exchange(commands(b"LIGMODE", b"LIGMODE", b"2C,3", b"2aFILL", b"2CPYWP2VP",
                                b"4MKEY,1,46,99,0,ordinary,\x1d(2C,5)\x1d(2aFILL)\x1d(2CPYWP2VP)",
                                b"7CEVENT,1,0,20,0,0,0,0,0,0,0,parked,",
                                b"7ZMSTEALTH,1",
                                b"7CEVENT,2,0,20,0,0,0,0,0,0,0,local,\x1d(2C,6)\x1d(2aFILL)\x1d(2CPYWP2VP)",
                                b"4MKEY,2,46,99,1,stealth,\x1d(7AEVENT,1)\x1d(7AEVENT,2)",
                                b"7ZMSTEALTH,0", b"7ZMBKGRND,1"))
    def bottom(f):
        m = f.metadata
        x = m["destination_x"] + m["destination_width"] // 2
        y = m["destination_y"] + m["destination_height"] - 1
        return f.rgb[y * m["output_width"] + x]

    assert bottom(frame) == frame.palette[3]
    t.send(header(0))
    receive_header(t, 1)
    t.client.wait_for(lambda f: f.application.get("transfer.active") and bottom(f) != frame.palette[3])
    duplicate = t.root / "downloads" / "DUP.BIN"
    duplicate.write_bytes(b"existing file")
    t.send(header(4) + subpacket(b"DUP.BIN\x001\x00", b"k"))
    t.client.wait_for(lambda f: "Overwrite" in f.text and "Rename" in f.text)
    t.client.key("enter")  # Skip is the default.
    receive_header(t, 5)
    assert duplicate.read_bytes() == b"existing file"
    t.client.wait_for(lambda f: "Rename" not in f.text and bottom(f) != frame.palette[3])
    t.client.text("xc")
    active = t.client.wait_for(lambda f: f.primary and f.primary[0] == 6)
    assert bottom(active) != active.palette[6]
    t.send(header(8))
    receive_header(t, 8)
    t.send(b"OO")
    t.client.wait_for(lambda f: not f.application.get("transfer.active") and bottom(f) == f.palette[6])
    t.input(b"parked")
    t.client.text("c")
    t.input(b"ordinary")
    t.exchange(commands(b"2RESET", b"2GEXIT"))


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


def check_graphics_reset(t):
    t.case("scene reset on a live graphics connection")
    frame = t.exchange(commands(b"2C,23", b"2aFILL", b"2CPYWP2BP", b"2CPYWP2VP", b"2WP2PCX",
                                b"2S,1,2,1,2,S", b"2SS,10,10,1,S,1", b"2FSELECT,32", b"2aSRGB,23,12,34,56",
                                b"2RESET", b"dVIDMODE", b"2aG,100,100", b"2CPYBP2WP", b"2aG,100,100",
                                b"2CPYHP2WP", b"2aG,100,100", b"2aGRGB,23", b"2sP,10,10,S", b"2aG,10,10",
                                b"2X,40,43,20,23,11,0,0,VP,A", b"2DSS"),
                       b"1\xff0\xff0\xff0\xff12,34,56\xff0\xff")
    for plane in (frame.primary, frame.alternate):
        assert not any(plane)
    # Both the saved page and assigned M resource remain accessible.
    frame = t.exchange(commands(b"2LWPPCX,WP", b"2aG,100,100", b"2S,1,2,1,2,S", b"2DSS",
                                b"2F,A.BMF,1", b"2X,40,43,20,23,11,0,0,VP,A"),
                       b"23\xff")
    assert frame.primary[10*640+10] == 0 and frame.primary[20*640+41] == 11


def check_marquees(t, root):
    t.case("marquees draw the initial text on WP and scroll a pixel at a time on VP")
    marker, = root.rglob("syncterm-cache-name.txt")
    cache = marker.parent / "DDOFX"
    font = bytearray(4096)
    font[0x82 * 16:0x83 * 16] = bytes([0x80]) * 16
    (cache / "FONTS" / "Strip.fnt").write_bytes(font)
    record = struct.Struct("<12sIIHH")
    bank = bytearray(record.pack(b"\xff", 0, 0, 0, 0) * 33)
    for slot, (name, length) in enumerate([(b"LINE.TXT", 20000), (b"LARGE.TXT", 20001)]):
        record.pack_into(bank, slot*24, name, len(bank), length, 0, 0)
        bank.extend((b"\x82 " * ((length + 1) // 2))[:length])
    (cache / "BBSTHEME" / "Scroll.M").write_bytes(bank)
    off = 464 * 640 + 624
    wire = commands(b"2fScroll.", b"fStrip.fnt", b"2C,39", b"2aFILL", b"2CPYWP2VP", b"~PAUSEBAK",
                    b"5CMARQEE,1,624,479,2,12,3,5,LINE.TXT", b"2aG,624,464",
                    b"5CMARQEE,300,20,80,1,14,4,10,LINE.TXT", b"5SHOWMQ,1", b"5SHOWMQ,300",
                    b"2aG,624,464", b"2aG,625,479", b"2aG,639,479", b"2aG,20,65",
                    b"5CMARQEE,2,12,159,2,9,5,10,LARGE.TXT", b"5SHOWMQ,2", b"2aG,12,144")
    frame = t.exchange(wire, b"39\xff12\xff3\xff3\xff14\xff39\xff")
    assert list(frame.primary[off:off+16]) == [39] * 16
    frame = t.exchange(commands(b"2CPYWP2VP"))
    assert list(frame.primary[off:off+16]) == [12] + [3] * 15
    t.send(commands(b"~UNPAUSEBAK"))
    frame = t.client.wait_for(lambda f: f.primary[off+1] == 12)
    assert list(frame.primary[off:off+16]) == list(frame.alternate[off:off+16]) == [3,12] + [3] * 14
    t.exchange(commands(b"5STOPMQ,1", b"5KILLMQ,300", b"2aERASE", b"2CPYWP2VP", b"w6",
                        b"5SHOWMQ,300", b"2aG,20,65"), b"0\xff")
    frame = t.exchange()
    assert not any(frame.primary[off:off+16])
    t.send(commands(b"5SHOWMQ,1"))
    t.client.wait_for(lambda f: 12 in f.primary[off:off+16])
    t.exchange(commands(b"5CMARQEE,0", b"2aERASE", b"5SHOWMQ,1", b"2aG,624,464", b"fIBMSTD.FNT"), b"0\xff")


def check_clocks(t, root):
    t.case("analog and text clocks on the visual page, local time, offsets and pause")
    marker, = root.rglob("syncterm-cache-name.txt")
    fonts = marker.parent / "DDOFX" / "FONTS"
    # Each scanline encodes the ASCII byte, so the displayed time can be read back.
    font = bytearray(4096)
    for c in b"0123456789:":
        font[c*16:(c+1)*16] = bytes([c]) * 16
    (fonts / "Digits.fnt").write_bytes(font)

    def value(frame):
        data = frame.primary[464*640+576:464*640+640]
        assert data == frame.alternate[464*640+576:464*640+640]
        assert set(data) == {3, 12}
        return bytes(sum((data[i+j] == 12) << (7-j) for j in range(8))
                     for i in range(0, 64, 8)).decode("ascii")

    center = 100*640 + 100
    frame = t.exchange(commands(b"2C,39", b"2aFILL", b"2CPYWP2VP",
                                b"5SCLOCK,30,100,100,20,60,61,62,63,64,0,0,0",
                                b"5SOCLOCK,30,576,479,12,3,-63,2,-5,Digits.fnt"))
    assert frame.primary[center] == frame.primary[-1] == 39
    started = int(time.time())
    frame = t.exchange(commands(b"5SHWCLK,30", b"5UOCLOCK,30", b"2aG,100,100", b"2aG,639,479"),
                       b"39\xff39\xff")
    first = value(frame)
    def expected(stamp):
        local = time.localtime(stamp)
        seconds = (3600 * (local.tm_hour-5) + 60 * (local.tm_min+2) + local.tm_sec-63) % 86400
        return f"{seconds//3600:02}:{seconds//60%60:02}:{seconds%60:02}"
    assert first in {expected(stamp) for stamp in range(started, int(time.time())+1)}, first
    for plane in (frame.primary, frame.alternate):
        assert plane[center] in (62,63,64) and plane[center-20*640] == 61
        assert plane[center-20*640-20] == 39
        assert any(60 in plane[center+y*640-8:center+y*640+9] for y in range(-8, 9))
    t.client.wait_for(lambda f: value(f) != first)
    t.exchange(commands(b"5POCLOCK,30", b"5STPCLK,30", b"2CPYWP2VP", b"w22"))
    frame = t.exchange()
    assert frame.primary[center] == frame.primary[-1] == 39
    frame = t.exchange(commands(b"5SHWCLK,30", b"5UOCLOCK,30"))
    assert frame.primary[center] != 39 and value(frame)[2] == ":"
    frame = t.exchange(commands(b"5SCLOCK,0", b"5KOCLOCK,30", b"2CPYWP2VP", b"5SHWCLK,30", b"5UOCLOCK,30",
                                b"5SOCLOCK,1,0,15,14,4,0,0,0,absent.fnt", b"5UOCLOCK,1"))
    assert frame.primary[center] == frame.primary[-1] == 39 and set(frame.primary[:64]) <= {4,14}
    assert any(14 in frame.primary[row*640:row*640+64] for row in range(16))
    # An active marquee stays in front even when a clock is shown between scroll ticks.
    t.send(commands(b"5CMARQEE,2,96,111,2,12,3,8,LINE.TXT", b"5SHOWMQ,2"))
    t.client.wait_for(lambda f: f.primary[100*640+100] in (3,12))
    frame = t.exchange(commands(b"5SCLOCK,1,100,100,20,60,61,62,63,64,0,0,0", b"5SHWCLK,1"))
    for plane in (frame.primary, frame.alternate):
        assert set(plane[100*640+96:100*640+112]) <= {3,12}
        assert plane[center-20*640] == 61
    t.exchange(commands(b"5SOCLOCK,0", b"5SCLOCK,0", b"5KILLMQ,2", b"2CPYWP2VP"))


def check_flics(t, root):
    t.case("FLI/FLC frames, palette, desktop layering, play-once and paused playback")
    marker, = root.rglob("syncterm-cache-name.txt")
    theme = marker.parent / "DDOFX" / "BBSTHEME"
    def chunk(kind, data):
        return struct.pack("<IH", 6+len(data), kind) + data
    def frame(*chunks):
        data = b"".join(chunks)
        return struct.pack("<IHH8x", 16+len(data), 0xf1fa, len(chunks)) + data
    def movie(name, magic, frames):
        prefix = struct.pack("<IH10x", 16, 0xf100)
        body = prefix + b"".join(frames)
        header = bytearray(128)
        struct.pack_into("<I5H", header, 0, 128+len(body), magic, len(frames), 5, 3, 8)
        (theme / name).write_bytes(header + body)

    pal = b"\x01\0\x25\x01"
    first = frame(chunk(18, b"meta"), chunk(4, pal + bytes([252,128,4])), chunk(16, bytes(range(15))))
    # Only the middle row changes, leaving both neighboring scanlines intact.
    second = frame(chunk(12, struct.pack("<HH", 1, 1) + bytes([2,0,254,15,1,2,8,9])))
    # Skip row zero, update the last odd pixel, then repeat one word twice.
    third = frame(chunk(7, struct.pack("<4H", 1, 0xffff, 0x8017, 1) + bytes([0,254,21,22])))
    movie("DELTA.FLC", 0xaf12, [first, second, third])
    brun = bytes([1,2,11,253,12,13,14])
    movie("BYTE.FLI", 0xaf11, [frame(chunk(13, b"")), frame(chunk(11, pal+bytes([1,2,3])),
                                                          chunk(15, brun*3))])
    movie("BAD.FLC", 0xaf12, [frame(chunk(4, pal+bytes([0,0,0])), chunk(15, bytes([1,6,99])))])
    x,y = 635,477
    def rows(f):
        result = [list(f.primary[r*640+x:r*640+x+5]) for r in range(y,y+3)]
        assert result == [list(f.alternate[r*640+x:r*640+x+5]) for r in range(y,y+3)]
        return result
    f = t.exchange(commands(b"2C,44", b"2aFILL", b"2CPYWP2VP", b"2RESA,2", b"2aSRGB,37,6,7,8",
                            b"5OFLIC,300,635,477,3,1,5,1,delta.flc"))
    assert rows(f) == [[44]*5]*3
    t.send(commands(b"5UFLIC,300"))
    f = t.client.wait_for(lambda f: rows(f)[1] == [5,6,7,8,9])
    assert rows(f) == [[0,1,2,3,4], [5,6,7,8,9], [10,11,12,13,14]]
    assert f.palette[37] == 0xff8204
    t.client.wait_for(lambda f: rows(f)[1] == [15,15,7,8,9])
    t.client.wait_for(lambda f: rows(f)[1] == [21,22,21,22,23])
    f = t.exchange(commands(b"2aG,639,479", b"5UFLIC,0", b"2CPYWP2VP", b"w9"), b"44\xff")
    assert rows(f) == [[44]*5]*3
    t.send(commands(b"2RESA,1", b"5OFLIC,1,635,477,2,0,8,0,BYTE.FLI", b"5UFLIC,0"))
    t.client.wait_for(lambda f: rows(f) == [[0]*5]*3)
    f = t.client.wait_for(lambda f: rows(f) == [[11,11,12,13,14]]*3)
    assert f.palette[37] == 0xff8204
    t.exchange(commands(b"5PFLIC,0", b"2CPYWP2VP", b"w6"))
    assert rows(t.exchange()) == [[44]*5]*3
    t.send(commands(b"5UFLIC,1"))
    t.client.wait_for(lambda f: rows(f) != [[44]*5]*3)
    t.exchange(commands(b"5SFLIC,1", b"5OFLIC,1,635,477,2,1,30,1,BYTE.FLI", b"5UFLIC,1", b"w4",
                        b"2aGRGB,37"), b"1,2,3\xff")
    f = t.exchange(commands(b"2CPYWP2VP", b"5OFLIC,2,635,477,1,1,30,1,BAD.FLC", b"5UFLIC,2", b"w4",
                            b"2aGRGB,37", b"5OFLIC,0"), b"1,2,3\xff")
    assert rows(f) == [[44]*5]*3

    # The same film row crosses a marquee, a text clock and the uncovered desktop.
    t.send(commands(b"5SOCLOCK,1,572,479,14,4,0,0,0,Digits.fnt", b"5UOCLOCK,1",
                    b"5CMARQEE,1,627,479,1,12,3,10,LINE.TXT", b"5SHOWMQ,1"))
    t.client.wait_for(lambda f: f.primary[477*640+634] in (3,12))
    t.send(commands(b"5OFLIC,1,633,477,1,1,20,0,DELTA.FLC", b"5UFLIC,1"))
    f = t.client.wait_for(lambda f: f.primary[477*640+637] == 4)
    for plane in (f.primary, f.alternate):
        strip = plane[477*640+633:477*640+638]
        assert set(strip[:2]) <= {3,12} and strip[2] in (4,14) and list(strip[3:]) == [3,4]
    t.exchange(commands(b"5CMARQEE,0", b"5SOCLOCK,0", b"2CPYWP2VP"))


def check_text_scale(t, root):
    t.case("graphic text uses the native FNT pixels and shared bottom-anchored cell dimensions")
    marker, = root.rglob("syncterm-cache-name.txt")
    font = bytearray(4096)
    for c in b"A0123456789:":
        font[c*16+8] = 0x80
        font[c*16+15] = 0x20
    (marker.parent / "DDOFX" / "FONTS" / "Small.fnt").write_bytes(font)
    frame = t.exchange(commands(b"2C,39", b"2aFILL", b"2CPYWP2VP", b"1SCALE,4,8", b"fSmall.fnt",
                            b"2XTWP,632,7,12,3,AA", b"2aG,632,0", b"2aG,636,0", b"2aG,638,7",
                            b"2aG,631,0", b"2aG,632,8", b"2CPYWP2VP"), b"12\xff12\xff12\xff39\xff39\xff")
    for plane in (frame.primary, frame.alternate):
        assert list(plane[632:640]) == [12,3,3,3]*2
    # DEC cursor positioning still addresses the 80 by 100 virtual text page.
    frame = t.exchange(b"\x1b[3;4H\x1b[31;44mAA\x1b[6n", b"\x1b[3;6R")
    for plane in (frame.primary, frame.alternate):
        assert list(plane[16*640+12:16*640+20]) == [4,1,1,1]*2
    frame = t.exchange(commands(b"5CLBOX,1,0,15,2,2,10,1,15,2,14,3,2,0", b"5LBMODLST,1,0,1,0,AA,,",
                            b"5LBSHWLST,1", b"5SOCLOCK,1,608,479,11,3,0,0,0,Small.fnt", b"5UOCLOCK,1",
                            b"5CTXTFLD,1,Small.fnt,AA,,,100,9,3,1,4,15,0,,0,0,0,0,0,0,0,0,0,",
                            b"2aG,100,2", b"2aG,104,2", b"2aG,116,2"), b"15\xff15\xff39\xff")
    for plane in (frame.primary, frame.alternate):
        assert list(plane[:8]) == [15,2,2,2]*2
        assert list(plane[472*640+608:473*640]) == [11,3,3,3]*8
    t.exchange(commands(b"2fScroll.", b"5CMARQEE,1,632,7,2,13,5,10,LINE.TXT", b"5SHOWMQ,1",
                        b"2aG,639,7", b"2aG,639,8", b"5CMARQEE,0"), b"5\xff39\xff")
    # Both objects retain their bottom anchor. At 8 by 16 the list's
    # first row is above the display, leaving the unhighlighted second row.
    frame = t.exchange(commands(b"1SCALE,8,16", b"1SCALE,0,7", b"2CPYWP2VP", b"5LBSHWLST,1",
                                b"5SHOWTFLD,1", b"2aG,108,2", b"2aG,116,2"), b"15\xff1\xff")
    for plane in (frame.primary, frame.alternate):
        assert list(plane[8*640:8*640+16]) == [10] + [1]*7 + [10] + [1]*7

    t.exchange(commands(b"1SCALE,4,8", b"2RESET", b"2XTWP,636,7,13,6,A", b"2aG,636,0",
                        b"1SCALE,8,16", b"fIBMSTD.FNT"), b"13\xff")


def check_sprite_banks(t, root):
    t.case("sprite bank files, slot order, row orientation and erase after load")
    marker, = root.rglob("syncterm-cache-name.txt")
    theme = marker.parent / "DDOFX" / "BBSTHEME"
    dat, raw = theme / "BANK.DAT", theme / "BANK.RAW"
    record = struct.Struct("<BB8sIHHH")
    t.exchange(commands(b"2sFF", b"2aCLIP,0,639,0,479", b"2aERASE",
                        b"2C,10", b"2aP,1,1", b"2C,20", b"2aP,2,2", b"2S,1,2,1,2,logo",
                        b"2STORES,BANK", b"2sP,5,6,LOGO", b"2aG,5,5"), b"10\xff")
    assert dat.read_bytes() == record.pack(1, 4, b"LOGO", 0, 104, 2, 2) + bytes(3199 * 20)
    assert raw.read_bytes() == bytes([0, 20, 10, 0]) + bytes(100)

    # A sparse foreign bank with a nonzero legacy pointer, an opaque tail,
    # and a one-row image at the end of the table.
    foreign = bytearray(3200 * record.size)
    record.pack_into(foreign, 20, 2, 1, b"A", 0xdeadbeef, 7, 2, 2)
    record.pack_into(foreign, 3199 * 20, 1, 1, b"B", 0, 2, 2, 1)
    dat.write_bytes(foreign)
    payload = bytes([3, 4, 1, 2, 90, 91, 92, 8, 9])
    raw.write_bytes(payload[:-1])
    t.exchange(commands(b"2LOADS,1,bank", b"2aERASE", b"2sP,3,4,LOGO", b"2aG,3,3"), b"10\xff")
    assert dat.exists() and raw.exists()
    raw.write_bytes(payload)
    t.exchange(commands(b"2loads,0,bank", b"2aERASE", b"2sP,3,4,LOGO", b"2aG,3,3",
                        b"2sP,3,4,a", b"2sP,8,8,b", b"2aG,3,3", b"2aG,4,4", b"2aG,9,8",
                        b"2sI,a", b"2stores,second"), b"0\xff1\xff4\xff9\xff")
    assert (theme / "SECOND.RAW").read_bytes() == bytes([1, 2, 3, 4]) + payload[4:]
    assert (theme / "SECOND.DAT").read_bytes()[3199*20:] == foreign[3199*20:]
    t.exchange(commands(b"2sFF", b"2LOADS,1,BANK", b"2sP,3,4,A", b"2aG,4,4",
                        b"2STORES,../escaped", b"2sFF"), b"4\xff")
    assert not dat.exists() and not raw.exists()
    assert not (theme.parent / "ESCAPED.DAT").exists()


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


def check_image_commands(t):
    t.case("scaled, rotated, outlined and smoothed page rectangles")
    # Source from top down is [1,2], [3,0]. Scale up against the
    # top/right edges, then shrink toward the same lower-left corner.
    seed = [b"2aERASE", b"2C,1", b"2aP,636,2", b"2C,2", b"2aP,637,2",
            b"2C,3", b"2aP,636,3", b"2CPYWP2BP"]
    frame = t.exchange(commands(*seed, b"5SCALE,BP,636,637,2,3,4,4", b"2CPYBP2VP"))
    for plane in (frame.primary, frame.alternate):
        assert [list(plane[y*640+636:(y+1)*640]) for y in range(4)] == [
            [1,1,2,2], [1,1,2,2], [3,3,0,0], [3,3,0,0]]
    t.exchange(commands(b"5SCALE,VP,636,639,0,3,2,2", b"2CPYVP2WP", b"2aG,636,0", b"2aG,637,2",
                        b"2aG,636,3", b"2aG,638,2"), b"0\xff2\xff3\xff0\xff")

    # Rotate about the top-left source pixel. VP already has a 7 where
    # the transparent source pixel would land.
    t.exchange(commands(b"2C,7", b"2aFILL", b"2CPYWP2HP", b"5ROTATE,VP,HP,636,637,2,3,90,636,2,0,10,20",
                        b"2CPYHP2WP", b"2aG,10,20", b"2aG,10,21", b"2aG,9,20", b"2aG,9,21"),
               b"1\xff2\xff3\xff7\xff")
    t.exchange(commands(b"2C,7", b"2aFILL", b"5ROTATE,HP,WP,9,10,20,21,360,-1,-1,7,-1,-1", b"2aG,9,20",
                        b"5ROTATE,WP,WP,9,10,20,21,90,9,20,0,0,0", b"2aG,0,0"), b"3\xff7\xff")
    # External pivot and a partly clipped 45-degree rotation. The
    # selected source point lies four pixels to the right of its pivot.
    t.exchange(commands(b"5ROTATE,HP,WP,10,10,20,20,45,6,20,0,-2,0", b"2aG,1,3",
                        b"2aG,0,2", b"2aG,2,3"), b"1\xff1\xff7\xff")
    frame = t.exchange(commands(b"2aERASE", b"2C,6", b"2aP,10,10", b"2C,255", b"2aP,12,12",
                                b"2CPYWP2VP", b"5OUTLN,VP,8,12,8,12,6,9"))
    for plane in (frame.primary, frame.alternate):
        for y in range(8,13):
            for x in range(8,13):
                expected = 6 if (x,y) == (10,10) else 9 if (x,y) == (12,12) or max(abs(x-10),abs(y-10)) == 1 else 0
                assert plane[y*640+x] == expected

    # Center red 60 and four black diagonals average to DAC red 12.
    # Select an independent palette with just one matching index.
    t.exchange(commands(b"2aSTOREPAL", b"2aZEROPAL", b"2aSRGB,30,60,0,0", b"2aSRGB,31,12,0,0",
                        b"2aERASE", b"2C,30", b"2aP,0,0", b"2aP,20,20", b"5ALIAS,WP,20,20,20,20,29,0",
                        b"2aG,20,20", b"5ALIAS,WP,20,20,20,20,-1,255", b"2aG,20,20",
                        b"5ALIAS,WP,20,20,20,20,30,0", b"2aG,20,20", b"2aG,0,0", b"2aRESTOREPAL"),
               b"30\xff30\xff31\xff30\xff")


def check_grid(t):
    t.case("one-based tile map, transparent foreground and scrolling viewport")

    def row(*wire):
        frame = t.exchange(commands(*wire))
        start = 476 * 640 + 634
        actual = list(frame.primary[start:start+6])
        assert actual == list(frame.alternate[start:start+6])
        return actual

    # Two 3x2 cells in the bottom-right of VP. The first frame of the
    # foreground has a single opaque pixel over the background.
    assert row(b"~PAUSEBAK", b"2C,8", b"2aFILL", b"2CPYWP2VP",
               b"2C,3", b"2aFILL", b"2S,0,2,0,1,Grass", b"2C,4", b"2aFILL",
               b"2S,0,2,0,1,Sand", b"2aERASE", b"2C,9", b"2aP,1,0", b"2S,0,2,0,1,A",
               b"2C,12", b"2aP,1,0", b"2S,0,2,0,1,B", b"5DEFGRID,634,476,3,3,2,2,3,2",
               b"5FILGRID,Grass,Grass,1", b"5CHGGRID,3,2,Sand,Sand,1",
               b"5CHGGRID,1,1,A,A,2") == [8]*6
    assert row(b"5DRWGRID") == [3,9,3,3,3,3]
    assert row(b"5POSTILE,1,1,3,2", b"5POSGRID,2,2") == [3,3,3,4,9,4]
    assert row(b"5MOVTILE,3,2,-1,0") == [3,9,3,4,4,4]
    assert row(b"5KILTILE,2,2", b"5MOVGRID,-1,-1") == [3]*6
    assert row(b"5CHGGRID,1,1,A,B,2", b"5DRWGRID", b"5MOVGRID,2147483647,0",
               b"5DEFGRID,1,1,151,3,2,2,3,2") == [3,9,3,3,3,3]

    t.send(commands(b"2sPV,637,477,Sand", b"~UNPAUSEBAK"))
    deadline = time.monotonic() + 3
    while True:
        pixels = t.client.capture().primary
        if pixels[476*640+635] == 12:
            assert pixels[476*640+637] == 4
            break
        assert time.monotonic() < deadline, "animated tile did not advance during idle input"
        time.sleep(0.03)

    assert row(b"5KILGRID", b"5DRWGRID") == [8]*6


def check_bitmap_fonts(t, root):
    t.case("bitmap font selection, scaling, outlining and rotation")
    [marker] = root.rglob("syncterm-cache-name.txt")
    # Legacy A is 3x3 in a six-byte slot. Its rows use one byte each.
    # With the source margins, the unscaled 4x4 image is .#../.##./...#/....
    legacy = bytearray(194 + 96 * 6)
    legacy[:2] = bytes((2, 3))
    legacy[2 + 33] = legacy[98 + 33] = 3
    legacy[194 + 33*6:194 + 33*6 + 3] = bytes((128, 192, 32))
    # In a four-bank font A is glyph 1 of bank 1. The comma and byte 0x82
    # each select their own bank and exercise literal text after the page.
    extended = bytearray(2 + 4 * (128 + 64*3))
    extended[:2] = bytes((129, 3))
    for code, bits in [(65, 32), (44, 64), (130, 128)]:
        bank = 2 + (code // 64) * 320
        slot = code % 64
        extended[bank + slot] = extended[bank + 64 + slot] = 3
        pos = bank + 128 + slot*3
        extended[pos:pos+3] = bytes((bits, bits, bits))
    record = struct.Struct("<12sIIHH")
    data = bytearray(record.pack(b"\xff", 0, 0, 0, 0)*33)
    for i, (name, payload) in enumerate([(b"A.BMF", legacy), (b"B.BMF", extended),
                                          (b"BAD.BMF", legacy[:-1])]):
        record.pack_into(data, i*24, name, len(data), len(payload), 0, 1)
        data.extend(payload)
    (marker.parent/"DDOFX"/"BBSTHEME"/"Fonts.M").write_bytes(data)

    t.exchange(commands(b"2fFonts.", b"2C,7", b"2aFILL", b"2CPYWP2VP", b"2CPYWP2BP",
                        b"2F,A.BMF,1", b"2F,B.BMF,32", b"2FSELECT,1"))
    glyph = [[7,11,7,7], [7,11,11,7], [7,7,7,11], [7,7,7,7]]
    for i, page in enumerate([b"WP", b"BP", b"HP", b"VP"]):
        rotated = glyph
        for _ in range(i):
            rotated = list(zip(*rotated[::-1]))
        wire = [b"2X,40,43,20,23,11,0," + str(90*i).encode() + b"," + page + b",A"]
        if page != b"VP":
            wire.append(b"2CPY" + page + b"2VP")
        frame = t.exchange(commands(*wire))
        assert all(list(plane[y*640+40:y*640+44]) == list(row)
                   for plane in (frame.primary, frame.alternate) for y,row in enumerate(rotated, 20))
        # Publishing a page updates HP. Restore the empty background for the next angle.
        t.exchange(commands(b"2aFILL", b"2CPYWP2BP", b"2CPYWP2VP"))

    frame = t.exchange(commands(b"2FSELECT,32", b"2X,5,14,8,11,13,0,0,VP,A,\x82",
                                b"2FSELECT,1", b"2X,25,32,8,15,0,255,0,VP,A",
                                b"2F,BAD.BMF,1", b"2X,40,43,20,23,99,0,0,VP,A"))
    for plane in (frame.primary, frame.alternate):
        assert list(plane[8*640+5:8*640+15]) == [7,7,7,13,7,13,7,13,7,7]
        assert list(plane[8*640+25:8*640+31]) == [7,255,0,0,255,7]
        assert list(plane[20*640+40:20*640+44]) == [7]*4
    frame = t.exchange(commands(b"2FSELECT,32", b"2X,0,3,0,3,13,0,45,VP,\x7f\x11A",
                                b"2X,40,43,20,23,99,0,0,VP," + b"A"*101,
                                b"2X,40,43,20,100,99,0,0,VP,A"))
    assert any(frame.primary[y*640+x] == 17 for x in range(5) for y in range(5))
    assert list(frame.primary[20*640+40:20*640+44]) == [7]*4


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


def check_vga_font(t, root):
    t.case("VGA font selection in Lite, Normal and on the graphics pages")
    [marker] = root.rglob("syncterm-cache-name.txt")
    directory = marker.parent / "DDOFX" / "FONTS"
    font = bytearray(4096)
    # Deliberately asymmetric first and last rows of A. Comma has its
    # own glyph so a parser that splits the text field loses visible ink.
    font[65*16] = 0x81
    font[65*16+15] = 0x40
    font[44*16+7] = 0x10
    (directory / "Test.FNT").write_bytes(font)
    replacement = bytearray(font)
    replacement[66*16:67*16] = font[65*16:66*16]
    (directory / "Second.FNT").write_bytes(replacement)
    (directory / "Short.FNT").write_bytes(font[:-1])
    (directory / "Long.FNT").write_bytes(font+b"x")

    def glyph(frame, x, y, fg, bg):
        for plane in (frame.primary, frame.alternate):
            width = frame.metadata["plane_width"]
            for dy in range(16):
                for dx in range(8):
                    ink = (dy == 0 and dx in (0,7)) or (dy == 15 and dx == 1)
                    assert plane[(y+dy)*width+x+dx] == (fg if ink else bg), (x,y,dx,dy,fg,bg)

    # Change a glyph already present in VGA text memory, then write another.
    frame = t.exchange(b"\x1b[2J\x1b[H\x1b[37;40mA"+commands(b"dGETFONT", b"~DEFVAR,90,1,TEST.FNT",
                       b"fDEFVAR90", b"fShort.FNT", b"fLong.FNT", b"fNONE.FNT", b"dGETFONT")+b"A",
                       b"IBMSTD.FNT\xffTEST.FNT\xff")
    cell = frame.cells[0]
    glyph(frame,0,0,cell.foreground,cell.background)
    glyph(frame,8,0,cell.foreground,cell.background)

    frame = t.exchange(commands(b"LIGMODE", b"LIGMODE", b"dGETFONT", b"2C,54", b"2aFILL",
                                b"2CPYWP2VP", b"2CPYWP2BP", b"2XT,632,479,0,8,A",
                                b"2XTBP,0,15,201,6,A,A", b"2XTWP,0,14,5,3,A",
                                b"2XTWP,633,479,5,3,A", b"2aG,633,479", b"2aG,0,0"),
                       b"TEST.FNT\xff54\xff54\xff")
    glyph(frame,632,464,0,54)
    frame = t.exchange(commands(b"2CPYBP2VP"))
    glyph(frame,0,0,201,6)
    glyph(frame,16,0,201,6)
    assert frame.primary[7*640+11] == 201
    frame = t.exchange(commands(b"2XTHP,88,31,19,3,A", b"2CPYHP2VP", b"2XTWP,12,31,2,8,A",
                                b"2aG,12,16", b"2aG,13,16"), b"2\xff54\xff")
    glyph(frame,88,16,19,3)
    frame = t.exchange(commands(b"2RESET") + b"\x1b[H\x1b[37;40mA")
    glyph(frame,0,0,7,0)
    frame = t.exchange(commands(b"2XT,0,15,18,4,A", b"fSecond.FNT", b"fNONE.FNT") + b"\x1b[1;2HB")
    glyph(frame,0,0,18,4)
    glyph(frame,8,0,7,0)
    frame = t.exchange(commands(b"2GEXIT", b"dGETFONT")+b"B", b"Second.FNT\xff")
    glyph(frame,0,0,cell.foreground,cell.background)
    glyph(frame,16,0,cell.foreground,cell.background)
    frame = t.exchange(commands(b"fIBMSTD.FNT", b"dGETFONT"), b"IBMSTD.FNT\xff")
    assert frame.primary[0] != cell.foreground


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
    check_marquees(t, root)
    check_clocks(t, root)
    check_flics(t, root)
    check_text_scale(t, root)
    check_sprite_banks(t, root)
    check_sprites(t)
    check_grid(t)
    check_image_commands(t)
    check_pcx(t, root)
    t.exchange(b"\x1b[75;45H" + b"\x1b[6n", b"\x1b[75;45R")
    restored = t.check("Lite display and cursor restored", commands(b"2GEXIT", b"dVIDMODE") + b"!",
                       (8, 1), [(1, 1, "Before!")], reset=False, expected=b"0\xff")
    assert restored.metadata["plane_height"] == lite.metadata["plane_height"]
    assert restored.palette[3] == lite.palette[3]
    t.exchange(enter + commands(b"2aGRGB,3", b"2aG,639,479", b"2LWPPCX,WP", b"2aG,1,0"),
               b"63,21,0\xff0\xff193\xff")
    check_fades(t)
    check_bitmap_fonts(t, root)
    check_graphics_reset(t)
    t.exchange(commands(b"2GEXIT"))


def check_speech(t, root):
    t.case("MTALK assembles raw RealPlay bytes from a phoneme resource")
    marker, = root.rglob("syncterm-cache-name.txt")
    theme = marker.parent / "DDOFX" / "BBSTHEME"
    record = struct.Struct("<12sIIHH")
    bank = bytearray(record.pack(b"\xff", 0, 0, 0, 0) * 33)
    samples = [(b"HELLO.SND", b"\x00\x80\xff"), (b"PAUSE.SND", bytes(range(11))), (b"P.SND", b"p")]
    for slot, (name, payload) in enumerate(samples):
        record.pack_into(bank, slot * record.size, name, len(bank), len(payload), 0, 0)
        bank.extend(payload)
    (theme / "Talk.The").write_bytes(bank)
    output = theme / "SAY.SAM"
    t.exchange(commands(b"MTALK,talk.the,SAY.SAM,hello-p1-p2-p3-p4--absent-p,1"))
    phrase = b"\x00\x80\xff" + b"".join(bytes(range(11 // n)) for n in range(1, 5)) + b"p"
    assert output.read_bytes() == phrase
    t.exchange(commands(b"MTALK,TALK.THE,say.sam,HELLO-,0", b"MTALK,TALK.THE,../SAY.SAM,p1,1"))
    assert output.read_bytes() == phrase + b"\x00\x80\xff"
    assert not (theme.parent / "SAY.SAM").exists()
    t.exchange(commands(b"LIGMODE", b"LIGMODE", b"MTALK,TALK.THE,say.sam,p,1", b"2GEXIT"))
    assert output.read_bytes() == b"p"
    t.exchange(commands(b"MTALK,TALK.THE,say.sam,,3"))
    assert not output.exists()


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


def check_text_files(t, root):
    t.case("write, append and retrieve BBSTHEME text in Lite and Normal")
    marker, = root.rglob("syncterm-cache-name.txt")
    theme = marker.parent / "DDOFX" / "BBSTHEME"
    t.exchange(commands(b"~DEFVAR,81,1,Note.TXT", b"EWTEXT,DEFVAR81,0,Hello,^M~", b"EWTEXT,note.txt,1,",
                        b"ERTEXT,NOTE.TXT,^@END^M,"), b"Hello,^M~\r\r\0END\r,")
    note, = (p for p in theme.iterdir() if p.name.upper() == "NOTE.TXT")
    assert note.read_bytes() == b"Hello,^M~\r\r"

    payload = (b"0123456789" * 495) + b"\0\x82\xff\r\n"
    note.write_bytes(payload)
    t.exchange(commands(b"LIGMODE", b"LIGMODE", b"4DEFM,1,1,\x1d(ERTEXT,NOTE.TXT,done)",
                        b"4RUNM,1", b"dVIDMODE"), payload + b"done1\xff")
    t.exchange(commands(b"EWTEXT,note.txt,0,new", b"EWTEXT,note.txt,2,bad", b"eWTEXT,note.txt,0,bad",
                        b"EWTEXT,../bad.txt,0,escape", b"ERTEXT,../bad.txt,A", b"ERTEXT,MISSING.TXT,B",
                        b"ERTEXT,note.txt,", b"2GEXIT"), b"ABnew\r")
    assert note.read_bytes() == b"new\r"
    assert not (theme.parent / "bad.txt").exists()


def check_info(t, root):
    t.case("persistent INFO records in Lite and Normal with binary data and typed capture")
    marker, = root.rglob("syncterm-cache-name.txt")
    theme = marker.parent / "DDOFX" / "BBSTHEME"
    t.exchange(commands(b"2fINFO.", b"~INFLOD,0,1", b"~INFSAV,0,0,bad", b"~INFLOD,0,32001"),
               b"NO FILE\xff")
    t.exchange(commands(b"~INFSAV,3,-2147483648,A^@,~^M^?", b"~INFLOD,-2147483648,3",
                        b"~INFLOD,2147483647,3", b"~INFLOD,0,4"),
               b"A\0,~\r\xffWRONG PW\xffNO INFO\xff")
    path, = theme.glob("*.[Tt]")
    data = path.read_bytes()
    assert data == bytes(255*3) + struct.pack("<iB", -2147483648, 6) + b"A\0,~\r\xff" + bytes(244)
    t.exchange(commands(b"~DEFVAR,9,1,old", b"~VARCAP,9", b"~INFLOD,-2147483648,3", b"~VARCAP,0",
                        b"~VARLEN,9,10", b"7DEFRET,2,DEFVAR10", b"7SRET,2", b"~INFSAV,1,0,DEFVAR9",
                        b"~INFLOD,0,1"), b"6A\0,~\r\xff")
    t.exchange(commands(b"~INFSAV,3,2147483647,27", b"~DEFVAR,10,2,0", b"~VARCAP,10", b"~INFLOD,2147483647,3",
                        b"~VARCAP,0", b"~INCVAR,10", b"7DEFRET,2,DEFVAR10", b"7SRET,2"), b"28")
    t.exchange(commands(b"~INFSAV,32000,3,"+b"B"*201, b"~VARCAP,9", b"~INFLOD,3,32000", b"~VARCAP,0",
                        b"7DEFRET,2,DEFVAR9", b"7SRET,2"), b"B"*50)
    assert path.stat().st_size == 32001*255
    with path.open("r+b") as info:
        info.seek(255*2)
        info.write(struct.pack("<iB", 23, 250) + bytes(range(250)))
    t.exchange(commands(b"~INFLOD,23,2", b"~VARCAP,9", b"~INFLOD,23,2", b"~VARCAP,0",
                        b"~IFVAR,DEFVAR9,=,,\x1d(7SRET,2)", b"~INFSAV,15,0,DEFVAR9", b"~INFLOD,0,15"),
               bytes(range(250)) + bytes(range(50)))
    t.exchange(commands(b"LIGMODE", b"LIGMODE", b"~INFLOD,3,32000", b"2fOTHER.", b"~INFLOD,0,3",
                        b"2GEXIT"), b"B"*200 + b"NO FILE\xff")


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


def check_lite_reset(t):
    # RESET executes inside a snapshotted macro line. It erases the
    # definitions and capture, then execution continues with that line.
    t.check("reset within a macro", commands(b"~DEFVAR,43,1,OLD", b"7DEFRET,1,OLD", b"~VARCAP,4",
            b"4DEFM,2,1,A\x1d(2RESET)B", b"4DEFM,2,2,LOST", b"4RUNM,2", b"4RUNM,2", b"7SRET,1",
            b"7DEFRET,3,DEFVAR43", b"7SRET,3", b":a comment", b"dVIDMODE") + b"C",
            (4, 1), [(1, 1, "ABC")], expected=b"0\xff")
    t.exchange(commands(b"~PAUSEBAK", b"7CEVENT,2,1,0,1,1,2000,0,0,0,0,OLD,", b"7AEVENT,2",
                        b"2RESET", b"7AEVENT,2", b"dVIDMODE"), b"0\xff")
    t.send(commands(b"7CEVENT,2,1,0,1,1,2000,0,0,0,0,NEW,", b"7AEVENT,2"))
    t.input(b"NEW")


def check_key_macros(t):
    for normal in (False, True):
        t.case("Normal key macros" if normal else "Lite key macros")
        enter = commands(b"LIGMODE", b"LIGMODE") if normal else b""
        # The first callback waits and then sends M before its binary return.
        # A second binding sees the same c. The original key follows both.
        t.exchange(enter + commands(b"7DEFRET,1,M", b"4MKEY,1,46,99,1,A~^M,\x1d(w2)\x1d(7SRET,1)",
                                    b"4MKEY,300,46,99,1,Z,", b"4MKEY,2,28,10,0,E,"))
        t.client.text("cc")
        t.input(b"MA,\rZcMA,\rZc")
        t.client.key("enter", 2)
        t.input(b"E")
        # The lock consumes an unbound x and the passed c. Local menu and
        # disconnect commands are serviced upstream of the graphics adapter.
        t.exchange(commands(b"~LOCKKEYS"))
        t.client.text("xc")
        t.input(b"MA,\rZ")
        t.exchange(commands(b"~UNLOCKKEYS", b"4MKEY,1,46,99,0,OLD,\x1d(4MKEY,1,46,99,1,NEW,)"))
        t.client.text("cc")
        t.input(b"OLDZcNEWZc")
        t.exchange(commands(b"4MKEY,0", b"4MKEY,300,255,255,0,ANY,"))
        t.client.key("up")
        t.client.text("漢")
        t.input(b"ANYANY")
        # One physical Alt+Y may also produce a translated event. Either
        # backend representation must produce just one callback per press.
        t.exchange(commands(b"4MKEY,0", b"4MKEY,2,21,0,0,ALT,", b"4MKEY,3,94,0,0,FN,"))
        t.client.physical_key(21, True, 4)
        t.client.text("y", 4)
        t.client.physical_key(21, False, 4)
        t.client.key("f1", 2)
        t.input(b"ALTFN")
        t.exchange(commands(b"4MKEY,1,1,27,1,CLEAR,\x1d(4MKEY,0)"))
        t.client.key("escape")
        t.client.text("c")
        t.input(b"CLEARc")
        t.exchange(commands(b"~LOCKKEYS", b"2RESET"))
        t.client.text("c")
        t.input(b"c")
        if normal:
            t.exchange(commands(b"2GEXIT"))


def check_pointers(t, root, mouse):
    t.case("Normal mouse pointer masks and animation")
    [marker] = root.rglob("syncterm-cache-name.txt")
    record = struct.Struct("<12sIIHH")
    data = bytearray(record.pack(b"\xff", 0, 0, 0, 0)*33)
    # The hotspot puts the first mask byte one pixel left of the mouse.
    color = struct.pack("<hh", 1, 0) + bytes([0, 240]+[255]*254) + bytes([3, 7]+[0]*254)
    mono = struct.pack("<hh32H", 0, 1, 0x9fff, *([0xffff]*15), 0xa000, *([0]*15))
    for i, (name, payload) in enumerate([(b"A.MPT", color), (b"B.MPT", mono), (b"BAD.MPT", mono[:-1])]):
        record.pack_into(data, i*record.size, name, len(data), len(payload), 0, 1)
        data.extend(payload)
    (marker.parent/"DDOFX"/"BBSTHEME"/"Mouse.M").write_bytes(data)

    def pixel(frame, x, y):
        m = frame.metadata
        px = m["destination_x"] + (2*x+1)*m["destination_width"]//(2*m["plane_width"])
        py = m["destination_y"] + (2*y+1)*m["destination_height"]//(2*m["plane_height"])
        return frame.rgb[py*m["output_width"]+px]

    def check(frame, *points):
        for x, y, index in points:
            assert pixel(frame, x, y) == frame.palette[index], (x, y, index, pixel(frame, x, y))

    mouse(70, 55, 0)
    t.exchange(commands(b"2fMouse.", b"2C,165", b"2aFILL", b"2CPYWP2VP",
                        b"3MPL,1,A.MPT", b"3MPL,100,B.MPT"))
    frame = t.exchange(commands(b"3SMP,1", b"3MPT,BAD.MPT", b"3MPL,1,MISSING", b"3SMP,1"))
    check(frame, (69,55,3), (70,55,167), (71,55,165))
    assert list(frame.primary[55*640+69:55*640+72]) == [165]*3
    check(t.exchange(commands(b"3SMP,100")), (69,55,165), (70,54,170), (71,54,0), (72,54,15))
    check(t.exchange(commands(b"3MVIS,0")), (70,54,165))
    mouse(0, 0, 0)
    check(t.exchange(commands(b"3MVIS,1", b"3MPT,A.MPT")), (0,0,167), (70,54,165))
    mouse(70, 55, 0)
    t.exchange(commands(b"3MPL,99,A.MPT", b"3AMP,100,99,100,1", b"3AAM,100", b"w5"))
    check(t.client.wait_for(lambda f: pixel(f,72,54) == f.palette[15]), (70,54,170))
    t.exchange(commands(b"3AMP,1,99,100,3", b"3AMP,2,99,100,2", b"3AAM,1", b"3AAM,2"))
    t.client.wait_for(lambda f: pixel(f,69,55) == f.palette[3])
    t.client.wait_for(lambda f: pixel(f,72,54) == f.palette[15])
    check(t.exchange(commands(b"3DAM", b"3MPT,A.MPT", b"3MPL,0", b"3AMP,0", b"w4")), (69,55,3))
    check(t.exchange(commands(b"3MPT,DEFAULT")), (69,55,165))

    t.case("Normal hover fields and pointer selection")
    t.exchange(commands(b"7DEFRET,1,M", b"3MPL,1,A.MPT", b"3MPL,2,B.MPT",
                        b"3AMP,2,2,2,2", b"3MFLD,300,100,130,80,100,2,1,H~^M,\x1d(w1)\x1d(7SRET,1)",
                        b"3MFLD,1,90,130,80,100,1,0,L,",
                        b"3MREGN,2,100,130,80,100,1,CLICK,"))
    mouse(100,80,0)
    t.input(b"MH,\r")
    t.client.wait_for(lambda f: pixel(f,102,79) == f.palette[15])
    mouse(100,80,1)
    mouse(100,80,0)
    t.input(b"CLICK")
    # Per the SDK an uncovered gap retains the remembered field.
    mouse(60,60,0)
    mouse(129,99,0)
    t.exchange()
    mouse(90,80,0)
    t.input(b"L")
    check(t.exchange(), (89,80,3))
    t.send(commands(b"3MFFRS"))
    t.input(b"L")
    t.send(commands(b"3MFSWP,1,300"))
    t.input(b"L")
    check(t.exchange(commands(b"3MFLD,0", b"3MREG,0")), (89,80,3))
    mouse(70,55,0)
    t.exchange(commands(b"2RESET"))


def check_push_buttons(t, mouse):
    t.case("Normal push buttons from keyboard and mouse")
    make = b"5CPUSHB,300,up,down,80,60,46,99,1,3,P~^M,\x1d(7SRET,1)"
    frame = t.exchange(commands(b"2C,6", b"2aFILL", b"2S,0,4,0,2,UP", b"2C,7", b"2aFILL",
                                b"2S,0,4,0,2,DOWN", b"2aERASE", b"7DEFRET,1,M", make, b"2aG,80,58"),
                       b"6\xff")
    pos = 58*640+80
    assert frame.primary[pos] == 0
    frame = t.exchange(commands(b"2CPYWP2VP", b"4MKEY,300,46,99,1,K,"))
    assert frame.primary[pos] == 6
    t.client.text("c")
    t.client.wait_for(lambda f: f.primary[pos] == 7)
    t.input(b"MP,\rKc")
    assert t.exchange().primary[pos] == 6
    t.exchange(commands(b"3MREGN,300,80,84,58,60,3,R,"))
    mouse(84,60,1)
    t.exchange()
    mouse(84,60,5)
    t.input(b"MP,\r")
    mouse(0,0,0)
    t.input(b"R")
    t.exchange(commands(make.replace(b",1,3,P~^M",b",0,3,Q")))
    t.client.text("c")
    t.input(b"MQ")
    t.exchange(commands(make.replace(b",up,",b",absent,")))
    t.client.text("c")
    t.input(b"Kc")
    t.exchange(commands(make, b"2RESET"))
    t.client.text("c")
    t.input(b"c")


def check_choices(t, mouse):
    t.case("Desktop radio and checkbox selection, rendering and grouped execution")
    radio = b"5CRBUTTON,1,2,2,12,3,EDGE,OFF,20,24,0,1,9,8,30,97,0,R,"
    second = b"5CRBUTTON,2,2,2,12,3,EDGE,OFF,30,24,1,1,9,8,48,98,1,S,"
    checkbox = b"5CCHKBOX,1,ON,EDGE,OFF,40,24,0,1,0,0,46,99,1,C~^M,\x1d(7SRET,1)"
    pos = lambda x, y: y*640+x
    f = t.exchange(commands(b"2C,7", b"2aFILL", b"2S,0,4,0,4,OFF", b"2C,11", b"2aFILL",
                            b"2S,0,4,0,4,ON", b"2C,6", b"2aFILL", b"2C,2", b"2aRECT,1,3,1,3",
                            b"2S,0,4,0,4,EDGE", b"2aERASE", radio, second, checkbox,
                            b"7DEFRET,1,M", b"2aG,22,22", b"2aG,32,22"), b"3\xff12\xff")
    assert f.primary[pos(22,22)] == 0
    f = t.exchange(commands(b"2CPYWP2VP", b"5EXCBOX,1", b"5EXRBTN,1"), b"S")
    assert [f.primary[pos(x,22)] for x in (20,22,32,42)] == [6,3,12,2]
    t.client.text("a")
    t.client.wait_for(lambda f: f.primary[pos(22,22)] == 12)
    f = t.exchange(commands(b"5EXRBTN,2"), b"R")
    assert f.primary[pos(32,22)] == 3
    t.client.text("a")
    t.exchange(commands(b"5EXRBTN,1"), b"R")
    t.client.text("c")
    t.input(b"c")
    f = t.exchange(commands(b"5EXCBOX,1"), b"MC,\r")
    assert f.primary[pos(42,22)] == 11
    mouse(42,22,4)
    mouse(42,22,0)
    t.exchange(commands(b"5EXCBOX,1"), b"MC,\r")
    mouse(44,24,1)
    mouse(43,23,1)
    mouse(43,23,0)
    f = t.exchange(commands(b"5EXCBOX,1", b"5DERBTN,1,0", b"5EXRBTN,2"))
    assert [f.primary[pos(x,22)] for x in (22,42)] == [7,2]
    f = t.exchange(commands(b"5DERBTN,1,1", b"5EXRBTN,1"))
    assert f.primary[pos(22,22)] == 3
    # The first callback yields, executes the other family, then disables
    # the last slot before the outer scan reaches it.
    one = b"5CCHKBOX,1,ON,EDGE,OFF,40,24,1,1,0,4,0,0,0,A,\x1d(w1)\x1d(5EXRBTN,1)\x1d(5DECBOX,300,0)"
    last = b"5CCHKBOX,300,ON,EDGE,OFF,50,24,1,1,0,4,0,0,0,Z,"
    mouse(20,20,1)
    mouse(20,20,0)
    t.exchange(commands(one, last, b"5EXCBOX,300"), b"RA")
    t.exchange(commands(last, b"5SWPCBOX,1,299", b"5SWPCBOX,300,2", b"5EXCBOX,299"), b"ZRA")
    t.exchange(commands(b"5CRBUTTON,0", b"5EXRBTN,1", b"5EXCBOX,2"), b"ZA")
    t.exchange(commands(b"5CCHKBOX,0", b"5EXCBOX,299", b"2RESET"))


def check_text_fields(t, root, mouse):
    t.case("desktop text fields edit locally, mask input, follow links and submit in creation order")
    marker, = root.rglob("syncterm-cache-name.txt")
    fonts = marker.parent / "DDOFX" / "FONTS"
    glyphs = bytearray(4096)
    glyphs[ord("*")*16:(ord("*")+1)*16] = b"\x80"*16
    (fonts / "EDIT.FNT").write_bytes(glyphs)
    name = b"5CTXTFLD,250,EDIT.FNT,ab,(,^M,30,80,7,1,4,20,1,*,0,2,1,12,1,1,59,0,0,\x1d(7SRET,1)"
    number = b"5CTXTFLD,2,IBMSTD.FNT,1,[,],635,90,4,3,5,11,0,,250,0,0,12,2,0,60,0,1,"
    pos = lambda x,y: y*640+x
    f = t.exchange(commands(b"2aERASE", b"2CPYWP2VP", b"7DEFRET,1,M", name, number,
                            b"2aG,30,67", b"2aG,31,67", b"2aG,639,73"), b"20\xff1\xff3\xff")
    assert f.primary[pos(30,67)] == 0
    t.exchange(commands(b"2CPYWP2VP", b"~DEFVAR,777,1,", b"~DEFVAR,778,2,0"))
    t.client.key("f1")
    t.client.wait_for(lambda f: f.primary[pos(31,67)] == 4)
    t.client.text("c7!")
    t.client.key("home")
    t.client.text("d")
    t.client.key("end")
    t.client.key("backspace")
    t.client.key("tab")
    t.client.wait_for(lambda f: f.primary[pos(635,73)] == 5)
    t.client.text("x23")
    t.client.key("enter")
    t.input(b"M(DAB\r[123]")
    f = t.exchange(commands(b"~GTXTFLD+,777,250", b"~GTXTFLD-,778,2", b"~INCVAR,778",
                            b"7DEFRET,2,DEFVAR777", b"7SRET,2", b"7DEFRET,2,DEFVAR778", b"7SRET,2"),
                   b"(DAB\r124")
    for plane in (f.primary, f.alternate):
        assert plane[pos(30,67)] == 20 and plane[pos(31,67)] == 1
    t.exchange(commands(b"5SWPTFLD,250,2", b"5EXTFLD,2"), b"[123]M(DAB\r")
    t.exchange(commands(b"5SWPTFLD,2,250"))
    mouse(31,67,1)
    mouse(31,67,0)
    t.client.wait_for(lambda f: f.primary[pos(31,67)] == 4)
    t.client.key("escape")
    t.client.wait_for(lambda f: f.primary[pos(31,67)] == 1)
    t.exchange(commands(b"5EXTFLD,250", b"5ACTTFLD,250"), b"M(\r[123]")
    t.client.text("q")
    t.client.key("enter")
    t.client.wait_for(lambda f: f.primary[pos(635,73)] == 5)
    t.client.key("tab", 1)
    t.client.wait_for(lambda f: f.primary[pos(31,67)] == 4)
    # SHOW redraws the unselected field on WP while editing continues on VP.
    f = t.exchange(commands(b"2aERASE", b"5SHOWTFLD,250", b"2aG,31,67"), b"1\xff")
    assert f.primary[pos(31,67)] == 4
    t.exchange(commands(b"5CTXTFLD,0", b"7DEFRET,1,S"))
    full = b"5CTXTFLD,1,EDIT.FNT," + b"z"*70 + b"," + b"B"*25 + b"," + b"A"*23 + b"^@,"
    full += b"0,479,70,1,4,9,0,,0,0,0,0,0,0,0,0,0,\x1d(w1)\x1d(7SRET,1)"
    t.exchange(commands(full, b"5ACTTFLD,1"))
    t.client.key("enter")
    t.client.text("x")
    t.input(b"S" + b"B"*25 + b"z"*70 + b"A"*23 + b"\x00x")
    t.exchange(commands(b"5CTXTFLD,0", b"5EXTFLD,2", b"~GTXTFLD-,778,2", b"7DEFRET,2,DEFVAR778",
                        b"7SRET,2", b"2RESET"), b"124")
    t.client.text("x")
    t.input(b"x")


def check_list_boxes(t, root, mouse):
    t.case("desktop list viewports, selection colours, mouse double-clicks and ordered callbacks")
    marker, = root.rglob("syncterm-cache-name.txt")
    font = bytearray(4096)
    for ch in b"ABCDE":
        font[ch*16:(ch+1)*16] = bytes([0x80 >> (ch-65)]) * 16
    (marker.parent / "DDOFX" / "FONTS" / "List.fnt").write_bytes(font)
    create = b"5CLBOX,300,600,479,5,2,10,1,11,2,12,3,5,1"
    items = [f"5LBMODLST,300,{i},1,0,{c},{c}~^M,".encode() for i,c in enumerate("ABCDE",1)]
    top, bottom = 448*640+600, 464*640+600
    f = t.exchange(commands(b"2C,44", b"2aFILL", b"2CPYWP2VP", b"fList.fnt", create, *items,
                            b"5LBMODLST,300,2,0,2,,,", b"5LBDRWLST,300", b"2aG,600,448",
                            b"2aG,601,448", b"2aG,601,464"), b"11\xff2\xff10\xff")
    assert f.primary[top] == f.alternate[top] == 44
    f = t.exchange(commands(b"5LBSHWLST,300", b"5LBTGLSEL,300"))
    assert list(f.primary[top:top+3]) == [12,2,2]
    f = t.exchange(commands(b"5LBADJHLT,300,0,1", b"5LBTGLSEL,300", b"5LBEXCLST,300"), b"A,\r")
    assert list(f.primary[bottom:bottom+3]) == [2,11,2]
    f = t.exchange(commands(b"5LBADJHLT,300,0,1", b"5LBTGLSEL,300", b"5LBADJLST,300,1,1",
                            b"5LBADJHLT,300,1,-1", b"5LBEXCLST,300"), b"A,\rC,\r")
    assert list(f.primary[top:top+5]) == [2,2,2,11,2]
    f = t.exchange(commands(b"5LBADJLST,300,2,-1", b"5LBDEACT,300", b"5LBADJHLT,300,0,1",
                            b"5LBEXCLST,300", b"5LBREACT,300", b"5LBEXCLST,300"), b"A,\rC,\r")
    assert f.primary[top] == 12
    # The key macro changes the visible list, then two mouse presses select D.
    t.exchange(commands(b"4MKEY,1,46,99,0,K,\x1d(5LBADJLST,300,2,1)"))
    t.client.text("c")
    t.input(b"K")
    mouse(605,450,1)
    mouse(605,450,0)
    mouse(605,450,1)
    mouse(605,450,0)
    t.exchange(commands(b"7DEFRET,1,M", b"5LBMODLST,300,3,2,2,,,\x1d(w1)\x1d(7SRET,1)",
                        b"5LBEXCLST,300"), b"A,\rMC,\rD,\r")
    # A key macro can operate the box without mouse input. Only one of this box's items can be picked.
    single = b"5CLBOX,1,0,31,3,2,15,0,15,1,14,2,2,0"
    t.exchange(commands(single, b"5LBMODLST,1,1,1,0,A,first,", b"5LBMODLST,1,2,1,0,B,second,",
                        b"5LBSHWLST,1", b"5LBTGLSEL,1", b"5LBADJHLT,1,0,1", b"5LBTGLSEL,1",
                        b"5LBEXCLST,1", b"5LBCLSLST,1", b"5LBREACT,1", b"5LBEXCLST,1",
                        b"5LBMODLST,300,0,2,0,,,", b"5LBEXCLST,300", b"5CLBOX,0", b"5LBEXCLST,300",
                        b"fIBMSTD.FNT", b"2RESET"), b"second")


def check_mouse(t, root):
    t.case("Normal mouse region callbacks and button history")
    frame = t.exchange(commands(b"LIGMODE", b"LIGMODE", b"3MACT", b"dMOUSE"), b"Y\xff3\xff")
    m = frame.metadata
    assert not t.exchange(commands(b"3MVIS,0", b"3MVIS,5")).metadata["pointer_visible"]
    assert t.exchange(commands(b"3MVIS,1")).metadata["pointer_visible"]

    def mouse(x, y, buttons):
        px = m["destination_x"] + (2*x+1)*m["destination_width"]//(2*m["plane_width"])
        py = m["destination_y"] + (2*y+1)*m["destination_height"]//(2*m["plane_height"])
        before = t.client.capture().metadata["events_dequeued"]
        t.client.mouse(px, py, action=1, buttons=buttons)
        t.client.wait_for(lambda s: s.metadata["events_dequeued"] > before)

    t.case("Normal mouse motion, limits and speed")
    # Move the default arrow before the first physical mouse sample.
    frame = t.exchange(commands(b"2C,4", b"2aFILL", b"2CPYWP2VP", b"3MMOV,120,90", b"3MINF"),
                       b"120,90,0\xff")
    px = m["destination_x"] + 241*m["destination_width"]//(2*m["plane_width"])
    py = m["destination_y"] + 181*m["destination_height"]//(2*m["plane_height"])
    assert frame.rgb[py*m["output_width"]+px] == frame.palette[0]
    assert frame.primary[90*640+120] == 4
    mouse(30, 30, 1)
    t.exchange(commands(b"3MINF", b"3MIOLD,1"), b"120,90,1\xff1,120,90\xff")
    mouse(32, 33, 0)
    t.exchange(commands(b"3MIOLD,-1", b"3MLIM,50,110,60,80", b"3MINF",
                        b"3MMOV,75,72", b"3MLIM,9,8,4,6", b"3MMOV,0,479", b"3MINF",
                        b"3MMOV,40,600", b"3MINF"),
               b"1,122,93\xff110,80,0\xff50,80,0\xff50,80,0\xff")
    t.exchange(commands(b"3MMOV,70,70", b"MSPD,16,8"))
    mouse(33, 34, 0)
    t.exchange(commands(b"3MINF"), b"70,72,0\xff")
    mouse(34, 35, 0)
    t.exchange(commands(b"3MINF", b"3MSPD,4,32", b"3MSPD,0,1"), b"71,74,0\xff")
    mouse(35, 36, 0)
    mouse(36, 37, 0)
    t.exchange(commands(b"3MINF", b"3MFLD,1,82,84,69,71,0,0,H,", b"3MREG,1,82,84,69,71,1,P,\x1d(3MREG,0)"),
               b"75,75,0\xff")
    mouse(36, 37, 1)
    t.send(commands(b"3MMOV,83,70"))
    # The host moved onto the region while left was held. Its return
    # precedes the hover callback for the same position.
    t.input(b"PH")
    mouse(36, 37, 0)
    t.exchange(commands(b"3MIOLD,-1", b"2RESET"), b"1,83,70\xff")

    # Setting limits and speed alone takes the first absolute mouse sample.
    t.exchange(commands(b"3MLIM,50,110,60,80", b"MSPD,16,16"))
    mouse(90, 70, 0)
    t.exchange(commands(b"3MINF"), b"90,70,0\xff")
    mouse(300, 72, 0)
    mouse(298, 72, 0)
    t.exchange(commands(b"3MINF", b"3MPT,DEFAULT"), b"109,72,0\xff")
    mouse(301, 90, 0)
    t.exchange(commands(b"3MINF", b"2RESET"), b"112,90,0\xff")
    check_push_buttons(t, mouse)
    check_choices(t, mouse)
    check_text_fields(t, root, mouse)
    check_list_boxes(t, root, mouse)
    check_pointers(t, root, mouse)
    t.exchange(commands(b"3MREGN,300,70,85,40,55,3,B,", b"7DEFRET,1,M",
                        b"3MREGN,1,70,85,40,55,3,A~^M,\x1d(w1)\x1d(7SRET,1)",
                        b"3MREG,1,90,85,40,55,3,BAD,"))
    mouse(70, 55, 5)  # Left + right, exactly on a corner
    mouse(20, 30, 4)
    mouse(20, 30, 0)
    t.input(b"MA,\rB")
    t.exchange(commands(b"3MINF", b"3MIOLD,2", b"3MIOLD,-1", b"3MIOLD,2"),
               b"20,30,0\xff1,70,55\xff1,20,30\xff0,70,55\xff")

    # A held middle button can enter a repeatable region. Its macro clears
    # the table on the third invocation, without another local input event.
    t.exchange(commands(b"3MREGN,0", b"~DEFVAR,1,2,0",
                        b"3MREG,1,70,85,40,55,4,R,\x1d(~INCVAR,1)\x1d(~IFVAR,DEFVAR1,=,3,\x1d[3MREGN,0])"))
    mouse(20, 30, 2)
    t.exchange(commands(b"3MINF"), b"20,30,4\xff")
    mouse(85, 40, 2)
    t.input(b"RRR")
    mouse(85, 40, 0)
    end = t.exchange(commands(b"3MIOLD,-3", b"2RESET", b"3MVIS,0", b"2GEXIT", b"dVIDMODE"),
                     b"1,85,40\xff0\xff")
    assert end.metadata["pointer_visible"] == m["pointer_visible"]


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
    def configure_downloads(root):
        (root / "downloads").mkdir(exist_ok=True)
        with (root / "syncterm.lst").open("a") as profile:
            profile.write(f"DownloadPath={root / 'downloads'}\n")

    with connection(args.executable, args.reports, encoding=args.encoding, prepare=configure_downloads,
                    options="Graphics=DDO F/X 2.0\n", settings="AudioModes=0\n") as fixture:
        t = Terminal(*fixture)
        for split in range(len(ident) + 1):
            t.send(ident[:split])
            t.exchange(ident[split:], reply)
        t.exchange(b"\x1d{dliterm}\x1d{DLITERM}", reply)
        t.exchange(b"\x1d{dMUSICON}\x1d{mMISSING.MOD}\x1d{dGETMUSIC}", b"NO\xffNONE\xff")
        t.exchange(b"\x1d{MGVOL}\x1d{MSVOL,20}\x1d{MMVOL,-5}\x1d{MGVOL}", b"64\xff15\xff")
        t.exchange(commands(b"dSOUNDON", b"dACARD", b"tABSENT.THE", b"dGETTHEME", b"6CHECKBS,150", b"6CHECKBS,151",
                            b"~VARCAP,10", b"6CHECKBS,1", b"~VARCAP,0", b"7DEFRET,1,DEFVAR10", b"7SRET,1"),
                   b"NO\xffNONE\xffNONE\xffN/A\xffN/A")
        check_package_downloads(t)
        check_background_download(t)
        check_cache(t, fixture[1])
        check_resources(t, fixture[1])
        check_speech(t, fixture[1])
        check_info(t, fixture[1])
        check_text_files(t, fixture[1])
        check_scripts(t)
        check_events(t)
        check_lite_reset(t)
        check_key_macros(t)
        check_mouse(t, fixture[1])
        check_vga_font(t, fixture[1])
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
