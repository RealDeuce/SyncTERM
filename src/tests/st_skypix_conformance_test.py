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
from syncterm_fixture import fixture,write_script,click_text


def run(executable, reports, hide_status, kick):
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(15)
        entries = ("[SkyPix]\nConnectionType=Raw\nAddress=127.0.0.1\n"
                   f"Port={listener.getsockname()[1]}\nGraphics=SkyPix (Kick {kick})\n"
                   f"NoStatus={str(hide_status).lower()}\nHidePopups=false\n"
                   "Encoding=ISO-8859-1\nAmigaFont=Pearl.font\nAmigaFontSize=8\nScreenMode=Amiga640x200\n")
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
                # Startup must allow upper pens without a depth-selection command.
                frame = exchange(b"\x1b[15;15!\x1b[1;200;100!")
                for plane in (frame.primary, frame.alternate):
                    assert plane[(100+frame.metadata["pixel_top"])*640+200] == 527
                frame = exchange(b"\x1b[12!\x1b[2J\x1b[H" + b"A\xe9")
                m = frame.metadata
                assert (m["columns"], m["rows"], m["cell_width"], m["cell_height"]) == (80, 25, 8, 8), m
                def check_aspect(snapshot):
                    # Status consumes space inside the fixed 4:3 mode.
                    assert (snapshot.metadata["plane_width"],snapshot.metadata["plane_height"]) == (640,200)
                    expected_width,expected_height=4,3
                    meta=snapshot.metadata
                    assert abs(meta["destination_width"]*expected_height-meta["destination_height"]*expected_width)<=expected_width,meta
                check_aspect(frame)
                assert frame.application["cterm.skypix"] == 1 and frame.application["cterm.graphics_mode"] == (3 if kick=="1.3" else 4)
                assert frame.lines[0].startswith("Aé"), frame.lines[:2]
                # Logical geometry stays fixed even when the host reports
                # another text grid; drawable scaling is a separate concern.
                client.grid_resize(100,30)
                resized=exchange(b"")
                assert (resized.metadata["columns"],resized.metadata["rows"],resized.metadata["cell_height"]) == (80,25,8)
                def pixel(frame, x, y):
                    value = frame.primary[(y + frame.metadata["pixel_top"]) * frame.metadata["plane_width"] + x]
                    return (value if value & 0x80000000 else frame.palette[value]) & 0xffffff
                # Pearl/8 A: literal source rows from the pinned bitmap.
                expected = (0x10,0x38,0x6c,0xc6,0xfe,0xc6,0xc6,0)
                for y, row in enumerate(expected):
                    for x in range(8):
                        assert pixel(frame, x, y) == (0xdddddd if row & (0x80 >> x) else 0), (x,y,pixel(frame,x,y))
                        index=(y+frame.metadata["pixel_top"])*frame.metadata["plane_width"]+x
                        assert frame.primary[index]==frame.alternate[index],(x,y)
                frame=exchange(b"\x1b[2J\x1b[Hgpy")
                descenders=((0,0,0x7e,0xc6,0xc6,0x7e,0x06,0x7c),
                            (0,0,0xfc,0xc6,0xc6,0xfc,0xc0,0xc0),
                            (0,0,0xc3,0x66,0x3c,0x18,0x30,0x60))
                for g,rows in enumerate(descenders):
                    for y,row in enumerate(rows):
                        for x in range(8):
                            assert pixel(frame,g*8+x,y)==(0xdddddd if row&(0x80>>x) else 0),(g,x,y)
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
                # Documented RGB4 defaults and the actual 8/16-colour
                # transition, independent of IcyTerm or captured images.
                defaults=(0,0x1111ff,0xdddddd,0xff0000,0x00ff11,0x33aaff,0xffff22,0xcc00ee,
                          0x00bb66,0x00dddd,0x00aaff,0x0077cc,0x0000ff,0x7700ff,0xcc00ee,0xcc0088)
                wire=b"\x1bc\x1b[15;13!\x1b[18;9!A"
                wire+=b''.join(f"\x1b[15;{pen}!\x1b[4;{pen*8};100;{pen*8+7};107!".encode() for pen in range(16))
                frame=exchange(wire)
                assert tuple(frame.palette[512:528])==defaults
                assert (frame.cells[0].foreground,frame.cells[0].background)==(525,521)
                for pen in range(16):
                    for plane in (frame.primary,frame.alternate):
                        assert plane[(100+frame.metadata["pixel_top"])*frame.metadata["plane_width"]+pen*8]==512+pen
                frame=exchange(b"\x1b[12!\x1b[0m\x1b[10;0!\x1b[15;13!\x1b[1;200;100!")
                assert pixel(frame,200,100)==0x7700ff
                frame=exchange(b"\x1b[17;1!\x1b[17;2!")
                for pen in range(16):
                    assert pixel(frame,pen*8,100)==defaults[pen&7]
                exchange(b"\x1bc")
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
                assert pixel(frame,60,20)==0xffffff and pixel(frame,61,20)==0
                send(b"\x1b[16;1;999;888!brush.iff!")
                assert read(21)==b"\x18"*10+b"\x08"*10+b"\r"
                frame=exchange(b"\x1b[7;0;0;62;20;2;1;192;255!")
                assert pixel(frame,62,20)==0xffffff
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
                assert pixel(frame,64,20)==0xffffff
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
                send(b"\x1b[16;4;0;0!unsupported.bin!")
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
                assert (notice.metadata["columns"], notice.metadata["rows"], notice.metadata["cell_height"]) == (80,25,8)
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
            assert f"Graphics=SkyPix (Kick {kick})" in saved and "Encoding=ISO-8859-1" in saved and "ScreenMode=Amiga640x200" in saved
            assert not (root/"fonts"/"amiga").exists()

