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
