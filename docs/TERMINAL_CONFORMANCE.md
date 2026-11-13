# Terminal conformance coverage

The shared graphics parser supports an optional owner-defined numeric reader.
`graphics_protocol_number` checks opaque byte ownership, every split, signed
values, supplied-value bypass, input-free completion, waits, raw arity and
error/EOF/cancel recovery. The existing IGS reader remains the default.

The BIG ordinary reader now covers destructive WORD construction and exact
finite scaling in `big_number`: 54 literal split-input cases, all WORDs
against a binary-rational oracle, count wrap, timeout/BEL events and recovery.
The shared RNG core is checked in `big_random` against all 32768 outputs,
signed rejection bounds and equivalent state streams under different poll
partitions. The stored evaluator is checked in `big_loop` with independent sequences,
reachable counter wrap, source changes within a command, unscaled stored
values, suspension prefixes and error/EOF recovery.

The `big_numeric` callbacks compose those sources with local
deadlines, ordinary final-r expansion, native clock access, delayed-key
checkpoints and exact partial error consumption; split-input/raw/EOF fixtures
cover ownership and recovery. Initial native CTerm input integration is
covered below; full application/command acceptance remains pending.

`big_state` checks the I/R logical reset order, retained application/frame
owners, palette-only I1, cached versus installed resolution and host-failure
prefixes. `big_profile` checks all three video/font profiles against original
ROM identities, the restored custom patterns and the shared 64-colour EGA
table. RIP now consumes that same immutable palette data; its native tests
and coherent CP437/UTF-8 raster captures passed. Indexed page/image storage,
its remaining native bindings and BIG command completion remain pending.

`cterm_big` checks the native pre-ANSI BIG path with production numeric
callbacks and actual I0/I1 indexed-console effects. Independent CP437/UTF-8/
status/split fixtures check IBM8x14 glyph rows, full-block pixels, palette,
cursor, frame locals, ff/NUL ownership, late-byte deadlines and EOF/reset/
semantic-hook recovery. Shared parser header-completion tests cover retained
identifiers, chaining, suspension and error prefixes. Wire & and native key/input integration are recorded below; complete command
acceptance remains pending.

BIG is selectable as profile/Wren value6. BBS codec and Wren tests preserve
previous values, cover its presets and property bounds, and persist entries/
defaults. `syncterm_big_input` uses coherent control-bitmap captures for all
three initial profiles, CP437/UTF-8, status and reconnect, I0/I1 effects,
exact original console fonts, both indexed blink planes, palette/cells/cursor,
DSR replies, RGB samples, fallback profiles and menu restoration. Physical
audio/MIDI are disabled. Complete command acceptance remains0/45.

`big_control` checks original repeat/flow/chain/reply ordering, both WORD count
domains, literal raw bytes, live reads, suspended prefixes and errors. The
CTerm fixture covers actual repeated/gang bodies with shared numeric sources,
source-to-wire seams, bounded work, output origins and reset/EOF recovery.
The optional shared-parser unknown-command handler owns the same accepted
suspension boundary without guessing payload. Application captures check
unknown chaining and final footer state. Wire &/text acquisition is covered
below; W rendering, < production and audio synchronization remain pending.

Native `cterm_big` key tests cover stored-loop delay probes and post-value
03/18 cancellation, zero/scan ordering across distinct readiness/read
boundaries, all native key entry kinds, CP437 conversion and UTF-8/modifier
replay. BIG/IGS share native event replay with separate protocol consumers.
Unread keys retain order behind finishing flow; saturation stays local;
reset/EOF and callback reset discard unread work. The standalone key cases inject
stored setup; wire & coverage is recorded below. Physical keyboard/pointer
coverage is not claimed.

Wire & setup is now covered by `big_stream`, `cterm_big` and the expanded
application fixture. Independent expectations cover immediate header/raw/
text/table stores, original suppression/step/count/source semantics, load-
abort zero filling, publication before XOFF,2048 actual stored slots, gang
copy bounds and fixed/selected-& bodies. Local auxiliary/text deadlines
preserve late-byte, cap/NUL/reuse/timeout ownership. Partial-pair timeouts
retain body entry. The session adapter recognizes full accepted pauses and
retains following bytes. The98 coherent application scenes include56 fixed/
gang loops and14 native Ctrl-C delayed loops with exact flow, current-value
effects and resumed buffered bytes. No physical audio/device or full command
acceptance is claimed; <, W rendering and music synchronization remain pending.

Wire R is covered by `cterm_big_resolution` and70 additional coherent
application scenes (168 total). Independent native split/bytewise CP437/
UTF-8/status cases check all three full indexed ROM scenes, geometry/cursor,
same-mode reinstall, full-WORD normalization, class0 source ownership,
retained application/frame/ratio state, deferred replies, stored R1..3,
returning-error/cache prefixes and EOF/reset/timeout. Application sentinels
precede R and fresh glyphs follow without ED, verifying clear/default/font
transitions in the same capture. Complete command acceptance remains pending.

Wire g stores its full class0 WORD without clamping or drawing. Native
split/bytewise cases cover encodings/status, special WORDs, I/R lifetime,
delayed stored values, replies and EOF/reset/timeout. Production numeric
callbacks probed with wire g/R state retain exact Y5->8/11 and cache-versus-
surface semantics; this does not claim a live coordinate drawing consumer.
70 added coherent g scenes (238 total) check complete flags and I1/R
retention with the unchanged full scene. Full command acceptance remains pending.

X dispatch/X6 now provide a live class2 consumer: exact full-WORD selector6,
immediate X store before Y, current-field signed clamps,12/6 defaults and
I/R/partial-input lifetime. Native cases cover split/bytewise encodings/status,
points/cursor, unknown selectors/continuation/replies and source-to-wire
transitions. Actual g2/g3 X6 Y5 gives8/11; stored Y5 bypasses that scaling.
70 added coherent step scenes (308 total) retain exact scene pixels and state.
Remaining X selectors and interactive pointer input remain pending; X completes
only after all nine selectors. No full command or physical-device claim is made.

X4 implements counted opaque zone loading, immediate coordinate stores,
special clear/loopback ids, live lengths, retained tails, conditional XON and
fresh per-helper deadlines, including its extra discarded read. Native tests
cover split/bytewise input, source/scaling/flow/prefix/error/reset/EOF and
bounded corruption.84 coherent session scenes (392 total) check zone data
and lifetimes with exact unchanged scene pixels/cells/palette/cursor/state.
Pointer hit testing and interactive acquisition remain pending. Negative ids
and actual table escapes fail at the accessed boundary, preserving accepted
prefixes instead of overwriting foreign DOS state.

Wire T/P now use the shared thin BGI kernel with original BIG clip/stroke/
point and polymarker schedules. `big_draw` checks literal shape masks, XOR
visits, phase/ties, WORD clips, copied dots and failure prefixes. Native wire
cases cover all modes/encodings/status, T normalization and partial stores,
P colour-before-input/final CP, source/scaling/EOF/reset/timeout and independent
blink-plane XOR.392 new coherent scenes (784 total) add all markers in all
three modes and patterned/rejected/zero-mask/width2 cases to the earlier
indexed scene/state/reply oracles. Pointer/page binding and remaining command
composition stay pending. Routine development tests are BIG-only; final goal
verification retains the full regression suite.

`big_collect` adds independently tested keyboard/zone collection operators:
all WORD type/options values,129-byte/count-wrap storage, raw bytes, echo
prefixes, signed first-hit/default/strict zones, unsigned cross-cell copies,
bounded failures and exact replies through the existing footer. Literal
movement vectors check X6 WORD overflow and asymmetric clamps. Native <
framing/echo/keyboard/mouse/pointer integration remains pending; the new
component tests do not claim bitmap or physical-device input coverage.

Native < now owns its three fields and local acquisition, retaining buffered
wire tails until completion. `cterm_big_collect` checks early append/key state,
raw getch/private scans, literal echo/wrap/deletion, counted replies/loop/chain,
pointer XOR/helper points, keyboard/mouse order and stationary reports,
UI/disabled/outside/status suppression, stale/overflow events and normal/error/
reset/EOF/destruction cleanup. Native mouse mode4 exposes active BIG pointer
ownership.42 coherent keyboard/echo/hybrid scenes bring the application oracle
to826 full indexed scenes with cells/fonts/palette/cursor/point state and replies.
Remaining page/image/audio composition and final full-suite verification are
pending; routine tests remain BIG-only.

Native BIG L/D/d now use the existing original clip/stroke/point adapters and
indexed sink. `cterm_big_line` checks split/bytewise input, colour before
acquisition, L/D endpoint and pattern order, the relative Y-sign defect,
source/scaling/repeat/deferred replies, point/error prefixes, per-plane XOR
and recovery.70 added coherent application scenes bring the oracle to896;
all modes/encodings/status choices retain full indexed planes, ROM text,
palette, points, cursor and exact replies. Page/image/audio composition and
final full-suite verification remain pending.

BIG m moves the three graphics point owners through existing WORD/origin
adapters without drawing or moving the console cursor. `cterm_big_line`
checks variable arity, full/negative selectors, original relative Y-sign
behavior, source/scaling seams, point/attribute/pixel retention and partial
failure/recovery.42 added coherent application scenes bring coverage to938:
absolute/relative m preserve the scene and subsequent D starts at the new
point. Final global page/image/audio composition and the full suite remain
pending; routine tests remain BIG-only.

BIG M selects the installed line-writing bit and then updates the application
cache; only inputs2/3 select XOR. `big_state` independently checks all65,536
WORDs. Native tests verify split/bytewise input, COPY/XOR pixels, source/flow/
failure prefixes and M/I1/R/pointer/I0 lifetime.56 coherent scenes bring the
application oracle to994, observing both mode owners, XOR line cancellation,
COPY dots under M3 and R's actual0/cache1 split with unchanged full scene,
palette, points, cursor and exact replies. Global page/image/audio composition
and final full-suite verification remain pending; routine tests are BIG-only.

