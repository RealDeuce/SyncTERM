#!/usr/bin/env python3
"""Exercise local HCL image playback through the application and TCP."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path
import struct

from st_migrated_control_test import connection
from st_rip3_button_styles_connected_test import number
from st_rip3_connected_test import pixel, query
from st_rip3_mouse_connected_test import field
from st_terminal_conformance_test import Terminal
from syncterm_control import SHIFT


def bitmap_row(depth, row, palette=b""):
    offset = 54 + len(palette)
    file_header = struct.pack("<2sIHHI", b"BM", offset + len(row), 0, 0, offset)
    info_header = struct.pack("<IiiHHIIiiII", 40, 2, 1, 1, depth, 0, len(row), 0, 0, 0, 0)
    return file_header + info_header + palette + row


def check_bitmap(t, assets, values):
    red, blue, green = 0xaa0000, 0x0000aa, 0x00aa00
    local_palette = b"\x00\x00\xaa\x00\xaa\x00\x00\x00"
    solid = bitmap_row(1, bytes(4), local_palette)
    (assets / "Solid.BMP").write_bytes(solid)
    (assets / "Split.BMM").write_bytes(bitmap_row(1, b"\x40" + bytes(3), local_palette))
    (assets / "Direct.BMP").write_bytes(bitmap_row(24, b"\x00\x00\xaa" * 2 + bytes(2)))
    (assets / "Shade.BMP").write_bytes(bitmap_row(1, b"\x40" + bytes(3),
        b"\x29\x63\x93\x00\xfd\x86\x09\x00"))
    (assets / "Truncated.BMP").write_bytes(solid[:-1])

    def coords(*points):
        return b"".join(number(p, 2) for p in points)

    def wire(commands):
        return t.exchange(b"\x01|" + commands + b"\r\n")

    def style(*points, flags=0):
        return b"1i" + coords(*points) + number(flags, 4)

    def tint(frame, x, y, width, height):
        return [[pixel(frame, x+dx, y+dy) for dx in range(width)] for dy in range(height)]

    def reset():
        wire(b"*")
        values(b"$WORLD(CUR,640,480)$")

    t.case("HCL BMP uses the complete stored rectangle and ignores all image flags")
    reset()
    before = wire(b"a0002|W0|S0102|B00000909|W1|" + style(2, 2, 5, 5, flags=63))
    frame = values(b"before$<solid.icn$after:$IMGSTYLE$", b"beforeafter:2:2:6:6:63")
    assert frame.palette == before.palette and (assets / "Solid.BMP").read_bytes() == solid
    expected = [[red if 2 <= x < 6 and 2 <= y < 6 else green for x in range(8)] for y in range(8)]
    assert tint(frame, 0, 0, 8, 8) == expected
    wire(b"W0|S0102|B00000909")
    frame = values(b"$IMGSTYLE(CUR,2,2,5,5)$$<Direct$")
    assert tint(frame, 0, 0, 8, 8) == expected and frame.palette == before.palette

    t.case("HCL BMP clips without refitting or restarting the dither phase")
    wire(style(2, 2, 5, 5) + b"|v" + coords(4, 3, 9, 9))
    values(b"$WORLD(CUR,320,240)$")
    frame = values(b"$<Split.msk$")
    assert tint(frame, 4, 3, 2, 3) == [[blue, blue]] * 3
    assert pixel(frame, 3, 3) == red and pixel(frame, 4, 2) == red and pixel(frame, 6, 3) == green
    values(b"$WORLD(CUR,640,480)$")
    wire(b"v00000909|" + style(1, 1, 8, 8))
    full = values(b"$<Shade$")
    assert len({p for row in tint(full, 1, 1, 8, 8) for p in row}) > 2
    before = wire(b"S0102|B00000909|v03030505")
    frame = values(b"$<Shade$")
    assert tint(frame, 3, 3, 3, 3) == tint(full, 3, 3, 3, 3)
    assert pixel(frame, 2, 3) == pixel(before, 2, 3) == green
    assert pixel(frame, 6, 3) == pixel(before, 6, 3) == green

    t.case("HCL BMP honors default, one pixel and inactive viewport cases")
    reset()
    wire(b"v03030505")
    frame = values(b"$<Solid$")
    assert tint(frame, 3, 3, 3, 3) == [[red] * 3] * 3 and pixel(frame, 6, 5) == 0
    wire(b"v00000909|" + style(0, 0, 0, 0))
    frame = values(b"$<Solid$")
    assert tint(frame, 0, 0, 2, 2) == [[red, 0], [0, 0]]
    values(b"$DVP(CUR)$")
    before = t.client.capture()
    frame = values(b"$<Truncated$$<Missing$$<Direct$alive", b"alive")
    assert frame.primary == before.primary and frame.alternate == before.alternate
    wire(b"v00000909")
    frame = values(b"$<Truncated$$<Missing$alive", b"alive")
    assert frame.primary == before.primary and frame.alternate == before.alternate

    t.case("HCL BMP uses absolute screen geometry and confines writes to the current port")
    reset()
    wire(b"2P1" + coords(4, 3, 8, 7) + b"0|2s100|v02020909|" + style(2, 2, 5, 5))
    values(b"$PROT(PORT,1)$")
    frame = values(b"$<Split.msk$")
    assert tint(frame, 4, 3, 2, 3) == [[blue, blue]] * 3
    assert pixel(frame, 4, 2) == pixel(frame, 3, 3) == pixel(frame, 6, 3) == 0
    values(b"$CUR(PORT)$:$ISPROT(PORT,1)$", b"1:1")

    t.case("HCL BMP draws in bitmap-local coordinates on the offscreen port")
    before = wire(b"2P2" + coords(0, 0, 8, 8) + b"1|2s200|v03030707|" + style(2, 2, 5, 5))
    frame = values(b"$<Split.msk$")
    assert frame.primary == before.primary and frame.alternate == before.alternate
    frame = wire(b"v00000707|2s000|2C2" + coords(0, 0, 8, 8) + b"0" + coords(30, 30, 38, 38) + b"0")
    assert tint(frame, 32, 32, 4, 4) == [[0, 0, 0, 0]] + [[0, red, blue, blue]] * 3
    assert frame.application["connected"] == 1


def check_host_directory(t, root, rip, values):
    def wire(commands):
        return t.exchange(b"\x01|" + commands + b"\r\n")

    def store(directory, color):
        directory.mkdir(parents=True, exist_ok=True)
        (directory / "Override.bmp").write_bytes(bitmap_row(24, color * 2 + bytes(2)))

    t.case("HOSTDIR selects, queries and clears named cache directories")
    wire(b"*")
    values(b"$WORLD(CUR,640,480)$$IMGSTYLE(CUR,2,2,2,2)$")
    values(b"$HOSTDIR$:$HOSTDIR()$", b"<NONE>:<NONE>")
    values(b"before$HOSTDIR( MiX Ed )$after:$HOSTDIR$", b"beforeafter:MIX ED")
    named = rip / "HOST-MIX ED"
    shared = root / "RIP"
    assert named.is_dir()
    store(named, b"\x00\x00\xaa")
    store(shared, b"\xaa\x00\x00")
    frame = values(b"$<Override$")
    assert pixel(frame, 2, 2) == 0xaa0000
    wire(b"2W0000001010000Capture.bmp")
    assert (named / "Capture.bmp").read_bytes().startswith(b"BM")
    assert not (root / "cache" / "RIP3").exists()
    frame = values(b"$HOSTDIR(Off)$$<Override$$HOSTDIR$", b"<NONE>")
    assert pixel(frame, 2, 2) == 0x0000aa and not (rip / "HOST-OFF").exists()
    wire(b"2W0000001010000AfterOff.bmp")
    assert (rip / "AfterOff.bmp").read_bytes().startswith(b"BM")
    assert not (shared / "AfterOff.bmp").exists() and not (named / "AfterOff.bmp").exists()
    original = (shared / "Override.bmp").read_bytes()
    wire(b"2W0000001010000Override.bmp")
    cached = (rip / "Override.bmp").read_bytes()
    assert cached.startswith(b"BM") and cached != original
    t.case("FILEDEL deletes regular files only in the selected BBS cache directory")
    other_bbs = root / "cache" / "other-bbs" / "RIP"
    store(other_bbs, b"\x00\xaa\x00")
    other_copy = (other_bbs / "Override.bmp").read_bytes()
    (rip / "KeepDir").mkdir()
    values(b"$FILEDEL()$alive", b"alive")
    values(b"$FILEDEL(../Override.bmp)$")
    assert (rip / "Override.bmp").read_bytes() == cached
    values(b"$FILEDEL(AfterOff.ICN)$")
    assert (rip / "AfterOff.bmp").is_file()
    values(b"$HOSTDIR(Mix Ed)$$FILEDEL(Capture.bmp)$$HOSTDIR(Off)$")
    assert not (named / "Capture.bmp").exists()
    assert (named / "Override.bmp").is_file() and (rip / "Override.bmp").is_file()
    values(b"before$FILEDEL(Missing.bmp,KeepDir,oVERRIDE.BMP,AfterOff.bmp)$after", b"beforeafter")
    assert not (rip / "Override.bmp").exists() and not (rip / "AfterOff.bmp").exists()
    assert (rip / "KeepDir").is_dir()
    frame = values(b"$FILEDEL(Override.bmp)$$<Override$")
    assert pixel(frame, 2, 2) == 0x0000aa
    assert (shared / "Override.bmp").read_bytes() == original
    assert (other_bbs / "Override.bmp").read_bytes() == other_copy

    t.case("HOSTDIR retains selection after creation failure and rejects invalid names")
    (rip / "HOST-BLOCKED").write_bytes(b"ordinary file")
    values(b"$HOSTDIR(Blocked)$$HOSTDIR$", b"BLOCKED")
    for bad in (b".", b"..", b"1234567890123", b"foo:bar", b"extra,argument"):
        values(b"$HOSTDIR(" + bad + b")$after:$HOSTDIR$", b"after:BLOCKED")
    values(b"$HOSTDIR(\"\")$")
    frame = values(b"$<Override$$HOSTDIR$", b"BLOCKED")
    assert pixel(frame, 2, 2) == 0x0000aa
    values(b"$HOSTDIR(  )$$HOSTDIR$", b"<NONE>")
    assert not (rip / "HOST-").exists()

    t.case("HOSTDIR avoids following a locally created directory link")
    outside = root / "outside-rip-cache"
    store(outside, b"\x00\x00\xaa")
    try:
        (rip / "HOST-LINKED").symlink_to(outside, target_is_directory=True)
        (rip / "Link.bmp").symlink_to(outside / "Override.bmp")
    except (NotImplementedError, PermissionError):
        pass  # Some Windows fixtures lack directory-link privileges.
    else:
        values(b"$FILEDEL(Link.bmp)$")
        assert (rip / "Link.bmp").is_symlink()
        frame = values(b"$HOSTDIR(Linked)$$<Override$$HOSTDIR$", b"LINKED")
        assert pixel(frame, 2, 2) == 0x0000aa
        wire(b"2W0000001010000Escape.bmp")
        values(b"$FILEDEL(Override.bmp)$")
        assert not (outside / "Escape.bmp").exists() and (outside / "Override.bmp").is_file()

    t.case("A linked RIP cache root cannot receive remote writes, deletions or new directories")
    saved = rip.with_name("RIP.saved")
    rip.rename(saved)
    try:
        try:
            rip.symlink_to(shared, target_is_directory=True)
        except (NotImplementedError, PermissionError):
            pass  # Some Windows fixtures lack directory-link privileges.
        else:
            wire(b"2W0000001010000EscapeRoot.bmp")
            values(b"$FILEDEL(Override.bmp)$")
            assert (shared / "Override.bmp").read_bytes() == original
            values(b"$HOSTDIR(Link-Root)$$HOSTDIR$", b"LINK-ROOT")
            wire(b"2W0000001010000EscapeNamed.bmp")
            values(b"$HOSTDIR(Off)$$HOSTDIR$", b"<NONE>")
            assert not (shared / "EscapeRoot.bmp").exists()
            assert not (shared / "HOST-LINK-ROOT").exists()
    finally:
        if rip.is_symlink():
            rip.unlink()
        saved.rename(rip)

    t.case("FILEDEL ignores a BBS cache bucket redirected to another BBS")
    bucket = rip.parent
    saved = bucket.with_name(bucket.name + ".saved")
    bucket.rename(saved)
    try:
        try:
            bucket.symlink_to(other_bbs.parent, target_is_directory=True)
        except (NotImplementedError, PermissionError):
            pass  # Some Windows fixtures lack directory-link privileges.
        else:
            values(b"$FILEDEL(Override.bmp)$")
            assert (other_bbs / "Override.bmp").read_bytes() == other_copy
    finally:
        if bucket.is_symlink():
            bucket.unlink()
        saved.rename(bucket)

    t.case("Graphical HOSTDIR actions retain their documented side effects")
    values(b"$HOSTDIR(Mix Ed)$")
    wire(b"T$HOSTDIR(OFF)$after")
    values(b"$HOSTDIR$", b"<NONE>")
    assert (rip / "HOST-BLOCKED").read_bytes() == b"ordinary file"

    t.case("Wire directory switching uses the same cache and pre-expansion clear sentinel")
    wire(b"2\x1b0001  MiX Ed  ")
    frame = values(b"$<Override$$HOSTDIR$", b"MIX ED")
    assert pixel(frame, 2, 2) == 0xaa0000
    wire(b"2\x1b0000")
    values(b"$HOSTDIR$", b"MIX ED")
    wire(b"2\x1b0000../not-a-name")
    values(b"$HOSTDIR$", b"MIX ED")
    wire(b"2\x1b0000 $oFF$ ")
    frame = values(b"$<Override$$HOSTDIR$", b"<NONE>")
    assert pixel(frame, 2, 2) == 0x0000aa
    values(b"$-=DIR=Mix Ed$")
    wire(b"2\x1b0000$&DIR$")
    frame = values(b"$<Override$$HOSTDIR$", b"MIX ED")
    assert pixel(frame, 2, 2) == 0xaa0000
    wire(b"2\x1b0000\\$OFF$")
    literal_off = rip / "HOST-$OFF$"
    assert literal_off.is_dir()
    store(literal_off, b"\x00\xaa\x00")
    frame = values(b"$<Override$")
    assert pixel(frame, 2, 2) == 0x00aa00
    wire(b"2\x1b0000$NULL$")
    values(b"$HOSTDIR$", b"<NONE>")
    wire(b"2\x1b0000   ")
    values(b"$HOSTDIR$", b"<NONE>")


def run(args):
    with connection(args.executable, args.reports, encoding=args.encoding,
                    options="ScreenMode=Custom\nRIP=RIPv3\n",
                    settings="CustomColumns=80\nCustomRows=60\nCustomFontHeight=8\nAudioModes=0\n") as (
                        client, root, peer, send):
        t = Terminal(client, root, peer, send)

        def values(program, expected=b""):
            return t.exchange(query(program, at_cursor=True), expected)

        t.exchange(b"\x1b[!", b"RIPSCRIP030001")
        owner, = (p for p in (root / "cache").glob("*/syncterm-cache-name.txt")
                  if p.read_text(encoding="utf-8").strip() == "Capture")
        assets = owner.parent / "RIP"
        assets.mkdir(exist_ok=True)
        fixture = Path(__file__).resolve().parents[1] / "media/pixel_image/tests/jpeg/gray.jpg"
        contents = fixture.read_bytes()
        (assets / "Gray.jpg").write_bytes(contents)
        (assets / "G$RAY.jpg").write_bytes(contents)
        (assets / "Broken.jpg").write_bytes(b"invalid JPEG")

        t.case("HCL JPEG uses current style and cached asset")
        # The asset's first eight source rows are gray 64, its last is gray 192.
        # The default indexed palette maps those to 69 and 186 respectively.
        t.exchange(b"\x01|*\r\n")
        values(b"$WORLD(CUR,640,480)$$-=PICTURE=gray$")
        send(query(b"$IFS(JPEG)$", at_cursor=True))
        supported = peer.recv(1)
        assert supported in (b"0", b"1"), supported
        frame = values(b"$IMGSTYLE(CUR,20,30,39,49,DELETE)$begin$(<<PICTURE>>$end", b"beginend")
        assert pixel(frame, 20, 30) == (0x454545 if supported == b"1" else 0)
        assert pixel(frame, 38, 48) == (0xbababa if supported == b"1" else 0)
        assert pixel(frame, 39, 30) == pixel(frame, 20, 49) == 0
        assert (assets / "Gray.jpg").read_bytes() == contents

        t.case("Playback consumes only its own source")
        baseline = frame
        frame = values(b"a$(Missing$bc$(Broken.jpg$def", b"abcdef")
        assert frame.primary == baseline.primary and frame.alternate == baseline.alternate
        values(b"$(123456789012345678901$ok", b"$(123456789012345678901$ok")
        values(b"before$(unclosed", b"before$(unclosed")
        values(b"alive", b"alive")

        t.case("Refresh also expands playback with a quoted filename dollar")
        values(b"$IMGSTYLE(CUR,50,60,69,79)$")
        t.exchange(b"\x01|2R0000pre$(G\\$RAY$post\r\n")
        frame = values(b"$REFRESH$done", b"prepostdone")
        assert pixel(frame, 50, 60) == (0x454545 if supported == b"1" else 0)
        assert pixel(frame, 68, 78) == (0xbababa if supported == b"1" else 0)
        assert frame.application["connected"] == 1

        t.case("Playback substitutes the current field, capture and modifiers")
        for name in ("F010.jpg", "F110.jpg", "F7010.jpg"):
            (assets / name).write_bytes(contents)
        client.physical_key(42, True, SHIFT)
        frame = values(b"$IMGSTYLE(CUR,80,90,99,109)$$(f*?[]$outside", b"outside")
        assert pixel(frame, 80, 90) == (0x454545 if supported == b"1" else 0)

        t.exchange(field((12, 20, 40, 40), b"ID=7:$MKILL$$(f*?[]$release"))
        m = frame.metadata

        def mouse(x, y, buttons, action=1):
            dx = m["destination_x"] + (2*x+1)*m["destination_width"]//(2*m["plane_width"])
            dy = m["destination_y"] + (2*y+1)*m["destination_height"]//(2*m["plane_height"])
            before = client.capture().metadata["events_dequeued"]
            client.mouse(dx, dy, action=action, buttons=buttons)
            client.wait_for(lambda s: s.metadata["events_dequeued"] > before)

        mouse(20, 24, 1)
        mouse(55, 52, 1, action=0)
        frame = values(b"$IMGSTYLE(CUR,110,120,129,139)$$(f*?[]$held", b"held")
        assert pixel(frame, 110, 120) == (0x454545 if supported == b"1" else 0)
        mouse(20, 24, 1, action=0)
        values(b"$IMGSTYLE(CUR,140,150,159,169)$")
        mouse(20, 24, 0)
        frame = t.input(b"release")
        assert pixel(frame, 140, 150) == (0x454545 if supported == b"1" else 0)
        client.physical_key(42, False)

        t.case("Playback bounds count decoded wire bytes rather than normalized storage")
        label = "é" * (19 if args.encoding == "CP437" else 9)
        label += "" if args.encoding == "CP437" else "A"
        (assets / (label + "$.jpg")).write_bytes(contents)
        encoded = label.encode("cp437" if args.encoding == "CP437" else "utf-8")
        assert len(encoded) == 19
        frame = values(b"$IMGSTYLE(CUR,170,180,189,199)$$( " + encoded + b"\\$$raw",
                       b"$( " + encoded + b"\\$$raw")
        assert pixel(frame, 170, 180) == 0
        frame = values(b"$(" + encoded + b"\\$$good", b"good")
        assert pixel(frame, 170, 180) == (0x454545 if supported == b"1" else 0)
        check_bitmap(t, assets, values)
        check_host_directory(t, root, assets, values)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("encoding", choices=("CP437", "UTF-8"))
    parser.add_argument("--reports", type=Path, required=True)
    run(parser.parse_args())
