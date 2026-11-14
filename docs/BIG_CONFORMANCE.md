# BIG 1.03 conformance

BIG (1.03) is selectable in BBS profiles and Wren. Its wire & setup, I/R/g/X0/X1/X2/X3/X4/X5/X6/X8, A/C/H/M/T/E attributes, S palettes, m movement, P markers, L/D/d lines, B bars/boxes, Z outlined fills and F fills run
through native CTerm. Native W graphical text, t pauses, < keyboard/hybrid pointer acquisition and G0..6 images/pages are integrated. Remaining drawing,
sound and complete command acceptance remain in progress.

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

BIG now uses parser backpressure and session/connection buffering. It sends
no automatic XON/XOFF or custom X5 flow bytes; X5 still retains its operands.
The remote needs no software flow-control support. Pacing continues across
the buffered tail after a pause without banking catch-up baud credit.
Historical verification entries below record earlier serial-flow behavior;
the native pause/buffering section records current executable expectations.

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
event-loop scheduling is covered by the native/wire integration below.
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
local-deadline scheduling are covered below; command completion remains 0/45.

## Numeric source and deadline callbacks

`big_numeric` checks the production shared-parser callbacks with per-byte
local deadlines, stored-source waits, ordinary final-r expansion and exact
accepted-byte counts on errors. Fixtures cover deadline boundaries/renewal,
BEL-before-construction effects, late ready bytes, delayed key reads, random
waits, dynamic raw arity, split/bytewise source changes and EOF recovery.

The portable policy uses nominal 18.2 wait units/second, converted directly to
monotonic ns; numeric timeout is 4010989011ns. It owns no foreign clock, BIOS
phase or IRQ state. The real project clock is exercised without sleeping.
All 12 selected native checks passed in 1.48s. Live BIG mode and event-loop integration
are covered below; full command completion remains 0/45.

## I/R state and video profiles

`big_state` checks logical I0 reset order (mode, two defaults passes, visual
then active page, application tail), palette-only I1, signed full-WORD
normalization, R mode mapping, returning rejection and partial host failures.
It distinguishes application attributes, BGI defaults, cached resolution and
interpreter-local page/palette owners. I/R retain custom pattern slots and
private font ratios. Complete indexed page/image storage and R wire binding remain
pending; I binding is covered below. These components do not complete I or R.

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

Common loop/gang/flow/reply footer, native keys and & wire setup are recorded
below. < production and music synchronization remain pending. I is
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
Wire commands other than the current I/& operations still need implementation,
and full I acceptance remains pending (0/45 complete).

Native CMake Release build (/usr/bin/cc, build/) passed without compiler/source diagnostics. Nine selected native tests passed in26.42s, including full wren_host, bbs_list, term, connection, screen_mode, app_session_mode and BIG state/profile/input checks. Four application regressions passed in13.53s: BIG input, IGS init/editor, CGA mode and BBS setters. After adding the unsupported-profile case, cterm_big and syncterm_big_input passed again in14.23s; the application test checks84 independently expected scenes across28 connections plus editor persistence.


## Common control/footer integration

The live CTerm owner now performs original repeat-before-footer ordering,
finishing XON, SPACE/X cleanup, chain deferral and counted input replies after
recognized and unknown commands. Gang selection consumes the shared numeric
source before publishing its selected command; it can end looping and leave
remaining fields to wire input. Repetition retains frame/page locals and
acquires no new pair deadline. Unknown handlers suspend through a new optional
shared-parser callback; existing notification-only users retain their behavior.

`big_control` checks both full WORD count domains, an actual32767-byte flow,
raw NUL/ff/pipe/tilde replies, current count/data/CR reads, readiness/send waits,
errors and idempotence. `cterm_big` checks real body repetition/gang seams,
byte/body work budgets, user versus response output, partial effects, reset/
EOF and pair deadlines after finishing flow. The existing application fixture
checks unknown chaining and coherent SPACE/footer/pending state in84 scenes.

The native transport queues output; no foreign UART or DOS capture UI is
reproduced. Defined gang suffix initialization and bounded reply access replace
unsafe foreign stack/address escapes. A malformed count fails at the first
out-of-range access after retaining its emitted prefix. Reset/EOF clears
pending work without fabricating data or running drawing defaults.

< production, W rendering and music synchronization remain pending. No command is marked complete (0/45).

Native CMake Release full build (/usr/bin/cc, build/) passed without compiler/source diagnostics. All90 selected BIG/shared-parser/IGS/RIP/SkyPix/terminal/session CTest checks passed in104.50s. BIG/IGS coherent application tests both passed in14.64s; the BIG fixture covers84 scenes. The strengthened post-flow pair-deadline case passed with big_control/cterm_big in2.59s. All9 final focused shared-parser/BIG/IGS checks passed in2.54s after appending the optional callback to preserve prior member offsets.

## Native stored-loop keys

Production numeric callbacks now observe native keys while an exact loop1
interpreter owns local acquisition. Delay probes do not consume; the later
checkpoint consumes one byte after calculation. Raw03/18 ends repetition
without dropping that result or its boundary/index/final-r effects. Other
bytes do not cancel. Extended keys return zero followed by a pending scan;
the pending DOS scan does not itself count as a new BIOS-ready event.

IGS and BIG share native event identity and replay, with separate local
protocol consumers/queues. BIG consumed text uses CP437, raw C0 values and
question-mark substitution for unmappable Unicode. Unused events preserve
normal codepage/UTF-8/modifier handling and wait behind pending footer work.
Shared replay normalizes codepage fallback text before conversion; both BIG
and IGS independently expect ANGSTROM SIGN to become CP437 byte8f.
Reset/EOF/error clears events/scans; callback reset stops accepted-prefix
replay. Saturation of64 events drops new local keys with a warning instead
of leaking them remotely. These are explicit bounded native adapter policies,
not foreign interrupt or arbitrary device-state emulation.

`cterm_big` independently checks cancellation/current-value application,
non-cancelling endpoints, zero/scan readiness order, all four native entry
kinds, Unicode non-truncation/normalization, function-key modifiers, blocked
XON, queue saturation and reset/EOF/replay recovery. The added test failed
before implementation. The standalone key fixtures inject setup state; wire & is covered below.
Interactive pointer/device/audio coverage remains pending.

Native CMake Release full build (/usr/bin/cc, build/) passed without compiler/source diagnostics. All91 selected BIG/shared-parser/IGS/RIP/SkyPix/terminal/session checks passed in406.40s; both coherent BIG/IGS application tests passed in25.60s. After the shared codepage-normalization correction and final full rebuild, all16 focused BIG/IGS keyboard/session checks passed in17.02s. No physical keyboard/pointer/audio or & wire acceptance is claimed.

## Wire loops and auxiliary/text sources

Initial & now performs immediate header stores, original step normalization,
raw target/type acquisition, bounded gang copy, P1..2048 table loading and
local load-abort zero filling. It publishes counters/loop1 and clears random
suppression before XOFF, then classifies varying words and enters the existing
common body. Selected & skips setup. Header/P inherited suppression, stored r,
RNG history and unused table suffixes retain their distinct lifetimes.

The shared BIG auxiliary/text component preserves local per-read versus
whole-call deadlines, late-byte consumption, SPACE/auto/BEL policy, text
reuse/Z/first-pass XON,128 stored bytes plus NUL, opaque embedded bytes and
unterminated timeout suffixes. Gang selectors still use0..127;129-byte gang
storage includes a full string's NUL. Missing NUL in the owned text extent
fails before unsafe copying, preserving completed setup prefixes.

`big_stream` and `cterm_big` independently verify split/cap/timeouts/reuse,
all2048 table slots, bounded work, raw types, header/step/count stores,
suppression/RNG history, fixed/gang bodies, setup cancellation that still
executes zero slots, runtime cancellation, partial-pair entry, blocked XOFF
and EOF/reset/callback recovery. The initial wire case failed before support.

The application fixture checks98 coherent scenes, including56 fixed/gang
wire loops and14 delayed-loop Ctrl-C cases with exact19/17 flow replies,
current-value effects, no leaked cancellation and buffered-tail resumption.
This caught a session adapter bug treating a fully accepted paused command
as fatal; only full acceptance with a paused owner is now normalized there.
< production, W rendering and music synchronization remain pending. No
additional full command completion is claimed (0/45).

Native CMake Release configure/full build (/usr/bin/cc, build/) passed without compiler/source diagnostics. All93 selected BIG/shared-parser/IGS/RIP/SkyPix/terminal/session/OOII CTest checks passed in406.84s; both final coherent BIG/IGS application tests passed in28.34s. BIG covers98 independent scenes,56 fixed/gang wire loops and14 delayed native Ctrl-C loops. After making retained-tail fixtures use permanently unknown codes, the final full build and big_stream/cterm_big checks passed in3.40s. No physical input/audio or full command acceptance is claimed.

Wire & setup/shared auxiliary-text reader integration commit: `b12aa6f`.
Full command acceptance and the complete goal remain in progress.

## Wire R modes

R uses one unscaled full-WORD argument and the established state operator,
with no same-mode shortcut. Normal VGA resolutions1/2/3 select the200/350/480
pixel surfaces and original8/14/16-row ROM banks. Driver3 downgrade, returning
rejection/state2 cache publication and the unconditional viewport/point tail
remain separate from physical installation. Application attributes, custom
patterns/ratios, numeric/RNG/flow state and frame locals survive R.

`cterm_big_resolution` failed before binding R and now independently checks
split/bytewise CP437/UTF-8/status cases, full indexed A/block scenes, geometry/
font/cell/cursor state, same-mode reinstall, invalid full WORDs, class0 g3
ownership, retained owners, deferred replies, wire-loop R1..3 and partial
EOF/reset/timeout. Injected driver/max-mode/state cases retain cache/surface
mismatches and completed return prefixes.

The expanded application fixture checks168 coherent scenes;70 new R scenes
cover1/3/2/repeated2/invalid258, each with a preexisting coloured sentinel and
new glyphs without a post-R clear. Exact indexed blink planes, ROMs, palettes,
cells/cursor/state and RGB samples check transitions/defaults and erasure.
Full G storage/input/drawing/music composition is still pending; R remains
in progress, with no new full command acceptance claimed (0/45).

