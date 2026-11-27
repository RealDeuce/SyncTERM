"""DDO F/X over a live connection with ordinary ANSI before and after commands."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from binascii import crc_hqx
import os
import re
import shutil
import struct
import time
import zipfile
from pathlib import Path

from st_ddofx_package_test import header, subpacket, package
from st_migrated_control_test import connection
from syncterm_fixture import click_text
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
    # Blood Quest sends its probe in the packet containing the final OO.
    # It retries at two seconds and consumes one reply for each subsequent query.
    t.send(b"OO" + commands(b"dLITERM"))
    t.input(b"LITERM Ver. 2.00\xff", timeout=2)
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
                                b"6DEFJOY,1,0,B1,ordinary,\x1d(2C,5)\x1d(2aFILL)\x1d(2CPYWP2VP)",
                                b"2GWIN,1,10,139,20,129,150,150,0,0,1,0,0,0,0,0,0,0,0,Invisible",
                                b"2DCGWIN,1,44,122,0,ordinary,\x1d(2C,5)", b"7ZMSTEALTH,1",
                                b"2GWIN,2,10,139,20,129,150,150,0,0,1,0,0,0,0,0,0,0,0,Invisible",
                                b"2DCGWIN,2,44,122,1,stealth,\x1d(2C,9)\x1d(2aFILL)\x1d(2CPYWP2VP)",
                                b"6DEFJOY,1,0,B2,stealth,\x1d(2C,4)\x1d(2aFILL)\x1d(2CPYWP2VP)",
                                b"6DEFJOY,1,1,AC,,",
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
    t.client.request(7, bytes([5, 0]))
    t.client.request(7, bytes([2, 0, 0, 20, 0, 32]) + bytes(16))
    t.client.wait_for(lambda f: f.primary and f.primary[0] == 4)
    t.client.request(7, bytes([3, 0]))
    t.client.text("z")
    t.client.wait_for(lambda f: f.primary and f.primary[0] == 9)
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


def check_capacity(t):
    t.case("drive, memory and speed queries on the connection and in script variables")
    t.exchange(commands(b"dDRIVE,", b"dMEM,", b"dVMEM,0", b"dMEM"), re.compile(rb"[0-9]+\xff"))
    t.exchange(commands(b"dSPEED,", b"~VARCAP,2", b"dSPEED", b"~VARCAP,0",
                        b"7DEFRET,2,DEFVAR2", b"dSPEED", b"7SRET,2"), re.compile(rb"(-1|[1-9][0-9]*)\xff\1"))
    # A DDO numeric variable can hold at most a signed 32-bit count.
    marker, = t.root.rglob("syncterm-cache-name.txt")
    available = shutil.disk_usage(marker.parent).free
    t.exchange(commands(b"~VARCAP,1", b"dDRIVE", b"~VARCAP,0", b"7DEFRET,1,DEFVAR1", b"7SRET,1",
                        b"7DEFRET,2,OK", b"~IFVAR,MEM,>,0,\x1d(7SRET,2)"),
               (str(2**31 - 1).encode() + b"OK" if available > 2**31 + 8*1024**2 else re.compile(rb"[0-9]+OK")))
    t.exchange(commands(b"LIGMODE", b"LIGMODE", b"dVIDMODE", b"dVMEM,1", b"dDRIVE", b"dMEM", b"dSPEED",
                        b"2GEXIT", b"dVIDMODE"), re.compile(rb"1\xff[0-9]+\xff[0-9]+\xff-?[0-9]+\xff0\xff"))


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


FULL_TEXT_WINDOW = b"1OPEN,1,1,80,1,30,7,15,N,N,N,N,N,,,"


def check_text_windows(t):
    t.case("overlapping text windows retain their pages, cursor, order and hidden output")
    t.exchange(commands(b"2C,33", b"2aFILL", b"2CPYWP2VP"))
    def colours(data, expected, reply=b""):
        frame = t.exchange(data, reply)
        for (x, y), index in expected.items():
            for plane in (frame.primary, frame.alternate):
                assert plane[y * 640 + x] == index, (data, x, y, index, plane[y * 640 + x])
    first = b"1OPEN,1,2,8,2,6,7,15,N,N,N,N,N,,,"
    second = b"1OPEN,4,3,7,2,5,7,15,N,N,N,N,N,,,"
    colours(commands(first) + b"\x1b[41m    " + commands(second) + b"\x1b[42m   ",
            {(8,16):4,(16,16):2,(40,16):0,(0,16):33})
    colours(commands(b"1WIN,1", b"1WIN,8", b"1WIN,11", b"1SWAP,1,8", b"1GETPOS"),
            {(8,16):4,(16,16):4}, b"5,1,1,1\xff")
    colours(commands(b"1SWAP,1,4", b"1HIDE", b"1HIDE") + b"\x1b[44m ",
            {(8,16):33,(16,16):2,(40,16):0})
    colours(commands(b"1UNHIDE", b"1GETPOS"), {(8,16):4,(16,16):4,(40,16):1}, b"6,1,1,1\xff")
    colours(commands(b"1WIN,1", b"1GETPOS"), {(16,16):2}, b"4,1,1,1\xff")
    moved = b"1CHANGE,11,17,7,10,7,15,N,N,N,N,N,,,"
    colours(commands(moved, b"1GETPOS"), {(16,16):4,(80,96):2}, b"4,1,1,1\xff")
    colours(commands(b"1CLOSE", b"1GETPOS"), {(16,16):4,(80,96):33}, b"6,1,1,1\xff")
    colours(commands(b"1CLOSE", b"1UNHIDE", b"1GETPOS"), {(8,16):33,(16,16):33}, b"NONE\xff")

    # A 4x3 aperture at screen (8,16) can see any part of its 80x100 page.
    viewport = b"1OPEN,1,2,5,2,4,7,15,N,N,N,N,Y,,,"
    colours(commands(viewport) + b"\x1b[5;7H\x1b[41m \x1b[6;8H\x1b[42m " +
            commands(b"1POS,7,5", b"1GETPOS"), {(8,16):4,(16,32):2,(0,16):33}, b"9,6,7,5\xff")
    for key, pos in [("end", b"8,5"), ("page_down", b"8,6"), ("home", b"7,6"), ("page_up", b"7,5")]:
        t.client.key(key, 2)
        t.exchange(commands(b"1GETPOS"), b"9,6," + pos + b"\xff")
    colours(commands(b"1POSR,-1,2147483647", b"1POS,81,0", b"1GETPOS"),
            {(8,16):0,(16,16):4,(24,32):2}, b"9,6,6,5\xff")
    colours(commands(b"1POS,77,98", b"1POSR,1,1", b"1GETPOS"), {(8,16):0}, b"9,6,77,98\xff")
    colours(commands(b"1GOTOC", b"1GETPOS"), {(8,32):0,(8,48):0}, b"9,6,9,4\xff")
    colours(commands(b"1POS,7,5") + b"\x1b[100;1H\n" + commands(b"1GETPOS"),
            {(8,16):0,(16,16):2,(0,16):33}, b"1,100,7,5\xff")
    colours(commands(b"1CHANGE,2,5,2,4,7,15,N,N,Y,Y,Y,,,") + b"\x1b[12;22H" +
            commands(b"1GETPOS"), {(8,16):0}, b"22,12,19,10\xff")
    # Both cursor movements occur in one receive burst.
    colours(b"\x1b[60;50H\x1b[12;22H" + commands(b"1GETPOS"), {(8,16):0}, b"22,12,22,12\xff")
    colours(b"\x1b[1;1H" + commands(b"1GETPOS", b"1CLOSE"), {(8,16):33}, b"1,1,1,1\xff")

    # Drag over the displayed columns of a scrolled page, then verify that
    # restoring the selection snapshot preserves both graphics planes.
    before = t.exchange(commands(viewport) + b"\x1b[9;17HVIEW" + commands(b"1POS,17,9"))
    m = before.metadata
    def display_point(x, y):
        return (m["destination_x"] + x*m["destination_width"]//640,
                m["destination_y"] + y*m["destination_height"]//480)
    start, end = display_point(9, 17), display_point(25, 17)
    t.client.command_key(24)  # Alt+O enables local mouse gestures.
    t.client.clipboard("unchanged")
    t.client.mouse(*start, action=1, buttons=1)
    t.client.mouse(*end, buttons=1)
    t.client.wait_for(lambda f: f.primary != before.primary)
    t.client.mouse(*end, action=1)
    after = t.client.wait_for(lambda f: f.clipboard == "VIE")
    assert before.primary == after.primary and before.alternate == after.alternate
    # The fourth displayed cell and one beyond the right edge both own
    # a link on the virtual page. Only the displayed cell is a hit.
    t.exchange(b"\x1b[9;20H\x1b]8;;https://example.invalid/test\x1b\\XY\x1b]8;;\x1b\\")
    t.client.mouse(*display_point(33, 17))
    t.client.wait_for(lambda f: f.metadata["pointer_shape"] == 2)
    t.client.mouse(*display_point(41, 17))
    t.client.wait_for(lambda f: f.metadata["pointer_shape"] != 2)
    t.client.command_key(24)
    t.exchange(commands(b"1CLOSE"))

    # Column limits apply to existing and subsequently opened windows.
    # The colour and cursor survive narrowing, and ordinary ANSI wraps.
    colours(commands(viewport) + b"\x1b[45mab" + commands(b"1SETC,5") + b"cdef" + commands(b"1GETPOS"),
            {(8,16):5,(8,32):5}, b"2,2,1,1\xff")
    colours(commands(b"1OPEN,2,10,13,2,4,7,15,N,N,N,N,N,,,") + b"123456" +
            commands(b"1GETPOS", b"1WIN,1", b"1GETPOS", b"1SETC,1", b"1GETPOS"),
            {(16,16):33}, b"2,2,1,1\xff2,2,1,1\xff1,2,1,1\xff")
    colours(commands(b"1SETC,80", b"1CLOSE", b"1CLOSE"), {(8,16):33})
    # The tabs of a double-line window use the selected FNT and center the
    # clipped title and help. Give each tested glyph a distinct literal row.
    marker, = t.root.rglob("syncterm-cache-name.txt")
    strike = bytearray(4096)
    for ch, ink in [(201, 0x80), (205, 0x40), (186, 0x20), (ord("X"), 0x10), (ord("H"), 0x08)]:
        strike[ch*16] = strike[ch*16+8] = ink
    (marker.parent / "DDOFX" / "FONTS" / "Border.fnt").write_bytes(strike)
    border = b"1OPEN,7,10,16,3,7,9,14,Y,N,N,N,N,X,H,Border.fnt"
    colours(commands(border, b"dGETFONT"), {(72,32):9,(73,32):0,(81,32):9,(74,48):9,
            (96,32):4,(99,32):14,(96,96):4,(100,96):14,(128,32):33}, b"Border.fnt\xff")
    colours(commands(first), {(96,32):1,(99,32):14})
    colours(commands(b"1WIN,7", b"1HIDE"), {(72,32):33,(99,32):33,(100,96):33})
    colours(commands(b"1UNHIDE", b"1SCALE,4,8"), {(36,16):9,(48,16):4,(51,16):14,
            (72,32):33,(99,32):33})
    colours(commands(b"1SCALE,8,16", b"1CLOSE", b"1CLOSE", b"fIBMSTD.FNT"), {(72,32):33,(8,16):33})

    colours(commands(b"1SETC,1", b"1OPEN,1,4,4,2,5,7,15,N,N,N,Y,N,,,") + b"abc" + commands(b"1GETPOS"),
            {(24,16):0}, b"1,3,1,1\xff")
    t.exchange(commands(b"2RESET", FULL_TEXT_WINDOW) + b"123456" + commands(b"1GETPOS", b"1CLOSE"),
               b"7,1,1,1\xff")


def check_desktop_text(t):
    t.case("resource line breaks after the last text window closes preserve the graphics desktop")
    t.exchange(commands(b"2C,33", b"2aFILL", b"2C,91", b"2aP,5,470", b"2CPYWP2VP"))
    scene = t.exchange()
    assert scene.primary[470*640+5] == 91
    window = b"1OPEN,2,2,5,3,5,7,15,N,N,N,N,N,,,"
    frame = t.exchange(commands(window) + b"\x1b[41m    ")
    for plane in (frame.primary, frame.alternate):
        assert list(plane[32*640+7:32*640+41]) == [33] + [4]*32 + [33]
    # Scroll a formerly invisible logical row into the bottom of the window.
    frame = t.exchange(b"\x1b[4;1H\x1b[42m    \x1b[100;1H\n")
    for plane in (frame.primary, frame.alternate):
        assert list(plane[64*640+7:64*640+41]) == [33] + [2]*32 + [33]
    # Closing a second, hidden window selects the visible window again.
    visible = frame
    hidden = b"1OPEN,8,1,80,1,30,7,15,N,Y,N,N,N,,,"
    frame = t.exchange(commands(hidden) + b"\x1b[2JHIDDEN" + commands(b"1CLOSE"))
    assert frame.primary == visible.primary and frame.alternate == visible.alternate
    for data, reply in ((commands(b"1CLOSE", b"1CLOSE"), b""),
                        (commands(b"2fPack.", b"*LINES.FX"), b"1\xff"),
                        (b"\x1b[2J" + b"wire text\r\n"*102, b"")):
        frame = t.exchange(data, reply)
        assert frame.primary == scene.primary and frame.alternate == scene.alternate
    t.client.command_key(48)
    t.client.wait_text("Scrollback")
    t.client.key("escape")
    frame = t.exchange()
    assert frame.primary == scene.primary and frame.alternate == scene.alternate


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

    t.case("a window movie pauses with focus and plays underneath an animation")
    t.exchange(commands(b"2C,61", b"2aFILL", b"2S,0,1,0,1,MARK",
                        b"2GWIN,1,20,119,30,129,1000,750,0,0,0,0,0,0,0,0,0,0,1,Movie",
                        b"2CHVGP,1,900,650", b"2ALERTGWIN,1", b"2C,23", b"2aFILL", b"2CPYVP2HP",
                        b"2RGWINI", b"5OFLIC,9,910,660,2,1,8,0,DELTA.FLC",
                        b"2ANIM,3,911,662,0,MARK,MARK,0,0,0,0,30,0", b"2PLAYA,3",
                        b"2CALMGWIN", b"5UFLIC,9", b"w20"))
    # Slot 1 was created inactive. Neither the sprite nor the film has run.
    start = 40*640+30
    assert t.exchange().primary[start] == 23
    t.exchange(commands(b"2AGWIN,1"))
    f = t.client.wait_for(lambda f: f.primary[start+640] == 15)
    assert list(f.primary[start+640:start+645]) == [15,61,61,8,9]
    f = t.exchange(commands(b"2STOPA,3"))
    assert list(f.primary[start+640:start+645]) == list(f.alternate[start+640:start+645]) == [15,15,7,8,9]
    # The desktop movie straddles the left border of the still-open window.
    f = t.exchange(commands(b"5OFLIC,10,18,40,1,1,50,0,DELTA.FLC", b"5UFLIC,10", b"w4"))
    assert list(f.primary[40*640+18:40*640+23]) == [0,1,23,23,23]
    f = t.exchange(commands(b"2CGWIN,1"))
    assert list(f.primary[40*640+18:40*640+23]) == [0,1,2,3,4]


def check_text_scale(t, root):
    t.case("graphic text uses the native FNT pixels and shared bottom-anchored cell dimensions")
    marker, = root.rglob("syncterm-cache-name.txt")
    font = bytearray(4096)
    for c in b"A0123456789:":
        font[c*16+8] = 0x80
        font[c*16+15] = 0x20
    (marker.parent / "DDOFX" / "FONTS" / "Small.fnt").write_bytes(font)
    frame = t.exchange(commands(FULL_TEXT_WINDOW, b"2C,39", b"2aFILL", b"2CPYWP2VP", b"1SCALE,4,8", b"fSmall.fnt",
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


def check_landscape(t):
    t.case("landscape camera, seeded maps, page exchange and background flight")
    queries = (b"5LGX", b"5LGY", b"5LGZ", b"5LGD")
    t.exchange(commands(b"2RESET", *queries), b"0\xff0\xff100\xff0\xff")
    t.exchange(commands(b"5LCX,254", b"5LCY,1", b"5LCZ,20", b"5LCDIR,0", b"5LMX,3",
                        b"5LMY,-3", b"5LMZ,-22", b"5LMDIR,-90", *queries), b"1\xff254\xff0\xff270\xff")
    t.exchange(commands(b"5LCZ,98", b"5LMZ,3", b"5LCDIR,360", b"5LCX,256",
                        b"~DEFVAR,2,2,0", b"~VARCAP,2", b"5LGZ", b"~VARCAP,0",
                        b"5LCY,DEFVAR2", *queries), b"1\xff100\xff100\xff0\xff")

    def seeded(seed):
        frame = t.exchange(commands(b"5CLAND," + seed, b"5LDMAP,385,479"))
        return frame.primary[225*640+385:226*640]

    first = seeded(b"912345")
    assert len(set(first)) > 8 and first == seeded(b"912345") and first != seeded(b"912346")
    assert seeded(b"0") != seeded(b"0")

    # Import with the map origin at the lower left, then export at the far edge.
    t.exchange(commands(b"2RESET", b"2C,41", b"2aFILL", b"2C,152", b"2aP,0,254",
                        b"2C,136", b"2aP,254,0", b"5LGMAPWP,0,254", b"2C,57", b"2aFILL",
                        b"2CPYWP2VP", b"5LDMAP,385,479", b"5LGMAP,385,479", b"5LDMAPWP,0,254",
                        b"2aG,0,254", b"2aG,254,0", b"2aG,100,100", b"2aG,255,100"),
               b"152\xff136\xff41\xff57\xff")
    # A flat map has one terrain index. Its reflected clouds cover only the
    # upper part of a second viewport, preserving the background between them.
    t.exchange(commands(b"2C,41", b"2aFILL", b"5LGMAPWP,0,254", b"2C,57", b"2aFILL",
                        b"2CPYWP2VP", b"5LLPOS,0,479", b"5LCPOS,320,199", b"5LSKY,75",
                        b"5LCLOUD,1", b"5LCD,1", b"5LMD,2", b"~PAUSEBAK", b"5LANIM,1"))
    assert t.exchange().primary[-640] == 57
    t.send(commands(b"~UNPAUSEBAK"))
    frame = t.client.wait_for(lambda f: f.primary[-640] == 41)
    for plane in (frame.primary, frame.alternate):
        assert plane[280*640] == 75 and plane[279*640] == 57
        assert plane[320] != 57 and plane[199*640+320] == 57
        assert plane[-640+320] == 57
    t.exchange(commands(b"5LCY,0", b"5LCSPD,5", b"5LMSPD,-1", b"w9", b"5LANIM,0",
                        b"5LGY"), re.compile(rb"(?:[1-9][0-9]*)\xff"))
    held = t.exchange().primary
    assert t.exchange(commands(b"5LSKY,3", b"w3")).primary == held
    # A ridge three units north must occlude the sky ahead. Turning east
    # from the same position looks along a flat row and exposes the sky.
    t.send(commands(b"2C,41", b"2aFILL", b"2C,136", b"2aRECT,0,254,218,219",
                    b"5LGMAPWP,0,254", b"5LCLOUD,0", b"5LCX,32", b"5LCY,32",
                    b"5LCZ,50", b"5LCSPD,0", b"5LCDIR,0", b"5LANIM,1"))
    center = 305 * 640 + 160
    t.client.wait_for(lambda f: f.primary[center] == 136)
    t.send(commands(b"5LCDIR,90"))
    t.client.wait_for(lambda f: f.primary[center] == 3)
    t.exchange(commands(b"2RESET", *queries), b"0\xff0\xff100\xff0\xff")


def check_window_worlds(t):
    t.case("landscape, raycaster and map overviews on graphic window pages")
    window = b"2GWIN,1,20,119,30,129,1200,900,0,0,0,0,0,0,0,0,0,0,1,World"
    top = 30 * 640 + 20
    frame = t.exchange(commands(b"2RESET", b"2C,41", b"2aFILL", b"2CPYWP2VP",
                                b"2S,0,1,0,1,Landmark", window,
                                b"2CHVGP,1,700,500", b"2ALERTGWIN,1", b"2C,63", b"2aFILL", b"2RGWINI",
                                b"5CLAND,123", b"5LGMAPWP,0,254", b"5LDMAP,700,754", b"2aG,700,500"),
                       b"41\xff")
    assert frame.primary[top] == 63  # Map writes wait for an interior refresh.
    frame = t.exchange(commands(b"2RGWINI", b"2ANIM,1,710,511,0,Landmark,Landmark,0,0,0,0,30,0",
                                b"2CALMGWIN", b"2PLAYA,1", b"5LSKY,75", b"5LLPOS,700,699",
                                b"5LANIM,1", b"5LCSPD,1", b"w3", b"5LGY"), b"0\xff")
    assert frame.primary[top] == 41
    t.send(commands(b"2AGWIN,1"))
    mark = top + 10*640 + 10
    f = t.client.wait_for(lambda f: f.primary[top] == 75 and f.primary[mark] == 41)
    assert f.alternate[top] == 75 and f.primary[top-1] == 41
    frame = t.exchange(commands(b"2STOPA,1"))
    assert frame.primary[mark] == frame.alternate[mark] == 75
    # Read the active window's terrain back, then write it to the work page.
    t.exchange(commands(b"5LANIM,0", b"2ALERTGWIN,1", b"2C,32", b"2aFILL", b"5LGMAP,700,754",
                        b"5LDMAPWP,0,254", b"2CALMGWIN", b"2aG,0,0"), b"32\xff")

    # Bind the overview independently of the raycaster, then move its marker
    # while the desktop is alerted. The map fits beyond the physical raster.
    t.exchange(commands(b"2ALERTGWIN,1", b"5CLSMAP,1", b"5SETXPOS,5,0", b"5SETYPOS,5,0",
                        b"52DMAP,700,500,13,27", b"2aG,939,504", b"2CALMGWIN",
                        b"5SETXPOS,6,1", b"52DMMAP,5,5,88,0", b"2ALERTGWIN,1",
                        b"2aG,938,504", b"2aG,939,504"), b"20\xff20\xff88\xff")
    frame = t.exchange(commands(b"2C,63", b"2aFILL", b"2RGWINI", b"2AGWIN,0",
                                b"5DSCALE,40,20", b"5DANIM,710,529,1", b"2CALMGWIN", b"w3"))
    idx = top + 10*640 + 10
    assert frame.primary[idx] == 63
    t.send(commands(b"2AGWIN,1"))
    frame = t.client.wait_for(lambda f: f.primary[idx] in (18, 19))
    assert frame.alternate[idx] == frame.primary[idx] and frame.primary[idx-1] == 63
    assert frame.primary[idx+40] == 63 and frame.primary[idx+20*640] == 63
    t.exchange(commands(b"5DANIM,710,529,0", b"2RESET"))


def check_polygon_objects(t, root):
    t.case("polygon objects, projection, animation and background restoration")

    def check(frame, expected):
        for (x, y), colour in expected.items():
            for plane in (frame.primary, frame.alternate):
                assert plane[y * 640 + x] == colour, (x, y, plane[y * 640 + x], colour)

    camera = b",-100,-100,-100,0,0,0,100"
    face = b"23DFACE,1,0,0,0,-40,0,0,0,-40,0,31"
    frame = t.exchange(commands(b"2RESET", b"2C,7", b"2aFILL", b"2CPYWP2BP", b"2CPYWP2VP",
                                b"23DINITL,1" + camera, face, b"23DPLAY,1,1", b"w2"))
    check(frame, {(105, 105): 16, (135, 135): 7})
    frame = t.exchange(commands(b"23DROLL,1,1,45", b"w2"))
    # A 45-degree roll puts the tips at (128,128) and (72,128).
    check(frame, {(84, 122): 16, (115, 122): 16, (100, 134): 7})
    frame = t.exchange(commands(b"23DROLL,1,1,0", b"23DPAINT,1,1,1,63", b"23DWIRE,1,1", b"w2"))
    check(frame, {(105, 105): 7, (100, 120): 48})
    frame = t.exchange(commands(b"23DWIRE,1,0", b"23DMOVE,1,1,2,-50", b"w2"))
    check(frame, {(100, 120): 7, (155, 105): 48})
    frame = t.exchange(commands(b"23DDIST,1,2,100", b"w2"))
    check(frame, {(155, 105): 7, (310, 210): 48})

    # Import two named meshes with local vertex indices and a fractional divisor.
    def chunk(kind, data):
        return struct.pack("<HI", kind, len(data) + 6) + data

    def triangle(x):
        points = struct.pack("<H9f", 3, x, 0, 0, x - 20, 0, 0, x, -20, 0)
        faces = struct.pack("<5H", 1, 0, 1, 2, 7)
        return chunk(0x4000, b"part\0" + chunk(0x4100, chunk(0x4110, points) + chunk(0x4120, faces)))

    model = chunk(0x4d4d, chunk(0x3d3d, triangle(0) + triangle(-40)))
    record = struct.Struct("<12sIIHH")
    bank = bytearray(record.pack(b"\xff", 0, 0, 0, 0) * 33)
    record.pack_into(bank, 0, b"TWO.3DS", len(bank), len(model), 0, 0)
    record.pack_into(bank, record.size, b"SHORT.3DS", len(bank), len(model)-1, 0, 0)
    marker, = root.rglob("syncterm-cache-name.txt")
    (marker.parent / "DDOFX" / "BBSTHEME" / "Mesh.M").write_bytes(bank + model)
    frame = t.exchange(commands(b"2fMesh.", b"23DDEL,1", b"23DINIT,2,.5,95" + camera + b",TWO.3DS",
                                b"23DINIT,2,.5,95" + camera + b",SHORT.3DS", b"23DPLAY,2,1", b"w2"))
    for plane in (frame.primary, frame.alternate):
        assert {plane[105*640+105], plane[105*640+185]} == {80, 87}
    check(frame, {(310, 210): 7, (160, 105): 7})
    frame = t.exchange(commands(b"~PAUSEBAK", b"23DPLAY,2,0"))
    check(frame, {(105, 105): 7, (185, 105): 7})
    frame = t.exchange(commands(b"~UNPAUSEBAK", b"23DINITL,3" + camera, b"23DEQU,3,2",
                                b"23DDEL,2", b"23DPLAY,3,1", b"w2"))
    assert frame.primary[105*640+185] in (80, 87)
    frame = t.exchange(commands(b"23DINITL,4" + camera, b"23DFACE,4,-160,0,0,-200,0,0,-160,-40,0,127",
                                b"23DMERGE,3,4", b"w2"))
    assert 112 <= frame.primary[105*640+265] <= 127
    frame = t.exchange(commands(b"23DMORPH,3,4,10", b"23DPAINT,4,1,1,159", b"w24"))
    check(frame, {(105, 105): 7, (185, 105): 7, (265, 105): 144})
    frame = t.exchange(commands(b"23DYAW,3,1,180", b"w2"))
    check(frame, {(265, 105): 7})
    frame = t.exchange(commands(b"23DYAW,3,2,-180", b"23DMOVE,3,1,1,-320", b"23DROLL,3,1,180", b"w2"))
    check(frame, {(155, 95): 144, (265, 105): 7})
    frame = t.exchange(commands(b"23DPITCH,3,1,180", b"w2"))
    check(frame, {(155, 105): 144, (155, 95): 7})
    t.exchange(commands(b"23DMOVE,3,1,1,-100", b"23DROLL,3,1,0", b"23DPITCH,3,1,0", b"23DYAW,3,3,45", b"w2"))
    t.client.wait_for(lambda f: f.primary and f.primary[105*640+265] == 7)
    frame = t.exchange(commands(b"23DINIT,0", b"w2"))
    check(frame, {(265, 105): 7, (105, 105): 7})

    # Desktop and window objects use independent backgrounds and clocks.
    # The scrolled window is initially inactive, and CALM precedes PLAY.
    frame = t.exchange(commands(b"2GWIN,1,20,119,30,129,1000,750,0,0,0,0,0,0,0,0,0,0,1,Mesh",
                                b"2CHVGP,1,700,500", b"2ALERTGWIN,1", b"2C,33", b"2aFILL", b"2CPYVP2HP",
                                b"2C,42", b"2aFILL", b"2RGWINI",
                                b"23DINITL,1,-710,-510,-100,0,0,0,100", face, b"2CALMGWIN",
                                b"23DINIT,2,.5,95" + camera + b",TWO.3DS", b"23DPLAY,1,1", b"23DPLAY,2,1", b"w3"))
    check(frame, {(35,45):42, (105,105):42})
    assert frame.primary[105*640+185] in (80,87)
    frame = t.exchange(commands(b"2AGWIN,1", b"w3"))
    check(frame, {(35,45):16, (75,85):42, (105,105):42})
    frame = t.exchange(commands(b"23DMOVE,1,1,1,-750", b"w3"))
    check(frame, {(35,45):33, (75,45):16, (105,105):42})
    frame = t.exchange(commands(b"23DPLAY,1,0", b"23DPLAY,2,0", b"w2"))
    check(frame, {(75,45):33, (105,105):42, (185,105):7})
    # A replacement window has fresh backing and may use a different mask stride.
    frame = t.exchange(commands(b"2CGWIN,1",
                                b"2GWIN,1,20,119,30,129,101,101,0,1,0,0,0,0,0,0,0,0,1,Small",
                                b"23DMOVE,1,1,1,-95", b"23DMOVE,1,2,1,-95", b"23DPLAY,1,1", b"w3"))
    check(frame, {(118,128):16, (117,120):0})
    frame = t.exchange(commands(b"23DDEL,1"))
    check(frame, {(118,128):0})
    t.exchange(commands(b"2RESET"))


def check_raycast_world(t, root):
    t.case("raycaster world, layered objects, SDK map definitions and overview")
    t.exchange(commands(b"2RESET", b"5CLSMAP,6", b"5CHMAP,1,1,1,8", b"5GETMAP,1,1",
                        b"5GETMAP,244,64", b"5GETMAP,0,1", b"5GETMAP,243,63"),
               b"1,1,0,0,0,0\xff1,1,0,0,0,0\xff0,0,6,6,0,0\xff")
    t.exchange(commands(b"5CHOBJ,1,243,2,1,4", b"5DLYR,3", b"5CHOBJ,3,243,2,2,15",
                        b"5CHMAP,3,243,2,8", b"5GETMAP,243,2", b"5DLYR,1", b"5GETMAP,243,2",
                        b"5CHOBJ,1,243,2,0,0", b"5CHMAP,6,243,2,5", b"5GETMAP,243,2"),
               b"3,8,0,0,3,15\xff3,8,0,0,1,4\xff0,0,6,5,0,0\xff")
    rows = ([b"header"]*9 + [b"002-4321wc", b"003-b"] + [b""]*60 + [b"header"]*8
            + [b"002-#-|0aw", b"003-aa"] + [b""]*60)
    contents = b"\r\n".join(rows)
    record = struct.Struct("<12sIIHH")
    bank = bytearray(record.pack(b"\xff", 0, 0, 0, 0) * 33)
    record.pack_into(bank, 0, b"DUNGEON.MAP", len(bank), len(contents), 0, 0)
    marker, = root.rglob("syncterm-cache-name.txt")
    (marker.parent / "DDOFX" / "BBSTHEME" / "World.M").write_bytes(bank + contents)
    expected = (b"1,4,0,0,0,0", b"2,3,0,0,0,0", b"3,2,0,0,0,0", b"4,1,0,0,0,0",
                b"0,0,23,1,0,0", b"0,0,3,23,0,0", b"0,0,6,6,0,0")
    t.exchange(commands(b"2fWorld.", b"5LOADMAP,dungeon.map",
                        *(f"5GETMAP,{x},2".encode() for x in range(243,237,-1)), b"5GETMAP,243,3"),
               b"\xff".join(expected) + b"\xff")
    frame = t.exchange(commands(b"5SETXPOS,44,0", b"5SETYPOS,33,0", b"5SETANGL,90",
                                b"5GETXPOS", b"5GETYPOS", b"5GETANGL", b"52DMAP,10,70,11,5"),
                       b"44\xff33\xff90,EAST\xff")
    for plane in (frame.primary, frame.alternate):
        assert list(plane[71*640+10:71*640+17]) == [11]*5 + [5,5]
        assert plane[102*640+210] == 5 ^ 15
    frame = t.exchange(commands(b"5SETXPOS,45,1", b"52DMMAP,100,20,12,1", b"5GETMAP,100,20",
                                b"5DLYR,3", b"5GETMAP,243,2"), b"0,0,6,6,0,0\xff1,4,0,0,3,15\xff")
    for plane in (frame.primary, frame.alternate):
        assert list(plane[102*640+209:102*640+211]) == [5 ^ 15,5]
        assert plane[89*640+154] == 12

    def maze(seed):
        frame = t.exchange(commands(b"5CMAZE,7,19,20,"+seed, b"5GETXPOS", b"5GETYPOS",
                                    b"52DMAP,0,0,11,5"), re.compile(rb"[0-9]+,[0-9]+\xff5\xff5\xff"))
        return b"".join(bytes(list(frame.primary[y*640:y*640+244])) for y in range(64))

    first = maze(b"81000")
    assert first.count(5) > 4000 and first == maze(b"81000") and first != maze(b"81001")
    assert first[239+4*244] == 5 ^ 15
    t.exchange(commands(b"2RESET"))


def check_raycast_view(t):
    t.case("raycaster perspective, textured layers, animation and movement")
    sprites = []
    for name, colour in [(b"Floor", 23), (b"Roof", 26), (b"Upper", 42), (b"Lower", 56),
                         (b"Clear", 0), (b"Banner", 77), (b"FrameA", 81), (b"FrameB", 82)]:
        sprites.extend([b"2C," + str(colour).encode(), b"2aFILL", b"2S,0,31,0,31," + name])
    t.exchange(commands(*sprites, b"2C,39", b"2aFILL", b"2CPYWP2VP",
                        b"5CTEXTURE,FLCL,1,Floor", b"5CTEXTURE,FLCL,2,Roof",
                        b"5CTEXTURE,HIWALL,3,Upper", b"5CTEXTURE,LOWALL,3,Lower",
                        b"5COBJECT,HIOBJ,1,Clear", b"5COBJECT,LOOBJ,1,Banner",
                        b"5CLSMAP,1", *(f"5CHMAP,6,100,{y},2".encode() for y in range(19,26)),
                        b"5CHMAP,2,100,19,3", b"5SETXPOS,100,0", b"5SETYPOS,25,0",
                        b"5SETANGL,360", b"5CTEXTURE", b"5VANIM,0,1,1", b"5DANIM,10,169,1"))
    def pixel(x,y):
        return (10+y)*640 + 10+x
    f = t.client.wait_for(lambda f: f.primary[pixel(160,75)] == 42)
    for plane in (f.primary, f.alternate):
        assert [plane[pixel(160,y)] for y in (0,75,85,159)] == [26,42,56,23]
        assert plane[pixel(320,80)] == 39
    t.send(commands(b"5CHOBJ,3,100,22,0,1"))
    f = t.client.wait_for(lambda f: f.primary[pixel(160,85)] == 77)
    assert f.primary[pixel(160,75)] == 42  # transparent upper half
    t.send(commands(b"5VANIM,3,1,2,FrameA,FrameB,20", b"5PLAYV,3,1"))
    t.client.wait_for(lambda f: f.primary[pixel(160,85)] == 82)
    t.exchange(commands(b"5STOPV,3,1", b"5DSCALE,160,80", b"5DANIM,480,479,1"))
    f = t.client.wait_for(lambda f: f.primary[440*640+560] == 82)
    assert f.primary[439*640+560] == 42 and f.primary[399*640+560] == 39
    # Passable objects allow movement. An interactive object stops the
    # engine and emits the encounter once, even with another step queued.
    t.exchange(commands(b"5CHOBJ,3,100,24,0,1", b"5VM,0,10", b"5GETYPOS",
                        b"5CHOBJ,2,100,23,2,1", b"5VM,0,10", b"5VM,0,10", b"5GETYPOS"),
               b"24\xff\x15100,23\xff24\xff")
    t.exchange(commands(b"5CHOBJ,2,100,23,0,0", b"5CHMAP,4,100,23,3", b"5DANIM,480,479,1",
                        b"5VM,0,10", b"5GETYPOS", b"5VD,5", b"5GETANGL"), b"24\xff5,NORTH\xff")
    t.exchange(commands(b"2RESET"))


def check_raycast_environment(t, root):
    t.case("save and import binary raycaster environments")
    marker, = root.rglob("syncterm-cache-name.txt")
    theme = marker.parent / "DDOFX" / "BBSTHEME"
    t.exchange(commands(b"2RESET", b"5CLSMAP,7", b"5CHMAP,4,41,12,8", b"5DLYR,2",
                        b"5CHOBJ,3,42,12,2,15", b"5SETANGL,90", b"5DSCALE,160,80",
                        b"5MAPPRO,3600,0,0,0,0,0,9,0,31,21,V,",
                        b"2C,17", b"2aFILL", b"2S,0,31,0,31,Floor",
                        b"5CTEXTURE,FLCL,7,Floor", b"5VANIM,0,7,2,Floor,Floor,50",
                        b"5SAVE3D,Dungeon.Map"))
    data = bytearray((theme / "Dungeon.Map").read_bytes())
    assert len(data) == 1141178
    world = 262250 + 4*65534
    at = (12+19)*256 + 41+4
    assert (data[world+at], data[world+32768+at]) == (31,169)
    lower = world + 3*65534
    assert (data[lower+at+1], data[lower+32768+at+1]) == (23,174)
    assert data[lower+65534+32768+at+1] == 175
    assert data[983124+11*245+41] == 2
    assert data[999049+7*32:999049+8*32] == bytes([17])*32
    animation = 1130119 + 7*138
    assert data[animation+3] == 2
    assert struct.unpack_from("<3H", data, animation+20) == (1,1,1)
    assert struct.unpack_from("<H", data, 2*(20*245+30)) == (3600,)
    assert data[31850+3599*64+9:31850+3599*64+11] == b"\x01V"
    assert struct.unpack_from("<HBHH", data, len(data)-7) == (128,1,160,80)

    # Edit the native world plane independently, then import it at a different player position.
    data[world+at], data[world+32768+at] = 5,9
    (theme / "Foreign.Map").write_bytes(data)
    t.exchange(commands(b"2RESET", b"5SETXPOS,31,0", b"5SETYPOS,21,0", b"5LOAD3D,FOREIGN.MAP",
                        b"5GETXPOS", b"5GETYPOS", b"5GETANGL", b"5GETMAP,41,12",
                        b"5DLYR,2", b"5GETMAP,42,12", b"5DANIM,0,159,1", b"w4"),
               b"31\xff21\xff90,EAST\xff0,0,5,9,0,0\xff0,0,7,7,3,15\xffV")
    (theme / "Short.Map").write_bytes(data[:-1])
    data[world+at] = 13
    struct.pack_into("<H", data, 31850+3599*64+62, 65535)
    (theme / "Invalid.Map").write_bytes(data)
    t.exchange(commands(b"5LOAD3D,SHORT.MAP", b"5LOAD3D,INVALID.MAP", b"5LOAD3D,ABSENT.MAP", b"5GETMAP,41,12",
                        b"2RESET"), b"0,0,5,9,0,0\xff")


def check_raycast_recordings(t, root):
    t.case("raycaster movement recording, append, loose and resource playback")
    marker, = root.rglob("syncterm-cache-name.txt")
    theme = marker.parent / "DDOFX" / "BBSTHEME"
    recorded = theme / "Tour.Rec"
    t.exchange(commands(b"2RESET", b"5CLSMAP,1", b"5SETXPOS,31,0", b"5SETYPOS,21,0",
                        b"5SETANGL,90", b"5DREC,0,Tour.Rec", b"5DANIM,0,159,1", b"w3",
                        b"5SETANGL,135", b"w3", b"5DREC,1,STOP"))
    poses = list(struct.iter_unpack("<HHH", recorded.read_bytes()))
    assert [(x//32-4,y//32-19,h) for x,y,h in poses] == [(31,21,128),(31,21,64)]
    t.exchange(commands(b"5DREC,1,TOUR.REC", b"w3", b"5DREC,0,STOP"))
    assert list(struct.iter_unpack("<HHH",recorded.read_bytes())) == poses + [poses[-1]]

    # External recordings store X and Y in 1/32-cell units and a 512-step heading.
    tour = struct.pack("<6H",1120,1280,128,1216,1376,0)
    (theme / "Walk.Rec").write_bytes(tour)
    t.exchange(commands(b"5DPLY,WALK.REC", b"w4", b"5GETXPOS", b"5GETYPOS", b"5GETANGL"),
               b"34\xff24\xff180,SOUTH\xff")
    assert (theme / "Walk.Rec").read_bytes() == tour

    record = struct.Struct("<12sIIHH")
    bank = bytearray(record.pack(b"\xff",0,0,0,0)*33)
    record.pack_into(bank,0,b"MEMBER.REC",len(bank),6,0,0)
    (theme / "Tour.M").write_bytes(bank+tour[:6])
    t.exchange(commands(b"2fTour.", b"~PAUSEBAK", b"5DPLYR,MEMBER.REC"))
    assert (theme / "MEMBER.REC").read_bytes() == tour[:6]
    t.exchange(commands(b"5DPLY,member.rec", b"~UNPAUSEBAK", b"w3", b"5GETXPOS", b"5GETYPOS", b"5GETANGL"),
               b"31\xff21\xff90,EAST\xff")
    assert not (theme / "MEMBER.REC").exists()
    t.exchange(commands(b"~PAUSEBAK", b"5DPLYR,MEMBER.REC", b"2RESET"))
    assert not (theme / "MEMBER.REC").exists()
    t.exchange(commands(b"~PAUSEBAK", b"5DPLYR,MEMBER.REC", b"EWTEXT,member.rec,0,replaced", b"2RESET"))
    assert (theme / "MEMBER.REC").read_bytes() == b"replaced\r"
    t.exchange(commands(b"5DREC,0,tour.rec", b"5DREC,0,STOP"))
    assert recorded.read_bytes() == b""


def check_joysticks(t):
    t.case("Lite joystick detection, button combinations, calibration and held actions")
    t.exchange(commands(b"6TESTJOY,1", b"6TESTJOY,2"), b"N\xffN\xff")
    for slot in (0, 1):
        t.client.request(7, bytes([5, slot]))

    def sample(slot, buttons=0, dpad=0, stick_x=0):
        packet = struct.pack("<BBHBBhhhh6x", 0, 20, buttons << 12 | dpad, 0, 0, stick_x, 0, 0, 0)
        t.client.request(7, bytes([2, slot]) + packet)

    sample(0)
    sample(1)
    t.exchange(commands(b"w2", b"6TESTJOY,1", b"6TESTJOY,2", b"7DEFRET,1,M",
                        b"6DEFJOY,1,3,RUC,U~^M,\x1d(7SRET,1)",
                        b"6DEFJOY,1,0,B3,B,\x1d(6DEFJOY,1,0,AC,,)", b"6DEFJOY,1,1,AC,,"), b"Y\xffY\xff")
    sample(0, 3, 9)  # Up and right while both buttons are held.
    t.input(b"MU,\rMU,\r")
    sample(0, 3)
    t.input(b"B")
    sample(1, stick_x=-30000)
    t.exchange(commands(b"w2", b"6CALJOY,2", b"6DEFJOY,2,0,RT,R,\x1d(6DEFJOY,2,0,CL,,)",
                        b"6DEFJOY,2,1,AC,,"))
    sample(1)
    t.input(b"R")
    t.client.request(7, bytes([3, 0]))
    t.exchange(commands(b"w2", b"6TESTJOY,1", b"6TESTJOY,2"), b"N\xffY\xff")
    t.client.request(7, bytes([3, 1]))
    t.exchange(commands(b"w2", b"6TESTJOY,2", b"2RESET"), b"N\xff")


def check_raycast_profiles(t):
    t.case("raycaster cell entry, shared profiles, and live callback chains")
    t.exchange(commands(b"2RESET", b"5SETXPOS,31,0", b"5SETYPOS,21,0", b"5SETANGL,90",
                        b"5MAPPRO,1,1,46,99,0,0,3,3600,31,21,K,",
                        b"5MAPPRO,3600,0,0,0,0,0,9,0,0,0,A~^M,\x1d(7SRET,1)",
                        b"7DEFRET,1,M", b"5CPYPRO,1,32,21",
                        b"5DANIM,0,159,1", b"w4"), b"MA,\r")
    t.client.text("c")
    t.input(b"cK")
    t.exchange(commands(b"4MKEY,1,46,99,1,P,"))
    t.client.text("c")
    t.input(b"PcK")
    t.exchange(commands(b"4MKEY,1,46,99,0,C,"))
    t.client.text("c")
    t.input(b"C")
    t.exchange(commands(b"4MKEY,0", b"5SETANGL,180"))
    t.client.text("c")
    t.input(b"c")
    t.exchange(commands(b"~PAUSEBAK", b"5SETXPOS,32,0", b"w4", b"~UNPAUSEBAK", b"w4"), b"MA,\r")
    # The first callback redirects its own chain before the interpreter reads the link.
    t.exchange(commands(b"4DEFM,1,1,\x1d(5MAPPRO,1,0,0,0,0,0,9,2,0,0,N,)",
                        b"5MAPPRO,1,0,0,0,0,0,9,3600,0,0,O,\x1d(4RUNM,1)",
                        b"5MAPPRO,2,0,0,0,0,0,9,2,0,0,D,\x1d(5DELMAPPRO,2)",
                        b"5SETXPOS,31,0", b"w4", b"5SETXPOS,32,0", b"w4"), b"ODN")
    t.exchange(commands(b"5FILPRO,3600", b"5SETXPOS,243,0", b"5SETYPOS,63,0",
                        b"w4", b"5DELMAPPRO,3600", b"5SETXPOS,31,0", b"w4"), b"MA,\r")
    t.exchange(commands(b"5MAPPRO,1,1,46,99,1,3,9,0,31,63,J,", b"5SETANGL,360"))
    t.client.request(7, bytes([5, 0]))
    t.client.request(7, bytes([2, 0, 0, 20, 0, 48]) + bytes(16))  # Both primary buttons.
    t.client.mouse(300, 200, action=1, buttons=1)
    t.client.text("c")
    t.input(b"cJ")
    t.client.request(7, bytes([3, 0]))
    t.client.text("c")
    t.input(b"c")
    t.client.mouse(300, 200, action=1, buttons=0)
    t.exchange(commands(b"5MAPPRO,0", b"5SETXPOS,32,0", b"w4",
                        b"5MAPPRO,3,1,46,99,0,0,9,3,32,63,CYCLE,", b"w2"))
    # An unfired circular chain must yield so a later wire command can delete it.
    t.exchange(commands(b"5DELMAPPRO,3", b"2RESET"))


def check_animation_profiles(t):
    t.case("desktop animation queue, frame controls and chained Bezier motion")
    def check(data, expected):
        frame = t.exchange(data)
        for (x, y), value in expected.items():
            for plane in (frame.primary, frame.alternate):
                assert plane[y*640+x] == value, (x, y, value, plane[y*640+x])

    # A is [0,11], B is [0,12], and C is solid 13. Each sprite is two rows high.
    setup = [b"2RESET", b"2C,0", b"2aFILL", b"2C,11", b"2aP,1,0", b"2aP,1,1",
             b"2S,0,1,0,1,A", b"2C,12", b"2aP,1,0", b"2aP,1,1", b"2S,0,1,0,1,B",
             b"2C,13", b"2aFILL", b"2S,0,1,0,1,C", b"2C,60", b"2aFILL", b"2CPYWP2VP"]
    check(commands(*setup, b"2ANIM,400,8,9,2,A,B,0,0,0,0,50,0", b"2PLAYA,400", b"w5"),
          {(8,8):0,(9,9):12,(10,9):60})
    check(commands(b"2ANIM,2,8,9,0,C,C,0,0,0,0,50,0", b"2PLAYA,2", b"w2"),
          {(8,9):13,(9,9):13})
    check(commands(b"2STOPA,2"), {(8,9):0,(9,9):12})
    check(commands(b"2STOPA,400", b"2PLAYA,400", b"w2"), {(8,9):0,(9,9):12})
    check(commands(b"2ANIM,400,8,9,1,A,A,0,0,0,1,20,0", b"2PLAYA,400", b"w2",
                   b"2AFLY,400,-1,9,A,A,2", b"w2"), {(0,9):60,(8,9):60,(9,9):60})
    check(commands(b"2AFLY,400,-1,9,A,A,1", b"w2"), {(0,9):11})
    check(commands(b"2AFLY,400,-1,9,C,C,0", b"w2"), {(0,9):60})

    # At the slowest animator resolution the first frame holds. Changing resolution
    # during that hold rearms playback and the one-shot arrives at its last frame.
    check(commands(b"2RESA,100", b"2ANIM,5,57,59,2,A,C,0,0,0,1,20,0", b"2PLAYA,5", b"w3"),
          {(58,59):11})
    check(commands(b"2RESA,1", b"w5"), {(58,59):13})
    check(commands(b"2RESA,100", b"2PLAYA,5", b"w2"), {(58,59):11})
    check(commands(b"2STOPA,5", b"2RESA,1"), {(58,59):60})

    t.send(commands(b"2ANIM,5,80,80,3,A,C,0,0,0,0,3,0", b"2PLAYA,5"))
    for index in (11,12,13,12):
        t.client.wait_for(lambda f: f.primary[80*640+81] == index)
    check(commands(b"2STOPA,5", b"2RESA,100", b"2PLAYA,5", b"w3"), {(81,80):12})
    check(commands(b"2STOPA,5", b"2RESA,1"), {(81,80):60})

    check(commands(b"2MOTION,255,20,25,30,25,22,25,28,25,3",
                   b"2MOTION,256,31,25,40,27,35,25,39,27,3",
                   b"2MFRAME,256,30,3,2,0,0",
                   b"2ANIM,3,0,0,4,A,B,255,256,2,1,50,0", b"~PAUSEBAK",
                   b"2PLAYA,3", b"w4"), {(21,25):60,(41,27):60})
    check(commands(b"~UNPAUSEBAK", b"w8"), {(21,25):60,(32,25):60,(40,27):60,(41,27):12})
    # Collision edges are inclusive, including the invisible part of an offscreen sprite.
    # Its macro reads the numeric animation ID before returning the collision text.
    t.exchange(commands(b"~PAUSEBAK", b"2COLL,300,-1,-1,8,8,1,1000,400,H,\x1d(7DEFRET,1,DEFVAR1000)\x1d(7SRET,1)",
                        b"2COLL,1,42,42,27,27,1,0,3,MISS,", b"~UNPAUSEBAK", b"w4"), b"400H")
    t.exchange(commands(b"2COLL,2,41,41,27,27,0,0,3,R,\x1d(2COLL,0)", b"w3"), b"R")
    check(commands(b"2STOPA,0", b"2PLAYA,3", b"w2"), {(41,27):60})
    check(commands(b"2RESET"), {(41,27):0})


def check_window_animations(t):
    t.case("animation frames and collisions follow window focus, backing and viewport")
    window = b"2GWIN,1,20,119,30,129,1000,750,0,0,0,0,0,0,0,0,0,0,1,Scene"
    def check(wire, expected, reply=b""):
        frame = t.exchange(commands(*wire), reply)
        for x, y, value in expected:
            index = y * 640 + x
            assert frame.primary[index] == frame.alternate[index] == value, (x, y, frame.primary[index], value)

    # Desktop animation crosses the window's left edge. A dirty VGP pixel
    # stays unpublished until a window command explicitly refreshes it.
    check([b"2RESET", b"2C,14", b"2aFILL", b"2S,0,3,0,3,A",
           b"2C,21", b"2aFILL", b"2S,0,3,0,3,B", b"2C,45", b"2aFILL", b"2CPYWP2VP",
           window, b"2CHVGP,1,700,500", b"2ALERTGWIN,1", b"2C,6", b"2aFILL",
           b"2CPYVP2HP", b"2RGWINI", b"2C,8", b"2aP,700,502",
           b"2COLL,1,700,705,500,510,1,0,0,Window,",
           b"2ANIM,4,702,505,2,A,B,0,0,0,0,90,0", b"2CALMGWIN",
           b"2COLL,2,700,705,500,510,1,0,0,Desktop,",
           b"2ANIM,1,18,35,0,A,A,0,0,0,0,90,0", b"2PLAYA,1", b"2PLAYA,4", b"w3"],
          [(18,32,14),(20,32,6),(22,32,6)])
    check([b"2AGWIN,1", b"w4"], [(18,32,14),(20,32,8),(22,32,21),(25,35,21)], b"Window")
    check([b"2ALERTGWIN,1", b"2ANIM,5,710,513,0,A,A,0,0,0,0,90,0", b"2CALMGWIN",
           b"2PLAYA,5", b"w2", b"2AGWIN,0", b"2AFLY,5,720,523,A,A,0", b"w2"],
          [(30,40,14),(40,50,6),(22,32,21)])
    check([b"2STOPA,4", b"2PLAYA,4", b"2AGWIN,1", b"w3", b"2CHVGP,1,702,502"],
          [(20,30,21),(24,30,6),(28,38,6),(38,48,14)])
    # Closing releases the old background. Recreating the same slot with
    # smaller pages must leave the off-page profile attached and harmless.
    check([b"2CGWIN,1", b"2GWIN,1,40,139,50,149,100,100,0,1,0,0,0,0,0,0,0,0,1,Small",
           b"w3"], [(20,32,14),(40,50,0)])
    check([b"2STOPA,4", b"2STOPA,1", b"2GWIN,0", b"2RESET"], [(20,32,0)])


def check_icons(t):
    t.case("animated icons, pointer input, shared hosted window and procedure order")
    def pixel(frame, x, y, value):
        assert frame.primary[y*640+x] == frame.alternate[y*640+x] == value, (x, y, value)
    frame = t.exchange(commands(b"2RESET", b"2C,23", b"2aFILL", b"2S,0,8,0,8,FACE",
                                b"2C,4", b"2aFILL", b"2CPYWP2VP",
                                b"2MOTION,1,600,400,500,400,560,400,540,400,5",
                                b"2ANIM,1,0,0,1,FACE,FACE,1,1,1,1,60,0",
                                b"2ANIM,2,0,0,0,FACE,FACE,0,0,0,1,60,0",
                                b"2ICON,1,100,110,2,1", b"2ICON,300,150,110,2,2",
                                b"2GICON?,1", b"2GICON?,300", b"2GICON?,301", b"2PLAYA,1", b"2PLAYA,2", b"w3"),
                       b"100,108,102,110\xff150,158,102,110\xff")
    pixel(frame, 100, 110, 23)
    pixel(frame, 150, 110, 23)
    t.exchange(commands(b"4DEFM,1,1,\x1d(2GWIN,2,280,479,80,229,400,300,1,1,0,0,0,0,0,0,0,0,0,Host)",
                        b"2ICONO,1,First,\x1d(4RUNM,1)", b"2ICONC,1,Closed,\x1d(2CGWIN,2)",
                        b"2ICONO,300,Second,", b"2ICONC,300,Again,", b"2ICOND,300,Drop,\x1d(2GICON?,300)"))
    m = frame.metadata
    def mouse(x, y, buttons):
        px = m["destination_x"] + (2*x+1)*m["destination_width"]//1280
        py = m["destination_y"] + (2*y+1)*m["destination_height"]//960
        n = t.client.capture().metadata["events_dequeued"]
        t.client.mouse(px, py, action=1, buttons=buttons)
        t.client.wait_for(lambda f: f.metadata["events_dequeued"] > n)
    mouse(152,105,1)
    mouse(172,125,1)
    mouse(172,125,0)
    t.input(b"170,178,122,130\xffDrop")
    frame = t.exchange(commands(b"2RAICON", b"2GICON?,300"), b"150,158,102,110\xff")
    pixel(frame, 170, 130, 4)
    pixel(frame, 150, 110, 23)
    mouse(104,106,1)
    mouse(104,106,0)
    t.input(b"First")
    frame = t.exchange(commands(b"2VGWIN?,2", b"2OICON,1"), b"Y\xff")
    pixel(frame, 100, 110, 4)
    mouse(433,90,1)  # Return-to-icon frame button.
    mouse(433,90,0)
    t.input(b"Closed")
    t.exchange(commands(b"2VGWIN?,2", b"2OICON,1", b"2OICON,300", b"2DICON,300", b"2CICON,300", b"2CICON,300"),
               b"N\xffFirstClosedSecondAgain")
    frame = t.exchange(commands(b"2MICON,1,400,450", b"2RICON,1", b"2GICON?,1", b"2DICON,300",
                                b"2KICON,300", b"2GICON?,300", b"2ICON,0", b"2GICON?,1"),
                       b"100,108,102,110\xff150,158,102,110\xffDrop")
    pixel(frame, 100, 110, 23)
    pixel(frame, 150, 110, 4)

    # The anchor and stats stay in virtual-page coordinates during a drag.
    frame = t.exchange(commands(b"2GWIN,1,20,119,30,129,1000,750,0,1,0,0,0,0,0,0,0,0,1,Scene",
                                b"2CHVGP,1,700,500", b"2ALERTGWIN,1", b"2ICON,8,710,515,0,2",
                                b"2PLAYA,2", b"w2", b"2CALMGWIN"))
    pixel(frame, 30, 45, 23)
    mouse(33, 40, 1)
    mouse(45, 54, 0)
    frame = t.exchange(commands(b"2GICON?,8", b"2KICON,8"), b"722,730,521,529\xff")
    pixel(frame, 42, 59, 0)
    t.exchange(commands(b"2RESET"))


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

    t.case("image transforms use virtual page bounds, preserve hidden data and wait for refresh")
    frame = t.exchange(commands(b"2GWIN,1,20,119,30,129,1000,750,0,1,0,0,0,0,0,0,0,0,1,Images",
                                b"2CHVGP,1,900,650", b"2ALERTGWIN,1", b"2C,22", b"2aP,998,748",
                                b"2CPYVP2HP", b"5SCALE,HP,998,998,748,748,2,2",
                                b"5ROTATE,HP,VP,998,999,747,748,90,998,747,0,910,660",
                                b"5OUTLN,VP,908,912,658,662,22,40", b"2aG,909,660", b"2aG,911,660"),
                       b"22\xff40\xff")
    assert frame.primary[40*640+29] == 0
    frame = t.exchange(commands(b"2RGWINI"))
    assert frame.primary[40*640+29] == frame.alternate[40*640+29] == 22
    t.exchange(commands(b"2aSTOREPAL", b"2aZEROPAL", b"2aSRGB,30,60,0,0", b"2aSRGB,31,12,0,0",
                        b"2C,30", b"2aP,902,652", b"5ALIAS,VP,902,902,652,652,30,0",
                        b"2aG,902,652", b"2aRESTOREPAL", b"2CPYHP2VP", b"2aG,999,747",
                        b"5ROTATE,HP,WP,999,999,747,747,0,999,747,0,35,40", b"2CGWIN,1", b"2aG,35,40"),
               b"31\xff22\xff22\xff")


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

    t.case("grid draws, moves, animates and restores its original window after CALM")
    window = b"2GWIN,1,20,119,30,129,1000,750,0,1,0,0,0,0,0,0,0,0,1,Tiles"
    frame = t.exchange(commands(window, b"2CHVGP,1,900,650", b"2ALERTGWIN,1", b"2C,23", b"2aFILL",
                                b"2CPYVP2HP", b"5DEFGRID,910,660,3,2,2,2,3,2", b"2CALMGWIN",
                                b"5FILGRID,Grass,Grass,1", b"5CHGGRID,2,1,A,B,2", b"5DRWGRID"))
    start = 40*640 + 30
    assert list(frame.primary[start:start+6]) == list(frame.alternate[start:start+6]) == [3,3,3,3,9,3]
    t.client.wait_for(lambda f: f.primary[start+4] == 12)
    frame = t.exchange(commands(b"2AGWIN,0", b"5POSGRID,2,1"))
    stationary = frame.primary[start+1]
    frame = t.exchange(commands(b"w30"))
    assert frame.primary[start+1] == stationary
    t.exchange(commands(b"2AGWIN,1"))
    t.client.wait_for(lambda f: f.primary[start+1] != stationary)
    # Replacing the window drops the old underlay but keeps the stored map.
    frame = t.exchange(commands(b"~PAUSEBAK", b"2CGWIN,1", window, b"2CHVGP,1,900,650", b"2ALERTGWIN,1",
                                b"2C,24", b"2aFILL", b"2CPYVP2HP", b"2CALMGWIN", b"5DRWGRID"))
    assert frame.primary[start] == 3
    frame = t.exchange(commands(b"5KILGRID"))
    assert frame.primary[start] == frame.alternate[start] == 24
    t.exchange(commands(b"2CGWIN,1", b"~UNPAUSEBAK"))


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

    t.case("rotated bitmap text publishes the alerted viewport and preserves the hidden page")
    frame = t.exchange(commands(b"2GWIN,1,20,119,30,129,1000,750,0,1,0,0,0,0,0,0,0,0,1,Bitmap",
                                b"2CHVGP,1,900,650", b"2ALERTGWIN,1", b"2C,7", b"2aFILL",
                                b"2CPYVP2HP", b"2RGWINI", b"2X,920,923,670,673,11,0,90,VP,A",
                                b"2X,920,923,670,673,12,0,0,HP,A", b"2aG,923,671"), b"11\xff")
    for y, row in enumerate(zip(*glyph[::-1]), 50):
        assert list(frame.primary[y*640+40:y*640+44]) == list(frame.alternate[y*640+40:y*640+44]) == list(row)
    frame = t.exchange(commands(b"2CPYHP2VP", b"2aG,921,670", b"2aG,923,671"), b"12\xff7\xff")
    assert frame.primary[50*640+41] == frame.alternate[50*640+41] == 12
    t.exchange(commands(b"2CGWIN,1"))
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


def check_graphic_windows(t):
    t.case("graphic window pages, stacking, viewport and restoration")
    frame = t.exchange(commands(b"2RESET", b"2C,7", b"2aFILL", b"2CPYWP2VP",
                                b"2VGWIN?,11", b"2HGWIN?,1", b"2FGWIN",
                                b"2GWIN,1,10,139,20,129,900,650,0,1,0,0,0,0,0,0,0,0,1,First",
                                b"2AGWIN?,1", b"2VGWIN?,1", b"2ALERTGWIN,1",
                                b"2C,37", b"2aFILL", b"2C,9", b"2aRECT,770,775,540,544",
                                b"2aG,775,544"), b"-1\xff-2\xff0\xffY\xffY\xff9\xff")
    def point(frame, x, y, colour):
        i = y * 640 + x
        assert frame.primary[i] == frame.alternate[i] == colour, (x, y, frame.primary[i], colour)

    point(frame, 10, 20, 0)
    frame = t.exchange(commands(b"2RGWINI", b"2SGWIN?,1"), b"10,139,20,129,0,0\xff")
    point(frame, 10, 20, 37)
    point(frame, 9, 20, 7)
    frame = t.exchange(commands(b"2CHVGP,1,770,540", b"2CHVGP,1,771,1", b"2SGWIN?,1"),
                       b"10,139,20,129,770,540\xff")
    point(frame, 10, 20, 9)
    frame = t.exchange(commands(b"2fArt.", b"2VXY,771,541,ART.PCX"))
    point(frame, 11, 21, 193)
    point(frame, 12, 22, 255)
    frame = t.exchange(commands(b"2CPYVP2HP", b"2C,17", b"2aFILL",
                                b"2CUTHP2VP,771,773,541,542,772,542",
                                b"2aG,772,541", b"2aG,774,542"), b"193\xff6\xff")
    point(frame, 12, 21, 193)
    frame = t.exchange(commands(b"2CPYHP2VP"))
    point(frame, 12, 22, 255)
    frame = t.exchange(commands(b"2VP2PCX", b"2aERASE", b"2LVPPCX,VP", b"2aG,773,542"),
                       b"6\xff")
    point(frame, 12, 22, 255)
    frame = t.exchange(commands(b"2CHVGPR,1,-1,-1", b"2CALMGWIN", b"2aG,10,20",
                                b"2GWIN,2,15,130,16,130,160,150,0,0,0,0,0,0,0,0,0,0,0,Second",
                                b"2FGWIN"), b"7\xff1\xff")
    point(frame, 12, 22, 193)
    point(frame, 30, 45, 37)
    frame = t.exchange(commands(b"2AGWIN,0"))
    point(frame, 30, 45, 0)
    frame = t.exchange(commands(b"2CHGWIN,2,25,140,25,140,1,0,0,0,0,0,0,0,Hidden",
                                b"2HGWIN?,2", b"2RGWIN,2", b"2SGWIN?,2"),
                       b"Y\xff15,130,16,130,0,0\xff")
    point(frame, 30, 45, 37)
    t.exchange(commands(b"2CHGWIN,1,30,159,30,139,0,1,1,1,1,1,1,1,Changed",
                        b"2RAGWIN", b"2SGWIN?,1", b"2RGWIN"), b"10,139,20,129,769,539\xff")
    t.exchange(commands(b"2ZGWIN,1", b"2SGWIN?,1", b"2ZGWIN,1", b"2SGWIN?,1"),
               b"0,639,0,479,260,170\xff10,139,20,129,260,170\xff")
    frame = t.exchange(commands(b"2CGWIN,1", b"2VGWIN?,1", b"2GWIN,0", b"2VGWIN?,2"),
                       b"N\xffN\xff")
    point(frame, 10, 20, 7)
    point(frame, 30, 45, 7)
    t.exchange(commands(b"2GWIN,3,5,104,5,104,100,100,0,1,1,0,0,0,0,0,0,0,1,Hidden",
                        b"2ALERTGWIN,3", b"2C,41", b"2aFILL", b"2HGWIN?,3", b"2AGWIN?,3",
                        b"2GWIN,3,5,104,5,104,100,100,0,0,0,0,0,0,0,0,0,0,1,Again",
                        b"2FGWIN", b"2aG,5,5", b"2ALERTGWIN,3", b"2aG,5,5", b"2RESET",
                        b"2VGWIN?,3"), b"Y\xffY\xff0\xff7\xff0\xffN\xff")


def check_graphic_window_controls(t):
    t.case("graphic window frame controls and close callbacks")
    create = b"2GWIN,1,60,259,80,229,400,300,0,0,0,0,0,0,0,0,0,0,0,First"
    frame = t.exchange(commands(create, b"2DCGWIN,1,46,99,0,R~^M,\x1d(2FGWIN)"))
    m = frame.metadata
    def mouse(x, y, buttons):
        px = m["destination_x"] + (2*x+1)*m["destination_width"]//1280
        py = m["destination_y"] + (2*y+1)*m["destination_height"]//960
        n = t.client.capture().metadata["events_dequeued"]
        t.client.mouse(px, py, action=1, buttons=buttons)
        t.client.wait_for(lambda f: f.metadata["events_dequeued"] > n)

    def click(x, y):
        mouse(x, y, 1)
        mouse(x, y, 0)

    def stats(expected):
        t.exchange(commands(b"2SGWIN?,1"), expected + b"\xff")

    click(249, 90)
    t.input(b"1\xffR,\r")
    t.client.text("c")
    t.input(b"1\xffR,\r")
    stats(b"60,259,80,229,0,0")
    mouse(95, 88, 1)
    mouse(125, 103, 1)
    mouse(125, 103, 0)
    stats(b"90,289,95,244,0,0")
    mouse(286, 241, 1)
    mouse(306, 261, 1)
    mouse(306, 261, 0)
    stats(b"90,309,95,264,0,0")
    click(275, 105)  # Restore the authored rectangle.
    stats(b"60,259,80,229,0,0")
    click(237, 90)
    stats(b"0,423,0,333,0,0")
    click(401, 10)
    stats(b"60,259,80,229,0,0")
    click(242, 222)
    click(253, 212)
    stats(b"60,259,80,229,20,20")
    mouse(110, 222, 1)  # Drag the horizontal thumb.
    mouse(135, 222, 1)
    mouse(135, 222, 0)
    mouse(254, 132, 1)  # Drag the vertical thumb.
    mouse(254, 151, 1)
    mouse(254, 151, 0)
    stats(b"60,259,80,229,45,39")
    t.exchange(commands(b"2CHGWIN,1,60,259,80,229,0,1,1,1,1,1,1,1,Fixed"))
    mouse(95, 88, 1)
    mouse(135, 128, 1)
    mouse(135, 128, 0)
    click(237, 90)
    click(242, 222)
    stats(b"60,259,80,229,45,39")
    t.exchange(commands(b"2DCGWIN,1,46,99,1,BYE,\x1d(2CGWIN,1)"))
    t.client.text("c")
    t.input(b"BYEc")
    t.exchange(commands(b"2VGWIN?,1", create, b"2DCGWIN,1,46,99,0,,"), b"N\xff")
    t.client.text("c")
    t.input(b"c")
    click(249, 90)
    t.exchange(commands(b"2VGWIN?,1", b"2RESET"), b"Y\xff")

    t.case("mouse regions and hover fields follow their window and its virtual origin")
    t.exchange(commands(b"3MREGN,1,0,200,0,160,1,Desktop,",
                        b"2GWIN,3,40,239,30,179,1000,700,0,1,0,0,0,0,0,0,0,0,0,Large",
                        b"2CHVGP,3,700,490", b"2ALERTGWIN,3",
                        b"3MREGN,2,707,718,495,509,1,Window,",
                        b"3MFLD,4,707,718,495,509,0,0,Hover,", b"2CALMGWIN"))
    mouse(62, 60, 0)
    t.input(b"Hover")
    t.exchange(commands(b"2ALERTGWIN,3", b"3MFLD,5,747,758,495,509,0,0,Panned,", b"2CALMGWIN"))
    t.send(commands(b"2CHVGPR,3,40,0"))
    t.input(b"Panned")
    t.send(commands(b"2CHVGPR,3,-40,0"))
    t.input(b"Hover")
    click(62, 60)
    t.input(b"Window")
    t.exchange(commands(b"2CGWIN,3"))
    click(62, 60)
    t.input(b"Desktop")
    t.exchange(commands(b"2GWIN,3,40,239,30,179,1000,700,0,1,0,0,0,0,0,0,0,0,0,Reopened",
                        b"2CHVGP,3,700,490"))
    click(62, 60)
    t.input(b"Window")
    t.case("window sprites and buttons use the owning page after CALM")
    t.exchange(commands(b"3MREG,0", b"3MFLD,0", b"2ALERTGWIN,3", b"2C,42",
                        b"2aRECT,780,784,550,554", b"2SV,780,784,550,554,UP",
                        b"2C,11", b"2aP,780,550", b"2S,780,784,550,554,DOWN",
                        b"2sPH,730,520,DOWN", b"2SH,730,734,516,520,HSPRITE",
                        b"2sRV,740,525,HSPRITE", b"2aG,744,521", b"2aG,740,521",
                        b"2aCLIP,700,709,490,499", b"2sC,708,496,UP", b"2aG,709,494",
                        b"2aG,710,494"), b"11\xff42\xff42\xff0\xff")
    frame = t.exchange(commands(b"5CPUSHB,1,UP,DOWN,710,503,46,99,0,1,P,",
                                b"5CCHKBOX,1,DOWN,UP,UP,720,503,0,1,0,0,0,0,0,C,",
                                b"5CRBUTTON,1,2,2,21,9,UP,UP,730,503,0,1,0,0,0,0,0,R,",
                                b"2RGWINI", b"2CALMGWIN"))
    for x, colour in [(62,42), (72,42), (82,9)]:
        assert frame.primary[59*640 + x] == colour
    click(63, 60)
    t.input(b"P")
    click(73, 60)
    click(83, 60)
    frame = t.exchange(commands(b"5EXCBOX,1", b"5EXRBTN,1"), b"CR")
    assert frame.primary[59*640 + 72] == 11
    assert frame.primary[59*640 + 82] == 21
    t.exchange(commands(b"2CHGWIN,3,40,239,30,179,1,0,0,0,0,0,0,0,Hidden"))
    t.client.text("c")
    t.input(b"P")
    t.exchange(commands(b"2RESET"))


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
    t.case("direct VGA text writes visual or hidden window pixels before viewport refresh")
    frame = t.exchange(commands(b"2GWIN,1,20,119,30,129,1000,750,0,1,0,0,0,0,0,0,0,0,1,Font",
                                b"2CHVGP,1,900,650", b"2ALERTGWIN,1", b"2XT,910,675,23,6,A",
                                b"2XTHP,910,675,93,7,A", b"2aG,910,660", b"2aG,911,660"), b"23\xff6\xff")
    assert frame.primary[40*640+30] == 0
    glyph(t.exchange(commands(b"2RGWINI")),30,40,23,6)
    glyph(t.exchange(commands(b"2CPYHP2VP")),30,40,93,7)
    t.exchange(commands(b"2XTWP,50,31,27,3,A", b"2CGWIN,1", b"2aG,50,16"), b"27\xff")
    frame = t.exchange(commands(b"2RESET", FULL_TEXT_WINDOW) + b"\x1b[H\x1b[37;40mA")
    glyph(frame,0,0,7,0)
    frame = t.exchange(commands(b"2XT,0,15,18,4,A", b"fSecond.FNT", b"fNONE.FNT") + b"\x1b[1;2HB")
    glyph(frame,0,0,18,4)
    glyph(frame,8,0,7,0)
    frame = t.exchange(commands(b"2GEXIT", b"dGETFONT")+b"B", b"Second.FNT\xff")
    glyph(frame,0,0,cell.foreground,cell.background)
    glyph(frame,16,0,cell.foreground,cell.background)
    frame = t.exchange(commands(b"fIBMSTD.FNT", b"dGETFONT"), b"IBMSTD.FNT\xff")
    assert frame.primary[0] != cell.foreground


def check_display_origin(t):
    t.case("absolute visual origin, wrapping, page ownership and background shake")
    def pixel(f, x, y):
        m = f.metadata
        px = m["destination_x"] + (2*x+1)*m["destination_width"]//1280
        py = m["destination_y"] + (2*y+1)*m["destination_height"]//960
        return f.rgb[py*m["output_width"] + px]

    scene = commands(b"2C,1", b"2aFILL", b"2C,12", b"2aRECT,0,4,20,25",
                     b"2C,10", b"2aRECT,20,24,20,25", b"2CPYWP2VP")
    base = t.exchange(scene)
    f = t.exchange(commands(b"2aCHORG,10,10"))
    assert (pixel(f, 631, 11), pixel(f, 11, 11), pixel(f, 11, 471)) == (f.palette[12], f.palette[10], 0)
    assert f.primary == base.primary and f.alternate == base.alternate
    f = t.exchange(commands(b"2CPYVP2WP", b"2aG,1,21", b"2C,14", b"2aP,1,21",
                            b"2CUTWP2VP,1,2,21,22,1,22"), b"12\xff")
    assert pixel(f, 631, 11) == f.palette[14]
    f = t.exchange(commands(b"2RESET") + scene)
    assert pixel(f, 631, 11) == f.palette[12]
    f = t.exchange(commands(b"~PAUSEBAK", b"2aSHAKE", b"2aCHORG,642,1"))
    assert pixel(f, 639, 20) == f.palette[12]
    t.exchange(commands(b"~UNPAUSEBAK", b"dVIDMODE"), b"1\xff")
    t.client.wait_for(lambda f: pixel(f, 50, 478) == 0 and pixel(f, 50, 1) == f.palette[1])
    # One of the last intermediate frames also passes through zero.
    t.client.wait_for(lambda f: f.rgb == base.rgb)
    t.exchange(commands(b"w2"))
    f = t.client.wait_for(lambda f: f.rgb == base.rgb)
    assert f.primary == base.primary
    f = t.exchange(commands(b"2aCHORG,0,480"))
    assert not any(f.rgb)
    t.exchange(commands(b"2aCHORG,0,0", b"2RESET"))


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
    check_text_windows(t)
    check_desktop_text(t)
    frame = t.exchange(commands(FULL_TEXT_WINDOW) + b"\x1b[H\x1b[41m " + commands(b"2CPYVP2WP", b"2aG,3,4",
                                                       b"2aSRGB,4,0,63,63"), b"4\xff")
    assert frame.primary[4 * 640 + 3] == 4 and frame.palette[4] == 0x00ffff
    check_drawing(t)
    check_marquees(t, root)
    check_clocks(t, root)
    check_flics(t, root)
    check_text_scale(t, root)
    check_landscape(t)
    check_window_worlds(t)
    check_polygon_objects(t, root)
    check_raycast_world(t, root)
    check_raycast_view(t)
    check_raycast_environment(t, root)
    check_raycast_recordings(t, root)
    check_raycast_profiles(t)
    check_animation_profiles(t)
    check_window_animations(t)
    check_icons(t)
    check_sprite_banks(t, root)
    check_sprites(t)
    check_grid(t)
    check_image_commands(t)
    check_pcx(t, root)
    check_graphic_windows(t)
    check_graphic_window_controls(t)
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
        (b"LINES.FX", 0, (commands(b":comment") + b"\r\n")*125 + commands(b"dVIDMODE")),
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


def check_full_screen(t):
    t.case("Lite full-screen selection, clearing, usable rows and restoration")

    def layout(frame, status, rows):
        assert frame.application["cterm.status_display"] == status
        assert frame.application["cterm.bottom_margin"] == rows

    # The fixture starts with the status row hidden. Inactive restoration,
    # an invalid choice and selecting the current layout all preserve text.
    frame = t.exchange(b"KEEP" + commands(b"0RSETFUL", b"0SETFUL,0", b"0SETFUL,2"))
    layout(frame, 0, 25)
    assert "KEEP" in frame.text
    frame = t.exchange(commands(b"0setful,1"))
    layout(frame, 1, 24)
    assert "KEEP" not in frame.text
    assert frame.application["cterm.x"] == frame.application["cterm.y"] == 1
    t.client.wait_for(lambda f: "Connected:" in f.lines[24])
    frame = t.exchange(b"\x1b[99;1HROW24" + commands(b"0SETFUL,1"))
    assert frame.lines[23].startswith("ROW24")
    frame = t.exchange(commands(b"0RSETFUL") + b"\x1b[99;1HROW25")
    layout(frame, 0, 25)
    assert frame.lines[24].startswith("ROW25")
    assert "ROW24" not in frame.text

    # Start from a visible indicator and change layouts several times before
    # restoring. Split a real CSI around the graphics command.
    frame = t.exchange(b"\x1b[1$~\x1b[25;" + commands(b"0SETFUL,2") + b"1HEND")
    layout(frame, 0, 25)
    assert frame.lines[24].startswith("END")
    frame = t.exchange(commands(b"0SETFUL,1", b"0SETFUL,2", b"0RSETFUL") + b"TOP")
    layout(frame, 1, 24)
    assert frame.lines[0].startswith("TOP")
    frame = t.exchange(commands(b"LIGMODE", b"LIGMODE", b"0SETFUL,2", b"2GEXIT"))
    layout(frame, 1, 24)
    assert frame.lines[0].startswith("TOP")

    # A host-writable DEC status display can also precede the door.
    frame = t.exchange(b"\x1b[2$~\x1b[1$}HOST" + commands(b"0SETFUL,2") + b"MAIN")
    layout(frame, 0, 25)
    assert frame.lines[0].startswith("MAIN")
    frame = t.exchange(commands(b"0RSETFUL") + b"\x1b[1$}ROW\x1b[0$}")
    layout(frame, 2, 24)
    assert frame.lines[24].startswith("ROW")
    t.exchange(b"\x1b[0$~")


def check_lite_screen(t, root):
    marker, = root.rglob("syncterm-cache-name.txt")
    directory = marker.parent / "DDOFX" / "BBSTHEME"
    record = struct.Struct("<12sIIHH")
    bank = bytearray(record.pack(b"\xff", 0, 0, 0, 0) * 33)
    raw = bytearray(b" \x07" * 2000)
    raw[:4] = b"\x82\x14\x1b\x8e"
    raw[23*160-2:23*160] = b"Z\x47"
    for slot, (name, payload) in enumerate(((b"MENU.RAW", raw), (b"SHORT.RAW", raw[:-1]))):
        record.pack_into(bank, slot * record.size, name, len(bank), len(payload), 0, 0)
        bank.extend(payload)
    (directory / "Screen.M").write_bytes(bank)
    prefix = b"\x1b[2$~\x1b[1$}STATUS\x1b[0$}TOP\x1b[11;6H"
    frame = t.check("Lite RAW copies VGA cells below the first row and above the live status row",
                    prefix + commands(b"2fScreen.", b"!menu.raw", b"!SHORT.RAW", b"!MISSING.RAW")
                    + b"\x1b[6n", (6, 11), ((1, 1, "TOP"), (1, 2, "é←"), (80, 24, "Z"), (1, 25, "STATUS")),
                    expected=b"\x1b[11;6R")
    cell = frame.cells[80]
    assert (cell.foreground, cell.background) == (1, 4)
    assert frame.cells[81].flags & 2
    frame = t.exchange(commands(b"LIGMODE", b"LIGMODE", b"!MENU.RAW", b"dVIDMODE"), b"1\xff")
    assert not any(frame.primary)
    t.exchange(commands(b"2GEXIT") + b"\x1b[0$~")
    t.case("Lite palette fades preserve the text page and restore its colours")
    initial = t.exchange(b"\x1b[31;44mA")
    t.send(commands(b"0TFADEOUT,500"))
    middle = t.client.wait_for(lambda f: 0 < f.palette[1] < initial.palette[1])
    assert middle.lines == initial.lines
    black = t.exchange(b"\x1b[HBlack")
    assert not any(black.palette[:256]) and black.lines[0].startswith("Black")
    restored = t.exchange(commands(b"0TFADEOUT,0", b"0TFADEIN,0"))
    assert restored.palette[:256] == initial.palette[:256] and restored.lines == black.lines
    ordinary = t.exchange(commands(b"0TFADEOUT,-1", b"0TFADEIN,20"))
    assert ordinary.palette[:256] == initial.palette[:256]



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


def check_text_editor(t, root):
    t.case("door edits a multiline CP437 message, cancels, and retrieves the result")
    marker, = root.rglob("syncterm-cache-name.txt")
    theme = marker.parent / "DDOFX" / "BBSTHEME"
    t.send(commands(b"EEDTEXT,MAIL.TXT,^@ok^M,", b"ERTEXT,MAIL.TXT,DONE"))
    t.client.wait_text("Editing MAIL.TXT")
    path, = (p for p in theme.iterdir() if p.name.upper() == "MAIL.TXT")
    assert path.read_bytes() == b""
    t.client.text("ABC")
    t.client.key("left")
    t.client.key("enter")
    t.client.text("é")
    t.client.wait_text("éC")
    t.client.key("f2")
    t.input(b"\0ok\r,AB\r\n\x82CDONE")
    assert path.read_bytes() == b"AB\r\n\x82C"

    original = b"one\rTWO\nthree\r\nend\0\xff"
    path.write_bytes(original)
    t.exchange(commands(b"LIGMODE", b"LIGMODE", b"2C,33", b"2aFILL", b"2CPYWP2VP"))
    for key, expected in (("escape", original), ("f2", b"!"+original)):
        t.send(commands(b"~VARCAP,3", b"EEDTEXT,MAIL.TXT,exit", b"~VARCAP,0",
                        b"ERTEXT,MAIL.TXT,DONE"))
        t.client.wait_text("Editing MAIL.TXT")
        t.client.text("!")
        t.client.wait_text("!one")
        t.client.key(key)
        frame = t.input(b"exit"+expected+b"DONE")
        assert path.read_bytes() == expected
        assert frame.primary[0] == frame.primary[-1] == 33
    t.send(commands(b"EEDTEXT,MAIL.TXT,CLOSED"))
    t.client.wait_text("Editing MAIL.TXT")
    t.client.key("down")
    t.client.key("home")
    t.client.key("backspace")
    t.client.wait_text("!oneTWO")
    path.write_bytes(b"a different session's message")
    t.client.key("f2")
    t.input(b"CLOSED")
    assert path.read_bytes() == b"a different session's message"

    original = b"X" * 65532
    path.write_bytes(original)
    t.send(commands(b"EEDTEXT,MAIL.TXT,LIMIT"))
    t.client.wait_text("Editing MAIL.TXT")
    t.client.text("A")
    t.client.key("delete")
    t.client.text("B")
    t.client.wait_text("BXXX")
    t.client.key("f2")
    t.input(b"LIMIT")
    assert path.read_bytes() == b"B" + original[1:]
    path.write_bytes(original + b"!")
    t.exchange(commands(b"EEDTEXT,MAIL.TXT,LARGE", b"EEDTEXT,../bad.txt,SKIP", b"2GEXIT"), b"LARGESKIP")
    assert path.read_bytes() == original + b"!"


def check_print_file(t, root):
    t.case("saving a DDO print file from Lite and Normal")
    marker, = root.rglob("syncterm-cache-name.txt")
    theme = marker.parent / "DDOFX" / "BBSTHEME"
    theme.mkdir(parents=True, exist_ok=True)
    source = theme / "PRINT.BIN"
    output = root / "downloads" / "OUT.PRN"
    original = bytes(range(256)) * 100
    source.write_bytes(original)
    request = commands(b"EPTEXT,PRINT.BIN,D:\\REMOTE\\OUT.PRN,^@done,^M", b"dVIDMODE")
    t.send(request)
    frame = t.client.wait_text("Save DDO F/X file")
    assert "OUT.PRN" in frame.text
    assert not output.exists()
    click_text(t.client, frame, "[ New ]")
    t.client.key("enter")
    t.input(b"\0done,\r0\xff")
    assert output.read_bytes() == b" " + original

    t.exchange(commands(b"LIGMODE", b"LIGMODE", b"2C,33", b"2aFILL", b"2CPYWP2VP"))
    source.write_bytes(b"")
    t.send(request)
    t.client.wait_text("Save DDO F/X file")
    t.client.key("escape")
    restored = t.input(b"\0done,\r1\xff")
    assert restored.primary[0] == restored.primary[-1] == 33
    assert output.read_bytes() == b" " + original

    t.send(commands(b"~DEFVAR,51,1,PRINT.BIN", b"~DEFVAR,52,1,OUT.PRN", b"~VARCAP,4",
                    b"EPTEXT,DEFVAR51,DEFVAR52,SAVED", b"~VARCAP,0"))
    t.client.wait_text("Save DDO F/X file")
    t.client.key("enter")
    t.client.wait_text("File exists, overwrite?")
    t.client.text("y")
    t.input(b"SAVED")
    assert output.read_bytes() == b" "
    t.exchange(commands(b"EPTEXT,ABSENT.TXT,OUT.PRN,MISSING", b"EPTEXT,../PRINT.BIN,OUT.PRN,BAD",
                        b"EPTEXT,PRINT.BIN,,EMPTY", b"EPTEXT,PRINT.BIN,LPT1,P", b"EPTEXT,PRINT.BIN,COM2,S",
                        b"2GEXIT"), b"MISSINGBADEMPTYPS")
    assert output.read_bytes() == b" "


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
    t.exchange(commands(b"2S,0,8,0,8,ICON", b"2ANIM,1,0,0,0,ICON,ICON,0,0,0,1,20,0",
                        b"2ICON,100,80,65,0,1", b"2MPICON,100,0,99", b"2MPICON,100,0,0",
                        b"2PLAYA,1", b"w3"))
    mouse(81, 60, 0)
    check(t.exchange(), (80,60,3))
    t.exchange(commands(b"2MPICON,100,1,100"))
    mouse(82, 60, 0)
    t.client.wait_for(lambda f: pixel(f,84,59) == f.palette[15])
    t.exchange(commands(b"2KICON,100"))
    mouse(70, 55, 0)
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


def check_lite_slides(t):
    t.case("Lite slides move the displayed page while ANSI continues drawing")
    red = t.exchange(b"\x1b[?25l\x1b[41m\x1b[2J\x1b[HOLD")
    red_rgb = red.palette[1]
    assert red_rgb in red.rgb
    t.send(commands(b"0SLIDEU"))
    t.client.wait_for(lambda f: 0 < f.rgb.count(red_rgb) < red.rgb.count(red_rgb))
    hidden = t.exchange(b"\x1b[42m\x1b[2J\x1b[HNEW\x1b[6n", b"\x1b[1;4R")
    assert hidden.lines[0].startswith("NEW") and not any(hidden.rgb)
    green = hidden.palette[2]
    t.send(commands(b"0SLIDED"))
    t.client.wait_for(lambda f: green in f.rgb and f.rgb.count(0) > red.rgb.count(0))
    shown = t.exchange()
    assert shown.primary == hidden.primary and shown.alternate == hidden.alternate
    assert shown.rgb.count(green) > red.rgb.count(red_rgb) * 0.95
    t.send(commands(b"0BOUNCED"))
    t.client.wait_for(lambda f: green in f.rgb and f.rgb.count(green) < shown.rgb.count(green) // 2)
    full = shown.rgb.count(green)
    t.client.wait_for(lambda f: f.rgb.count(green) > full * 0.93)
    t.client.wait_for(lambda f: f.rgb.count(green) < full * 0.85)
    t.client.text("qk")
    assert t.input(b"k").rgb == shown.rgb
    t.exchange(b"\x1b[0m\x1b[?25h\x1b[2J\x1b[H")


def check_sleeper(t):
    t.case("inactivity countdown, dismissal, scene restoration and local hangup")
    before = t.exchange(commands(b"5SLEEPER,0", b"LIGMODE", b"LIGMODE", b"2C,9", b"2aFILL", b"2CPYWP2VP"))
    # The countdown is a local window over an otherwise solid graphics page.
    t.send(commands(b"5SLEEPER,1"))
    warning = t.client.wait_for(lambda f: f.primary != before.primary)
    assert warning.primary[0] == before.primary[0] == 9
    updated = t.client.wait_for(lambda f: f.primary != warning.primary)
    assert updated.primary[0] == 9
    t.send(commands(b"5SLEEPER,8", b"dVIDMODE"))
    t.client.text("c")
    restored = t.input(b"1\xff")
    assert restored.primary == before.primary and restored.alternate == before.alternate
    t.client.text("d")
    t.input(b"d")
    t.send(commands(b"5SLEEPER,0"))
    assert t.peer.recv(1) == b"", "sleep disconnect did not close the transport"


def check_ping(args):
    with connection(args.executable, args.reports, encoding=args.encoding,
                    options="Graphics=DDO F/X 2.0\n", settings="AudioModes=0\n") as fixture:
        t = Terminal(*fixture)
        t.case("one-minute DDO ping arrives on an idle UTF-8 connection")
        t.exchange(commands(b"LIGMODE", b"LIGMODE", b"~DEFVAR,1,2,1", b"~VARCAP,2", b"dVIDMODE"))
        start = time.monotonic()
        t.exchange(commands(b"5PING,DEFVAR1,A^@,^h~^?12345678"))
        t.input(b"A\0,\b~\xff1234", timeout=70)
        assert time.monotonic() - start >= 59
        t.exchange(commands(b"5PING,0,", b"~VARCAP,0", b"7DEFRET,1,DEFVAR2", b"7SRET,1"), b"1")


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
        check_capacity(t)
        check_joysticks(t)
        check_package_downloads(t)
        check_background_download(t)
        check_cache(t, fixture[1])
        check_resources(t, fixture[1])
        check_full_screen(t)
        check_lite_screen(t, fixture[1])
        check_speech(t, fixture[1])
        check_info(t, fixture[1])
        check_text_files(t, fixture[1])
        check_text_editor(t, fixture[1])
        check_print_file(t, fixture[1])
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
        t.exchange(commands(b"LIGMODE", b"LIGMODE"))
        check_display_origin(t)
        t.exchange(commands(b"2GEXIT"))
        check_lite_slides(t)
        check_sleeper(t)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("encoding", choices=("CP437", "UTF-8"))
    parser.add_argument("--reports", type=Path, required=True)
    parser.add_argument("--ping", action="store_true")
    args = parser.parse_args()
    (check_ping if args.ping else run)(args)
