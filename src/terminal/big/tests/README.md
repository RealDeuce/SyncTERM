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
expansion and application deadline scheduling have separate pending owners.


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
error counts, EOF and fresh-field recovery. Native event-loop wakeups and
keyboard-device mapping remain application integration tests to add later.


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