Native CMake Release configure/full build (/usr/bin/cc, build/) passed without compiler/source diagnostics. All24 selected BIG/shared-parser/terminal/IGS/RIP/SkyPix/session/screen-mode native checks passed in29.19s. BIG/IGS/CGA application regressions all passed in33.69s; BIG covers168 coherent scenes, including70 R transition scenes. The initial R wire expectation failed before implementation. No physical audio/device or full command acceptance is claimed.

Wire R/native resolution binding integration commit: `0e3835c`.
R and the full45-entry goal remain in progress.

## Wire g scaling selection

g stores one complete unscaled WORD, without validation or an immediate BGI
effect. I0/I1/R retain it. Exact flags2/3 and matching cached modes2/3 govern
the existing ordinary class2 reader; other flags/classes and stored operands
do not receive that ordinary scaling.

Native tests failed before registration and now cover split/bytewise input,
WORD encodings/overlong input, own class0 acquisition, I/R retention,
partial EOF/reset versus timeout, delayed stored g1/2/3 and deferred replies.
Wire-acquired state drives the production numeric callbacks: Y5 gives8 or11,
class0/1 stays5, and a rejected R/cache3 uses g3 even with a350-line surface.
These are explicit reader probes, not a synthetic live drawing command.

70 added coherent g scenes bring the application fixture to238 scenes,
checking complete flags, I1/R retention and unchanged indexed pixels/fonts/
palette/cells/cursor in one capture. Coordinate drawing consumers, input and
audio/music composition remain pending; g is in progress (0/45 complete).

Native CMake Release configure/full build (/usr/bin/cc, build/) passed without compiler/source diagnostics. All24 selected BIG/shared-parser/terminal/IGS/RIP/SkyPix/session/screen-mode native checks passed in29.98s. BIG/IGS/CGA application regressions all passed in36.43s; BIG covers238 coherent scenes, including70 new g scenes. After strengthening independent console-cursor/BGI-point retention expectations, the final build and cterm_big check passed in4.37s. No live coordinate drawing consumer, physical device/audio or full command acceptance is claimed.

Wire g/scaling selection integration commit: `efc438d`.
Live coordinate consumers and the full45-entry goal remain in progress.

## X dispatch and X6 steps

X uses an unscaled full-WORD selector. Exact6 acquires class1 X then class2 Y;
unknown selectors consume no guessed extension operands and retain shared
repeat/chain/reply behavior. Other known selectors are explicitly unsupported,
not accepted implementations. Parent X cannot complete before all nine children.

X6 stores X before Y waits, stores Y on return, then performs four ordered
signed checks to1..15 against current fields. Application steps initialize
to12/6 and survive I/R/frame entry. EOF/reset can retain an unclamped X
prefix. No point movement or host mouse-speed change is made. Interactive
keyboard/mouse pointer acquisition remains pending.

Native expectations failed before registration and now cover splits/bytewise,
encodings/status, WORD clamps, defaults/points/lifetime, partial/current
stores, source transitions, unknown selectors, replies and EOF/reset/timeout.
Live g2/R2 and g3/R3 X6 Y5 produces8 and11; stored Y5 stays5, even when its
read ends the loop.70 added application scenes bring coherent coverage to308,
checking exact retained pixels/fonts/palettes/cells/cursor alongside step state.
X6 and the parent remain in progress; full command acceptance remains0/45.

Native CMake Release configure/full build (/usr/bin/cc, build/) passed without compiler/source diagnostics. All24 selected BIG/shared-parser/terminal/IGS/RIP/SkyPix/session/screen-mode native checks passed in31.86s. BIG/IGS/CGA application regressions all passed in39.92s; BIG covers308 coherent scenes, including70 X6/default/retention scenes. Independent wire-ownership expectations failed before implementation. Other known X selectors and interactive pointer/audio composition remain pending; no full command or physical-device acceptance is claimed.

X dispatch/X6 keyboard-step integration commit: `b94c237`.
Other X selectors, pointer input and the full45-entry goal remain in progress.

## X4 counted zone loading

X4 now implements ordinary definitions and all three special ids, retaining
its application zone/payload/loopback owner across I/R and frame entry.
Coordinates commit one at a time; class2 uses ordinary g scaling while stored
values bypass it. Lengths signed-clamp to1..80. Only exact current loop1
requests an early XON after the length store, before fresh raw deadlines.
Payloads are opaque, including NUL/ff/I#, with retained tails and one extra
unconditionally discarded helper read. Live lengths are reread between calls;
an already admitted extra read stays a discard. Work budgets yield without
truncation. Reset/EOF preserve completed stores without inventing a tail.

The original negative-id DOS-memory overwrite is replaced by OUT_OF_RANGE
at the first coordinate store, after consuming X1. A corrupt unsigned length
may cross records within the flat3888-byte payload table; an actual escape
fails after consuming that helper byte, preserving the accepted prefix.

Independent wire expectations failed before implementation. Native tests
cover partitions/bytewise input, encodings/status, initial zero tables,
specials/steps/lifetimes, clamps/scaling/source changes, deadlines/flow waits,
partial errors/reset/EOF, live lengths, retained tails and bounded corruption.
84 additional coherent application scenes bring coverage to392, checking zone
state and opaque data alongside the complete unchanged indexed scene, cells,
palette/cursor/mode and RGB samples. Pointer acquisition/hit testing and music
composition remain pending; neither X4 nor parent X is fully accepted yet.

Native CMake Release configure/full build (/usr/bin/cc, build/) passed without compiler/source diagnostics. All24 selected BIG/shared-parser/terminal/IGS/RIP/SkyPix/session/screen-mode native checks passed in9.89s. BIG/IGS/CGA application regressions all passed in29.63s; BIG covers392 coherent scenes, including84 zone scenes. After adding explicit chain/deferred-reply, X4-clear step retention and ordinary/admitted-key ownership checks, the final full build and cterm_big_resolution passed in9.55s. Pointer acquisition/hit testing and music composition remain pending; no full command or physical-device acceptance is claimed.

X4 counted-zone producer integration commit: `cc974bc`.
Pointer input/hit testing and the full45-entry goal remain in progress.

## T attributes and P marker drawing

RIP and BIG now share the extracted thin candidate/pattern loop and four
builtin line patterns. BIG keeps original WORD translation, fixed-slope
clipping, overflow rejection, center/minus/plus thickness and separate public/
DDO/device points. The direct helper preserves seven live type tests, size
reads, connected-edge/ray order, inherited line settings and XOR duplicate
visits. Crosshair ends at its right tip; type0 has no helper geometry.

P selects colour before acquisition, uses class1/2 coordinates, fresh copied
marker0 pixels, the independent nonzero helper test and a final requested
anchor move. T publishes the normalized type byte before waiting for size;
line attributes use low-WORD multiplication and original width remaps.
Rejected width records BGI-11 and retains installed settings, allowing later
drawing. Reset during a timeout notification prevents remaining command tails.
The console backend preserves distinct indexed pens and both blink planes.

Safe corrections replace original divide traps/clip cycles, uninstalled
DOS exits and actual native pixel escapes with checked errors and retained
prefixes. Unexpected asynchronous drawing-sink returns are protocol errors.
Literal `big_draw` expectations cover all shapes/points/XOR/patterns/clip and
failure cases. Native wire partitions cover modes/encodings/status, sources,
scaling, partial effects, reset/EOF/timeouts and attribute lifetimes.
392 new coherent application scenes bring coverage to784, including all eight
markers in all three modes and pattern/rejection/zero-mask/width2 composition.
Pointer acquisition, page binding and remaining consumer/audio composition
remain pending; T/P are still in progress in the full45-entry ledger.

Routine goal development now runs BIG-specific tests only, as requested;
non-BIG regressions are deferred to the final full-suite verification.

Native CMake Release configure/full build (/usr/bin/cc, build/) and final focused BIG target build passed without compiler/source diagnostics. All12 BIG-specific CTest checks passed in46.04s, including784 coherent application scenes. Before the BIG-only iteration request, shared-kernel validation passed37 selected native checks in15.58s and13 BIG/IGS/CGA/RIP application suites in46.44s, including all ten RIP suites with unchanged expectations. Subsequent iteration is BIG-only until final full-suite verification. T/P integration is implemented; pointer acquisition, page binding and remaining drawing/audio composition are pending, with no full-command/device acceptance claimed.

Shared thin-line/BIG polymarker and wire T/P integration commit: `a2d67a7`.
Interactive input, page binding and the full45-entry goal remain in progress.

## Keyboard and zone collection operators

`big_collect` implements < entry/options/count semantics, raw keyboard byte
handling and signed first-hit/default/strict zone lookup as a component.
It preserves the129-byte keyboard bound, DEL underflow/position0, deleted
tails, unsigned255-byte zone copies, ordered echo requests and existing
counted deferred replies. Actual escaped accesses fail at the store/read
boundary with prior count/data/echo prefixes retained. Errors latch and
unexpected asynchronous echo requests fail instead of replaying prefixes.

Independent tests exercise all 65,536 type and option WORDs, count/deletion/
control/high-byte cases, reply/chaining/replacement, full-byte zone data,
signed overlaps, corrupt-length prefixes, cancellation and literal X6 movement
vectors. Native < framing/echo/event/pointer integration is still pending;
this does not claim keyboard/mouse or bitmap application acceptance.
Native pointer integration will use actual event order and neutral idle/unknown
input rather than reconstructing the original helper's uninitialized foreign
SI drift. The accepted keyboard/mouse/release behavior remains unchanged.

Corrupted zone lengths130..255 use the existing bounded256-byte native reply
owner instead of overwriting the original adjacent DOS syrup/loop/filename
owners. Normal keyboard positions1..129 and producer lengths1..80 keep their
ordinary count/data semantics; unsafe foreign-neighbor aliases are not modeled.

Native CMake Release configure/full build (/usr/bin/cc, build/) passed without compiler/source diagnostics. All12 BIG-specific native CTest checks passed in15.49s. After strengthening cancellation/invalid-call and printable-boundary checks, the final focused build and big_collect passed in0.11s (test body0.04s). This component introduces no native terminal behavior; the unchanged784-scene application fixture was not rerun. Native < framing, echo/event delivery and pointer cleanup remain pending; no native input/bitmap/audio acceptance is claimed.

Keyboard/zone collection operator dependency commit: `5e24fd6`.
Native < integration and the full45-entry goal remain in progress.

## Native < keyboard and hybrid pointer acquisition

