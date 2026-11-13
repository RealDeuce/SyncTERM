# BIG numeric expectations

`st_big_number_test.c` contains literal expectations independently transcribed
or hand-derived from `~/IGS/docs/big/05-numeric-reader.adoc`, chapter 34,
`numeric-examples.json`, and the original instructions at
`04f3:0f26..11b1` and `04f3:144c..14e8` retained in `code-evidence.adoc`.
The target is BIG 1.03 terminal, 248206 bytes, SHA256
`edc9e2280d822d41361a11f622d8a70e16ada4e792a4944a77cc26af532f4a1c`.
BF viewer semantics and altered DOS memory/FPU environments are excluded.

The 54 literal vectors distinguish destructive weighting from ordinary
decimal concatenation and ordered auxiliary tests from mutually exclusive
token parsing. Expected consumed counts include the completing nondigit and
any auxiliary bytes, preserving the following suffix. Each vector runs at
every split and bytewise. The count-wrap fixture has 65536 digits followed
by `56:!`; WORD count wrap replaces the first two slots and returns 56,
leaving `!` unread, without storing the whole incoming token in the reader.

The exhaustive scaling oracle multiplies the original binary64 numerator
`0x13333333333333` by a WORD magnitude using two 32-bit limbs, then divides
by `2^51`. It uses neither the implementation's finite g3 integer formula
nor host floating-point multiplication. Chapter 34 proves the original
software/default 64-bit significand rounding cannot cross an integer for
these inputs, so exact rational truncation supplies the expected result.
All 65536 WORDs run across 30 scale/resolution/class combinations, with
explicit signed interpretation and low-WORD reduction.

Timeout tests supply an explicit expired-deadline event. They verify ordinary
zero versus auxiliary SPACE, ready-byte consumption before substitution,
BEL/auto policy and retained digit slots. They do not claim real-time
scheduling coverage. Shared-parser tests use the actual BIG reader and
independent expected values, including supplied values that bypass scaling,
chaining, input-free timeout completion and partial-field EOF recovery.

The tests require no external documentation tree, ROM, DOS program, graphics
display or physical audio device. Stored-loop evaluation, final random
expansion and application deadline scheduling have separate owners covered
by the later component/integration fixtures below.


`st_big_random_test.c` uses chapter 33 and `controls-examples.json` seed and
range facts. Its seed-zero/seed-one/state-maximum expectations are literal
arithmetic derivations. The multiplier inverse `0x2925141d` is checked by its
modulo-2^32 identity, then selects a predecessor for every possible 15-bit
rand output. The range checks cover all outputs independently of random
sampling. Poll partition tests verify exact state advancement through rejected
attempts and resumption after bounds change. No DOS seed-time conversion,
command integration or physical audio behavior is claimed.


`st_big_loop_test.c` transcribes or hand-derives expectations from chapter 21,
`loop-examples.json`, chapter 65, `loop-review-examples.json`, chapter 73 and
original `04f3:11b2..143b`. Its twelve complete sequences have literal expected
values, final stepping positions and endpoint state. Publication tests retain
unused table words and distinguish pre-flow publication from post-flow
classification. The 2048-x-word fixture reaches the documented cumulative
counter wrap after 32 table passes; its next value must repeat position31.
No expected results come from the implementation under test.

Callbacks expose local wait/checkpoint/RNG completion boundaries without
running clocks or devices. Tests verify completed prefixes survive waits,
errors and cancellation, including a random tail after loop mode cleared.
The shared-parser adapter combines the real ordinary and stored readers:
within one four-coordinate command, stored Y 5 stays5 and wire Y 5 becomes11.
Every input split and bytewise feed checks that source boundary and unread
suffix. Further checks cover pending source input, random resumption and EOF
recovery. These are component tests, not claims of complete & dispatch,
application key mapping, native timers, graphics or audible-device coverage.


`st_big_numeric_test.c` uses the production parser callbacks. Local-time
expectations explicitly use the portable 91/5-units-per-second policy, not a
foreign-machine clock model:73 units first expire at 4010989011ns; one/two
loop units reach 54945055/109890110ns. Supplied timestamps test both sides of
boundaries, rearming after each acquisition, saturation and backwards/error
samples. Separate tests call the real project monotonic backend without sleeps.

