# BIG 1.03 conformance

BIG (1.03) is selectable in BBS profiles and Wren. Its input/I operations run
through native CTerm. Drawing, sound and complete command acceptance remain
in progress.

The [standalone goal](BIG_IMPLEMENTATION.adoc) and
[implementation ledger](../src/tests/big/conformance.json) track 36 top-level
commands including X and nine X selectors (45 implementation entries).
The reference's 64 accepted research criteria describe documentation, not
implemented SyncTERM behavior. BF, DOS client UI, demo restrictions and
CONDOR implementation are excluded.

The target is fully enabled BIG 1.03 with VGA-capable 640x200/350/480 modes,
original numeric rounding (including g3 Y 5 to 11), exact normal pixels/fonts/
palettes/replies, indexed storage and local timing. Safe malformed-state
corrections are documented per entry. Sound acceptance is functional;
bit-identical reference PCM and listening are not development gates.

Interactive acquisition retains the original keyboard pointer/X6 steps and
adds native mouse movement. The latest movement controls the software pointer;
stationary button samples do not undo keyboard movement. Left release after
a valid press invokes the shared click action. UI/disabled/outside presses,
duplicate confirmation and reset/EOF releases must not create selections.

## CGA 80x25 prerequisite

The mode has stable value 32, 80x25 cells, an 8x8 font bank, 640x200 raster,
4:3 display aspect, normal PC palette/blink and cursor scanlines 6..7.
Existing modes retain their IDs. Ordered screen/canvas choices place CGA
immediately before EGA; `Menu.screenModes` stays indexed by stable ID.
Native and application verification passed: 21 selected CTest checks,
including eight coherent CP437/UTF-8 scenes across status visibility and
reconnects. The native indexed blink planes are compared in full; cells,
cursor, palette, replies, startup/return geometry and composed RGB interior
samples are checked together. No BIG command is complete yet (0/45).

## Shared parser boundary

An optional incremental numeric reader can own BIG's byte acquisition and
WORD rules while sharing attention framing, chaining, parameters and command
suspension. Supplied loop values bypass the wire reader. Tests use an opaque
synthetic reader to check partitions, byte ownership, input-free deadlines,
waits, dynamic arity, errors and recovery. All 69 selected native parser,
IGS, RIP and SkyPix checks passed. The ordinary BIG reader is recorded below.

## Ordinary numeric acquisition

The BIG component implements destructive four-WORD digit construction,
continuations, consumed delimiters, ordered auxiliary controls and expression
encodings. Its finite Y scaling retains original g3 Y 5 to 11 behavior.
`big_number` checks 54 literal cases at every split/bytewise input, all 65536
WORD values against an independent binary-rational oracle, digit-count wrap,
explicit timeout/BEL policy, shared-parser integration and EOF/reset recovery.

The leaf reader consumes explicit timeout events; the numeric source
callbacks below provide local deadline and final-r composition. Live terminal
event-loop scheduling remains pending.
The leaf-reader tests do not claim command completion; the selectable mode
and its application coverage are recorded below. Completion remains 0/45.

## Shared random core

The original WORD seed, 32-bit LCG and signed rejection helper are implemented.
`big_random` checks literal seed/state cases, all 32768 output values, signed
bounds, rejected-attempt state advancement and partition-independent polls.
Unreachable ranges yield cooperatively without a total retry cap or bound
repair. Numeric r, X0 and effects still need their shared application owner,
seed-time conversion and caller-specific integration; no command is complete.

## Stored numeric evaluator

`big_loop` covers original stepping, mirrored y, ordered expressions,
vertical-bar repeats, endpoint and counter WORD wrap, final text markers,
cancellation and exact delay/key/random suspension prefixes. A full 2048-word
table runs through 32 passes to prove cumulative-counter wrap. Shared-parser
partitions and bytewise input verify source changes within one command,
unscaled stored Y 5 versus wire Y 5->11, final-r ownership and EOF recovery.

The component rejects invalid count/index accesses instead of following
escaped DOS addresses. It preserves valid WORD overflow and pending source
state. Complete & acquisition/dispatch, native key mapping and application
local-deadline scheduling remain pending; command completion remains 0/45.

## Numeric source and deadline callbacks

`big_numeric` checks the production shared-parser callbacks with per-byte
local deadlines, stored-source waits, ordinary final-r expansion and exact
accepted-byte counts on errors. Fixtures cover deadline boundaries/renewal,
BEL-before-construction effects, late ready bytes, delayed key reads, random
waits, dynamic raw arity, split/bytewise source changes and EOF recovery.

The portable policy uses nominal 18.2 wait units/second, converted directly to
monotonic ns; numeric timeout is 4010989011ns. It owns no foreign clock, BIOS
phase or IRQ state. The real project clock is exercised without sleeping.
All 12 selected native checks passed in 1.48s. Live BIG terminal-mode/event-loop
and command integration remain pending; command completion remains 0/45.