All three class0 operands now run through native CTerm, including early
append-CR publication and pending/count replacement after options. Keyboard
ownership starts during operand acquisition. Following wire bytes wait for
local input, then resume through the shared loop/chain/counted footer.
Literal native echo preserves the selected console window/style and wraps
immediately; no ANSI/VT52 parser receives synthetic echo bytes.

The direct polymarker path provides fixed318,98/XOR pointer entry, original
shape/current-point/visit behavior, keyboard X6 steps and current cached-mode
restoration. Ordered native mouse and key events share the software anchor;
stationary button reports preserve keyboard movement, while valid motion uses
unquantized drawable pixels. A valid left press arms release confirmation.
UI/disabled/status/outside samples disarm it, wheel-up stays local, generation
checks suppress stale samples and overflow requires a fresh press. Enter
retains the result-value wait; private getch scan bytes remain independent.

Normal selection restores the marker/mode/host pointer before signed zone
lookup; strict miss BEL restarts the fixed origin. Reset/EOF/error/destruction
restore ownership/visibility without fabricating a reply or replaying partial
draws. Deferred mouse errors reach the next poll/write. Effective mouse mode4
exposes acquisition to existing front ends; Wren documentation is updated.

Native tests cover split input, partial state, raw keys, echo, replies/flow,
source replacement, buffered tails, pointer pixels, private scans, mixed
movement, suppression/stale/overflow cases and failure/teardown.42 added
coherent application scenes bring coverage to826 and verify real keyboard/
echo replies and hybrid releases across the three modes/encodings/statuses.
Page/image and audio composition remain pending; full acceptance stays in the
45-entry ledger. These bitmap tests do not claim physical input/audio coverage.

Mouse observations without pixel coordinates retain the last known pixel
sample, including across acquisitions; they do not fabricate motion from cell
coordinates or make a later stationary button report undo a keyboard move.

Native CMake Release configure/full build (/usr/bin/cc, build/) passed without compiler/source diagnostics. All13 BIG-specific native CTest checks passed in15.17s. The BIG application fixture passed in51.42s with826 coherent scenes; final missing-pixel/history and failure-cleanup cases are additionally covered by the native suite. Only BIG-specific tests ran for this step; the full regression suite remains deferred until goal completion. Native < framing/keyboard/hybrid-pointer/echo/reply integration is implemented. Global page/image and audio composition, physical input/audio and full45-entry acceptance remain pending.

Native < keyboard/hybrid pointer integration commit: `03e4edc`.
Page/image/audio composition and the full45-entry goal remain in progress.

## Native L/D/d lines

L now reads class1/2/1/2, D class1/2 and d class0/1/0/2 through the same
pre-ANSI parser/source path. Each installs current line colour before the
first operand; waiting acquisition preserves subsequent installed changes.
L keeps endpoint order and explicitly moves after drawing. D uses the target
first and old DDO point second, publishing DDO before clipping and final
points after return. d negates X for any nonzero flag but retains the original
Y-sign defect: its flag is negated instead of deltaY. Negative deltaY remains
negative. Existing WORD/clip/stroke/pattern/indexed COPY/XOR adapters supply
geometry; no new framebuffer or duplicate rasterizer is introduced.

`cterm_big_line` checks independent literal candidates at every split and
bytewise, all modes/encodings/status choices, colour prefixes/live changes,
source/scaling seams, repeated/chained replies, fixed-slope clipping, visible
thick sides, phase order/restart, invisible point tails, separate public/DDO
owners, per-plane XOR, partial failures and EOF/reset/timeout recovery. The
initial wire test failed before registration.70 added coherent application
scenes bring the oracle to896 with full indexed/text/palette/point/cursor/
reply/RGB observations; retained BGI error-11 does not suppress lines.

Native CMake Release configure/full build (/usr/bin/cc, build/) passed without compiler/source diagnostics. All14 BIG-specific native CTest checks passed in15.53s; the BIG application fixture passed in61.39s with896 coherent scenes. Final native line assertions for live installed colour, repetition and deferred replies passed separately. Only BIG-specific tests ran; the final full regression suite remains deferred. Native L/D/d integration is implemented, while page/image/audio composition and full45-entry acceptance remain pending.

Native L/D/d line integration commit: `0ff31e8`.
Page/image/audio composition and the full45-entry goal remain in progress.

## Native m graphics-point movement

m reads operation/X as classes0/1, then class2 Y for signed operation>1 or
class0 signY/class2 deltaY otherwise. Negative selectors take the relative
form and negate X; signY retains the original defect and does not negate Y.
Relative motion reads current public Y then X after acquisition. The existing
moveto adapter translates lower DDO/device points by the viewport origin
with WORD wrapping before publishing the unbounded logical public point.
It draws nothing and retains colours, styles, palette, installed pages and
console cursor. Partial EOF/reset leaves points unchanged; native uninstalled failure
occurs after fields, without a process exit. Common source/repeat/chain and
counted deferred replies remain in use.

Independent `cterm_big_line` cases cover split/bytewise mode/encoding/status
input, variable arity, negative/full-WORD selectors, Y-sign and negative-delta
behavior, wrap boundaries, changed current point during acquisition, origin/
point ownership, unchanged indexed planes and attributes, g3 stored/wire
seams, repeats/replies and EOF/reset/timeout/error recovery. The first wire
case failed before implementation.42 new coherent scenes bring application
coverage to938: absolute and relative m leave the scene intact, then D
draws from the moved point. Full indexed/text/palette/point/cursor/reply/RGB
expectations retain all three modes, CP437/UTF-8 and status choices.

Native CMake Release configure/full build (/usr/bin/cc, build/) passed without compiler/source diagnostics. All14 BIG-specific native CTest checks passed in15.49s, including the expanded cterm_big_line in1.13s. The BIG application fixture passed in55.70s with938 coherent scenes. Only BIG-specific tests ran; the final full regression suite remains deferred. Native m integration is implemented, while page/image/audio composition and full45-entry acceptance remain pending.

Native m graphics-point integration commit: `08e9991`.
Page/image/audio composition and the full45-entry goal remain in progress.

## Native M writing mode

M acquires one class0 WORD, normalizes only2/3 to XOR1 and all other inputs
to COPY0, changes the installed bit, then stores the application cache after
normal return. It does not draw or clear prior BGI errors. Existing raster
consumers keep their own operators: line strokes use the mode, while P dots
still copy. R restores actual COPY while retaining cache; I1 retains both,
pointer cleanup restores current cache, and I0 resets both. Uninstalled
dispatch returns native protocol error before either mode changes, preserving
reader prefixes and BGI error-1. Common repeat/chain/replies remain in use.

`big_state` exhaustively checks all65,536 WORDs against the independent2/3
predicate. Native wire cases check partitions/bytewise mode/encoding/status,
XOR cancellation versus COPY, distinct blink-plane pens, invalid/full/negative
requests, class0/source/flow, partial EOF/reset/timeout and failure/recovery.
Input tests exercise M/I1/R and cached-mode restoration after normal pointer
completion, EOF and reset, then I0. Both initial wire tests failed before
registration.56 added coherent application scenes bring coverage to994 with
exact line/dot pixels and both mode owners, including R's0/1 split. Full
indexed/text/palette/points/cursor/replies/RGB remain coherent. Other image/
page/audio composition and final full-suite verification remain pending.

Native CMake Release configure/full build (/usr/bin/cc, build/) passed without compiler/source diagnostics. All14 BIG-specific native CTest checks passed in15.67s; the BIG application fixture passed in58.34s with994 coherent scenes. All65,536 input WORDs are independently checked in big_state. Only BIG-specific tests ran; the full regression suite remains deferred. Native M state/wire/consumer integration is implemented, while page/image/audio composition and full45-entry acceptance remain pending. After strengthening the stored-repeat endpoint assertions, the final focused build and cterm_big_line passed in1.60s (test body).

Native M writing-mode integration commit: `5bfa8a8`.
Page/image/audio composition and the full45-entry goal remain in progress.

## Native C application colours

C reads selector/colour as class0 WORDs, then normalizes signed selectors
outside0..3 to marker0 and stores the entire colour WORD. It calls no BGI
setter, so installed colours/fill, errors, palette, points and pixels stay
unchanged even with uninstalled BGI. Line/marker consumers reduce their own
later values: line WORD291 installs draw BYTE23hex/device pen3, while marker
WORD261 yields pen5. Fill/text stores remain delayed. I1/R retain the slots;
I0 resets all four to15. Both reads occur for invalid selectors; incomplete
EOF/reset does not store, actual timeouts substitute normally, and common
source/chain/replies stay in use.

`big_state` checks every selector and every colour WORD in each valid slot
independently. Native wire cases cover every split/default profile, bytewise
all modes/encodings/statuses, delayed stores, reductions, invalid/full words,
source/g3, replies, lifetime and partial/error recovery. The first wire test
failed before implementation.56 new coherent scenes bring coverage to1050
with independent line/marker pens, delayed fill/text stores and selector258.
Every scene now checks all four application WORDs, public draw colour,
device pen and installed fill along with complete indexed/text/palette/
point/cursor/reply state. New scenes additionally verify composed cyan RGB
inside a solid swatch. Remaining fill/text, page/image/audio composition and
final full-suite verification remain pending.

Native CMake Release configure/full build (/usr/bin/cc, build/) passed without compiler/source diagnostics. All14 BIG-specific native CTest checks passed in15.60s; the BIG application fixture passed in60.25s with1050 coherent scenes. Component checks cover every selector WORD and every colour WORD per valid slot independently. Final varying-selector repetition/flow assertions passed in cterm_big_line in2.03s. Only BIG-specific tests ran; the full regression suite remains deferred. Native C state/wire/line-marker integration is implemented, while fill/text, page/image/audio composition and full45-entry acceptance remain pending.

Native C application-colour integration commit: `e34a731`.
Fill/text, page/image/audio composition and the full45-entry goal remain in progress.

## Native A fill selection

A stores a class0 WORD and the common helper clamps signed values outside
0..23 to1, then tests current built-in/custom selection separately. Built-ins
retain full colour WORDs and publish style/colour/lower BYTE before dispatch;
customs reject unsigned colours>15 before those changes. Accepted custom
sources have independent forward library/device copies. Active masks expand
at installation into an8x8 indexed pen tile, preserving colour latching and
opaque pen0 without a planar shadow. Later tuple-colour changes do not retile.
I1 retains fill, R restores default userFF/solid15 while retaining application
owners, and I0 additionally resets application selection/colours. Native
uninstalled failures retain the original reached public/device/cache prefix.