Literal expected values cover late-byte consumption, retained weighted slots,
BEL-before-scaling effects, delayed key reads, raw03/18 cancellation, shared
RNG suspension/suppression, exact-loop1 common-tail recheck, mixed stored/wire
Y 5 values 5/11 and final random ownership after loop termination. The parser
fixtures bind the actual callbacks (no test replacement reader), test every
split/bytewise source seam, and verify raw payload ownership, partial clock
error counts, EOF and fresh-field recovery. Wire/session wakeups are covered
by the application integration below; physical keyboard-device coverage is
not claimed. The native `cterm_big` fixture now binds real key entry points
to these callbacks: independent expectations cover post-value03/18 cancellation,
non-cancelling bytes, delay probes, extended zero/pending-scan boundaries,
CP437/UTF-8/doorway/modifier replay, Unicode non-truncation, saturation and
reset/EOF/callback-reset prefixes. Those standalone key cases inject setup;
additional wire & cases are covered below.


`st_big_state_test.c` takes I/R call order and lifetimes from chapters 27,
54 and 80. Literal expectations distinguish normal I0's MBBVA sequence,
I1's sixteen ascending palette calls, state2 early returns, full-WORD
normalization, driver3 R3 downgrade and cached resolution stores after a
returning rejection. Independent driver checks and host failure prefixes are
tested without generating expectations from the implementation. No indexed
page storage, rendering or complete command dispatch is claimed.

`st_big_profile_test.c` takes BIOS mode, page pitch, geometry and console font
facts from chapters 47, 48 and 77. The three full font banks are pinned by
FNV-1a64 values independently calculated from the original ROM files, not
from SyncTERM. They match existing font0 banks byte for byte. The restore
patterns likewise have an independent full-data identity. Chapter 26/83's
64-colour EGA table is checked against the individual red/green/blue bit
weights; the sixteen default codes are literal. Full SHA256 identities and
the source paths are retained in docs/BIG_IMPLEMENTATION.adoc. No external
ROM/reference installation is required to run these tests.


`st_big_control_test.c` transcribes footer/flow rules from original chapters
21,32,33 and43. Literal traces distinguish repeat from finishing XON, type/
gang cleanup, chain deferral and pending preclear. An independent unsigned
interval oracle checks all65536 count predicates for signed flow repetition
and signed WORD(count+1)>index. A real32767-byte flow runs in bounded batches.
Raw reply expectations include NUL/ff/pipe/tilde and exact CR readiness counts.
Callbacks alter current counts/data/CR and block before/after byte acquisition
to verify original read order, latched arguments, completed prefixes and errors.
The bounded reply test emits positions1..255 before rejecting position256;
it does not infer safety from a preclamped total. No fixture runs a foreign
UART, clock or DOS program. CTerm/application integration is checked separately.

`st_big_stream_test.c` uses chapters7/8/62/64/73's literal auxiliary/text
ownership. Every split covers CR/LF filtering, embedded NUL/ff and attention
bytes, normal empty @ and exact128-byte completion without draining @.
Independent deadline cases retain OLD's suffix after overwriting only its
first byte, consume/notify on late @, distinguish auxiliary SPACE/auto policy,
reject backwards samples and check completion idempotence. The loop/type
entry matrix and blocked first-pass XON verify reuse and whole-call timing.

Live `cterm_big` setup tests use those production readers and wire &, including
header/step/P stores, inherited random suppression/history, clamped P with
all2048 actual table words, untouched suffixes, bounded setup/body work,
gang copy through NUL/retained tail, raw byte identity, load abort that still
executes zero slots, runtime keys, blocked XOFF publication, stale text,
partial-pair/body identity and reset/EOF/callback prefixes. The coherent
application fixture adds fixed/gang wire loops and delayed native Ctrl-C
with exact flow, retained scene/current result and buffered-tail resumption.
None of these fixtures need external reference files, devices or audio.

