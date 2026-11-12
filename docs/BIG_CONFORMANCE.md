# BIG 1.03 conformance

BIG terminal implementation is in progress. The completed first prerequisite adds
CGA 80x25; BIG framing, drawing and sound are not yet selectable.

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
The BIG graphics mode is not yet selectable; command completion remains 0/45.

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