RIP/BIG now share the twelve immutable same-index masks: all96 bytes and
initializer text match RIP's previous table. `big_fill` checks independent
literal masks/tiles, every selector/colour WORD, rejected custom state,
current colour/cache separation, resubmission and actual bounded overlapping
source copies. Native A tests cover split/bytewise profiles, source/flow/
reply/partial/error/lifetime behavior and unchanged complete planes/points/
cursor. The first wire test failed before registration.56 coherent scenes
bring coverage to1106, adding selected/public/lower/device fill and cached
rows to the full indexed/text/palette/points/cursor/replies/RGB oracle.
Bar/flood/sector consumers, X1 edits and remaining global composition stay
pending. Only BIG tests run during iteration; RIP regressions and the full
suite remain deferred.

Native CMake Release configure/full build (/usr/bin/cc, build/) passed without compiler/source diagnostics. All16 BIG-specific native CTest checks passed in15.59s; the BIG application fixture passed in62.81s with1106 coherent scenes. All96 shared fill-mask bytes and initializer text match the previous RIP table exactly; independent BIG tests cover all selector/colour WORDs and literal indexed cache rows. Only BIG-specific tests ran; RIP and other regressions remain deferred until the final full suite. Native A fill state/wire/default integration is implemented. Rendering consumers, X1 edits, text, page/image/audio composition and full45-entry acceptance remain pending.

Native A fill/shared-mask/indexed-cache integration commit: `3e0da7e`.
Rendering consumers, X1 edits and remaining full45-entry composition stay in progress.

## Native X1 custom-pattern edits

X1 reads slot/alter before normalization, requests exactly eight class0 row
values only for alter1 and otherwise restores the selected original row.
Slots outside12..23 become12; invalid flags become0 without draining row
payloads. Each low-BYTE store precedes the next acquisition. A fixed edit
loop survives source changes and a separate final flag test can restore
after editing. Live frame slots are checked at the actual access; errors
latch, and cancellation retains accepted application prefixes.

The operation changes no installed/library/device fill state, colour, write
mode, point or pixel, including with uninstalled BGI. I/R retain edited rows;
A explicitly installs their later contents. `big_fill` checks full slot/alter
domains, all restore destinations, prefixes/live locals/error/cancel and
unchanged BGI state. Native tests cover partitions/bytewise profiles, special
values, delayed reads/EOF/reset/timeouts, source seams within the eight reads,
all11 supplied fields, repeats/replies and delayed A. The first wire test
failed before implementation.56 coherent scenes bring coverage to1162 with
edit/install/restore/resubmit, retained slot bytes and editor ownership plus
the complete indexed/text/palette/points/cursor/reply/fill-cache/RGB scene.

Native CMake Release configure/full build and final focused build (/usr/bin/cc, build/) passed without compiler/source diagnostics. The full build emitted generated-dependency timestamp jitter notices; changed objects were verified newer than their sources, and the final focused build was clean. All16 BIG-specific native CTest checks passed in15.75s; the BIG application fixture passed in64.93s with1162 coherent scenes. The final first-row source-transition case passed in cterm_big_fill in0.84s. Only BIG-specific tests ran; the full regression suite remains deferred. Native X1 edit/restore and delayed A integration are implemented; remaining global composition and full45-entry acceptance stay pending.

Native X1 edit/restore integration commit: `3fd8fa6`.
Rendering consumers and remaining full45-entry composition stay in progress.

## Original opaque bar primitive

`st_big_draw_bar` now supplies original WORD translation, signed rectangle
sort/reject/clamp and raw unsigned sorting. It uses the current device fill
pen and active absolute8x8 mask rather than the latched tile. Holes copy pen0;
line XOR/style/width are ignored, and all three point owners remain unchanged.
Edge foreground/background and middle clear/foreground passes retain their
original order. The native indexed adapter expands masked bytes left-to-right
and retains accepted prefixes on errors; it does not emulate planar latches
or add a planar framebuffer. Escaping native geometry/pen values are rejected.

`big_bar` independently checks literal clip/pass traces, every mask byte and
endpoint phase, absolute diagonal rows, all pens, wrapped/inverted geometry,
current pen versus stale cache, row-read lifetime, unchanged state and native
failure prefixes. Current RIP bar reuse was reviewed; its matching mask data
remain shared, while its combined clipping/scaling/output differs from BIG.
This primitive has no wire binding yet. B's complete bar3d/input/attribute
paths and remaining global composition are pending. Only BIG-specific tests
run during iteration; the final full suite remains deferred.

Native CMake Release configure/full build and final focused build (/usr/bin/cc, build/) passed without compiler/source diagnostics. The full build emitted generated-dependency timestamp jitter notices; changed objects were verified newer than sources and the final focused build was clean. All17 BIG-specific native CTest checks passed in15.55s; the final big_bar check including every pen under COPY/XOR passed in0.23s (test0.17s). No non-BIG tests ran. The unchanged application fixture was not rerun for this unbound primitive; final full-suite verification remains deferred. Wire B/bar3d and global composition remain pending.

Original opaque bar primitive commit: `44b6e10`.
Wire B/bar3d and remaining full45-entry composition stay in progress.

## Shared point producer

`st_big_producer_emit` now implements DDO0615's mode0 carry-clear bypass,
nonzero/non2 run collector and mode2 physical point return. The collector
retains first-point duplicate suppression, repeated non-first points,
8001 closing markers, WORD run wrap and pre-check increments. Original
capacity overflow sets BGI-6 and returns normally; checked native extent
failures stop before later tails. Configured bytes/6 capacity, actual
borrowed storage and stage0 reset have separate owners.

Mode2 returns carry clear on acceptance or rejection, loads DL before
clipping, and preserves the actual changed AX/BX/CX/DX point-return words.
Unbatched AX0f02 differs from batched segment-high/pre-write-byte AL. The
batched byte comes through a synchronous host-read boundary for later
indexed integration; no planar framebuffer is added. The producer does
not move graphics CP and ignores line XOR/pattern/width for its point.

`big_producer` independently checks all mode bytes/configured buffer-size
WORDs, literal duplicate/marker schedules, overflow/error prefixes, all
bit phases/read/colour BYTEs, physical clipping, point owners, captured
input and synchronous callbacks. This remains component coverage: native
read-byte integration, lower emission gates, bar3d, buffer allocation/flush,
wire B and the remaining curve/global composition are pending. Only BIG
tests run during iteration; the full suite remains deferred.

Native CMake Release configure/full build and final focused builds (/usr/bin/cc, build/) passed without compiler/source diagnostics. The full build emitted generated-dependency timestamp jitter notices; changed objects were verified newer than sources and final focused builds were clean. All18 BIG-specific native CTest checks passed in15.75s. Final expanded big_producer checks passed in0.10s (test0.01s). No non-BIG tests ran. The unchanged application fixture was not rerun for this unbound component; final full-suite verification remains deferred. Native indexed byte-read binding, lower gates/bar3d/curve and global composition remain pending.

Original shared point producer commit: `404d4c1`.
Native B/bar3d/curve and remaining full45-entry composition stay in progress.

## Native B bars and boxes

B acquires four scaled coordinates and unscaled kind before installing
current line colour/fill. It independently selects direct bar0, depth0
bar3d1 or signed kind>1 plus a sixth unscaled top read. A waiting sixth
read retains attributes; current kind supplies depth when that read returns.
Negative kinds draw nothing, rejected custom fills retain the old installed
mask/pen, and H/old BGI errors do not suppress geometry. Existing numeric,
source/deadline, footer/flow/reply and cancellation owners remain in use.

Bar3d preserves unsigned public sorting, lower initial move, signed inset
and second sort, original stroke phase/order, wrapped depth, orientation,
top BYTE and all three point owners. Collector and point-return paths use
the shared producer; changed projection registers are retained. Returning
colour resubmission uses the current tuple, with no tile rebuild or forced
line mode. Native errors retain prefixes and skip unreached tails. Batched
read bytes are reconstructed from PRIMARY indexed bits in the same console
transaction; no planar framebuffer is added. Physical page/image and curve
buffer allocation/flush remain separate pending dependencies.

`big_bar3d` checks literal masks/visits/points, projection boundaries,
collectors, point-register quirks and failures. `cterm_big_bar` compares full
indexed blink planes at every split/default profile and bytewise across all
three modes/encodings/status settings, with prefixes/timeout/EOF/reset,
recovery, current depth, scaling/source/repetition/replies and all four native
read planes. The initial wire case failed before registration.84 application
scenes bring coverage to1246, retaining coherent complete scene/state/replies
and RGB checks while adding XOR corner parity, top/CP changes and opaque
checker holes over prior nonzero pixels. B integration is implemented;
remaining global composition and full45-entry acceptance stay pending.
Routine verification remains BIG-only; the final full suite is deferred.

Native CMake Release configure/full build and final focused builds (/usr/bin/cc, build/) passed without compiler/source diagnostics. The full build emitted generated-dependency timestamp jitter notices; changed objects were verified newer than sources and focused builds were clean. All20 BIG-specific native CTest checks passed in17.62s. The application fixture passed in67.91s (test67.87s),1246 coherent scenes including84 new B scenes. Final live-attribute-prefix cases passed in cterm_big_bar in1.47s (test1.40s). No non-BIG tests ran; the final full suite remains deferred. Native B/bar3d/indexed point-read integration is implemented; page/image/curve/audio and full45-entry composition remain pending.

Native B/bar3d/indexed-point-read integration commit: `93e76fd`.
Final explicit-standard-header build was clean; big_bar3d and cterm_big_bar
passed together in1.45s. Remaining full45-entry composition stays in progress.

## Synchronous indexed flood kernel

`st_big_flood_fill` now implements original WORD seed/clip/boundary handling,
right-then-left spans, UP-before-DOWN LIFO, previous-span containment and
already-correct-pattern suppression. Zero pattern rows retain unconditional
directional propagation. Clip endpoints narrow adjacency, inverted scans
still probe, and popped boundary seeds still follow their left-span path.
Style0/1 versus cached-pattern mode, absolute phase, opaque pen0 holes and
whole-band plane-mask write order have separate owners. Line XOR and RGB
palette aliases do not change the indexed comparisons or COPY result.

