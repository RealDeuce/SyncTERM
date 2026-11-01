#!/usr/bin/env python3
"""Exercise the actual external frontend through the control bitmap backend."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import importlib.util
from pathlib import Path
import re
import sys
import threading
from http.server import HTTPServer, BaseHTTPRequestHandler

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT.parents[1]
sys.path.insert(0, str(PROJECT / 'tools/testing'))
from syncterm_fixture import fixture, application, click_text, click_cell, listener, receive
from syncterm_control import ALT

spec = importlib.util.spec_from_file_location('modern_install', ROOT / 'install.py')
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)

ENTRIES = '[Alpha]\nAddress=alpha.example\nComment=First connection\n[München]\nAddress=second.example\n[Zulu]\nAddress=zulu.example\n'


def install(root):
    installer.install(root / 'scripts')


def alt_key(client, evdev):
    client.physical_key(56, True, ALT)
    client.physical_key(evdev, True, ALT)
    client.physical_key(evdev, False, ALT)
    client.physical_key(56, False)


def directory(executable, reports):
    with fixture(executable, entries=ENTRIES, settings='AudioModes=0\n', prepare=install,
                 reports=reports) as (client, root):
        def painted(frame):
            # Wait for the post-composition raster chrome as well as text;
            # inspect both from the same coherent capture.
            m = frame.metadata
            corner = 8 * m['cell_height'] * m['plane_width'] + 19 * m['cell_width']
            return 'Connection directory' in frame.text and frame.primary[corner] == 0x80f1f5f9
        frame = client.wait_for(painted)
        assert 'MODERN UI' in frame.text and 'Alpha' in frame.text
        reports.mkdir(parents=True, exist_ok=True)
        frame.save_image(reports / 'directory.ppm')
        click_text(client, frame, 'Zulu')
        frame = client.wait_text('zulu.example')
        assert frame.application['session_active'] == 0
        client.key(6)
        client.text('SECOND.EXAMPLE')
        frame = client.wait_text('1 connections')
        assert 'München' in frame.text and 'Zulu' not in frame.text
        alt_key(client, 25)
        client.wait_text('Insert Emoji')
        client.key('escape')
        client.key('escape')
        client.wait_text('3 connections')
        # Cancel a new record: its creation/deletion must not leave stale handles.
        client.key('insert')
        client.wait_text('New connection')
        client.text('Cancelled')
        client.key('enter')
        client.wait_text('Connection Type')
        client.key('escape')
        frame = client.wait_text('3 connections')
        assert 'Error:' not in frame.text and 'Cancelled' not in frame.text
        assert 'Cancelled' not in (root / 'syncterm.lst').read_text(encoding='utf-8')
        client.key('insert')
        client.wait_text('New connection')
        client.text('Created')
        client.key('enter')
        client.wait_text('Connection Type')
        client.key('enter')
        client.wait_text('Address / device / command')
        client.text('created.example')
        client.key('enter')
        client.wait_text('4 connections')
        saved = (root / 'syncterm.lst').read_text(encoding='utf-8')
        assert '[Created]' in saved and 'created.example' in saved
        # A directory mutation expires old handles; filtering remains usable.
        client.key(6)
        client.text('Alpha')
        frame = client.wait_text('1 connections')
        click_text(client, frame, 'Delete')
        client.wait_text('Delete Alpha?')
        client.key('enter')
        client.wait_text('0 connections')
        client.key('escape')
        client.wait_text('3 connections')
        assert '[Alpha]' not in (root / 'syncterm.lst').read_text(encoding='utf-8')


def settings(executable, reports):
    with fixture(executable, entries=ENTRIES, settings='AudioModes=0\n', prepare=install,
                 reports=reports) as (client, root):
        frame = client.wait_text('Connection directory')
        click_text(client, frame, 'Settings')
        frame = client.wait_text('Choose a settings category')
        reports.mkdir(parents=True, exist_ok=True)
        frame.save_image(reports / 'settings.ppm')
        click_text(client, frame, 'Program Settings')
        frame = client.wait_text('Prompt to Save')
        click_text(client, frame, 'Prompt to Save')
        client.key('escape')
        frame = client.wait_text('Choose a settings category')
        assert 'PromptSave=false' in (root / 'syncterm.ini').read_text(encoding='utf-8')
        click_text(client, frame, 'Licenses')
        client.wait_text('Unicode data')
        client.key('escape')
        client.wait_text('Choose a settings category')
        client.key('escape')
        frame = client.wait_text('Connection directory')
        assert 'Alpha' in frame.text and 'Error:' not in frame.text
        client.close()
        with application(executable, root, reports=reports) as restarted:
            frame = restarted.wait_text('Connection directory')
            click_text(restarted, frame, 'Settings')
            frame = restarted.wait_text('Choose a settings category')
            click_text(restarted, frame, 'Program Settings')
            frame = restarted.wait_text('Prompt to Save')
            assert any('Prompt to Save' in line and 'No' in line for line in frame.lines)
            restarted.key('escape')


def connection(executable, reports):
    with listener() as server:
        entries = f'[Local]\nConnectionType=Raw\nAddress=127.0.0.1\nPort={server.getsockname()[1]}\n'
        with fixture(executable, entries=entries, settings='AudioModes=0\n', prepare=install,
                     reports=reports) as (client, root):
            frame = client.wait_text('Connection directory')
            for y, row in enumerate(frame.lines):
                match = re.search(r'\bConnect\b', row)
                if match:
                    click_cell(client, frame, match.start() + 2, y)
                    break
            with server.accept()[0] as peer:
                peer.sendall(b'ADDON-SESSION')
                client.wait_text('ADDON-SESSION')
                alt_key(client, 18)
                frame = client.wait_text('SESSION DIRECTORY')
                assert 'Edit entry' in frame.text and 'Local' in frame.text
                click_text(client, frame, 'Return')
                client.wait_text('ADDON-SESSION')
                client.text('returned')
                receive(peer, b'returned')
            client.wait_text('Connection directory')


def recovery(executable, reports):
    with fixture(executable, settings='AudioModes=0\n', prepare=install, reports=reports) as (client, root):
        frame = client.wait_text('Your directory is empty.')
        assert '0 connections' in frame.text
        client.key('f1')
        client.wait_text('Use Directory and Settings')
        client.key('escape')
        client.key(0x29e0)
        client.wait_text('(main_menu) wren>')
        client.key('escape')
        client.wait_text('Your directory is empty.')
        client.close()
        installer.uninstall(root / 'scripts')
        with application(executable, root, reports=reports) as restored:
            frame = restored.wait_text('Directory: Name')
            assert 'MODERN UI' not in frame.text
    with fixture(executable, settings='AudioModes=0\nScreenMode=Custom\nCustomRows=18\nCustomColumns=40\nCustomFontHeight=16\n',
                 prepare=install, reports=reports) as (client, root):
        frame = client.wait_text('Modern UI needs 72 columns')
        assert frame.metadata['columns'] == 40
        client.key('enter')
        frame = client.wait_text('Connection directory')
        assert frame.metadata['columns'] == 80 and frame.metadata['rows'] == 25
    with fixture(executable, entries=ENTRIES, settings='AudioModes=0\n', extra=('-S',),
                 prepare=install, reports=reports) as (client, root):
        frame = client.wait_text('Connection directory')
        assert 'SAFE MODE' in frame.text
        original = (root / 'syncterm.lst').read_bytes()
        client.key('insert')
        client.wait_text('Directory changes are disabled')
        client.key('enter')
        client.wait_text('Connection directory')
        assert (root / 'syncterm.lst').read_bytes() == original


def web_list(executable, reports):
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            body = b'[Fetched web entry]\nAddress=downloaded.example\n'
            self.send_response(200)
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        def log_message(self, *args):
            pass
    server = HTTPServer(('127.0.0.1', 0), Handler)
    worker = threading.Thread(target=server.serve_forever, kwargs={'poll_interval': 0.05})
    worker.start()
    try:
        with fixture(executable, entries=ENTRIES, settings='AudioModes=0\n', prepare=install,
                     reports=reports) as (client, root):
            frame = client.wait_text('Connection directory')
            click_text(client, frame, 'Settings')
            frame = client.wait_text('Choose a settings category')
            click_text(client, frame, 'Web Lists')
            client.wait_text('INS Add')
            client.key('enter')
            client.wait_text('Add Web List')
            client.key('enter')
            client.wait_text('http://syncterm.bbsdev.net/syncterm.lst')
            client.text(f'http://127.0.0.1:{server.server_port}/list')
            client.key('enter')
            client.wait_for(lambda s: 'SyncTERM BBS List' in s.text and 'Add Web List' not in s.text)
            client.key('escape')
            frame = client.wait_for(lambda s: 'Choose a settings category' in s.text
                                   and 'SyncTERM BBS List' not in s.text)
            click_text(client, frame, 'Directory')
            frame = client.wait_text('4 connections')
            assert 'Fetched web entry' in frame.text and 'Error:' not in frame.text
            assert '[WebLists]' in (root / 'syncterm.ini').read_text(encoding='utf-8')
    finally:
        server.shutdown()
        server.server_close()
        worker.join(timeout=5)
        assert not worker.is_alive()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('executable', type=Path)
    parser.add_argument('case', choices=('directory', 'settings', 'connection', 'recovery', 'web_list'))
    parser.add_argument('--reports', type=Path, required=True)
    args = parser.parse_args()
    globals()[args.case](args.executable, args.reports)


if __name__ == '__main__':
    main()
