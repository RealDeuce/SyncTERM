#!/usr/bin/env python3
"""Live button-style wire selection, protection, backups and SOFT lifecycle.

This tests the style owner, not button faces or click handling.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path

from st_migrated_control_test import connection, square_canvas
from st_rip3_connected_test import query,pixel
from st_terminal_conformance_test import Terminal


def number(value, width, radix=36):
    digits=b"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz#&"
    result=bytearray(b"0"*width)
    for at in range(width-1,-1,-1):
        value,digit=divmod(value,radix)
        result[at]=digits[digit]
    assert value==0
    return bytes(result)


def style(radix=36, xy=2):
    values=(64,32,2,769,16,255,0,15,8,7,35,255,15,7,35,0)
    widths=(xy,xy,2,4,2,2,2,2,2,2,2,2,2,2,1,5)
    return b"\x01|1B"+b"".join(number(v,w,radix) for v,w in zip(values,widths))+b"\r\n"


def run(args, width, height):
    with connection(args.executable,args.reports,encoding=args.encoding,
                    options="ScreenMode=Custom\nRIP=RIPv3\n",
                    settings=square_canvas(width, height)) as (client,root,peer,send):
        t=Terminal(client,root,peer,send)
        def values(text, expected=b""):
            return t.exchange(query(text,at_cursor=True),expected)

        t.exchange(b"\x1b[!",b"RIPSCRIP030001")
        t.case(f"rip3-button-style-wire-{width}x{height}")
        values(b"$CUR(BUT)$:$INUSE(BUT,5)$",b"0:0")
        before=t.exchange()
        for radix in (36,64):
            for xy in range(2,6):
                t.exchange(b"\x01|J"+number(radix,2)+b"|n"+number(xy,1,radix)+b"\r\n"+style(radix,xy))
        after=t.exchange(b"\x01|J36|n2\r\n")
        assert after.primary==before.primary and after.alternate==before.alternate
        for i in range(1,36):
            t.exchange(b"\x01|2B"+number(i,1)+b".\r\n")
            values(f"$CUR(BUT)$:$INUSE(BUT,{i})$:$ISPROT(BUT,{i})$".encode(),f"{i}:1:0".encode())
        t.exchange(b"\x01|2B5D|2B7\r\n")
        values(b"$CUR(BUT)$:$ISPROT(BUT,35)$:$ISPROT(BUT,5)$",b"5:0:1")
        t.exchange(b"\x01|2B5F|2B500 comment\r\n")
        values(b"$CUR(BUT)$:$ISPROT(BUT,5)$",b"5:0")

        t.case(f"rip3-button-style-backups-{width}x{height}")
        values(b"$CUR(BUT,5)$$PROT(BUT,5)$$SBS$$SBS(0)$$SBS(PUSH)$")
        values(b"$INUSE(BUT,BASE)$$INUSE(BUT,S0)$$INUSE(BUT,STACK)$",b"111")
        values(b"$CUR(BUT,6)$$PROT(BUT,6)$$RBS(POP)$")
        values(b"$CUR(BUT)$:$ISPROT(BUT,5)$:$ISPROT(BUT,6)$:$INUSE(BUT,STACK)$",b"5:1:0:0")
        # Protected CUR reset rejects unchanged. A later program still runs.
        values(b"$RESET(BUT,CUR)$")
        values(b"$CUR(BUT)$:$ISPROT(BUT,5)$",b"5:1")
        values(b"$CUR(BUT,6)$$RESET(BUT,TBL)$")
        values(b"$INUSE(BUT,5)$:$ISPROT(BUT,5)$:$INUSE(BUT,6)$",b"1:1:0")
        values(b"$CUR(BUT,0)$")
        # SOFT selects zero, keeps protected definitions and independent saves.
        t.exchange(b"\x01|*\r\n")
        values(b"$CUR(BUT)$:$INUSE(BUT,5)$:$ISPROT(BUT,5)$:$INUSE(BUT,BASE)$:$INUSE(BUT,S0)$",b"0:1:1:1:1")
        values(b"$RBS(0)$")
        values(b"$CUR(BUT)$:$INUSE(BUT,6)$:$INUSE(BUT,S0)$",b"5:1:0")
        # Generic whole-table COPY reaches the same typed snapshot family.
        values(b"$COPY(BUT,TBL,S1,PUSH)$$PROT(BUT,S1)$$CUR(BUT,8)$$RBS(1)$")
        values(b"$CUR(BUT)$:$INUSE(BUT,S1)$:$ISPROT(BUT,S1)$",b"5:1:1")
        values(b"$RESET(BUT,SLOTS,STACK)$$INUSE(BUT,S1)$",b"1")
        values(b"$UNPROT(BUT,S1)$$RESET(BUT,BACKUP)$")
        values(b"$INUSE(BUT,BASE)$$INUSE(BUT,S1)$$INUSE(BUT,STACK)$",b"000")
        values(b"$CUR(BUT,4)$$SBS$$SBS(2)$$PROT(BUT,S2)$")
        values(b"$RESET(BUT,ALL)$")
        values(b"$INUSE(BUT,5)$$ISPROT(BUT,5)$$INUSE(BUT,4)$$INUSE(BUT,BASE)$"
               b"$INUSE(BUT,S2)$$ISPROT(BUT,S2)$$INUSE(BUT,STACK)$",b"1100110")
        values(b"$RBS(2)$$UNPROT(BUT,S2)$$RESET(BUT,BACKUP)$")

        t.case(f"rip3-button-style-current-reset-{width}x{height}")
        values(b"$UNPROT(BUT,5)$$PROT(BUT,7)$$CUR(BUT,4)$$RESET(BUT,CUR)$")
        values(b"$CUR(BUT)$",b"0")
        expression=b"".join(f"$INUSE(BUT,{i})$".encode() for i in range(1,36))
        defined={2,5,6,8,12,14,16,22,24,*range(26,36)}
        values(expression,b"".join(b"1" if i in defined else b"0" for i in range(1,36)))
        # Malformed wire fields and backup selectors are consumed internally.
        t.exchange(b"\x01|1Bbad|2BZ?|2B10\r\n")
        values(b"$RESET(BUT,S10)$", b"$RESET(BUT,S10)$")
        frame=values(b"$CUR(BUT)$:alive",b"1:alive")
        assert frame.application["connected"]==1

        t.case(f"rip3-button-style-undefined-definition-{width}x{height}")
        values(b"$CUR(BUT,5)$$UNPROT(BUT,5)$$RESET(BUT,5)$")
        values(b"$CUR(BUT)$:$INUSE(BUT,5)$:$ISPROT(BUT,5)$:$ISPROT(BUT,CUR)$",b"5:0:-1:0")
        # Literal style B from PR114; a failed definition leaves it unused.
        t.exchange(b"\x01|1Bbad\r\n")
        values(b"$INUSE(BUT,5)$",b"0")
        t.exchange(b"\x01|1B..020074.0C.0F0802..0F07.Z9\r\n")
        values(b"$CUR(BUT,5)$$CUR(BUT)$:$INUSE(BUT,5)$:$ISPROT(BUT,5)$:$ISPROT(BUT,CUR)$",b"5:1:0:0")
        frame=t.exchange(b"\x01|1U0A0A2I14..Z<>Style B\r\n")
        assert any(pixel(frame,x,y)==0xff5555
                   for y in range(10*height//350,40*height//350)
                   for x in range(10*width//640,90*width//640))
        values(b"$RESET(BUT,TBL)$$CUR(BUT)$:$INUSE(BUT,5)$:$ISPROT(BUT,CUR)$",b"5:0:0")
        values(b"$CUR(BUT,7)$$PROT(BUT,7)$$CUR(BUT,0)$$RESET(BUT,CUR)$")
        values(b"$INUSE(BUT,7)$:$ISPROT(BUT,7)$:$ISPROT(BUT,CUR)$",b"0:-1:0")
        values(b"$CUR(BUT,7)$$CUR(BUT)$:$INUSE(BUT,7)$:$ISPROT(BUT,7)$:$ISPROT(BUT,CUR)$",b"7:1:0:0")

        t.case(f"rip3-button-style-aggregate-queries-{width}x{height}")
        values(b"$RESET(HARD)$")
        values(b"$ISPROT(BUT,ALL)$:$ISPROT(BUT,ANY)$:$INUSE(BUT,BACKUP)$", b"0:0:0")
        values(b"".join(f"$CUR(BUT,{i})$".encode() for i in range(1,35)) + b"$PROT(BUT,ALL)$")
        values(b"$ISPROT(BUT,ALL)$:$ISPROT(BUT,ANY)$:$ISPROT(BUT,35)$", b"0:1:-1")
        values(b"$CUR(BUT,35)$$PROT(BUT,35)$$ISPROT(BUT,ALL)$", b"1")
        values(b"$UNPROT(BUT,ALL)$$ISPROT(BUT,ANY)$", b"0")

        # Check BASE and STACK independently with all numbered slots empty.
        values(b"$SBS(PUSH)$$INUSE(BUT,BACKUP)$$INUSE(BUT,ANYSLOTS)$", b"10")
        values(b"$RBS(POP)$$INUSE(BUT,BACKUP)$", b"0")
        values(b"$SBS$$INUSE(BUT,BACKUP)$$INUSE(BUT,ANYSLOTS)$", b"10")
        values(b"$RESET(BUT,BASE)$$INUSE(BUT,BACKUP)$", b"0")

        # The portable aggregate predicates cover all ten numbered slots.
        values(b"$SBS(8)$$PROT(BUT,S8)$$INUSE(BUT,S8)$:$ISPROT(BUT,S8)$", b"1:1")
        values(b"$INUSE(BUT,ANYSLOTS)$:$ISPROT(BUT,ANYSLOTS)$:$INUSE(BUT,BACKUP)$", b"1:1:1")
        values(b"$INUSE(BUT,S9)$:$ISPROT(BUT,S9)$", b"0:-1")
        values(b"".join(f"$SBS({i})$".encode() for i in range(8))
               + b"$PROT(BUT,ALLSLOTS)$$INUSE(BUT,ALLSLOTS)$:$ISPROT(BUT,ALLSLOTS)$", b"0:0")
        values(b"$SBS(9)$$INUSE(BUT,ALLSLOTS)$:$ISPROT(BUT,ALLSLOTS)$", b"1:0")
        values(b"$PROT(BUT,S9)$$ISPROT(BUT,ALLSLOTS)$", b"1")
        values(b"$UNPROT(BUT,ALLSLOTS)$$RESET(BUT,SLOTS)$$INUSE(BUT,BACKUP)$", b"0")


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable",type=Path)
    parser.add_argument("encoding",choices=("CP437","UTF-8"))
    parser.add_argument("--reports",type=Path,required=True)
    args=parser.parse_args()
    for width,height in ((640,480),(800,600),(1024,768),(720,400)):
        run(args,width,height)
