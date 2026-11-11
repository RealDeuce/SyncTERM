#!/usr/bin/env python3
"""X8: requested/raw palette cycling over unchanged indexed scenes."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import socket

from st_igs_cursor_test import check as check_scene, fixture, initial

MAP = (0,15,1,2,4,6,3,5,7,8,9,10,12,14,11,13)
DAC = (0,36,73,109,146,182,219,255)
REQUEST = (0,142,285,428,571,714,857,1000)
WORDS = (8,1792,112,7,1911,801,291,1110,1620,1893,1,256,16,1365,273,819)
CAPTURES = 0


class Palette:
    def __init__(self, resolution):
        self.resolution = resolution
        self.mask = (15,3,1)[resolution]
        self.rgb = list(initial(resolution))
        self.raw = [0]*16
        self.requested = [[0]*3 for _ in range(16)]
        self.saved = [[0]*3 for _ in range(16)]
        self.saved_raw = [0]*16
        self.restore_raw = False

    def register(self, i, word):
        self.raw[i & 15] = word
        if self.resolution != 2:
            self.rgb[i & 15] = DAC[(word >> 8) & 7] << 16 | DAC[(word >> 4) & 7] << 8 | DAC[word & 7]
        elif i & 15 == 0:
            self.rgb[0] = 0xffffff if word & 1 else 0
            self.rgb[1] = self.rgb[0] ^ 0xffffff

    def pen(self, i, value):
        if not 0 <= i <= self.mask: return
        self.requested[i] = value[:]
        if self.resolution != 2:
            q = [sum(c >= t for t in (71,214,357,500,643,786,929)) for c in value]
            self.register(MAP[i] & self.mask, q[0]*256+q[1]*16+q[2])
        else:
            total = sum(0 if c < 142 else 1000 if c >= 858 else c for c in value)
            if total in (0,3000): self.register(0, i if total == 0 else (~i & 65535))

    def setup(self):
        wire = bytearray()
        for i in range(16):
            components = ((i+1)%8, (3*i+2)%8, (5*i+3)%8)
            wire += f'G#S {i},{components[0]},{components[1]},{components[2]}:'.encode()
            self.pen(i, [REQUEST[c] for c in components])
        for i, word in enumerate(WORDS):
            wire += f'G#X 1,{i},{word}:'.encode(); self.register(i,word)
        return wire

    def cycle(self, first, last, times, delay, key=False):
        raw = first > 15 or last > 15
        if raw:
            self.restore_raw = True
            first, last = ((v-15+32768)%65536-32768 for v in (first,last))
        if (first,last,times,delay) == (1,1,1,1):
            self.saved = [self.requested[min(i,self.mask)][:] for i in range(16)]
            self.saved_raw = [v & 0x777 for v in self.raw]
        if times == 0:
            for i in range(16):
                if self.restore_raw: self.register(i,self.saved_raw[i])
                else: self.pen(i,self.saved[i])
            self.restore_raw = False
            return
        first, last = (v if 0 <= v <= 15 else 15 for v in (first,last))
        if times < 0 or first == last: return
        cache = [0,0,0]
        def query(i):
            nonlocal cache
            if raw: return self.raw[i & 15] & 0x777
            if 0 <= i <= self.mask: cache = self.requested[i][:]
            return cache[:]
        for _ in range(1 if key else times+1):
            saved = query(last)
            step = -1 if first < last else 1
            for i in range(last,first+step,step):
                value = query(i+step)
                if raw: self.register(i,value)
                else: self.pen(i,value)
            # Final state is the inclusive permutation; intermediate writes
            # are checked separately by the native test's service model.
            if raw: self.register(first,saved)
            else: self.pen(first,saved)


def verify(frame, model, status, marker, active=False, scaling=0, writing=1):
    global CAPTURES
    state = frame.application
    check_scene(frame,model.resolution,status,4,'A',False,False,False,model.rgb)
    assert state['cterm.igs_draw_x'] == marker
    assert state['cterm.igs_scaling_mode'] == scaling
    assert state['cterm.igs_writing_mode'] == writing
    assert state['cterm.igs_palette_rotation_active'] == active
    assert state['cterm.igs_input_paused'] == active
    assert state['cterm.igs_palette_rotation_restore_raw'] == model.restore_raw
    raw = state['cterm.igs_raw_palette']
    assert [int(raw[4*i:4*i+4],16) for i in range(16)] == model.raw
    assert list(map(int,state['cterm.igs_requested_palette'].split(','))) == sum(model.requested,[])
    assert state['cterm.igs_text_counter'] == state['cterm.igs_text_deadline'] == 0
    if not active: assert state['cterm.igs_palette_rotation_remaining'] == 0
    CAPTURES += 1


def run(executable,reports,resolution,encoding,status):
    screen = ('AtariST40x25','AtariST80x25','AtariST80x25Mono')[resolution]
    with socket.socket() as listener:
        listener.bind(('127.0.0.1',0)); listener.listen(2); listener.settimeout(15)
        entries = ('[IGS palette cycle]\nConnectionType=Raw\nAddress=127.0.0.1\n'
                   f'Port={listener.getsockname()[1]}\nGraphics=IGS (2.21)\nScreenMode={screen}\n'
                   f'Font=Atari ST\nEncoding={encoding}\nNoStatus={str(not status).lower()}\n')
        with fixture(executable,entries=entries,settings='AudioModes=0\nMidiModes=0\nBlockyScaling=true\n',
                     reports=reports,quiet=False) as (client,root):
            client.wait_text('IGS palette cycle'); client.key('enter')
            model, marker = Palette(resolution), 1000
            setup = b'\x1bfG#c>1,3:c 0,0:\x1bEG#p 3,2:A'
            def send(peer,wire):
                with (root/'wire.bin').open('ab') as log: log.write(wire)
                peer.sendall(wire)
            def fence(): return f'G#g>0:X 10,{marker},777:'.encode()
            def captured():
                return client.wait_for(lambda f: f.application.get('cterm.igs_draw_x') == marker)
            def disconnect(peer):
                peer.shutdown(socket.SHUT_RDWR)
                client.wait_text('Disconnected: Enter exits, F1 details'); client.key('enter'); client.wait_text('Directory: Name')
            with listener.accept()[0] as peer:
                client.wait_for(lambda f: f.application.get('connected') == 1); client.resize(1280,960)
                send(peer,setup+model.setup()+fence()); verify(captured(),model,status,marker)
                steps = ((1,1,1,1), (1,3,1,1), (0,0,0,9999), (16,16,1,1),
                         (18,15,2,0), (0,2,1,2), (0,0,0,0), (0,0,0,0),
                         (1,16,1,0), (1,1,1,1), (15,30,1,0), (0,0,0,0))
                for first,last,times,delay in steps:
                    marker += 1; model.cycle(first,last,times,delay)
                    send(peer,f'G#g>1:X 8,{first},{last},{times},{delay}:'.encode()+fence())
                    verify(captured(),model,status,marker)
                # Queue another command, then cancel during the first post-pass
                # delay. The queued command must still be pending in this frame.
                send(peer,b'G#g>1:X>8,1,3,9999,9999:M 2:'+f'G#g>0:X 10,{marker+1},777:'.encode())
                model.cycle(1,3,9999,9999,key=True)
                frame = client.wait_for(lambda f: f.application.get('cterm.igs_palette_rotation_delaying') == 1)
                verify(frame,model,status,marker,active=True,scaling=1)
                client.key(ord('a')); marker += 1
                verify(captured(),model,status,marker,writing=2)
                peer.settimeout(0.03)
                try: reply = peer.recv(1)
                except socket.timeout: pass
                else: raise AssertionError(f'unexpected X8 reply/key: {reply!r}')
                # Disconnect in another active cycle. No queued command or
                # snapshot/mode may survive in the fresh session.
                send(peer,b'G#X 8,16,18,9999,9999:')
                client.wait_for(lambda f: f.application.get('cterm.igs_palette_rotation_active') == 1)
                disconnect(peer)
            client.key('enter')
            with listener.accept()[0] as peer:
                client.wait_for(lambda f: f.application.get('connected') == 1); client.resize(1280,960)
                model = Palette(resolution); marker += 1
                wire = setup+model.setup()+b'G#X 8,0,0,0,0:'; model.cycle(0,0,0,0)
                send(peer,wire+fence()); verify(captured(),model,status,marker)
                disconnect(peer)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('executable')
    parser.add_argument('--reports',default='reports/test-control'); args = parser.parse_args()
    for resolution in range(3):
        for encoding in ('ATARI-ST','CP437','UTF-8'):
            for status in (False,True): run(args.executable,args.reports,resolution,encoding,status)
    assert CAPTURES == 288, CAPTURES
