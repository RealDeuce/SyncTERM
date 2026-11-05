#!/usr/bin/env python3
"""Independent custom-font rasters and assignment masks through the real app."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse

from st_migrated_control_test import connection
from st_terminal_conformance_test import Terminal, tile, colour, cup, ESC
from syncterm_fixture import cell_at, receive

SCRIPT = '''
import "syncterm" for Font, Download, Codepage, CTerm, CTermFontMask, FontError, FontErr, Screen, Hook, Key, Conn, Error
class AssignmentTest {
  static start() {
    __a = Font.load(Download.list["left.f16"], Codepage.cp437)
    __b = Font.load(Download.list["right.f16"], Codepage.cp437)
    if (__a is Error || __b is Error) Fiber.abort("font fixture load")
    var result = CTerm.setFont(__a, CTermFontMask.all)
    if (result != null) Fiber.abort(result)
    Screen.checked_(Screen.videoFlags.setAltChars(true))
    Screen.checked_(Screen.videoFlags.setBlinkAltChars(true))
    Screen.checked_(Screen.videoFlags.setNoBright(true))
    Screen.checked_(Screen.videoFlags.setNoBlink(true))
    __stage = 0
    Conn.sendBytes("FONT-READY\\n")
  }
  static next() {
    __stage = __stage + 1
    var result = null
    if (__stage == 1) {
      result = CTerm.setFont(__b, CTermFontMask.normal | CTermFontMask.bright)
    } else if (__stage == 2) {
      result = CTerm.setFont(__b, CTermFontMask.blink | CTermFontMask.brightBlink)
    } else if (__stage == 3) {
      result = CTerm.setFont(254, CTermFontMask.all)
      if (!(result is FontError) || result.code != FontErr.notLoaded) Fiber.abort("missing font accepted")
      result = null
    } else {
      if (!Font.available(__a) || !Font.available(__b)) Fiber.abort("reset lost registered fonts")
    }
    if (result != null) Fiber.abort(result)
    Conn.sendBytes("FONT-%(__stage)\\n")
  }
}
AssignmentTest.start()
Hook.onKey(Key.f8, 0) {|key|
  AssignmentTest.next()
  return true
}
'''


def run(executable, encoding, reports):
    def prepare(root):
        for name, byte in (("left.f16", 0x80), ("right.f16", 0x40)):
            bitmap = bytearray(4096)
            bitmap[ord('A') * 16:(ord('A') + 1) * 16] = bytes([byte]) * 16
            (root / 'downloads' / name).write_bytes(bitmap)
        with (root / 'syncterm.lst').open('a', encoding='utf-8') as stream:
            stream.write(f'DownloadPath={root / "downloads"}\n')

    with connection(executable, reports, encoding=encoding, settings='AudioModes=0\n',
                    script=SCRIPT, prepare=prepare) as (client, root, peer, send):
        peer.settimeout(10)
        assert receive(peer, b'FONT-READY\n') == b'FONT-READY\n'
        terminal = Terminal(client, root, peer, send)

        def paint(row):
            # Independent source attribute combinations, with a stable white
            # foreground, black background and no cursor in the capture.
            return (cup(1, row) + ESC + b'[?25l' + ESC + b'[0;37;40mA' +
                    ESC + b'[1mA' + ESC + b'[0;37;40;5mA' + ESC + b'[1mA')

        def check_row(frame, row, columns):
            for x, marked in enumerate(columns, 1):
                cell = cell_at(frame, x - 1, row - 1)
                assert frame.cell_text(cell) == 'A'
                foreground, background = colour(frame, cell), colour(frame, cell, True)
                pixels = tuple((value if value & 0x80000000 else frame.palette[value]) & 0xffffff
                               for value in tile(frame, x, row))
                expected = tuple(foreground if column == marked else background
                                 for _ in range(16) for column in range(8))
                assert pixels == expected, (encoding, row, x, pixels, expected)

        terminal.case('font-assignments-' + encoding)
        frame = terminal.exchange(ESC + b'[2J' + paint(1))
        check_row(frame, 1, [0, 0, 0, 0])
        for stage, expected in ((1, [1, 1, 0, 0]), (2, [1, 1, 1, 1]), (3, [1, 1, 1, 1])):
            client.key('f8')
            assert receive(peer, f'FONT-{stage}\n'.encode()) == f'FONT-{stage}\n'.encode()
            frame = terminal.exchange(paint(stage + 1))
            check_row(frame, 1, [0, 0, 0, 0])
            if stage > 1:
                check_row(frame, 2, [1, 1, 0, 0])
            check_row(frame, stage + 1, expected)
        terminal.exchange(reset=True)
        client.key('f8')
        assert receive(peer, b'FONT-4\n') == b'FONT-4\n'
        frame = terminal.exchange(b'RECOVERED')
        assert frame.lines[0].startswith('RECOVERED')
        assert b'[wren' not in client.stderr, bytes(client.stderr)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable')
    parser.add_argument('encoding', choices=('CP437', 'UTF-8'))
    parser.add_argument('--reports', default='reports/test-control')
    args = parser.parse_args()
    run(args.executable, args.encoding, args.reports)