`cterm_big_resolution` binds the actual wire R to the state operator. Literal
ROM rows and complete indexed scenes cover every input split/bytewise,
CP437/UTF-8/status and all three geometries. Further cases distinguish
same-mode reinstall, full-WORD normalization, unscaled g3 arguments,
application versus installed defaults, frame/ratio/source retention, counted
replies, stored R1..3, partial EOF/reset/timeout and returning mode failure.
The application fixture adds70 independent R transition scenes with a dirty
sentinel and no subsequent ED. Its168 coherent captures include the earlier
fixed/gang/input-cancellation and reconnect cases, with physical audio off.

Native wire g cases check full-WORD storage, split/bytewise ownership,
I0/I1/R retention, delayed stored values, deferred replies and partial
EOF/reset/timeout. Reader probes use real wire-acquired g/R state with the
production numeric callbacks and independent Y5 expectations8/11, including
a rejected R3/cache3 while the surface remains350 lines. No synthetic
coordinate command is registered. The application fixture adds70 unchanged-
scene g captures (238 total); live coordinate consumers remain separate work.

The live X6 cases in `cterm_big_resolution` now use actual class1/class2
operands through full-WORD X dispatch. Independent expectations cover12/6
defaults, split/bytewise CP437/UTF-8/status, signed1..15 clamps, no point/
cursor movement, X's raw prefix before Y waits, changed-current-X clamping,
I/R/EOF/reset/timeout, unknown selectors/repeats/replies and source-to-wire
seams after selector or X. Actual g3/cache3 Y5 becomes11, but final stored
Y5 stays5. The application fixture adds70 step/default/retention scenes,
308 coherent total; pointer acquisition and the other X selectors remain pending.

`cterm_big_line` wires L/D/d through native CTerm with independent literal
expectations from chapters12/51/57. Patterncccc selects L's increasing-X
positions2/3/6/7, while equal-Y D starts at the target and selects5/4/1/0.
The fixed-slope corner yields two pixels; separately clipped thick sides
remain visible after a rejected center. Relative d preserves the Y-sign
defect and distinguishes public target computation from old DDO start.
Fixtures compare complete indexed blink planes and explicit point prefixes,
including live colour changes, stored/wire seams, repeats/deferred replies,
XOR, failure/EOF/reset/timeout and recovery. Every split is checked in the
default profile, with bytewise input across all three modes, CP437/UTF-8 and
status choices. The real
application adds70 independently authored line scenes (896 total) with
coherent scene/text/palette/state/reply expectations. No reference executable,
external ROM, physical input or audible device is required.

The same native fixture checks lowercase m from chapter51's complete caller
and moveto/moverel listings and hand-derived examples. It distinguishes
signed operation>1 absolute arity from zero/one/negative relative forms,
retains the Y-sign defect, and checks WORD/origin/public-versus-lower points,
live CP after acquisition, unchanged pixels/attributes/console cursor,
source/scaling seams, repeats/replies and partial failures/recovery. Every
split is tested in the default profile; bytewise cases cover all three modes
and both encodings/status choices.42 coherent application scenes (938 total)
expect no raster change from absolute/relative m, then literal D pixels from
the new point. Expected scenes are authored independently of SyncTERM.

M cases derive from chapter50's complete command/setter/device listings and
its hand-derived attribute examples. `big_state` checks all65,536 WORDs using
the independent predicate input2-or3, while retaining unrelated state/old
errors and checking uninstalled/null rejection. Native line cases cover every
split/default profile and bytewise/all mode/encoding/status combinations,
XOR cancellation versus COPY, distinct blink-plane pens, class0/source/flow,
EOF/reset/timeout and failure prefixes. Native input cases check M/I1/R,
pointer cached-mode restoration after completion/EOF/reset and I0.56 new
application scenes (994 total) independently expect XOR lines to cancel,
M3 dots to copy, R actual0/cache1 and invalid258 normalizing0. Both mode
owners are now checked in every coherent scene; physical audio remains off.

