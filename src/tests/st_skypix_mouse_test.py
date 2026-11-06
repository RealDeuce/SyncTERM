#!/usr/bin/env python3
"""Independent JR-Comm click bytes, native pixel mapping and local mouse gate."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import socket
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'testing'))
from syncterm_fixture import fixture, click_text

SCRIPT = r'''
import "syncterm" for Hook, Key, CTerm, Conn
Hook.onKey(Key.f8) {|event|
  Conn.sendBytes("STATE %(CTerm.mouseMode) %(CTerm.mouseDisabled) %(CTerm.mouseSgrPixels)\n")
  return true
}
'''


def run(executable, reports, kick, encoding, status):
    print(kick, encoding, 'status', status, flush=True)
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        listener.listen(1)
        listener.settimeout(15)
        entries = ('[SkyPix pointer]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                   f'Port={listener.getsockname()[1]}\nGraphics=SkyPix (Kick {kick})\n'
                   f'Encoding={encoding}\nNoStatus={str(not status).lower()}\n'
                   'ScreenMode=Amiga640x200\nAmigaFont=Diamond.font\nAmigaFontSize=20\nHidePopups=false\n')
        with fixture(executable, entries=entries, settings='AudioModes=0\n',
                     script=SCRIPT, reports=reports) as (client, root):
            client.wait_text('SkyPix pointer')
            client.key('enter')
            with listener.accept()[0] as peer:
                peer.settimeout(10)
                client.wait_for(lambda s: s.application.get('connected') == 1)

                def read_to(suffix):
                    result = bytearray()
                    while not result.endswith(suffix):
                        part = peer.recv(1)
                        assert part, result
                        result.extend(part)
                    return bytes(result)

                def exchange(data=b'', prefix=b''):
                    peer.sendall(data + b'\x1b[5n')
                    assert read_to(b'\x1b[0n') == prefix + b'\x1b[0n'
                    return client.capture()

                def probe(disabled):
                    client.key('f8')
                    assert read_to(b'\n') == f'STATE 2 {str(disabled).lower()} false\n'.encode()

                def indicator(disabled):
                    if not status:
                        return
                    colour = 0x545454 if disabled else 0xffff54
                    def ready(s):
                        for x, char in enumerate(s.lines[-1]):
                            if char != 'M':
                                continue
                            m = s.metadata
                            pixels = set()
                            for y in range(8):
                                for px in range(8):
                                    value = s.primary[((m['rows']-1)*8+y)*m['plane_width']+x*8+px]
                                    pixels.add((value if value & 0x80000000 else s.palette[value]) & 0xffffff)
                            if colour in pixels and pixels <= {colour, 0x0000a8}:
                                return True
                        return False
                    client.wait_for(ready)

                def mouse(px, py, buttons=0, action=1, wheel_y=0):
                    shown = client.capture()
                    m = shown.metadata
                    # Select the centre of the desired native raster pixel,
                    # accounting for the canvas's non-square pixel aspect.
                    x = m['destination_x'] + (2*px+1)*m['destination_width']//(2*m['plane_width'])
                    y = m['destination_y'] + (2*py+1)*m['destination_height']//(2*m['plane_height'])
                    before = m['events_dequeued']
                    client.mouse(x, y, action=action, buttons=buttons, wheel_y=wheel_y)
                    return client.wait_for(lambda s: s.metadata['events_dequeued'] > before)

                probe(False)
                indicator(False)
                exchange(b'\x1b[2J\x1b[H')
                # The exact documented report needs no enable/gadget command,
                # no cell rounding, no one-based offset and no appended CR/LF.
                mouse(317, 143, 1)
                assert read_to(b'!') == b'\x1b[21;1;317;143!'
                mouse(318, 144, 1, action=0)
                mouse(318, 144, 1)
                mouse(318, 144, 0)
                mouse(100, 100, 2)
                mouse(100, 100, 0)
                mouse(100, 100, 4)
                mouse(100, 100, 0)
                exchange()
                # Command 22 only retains a record and paints an inclusive
                # foreground rectangle. Defining it sends no acknowledgment;
                # inside/outside clicks still report raw controller-1 pixels.
                shown = exchange(b'\x1b[15;3!\x1b[22;1;19;310;140;320;146!')
                def pixel(s, x, y, alternate=False):
                    m = s.metadata
                    plane = s.alternate if alternate else s.primary
                    value = plane[(y+m['pixel_top'])*m['plane_width']+x]
                    return (value if value & 0x80000000 else s.palette[value]) & 0xffffff
                for y in range(139,148):
                    for x in range(309,322):
                        expected = 0xff0000 if 310<=x<=320 and 140<=y<=146 else 0
                        assert pixel(shown,x,y)==expected,(kick,encoding,x,y)
                        assert pixel(shown,x,y,True)==expected,(kick,encoding,x,y,'alternate')
                mouse(317,143,1)
                assert read_to(b'!') == b'\x1b[21;1;317;143!'
                mouse(317,143,0)
                mouse(101,99,1)
                assert read_to(b'!') == b'\x1b[21;1;101;99!'
                mouse(101,99,0)
                shown = exchange(b'\x1b[15;4!\x1b[22;1;0;12;20;14;22!'
                                 b'\x1b[22;20;65535;639;199;639;199!'
                                 b'\x1b[22;0;1;10;20;14;24!\x1b[22;21;1;10;20;14;24!')
                assert pixel(shown,317,143)==0xff0000
                for y in range(19,24):
                    for x in range(11,16):
                        expected = 0x00ff11 if 12<=x<=14 and 20<=y<=22 else 0
                        assert pixel(shown,x,y)==expected,(kick,encoding,x,y)
                        assert pixel(shown,x,y,True)==expected,(kick,encoding,x,y,'alternate')
                assert pixel(shown,639,199)==0x00ff11
                probe(False)
                indicator(False)
                # Toggle while left is held; its disabled release must still
                # synchronize button state for the next enabled press.
                mouse(17, 19, 1)
                assert read_to(b'!') == b'\x1b[21;1;17;19!'
                client.command_key(24)  # Alt/Command-O
                probe(True)
                indicator(True)
                mouse(17, 19, 0)
                mouse(100, 100, 1)
                mouse(100, 100, 0)
                exchange()
                client.command_key(24)
                probe(False)
                indicator(False)
                # Native coordinate mapping remains exact at another drawable
                # size; the connection font must not affect pixel reports.
                client.resize(1280, 1000)
                exchange(b'\x1b[?1000h\x1b[?1006h\x1b[?1016h')
                probe(False)
                mouse(317, 143, 1)
                assert read_to(b'!') == b'\x1b[21;1;317;143!'
                mouse(317, 143, 0)
                if status:
                    mouse(10, 201, 1)
                    mouse(10, 201, 0)
                exchange()
                client.command_key(44)  # Alt/Command-Z
                menu = client.wait_text('SyncTERM Online Menu')
                # UI-handled clicks must not leak controller messages.
                click_text(client, menu, 'Toggle Remote Mouse')
                # The modal input claim can still own queued keys while it
                # unwinds. Fence its completion before sending the probe key.
                exchange()
                probe(True)
                indicator(True)
                client.command_key(24)
                probe(False)
                # A protocol with no wheel reports keeps local scrollback.
                mouse(100, 100, action=2, wheel_y=1)
                client.wait_text('Scrollback')
                client.key('escape')
                exchange()
                mouse(0, 0, 1)
                assert read_to(b'!') == b'\x1b[21;1;0;0!'
                mouse(0, 0, 0)
                exchange(b'\x1bc')
                probe(False)
                indicator(False)
                assert b'[wren' not in client.stderr, bytes(client.stderr)
                peer.shutdown(socket.SHUT_RDWR)
            client.wait_text('Directory: Name')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('executable')
    parser.add_argument('--reports', default='reports/test-control')
    args = parser.parse_args()
    for kick in ('1.3', '3.1'):
        for encoding in ('CP437', 'UTF-8'):
            for status in (False, True):
                run(args.executable, args.reports, kick, encoding, status)
