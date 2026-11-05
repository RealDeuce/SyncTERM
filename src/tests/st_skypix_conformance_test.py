#!/usr/bin/env python3
"""SkyPix expectations from JR-Comm reference documents and native font metrics.

Use a reply fence before capturing pixels, cells and state together. No expected
images are generated with SyncTERM. Every fixture disables physical audio.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import binascii
import socket
import struct
import sys
import time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "testing"))
from syncterm_fixture import fixture,write_script


def run(executable, reports, hide_status, kick):
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(15)
        entries = ("[SkyPix]\nConnectionType=Raw\nAddress=127.0.0.1\n"
                   f"Port={listener.getsockname()[1]}\nGraphics=SkyPix (Kick {kick})\n"
                   f"NoStatus={str(hide_status).lower()}\nHidePopups=false\n"
                   "Encoding=UTF-8\nFont=Codepage 437 English\nScreenMode=80x28\n")
        def prepare(root):
            write_script(root,"zzz_skypix_fixture",'''import "syncterm" for Hook, Key, CTerm, Conn, Error
Hook.onKey(Key.f10) {|event|
  var result = CTerm.reset()
  if (result is Error) Fiber.abort(result)
  Conn.sendText("RESET")
  return true
}
''',"connected")
        with fixture(executable, entries=entries, prepare=prepare, settings="AudioModes=0\nScrollBackLines=100\n",
                     reports=reports, quiet=False) as (client, root):
            client.wait_text("SkyPix")
            original = client.capture()
            client.key("enter")
            with listener.accept()[0] as peer:
                peer.settimeout(10)
                client.wait_for(lambda s: s.application.get("connected") == 1)
                def send(data):
                    with (root / "wire.bin").open("ab") as stream:
                        stream.write(data)
                    peer.sendall(data)
                def read(size):
                    result = bytearray()
                    while len(result) < size:
                        data = peer.recv(size-len(result))
                        assert data, result
                        result.extend(data)
                    return bytes(result)
                def exchange(data):
                    send(data + b"\x1b[5n")
                    assert read(4) == b"\x1b[0n"
                    return client.capture()
                frame = exchange(b"\x1b[2J\x1b[H" + b"A\xe9")
                m = frame.metadata
                assert (m["columns"], m["rows"], m["cell_width"], m["cell_height"]) == (80, 25 if hide_status else 26, 8, 8), m
                def check_aspect(snapshot):
                    # The canvas is 4:3; an optional extra status row has the
                    # same 12:5 pixel-height correction as the 200 canvas rows.
                    expected_width,expected_height=100,75 if hide_status else 78
                    meta=snapshot.metadata
                    assert abs(meta["destination_width"]*expected_height-meta["destination_height"]*expected_width)<=expected_width,meta
                check_aspect(frame)
                assert frame.application["cterm.skypix"] == 1 and frame.application["cterm.graphics_mode"] == (3 if kick=="1.3" else 4)
                assert frame.lines[0].startswith("Aé"), frame.lines[:2]
                # Logical geometry stays fixed even when the host reports
                # another text grid; drawable scaling is a separate concern.
                client.grid_resize(100,30)
                resized=exchange(b"")
                assert (resized.metadata["columns"],resized.metadata["rows"],resized.metadata["cell_height"]) == (80,25 if hide_status else 26,8)
                def pixel(frame, x, y):
                    value = frame.primary[(y + frame.metadata["pixel_top"]) * frame.metadata["plane_width"] + x]
                    return (value if value & 0x80000000 else frame.palette[value]) & 0xffffff
                # Pearl/8 A: literal source rows from the pinned bitmap.
                expected = (0,0x10,0x38,0x6c,0xc6,0xfe,0xc6,0xc6)
                for y, row in enumerate(expected):
                    for x in range(8):
                        assert pixel(frame, x, y) == (0xdddddd if row & (0x80 >> x) else 0), (x,y,pixel(frame,x,y))
                        index=(y+frame.metadata["pixel_top"])*frame.metadata["plane_width"]+x
                        assert frame.primary[index]==frame.alternate[index],(x,y)
                frame = exchange(b"\x1b[15;3!\x1b[4;10;20;14;24!\x1b[6;10;19;5;5!\x1b[7;0;0;30;20;5;5;192;255!")
                assert pixel(frame,10,20)==0xff0000 and pixel(frame,34,24)==0xff0000 and pixel(frame,35,24)==0
                # Both phase planes must carry the same SkyPix scene, for
                # primitive pixels and native-font text, across all eight pens.
                for pen in range(8):
                    frame=exchange(f"\x1b[15;{pen}!\x1b[4;{80+pen*8};20;{87+pen*8};27!".encode())
                    for y in range(20,28):
                        for x in range(80+pen*8,88+pen*8):
                            index=(y+frame.metadata["pixel_top"])*frame.metadata["plane_width"]+x
                            assert frame.primary[index]==frame.alternate[index],(pen,x,y)
                frame=exchange(b"\x1b[19;200;30!\x1b[10;20!Diamond.font!f")
                for y in range(30,60):
                    for x in range(195,225):
                        index=(y+frame.metadata["pixel_top"])*frame.metadata["plane_width"]+x
                        assert frame.primary[index]==frame.alternate[index],(x,y)
                exchange(b"\x1b[10;0!")
                palette = b"\x1b[11;0;15;3549;3840;241;943;4082;3086;182;221;175;124;15;1807;3086;3080!"
                frame = exchange(palette+b"\x1b[15;1!\x1b[1;40;20!")
                assert pixel(frame,40,20)==0x0000ff
                # Font widths are pixel advances, not fixed cells. The next R's
                # pixels must occur beyond the first glyph's 8-pixel cell.
                frame = exchange(b"\x1b[19;100;50!\x1b[10;19!Sapphire.font!RR\x1b[10;0!")
                assert any(pixel(frame,x,y) for y in range(50,75) for x in range(116,132))
                start=time.monotonic()
                frame=exchange(b"\x1b[14;12!\x1b[15;4!\x1b[1;50;20!")
                assert time.monotonic()-start >= 0.18 and pixel(frame,50,20)==0x00ff11
                # A long pause must leave the UI responsive and retain unread
                # bytes; local reset cancels it and the suffix still executes.
                send(b"\x1b[14;600!\x1b[15;3!\x1b[1;70;20!\x1b[5n")
                start=time.monotonic()
                frame=client.capture()
                assert time.monotonic()-start<1
                client.key("f10")
                assert read(9)==b"RESET\x1b[0n"
                frame=client.capture()
                assert pixel(frame,70,20)==0xff0000
                send(palette)
                # A transfer begins at the second bang, immediately followed by
                # SOH. SkyPix adds no RIP framing CRLF.
                send(b"\x1b[16;1;2;1!brush.iff!")
                assert read(1)==b"C"
                header=struct.pack(">HHhhBBBBHBBhh",2,1,0,0,1,0,0,0,0,1,1,2,1)
                chunks=b"BMHD"+struct.pack(">I",20)+header+b"CMAP"+struct.pack(">I",6)+b"\x00\x00\x00\xff\xff\xff"+b"BODY"+struct.pack(">I",2)+b"\x80\x00"
                brush=b"FORM"+struct.pack(">I",len(chunks)+4)+b"ILBM"+chunks
                def download(payload, suffix=b""):
                    for number, offset in enumerate(range(0, len(payload), 128), 1):
                        block=payload[offset:offset+128].ljust(128,b"\x1a")
                        send(bytes((1,number,255-number))+block+binascii.crc_hqx(block,0).to_bytes(2,"big"))
                        reply=read(1)
                        while reply==b"C": reply=read(1)
                        assert reply==b"\x06",reply
                    send(b"\x04")
                    reply=read(1)
                    if reply==b"\x15":
                        send(b"\x04"+suffix)
                        reply=read(1)
                    assert reply==b"\x06",reply
                download(brush)
                frame=exchange(b"\x1b[7;0;0;60;20;2;1;192;255!")
                assert pixel(frame,60,20)==0x0000ff and pixel(frame,61,20)==0
                send(b"\x1b[16;1;999;888!brush.iff!")
                assert read(21)==b"\x18"*10+b"\x08"*10+b"\r"
                frame=exchange(b"\x1b[7;0;0;62;20;2;1;192;255!")
                assert pixel(frame,62,20)==0x0000ff
                # General files retain leading LF bytes and publish only a
                # completed transfer. Cancellation and malformed brushes leave
                # no visible cache entry or partial file, and preserve the brush.
                send(b"\x1b[16;20;0;0!general.bin!")
                assert read(1)==b"C"
                payload=b"\n"+b"G"*127
                download(payload,b"\x1b[5n")
                assert read(4)==b"\x1b[0n"
                cached=list(root.rglob("general.bin"))
                assert len(cached)==1 and cached[0].read_bytes()==payload, cached
                send(b"\x1b[16;1;2;1!invalid.iff!")
                assert read(1)==b"C"
                download(b"not an ILBM")
                frame=exchange(b"\x1b[7;0;0;64;20;2;1;192;255!")
                assert pixel(frame,64,20)==0x0000ff
                assert not list(root.rglob("invalid.iff"))
                send(b"\x1b[16;20;0;0!cancel.bin!")
                assert read(1)==b"C"
                send(b"\x18\x18")
                # The shared XMODEM driver echoes its cancellation sequence.
                reply=read(1)
                while reply==b"C": reply=read(1)
                assert reply+read(15)==b"\x18"*8+b"\x08"*8
                client.wait_for(lambda s: s.application.get("transfer.active") == 0 and s.application.get("transfer.reserved") == 0)
                exchange(b"")
                assert not list(root.rglob("cancel.bin")) and not list(root.rglob("*.part"))
                send(b"\x1b[16;2;0;0!sound.iff!")
                assert read(2)==b"\x18\x18"
                frame=exchange(b"\x1bc\x1b[2J\x1b[HEND")
                assert frame.lines[0].startswith("END")
                # Remote EOF also cancels a queued delay instead of holding
                # the disconnected UI for twelve seconds.
                send(b"\x1b[14;600!\x1b[HAFTER-DELAY")
                start=time.monotonic()
                peer.shutdown(socket.SHUT_RDWR)
                notice=client.wait_text("Disconnected: Enter exits, F1 details")
                assert time.monotonic()-start<2
                check_aspect(notice)
                assert (notice.metadata["columns"], notice.metadata["rows"], notice.metadata["cell_height"]) == (80,25 if hide_status else 26,8)
                client.command_key(48)
                viewer=client.wait_text("Scrollback")
                check_aspect(viewer)
                assert viewer.metadata["rows"]==notice.metadata["rows"] and viewer.metadata["cell_height"]==8
                client.key("escape")
                client.wait_text("Disconnected: Enter exits, F1 details")
                client.key("enter")
            restored=client.wait_text("Directory: Name")
            for key in ("columns","rows","cell_width","cell_height"):
                assert restored.metadata[key]==original.metadata[key],key
            saved=(root/"syncterm.lst").read_text()
            assert f"Graphics=SkyPix (Kick {kick})" in saved and "Encoding=UTF-8" in saved and "ScreenMode=80x28" in saved
            assert not (root/"fonts"/"amiga").exists()

if __name__=="__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("executable")
    parser.add_argument("--reports",default="reports/test-control")
    args=parser.parse_args()
    for kick in ("1.3","3.1"):
        for hide in (False,True): run(args.executable,args.reports,hide,kick)