C cases use chapter50's complete two-read/store caller and hand-derived
delayed-colour examples. `big_state` checks all selector WORDs and all colour
WORDs per valid slot independently, with separate installed state and old
errors retained even when BGI is uninstalled. Native cases add split/bytewise
mode/encoding/status, full-WORD fallback, consumer BYTE/pen reductions,
delayed second fields, EOF/reset/timeout/reset callbacks, g3/source seams,
deferred replies and I1/R/I0 lifetime.56 application scenes (1050 total)
use literal coloured candidates and a3x3 cyan RGB swatch, observe all four
application colour WORDs plus installed draw/device/fill owners, and retain
the existing full indexed/text/palette/state/reply oracle. No raster output
from SyncTERM is used to generate expectations.

`big_fill` derives A and its lower built-in/custom wrappers from chapters
10/11/50/54/83. The stock96 bytes are independently transcribed; literal
packed pixel rows distinguish slash orientation, checker rows, zero/solid
masks and foreground pen identity. All selector WORDs and all colour WORDs
exercise built-in reduction versus unsigned custom rejection. Separate
application slots, library/driver user copies, active masks and indexed
tile lifetimes are checked, including current fill BYTE changes without
retiling, temporary built-in overrides followed by resubmission, valid
overlapping source copies, old errors and builtin/custom uninstalled prefixes.
The overlap cases distinguish forward byte order and re-reading the original
source from copying the library mirror.

`cterm_big_fill` independently expects A wire states at every split/default
profile and bytewise in all three modes, CP437/UTF-8 and status choices. It
checks that A leaves both indexed planes/cursor/points intact, class0/stored
sources and repetition/flow, invalid-custom deferred replies, partial EOF/
reset/timeout/callback reset, I1/R/I0 defaults and actual uninstalled prefixes.
The initial wire case failed before registration.56 added coherent scenes
bring application coverage to1106, comparing selected/public/device fill
state and literal eight-row cached tiles with the unchanged full indexed,
ROM text/cells, palette, points/cursor, replies and coloured RGB scene.
These tests do not claim bar/flood/sector pixels or physical audio coverage.

X1 editing cases use chapters10/50's complete header, eight-store loop and
separate restoration test. `big_profile` pins the original96-byte defaults;
`big_fill` checks all slot/alter WORDs, every restore destination, literal
BYTE prefixes, unchanged BGI object state, live locals/edit-then-restore,
first-error latching and cancellation. Native cases check all splits/default
profile, bytewise/all mode/encoding/status, invalid headers/special values,
partial EOF/reset/per-read timeouts, source completion after the header and
first stored row, all11 supplied values, repetition/flow, deferred replies,
I/R retention and A installation timing. A live-frame test verifies changed
destinations and the exact accepted-byte/error prefix before recovery.
56 coherent application scenes (1162 total) independently expect retained
slot changes, stale installed tiles after edit/restore, then explicit A
resubmission. The existing full indexed/text/palette/state/reply/RGB scene
remains unchanged; no expected output is generated by the renderer.

`big_bar` uses chapters11/23/59 and the addressed original DDO rectangle
clip/raw EGAVGA bar listings. Expected clip results and edge/middle visit
traces are literal. A direct mask truth table checks all256 row bytes and
both endpoint phases over one to four byte rectangles; independent diagonal
rows check absolute X/Y phase. Tests distinguish WORD translation before
signed sorting, signed extrema versus line-clip overflow, disabled/inverted
clips, unsigned raw sorting and checked native bounds. All sixteen pens
copy under either line writing mode. Current fill pen versus stale tile,
rejected custom installation, per-row mask and per-bar pen reads, unchanged
point owners/state, old errors, accepted-prefix failures and synchronous
sink rules are checked. No expected pixels come from an emulator or external
reference executable. This is primitive coverage, with no B wire/bar3d or
new coherent application coverage claimed.

`big_producer` uses addressed DDO0615..0699/025f..0282 and EGAVGA point
addressing/cleanup in chapters13/58/59. Literal sequences distinguish a
first-point repeat from repeated non-first points and check8001 markers.
All256 mode bytes and65536 configured buffer-size WORDs are checked. Cases cover
WORD run wrap, original capacity overflow, native extent errors before the
closing point or marker and exact reached scratch/register tails. Point
returns are literal across all bit phases and both batch paths; every byte
value is checked as read AL/colour DL, retaining CX/DX effects, old errors
and all point owners. A host fixture supplies pre-write bytes and checks
read-before-write, current physical clipping, captured values and failures.
It does not emulate a controller, allocate/flush buffers or claim native
indexed read/B/curve integration or new coherent application scenes.

