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


def program_layout(client, shot, reports):
    def selected_tab(snapshot, name):
        assert f"[{name}]" in "\n".join(snapshot.lines[:6]), snapshot.text
        assert name not in "\n".join(snapshot.lines[-4:]), snapshot.text

    def boxed_number(snapshot, value):
        y = next(y for y, line in enumerate(snapshot.lines) if "Scrollback Buffer" in line)
        line = snapshot.lines[y]
        assert value in line, snapshot.text  # Label and editor share one row.
        x = line.index(value)
        assert "[" in line[:x] and "]" in line[x + len(value):], line
        right = line.index("]", x)
        assert cell_at(snapshot, x, y).background == cell_at(snapshot, right - 1, y).background
        assert cell_at(snapshot, x, y).background != cell_at(snapshot, 0, y).background

    selected_tab(shot, "General")
    boxed_number(shot, "2000")  # The unfocused editor has a visible rectangle.
    y = next(y for y, line in enumerate(shot.lines) if "[General]" in line)
    assert cell_at(shot, shot.lines[y].index("General"), y).background != cell_at(shot, shot.lines[y].index("Display"), y).background
    selected_background = cell_at(shot, shot.lines[y].index("General"), y).background
    # Keyboard focus can visit another tab without changing the current page.
    client.key("tab", 1)
    shot = client.wait_text(">[General]")
    client.key("end")
    shot = client.wait_text("> Colours")
    assert "[General]" in shot.lines[y] and "Main Interface" in shot.text, shot.text
    focus_background = cell_at(shot, shot.lines[y].index("Colours"), y).background
    assert selected_background == cell_at(shot, shot.lines[y].index("General"), y).background, shot.text
    assert focus_background != selected_background, shot.text
    shot.save_image(Path(reports) / "program-tabs.ppm")
    client.key("home")
    shot = client.wait_text(">[General]")
    client.key("right")
    shot = client.wait_text("> Display")
    assert "[General]" in shot.lines[y] and "Main Interface" in shot.text, shot.text
    client.key("end")
    client.wait_text("> Colours")
    client.key("enter")
    shot = client.wait_text("[Colours]")
    assert "Frame Colour" in shot.text, shot.text
    client.key("tab", 1)
    shot = client.wait_text(">[Colours]")
    assert cell_at(shot, shot.lines[y].index("Colours"), y).background == focus_background, shot.text
    client.key("page_down", 2)
    shot = client.wait_text("Main Interface")
    selected_tab(shot, "General")
    click_text(client, shot, "2000")
    client.text("4096")
    client.key("tab")
    shot = client.wait_text("4096")
    boxed_number(shot, "4096")  # Losing focus keeps the entire field visible.
    for columns, rows in ((132, 50), (40, 25), (40, 14), (80, 25)):
        client.grid_resize(columns, rows)
        shot = client.wait_for(lambda s: (s.metadata["columns"], s.metadata["rows"]) == (columns, rows) and "[General]" in s.text)
        if rows == 14:
            # A pending edit hidden by reflow still requires discard approval.
            client.key("escape")
            client.wait_text("Discard changes?")
            client.key("escape")
            shot = client.wait_text("[General]")
            # Overflow navigation stays beside the tabs, within this category.
            click_text(client, shot, "Next")
            shot = client.wait_text("4096")
            y = next(y for y, line in enumerate(shot.lines) if "Prev" in line)
            assert y < 7, shot.text
            client.key("page_up")
            shot = client.wait_text("Main Interface")
            selected_tab(shot, "General")
        else:
            boxed_number(shot, "4096")
            shot.save_image(Path(reports) / f"program-form-{columns}.ppm")
        selected_tab(shot, "General")
    click_text(client, shot, "Display")
    shot = client.wait_text("Startup Screen Mode")
    selected_tab(shot, "Display")
    client.key("tab", 1)
    client.wait_text(">[Display]")
    # Wrapped tabs keep focus on the same tab across reflow. Home/End act
    # on the complete strip, rather than just the current visual row.
    client.grid_resize(40, 25)
    client.wait_for(lambda s: s.metadata["columns"] == 40 and ">[Display]" in s.text)
    client.key("home")
    shot = client.wait_text("> General")
    assert "[Display]" in shot.text, shot.text
    client.key("end")
    shot = client.wait_text("> Colours")
    assert "[Display]" in shot.text, shot.text
    client.grid_resize(80, 25)
    client.wait_for(lambda s: s.metadata["columns"] == 80 and "> Colours" in s.text)
    # Tab leaves the strip as one stop; either direction re-enters at Display.
    client.key("tab")
    client.wait_for(lambda s: "> Colours" not in s.text and "Startup Screen Mode" in s.text)
    client.key("tab", 1)
    client.wait_text(">[Display]")
    client.key("tab", 1)  # Back out to Save/Cancel.
    client.wait_for(lambda s: ">[Display]" not in s.text and "Startup Screen Mode" in s.text)
    client.key("tab")
    client.wait_text(">[Display]")
    client.key("page_up", 2)
    shot = client.wait_text("Main Interface")
    selected_tab(shot, "General")
    boxed_number(shot, "4096")
    click_text(client, shot, "4096")
    client.text("2000")
    client.key("tab")
    return client.wait_text("2000")


