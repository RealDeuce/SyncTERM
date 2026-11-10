# Testing bitmap driver

Configure with `-DST_CONSOLE_TEST=ON`, build normally, and run the regular
SyncTERM executable with `--test-control`. This feature defaults to OFF and is
reported under **Video / Testing bitmap backend** in Build Options. When it is
disabled, the backend, control transport, and application inspection hooks are
excluded from the build. CI enables it for testing, then rebuilds with both
`ST_CONSOLE_TEST=OFF` and `BUILD_TESTING=OFF` before collecting binaries for
upload.

The driver uses the normal software bitmap presenter, application, Wren menus,
CTerm engine, connection layer, and transfer workers. It needs no display
server. Native backend tests remain necessary for native window/input behavior.

## Python

`tools/testing/syncterm_control.py` requires only Python's standard library.
Its client starts SyncTERM with anonymous pipes and works with POSIX executables
and the normal Windows `.exe` and `.com` launchers. Windows still loads the
ordinary `syncterm.dll`; there is no separate test application.

```python
import sys
sys.path.insert(0, "tools/testing")
from syncterm_control import Client, ALT

with Client("build/bin/syncterm", ["--test-directory", "/tmp/my-fixture"]) as term:
    menu = term.wait_text("Build Options")
    term.key("right", ALT)
    frame = term.wait_for(lambda s: s.metadata["scaling_millionths"] == 2000000)
    print(frame.text)
    print(frame.cells[0])
    frame.save_image("menu.ppm")
```

Create the fixture directory first. `--test-directory` explicitly redirects
settings, personal/shared BBS lists, cache, downloads, keys, scripts, and themes
beneath that directory on every platform. It requires `--test-control`.
Ordinary `-n`, `-b`, and other command-line options remain available.
Without `--test-directory`, normal application paths apply. Audio retains its
normal configured behavior.

For a human-readable capture:

```sh
python3 tools/testing/syncterm_control.py --image menu.ppm \
    --wait-text 'Build Options' build/bin/syncterm \
    --test-directory /tmp/my-fixture
```

The client also offers `text`, `physical_key`, `mouse`, `focus`, drawable
`resize`, terminal `grid_resize`, and private `clipboard` operations. Mouse
coordinates are drawable pixels; the driver uses the shared bitmap coordinate
mapper. Physical key events describe edges; normalized key/text input is sent
separately, as with native backends. Use modifier constants `SHIFT`, `CONTROL`,
`ALT`, and `GUI`. Alt-Left/Right use the normal scaling helper; Alt-Enter toggles fullscreen
state.

Real time drives presentation by default. `presentation_time(milliseconds)`
overrides only the timestamp this driver supplies to the presenter;
`presentation_time()` returns to real time. This never changes application,
network, transfer, or script timers.

## Snapshot contract

`capture()` is one transaction on the application/input owner. It pins the
active console, holds its state and backend locks, renders, and copies cells,
raw planes, palette, composed RGB, host state, and application state before
releasing those locks. Transfer progress/completion is copied under the
worker's mutex within that transaction. No read clears transfer UI dirty flags
or drains queues. The transport thread never reads live CTerm or Wren state.

Captures own their buffers and remain valid after another frame, console switch,
resize, or session/display destruction. Pipe serialization occurs after the
transaction releases its locks. Snapshot IDs, presentation serials, content
generations, and timestamps have distinct meanings. The presenter generation alone does not
identify a blink-phase selection. An unchanged image still
has a new capture and presentation serial.

Cell fields have exactly the `st_console_cell` meanings: `glyph` is a resolved
font glyph, **not a Unicode scalar or codepage byte**. The glyph table supplies
representative text for each `(glyph_font, glyph)` pair. It cannot recover the
original selecting string when several strings map to the same glyph. Wide
continuation cells remain explicit. Font reverse mapping is performed only
while capturing, not during ordinary cell creation.