`big_bar3d` derives literal shapes, phase/visit sequences and point states
from chapters23/59's full public/DDO caller and the shared producer evidence.
It covers COPY/XOR corners, patterned strokes, inverted inset, orientation,
low top BYTE, wrapped depth boundaries, current saved corners/tuple, collector
markers/capacity, point-register clobbers and accepted-prefix failures.
`cterm_big_bar` adds every input split/default profile and bytewise/all mode/
encoding/status cases with full indexed blink planes, plus acquisition/
attribute timing, sixth-read EOF/reset/timeout, current depth, rejected fill/
uninstalled recovery, g3 Y5->11/class0 fields, supplied-source seams, inclusive
varying repetition and counted replies. Its native batched point checks
reconstruct all four read-plane bytes from actual indexed primary pixels.
84 application scenes (1246 total) independently expect negative attributes,
COPY under line XOR, corner parity, front/side/top/CP changes and opaque
checker holes over prior nonzero pixels. Complete text/pixels/palette/state/
cursor/replies and solid RGB interiors come from coherent captures; no
expected pixels are generated by SyncTERM or an external renderer.

`big_flood` derives independent literal spans, LIFO/directional/containment
behavior and byte-pass traces from24/59 and the full EGAVGA0eaa..1452 source.
It checks every mask/pen pair through whole-byte writes, partial plane pens,
opaque holes, cache/solid snapshots, fresh comparison reads, clip-edge
shrink, inverted/narrow probes, pending-boundary continuation, overflow
prestate, bounded-storage/cursor failures, interruption/recovery and all
point owners. Equal palette codes for distinct indices do not merge borders.
The operation is synchronous with no yield/resume budget; native errors
return their actual prefix. The bounded frontier replaces unsafe original
SS:BP underflow without claiming exact heap-dependent capacity. F/Z native
wire/flow/page/resource integration was pending at the kernel checkpoint.

`cterm_big_fill` now binds F's class1/2/0 numeric acquisition, signed clamps,
fill-only prefix, current border after XOFF and independent live loop tests
before XOFF/after rendering. Literal full-plane expectations cover all splits
and modes/encodings/status settings, source/repetition/replies, readiness
waits, EOF/reset/timeout, bounded exhaustion and host-error recovery. Native
byte passes preserve separate blink-plane pen bits and untouched alternate
pixels outside the mask. The native frontier has512 explicit records, based
on4096 nominal bytes, without reproducing DOS heap/stack placement.

BIG command errors now cancel pending acquisition/source/input/flow/footer
state and warn without failing terminal input/polls or disconnecting. Tests
retain accepted state/pixel-prefix assertions and inspect last error/count.
`cterm_big` checks a rejected negative X4 followed by graphics/ANSI/DSR at
every split, including bytewise. Fill readiness failures check that the first
unread byte is retried after cancellation; keyboard/pointer errors check
poll recovery.70 new application captures (1316 total) add solid/checker/
empty fills and same-connection rejection recovery, with complete retained
indexed/text/palette/state/points/cursor/replies/diagnostics and RGB samples.
Page/padding/configured buffers, Z and global composition remain pending.

`big_state` independently checks all65536 H WORDs: negatives become0 and
nonnegative values retain all bits, without an upper clamp. Every other
application/BGI/frame byte stays unchanged even with BGI uninstalled and an
old error. `cterm_big` adds every split/default profile and bytewise all
three modes/encodings/status settings, full indexed planes/retained state,
prefix/timeout/EOF/reset, I/R lifetimes, class0 scaling, source/repetition
and blocked/deferred reply cleanup. The H wire case failed before its
registration.28 new coherent application captures (1344 total) prove
H256/negative0 retain the prior scene, text/palette/points/fill/cursor/replies/
diagnostics and RGB. H's Z/O/Q/V/Y consumers remain separate pending steps.