def contracts(executable, reports):
    def prepare(root):
        write_script(root, "menu_dos_test", (SOURCE / "scripts/menu_dos_test.wren").read_text())
        install_suite(root, 'import "menu_dos_test" for DosTests\nDosTests.run()\n', "dos")
    with fixture(executable, entries=ENTRIES, prepare=prepare, reports=reports) as (client, root):
        client.wait_text("TEST-COMPLETE dos")
        assert b"DOS-CONTRACTS-PASS" in client.stderr


def keyboard(executable, reports):
    with fixture(executable, entries=ENTRIES, settings="MainUI=DOS\n", reports=reports) as (client, root):
        client.wait_text("Directory:")
        original_ini = (root / "syncterm.ini").read_bytes()
        shot = open_program(client)
        y = next(y for y, line in enumerate(shot.lines) if "Main Interface" in line)
        highlight = cell_at(shot, shot.lines[y].index("DOS"), y).background

        def focused(snapshot, label):
            if label in ("Save", "Cancel"):
                label = "[ " + label + " ]"
            for y, line in enumerate(snapshot.lines):
                if label in line:
                    return cell_at(snapshot, line.index(label), y).background == highlight
            return False

        # In one column, Right on a choice must not move down the form.
        client.key("right")
        client.text(" ")
        client.wait_text("Classic")
        client.key("escape")
        client.wait_for(lambda s: "Main Interface" in s.text and "Classic" not in s.text)
        client.key("down")
        client.wait_for(lambda s: focused(s, "Confirm Program Exit"))
        client.key("right")
        client.key("down")
        client.wait_for(lambda s: focused(s, "Prompt to Save"))
        client.key("up")
        client.wait_for(lambda s: focused(s, "Confirm Program Exit"))
        # Tab skips the remaining fields and visits the actions as one group.
        client.key("tab")
        client.wait_for(lambda s: focused(s, "Save"))
        client.key("right")
        client.wait_for(lambda s: focused(s, "Cancel"))
        client.key("down")
        client.key("right")
        client.key("left")
        client.wait_for(lambda s: focused(s, "Save"))
        client.key("tab")
        client.wait_text(">[General]")
        client.key("tab")
        client.wait_for(lambda s: focused(s, "Confirm Program Exit"))
        client.key("up")
        client.grid_resize(132, 50)
        client.wait_for(lambda s: s.metadata["columns"] == 132 and "Main Interface" in s.text)
        # The two columns form a grid, with no vertical wrapping on Left/Right.
        client.key("right")
        client.wait_for(lambda s: focused(s, "Invert Mouse Wheel"))
        client.key("down")
        client.wait_for(lambda s: focused(s, "Atari ST Key Clicks"))
        client.key("left")
        client.wait_for(lambda s: focused(s, "Confirm Program Exit"))
        client.key("down")
        client.wait_for(lambda s: focused(s, "Prompt to Save"))
        client.key("right")
        client.wait_for(lambda s: s.metadata["cursor_visible"] == 1)
        client.key("home")
        client.key("left")  # At the start of an editor, cross back to its row mate.
        client.wait_for(lambda s: focused(s, "Prompt to Save"))
        client.key("down")
        client.wait_for(lambda s: focused(s, "Colour Emoji"))
        client.key("right")  # There is no right-hand field on this row.
        client.key("up")
        client.wait_for(lambda s: focused(s, "Prompt to Save"))
        client.key("tab")
        client.wait_for(lambda s: focused(s, "Save"))
        client.key("tab", 1)
        client.wait_for(lambda s: focused(s, "Prompt to Save"))
        client.key("escape")
        client.wait_text("Directory:")
        assert (root / "syncterm.ini").read_bytes() == original_ini


