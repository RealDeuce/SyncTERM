#!/usr/bin/env python3
"""Byte-oriented graphics metric replies through live RIP3 connections."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path
from st_migrated_control_test import connection
from st_terminal_conformance_test import Terminal
from st_rip3_connected_test import pixel, query
from st_rip3_button_styles_connected_test import number
from syncterm_fixture import receive


def run(args,width,height):
    with connection(args.executable,args.reports,encoding=args.encoding,
                    options='ScreenMode=Custom\nRIP=RIPv3\n',
                    settings=f'CustomColumns={width//8}\nCustomRows={height//8}\nCustomFontHeight=8\n') as (client,root,peer,send):
        t=Terminal(client,root,peer,send)
        cw,ch=width//80,height//43
        def decimal(value):
            return str(value).encode()
        def signed(value):
            return (value+32768)%65536-32768
        def width_reply(count):
            return decimal(signed(count*cw))+b'\r'
        def outline(name=b'Marin',size=90,rotation=0,flags=0,baseline=0):
            alphabet=b'0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz!?'
            def ultra(value):
                return bytes((alphabet[value//64],alphabet[value%64]))
            fields=(b'00'+ultra(flags//4096)+ultra(flags%4096)+ultra(size)+b'00'
                    +ultra(rotation)+ultra(rotation)+ultra(baseline)+ultra(100)+b'00'+b'000000')
            t.exchange(b'\x01|y'+fields+name+b'\r\n')
        def values(source,expected=b''):
            return t.exchange(query(source,at_cursor=True),expected)
        def metric(source,expected,mode=0,domain=1,reserved=b'0000'):
            return t.exchange(b'\x01|r'+bytes((48+mode,48+domain))+reserved+source+b'\r\n',expected)
        t.exchange(b'\x1b[!',b'RIPSCRIP030001')
        t.exchange(b'\x01|*|Y00000100\r\n\x1b[?25l')
        values(f'$WORLD(CUR,{width},{height})$'.encode())
        t.exchange(b'\x01|m0A0K\r\n')
        t.case(f'rip3-metric-replies-and-no-paint-{width}x{height}')
        before=t.exchange()
        frame=t.exchange(b'\x01|r.1.ab\r\n',width_reply(2))
        assert frame.primary==before.primary and frame.alternate==before.alternate
        values(b'$TEXTXY$',b'10:20')
        metric(b'$textdata$',decimal(cw)+b':'+decimal(cw)+b'\r',mode=2)
        metric(b'$TEXTDATA$',decimal(ch)+b'\r',mode=1)
        metric(b'$TEXTDATA$',decimal(ch)+b':'+decimal(ch)+b'\r',mode=3)
        values(f'$WORLD(CUR,{2*width},{2*height})$'.encode())
        metric(b'$TEXTDATA$',width_reply(4),domain=0)
        metric(b'$TEXTDATA$',decimal(2*cw)+b':'+decimal(2*cw)+b'\r',mode=2,domain=0)
        values(f'$WORLD(CUR,{width},{height})$'.encode())
        t.case('rip3-metric-provider-policy-retained-sentinel-and-early-validation')
        values(b'[3:]AB')
        metric(b'<<?3>>',width_reply(2))
        metric(b'$?3$',width_reply(4))
        metric(b'$=MISSING$',width_reply(10))  # ordinary unknown references cannot prompt
        metric(b'$RESX$',width_reply(3))
        metric(b'\\$V$',width_reply(3))
        metric(b'$TEXTDATA$',width_reply(3))  # no second expansion of retained "$V$"
        t.exchange(b'\x01|r410000$COORDSIZE(CUR,5)$|r020000$COORDSIZE(CUR,5)$'
                   b'|r01????$COORDSIZE(CUR,5)$|r01000\r\n')
        values(b'$COORDSIZE$',b'2')
        metric(b'$TEXTDATA$',width_reply(3))
        t.exchange(b'\x01|r01000Hello\r\n',width_reply(4))
        t.exchange(b'\x01|r01....Hello\r\n',width_reply(8))
        t.exchange(b'\x01|J1S|r01abcdab|J10\r\n',width_reply(2))
        metric(b'\$V$',width_reply(3))
        t.exchange(b'\x01|r010000<<MKILL>>\r\n')
        metric(b'$TEXTDATA$',width_reply(3))  # failed expansion retains the last completed text
        t.case('rip3-metric-original-byte-profile-and-working-prefix')
        encoded=b'\x82' if args.encoding=='CP437' else b'\xc3\xa9'
        metric(encoded,width_reply(1) if args.encoding=='CP437' else decimal(cw)+b':'+decimal(cw)+b'\r',mode=2)
        encoded=b'\x82'*1022 if args.encoding=='CP437' else b'\xc3\xa9'*511
        metric(encoded+b'$COORDSIZE(CUR,5)$',width_reply(1022))
        values(b'$COORDSIZE$',b'2')
        t.exchange(b'\x01|Y00000500\r\n')
        metric(b'A'*1022,width_reply(5110))  # 1022*40 narrowed to signed WORD
        values(f'$WORLD(CUR,{2*width},{2*height})$'.encode())
        metric(b'$TEXTDATA$',decimal(signed(2*signed(5110*cw)))+b'\r',domain=0)  # signed conversion narrows again
        t.exchange(b'\x01|Y00000100\r\n')
        values(f'$WORLD(CUR,{width},{height})$'.encode())
        t.case('rip3-metric-real-angle-prompt-and-ordered-wire')
        send(b'\x01|r010000<<LIVE>>\r\n'+query(b'after',at_cursor=True)+b'\x1b[5n')
        client.wait_text('LIVE')
        client.text('AB');client.key('enter')
        receive(peer,width_reply(2)+b'after\x1b[0n',root/'replies.bin')
        metric(b'$TEXTDATA$',width_reply(2))
        t.case('rip3-real-outline-width-height-cold-and-warm-fallback')
        outline(b'absent')
        metric(b'F',b'0\r')
        metric(b'$TEXTDATA$',b'0\r',mode=1)
        outline()
        metric(b'FF',b'100\r')
        metric(b'$TEXTDATA$',b'104\r',mode=1)
        metric(b'$TEXTDATA$',b'50:50\r',mode=2)
        metric(b'$TEXTDATA$',b'104:104\r',mode=3)
        for rotation in (900,1800,2700):
            outline(rotation=rotation)
            metric(b'F',b'50\r')
            metric(b'$TEXTDATA$',b'104\r',mode=1)
        # Preparation sees setters obtained from a resumed real prompt.
        outline()
        send(b'\x01|r010000<<METRICFRAME>>\r\n'+b'\x1b[5n')
        client.wait_text('METRICFRAME')
        client.text(f'$WORLDW(0,{2*width})$F');client.key('enter')
        receive(peer,b'25\r\x1b[0n',root/'replies.bin')
        values(f'$WORLD(CUR,{width},{height})$'.encode())
        metric(b'F',b'50\r')
        # Inactive viewport prevents preparing newly selected Q=10 metrics.
        values(b'$DVP(CUR)$')
        outline(size=10)
        metric(b'F',b'50\r')
        metric(b'$TEXTDATA$',b'104\r',mode=1)
        values(f'$WORLD(CUR,{2*width},{2*height})$'.encode())
        metric(b'$TEXTDATA$',b'100\r',domain=0)
        metric(b'$TEXTDATA$',b'208\r',mode=1,domain=0)
        values(b'$AVP(CUR)$')
        metric(b'FF',b'4\r')
        metric(b'$TEXTDATA$',b'5\r',mode=1)
        metric(b'$TEXTDATA$',b'2:2\r',mode=2)
        metric(b'$TEXTDATA$',b'5:5\r',mode=3)
        values(f'$WORLD(CUR,{width},{height})$'.encode())
        outline()
        metric(b'$NULL$',b'104\r',mode=1)
        metric(b'$TEXTDATA$',b'0\r')
        metric(b'$TEXTDATA$',b'\r',mode=3)
        # Literal-empty outline source clears retained text without a reply.
        metric(b'F',b'50\r')
        metric(b'',b'',mode=1)
        metric(b'$TEXTDATA$',b'104\r',mode=1)
        metric(b'$TEXTDATA$',b'0\r')
        t.case('rip3-textdata-at-sentinel-and-independent-user-name')
        metric(b'F',b'50\r')
        t.exchange(b'\x01|Y00000100|@0A0K$textdata$\r\n')
        values(b'$TEXTXY$',b'60:20')  # font-zero @ uses the shared cached width
        metric(b'$TEXTDATA$',width_reply(1))
        values(b'$-=TEXTDATA=AB$')
        # T has ordinary user resolution. It must not see the retained F.
        send(b'\x01|m0A0K|T$TEXTDATA$\r\n'+b'\x1b[5n')
        client.wait_text('TEXTDATA');client.key('enter')
        receive(peer,b'\x1b[0n',root/'replies.bin')
        values(b'$TEXTXY$',b'26:20')  # actual raster bank uses 8px cells
        metric(b'$TEXTDATA$',width_reply(1))
        metric(b'$TEXTDATA$x',width_reply(11))  # exact raw sentinel only
        metric(b' $TEXTDATA$',width_reply(11))
        metric(b'F',width_reply(1))
        values(b'$COPY(STYLE,TBL,BASE)$$COPY(PORT,TBL,BASE)$$COPY(ENV,TBL,BASE)$')
        metric(b'FF',width_reply(2))
        values(b'$COPY(STYLE,BASE,TBL)$$COPY(PORT,BASE,TBL)$$COPY(ENV,BASE,TBL)$')
        metric(b'$TEXTDATA$',width_reply(2))
        t.exchange(b'\x01|*\r\n')
        metric(b'$TEXTDATA$',b'0\r')
        outline(b'absent')
        metric(b'$TEXTDATA$',b'0\r',mode=1)
        # A positive WORLD can truncate both effective DPIs to zero; this
        # prepares valid zero metrics rather than retaining an older face.
        values(b'$WORLD(CUR,65535,65535)$')
        outline()
        metric(b'F',b'0\r')
        metric(b'$TEXTDATA$',b'0\r',mode=1)
        # Ordinary outline @ prepares the same height snapshot as metrics.
        values(f'$WORLD(CUR,{width},{height})$'.encode())
        outline()
        t.exchange(b'\x01|@0A0KF\r\n')
        outline(b'absent')
        metric(b'F',b'50\r')
        metric(b'$TEXTDATA$',b'104\r',mode=1)
        t.case('rip3-retained-text-and-outline-metrics-across-table-and-scene-resets')
        outline()
        metric(b'F',b'50\r')
        values(b'$SAVEALL$')
        metric(b'FF',b'100\r')
        values(b'$RESTOREALL$$RESET(STYLE,CUR)$')
        t.exchange(b'\x01|h...|m0A0K|TA|E\r\n')
        outline(b'absent',size=10)
        metric(b'$TEXTDATA$',b'50:50\r',mode=2)
        metric(b'$TEXTDATA$',b'104\r',mode=1)
        for reset in (query(b'$RESET(SOFT)$',at_cursor=True),
                      query(b'$RESET(HARD)$',at_cursor=True),
                      b'\x01|*\r\n', b'\x01|h01000200\r\n', b'\x01|h01000100\r\n'):
            values(f'$WORLD(CUR,{width},{height})$'.encode())
            outline()
            metric(b'FF',b'100\r')
            t.exchange(reset)
            outline(b'absent')
            metric(b'$TEXTDATA$',b'\r',mode=2)
            metric(b'$TEXTDATA$',b'0\r',mode=1)
        if (width,height)==(640,480):
            t.case('rip3-decorated-outline-text-xors-visible-rgb-and-preserves-advance')
            values(b'$WORLD(CUR,640,480)$')
            t.exchange(b'\x1b[?25l\x1b[0;48;2;17;34;51m'
                       + b''.join(f'\x1b[{row};1H'.encode()+b' '*20 for row in range(1,18)))
            outline(flags=15)
            draw=b'\x01|c04|W1|@0K2SF\r\n'
            frame=t.exchange(draw+query(b'$TEXTXY$',at_cursor=True),b'70:100')
            # At 90 points the underline begins 9 pixels below the baseline
            # and strikeout 30 pixels above. Both bands cross the glyph advance.
            for x,y in ((40,110),(40,72)):
                assert pixel(frame,x,y)==(0x112233^0xaa0000), (x,y,pixel(frame,x,y))
            assert pixel(frame,80,110)==0x112233
            frame=t.exchange(draw)
            assert all(pixel(frame,x,y)==0x112233 for y in range(32,120) for x in range(10,90))
            t.case('rip3-bounded-outline-text-fits-upright-in-a-translated-viewport')
            t.exchange(b'\x01|*\r\n\x1b[?25l')
            values(b'$WORLD(CUR,640,480)$')
            t.exchange(b'\x01|v0507HRDB|c0F|m0A0K\r\n')
            values(b'$COORDSIZE(CUR,3)$')
            # F advances 556/1000 em. Global height is 1166/1000 em.
            # At 72 DPI the 41x25 box fits size 22, space 174, steps 20+20.
            # F ink spans design x=18..547 and y=-662..0 per 1000 units.
            outline(rotation=1800,flags=0x20,baseline=8)
            metric(b'FF',b'100\r')
            values(b'[3:]FF')

            def bounded(corners,source=b'',reserved=b'1Z'):
                return b'"'+b''.join(number(coord,3) for coord in corners)+reserved+source

            def line(*commands):
                return b'\x01|'+b'|'.join(commands)+b'\r\n'

            def ink(frame,x0,x1,y0,y1):
                return {(x,y) for x in range(x0,x1) for y in range(y0,y1)
                        if pixel(frame,x,y)==0xffffff}

            t.exchange(line(bounded((20,30,61,55),b'<<?3>>',reserved=b'.')))
            values(b'$WORLD(CUR,1280,960)$')
            # At 36 DPI, size 44 paints identically. Odd world endpoints
            # truncate separately before calculating the reversed extent.
            t.exchange(line(bounded((303,111,220,60),b'FF')))
            frame=t.exchange(query(b'$WORLD(CUR,640,480)$$TEXTXY$',at_cursor=True),b'15:27')
            left=ink(frame,0,40,35,68)
            right=ink(frame,40,85,35,68)
            assert left and all(25<=x<=37 and 47<=y<=62 for x,y in left), left
            assert right=={(x+20,y) for x,y in left}
            assert ink(frame,90,175,35,68)=={(x+90,y) for x,y in left|right}
            outline(b'absent')
            metric(b'$TEXTDATA$',b'40\r')
            metric(b'$TEXTDATA$',b'25\r',mode=1)

            t.case('rip3-bounded-text-uses-retained-bytes-and-preserves-the-cache-on-rejection')
            outline()
            metric(b'\\$V$',b'155\r')
            frame=t.exchange(line(bounded((40,75,110,115),b'$tExTdAtA$'))
                             +query(b'$TEXTXY$',at_cursor=True),b'15:27')
            assert ink(frame,45,115,95,124)
            # At size 35 and spacing 120, the retained '$V$' has steps 20, 30, 20.
            outline(b'absent')
            metric(b'$TEXTDATA$',b'20:30:20\r',mode=2)
            metric(b'$TEXTDATA$',b'40\r',mode=1)
            before=t.exchange()
            frame=t.exchange(line(b'Y00000100',
                                  bounded((40,75,110,115),b'$COORDSIZE(CUR,5)$'),
                                  bounded((40,75,110,115)))
                             +query(b'$COORDSIZE$:$TEXTXY$',at_cursor=True),b'3:15:27')
            assert (frame.primary,frame.alternate)==(before.primary,before.alternate)
            outline()
            values(b'$DVP(CUR)$')
            t.exchange(line(bounded((40,75,110,115),b'$COORDSIZE(CUR,5)$'))
                       +query(b'$COORDSIZE$',at_cursor=True),b'3')
            values(b'$AVP(CUR)$')
            frame=t.exchange(line(bounded((40,75,40,115),b'F'),
                                  bounded((40,75,110,115),b'$NULL$'),
                                  bounded((40,75,110,115),b'F',reserved=b'?0'))
                             +query(b'$TEXTXY$',at_cursor=True),b'15:27')
            assert (frame.primary,frame.alternate)==(before.primary,before.alternate)
            outline(b'absent')
            metric(b'$TEXTDATA$',b'20:30:20\r',mode=2)
            metric(b'$TEXTDATA$',b'40\r',mode=1)

            t.case('rip3-bounded-text-publishes-final-metrics-even-when-clipped')
            outline()
            encoded=b'\x82' if args.encoding=='CP437' else b'\xc3\xa9'
            frame=t.exchange(line(bounded((160,110,180,125),encoded)))
            assert ink(frame,165,185,120,133)
            clipped=t.exchange(line(bounded((650,110,661,124),encoded))
                               +query(b'$TEXTXY$',at_cursor=True),b'15:27')
            assert (frame.primary,frame.alternate)==(clipped.primary,clipped.alternate)
            outline(b'absent')
            metric(b'F',b'11\r')
            metric(b'$TEXTDATA$',b'14\r',mode=1)
        values(b'alive',b'alive')
        assert client.capture().application['connected']==1


def check_wide_device(args):
    with connection(args.executable, args.reports, encoding=args.encoding,
                    options='ScreenMode=Custom\nRIP=RIPv3\n',
                    settings='CustomColumns=160\nCustomRows=50\nCustomFontHeight=8\n') as (client, root, peer, send):
        t = Terminal(client, root, peer, send)
        t.case('outline text on a 1280-pixel device with a one-unit WORLD width')
        t.exchange(b'\x01|*\r\n\x1b[?25l' + query(b'$WORLD(CUR,1,400)$', at_cursor=True))
        # MARIN F at ten points rounds to 111 logical units. X DPI is
        # 72 * 1280, giving a device advance of 111 * 92160 / 1440.
        frame = t.exchange(b'\x01|y0000000A000000001a1a000000Marin|c0F|@002SF|r010000F\r\n',
                           b'7104\r')
        assert any(pixel(frame, x, y) == 0xffffff for x in range(1280) for y in range(92, 101))
        assert pixel(frame, 0, 100) == 0 and frame.application['connected'] == 1


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path)
    parser.add_argument('encoding',choices=('CP437','UTF-8'))
    parser.add_argument('--reports',type=Path,required=True)
    args=parser.parse_args()
    check_wide_device(args)
    for width,height in ((640,480),(720,400)):
        run(args,width,height)
