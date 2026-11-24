#!/usr/bin/env python3
"""Exercise local HCL image playback through the application and TCP."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path
import struct
import time

from st_migrated_control_test import connection
from st_rip3_button_styles_connected_test import number
from st_rip3_connected_test import pixel, query
from st_rip3_mouse_connected_test import field
from st_rip3_scroll_connected_test import plane_pixel
from st_terminal_conformance_test import Terminal, cell_at
from syncterm_control import SHIFT


def bitmap_row(depth, row, palette=b"", *, width=2):
    offset = 54 + len(palette)
    file_header = struct.pack("<2sIHHI", b"BM", offset + len(row), 0, 0, offset)
    info_header = struct.pack("<IiiHHIIiiII", 40, width, 1, 1, depth, 0, len(row), 0, 0, 0, 0)
    return file_header + info_header + palette + row


def check_bitmap(t, assets, values, encoding):
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

    t.case("Wire icon and bitmap commands normalize legacy extensions before lookup")
    reset()
    converted = bitmap_row(24, b"\xaa\x00\x00" * 2 + bytes(2))
    for i, (alias, bitmap) in enumerate((("Legacy.iCn", "legacy.BMP"),
                                        ("Legacy.hIc", "legacy.BMH"),
                                        ("Legacy.mSk", "legacy.BMM"))):
        # Both names contain a valid BMP, with different colors. Only the
        # normalized name provides the blue pixels.
        (assets / alias).write_bytes(solid)
        (assets / bitmap).write_bytes(converted)
        x = 210 + 3*i
        frame = wire(b"1I" + coords(x, 30) + b".0.0." + alias.encode()
                     + b"|1b" + coords(x, 31, x+1, 31) + b"....000Z" + alias.encode())
        assert tint(frame, x, 30, 2, 2) == [[blue, blue], [blue, blue]]
        assert (assets / alias).read_bytes() == solid

    (assets / "Raw.icn").write_bytes(solid)
    frame = wire(b"1I" + coords(250, 30) + b"00000Raw.icn"
                 + b"|1b" + coords(250, 31, 251, 31) + b"0000000000Raw.icn")
    assert tint(frame, 250, 30, 2, 2) == [[0xffffff, 0xffffff]] * 2
    assert (assets / "Raw.icn").read_bytes() == solid
    assert not (assets / "Raw.BMP").exists()
    values(b"alive", b"alive")

    t.case("A missing wire icon places its marker at the projected, clipped anchor")
    reset()
    wire(b"v" + coords(400, 100, 420, 115) + b"|S0104|B" + coords(0, 0, 20, 15))
    values(b"$WORLD(CUR,320,240)$")
    frame = wire(b"1I" + coords(3, 2) + b"1400.Missing.bmp")
    for phase in ("primary", "alternate"):
        assert [plane_pixel(frame, x, 104, phase) for x in (400, 405, 406, 420, 421)] == [
            red, red, 0xffffff, 0xffffff, 0]
        assert [plane_pixel(frame, 407, y, phase) for y in (103, 104, 105, 115, 116)] == [
            red, 0xffffff, 0, 0, 0]
    values(b"alive", b"alive")

    t.case("Wire bitmap wallpaper repeats the live composited seed in both blink phases")
    reset()
    glyph = b"\xdb" if encoding == "CP437" else "█".encode()
    frame = t.exchange(b"\x1b[?25l\x1b[5;38;2;17;34;51;48;2;170;187;204m" + glyph)
    phases = ("primary", "alternate")
    retained = tuple(plane_pixel(frame, 1, 1, phase) for phase in phases)
    assert set(retained) == {0x112233, 0xaabbcc}, retained
    frame = wire(b"v" + coords(0, 0, 9, 5) + b"|1b" + coords(1, 1, 2, 1)
                 + b"00" + number(8 | 16, 2) + b"000000Split.BMM")
    for phase, rgb in zip(phases, retained):
        for y in range(6):
            actual = [plane_pixel(frame, x, y, phase) for x in range(10)]
            assert actual == [blue, rgb] * 5, (phase, y, actual)
        assert plane_pixel(frame, 10, 1, phase) == 0

    t.case("Wallpaper skips samples outside the screen backing in both blink phases")
    reset()
    # The first two samples are red and blue. The other two lie beyond X=639.
    (assets / "Edge.BMP").write_bytes(bitmap_row(1, b"\x50" + bytes(3), local_palette, width=4))
    before = t.exchange(b"\x1b[?25l\x1b[5;38;2;17;34;51;48;2;170;187;204m" + glyph)
    frame = wire(b"v" + coords(0, 0, 641, 3) + b"|1b" + coords(638, 1, 641, 1)
                 + b"00" + number(16 | 32, 2) + b"000000Edge.BMP")
    for phase in phases:
        for y in range(4):
            old = [plane_pixel(before, x, y, phase) for x in range(640)]
            expected = [red, blue, None, None] if y % 2 == 0 else [None, None, red, blue]
            actual = [plane_pixel(frame, x, y, phase) for x in range(640)]
            assert actual == [old[x] if p is None else p for x, p in enumerate(expected * 160)], (phase, y)
        assert plane_pixel(frame, 0, 4, phase) == plane_pixel(before, 0, 4, phase)

    t.case("WRITE_ICON normalizes the basename and selects scaled or backing clipboard pixels")
    reset()
    wire(b"S0104|B" + coords(100, 100, 105, 102)
         + b"|S0102|B" + coords(100, 103, 105, 105)
         + b"|1C" + coords(100, 100, 105, 105) + b"0")
    values(b"$WORLD(CUR,1280,960)$")
    wire(b"1W.Scaled.iCn|1W2Device.hIc|1W6Mask.mSk|1W2Extensionless")

    def rows(name, height):
        data = (assets / name).read_bytes()
        assert data[:2] == b"BM"
        assert struct.unpack_from("<iiHH", data, 18) == (7, height, 1, 8)
        offset, = struct.unpack_from("<I", data, 10)
        return [list(data[offset + 8*y:offset + 8*y + 7]) for y in reversed(range(height))]

    assert rows("Device.BMH", 7) == [[4] * 7] * 3 + [[2] * 7] * 4
    assert rows("Scaled.BMP", 5) == [[4] * 7] * 2 + [[2] * 7] * 3
    assert rows("Mask.BMM", 7) == rows("Extensionless.BMP", 7) == rows("Device.BMH", 7)
    assert not (assets / "Scaled.iCn").exists()
    assert not (assets / "Extensionless").exists()
    wire(b"1W2../Escape")
    assert not (assets.parent / "Escape.BMP").exists()
    values(b"alive", b"alive")

    t.case("PORT_WRITE uses raw device crops for screen ports and all backing pixels for bitmaps")
    reset()
    values(b"$UNPROT(PORT,1)$")
    wire(b"2P100002S2S0|2s100|v0A0A0H0D")
    labels = [[1 + 8*y + x for x in range(8)] for y in range(4)]
    wire(b"|".join(b"c" + number(index, 2) + b"|X" + coords(10+x, 10+y)
                   for y, row in enumerate(labels) for x, index in enumerate(row)))
    wire(b"2P200000A0600030000|S0104|B00000905|S0102|B01010804|v01010804")
    values(b"$DVP(1,2)$$PROT(PORT,1,2)$$WORLD(CUR,1280,960)$$COORDSIZE(CUR,3)$")

    def export(port, area, name):
        return b"2W" + number(port, 1) + b"".join(number(p, 3) for p in area) + b"0000" + name.encode("ascii")

    def bmp_pixels(name, expected):
        data = (assets / name).read_bytes()
        height, width = len(expected), len(expected[0])
        assert data[:2] == b"BM"
        header = struct.unpack_from("<iiHH", data, 18)
        assert header == (width, height, 1, 8), (name, header)
        offset, = struct.unpack_from("<I", data, 10)
        padded = (width + 3) // 4 * 4
        raster = b"".join(bytes(row) + bytes(padded-width) for row in reversed(expected))
        assert data[offset:] == raster, (name, data[offset:].hex(), raster.hex())

    before = values(b"$CUR(PORT)$:$ISPROT(PORT,1)$:$ISPROT(PORT,2)$", b"2:1:1")
    crops = (
        ((0, 0, 0, 0), "FullView.bmp", labels),
        ((1, 1, 0, 0), "Remainder.bmp", [row[1:] for row in labels[1:]]),
        ((1, 1, 9, 2), "ClipRight.bmp", [labels[1][1:]]),
        ((1, 1, 2, 9), "ClipBottom.bmp", [[row[1]] for row in labels[1:]]),
    )
    (assets / "Prior.bmp").write_bytes(b"prior")
    frame = wire(b"|".join([export(1, area, name) for area, name, _ in crops]
                           + [export(9, (0, 0, 0, 0), "Prior.bmp"),
                              export(1, (9, 1, 0, 0), "Prior.bmp"),
                              b"2W2" + b"".join(number(p, 3) for p in (1, 1, 2, 2)) + b".0ZBacking.bmp"]))
    assert (frame.primary, frame.alternate) == (before.primary, before.alternate)
    for _, name, expected in crops:
        bmp_pixels(name, expected)
    bmp_pixels("Backing.bmp", [[4]*10] + [[4] + [2]*8 + [4]]*4 + [[4]*10])
    assert (assets / "Prior.bmp").read_bytes() == b"prior"
    values(b"$CUR(PORT)$:$ISPROT(PORT,1)$:$ISPROT(PORT,2)$", b"2:1:1")


def check_host_directory(t, root, rip, values, encoding):
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
    wire(b"2\x1b.")
    values(b"$HOSTDIR$", b"MIX ED")
    wire(b"2\x1b00.Elsewhere")
    values(b"$HOSTDIR$", b"MIX ED")
    wire(b"2\x1b0000../not-a-name")
    values(b"$HOSTDIR$", b"MIX ED")
    wire(b"2\x1b. $oFF$ ")
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

    t.case("Directory name limits count protocol bytes before UTF-8 storage")
    codec = "cp437" if encoding == "CP437" else "utf-8"
    suffix = "│" * (11 if encoding == "CP437" else 3)
    if encoding == "UTF-8":
        suffix += "§"

    def directory_name(prefix):
        result = (prefix + suffix).encode(codec)
        assert len(result) == 12
        return result

    named = directory_name("a")
    values(b"$HOSTDIR( " + named + b" )$$HOSTDIR$", directory_name("A"))
    assert (rip / ("HOST-A" + suffix)).is_dir()
    values(b"$HOSTDIR(" + named + b"b)$ok:$HOSTDIR$", b"ok:" + directory_name("A"))
    assert not (rip / ("HOST-A" + suffix + "B")).exists()

    named = directory_name("b")
    wire(b"T$HOSTDIR(" + named + b")$")
    values(b"$HOSTDIR$", directory_name("B"))
    assert (rip / ("HOST-B" + suffix)).is_dir()

    named = directory_name("c")
    values(b"$-=DIR=" + named + b"$")
    wire(b"2\x1b0000$&DIR$")
    values(b"$HOSTDIR$", directory_name("C"))
    assert (rip / ("HOST-C" + suffix)).is_dir()
    wire(b"2\x1b0000$&DIR$X")
    values(b"$HOSTDIR$", directory_name("C"))
    assert not (rip / ("HOST-C" + suffix + "X")).exists()


def check_read_scene(t, root, assets, encoding):
    def read_scene(name):
        return b"1R00000000" + name

    t.case("Local 1R scenes preserve drawing changes across nested returns")
    shared = root / "RIP"
    shared.mkdir(exist_ok=True)
    (assets / "Parent.RIP").write_bytes(
        b"!|c04|X0A0A|1R0A.Child.FN\r\n!|X0B0B\r\n")
    accent = b"\x82" if encoding == "CP437" else "é".encode("utf-8")
    (shared / "Child.FN").write_bytes(
        b"!|c02|X0C0C\r\n\x1b[9;4H" + accent + b"\x1b[6n"
        + query(b"local reply", at_cursor=True))
    t.exchange(query(b"$WORLD(CUR,640,480)$$-=SCENE=Parent.RIP$", at_cursor=True))
    frame = t.exchange(b"\x1b[3;5H\x01|1R..$&SCENE$|X0D0D\r\n",
                       b"\x1b[9;5Rlocal reply")
    assert pixel(frame, 10, 10) == 0xaa0000
    assert [pixel(frame, point, point) for point in (11, 12, 13)] == [0x00aa00] * 3
    assert frame.cell_text(cell_at(frame, 3, 8)) == "é"
    assert frame.application["cterm.x"] == 5 and frame.application["cterm.y"] == 9

    t.case("Missing and recursive 1R scenes return to the caller")
    (assets / "Self.FN").write_bytes(b"!|" + read_scene(b"Self.FN") + b"\r\n")
    frame = t.exchange(b"\x01|1R.00.Child.FN|" + read_scene(b"Missing.RIP") + b"|" + read_scene(b"Self.FN")
                       + b"|X0E0E\r\n" + query(b"alive", at_cursor=True), b"alive")
    assert pixel(frame, 14, 14) == 0x00aa00 and frame.application["connected"] == 1

    t.case("Local scene line endings, continuation, NUL and EOF are local to each file")
    for ending, digit in zip((b"\r", b"\n", b"\r\n", b"\f"), b"GHIJ"):
        (assets / "Boundary.RIP").write_bytes(
            b"!|c\\" + ending + b"0\x003" + ending
            + b"!|1\x1b0000child" + ending + b"!|X0U0U")
        coordinate = b"0" + bytes([digit])
        frame = t.exchange(b"\x01|" + read_scene(b"prefix" * 60 + b"*Boundary")
                           + b"|X" + coordinate * 2 + b"\r", b"child")
        point = int(chr(digit), 36)
        assert pixel(frame, point, point) == 0x00aaaa
        assert pixel(frame, 30, 30) == 0

    (assets / "DosEnd.RIP").write_bytes(b"!|X0U0U\x1a\r!|1\x1b0000unexpected\r")
    frame = t.exchange(b"\x01|" + read_scene(b"DosEnd") + b"|"
                       + read_scene(b"Boundary*") + b"|X0K0K\r"
                       + query(b"alive", at_cursor=True), b"alive")
    assert pixel(frame, 30, 30) == 0 and pixel(frame, 20, 20) == 0x00aaaa

    t.case("Local ordinary LF returns to column one and bare CR stays on its row")
    (assets / "Lines.TXT").write_bytes(b"A\r\nB\nC!\rD\x1aZ")
    frame = t.exchange(b"\x1b[17;4H\x01|" + read_scene(b"Lines.TXT") + b"\r")
    assert frame.lines[16][3:4] == "A"
    assert frame.lines[17][:2] == "B "
    assert frame.lines[18][:3] == "D! "

    t.case("Each nested scene discards its incomplete ANSI syntax and resumes its caller")
    (assets / "AnsiInner.RIP").write_bytes(b"\x1b[21;5H\x1b[32mI\x1b]0;unfinished")
    (assets / "AnsiOuter.RIP").write_bytes(
        b"\x1b[4\x01|" + read_scene(b"AnsiInner") + b"\n4mP\x1b[3")
    frame = t.exchange(b"\x1b[0m\x1b[3\x01|" + read_scene(b"AnsiOuter")
                       + b"\r1mL\x1b[6n", b"\x1b[21;8R")
    assert frame.lines[20][4:7] == "IPL"
    assert frame.application["cterm.attr"] == 0x14

    t.case("A scene cannot consume the caller's pending RIP identification prefix")
    (assets / "AnsiPrefix.RIP").write_bytes(b"!\x1b[")
    t.exchange(b"\x1b[\x01|" + read_scene(b"AnsiPrefix") + b"\r!", b"RIPSCRIP030001")
    t.exchange(b"\x1b[\x01|" + read_scene(b"Missing") + b"\r5n", b"\x1b[0n")

    t.case("Baud emulation paces local scene bytes")
    (assets / "Pace.TXT").write_bytes(b"abcdef")
    t.exchange(b"\x01|3e8C\r\n")  # 300 baud, 99 ms for each three-byte group.
    begin = time.monotonic()
    frame = t.exchange(b"\x1b[12;3H\x01|" + read_scene(b"Pace.TXT") + b"\r\n")
    assert time.monotonic() - begin >= 0.19
    assert frame.lines[11][2:8] == "abcdef"
    t.exchange(b"\x01|3e.\r\n" + query(b"$BAUDEMUL$", at_cursor=True), b"0")

    t.case("HCL scene macros finish nested scenes before replying")
    (assets / "Macro.RIP").write_bytes(query(b"$>Nested$local", at_cursor=True))
    (assets / "Nested.RIP").write_bytes(
        query(b"$-=LOADED=yes$from child", at_cursor=True) + b"\x1b[14;4Hdrawn")
    (assets / "Tail.RIP").write_bytes(query(b"$-=LOADED=last$", at_cursor=True))
    frame = t.exchange(query(b"before$>Missing$$>Macro$$&LOADED$$>Tail$$&LOADED$after", at_cursor=True),
                       b"from childlocalbeforeyeslastafter")
    assert frame.lines[13][3:8] == "drawn"
    t.exchange(b"\x01|2R0000refresh$>Nested$$&LOADED$\r\n")
    t.exchange(query(b"$REFRESH$done", at_cursor=True), b"from childrefreshyesdone")

    t.case("File-query and ANSI replies precede the response that opened the scene")
    (assets / "Reply.RIP").write_bytes(b"!|1F000000Lines.TXT\n\x1b[5n")
    frame = t.exchange(query(b"$>Reply$OK", at_cursor=True), b"1\x1b[0nOK")
    assert frame.application["connected"] == 1

    t.case("Refusing excessive scene depth still completes the callers' responses")
    (assets / "Loop.RIP").write_bytes(query(b"$>Loop$Q", at_cursor=True))
    t.exchange(query(b"X$>Loop$Y", at_cursor=True), b"Q" * 8 + b"XY")

    t.case("Mouse field scene playback resumes the same action")
    (assets / "Click.RIP").write_bytes(b"!|c04|X0F0F\r\n")
    frame = t.exchange(field((100, 120, 140, 150), b"ID=7:$>Click$$FIELDID$done"))
    m = frame.metadata
    x = m["destination_x"] + 121 * m["destination_width"] // m["plane_width"]
    y = m["destination_y"] + 135 * m["destination_height"] // m["plane_height"]
    t.client.mouse(x, y, action=1, buttons=1)
    t.client.mouse(x, y, action=1, buttons=0)
    frame = t.input(b"7done")
    assert pixel(frame, 15, 15) == 0xaa0000

    t.case("A live scene reset retains its local playback rate")
    (assets / "PaceReset.RIP").write_bytes(
        b"!|1\x1b0000BEFORE=$BAUDEMUL$\r!|*\r!|1\x1b0000AFTER=$BAUDEMUL$\r")
    t.exchange(query(b"$CUR(ENV,5)$$BAUDEMUL(2400)$", at_cursor=True))
    t.exchange(b"\x01|" + read_scene(b"PaceReset") + b"\r", b"BEFORE=2400AFTER=2400")
    t.exchange(query(b"$CUR(ENV)$:$BAUDEMUL(CUR)$:$BAUDEMUL(5,2400)$:$BAUDEMUL$",
                     at_cursor=True), b"0:$BAUDEMUL(CUR)$:$BAUDEMUL(5,2400)$:2400")
    t.exchange(query(b"$BAUDEMUL(0)$", at_cursor=True))


def check_wire_jpeg(t, values, supported):
    top = 0x454545 if supported else 0
    bottom = 0xbababa if supported else 0
    values(b"$IMGSTYLE(CUR,20,30,39,49,DELETE)$")
    t.case("wire JPEG initializes the slideshow variable and resolves it at draw time")
    values(b"$INUSE(TV,NEXT_IMG)$/$INUSE(tv,RESX)$", b"0/1")
    t.send(b"!|1p.<")
    frame = t.exchange(b"<IF $INUSE(TV,NEXT_IMG)$=\"0\">>$-=NEXT_IMG=Gray.JPG$"
                       b"<<ENDIF>>$&NEXT_IMG$\r\n")
    assert pixel(frame, 20, 30) == top and pixel(frame, 38, 48) == bottom
    assert pixel(frame, 39, 30) == pixel(frame, 20, 49) == 0
    values(b"$INUSE(TV,NEXT_IMG)$:$&NEXT_IMG$", b"1:Gray.JPG")

    t.case("wire JPEG displays the final progressive scans")
    values(b"$IMGSTYLE(CUR,90,30,109,49)$")
    frame = t.exchange(b"!|1p.Progress.jpg\r\n")
    for phase in ("primary", "alternate"):
        assert plane_pixel(frame, 90, 30, phase) == top
        assert plane_pixel(frame, 108, 48, phase) == bottom
        assert plane_pixel(frame, 109, 30, phase) == 0

    t.case("wire JPEG skips an unselected branch and unquotes the resource name once")
    values(b"$IMGSTYLE(CUR,50,60,69,79)$")
    frame = t.exchange(b"!|1p0000<<IF $INUSE(TV,NEXT_IMG)$=\"0\">>$&UNREACHED$"
                       b"<<ELSE>>G\\$RAY.jpg<<ENDIF>>\r\n")
    assert pixel(frame, 50, 60) == top and pixel(frame, 68, 78) == bottom

    t.case("wire JPEG validation precedes expansion and inactive output keeps provider effects")
    baseline = values(b"$DVP(CUR)$")
    frame = t.exchange(b"!|1p0!00$-=BAD=1$Gray.JPG\r\n!|1p0000$-=SIDE=done$Gray.JPG\r\n"
                       + query(b"$INUSE(TV,BAD)$:$&SIDE$", at_cursor=True), b"0:done")
    assert frame.primary == baseline.primary and frame.alternate == baseline.alternate
    values(b"$AVP(CUR)$")

    t.case("wire image style accepts the zero field and complete reserved suffix")
    area = b"".join(number(v, 2) for v in (80, 90, 98, 108))
    frame = t.exchange(b"!|1i" + area + b".Z short reserved followed by space"
                       b"|1p.Gray.jpg\r\n")
    assert pixel(frame, 80, 90) == 0
    frame = t.exchange(b"!|1i" + area + b".ZZZZZZZZZZZZ ignored|1p.Gray.jpg\r\n")
    assert pixel(frame, 80, 90) == top and pixel(frame, 97, 107) == bottom
    assert pixel(frame, 98, 107) == pixel(frame, 97, 108) == 0


def run(args):
    with connection(args.executable, args.reports, encoding=args.encoding,
                    options="ScreenMode=Custom\nRIP=RIPv3\n",
                    settings="CustomColumns=80\nCustomRows=60\nCustomFontHeight=8\nAudioModes=0\n") as (
                        client, root, peer, send):
        t = Terminal(client, root, peer, send)

        def values(program, expected=b""):
            return t.exchange(query(program, at_cursor=True), expected)

        t.exchange(b"\x1b[!", b"RIPSCRIP030001")
        t.case("RIPv3 tone actions validate and expand with audio disabled")
        values(b"before$BEEP$$BLIP(700)$$T(0,0)$$ALARM$$MUSIC(2)$$PHASER$$REVPHASER$after",
               b"beforeafter")
        invalid = b"$T(1000)$$BEEP(65536)$$MUSIC(-1)$$PHASER(20,40)$$REVPHASER(50,2500,0)$"
        values(b"before" + invalid + b"after", b"before" + invalid + b"after")
        values(b"alive", b"alive")

        owner, = (p for p in (root / "cache").glob("*/syncterm-cache-name.txt")
                  if p.read_text(encoding="utf-8").strip() == "Capture")
        assets = owner.parent / "RIP"
        assets.mkdir(exist_ok=True)
        check_read_scene(t, root, assets, args.encoding)
        t.case("Wire WAV filenames expand through the connected editor")
        t.send(b"\x01|1w1.$*=WAVENAME,24@Wave name$|1\x1b0000$&WAVENAME$:alive\r\n")
        client.wait_text("Wave name")
        client.text("local clip")
        client.key("enter")
        frame = t.input(b"local clip:alive")
        assert frame.application["connected"] == 1
        fixture = Path(__file__).resolve().parents[1] / "media/pixel_image/tests/jpeg/gray.jpg"
        contents = fixture.read_bytes()
        (assets / "Gray.jpg").write_bytes(contents)
        (assets / "Progress.jpg").write_bytes(fixture.with_name("gray-progressive.jpg").read_bytes())
        (assets / "G$RAY.jpg").write_bytes(contents)
        (assets / "Broken.jpg").write_bytes(b"invalid JPEG")

        # The asset's first eight source rows are gray 64, its last is gray 192.
        # The default indexed palette maps those to 69 and 186 respectively.
        t.exchange(b"\x01|*\r\n")
        values(b"$WORLD(CUR,640,480)$")
        send(query(b"$IFS(JPEG)$", at_cursor=True))
        supported = peer.recv(1)
        assert supported in (b"0", b"1"), supported
        check_wire_jpeg(t, values, supported == b"1")
        t.case("HCL JPEG uses current style and cached asset")
        t.exchange(b"\x01|*\r\n")
        values(b"$WORLD(CUR,640,480)$$-=PICTURE=gray$")
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
        check_bitmap(t, assets, values, args.encoding)
        check_host_directory(t, root, assets, values, args.encoding)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("encoding", choices=("CP437", "UTF-8"))
    parser.add_argument("--reports", type=Path, required=True)
    run(parser.parse_args())
