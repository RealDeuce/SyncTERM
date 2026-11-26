#!/usr/bin/env python3
"""Plain 1U buttons through TCP: rendering, click/reset and shared capacity.

Payloads and half-open hit geometry follow PR117. Pixel expectations are
independent geometry/color checks, not RIPtel rasters.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path
from st_migrated_control_test import connection
from st_rip3_button_styles_connected_test import number
from st_rip3_mouse_connected_test import field
from st_rip3_connected_test import query,pixel
from st_terminal_conformance_test import Terminal


def style(flags=256,width=0,height=0,flags2=0,surface=7,orientation=2,bevel=3,underline=15,foreground=15,group=0):
    values=(width,height,orientation,flags,bevel,foreground,0,15,8,surface,group,flags2,underline,7,0,0)
    widths=(2,2,2,4,2,2,2,2,2,2,2,2,2,2,1,5)
    return b"\x01|1B"+b"".join(number(v,w) for v,w in zip(values,widths))+b"\r\n"


def button(area,payload=b"<><>",hotkey=0,flags=0,radix=36,xy=2):
    return (b"\x01|1U"+b"".join(number(v,xy,radix) for v in area)
            +number(hotkey,2,radix)+number(flags,1,radix)+b"0"+payload+b"\r\n")


def remove_fields(area,flags,*,flags_width=4):
    return b"\x01|1k"+b"".join(number(v,2) for v in area)+number(flags,flags_width)+b"\r\n"


def run(args,width,height):
    with connection(args.executable,args.reports,encoding=args.encoding,
                    options="ScreenMode=Custom\nRIP=RIPv3\n",
                    settings=f"CustomColumns={width//8}\nCustomRows={height//8}\nCustomFontHeight=8\n"
                             "CustomAspectWidth=4\nCustomAspectHeight=3\n") as (client,root,peer,send):
        t=Terminal(client,root,peer,send)
        def values(program,expected=b""):
            return t.exchange(query(program,at_cursor=True),expected)
        t.exchange(b"\x1b[!",b"RIPSCRIP030001")
        t.exchange(b"\x01|*\r\n\x1b[?25l")
        frame=values(f"$WORLD(CUR,{width},{height})$".encode())
        m=frame.metadata
        def mouse(x,y,left=False,move=False):
            px=m["destination_x"]+(2*x+1)*m["destination_width"]//(2*m["plane_width"])
            py=m["destination_y"]+(2*y+1)*m["destination_height"]//(2*m["plane_height"])
            before=client.capture().metadata["events_dequeued"]
            client.mouse(px,py,action=0 if move else 1,buttons=int(left))
            return client.wait_for(lambda s:s.metadata["events_dequeued"]>before)
        def click(x,y,expected=b""):
            mouse(x,y,True);mouse(x,y)
            return t.input(expected) if expected else t.exchange()

        if width==640:
            t.case("rip3-combined-button-effects-in-device-coordinates")
            values(b"$WORLD(CUR,1280,480)$")
            # X scales by one half, Y by one. A six-unit bevel is three
            # pixels on all sides. The 40-pixel face selects chisel inset 6.
            frame = t.exchange(style(flags=256|512|16|8|32768|32|1024, bevel=6,
                                     surface=3, foreground=12)
                               + button((160,100,320,140), b"<>H<>hit"))
            assert [pixel(frame,x,120) for x in range(74,88)] == [
                0,0x555555,0,0xffffff,0xffffff,0xffffff,0x00aaaa,0x555555,
                0x00aaaa,0x00aaaa,0x00aaaa,0x00aaaa,0x555555,0x00aaaa]
            assert [pixel(frame,105,y) for y in range(135,145)] == [
                0x00aaaa,0x00aaaa,0x00aaaa,0xffffff,0x00aaaa,
                0x555555,0x555555,0x555555,0,0xffffff]
            ink = {(x,y) for x in range(88,152) for y in range(108,132)
                   if pixel(frame,x,y)==0xff5555}
            shadow = {(x,y) for x in range(88,152) for y in range(108,132)
                      if pixel(frame,x,y)==0}
            assert ink and shadow == {(x+1,y+1) for x,y in ink} - ink
            click(76,120)
            click(77,120,b"hit")
            values(b"$MKILL$$WORLD(CUR,640,480)$")

            t.case("rip3-selected-font-hotkey-descenders")
            for font in (b"Y01000309", b"y00000W0M000000001a1a000000Dixon"):
                # Four copies of Hgg separate the highlight from its underline
                # on the first matching glyph. Each face has identical layout.
                commands = bytearray(b"\x01|"+font+b"\r\n")
                for column, key in enumerate(b"HHGG"):
                    x = 5+155*column
                    commands += style(flags=256|1024|(2048 if column%2 else 0),
                                      flags2=2, surface=3, foreground=12, underline=14)
                    commands += button((x,180,x+150,260), b"<>Hgg<>", hotkey=key)
                frame = t.exchange(bytes(commands))
                def marks(column, color):
                    return {(x,y) for x in range(150) for y in range(180,260)
                            if pixel(frame,5+155*column+x,y)==color}
                h, g = marks(0,0xffff55), marks(2,0xffff55)
                assert h and g and max(x for x,y in h) < min(x for x,y in g)
                assert any(x>max(g)[0] for x,y in marks(2,0xff5555))
                rows = []
                for column, glyph in ((1,h),(3,g)):
                    underline = marks(column,0xffff55) - glyph
                    assert underline and len({y for x,y in underline})==1
                    rows.append(next(iter(underline))[1])
                assert rows[1]>rows[0], (font,rows)
                values(b"$MKILL$")
            t.exchange(b"\x01|*\r\n")
            values(b"$WORLD(CUR,640,480)$")

            t.case("rip3-computed-field-playback-and-continued-button-slideshow")
            shared=root/"RIP"
            shared.mkdir(exist_ok=True)
            (shared/"Cycle.FN").write_bytes(query(
                b'<<IF $INUSE(TV,NEXT_IMG)$="0">>$-=NEXT_IMG=ASTRO.JPG$<<ENDIF>>$>Show.FN$',
                at_cursor=True))
            (shared/"Show.FN").write_bytes(query(b'$-=SHOWN=<<NEXT_IMG>>$',at_cursor=True))
            values(b'$-=RETURN=>Cycle.FN$$-=NO_WIPES=NONE$$-=TGMENU_WIPES=1$')
            back=(b'<<IF $TGMENU_WIPES$="1" AND ($NO_WIPES$="" OR $NO_WIPES$="NONE")>>'
                  b'<<IF $RETURN$!="">>$<<RETURN>>$<<ENDIF>><<ELSE>>$NULL$<<ENDIF>>')
            t.exchange(field((10,10,60,30),b'ID=1:'+back))
            advance=(b'<<IF $NEXT_IMG$="ASTRO.JPG">>$-=NEXT_IMG=JUPITER.JPG$'
                     b'<<ELSEIF $NEXT_IMG$="JUPITER.JPG">>$-=NEXT_IMG=BEACH2.JPG$'
                     b'<<ELSE>>$-=NEXT_IMG=ASTRO.JPG$<<ENDIF>>$>Cycle.FN$')
            t.exchange(style(flags=256|1024)+button((10,40,100,65),
                b'$CUR(STYLE,5)$<>$COORDSIZE(CUR,3)$Next Image<>'+advance.replace(
                b'<<ELSE',b'\\\r\n<<ELSE')))
            values(b'$CUR(STYLE)$:$COORDSIZE$:$INUSE(TV,NEXT_IMG)$:$INUSE(TV,SHOWN)$',b'5:3:0:0')
            values(b'$CUR(STYLE,0)$$COORDSIZE(CUR,2)$')
            click(20,20)
            values(b'$&NEXT_IMG$:$&SHOWN$',b'ASTRO.JPG:ASTRO.JPG')
            for image in (b'JUPITER.JPG',b'BEACH2.JPG',b'ASTRO.JPG'):
                click(20,50)
                values(b'$&NEXT_IMG$:$&SHOWN$',image+b':'+image)
            values(b'$RESET(TV,NO_WIPES)$$INUSE(TV,NO_WIPES)$$MKILL$',b'0')
            t.exchange(b'\x01|E\r\n')

            t.case("rip3-auto-stamp-label-cancellation-and-hard-abort")
            for reference,visible in ((b'$PROMPT_LABEL$',True),(b'$&PROMPT_LABEL$',False)):
                send(style(flags=256|64|1024,surface=3,foreground=12)
                     +button((20,40,120,80),b'<>'+reference+b'<>clicked'))
                client.wait_text('PROMPT_LABEL')
                client.key('escape')
                frame=values(b'$INUSE(MOUSE,ANY)$:$INUSE(TV,PROMPT_LABEL)$',b'1:0')
                assert any(pixel(frame,x,y)==0xff5555 for y in range(40,80)
                           for x in range(20,120))==visible
                # The label can prompt again when the field is redrawn.
                # Give it an existing value so the click can complete.
                values(b'$-=PROMPT_LABEL=ready$')
                click(60,60,b'clicked')
                values(b'$MKILL$$RESET(TV,PROMPT_LABEL)$')
            frame=t.exchange(style(flags=256|64|1024,surface=3)+button(
                (20,40,120,80),b'<>bad<<MKILL>><>$-=ADMITTED=yes$')+
                query(b'$INUSE(MOUSE,ANY)$:$INUSE(TV,ADMITTED)$',at_cursor=True),b'0:0')
            assert pixel(frame,60,60)==0x00aaaa
            # Capture and style conversion preceded the failed label.
            frame=t.exchange(button((160,40,0,0)))
            assert pixel(frame,259,79)==0x00aaaa
            t.exchange(b'\x01|*\r\n')
            values(b'$WORLD(CUR,640,480)$')

        t.case(f"rip3-plain-button-face-{width}x{height}")
        before=t.exchange(style(surface=3)+button((10,20,60,40)))
        assert all(pixel(before,x,y)==0x00aaaa for x,y in ((10,20),(59,39)))
        assert pixel(before,9,20)==0 and pixel(before,60,40)==0
        t.exchange(style(flags=256|512|16)+button((10,20,60,40)))
        before=t.exchange()
        assert pixel(before,10,17)==0xffffff and pixel(before,7,20)==0xffffff
        assert pixel(before,62,20)==0x555555 and pixel(before,10,42)==0x555555
        assert pixel(before,6,30)==0 and pixel(before,5,30)==0x555555

        t.case(f"rip3-button-label-state-{width}x{height}")
        accented=b"\x82Classic" if args.encoding=="CP437" else "éClassic".encode()
        t.exchange(b"\x01|m2S2S\r\n"+style()+button((200,100,400,160),b"<>"+accented+b"<>"))
        values(b"$TEXTXY$",b"100:100")
        # No ordinary outline operation precedes this label. Creating it must
        # not warm the ordinary cursor-width cache or move any PORT cursor.
        frame=t.exchange(b"\x01|y00000W0M000000001a1a000000Dixon\r\n"
                         +button((200,180,400,240),b"<>Dixon<>"))
        assert any(pixel(frame,x,y)==0xffffff for y in range(180,240) for x in range(200,400))
        values(b"$TEXTXY$",b"100:100")
        t.exchange(b"\x01|Y00000100|@5K5KF\r\n")
        values(b"$TEXTXY$",b"200:200")

        if (width, height) == (1024, 768):
            t.case("classic DEFAULT EX button measurement and hotkey")
            # ACC.RIP's fixed 268x28 face with its first matching N decorated.
            # Q=22, P=109 at 72 DPI gives 15+1 pixels per byte. The centered
            # 272-pixel run starts at x=30, baseline y=198. The first match
            # for N is the lowercase n in Phone, after eight bytes.
            settings = style(flags=256|1024|2048|16384, width=268, height=28,
                             flags2=2, underline=14, surface=3, group=1)
            definition = button((32,176,0,0), b"<>Home Phone Number<>font-zero", hotkey=ord("N"))
            frame = t.exchange(b"\x01|Y00000209\r\n" + settings + definition)
            span = {x for x in range(24, 370) if pixel(frame,x,201) == 0xffff55}
            assert span == set(range(158, 173)), sorted(span)
            assert any(pixel(frame,x,y) == 0xffff55 for y in range(182,198) for x in range(158,173))
            white = {x for y in range(175,204) for x in range(24,370)
                     if pixel(frame,x,y) == 0xffffff}
            assert white and min(white) >= 30 and max(white) <= 302
            # The button controls alignment and decoration, even when the
            # captured Y selects vertical graphics text and ordinary shadow.
            before = t.exchange(b"\x01|1K|Y0001023T\r\n" + settings + definition)
            assert {x for x in range(24,370) if pixel(before,x,201) == 0xffff55} == span
            # A radio activation redraws the label using its original size
            # and creation WORLD after the current STYLE and WORLD change.
            values(b"$WORLD(CUR,512,384)$")
            t.exchange(b"\x01|Y01000309\r\n")
            after = click(124,189,b"font-zero")
            assert after.primary == before.primary and after.alternate == before.alternate

            # At scale 2, an eight-WORLD-unit right margin adds 16 pixels.
            # The 40x16 WORLD face projects to 80x32. Size 1 at 144 DPI
            # has ascent 16 and unspaced advance 15.
            frame = t.exchange(b"\x01|Y00000100\r\n"
                               + style(flags=256|1024|2048, width=40, height=16,
                                       orientation=3, underline=14)
                               + button((32,150,0,0), b"<>ABA<>", hotkey=ord("A")))
            assert {x for x in range(140,230) if pixel(frame,x,327) == 0xffff55} == set(range(160,175))
            values(b"$WORLD(CUR,1024,768)$")

            # With g in the label and descender fit enabled, 16+5 pixels
            # center in the 40-pixel face at top 259 and baseline 275.
            frame = t.exchange(b"\x01|Y00000209\r\n"
                               + style(flags=256|1024|2048|8192, width=80, height=40, underline=14)
                               + button((430,250,0,0), b"<>Ag<>", hotkey=ord("A")))
            assert {x for x in range(430,510) if pixel(frame,x,278) == 0xffff55} == set(range(454,469))
            t.exchange(b"\x01|1K|Y00000100\r\n")

            t.case("classic DEFAULT EX button justification and chisel margins")
            # A 40x40 WORLD face becomes 80x40 DEVICE. The size-1 A has
            # advance 15 plus one spacing pixel at 144 DPI horizontally,
            # with an 8-pixel baseline height at 72 DPI vertically.
            values(b"$WORLD(CUR,512,768)$")
            variants = ((0, 8, 20), (0, 16, 46), (0, 24, 20),
                        (8, 8, 40), (8, 16, 26), (8, 24, 40))
            commands = []
            underlines = []
            for orientation, dy in enumerate((-2, 27, 27, 27, 56)):
                y = 330 + 75*orientation
                for column, (chisel, flags, dx) in enumerate(variants):
                    x = 64 + 160*column
                    commands.append(style(flags=256|1024|2048|chisel, flags2=flags,
                                          width=40, height=40, orientation=orientation, underline=14)
                                    + button((x//2, y, 0, 0), b"<>A<>", hotkey=ord("A")))
                    if orientation == 1:
                        dx = -32  # label advance and eight WORLD units left of the face
                    elif orientation == 3:
                        dx = 96  # eight WORLD units past the far edge
                    underlines.append((x, y + dy, x + dx))
            frame = t.exchange(b"".join(commands))
            for x, y, start in underlines:
                marks = {px for px in range(x-48, x+112) if pixel(frame,px,y) == 0xffff55}
                assert marks == set(range(start, start+15)), (x, y, sorted(marks))
            values(b"$MKILL$$WORLD(CUR,1024,768)$")

        t.case(f"rip3-button-pointer-and-host-{width}x{height}")
        # Preflight must not execute MKILL while creating a button.
        t.exchange(field((150,20,180,50),b"old")+style(flags=256|512|16|1024|2|2048,flags2=2,underline=14)
                   +button((10,20,60,40),b"ID=7:<>Go<>$MKILL$button:$FIELDID$",hotkey=ord("G")))
        click(63,30);click(6,30)  # exclusive bevel far edge; no near halo
        click(160,30,b"old")
        before=t.exchange()
        assert any(pixel(before,x,y)==0xffff55 for y in range(20,40) for x in range(10,60))
        mouse(20,30,True)
        pressed=t.exchange()
        assert pixel(pressed,20,30)==(pixel(before,20,30)^0xffffff)
        assert pixel(pressed,5,30)==pixel(before,5,30)  # recess excluded
        mouse(200,50,True,move=True)
        outside=t.exchange()
        assert outside.primary==before.primary and outside.alternate==before.alternate
        mouse(20,30,True,move=True);mouse(20,30)
        after=t.input(b"button:7")
        assert after.primary==before.primary and after.alternate==before.alternate
        click(160,30);click(20,30)

        if width == 640:
            t.case('rip3-hot-flag-does-not-enable-icon-loading-on-other-faces')
            # The filename expression is prepared on creation even for PLAIN.
            # An ungrouped button without inversion needs no press redraw.
            source = b'$-=HOTFILE=prepared$NoSuchBitmap<><>plain-hot'
            before = t.exchange(style(flags=256|1024|4096, surface=3) + button((10,70,60,100), source))
            values(b'$&HOTFILE$', b'prepared')
            values(b'$-=HOTFILE=idle$')
            mouse(30,80,True)
            pressed = values(b'$&HOTFILE$', b'idle')
            assert pressed.primary == before.primary and pressed.alternate == before.alternate
            mouse(30,80)
            t.input(b'plain-hot')

            # An invertible button does redraw on press. HOT still supplies
            # no bitmap I/O or icon fallback during that redraw.
            t.exchange(style(flags=256|1024|4096|2, surface=3)
                       + button((70,70,120,100), b'NoSuchBitmap<><>plain-invert'))
            mouse(90,80,True)
            pressed = t.exchange()
            assert pixel(pressed,90,80) == 0xff5555
            mouse(90,80)
            released = t.input(b'plain-invert')
            assert pixel(released,90,80) == 0x00aaaa

            crop = b''.join(number(v,2) for v in (130,70,141,81))
            t.exchange(b'\x01|S0103|B' + crop + b'|1C' + crop + b'0\r\n'
                       + style(flags=1|1024|4096|2)
                       + button((150,70,0,0), b'NoSuchBitmap<><>clipboard-invert'))
            mouse(155,75,True)
            pressed = t.exchange()
            assert pixel(pressed,155,75) == 0xff5555
            mouse(155,75)
            released = t.input(b'clipboard-invert')
            assert pixel(released,155,75) == 0x00aaaa
            values(b'$MKILL$')

            t.case('rip3-ordinary-button-cancellation-restores-custom-palette')
            # This palette has no exact complement for entry 137. Repeating
            # a nearest-color inversion cannot recover the original index.
            t.exchange(b'\x01|d' + number(137,2,64) + b'8' + number(0x103454,4,64) + b'\r\n')
            before = t.exchange(style(flags=256|1024|2, surface=137)
                                + button((200,70,250,100), b'<><>committed'))
            assert pixel(before,220,80) == 0x103454
            mouse(220,80,True)
            pressed = t.exchange()
            assert pixel(pressed,220,80) == 0xefcbab
            mouse(255,80,True,move=True)
            outside = t.exchange()
            assert outside.primary == before.primary and outside.alternate == before.alternate

            # Scene writes and palette updates during a held press become
            # visible in the restored scene, including at the edited pixel.
            mouse(220,80,True,move=True)
            plot = b'X' + number(221,2) + number(81,2)
            frame = t.exchange(b'\x01|d' + number(137,2,64) + b'8' + number(0x443420,4,64)
                               + b'|c02|' + plot + b'\r\n')
            assert pixel(frame,220,80) == 0xbbcbdf and pixel(frame,221,81) == 0xff55ff
            mouse(255,80)
            outside = values(b'$MKILL$alive', b'alive')
            assert pixel(outside,220,80) == 0x443420 and pixel(outside,221,81) == 0x00aa00

            t.case("rip3-checkbox-feedback-on-a-custom-palette")
            frame = t.exchange(style(flags=256|1024|2, flags2=1, group=3, surface=137)
                               + button((200,110,250,140), b"<><>checked"))
            assert pixel(frame,220,120)==0x443420
            mouse(220,120,True)
            assert pixel(t.exchange(),220,120)==0xbbcbdf
            mouse(255,120,True,move=True)
            frame = t.exchange()
            assert pixel(frame,220,120)==0x443420
            mouse(255,120)
            t.exchange()  # Releasing outside sent no command.
            mouse(220,120,True)
            mouse(255,120,True,move=True)
            mouse(220,120,True,move=True)
            mouse(220,120)
            # The closest indexed color to RGB bb,cb,df is the default
            # gray with DAC value 50, which expands to ca,ca,ca.
            selected = 0xcacaca
            assert pixel(t.input(b"checked"),220,120)==selected
            mouse(220,120,True)
            assert pixel(t.exchange(),220,120)==0x443420
            mouse(255,120)
            assert pixel(t.exchange(),220,120)==selected
            assert pixel(click(220,120,b"checked"),220,120)==0x443420
            values(b"$MKILL$")

        t.case(f"rip3-button-reset-before-host-{width}x{height}")
        t.exchange(style(flags=256|1024|4)+button((10,20,60,40),b"ID=9:<><>reset:$FIELDID$:$CUR(BUT)$"))
        frame=click(20,30,b"reset:9:0")
        for plane in (frame.primary,frame.alternate):
            assert all(((word if word&0x80000000 else frame.palette[word])&0xffffff)==0 for word in plane)
        values(f"$WORLD(CUR,{width},{height})$".encode())
        t.exchange(b"\x1b[?25l")

        t.case(f"rip3-button-capacity-and-decorations-{width}x{height}")
        occupancy = b"$INUSE(MOUSE,ANY)$:$INUSE(MOUSE,ALL)$"
        values(occupancy, b"0:0")
        t.exchange(style(flags=256)+button((300,300,330,330)))
        values(occupancy, b"0:0")
        t.exchange(style(flags=256|1024)+button((300,300,330,330),b"<><>single"))
        values(occupancy, b"1:0")
        values(b"$SMF(8)$$MKILL$"+occupancy+b":$INUSE(MOUSE,S8)$", b"0:0:1")
        values(b"$RMF(8)$"+occupancy, b"1:0")
        values(b"$MKILL$")
        values(b"$INUSE(MOUSE,1)$alive", b"$INUSE(MOUSE,1)$alive")
        definitions=[]
        for i in range(128):
            definitions.append(field((100,100,120,120),b"full") if i%2==0
                               else button((100,100,121,121),b"<><>full"))
        t.exchange(b"".join(definitions[:-1]))
        values(occupancy, b"1:0")
        before=t.exchange(button((200,100,221,121),b"<><>last"))
        values(occupancy, b"1:1")
        t.exchange(b"\x01|h01000400\r\n")
        values(occupancy, b"1:1")
        t.exchange(b"\x01|#\r\n")
        after=t.exchange(button((300,300,330,330),b"<><>overflow"))
        assert after.primary==before.primary and after.alternate==before.alternate
        click(110,110,b"full")
        # Nonmouse decorations do not consume any of those 128 records.
        t.exchange(style(surface=4)+button((300,300,330,330))*129)
        frame=t.exchange()
        assert pixel(frame,310,310)==0xaa0000
        t.exchange(remove_fields((199,99,222,122),1))
        values(occupancy, b"1:0")
        values(b"$MKILL$")
        values(occupancy, b"0:0")
        click(110,110)
        before=t.exchange()
        # Creation stores raw HCL without querying providers. A later reached
        # argument error neither drops the field nor terminates the session.
        for rejected in (b'$RAND("bad")$', b'$RAND(bad)$'):
            action = b'$-=BEFORE=1$pre' + rejected + b'post$-=AFTER=1$'
            values(b"$-=BEFORE=0$$-=AFTER=0$")
            after=t.exchange(style(flags=256|1024)+button((400,300,430,330),b'<><>'+action,flags=1)
                             +b"\x01|1Ubad\r\n")
            assert pixel(after,410,310)==0xaaaaaa
            values(b"$&BEFORE$:$&AFTER$",b"0:0")
            click(410,310,b"pre"+rejected+b"post")
            values(b"$&BEFORE$:$&AFTER$",b"1:1")
            values(b"$-=BEFORE=0$$-=AFTER=0$")
            values(action,b"pre"+rejected+b"post")
            values(b"$&BEFORE$:$&AFTER$",b"1:1")
            values(b"$MKILL$")
        t.exchange(button((400,300,430,330),b"<><><>"))
        click(410,310,b"<>")

        t.case(f"rip3-clipboard-button-face-{width}x{height}")
        values(b"$MKILL$")
        # GET_IMAGE captures the requested 4x2 uniform source into its
        # documented 5x3 clipboard allocation. Erase the source afterward:
        # button rendering must read the clipboard, not the old screen.
        t.exchange(b"\x01|S0103|BDW0KDZ0L|1CDW0KDZ0L0|S0100|BDW0KDZ0L\r\n")
        frame=t.exchange(style(flags=1|256|1024,width=50,height=40,flags2=96)
                         +button((400,300,0,0),b"ID=12:<><>clipboard:$FIELDID$"))
        assert all(pixel(frame,x,y)==0x00aaaa for y in range(300,303) for x in range(400,405))
        assert pixel(frame,405,300)==0xaaaaaa  # old larger face is outside this new field
        assert pixel(frame,500,20)==0
        click(404,302,b"clipboard:12")
        click(405,300)
        # The effective destination is still PORT0 with a bitmap current.
        t.exchange(b"\x01|2P6000008081|2s600\r\n"
                   +button((300,300,1,1),b"<><>screen-zero"))
        frame=values(b"$CUR(PORT)$",b"6")
        assert pixel(frame,300,300)==0x00aaaa and pixel(frame,304,302)==0x00aaaa
        click(304,302,b"screen-zero")
        values(b"$CUR(PORT,0)$")

        t.case(f"rip3-clipboard-inactive-source-padding-{width}x{height}")
        # The clipboard allocation is 5x3. Expand its viewport to 7x5,
        # deactivate and protect it, then retain that logical image size.
        frame=values(b"$CUR(PORT)$",b"0")
        values(b"$CUR(PORT,1)$")
        t.exchange(b"\x01|v00000604|v00000000\r\n")
        values(b"$PROT(PORT,1)$$CUR(PORT,0)$")
        frame=t.exchange(button((250,300,1,1),b"<><>padded"))
        assert pixel(frame,250,300)==0x00aaaa and pixel(frame,254,302)==0x00aaaa
        assert pixel(frame,255,300)==0 and pixel(frame,250,303)==0 and pixel(frame,256,304)==0
        click(256,304,b"padded")
        click(257,304)
        values(b"$UNPROT(PORT,1)$")

        t.case(f"rip3-initially-selected-plain-and-clipboard-images-{width}x{height}")
        values(b"$-=SELECTED_ACTION=0$")
        frame=t.exchange(style(flags=256|1024|2|512|16,surface=3,flags2=1)
                         +button((20,220,70,240),b"ID=91:<><>$-=SELECTED_ACTION=1$plain:$FIELDID$",
                                 hotkey=ord('Q'),flags=1))
        assert pixel(frame,20,220)==0xff5555
        assert pixel(frame,17,230)==0 and pixel(frame,15,230)==0x555555
        values(b"$&SELECTED_ACTION$",b"0")  # image selection never runs host source
        mouse(30,230,True);frame=t.exchange()
        assert pixel(frame,20,220)==0x00aaaa  # selected checkbox previews OFF
        mouse(90,230);frame=t.exchange()
        assert pixel(frame,20,220)==0xff5555  # cancel retains committed ON
        values(b"$&SELECTED_ACTION$",b"0")  # outside release is not activation
        click(30,230,b"plain:91")
        values(b"$&SELECTED_ACTION$",b"1")
        values(b"$-=SELECTED_ACTION=0$")
        client.text('q');frame=t.input(b"plain:91")
        assert pixel(frame,20,220)==0xff5555  # translated key toggles ON
        values(b"$&SELECTED_ACTION$",b"1")
        values(b"$-=SELECTED_ACTION=0$")
        # The existing protected/inactive 5x3 source has a 7x5 viewport.
        # Initial selection complements its padding as well as its face.
        frame=t.exchange(style(flags=1|1024|2,flags2=1)
                         +button((120,220,0,0),b"ID=92:<><>$-=SELECTED_ACTION=1$clip:$FIELDID$",flags=1))
        assert pixel(frame,120,220)==0xff5555
        assert pixel(frame,125,220)==0xffffff and pixel(frame,120,223)==0xffffff
        values(b"$&SELECTED_ACTION$",b"0")
        frame=click(122,221,b"clip:92")
        assert pixel(frame,120,220)==0x00aaaa
        assert pixel(frame,125,220)==0 and pixel(frame,120,223)==0
        values(b"$&SELECTED_ACTION$",b"1")

        t.case(f"rip3-enclosed-field-types-and-capture-{width}x{height}")
        values(b"$MKILL$")
        definitions=(field((10,100,50,140),b"simple",invert=True)
                     +style(flags=256|1024|2)
                     +button((100,100,140,140),b"ID=7:<><>button:$FIELDID$"))
        before=t.exchange(definitions)
        after=t.exchange(remove_fields((0,0,200,200),1|16))
        assert after.primary==before.primary and after.alternate==before.alternate
        click(20,110)
        click(110,110,b"button:7")
        # Removing an older field compacts the table but preserves the held
        # button instance and both native feedback planes exactly.
        values(b"$MKILL$")
        before=t.exchange(definitions)
        mouse(110,110,True)
        pressed=t.exchange()
        after=t.exchange(remove_fields((0,0,60,150),1|16))
        assert after.primary==pressed.primary and after.alternate==pressed.alternate
        mouse(110,110)
        after=t.input(b"button:7")
        assert after.primary==before.primary and after.alternate==before.alternate
        # Deleting that captured button removes feedback before freeing it.
        # A replacement with the same ID/rectangle cannot inherit the press.
        mouse(110,110,True)
        after=t.exchange(remove_fields((0,0,200,200),1|8,flags_width=1))
        assert after.primary==before.primary and after.alternate==before.alternate
        t.exchange(button((100,100,140,140),b"ID=7:<><>replacement:$FIELDID$"))
        mouse(110,110,True,move=True);mouse(110,110)
        t.exchange()
        click(110,110,b"replacement:7")
        # Unsupported flags and malformed bodies are rejected inside RIP,
        # leaving a usable held button and connection intact.
        mouse(110,110,True)
        pressed=t.exchange()
        after=t.exchange(remove_fields((0,0,200,200),256)+b"\x01|1kbad\r\n")
        assert after.primary==pressed.primary and after.alternate==pressed.alternate
        mouse(110,110)
        t.input(b"replacement:7")

        t.case(f"rip3-enclosed-field-copy-cleanup-{width}x{height}")
        values(b"$MKILL$")
        far=number(width-1,2)+number(height-1,2)
        t.exchange(b"\x01|S0103|B0000"+far+b"|W1\r\n"
                   +field((100,100,140,140),b"survivor",invert=True))
        # No matches after both exclusions: 128 still paints exactly S.
        # A surviving held gesture retains its instance through that paint.
        mouse(110,110,True)
        t.exchange(remove_fields((105,105,135,135),1|8|16|32|128))
        mouse(110,110)
        frame=t.input(b"survivor")
        assert pixel(frame,110,110)==0 and pixel(frame,104,110)==0x00aaaa
        # No region bits suppress both deletion and painting.
        before=frame
        frame=t.exchange(remove_fields((0,0,200,200),32|128))
        assert frame.primary==before.primary and frame.alternate==before.alternate
        # Absolute selector ignores a changed/inactive PORT0 viewport and an
        # unrelated current bitmap port; COPY also ignores the active XOR W.
        t.exchange(b"\x01|a0009|vB4B4DWAM|v00000000|2s600\r\n")
        frame=t.exchange(remove_fields((140,140,100,100),1|32|128))
        assert pixel(frame,100,100)==0x0000ff and pixel(frame,139,139)==0x0000ff and pixel(frame,140,140)==0x00aaaa
        click(110,110)
        values(b"$CUR(PORT)$",b"6")
        # 64 wins 32 and uses absolute 8-pixel mask phase even while the
        # destination port is an inactive bitmap. No field match is needed.
        t.exchange(b"\x01|k01|s3K0000000000000002\r\n")
        frame=t.exchange(remove_fields((11,21,30,40),1|8|16|32|64|128))
        assert pixel(frame,16,24)==0x00aa00 and pixel(frame,17,24)==0x0000aa
        assert pixel(frame,16,25)==0x0000aa and pixel(frame,10,24)==0x00aaaa
        values(b"$CUR(PORT,0)$")
        t.exchange(b"\x01|a0000|v0000"+far+b"|W0\r\n")

        t.case(f"rip3-auto-stamp-protected-style-{width}x{height}")
        values(b"$MKILL$$CUR(BUT,5)$")
        t.exchange(style(flags=256|64|512|16|8|32768|1024,foreground=12))
        values(b"$PROT(BUT,5)$$SBS(0)$")
        frame=t.exchange(button((100,240,200,280),b"ID=13:<>Label<>auto:$FIELDID$")
                         +b"\x01|1W2Auto.bmp\r\n")
        values(b"$CUR(BUT)$:$ISPROT(BUT,5)$:$INUSE(PORT,1)$",b"5:1:1")
        assert any(pixel(frame,x,y)==0xff5555 for y in range(240,280) for x in range(100,200))
        files=list(root.rglob("Auto.bmp"));assert len(files)==1,files
        data=files[0].read_bytes()
        assert data[:2]==b"BM" and int.from_bytes(data[28:30],"little")==8
        assert (int.from_bytes(data[18:22],"little"),int.from_bytes(data[22:26],"little"))==(106,46)
        offset=int.from_bytes(data[10:14],"little");stride=108
        # All bitmap pixels precede label output and exclude the black recess.
        assert all(value in (7,8,15) for y in range(46) for value in data[offset+y*stride:offset+y*stride+106])
        frame=t.exchange(button((300,240,0,0),b"ID=14:<><>captured:$FIELDID$"))
        assert pixel(frame,300,240)==0xffffff and pixel(frame,405,285)==0x555555
        assert all(pixel(frame,x,y)!=0xff5555 for y in range(240,286) for x in range(300,406))
        click(405,285,b"captured:14")
        click(406,285)
        click(97,240,b"auto:13")  # original field retains its creation-time bevel
        click(203,240)

        t.case(f"rip3-auto-stamp-failure-recovery-{width}x{height}")
        values(b"$RBS(0)$$PROT(PORT,1)$")
        frame=t.exchange(button((450,100,550,140),b"ID=15:<>Failure<>unexpected"))
        assert pixel(frame,480,120)==0xaaaaaa  # earlier face painting is retained
        click(480,120)
        values(b"$ISPROT(BUT,5)$:$ISPROT(PORT,1)$",b"1:1")
        values(b"$UNPROT(PORT,1)$")
        t.exchange(button((450,150,550,190),b"ID=15:<><>after-error:$FIELDID$"))
        click(447,160,b"after-error:15")  # failed capture did not clear BEVEL

        t.case(f"rip3-auto-stamp-current-clipboard-{width}x{height}")
        values(b"$MKILL$$UNPROT(BUT,5)$")
        t.exchange(style(flags=256|64,foreground=12)+b"\x01|S0103|B00000V0V\r\n")
        values(b"$CUR(PORT,1)$")
        send(button((0,0,20,16),b"<>$&ALIAS_LABEL$<>"))
        client.wait_text("ALIAS_LABEL")
        client.text("A")
        client.key("enter")
        frame=t.exchange(b"\x01|1W2Alias.bmp\r\n")
        values(b"$CUR(PORT)$",b"1")
        assert all(pixel(frame,x,y)==0x00aaaa for y in range(16) for x in range(20))
        files=list(root.rglob("Alias.bmp"));assert len(files)==1,files
        data=files[0].read_bytes();offset=int.from_bytes(data[10:14],"little")
        assert (int.from_bytes(data[18:22],"little"),int.from_bytes(data[22:26],"little"))==(106,46)
        assert any(value==12 for y in range(46) for value in data[offset+y*108:offset+y*108+106]), (data[28:30],{i:data[offset:].count(i) for i in set(data[offset:])})
        values(b"$CUR(PORT,0)$")

        t.case(f"rip3-auto-stamp-later-font-failure-{width}x{height}")
        frame=t.exchange(style(flags=256|64)
                         +b"\x01|y00000W0M000000001a1a000000NoSuchFont\r\n"
                         +button((40,40,60,56),b"<>Label<>")
                         +b"\x01|1W2AfterFontError.bmp\r\n")
        files=list(root.rglob("AfterFontError.bmp"));assert len(files)==1,files
        data=files[0].read_bytes();offset=int.from_bytes(data[10:14],"little")
        assert (int.from_bytes(data[18:22],"little"),int.from_bytes(data[22:26],"little"))==(20,16)
        assert all(value==7 for y in range(16) for value in data[offset+y*20:offset+y*20+20])
        # Successful capture converted the style before font realization
        # failed. A label-free clipboard button still uses the captured face.
        frame=t.exchange(button((80,40,0,0)))
        assert pixel(frame,80,40)==0xaaaaaa and pixel(frame,99,55)==0xaaaaaa
        t.case(f"rip3-auto-stamp-live-redraw-providers-and-fixed-crop-{width}x{height}")
        t.exchange(b"\x01|*|Y00000100\r\n\x1b[?25l")
        values(f"$WORLD(CUR,{width},{height})$".encode()+b"$-=LABEL=A$")
        frame=t.exchange(style(flags=256|1024|64|512|2,surface=3,foreground=12,flags2=8)
                         +button((100,50,120,60),b"ID=90:$-=PHASE=draw$<>$&LABEL$<>redraw:$FIELDID$"))
        values(b"$&PHASE$",b"draw")
        assert pixel(frame,97,47)==0xffffff and pixel(frame,122,62)==0x555555
        red=[(x,y) for y in range(47,63) for x in range(97,123) if pixel(frame,x,y)==0xff5555]
        assert red and min(x for x,y in red)>=110
        # Replace the current clipboard's backing with a known solid 14x12
        # image. Its larger viewport has just 9x5 available pixels at origin.
        t.exchange(b"\x01|2P100000E0C1|2s100|S0104|B00000D0B|v05070K0G|v00000000\r\n")
        values(b"$PROT(PORT,1)$$CUR(PORT,0)$$-=LABEL= $$-=PHASE=before$")
        mouse(98,48,True);t.exchange()
        values(b"$&PHASE$",b"draw")  # filename stage ran for selected full redraw
        values(b"$-=PHASE=before-up$")
        mouse(98,48);frame=t.input(b"redraw:90")
        for y in range(47,63):
            for x in range(97,123):
                assert pixel(frame,x,y)==(0xaa0000 if x<106 and y<52 else 0)
        values(b"$&PHASE$",b"draw")  # unselected redraw also runs its providers
        click(123,48)  # the original half-open hit boundary never grew

        frame=values(b"alive",b"alive")
        assert frame.application["connected"]==1
        assert (frame.metadata["plane_width"],frame.metadata["plane_height"])==(width,height)


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable",type=Path)
    parser.add_argument("encoding",choices=("CP437","UTF-8"))
    parser.add_argument("--reports",type=Path,required=True)
    args=parser.parse_args()
    for width,height in ((640,480),(800,600),(1024,768),(720,400)):
        run(args,width,height)