BIG C stores one of four full application colour WORDs after both class0
reads, without installing BGI state or drawing. Exhaustive selector/colour
checks and native split/source/lifetime/partial-input cases preserve delayed
line/marker/fill/text ownership.56 new coherent scenes bring coverage to1050,
observing application WORDs, public draw BYTE, device pen and installed fill
with complete indexed planes/text/palette/points/cursor/replies; coloured
swatch interiors also verify composed RGB. Fill/text consumers, page/image/
audio and final full-suite verification remain pending; routine tests are BIG-only.

BIG A installs distinct public/lower/device fill owners and an indexed8x8
pattern tile without drawing. Built-in/custom validation, copy order, cached
colour, error prefixes and I/R lifetime have independent component/native
tests. RIP and BIG share the unchanged96-byte mask table.56 added coherent
scenes bring coverage to1106, observing selected fill and literal cached rows
with the full indexed/text/palette/points/cursor/replies/RGB scene. Fill
rendering consumers and remaining global composition are pending; only BIG
tests run during iteration, with RIP/full regressions deferred to the end.

BIG X1 now edits/restores retained custom rows with conditional arity,
immediate BYTE prefixes and a fixed eight-read loop across source changes.
It leaves installed patterns and pixels unchanged until A resubmits them.
Component/native cases cover full slot/flag domains, restore data, live
locals, bounds/cancellation, split input, timeouts/source/reply/I/R behavior.
56 new coherent scenes bring coverage to1162, adding retained slot bytes
and editor ownership to existing full indexed/text/palette/points/cursor/
fill-cache/replies/RGB captures. Remaining consumers/global composition and
the final full suite remain pending; routine tests stay BIG-only.

BIG's original direct-bar primitive now has independent WORD clip, literal
pass-order and exhaustive row-mask/endpoint-phase tests in `big_bar`. Opaque
holes copy pen0; the current device pen/active mask are distinct from the
latched tile, line XOR is ignored, and all point owners are preserved. Native
indexed failures retain the accepted prefix. No wire B binding is claimed;
its bar3d and input/attribute paths remain pending. Iteration stays BIG-only,
with the full suite deferred until the end of the goal.

BIG's shared DDO point producer has independent `big_producer` coverage for
all mode bytes/configured buffer-size WORDs, first-point duplicate/8001 marker
rules, run wrap and original versus native failure prefixes. Point mode2
retains carry-clear rejection and actual AX/BX/CX/DX returns, including the
batched pre-write-byte boundary. It leaves all point owners unchanged;
no native B/curve binding or planar framebuffer is claimed. Native read-byte,
bar3d and curve composition remain pending; routine tests stay BIG-only.

BIG B now implements its five/six-field input and attribute prefixes, opaque
bars and original bar3d inset/front/side/top schedules. WORD projection,
top BYTE, orientation, patterns/XOR, producer register effects and distinct
point owners have independent component/native expectations. Native tests
cover split/profile, source/scaling/flow/reply and timeout/EOF/reset/error
prefixes; batched byte reads reconstruct all four planes at the indexed
boundary.84 coherent application scenes bring coverage to1246, checking
opaque/XOR/box/top scenes with existing complete text/pixels/palette/points/
fill/cursor/replies/RGB observations. Page/image/curve/audio composition and
full45-entry acceptance remain pending; routine tests remain BIG-only.

BIG's flood kernel is synchronous without periodic work-budget yields;
framebuffer callbacks must not block or wait. `big_flood` checks original
WORD spans, directional/LIFO/pattern suppression, clipped/inverted probes,
whole-band plane-mask order, all mask/pen combinations and partial errors.
The bounded native frontier explicitly corrects unsafe SS:BP underflow;
overflow preserves pre-push state and reports BGI-7 after pending work.
Native F now binds three-field acquisition, clamps, fill-only attributes,
current selected border after XOFF, separate live loop tests around paired
flow and unchanged points. A512-record bounded native frontier and checked
indexed byte/masked pen writes integrate the kernel with both blink planes.
Native tests cover partitions/profiles, waits, source/replies, cancellation,
overflow, error prefixes and recovery.70 added application scenes bring BIG
coverage to1316, including solid/checker/empty F and rejected X4 followed by
valid graphics/ANSI/DSR on the same connection. Z and page/padding/configured
buffer composition remain pending. Routine verification stays BIG-specific.

BIG H now acquires one class0 value, stores the full WORD and clamps only
signed negatives to0. All65536 WORDs and retained state under uninstalled
BGI/old errors have independent expectations. Native partitions/profiles,
whole indexed planes, prefix/timeout/reset/EOF, I/R lifetimes and blocked/
deferred replies are covered.28 coherent application scenes bring BIG to
1344, verifying H256/negative0 while retaining complete scene/state/replies/
diagnostics and RGB. Z/O/Q/V/Y consumer composition and final full-suite
acceptance remain pending; routine tests remain BIG-specific.

BIG Z now composes four standalone outline strokes with the existing indexed
flood, gated after outline by the full H WORD. It preserves all point owners,
uses the wrapped first-corner seed and adds no own flow calls. Component and
native checks cover order/phase, H0/256, reversed/clipped/wrapped/degenerate/
thick/pattern/XOR cases and acquisition/source/error recovery.84 coherent
scenes bring BIG coverage to1428 with complete prior scene/state/replies/
diagnostics/RGB observations. Page/padding/configured buffers and global
composition remain pending; only BIG tests run during development.

BIG's indexed address/storage dependency now distinguishes active CPU
segment/WORD addresses from BIOS display-start/padding metadata, using the
visible bitmap as its authoritative window and borrowed backing elsewhere.
`big_video` checks every canonical aperture pixel and all pixels in40
display layouts, original BYTE/WORD arithmetic, padding, blink values and
precise extent-error prefixes. Native B segment-return expectations remain
covered; G wire/page/image/console/presentation binding is still pending.
No additional application scene or hardware/device acceptance is claimed.

BIG G4/G5/G6 now bind remembered memory, actual active and actual visual page
selection with original BYTE/WORD/resolution/publication rules. All graphics
consumers and batched reads use indexed active memory; console text/cursor
uses visual memory with four cursor banks. Both blink values and padding
persist, while host status artwork stays outside the video view and source
rows behind it stay in backing. Native tests cover all page WORDs, split/
profile/source/loop/reply cases, stride/display aliases, mode/I1 lifetimes
and nonfatal save/load/CPU-extent failures.140 new coherent application scenes
bring BIG to1568, checking complete retained pixels/cells/palette/state/points/
cursor/replies/diagnostics/RGB. G0..3 images/global acceptance remain pending.

BIG's image dependency now supplies the original WORD size query, four-byte
header, plane3..0 format and ordered capture over the shared indexed memory
interface. Conversion occurs at import/capture edges; source-byte reads use
cached indexed pens, including retained tails, with actual fixed-segment
cursor/extent checks. `big_image` has independent literal bytes, every size
delta, alignments/rows, partial failures, malformed heights and source/buffer
wrap expectations. Native G0..3/paste/lifetime integration and application
image scenes remain pending; existing1568 coherent scenes are unchanged.

BIG's image paste dependency now preserves physical right rejection,
unsigned bottom crop, ADD/JL flags and temporary header/error prefixes.
`big_image_paste` uses independent truth tables, full dual-phase memory and
byte/plane traces, every WORD destination/mode, alignment/boundary/crop cases,
source/destination wrap and checked partial failures. Indexed source reads
avoid packed decoding at blits; the Boolean kernel is shared with RIP's
indexed branch under reference83's matched domain. Native G0..3 ownership/
wire/page binding and application image scenes remain pending. Non-BIG
regressions are deferred to the final suite; existing1568 scenes are unchanged.

BIG now binds G0..3 through the common pre-ANSI numeric parser, preserving
argument/page/gate/mode prefixes, G1's original-corner/cache3 behavior and
G3's repeated active selection. An engine-owned image survives I/R/attention/
EOF/reset and starts defined/zeroed on reconnect. Independent native tests
cover splits/profiles/loops, overlap, separate pages/blink values, error and
partial-write prefixes, and indexed source behind protected host UI.154 new
coherent scenes bring BIG to1722 with complete pixels/cells/palette/state/
points/cursor/pages/replies/header/payload/RGB expectations. Native G images
are implemented; global45-entry acceptance and final regressions are pending.

BIG S now reads both class0 arguments before its shared feature gates, then
preserves original count/sign/silent-return and cache-before-device behavior.
Raw cache bytes remain distinct from six-bit physical palette colors; S0
changes logical background/palette0 while indexed pixels/images stay intact.
Independent exhaustive state tests and native split/profile/lifetime/error
fixtures verify all raw bytes against a separate RGB formula.98 new coherent
scenes bring BIG to1820, checking live/saved-image recoloring, raw cache,
silent rejection, capture under palette aliases and I1 restoration alongside
the complete prior scene/state/replies. X8/global/final regressions are pending.

BIG X8 now implements capture/restore and the original inclusive palette
walks with per-setter snapshots and a stale final wrap. Unsigned millisecond
delays use local monotonic deadlines; key cancellation follows the full wait.
Signed WORD count wrap, current flow/loop owners and the common footer are
preserved. Explicit zero-owned padding replaces the source's uninitialized
upper-endpoint byte. Independent component/native cases cover all endpoint
pairs, splits/profiles, keyboard results/scans, flow retries, deadline and
EOF/reset/error prefixes.126 new coherent application scenes bring BIG to1946,
checking palette/local/saved/RGB results and unchanged indexed pixels/images
alongside cells/points/fill/pages/cursor/replies. N/audio/global composition
and final cross-emulation regressions remain pending.

