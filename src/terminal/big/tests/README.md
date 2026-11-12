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
