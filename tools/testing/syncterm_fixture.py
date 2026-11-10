"""Isolated application fixtures shared by process regression tests."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import array
import contextlib
import json
import os
from pathlib import Path
import socket
import tempfile

from syncterm_control import Client

SOURCE = Path(__file__).resolve().parents[2]


def environment():
    env = dict(os.environ)
    for name in ("DISPLAY", "WAYLAND_DISPLAY", "XAUTHORITY", "XDG_RUNTIME_DIR"):
        env.pop(name, None)
    return env


def write_script(root, name, source, event=None):
    directory = root / "scripts"
    if event:
        directory = directory / "auto" / event
    directory.mkdir(parents=True, exist_ok=True)
    (directory / (name + ".wren")).write_text(source, encoding="utf-8")


def failure_report(client, root, reports):
    """Keep diagnostics outside the disposable application directory."""
    reports = Path(reports)
    reports.mkdir(parents=True, exist_ok=True)
    target = Path(tempfile.mkdtemp(prefix="failure-", dir=reports))
    (target / "stderr.txt").write_bytes(bytes(client.stderr))
    if client.last_snapshot is not None:
        snapshot = client.last_snapshot
        (target / "snapshot.json").write_text(
            json.dumps(snapshot.diagnostic(), ensure_ascii=False, indent=2), encoding="utf-8")
        snapshot.save_image(target / "screen.ppm")
    for name in ("wire.bin", "replies.bin", "cases.txt", "syncterm.ini", "syncterm.lst", "session.log"):
        path = root / name
        if path.is_file():
            (target / name).write_bytes(path.read_bytes())
    print(f"SyncTERM failure diagnostics: {target}", flush=True)


@contextlib.contextmanager
def application(executable, root, *, extra=(), reports="reports/test-control", quiet=True):
    try:
        client = Client(executable, ["--test-directory", root, *(["-q"] if quiet else []), *extra],
                        env=environment(), timeout=15)
    except BaseException as error:
        directory = Path(reports)
        directory.mkdir(parents=True, exist_ok=True)
        target = Path(tempfile.mkdtemp(prefix="startup-", dir=directory))
        (target / "error.txt").write_text(str(error), encoding="utf-8")
        print(f"SyncTERM startup diagnostics: {target}", flush=True)
        raise
    with client:
        try:
            yield client
            client.close()
            assert client.process.returncode == 0, client.diagnostics()
        except BaseException:
            failure_report(client, root, reports)
            raise


@contextlib.contextmanager
def fixture(executable, *, entries="", extra=(), script=None,
            settings="", prepare=None, reports="reports/test-control", quiet=True):
    with tempfile.TemporaryDirectory(prefix="syncterm-control-") as directory:
        root = Path(directory)
        (root / "syncterm.ini").write_text(
            "[SyncTERM]\nConfirmClose=false\nScalingFactor=1\nAudioModes=0\nMidiModes=0\n" + settings, encoding="utf-8")
        (root / "syncterm.lst").write_text(entries, encoding="utf-8")
        (root / "downloads").mkdir()
        if script is not None:
            write_script(root, "zzz_test_control", script, "connected")
        if prepare:
            prepare(root)
        with application(executable, root, extra=extra, reports=reports, quiet=quiet) as client:
            yield client, root


def cell_at(snapshot, x, y):
    return snapshot.cells[y * snapshot.metadata["columns"] + x]


def check_pixel_planes(snapshot, expected, *, start=0, context=()):
    """Compare every expected word in both phases without boxing observed pixels."""
    values = array.array("I", expected)
    data, width = values.tobytes(), snapshot.metadata["plane_width"]
    for phase in ("primary", "alternate"):
        observed = memoryview(getattr(snapshot, phase))[start:start + len(values)]
        assert observed.tobytes() == data, (context, phase,
            next(((i % width, i // width, a, e)
                  for i, (a, e) in enumerate(zip(observed, values)) if a != e),
                 ("length", len(observed), len(values))))


def click_cell(client, snapshot, x, y, buttons=1):
    m = snapshot.metadata
    px = m["destination_x"] + (x * m["cell_width"] + m["cell_width"] // 2) * m["destination_width"] // m["plane_width"]
    py = m["destination_y"] + (y * m["cell_height"] + m["cell_height"] // 2 + m["pixel_top"]) * m["destination_height"] // m["plane_height"]
    client.mouse(px, py, action=1, buttons=buttons)
    client.mouse(px, py, action=1, buttons=0)


def click_text(client, snapshot, text):
    # Text columns are not cell columns when a preceding glyph is wide.
    width = snapshot.metadata["columns"]
    for y in range(snapshot.metadata["rows"]):
        line, positions = "", []
        for x in range(width):
            cell = cell_at(snapshot, x, y)
            if cell.flags & (1 << 19):
                continue
            value = snapshot.cell_text(cell)
            positions.extend([x] * len(value))
            line += value
        if text in line:
            click_cell(client, snapshot, positions[line.index(text)], y)
            return
    raise AssertionError(f"Missing {text!r}\n{snapshot.text}")


@contextlib.contextmanager
def listener():
    with socket.socket() as server:
        server.bind(("127.0.0.1", 0))
        server.listen(1)
        server.settimeout(15)
        yield server


def receive(peer, expected, wire=None):
    peer.settimeout(10)
    received = bytearray()
    while expected not in received:
        data = peer.recv(4096)
        if not data:
            raise AssertionError(f"Connection closed before {expected!r}; got {received!r}")
        received.extend(data)
        if wire:
            with wire.open("ab") as stream:
                stream.write(data)
    return received


def install_suite(root, source, token="DONE"):
    """Run a Wren fixture through ordinary module/auto-script loading."""
    write_script(root, "test_completion", '''
import "syncterm" for Screen, Input, Key, KeyEvent
class TestCompletion {
  static finish(token) {
    Screen.checked_(Screen.window.setPosition([1, 1]))
    Screen.checked_(Screen.window.print("TEST-COMPLETE " + token))
    while (true) {
      var event = Input.next()
      if (event is KeyEvent && event.key == Key.quit) break
    }
  }
}
''')
    write_script(root, "test_case", source + '\nimport "test_completion" for TestCompletion\n'
                 f'TestCompletion.finish("{token}")\n')
    # Component suites run after host/picker construction without retaining a
    # live directory controller whose BBS handles their Menu.load calls retire.
    # Keep the shipped classes available to tests that import main_menu helpers.
    menu = (SOURCE / "scripts/auto/menu/main_menu.wren").read_text(encoding="utf-8")
    original = '''    if (!prepare()) return null
    var menu = MainMenuApp.new(current, connected)
    var result = menu.run()
    rememberView_(connected, menu.view_)
    return result'''
    assert menu.count(original) == 1, "MainMenu fixture entry needs updating"
    menu = menu.replace(original, '    import "test_case"\n    return null')
    write_script(root, "main_menu", menu, "menu")