The raw primary and alternate planes preserve indexed colours and tagged RGB
values. Resolve an indexed pixel through the captured palette; a pixel with
`0x80000000` set carries direct RGB in its low 24 bits. The RGB buffer is the
complete drawable after composition, scaling, cursor selection, and black
letterboxing, with each pixel encoded as `0x00RRGGBB`.

Application fields report `session_active`, owner-observed connection state, CTerm state
(including margins, modes and LCF), and transfer ownership/progress. Offline
captures have `session_active=0`. URL opens are recorded as the latest URL and
an increasing `url_serial`; the driver does not launch a browser. Clipboard
contents are private to the driver.

An input response acknowledges queueing, not completion of the action. Menus
can prefetch input; network operations and scripts can remain active. Wait for
an observable capture predicate with a deadline. There is no global-idle
promise. `events_dequeued` counts input records delivered from the console
queue, including synthesized releases; it does not imply Wren has handled them.
Timeout diagnostics include the last capture and captured stderr.

## Protocol version 1

All integers use little endian. Each request/response has a 32-bit byte length
excluding that length word, then four 32-bit fields:

| Field | Meaning |
| --- | --- |
| version | `1` |
| request ID | Echoed in the response |
| opcode | Echoed in the response |
| status | Zero in requests; zero or a signed `IO_ERROR_*` in responses |

Requests are at most 1 MiB and responses at most 128 MiB. One request is serviced
at a time. Drawable and source rasters are limited to 4096 pixels per dimension
and 4,194,304 pixels in total. Read each response before sending the next request. Unknown commands
and invalid parameters return errors. Invalid framing/version or truncated
input closes the transport and posts a normal application close event. Startup
errors appear on stderr and close the process/pipes.

| Opcode | Request | Successful response |
| --- | --- | --- |
| 1 | Empty | Three u32: version, console build-feature bits, capability bits |
| 2 | Empty | Coherent snapshot sections |
| 3 | Nine u32: event type and eight union words | Empty; input accepted |
| 4 | Two u32: drawable width, height | Empty |
| 5 | UTF-8 clipboard bytes without NUL | Empty |
| 6 | Two u32: override enabled (0/1), presentation milliseconds | Empty |

Capability bits 0 through 8 advertise keyboard/text input, mouse input,
clipboard, host state, raw planes/cells, composed RGB, terminal state, transfer
state, and presentation-time override, respectively.

Event type values follow `st_console_event_type`. KEY_CODE words are code and
modifiers. PHYSICAL_KEY words are evdev code, modifiers, pressed, repeat. TEXT
words are scalar and modifiers. MOUSE words are action, drawable x/y, buttons,
modifiers, wheel x/y. RESIZE words are columns/rows; FOCUS's first word is a
boolean. CLOSE has no union data. CODEPAGE_BYTE (type 8) has one byte value
in its first word. Physical Alt+Keypad input exercises shared composition;
CODEPAGE_BYTE can also be injected to test a consumer independently.
Unused words are zero. A normalized
KEY_CODE Alt-Left/Right is consumed by window scaling, as in a native driver.

Snapshot sections each begin with u32 tag and u32 payload byte length:

| Tag | Payload |
| --- | --- |
| 1 | Metadata fields |
| 2 | Row-major cells: four u32 then four u16, matching the declared cell fields |
| 3 | 65,536 u32 palette entries |
| 4, 5 | Primary and alternate planes, row-major u32 |
| 6 | Composed drawable RGB, row-major u32 |
| 7 | u32 glyph count; entries of u32 font, u32 glyph, string |
| 8 | Application fields |
| 9 | Title, clipboard, latest requested URL: three strings |

A string is a u32 byte length followed by UTF-8 bytes. A field collection is a
u32 count followed by entries of string name, u32 kind, and value. Kind 0 is a
signed 64-bit integer; kind 1 is a string. Unknown section tags can be skipped.
`scaling_millionths` is the window scale multiplied by one million.