BIG X5 now configures the existing flow helpers through class0 incremental
reads. Post-read loop override, byte-before-count stores, independent live
mode checks and signed default/count rules follow the original caller.
Independent exhaustive component/native tests cover wire splits, partial
effects and lifetimes, source changes, actual counted NUL/ff output,
resumption and the common footer.126 new coherent captures bring BIG to2072,
checking current settings and exact flow/replies with the full unchanged
scene/state/images/RGB. Remaining commands/global45-entry and final
cross-emulation regressions remain pending.

BIG X2 now seeds the shared original generator before either class0 bound
read, publishes raw FROM before TO, and performs signed repair afterward.
Local wall seconds supply the low seed WORD; delays retain monotonic time.
Independent native tests cover full bound domains, partial/pending random
reads, source changes, WORD wrap/equality, lifetimes and nonfatal errors.
126 new coherent scenes bring BIG to2198, checking seed/range/RNG progression
with unchanged scene/image/state and exact replies. X0/speaker/global45-entry
and final cross-emulation regressions remain pending.

BIG X0 now installs colour/reseeds before its five incremental reads, scales
two shared random draws per attempt with signed WORD ranges, and calls the
existing indexed point/direct-marker adapters. It preserves helper CP tails,
point COPY, clipping without resampling, zero/negative counts and failure
prefixes. Independent component/native expectations cover the full signed
range domain, literal samples, profiles/partitions, hidden pages, source and
lifetime behavior.126 new coherent scenes bring BIG to2324 with authored
zero-range geometry, separate RNG checks and the complete retained scene/
images/state/replies/RGB. Speaker/global45-entry and final regressions remain
pending.

BIG X3 now installs custom text ratios after four completed class0 reads.
Unsigned maxima, returning size0, saved/effective/cache distinctions and
original1/1 initialization follow the source. No glyph/pixel/point operation
is added. Independent component/native tests check full input WORD domains,
partial/error/source/lifetime paths and complete unchanged scene data.
126 new coherent captures bring BIG to2450, checking text caches/ratios and
retained scene/images/state/replies/RNG. E/W font rendering, audio/global45-entry
and final cross-emulation regressions remain pending.

BIG now embeds the four original V100 stroke fonts and registers their
borrowed bodies at entry. Source SHA-256/compiled CRC checks, every glyph/
record and malformed-access fixtures verify the separate common CHR format
accessor and registry lifetimes. All2450 existing coherent scenes additionally
assert count10, mask0f, loaded0 and inactive body alongside unchanged scene/
state data. E selection and W rendering remain pending; this data/startup
coverage does not claim graphical-text conformance.

BIG E now installs original borrowed fonts with separate selected/loaded/
active/DDO/header/bitmap-device owners, late three-field clamps, original
positive/custom ratios and BYTE metrics. Font0/I0/R retain resources, custom
ratios and remembered header/id while resetting bitmap style; I1 retains
text. Native full-WORD/split/profile/source/lifecycle/error tests and182 new
coherent application scenes (2632 total) check exact text state alongside
the retained complete pixels/cells/palette/images/points/pages/replies/RNG.
Missing resources and unsafe accesses use documented checked nonfatal
fallbacks. Native E is implemented; W rendering, global45-entry composition
and final cross-emulation/full-suite acceptance remain pending.

CGA 80x25 adds a 640x200 PC raster with 80x25 8x8 cells, 4:3 display aspect,
PC blink and cursor scanlines6..7. Stable mode32 appears before EGA in ordered
screen/canvas pickers without renumbering existing mode values or the
ID-indexed Wren name catalog. Native mode/startup/settings/profile/connection
and Wren tests cover geometry, ordering and persistence. `syncterm_cga_mode`
checks eight coherent CP437/UTF-8, status on/off and reconnect captures with
literal full-block rasters in both blink phases, palette/cells/cursor and RGB
interior samples. Physical audio/MIDI are disabled. This is the first
prerequisite for [BIG](BIG_CONFORMANCE.md); no BIG commands are claimed yet.

RIP, IGS and BIG command rejections are nonfatal warnings at the graphics input
stage. Native recovery tests and `syncterm_behavior_session_modes` verify
following graphics and ANSI/VT52 handling, continued bidirectional connection
use, and the normal notice on later remote disconnect.
BIG's `cterm_big`/`syncterm_big_input` additionally verify every input split,
continued same-connection graphics/ANSI/replies and explicit last-error/count
diagnostics. Its native collection/fill/line/bar fixtures check retained
failure prefixes with successful terminal input/polls; pending commands,
sources, flow, replies and deferred pointer errors are cancelled on rejection.

