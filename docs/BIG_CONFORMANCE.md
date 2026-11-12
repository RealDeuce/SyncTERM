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

This component consumes explicit timeout events; application-local deadline
scheduling and caller-specific source/random integration remain pending.
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
unscaled stored Y5 versus wire Y5->11, final-r ownership and EOF recovery.

The component rejects invalid count/index accesses instead of following
escaped DOS addresses. It preserves valid WORD overflow and pending source
state. Complete & acquisition/dispatch, native key mapping and application
local-deadline scheduling remain pending; command completion remains 0/45.
