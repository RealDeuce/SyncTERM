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
        frame=metric(b'ab',b'16\r',reserved=b'????')
        assert frame.primary==before.primary and frame.alternate==before.alternate
        values(b'$TEXTXY$',b'10:20')
        metric(b'$textdata$',b'8:8\r',mode=2)
        values(f'$WORLD(CUR,{2*width},{2*height})$'.encode())
        metric(b'$TEXTDATA$',b'32\r',domain=0)
        metric(b'$TEXTDATA$',b'16:16\r',mode=2,domain=0)
        values(f'$WORLD(CUR,{width},{height})$'.encode())
        t.case('rip3-metric-provider-policy-retained-sentinel-and-early-validation')
        values(b'[3:]AB')
        metric(b'<<?3>>',b'16\r')
        metric(b'$?3$',b'32\r')
        metric(b'$=MISSING$',b'80\r')  # ordinary unknown references cannot prompt
        metric(b'$RESX$',b'24\r')
        metric(b'\\$V$',b'24\r')
        metric(b'$TEXTDATA$',b'24\r')  # no second expansion of retained "$V$"
        t.exchange(b'\x01|r410000$COORDSIZE(CUR,5)$|r020000$COORDSIZE(CUR,5)$|r01000\r\n')
        values(b'$COORDSIZE$',b'2')
        metric(b'$TEXTDATA$',b'24\r')
        t.exchange(b'\x01|r010000<<MKILL>>\r\n')
        metric(b'$TEXTDATA$',b'24\r')  # failed expansion retains the last completed text
        t.case('rip3-metric-original-byte-profile-and-working-prefix')
        encoded=b'\x82' if args.encoding=='CP437' else b'\xc3\xa9'
        metric(encoded,b'8\r' if args.encoding=='CP437' else b'8:8\r',mode=2)
        encoded=b'\x82'*1022 if args.encoding=='CP437' else b'\xc3\xa9'*511
        metric(encoded+b'$COORDSIZE(CUR,5)$',b'8176\r')
        values(b'$COORDSIZE$',b'2')
        t.exchange(b'\x01|Y00000500\r\n')
        metric(b'A'*1022,b'-24656\r')  # 1022*40 narrowed to signed WORD
        values(f'$WORLD(CUR,{2*width},{2*height})$'.encode())
        metric(b'$TEXTDATA$',b'16224\r',domain=0)  # signed conversion narrows again
        t.exchange(b'\x01|Y00000100\r\n')
        values(f'$WORLD(CUR,{width},{height})$'.encode())
        t.case('rip3-metric-real-angle-prompt-and-ordered-wire')
        send(b'\x01|r010000<<LIVE>>\r\n'+query(b'after',at_cursor=True)+b'\x1b[5n')
        client.wait_text('LIVE')
        client.text('AB');client.key('enter')
        receive(peer,b'16\rafter\x1b[0n',root/'replies.bin')
        metric(b'$TEXTDATA$',b'16\r')
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
