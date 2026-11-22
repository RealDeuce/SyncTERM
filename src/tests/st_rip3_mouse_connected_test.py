#!/usr/bin/env python3
"""Live RIPv3 simple fields: hit geometry, host commands and native feedback.

Gesture commitment and identity scope follow the adopted PR115 contract. This
does not claim button faces; separate hover cases cover resident queries.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path

from st_migrated_control_test import connection
from st_rip3_button_styles_connected_test import number
from st_rip3_connected_test import query
from st_terminal_conformance_test import Terminal


def field(area, program, invert=False, clear=False, radix=36, xy=2):
    return (b"\x01|1M00"+b"".join(number(v,xy,radix) for v in area)
            +bytes((48+invert,48+clear))+b"00000"+program+b"\r\n")


def run(args):
    with connection(args.executable,args.reports,encoding=args.encoding,
                    options="ScreenMode=Custom\nRIP=RIPv3\n",
                    settings="CustomColumns=80\nCustomRows=60\nCustomFontHeight=8\n"
                             "CustomAspectWidth=4\nCustomAspectHeight=3\n") as (client,root,peer,send):
        t=Terminal(client,root,peer,send)
        def values(program,expected=b""):
            return t.exchange(query(program,at_cursor=True),expected)
        t.exchange(b"\x1b[!",b"RIPSCRIP030001")
        # Use real native text (including a different alternate blink plane)
        # and a known device/world frame. No sound-producing sequences.
        t.exchange(b"\x01|*\r\n"+query(b"$WORLD(CUR,640,480)$",at_cursor=True)
                   +b"\x1b[?25l\x1b[5mNATIVE TEXT\x1b[m")
        initial=t.exchange()
        m=initial.metadata
        def mouse(x,y,left=False,move=False):
            px=m["destination_x"]+(2*x+1)*m["destination_width"]//(2*m["plane_width"])
            py=m["destination_y"]+(2*y+1)*m["destination_height"]//(2*m["plane_height"])
            before=client.capture().metadata["events_dequeued"]
            client.mouse(px,py,action=0 if move else 1,buttons=int(left))
            return client.wait_for(lambda s:s.metadata["events_dequeued"]>before)
        def click(x,y,expected):
            mouse(x,y,True)
            mouse(x,y)
            return t.input(expected) if expected else t.exchange()

        t.case("rip3-field-native-feedback-and-self-delete")
        t.exchange(field((0,0,96,16),b"$MKILL$clicked^M",invert=True))
        before=t.exchange()
        mouse(1,1,True)
        pressed=t.exchange()
        for y in range(16):
            for x in range(96):
                at=y*640+x
                assert pressed.primary[at]&0xffffff == (before.primary[at]^0xffffff)&0xffffff
                assert pressed.alternate[at]&0xffffff == (before.alternate[at]^0xffffff)&0xffffff
        # Leave/return must not launch the default local selection hook.
        mouse(100,20,True,move=True)
        outside=t.exchange()
        assert outside.primary==before.primary and outside.alternate==before.alternate
        mouse(1,1,True,move=True)
        mouse(1,1)
        after=t.input(b"clicked\r")
        assert after.primary==before.primary and after.alternate==before.alternate
        click(1,1,b"")

        t.case("rip3-field-feedback-around-incoming-output")
        t.exchange(field((0,0,96,16),b"drawn",invert=True))
        mouse(2,2,True)
        # Both ANSI text and RIP drawing arrive while pressed. Feedback must
        # surround the new scene, then release must restore that new scene.
        pressed=t.exchange(b"\x1b[HREPLACED\x01|c0C|X0202\r\n")
        assert pressed.primary[2*640+2]&0xffffff == 0x00aaaa
        mouse(2,2)
        after=t.input(b"drawn")
        assert after.primary[2*640+2]&0xffffff == 0xff5555
        t.exchange(b"\x01|1K\r\n")

        t.case("rip3-field-overlap-numeric-formats-and-capacity")
        for radix in (36,64):
            for xy in range(2,6):
                t.case(f"rip3-field-numeric-{radix}-{xy}")
                t.exchange(b"\x01|1K|J"+number(radix,2)+b"|n"+number(xy,1,radix)+b"\r\n"
                           +field((10,30,40,50),b"old",radix=radix,xy=xy)
                           +field((20,40,40,50),b"new",radix=radix,xy=xy))
                click(20,40,b"new")
                click(10,30,b"old")
        t.exchange(b"\x01|1K|J10|n2\r\n")
        t.exchange(b"".join(field((100,100,120,120),b"ok") for _ in range(128))
                   +field((100,100,120,120),b"overflow"))
        click(110,110,b"ok")
        values(b"$MKILL$")
        click(110,110,b"")

        t.case("rip3-field-host-encoding-and-continuation")
        label=b"\x82" if args.encoding=="CP437" else "é".encode()
        t.exchange(field((100,100,120,120),b"$D(3)$`C^M"+label+b"$XY(DEVICE)$"))
        click(110,110,b"\x03\r"+label+b"0110:0110")
        values(b"$MKILL$")
        values(b"$WORLD(CUR,1280,480)$")
        t.exchange(field((200,100,240,120),b"scaled:$XY(WORLD)$:$XY(DEVICE)$"))
        click(100,100,b"scaled:0200:0100:0100:0100")
        values(b"$MKILL$$WORLD(CUR,640,480)$")

        t.case("rip3-field-identity-grammar-and-scoped-expansion")
        prefixes=((b"ID=0:",0,b""),(b"id=00007:",7,b""),(b"Id=32767:",32767,b""),
                  (b"ID=32768:",-1,b"ID=32768:"),(b"ID=000001:",-1,b"ID=000001:"),
                  (b"ID=+7:",-1,b"ID=+7:"),(b"ID= 7:",-1,b"ID= 7:"),
                  (b"ID=7 :",-1,b"ID=7 :"),(b" ID=7:",-1,b" ID=7:"),
                  (b"ID=0x7:",-1,b"ID=0x7:"),(b"ID=7:ID=8:",7,b"ID=8:"),(b"",-1,b""))
        for prefix,identity,retained in prefixes:
            values(b"$MKILL$")
            t.exchange(field((100,100,120,120),prefix+b"$FIELDID$:$MKILL$$FIELDID$"))
            expected=str(identity).encode()
            click(110,110,retained+expected+b":"+expected)
            # Outside callback context, this is unavailable, never a user
            # prompt or a persistent last-field value.
            values(b"$FIELDID$outside",b"outside")
        t.exchange(field((100,100,120,120),b"ID=7:$D(3)$id:$FIELDID$"))
        click(110,110,b"id:7")
        values(b"$MKILL$$FIELDID$outside",b"outside")

        t.case("rip3-field-screen-zero-with-other-current-port")
        for select in (query(b"$CUR(PORT,5)$",at_cursor=True),
                       b"\x01|2P1000002021|2s100\r\n"):
            t.exchange(select+field((100,100,120,120),b"screen-zero"))
            click(110,110,b"screen-zero")
            values(b"$MKILL$")
        values(b"$CUR(PORT,0)$")

        t.case("rip3-field-viewport-storage-and-nonfatal-recovery")
        # PORT0 viewport offset; field partially clipped, then viewport moves.
        t.exchange(b"\x01|v0A0K2S2S\r\n"+field((0,0,100,100),b"offset")
                   +b"\x01|v0000HRCB\r\n")
        click(10,20,b"offset")
        click(100,100,b"offset")
        click(101,100,b"")
        click(9,20,b"")
        # SOFT destroys fields; malformed/unsupported fields cannot replace a
        # usable one or escape as a connection error.
        t.exchange(b"\x01|*\r\n")
        click(10,20,b"")
        values(b"$WORLD(CUR,640,480)$")
        t.exchange(field((10,20,20,30),b"valid")
                   +field((10,20,20,30),b"clear-pending",clear=True)
                   +b"\x01|1Mbad|1Kbad\r\n")
        click(15,25,b"valid")
        frame=values(b"alive",b"alive")
        assert frame.application["connected"]==1


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable",type=Path)
    parser.add_argument("encoding",choices=("CP437","UTF-8"))
    parser.add_argument("--reports",type=Path,required=True)
    run(parser.parse_args())
