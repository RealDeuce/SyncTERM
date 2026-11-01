#!/usr/bin/env python3
"""Emoji picker UI and connected gating through coherent control captures."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/testing'))
from syncterm_fixture import fixture, install_suite, click_text, click_cell, write_script, SOURCE
from syncterm_control import ALT
from st_migrated_control_test import connection
from st_terminal_conformance_test import Terminal, ESC


def alt_key(client, evdev):
    client.physical_key(56, True, ALT)
    client.physical_key(evdev, True, ALT)
    client.physical_key(evdev, False, ALT)
    client.physical_key(56, False)


def break_picker(root):
    broken = (SOURCE / 'scripts/ui_emoji_picker.wren').read_text().replace(
        '    super.onPaint_()', '    super.onPaint_()\n    Fiber.abort("picker paint fixture")')
    write_script(root, 'ui_emoji_picker', broken)


def menu(executable, reports):
    source = '''
import "syncterm" for Screen, Key, Emoji, Host
import "ui_app" for App
import "ui_input" for TextInput
import "ui_pane" for Pane
import "ui_widget" for Rect
var app = App.new()
app.onError = Fn.new {|fiber| Host.print("EMOJI ERROR: %(fiber.error)") }
var pane = Pane.new()
pane.title = "Emoji input fixture"
pane.bounds = Rect.new(1, 1, 40, 7)
pane.focused = true
var field = TextInput.new()
field.bounds = Rect.new(3, 4, 34, 1)
field.value = "ab"
field.cursor = 1
field.maxLen = 8
pane.add(field)
app.root.add(pane)
app.root.focusedIndex = 0
pane.focusedIndex = 0
app.bind(Key.escape, Fn.new { app.quit() })
app.runSync()
'''
    def prepare(root):
        install_suite(root, source, 'emoji')
    with fixture(executable, settings='AudioModes=0\n', prepare=prepare, reports=reports) as (client, root):
        client.wait_text('Emoji input fixture')
        alt_key(client, 25)
        client.wait_text('Insert Emoji')
        client.text('grinning face')
        client.wait_text('grinning face')
        client.key('enter')
        frame = client.wait_text('a😀b')
        assert 'Insert Emoji' not in frame.text
        assert '😀' in (root / 'syncterm.ini').read_text()
        alt_key(client, 25)
        frame = client.wait_text('Recently used')
        client.text('no such emoji zzqq')
        client.wait_text('No matching emoji')
        client.key('escape')
        client.wait_text('a😀b')
        # Exact gender/tone variants are searchable and remain a single glyph.
        alt_key(client, 25)
        client.wait_text('Insert Emoji')
        client.text('MAN SHRUGGING: MEDIUM SKIN TONE')
        frame = client.wait_text('man shrugging: medium skin tone')
        # The reverse glyph map shares artwork with the spelling without VS16.
        assert any(frame.cell_text(c) == '🤷🏽‍♂' for c in frame.cells)
        index = next(i for i, c in enumerate(frame.cells) if frame.cell_text(c) == '🤷🏽‍♀')
        m = frame.metadata
        x, y = index % m['columns'], index // m['columns']
        px = m['destination_x'] + (x * m['cell_width'] + m['cell_width'] // 2) * m['destination_width'] // m['plane_width']
        py = m['destination_y'] + (y * m['cell_height'] + m['cell_height'] // 2 + m['pixel_top']) * m['destination_height'] // m['plane_height']
        client.mouse(px, py)
        frame = client.wait_text('woman shrugging: medium skin tone')
        male = next(i for i, c in enumerate(frame.cells) if frame.cell_text(c) == '🤷🏽‍♂')
        click_cell(client, frame, male % m['columns'], male // m['columns'])
        frame = client.wait_text('a😀🤷🏽‍♂b')
        assert '🤷🏽‍♂️' in (root / 'syncterm.ini').read_text()
        alt_key(client, 25)
        client.wait_text('Insert Emoji')
        client.text('grinning face')
        client.key('enter')
        frame = client.wait_text('That emoji does not fit in this field.')
        assert sum('grinning face' in line for line in frame.lines) >= 2
        client.key('escape')
        client.wait_text('a😀🤷🏽‍♂b')
        # Reopening, then F2 and Escape, returns to the exact recent variant.
        alt_key(client, 25)
        client.wait_text('Recently used')
        client.key('f2')
        client.wait_text('Variants')
        client.key('right')
        client.key('escape')
        client.wait_text('man shrugging: medium skin tone')
        # Compact layouts retain a wrapped highlighted name and clickable actions.
        client.grid_resize(40, 14)
        frame = client.wait_for(lambda s: s.metadata['columns'] == 40 and 'man shrugging:' in s.text)
        assert 'medium skin tone' in frame.text
        click_text(client, frame, 'Esc Back')
        client.wait_text('a😀🤷🏽‍♂b')
        client.key('escape')
        client.wait_text('TEST-COMPLETE emoji')

    # A failed picker paint must pop the modal and restore its mouse mask.
    def prepare_failure(root):
        install_suite(root, source.replace(
            'app.onError = Fn.new {|fiber| Host.print("EMOJI ERROR: %(fiber.error)") }',
            '''var mouse = null
app.bind(Key.altP, Fn.new {
  mouse = Input.mouseEvents
  app.openEmojiPicker()
})
app.onError = Fn.new {|fiber|
  if (Input.mouseEvents != mouse) Fiber.abort("picker leaked mouse mask")
  Host.print("EXPECTED EMOJI ERROR: %(fiber.error)")
}''').replace('Emoji, Host', 'Emoji, Host, Input'), 'emoji')
        break_picker(root)
    with fixture(executable, settings='AudioModes=0\n', prepare=prepare_failure, reports=reports) as (client, root):
        client.wait_text('Emoji input fixture')
        alt_key(client, 25)
        client.text('!')
        client.wait_text('a!b')
        assert b'EXPECTED EMOJI ERROR: picker paint fixture' in client.stderr
        client.key('escape')
        client.wait_text('TEST-COMPLETE emoji')


def connected(executable, reports):
    for encoding in ('UTF-8', 'CP437', 'CP1251'):
        with connection(executable, reports, encoding=encoding, settings='AudioModes=0\n') as (client, root, peer, send):
            terminal = Terminal(client, root, peer, send)
            terminal.case('emoji-picker-' + encoding)
            terminal.exchange(b'LIVE', reset=True)
            client.key(16)
            terminal.input(b'\x10')
            if encoding != 'UTF-8':
                alt_key(client, 25)
                # Fence the UI without requiring a particular remote Alt-P encoding.
                client.text('~')
                peer.settimeout(10)
                wire = b''
                while not wire.endswith(b'~'):
                    wire += peer.recv(256)
                frame = terminal.exchange()
                assert 'Insert Emoji' not in frame.text
                alt_key(client, 44)
                frame = client.wait_text('SyncTERM Online Menu')
                assert 'Insert Emoji...' not in frame.text
                client.key('escape')
                # A local edit field reached through Alt-E is UTF-8 even
                # while its parked terminal connection uses a codepage.
                alt_key(client, 18)
                client.wait_text('Directory: Name')
                client.key('f2')
                client.wait_text('Edit Directory Entry')
                client.key('home')
                client.key('enter')
                client.wait_text('Capture')
                alt_key(client, 25)
                client.wait_text('Insert Emoji')
                client.text('grinning face')
                client.key('enter')
                client.wait_text('😀')
                client.key('escape')
                client.wait_text('Edit Directory Entry')
                client.key('escape')
                client.wait_text('Directory: Name')
                client.key('escape')
                terminal.exchange()
                continue
            alt_key(client, 25)
            client.wait_text('Insert Emoji')
            client.text('grinning face')
            client.wait_text('grinning face')
            client.key('enter')
            terminal.input('😀'.encode())
            assert '😀' in (root / 'syncterm.ini').read_text()
            frame = terminal.exchange()
            assert 'LIVE' in frame.text and 'Insert Emoji' not in frame.text
            # Reopening starts on the recent tile; Right reaches the same
            # emoji's base tile, rather than skipping ahead in the base grid.
            alt_key(client, 25)
            client.wait_text('Recently used')
            client.key('right')
            client.key('enter')
            terminal.input('😀'.encode())
            # The online menu provides the same entry.
            alt_key(client, 44)
            client.wait_text('Insert Emoji...')
            client.key('escape')
            terminal.exchange()
            alt_key(client, 25)
            client.wait_text('Insert Emoji')
            client.text('woman shrugging: dark skin tone')
            client.wait_text('woman shrugging: dark skin tone')
            client.key('enter')
            terminal.input('🤷🏿‍♀️'.encode())

    # A failed connected picker must release output suspension and input.
    with connection(executable, reports, encoding='UTF-8', settings='AudioModes=0\n',
                    prepare=break_picker) as (client, root, peer, send):
        terminal = Terminal(client, root, peer, send)
        terminal.exchange(b'BEFORE', reset=True)
        alt_key(client, 25)
        client.key(16)
        terminal.input(b'\x10')
        frame = terminal.exchange(b'AFTER')
        assert 'BEFOREAFTER' in frame.text and 'Insert Emoji' not in frame.text


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('executable', type=Path)
    parser.add_argument('group', choices=('menu', 'connected'))
    parser.add_argument('--reports', type=Path, required=True)
    args = parser.parse_args()
    (menu if args.group == 'menu' else connected)(args.executable, args.reports)

if __name__ == '__main__':
    main()
