# Terminal conformance coverage

IGS 2.21 work is tracked separately in [IGS_CONFORMANCE.md](IGS_CONFORMANCE.md)
and [IGS_IMPLEMENTATION.adoc](IGS_IMPLEMENTATION.adoc). The shared graphics
byte-parser foundation has native split-input and raw-ownership coverage;
native image-format tests check indexed storage and boundary conversions.
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
original integer-division bug; drawing and loop execution remain pending.
`M` has native mode-selection/reset/input-boundary tests and coherent bitmap
checks for an unchanged scene, palette and window, across ST geometries,
Atari ST/CP437/UTF-8 and status choices. Primitive-specific use of the writing
mode remains part of the pending drawing tests. `C` covers independent
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
also check a following D. X dispatch remains in progress: out-of-range
selectors are no-ops; pending valid selectors fail without consuming payload.
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
Other drawing, loops and sound remain pending; see the IGS ledger for details.

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

MIDI's `music_parse`, `music_decode`, `music_calibrate` and `music_player` tests
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
Optional final-output captures support independent loudness comparisons;
unweighted RMS is not used to claim equal perceived loudness.
Manually selected synth checks compare onset and sustained levels across
eight pitches and verify dry ANSI/file-effect restoration where supported.
They render into discarded buffers or the pull-only mixer without devices.
The `extensions` control group checks feature 102 with MIDI disabled, rejected
cache-path recovery and seven-octave ANSI capture recovery in CP437/UTF-8.
These tests do not claim native MIDI-device or auditory coverage. MIDI and
PCM masks are both disabled in shared headless application fixtures.
