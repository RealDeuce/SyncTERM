#!/usr/bin/env python3
"""Literal two-phase pixels through shared live/ring/disconnected history.

Expected colours and rectangle positions come from this fixture, not captures
of the emulator. All related state and pixels use one reply-fenced snapshot.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import re
import socket
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'testing'))
from syncterm_fixture import fixture, receive

SCRIPT = '''
import "syncterm" for Screen, PixelBuffer, PixelBlit, PixelColor, Hook, Key, Conn, Surface, Font
Hook.onKey(Key.f8) {|event|
  var cell = Screen.checked_(Screen.cellPixelSize)
  Screen.checked_(Screen.palette.set(200, 0xff0000))
  Screen.checked_(Screen.palette.set(201, 0x0000ff))
  var pixels = PixelBuffer.new(cell[0] * 2, cell[1] * 2)
  for (y in 0...pixels.height) {
    for (x in 0...pixels.width) {
      pixels.setPixel(x, y, x < cell[0] ? 200 : 201)
      pixels.setAlternate(x, y, PixelColor.rgb(x < cell[0] ? 0x00ff00 : 0xffff00))
    }
  }
  var blit = PixelBlit.new()
  blit.destinationX = 20 * cell[0]
  Screen.checked_(Screen.blitPixels(pixels, blit))
  var source = Screen.checked_(Screen.readRect(21, 1, 22, 2))
  var composed = Surface.new(4, 3)
  composed.putRect(source, 1, 1)
  Screen.checked_(Screen.putRect(composed, 31, 5))
  var overlay = Surface.new(2, 2)
  overlay.putRect(source, 0, 0)
  overlay[0].font = Font.cp437English
  overlay[0].ch = "X"
  Screen.checked_(Screen.putRect(overlay, 41, 5))
  // Overlapping self copies must retain the original two source columns.
  var overlap = Surface.new(4, 2)
  overlap.putRect(source, 0, 0)
  overlap.putRect_(overlap, 0, 0, 2, 2, 1, 0)
  Screen.checked_(Screen.putRect(overlap, 51, 5))
  Conn.sendBytes("PAINTED\n")
  return true
}
Hook.onKey(Key.f9) {|event|
  Screen.checked_(Screen.palette.set(200, 0))
  Screen.checked_(Screen.palette.set(201, 0))
  Conn.sendBytes("PALETTE\n")
  return true
}
'''


def rgb(frame, x, y, alternate=False):
    m = frame.metadata
    plane = frame.alternate if alternate else frame.primary
    value = plane[y * m['plane_width'] + x]
    return (value if value & 0x80000000 else frame.palette[value]) & 0xffffff


def check_blocks(frame, column, row):
    cw, ch = frame.metadata['cell_width'], frame.metadata['cell_height']
    for phase, colours in ((False, (0xff0000, 0x0000ff)), (True, (0x00ff00, 0xffff00))):
        for y in range(2 * ch):
            for x in range(2 * cw):
                assert rgb(frame, column * cw + x, row * ch + y, phase) == colours[x // cw], (column, row, phase, x, y)


def run(executable, reports, graphics, encoding, status):
    print(graphics,encoding,'status',status,flush=True)
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0)); listener.listen(1); listener.settimeout(15)
        skypix = graphics.startswith('SkyPix')
        mode = 'Amiga640x200' if skypix else '80x43' if graphics == 'RIPv1' else '80x25'
        entries = ('[Graphics History]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                   f'Port={listener.getsockname()[1]}\nGraphics={graphics}\nScreenMode={mode}\n'
                   f'Encoding={encoding}\nNoStatus={str(not status).lower()}\nHidePopups=false\n')
        with fixture(executable, entries=entries, settings='AudioModes=0\nScrollBackLines=100\n',
                     script=SCRIPT, reports=reports, quiet=False) as (client, root):
            client.wait_text('Graphics History'); client.key('enter')
            with listener.accept()[0] as peer:
                peer.settimeout(10)
                connected = client.wait_for(lambda s: s.application.get('connected') == 1)
                height = connected.metadata['rows'] - (1 if connected.application['cterm.status_display'] >= 1 else 0)
                def exchange(data=b''):
                    peer.sendall(data + b'\x1b[5n')
                    assert receive(peer, b'\x1b[0n') == b'\x1b[0n'
                    return client.capture()
                exchange(b'\x1b[2J\x1b[H' + b''.join(f'\x1b[{row+1};12HROW {row:02d}'.encode() for row in range(height)))
                if graphics.startswith('RIP'):
                    # RIP_PIXEL at (200,0), standard EGA red, independently
                    # of the native Screen pixel fixture at columns 20/21.
                    exchange(b'\r!|c04|X5K00\r\n!|#\r\n')
                if skypix:
                    exchange(b"\x1b[19;384;0!\x1b[10;8!Pearl.font!gpy")
                def check_native_font(shown,row):
                    if not skypix:
                        return
                    expected=((0,0,0x7e,0xc6,0xc6,0x7e,0x06,0x7c),
                              (0,0,0xfc,0xc6,0xc6,0xfc,0xc0,0xc0),
                              (0,0,0xc3,0x66,0x3c,0x18,0x30,0x60))
                    for g,rows in enumerate(expected):
                        for y,bits in enumerate(rows):
                            for x in range(8):
                                assert rgb(shown,384+g*8+x,row*8+1+y)==(0xdddddd if bits&(0x80>>x) else 0),(g,x,y)
                client.key('f8')
                assert receive(peer, b'PAINTED\n') == b'PAINTED\n'
                scene = exchange()
                check_blocks(scene, 20, 0)
                check_native_font(scene,0)
                check_blocks(scene, 31, 5)
                assert scene.lines[4][40] == 'X'
                assert scene.cells[4*scene.metadata['columns']+40].flags & 8 == 0
                assert rgb(scene,41*scene.metadata['cell_width'],4*scene.metadata['cell_height']) == 0x0000ff
                cw, ch = scene.metadata['cell_width'], scene.metadata['cell_height']
                for phase, expected in ((False, (0xff0000, 0xff0000, 0x0000ff)), (True, (0x00ff00, 0x00ff00, 0xffff00))):
                    for column, colour in enumerate(expected, 50):
                        assert rgb(scene, column*cw, 4*ch, phase) == colour
                if graphics.startswith('RIP'):
                    assert rgb(scene, 200, 0) == 0xaa0000
                # Live rows must retain pixels as well as the ring's rows.
                client.command_key(48)
                live = client.wait_text('Scrollback')
                check_blocks(live, 20, 0)
                check_native_font(live,0)
                check_blocks(live, 31, 5)
                client.key('escape')
                check_blocks(exchange(),31,5)
                frame = exchange(f'\x1b[{height};1H\n\n\n'.encode())
                check_blocks(frame, 31, 2)
                client.key('f9')
                assert receive(peer, b'PALETTE\n') == b'PALETTE\n'
                exchange()
                def oldest_scene():
                    def step(key):
                        before=client.capture().metadata['events_dequeued']
                        client.key(key)
                        return client.wait_for(lambda s:s.metadata['events_dequeued']>before)
                    shown=step('home')
                    for _ in range(12):
                        if 'ROW 00' in shown.text:
                            y=next(y for y,line in enumerate(shown.lines) if 'ROW 00' in line)
                            if y+1 < height:
                                check_blocks(shown,20,y)
                                check_native_font(shown,y)
                                return shown,y
                            shown=step('down')
                        else:
                            visible=[int(n) for n in re.findall(r'ROW (\d+)',shown.text)]
                            # A page may cross the first two scene rows. Step
                            # back toward ROW 00 rather than paging past it.
                            shown=step('up' if visible and min(visible)>0 else 'page_down')
                    raise AssertionError('original graphics row missing from history')
                client.command_key(48); client.wait_text('Scrollback')
                oldest,offset=oldest_scene()
                if graphics.startswith('RIP'):
                    assert rgb(oldest, 200, offset*ch) == 0xaa0000
                client.key('escape'); exchange()
                peer.shutdown(socket.SHUT_RDWR)
                client.wait_text('Disconnected: Enter exits, F1 details')
                client.command_key(48); client.wait_text('Scrollback')
                retained,offset=oldest_scene()
                client.key('escape'); client.wait_text('Disconnected: Enter exits, F1 details')
                client.key('enter')
            client.wait_text('Directory: Name')
            client.command_key(48); client.wait_text('Scrollback')
            offline,offset=oldest_scene()
            client.key('escape'); client.wait_text('Directory: Name')
            assert b'[wren' not in client.stderr, bytes(client.stderr)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('executable')
    parser.add_argument('--reports', default='reports/test-control')
    args = parser.parse_args()
    for graphics in ('Off', 'RIPv1', 'RIPv3', 'SkyPix (Kick 1.3)', 'SkyPix (Kick 3.1)'):
        for encoding in (('UTF-8',) if graphics.startswith('RIP') else ('CP437', 'UTF-8')):
            for status in (False, True):
                run(args.executable, args.reports, graphics, encoding, status)
