#!/usr/bin/env python3
"""Connected RIP3 block-mode requests, storage and media presentation."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from binascii import crc_hqx
from pathlib import Path
from socket import SHUT_RDWR
import struct
from zlib import crc32

from st_migrated_control_test import connection
from st_rip3_connected_test import pixel, query
from st_terminal_conformance_test import Terminal
from syncterm_fixture import receive


def check_transfer_unavailable(args, *, reason, extra=(), script=None):
    with connection(args.executable, args.reports, encoding=args.encoding,
                    options="RIP=RIPv3\n", extra=extra, script=script) as (client, root, peer, send):
        terminal = Terminal(client, root, peer, send)
        terminal.case(f"rip3-transfer-unavailable-{reason}")
        terminal.exchange(b"\x1b[!", b"RIPSCRIP030001")
        terminal.exchange(query(b"$IFS(_PROTOCOLS)$:$IFS(LIST,_PROTOCOLS)$:$IFS(XMODEMCRC)$"
                                b":$IFS(YMODEMG)$:$IFS(ZMODEM)$", at_cursor=True), b"0::0:0:0")
        terminal.exchange(b"!|9\x1b01010000blocked.bin<>\r\n" + query(b"alive", at_cursor=True),
                          b"\x18" * 10 + b"alive")
        assert not list(root.rglob("blocked.bin"))
        if script is not None:
            assert b"connected Wren initialization failed" in client.stderr


def run(args):
    with connection(args.executable, args.reports, encoding=args.encoding,
                    options="ScreenMode=Custom\nRIP=RIPv3\n",
                    settings="CustomColumns=80\nCustomRows=60\nCustomFontHeight=8\n"
                             "CustomAspectWidth=4\nCustomAspectHeight=3\n") as (client, root, peer, send):
        terminal = Terminal(client, root, peer, send)
        terminal.exchange(b"\x1b[!", b"RIPSCRIP030001")
        terminal.case("rip3-transfer-capabilities-match-the-connected-worker")
        terminal.exchange(query(b"$IFS(_protocols)$:$IFS(LIST,_PROTOCOLS)$:$IFS(XMODEMCRC)$/$IFS(YMODEMG)$"
                                b"/$IFS(ZMODEM)$:$IFS(KERMIT)$/$IFS(ZMODEMCR)$/$IFS(CISQUICKB)$",
                                at_cursor=True),
                          b"1:XMODEM,XMODEM1K,XMODEM1KG,XMODEMCRC,YMODEM,YMODEMG,ZMODEM:1/1/1:0/0/0")
        terminal.exchange(query(b"$HOSTDIR(ART)$", at_cursor=True))
        cache, = (root / "cache").glob("*/RIP/HOST-ART")

        def packet(number, data):
            return bytes((1, number, 255 - number)) + data + crc_hqx(data, 0).to_bytes(2, "big")

        payload = (b"!|c0F|X0000\r\n\x1b[2J").ljust(128, b"\x1a")

        def stored(filename, data):
            matches = list(root.rglob(filename))
            assert len(matches) == 1 and matches[0].parent.name == "HOST-ART", matches
            assert matches[0].read_bytes() == data

        def reply(data):
            receive(peer, data, root / "replies.bin")

        def send_file(data, prefix=b""):
            for offset in range(0, len(data), 128):
                block = data[offset:offset + 128].ljust(128, b"\x1a")
                send((prefix if offset == 0 else b"") + packet(1 + offset // 128, block))
                reply(b"\x06")

        def end_file(last=b"\x06"):
            send(b"\x04")
            reply(b"\x15")
            send(b"\x04")
            reply(last)

        def batch_file(name, data):
            header = name.encode("ascii") + b"\x00" + str(len(data)).encode("ascii") + b"\x00"
            send(packet(0, header.ljust(128, b"\x00")))
            reply(b"\x06C")
            send_file(data)
            end_file(b"\x06C")

        def end_batch():
            send(packet(0, bytes(128)))
            reply(b"\x06")
            client.wait_for(lambda frame: not frame.application.get("transfer.active"))

        def download(command, filename, prefix=b""):
            send(command)
            receive(peer, b"C", root / "replies.bin")
            # CR was already consumed by the RIP parser. The delayed LF
            # and the first binary block can arrive in the same read.
            send_file(payload, prefix)
            end_file()
            client.wait_for(lambda frame: not frame.application.get("transfer.active"))
            frame = terminal.exchange(query(b"ready", at_cursor=True), b"ready")
            assert frame.application["connected"] == 1
            stored(filename, payload)

        terminal.case("rip3-wire-block-mode-owns-split-crlf-and-binary-payload")
        download(b"!|9\x1b01020000data.bmp<>\r", "data.bmp", b"\n")
        terminal.case("rip3-block-mode-collects-zero-fields-before-the-filename")
        for base, command, name in ((b"10", b".101..", "mega.bin"),
                                    (b"1S", b".11<0<0&", "ultra.bin")):
            terminal.exchange(b"\x01|J" + base + b"\r\n")
            # Type 1 stores the payload without playing it as a RIP scene.
            download(b"\x01|9\x1b" + command + name.encode() + b"<>\r\n", name)
            terminal.exchange(b"\x01|9\x1b.1010..bad.bin<>\r\n"
                              + query(b"alive", at_cursor=True), b"alive")
            assert not (cache / "bad.bin").exists()
        terminal.exchange(b"\x01|J10\r\n")
        terminal.case("rip3-xfer-shares-the-live-transfer-worker")
        download(b"!|1\x1b0000$XFER(receive,XMODEMCRC,none,macro.bin)$\r",
                 "macro.bin", b"\n")

        terminal.case("rip3-xfer-max-arguments-and-twenty-byte-filename")
        names = [b'ABCDEFGHIJKLMNOPQRST'] + [b'x'] * 36
        arguments = b','.join(names)
        filename = b' '.join(names)
        download(query(b'$XFER(RECEIVE,XMODEMCRC,NONE,' + arguments + b')$', at_cursor=True),
                 filename.decode('ascii'))
        send(query(b'$XFER(SEND,XMODEMCRC,NONE,' + arguments + b')$', at_cursor=True))
        client.wait_text("The host requests an upload")
        client.key("escape")
        reply(b"\x18" * 10)
        terminal.exchange(query(b"declined", at_cursor=True), b"declined")
        encoding = args.encoding
        name = 'é' + 'n' * (19 if encoding == 'CP437' else 18)
        encoded = name.encode(encoding)
        assert len(encoded) == 20
        download(query(b'$XFER(RECEIVE,XMODEMCRC,NONE,' + encoded + b')$', at_cursor=True), name)
        for direction in (b'SEND', b'RECEIVE'):
            for invalid in (encoded + b'x', arguments + b',x'):
                source = b'$XFER(' + direction + b',XMODEMCRC,NONE,' + invalid + b')$'
                terminal.exchange(query(source + b':live', at_cursor=True), source + b':live')

        terminal.case("rip3-transfer-type-six-stores-without-playing")
        download(b"!|9\x1b01060000extra.bin<>\r", "extra.bin")

        terminal.case("rip3-both-receive-codes-reach-the-zmodem-worker")
        def z_header(kind, position=0):
            body = bytes((kind,)) + position.to_bytes(4, "little")
            return b"**\x18B" + (body + crc_hqx(body, 0).to_bytes(2, "big")).hex().encode() + b"\r\n"

        def z_subpacket(data, end):
            def escaped(raw):
                return b"".join(bytes((0x18, byte ^ 0x40)) if byte & 0x60 == 0 else bytes((byte,))
                                for byte in raw)

            return escaped(data) + b"\x18" + end + escaped(crc_hqx(data + end, 0).to_bytes(2, "big"))

        def z_receive_init():
            zrinit = receive(peer, b"\r\n\x11", root / "replies.bin")
            assert zrinit[:6] == b"**\x18B01" and len(zrinit) == 21, zrinit
            header = bytes.fromhex(zrinit[4:18].decode("ascii"))
            assert crc_hqx(header[:5], 0) == int.from_bytes(header[5:], "big")

        def z_finish():
            finish_frame = z_header(8)  # ZFIN
            send(finish_frame)
            reply(finish_frame)
            send(b"OO")
            client.wait_for(lambda frame: not frame.application.get("transfer.active"))
            terminal.exchange(query(b"live", at_cursor=True), b"live")

        for request in (b"!|9\x1b07010000<>\r\n", b"!|9\x1b08060000display-name<>\r\n"):
            send(request)
            z_receive_init()
            z_finish()
        assert not (cache / "display-name").exists()

        terminal.case("rip3-zmodem-interruption-removes-the-unfinished-download")
        contents = b"partial data with a completed suffix"
        prefix = contents[:12]
        destination = cache / "Resume.BIN"
        cache_root = cache.parent.parent
        cache_entries = set(cache_root.rglob("*")) | {destination}

        def offer_resume_file(name, *, temporary=False):
            send(b"!|9\x1b0701" + (b"0100" if temporary else b"0000") + b"<>\r\n")
            z_receive_init()
            info = name + b"\x00" + str(len(contents)).encode("ascii") + b"\x00"
            send(z_header(4) + z_subpacket(info, b"k"))  # ZFILE, ZCRCW

        offer_resume_file(b"Resume.BIN", temporary=True)
        reply(z_header(9) + b"\x11")  # ZRPOS
        assert destination.is_file() and set(cache_root.rglob("*")) == cache_entries
        send(z_header(10) + z_subpacket(prefix, b"k"))  # ZDATA, ZCRCW
        reply(z_header(3, len(prefix)))  # ZACK
        client.wait_text("RIP Asset Download")
        client.key("escape")
        reply(b"\x18" * 8 + b"\x08" * 8)
        client.wait_for(lambda frame: not frame.application.get("transfer.active"))
        terminal.exchange(query(b"$FILEEXISTS(Resume.BIN)$:recovered", at_cursor=True), b"0:recovered")
        assert set(cache_root.rglob("*")) == cache_entries - {destination}

        def check_resume_request(*, temporary=False):
            offer_resume_file(b"resume.bin", temporary=temporary)
            request = receive(peer, b"\r\n\x11", root / "replies.bin")
            assert request == z_header(13, len(prefix)) + b"\x11", request  # ZCRC

        terminal.case("rip3-zmodem-reuses-a-verified-partial-file-in-the-bbs-cache")
        for offset in (len(prefix), 0):
            destination.write_bytes(prefix if offset else b"x" * len(prefix))
            check_resume_request()
            send(z_header(13, crc32(prefix)))
            reply(z_header(9, offset) + b"\x11")  # ZRPOS
            send(z_header(10, offset) + z_subpacket(contents[offset:], b"h") +
                 z_header(11, len(contents)))  # ZDATA, ZCRCE, ZEOF
            z_receive_init()
            z_finish()
            stored("Resume.BIN", contents)
            assert set(cache_root.rglob("*")) == cache_entries

        terminal.case("rip3-zmodem-abort-before-opening-output-keeps-the-existing-file")
        destination.write_bytes(prefix)
        check_resume_request(temporary=True)
        client.wait_text("RIP Asset Download")
        client.key("escape")
        reply(b"\x18" * 8 + b"\x08" * 8)
        client.wait_for(lambda frame: not frame.application.get("transfer.active"))
        terminal.exchange(query(b"recovered", at_cursor=True), b"recovered")
        stored("Resume.BIN", prefix)

        terminal.case("rip3-composite-batch-stores-an-unlisted-extension")
        send(b"!|9\x1b05040000batch.dat<>\r\n")
        receive(peer, b"C", root / "replies.bin")
        batch_file("other.dat", b"data")
        end_batch()
        stored("other.dat", b"data")

        # Top row 1,4. Bottom row 8,2. Each source row has four separate
        # one-byte planes, from most significant to least significant.
        icon = bytes.fromhex("01000100 00400080 80004000")
        ega = (0x000000, 0x0000aa, 0x00aa00, 0x00aaaa, 0xaa0000, 0xaa00aa, 0xaa5500, 0xaaaaaa,
               0x555555, 0x5555ff, 0x55ff55, 0x55ffff, 0xff5555, 0xff55ff, 0xffff55, 0xffffff)
        bmp = (struct.pack("<2sIHHI", b"BM", 126, 0, 0, 118) +
               struct.pack("<IiiHHIIiiII", 40, 2, 2, 1, 4, 0, 0, 2, 2, 16, 0) +
               b"".join(rgb.to_bytes(4, "little") for rgb in ega) +
               bytes.fromhex("82000000 14000000"))
        terminal.case("rip3-completed-icon-uses-the-fixed-palette-and-preserves-the-bmp-tail")
        terminal.exchange(b"!|a0104\r\n" + query(b"$PALENTRY(CUR,1)$", at_cursor=True), b"6;42:0:0")
        before = b"X" * len(bmp) + b"previous tail"
        (cache / "ICON.BMP").write_bytes(before)
        send(b"!|9\x1b01020000Icon.iCn<>\r\n")
        reply(b"C")
        send_file(icon)
        stored("ICON.BMP", before)
        end_file()
        # Confirm the host can immediately load the alias after transfer.
        frame = terminal.exchange(query(b"$WORLD(CUR,640,480)$$RESET(PAL,0)$", at_cursor=True)
                                  + b"!|1I0A0K00000icon.icn\r\n")
        assert [pixel(frame, x, y) for y in (20, 21) for x in (10, 11)] == [ega[i] for i in (1, 4, 8, 2)]
        stored("ICON.BMP", bmp + b"previous tail")
        assert not (cache / "Icon.iCn").exists()

        terminal.case("rip3-cached-upload-requires-user-confirmation")
        send(b"!|1\x1b0000$XFER(SEND,XMODEMCRC,NONE,macro.bin)$\r\n")
        client.wait_text("The host requests an upload of cached RIP file macro.bin.")
        client.key("escape")
        receive(peer, b"\x18" * 10, root / "replies.bin")
        terminal.exchange(query(b"declined", at_cursor=True), b"declined")

        terminal.case("rip3-confirmed-upload-sends-the-cached-file")
        send(b"!|9\x1b11010000macro.bin<>\r\n")
        client.wait_text("The host requests an upload of cached RIP file macro.bin.")
        client.text("y")
        client.wait_for(lambda frame: frame.application.get("transfer.active") == 1)
        send(b"C")
        receive(peer, packet(1, payload), root / "replies.bin")
        send(b"\x06")
        receive(peer, b"\x04", root / "replies.bin")
        send(b"\x15")
        receive(peer, b"\x04", root / "replies.bin")
        send(b"\x06")
        client.wait_for(lambda frame: not frame.application.get("transfer.active"))
        terminal.exchange(query(b"uploaded", at_cursor=True), b"uploaded")

        terminal.case("rip3-block-refusal-and-malformed-wire-recover-nonfatally")
        for request in (b"05010000<>", b"09010000invalid.dat<>", b"0A060000<>",
                        b"11010000<>", b"17010000<>", b"17070000macro.bin<>",
                        b"18010000macro.bin<>", b"11070000macro.bin<>",
                        b"21010000macro.bin<>", b"01070000invalid.dat<>"):
            terminal.exchange(b"!|9\x1b" + request + b"\r\n" + query(b"alive", at_cursor=True), b"alive")
        terminal.exchange(query(b"$XFER(RECEIVE,YMODEM,NONE)$"
                                b"$XFER(RECEIVE,YMODEM,NONE,ignored.dat)$alive", at_cursor=True), b"alive")
        terminal.exchange(b"!|9\x1b04010000kermit.bin<>\r\n"
                          + query(b"unsupported", at_cursor=True), b"\x18" * 10 + b"unsupported")
        terminal.exchange(b"!|9\x1b01010000bad.bin<>|c0F\r\n"
                          + query(b"alive", at_cursor=True), b"alive")
        terminal.exchange(b"!|9\x1b01010000../escape.bin<>\r\n"
                          + query(b"confined", at_cursor=True), b"\x18" * 10 + b"confined")
        terminal.exchange(b"!|9U000000\r\n!|9U010000\r\n!|9U020000\r\n"
                          + query(b"$XFER(RECEIVE,XMODEMCRC,NONE)$after", at_cursor=True), b"$XFER(RECEIVE,XMODEMCRC,NONE)$after")
        assert not list(root.rglob("escape.bin"))

        terminal.case("rip3-received-scene-plays-before-the-calling-expression-resumes")
        terminal.exchange(query(b"$WORLD(CUR,640,480)$", at_cursor=True))
        scene = b"!|c0C|X0A0A|m0K0U\r\n"
        send(b"!|1\x1b0000$XFER(RECEIVE,XMODEMCRC,RIP,paint.rip)$$TEXTXY$\r\n")
        reply(b"C")
        send_file(scene)
        assert pixel(client.capture(), 10, 10) == 0
        end_file(b"\x0620:30")
        frame = terminal.exchange(query(b"live", at_cursor=True), b"live")
        assert pixel(frame, 10, 10) == 0xff5555
        stored("paint.rip", scene.ljust(128, b"\x1a"))

        fixture = Path(__file__).resolve().parents[1] / "media/pixel_image/tests/jpeg/gray.jpg"
        jpeg = fixture.read_bytes()
        send(query(b"$IFS(JPEG)$", at_cursor=True))
        supported = peer.recv(1)
        assert supported in (b"0", b"1")
        terminal.case("rip3-received-image-uses-the-current-style-and-retains-the-file")
        terminal.exchange(query(b"$IMGSTYLE(CUR,20,30,39,49,DELETE)$", at_cursor=True))
        send(b"!|9\x1b01030000picture.jpg<>\r\n")
        reply(b"C")
        send_file(jpeg)
        end_file()
        frame = terminal.exchange(query(b"live", at_cursor=True), b"live")
        assert pixel(frame, 20, 30) == (0x454545 if supported == b"1" else 0)
        assert pixel(frame, 38, 48) == (0xbababa if supported == b"1" else 0)
        assert pixel(frame, 39, 30) == 0
        stored("picture.jpg", jpeg.ljust(256, b"\x1a"))

        terminal.case("rip3-completion-requires-jfif-for-automatic-jpeg-display")
        # Remove the APP0 segment from this otherwise valid JPEG.
        app0_end = 4 + int.from_bytes(jpeg[4:6], "big")
        without_jfif = jpeg[:2] + jpeg[app0_end:]
        terminal.exchange(query(b"$IMGSTYLE(CUR,100,30,119,49)$", at_cursor=True))
        send(b"!|9\x1b01030000suffix.jpg<>\r\n")
        reply(b"C")
        send_file(without_jfif)
        end_file()
        frame = terminal.exchange(query(b"live", at_cursor=True), b"live")
        assert pixel(frame, 100, 30) == 0
        stored("suffix.jpg", without_jfif.ljust((len(without_jfif) + 127) // 128 * 128, b"\x1a"))

        terminal.case("rip3-active-batch-uses-receive-order-and-the-transfer-cache")
        first = (b"!|1\x1b0000$HOSTDIR(OTHER)$$IMGSTYLE(CUR,40,50,59,69)$\r\n"
                 b"!|c0C|X0B0B\r\n")
        second = (b"!|1\x1b0000$IMGSTYLE(CUR,70,50,89,69)$\r\n"
                  b"!|c0A|X0B0B\r\n")
        send(b"!|9\x1b05050000batch.dat<>\r\n")
        reply(b"C")
        batch_file("one.RIP", first)
        batch_file("picture2.RIP", jpeg)
        batch_file("no-extension", jpeg)
        batch_file("two.rip", second)
        batch_file("ordinary.dat", b"!|c0F|X0C0C\r\n")
        batch_file("hot.HIC", icon)
        batch_file("mask.msk", icon)
        batch_file("jpeg.icn", jpeg)
        (cache / "BAD.BMP").write_bytes(b"previous bitmap")
        batch_file("bad.icn", b"not a legacy icon")
        (cache / "BLOCKED.BMP").mkdir()
        batch_file("blocked.icn", icon)
        assert pixel(client.capture(), 11, 11) == 0
        stored("hot.HIC", icon)
        assert not (cache / "HOT.BMP").exists()
        end_batch()
        frame = terminal.exchange(query(b"$HOSTDIR$", at_cursor=True), b"OTHER")
        assert pixel(frame, 11, 11) == 0x55ff55
        assert pixel(frame, 12, 12) == 0
        assert pixel(frame, 40, 50) == (0x454545 if supported == b"1" else 0)
        assert pixel(frame, 70, 50) == (0x454545 if supported == b"1" else 0)
        stored("one.RIP", first)
        stored("two.rip", second)
        stored("picture2.RIP", jpeg)
        stored("no-extension", jpeg)
        stored("ordinary.dat", b"!|c0F|X0C0C\r\n")
        stored("HOT.BMP", bmp)
        stored("MASK.BMP", bmp)
        assert not (cache / "hot.HIC").exists() and not (cache / "mask.msk").exists()
        stored("jpeg.icn", jpeg)
        assert not (cache / "JPEG.BMP").exists()
        stored("bad.icn", b"not a legacy icon")
        stored("BAD.BMP", b"previous bitmap")
        stored("blocked.icn", icon)
        assert (cache / "BLOCKED.BMP").is_dir()
        assert not list(cache.parent.rglob("*.part"))

        terminal.case("rip3-cancelled-download-does-not-play-its-partial-scene")
        send(b"!|9\x1b01000100interrupted.rip<>\r\n")
        reply(b"C")
        send_file(b"!|c0F|X0D0D\r\n")
        send(b"\x18\x18")
        reply(b"\x18" * 8 + b"\x08" * 8)
        client.wait_text("Transfer completion failed")
        client.key("escape")
        frame = terminal.exchange(query(b"$FILEEXISTS(interrupted.rip)$:recovered", at_cursor=True),
                                  b"0:recovered")
        assert pixel(frame, 13, 13) == 0
        partial = cache.parent / "HOST-OTHER" / "interrupted.rip"
        assert not partial.exists()

        terminal.case("rip3-temporary-resources-remain-available-through-reset-and-directory-changes")
        terminal.exchange(query(b"$HOSTDIR(ART)$", at_cursor=True))
        download(b"!|9\x1b01010100registered.bin<>\r\n", "registered.bin")
        download(b"!|9\x1b01010000REGISTERED.BIN<>\r\n", "registered.bin")
        terminal.exchange(query(b"$WORLD(CUR,640,480)$", at_cursor=True))
        terminal.exchange(b"!|c0F|X0A0A|1C0A0A0B0B0|1W3SessionIcon.hic|1W1SessionTiny|1W1\r\n")
        terminal.exchange(b"!|2W.0A0A0C0C01.SessionPort.bmp|2W.0A0A0C0C02Z0KeepPort.bmp\r\n")
        port_export = (cache / "SessionPort.bmp").read_bytes()
        assert port_export.startswith(b"BM")
        assert (cache / "KeepPort.bmp").read_bytes() == port_export
        (cache / "Export.RIP").write_bytes(b"!|1W3FromScene\r\n")
        terminal.exchange(query(b"$>Export.RIP$alive", at_cursor=True), b"alive")
        for name in ("SessionIcon.BMH", "SessionTiny.BMP", "FromScene.BMP"):
            assert (cache / name).read_bytes().startswith(b"BM")
        other = cache.parent / "HOST-OTHER"
        shared = root / "RIP"
        shared.mkdir(exist_ok=True)
        for path in (other / "SessionIcon.BMH", shared / "SessionIcon.BMH"):
            path.write_bytes(b"separate resource")
        terminal.exchange(query(b"$RESET(HARD)$$HOSTDIR(OTHER)$", at_cursor=True))
        stored("registered.bin", payload)
        assert (cache / "SessionIcon.BMH").read_bytes().startswith(b"BM")
        terminal.exchange(query(b"$HOSTDIR(ART)$", at_cursor=True))

        terminal.case("rip3-completed-temporary-batch-files-are-processed-and-purged-on-disconnect")
        send(b"!|9\x1b05040100batch.dat<>\r\n")
        reply(b"C")
        batch_file("remove-at-eof.bin", b"completed")
        stored("remove-at-eof.bin", b"completed")
        batch_file("disconnected.icn", icon)
        stored("disconnected.icn", icon)
        assert not (cache / "DISCONNECTED.BMP").exists()
        header = b"unfinished.bin\x00256\x00"
        send(packet(0, header.ljust(128, b"\x00")))
        reply(b"\x06C")
        send_file(bytes(128))
        peer.shutdown(SHUT_RDWR)
        frame = client.wait_for(lambda snap: snap.application.get("session_active") == 0
                                or any("Transfer completion failed" in line for line in snap.lines))
        if frame.application.get("session_active") != 0:
            client.key("escape")
            client.wait_for(lambda snap: snap.application.get("session_active") == 0)
        stored("DISCONNECTED.BMP", bmp)
        assert not (cache / "disconnected.icn").exists()
        for name in ("registered.bin", "SessionIcon.BMH", "SessionTiny.BMP", "FromScene.BMP",
                     "SessionPort.bmp", "remove-at-eof.bin"):
            assert not (cache / name).exists(), name
        assert (cache / "KeepPort.bmp").read_bytes() == port_export
        for path in (other / "SessionIcon.BMH", shared / "SessionIcon.BMH"):
            assert path.read_bytes() == b"separate resource"
        assert not partial.exists() and not (cache / "unfinished.bin").exists()
        stored("Resume.BIN", prefix)
        stored("extra.bin", payload)

    check_transfer_unavailable(args, reason="safe-mode", extra=("-s",))
    check_transfer_unavailable(args, reason="bridge-startup",
                               script='Fiber.abort("transfer bridge test")\n')


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("encoding", choices=("CP437", "UTF-8"))
    parser.add_argument("--reports", type=Path, required=True)
    run(parser.parse_args())
