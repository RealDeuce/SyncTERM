# RIPterm 1.54 compatibility tests

SkyPix is selected independently and has its own parser, raster policy, fonts
and asset state; see [SKYPIX_CONFORMANCE.md](SKYPIX_CONFORMANCE.md). The RIP parser
recognizes only its own CSI `!`, `0!`, `1!` and `2!` forms, passing unrelated CSI
forms to ANSI handling. SkyPix commands do not activate a RIP graphics engine.
Historical RIP captures and their exclusions remain unchanged.

The [RIP input-loss report](https://github.com/RealDeuce/SyncTERM/issues/2)
was checked against the v1.10b fix `8642e3451be5b872042daf88fce0cf366d737dcc`.
The modern path changes text geometry in place, retaining the terminal and its
session callbacks. Its parser emits passthrough text synchronously before
checking the cursor and keeps pending introducers outside the caller's input
buffer. The three legacy fixes therefore require no production changes here.
`cterm_rip_live` exercises the reported plain/bogus introducers and first-column
fallbacks at every two-part split and bytewise in CP437 and UTF-8;
`cterm_rip_parser` checks intact text after a split lone `!`, SOH or STX.
The live interaction suites check keyboard, button and mouse-region host output
after repeated resets and each of the five RIP 1.54 text fonts, with version
replies and DSR fences still working. These are independent literal expectations;
historical capture pixels are unchanged.

The later v1.10 commit `8781091d92` exposed a shared interleaving defect:
RIPv1 SOH/STX commands must interrupt an unfinished ANSI sequence while
retaining its prefix. The modern parser now keeps those two accumulators
independent, applies completion suppression before ANSI resumes, releases a
bogus introducer after the saved prefix, and records all consumed wire bytes
in its history. RIPv3 retains its escape priority; SkyPix uses its separate
selected parser. The legacy output-buffer/deferred-drain changes are
unnecessary for modern callback passthrough. Native independent expectations
cover every split and bytewise input, repeated interruptions, argument ESC,
autosense resumption, overflow and bogus-introducer/VT expansion. The live
wire suites check replies, text/colours, cursor and both pixel phases from
one coherent capture in CP437 and UTF-8. This closes S2/S4 and the mid-ANSI
SOH/STX priority difference here; the other audit findings remain open.

The numeric follow-up replaces the separate raster, host and file-query
readers with `cterm_rip_args`: validated signed base-36 conversion, low-word
reduction, a separate success result and the actual consumed text cursor.
It closes modern P1 and the conversion portion of S5. Consumers explicitly
convert bytes/flag words and check indexes, styles, sizes and image modes
before use. The [modern consumer ledger](RIP154_PARSER.adoc) records those
checks and retained differences, including safe rejection of unsafe reference
indexes, supported font sizes and the unresolved descriptor parameter cap.
`1B` now validates three reserved two-digit fields separately; `1I` validates
its six-field prefix. The nonpositive `Z` count uses the executable's explicit
0.005 step, correcting the older residual-register explanation.

`cterm_rip_args` independently checks literal spellings, invalid syntax and
all 65,536 word values in both positive and negative form. Native consumer
tests cover signed clipping, byte colours/patterns, high flag bits, reserved
fields, palette/style/font/image bounds, transfer rejection and recovery.
Split/bytewise parser expectations include both spellings of `ffff`.
Each live wire suite adds 27 numeric cases and three Bézier count cases
with independent replies, cells/cursor/state and literal pixels in both
planes from coherent captures. These run in CP437 and UTF-8 with physical
audio disabled. Historical captures and exclusions are unchanged.
The native FreeBSD CMake configure/build and all 36 selected RIP, SkyPix and
terminal/session CTests passed after this follow-up.

`1G` (`RIP_COPY_REGION`) now copies absolute screen pixels, independently
of viewport and drawing state, while preserving the RIP clipboard. RIPv1
retains the disassembled alignment/fifth-word quirks, inclusive rows,
pre-clamp height guard and downward endpoint re-sort. Its loose bottom
check copies only visible rows; invalid widths and out-of-raster accesses
are safely rejected. RIPv3 uses outward alignment, ignores the reserved
word, accepts one row and requires full destination fit. Both blink planes
are snapshotted and written under one console raster lock; reduced-display
row aliases read original pixels and retain logical write order.

`cterm_rip_copy` checks 27 literal geometry/rejection cases in both versions,
with intact and bytewise feeds, every split of an overlapping command,
distinct planes, raster padding, clipboard/state preservation and reduced
mapping. The live wire suites add 20 full-plane cases for each version in
CP437 and UTF-8. Each coherent capture also checks the cursor/cells,
following-command recovery and an unrelated clipboard paste. Expected
rectangles come from the local disassembly and `ripscrip.txt`; no new DOS
captures or regenerated historical images are claimed. See the complete
[screen-copy contract](RIP154_PARSER.adoc).
The native FreeBSD CMake configure/build and all 37 selected RIP, SkyPix and
terminal/session CTests passed after the copy implementation.

RIP's two thin-line paths now use `st_bgi_thin_line`, extracted from the
existing candidate/pattern loop. RIP retains clipping, thickness, coordinate
mapping, colour/XOR, current-point and error policy. BIG uses the same lower
stepping kernel with its own WORD clipping and marker adapters. The ten
RIP application suites and native raster/oracle tests passed after extraction;
historical captures and expected pixels were not regenerated.

RIP's existing bitmap and CHR bytes now reside once in the shared BGI
component, also used by BIG. The font accessor retains its IDs and borrowed
views. Both protocols use the common bitmap row expander and CHR record
decoder; RIP retains its placement, bounds, cursor/advance and sticky-error
policy. No RIP font bytes or capture expectations changed. This extraction
has a native build and BIG-focused kernel verification; RIP-specific runs
remain scheduled for the BIG goal's final full regression suite.

RIP command/control failures log warnings and continue at the existing wire
boundary. A rejected command before `|` does not swallow the following command;
following ordinary ANSI/text bytes continue through the base parser. A failed
raster operation clears its error latch before the next command. Native
`cterm_rip_engine` and `cterm_rip_variables` tests cover command and prompt
failures, including EOF/reset flushing. `syncterm_behavior_session_modes`
checks continued bidirectional connection use after a missing RIP scene and
the normal disconnect notice on later remote EOF.

Enable `ST_CONSOLE_TEST` and run:

```sh
ctest --test-dir <build> -R 'syncterm_rip154_|cterm_rip_' --output-on-failure
```

The ten `syncterm_rip154_*` suites exercise the ordinary executable over private
loopback Raw connections in CP437 and UTF-8. They use a 640x350 80x43 starting
screen, disable physical audio with `AudioModes=0`, and need no display server,
DOSBox, RIPterm installation, external corpus, Pillow, or network service.
Rendering checks resolve both pixel planes and palette from one coherent
control checkpoint. Wire replies fence parsing; injected input is awaited on the
connection before a capture. No timing sleeps or packet-boundary assumptions are
used. Failed cases retain their identifier, input/reply bytes, snapshot and PPM
under `reports/test-control`.

The separate `syncterm_behavior_cterm_reset_cp437/utf8` application tests also
exercise RIP-enabled direct host reset through Wren, including reset during an
input hook and a buffered RIP_DEFINE that opens the existing menu-VM prompt.
The native `cterm_rip_variables` test checks pending prompt completion,
reconstruction with persisted-variable reload and safe teardown after a prompt
failure. These checks concern `st_cterm_reset_terminal`, not changes to RIP's
protocol reset commands or the historical graphics captures inventoried below.

The supplemental `syncterm_graphics_scrollback` fixture exercises RIP v1/v3
through the shared graphics-history implementation: actual RIP_PIXEL output,
both blink planes, Surface composition, scrolling, palette changes and retained
disconnected/offline history, with status on/off. These literal expectations
are separate from the historical DOS captures and do not change their provenance.
The native bitmap clear/draw/status regression also protects explicit RIP pixel
rasters from a stale pending text redraw; no RIP wire semantics are changed.

## Raster edits and blink phases

RIP primitives edit the console raster under its write lock. A drawing command
borrows the normalized framebuffer once; line, stroke-font and curve walkers
reuse that view instead of locking or allocating at every pixel. XOR retains
the legacy unscaled source read and independently mapped destination write.
Bars, viewport erasure, polygon/ellipse fills and filled poly-Béziers no longer
allocate canvas-sized output images or masks. The poly-Bézier crossing marks,
polygon intersection storage and bounded flood seed stack remain part of their
documented raster algorithms. Damage covers the pixels painted, rather than a
full masked source image.

The fixed BGI palette-index bank 16–31 permutes colour bits, so native Boolean
blits can operate directly on indexed pixels even when two palette entries have
the same RGB. Palette publication updates the contiguous bank in one console
call instead of sixteen separate updates. RGB reverse lookup remains only where the input is actually RGB
or otherwise outside that native bank. Boolean paste and inversion edit their
destination rectangles in place. File loading and clipboard/screen snapshots
retain owned buffers because their pixels must outlive drawing; file I/O stays
outside the console lock. Independent button strokes/glyphs borrow the raster
for their operation, while isolated corner points use the single-pixel API.

Non-blinking graphics must be painted into both blink phases. Source buffers
without an alternate plane are copied to both destinations; a supplied alternate
is copied verbatim, with no per-plane modification tracking. Sixel and decoded
icons use the former contract. Saved screens and captured images preserve both
phases, including any captured blinking text. The SkyPix and shared console
drawing paths also explicitly write both phases or retain their saved originals.

SyncTERM v1.10a (`d2ef8842b2`, `src/syncterm/ripper.c:13636`) explicitly calls
`FREE_AND_NULL(pix->pixelsb)` before publishing a flood fill. Its bitmap blitter
then copies the filled primary image to **both** screens across the entire
viewport, including the border and other pixels outside the filled region.
That plane removal was lost in the modern port. The in-place flood path now
restores v1.10a's result by synchronizing the live alternate viewport from
primary after filling, without capturing and republishing the framebuffer.
The seed/scanline algorithm and its intentional RIPterm compatibility quirks
remain unchanged.

`cterm_rip_raster` checks separate native/RGB phases on a 648x360 raster, offset
viewports, world-coordinate XOR ordering, RGB-preserving inversion, all 256
source/destination indexed pairs for Boolean paste, RGB fallback and duplicate
palette RGB values. It checks both source-plane publication contracts and the
legacy flood synchronization, including unchanged phases outside its viewport.
The live `packed-raster-edits` wire case checks both phases, palette and geometry
in one reply-fenced capture: clipped offset fills/XOR, Boolean clipboard modes,
flood-filled interior/border, and bottom padding pixels below the text grid.
Every historical graphics scene below is compared against the same independent
reference in **both** phases, with its original UI exclusions intact.

## Historical reference captures

`src/tests/rip154/captures.json` inventories 26 independent DOSBox RIPterm 1.54
captures. Each primitive scene has its original `.rip` source and a zlib-compressed
640x350 RGB reference (`.rgb6.z`). The source XWD title identifies RIPTERM; input,
original capture and decoded reference all have SHA-256 provenance. These
expectations were imported from the old `src/syncterm/rip_test` research directory,
not generated by the current renderer. Source-file LF line endings are sent as
CRLF because CR terminates a RIP wire command. Literal LF handling has separate
parser and live tests.

Reference comparisons use the exact six-bit DAC channels. DOSBox expands a DAC
value of 42 to `0xAA`; SyncTERM may expand it to `0xA8`. Shifting each channel by
two removes only that presentation difference. There is no pixel-count tolerance,
nearest-colour matching, fuzzy edge comparison, or regenerated SyncTERM oracle.
All compared graphics pixels must match.

Historical context matters. The captures contain application UI that is not RIP
scene output. Explicit, case-specific exclusions are recorded in the manifest:

- `(0,0,24,8)` contains a legacy text-window `$` glyph and solid text cursor, except
  in `test12_buttons` and `test14_thick_xor_ellipse`, which hid that UI.
- The four `test14_buttons_plain` through `test17_buttons_sizes` captures include
  RIPterm's own status bar at `(0,336,640,14)`.
- `test03_floodfill` and `test14_thick_xor_ellipse` have an isolated black mouse
  pointer tip at `(639,349)`, identified from the historical capture context.
  The paired historical SyncTERM captures do not have it. A separate live
  `full-canvas-last-pixel` case checks that this drawable pixel is actually filled.

The original reference pixels remain intact, including excluded UI. Do not add
exclusions in response to a rendering failure. Establish the capture's historical
context or fix the behavior instead. The raw capture hashes identify which
historical observations these exclusions describe.

To reproduce the import from those original captures:

```sh
python3 tools/testing/import_rip154_captures.py \
  /path/to/legacy/src/syncterm/rip_test src/tests/rip154
```

The importer accepts only the recorded TrueColor XWD layout and RIPTERM window
titles. Its fixed scene list never selects `*-syncterm.xwd`. Review changed input,
reference hashes and UI exclusions when updating the data.

## Why these cases exist

The original Synchronet history and reverse-engineering notes supply the expected
behavior. Commit IDs below belong to the Synchronet reference repository.

| Historical evidence | Covered behavior |
| --- | --- |
| `679e5bae93`, `rip_test/ellipse_research.md` | BGI ellipses use decimal fixed-point scaling and truncation, not a mathematically smoother ellipse. Eight captured sweeps include 371 radius combinations, tall/wide and degenerate shapes. |
| `d1c27d856b` | Arc clipping follows BGI's extracted trigonometric tables. Captured ellipse and pie scenes cover quadrants and partial arcs. |
| `7e79e2c438` | Bezier evaluation accumulates the parameter increment, including its rounding error. Arithmetic must retain double-precision rounding at each operation rather than fused multiply-add or excess register precision. `test08_bezier` and `test13_thick_xor` preserve those pixels; focused C tests also check the integer-boundary samples that exposed platform differences. |
| `fadd8eb62f`, `rip_test/thick_xor_research.md` | Thick ellipses use a sweep of thick line segments, including duplicate-point caps. `test14_thick_xor_ellipse` checks the resulting XOR raster. |
| `e4478e1d16`, `rip_test/polygon_notes.md` | Degenerate vertices, sub-polygon closure, colour-zero outline suppression, patterned fill and XOR ordering. Polygon, edge and fill-poly scenes check the complete graphics region. |
| `e4478e1d16`, `rip_test/fill_notes.md` | BGI flood-fill quirks and pattern gating. The captured flood scene covers border openings and patterned fills; existing focused C tests retain additional flood expectations. |
| `e4478e1d16`, `rip_test/disasm_notes.md` | Iterative line clipping and independent thick-line side passes. The new C viewport test and live `viewport-origin-clipping` test check a nonzero origin; all captured line/Bezier/button scenes remain regressions. |
| `2959fde5a6`, `5ca54e0939`, `rip_test/button_notes.md` | Fixed/dynamic button extents, exclusive face edges, bevels, shadows, orientation and label metrics. Five captured button scenes exercise these combinations. |
| `9c50d54228` | Buttons retain their drawing font when later graphics select another font. Live pressed/released redraw and subsequent graphics text check retention and restoration. |
| `6cce9286f0`, `c438d8a8d5`, later `rip_test/cmd_parse.md` research | Literal `!!`, scene boundaries, repeated `|#`, EBC suppression and following introducers. The later disassembly clarified that LF inside a command is data handling, not a line terminator. Independent parser vectors and live cases preserve those distinctions. |

The manifest lists every captured scene and its particular scope.

## Wire, state and interaction cases

The `wire` suite checks both exact query replies and visible results:

- Both version-query forms, literal/mid-column/escaped introducers, SOH and STX,
  and the cursor-column fallback after ANSI cursor movement.
- Unknown-command EBC completion, VT passthrough, suppression of one completion
  LF, repeated `|#`, and LF buffered inside a command after `|#`.
- LF in fixed-width fields, odd and triple backslash continuation, literal
  backslashes, escaped pipes, caret/backquote control bytes including CRLF and NUL, and a
  CP437 query byte through both connection encodings.
- Partial reset semantics: draw colour, fill colour, line width and XOR survive
  `RIP_RESET`. Viewport clipping, world-coordinate scaling, the last drawable
  pixel, the executable's nonpositive-count Bézier step (`0.005`), malformed polygon counts
  and missing reserved font parameters.
- All five RIP 1.54 text-window font sizes, preservation of graphics across text
  window reinitialization, disabled ordinary text, and a fixed bitmap expectation
  for connection-encoded RIP graphic text.

The `interaction` suite checks mouse inversion and exact host commands, case-folded
button hotkeys, saved mouse fields, forward/backward Tab activation, checkbox
toggling, same-group radio exclusion, button font retention and font restoration.
These use the normal application input path; they are not direct calls into the
RIP implementation.

The same interaction suites exercise host-command template definition, chains,
repeated slots, null/undefined references, checkbox-group aggregation, and local
scene variables within host commands and templates. Nested scene queries retain
their order in the final host response. Pick lists run in the menu VM: keyboard
hotkeys, mouse selection, optional cancellation, required selection after Escape,
Unicode labels, and template responses are covered. Each dismissal checks the
restored pixel plane and palette from one coherent checkpoint in CP437 and UTF-8.
These are literal protocol expectations, separate from the historical captures.

The native `cterm_rip_variables` fixture additionally checks template lifetime
across window/direct reset, recursion and 4095-byte response limits, radio and
checkbox definitions, saved-field ownership, commands that delete their own
fields, missing/unsafe scene names, callback errors and recovery, and consumers
without popup UI. Protocol window reset retains templates; direct terminal reset
starts a new template store. Local playback retains the legacy eight-scene depth
limit and bare-name lookup without defaulting an extension.

`cterm_rip_parser` additionally runs 15 independent dispatch/text expectations at
every two-part input split and bytewise, including C0 data in arguments and EOF
flush. TCP writes are not a way to force decoder chunk boundaries, so these C
checks remain necessary. `cterm_rip_raster` tests a translated viewport with a
thin clipped line and a thick line whose center is invisible but a side pass is
visible.

## Command-parser comparison, 2026-11-14

The tables below retain the original audited baselines and reproductions.
The interleaving follow-up above closes modern S2/S4 and the mid-ANSI
SOH/STX issue; the numeric follow-up closes P1 and S5's word conversion.
S5's parameter limits remain open. Subsequent legacy commits `6cd3be1d5e`,
`06f0a7fec7` and `8781091d92` also postdate the v1.10b baseline below; do not read its rows
as assertions that those fixes are still absent from current legacy HEAD.

The parser is **not fully compliant** with the findings in
`/synchronet/src/sbbs/src/syncterm/rip_test/cmd_parse.md`. Passing the existing
regression suites does not establish that broader claim. The issue #2 checks
above concern three particular legacy defects and remain valid.

[RIP154_PARSER.adoc](RIP154_PARSER.adoc) records the full stream/number contract
and all 62 reference command entries, including every numeric field's use,
signed/unsigned checks, byte/word conversions, ignored reserved fields,
internal errors and unsafe reference accesses. It distinguishes RIP's cached
command state from effective BGI state and records corrections to the older
research, including the explicit Bézier nonpositive-count step of `0.005`,
the image mode table's unchecked byte index, and `1G`'s use of its nominally
reserved fifth argument during alignment. The follow-ups above distinguish
implemented fixes from the remaining parser/handler work.

This comparison used modern commit `794e369`, legacy v1.10b commit
`74cce5b27a610bddb4a05236c011b76747619122` (the current form of the issue #2
fix), and explicit Synchronet tag refs:

| Reference | Commit | Verification |
| --- | --- | --- |
| `refs/tags/syncterm-1.8` | `a20f6c6dee634b19b271479c082f02246b7b9ded` | Source comparison and parser replay using the current legacy console objects. |
| `refs/tags/syncterm-1.9` | `5bd319427e4f58cd965b056c69bb96ed47db7a81` | Source comparison; `parse_mega`, `parse_rip_args`, `cb_accept_introducer` and `cb_feed` are identical to v1.8. |
| `refs/tags/syncterm-1.8rc2` | `3e6e5cef79d2547f469e5022687f5487fbd82cda` | Source review of the earlier state machine; not a runtime test of that release. |

The release tags must be qualified: this checkout also has branches with the
same names. The v1.8 replay is a parser comparison, not a build/test claim for
the complete historical release or its Windows executable. Legacy sources and
the research document were not modified.

Independent native probes exercised 62 assembly vectors in 674 two-part/bytewise
feeds; eleven vectors differed from the reference expectations. Twenty-four
live control-bitmap observations checked the ordinary v2.0
executable in CP437 and UTF-8 with `AudioModes=0`; cells, cursor and pixels came
from the same DSR-fenced capture. A native headless legacy harness replayed the
corresponding cases against v1.10b and the tagged v1.8 parser. Temporary probe
sources and results were retained under `/tmp/rip-cmd-parse-audit` for this run.
They are diagnostic evidence, not registered passing CTest coverage for the
unresolved differences below.

The native CMake configure/build and all 35 selected RIP, SkyPix and terminal
session CTests passed. Those passing regression results do not close the audit
findings.

### Regressions introduced by the modern port

| ID | Reproduction | Reference / legacy behavior | Modern behavior and location |
| --- | --- | --- | --- |
| P1 | `!\|g001\r\n` | A final two-digit meganum field may contain one digit: move to column 1, row 2. v1.8/v1.10b both do so. | Remains at 1,1. `st_cterm.c`'s `g` handler requires four raw characters. Its `rip_number` helper and other fixed-offset host handlers also bypass the legacy descriptor decoder's shortened-field and `-` validation rules. `!\|g0-01\r\n` also moves to 1,2 in the legacy replay and is ignored here. |
| P2 | `ESC[3!HELLO` or `ESC[00!HELLO` | The CSI bang handler consumes the complete sequence; following text is `HELLO`. Both legacy parsers preserve all five letters. A zero-valued parameter also requests the version in RIPterm, even when zero-padded. | Displays `ELLO`: `cterm_rip_parser.c:feed_escape` only intercepts the exact no-parameter or single `0`/`1`/`2` forms. Other forms reach the ECMA parser, which treats `!` as an intermediate and consumes the following `H` as its final byte. Missing the zero-padded version reply itself is inherited; losing the next text character is new. |
| P3 | Unterminated wire command followed by session finish/disconnect | The documented wire machine never dispatches without CR or an unescaped pipe. Legacy `parse_rip(..., 0, ...)` drains deferred input without dispatching an incomplete command. Local scene EOF has a separate explicit flush. | Source inspection: `st_cterm_finish` unconditionally calls `cterm_rip_engine_flush`, and disconnect preparation, session teardown and several setters call it. Thus the local-EOF extension can dispatch a partial wire command. This boundary was source-reviewed; the live disconnect path was not independently replayed in this audit. |

P1 and P2 have direct modern/legacy runtime comparisons. P3 is a distinct
modern ownership/boundary issue and should not be presented as a measured
historical binary difference.

### Differences already present in v1.8 and v1.10b

These need consideration in both maintained branches, rather than being
classified as modern-port regressions.

| ID | Finding and independent reproduction | Evidence / affected implementation |
| --- | --- | --- |
| S1 | Three backslashes before a pipe dispatch in RIPterm because the last two are backslashes; four before CR continue because the last three are backslashes. These are not parity tests. For example, `!\|TA` + three backslashes + `\|Tnext\r\nZ` dispatches two text commands in the reference but merges them in SyncTERM. | Re-disassembled `1A04:007E–00DA` and `0125–0186`; both modern `trailing_backslashes(...) & 1` and legacy `nbs & 1` disagree. Native assembly probes and legacy replay confirm the current behavior. |
| S2 | `!\|#\rESC[31mZ` should suppress the first post-command ESC via EBC, then pass `[31mZ` as ordinary text. Both SyncTERM paths process the escape and leave EBC pending, eventually suppressing `Z`. | `1634:0272–0295` versus modern `feed_escape` priority and legacy `cb_esc_feed` priority. Native and live/legacy probes leave the line blank. |
| S3 | `!\|#\|!XYZ\r\n` should clear the bare-pipe accumulator, buffer `!`, then release literal `!XYZ` when `X` proves it is not a command. Both SyncTERM parsers discard the literal text as an unknown `!` command. | Re-disassembled `1A04:00F4–0117` and the called accumulator reset. Neither `feed_one` nor `cb_feed` implements this reset. Confirmed in modern live captures and both legacy replays. |
| S4 | The five-byte window should follow consumed RIP bytes too. `ABC SOH\|#\r!\|c0F\|X2S2S\r\n` should accept the second introducer after the command's CR. Both SyncTERM paths retain the earlier text history, reject it and display `ABC\|c0F\|X2S2S`. | Re-disassembled `1634:029B–02FB`; modern `slide` and legacy `cb_slide` only see the passthrough path. The live modern pixel stays black instead of white. |
| S5 | The reference numeric validator accepts `-`, but conversion uses the base-36 signed conversion routine and stores a 16-bit word. Both raster/legacy `parse_mega` implementations instead treat every `-` as a zero digit; the modern host helper rejects it entirely. | `110E:0213–02D8`, `01A2:6591` and the AX store at `110E:02D5`. A signed field must not be declared compliant from the current zero-digit implementation. Wide-value conversion and the documented 511-word parameter cap also differ from the current integer/point limits. Exact drawing for reference out-of-bounds/stale-coordinate cases is not a suitable compatibility target. |
| S6 | Numeric CSI decoding is absent for zero-padded autosense parameters. `ESC[00!` should reply with `RIPSCRIP015410`; neither release path does. | Re-disassembled `09C6:064A–06AD`: the handler tests decoded parameter count/value, not literal spelling. P2 is the additional modern text-loss regression on the fallback. |
| S7 | The reference descriptor table contains both `9 ESC` and `0 ESC`, and both jump-table entries target the same block-mode handler. Both SyncTERM command assemblers only consume level digits `1`–`9`, so the `0 ESC` spelling is decoded as command `0`. | The binary table at file offset `0x2FD62` and targets 59/60 at `1814:197D`; modern and legacy dispatch prefix decoding. Host dispatch also needs to preserve the alias if this reference behavior is implemented. |

One-way disable is an existing, explicitly documented SyncTERM compatibility
behavior, including the v1.8rc2 source. It is not a v2.0 regression: once
disabled, the RIP parser cannot receive `ESC[2!`. It nevertheless differs from
the research reference, where ANSI handles the enable sequence outside the RIP
gate. This audit does not silently change that policy. Similarly, newer RIP
extensions retained from legacy SyncTERM are not automatically bugs merely
because the RIPterm 1.54 descriptor table lacks them.

### Coverage of the research findings

| Research finding | Assessment |
| --- | --- |
| Q1: ANSI/RIP separation, autosense and unknown CSI | Known one-way policy; P2/S6 remain unresolved. Unknown ordinary CSI finals are consumed by the base ANSI parser. |
| Q2: unconditional backslash preprocessing and continuation | Generic escapes and escaped pipes pass the focused vectors. S1 covers the longer delimiter runs; do not extrapolate parity from the passing one/two/three-slash CR cases. |
| Q3: introducers, byte history and cursor fallback | Normal BOL/mid-column/escaped cases and every split pass existing tests. Modern S4 and mid-ESC SOH/STX are fixed; S3 remains. The notes' conflicting summary is corrected below. |
| Q4: pre-dispatch viewport/cursor helper | The modern boundary callback commits text before graphics; the legacy helper has no command-buffer side effects. Existing real-renderer cursor/split tests cover that ordering. |
| Q5: descriptors, meganums, freeform text | Empty `T`/`@` graphic text becomes a space. P1 and signed/word conversion are fixed; S7, parameter limits and the consumer differences in the argument ledger prevent a full compliance claim. |
| Q6: lone introducer followed by CR | `!`, SOH and STX release both bytes; native split probes pass. |
| Q7: embedded C0 data | The listed non-NUL C0 bytes other than LF/CR remain command data; 29 independent byte vectors pass at every split and bytewise. LF is swallowed and CR dispatch/continuation is separately covered. |
| EBC and `RIP_NO_MORE` | Printable/LF/VT/ESC completion and repeated `#` are covered; no dedicated NO_MORE state exists. Modern S2 is fixed. |
| `P`/`p` and colour-zero outline | Both use the polygon renderer; colour-zero fill remains active while the outline is suppressed. Existing native/historical raster cases cover this. Malformed counts are rejected instead of reproducing reference reads of unavailable coordinates. |
| Stream boundaries | Network write boundaries do not flush commands. Explicit local EOF flush is an extension; its use on live wire state is P3. |

### Corrections needed when using the research notes as an oracle

The installed `/usr/home/admin/art/rips/RIPTERM.EXE` has SHA-256
`9a228c976a760107d0aed1e5599adae4ceccaf012e47b625de8a570612083ebd`.
The checks above used its bytes directly; no DOSBox or historical Windows
binary execution is claimed.

The notes' mid-ESC table says SOH/STX are rejected. The actual instructions at
`1A04:01BE–020D` jump to the last-chance SOH/STX acceptance path after the ANSI
accumulator check fails; that path only rejects a preceding backslash. Both
SyncTERM escape submachines intercepted those bytes before that choice at the
audited baseline.
The interleaving follow-up resolves this branch in modern RIPv1 with
independent expected results rather than following the contradictory prose.

The extracted 62 descriptors also clarify transcription errors: `Q` contains
sixteen `2#` fields, `1M` ends in `++`, and `1B` has seventeen fields totaling
36 digits (not eighteen fields/38 digits). The early jump-table examples in the
notes are inconsistent with their later table: colour is index 4, while both
polygon entries 18 and 25 target `0F4F`. The `b2#` entry targets the common exit,
so its descriptor alone does not establish a background-colour operation;
SyncTERM's extended text-window `b` is a later protocol extension.

## Coverage boundaries

This is a regression inventory of the historical corner cases, not a claim that
unknown RIPterm behavior has been exhausted. Existing parser, engine, raster,
Unicode, variables, asset, icon and transfer tests remain enabled. They cover
additional numeric/reserved-field validation, button/clipboard state, local scene
playback, file-query replies, and transfer lifecycle/error paths. The
`cterm_rip_transfer` fixture also rejects access to the reserved
`syncterm-cache-name.txt` ownership metadata, including case aliases. External-corpus
comparison remains optional and is not required for these fixtures.

The tests do not validate physical audio, native mouse cursors, filesystem-wide
artwork collections, or every interactive template/popup permutation. The existing
SyncTERM parser's one-way disable behavior is covered by `cterm_rip_parser`;
this suite does not claim that `ESC [ 2 !` can re-enable it after disable.

RIP's existing91-word Q1.15 table and sine/unsigned-radius product now reside
once in st_bgi_trig, also used by BIG. Every table word matches the retained
DDO reference; signed product results have defined WORD interpretation.
RIP retains its wider cosine addition, metric and complete curve rendering
policy. BIG's own WORD-angle/metric/aspect adapters do not replace them.
The new big_curve_scalar test independently checks the actual common kernels;
RIP-specific wire/raster/capture runs remain deferred to the BIG goal's final
full regression suite. No RIP capture baseline or expected pixels changed.
