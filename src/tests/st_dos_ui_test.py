#!/usr/bin/env python3
"""Built-in DOS directory/forms against independent cell and persistence results."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import socket
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools/testing"))
from syncterm_fixture import SOURCE, fixture, application, click_text, write_script, install_suite, listener, cell_at

ENTRIES = """[Alpha]
Address=alpha.example
Port=2323
ConnectionType=Telnet
ScreenMode=80x25
Graphics=RIPv1
Comment=original notes
TotalCalls=42
LastConnected=1700000000
Added=1600000000
[Beta]
Address=beta.example
Port=22
ConnectionType=SSH
ScreenMode=80x25
Graphics=Off
"""


def open_program(client):
    client.key("f4")
    shot = client.wait_text("Program Settings")
    click_text(client, shot, "Program Settings")
    return client.wait_text("Main Interface")


def contracts(executable, reports):
    def prepare(root):
        write_script(root, "menu_dos_test", (SOURCE / "scripts/menu_dos_test.wren").read_text())
        install_suite(root, 'import "menu_dos_test" for DosTests\nDosTests.run()\n', "dos")
    with fixture(executable, entries=ENTRIES, prepare=prepare, reports=reports) as (client, root):
        client.wait_text("TEST-COMPLETE dos")
        assert b"DOS-CONTRACTS-PASS" in client.stderr


def directory(executable, reports):
    with fixture(executable, entries=ENTRIES, settings="MainUI=DOS\n", reports=reports) as (client, root):
        shot = client.wait_text("Term/Graphics")
        Path(reports).mkdir(parents=True, exist_ok=True)
        shot.save_image(Path(reports) / "directory-80.ppm")
        assert "alpha.example" in shot.text and "2323" in shot.text and "ANSI/RIP1" in shot.text, shot.text
        assert "original notes" in shot.lines[-3], shot.text
        assert not any(value in shot.lines[-3] for value in ("alpha.example", "2323", "ANSI/RIP1")), shot.text
        assert not any(label in shot.lines[0] for label in ("[ Edit ]", "[ Connect ]", "[ Settings ]")), shot.text
        # The single command bar retains mouse and keyboard access to actions.
        click_text(client, shot, "F4 Settings")
        shot = client.wait_text("Program Settings")
        click_text(client, shot, "Program Settings")
        client.wait_text("Main Interface")
        client.key("escape")
        shot = client.wait_text("Term/Graphics")
        assert "original notes" in shot.lines[-3], shot.text
        click_text(client, shot, "F2 Edit Item")
        client.wait_text("Edit Directory Entry")
        client.key("escape")
        client.wait_text("Term/Graphics")
        client.key("tab")
        client.key("right")
        client.key("right")
        client.key("enter")
        client.wait_text("Edit Directory Entry")
        client.key("escape")
        client.wait_text("Term/Graphics")
        client.key("f5")
        client.wait_text("F6 Paste")
        client.key("down")
        client.wait_for(lambda s: "Term/Graphics" in s.text and s.lines[-3].strip() == "")
        for columns, rows in ((132, 50), (40, 25), (80, 25)):
            client.grid_resize(columns, rows)
            shot = client.wait_for(lambda s: (s.metadata["columns"], s.metadata["rows"]) == (columns, rows) and "beta.example" in s.text)
            if columns == 132:
                shot.save_image(Path(reports) / "directory-132.ppm")
                assert "Encoding" in shot.text and "Calls" in shot.text and "Last Call" in shot.text, shot.text
            if columns == 40:
                assert "Name" in shot.text and "Address" in shot.text, shot.text
            assert shot.lines[-3].strip() == "", shot.text
        # Search addresses independently of display clipping or name search keys.
        client.key(102, 2)
        client.text("alpha.example")
        client.key("enter")
        client.key("f2")
        shot = client.wait_text("Edit Directory Entry")
        assert "alpha.example" in shot.text and "Connection" in shot.text, shot.text
        client.key("escape")
        shot = client.wait_text("Term/Graphics")
        assert "original notes" in shot.lines[-3], shot.text


def forms(executable, reports):
    with fixture(executable, entries=ENTRIES, settings="MainUI=DOS\n", reports=reports) as (client, root):
        client.wait_text("Term/Graphics")
        original = (root / "syncterm.lst").read_bytes()
        client.key("f2")
        shot = client.wait_text("Edit Directory Entry")
        Path(reports).mkdir(parents=True, exist_ok=True)
        y = next(y for y, line in enumerate(shot.lines) if "Alpha" in line)
        x = shot.lines[y].index("Alpha")
        assert cell_at(shot, x, y).background != cell_at(shot, 70, y).background, "focused field uses the active form palette"
        shot.save_image(Path(reports) / "entry-form.ppm")
        # Name is the first field after the category buttons.
        client.text("Changed")
        client.key("escape")
        client.wait_text("Discard changes?")
        client.key("enter")
        client.wait_text("Term/Graphics")
        assert (root / "syncterm.lst").read_bytes() == original
        client.key("f2")
        client.wait_text("Edit Directory Entry")
        client.text("Renamed")
        client.key("f10")
        client.wait_for(lambda s: "Term/Graphics" in s.text and "Renamed" in s.text)
        assert "[Renamed]" in (root / "syncterm.lst").read_text()
        shot = open_program(client)
        shot.save_image(Path(reports) / "program-form.ppm")
        # Cancelling program settings must not apply its draft.
        click_text(client, shot, "Confirm Program Exit")
        client.key("escape")
        client.wait_text("Discard changes?")
        client.key("enter")
        client.wait_text("Term/Graphics")
        assert "ConfirmClose=false" in (root / "syncterm.ini").read_text()


def switching(executable, reports):
    with fixture(executable, entries=ENTRIES, reports=reports) as (client, root):
        shot = client.wait_text("Program Settings")
        click_text(client, shot, "Program Settings")
        click_text(client, client.capture(), "Program Settings")
        shot = client.wait_text("Main Interface")
        click_text(client, shot, "Main Interface")
        client.wait_text("DOS")
        client.key("down")
        client.key("enter")
        client.key("escape")
        client.wait_text("Term/Graphics")
        assert "MainUI" in (root / "syncterm.ini").read_text()
        # The saved preference selects DOS again on a fresh process.
        client.close()
        with application(executable, root, reports=reports) as second:
            second.wait_text("Term/Graphics")
            shot = open_program(second)
            click_text(second, shot, "Main Interface")
            second.key("home")
            second.key("enter")
            second.key("f10")
            second.wait_text("SyncTERM Settings")
            assert "Term/Graphics" not in second.capture().text


def failures(executable, reports):
    with fixture(executable, entries=ENTRIES, settings="MainUI=DOS\n", reports=reports) as (client, root):
        client.wait_text("Term/Graphics")
        original = (root / "syncterm.lst").read_bytes()
        for cancel in (True, False):
            client.key("f2")
            client.wait_text("Edit Directory Entry")
            client.text("Retry Name")
            # Reflow while a text field is dirty, without committing it.
            for width in (132, 40, 80):
                client.grid_resize(width, 25)
                client.wait_for(lambda s: s.metadata["columns"] == width and "Retry Name" in s.text)
            path = root / "syncterm.lst"
            backup = root / "list-backup"
            path.rename(backup)
            path.mkdir()
            client.key("f10")
            client.wait_text("Save Failed")
            path.rmdir()
            backup.rename(path)
            client.key("enter")
            client.wait_text("Edit Directory Entry")
            if cancel:
                client.key("escape")
                client.wait_text("Discard changes?")
                client.key("enter")
                shot = client.wait_text("Term/Graphics")
                assert "Retry Name" not in shot.text and path.read_bytes() == original
            else:
                client.key("f10")
                client.wait_for(lambda s: "Term/Graphics" in s.text and "Retry Name" in s.text)
                assert "[Retry Name]" in path.read_text()
        shot = open_program(client)
        click_text(client, shot, "Main Interface")
        client.wait_text("Classic")
        client.key("home")
        client.key("enter")
        path = root / "syncterm.ini"
        backup = root / "ini-backup"
        path.rename(backup)
        path.mkdir()
        client.key("f10")
        client.wait_text("could not be saved")
        path.rmdir()
        backup.rename(path)
        client.key("enter")
        client.key("escape")
        client.wait_text("Discard changes?")
        client.key("enter")
        client.wait_text("Term/Graphics")
        assert "MainUI=DOS" in path.read_text()


def creation(executable, reports):
    with fixture(executable, entries=ENTRIES, settings="MainUI=DOS\n", reports=reports) as (client, root):
        client.wait_text("Term/Graphics")
        original = (root / "syncterm.lst").read_bytes()
        client.key("insert")
        client.wait_text("New Directory Entry")
        client.text("Gamma")
        client.key("tab")
        client.text("gamma.example")
        client.key("escape")
        client.wait_text("Discard changes?")
        client.key("enter")
        client.wait_text("Term/Graphics")
        assert (root / "syncterm.lst").read_bytes() == original
        client.key("insert")
        client.wait_text("New Directory Entry")
        client.text("Gamma")
        client.key("tab")
        client.text("gamma.example")
        client.key("f10")
        shot = client.wait_for(lambda s: "Term/Graphics" in s.text and "Gamma" in s.text)
        assert "gamma.example" in shot.text
        assert "[Gamma]" in (root / "syncterm.lst").read_text()


def connected(executable, reports):
    with listener() as server:
        entries = ("[Wire]\nAddress=127.0.0.1\nConnectionType=Raw\nScreenMode=80x25\n"
                   f"Port={server.getsockname()[1]}\nComment=connected notes\nNoStatus=true\nHidePopups=true\n")
        with fixture(executable, entries=entries, settings="MainUI=DOS\n", reports=reports) as (client, root):
            client.wait_text("Term/Graphics")
            client.key("enter")
            with server.accept()[0] as peer:
                peer.sendall(b"\x1b[2J\x1b[HREMOTE TERMINAL CONTENT")
                before = client.wait_text("REMOTE TERMINAL CONTENT")
                client.command_key(18)
                menu = client.wait_text("Term/Graphics")
                assert (menu.metadata["columns"], menu.metadata["rows"]) == (80, 25)
                assert "connected notes" in menu.lines[-3] and "127.0.0.1" not in menu.lines[-3], menu.text
                client.key("enter")  # Connected Enter edits instead of connecting.
                client.wait_text("Edit Directory Entry")
                client.text("Local edit")
                client.key("escape")
                client.wait_text("Discard changes?")
                client.key("enter")
                menu = client.wait_text("Term/Graphics")
                click_text(client, menu, "Esc Exit")
                after = client.wait_for(lambda s: "REMOTE TERMINAL CONTENT" in s.text and "Directory:" not in s.text)
                assert after.cells == before.cells
                assert after.application.get("session_active") == 1
                peer.settimeout(0.2)
                try:
                    received = peer.recv(1024)
                except socket.timeout:
                    received = b""
                assert received == b"", received
                # The selected UI can also be changed in the connected directory.
                client.command_key(18)
                client.wait_text("Term/Graphics")
                shot = open_program(client)
                click_text(client, shot, "Main Interface")
                client.wait_text("Classic")
                client.key("home")
                client.key("enter")
                client.key("f10")
                client.wait_text("SyncTERM Settings")
                client.key("escape")
                client.wait_text("REMOTE TERMINAL CONTENT")


def safe(executable, reports):
    with fixture(executable, entries=ENTRIES, settings="MainUI=DOS\n", extra=("-S",), reports=reports) as (client, root):
        client.wait_text("Term/Graphics")
        original_ini = (root / "syncterm.ini").read_bytes()
        original_list = (root / "syncterm.lst").read_bytes()
        client.key("insert")
        client.wait_text("Cannot edit list in safe mode")
        client.key("enter")
        client.wait_text("Term/Graphics")
        shot = open_program(client)
        click_text(client, shot, "Main Interface")
        client.wait_text("Classic")
        client.key("home")
        client.key("enter")
        client.key("f10")
        client.wait_text("SyncTERM Settings")
        assert (root / "syncterm.ini").read_bytes() == original_ini
        assert (root / "syncterm.lst").read_bytes() == original_list


def offer(executable, reports):
    def prepare(root):
        source = '\n'.join([
            'import "syncterm_menu" for Menu',
            'import "menu_dos_forms" for DosForms',
            'if (Menu.load(null) != null) Fiber.abort("offer load")',
            'var source = Menu.quickConnect("telnet://offer.example:2323")',
            'if (!DosForms.offerSave(source, "Offered")) Fiber.abort("offer save")',
        ])
        install_suite(root, source, "dos-offer")
    with fixture(executable, settings="MainUI=DOS\n", prepare=prepare, reports=reports) as (client, root):
        client.wait_text("New Directory Entry")
        client.key("f10")
        client.wait_text("TEST-COMPLETE dos-offer")
        contents = (root / "syncterm.lst").read_text()
        assert "[Offered]" in contents and "offer.example" in contents and "2323" in contents


def main():
    p = argparse.ArgumentParser()
    p.add_argument("executable")
    p.add_argument("case", choices=("contracts", "directory", "forms", "switching", "failures", "creation", "connected", "safe", "offer"))
    p.add_argument("--reports", default="reports/dos-ui")
    args = p.parse_args()
    globals()[args.case](args.executable, args.reports)

if __name__ == "__main__":
    main()
