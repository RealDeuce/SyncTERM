# SyncTERM Modern UI

An optional, independently installed Wren frontend for SyncTERM 2.0α's
directory and settings. None of these scripts are embedded in the executable.
Removing the add-on restores the shipped interface.

The frontend provides a searchable connection directory, connection details,
keyboard/mouse navigation, quick connect, entry creation/editing/deletion,
sort profiles, and a settings workspace. Names, addresses and comments are
searched with SyncTERM's canonical caseless Unicode comparison. Selection and
search text survive a connection. Alt+E opens an edit-only directory during a
connection. Safe mode retains the host's restrictions.

The interface uses flat panels, a sidebar, light colours, and a compact
directory with name and address columns. Entries stay on one line when both
values fit their columns; otherwise the name and address use separate,
full-width lines. Longer values wrap at Unicode grapheme boundaries without
truncation. Faint pixel separators divide entries, including expanded ones.
A labeled gutter and tinted two-row panel separate the
selected connection's metadata/comment from the list. Directory action buttons
are shorter than the workspace buttons. On graphical backends bitmap captions
are centered at pixel precision, and rounded edges use coverage anti-aliasing against the
surrounding colour. Its text uses SyncTERM's bitmap fonts;
it does not provide proportional fonts or GPU rendering. On text-only backends
the same controls work without rounded pixel chrome. At least 72 columns and
24 rows are needed; smaller modes offer Enter to switch to 80x25, or Escape
to exit/return to the terminal.

Detailed entry and settings editors reuse the shared Wren UI components and
host APIs, with the add-on's theme. Encryption/password handling, persistence,
file consent and connection establishment remain owned by SyncTERM. The
connected terminal, file picker, and startup/exit prompts retain their existing
implementations. A persistent graphical frame around the live terminal is
outside this first version's scope.

## Install

Use a matching rebuilt SyncTERM executable: this frontend needs the
`Screen.update` API to present its cell and pixel layers together.

Quit SyncTERM, then run Python 3 with the actual **scripts directory**:

```
python3 install.py install --scripts-dir /path/to/SyncTERM/scripts
```

Typical locations:

| Platform | Scripts directory |
|---|---|
| Linux / BSD | `$XDG_DATA_HOME/syncterm/scripts`, usually `~/.local/share/syncterm/scripts` |
| macOS | `~/Library/Application Support/SyncTERM/scripts` |
| Windows | `%APPDATA%\SyncTERM\scripts` |
| Haiku | `~/config/settings/SyncTERM/scripts` |

The installer requires an explicit destination. It refuses to replace an
existing, different script, including another `auto/menu/main_menu.wren`
override. Identical files make reinstalling idempotent. Support modules are
installed before the menu entry point; failed installs roll back new files.
No personal configuration, dialing directory, or theme file is rewritten by
installation.

To upgrade, quit SyncTERM and uninstall using the previous package's
`install.py`, then install the new package. Keep that previous package until
the upgrade is complete; removal checks its original script contents.

## Remove

```
python3 install.py uninstall --scripts-dir /path/to/SyncTERM/scripts
```

Removal preserves other scripts. It refuses to delete files changed after
installation. To disable a modified copy manually, rename or move
`scripts/auto/menu/main_menu.wren` out of the auto-load directory. Restart
SyncTERM to load the built-in frontend again.

## Controls

- Directory / Settings: switch workspaces.
- Ctrl+F: focus and select the search field.
- Enter in search: focus the result list; Enter in the list: connect or edit.
- F2: edit the selected entry. Insert or Ctrl+N: create an entry.
- Tab / Shift+Tab: move focus. Mouse wheel: scroll the directory viewport,
  preserving the selection. Clicking a card selects it; Connect or Enter dials.
- Escape: clear search, return from Settings, or quit/return to the terminal.
- F1: help. Ctrl+grave: Wren console; ANSI/ncurses also support Ctrl+Q.
- Alt+B: view the last session's scrollback while offline.

Trusted menu extensions using `MainMenu.registerCommand` retain their key
bindings. They receive the add-on's running `App` and the connected-directory
flag, and run only while no modal dialog owns input.

## Build and verify

From the SyncTERM checkout:

```
cmake --build build --target syncterm_modern_ui_addon
ctest --test-dir build -R modern_ui --output-on-failure
```

The ZIP is written to `build/addons/syncterm-modern-ui.zip`. Packaging requires
Python 3 and leaves the SyncTERM executable unchanged. It also works directly:

```
python3 addons/modern_ui/install.py package --output /tmp/syncterm-modern-ui.zip
```

The installer tests use temporary directories. End-to-end tests install the
actual external scripts into isolated configurations and use the control bitmap
backend with audio disabled. No interactive desktop is used. Code is BSD-2-Clause;
see `LICENSE`. Font and other host component licenses remain with SyncTERM.