Rendering is synchronous, with no work-budget yields or blocking waits in
framebuffer updates. Callback failures retain accepted prefixes; unexpected
IN_PROGRESS is a protocol error. WORD addresses use checked native backing,
and a repeated right-byte cursor fails OUT_OF_RANGE instead of hanging.
Borrowed bounded records replace unsafe SS:BP underflow, explicitly changing
the heap-placement-dependent capacity. Refusal preserves pre-push state,
finishes the other direction/pending work and reports BGI-7 on normal return.
Native failures skip unreached tails. No DOS stack/controller/interrupt or
persistent planar framebuffer is introduced.

`big_flood` checks literal masks/spans/pass order/pixels, every mask/pen
combination, partial plane results, cache snapshots, palette-index identity,
edge/inverted/narrow probes, fresh reads, pending-boundary continuation,
overflow and failure/interruption prefixes, recovery and unchanged points.
These are component checks; the following section records native F wire/flow
binding and coherent captures. Z and page/padding/configured-buffer binding
remain pending. The unchanged application fixture was not rerun at the
unbound kernel checkpoint. Only BIG tests run during iteration, with the
full suite deferred until the end.

Native CMake Release configure/full build and final focused builds (/usr/bin/cc, build/) passed without compiler/source diagnostics. The full build emitted generated-dependency timestamp jitter notices; changed objects were verified newer than sources and final focused builds were clean. All21 BIG-specific native CTest checks passed in15.40s. Final expanded big_flood checks passed in0.08s (test0.01s), including post-overflow pushes and malformed-record guards. No non-BIG tests ran. The unchanged application fixture was not rerun for this unbound kernel; F/Z wire/flow/page/resource binding and final full-suite verification remain pending. Rendering is synchronous without periodic work-budget yields or blocking waits in framebuffer updates.

Synchronous indexed flood kernel commit: `b76f7ef`.
Z, configured resources/pages/padding and remaining full45-entry composition
stay in progress; native F visible-page coverage is recorded below.

## Native F and nonfatal BIG input errors

F now binds production class1/2/0 acquisition, signed coordinate/selector
clamps, fill-only attribute installation, current selected C colour after
XOFF and independent live loop tests around XOFF/XON. H and drawing colour
do not gate it. It uses the indexed flood kernel in the visible console,
with checked byte reads, masked pen-bit writes on both blink planes and
unchanged public/DDO/device points.512 native frontier records represent
the nominal4096 logical bytes; bounded capacity intentionally replaces
unsafe DOS stack/heap placement. Off-screen page/padding/configured buffer
composition and Z remain pending.

Command errors are nonfatal warnings. They cancel pending parser/source/
input/flow/footer work, retain accepted state/pixels and process unread
bytes normally, including the first unread byte of a failed continuation.
Poll/deferred pointer errors and EOF cleanup cannot turn these rejections
into session failures. Last-error/rejection-count inspection preserves
diagnostics. Invalid API calls and actual connection failures retain their
own error handling.

Native tests cover every split/default profile and bytewise all modes,
encodings/status states, selectors/clamps, current state across readiness
waits, loop/source/reply ordering, EOF/reset/timeout, bounded overflow,
host-error prefixes, recovery and independent indexed blink-plane writes.
The previously failing F and rejection-recovery cases pass.70 additional
application scenes bring coverage to1316, checking solid/checker/empty
fills and a negative X4 followed by F, ANSI and DSR on the same connection.
Each capture checks full retained indexed/text/palette/state/points/cursor,
diagnostics, replies and composed RGB. Full45-entry acceptance remains0/45;
only BIG tests run during development and the final full suite is deferred.

Native CMake Release configure/full build and final focused builds (/usr/bin/cc, build/) passed without compiler/source diagnostics. The full build emitted existing clock-skew notices; changed BIG objects were verified newer than sources and the final focused build was clean. All21 BIG-specific native CTest checks passed in15.31s. The application fixture passed in73.98s (test73.93s),1316 coherent scenes including70 new F/recovery scenes. Final expanded cterm_big and cterm_big_fill checks passed in6.26s (test4.72s/1.47s). No non-BIG tests ran; the final full suite remains deferred. Native F visible-page integration and nonfatal BIG command recovery are implemented; Z, pages/padding/configured buffers, curves/audio and full45-entry composition remain pending.

Native F and nonfatal command recovery commit: `e2f363e`.

## Native H hollow-state selection

H now reads one class0 value, stores the full WORD and reloads it for a
signed-negative clamp to0. Nonnegative values have no upper clamp;256 stays
256. It has no BGI/point/fill/colour/palette/audio operation. Loaded0,
incomplete/EOF/reset/timeout prefixes, I's post-selector reset and R/entry/
footer retention follow the original43/51 callers. Repetition, flow,
chaining and blocked/deferred replies use the common footer after the store.

`big_state` checks all65536 WORD values and every other retained state byte,
including uninstalled BGI and an old error. Native tests check partitions,
all modes/encodings/status states, full indexed planes, acquisition and
reply lifetimes.28 added coherent scenes bring coverage to1344, proving
H256 and negative0 preserve the complete prior pixels/text/palette/points/
fill/cursor/replies/RGB and rejection diagnostics. The H wire case failed
before registration. Native H is implemented; its Z/O/Q/V/Y consumers and
full45-entry acceptance remain pending. Verification stays BIG-specific.

Native CMake Release configure/full build and final focused build (/usr/bin/cc, build/) passed without compiler/source diagnostics. Existing full-build clock-skew notices remain; changed objects were verified current and the final focused build was clean. All21 BIG-specific native CTest checks passed in15.73s, including cterm_big in5.20s. The application fixture passed in73.97s (test73.92s),1344 coherent scenes including28 new H scenes. All65536 H WORDs retain independent expectations. No non-BIG tests ran; final full-suite verification and H consumers/global45-entry composition remain pending.

Native H state/wire integration commit: `3b46c71`.

## Native Z outline and conditional flood

Z installs current line/fill only after all four class1/2/1/2 reads. Its
four absolute standalone edges retain order, fresh phase, repeated corners,
original clip/thickness and all point owners. First X/Y are captured; later
arguments stay live between edges. After outline, current full H==0 calls
the indexed flood kernel with the current full line colour and WORD-wrapped
first corner+(1,1). It does not sort the seed or add F's own flow calls.

Independent component traces and native full-plane tests cover phase/order,
H0/1/256, solid/checker, reversed outside filling, clipped/wrapped seeds,
thick-seed rejection, zero-pattern escape, XOR corners and degenerate four
visits. Split/profile, source/g3/loops/replies, acquisition/EOF/reset/timeout,
current attributes, invalid fill, bounded overflow and failure prefixes with
nonfatal recovery are checked.84 application scenes bring coverage to1428,
retaining the entire prior scene/state/points/replies/diagnostics/RGB in
coherent captures. Native Z and H's Z consumer are implemented; other H
consumers, page/padding/configured buffers and full45-entry acceptance remain
pending. Routine verification stays BIG-specific.

Native CMake Release configure/full build and final focused build (/usr/bin/cc, build/) passed without compiler/source diagnostics. Changed objects were verified current; both the full and final focused builds were clean. All21 BIG-specific native CTest checks passed in16.34s. The application fixture passed in77.19s (test77.14s),1428 coherent scenes including84 new Z scenes. Final big_draw/cterm_big_fill/cterm_big_bar checks passed in5.15s (test0.00s/1.39s/3.69s). No non-BIG tests ran; the final full suite remains deferred. Native Z visible-page and H-Z consumer integration are implemented; other H consumers, pages/padding/configured buffers, curves/audio and global45-entry composition remain pending.

Native Z outline/fill integration commit: `89d360a`.

## Indexed video address/storage dependency

The new borrowed indexed-video component separates CPU segment+WORD
addressing, physical backing and the authoritative visible bitmap. Visible
addresses use that bitmap directly; stale backing at those addresses is not
a second authoritative framebuffer. Off-screen addresses use actual backing,
with checked aperture/extent failures. Display intervals can wrap separately
from CPU accesses. Save/load preserves both blink tokens and padding, with
explicit accepted prefixes and caller-owned view/allocation lifetimes.

Profiles now distinguish BIOS visual4000/8000/a000 from BGI active4000/8000/
9600. The low-BYTE active helper and signed-BYTE BIOS display-start helper
retain original WORD reductions; native B point-return segment metadata
uses the former. No planar shadow, controller or physical timing model is
introduced. Tests cover literal aliases/bounds, all524288 canonical CPU
pixels, all WORD pitches with representative page BYTEs, every pixel of40
display layouts, padding, distinct blink values, view authority, page
preservation and failure prefixes. G wire/page/image/console/presentation
binding and resource lifetimes remain pending. This is dependency coverage;
the unchanged application fixture was not rerun and no new scene is claimed.

Native CMake Release configure/full builds and final focused build (/usr/bin/cc, build/) passed cleanly without compiler/source diagnostics or clock-skew notices; changed objects were verified current. All22 BIG-specific native CTest checks passed in15.70s, including big_video in0.27s and cterm_big_bar in3.60s. Expanded address/window checks also passed alone in0.33s (test0.26s). No non-BIG tests ran. The unchanged application fixture was not rerun for this dependency, and no new scenes or G wire/native page/image acceptance are claimed. Active/visual/console/presentation/resource binding and full45-entry composition remain pending; the final full suite remains deferred.

Indexed video address/window dependency commit: `87cd9b6`.

## Native G4/G5/G6 pages

Native G4 memory/G5 display/G6 active selection now preserves the original
operation-return rez3 reset, distinct full-WORD clamps and local publication
before physical calls. Frame locals, BGI/public prefixes, actual active page
and actual BIOS display start remain separate. Fresh attention resets frame
1/0/0 without touching physical pages; chains/repeats retain the frame.

Owned indexed backing and the authoritative visible bitmap retain both
blink values and padding. P/pointer markers, L/D/d, B/Z/F and batched point
reads use active memory; console pixels/cursors use visual memory. Four
normalized cursor banks and BIOS/BGI stride differences/aliases have native
tests. Logical cell metadata/ANSI state remains with the base emulator and
pixel-managed graphics. Host status artwork is excluded from saved pages,
while source pixels behind it remain in backing. Successful mode installation clears backing
and selects physical0; I1 retains pages and pixels. Error/EOF/reset paths keep accepted
page/pixel prefixes and recover without a session failure.

