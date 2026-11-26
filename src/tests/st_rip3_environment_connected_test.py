#!/usr/bin/env python3
"""ENV selection/entry-copy ownership through actual RIP3 TCP sessions."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path
from unicodedata import normalize
from st_migrated_control_test import connection
from st_terminal_conformance_test import Terminal
from st_rip3_connected_test import query,pixel
from st_rip3_button_styles_connected_test import number
from syncterm_fixture import cell_at


def charset_metadata(t, encoding):
    def values(source, expected=b''):
        return t.exchange(query(source, at_cursor=True), expected)

    t.case('rip3-charset-metadata-replies-and-recovery')
    values(b'$CDCS$/$SDCS()$/$DCS()$/$CHARSET()$/$LANG$', b'USASCII/USASCII/0/USASCII/eng')
    values(b'$CHARSET(35)$:$DCS(35,OTHER)$:$DCS(35,KANJI)$:$INUSE(ENV,35)$', b'-1:-1:-1:0')
    rejected = (b'[$CHARSET(36)$][$CHARSET(-1)$][$CHARSET(CUR,0)$][$DCS(KANJI)$]'
           b'[$DCS(CUR,OTHER)$][$SDCS(OTHER)$][$SDCS(OFF,QUERY)$]alive')
    values(rejected, rejected)
    sample = b'\x1b[H#' + (b'\xb3' if encoding == 'CP437' else '│'.encode())
    t.check('rip3-charset-default-ansi-glyphs', sample, lines=((1, 1, '#│'),), reset=False)
    t.exchange(b'\x01|T$SDCS(KANJI)$\r\n')
    values(b'$SDCS(query)$:$DCS$:$CHARSET(cur)$', b'KANJI:0:KANJI')
    t.check('rip3-charset-kanji-metadata-preserves-ansi-glyphs', sample,
            lines=((1, 1, '#│'),), reset=False)

    t.case('rip3-charset-entry-protection-copy-and-backup')
    values(b'$DCS(CUR,USASCII)$$DCS(CUR,OFF)$$DCS(CUR,query)$:$CHARSET$', b'USASCII:USASCII')
    values(b'$CUR(ENV,5)$$DCS$:$CHARSET$', b'0:KANJI')
    values(b'$PROT(ENV,5)$$DCS(CUR,USASCII)$$DCS$:$CHARSET$', b'USASCII:USASCII')
    values(b'$RESET(CHARSET,DCS,5)$$DCS$:$CHARSET$', b'USASCII:USASCII')
    values(b'$DCS(CUR,OFF)$$DCS$:$CHARSET$', b'0:KANJI')
    values(b'$DCS(CUR,KANJI)$$COPY(ENV,5,6)$$DCS(6)$:$ISPROT(ENV,6)$', b'KANJI:1')
    values(b'$SENV$$DCS(5,USASCII)$$SDCS(OFF)$$CUR(ENV,0)$$RENV$'
           b'$CUR(ENV)$:$DCS$:$SDCS$:$CHARSET$', b'5:KANJI:-1:KANJI')
    values(b'$RESET(CHARSET,DCS,TBL)$$DCS(0)$:$DCS(5)$:$DCS(6)$', b'0:KANJI:KANJI')
    values(b'$CUR(ENV,0)$$DCS(0,KANJI)$$RESET(CHARSET,DCS,CUR)$$DCS$:$CHARSET$', b'0:USASCII')

    t.case('rip3-charset-header-soft-and-hard-reset')
    values(b'$SDCS(USASCII)$$DCS(0,USASCII)$')
    t.exchange(b'\x01|h00000401901\r\n' + query(b'$SDCS$', at_cursor=True), b'USASCII')
    t.exchange(b'\x01|h00000002901200060\r\n' + query(b'$SDCS$:$CHARSET$', at_cursor=True), b'KANJI:USASCII')
    t.exchange(b'\x01|*\r\n')
    values(b'$CUR(ENV)$:$SDCS$:$DCS$:$CHARSET$:$DCS(5)$', b'0:KANJI:0:KANJI:KANJI')
    values(b'$RESET(HARD)$$SDCS$:$DCS$:$DCS(5)$:$CHARSET$', b'KANJI:0:-1:KANJI')
    values(b'$RESET(CHARSET,SDCS,CUR)$$SDCS$:$DCS$:$CHARSET$', b'-1:0:USASCII')
    values(b'$SDCS(USASCII)$')


def run(args,width,height):
    with connection(args.executable,args.reports,encoding=args.encoding,
                    options='ScreenMode=Custom\nRIP=RIPv3\n',
                    settings=f'CustomColumns={width//8}\nCustomRows={height//8}\nCustomFontHeight=8\n') as (client,root,peer,send):
        t=Terminal(client,root,peer,send)
        def values(source,expected=b''):
            return t.exchange(query(source,at_cursor=True),expected)
        def wire(source):
            return t.exchange(b'\x01|'+source+b'\r\n')
        def other_selections():
            values(b'$CUR(STYLE)$:$CUR(BUT)$:$CUR(PORT)$:$CUR(TW)$:$CUR(PAL)$',b'7:6:4:3:17')
        t.exchange(b'\x1b[!',b'RIPSCRIP030001')
        if width == 640:
            if args.encoding == 'UTF-8':
                frame = t.check('rip3-group-markers-preserve-a-pending-grapheme',
                                b'\x1b[He\x01|(|(|)|)\r\n' + '\u0301!'.encode(),
                                xy=(3, 1), reset=False)
                assert normalize('NFC', frame.cell_text(cell_at(frame, 0, 0))) == 'é'
                assert frame.cell_text(cell_at(frame, 1, 0)) == '!'
            charset_metadata(t, args.encoding)
            t.case('rip3-coordinate-width-reserve-and-invalid-size')
            width_query = b'$BASEMATH$:$COORDSIZE$'
            wire(b'(note|n3001suffix|)note')
            values(width_query,b'36:3')
            wire(b'n6|n50.')
            values(width_query,b'36:3')
            wire(b'J1S|n4&')
            values(width_query,b'64:4')
            wire(b'n.')
            values(width_query,b'64:2')
        t.exchange(b'\x01|*\r\n\x1b[?25l')
        values(f'$WORLD(CUR,{width},{height})$'.encode())
        values(b'$CUR(STYLE,7)$$CUR(BUT,6)$$CUR(PORT,4)$$CUR(TW,3)$$CUR(PAL,17)$')
        accented=b'\x82' if args.encoding=='CP437' else 'é'.encode()
        t.exchange(b'\x1b[?25l\x1b[33;44m\x1b[2;3H'+accented)
        wire(b'c04|m0A0B|X0A0A')
        values(b'$MCURSOR(6)$$BAUDEMUL(300)$$COPY(ENV,0,5)$$MCURSOR(2)$')
        values(f'$WORLD(5,{2*width},{2*height})$'.encode()+
               b'$BASEMATH(5,64)$$COORDSIZE(5,4)$$COLORMODE(5,RGB,8)$'
               b'$PROT(ENV,5)$')
        t.case(f'rip3-env-activation-writeback-and-independent-tables-{width}x{height}')
        before=client.capture();after=wire(b'2E5.')
        assert before.primary==after.primary and before.alternate==after.alternate
        assert after.metadata['pointer_shape']==6
        assert (after.metadata['plane_width'],after.metadata['plane_height'])==(width,height)
        values(b'$CUR(ENV)$:$WORLD$:$BASEMATH$:$COORDSIZE$:$COLORMODE$:$BAUDEMUL$',
               f'5:{2*width}:{2*height}:64:4:8:300'.encode())
        values(b'$BASEMATH(0)$:$COORDSIZE(0)$:$COLORMODE(0)$:$WORLD(0)$:$BAUDEMUL()$',
               f'64:4:8:{width}:{height}:300'.encode())
        values(b'$BAUDEMUL(1200)$')
        other_selections()
        after=values(b'$CUR(ENV,0)$')
        assert after.metadata['pointer_shape']==4
        assert (after.application['cterm.x'],after.application['cterm.y'],after.application['cterm.attr'])==(4,2,0x16)
        values(b'$TEXTXY$:$WORLD$:$BASEMATH$:$COORDSIZE$:$COLORMODE$',
               f'10:11:{width}:{height}:64:4:8'.encode())
        values(b'$COLORMODE(CUR,PAL)$')
        frame=wire(b'c04|X'+number(62,4,64)+number(30,4,64))
        assert pixel(frame,62,30)==0xaa0000  # Current base64 and four-digit coordinates are active.
        # The slot and flags widths are independent of the coordinate size.
        wire(b'2E500 comment')
        values(b'$CUR(ENV,2)$')
        values(b'$WORLD$:$BASEMATH$:$COORDSIZE$:$COLORMODE$:$BAUDEMUL$',b'640:350:36:2:0:1200')
        values(b'$PROT(STYLE,7)$')
        frame=wire(b'M18|1c03')
        assert frame.metadata['pointer_shape']==1
        values(b'$COLORMODE$',b'8')  # STYLE protection does not lock ENV settings.
        values(b'$COLORMODE(CUR,PAL)$$MCURSOR$$UNPROT(STYLE,7)$')
        values(b'$BASEMATH(5)$:$COORDSIZE(5)$:$COLORMODE(5)$:$WORLD(5)$:$ISPROT(ENV,5)$',
               f'36:2:0:{2*width}:{2*height}:1'.encode())
        frame=wire(b'2EZ0');assert frame.metadata['pointer_shape']==0
        values(b'$CUR(ENV)$:$INUSE(ENV,35)$',b'35:1')
        t.case('rip3-env-protection-bypasses-and-entry-copy-preflight')
        values(b'$CUR(ENV,5)$')
        frame=wire(b'M18|1c03')  # Protected current ENV rejects both wire edits.
        assert frame.metadata['pointer_shape']==6
        values(b'$COLORMODE$',b'0')
        frame=values(b'$COLORMODE(CUR,RGB,3)$$MCURSOR(7)$$COLORMODE$',b'3')
        assert frame.metadata['pointer_shape']==7
        values(b'$COLORMODE(CUR,PAL)$$COPY(ENV,CUR,6,8)$')
        values(b'$ISPROT(ENV,6)$:$ISPROT(ENV,8)$:$CUR(ENV)$',b'1:1:5')
        for source, rejected in (
            (b'$COPY(ENV,5,9,6,10)$', False),
            (b'$COPY(ENV,5,9,5,10)$', True),
            (b'$COPY(ENV,5,9,36,10)$', True),
            (b'$COPY(ENV,34,9)$', False),
        ):
            values(source, source if rejected else b'');values(b'$INUSE(ENV,9)$$INUSE(ENV,10)$$INUSE(ENV,34)$',b'000')
        values(b'$COPY(ENV,5,0)$$CUR(ENV)$',b'5')
        values(b'$CUR(ENV,0)$$ISPROT(ENV,CUR)$',b'0')
        values(b'$WORLD$:$BAUDEMUL$',f'{2*width}:{2*height}:1200'.encode())
        frame=values(b'$COPY(ENV,2,CUR)$')
        assert frame.metadata['pointer_shape']==0
        values(b'$CUR(ENV)$:$WORLD$:$BAUDEMUL$',b'0:640:350:1200')
        other_selections()
        values(b'$COPY(ENV,2,9)$$WORLD(9,0,0)$$CUR(ENV,9)$')
        values(b'$CUR(ENV)$:$WORLD$',b'9:0:0')
        wire(b'X0000|2E..')  # Unusable projection is local; the next selector still runs.
        values(b'$CUR(ENV)$:$WORLD$',b'0:640:350')
        t.case('rip3-env-nonzero-backup-selection-soft-and-nonfatal-recovery')
        values(b'$CUR(ENV,6)$$SENV(1)$$CUR(ENV,0)$')
        frame=values(b'$RENV(1)$');assert frame.metadata['pointer_shape']==7
        values(b'$CUR(ENV)$:$WORLD$:$INUSE(ENV,S1)$',f'6:{2*width}:{2*height}:0'.encode())
        other_selections()
        t.exchange(b'\x01|*\r\n\x1b[?25l')
        values(b'$CUR(ENV)$:$WORLD$:$INUSE(ENV,5)$$INUSE(ENV,6)$$INUSE(ENV,8)$',b'0:640:350:111')
        values(b'$BAUDEMUL$',b'1200')
        values(b'$CUR(ENV,36)$', b'$CUR(ENV,36)$');wire(b'2E|2E0|2E10.|2E?0|2E..')
        values(b'$CUR(ENV)$:alive',b'0:alive')
        assert client.capture().application['connected']==1

        t.case('rip3-env-wire-source-and-destination-protection')
        wire(b'2E1. text|2E205 note')
        values(b'$CUR(ENV)$:$ISPROT(ENV,1)$:$ISPROT(ENV,2)$',b'2:1:1')
        wire(b'2E10Fignored')
        values(b'$CUR(ENV)$:$ISPROT(ENV,1)$:$ISPROT(ENV,2)$',b'1:0:0')
        wire(b'2E005')
        values(b'$CUR(ENV)$:$ISPROT(ENV,CUR)$:$ISPROT(ENV,1)$',b'0:0:1')
        wire(b'2E10')
        values(b'$CUR(ENV)$:$ISPROT(ENV,1)$:alive',b'1:1:alive')

        t.case(f'rip3-live-environment-reset-activates-defaults-{width}x{height}')
        values(b'$CUR(ENV,11)$$WORLD(CUR,1280,960)$$BASEMATH(CUR,64)$$COORDSIZE(CUR,5)$'
               b'$COLORMODE(CUR,RGB,8)$$MCURSOR(7)$$SDCS(KANJI)$$DCS(CUR,USASCII)$'
               b'$CUR(STYLE,7)$$CUR(BUT,6)$$CUR(PORT,4)$$CUR(TW,3)$$CUR(PAL,17)$'
               b'$SENV$$SENV(3)$$PROT(ENV,S3)$')
        values(b'$RESET(ENV,BASE,11,INVALID)$$INUSE(ENV,BASE)$:$WORLD$:$COORDSIZE$',
               b'$RESET(ENV,BASE,11,INVALID)$1:1280:960:5')
        values(b'$RESET(ENV,0)$$WORLD$:$BASEMATH$:$COORDSIZE$:$COLORMODE$', b'1280:960:64:5:8')
        before=wire(b'h01000400')
        after=values(b'$RESET(ENV,11)$$CUR(ENV)$:$INUSE(ENV,11)$:$WORLD$:$BASEMATH$:$COORDSIZE$:$COLORMODE$',
                     b'11:0:640:350:36:2:0')
        assert after.primary == before.primary and after.alternate == before.alternate
        assert after.metadata['pointer_shape'] == 0  # The ENV pointer changes while input stays gated.
        assert (after.application['cterm.x'], after.application['cterm.y'], after.application['cterm.attr']) == (
            before.application['cterm.x'], before.application['cterm.y'], before.application['cterm.attr'])
        values(b'$WORLD(11)$:$COLORMODE(11)$:$DCS$:$SDCS$:$CHARSET$', b'-1:-1:0:KANJI:KANJI')
        values(b'$ISPROT(ENV,CUR)$:$ISPROT(ENV,11)$', b'0:-1')
        frame=wire(b'#|c04|X0W0Z')
        assert frame.metadata['pointer_shape'] == 0
        assert pixel(frame, 32 * width // 640, height // 10) == 0xaa0000
        wire(b'fHSDC')  # Setter works on the selected undefined record.
        values(b'$WORLD$:$WORLD(11)$:$INUSE(ENV,11)$', b'640:480:-1:0')
        values(b'$RESET(ENV,11,CUR)$$CUR(ENV)$:$WORLD(11)$:$COLORMODE(11)$', b'11:640:350:0')
        values(b'$RESET(ENV,ALL)$$CUR(ENV)$:$INUSE(ENV,11)$:$ISPROT(ENV,1)$'
               b':$INUSE(ENV,BASE)$:$ISPROT(ENV,S3)$', b'11:0:1:0:1')
        other_selections()

        t.case(f'rip3-live-compat-switches-drawing-syntax-{width}x{height}')
        values(b'$RESET(HARD)$$CUR(ENV,12)$$WORLD(CUR,1280,960)$$BASEMATH(CUR,64)$'
               b'$COORDSIZE(CUR,5)$$COLORMODE(CUR,RGB,8)$$MCURSOR(6)$$DCS(CUR,KANJI)$'
               b'$BAUDEMUL(1200)$$PROT(ENV,12)$$CUR(ENV,0)$')
        values(b'$WORLD(CUR,800,600)$$BASEMATH(CUR,64)$$COORDSIZE(CUR,5)$$COLORMODE(CUR,RGB,8)$')
        t.exchange(b'\x1b[?25l')
        before = client.capture()
        after = values(b'[$COMPAT(12)$]$CUR(ENV)$:$WORLD$:$BASEMATH$:$COORDSIZE$:$COLORMODE$'
                       b':$WORLD(12)$:$BASEMATH(12)$:$COORDSIZE(12)$:$COLORMODE(12)$:$ISPROT(ENV,12)$',
                       b'[]0:800:600:64:5:8:640:350:36:2:0:1')
        assert after.primary == before.primary and after.alternate == before.alternate
        values(b'[$COMPAT(13)$][$COMPAT(36)$][$COMPAT(CUR,0)$]$WORLD$:$INUSE(ENV,13)$',
               b'[$COMPAT(13)$][$COMPAT(36)$][$COMPAT(CUR,0)$]800:600:0')
        after = values(b'[$COMPAT(cur)$]$WORLD$:$BASEMATH$:$COORDSIZE$:$COLORMODE$:$BAUDEMUL$',
                       b'[]640:350:36:2:0:1200')
        assert after.primary == before.primary and after.alternate == before.alternate
        frame = wire(b'c04|X3C1Y')  # World (120, 70), two base-36 digits per axis.
        assert pixel(frame, 120 * width // 640, height // 5) == 0xaa0000
        frame = values(b'$CUR(ENV,12)$$DCS$:$ISPROT(ENV,CUR)$:$BAUDEMUL$', b'KANJI:1:1200')
        assert frame.metadata['pointer_shape'] == 6
        values(b'$COLORMODE(CUR,RGB,8)$')
        values(b'$COMPAT(CUR)$$COLORMODE$:$ISPROT(ENV,CUR)$', b'0:1')
        values(b'$COLORMODE(CUR,RGB,8)$$COMPAT(12)$$COLORMODE$:$ISPROT(ENV,CUR)$', b'0:1')
        values(b'$UNPROT(ENV,12)$$RESET(ENV,12)$$WORLD(CUR,800,600)$'
               b'$COMPAT(12)$$WORLD$:$INUSE(ENV,12)$', b'$COMPAT(12)$800:600:0')
        values(b'$COMPAT$$WORLD$:$INUSE(ENV,12)$:$BAUDEMUL$', b'640:350:0:1200')
        values(b'$WORLD(CUR,800,600)$$COMPAT()$$WORLD$:$INUSE(ENV,12)$', b'640:350:0')
        values(b'$WORLD(0,800,600)$$COMPAT(0)$$WORLD(0)$:$CUR(ENV)$', b'640:350:12')
        assert client.capture().application['connected'] == 1


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path)
    parser.add_argument('encoding',choices=('CP437','UTF-8'))
    parser.add_argument('--reports',type=Path,required=True)
    args=parser.parse_args()
    for width,height in ((640,480),(800,600),(1024,768),(720,400)):
        run(args,width,height)
