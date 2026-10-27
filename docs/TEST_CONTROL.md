# Testing bitmap driver

Configure with `-DST_CONSOLE_TEST=ON`, build normally, and run the regular
SyncTERM executable with `--test-control`. This feature defaults to OFF and is
reported under **Video / Testing bitmap backend** in Build Options. CI enables
it in the same application binaries it builds and publishes.

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
boolean. CLOSE has no union data. Unused words are zero. A normalized
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
pipe I/O outside application/display locks, suppresses SIGPIPE only in the
POSIX transport thread, and cancels blocked Windows I/O during shutdown.
Control handles do not pass to launched child programs.

For native C tests, `st_console_test.h` exposes owned captures and a small
owner-thread application-state callback. Callbacks copy state only; they must
not call console APIs or reenter Wren while capture holds the display locks.