`big_draw` now checks the original public rectangle's four standalone edges,
literal pattern trace (including raw upper-Y ordering), repeated XOR corners
and degenerate four visits. First-corner capture/live later arguments,
unchanged state/points and callback errors have separate expectations.
`cterm_big_bar` binds Z with every split/default profile and bytewise modes/
encodings/status, full indexed blink planes and original attribute timing.
It checks H0/1/256, solid/checker, outside reversed seed, clipping/WORD seed
wrap, thick borders rejecting the seed, zero-pattern escape, source/g3/loops/
replies, EOF/reset/timeout, bounded exhaustion and host-error prefix recovery.
84 coherent application scenes (1428 total) add H256 outlines, solid/checker/
empty floods, XOR edge/corner parity and reversed hollow outlines, preserving
the full previous text/pixels/palette/state/points/cursor/replies/diagnostics/
RGB. Expected pixels come from23/24/57/59 deductions. Z visible-page and H's
Z consumer are implemented; remaining page/buffer/global composition is pending.

`big_video` checks the borrowed indexed address/window boundary needed by G.
Literal addresses distinguish CPU aliases, aperture escapes and independent
display-counter wrap. Every canonical524288 pixel and every pixel for40
display layouts have independently walked expectations. All WORD pitches
with signed/unsigned page cases check active-segment and BIOS display-start
reductions. Tests retain full32-bit primary/alternate tokens, stale visible
backing, visible-only binding, actual hidden extents, padding and wrapped
page save/load, with exact partial/error prefixes and unchanged metadata.
`big_profile` separately expects BIOS4000/8000/a000 versus BGI4000/8000/9600.
Existing native B tests cover the consumed segment helper. This component
does not allocate, initialize, convert RGB or emulate a controller/scan clock.
Native G/page/image/console/presentation and resource bindings remain pending;
the unchanged application fixture is not rerun for this dependency.

`big_state` now independently checks all65536 G4/G5/G6 page WORDs against
five resolution-cache cases, all256 entry cache BYTEs, ordered local/device
publication and error prefixes. `cterm_big_pages` adds every split/default
profile and bytewise three modes/encodings/status settings, full-WORD/unscaled
input and source/repeat/reply/EOF/reset/timeout behavior. Active and displayed
pages differ in literal whole-plane expectations for P/pointer markers,
L/D/d/B/Z/F, XOR blink tokens and every batched point-read plane. Console
glyph/cursor banks, R3 active/display offsets, R2 display aliases, mode clears,
I1 retention, protected status artwork/source rows and exact save/load failure
prefixes are checked. Earlier Z whole-page cases retain their source-row
expectations in backing while expecting untouched native status pixels.
140 coherent application scenes (1568 total) add hidden/revealed graphics,
restored old ROM text/blink/geometry, explicit frame/physical page inspection,
source repetition and mode reset, with complete pixels/cells/palette/state/
points/cursor/replies/diagnostics and RGB. G0..3 image routes and full global
composition remain pending; this step does not claim complete G acceptance.

`big_image` derives expectations from chapters25/40/56 and the original
public size/header, DDO start translation and EGAVGA capture instructions.
It tests every WORD delta using independent modular distances, literal4/9
pixel encodings, all eight alignments and widths around byte boundaries,
three physical modes with rows below the visible bottom, explicit import
and cached source access, retained tails/padding and bounded malformed widths.
Every read error of a shifted capture retains independently expected raw
bytes and indexed bits. Shift0 lookahead, WORD source wrap, ffff-height
body entry, query0 capture through its actual cursor-wrap failure,16
unreachable owned tail bytes, short/header/cache extents, uninstalled state,
callback pauses and invalid pens have separate checks. No external images
or emulator-produced expectations are needed. The shared memory sink rename
also retains `big_flood` and native fill/bar checks. G0..3 acquisition,
native buffer lifetime/page routing and paste/application scenes are pending.

