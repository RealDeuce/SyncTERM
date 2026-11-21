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


def style(flags=256,width=0,height=0,flags2=0,surface=7,orientation=2,bevel=3,underline=15,foreground=15):
    values=(width,height,orientation,flags,bevel,foreground,0,15,8,surface,0,flags2,underline,7,0,0)
    widths=(2,2,2,4,2,2,2,2,2,2,2,2,2,2,1,5)
    return b"\x01|1B"+b"".join(number(v,w) for v,w in zip(values,widths))+b"\r\n"


def button(area,payload=b"<><>",hotkey=0,flags=0,radix=36,xy=2):
    return (b"\x01|1U"+b"".join(number(v,xy,radix) for v in area)
            +number(hotkey,2,radix)+number(flags,1,radix)+b"0"+payload+b"\r\n")


def remove_fields(area,flags):
    return b"\x01|1k"+b"".join(number(v,2) for v in area)+number(flags,4)+b"\r\n"


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

        t.case(f"rip3-button-reset-before-host-{width}x{height}")
        t.exchange(style(flags=256|1024|4)+button((10,20,60,40),b"ID=9:<><>reset:$FIELDID$:$CUR(BUT)$"))
        frame=click(20,30,b"reset:9:0")
        for plane in (frame.primary,frame.alternate):
            assert all(((word if word&0x80000000 else frame.palette[word])&0xffffff)==0 for word in plane)
        values(f"$WORLD(CUR,{width},{height})$".encode())
        t.exchange(b"\x1b[?25l")

        t.case(f"rip3-button-capacity-and-decorations-{width}x{height}")
        t.exchange(style(flags=256|1024))
        definitions=[]
        for i in range(128):
            definitions.append(field((100,100,120,120),b"full") if i%2==0
                               else button((100,100,121,121),b"<><>full"))
        before=t.exchange(b"".join(definitions))
        after=t.exchange(button((300,300,330,330),b"<><>overflow"))
        assert after.primary==before.primary and after.alternate==before.alternate
        click(110,110,b"full")
        # Nonmouse decorations do not consume any of those 128 records.
        t.exchange(style(surface=4)+button((300,300,330,330))*129)
        frame=t.exchange()
        assert pixel(frame,310,310)==0xaa0000
        values(b"$MKILL$")
        click(110,110)
        before=t.exchange()
        # Creation stores raw HCL without querying providers. A later reached
        # argument error neither drops the field nor terminates the session.
        values(b"$-=CLICKED=0$")
        after=t.exchange(style(flags=256|1024)+button((400,300,430,330),b'<><>$-=CLICKED=1$$RAND("bad")$alive',flags=1)
                         +b"\x01|1Ubad\r\n")
        assert pixel(after,410,310)==0xaaaaaa
        values(b"$&CLICKED$",b"0")
        click(410,310)
        values(b"$&CLICKED$",b"1")
        values(b"$-=CLICKED=0$")
        click(410,310)
        values(b"$&CLICKED$",b"1")
        values(b"$MKILL$")
        t.exchange(button((400,300,430,330),b"<><><>"))
        click(410,310,b"<>")

        t.case(f"rip3-clipboard-button-face-{width}x{height}")
        values(b"$MKILL$")
        # GET_IMAGE captures the requested 4x2 uniform source into its
        # documented 5x3 clipboard allocation. Erase the source afterward:
        # button rendering must read the clipboard, not the old screen.
        t.exchange(b"\x01|S0103|BDW0KDZ0L|1CDW0KDZ0L|S0100|BDW0KDZ0L\r\n")
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
        after=t.exchange(remove_fields((0,0,200,200),1|8))
        assert after.primary==before.primary and after.alternate==before.alternate
        t.exchange(button((100,100,140,140),b"ID=7:<><>replacement:$FIELDID$"))
        mouse(110,110,True,move=True);mouse(110,110)
        t.exchange()
        click(110,110,b"replacement:7")
        # Pending paint flags and malformed bodies are rejected inside RIP,
        # leaving a usable held button and connection intact.
        mouse(110,110,True)
        pressed=t.exchange()
        after=t.exchange(remove_fields((0,0,200,200),1|32)+b"\x01|1kbad\r\n")
        assert after.primary==pressed.primary and after.alternate==pressed.alternate
        mouse(110,110)
        t.input(b"replacement:7")

        t.case(f"rip3-auto-stamp-protected-style-{width}x{height}")
        values(b"$MKILL$$CUR(BUT,5)$")
        t.exchange(style(flags=256|64|512|16|8|32768|1024,foreground=12))
        values(b"$PROT(BUT,5)$$SBS(0)$")
        frame=t.exchange(button((100,240,200,280),b"ID=13:<>Label<>auto:$FIELDID$")
                         +b"\x01|1W0Auto.bmp\r\n")
        values(b"$CUR(BUT)$:$ISPROT(BUT,5)$:$INUSE(PORT,1)$",b"5:1:1")
        assert any(pixel(frame,x,y)==0xff5555 for y in range(240,280) for x in range(100,200))
        files=list(root.rglob("Auto.bmp"));assert len(files)==1,files
        data=files[0].read_bytes()
        assert data[:2]==b"BM" and int.from_bytes(data[28:30],"little")==8
        assert (int.from_bytes(data[18:22],"little"),int.from_bytes(data[22:26],"little"))==(107,47)
        offset=int.from_bytes(data[10:14],"little");stride=108
        # All bitmap pixels precede label output and exclude the black recess.
        assert all(value in (7,8,15) for y in range(47) for value in data[offset+y*stride:offset+y*stride+107])
        frame=t.exchange(button((300,240,0,0),b"ID=14:<><>captured:$FIELDID$"))
        assert pixel(frame,300,240)==0xffffff and pixel(frame,406,286)==0x555555
        assert all(pixel(frame,x,y)!=0xff5555 for y in range(240,287) for x in range(300,407))
        click(406,286,b"captured:14")
        click(407,286)
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
        frame=t.exchange(button((0,0,20,16),b"<>A<>")+b"\x01|1W0Alias.bmp\r\n")
        values(b"$CUR(PORT)$",b"0")
        assert any(pixel(frame,x,y)==0xff5555 for y in range(16) for x in range(20))
        files=list(root.rglob("Alias.bmp"));assert len(files)==1,files
        data=files[0].read_bytes();offset=int.from_bytes(data[10:14],"little")
        assert (int.from_bytes(data[18:22],"little"),int.from_bytes(data[22:26],"little"))==(21,17)
        assert all(value==3 for y in range(17) for value in data[offset+y*24:offset+y*24+21])

        t.case(f"rip3-auto-stamp-later-font-failure-{width}x{height}")
        frame=t.exchange(style(flags=256|64)
                         +b"\x01|y00000W0M000000001a1a000000NoSuchFont\r\n"
                         +button((40,40,60,56),b"<>Label<>")
                         +b"\x01|1W0AfterFontError.bmp\r\n")
        files=list(root.rglob("AfterFontError.bmp"));assert len(files)==1,files
        data=files[0].read_bytes();offset=int.from_bytes(data[10:14],"little")
        assert (int.from_bytes(data[18:22],"little"),int.from_bytes(data[22:26],"little"))==(21,17)
        assert all(value==7 for y in range(17) for value in data[offset+y*24:offset+y*24+21])
        # Successful capture converted the style before font realization
        # failed. A label-free clipboard button still uses the captured face.
        frame=t.exchange(button((80,40,0,0)))
        assert pixel(frame,80,40)==0xaaaaaa and pixel(frame,100,56)==0xaaaaaa
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