IGS 2.21 work is tracked separately in [IGS_CONFORMANCE.md](IGS_CONFORMANCE.md)
and [IGS_IMPLEMENTATION.adoc](IGS_IMPLEMENTATION.adoc). The shared graphics
byte-parser foundation has native split-input and raw-ownership coverage;
native image-format tests check indexed storage and boundary conversions.
X9 native and coherent application acceptance passed. Native tests cover its shared N/script buffer
loader, guarded execution/clear, retained holes, capacity, local deadlines,
file-style readers/flow, shared loop state, local input and cancellation.
The shared parser explicitly supports source-ended text and accepted return
boundaries without a second parser. Application X9 covers972 coherent captures
across18 profiles, including script scenes, waits/key ownership and reconnects.
All63 command entries, six required foundations and final IGS goal acceptance
are complete; the removed planar/clock experiments remain superseded.
Serial/file dispatch regression coverage now includes independent X8 requested
snapshot banks, shared raw snapshots, file X11 error retention, source-specific
X0 restore and F/f/z/t/N flow, and invalid-T direct chain exits. Expanded X9
coverage has972 coherent application captures. The common numeric reader now
performs the ordered r/R tests after array lookup, relative encoding and loop
evaluation, preserves stored-load suppression and avoids re-evaluating supplied
values. Native tests retain exhaustive signed-word coverage with independently
pinned sentinel results. The X2 application test adds54 coherent captures
across18 profiles for dynamic array selectors, operands and loops with literal
Random states. Final acceptance has230 distinct passing CTest checks, including
all126 IGS native/application checks, ANSI/VT52, RIP, SkyPix, profiles, Wren,
audio, graphics lifecycle and four native X11 checks on a private Xvfb display.
The final native FreeBSD Release CMake build has zero diagnostics. Physical
audio/MIDI were disabled in headless fixtures; the
[manual audio checklist](IGS_CONFORMANCE.md#manual-audio-checklist) is supplied.
The CTerm input adapter has native ST geometry/encoding/status and lifecycle
coverage. IGS is selectable through profiles, menus and Wren. `I` and `R` have
native selector/state/deadline coverage and coherent application bitmap checks
in all ST modes and status choices. `R` also covers CP437/Atari ST/UTF-8 wire
input, retained images, unchanged-mode scenes, colour transitions and reconnect.
Resolution changes retain physical window size through fractional/sub-native
display scaling, with native bitmap/scaler and private X11 window checks.
`g` has literal coordinate-reader/state tests and coherent application checks
for captured resolution, I/R preservation, reset/reconnect and no immediate
scene or window effect. Its documented normalized mapping corrects the
original integer-division bug; drawing and stored-loop tests also exercise it.
`M` has native mode-selection/reset/input-boundary tests and coherent bitmap
checks for an unchanged scene, palette and window, across ST geometries,
Atari ST/CP437/UTF-8 and status choices. Primitive-specific raster tests cover
the writing modes during drawing. `C` covers independent
graphical-pen selection, depth-specific validation, requested marker cache,
I/R preservation, split/error recovery and coherent unchanged application
scenes in the same mode/encoding/status matrix. `S` adds literal component,
physical palette and mono polarity tests plus coherent composed-RGB checks
that existing indexed glyphs recolour without rewritten pixels/cells. Invalid
negative pens are ignored safely. Palette restoration, I/R retention and
reconnect run in the same matrix. `H` adds hollow/solid interior, writing-mode
and perimeter state tests, including M interaction, index/pattern retention,
selective I versus full reset and I5/R preservation. Coherent application
checks use the same geometry/encoding/status matrix. `A` adds fill-style bounds,
user-bank copying, source/active pattern separation, literal random rows and
shared random-source progression, plus I/R/reset/reconnect checks. Its user
branch corrects the pointer-to-style bug and safely bounds negative indices.
Coherent application captures include all active rows and seeds with the
unchanged scene. `T` adds conditional arity and once-only random-kind
evaluation, marker/type sizing, literal width/end tables and all custom-mask
rows/fallbacks. Coherent application captures cover state and I/R/reset/
reconnect with the same unchanged-scene matrix. `P` adds exact native marker
pixels: all shapes/sizes/modes/depths, ordered XOR crossings/endpoints,
inherited clipping and native rounding, Drawto, independent blink phases,
safe invalid input and host status protection. Coherent application captures
check literal marker/font rows, glyph overlays, indexed pixels and composed
RGB in all ST modes, Atari ST/CP437/UTF-8 and status choices, plus retained
pens and disconnect/reconnect. `L` adds native patterned thin shafts, wide
polygon shafts, caps and arrowheads with exact ordered XOR composition and
original Drawto positions. Its 3321 independent dual-phase cases cover all
styles, odd widths, end pairs, modes/depths, clipping, reversals, degeneracy
and inherited cap state. Coherent application scenes verify pixels, colours,
cells, text overlays, status and lifecycle in the same mode/encoding matrix.
Ordinary tests use checked-in reference data without ROMs or external assets.
`D` shares those independent raster expectations while testing original L/P
origins, chained endpoint visits, both remembered coordinate pairs, typed and
random operands, errors/cancellation, suspended R, reset and reconnect.
Coherent application scenes add arrowed-L and visible-P origins, literal
chains, text overlays and bottom edges in the same mode/encoding/status matrix.
`X10` adds non-drawing origin selection with intended X/Y scaling (correcting
the original binary's 0/1 reader classes), retained start/clip/raster state,
once-only random-selector arity and input/lifecycle checks. Coherent scenes
also check a following D. X dispatch covers all16 selectors; out-of-range
selectors are one-argument no-ops with normal chaining and no payload.
`z` adds connected polylines with segment phase/XOR ownership, native wide joins
and multivertex arrow shortening. Its independent 1,692-case native matrix,
literal input/lifecycle/flow checks and coherent application scenes cover all
ST modes, encodings and status choices, including exact XOFF/XON replies.
`Z` adds inclusive, perimeter-free rectangle fill using all native built-in
and active user patterns. Its 1,992-case independent native matrix and coherent
application scenes cover fill modes/pens/anchoring, boundaries, overlays,
state/input/lifecycle recovery and all ST modes, encodings and status choices.
`B` adds bars and rounded filled boxes with native thin solid perimeters,
polygon span/closure/XOR behavior and retained raster state. Its independent
2,688-case native matrix and 96 coherent source-derived application scenes
also cover glyph/border overlays, input partitions, flags and lifecycle.
`U` adds cap-suppressed thin/wide rounded outlines and signed fill/no-op
routing. Independent native outlines and reused rounded-fill vectors cover
styles/widths/flags/state, while coherent application scenes add console-cell
replacement, boundaries, following D and lifecycle in every ST mode/encoding.
`O` adds aspect-corrected filled circles with original integer trigonometry,
adaptive 32–128-step polygon construction, fill patterns/perimeters and native
signed-radius rejection. Its 3,126 independent dual-phase cases cover all
fills/modes/depths and step counts; coherent application scenes cover glyph
composition/replacement, status/bottom pixels, following D and lifecycle in
every ST mode, encoding and status choice.
`Q` adds ellipses with independent X/Y radii using the shared filled-curve path.
Its 3,660 independent dual-phase cases cover all fill/mode/depth combinations,
zero/mixed-sign/tall/wide radii and every adaptive step count driven by each
axis. Shared circle/ellipse application captures cover independent scenes,
glyph composition/replacement, retained state, status and lifecycle throughout
the ST mode/encoding/status matrix.
`K` adds open circular arcs through native curve construction and existing
thin/wide lines, joins, caps and arrows. Its 3,876 independent dual-phase cases
cover native geometry/composition and safe periodic corrections for large or
negative angles. Coherent scenes include console-cell replacement, glyph
interaction, retained Drawto state, status protection and lifecycle across
all ST modes, encodings and status choices.
`J` adds elliptical arcs with independent X/Y radii and shared K/J angle-tail,
curve and line/cap execution. Its 4,659 independent native cases cover both
radius axes, styles/caps/widths, signed geometry and safe angle handling.
The shared application recipe checks 96 independent scenes plus glyph/state/
status/lifecycle interactions throughout the ST mode/encoding/status matrix.
`V` adds patterned circular pies with native center append, radial closure and
optional fill-colour perimeter. Its 3,819 independent dual-phase cases cover
fills/modes/depths, sweep/radius/step boundaries, clipping and safe angles.
Shared filled-curve captures check 120 independent scenes, text replacement,
glyph/XOR parity, status protection and lifecycle in every ST mode/encoding.
`Y` adds elliptical pies with independent radii through shared V/Y preparation
and patterned curve fill. Its 5,286 independent dual-phase cases cover both
radius axes, fills/modes/depths, clipping and safe angles. Shared captures check
132 independent scenes, including text replacement and tall ellipses, plus
state/glyph/status/lifecycle checks throughout the ST mode/encoding matrix.
`f` adds counted 1–128-point filled polygons through the shared patterned fill
path and z's count/evaluator/serial-flow dispatch. Its 4,056 independent
full-frame dual-phase cases include degenerate/concave/self-crossing polygons,
128 simultaneous intersections and native orientation-sensitive rounding.
Application captures check 168 independent scenes, text replacement, glyph/
state/status/flow/lifecycle throughout the ST mode/encoding/status matrix.
`F` adds seed-colour flood fill with native ordered live runs, stored bounds,
ignored perimeter, independent blink phases and an owned growable queue.
Its 3,660 independent dual-phase frames and 168 application scenes cover
fills/modes/depths, distinct phase topology, queue growth, text/graphics,
status/coordinates/state/flow and lifecycle through the ST mode/encoding matrix.
`E` adds graphical text attributes with native effect masking, six canonical
point/top selections and safe signed rotation state. Its 219,072 expected
state cases and 120 coherent application scenes verify sizing/rotation/effects,
unchanged console/graphics pixels, default restoration, parser and lifecycle.
`W` adds original graphical glyphs, source/doubled fonts, combined effects,
quadrant rotation, native clipping/light phases and underline. Its 22,554
independent dual-phase frames and 186 full application scenes cover all ST
modes, opaque/bounded text, console overlays/replacement, coordinates/status,
reader reuse/handshake, absolute local deadlines and recovery/lifetime.
`c` adds raw physical console colors through persistent native VT52 state.
Its 1,179,648 word cases, 1,536 color-pair cases, 3,696 complete-frame contexts
plus 2,688 printable-byte cases and 366 application scenes cover reverse
video, pending sequences, controls,
scrolling graphics, physical attributes, raw bytes and recovery/lifetime.
`p` adds unscaled console positioning through that same VT52 state.
Its 157,252 word/byte-class cases, 4,452 complete-frame composition cases,
6,144 byte-role frames and 360 coherent application scenes cover wrapped
coordinates, pending controls/colors, status boundaries, unchanged graphics,
scrolling and parser/error/reset/disconnect recovery.
`m` adds repeated VT52 movement with signed selector/count behavior.
Its 70,404 algebraic cases, 1,764 complete-frame contexts and 147 application
scene inputs verify zero/negative counts, maximum repetition, ESC parity,
pending parameters, edge behavior, both pixel phases and recovery.
`r` adds one-shot Atari cursor restore with a home fallback through
the shared VT52 save slot. Coverage includes 672 native scenes and
108 application captures with literal glyphs, pending parameters,
mode initialization and reconnect.
`k` adds counted Atari cursor hide and forced enable with live index
inversion in the presenter. Native input cases and 180 coherent application
captures cover blink, movement, drawing under the cursor and serial
destructive backspace through the pending VT52 state.
`v` controls Atari reverse video through the same VT52 handlers. Its 336
native scenes and 72 coherent application captures cover pending escape
and parameter states, retained text and graphics, raw background on
scroll, and reset and reconnect.
`w` controls Atari wrapping through VT52. Its 336 native scenes and 90
coherent application captures cover pending console input, immediate
right-edge wrap, overwrite, scrolling with graphics, status row
boundaries, and reconnect.
`l` adds three inclusive Atari VT52 line erasures with raw background
color and a depth-dependent destination column for whole-line erase.
Its 492 native scenes and 90 coherent application captures check the
erase range, graphics and text, cursor presentation, status bounds and
reconnect.
`s` adds full and partial Atari VT52 screen erasure, a VDI clear to
index zero, and the original mode-dependent color-reset sequence. Its
498 native scenes and 126 application captures cover erase ranges,
palette preservation, pending state, scrollback and reconnect.
`d` adds repeated VT52 line deletion. Its 433 native scenes and 90
application captures cover signed counts, pending state, indexed text
and graphics movement, raw background, cursor, history and reconnect.
`i` adds repeated reverse index and line insertion through the same
VT52 state and indexed row movement. Its 938 native scenes and 126
application captures cover top and bottom edges, signed counts,
pending input, raw background, cursor and reconnect.
`t` adds a cancellable local pause with flow control and retained
input. Its 252 native duration contexts and 74 coherent application
checkpoints cover deadline boundaries, key consumption, suffix
resumption, errors, reset and reconnect.
`q` adds sync waits and retained double-copy control. Its 66,346
native duration cases and 145 coherent application checkpoints cover
count boundaries, control lifetime, key forwarding, suffix resumption
and reconnect.
`X1` adds raw palette registers, signed index aliases, negative-color queries,
complete raw-word retention and mono polarity, preserving the VDI color cache.
Its 197,994 native word cases and 126 coherent application captures verify
retained indexed pixels, unscaled operands, parser recovery and reconnect.
`X12` adds four-register groups through the same raw palette service,
including negative group aliases and two zero-color arities. Its 67,048
native cases and 126 coherent application captures cover group order,
query/write mixtures, parser partitions, retained pixels and reconnect.
`X7` loads sixteen source-pattern rows through the shared W text reader.
Completed rows retain their shared-window edits; A4 selection makes an
independent active copy. Native reader/partition/lifecycle checks and 108
coherent application captures verify pattern use by Z, including all ST
modes, encodings and status choices, with audio and MIDI off.
`X2` configures both random ranges with ordered operand evaluation and
conditional arity. Its 75,238 native cases and 126 coherent application
captures cover range repairs, input partitions, retained scene and reconnect.
Valid narrow ranges now allow a full relevant generator period.
`X0` adds byte-sampled marker scatter, bounded resampling and persistent
color cycling through the shared P raster primitive. Its 1,152 native
raster cases, signed-word/state tests and 126 coherent application captures
cover shapes, writing modes, cycles/cache rules, normalized extents,
retained Drawto state, XOR over text and reconnect.
`X13` adds bounded signed-word array math and decimal console/graphical
output through the existing renderers. Native exhaustive word/index tests
and 144 coherent application captures check arithmetic, safe division and
bounds, staged W/X7 buffer reuse, typed coordinates, exact pixels/cells/RGB,
input partitions, recovery and reconnect, with audio and MIDI off.
`G` adds all sixteen indexed Boolean screen/memory transfer operations,
retained capture coordinates and stride views, phase-preserving images and
local second-copy deadlines. Independent native pixel/row-alias vectors and
coherent application scenes cover capture/replay, hidden memory edits,
clipping/status protection, input recovery and reconnect.
`X14` selects G's persistent transfer route. Native signed-word and lifetime
tests plus 162 coherent application captures verify both banks, opposite
G1/G3 cross-memory directions, self-copy selection, disabled routes and
reset/reconnect defaults without immediate raster or console side effects.
`X11` adds native-byte bitmap wipes and incremental raw imports into indexed
memory 0, with optional single display, retained image suffixes and local
receive deadlines. Independent native tests and 191 coherent application
captures cover all ST modes, encodings/status choices, random fills, timeout
accumulation, opaque controls, binary Telnet escaping and reconnect. The shared
parser exposes generic raw-field observation/early completion; bitmap policy
remains in CTerm. Physical audio and MIDI are disabled in these fixtures.
`X8` adds requested/raw palette rotation, dual snapshots and sticky restore
selection through the existing palette backend and local deadlines. Native
checks verify every intermediate write across 2022 independent cases plus
input/lifecycle/error/max-count coverage. Its 288 coherent application
captures check unchanged indexed scenes, palette/RGB/cache state, queued
commands, cancellation and reconnect across all ST modes/encodings/status
choices, with audio and MIDI disabled.
`X15` adds anchored sweeps through the existing polyline renderer, with
exclusive unscaled bounds, original endpoint retention and exact local
post-line waits. Wider host arithmetic fixes wrapped/infinite sweeps.
Independent 600-case native compositions and 451 coherent application
captures cover ordered patterned/wide/arrow/XOR drawing, console overlays,
Drawto, status/input/error/lifecycle boundaries and key forwarding during
waits, across all ST modes/encodings/status choices with audio/MIDI off.
The GIST software-voice foundation adds standalone register/record tests:
1044 independent envelope cases, 160 API cases, all pitch/control words and
original-patch lifetime checks. These do not establish PCM playback or b/n/N
command support; PSG synthesis has separate functional tests below.
Live audio-source tests additionally cover sample delivery and acknowledgment,
clear/detach and concurrent stream destruction/engine stop with an active
callback, using the existing consumer and device-free pull runtime.
PSG tests add literal register/envelope/gate/noise expectations, rational
local-sample phase/frequency checks, DC probes/filter tails, partition
invariance and all 20 original GIST patches. No foreign clock or reference
PCM-byte identity is required. The b command now adds editable source/saved
patch banks, exact trigger counts, restoration and stop-all, through a dedicated
session stream shared by GIST and console sounds. The Program Settings
"Atari ST Key Clicks" toggle suppresses local clicks independently of BEL and
remote effects, applies to live sessions, and survives terminal reset and
application restart. Native audio, settings/runtime and headless editor tests
cover these preference paths; physical-device audibility is not claimed.
Disabled audio advances on
local deadlines without PCM; fast phase/noise jumps prevent replay on re-enable.
Native input/lifecycle/error tests and 198 coherent application captures cover
all ST modes, Atari ST/CP437/UTF-8 and status choices, with physical audio/MIDI
off. The n helper adds private normalization, priority-7 mutable-patch notes,
local deadline/key waits and selected/all release/stop, with 235 independent
helper cases and 306 coherent all-mode/encoding/status application captures.
It corrects low-word clock-wrap early exits and preserves source/caller state,
queued input and key ownership. N adds retained raw MIDI/chip buffers, byte
alignment, replay ranges, distinct inner/outer keys, flow and local deadlines
through those existing helpers. Native buffer/backpressure/lifecycle tests and
342 coherent all-mode/encoding/status application captures check opaque input,
retained records, unchanged scenes and reconnect. No audible/device coverage
is claimed.
X5 now configures flow mode and custom XON/XOFF bytes/counts through staged
numeric fields. Literal native cases and 288 coherent application captures
cover signed/loop rules, partial stores, exact raw replies, unchanged scenes
and reconnect across all modes/encodings/status choices with physical output
disabled. Existing serial helpers retain their once-per-entry mode gate and
live byte/count reads.
X3 now loads and sends opaque right-button macros, retaining its individual
configuration stores and old payload suffix. All-buttons release resumes the
serial suffix and queued original key events. Literal native coverage and423
coherent all-mode/encoding/status application captures check exact raw bytes,
unchanged pixels/palettes/glyphs, local/Wren mouse routing, UTF-8 key replay and
disconnect/reconnect with physical audio/MIDI disabled. N and X3 share the
owner-side input queue. Complete command, loop and input coverage is recorded
in the IGS ledger.
X4 adds48 retained zone definitions, individual scaled coordinate stores,
signed-word length clamping, raw auxiliary loads/discard and strict-loopback
controls for the `<` consumer. Native literal tests plus432 coherent
all-mode/encoding/status application captures verify opaque data, unchanged
scenes, queued keys and EOF/reconnect, sharing X3's auxiliary service with
physical sound/MIDI disabled. Negative zone memory escapes are rejected.
X6 adds exact left-only release CR/CRLF and immediate right-only handoff to
X3, with shared owner-side key/serial suspension. Native byte/word/split/
callback/lifetime cases and675 coherent all-profile captures verify raw output
order, unchanged scenes and Wren/UI/disabled/scrollback/reconnect behavior.
Only mode byte2 appends LF; initial both-buttons does not enter the left hold.
`?` adds raw version/cursor/pixel/resolution replies and owned interactive
cursor/marker/GEM input. All8 original AES forms use indexed save-under and
exact native masks/hotspots. Native independent form/reply/lifecycle cases
and864 coherent all-profile captures cover overlay cleanup, prior mode/host
visibility, press versus release state, queued keys and reconnect. Uninitialized
motion, write-mode restore and malformed decimal bugs are corrected explicitly.

`<` collects keyboard or X4 zone input and defers its raw reply until the
whole graphics chain exits. Keyboard echo uses the existing VT52 parser;
mouse forms share indexed pointer rendering with `?`. It retains129 printable
bytes, exact press-zone selection and strict retry, with safe Delete bounds
and cleanup on EOF/reset/error.

Shared graphics-parser tests additionally cover source-supplied typed numbers,
retryable waits and exact handoff to wire fields, plus direct/chained command
selection and suspended-view ownership. IGS `&` now uses these sources for
stored-operand and gang execution, with local deadlines, cooperative input
service and exact handoff to wire fields when the loop ends mid-command.
Independent native traces/scenes and846 coherent application captures cover
flow, text/raw ownership, deferred replies, cancellation and reconnect.

SkyPix has its own parser and Kick 1.3/3.1 rendering expectations documented
in [SKYPIX_CONFORMANCE.md](SKYPIX_CONFORMANCE.md). Its application test covers
both variants with status on/off, Latin-1/CP437/UTF-8, configurable pixel canvases,
native Amiga fonts and complete descenders, system-font UI isolation,
transfers and delay/reset/disconnect recovery. Diamond 20 cases check independent
cursor-probe coordinates, mask clipping and proportional advances in both
variants/encodings on Amiga 640x200 and 640x400, plus literal 8x8 CP437 UI in
both modes. The 400-line cases check native glyph rows below Y=199, exact
canvas/status clipping, aspect ratio, reset and directory geometry restoration.
Startup and full reset select sixteen colours; upper-pen pixel checks cover both
phase planes without a mode command. Explicit eight-colour selection retains
depth-reduction, brush-promotion and cursor-complement coverage.
Primitive rendering edits the native indexed/RGB raster in place and preserves
untouched phase pixels. Native tests check exact damage, canvas/stride boundaries,
transparent glyph revision and exhaustive brush minterms/masks; application tests
check literal corner clipping. See the SkyPix raster implementation notes for the
remaining restoration, depth and scrolling snapshots.
The RoF caller-header regression retains a native font-reset baseline across
horizontal ANSI movement, so the following line cannot erase the lower glyph
rows. Literal complete-scene expectations cover both phases, variants,
CP437/UTF-8, status choices and 640x200/640x400; native tests cover every input
split and horizontal/vertical movement aliases.
Status reserves a row inside the selected mode without changing resolution,
aspect or system grid: 640x200 remains 80x25, 640x400 remains 80x50. Current,
Custom and non-Amiga modes (including 80x43's six padding pixels) have literal
mode-table geometry/boundary expectations in both variants and status choices.
Native-font wrapping
checks actual advances, exact-edge cases, explicit flushes, disabled wrap,
widened boundary comparisons and Unicode revision without a second wrap.
Depth tests cover all sixteen pens, four-bit text foreground/background,
reset/reduction/expansion and both pixel planes. Brush tests cover palette
ownership, first-blit application, depth promotion and standard ByteRun1
no-op/128-byte boundaries without weakening transfer error recovery. Literal Diamond i/W rasters check both planes after
wrapping, BS-space in JAM1/JAM2 and tcsh's BS CSI K deletion sequence; native
tests check all erase-line modes against the current font's ascent/descent.
`syncterm_skypix_welcome` checks the captured RoF Times/15 network lines
against independent native-strike pixel-mask hashes, with both variants,
CP437/UTF-8, status choices and reset/replay. Times/15 uses its native seven-pixel
nominal width for BS/tabs and wrap-off positioning; wrapping itself uses
actual advances. The Kick 1.3 first-line expectation is independently revised
for per-character ROM clipping; the 3.1 mask and final positions remain intact.
Native compositor tests
verify cursor overlay pixels, capture exclusion, clipping and snapshot cleanup.
An identical-cell clear followed by explicit graphics and a status-row update
must retain literal primary/alternate raster colours;
native SkyPix cursor tests check idle stability and clean background restoration.
This is separate from the ANSI
CP437/UTF-8 inventory below and from the historical RIP captures.

SkyPix cursor rendering follows SkyTERM: exact nominal width at the text
baseline, complementing active native plane bits with no idle blinking or
separate overlay. `cterm_skypix_cursor` checks widths 8/9/12/20, clipping,
exact background restoration, the command-entry effect at the first bang,
unknown/rejected commands, font/reset transitions, Unicode revision, brushes
and absence of cursor ghosts in brushes and retired scrollback. Unlike SkyTERM,
the implementation tracks its actual live underline separately from its enable
flag, removing it before mutation and movement. `syncterm_skypix_cursor` checks complete literal scenes
in both planes, palette/state and menu snapshot restoration across both
variants, CP437/UTF-8, status choices and 640x200/640x400. Old position/raster
fixtures now gate cursor operations explicitly and observe logical positions
instead of relying on JR-Comm overlay metadata. The selected text geometry,
separate CR/LF and broader ANSI support remain as documented in the SkyPix
reconciliation table.

`syncterm_skypix_cache` checks command-16 types 2/3 as opaque cache uploads
in both variants, CP437/UTF-8 and status choices. Literal SOH/STX packets test
CRC retry and duplicate protection, LF/NUL/high bytes and padding, shared cache
hits across types 2/3/20, filename confinement, unsupported modes, cancellation,
disconnect cleanup and following terminal input. Reply-fenced captures check the
retained brush and palette together in both pixel planes. It also checks type-3
loading after receipt/cache hits, padding, odd lengths, invalid-load retention,
type-2/20 isolation and reset using coherent sample/pixel/palette observations.
Physical audio is disabled, so application tests claim loading and immediate
muted-play recovery, not audible output.

The [1.10 SkyPix report review](SKYPIX_110_REPORT.md) maps all six reported
issues to the current implementation and tests. `syncterm_skypix_report` checks
RIP identification isolation, immediate outline/fill, positioned text, named
font framing and reset/editor recovery using literal full-scene expectations.
It also checks Pearl/8 and Opal/9 selection with trailing empty and explicit
extra numeric parameters, right-edge advances, and height-zero reset on both
Amiga resolutions, Kick variants, encodings and status choices. String positions
are independent of numeric positions. Native parser cases check all input splits,
empty strings, the 17-field/32-digit/1,024-byte limits, long unterminated input
and EOF recovery; command-16 uploads and cache hits also carry extra fields.
It covers both variants, CP437/UTF-8 and status choices with reset/replay;
native `cterm_skypix` checks every input split and bytewise replay.
`amiga_font_report` checks all 65 embedded native files and exact native
metrics/ink hashes for all 35 strikes in each Kickstart profile. The user's current ACIS
screenshot is recorded separately as manual verification, not a pixel golden.

`cterm_skypix_audio` checks the independent signed-PCM vector and literal linear
interpolation results before the common mixer. Pull/fake backends verify channel-1
routing, quantized periods, varying buffer sizes, continuous repeat phase,
zero-as-one, bounded buffering above 131000 source bytes, parser holds, backend
latency/failure, disabled/unavailable output, allocation failure, previous-sample
retention, odd/max-length input, reset/EOF/shutdown and active destruction. No
device is opened and no physical Amiga waveform/filter or audible claim is made.
The Wren host test additionally drains a long-sample queue, then checks refill
while connected/menu input waits run with a hidden cursor; caller timeouts and
borrowed-owner removal on destruction are retained.

`syncterm_skypix_mouse` checks the documented literal controller-1 bytes,
native zero-based pixels at sub-cell positions and scaled drawable mapping,
with both variants/encodings/status choices. It checks bright/dim M, the
Alt/Command-O and online-menu gates, disabled button-state recovery, UI/status
exclusion, reset, and local wheel scrollback. Native tests cover invalid/missing
pixel coordinates, boundaries and ANSI mouse requests during SkyPix. Command-22
gadget definitions check inclusive foreground fills, replacement without erasing
old pixels, endpoint slots, invalid definitions and unchanged controller-1 clicks
inside/outside the rectangle. Native tests also check retained five-word records,
all input splits, clipping, numeric wrapping and full-reset cleanup.

`syncterm_graphics_scrollback` checks the shared history path in ordinary ANSI,
RIP v1/v3 and both SkyPix variants, with status on/off and CP437/UTF-8 where
applicable. Literal coloured rectangles and two blink planes survive live
viewing, scrolling, palette changes, disconnect and reopening from the directory.
The RIP cases include an actual RIP_PIXEL command; SkyPix includes native Pearl
g/p/y text after remote font selection. Clipped/overlapping Surface copies and
text overlays retain their expected pixels. Input replies fence one coherent
capture; source expectations are fixture colours and pinned glyph rows.
Native CTerm/history tests cover optional text-only storage, ring wrap, resize,
capacity trimming, terminal reset, invalid requests and destruction.

`syncterm_skypix_scrolling` checks exact Diamond/20 bitmap scrolling and a stable
bottom cursor using literal colour stripes in both variants/encodings/status
choices. Partial history strips join the next scroll without a gap at the live
boundary, including in disconnected and directory views. Native tests check
both phases, palette changes while a strip is pending, ring wrap and capacity
limits, reset, detached snapshots, an off-grid 19-pixel overshoot, ordinary cell
scrolling after a fractional move, and exposed background through Ruby/12's
font-row remainder. The raster scroll reaches the drawable bottom, independently
of how many whole font rows fit; status and mode padding outside that drawable
area remain intact. Literal box outlines crossing the old font-row boundary
stay continuous through Times/15, Ruby/8 and grid-aligned Pearl/8 scrolls on
640x200/640x400 in both variants/encodings/status choices. Expected full-scene
pixels follow SkyTERM's documented ordinary ScrollRaster copy rule. The RoF
login screenshot/capture motivated this test but is not an expected raster.

`src/tests/st_terminal_conformance_test.py` runs the normal application through
its control bitmap backend and a private loopback Raw connection, in CP437 and
UTF-8. Each group is a separate CTest `syncterm_conformance_<group>_<encoding>`.
Enable `ST_CONSOLE_TEST` and run `ctest --test-dir <build> -R syncterm_conformance`.

This inventory tracks the local Synchronet termtest reference at SHA-256
`a8bc360b2b1cd9c1b6041160d3e994a410132418fb568a369f3a10724915ab00`. It includes all 149 active output
cases, its one commented historical regression, and all 59 keyboard cases.
Repeated names have `.2` suffixes. The JSON manifest beside the runner is the
machine-readable inventory; each group fails if it misses an assigned case.
Update both inventories when adding or changing coverage.

Expected cells, cursor coordinates, colours and replies are specified independently
of the emulator. A terminal status reply fences input processing; a single control
checkpoint then captures cells, palette, planes and application state together.
Keys travel through the real application input and connection paths. Version and
optional capability replies have bounded format expectations; supported fixed
replies are exact. Pixel assertions cover downloaded fonts, conceal, copy/paste,
blink planes and palette changes without assuming physical palette-bank numbers.

The supplemental `syncterm_behavior_cterm_reset_cp437` and
`syncterm_behavior_cterm_reset_utf8` tests invoke Wren's direct `CTerm.reset()`
with RIP off/on. They check retained screen text, default modes/margins/colours,
cursor state and exact replies after incomplete escape/decoder input, repeated
reset, reset from an input hook and a pending RIP prompt. Related cells, palette,
pixels and state come from one fenced control capture. Truncated UTF-8 displays
the presentation font's dedicated boxed-cross decode-error glyph, whose 8x16
raster is independently specified; native capture tests separately check its
U+FFFD text callback. Native CTerm tests retain history checks and real failures
before/after reset effects. This adds no new RIS sequence semantics or native
reset algorithm, and does not claim audio/native-display coverage.

The `keyboard` group also checks physical Alt+Keypad decimal entry: codepage
bytes, leading-zero Unicode, non-BMP text on UTF-8 connections and non-BMP
mappings on ATASCII/PETSCII/Prestel codepage connections, duplicate native
text suppression, invalid values and focus-loss cancellation. These expectations
come from SyncTERM's documented input convention rather than termtest.js.
UTF-8 checks require Alt+255 and Alt+0255 to send U+00FF identically.
CP1251, KOI8-R, CP850 and ISO-8859-1 connections check both direct byte entry
and the corresponding Unicode entry against fixed wire bytes. Wren tests also
check queued and claimed input across encoding changes with an independent
display font, including Unicode numeric input in menus.
Native X11 tests separately cover keypad aliases that could trigger window
shortcuts. Wren compatibility-key tests retain their font-codepage decoding checks.

The supplemental `term_key_forward` C test checks the session's shared native
keyboard-forwarding boundary against fixed output bytes in UTF-8 and CP437,
with RIP off/on. It covers Unicode and non-BMP text, NUL, explicit numeric
bytes, Shift-Tab, DoorWay Up, unsupported Alt keys, physical edges with
reporting disabled, invalid scalars/events, and a stopped output queue.
Forwarding must bypass script hooks and local session commands. Numeric byte
0x82 must remain CP437 é in a RIP session, independent of the renderer's
internal UTF-8 mode. These are wire-boundary checks; the control-bitmap
keyboard suites above retain application-level keyboard coverage.

Tab-stop tests cover insertion at the end of the list, sorted insertion,
duplicate stops, and reinsertion after clearing the list. The latter must not
mistake stale allocation contents for an active stop; the focused C tests also
run these cases under sanitizers.
Default-stop coverage checks widths 1, 8, 9, 20, 80, 132, 145, 160 and 8192,
exact reports (including replies larger than the former fixed reply buffer),
right-edge wrapping and bottom-row scrolling. Resize tests preserve custom and
deleted stops across shrink/grow, extend defaults into new columns, and respect
clear-all until reset. No stop beyond the initialized width is required.

The additional `recovery` group runs 48 bounded cases with seed `0x43544552`,
including overlong parameters, malformed strings, invalid UTF-8 and random bytes.
Every case must recover known cells, margins and cursor coordinates and answer a
query. TCP write boundaries are not parser chunk boundaries: the existing
`cterm_text` C tests retain exhaustive split/bytewise decoder coverage.

`cterm_music_render` holds foreground ANSI playback, APC audio waits on
channels 0, 1, 2 and 15, BEL playback, response output and application APC
hooks while the real presentation worker flushes independent pixel damage
and renders both cursor blink phases through the shared bitmap presenter.
CP437 and UTF-8 cases retain bytewise music input and following-text ordering,
then check normal completion, flush cancellation, audio shutdown and reset
recovery. APC dispatch closes the active parser display's batch before cache
I/O or callbacks, including APCs routed from the status display to its parent.
BEL and response callbacks likewise release their batch for playback or
transport backpressure. These are native headless rendering tests using
pull-only audio, without physical audio or MIDI output.

`cterm_numeric` uses a pull-only audio runtime to check long ANSI music
parameters without opening a physical device. It covers leading zeroes,
values beyond signed/unsigned integer ranges, all five numeric fields,
clamping, silent out-of-range notes, exact frame counts, split capture,
invalid capture and reset recovery in CP437 and UTF-8. Font DCS and macro
parameters are checked before narrowing; the control-bitmap `extensions`
group additionally verifies that oversized macro fields cannot replace an
existing macro, using exact cursor positions in both encodings.

`cterm_sixel` checks literal expanded pixel rectangles, opaque/transparent
backgrounds, background-only images, single-pixel output, every input split
and bytewise input in CP437 and UTF-8. Oversized dimensions, repeats and
palette selectors exercise saturation before narrowing, clipped background
storage, the last palette entry, zero pixel sizes, and tall rows that scroll
above the viewport. Cancellation, a replacement string, viewport changes,
reset and destruction cover partial-image ownership and cursor restoration;
unchanged geometry and failed resize requests retain the pending image.
The control-bitmap `extensions` group checks both pixel planes and cursor
coordinates from one reply-fenced capture for normal and oversized sixel
sequences; these fixtures do not open physical audio devices.

`syncterm_emoji_aspect` checks the independent grinning-face ink extents in
square-pixel and 6:5 tall-pixel colour banks at 8, 14, and 16 pixels high,
using a single control capture for related cells, both pixel planes and cursor
state. It repeats after terminal reset. Native mode/aspect-only redraw,
threshold selection, compressed-stream validation and copied font ownership
are covered by the console colour-glyph tests. No auditory or native-display
coverage is inferred from these bitmap checks.

`syncterm_emoji_presentation` independently checks two-cell emoji presentation
and single-cell text presentation across 8-, 14-, and 16-pixel fonts, with
colour both enabled and disabled. It covers native narrow symbols such as
smiling faces, hearts, weather, vehicles, and the hut; repeated resets must
preserve cell pairs and cursor advancement. A VS16 revision at the right margin
must wrap the complete emoji without leaving its narrow prefix behind.
CP437 byte output retains its
original cell width. Font tests separately verify that native monochrome
outlines remain unchanged, centered when emoji presentation needs two cells.

Audibility and physical speed pacing are explicitly partial below. Real OS
clipboard/window operations and native console/input integration remain backend
tests. This inventory does not claim coverage of every sequence in cterm.adoc,
or of terminal emulations other than ANSI-BBS. Keep the specialized CTerm, audio,
RIP, APC, transfer, native backend and lifecycle tests. The focused `cterm_apc`
fixture also checks that cache store/list operations cannot overwrite or expose
`syncterm-cache-name.txt`, the visible per-BBS ownership metadata.

These headless fixtures set `AudioModes=0`, so they do not open physical audio
devices. BEL/music cases check parsing only; silence here is not an audio pass.

RIPterm 1.54 compatibility has a separate [historical regression inventory](RIP154_CONFORMANCE.md),
including reference rasters, parser boundaries, button interaction and capture UI context.
Its CP437/UTF-8 interaction tests also check host-command templates, local playback
variables and pick-list input with coherent screen restoration.
RIP raster checks compare both blink phases against the historical graphics
references and add literal packed-index, viewport/stride, RGB fallback and
clipboard expectations. Flood fill restores v1.10a's two-phase viewport output;
the single-plane-source blit contract is checked explicitly.

Failures print the case identifier and retain the outgoing input, exact reply
bytes, case trace, coherent snapshot and PPM under the build's
`reports/test-control` directory. Recovery IDs include seed and iteration.

| Reference case | Line | Group | Assertion scope |
| --- | ---: | --- | --- |
| `NUL` | 209 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `BEL` | 214 | extensions | Partial: parsing/recovery only; audibility needs audio-device validation |
| `BS` | 220 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `HT` | 228 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `LF` | 235 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `CR` | 240 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `NEL` | 245 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `HTS` | 250 | movement | Custom tab stops, sorted and duplicate insertion, reinsertion after clear; expected cursor coordinates |
| `RI` | 257 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `APC` | 262 | extensions | String consumption and surrounding visible text |
| `DCS` | 267 | extensions | String consumption and surrounding visible text |
| `PM` | 276 | extensions | String consumption and surrounding visible text |
| `OSC` | 281 | extensions | String consumption and surrounding visible text |
| `SOS` | 287 | extensions | String consumption and surrounding visible text |
| `RIS` | 292 | movement | Cursor home after reset |
| `ICH` | 298 | editing | Expected edited/scrolled cell contents and cursor coordinates |
| `SL` | 314 | editing | Expected edited/scrolled cell contents and cursor coordinates |
| `CUU` | 327 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `SR` | 340 | editing | Expected edited/scrolled cell contents and cursor coordinates |
| `CUD` | 357 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `CUF` | 370 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `CUB` | 383 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `FNT` | 396 | extensions | Successful font selection report and changed glyph raster |
| `CNL` | 414 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `CPL` | 429 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `CHA` | 444 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `CUP` | 457 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `CHT` | 484 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `ED` | 504 | editing | Expected edited/scrolled cell contents and cursor coordinates |
| `EL` | 531 | editing | Expected edited/scrolled cell contents and cursor coordinates |
| `IL` | 554 | editing | Expected edited/scrolled cell contents and cursor coordinates |
| `DL` | 585 | editing | Expected edited/scrolled cell contents and cursor coordinates |
| `CTSAM` | 607 | extensions | Partial: parsing/recovery only; audibility needs audio-device validation |
| `BCAM` | 624 | extensions | Partial: parsing/recovery only; audibility needs audio-device validation |
| `DCH` | 630 | editing | Expected edited/scrolled cell contents and cursor coordinates |
| `SU` | 643 | editing | Expected edited/scrolled cell contents and cursor coordinates |
| `XTSRGA` | 662 | queries | Exact reply; version and optional capability formats validated |
| `SD` | 677 | editing | Expected edited/scrolled cell contents and cursor coordinates |
| `ECH` | 696 | editing | Expected edited/scrolled cell contents and cursor coordinates |
| `CVT` | 708 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `CBT` | 732 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `HPA` | 754 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `HPR` | 767 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `REP` | 780 | movement | Repeated graphic cells and resulting cursor |
| `DA` | 795 | queries | Exact reply; version and optional capability formats validated |
| `CTDA` | 804 | queries | Exact reply; version and optional capability formats validated |
| `VPA` | 813 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `TSR` | 821 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `VPR` | 832 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `CUP.2` | 845 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `TBC` | 872 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `BCDM` | 894 | extensions | Doorway NUL quoting displays a literal CP437 control glyph |
| `DECSET` | 900 | extensions | Exact X10 mouse button-down report from injected cell click |
| `HPB` | 927 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `VPB` | 940 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `BCRST` | 953 | extensions | Leaving doorway mode restores cursor sequence parsing |
| `DECRST` | 959 | margins | Expected cursor/cells or exact mode replies across margin/wrap transitions |
| `SGR` | 970 | attributes | Logical RGB cell colours and rendered planes |
| `DSR` | 992 | queries | Exact reply; version and optional capability formats validated |
| `BCDSR` | 1004 | queries | Exact reply; version and optional capability formats validated |
| `CTSMRR` | 1018 | queries | Font selection, cell dimensions and exact enabled-mode report; both mode-80 states |
| `DECDSR` | 1039 | queries | Exact reply; version and optional capability formats validated |
| `DECSTBM` | 1060 | margins | Expected cursor/cells or exact mode replies across margin/wrap transitions |
| `DECSCS` | 1081 | queries | Partial: speed setting/report; physical pacing remains a timing test |
| `CTSMS` | 1090 | margins | Expected cursor/cells or exact mode replies across margin/wrap transitions |
| `DECSLRM` | 1123 | margins | Expected cursor/cells or exact mode replies across margin/wrap transitions |
| `SCOSC` | 1145 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `DECSC` | 1153 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `CT24BC` | 1161 | attributes | Logical RGB cell colours and rendered planes |
| `CTRMS` | 1177 | margins | Expected cursor/cells or exact mode replies across margin/wrap transitions |
| `SCORC` | 1211 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `DECRC` | 1219 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `DECTABSR` | 1227 | queries | Exact reply; version and optional capability formats validated |
| `DECRQCRA` | 1236 | queries | CRC-16 calculated independently over captured modern cell ABI |
| `DECINVM` | 1243 | extensions | Defined cursor macro invocation reaches expected coordinates |
| `CTOSF` | 1249 | extensions | All three font payload lengths consumed; exact downloaded 8x16 glyph raster |
| `CTELCF` | 1272 | margins | Expected cursor/cells or exact mode replies across margin/wrap transitions |
| `CTDLCF` | 1291 | margins | Expected cursor/cells or exact mode replies across margin/wrap transitions |
| `OriginMode` | 1301 | margins | Expected cursor/cells or exact mode replies across margin/wrap transitions |
| `CUUClamp` | 1330 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `CUDClamp` | 1336 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `CUFClamp` | 1342 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `CUBClamp` | 1348 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `HPBClamp` | 1354 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `VPBClamp` | 1360 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `DECRQSS_m` | 1366 | queries | Exact reply; version and optional capability formats validated |
| `DECRQSS_r` | 1382 | queries | Exact reply; version and optional capability formats validated |
| `DECRQSS_s` | 1395 | queries | Exact reply; version and optional capability formats validated |
| `CTSMRR_4` | 1409 | queries | Exact reply; version and optional capability formats validated |
| `CTSMRR_5` | 1429 | queries | Exact reply; version and optional capability formats validated |
| `CTSV` | 1439 | queries | CP437/UTF-8 wire replies restricted to BS–CR/SP–~; native α/β transliteration, every other byte, malformed input, long/empty/disabled versions and retained split-input framing |
| `CTQJS` | 1450 | queries | JPEG XL capability reply (not joystick) |
| `SGR256FG` | 1461 | attributes | Logical RGB cell colours and rendered planes |
| `SGR256BG` | 1479 | attributes | Logical RGB cell colours and rendered planes |
| `SGRRGBFG` | 1497 | attributes | Logical RGB cell colours and rendered planes |
| `SGRRGBBG` | 1515 | attributes | Logical RGB cell colours and rendered planes |
| `SGRBright` | 1533 | attributes | Logical RGB cell colours and rendered planes |
| `SGRNeg` | 1569 | attributes | Logical RGB cell colours and rendered planes |
| `SGRConceal` | 1600 | attributes | Logical RGB cell colours and rendered planes |
| `OSC4` | 1619 | attributes | Logical palette change and single-entry reset |
| `OSC104All` | 1651 | attributes | Logical palette change and full palette reset |
| `DECSCUSR` | 1676 | attributes | Block/underline raster extents and blink state for styles 1 through 4 |
| `AutoWrap` | 1698 | margins | Expected cursor/cells or exact mode replies across margin/wrap transitions |
| `RIScroll` | 1720 | editing | Expected edited/scrolled cell contents and cursor coordinates |
| `LFScroll` | 1733 | editing | Expected edited/scrolled cell contents and cursor coordinates |
| `DECSETBlinkBG` | 1747 | attributes | Logical RGB cell colours and rendered planes |
| `CopyPaste` | 1769 | extensions | Exact pixel plane restored; text cells stay erased (not OS clipboard) |
| `DECDMAC` | 1787 | extensions | Hex-encoded macro definition and invocation reaches expected coordinates |
| `DECMSR` | 1809 | queries | Exact reply; version and optional capability formats validated |
| `DECCKSR` | 1820 | queries | Empty and defined-macro CRC-16 with echoed request IDs |
| `DECSTBMScroll` | 1831 | margins | Expected cursor/cells or exact mode replies across margin/wrap transitions |
| `DECSLRMScroll` | 1852 | margins | Expected cursor/cells or exact mode replies across margin/wrap transitions |
| `SGRDim` | 1866 | attributes | Logical RGB cell colours and rendered planes |
| `CR.2` | 1884 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `LF.2` | 1896 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `BS.2` | 1909 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `HT.2` | 1922 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `NEL.2` | 1934 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `RI.2` | 1942 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `HVP` | 1950 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `HTS.2` | 1961 | movement | Exact report for explicitly installed tab stops |
| `LCFFullLine` | 1987 | margins | Expected cursor/cells or exact mode replies across margin/wrap transitions |
| `LCFDSR` | 2029 | margins | Expected cursor/cells or exact mode replies across margin/wrap transitions |
| `Regressions` | 2045 | editing | Active regression here; commented out in reference |
| `DECERA` | 2058 | editing | Expected edited/scrolled cell contents and cursor coordinates |
| `DECFRA` | 2068 | editing | Expected edited/scrolled cell contents and cursor coordinates |
| `DECCRA` | 2076 | editing | Expected edited/scrolled cell contents and cursor coordinates |
| `DECIC` | 2084 | editing | Expected edited/scrolled cell contents and cursor coordinates |
| `DECDC` | 2096 | editing | Expected edited/scrolled cell contents and cursor coordinates |
| `DECSSDT_hide` | 2108 | extensions | Status type zero, main last-row text and cursor |
| `DECSSDT_host` | 2119 | extensions | Host status text and main text routed to their respective rows |
| `DECCARA` | 2134 | attributes | Logical RGB cell colours and rendered planes |
| `DECRARA` | 2151 | attributes | Logical RGB cell colours and rendered planes |
| `DECSACE` | 2168 | attributes | Stream versus rectangular attribute-change cell boundaries |
| `CT24BC.2` | 2186 | attributes | Logical RGB cell colours and rendered planes |
| `FETM` | 2201 | queries | Exact reply; version and optional capability formats validated |
| `TTM` | 2212 | queries | Exact reply; version and optional capability formats validated |
| `CVTwithVTS` | 2222 | movement | Expected cursor coordinates; defaults, clamping and tab stops |
| `OriginMode.2` | 2235 | margins | Expected cursor/cells or exact mode replies across margin/wrap transitions |
| `SLMargins` | 2246 | margins | Expected cursor/cells or exact mode replies across margin/wrap transitions |
| `SRMargins` | 2256 | margins | Expected cursor/cells or exact mode replies across margin/wrap transitions |
| `CTSMS_CTRMS` | 2267 | margins | Expected cursor/cells or exact mode replies across margin/wrap transitions |
| `SGRNoBlink` | 2284 | attributes | Logical RGB cell colours and rendered planes |
| `SGRNormInt` | 2296 | attributes | Logical RGB cell colours and rendered planes |
| `SGRDefFG` | 2308 | attributes | Logical RGB cell colours and rendered planes |
| `SGRDefBG` | 2320 | attributes | Logical RGB cell colours and rendered planes |
| `SGRBrightBG` | 2332 | attributes | Logical RGB cell colours and rendered planes |
| `DECRQSS_*x` | 2346 | queries | Exact reply; version and optional capability formats validated |
| `DECRQSS_*r` | 2352 | queries | Exact reply; version and optional capability formats validated |
| `OSC8` | 2359 | extensions | Hyperlink cell bounds and exact opened URI |
| `IN:Up` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:Down` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:Right` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:Left` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:Home` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:End` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:PgUp` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:PgDn` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:Ins` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:Del` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:BkSp` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:S-Tab` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:F1` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:F2` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:F3` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:F4` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:F5` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:F6` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:F7` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:F8` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:F9` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:F10` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:F11` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:F12` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:S-F1` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:S-F2` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:S-F3` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:S-F4` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:S-F5` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:S-F6` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:S-F7` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:S-F8` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:S-F9` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:S-F10` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:S-F11` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:S-F12` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:C-F1` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:C-F2` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:C-F3` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:C-F4` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:C-F5` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:C-F6` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:C-F7` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:C-F8` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:C-F9` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:C-F10` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:C-F11` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:C-F12` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:A-F1` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:A-F2` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:A-F3` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:A-F5` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:A-F6` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:A-F7` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:A-F8` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:A-F9` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:A-F10` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:A-F11` | input_keys | keyboard | Exact encoded bytes from injected normalized key |
| `IN:A-F12` | input_keys | keyboard | Exact encoded bytes from injected normalized key |

Custom font assignments are additionally checked by
`syncterm_font_assignments_CP437` and `syncterm_font_assignments_UTF-8` through
the control bitmap backend. Independent 256-glyph fixtures assert exact raster
columns for all four source attribute combinations, masked changes, untouched
assignments, retained cells, failed selection and reset recovery in coherent
captures. Native `cterm` tests cover per-geometry remote mapping metadata and
restoration; these checks do not claim new remote-font protocol behavior.

MIDI's `music_parse`, `music_decode`, `music_calibrate`, `music_player` and
`music_stream` tests
independently check all 84 ANSI pitches, existing articulation/frame rounding, SMF0/1/RMID,
tempo/running status/SMPTE, split SysEx, FIFO limits, internal PCM bounds,
external channel-zero gain/fade/wait/cancellation and runtime shutdown. The
calibration tests check independent K-weighted sine references, separate
channel DC offsets,
DC-only silence, silent/error paths and state
restoration using the actual ANSI synth voice. Literal program messages and
file/ANSI transitions check voice selection without changing MIDI-file
instruments. PCM player captures check fixed source calibration with unchanged
channel gains, wide mixing before clipping and fade/recovery behavior. Final
PCM power diagnostics exclude DC independently in each stereo channel.
Live MIDI shares the worker and decoder, retaining partial messages, running
status, SysEx and held voices across writes. Literal split-input tests cover
wire recovery and realtime; injected-sink tests cover bounded continuous PCM,
raw/file/ANSI transitions, copied FIFO ownership/backpressure, completion
positions, flush/fade/error recovery and in-flight shutdown quiescence.
IGS N uses this queue; its parser/session coverage is tracked separately in
the IGS inventory. No physical-device coverage is implied.
Optional final-output captures support independent loudness comparisons;
unweighted RMS is not used to claim equal perceived loudness.
Manually selected synth checks compare onset and sustained levels across
eight pitches and verify dry ANSI/file-effect restoration where supported.
They render into discarded buffers or the pull-only mixer without devices.
The `extensions` control group checks feature 102 with MIDI disabled, rejected
cache-path recovery and seven-octave ANSI capture recovery in CP437/UTF-8.
These tests do not claim native MIDI-device or auditory coverage. MIDI and
PCM masks are both disabled in shared headless application fixtures.
