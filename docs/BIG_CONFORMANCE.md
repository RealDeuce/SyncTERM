# BIG 1.03 conformance

BIG (1.03) is selectable in BBS profiles and Wren. Its wire & setup, I/R/g/X4/X6, T attributes, P markers and L/D/d lines run
through native CTerm. Native < keyboard/hybrid pointer acquisition is integrated. Remaining drawing,
page/image binding, sound and complete command acceptance remain in progress.

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