Independent tests cover every page WORD, all entry cache BYTEs, partitions/
profiles, current-resolution/source/loop/reply behavior, every drawing sink,
distinct hidden XOR blink values, point-read planes, cursor/console routing,
R3 offset differences, display aliases, mode resets and resource failures.
140 coherent scenes bring coverage to1568, retaining complete prior scene/
state/cells/palette/points/cursor/replies/diagnostics/RGB while checking hidden
draws, reveal, page restoration and mode clear. G0..3 image routes remain
unsupported and G/full45-entry acceptance stays in progress. Only BIG tests
run during iteration; final cross-emulation/full-suite verification is deferred.

Native CMake Release configure/full builds and final focused builds (/usr/bin/cc, build/) passed cleanly without compiler/source diagnostics or clock-skew notices; changed page/render objects were verified current. All23 BIG-specific native CTest checks passed in15.16s, including cterm_big_pages in0.74s and cterm_big_bar in5.13s. The final expanded page fixture (including D/d and pointer-marker routing) passed in0.81s (test0.75s). The final application fixture passed in82.37s (test82.24s),1568 coherent scenes including140 new page scenes; its first expanded run also passed in83.20s. No non-BIG tests ran. G4/5/6/native page routing is implemented; G0..3 images and remaining global45-entry composition are pending, with final full-suite verification deferred.

Native G4/G5/G6 and indexed page-routing commit: `d736676`.

## Image format and capture dependency

`big_image` covers the original four-byte little-endian header, row/plane
3..0 packed format and WORD size query independently of capture capacity.
The65,548-byte borrowed allocation begins at B:0004:65,532 bytes are
reachable, with16 owned tail bytes beyond the fixed segment. Cursor wrap
into allocator prefix0..3 fails at its actual access. Capture unsigned-sorts
corners, writes the header before dispatch, translates only the start and
reads lookahead even at shift0. It retains ordered partial effects, WORD
source wrap and body-before-decrement heightffff semantics. A zero size
query does not suppress capture. Normal calls retain all point/attribute/
palette/error owners.

Packed data converts at import/capture edges into padded indexed rows,
including stale tails and partially stored planes. Later source reads return
cached pens with checked original cursor addresses; they perform no packed
decoding or RGB conversion. Failed reimports cannot expose the older layout.
The memory callback contract is shared with flood fill. Native initialization
will explicitly replace uninitialized DOS RAM with zero bytes; this component
binds borrowed storage without clearing it or adding a capture-valid flag.

Independent expectations include all WORD size deltas, literal byte vectors,
all alignments and byte boundaries in three resolutions, every read-error
prefix of an unaligned capture, retained raw/indexed changes, source/cursor
wrap, malformed height, header/cache extents and uninstalled/pause/invalid
pen failures. G0..3 wire routes, native image lifetime/page routing, paste
geometry/Boolean writes and application image captures remain pending.
No new coherent scenes or complete G acceptance are claimed by this step.

Native CMake Release configure/full build and final focused build (/usr/bin/cc, build/) passed cleanly without compiler/source diagnostics or clock-skew notices. All24 BIG-specific native CTest checks passed in16.00s, including big_image in0.53s. Final expanded origin-wrap/full-WORD-width image checks passed in0.58s (test0.52s). No non-BIG tests ran. The unchanged application fixture was not rerun for this library dependency; existing1568 scenes and pending native G0..3/paste/lifetime/global acceptance remain unchanged. Final full-suite verification stays deferred.

Image format, indexed cache and ordered capture dependency commit: `0d1d737`.

## Indexed image paste dependency

`big_image_paste` tests the original physical right-end rejection, unsigned
bottom crop and ADD/JL flags, with no viewport/upper-Y substitute. The actual
source header is patched before lower dispatch and restored only on success;
errors retain the crop and accepted pixels. The device consumes cached
indices in row/plane3..0/byte order, with a destination read before every
masked Boolean write. Last-byte spill reuses the same loaded source; source
and video WORD offsets wrap independently. Missing storage, invalid low-BYTE
mode indexes, pens and stale width layouts fail at checked native prefixes.
No packed decoding or RGB conversion occurs during paste.

The shared indexed memory write applies COPY/XOR/OR/AND/NOT-source separately
to both blink destinations and retains unselected bits. Flood requests COPY.
The proved four-bit operation from reference83 is shared through
`st_bgi_pixel.h` with RIP's indexed image branch; RIP's geometry/lifecycle and
RGB fallback remain its own. This kernel does not replace BIG with RIP paste.

Independent truth-table tests cover all nibble pairs and five modes. Full
memory/trace cases check all alignments, widths around byte boundaries,
distinct blink values, row/plane order and masks. Every read/write failure
of a cropped unaligned prefix checks accepted pixels and the retained source
header. All WORD destinations against representative origins, all mode WORDs,
three physical bottoms, overflow/negative/rejected geometry, fresh zero image,
cached-source reuse, spill-before-source-error, malformed widths/heights,
cursor wrap and invalid pen/cache/header/dispatch cases have independent
expectations. Native G0..3 acquisition/page/gate/mode/ownership binding and
application image captures are pending. Existing1568 scenes remain unchanged;
only BIG tests run during development, with full regressions deferred.

Native CMake Release configure/full build (/usr/bin/cc, build/) passed cleanly without compiler/source diagnostics or clock-skew notices. All25 BIG-specific native CTest checks passed in15.47s, including big_image_paste in1.24s and big_image in0.61s. The first focused paste/flood run passed in1.29s (paste1.22s). No non-BIG tests ran; RIP/shared regressions remain deferred to the final full suite. The unchanged application fixture was not rerun for this dependency, and existing1568 scenes are unchanged. Native G0..3 wire/page/gate/mode/ownership/lifetime and full45-entry composition remain pending.

Indexed image paste and shared Boolean kernel commit: `c1e6c86`.

## Native G0..3 image routes

G now binds all seven routes. Images use dynamic mode/X/Y argument counts
through the existing numeric/pre-ANSI reader. G0/G1/G2 select local active
before mode; G3 selects memory only after its full suffix. Mode clamps at
the original time, including G2 after its destination. Independent gates
retain preceding page/query prefixes. G1 captures even at cache3 but skips
its memory paste there; other modes paste at the original first corner and
restore active only on success. G3 preserves both active calls and its
false-gate tail. G0 captures before overlapping paste. G2 uses retained bytes
without a capture-valid flag or size query. Unknown WORD operations retain
the resolution3 reset without guessed payloads. Normal completion retains
the common footer/source/flow/reply behavior.

The BIG engine owns the original65,548-byte raw allocation and a bounded
indexed cache. Defined startup zeros replace uninitialized DOS bytes;
I/R/attention/EOF/reset/error cleanup retain the image and destruction frees
it. Fresh reconnects begin with the real1x1 pen0 image. Native memory access
uses actual active/memory pages with both blink destinations and protected
status artwork. Source rows behind host UI remain separately addressable.
Full indexed backing is reserved before the console update; allocation
failure rejects before image/header mutation. Reached kernel failures keep
page/header/pixel prefixes and cannot disconnect the session.

Independent native tests cover every default split, bytewise three-mode/
encoding/status combinations, five modes with distinct blink pixels,
unaligned patterns, packed bytes, G1/G3 page/corner/cache behavior, all four
R1 pages, snapshot overlap, gate/mode/query timing, unknown selectors and
I/R/EOF/reset/reconnect lifetimes. g3 and supplied-coordinate loops, replies,
failed page/capture/paste prefixes, partial native byte writes and source
behind UI are checked.154 new coherent application scenes bring coverage to
1722, preserving complete prior scenes/state/replies while checking image
operations, stored header/payload, memory staging and R/I1 retention. G's
native binding is implemented; global composition/final full-suite acceptance
remains in progress. Routine verification stays BIG-specific.

The last image scene additionally forces a real capture-bound error and
verifies retained639/349 header, exact rejection count, unchanged full scene
and subsequent graphics/ANSI/DSR on the live connection. Native tests also
successfully paste the accepted first200 rows of that failed image after R1,
then restore its old height349 without a capture-valid flag.

Native CMake Release configure/full build and final focused builds (/usr/bin/cc, build/) passed cleanly without compiler/source diagnostics or clock-skew notices. All26 BIG-specific native CTest checks passed in16.53s. The final expanded cterm_big_images fixture, including custom pages/status/partial native writes and reuse of a failed capture prefix, passed in6.10s (test6.03s). Final syncterm_big_input passed in89.74s (test89.65s),1722 coherent scenes including154 new image scenes and live nonfatal capture failure; the earlier1708-scene run also passed in96.11s. No non-BIG tests ran. All seven native G routes are implemented; remaining global45-entry composition and final full-suite acceptance stay pending.

Native G0..3 image routes and retained ownership commit: `4617796`.

## Native S palette command

S reads two class0 WORDs before checking birch/plywood then pine/cone, shared
with G2/G0. The public setter silently rejects state0, unsigned index beyond
U16(sizeBYTE-1), or a signed-negative full value. Accepted calls store the
low BYTE, clear logical background for index0 and then publish to the native
palette. Old BGI errors are retained and do not gate calls. Backend failures
retain accepted cache/background stores and recover through the BIG owner.

The actual cache size BYTE is preserved; defaults select16 while I1 does not
change it. Local frame snapshots copy the current size/raw cache, and S does
not update saved WORDs or refresh a same-frame local. Internal I1/future X8
setters bypass S's feature tests. Corrupt count-driven accesses retain the
original preceding checks, then reject outside the native16-entry extent
instead of writing foreign adjacent/wrapped DOS data.

Raw values remain distinct from six-bit physical colors:128 caches80 but
displays0,262 caches06 and displays yellow06 rather than brown14, and positive
WORD7fff cachesff and displays3f. Native per-index palette publication preserves
stored pens, both blink values, pages, points and images. S0 changes palette0
and logical background0, without clearing pixels or selecting overscan.

Independent tests cover every WORD value/index, all cache-size BYTEs and
boundary/state/sign cases, callback/error ordering and owner lifetimes.
Native split/profile/source/loop/timeout/EOF/reset/gate/recovery cases verify
all256 raw bytes with an independent RGB formula and captured images under
palette aliases.98 new coherent scenes bring coverage to1820, including raw
cache versus physical RGB, live image recoloring, silent rejections, S0 and
I1 restoration with complete planes/cells/palette/state/points/pages/replies/
image data/RGB expectations. S is natively implemented; X8 and remaining
global45-entry/final-suite acceptance are pending. Routine tests remain BIG-only.

