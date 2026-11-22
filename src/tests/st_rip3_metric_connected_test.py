#!/usr/bin/env python3
"""Byte-oriented graphics metric replies through live RIP3 connections."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path
from st_migrated_control_test import connection
from st_terminal_conformance_test import Terminal
from st_rip3_connected_test import query
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
        def outline(name=b'Marin',size=90,rotation=0):
            alphabet=b'0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz!?'
            def ultra(value):
                return bytes((alphabet[value//64],alphabet[value%64]))
            fields=b'00'+b'0000'+ultra(size)+b'00'+ultra(rotation)+ultra(rotation)+b'00'+ultra(100)+b'00'+b'000000'
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
        frame=metric(b'ab',width_reply(2),reserved=b'????')
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
        t.exchange(b'\x01|r410000$COORDSIZE(CUR,5)$|r020000$COORDSIZE(CUR,5)$|r01000\r\n')
        values(b'$COORDSIZE$',b'2')
        metric(b'$TEXTDATA$',width_reply(3))
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
        values(b'alive',b'alive')
        assert client.capture().application['connected']==1


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path)
    parser.add_argument('encoding',choices=('CP437','UTF-8'))
    parser.add_argument('--reports',type=Path,required=True)
    args=parser.parse_args()
    for width,height in ((640,480),(720,400)):
        run(args,width,height)