def run_preferences(executable, reports, encoding, height=8):
    # Independent synthetic native font: one named character has a left-edge
    # stroke, every other byte uses the right-edge fallback stroke.
    code = 0x82 if encoding == "CP437" else 0xe9
    def prepare(root):
        image=bytearray(256)
        def word(at,n):struct.pack_into(">H",image,at,n)
        def long(at,n):struct.pack_into(">I",image,at,n)
        for at,n in ((0,1011),(8,1),(20,55),(24,1001),(28,55),(252,1010)):long(at,n)
        for at,n in ((18,0x0f80),(78,8),(82,8),(84,7),(96,2),(190,8),(192,8),(194,8)):word(32+at,n)
        image[32+90:32+92]=bytes((code,code))
        long(32+92,160);long(32+98,188)
        image[32+160:32+176]=bytes((0x80,0x01))*8
        package=root/"fonts"/"amiga"/"bytetest.font"
        (package/"ByteTest").mkdir(parents=True)
        (package/"ByteTest"/"8").write_bytes(image)
        descriptor=bytearray(264)
        struct.pack_into(">HH",descriptor,0,0x0f00,1)
        descriptor[4:15]=b"ByteTest/8\0"
        struct.pack_into(">H",descriptor,260,8)
        (package/"family.font").write_bytes(descriptor)
        (package/"name").write_text("ByteTest.font")
    with socket.socket() as listener:
        listener.bind(("127.0.0.1",0));listener.listen(1);listener.settimeout(15)
        family="ByteTest.font" if height==8 else "Ruby.font"
        entries=("[Preferences]\nConnectionType=Raw\nAddress=127.0.0.1\n"
                 f"Port={listener.getsockname()[1]}\nGraphics=SkyPix (Kick 3.1)\n"
                 f"Encoding={encoding}\nAmigaFont={family}\nAmigaFontSize={height}\n"
                 "ScreenMode=132x25\nNoStatus=false\n")
        with fixture(executable,entries=entries,prepare=prepare,settings="AudioModes=0\n",reports=reports) as (client,root):
            client.wait_text("Preferences");client.key("enter")
            with listener.accept()[0] as peer:
                peer.settimeout(10)
                client.wait_for(lambda s: s.application.get("connected") == 1)
                def read(n):
                    data=b""
                    while len(data)<n:
                        part=peer.recv(n-len(data));assert part;data+=part
                    return data
                def frame(data):
                    peer.sendall(data+b"\x1b[5n")
                    assert read(4)==b"\x1b[0n"
                    return client.capture()
                data=b"\x1b[2J\x1b[H"+(b"\x82" if encoding=="CP437" else "é".encode())
                first=frame(data)
                expected_width=8
                ui_height=16
                assert first.metadata["cell_width"]==expected_width and first.metadata["cell_height"]==ui_height,first.metadata
                assert 0 <= first.metadata["cursor_start"] <= first.metadata["cursor_end"] < first.metadata["cursor_range"],first.metadata
                assert first.metadata["columns"]==(1056+expected_width-1)//expected_width
                assert first.metadata["rows"]==25
                assert first.application["cterm.encoding"]==(1 if encoding=="CP437" else 0)
                if encoding=="CP437": assert first.application["cterm.codepage"]==0
                if height==8:
                    stride=first.metadata["plane_width"];top=first.metadata["pixel_top"]
                    for y in range(8):
                        assert first.primary[(top+y)*stride]==514,(encoding,height,y,[hex(v) for v in first.primary[(top+y)*stride:(top+y)*stride+8]])
                        assert first.primary[(top+y)*stride+7]==512
                if height==12:
                    # Literal Ruby/12 e-acute strike rows, including its first
                    # accent row. A baseline offset must not shift this down.
                    expected=(0x0e,0x18,0,0x7e,0xc3,0xff,0xc0,0xc1,0x7e,0,0,0)
                    for y,row in enumerate(expected):
                        for x in range(8):
                            index=(first.metadata["pixel_top"]+y)*first.metadata["plane_width"]+x
                            assert (first.primary[index]!=512)==bool(row&(0x80>>x)),(x,y)
                # The mode stays 1056x400; its 16-pixel status row starts
                # at Y=384. Check the final drawable pixel and brush copy.
                assert (first.metadata["plane_width"],first.metadata["plane_height"]) == (1056,400)
                edge=frame(b"\x1b[15;3!\x1b[1;1055;383!\x1b[6;1055;382;1;1!\x1b[7;0;0;1054;383;1;1;192;255!")
                stride=edge.metadata["plane_width"];top=edge.metadata["pixel_top"]
                assert edge.primary[(top+383)*stride+1055]==515,(top,stride,edge.metadata,[hex(v) for v in edge.primary[(top+383)*stride+1050:(top+383)*stride+1056]])
                assert edge.primary[(top+383)*stride+1054]==515
                # Remote changes and reset return to the configured native
                # font, not Pearl. Compare independent synthetic glyph rows.
                reset=frame(b"\x1b[10;8!Topaz.font!\x1b[10;0!\x1b[2J\x1b[H"+(b"\x82" if encoding=="CP437" else "é".encode()))
                if height==8:
                    for y in range(8):
                        assert reset.primary[(top+y)*stride]==515
                        assert reset.primary[(top+y)*stride+7]==512
                for y in range(height):
                    for x in range(expected_width):
                        index=(top+y)*stride+x
                        assert (reset.primary[index]!=512)==(first.primary[index]!=512),(encoding,height,x,y)
                assert reset.metadata["cell_width"]==expected_width
                # System UI must never use the synthetic/native connection
                # font: verify status, online menu and transfer protocol picker.
                shown=client.wait_for(lambda s: "Preferences" in s.lines[-1])
                status_x=shown.lines[-1].index("Preferences")
                system_font=shown.cells[(shown.metadata["rows"]-1)*shown.metadata["columns"]+status_x].font
                assert system_font != shown.cells[0].font
                assert shown.cells[(shown.metadata["rows"]-1)*shown.metadata["columns"]+status_x].glyph == ord("P")
                client.command_key(44)
                menu=client.wait_text("SyncTERM Online Menu")
                row=next(y for y,line in enumerate(menu.lines) if "ANSI Music Control" in line)
                column=menu.lines[row].index("ANSI Music Control")
                cell=menu.cells[row*menu.metadata["columns"]+column]
                assert cell.font==system_font and cell.glyph==ord("A")
                # Literal CP437 English 8x16 A, independent of Amiga metrics.
                expected=(0,0,0x10,0x38,0x6c,0xc6,0xc6,0xfe,0xc6,0xc6,0xc6,0xc6,0,0,0,0)
                background=(cell.background if cell.background&0x80000000 else menu.palette[cell.background])&0xffffff
                for y in range(ui_height):
                    for x in range(expected_width):
                        index=(menu.metadata["pixel_top"]+row*ui_height+y)*menu.metadata["plane_width"]+column*expected_width+x
                        colour=menu.primary[index]
                        colour=(colour if colour&0x80000000 else menu.palette[colour])&0xffffff
                        assert (colour!=background)==bool(expected[y]&(0x80>>(x*8//expected_width))),(encoding,height,x,y)
                click_text(client,menu,"Download")
                dialog=client.wait_text("Download Protocol")
                row=next(y for y,line in enumerate(dialog.lines) if "Download Protocol" in line)
                column=dialog.lines[row].index("Download Protocol")
                cell=dialog.cells[row*dialog.metadata["columns"]+column]
                assert cell.font==system_font and cell.glyph==ord("D")
                client.key("escape")
                restored=frame(b"")
                assert "Download Protocol" not in restored.text
                assert restored.metadata["cell_width"]==expected_width
                peer.shutdown(socket.SHUT_RDWR)

def run_diamond(executable, reports, kick, encoding, hide_status, canvas_height=200):
    with socket.socket() as listener:
        listener.bind(("127.0.0.1",0));listener.listen(1);listener.settimeout(15)
        entries=("[Diamond Cursor]\nConnectionType=Raw\nAddress=127.0.0.1\n"
                 f"Port={listener.getsockname()[1]}\nGraphics=SkyPix (Kick {kick})\n"
                 f"Encoding={encoding}\nAmigaFont=Diamond.font\nAmigaFontSize=20\n"
                 f"ScreenMode=Amiga640x{canvas_height}\nNoStatus={str(hide_status).lower()}\nHidePopups=false\n")
        def prepare(root):
            write_script(root,"zzz_diamond_cursor",'''import "syncterm" for CustomCursor, Screen, Hook, Key, Conn
Hook.onKey(Key.f8) {|event|
  CustomCursor.preserve(Fn.new { Screen.checked_(Screen.cursor.setVisible(false)) })
  Conn.sendBytes("CURSOR-RESTORED")
  return true
}
''',"connected")
        with fixture(executable,entries=entries,settings="AudioModes=0\nScrollBackLines=100\n",
                     prepare=prepare,reports=reports,quiet=False) as (client,root):
            original=client.wait_text("Diamond Cursor");client.key("enter")
            with listener.accept()[0] as peer:
                peer.settimeout(10)
                client.wait_for(lambda s:s.application.get("connected")==1)
                def frame(data):
                    peer.sendall(data+b"\x1b[5n")
                    reply=b""
                    while len(reply)<4:
                        part=peer.recv(4-len(reply));assert part;reply+=part
                    assert reply==b"\x1b[0n",reply
                    return client.capture()
                def position(shown,column,row):
                    # Logical cell position and fixed mode geometry remain
                    # observable while this raster fixture gates the cursor.
                    m=shown.metadata
                    assert (m["cell_width"],m["cell_height"],m["columns"],m["rows"])==(8,8,80,canvas_height//8),m
                    assert (m["plane_width"],m["plane_height"])==(640,canvas_height),m
                    assert abs(m["destination_width"]*3-m["destination_height"]*4)<=4,m
                    assert m["pixel_cursor_enabled"]==0,m
                    assert (m["cursor_x"],m["cursor_y"])==(column,row),m
                def wrapped_narrow(shown):
                    position(shown,16,0)
                    # Literal Diamond/20 'i' strike, extracted from the native
                    # file (SHA-256 96117752c5be1e3bf80921b88937c0374d658970a5d5ce08517f2f9a6c8e1356).
                    # Width 3, advance 4, baseline 14: all 33 glyphs fit
                    # on row one using actual advances.
                    rows=(7,7,0,0,0,0,7,7,7,7,7,7,7,7,0,0,0,0,0,0)
                    m=shown.metadata
                    for y in range(40):
                        for x in range(132):
                            count=33 if y<20 else 0
                            ink=x//4<count and x%4<3 and bool(rows[y%20]&(4>>(x%4)))
                            index=(y+m["pixel_top"])*m["plane_width"]+x
                            for plane in (shown.primary,shown.alternate):
                                value=plane[index]
                                value=(value if value&0x80000000 else shown.palette[value])&0xffffff
                                assert value==(0xdddddd if ink else 0),(kick,encoding,x,y,value)
                def backspace_space(shown, transparent):
                    position(shown,1,0)
                    # Literal W strike from the same native file. BS moves
                    # twenty pixels; the blank space advances nine. JAM1
                    # preserves it; our JAM2 background spans the full strike
                    # height, rather than only one eight-pixel console row.
                    rows=(460551,)*12+(462606,522744,0,0,0,0,0,0)
                    m=shown.metadata
                    for y,bits in enumerate(rows):
                        for x in range(20):
                            ink=x<19 and bool(bits&(1<<(18-x))) and (transparent or x>=9)
                            index=(y+m["pixel_top"])*m["plane_width"]+x
                            for plane in (shown.primary,shown.alternate):
                                value=plane[index]
                                value=(value if value&0x80000000 else shown.palette[value])&0xffffff
                                assert value==(0xdddddd if ink else 0),(kick,encoding,transparent,x,y,value)
                def tcsh_erase(shown):
                    position(shown,0,0)
                    m=shown.metadata
                    # A clean tcsh PTY with TERM=ansi emits BS CSI K for
                    # deletion. JR-Comm H17:1ce0 uses the whole font height.
                    for y in range(20):
                        for x in range(20):
                            index=(y+m["pixel_top"])*m["plane_width"]+x
                            for plane in (shown.primary,shown.alternate):
                                value=plane[index]
                                value=(value if value&0x80000000 else shown.palette[value])&0xffffff
                                assert value==0,(kick,encoding,x,y,value)
                # Literal JR-Comm position-probe expectations: baseline 14,
                # i advances four pixels and W advances twenty. Exact pixel
                # cursor geometry is independently checked by the cursor fixture.
                position(frame(b"\x1b[?25l\x1b[2J\x1b[H"),0,0)
                position(frame(b"i"),0,0)
                position(frame(b"W"),3,0)
                client.key("f8")
                reply=b""
                while len(reply)<15:
                    part=peer.recv(15-len(reply));assert part;reply+=part
                assert reply==b"CURSOR-RESTORED",reply
                position(client.capture(),3,0)
                position(frame(b"\n"),3,2)
                position(frame(b"\r"),0,2)
                backspace_space(frame(b"\fW\b "),False)
                tcsh_erase(frame(b"\fW\b\x1b[K"))
                position(frame(b"\x1b[19;101;40!"),12,5)
                position(frame(b"\x1b[10;8!Pearl.font!"),12,5)
                position(frame(b"\x1b[10;20!Diamond.font!\f"),0,0)
                backspace_space(frame(b"W\b "),True)
                tcsh_erase(frame(b"\fW\b\x1b[K"))
                wrapped_narrow(frame(b"\f"+b"i"*33))
                position(frame(b"\f"+b"W"*40),20,2)
                position(frame(b"\f\x1b[?7l"+b"W"*33),79,0)
                # Query flushes and network splits do not alter wrapping.
                position(frame(b"\x1b[?7h\f"+b"i"*32),16,0)
                position(frame(b"i"),16,0)
                # RIS restores the configured Diamond default, so this also
                # exercises the initial/reset font rather than command 10.
                wrapped_narrow(frame(b"\x1bc\x1b[?25l\f"+b"i"*33))
                position(frame(b"\f"),0,0)
                client.command_key(44)
                menu=client.wait_text("SyncTERM Online Menu")
                assert menu.metadata["cell_width"]==8 and menu.metadata["cell_height"]==8,menu.metadata
                row=next(y for y,line in enumerate(menu.lines) if "ANSI Music Control" in line)
                column=menu.lines[row].index("ANSI Music Control")
                cell=menu.cells[row*80+column]
                expected=(0x30,0x78,0xcc,0xcc,0xfc,0xcc,0xcc,0)
                bg=(cell.background if cell.background&0x80000000 else menu.palette[cell.background])&0xffffff
                for y,bits in enumerate(expected):
                    for x in range(8):
                        value=menu.primary[(row*8+y)*menu.metadata["plane_width"]+column*8+x]
                        value=(value if value&0x80000000 else menu.palette[value])&0xffffff
                        assert (value!=bg)==bool(bits&(0x80>>x)),(kick,encoding,x,y)
                assert menu.metadata["pixel_cursor_enabled"]==0
                client.key("escape")
                position(frame(b""),0,0)
                if canvas_height==400:
                    # Literal canvas boundaries: two red pixels on each of
                    # the last two rows, with off-canvas points clipped.
                    bottom=canvas_height-(0 if hide_status else 8)
                    shown=frame(f"\f\x1b[15;3!\x1b[4;638;{bottom-2};641;{bottom+1}!\x1b[1;10;{bottom}!\x1b[1;640;10!".encode())
                    m=shown.metadata
                    for y in (10,198,199,200,bottom-3,bottom-2,bottom-1):
                        for x in (10,79,77):
                            for plane in (shown.primary,shown.alternate):
                                value=plane[(y+m["pixel_top"])*m["plane_width"]+x]
                                value=(value if value&0x80000000 else shown.palette[value])&0xffffff
                                assert value==(0xff0000 if x>=638 and y>=bottom-2 else 0),(kick,encoding,hide_status,x,y,value)
                    # Diamond i keeps its literal 20-pixel strike below the
                    # 200-line boundary. Command 19 puts baseline at Y+19;
                    # the native baseline 14 puts the strike's top at Y+5.
                    shown=frame(b"\x1b[15;2!\x1b[19;100;240!i")
                    position(shown,13,30)
                    rows=(7,7,0,0,0,0,7,7,7,7,7,7,7,7,0,0,0,0,0)
                    for y,bits in enumerate(rows):
                        for x in range(4):
                            for plane in (shown.primary,shown.alternate):
                                value=plane[(245+y+shown.metadata["pixel_top"])*shown.metadata["plane_width"]+100+x]
                                value=(value if value&0x80000000 else shown.palette[value])&0xffffff
                                assert value==(0xdddddd if x<3 and bits&(4>>x) else 0),(kick,encoding,x,y,value)
                    position(frame(b"\x1bc"),0,0)
                peer.shutdown(socket.SHUT_RDWR)
                notice=client.wait_text("Disconnected: Enter exits, F1 details")
                assert notice.metadata["cell_height"]==8
                client.command_key(48);client.wait_text("Scrollback")
                client.key("escape");client.wait_text("Disconnected: Enter exits, F1 details")
                client.key("enter");restored=client.wait_text("Directory: Name")
                for key in ("columns","rows","cell_width","cell_height","plane_width","plane_height"):
                    assert restored.metadata[key]==original.metadata[key],key
                assert b"[wren" not in client.stderr,bytes(client.stderr)

def run_mode_geometry(executable, reports, kick, hide_status, selected):
    # Independent mode-table expectations, including CURRENT and CUSTOM.
    custom=selected=="Custom"
    columns,rows,cw,ch,width,height,aw,ah=(91,27,8,8,728,216,5,4) if custom else (80,28,8,14,640,392,4,3)
    if selected=="80x43":
        columns,rows,cw,ch,width,height,aw,ah=80,43,8,8,640,350,4,3
    bottom=rows*ch-(0 if hide_status else ch)
    with socket.socket() as listener:
        listener.bind(("127.0.0.1",0));listener.listen(1);listener.settimeout(15)
        entries=("[Mode geometry]\nConnectionType=Raw\nAddress=127.0.0.1\n"
                 f"Port={listener.getsockname()[1]}\nGraphics=SkyPix (Kick {kick})\n"
                 f"ScreenMode={selected}\nNoStatus={str(hide_status).lower()}\n"
                 "AmigaFont=Diamond.font\nAmigaFontSize=20\n")
        settings=("AudioModes=0\nScreenMode=80x28\nCustomColumns=91\nCustomRows=27\n"
                  "CustomFontHeight=8\nCustomAspectWidth=5\nCustomAspectHeight=4\n")
        with fixture(executable,entries=entries,settings=settings,reports=reports) as (client,root):
            original=client.wait_text("Mode geometry");client.key("enter")
            with listener.accept()[0] as peer:
                peer.settimeout(10)
                client.wait_for(lambda s:s.application.get("connected")==1)
                def frame(data):
                    peer.sendall(data+b"\x1b[5n")
                    reply=b""
                    while len(reply)<4:
                        part=peer.recv(4-len(reply));assert part;reply+=part
                    assert reply==b"\x1b[0n",reply
                    shown=client.capture();m=shown.metadata
                    assert (m["columns"],m["rows"],m["cell_width"],m["cell_height"])==(columns,rows,cw,ch),m
                    assert (m["plane_width"],m["plane_height"])==(width,height),m
                    assert abs(m["destination_width"]*ah-m["destination_height"]*aw)<=aw,m
                    return shown
                shown=frame(f"\f\x1b[15;3!\x1b[4;{width-2};{bottom-2};{width+1};{bottom+1}!\x1b[1;10;{bottom}!".encode())
                for plane in (shown.primary,shown.alternate):
                    top=shown.metadata["pixel_top"]
                    for y in (bottom-3,bottom-2,bottom-1):
                        for x in (10,width-3,width-2,width-1):
                            assert plane[(top+y)*width+x]==(515 if y>=bottom-2 and x>=width-2 else 512),(selected,hide_status,x,y)
                    if not hide_status:
                        assert all(not 512<=value<528 for value in plane[(top+bottom)*width:(top+height)*width]),selected
                frame(b"\x1bc")
                peer.shutdown(socket.SHUT_RDWR)
            restored=client.wait_text("Directory: Name")
            for key in ("columns","rows","cell_width","cell_height","plane_width","plane_height"):
                assert restored.metadata[key]==original.metadata[key],(selected,key)
            assert b"[wren" not in client.stderr,bytes(client.stderr)


def run_editor_preferences(executable,reports):
    entries="[Editor]\nConnectionType=Raw\nAddress=example.invalid\nGraphics=SkyPix (Kick 3.1)\n"
    with fixture(executable,entries=entries,settings="AudioModes=0\n",reports=reports) as (client,root):
        client.wait_text("Editor");client.key("f2")
        screen=client.wait_text("Pearl.font 8")
        click_text(client,screen,"Font")
        client.wait_text("Amiga Font")
        client.text("f",2)
        client.wait_text("Find");client.text("Ruby.font 12");client.key("enter");client.key("enter")
        screen=client.wait_text("Ruby.font 12")
        click_text(client,screen,"Encoding")
        client.wait_text("Enter Select");client.key("end");client.wait_text("UTF-8");client.key("enter")
        screen=client.wait_text("Ruby.font 12")
        click_text(client,screen,"Screen Mode")
        client.wait_text("Enter Select");client.key("end");client.wait_text("Amiga 640x400");client.key("enter")
        client.wait_text("Ruby.font 12");client.key("escape")
        client.wait_text("Directory: Name")
        saved=(root/"syncterm.lst").read_text()
        assert "AmigaFont=Ruby.font" in saved and "AmigaFontSize=12" in saved,saved
        assert "Encoding=UTF-8" in saved and "ScreenMode=Amiga640x400" in saved,saved

if __name__=="__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("executable")
    parser.add_argument("--reports",default="reports/test-control")
    args=parser.parse_args()
    for kick in ("1.3","3.1"):
        for hide in (False,True): run(args.executable,args.reports,hide,kick)

    run_preferences(args.executable,args.reports,"CP437")
    run_preferences(args.executable,args.reports,"UTF-8")
    run_preferences(args.executable,args.reports,"UTF-8",12)
    for kick in ("1.3","3.1"):
        for encoding in ("CP437","UTF-8"):
            for hide in (False,True):
                for height in (200,400):
                    run_diamond(args.executable,args.reports,kick,encoding,hide,height)
    for kick in ("1.3","3.1"):
        for hide in (False,True):
            for mode in ("80x28","80x43","Current","Custom"):
                run_mode_geometry(args.executable,args.reports,kick,hide,mode)
    run_editor_preferences(args.executable,args.reports)