## I/R state and video profiles

`big_state` checks logical I0 reset order (mode, two defaults passes, visual
then active page, application tail), palette-only I1, signed full-WORD
normalization, R mode mapping, returning rejection and partial host failures.
It distinguishes application attributes, BGI defaults, cached resolution and
interpreter-local page/palette owners. I/R retain custom pattern slots and
private font ratios. Actual indexed storage and live command binding remain
pending; these component tests do not complete I or R.

`big_profile` pins the three 640-pixel modes, console rows/font heights,
4:3 aspect and per-plane page pitches. Existing font0 banks match the supplied
IBM ROM bytes exactly; no substitute or new console font is needed. Original
BIG restored custom patterns are retained. The complete EGA colour table and
default pen map are now shared with RIP, with independent bit-formula checks.
BIG's separate graphical bitmap/CHR fonts still need their own integration.

Safe-port corrections define the formerly uninitialized X8 frame-local
palette as a snapshot at interpreter entry, stop at completed prefixes on host
backend failures, and report uninstalled-graphics defaults as a nonfatal error
instead of exiting the application. These do not add resets to ordinary I/R.

The native Release build passed without compiler/source diagnostics. All 18
selected native checks passed in 1.92s, and both coherent RIP raster capture
tests passed in 14.24s. The strengthened cached-resolution failure checks also
passed separately. BIG remains unselectable; completion remains 0/45.


## Initial live input integration

Native CTerm graphics mode6 now runs BIG at640x350, before ANSI parsing at
the same insertion point as IGS/RIP. The shared parser owns I#/chaining and
the production numeric callbacks own WORD acquisition. Outerff filtering,
NUL pairs, retained frame locals, unknown headers and owner-completed pair
timeouts are covered. A ready byte is consumed before timeout substitution;
previous numeric digits survive the substituted nondigit.

I's tested state operations now reach the actual indexed console: I0 installs
and clears the350-line profile, while I1 changes palette entries and retains
pixels, cursor and unrelated state. Native CP437/UTF-8/status/split-input
fixtures check literal IBM8x14 glyph rows, full-block pixels in both blink
phases, palette/cursor state, deadlines, random suspension, EOF/reset and
reset from a semantic callback. Physical audio is absent.

The complete loop/gang/flow/reply footer and native key binding remain pending.
Profile/menu/Wren selection and application captures are recorded below. I is
in progress; no complete BIG command is claimed (0/45).

Native CMake Release full build (/usr/bin/cc, build/) passed with no compiler/source diagnostics. The broader run passed88 existing BIG/shared-parser/IGS/RIP/SkyPix/terminal/session regressions in105.29s; its initial BIG fixture failure was a wrong assumption that timeout discards prior digits. Chapter5 and independent big_number expectations establish retained slots. The corrected/rebuilt cterm_big and graphics_protocol_parser tests both passed in2.43s (cterm_big2.36s); production behavior was unchanged by that fixture correction.


## BBS/Wren selection and application input captures

`Graphics=BIG (1.03)` and `GraphicsMode.big103` use stable value6; previous
values remain unchanged. The editor presets EGA, CP437 and the original IBM
font, permits CGA/EGA/80x30 initial modes and UTF-8, and persists the choices.
Current or unsupported saved modes use EGA without changing the saved profile.
The terminal advertises the active system grid and uses ANSI as its base.

`syncterm_big_input` checks
editor selection/persistence and coherent full native bitmap scenes across
all three initial resolutions, both encodings, status visibility and reconnect.
It compares independent literal IBM A rows, complete full-block/blink rasters,
EGA palette, cells, cursor, state and ANSI DSR replies, plus flat composed-RGB
samples and menu restoration. Current and unsupported presets exercise the
EGA fallback. I0 clears to the350-line mode; a following scene checks the
actual14-row font rather than a stretched8-row glyph. Physical audio/MIDI
are disabled; these checks make no audio or full-command-completion claim.

Mode installation now registers/selects each exact ROM console bank, with at
most one new registration per size in a terminal owner. The application uses
a narrow read-only inspection callback; BIG state stays private to CTerm.
All wire commands except the current I operations still need implementation,
and full I loop/flow/reply acceptance remains pending (0/45 complete).

Native CMake Release build (/usr/bin/cc, build/) passed without compiler/source diagnostics. Nine selected native tests passed in26.42s, including full wren_host, bbs_list, term, connection, screen_mode, app_session_mode and BIG state/profile/input checks. Four application regressions passed in13.53s: BIG input, IGS init/editor, CGA mode and BBS setters. After adding the unsupported-profile case, cterm_big and syncterm_big_input passed again in14.23s; the application test checks84 independently expected scenes across28 connections plus editor persistence.