def names(executable, reports):
    ordinary = "ABCDEFGHIJKLMNOPQRSTUVWXYZ1234"
    wide = "ABCDEFGHIJKLMNO" + "界" * 15
    long_comment = "Long" + "-" * 96
    entries = (f"[{ordinary}]\nAddress=ascii.example\nComment=centred note\n"
               f"[{wide}]\nAddress=wide.example\nComment=界note\n"
               f"[Z]\nAddress=z.example\nComment={long_comment}\n")
    with fixture(executable, entries=entries, settings="MainUI=DOS\n", reports=reports) as (client, root):
        Path(reports).mkdir(parents=True, exist_ok=True)
        for columns, rows in ((80, 25), (132, 50), (40, 25)):
            client.grid_resize(columns, rows)
            shot = client.wait_for(lambda s: (s.metadata["columns"], s.metadata["rows"]) == (columns, rows) and ordinary in s.text)
            assert shot.lines[-3].index("centred note") == (columns - len("centred note")) // 2, shot.text
            comment_y = rows - 3
            assert cell_at(shot, 0, comment_y).foreground == cell_at(shot, columns - 1, comment_y).foreground == cell_at(shot, (columns - 12) // 2, comment_y).foreground
            if columns >= 80:
                assert wide in shot.text, shot.text
                header_y = next(y for y, line in enumerate(shot.lines) if "Name" in line and "Address" in line)
                address_x = shot.lines[header_y].index("Address")
                assert address_x - shot.lines[header_y].index("Name") == 46, shot.text
                entry_y = next(y for y, line in enumerate(shot.lines) if wide in line)
                assert shot.cell_text(cell_at(shot, address_x, entry_y)) == "w", shot.text
                shot.save_image(Path(reports) / f"names-{columns}.ppm")
            else:
                for _ in range(3):
                    client.key("right")
                shot = client.wait_text("Address")
                assert "界" * 12 in shot.text and "wide.exa" in shot.text, shot.text
        client.grid_resize(80, 25)
        client.wait_for(lambda s: s.metadata["columns"] == 80 and ordinary in s.text)
        client.key("down")
        shot = client.wait_for(lambda s: "界note" in s.lines[-3])
        assert shot.lines[-3].index("界note") == (80 - 6) // 2, shot.text
        client.key("down")
        shot = client.wait_for(lambda s: s.lines[-3].startswith("  Long"))
        assert shot.lines[-3] == "  " + long_comment[:76] + "  ", shot.text


def directory(executable, reports):
    with fixture(executable, entries=ENTRIES, settings="MainUI=DOS\n", reports=reports) as (client, root):
        shot = client.wait_text("Directory:")
        Path(reports).mkdir(parents=True, exist_ok=True)
        shot.save_image(Path(reports) / "directory-80.ppm")
        assert "alpha.example" in shot.text and "2323" in shot.text and "Telnet" in shot.text, shot.text
        assert "original notes" in shot.lines[-3], shot.text
        assert not any(value in shot.lines[-3] for value in ("alpha.example", "2323", "ANSI/RIP1")), shot.text
        assert not any(label in shot.lines[0] for label in ("[ Edit ]", "[ Connect ]", "[ Settings ]")), shot.text
        # The single command bar retains mouse and keyboard access to actions.
        click_text(client, shot, "F4 Settings")
        shot = client.wait_text("Program Settings")
        click_text(client, shot, "Program Settings")
        client.wait_text("Main Interface")
        client.key("escape")
        shot = client.wait_text("Directory:")
        assert "original notes" in shot.lines[-3], shot.text
        click_text(client, shot, "F2 Edit Item")
        client.wait_text("Edit Directory Entry")
        client.key("escape")
        client.wait_text("Directory:")
        client.key("tab")
        client.key("right")
        client.key("right")
        client.key("enter")
        client.wait_text("Edit Directory Entry")
        client.key("escape")
        client.wait_text("Directory:")
        client.key("f5")
        client.wait_text("F6 Paste")
        client.key("down")
        client.wait_for(lambda s: "Directory:" in s.text and s.lines[-3].strip() == "")
        for columns, rows in ((132, 50), (40, 25), (80, 25)):
            client.grid_resize(columns, rows)
            shot = client.wait_for(lambda s: (s.metadata["columns"], s.metadata["rows"]) == (columns, rows) and "Beta" in s.text)
            if columns == 132:
                shot.save_image(Path(reports) / "directory-132.ppm")
                assert "Encoding" in shot.text and "Calls" in shot.text and "Last Call" in shot.text, shot.text
            if columns == 40:
                assert "Name" in shot.text and "Beta" in shot.text, shot.text
                for _ in range(3):
                    client.key("right")
                shot = client.wait_text("Address")
                assert "beta.exa" in shot.text, shot.text
            assert shot.lines[-3].strip() == "", shot.text
        # Search addresses independently of display clipping or name search keys.
        client.key(102, 2)
        client.text("alpha.example")
        client.key("enter")
        client.key("f2")
        shot = client.wait_text("Edit Directory Entry")
        assert "alpha.example" in shot.text and "Connection" in shot.text, shot.text
        client.key("escape")
        shot = client.wait_text("Directory:")
        assert "original notes" in shot.lines[-3], shot.text


def forms(executable, reports):
    with fixture(executable, entries=ENTRIES, settings="MainUI=DOS\n", reports=reports) as (client, root):
        client.wait_text("Directory:")
        original = (root / "syncterm.lst").read_bytes()
        client.key("f2")
        shot = client.wait_text("Edit Directory Entry")
        Path(reports).mkdir(parents=True, exist_ok=True)
        assert all(label in shot.lines[1] for label in ("[Connection]", "Terminal", "Files & Notes")), shot.text
        assert "Username" in shot.text and "Password" in shot.text, shot.text
        y = next(y for y, line in enumerate(shot.lines) if "Alpha" in line)
        x = shot.lines[y].index("Alpha")
        assert cell_at(shot, x, y).background != cell_at(shot, 0, y).background, "focused field uses the input palette"
        shot.save_image(Path(reports) / "entry-form.ppm")
        click_text(client, shot, "Terminal")
        shot = client.wait_text("Screen Mode")
        assert "Palette" in shot.text and "Wren Scripts" in shot.text, shot.text
        click_text(client, shot, "Files & Notes")
        shot = client.wait_text("Comment")
        assert "Download Path" in shot.text and "File Transfer Log Level" in shot.text, shot.text
        click_text(client, shot, "original notes")
        client.text("draft notes")
        client.key("page_up", 2)
        client.wait_text("Screen Mode")
        client.key("page_down", 2)
        shot = client.wait_text("draft notes")
        assert "[Files & Notes]" in shot.lines[1], shot.text
        client.key("escape")
        client.wait_text("Discard changes?")
        client.key("enter")
        client.wait_text("Directory:")
        assert (root / "syncterm.lst").read_bytes() == original
        client.key("f2")
        client.wait_text("Edit Directory Entry")
        # Name is the first field after the category buttons.
        client.text("Changed")
        client.key("escape")
        client.wait_text("Discard changes?")
        client.key("enter")
        client.wait_text("Directory:")
        assert (root / "syncterm.lst").read_bytes() == original
        client.key("f2")
        client.wait_text("Edit Directory Entry")
        client.text("Renamed")
        shot = client.capture()
        click_text(client, shot, "Files & Notes")
        shot = client.wait_text("Comment")
        click_text(client, shot, "original notes")
        client.text("saved notes")
        client.key("f10")
        client.wait_for(lambda s: "Directory:" in s.text and "Renamed" in s.text)
        assert "[Renamed]" in (root / "syncterm.lst").read_text()
        assert "Comment=saved notes" in (root / "syncterm.lst").read_text()
        shot = open_program(client)
        shot = program_layout(client, shot, reports)
        shot.save_image(Path(reports) / "program-form.ppm")
        # Cancelling program settings must not apply its draft.
        click_text(client, shot, "Confirm Program Exit")
        client.key("escape")
        client.wait_text("Discard changes?")
        client.key("enter")
        client.wait_text("Directory:")
        assert "ConfirmClose=false" in (root / "syncterm.ini").read_text()
        # Saving includes an editor moved out of view by a smaller grid.
        shot = open_program(client)
        original_ini = (root / "syncterm.ini").read_bytes()
        click_text(client, shot, "2000")
        client.key("delete")
        shot = client.wait_for(lambda s: "2000" not in s.text)
        y = next(y for y, line in enumerate(shot.lines) if "Scrollback Buffer Lines" in line)
        line = shot.lines[y]
        left, right = line.index("["), line.index("]")
        assert not line[left + 1:right].strip(), line
        assert cell_at(shot, left + 1, y).background != cell_at(shot, 0, y).background
        client.text("0")
        client.grid_resize(40, 14)
        client.wait_for(lambda s: s.metadata["rows"] == 14 and "[General]" in s.text)
        client.key("f10")
        client.wait_text("Enter an integer")
        assert (root / "syncterm.ini").read_bytes() == original_ini
        client.key("enter")
        client.wait_text("Scrollback Buffer")
        client.text("8192")
        client.grid_resize(80, 25)
        client.wait_for(lambda s: s.metadata["columns"] == 80 and "8192" in s.text)
        client.grid_resize(40, 14)
        client.wait_for(lambda s: s.metadata["rows"] == 14 and "Main Interface" in s.text)
        client.key("f10")
        client.wait_text("Directory:")
        assert "ScrollBackLines=8192" in (root / "syncterm.ini").read_text()


def dropdowns(executable, reports):
    def field(snapshot, label):
        y = next(y for y, line in enumerate(snapshot.lines) if label in line)
        line = snapshot.lines[y]
        left = line.index("[", line.index(label) + len(label))
        return left, y, line.index("]", left)

    def attached(snapshot, label, above=False):
        left, y, right = field(snapshot, label)
        row = y - 1 if above else y + 1
        assert snapshot.lines[row][left] == ("└" if above else "┌"), snapshot.text
        assert snapshot.lines[row][right] == ("┘" if above else "┐"), snapshot.text

    with fixture(executable, entries=ENTRIES, settings="MainUI=DOS\n", reports=reports) as (client, root):
        client.wait_text("Directory:")
        original_ini = (root / "syncterm.ini").read_bytes()
        shot = open_program(client)
        left, y, right = field(shot, "Main Interface")
        assert shot.lines[y][right - 1] == "v", shot.text
        form_background = cell_at(shot, 2, 20).background
        click_text(client, shot, "Main Interface")
        shot = client.wait_text("Classic")
        attached(shot, "Main Interface")
        assert cell_at(shot, 2, 20).background == form_background, shot.text
        assert cell_at(shot, left + 1, y + 2).background != cell_at(shot, left + 1, y + 3).background, shot.text
        Path(reports).mkdir(parents=True, exist_ok=True)
        shot.save_image(Path(reports) / "program-dropdown.ppm")
        client.key("f1")
        client.wait_text("Classic keeps the original layout")
        client.key("escape")
        shot = client.wait_for(lambda s: "Classic" in s.text and "keeps the original layout" not in s.text)
        attached(shot, "Main Interface")
        # Navigation is a preview; Escape and outside clicks leave the draft alone.
        client.key("up")
        client.key("escape")
        shot = client.wait_for(lambda s: "Main Interface" in s.text and "Classic" not in s.text)
        assert "DOS" in shot.lines[field(shot, "Main Interface")[1]], shot.text
        client.key("down", 4)  # Alt-Down opens the focused enum.
        shot = client.wait_text("Classic")
        attached(shot, "Main Interface")
        for columns, rows in ((132, 50), (40, 14), (80, 25)):
            client.grid_resize(columns, rows)
            shot = client.wait_for(lambda s: (s.metadata["columns"], s.metadata["rows"]) == (columns, rows) and "Classic" in s.text)
            attached(shot, "Main Interface")
        click_text(client, shot, "Scrollback Buffer Lines")
        shot = client.wait_for(lambda s: "Main Interface" in s.text and "Classic" not in s.text)
        click_text(client, shot, "Main Interface")
        shot = client.wait_text("Classic")
        click_text(client, shot, "Classic")
        shot = client.wait_for(lambda s: "Classic" in s.text and "┌" not in s.text)
        assert "Classic" in shot.lines[field(shot, "Main Interface")[1]], shot.text
        assert (root / "syncterm.ini").read_bytes() == original_ini
        client.key("escape")
        client.wait_text("Discard changes?")
        client.key("enter")
        client.wait_text("Directory:")
        shot = open_program(client)
        click_text(client, shot, "Colours")
        client.wait_text("Frame Colour")
        client.grid_resize(40, 14)
        shot = client.wait_for(lambda s: s.metadata["rows"] == 14 and "Inverse Colour" in s.text)
        click_text(client, shot, "Inverse Colour")
        shot = client.wait_for(lambda s: "└" in s.text and "Inverse Colour" in s.text)
        attached(shot, "Inverse Colour", above=True)
        client.grid_resize(80, 25)
        shot = client.wait_for(lambda s: s.metadata["columns"] == 80 and "┌" in s.text)
        attached(shot, "Inverse Colour")
        client.grid_resize(40, 14)
        shot = client.wait_for(lambda s: s.metadata["rows"] == 14 and "└" in s.text)
        attached(shot, "Inverse Colour", above=True)
        client.key("home")
        client.wait_text("Black")
        client.key("end")
        shot = client.wait_for(lambda s: "Light Gray" in s.text and "Black" not in s.text)
        left, y, right = field(shot, "Inverse Colour")
        # Default is the last background-colour option, after the eight colours.
        assert "Default" in shot.lines[y - 2][left:right + 1], shot.text
        client.key("escape")
        client.wait_for(lambda s: "Inverse Colour" in s.text and "┌" not in s.text)
        client.grid_resize(80, 25)
        shot = client.wait_for(lambda s: s.metadata["columns"] == 80 and "Lightbar Background" in s.text)
        click_text(client, shot, "Lightbar Background")
        client.wait_for(lambda s: "┌" in s.text)
        client.grid_resize(40, 14)
        client.wait_for(lambda s: s.metadata["rows"] == 14 and "[Colours]" in s.text and "┌" not in s.text)
        client.key("escape")
        client.wait_text("Directory:")
        assert (root / "syncterm.ini").read_bytes() == original_ini

    with fixture(executable, entries=ENTRIES, settings="MainUI=DOS\n", reports=reports) as (client, root):
        client.wait_text("Directory:")
        original_list = (root / "syncterm.lst").read_bytes()
        client.key("f2")
        shot = client.wait_text("Edit Directory Entry")
        click_text(client, shot, "Connection Type")
        shot = client.wait_text("RLogin")
        attached(shot, "Connection Type")
        shot.save_image(Path(reports) / "entry-dropdown.ppm")
        client.text("ssh")
        client.key("tab")
        shot = client.wait_for(lambda s: "Edit Directory Entry" in s.text and "┌" not in s.text)
        assert "SSH" in shot.lines[field(shot, "Connection Type")[1]] and "22" in shot.text, shot.text
        client.key("tab", 1)  # Return from Save/Cancel to Connection Type.
        client.key("down")
        client.text(" ")
        client.wait_text("IPv4")
        client.key("escape")
        client.wait_for(lambda s: "Edit Directory Entry" in s.text and "┌" not in s.text)
        assert (root / "syncterm.lst").read_bytes() == original_list
        client.key("f10")
        client.wait_text("Directory:")
        contents = (root / "syncterm.lst").read_text()
        assert "ConnectionType=SSH" in contents and "Port=22" in contents, contents


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
        client.wait_text("Directory:")
        assert "MainUI" in (root / "syncterm.ini").read_text()
        # The saved preference selects DOS again on a fresh process.
        client.close()
        with application(executable, root, reports=reports) as second:
            second.wait_text("Directory:")
            shot = open_program(second)
            click_text(second, shot, "Main Interface")
            second.key("home")
            second.key("enter")
            second.key("f10")
            second.wait_text("SyncTERM Settings")
            assert "SyncTERM Settings" in second.capture().text


def failures(executable, reports):
    with fixture(executable, entries=ENTRIES, settings="MainUI=DOS\n", reports=reports) as (client, root):
        client.wait_text("Directory:")
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
                shot = client.wait_text("Directory:")
                assert "Retry Name" not in shot.text and path.read_bytes() == original
            else:
                client.key("f10")
                client.wait_for(lambda s: "Directory:" in s.text and "Retry Name" in s.text)
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
        client.wait_text("Directory:")
        assert "MainUI=DOS" in path.read_text()


def creation(executable, reports):
    with fixture(executable, entries=ENTRIES, settings="MainUI=DOS\n", reports=reports) as (client, root):
        client.wait_text("Directory:")
        original = (root / "syncterm.lst").read_bytes()
        client.key("insert")
        client.wait_text("New Directory Entry")
        client.text("Gamma")
        client.key("down")
        client.text("gamma.example")
        client.key("escape")
        client.wait_text("Discard changes?")
        client.key("enter")
        client.wait_text("Directory:")
        assert (root / "syncterm.lst").read_bytes() == original
        client.key("insert")
        client.wait_text("New Directory Entry")
        client.text("Gamma")
        client.key("down")
        client.text("gamma.example")
        client.key("f10")
        shot = client.wait_for(lambda s: "Directory:" in s.text and "Gamma" in s.text)
        assert "gamma.example" in shot.text
        assert "[Gamma]" in (root / "syncterm.lst").read_text()


def connected(executable, reports):
    with listener() as server:
        entries = ("[Wire]\nAddress=127.0.0.1\nConnectionType=Raw\nScreenMode=80x25\n"
                   f"Port={server.getsockname()[1]}\nComment=connected notes\nNoStatus=true\nHidePopups=true\n")
        with fixture(executable, entries=entries, settings="MainUI=DOS\n", reports=reports) as (client, root):
            client.wait_text("Directory:")
            client.key("enter")
            with server.accept()[0] as peer:
                peer.sendall(b"\x1b[2J\x1b[HREMOTE TERMINAL CONTENT")
                before = client.wait_text("REMOTE TERMINAL CONTENT")
                client.command_key(18)
                menu = client.wait_text("Directory:")
                assert (menu.metadata["columns"], menu.metadata["rows"]) == (80, 25)
                assert "connected notes" in menu.lines[-3] and "127.0.0.1" not in menu.lines[-3], menu.text
                client.key("enter")  # Connected Enter edits instead of connecting.
                client.wait_text("Edit Directory Entry")
                client.text("Local edit")
                client.key("escape")
                client.wait_text("Discard changes?")
                client.key("enter")
                menu = client.wait_text("Directory:")
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
                client.wait_text("Directory:")
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
        client.wait_text("Directory:")
        original_ini = (root / "syncterm.ini").read_bytes()
        original_list = (root / "syncterm.lst").read_bytes()
        client.key("insert")
        client.wait_text("Cannot edit list in safe mode")
        client.key("enter")
        client.wait_text("Directory:")
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
    p.add_argument("case", choices=("contracts", "directory", "forms", "keyboard", "names", "dropdowns", "switching", "failures", "creation", "connected", "safe", "offer"))
    p.add_argument("--reports", default="reports/dos-ui")
    args = p.parse_args()
    globals()[args.case](args.executable, args.reports)

if __name__ == "__main__":
    main()