Transport code owns and joins its worker. It handles short reads/writes, keeps
pipe I/O outside application/display locks, protects POSIX pipe writes with
descriptor flags or a worker signal mask, and cancels blocked Windows I/O
during shutdown. The standalone POSIX executable also ignores SIGPIPE
process-wide.
Control handles do not pass to launched child programs.

For native C tests, `st_console_test.h` exposes owned captures and a small
owner-thread application-state callback. Callbacks copy state only; they must
not call console APIs or reenter Wren while capture holds the display locks.

## Regression suites and migration map

The `syncterm_conformance_*` suites add independent expected-result terminal
checks in CP437 and UTF-8, plus bounded seeded parser recovery. See
[the termtest coverage inventory](TERMINAL_CONFORMANCE.md) for individual cases
and explicit audio/native-device limitations. Their fixtures disable physical
audio output. Failures retain case traces and exact reply bytes alongside the
normal snapshot and wire diagnostics.

The `syncterm_rip154_*` suites add [RIPterm 1.54 historical raster, wire and
interaction checks](RIP154_CONFORMANCE.md) in both encodings. Their independent
reference captures include explicit provenance and UI-artifact exclusions;
parser unit tests still exercise every input split without relying on TCP
packet boundaries.

With `ST_CONSOLE_TEST=ON`, CTest runs `syncterm_behavior_*` against the normal
application executable. `syncterm_control_*` covers the driver, protocol,
transfers, and initial application integration. Native Windows also exercises
the ordinary `.com` launcher; application tests use the ordinary `.exe` and DLL.
These suites require only standard-library Python and need no display server.
The option remains OFF by default; migrated integration tests have no separate
legacy-harness fallback when the driver is disabled.

`tools/testing/syncterm_fixture.py` provides private application directories,
loopback listeners, bounded wire reads, cell-aware clicks, and failure reports.
All process fixtures clear inherited desktop environment variables. Tests wait
for visible/state predicates, not sleeps or a global-idle condition. Wire writes
do not prescribe decoder read boundaries; exact fragmentation remains a parser
unit-test responsibility.