`big_image_paste` uses literal plane patterns96/3c/5a/c3 and an independent
five-entry Boolean truth table. It covers every nibble pair, full primary/
alternate memory across modes/alignments/byte-boundary widths, ordered rows/
planes/masks, all WORD destinations with representative origins and every
mode WORD. Cropped header visibility/restoration and every read/write failure
in a shifted first-row prefix retain independent whole-memory expectations.
Three physical bottom limits, right/negative rejection, signed ADD overflow,
the fresh zero image, cache reuse without packed re-decoding, last-byte spill
before a missing source, malformed width/height, separate source/video WORD
wrap and invalid extents/pens/layout/dispatch errors are covered. The fixture
initially compiled but failed to link the unimplemented API. The shared
four-bit kernel is exercised directly, and flood continues requesting COPY.
Native G0..3 wiring and application image captures remain pending; no new
coherent scenes or RIP regression coverage is claimed by this dependency.

`cterm_big_images` binds all image routes with every default-profile split,
bytewise all three modes/encodings/status settings, five Boolean modes with
distinct blink values, literal packed bytes and unaligned multirow patterns.
It verifies G1's original corner/cache3 skip, G3's two active calls, custom R1
memory3/active2/visual pages, overlap, gate/clamp/query prefixes, unknown WORD
selectors and retained buffer lifetimes across I/R/attention/EOF/reset/fresh
engines. Supplied-source loops bypass scaling while wire g3 Y5 becomes11.
Failure fixtures retain selected memory after G1, omit G3's final restore,
preserve three accepted native pixel writes before a byte callback fails,
continue ANSI/graphics replies, and reach actual cursor/aperture errors.
All modes capture virtual source rows behind distinct host status artwork
without reading/clobbering it.154 coherent application image scenes bring
coverage to1722, checking all G image operations, indexed planes/palette/
cells/state/points/pages/cursor/replies/header/payload and RGB interiors,
including R/I1 buffer retention and I1's independent hollow reset. Expected
images are authored from25/56 rather than generated by SyncTERM. Native G
is implemented; remaining global composition/final regressions stay pending.

`big_palette` derives public setter expectations from26/55's complete original
WORD/count/sign/store/dispatch instructions. Every value WORD runs for16 pens;
every index WORD runs under six count bytes; all256 sizes and initialized
states have boundary cases. Complete state comparisons retain unrelated
owners and old errors. Callback tests observe raw cache/background stores
before device errors, including an invalid synchronous pause. I1 and local/
saved palette lifetimes are checked; the new fixture first failed to link
the absent API. Native `cterm_big_palette` first failed against unsupported S,
then checks split/bytewise profiles, all256 raw byte codes with independent
component arithmetic, late shared gates/count/state, g3/stored loops/replies,
timeout/EOF/reset, nonfatal corrupt-count recovery and fresh-engine defaults.
Existing image pens remain distinct under RGB aliases, including a new capture.
98 new application scenes (1820 total) compare raw cache and physical RGB,
unchanged full indexed/cell/image/state/point/page/reply data during recoloring,
silent rejections, palette0 aliases and I1 restoration. Expected data comes
from the original palette constants/component weights, not the renderer.
Native S is implemented; X8 and remaining global/final acceptance are pending.

`big_palette_rotation` derives literal setter/getter/wrap expectations from
the complete X8 caller in26/55/palette-review-code. Every endpoint pair and
every endpoint WORD clamp, magic CBW capture, negative setter candidates,
current saved words, independent direction tests and each failure prefix
are checked without using the implementation to generate expectations.
Defined entry snapshots and explicit zero padding replace original
uninitialized stack reads; ordinary stale local behavior is retained.
`cterm_big_rotation` checks all splits and bytewise native profiles, complete
indexed planes/image retention and independent physical RGB, local unsigned
millisecond deadlines, key-after-delay order, count32767 wrap, all native key
kinds/NUL/extended scans/private scan replay, blocked reads/flow, asymmetric
loop flags, current count0 tail restore, g3/stored sources, deferred replies
and error/EOF/reset prefixes.126 new application scenes (1946 total) retain
the complete scene while checking both rotations/endpoints, saved negative
words, live local-key cancellation and I1 restoration. Native X8 is
implemented; sound/global composition and final regressions remain pending.