Native CMake Release configure/full build and final focused build (/usr/bin/cc, build/) passed without compiler/source diagnostics. The full build emitted an existing unrelated generated-dependency timestamp notice (65 microseconds in the future); all14 state-header-dependent BIG/CTerm objects were verified current and the final focused build was clean. All28 BIG-specific native CTest checks passed in17.98s, including big_palette0.15s and cterm_big_palette1.03s. syncterm_big_input passed in94.45s (test94.32s),1820 coherent scenes including98 new S/palette scenes. No non-BIG tests ran. Native S/public single-palette behavior is implemented; X8 and remaining global45-entry/final-suite acceptance stay pending.

Native S/public single-palette implementation commit: `b07e881`.

## Native X8 palette rotation

All four class0 fields precede exact1/1/1/1 capture and signed endpoint clamps.
Saved values sign-extend raw bytes; count0 restores current saved WORDs via
the shared public setter. Ordinary rotation retains the frame snapshot and
bypasses S gates. Inclusive endpoint writes, getter-after-setter order and
the stale final wrap snapshot follow the original caller. Count1 performs
two turns; the signed WORD counter wraps and count32767 has no finite end.

The original uninitialized byte beyond the local palette becomes an explicit
zero-initialized owned byte, retained across snapshots/I/R/attention. Checked
local/cache accesses reject escapes nonfatally. Local monotonic deadlines
translate the unsigned delay WORD to milliseconds; no foreign timer or
audio-clock programming is implemented. Each full delay precedes the key
result check. Extended keys, NUL, pending private scans, blocked reads,
current loop-dependent flow and independent tail restore remain distinct.
EOF/reset/error retains accepted colors without synthetic XON/restoration.

`big_palette_rotation` independently verifies every endpoint pair and every
endpoint WORD clamp, saved/sign/local ownership and failure prefixes.
`cterm_big_rotation` checks split/bytewise profiles, native RGB/indexed image
preservation, local delay boundaries and errors, count wrap, keyboard results,
flow waits/asymmetry, g3/stored sources, chaining and deferred replies.
126 new application captures bring coverage to1946 coherent scenes across
all three resolutions, encodings and status visibility. They include live
keyboard cancellation, both directions and endpoints, signed saved restore
and I1 restoration with complete state/pixels/images/replies/RGB comparisons.
Native X8 is implemented; audio/global45-entry and final-suite acceptance
remain pending.

Native CMake Release configure/full build (/usr/bin/cc, build/) passed without
compiler/source diagnostics. All30 BIG-specific native CTests passed in43.61s;
`cterm_big_rotation` took0.64s. `syncterm_big_input` passed in101.50s
(test101.45s),1946 coherent scenes. No non-BIG tests ran.

Native X8 implementation commit: `097a486`.

## Native X5 flow settings

X5 stores the class0 operation WORD, then tests the current loop WORD; any
nonzero loop forces mode1. Custom mode2/3 reads publish their byte before
the count arrives. Independent current-mode checks permit both branches
and the final signed mode>3 default reset; the raw mode remains unchanged.
Negative modes retain custom settings, and full count WORDs keep their
signed transmission behavior. X5 sends no flow itself. Existing helpers
and the common repeat/chain/deferred-reply footer consume the new settings.
I/R/attention/EOF/reset preserve completed stores; reconnect starts defaults.

`big_flow_settings` checks all operation WORDs under four loop states and
all character/count WORDs in both branches, with complete unrelated-state
comparisons and live branch changes. `cterm_big_flow` verifies splits/profiles,
partial effects, stored/wire transitions, g3, timeout/EOF/reset/error recovery,
deferred replies, NUL/ff flow and output resumption.126 new coherent application
scenes bring BIG to2072, observing settings/literal replies with the unchanged
indexed image/pixel/cell/palette/point/fill/page/cursor scene and composed RGB.
Native X5 is implemented; remaining commands/global45-entry and final
cross-emulation regressions remain pending.

Native CMake Release configure/full build (/usr/bin/cc, build/) passed without
compiler/source diagnostics. All32 BIG-specific native CTests passed in45.23s,
including cterm_big_flow0.73s. syncterm_big_input passed in107.60s
(test107.54s),2072 coherent scenes. No non-BIG tests ran.

Native X5 implementation commit: `e82e16f`.

## Native X2 random seed and range

X2 samples local wall-clock seconds and seeds the original shared CRT
state from its low WORD before reading FROM. Raw FROM is visible while TO
is read; signed repair occurs only after TO publication. Negative FROM
becomes0, TO<FROM becomes wrapped FROM+1, and equality is retained.
I/R/attention/EOF/reset preserve reached seed/range stores. Pending r draws
continue the same stream without reseeding or premature repair. Common
loop/chain/flow/deferred replies remain in effect.

Native time(NULL) provides the seed observation; monotonic waits are
independent. Historical DOS date/time/TZ/DST/environment side effects are
outside the native clock boundary. The generator and WORD arithmetic remain
original, with no host PRNG. Clock errors reject nonfatally before mutation.

`big_random` sweeps both bound WORD domains with boundary partners.
`cterm_big_random` checks split/bytewise profiles, low-WORD seed conversion,
raw partial ranges, stored/wire r and pending rejection, g3, wrap/equality,
lifetimes, timeout/error recovery and unchanged full indexed/RGB/image/state.
126 new coherent application scenes bring BIG to2198, verifying range/seed
state and a shared random draw alongside complete prior scene/reply data.
Native X2 is implemented; X0/speaker and global45-entry/final regressions
remain pending.

Native CMake Release configure/full build (/usr/bin/cc, build/) passed without compiler/source diagnostics. All33 BIG-specific native CTests passed in50.47s, including cterm_big_random0.69s. syncterm_big_input passed in114.43s (test114.36s),2198 coherent scenes. No non-BIG tests ran. Native X2 is implemented; X0/speaker/global45-entry and final regressions remain pending.

Native X2 implementation commit: `125d524`.

## Native X0 spray drawing

X0 installs marker colour and reseeds before five separate coordinate/range/
concentration reads. Signed concentration controls attempts; each advances
the shared original generator twice, scales signed ranges with truncation,
and performs the independent current point/helper predicates. Zero ranges
still draw twice from the generator; clipping does not resample. The direct
helper retains its own point tail, and point spray keeps the preceding CP.
No P-style final move, own delay/key poll or flow is introduced. Existing
source/chain/repeat/reply, indexed page routing and nonfatal error behavior
remain in force. Nonpositive count retains its prefix without a canvas call.

`big_spray` checks literal samples, every signed range WORD with fixed draws,
marker tails, count boundaries, live owners and partial errors. Native
`cterm_big_spray` covers partitions/profiles, positive/negative sampling,
coordinate scaling/stored bypass, prefix/lifecycle/error ordering, hidden
pages, shared r, repetitions and deferred replies.126 new coherent scenes
bring BIG to2324, comparing zero-range geometry independently of seed time
with complete retained scene/image/state/replies and RNG advancement.
Native X0 is implemented; remaining speaker/global45-entry and final-suite
acceptance remain pending.

Native CMake Release configure/full build (/usr/bin/cc, build/) passed without compiler/source diagnostics. All35 BIG-specific native CTests passed in47.12s, including cterm_big_spray0.65s. syncterm_big_input passed in116.92s (test116.84s),2324 coherent scenes. The first application run caught a fixture selecting M1/COPY for its intended XOR scene; the command was corrected to M2 while retaining the independent XOR expectation. No non-BIG tests ran. Native X0 is implemented; remaining speaker/global45-entry and final regressions stay pending.

Native X0 implementation commit: `94a4fb9`.

## Native X3 custom text ratios

All four class0 reads precede signed1..4 clamps and immediate ratio
installation. Physical-max rejection skips dispatch but returns public size0;
uninstalled/lower failure omits that tail. Normal control caches returned
dimensions separately from restored original ratio arguments, saves custom
words and repairs only effective zero denominators. The actual EGAVGA custom
entry leaves bitmap magnification unchanged. Checked scaled-metric overflow
retains completed prefixes and rejects nonfatally.

Original loaded ratio arrays now initialize to1/1; the copied header starts2b.
I1 retains text state; normal I0/R restore font0/public size1 and8/8 private
bitmap caches while keeping saved/effective ratios. X3 draws/moves nothing
and preserves console fonts, points, palette, pages and images. E/W rendering
and borrowed font/resource integration remain separate pending work.

`big_text_ratio` independently checks each full WORD input domain, public
validation, raw ratio/denominator/header behavior and callback/failure order.
Native split/bytewise profiles check partial reads, source/default/lifetime
behavior, errors and complete unchanged indexed pixels/RGB/image/state.
126 new coherent scenes bring BIG to2450; all earlier scenes also verify
initial ratio/cache values. New captures retain the complete previous scene
and RNG alongside exact new text state and replies. Native X3 is implemented;
remaining text/audio/global45-entry and final regressions stay pending.

Native CMake Release configure/full build and final configure/build (/usr/bin/cc, build/) passed without compiler/source diagnostics. All37 BIG-specific native CTests passed in47.80s, including cterm_big_text_ratio0.54s. Final strengthened big_state/big_text_ratio/cterm_big_text_ratio checks passed in0.75s. syncterm_big_input passed in121.97s (test121.90s),2450 coherent scenes, after its BIG-only CTest bound increased from120 to180 seconds. No expectations were weakened and no non-BIG tests ran. Native X3 is implemented; E/W fonts/rendering, audio/global45-entry and final regressions remain pending.

Native X3 implementation commit: `25d2c2f`.

## Original stroke-font data and startup bindings

Historical implementation (superseded by the current-font policy below):
the four original V100 CHR files were embedded from pinned checked-in
bytes, retaining original headers/notices. They are distinct from the newer
RIP font arrays. BIG initializes ten names/twenty records, registers the
borrowed assets in4/2/3/1 order, and retains separate loaded/active owners.
Registration preserves old errors and defers body-plus validation to later
consumers; bounded native byte views replace source paragraph/segment aliases.
There is no runtime font-file loading or owned DOS font allocator.

