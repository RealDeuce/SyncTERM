#!/usr/bin/env python3
"""RIP3 refresh bindings and the local refresh action over TCP."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path

from st_migrated_control_test import connection
from st_rip3_connected_test import query
from st_terminal_conformance_test import Terminal


def run(args, width, height):
    with connection(args.executable, args.reports, encoding=args.encoding,
                    options='ScreenMode=Custom\nRIP=RIPv3\n',
                    settings=f'CustomColumns={width // 8}\nCustomRows={height // 8}\n'
                             'CustomFontHeight=8\nAudioModes=0\n') as (client, root, peer, send):
        t = Terminal(client, root, peer, send)

        def values(source, expect=b''):
            return t.exchange(query(source, at_cursor=True), expect)

        def bind(source, prefix=b'0000'):
            return t.exchange(b'\x01|2R' + prefix + source + b'\r\n')

        def local(expect=b''):
            count = client.capture().metadata['events_dequeued']
            client.physical_key(63, True, 4)
            client.key('f5', 4)  # Alt+F5
            client.physical_key(63, False, 4)
            client.wait_for(lambda frame: frame.metadata['events_dequeued'] >= count + 3)
            return t.input(expect) if expect else t.exchange()

        t.exchange(b'\x1b[!', b'RIPSCRIP030001')
        t.case(f'rip3-refresh-register-expand-and-local-action-{width}x{height}')
        local()
        accented = b'\x82' if args.encoding == 'CP437' else 'é'.encode()
        bind(accented + b'$&NAME$^m', b'1234')
        values(b'$-=NAME=ready$')
        local(accented + b'ready\r')
        values(b'$-=NAME=later$prefix$REFRESH$suffix', accented + b'later\rprefixsuffix')
        values(b'$BASEMATH(CUR,64)$')
        bind(b'radix^m', b'ZZa&')
        local(b'radix\r')
        values(b'$BASEMATH(CUR,36)$')
        bind(b'bad', b'00*0')
        local(b'radix\r')

        t.case('rip3-refresh-header-and-scene-lifetime')
        t.exchange(b'\x01|h01000000\r\n')
        local(b'radix\r')
        t.exchange(b'\x01|h01000400\r\n')
        local()
        t.exchange(b'\x01|#\r\n')
        local(b'radix\r')
        t.exchange(b'\x01|h01000G00\r\n')
        local()
        for clear in (b'\x01|2R0000$OFF$\r\n', b'\x01|2R0000\r\n',
                      query(b'$NOREFRESH$', at_cursor=True), b'\x01|*\r\n',
                      b'\x01|h01000100\r\n'):
            bind(b'again')
            t.exchange(clear)
            local()
            values(b'$REFRESH$alive', b'alive')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('encoding', choices=('CP437', 'UTF-8'))
    parser.add_argument('--reports', type=Path, required=True)
    args = parser.parse_args()
    for width, height in ((640, 480), (720, 400)):
        run(args, width, height)
