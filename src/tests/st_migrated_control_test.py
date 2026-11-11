#!/usr/bin/env python3
"""Application and rendering regressions migrated from embedded C harnesses."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import contextlib
import json
import re
import socket
import subprocess
import tempfile
import threading
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "testing"))
from syncterm_control import ProtocolError, COMMAND
from syncterm_fixture import (SOURCE, application, cell_at, click_text, fixture,
                              environment, install_suite, listener, receive, write_script)

SUITES = {
    "draw": ("ui_draw_test", "UiDrawTest", "run"),
    "widget": ("ui_widget_test", "UiWidgetTest", "run"),
    "radio": ("ui_radio_test", "UiRadioTest", "run"),
    "input": ("ui_input_test", "UiInputTest", "run"),
    "form": ("ui_form_test", "UiFormTest", "run"),
    "color": ("ui_color_picker_test", "UiColorPickerTest", "run"),
    "menubar": ("ui_menubar_test", "UiMenubarTest", "run"),
    "spinbox": ("ui_spinbox_test", "UiSpinboxTest", "run"),
    "popup": ("ui_popup_test", "UiPopupTest", "run"),
    "help": ("ui_help_test", "UiHelpTest", "run"),
    "progress": ("ui_progress_test", "UiProgressTest", "run"),
    "menu": ("menu_ui_test", "MenuUiTest", "run"),
    "logview": ("ui_logview_test", "UiLogviewTest", "run"),
    "console": ("wren_console_test", "WrenConsoleTest", "run"),
    "main_menu_address": ("main_menu_test", "MenuTest", "run"),
    "settings": ("menu_settings_ui_test", "SettingsMenuTest", "run"),
    "entry_editor": ("menu_bbs_editor_test", "MenuTest", "run"),
    "list": ("ui_list_test", "UiListTest", "run"),
    "style": ("ui_style_test", "UiStyleTest", "run"),
    "button": ("ui_button_test", "UiButtonTest", "run"),
    "checkbox": ("ui_checkbox_test", "UiCheckboxTest", "run"),
    "date": ("ui_date_picker_test", "UiDatePickerTest", "run"),
    "palette": ("menu_palette_picker_test", "MenuPalettePickerTest", "runAudit"),
    "statusbar": ("ui_statusbar_test", "UiStatusbarTest", "run"),
    "pixels": ("pixel_vm_test", "PixelVmTest", "run"),
}


def wren_suite(executable, name, reports):
    module, klass, method = SUITES[name]

    def prepare(root):
        write_script(root, module, (SOURCE / "scripts" / (module + ".wren")).read_text(encoding="utf-8"))
        source = 'import "syncterm_menu" for Menu\nif (Menu.load(null) != null) Fiber.abort("fixture load")\n'
        source += f'import "{module}" for {klass}\nvar result = {klass}.{method}()\n'
        if name != "settings":
            source += '''
import "syncterm" for Console, Host
for (entry in Console) if (entry[2].contains("FAIL")) Host.print(entry[2] + "\n")
if (!(result is List) || result.count != 2 || result[0] <= 0 || result[1] != 0) {
  Fiber.abort("suite failed: %(result)")
}
Host.print("Assertions: %(result[0]) passed, %(result[1]) failed\n")
'''
        install_suite(root, source, name)
        if name == "pixels":
            write_script(root, "zzz_pixel_contract", '''
import "pixel_vm_test" for PixelVmTest
import "syncterm" for Host
var result = PixelVmTest.run()
if (result[0] <= 0 || result[1] != 0) Fiber.abort("picker pixel contracts: %(result)")
Host.print("PICKER-PIXELS-PASS")
''', "picker")

    with fixture(executable, entries="[Existing]\nAddress=existing.example\n",
                 prepare=prepare, reports=reports) as (client, root):
        if name == "console":
            client.wait_text("(main_menu) wren>")
            client.text("/edit Aé一Z")
            for key in ("left", "backspace", "left", "right", "left", "delete"):
                client.key(key)
            client.text("Ω")
            client.key("enter")
            client.text("/edit keep éword")
            client.key(23, 2)
            client.text("done")
            client.key("enter")
            client.key("escape")
        client.wait_text("TEST-COMPLETE " + name)
        if name == "pixels":
            assert b"PICKER-PIXELS-PASS" in client.stderr


@contextlib.contextmanager
def connection(executable, reports, *, encoding="CP437", options="", settings="", script=None, menu_grid=None, prepare=None, hide_popups=True):
    with listener() as server:
        entries = ("[Capture]\nConnectionType=Raw\nAddress=127.0.0.1\n"
                   f"Port={server.getsockname()[1]}\nScreenMode=80x25\n"
                   f"NoStatus=true\nHidePopups={str(hide_popups).lower()}\nForceLCF=true\nRIP=Off\n"
                   + (f"Encoding={encoding}\n" if encoding is not None else "") + options)
        with fixture(executable, entries=entries, settings=settings, script=script,
                     reports=reports, prepare=prepare, quiet=hide_popups) as (client, root):
            client.wait_text("Capture")
            if menu_grid:
                client.grid_resize(*menu_grid)
                client.wait_for(lambda s: (s.metadata["columns"], s.metadata["rows"]) == menu_grid)
            client.key("enter")
            with server.accept()[0] as peer:
                peer.settimeout(10)
                client.wait_for(lambda s: s.application.get("connected") == 1)
                def send(data):
                    with (root / "wire.bin").open("ab") as stream:
                        stream.write(data)
                    peer.sendall(data)
                yield client, root, peer, send
            client.wait_for(lambda s: s.application.get("session_active") == 0)
            client.wait_text("Directory: Name")


def terminal(executable, encoding, reports):
    # Screen contents and terminal state must describe the same capture.
    for expanded in (False, True):
        with connection(executable, reports, encoding=encoding,
                        options=f"LFExpand={'true' if expanded else 'false'}\nBPSRate=9600\n") as (client, root, peer, send):
            send(b"\x1bP$q*r\x1b\\")
            receive(peer, b"\x1bP1$r;6*r\x1b\\", root / "wire.bin")
            for reset in (b"", b"\x1bc"):
                send(reset + b"\x1b[2J\x1b[HAB\nC\r\nD\x1b[0m")
                frame = client.wait_for(lambda s: s.lines[2].startswith("D"))
                assert frame.lines[0].startswith("AB")
                assert frame.lines[1][0 if expanded else 2] == "C"
                assert frame.application["cterm.x"] == 2 and frame.application["cterm.y"] == 3
            # DSR goes through the actual connection, not a substituted callback.
            send(b"\x1b[6n")
            receive(peer, b"\x1b[3;2R", root / "wire.bin")
            # Reset preserves local profile policy and configured output speed.
            send(b"\x1bP$q*r\x1b\\")
            receive(peer, b"\x1bP1$r;6*r\x1b\\", root / "wire.bin")


def cterm_reset(executable, encoding, reports):
    source = '''
import "syncterm" for CTerm, Conn, Hook, Key, Error
var resets = 0
Hook.onWire(35) {|byte|
  Conn.sendText("RESET-STAGED")
  return true
}
Hook.onKey(Key.f10) {|event|
  var result = CTerm.reset()
  if (result is Error) Fiber.abort(result)
  resets = resets + 1
  Conn.sendText("RESET-DONE-%(resets)")
  return true
}
Hook.onInput(88) {|scalar|
  var result = CTerm.reset()
  if (result is Error) Fiber.abort(result)
  Conn.sendText("RESET-HOOK")
  return true
}
Conn.sendText("RESET-READY")
'''
    for rip in ("Off", "RIPv1"):
        with connection(executable, reports, encoding=encoding, script=source,
                        options=f"RIP={rip}\n") as (client, root, peer, send):
            receive(peer, b"RESET-READY", root / "wire.bin")
            send(b"\x1b[2J\x1b[HKEEP\x1b[31m\x1b[3;20r\x1b[?2004h\x1b[5;6H\x1b]0;unfinished#")
            receive(peer, b"RESET-STAGED", root / "wire.bin")
            client.key("f10")
            receive(peer, b"RESET-DONE-1", root / "wire.bin")
            send(b"Z\x1b[6n")
            receive(peer, b"\x1b[1;2R", root / "wire.bin")
            frame = client.wait_for(lambda f: f.lines[0].startswith("ZEEP"))
            assert frame.application["cterm.top_margin"] == 1 and frame.application["cterm.bottom_margin"] == 25
            assert frame.application["cterm.ext_attr.bracket_paste"] == 0
            assert frame.application["cterm.x"] == 2 and frame.application["cterm.y"] == 1
            cell = cell_at(frame, 0, 0)
            def rgb(word):
                return word & 0xffffff if word & 0x80000000 else frame.palette[word]
            assert rgb(cell.foreground) == 0xa8a8a8
            assert rgb(cell.background) == 0
            assert (frame.metadata["columns"], frame.metadata["rows"]) == (80, 25)
            # Reset finishes a partial decoder without treating raw bytes as Wren text.
            send(b"\x1b[2;5H" + (b"\xe2\x82" if encoding == "UTF-8" else b"\x82") + b"#")
            receive(peer, b"RESET-STAGED", root / "wire.bin")
            client.key("f10")
            receive(peer, b"RESET-DONE-2", root / "wire.bin")
            send(b"Y\x1b[6n")
            receive(peer, b"\x1b[1;2R", root / "wire.bin")
            frame = client.wait_for(lambda f: f.lines[0].startswith("YEEP"))
            replacement = cell_at(frame, 4, 1)
            if encoding == "UTF-8":
                # The presentation font's boxed-cross .decodeerror has no text
                # mapping. Check its independently specified 8x16 raster; glyph
                # ordinals vary with the bundled base bank's coverage.
                assert replacement.glyph_font == 0 and frame.cell_text(replacement) == ""
                assert (frame.metadata["cell_width"], frame.metadata["cell_height"]) == (8, 16)
                rows = (0xff, 0xc3, 0xc3, 0xc3, 0xa5, 0xa5, 0xa5, 0x99,
                        0x99, 0x99, 0x99, 0x99, 0x99, 0xa5, 0xa5, 0xff)
                for y, bits in enumerate(rows):
                    for x in range(8):
                        index = (16 + y + frame.metadata["pixel_top"]) * frame.metadata["plane_width"] + 32 + x
                        expected = replacement.foreground if bits & (0x80 >> x) else replacement.background
                        assert frame.primary[index] == expected, (rip, x, y)
            else:
                assert frame.cell_text(replacement) == "é"
            assert frame.application["cterm.encoding"] == (1 if encoding == "CP437" and rip == "Off" else 0)
            # Reset from onInput runs within native input dispatch, including RIP.
            send(b"\x1b[4;5HXQ\x1b[6n")
            # Both replies may arrive in one recv; validate their order
            # together rather than discarding a coalesced cursor reply.
            receive(peer, b"RESET-HOOK\x1b[1;2R", root / "wire.bin")
            frame = client.wait_for(lambda f: f.lines[0].startswith("QEEP"))
            assert frame.application["cterm.x"] == 2 and frame.application["cterm.y"] == 1
            if rip != "Off":
                # Native finish dispatches a buffered RIP_DEFINE through the
                # existing menu-VM prompt before rebuilding the RIP engine.
                send(b"\r!|1D00000TMP,5:?Reset prompt?Start#")
                receive(peer, b"RESET-STAGED", root / "wire.bin")
                client.key("f10")
                client.wait_text("Reset prompt")
                client.key("enter")
                receive(peer, b"RESET-DONE-3", root / "wire.bin")
                send(b"\x1b[6n")
                receive(peer, b"\x1b[1;1R", root / "wire.bin")


def status(executable, encoding, reports):
    cases = ((b"M\x1b[2$~\x1b[1$}S\x1b[0$}N", "MN", "S", 2),
             (b"M\x1b[2$~\x1b[1$}S\x1b[1$~N", "MN", " ", 1),
             (b"M\x1b[2$~\x1b[1$}S\x1b[0$~N\x1b[2$~\x1b[1$}T\x1b[0$}O", "MNO", "T", 2))
    with connection(executable, reports, encoding=encoding,
                    options="NoStatus=false\n") as (client, root, peer, send):
        for stream, text, status_text, display_type in cases:
            send(b"\x1bc\x1b[2J\x1b[H" + stream + b"\x1b[0m")
            frame = client.wait_for(lambda s: s.lines[0].startswith(text)
                                    and s.application.get("cterm.status_display") == display_type)
            assert frame.cell_text(cell_at(frame, 0, 24)) == status_text


def rendering(executable, encoding, reports):
    with connection(executable, reports, encoding=encoding) as (client, root, peer, send):
        for style in range(1, 5):
            send(f"\x1b[{style} qPrompt\r\n".encode())
            frame = client.wait_for(lambda s: s.metadata["cursor_blink"] == style % 2
                                    and s.metadata["cursor_start"] == (0 if style <= 2 else 14))
            assert frame.metadata["cursor_end"] == 15 and frame.metadata["cursor_visible"]
        send(b"\x1b[?25l\x1b[2J\x1b[H\x1b[0;5;41mX\x1b[0m")
        frame = client.wait_for(lambda s: s.primary != s.alternate)
        for milliseconds in (1000, 1266):
            client.presentation_time(milliseconds)
            phase = client.capture()
            if milliseconds == 1000:
                first = phase
            else:
                assert first.cells == phase.cells and first.primary == phase.primary
                assert first.alternate == phase.alternate and first.rgb != phase.rgb
        # Rectangular checksum distinguishes BlinkBG and returns to its baseline.
        replies = []
        for mode in (b"", b"\x1b[?33h", b"\x1b[?33l"):
            send(mode + b"\x1b[1;1H\x1b[0;5;41m \x1b[1;1;1;1;1;1*y")
            reply = receive(peer, b"\x1b\\", root / "wire.bin")
            replies.append(reply)
        assert replies[0] != replies[1] and replies[0] == replies[2]
        client.presentation_time()
        # Exact black letterboxing and drawable geometry use composed RGB.
        client.resize(777, 611)
        frame = client.capture()
        m = frame.metadata
        assert (m["output_width"], m["output_height"]) == (777, 611)
        for y in range(m["output_height"]):
            if y < m["destination_y"] or y >= m["destination_y"] + m["destination_height"]:
                assert not any(frame.rgb[y * 777:(y + 1) * 777])


def profile(executable, reports):
    def prepare(root):
        write_script(root, "entry_options", '''
import "syncterm" for Session, CTerm, Conn, Host
if (CTerm.music != 2 || !CTerm.lastColumnFlag.forced || !CTerm.hasPaletteOverride || CTerm.paletteOverride[6] != 66051) Fiber.abort("presentation options")
if (Session.current.calls != 6 || Session.current.lastConnected <= 0 || Host.logLevel != 7 || Session.current.profile.ctermLogLevel != 7 || Session.current.profile.wrenScripts.count != 1) Fiber.abort("session options")
var error = CTerm.write("\\U0010ffff\\n")
if (error != null) Fiber.abort(error)
if (!Conn.sessionLog.contains("CTerm Debug: Missing glyph (.noglyph)")) Fiber.abort("session log snapshot")
Conn.sendText("PROFILE-READY")
''')

    with listener() as server:
        # Each launch exercises real log ownership, counters, and teardown.
        with fixture(executable, prepare=prepare, reports=reports,
                     settings="AudioModes=0\n") as (initial, root):
            initial.wait_text("Directory: Name")
            initial.close()
            log = root / "session.log"
            entries = ("[Options]\nConnectionType=Raw\nAddress=127.0.0.1\n"
                       f"Port={server.getsockname()[1]}\nScreenMode=80x25\n"
                       "NoStatus=true\nHidePopups=true\nForceLCF=true\nYellowIsYellow=true\n"
                       "ANSIMusic=2\nPalette=66051,16448250\nTotalCalls=5\n"
                       "ProtocolLogLevel=Debug\nTransferLogLevel=Debug\nCTermLogLevel=Debug\n"
                       "WrenScripts= entry_options ,entry_options\n"
                       f"LogFile={log}\nAppendLogFile=false\n")
            (root / "syncterm.lst").write_text(entries, encoding="utf-8")
            with application(executable, root, reports=reports) as client:
                client.wait_text("Options")
                client.key("enter")
                with server.accept()[0] as peer:
                    receive(peer, b"PROFILE-READY", root / "wire.bin")
                    frame = client.capture()
                    assert frame.application["cterm.last_column_flag.forced"] == 1
                client.wait_for(lambda s: s.application.get("session_active") == 0)
            contents = log.read_text(encoding="utf-8")
            assert "Log opened\n" in contents and "Log closed\n---------------\n" in contents
            assert "CTerm Debug: Missing glyph (.noglyph)" in contents and "U+10FFFF" in contents
            assert "TotalCalls=6" in (root / "syncterm.lst").read_text(encoding="utf-8")
            # Subsequent ordinary sessions test append, truncate, safe mode,
            # and a log destination that cannot be opened as a file.
            entries = entries.replace("TotalCalls=5", "TotalCalls=6").replace(
                "WrenScripts= entry_options ,entry_options", "WrenScripts=")
            for append, safe, invalid in ((True, False, False), (False, False, False),
                                           (False, True, False), (False, False, True)):
                value = entries.replace("AppendLogFile=false", f"AppendLogFile={'true' if append else 'false'}")
                if invalid:
                    value = value.replace(f"LogFile={log}", f"LogFile={root / 'cache'}")
                (root / "syncterm.lst").write_text(value, encoding="utf-8")
                before = log.read_bytes()
                with application(executable, root, extra=("-s",) if safe else (), reports=reports) as client:
                    client.wait_text("Options")
                    client.key("enter")
                    with server.accept()[0] as peer:
                        peer.sendall(b"LOG-SESSION")
                        client.wait_text("LOG-SESSION")
                    client.wait_for(lambda s: s.application.get("session_active") == 0)
                after = log.read_bytes()
                opened_count = after.replace(b"\r\n", b"\n").count(b"Log opened\n")
                if safe or invalid:
                    assert after == before
                elif append:
                    assert after.startswith(before) and opened_count == 2
                else:
                    assert b"Missing glyph" not in after and opened_count == 1
                if safe:
                    assert (root / "syncterm.lst").read_text(encoding="utf-8") == value


def session_modes(executable, reports):
    for graphics, mode, encoding, wire in (
        ("IGS (2.21)", "AtariST40x25", "ATARI-ST",
         b"G#X>5,0:G#P>10000,1:\x1bHOKG#X>10,17,23:"),
        ("IGS (2.21)", "AtariST40x25", "CP437",
         b"G#X>5,0:G#P>10000,1:\x1bHOKG#X>10,17,23:"),
        ("IGS (2.21)", "AtariST40x25", "UTF-8",
         b"G#X>5,0:G#P>10000,1:\x1bHOKG#X>10,17,23:"),
        ("RIPv1", "80x43", "CP437",
         b"!|1R00000000missing.rip|c02\r\n\x1b[31m\x1b[HOK"),
        ("RIPv1", "80x43", "UTF-8",
         b"!|1R00000000missing.rip|c02\r\n\x1b[31m\x1b[HOK"),
    ):
        for hide_popups in (False, True):
            options = f"Graphics={graphics}\nScreenMode={mode}\nCTermLogLevel=Warning\n"
            with connection(executable, reports, options=options, encoding=encoding,
                            hide_popups=hide_popups) as (client, root, peer, send):
                send(wire)
                frame = client.wait_for(lambda s: s.lines[0].startswith("OK"))
                assert frame.application["connected"] == 1
                if graphics.startswith("IGS"):
                    assert (frame.application["cterm.igs_draw_x"], frame.application["cterm.igs_draw_y"]) == (17, 23)
                else:
                    assert frame.application["cterm.attr"] & 15 == 4  # ANSI red
                # A command rejection leaves the real transport usable in
                # both directions; only actual EOF enters disconnect UI.
                client.text("z")
                receive(peer, b"z")
                send((b"\x1bH" if graphics.startswith("IGS") else b"\x1b[H") + b"STILL-HERE")
                client.wait_text("STILL-HERE")
                peer.shutdown(socket.SHUT_RDWR)
                if not hide_popups:
                    client.wait_text("Disconnected: Enter exits, F1 details")
                    client.key("enter")
    for mode, shape in (("80x25", (80, 25, 16)), ("C128-40col", (40, 25, 8)),
                        ("Current", (80, 28, 14)), ("Custom", (91, 27, 8))):
        options = f"ScreenMode={mode}\n"
        if mode == "C128-40col":
            options += "Font=Commodore 128 (Lower)\n"
        settings = ("ScreenMode=80x28\nCustomColumns=91\nCustomRows=27\n"
                    "CustomFontHeight=8\nCustomAspectWidth=5\nCustomAspectHeight=4\n")
        with connection(executable, reports, options=options, settings=settings,
                        encoding=None if mode == "C128-40col" else "CP437") as (client, root, peer, send):
            send(b"completed session")
            frame = client.wait_text("COMPLETED SESSION" if mode == "C128-40col" else "completed session")
            assert tuple(frame.metadata[k] for k in ("columns", "rows", "cell_height")) == shape
            if mode == "C128-40col":
                assert frame.application["cterm.codepage"] == 25  # Codepage.petsciiLower
            peer.shutdown(socket.SHUT_RDWR)
            restored = client.wait_for(lambda s: s.application.get("session_active") == 0
                                       and "Directory: Name" in s.text)
            assert tuple(restored.metadata[k] for k in ("columns", "rows", "cell_height")) == (80, 28, 14)
            assert restored.metadata["cursor_range"] == 14
            if mode == "C128-40col":
                entry = (root / "syncterm.lst").read_text(encoding="utf-8")
                assert "ScreenMode=C128-40col" in entry and "Font=Commodore 128 (Lower)" in entry
    with connection(executable, reports, options="ScreenMode=Current\n",
                    settings="ScreenMode=80x28\n", menu_grid=(93, 31)) as (client, root, peer, send):
        send(b"RESIZED-CURRENT")
        frame = client.wait_text("RESIZED-CURRENT")
        assert (frame.metadata["columns"], frame.metadata["rows"]) == (93, 31)
        peer.shutdown(socket.SHUT_RDWR)
        frame = client.wait_for(lambda s: s.application.get("session_active") == 0)
        assert (frame.metadata["columns"], frame.metadata["rows"]) == (93, 31)
    with connection(executable, reports, settings="ScreenMode=Prestel\n") as (client, root, peer, send):
        send(b"ANSI-OVER-PRESTEL")
        client.wait_text("ANSI-OVER-PRESTEL")
        peer.shutdown(socket.SHUT_RDWR)
        frame = client.wait_for(lambda s: s.application.get("session_active") == 0)
        assert tuple(frame.metadata[k] for k in ("columns", "rows", "cell_height")) == (40, 25, 20)
        assert frame.palette[4] == 0x0000ff and frame.metadata["blink_mode"] == 1


def scrollback(executable, encoding, reports):
    def check_viewer_row(frame, has_status=True):
        width = frame.metadata["columns"]
        if has_status:
            assert frame.lines[-1] == " " * width
        for cell in frame.cells[-width:]:
            color = cell.background
            rgb = color & 0xffffff if color & 0x80000000 else frame.palette[color]
            assert rgb == (0xaa0000 if has_status else 0)

    def check_terminal_unchanged(before, after, scrolled=False):
        for field in ("cterm.bottom_margin", "cterm.right_margin", "cterm.status_display",
                      "cterm.scrollback_start", "cterm.x", "cterm.y"):
            assert after.application[field] == before.application[field], field
        capacity = before.application["cterm.scrollback_lines"]
        assert after.application["cterm.scrollback_pos"] == (
            before.application["cterm.scrollback_pos"] + int(scrolled)) % capacity

    def check_completed_screen(completed, viewer):
        width = completed.metadata["columns"]
        assert viewer.metadata["rows"] == completed.metadata["rows"]
        for index, cell in enumerate(completed.cells):
            # Only the two ten-cell Scrollback indicators replace output.
            if index < 10 or width - 10 <= index < width:
                continue
            assert viewer.cells[index] == cell, index

    for hide_popups in (True, False):
        with connection(executable, reports, encoding=encoding,
                        settings="ScrollBackLines=100\n", hide_popups=hide_popups) as (client, root, peer, send):
            sample = "é".encode("utf-8") if encoding == "UTF-8" else b"\x82"
            send(b"\x1b[2J\x1b[H" + b"\r\n".join(sample + f" row {i:02d}".encode() for i in range(45)))
            client.wait_text("row 44")
            # RIS preserves the existing history instead of rotating it.
            send(b"\x1bc\x1b[2J\x1b[HAFTER-RESET\r\nSUMMARY\x1b[25;1HTAIL-ROW")
            client.wait_text("SUMMARY")
            client.command_key(48)
            client.wait_text("Scrollback")
            client.key("home")
            client.wait_for(lambda s: "Scrollback" in s.text and "SUMMARY" not in s.text)
            client.key("page_down")
            client.wait_text("row 01")
            client.key("up")
            oldest = client.wait_text("row 00")
            assert oldest.text.index("row 00") < oldest.text.index("row 01")
            assert "é row 00" in oldest.text
            client.key("end")
            newest = client.wait_text("SUMMARY")
            client.key("up")
            client.wait_text("AFTER-RESET")
            client.key("escape")
            completed = client.wait_text("TAIL-ROW")
            peer.shutdown(socket.SHUT_RDWR)
            if not hide_popups:
                notice = client.wait_text("Disconnected: Enter exits, F1 details")
                check_terminal_unchanged(completed, notice, scrolled=True)
                assert "TAIL-ROW" in notice.lines[-2]
                # Current-session history is available before leaving the notice.
                client.command_key(48)
                viewer = client.wait_text("Scrollback")
                check_terminal_unchanged(notice, viewer)
                check_completed_screen(completed, viewer)
                # NoStatus sessions retain all 25 rows in the viewer.
                check_viewer_row(viewer, has_status=False)
                assert "TAIL-ROW" in viewer.lines[-1]
                assert "SUMMARY" in viewer.text
                client.key("home")
                client.key("page_down")
                client.key("up")
                oldest = client.wait_text("row 00")
                check_viewer_row(oldest, has_status=False)
                check_terminal_unchanged(notice, oldest)
                client.key("escape")
                notice = client.wait_text("Disconnected: Enter exits, F1 details")
                assert "SUMMARY" in notice.text
                # Logical shortcuts also work for ANSI/curses-style input.
                client.key(98, COMMAND)
                client.wait_text("Scrollback")
                client.key("escape")
                client.wait_text("Disconnected: Enter exits, F1 details")
                client.key("f1")
                client.wait_text("Remote host dropped connection")
                client.key("escape")
                client.wait_text("Disconnected: Enter exits, F1 details")
                client.key("enter")
            client.wait_text("Directory: Name")
            # The offline viewer must retain the same ordering and final screen.
            client.command_key(48)
            client.wait_text("Scrollback")
            newest = client.wait_text("SUMMARY")
            assert "TAIL-ROW" in newest.text
            assert "Disconnected:" not in newest.text
            client.key("home")
            client.key("page_down")
            client.key("up")
            client.wait_text("row 00")
            client.key("escape")

    # A host close must unwind the standalone viewer and the notice. Also
    # exercise a session with the ordinary status row enabled.
    with listener() as server:
        entries = ("[Capture]\nConnectionType=Raw\nAddress=127.0.0.1\n"
                   f"Port={server.getsockname()[1]}\nScreenMode=80x25\nRIP=Off\n"
                   f"Encoding={encoding}\n")
        with fixture(executable, entries=entries, quiet=False, reports=reports) as (client, root):
            client.wait_text("Capture")
            client.key("enter")
            with server.accept()[0] as peer:
                peer.sendall(b"\x1b[2J\x1b[HCLOSE-VIEWER\x1b[24;1HSTATUS-TAIL")
                completed = client.wait_text("STATUS-TAIL")
                peer.shutdown(socket.SHUT_RDWR)
                notice = client.wait_text("Disconnected: Enter exits, F1 details")
                check_terminal_unchanged(completed, notice)
                client.key(98, COMMAND)
                viewer = client.wait_text("Scrollback")
                check_viewer_row(viewer)
                check_terminal_unchanged(completed, viewer)
                assert "STATUS-TAIL" in viewer.lines[-2]
                client.key("home")
                client.key("end")
                newest = client.wait_text("STATUS-TAIL")
                check_viewer_row(newest)
                assert "STATUS-TAIL" in newest.lines[-2]
                # Shutdown can close the transport before its event reply.
                client.close()
                assert client.process.returncode == 0


def picker(executable, reports):
    def prepare(root):
        # Supply NFD to the picker and access the NFC directory on disk.
        picker_dir = root / "downloads" / "café"
        picker_dir.mkdir()
        # File.read() is binary; keep the expected LF on Windows too.
        (picker_dir / "alpha.txt").write_bytes(b"picked\n")
        (picker_dir / "pixel.ppm").write_text("P3\n1 1\n255\n255 255 255\n", encoding="utf-8")
        # Call the public picker API, using the shipped picker and normal input.
        source = '''
import "syncterm" for Host, Screen, FilePickerOptions, PixelBuffer, PixelColor
var original = Screen.window.position
Screen.checked_(Screen.window.setPosition([8, 5]))
Screen.checked_(Screen.window.print("PICKER-MARKER"))
var file = Host.pickFile(PATH, "alpha.txt", 1, "Read fixture")
if (file == null || !file.path.endsWith("alpha.txt")) Fiber.abort("selected path")
file.open()
if (!file.isOpen || file.read() != "picked\\n") Fiber.abort("selected readable file")
file.close()
var cancelled = Host.pickFile(PATH, "alpha.txt", 1, "Cancel fixture")
if (cancelled != null) Fiber.abort("cancelled picker returned file")
Host.print("PICKER-STAGE multi")
var files = Host.pickFiles(PATH, "alpha.txt", 1)
if (files == null || files.count != 1 || !files[0].path.endsWith("alpha.txt")) Fiber.abort("multiple selection")
Host.print("PICKER-STAGE multi-cancel")
if (Host.pickFiles(PATH, "alpha.txt", 1) != null) Fiber.abort("cancelled multi-selection")
var directory = Host.pickFile(PATH, "*", FilePickerOptions.selectDirectory, "Directory fixture")
if (directory == null || (directory.path.replace("\\\\", "/").trimEnd("/") != NFC_PATH.replace("\\\\", "/").trimEnd("/"))) Fiber.abort("directory selection")
Host.print("PICKER-STAGE overwrite")
var output = Host.pickSavePath(PATH, "alpha.txt", FilePickerOptions.allowAppend)
if (output == null) Fiber.abort("overwrite selection")
output.open()
output.write("overwritten\\n")
output.close()
var used = Fiber.new { output.open() }
used.try()
if (used.error == null) Fiber.abort("write consent reused")
Host.print("PICKER-STAGE append")
output = Host.pickSavePath(PATH, "alpha.txt", FilePickerOptions.allowAppend)
if (output == null) Fiber.abort("append selection")
output.open()
output.write("appended\\n")
output.close()
var pixels = Host.pickFile(PATH, "pixel.ppm", 1, "Pixel fixture")
var image = PixelBuffer.fromPpm(pixels)
if (image == null || image.width != 1 || PixelColor.toRgb(image[0]) != 0xffffff) Fiber.abort("readable File pixel decode")
Host.print("PICKER-STAGE pixel-write")
var writeOnly = Host.pickSavePath(PATH, "pixel.ppm")
var rejected = Fiber.new { PixelBuffer.fromPpm(writeOnly) }
rejected.try()
if (rejected.error == null) Fiber.abort("write File accepted by pixel decoder")
'''.replace("NFC_PATH", json.dumps(str(picker_dir))).replace(
            "PATH", json.dumps(str(root / "downloads" / "cafe\u0301")))
        install_suite(root, source, "picker")
    with fixture(executable, prepare=prepare, reports=reports) as (client, root):
        first = client.wait_text("Read fixture")
        picker_id = first.metadata["console_id"]
        client.key("enter")
        client.wait_text("Cancel fixture")
        client.key("escape")
        def stage(name):
            return client.wait_for(lambda s: ("PICKER-STAGE " + name + "\n").encode() in client.stderr
                                    and "alpha.txt" in s.text)
        stage("multi")
        client.key(1, 2)
        client.key(10, 2)
        stage("multi-cancel")
        client.key("escape")
        client.wait_text("Directory fixture")
        client.key(10, 2)
        for name, key in (("overwrite", "o"), ("append", "a")):
            stage(name)
            client.key("enter")
            client.wait_text("File exists")
            client.text(key)
        client.wait_text("Pixel fixture")
        client.key("enter")
        client.wait_for(lambda s: b"PICKER-STAGE pixel-write\n" in client.stderr and "Save as" in s.text)
        client.key("enter")
        client.wait_text("File exists, overwrite?")
        client.text("y")
        restored = client.wait_text("TEST-COMPLETE picker")
        assert restored.metadata["console_id"] != picker_id and "PICKER-MARKER" in restored.text
        assert (root / "downloads" / "café" / "alpha.txt").read_text(encoding="utf-8") == "overwritten\nappended\n"


def menu_exit(executable, reports):
    with connection(executable, reports) as (client, root, peer, send):
        send(b"MENU-EXIT-SESSION")
        client.wait_text("MENU-EXIT-SESSION")
        client.command_key(18)  # Alt-E opens the connected directory.
        client.wait_text("Directory: Name")
        client.key("escape")
        frame = client.wait_text("MENU-EXIT-SESSION")
        assert frame.application["connected"] == 1
    with fixture(executable, reports=reports) as (client, root):
        client.wait_text("Directory: Name")
        try:
            client.key("escape")
        except ProtocolError as error:
            # Normal shutdown can close the response pipe before the queue
            # acknowledgement is written. The process must still exit cleanly.
            if not isinstance(error.__cause__, (EOFError, BrokenPipeError)):
                raise
        client.process.wait(timeout=15)
        assert client.process.returncode == 0


def menu_position(executable, reports):
    with listener() as server:
        def entries(count=40, omitted=None):
            return "".join(f"[Entry {i:02d}]\nConnectionType=Raw\nAddress=127.0.0.1\n"
                           f"Port={server.getsockname()[1]}\nNoStatus=true\nHidePopups=true\n"
                           for i in range(count) if i != omitted)
        def prepare(root):
            write_script(root, "zzz_position", '''
import "syncterm" for Key, Host, WON, Mod
import "main_menu" for MainMenu
MainMenu.registerCommand(121, Mod.command, "Alt-Y", "Capture position", Fn.new {|app, connected|
  var list = app.root.children[1].children[0]
  Host.print("MENU-POSITION " + WON.serialize([connected, list.selectedItem, list.selected, list.scrollTop, list.bounds.h]))
})
''', "menu")
        with fixture(executable, entries=entries(), prepare=prepare, reports=reports) as (client, root):
            def position():
                pattern = rb"MENU-POSITION (\[[^\r\n]+\])"
                count = len(re.findall(pattern, client.stderr))
                client.command_key(21)
                client.wait_for(lambda s: len(re.findall(pattern, client.stderr)) > count)
                return json.loads(re.findall(pattern, client.stderr)[-1])
            def visit(action=None):
                client.key("enter")
                with server.accept()[0] as peer:
                    peer.sendall(b"POSITION-SESSION")
                    client.wait_text("POSITION-SESSION")
                    if action:
                        action(peer)
                client.wait_for(lambda s: s.application.get("session_active") == 0
                                and "Directory: Name" in s.text)
            client.wait_text("Entry 00")
            for _ in range(30):
                client.key("down")
            for _ in range(3):
                client.key("up")
            before = position()
            assert before[1] == "Entry 27" and before[2] == 27 and before[3] > 0
            visit()
            assert position() == before
            visit(lambda peer: (root / "syncterm.lst").write_text(entries(omitted=0), encoding="utf-8"))
            shifted = position()
            assert shifted[1:4] == ["Entry 27", 26, before[3]]
            def connected_directory(peer):
                (root / "syncterm.lst").write_text(entries(), encoding="utf-8")
                client.command_key(18)
                client.wait_text("Directory: Name")
                assert position() == [True, *before[1:]]
                client.key("end")
                client.key("escape")
                client.wait_text("POSITION-SESSION")
                client.command_key(18)
                client.wait_text("Directory: Name")
                # Each visit starts from the offline bookmark, even after
                # browsing elsewhere during the previous connected visit.
                assert position() == [True, *before[1:]]
                client.key("down")
                assert position()[0] is True
                client.key("escape")
                client.wait_text("POSITION-SESSION")
            visit(connected_directory)
            assert position() == before
            def shifted_connected_directory(peer):
                # Move the connected entry outside the offline viewport.
                added = "".join(f"[Added {i:02d}]\n" for i in range(40))
                (root / "syncterm.lst").write_text(added + entries(), encoding="utf-8")
                client.command_key(18)
                client.wait_text("Directory: Name")
                current = position()
                assert current[:3] == [True, "Entry 27", 67]
                assert current[3] == min(67 - current[4] // 2, 81 - current[4])
                client.key("escape")
                client.wait_text("POSITION-SESSION")
                (root / "syncterm.lst").write_text(entries(), encoding="utf-8")
            visit(shifted_connected_directory)
            assert position() == before
            visit(lambda peer: (root / "syncterm.lst").write_text(entries(omitted=27), encoding="utf-8"))
            removed = position()
            assert removed[1:4] == ["Entry 28", 27, before[3]]
            visit(lambda peer: (root / "syncterm.lst").write_text(entries(3), encoding="utf-8"))
            clamped = position()
            assert clamped[2:4] == [3, 0]


def menu_shortcuts(executable, reports):
    def prepare(root):
        write_script(root, "zzz_shortcut_position", '''
import "syncterm" for Host, WON, Mod
import "main_menu" for MainMenu
MainMenu.registerCommand(121, Mod.command, "Alt-Y", "Capture selection", Fn.new {|app, connected|
  var list = app.root.children[1].children[0]
  Host.print("SHORTCUT-SELECTION " + WON.serialize(list.selectedItem))
})
''', "menu")

    with fixture(executable, entries="[Alpha]\n[Café One]\n[Café Two]\n",
                 prepare=prepare, reports=reports) as (client, root):
        def selection():
            pattern = rb'SHORTCUT-SELECTION ("[^\r\n]+")'
            count = len(re.findall(pattern, client.stderr))
            client.command_key(21)
            client.wait_for(lambda s: len(re.findall(pattern, client.stderr)) > count)
            return json.loads(re.findall(pattern, client.stderr)[-1])

        client.wait_text("Directory: Name")
        # Native backends supply C0 key events; logical text with Control is
        # also a supported input form. Both must reach the same commands.
        for text_form in (False, True):
            def shortcut(letter):
                if text_form:
                    client.text(letter, 2)
                else:
                    client.key(ord(letter) - 96, 2)

            client.key("home")
            shortcut("f")
            client.wait_text("Find")
            client.text("FÉ")
            client.key("enter")
            assert selection() == "Café One"
            shortcut("g")
            assert selection() == "Café Two"
            shortcut("g")
            assert selection() == "Café One"
            shortcut("s")
            client.wait_text("Sort Profiles")
            client.key("escape")
            client.wait_text("Directory: Name")
            shortcut("d")
            client.wait_text("Quick Connect")
            client.key("escape")
            client.wait_text("Directory: Name")

        # Desktop Ctrl+` is a physical shortcut: the key may produce no text.
        client.physical_key(41, True, 2)
        client.physical_key(41, False, 2)
        client.wait_text("(main_menu) wren>")
        client.key("escape")
        client.wait_text("Directory: Name")
        for text_form in (False, True):
            if text_form:
                client.text("`", 2)
            else:
                client.key(96, 2)
            client.wait_text("(main_menu) wren>")
            client.key("escape")
            client.wait_text("Directory: Name")


def bbs_setters(executable, reports):
    source = (SOURCE / "src/tests/wren/bbs_setters.wren").read_text(encoding="utf-8")
    with fixture(executable, entries="[Audit]\nAddress=example.invalid\nPalette=1,2\nWrenScripts=original\n",
                 prepare=lambda root: install_suite(root, source, "bbs-setters"), reports=reports) as (client, root):
        client.wait_text("TEST-COMPLETE bbs-setters")


def autoload(executable, reports):
    def prepare(root):
        source = '''
import "syncterm" for Console, Host
var messages = Console.entries.map {|entry| entry[2] }.toList
var previous = -1
for (text in ["EVENT-imported", "EVENT-importer", "EVENT-override"]) {
  if (messages.count {|message| message == text } != 1) Fiber.abort("module did not run exactly once: %(text)")
  var current = messages.indexOf(text)
  if (current <= previous) Fiber.abort("wrong module loading order")
  previous = current
}
'''
        install_suite(root, source.replace("EVENT", "menu"), "autoload")
        for event in ("menu", "picker"):
            write_script(root, "a_importer", f'import "z_imported"\nSystem.print("{event}-importer")\n', event)
            write_script(root, "z_imported", f'System.print("{event}-imported")\n', event)
            module = "main_menu" if event == "menu" else "file_picker"
            path = root / "scripts/auto" / event / (module + ".wren")
            original = path.read_text(encoding="utf-8") if path.exists() else (
                SOURCE / "scripts/auto" / event / (module + ".wren")).read_text(encoding="utf-8")
            write_script(root, module, original + f'\nSystem.print("{event}-override")\n', event)
            write_script(root, module, 'class Broken {\n')  # A different role must not override the entry point.
        write_script(root, "zzz_picker_assert", source.replace("EVENT", "picker") +
                     'Host.print("PICKER-AUTOLOAD-PASS")\n', "picker")
    with fixture(executable, prepare=prepare, reports=reports) as (client, root):
        client.wait_text("TEST-COMPLETE autoload")
        assert b"PICKER-AUTOLOAD-PASS" in client.stderr


def recovery(executable, reports, close=False):
    def prepare(root):
        install_suite(root, '''
import "syncterm" for Host
if (Host.pickFile(".", "*", 0) != null) Fiber.abort("recovery returned a selection")
''', "recovery")
        write_script(root, "file_picker", 'class Broken {\n', "picker")
    with fixture(executable, prepare=prepare, reports=reports) as (client, root):
        client.wait_text("File picker error")
        if close:
            client.close()
        else:
            client.key("escape")
            client.wait_text("TEST-COMPLETE recovery")


def menu_error(executable, reports):
    with tempfile.TemporaryDirectory(prefix="syncterm-menu-error-") as directory:
        root = Path(directory)
        (root / "syncterm.ini").write_text("[SyncTERM]\nConfirmClose=false\n", encoding="utf-8")
        write_script(root, "broken_addon", "class Broken {\n", "menu")
        process = subprocess.run([executable, "--test-control", "--test-directory", directory, "-q"],
                                 input=b"", stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                 env=environment(), timeout=15,
                                 creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        assert process.returncode != 0 and b"cannot initialize the menu" in process.stderr, process.stderr


def web_list(executable, reports):
    with listener() as server:
        errors = []
        def serve():
            try:
                with server.accept()[0] as peer:
                    receive(peer, b"\r\n\r\n")
                    body = b"[Downloaded]\nAddress=downloaded.example\n"
                    peer.sendall(f"HTTP/1.1 200 OK\r\nContent-Length: {len(body)}\r\nConnection: close\r\n\r\n".encode() + body)
            except BaseException as error:
                errors.append(error)
        worker = threading.Thread(target=serve)
        worker.start()
        try:
            source = f'''
import "syncterm_menu" for Menu
if (Menu.load(null) != null) Fiber.abort("load fixture")
if (Menu.addWebList("Fetched", "http://127.0.0.1:{server.getsockname()[1]}/list", 0) != null) Fiber.abort("web add failed")
if (!Menu.webListsDirty || Menu.webLists.count != 1) Fiber.abort("web add state")
if (Menu.saveWebLists() != null || Menu.webListsDirty) Fiber.abort("web save")
if (Menu.addWebList("Bad", "invalid", 0) == null) Fiber.abort("fetch failure accepted")
if (Menu.webLists.count != 1 || Menu.webLists[0][0] != "Fetched" || Menu.webListsDirty) Fiber.abort("failed fetch changed configuration")
if (Menu.load(null) != null || !Menu.entries.any {{|entry| entry.name == "Downloaded" }}) Fiber.abort("downloaded list was not loaded")
if (Menu.updateWebList(0, "https://example.invalid/changed") != null || !Menu.webListsDirty) Fiber.abort("web URI edit")
if (Menu.addWebList("Bad", "invalid", 0) == null || !Menu.webListsDirty || Menu.webLists.count != 1) Fiber.abort("fetch failure lost earlier edits")
if (Menu.deleteWebList(0) != null || Menu.webLists.count != 0 || Menu.saveWebLists() != null) Fiber.abort("web delete/save")
'''
            with fixture(executable, prepare=lambda root: install_suite(root, source, "web-list"), reports=reports) as (client, root):
                client.wait_text("TEST-COMPLETE web-list")
        finally:
            worker.join(timeout=16)
            assert not worker.is_alive(), "HTTP fixture did not shut down"
        if errors:
            raise errors[0]


def settings_ui(executable, reports):
    with fixture(executable, reports=reports) as (client, root):
        frame = client.wait_text("Program Settings")
        click_text(client, frame, "Program Settings")
        click_text(client, client.capture(), "Program Settings")
        frame = client.wait_text("Atari ST Key Clicks")
        assert any("Atari ST Key Clicks" in row and "Yes" in row for row in frame.lines)
        click_text(client, frame, "Atari ST Key Clicks")
        frame = client.wait_for(lambda s: any("Atari ST Key Clicks" in row and "No" in row for row in s.lines))
        click_text(client, frame, "Prompt to Save")
        # The normal editor keeps the draft open after a persistence error.
        settings = root / "syncterm.ini"
        backup = root / "settings.backup"
        settings.rename(backup)
        settings.mkdir()
        client.key("escape")
        client.wait_text("The settings could not be saved.")
        client.key("enter")
        client.wait_text("Prompt to Save")
        settings.rmdir()
        backup.rename(settings)
        client.key("escape")
        client.wait_text("Directory: Name")
        client.close()
        contents = settings.read_text(encoding="utf-8")
        assert "PromptSave=false" in contents, contents
        assert "AtariSTKeyClicks=false" in contents, contents
        # Verify persisted values through a fresh application instance.
        with application(executable, root, reports=reports) as restarted:
            frame = restarted.wait_text("Program Settings")
            click_text(restarted, frame, "Program Settings")
            click_text(restarted, restarted.capture(), "Program Settings")
            frame = restarted.wait_text("Prompt to Save")
            assert any("Prompt to Save" in row and "No" in row for row in frame.lines)
            assert any("Atari ST Key Clicks" in row and "No" in row for row in frame.lines)
            restarted.key("escape")


def graphics(executable, encoding, reports):
    with connection(executable, reports, encoding=encoding,
                    script='import "syncterm" for CTerm\nCTerm.ooiiMode = 1\n') as (client, root, peer, send):
        send(b"\xab?_2|")
        receive(peer, b"\xaf2|", root / "wire.bin")
        send(b"\xaba|")
        client.wait_text("Supply Room")
        client.text("U")
        receive(peer, b"U", root / "wire.bin")
        send(b"55\xab\\|AFTER")
        client.wait_text("55AFTER")
    with connection(executable, reports, encoding=encoding,
                    options="RIP=RIPv1\n") as (client, root, peer, send):
        send(b"!|*\r\n!|X2S2S\r")
        frame = client.wait_for(lambda s: s.primary[100 * s.metadata["plane_width"] + 100] == 0x14)
        assert (frame.metadata["columns"], frame.metadata["rows"], frame.metadata["cell_width"], frame.metadata["cell_height"]) == (80, 43, 8, 8)
        send(b"!|1\x1b0000\x82^M\r\n")
        receive(peer, b"\x82\r", root / "wire.bin")
        send(b"\x82\r" if encoding == "CP437" else "é\r".encode("utf-8"))
        client.wait_text("é")


def wire_capture(executable, encoding, reports):
    source = '''
import "syncterm" for Capture, Conn, Host, Hook
var file = Host.pickSavePath(null, "capture.ans")
if (file == null || Capture.start(file, true) != null) Fiber.abort("start raw capture")
Hook.onWire(35, Fn.new {|byte| true })
Hook.onWire(88, Fn.new {|byte| "REPLACED" })
Conn.sendText("CAPTURE-READY")
'''
    with connection(executable, reports, encoding=encoding, script=source) as (client, root, peer, send):
        frame = client.wait_text("capture.ans")
        # The picker scrolls long paths horizontally, including macOS TMPDIRs.
        click_text(client, frame, str(Path("downloads") / "capture.ans"))
        client.key("enter")
        receive(peer, b"CAPTURE-READY", root / "wire.bin")
        expected = b"A#XZ\x1b[32m\x80\xff\x00\xef\xbf\xbd\xc3"
        send(expected)
        client.wait_text("REPLACED")
        peer.shutdown(socket.SHUT_RDWR)
        client.wait_for(lambda s: s.application.get("session_active") == 0)
        assert (root / "downloads/capture.ans").read_bytes() == expected


def emoji(executable, reports):
    for height in (8, 14, 16):
        for colour in (False, True):
            settings = (f"CustomRows=25\nCustomColumns=80\nCustomFontHeight={height}\n"
                        f"ColorEmoji={'true' if colour else 'false'}\n")
            with connection(executable, reports, encoding="UTF-8", settings=settings,
                            options="ScreenMode=Custom\n") as (client, root, peer, send):
                send("\x1b[?25l\x1b[2J\x1b[H💰\x1b[0m".encode("utf-8"))
                frame = client.wait_text("💰")
                assert frame.metadata["cell_height"] == height
                span = 2 if cell_at(frame, 1, 0).flags & (1 << 19) else 1
                pixels = {frame.primary[y * frame.metadata["plane_width"] + x]
                          for y in range(height) for x in range(span * frame.metadata["cell_width"])}
                if colour:
                    assert len(pixels) > 2, "colour glyph was rendered as monochrome"
                else:
                    cell = cell_at(frame, 0, 0)
                    assert pixels <= {cell.foreground, cell.background}, "monochrome setting retained colour pixels"


CASES = {"cterm_reset_cp437": lambda e, r: cterm_reset(e, "CP437", r),
         "cterm_reset_utf8": lambda e, r: cterm_reset(e, "UTF-8", r),
         "terminal_cp437": lambda e, r: terminal(e, "CP437", r),
         "terminal_utf8": lambda e, r: terminal(e, "UTF-8", r),
         "status_cp437": lambda e, r: status(e, "CP437", r),
         "status_utf8": lambda e, r: status(e, "UTF-8", r),
         "rendering_cp437": lambda e, r: rendering(e, "CP437", r),
         "rendering_utf8": lambda e, r: rendering(e, "UTF-8", r),
         "scrollback_cp437": lambda e, r: scrollback(e, "CP437", r),
         "scrollback_utf8": lambda e, r: scrollback(e, "UTF-8", r),
         "profile": profile, "session_modes": session_modes, "picker": picker,
         "menu_exit": menu_exit, "menu_position": menu_position, "menu_shortcuts": menu_shortcuts,
         "autoload": autoload, "recovery": recovery,
         "recovery_close": lambda e, r: recovery(e, r, True),
         "menu_error": menu_error, "web_list": web_list, "bbs_setters": bbs_setters,
         "settings_ui": settings_ui,
         "graphics_cp437": lambda e, r: graphics(e, "CP437", r),
         "graphics_utf8": lambda e, r: graphics(e, "UTF-8", r),
         "capture_cp437": lambda e, r: wire_capture(e, "CP437", r),
         "capture_utf8": lambda e, r: wire_capture(e, "UTF-8", r), "emoji": emoji}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable")
    parser.add_argument("case")
    parser.add_argument("--reports", default="reports/test-control")
    args = parser.parse_args()
    if args.case in SUITES:
        wren_suite(args.executable, args.case, args.reports)
    else:
        CASES[args.case](args.executable, args.reports)


if __name__ == "__main__":
    main()