`big_flow_settings` derives X5 expectations from the complete33/52 caller.
All operation WORDs run under loop0/1/2/ffff, and each byte/count WORD runs
through both custom branches. Full unrelated-state comparisons, live mode
changes activating both branches/defaults and post-read loop override retain
the original ordering. The API fixture first failed to link before the reader
was implemented. Native `cterm_big_flow` checks all splits/default profile
and bytewise three modes/encodings/status cases, complete indexed planes/
RGB/image/state preservation, partial reads/EOF/reset/timeout/error,
g3/stored-wire seams and common loop/chain/deferred replies. Actual X8
flow tests verify NUL/ff literals, signed-negative counts and130-byte output
resumption without duplicate prefixes.126 new application scenes (2072 total)
observe flow settings/replies with the complete retained scene. Native X5 is
implemented; remaining global/full-suite acceptance stays pending.

`big_random` additionally derives X2 repair expectations from33/52's signed
compare/store instructions, sweeping every FROM/TO WORD with boundary partners.
Native `cterm_big_random` verifies all splits/default profile and bytewise
three modes/encodings/status choices, full unchanged indexed planes/native
RGB/BGI state/image data, injected low-WORD wall-second seeds and the actual
local wall-clock source. Raw FROM/old TO are observed during pending r; polls
advance the shared stream without a second seed or premature repair. Cases
cover source loops/g3, supplied32767 wrap, equality, deferred replies,
timeout versus EOF/reset and nonfatal clock/source errors.126 new application
scenes (2198 total) bracket seed observations with local wall seconds, check
one shared LCG draw separately, and retain the complete scene/state/replies.
Native clock conversion intentionally uses local time(NULL), with no DOS
calendar/TZ/DST/environment or foreign pointer model. X0/speaker/global and
final-suite acceptance remain pending.

`big_spray` derives X0 sampling and point/helper tails from the complete22/58
caller. Seed1's first eight outputs346/130/10982/1090/11656/7117/17595/6415
supply literal positive/negative sample positions. Inverse predecessor
42bdebe3 selects X32767/Y12747, allowing every signed range WORD to be
checked against independent magnitude division. Zero ranges/counts,
maximum32767 concentration, all marker tails, type/colour/anchor/counter
changes and failed draw prefixes retain original state/RNG ordering.
Native `cterm_big_spray` checks split/bytewise profiles, full indexed planes,
partial reads and colour-before-time, g2 scaling/stored bypass, shared r,
repeat/chain/replies, I/R/EOF/reset/error lifetimes, hidden-page reveal and
nonfatal aperture failure.126 new coherent application scenes (2324 total)
use zero ranges for independently authored point/crosshair/square geometry,
line XOR parity, point COPY, clipped output and count-prefix behavior.
Expected RNG states are calculated separately from bounded local wall-second
seed observations; pixels do not come from the implementation under test.
Complete prior pixels/cells/palette/image/fill/pages/cursor/points/state/
replies and RGB remain in one capture. Native X0 is implemented; remaining
global/final-suite acceptance stays pending.

`big_text_ratio` derives X3/clamp/public/DDO/custom/metric expectations from
28/53 and the complete text/style listings. Every WORD runs independently in
each slot with a full-state oracle. Raw cases distinguish unsigned maximum
rejection from uninstalled failure, saved zero versus effective denominator1,
restored original ratios versus returned dimensions, BYTE height wrap,
front-control bypass, changed callback owners and metric/error prefixes.
Literal DDO data1582..1591 initializes all ratios1, and the copied header
starts2b; these replace the former unsupported zero ratio initialization.
Native `cterm_big_text_ratio` checks splits/profiles, full indexed/RGB/image/
state preservation, a blocked fourth read, g3/stored source, I1/I0/R retention,
timeout versus EOF/reset and nonfatal validation/dispatch/source errors.
126 new coherent application scenes (2450 total) compare custom state while
retaining all prior spray/image/pixel/cell/palette/point/fill/page/cursor/
reply/RNG observations; earlier scenes also check initial text state.
These tests do not claim E/W font rendering or auditory coverage. Native X3
is implemented; remaining text/audio/global/final acceptance stays pending.