| Previous coverage | Replacement / retained contract |
| --- | --- |
| Loader runner's drawing/widget/control suites | `syncterm_behavior_{draw,widget,radio,input,form,color,menubar,spinbox,popup,help,progress,menu,logview}`; original Wren assertions run through normal script imports. List, style, button, checkbox, date, palette, and status-bar suites also run this way. |
| Synthetic wide-font assertions within those suites | `wren_{draw,input,logview,popup,help,progress,menu}_synthetic` runs only `runSyntheticFontAudit()`, retaining the C fixture's exact wide mapping and double-width ASCII font. These assertions run even without bundled fallback fonts. |
| Loader runner's interactive Wren console | `syncterm_behavior_console` sends text/editing events to the running REPL; `wren_console_synthetic` retains its guaranteed wide-glyph prompt assertion. |
| Loader runner's main-menu address, position, and exit cases | `syncterm_behavior_{main_menu_address,menu_position,menu_exit}`; position tests include reconnect, insertion/removal, short-list clamping, and independent online/offline bookmarks. |
| Loader runner's auto-load order, menu failure, and picker recovery | `syncterm_behavior_{autoload,menu_error,recovery,recovery_close}` checks role-specific overrides, import ordering, exactly-once loading in both VMs, startup rejection, cancellation, and close from recovery. |
| Loader runner's BBS setters | `syncterm_behavior_bbs_setters`, with the same transactional rejection, save/reload, identity, GHost, and sorting assertions moved to `src/tests/wren/bbs_setters.wren`. |
| Settings UI runner and web-list server | `syncterm_behavior_settings` reuses the complete Wren settings-action suite; `syncterm_behavior_settings_ui` drives the real editor through a save failure, retry, and restart. `syncterm_behavior_web_list` uses a bounded local HTTP peer and retains fetch/save/edit/failure assertions. |
| Settings runtime and private policy checks | `wren_settings_runtime` retains live application to both consoles, history capacity, audio policy, default cursors/modes, safe-mode KDF protection, and direct save/profile-offer policy checks. It no longer replays the complete settings UI suite or runs a web server. |
| Picker interaction replay | `syncterm_behavior_picker` drives the shipped picker: read/cancel, multi-select/cancel, directory selection, overwrite/append, one-shot write consent, readable pixel-file decoding, write-only rejection, and restoration of the menu screen. |
| Picker VM and injected restore failures | `wren_picker_contract` retains separate VM/font/mouse/context ownership and failed backend restoration. Its prequeued successful interaction replay is removed. |
| Wren host's pixel suite in both VMs | `syncterm_behavior_pixels` runs the original pixel/blit/mask/codec assertions in the real menu and picker VMs. |
| Application session replay | `syncterm_behavior_{session_modes,profile,terminal_cp437,terminal_utf8}` covers real disconnect/return, legacy C128 fonts, Current/custom modes, resize restoration, profile scripts/options/counters, logs, LF expansion, and initial-speed replies. `app_session_mode` retains direct API invariants, invalid arguments, logging helpers, and failure paths. |
| Terminal text/status/rendering checks | `syncterm_behavior_{terminal,status,rendering}_{cp437,utf8}` exercises real wire input/replies, coherent state/cells, status routing, cursor styles, BlinkBG checksums, blink planes, and composed letterboxing. CTerm tests retain exact fragmentation, parser internals, mapped palettes, and ring/ownership contracts. |
| Scrollback integration | `syncterm_behavior_scrollback_{cp437,utf8}` verifies online/offline viewers, ordering across reset, the live final screen, and preserved blank pages. Direct ring/finish/copy/error tests remain in C. |
| Shell-based generic RIP drawing/query cases | `syncterm_behavior_graphics_{cp437,utf8}` covers actual RIP geometry/pixels/query wire bytes and OOII display/input/replies. PTY-specific boundaries, response-suppression callbacks, protocol units, transfer peers, and legacy/reference comparisons remain in their specialized harnesses. |
| Raw capture callback checks | `syncterm_behavior_capture_{cp437,utf8}` additionally verifies the real ANSI capture file preserves wire bytes despite filtering/replacement and malformed/mixed input. Lower-level callback-order and parser-boundary assertions remain in C. |
| Synthetic colour-glyph rendering | `st_console_color_glyph` now uses the testing driver's owned C capture for cells, planes, and palette. Synthetic font allocation/replacement and validation remain C checks; the explicit forced redraw in the replacement case is preserved. This suite requires the testing driver. |
| Bundled emoji rendering | `syncterm_behavior_emoji` checks monochrome/colour output at 8×8, 8×14, and 8×16 when bundled fallback fonts are enabled. Synthetic glyph coverage remains independent of those optional fonts. |

Component Wren suites override only the fixture's `MainMenu.run` through the
documented script override mechanism. This starts them after picker construction
without retaining directory-controller handles that their intentional
`Menu.load()` calls invalidate. Shipped helper classes remain available. Suites
publish an explicit completion token and assertion totals where applicable,
then service input until a normal close. Application workflow tests use the
unmodified shipped controllers.

Backend-specific curses/ANSI/Win32, X11, SDL, Wayland, Quartz, GDI, and libretro
tests remain. This includes native scaling/persistence and text centring/border
checks: the testing driver cannot validate those backends' input translation or
window behaviour. Presenter cache identity, upload retries, allocation faults,
synthetic fonts, transport/transfer units, and private Wren API contracts also
remain focused C tests.

On failure, migrated suites save diagnostics under the build's
`reports/test-control/`: the last coherent snapshot as JSON, its PPM image,
stderr, fixture settings/list/log files, and available wire bytes. Startup
failures save the startup error, including captured stderr. The existing CI
report artifact includes this directory. Successful fixtures remove their
temporary application directories, and local peer workers have bounded I/O and
are joined on success and failure.