`big_font` checks compiled-data CRCs/configure SHA-256 identities, every glyph,
all two-byte records, selected literal metrics and terminal moves, register/
rebind/version rules and malformed bounds/WORD traversal. The shared CHR
accessor preserves zero-width glyph programs and original BYTE/WORD address
rules while keeping layout and rendering in protocol consumers. All2450
existing coherent application scenes additionally verify registry count10,
mask0f, loaded0 and inactive body with the complete prior pixels/images/state.
E wire selection and W graphical text remain pending; no glyph-rendering
acceptance is claimed by these data/registration tests.

Native CMake Release configure/full build (/usr/bin/cc, build/) passed without compiler/source diagnostics. All38 BIG-specific native CTests passed in46.91s; big_font0.01s. syncterm_big_input passed in123.17s (test123.08s),2450 coherent scenes with the additional startup-registry assertions. No non-BIG tests ran. Original data/borrowed startup/common format access are implemented; E selection/W rendering and full global45-entry/final acceptance stay pending.

Original V100 font data/startup implementation commit: `2b1fa26`.


## Native E font selection and text style

E reads all three font,size,rotation values before signed normalization,
then selects the original borrowed fonts and installs separate public,
DDO and bitmap-device state. Repeating the selected font skips registry
refresh. Font0 retains loaded/active resources and remembered DDO header/id;
reselecting a loaded font republishes its current registry body while a
same DDO id still skips header refresh. Stroke sizes1..4 use3/5,2/3,3/4,1;
size0 reuses persistent custom ratios. Positive ratio data is shared with
RIP through st_bgi_data while preserving its index0 semantics. Original BYTE height metrics and
bitmap8x8 dimensions/magnification remain distinct from console fonts.
I0/R use the same bitmap defaults path; I1 retains text state. No own raster,
point, page, palette, image or console-font operation belongs to E.

Independent big_text_style and native cterm_big_text_style fixtures cover
complete WORD clamps, original metric values, cache/resource lifetimes,
bitmap magnification, all split/profile/source/reply/lifecycle paths and
nonfatal partial errors.182 new coherent application scenes (2632 total)
check font/registry/DDO/device/header/ratio/metric state alongside all retained
pixels/cells/palette/images/points/fill/pages/cursor/replies/RNG. Missing
nonloaded bodies safely fall back with BGI-8/font0/size1 and requested
direction, without a host font loader. Checked extents/division preserve
reached prefixes. Native E is implemented; W text rendering, global45-entry
composition and the final full regression suite remain pending.

Native CMake Release configure/full build and final refresh (/usr/bin/cc, build/) passed without compiler/source diagnostics. All40 BIG-specific native CTests passed in47.54s, including big_text_style0.08s and cterm_big_text_style0.51s. syncterm_big_input passed in130.78s (test130.73s),2632 coherent scenes. Initial fixture corrections used BIG +1 for negative1, the three-value E stored-source arity and settled pre-E graphics points; independent font/metric/pixel expectations were retained. No non-BIG tests ran; RIP table regressions and the full suite remain deferred. Native E is implemented; W rendering, global45-entry and final acceptance stay pending.

Native E font selection/style implementation commit: `9dbf75f`.


## Direct stroke-text prerequisite for W

The native direct stroke primitive now measures the original CHRs, applies
WORD placement and signed coordinate scaling, draws through the existing
fixed-slope clip/shared thin kernel, and advances by each terminal decoded
point. Small AA measures7 but advances6; Gothic22 draws and advances14 with
metric0. Width lookup and program lookup are separate shared format accesses
so malformed data fails at its reached prefix. Public/DDO/device points,
private justification and public/DDO/device line attributes stay independent.
Normal style restoration leaves device width1 while restoring cached width3.

big_text_stroke uses literal original Small glyph masks plus authored format
fixtures for both directions/COPY/XOR, placement, missing/zero-width glyphs,
source bindings, signed/WORD arithmetic, counted NUL and partial failures.
All2632 existing coherent scenes additionally observe the new cache fields
with retained full scene/state/replies. This verifies the direct drawing
prerequisite. W acquisition/initial move/count front, bitmap fonts/raster and
command wiring remain pending; no W wire completion is claimed.

Native CMake Release configure/full build (/usr/bin/cc, build/) passed without compiler/source diagnostics. All41 BIG-specific native CTests passed in46.83s. Final strengthened big_text_stroke passed in0.07s, including literal pixel/decoded-point failure prefixes and current per-glyph binding checks. syncterm_big_input passed in130.04s (test129.99s),2632 coherent scenes with the additional private-cache assertions. The first focused run exposed a64x64 fixture without matching clip bounds; the fixture now declares that bounded surface, preserving the original glyph/advance expectations. No non-BIG tests ran. Direct stroke layout/rendering is implemented; W bitmap sources/raster/initial move/count/acquisition/wire and full global45-entry/final acceptance remain pending.

Direct stroke-text layout/drawing implementation commit: `5c19345`.


## Current font policy and shared glyph drawing

BIG now uses the current RIP font data. There is one shared immutable font
set and common bitmap row expansion/CHR decoding; only protocol placement,
read order, bounds, advances and error policy remain in adapters. Historical
V100 files and the separate embedding script are removed. A historical font
variant will be considered only with artwork demonstrating a need. Current
font-version metric/outline changes intentionally supersede older numeric
expectations; preserved historical sections describe past verification.

The complete native bitmap and outtextxy primitives have independent row,
rotation/magnification/placement/count/bounds/point/failure tests. All four
BIG font views are pointer-identical to shared current data and retain
compiled checksums. W stream-reader integration and wire acceptance remain
pending.

## BIG cursor visibility

BIG no longer hides the cursor unconditionally. Entry keeps the normal
visible cursor and ANSI show/hide remains effective across R1/R2/R3 and
I0/I1. Native CP437/UTF-8/status tests cover both visibility states. Existing
coherent scenes explicitly hide their cursor; connection captures now verify
that BIG starts with it visible.

Native CMake Release configure/full build (/usr/bin/cc, build/) passed without compiler/source diagnostics. All42 BIG-specific native CTests passed in47.87s; big_text_bitmap0.01s. syncterm_big_input passed in130.64s (test130.59s),2632 coherent scenes with current shared-font metric/header expectations and visible-entry/explicit-remote-hide cursor checks. No non-BIG tests ran; RIP and the final full regression suite remain deferred. Shared current fonts/glyph kernels, bitmap/outtextxy primitives and cursor correction are verified; W wire integration and global45-entry/final acceptance remain pending.

Shared current fonts/glyph kernels, bitmap text and cursor correction commit: `a42a00f`.


## Native W graphical text

W now acquires both coordinates before installing text colour and entering
the shared text reader. Raw payload remains opaque to ANSI, including NUL/ff
and fake I# sequences. CR/LF filtering, @/128-byte termination, empty @,
stale-suffix timeout output, stored reuse/finalZ/first-pass flow, numeric
scaling/bypass, chains and deferred replies retain their established owners.
EOF/reset and errors cancel incomplete output with accepted prefixes intact.
A reset requested during timeout notification prevents subsequent drawing.

The native output adapter positions/scans/places before requesting pixels,
retains public CP, and uses the shared current fonts and glyph kernels.
Bitmap text copies under M2; stroke text keeps line XOR and its separate
metric/decoded advance and line-cache restoration behavior. Images, physical
pages, status pixels and unrelated console/font/fill/palette state are retained.

cterm_big_text checks complete bitmap/Small-XOR planes and state across all
modes/encodings/status choices, split input, lifecycle/source/error paths,
all four current face metrics/landmarks and recovery.224 new coherent scenes
bring application coverage to2856, comparing independent text pixels and
count/ratio/header/device/point owners with the full preceding scene and
replies/RNG. Native W is implemented; global45-entry/final acceptance remains
pending. The next requested command order is t, then ?.

Native CMake Release configure/full build (/usr/bin/cc, build/) passed without compiler/source diagnostics. The initial full build emitted generated-dependency timestamp jitter notices; the final full build and focused build passed cleanly. All43 BIG-specific native CTests passed in48.43s. syncterm_big_input passed in141.68s (test141.61s),2856 coherent scenes. The final strengthened cterm_big_text passed in1.21s (test1.09s), including all four current faces and padding-independent image retention. No non-BIG tests ran; the final full regression suite remains deferred. Native W is implemented; global45-entry/final acceptance stays pending. Next requested order: t, then ?.

Native W graphical text implementation commit: `83acdfc`.

## Native t and buffered input

t drains ready local keys before shared class0 numeric acquisition, clamps
signed seconds to1..30, then waits on a local monotonic deadline or one ready
getch byte. Expiry still polls the keyboard; private scans alone are not ready.
No post-exit key drain, echo or retransmit is added. Shared stored sources,
repetition/chains and deferred replies compose through the existing owner.
EOF/reset/errors and callback reset cancel nonfatally with no flow tail.

All BIG automatic XON/XOFF/custom flow emission is removed. X5 retains its
operand/state semantics; legacy flow sites make no readiness requests or
output-budget/count waits. Parser backpressure leaves the unread tail in
existing session/connection buffers. Pacing no longer banks credit during
pauses or their resume tick, so buffered tails continue at the selected rate.

cterm_big_pause independently covers all profiles, partitions, complete
indexed planes/state/image retention, local deadline/key semantics, source/
reply composition and error/reset recovery. big_session_pacing reproduced the
credit-burst defect through a real POSIX shell connection, then checked paced
and unpaced ordered tails in both encodings. Existing BIG flow-only tests
were replaced under the new transport contract while retaining scene/state/
source/reply checks and actual counted-reply readiness tests.28 new coherent
application pause scenes bring coverage to2884. ? is next; global45-entry
and final full-suite acceptance remain pending.

Native CMake Release configure/full build (/usr/bin/cc, build/, FreeBSD) and focused final test builds passed without compiler/source diagnostics. Initial generated-dependency timestamp jitter notices were absent from the final full/focused builds. All45 BIG-specific native CTests passed in50.34s, including cterm_big_pause0.57s and big_session_pacing1.34s. syncterm_big_input passed in146.75s (test146.69s),2884 coherent scenes. No non-BIG tests ran; the shared session pacing change and all other regressions remain subject to the goal-final full suite. Native t, buffered flow policy and paced resume are verified; ? is next. Global45-entry/audio/final acceptance remains pending.
