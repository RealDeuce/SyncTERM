# RIPscrip 3.0 implementation goal

The target is the reconstructed RIPscrip 3.0 language as shipped by RIPtel
Visual Telnet 3.1, using [reference revision
2fb17724b6122a5ad5cd1df38b69b8cce3a7079f](https://github.com/bbs-land/remote-imaging-protocol/tree/2fb17724b6122a5ad5cd1df38b69b8cce3a7079f/version/3.0).
That documentation is the contract, including explicit rendering corrections
and referenced inherited formats. Exact RIPtel raster reproduction is not
required. RIPlib extensions and future proposals are outside the goal.
External application and URL launching are excluded: recognize the syntax,
do not execute it, and report unsupported capabilities truthfully.

**Status: implementation in progress; no full-compliance claim.** The existing
RIP 3 implementation remains active until the separate engine passes acceptance.
Source indexing is discovery, not adjudication or working-feature evidence.

## RIP 1.54 preservation boundary

The baseline is commit `e49ee49a0ee97b397dd190eb4e3ab3fe3bbffb7a`.
[`rip154-freeze.json`](../src/tests/rip3/rip154-freeze.json) freezes legacy RIP
sources, shared BGI kernels/data, historical assets/expectations, and existing
`rip_*` helper definitions in the terminal adapter. Do not regenerate it or
advance its baseline during this goal. Surrounding public dispatch/lifecycle
changes still require unchanged runtime expectations to pass. Known 1.54
differences are retained, not fixed here.

The frozen capture inventory includes its descriptive scope text. Keep new
coverage notes in `docs/RIP154_CONFORMANCE.md` without modifying that inventory
or advancing the preservation baseline.

The 26 existing native/application RIP and graphics-scrollback CTests passed
at the baseline on native FreeBSD with the existing Release CMake configuration.
They cover CP437/UTF-8, historical captures, both pixel phases, replies,
interaction and reset/variable/asset/transfer paths. This does not establish
new RIP3 coverage, physical audio or Windows execution. The same 26 unchanged
runtime expectations passed again after the initial v3 font/image/device-pixel
foundations, using the rebuilt native application and terminal-test binary.
The preserved source/data hashes and helper definitions still match the original
baseline.

All 26 unchanged native/application RIP and graphics-scrollback expectations
also passed after the parser-owned host-reference recovery boundary, using the
rebuilt native application/terminal tests with interactive display variables
cleared. The frozen 93 files and 41 helper definitions still match the baseline.

## Contract and upstream issues

[`requirements.json`](../src/tests/rip3/requirements.json) preserves source
hashes and source blocks with exact line references and review states. Its
blocks include prose, flag/limit table rows and examples; contextual/inherited
material must be adjudicated explicitly. Chapter/corpus replay is not a
substitute for individual requirements review. Use `pending` for reviewed
requirements awaiting implementation, `blocked` with issue links for unresolved
requirements, and `implemented` only with implementation paths and independent
CTest evidence. Context/exclusion entries require reasons.

Open clarification issues:

- [#2](https://github.com/bbs-land/remote-imaging-protocol/issues/2): existing
  command-width, radix, marker, article, delay and fill findings.
- [#3](https://github.com/bbs-land/remote-imaging-protocol/issues/3): extended
  outline-font field meanings.
- [#4](https://github.com/bbs-land/remote-imaging-protocol/issues/4): column,
  article and overflow semantics.
- [#5](https://github.com/bbs-land/remote-imaging-protocol/issues/5): header
  segments and unrecovered wire entries.
- [#6](https://github.com/bbs-land/remote-imaging-protocol/issues/6): macro
  ordering, uncertain predefined variables, transparent-mode prompt exceptions,
  prefix `=`/`%` scope and repeated directives.
- [#7](https://github.com/bbs-land/remote-imaging-protocol/issues/7): framing,
  escaping and identification; ignored inline text versus command-corrupting
  extra data, clarified further in comment 6919726495.
- [#8](https://github.com/bbs-land/remote-imaging-protocol/issues/8): inherited
  BMM mask raster and compositing convention.
- [#9](https://github.com/bbs-land/remote-imaging-protocol/issues/9): conflicting
  RGB component example, incorrect C palette-index expression and malformed
  PALENTRY range-example delimiters.
- [#10](https://github.com/bbs-land/remote-imaging-protocol/issues/10): offscreen
  port extents, clipped verbatim copies and viewport diagram arithmetic.
- [#11](https://github.com/bbs-land/remote-imaging-protocol/issues/11): PIXEL/POINT
  coordinate policy, line-thickness units and polyline vertex/side wording.
- [#12](https://github.com/bbs-land/remote-imaging-protocol/issues/12): conflicting
  preset fill-mask bytes/radix examples and unspecified pattern anchoring.
- [#13](https://github.com/bbs-land/remote-imaging-protocol/issues/13): two-digit
  polygon counts versus the 4,096-vertex limit and multi-contour coordinate widths.
- [#14](https://github.com/bbs-land/remote-imaging-protocol/issues/14): arc/pie/chord
  angle ordering and empty, wrapped, full and multi-turn sweep semantics.

- [#15](https://github.com/bbs-land/remote-imaging-protocol/issues/15): malformed
  Bézier equation, subdivision/expanded-vertex limits, final-block validation
  and a census poly-Bézier payload inconsistent with the published layout.

- [#16](https://github.com/bbs-land/remote-imaging-protocol/issues/16): table reset
  in-use status, intra-table copy protection/error behavior and conflicting
  drawing-port current/copy descriptions; undefined INUSE(BACKUP), the FALSE
  example and aggregate ISPROT replies when entries are undefined.

- [#17](https://github.com/bbs-land/remote-imaging-protocol/issues/17): six emitted
  BOLD.CHR opcode-01 scan records contradict the inherited unused-opcode claim;
  their painting semantics need clarification for v3.

- [#18](https://github.com/bbs-land/remote-imaging-protocol/issues/18): YEAR's
  syntax typo, inconsistent DATETIME weekday/date example and exact single-digit
  day padding/trailing-newline policy.

- [#19](https://github.com/bbs-land/remote-imaging-protocol/issues/19): which
  palette supplies legacy-icon conversion RGB values and whether 2.x source
  deletion/timestamp behavior belongs to RIPtel 3.1's conversion contract.

- [#20](https://github.com/bbs-land/remote-imaging-protocol/issues/20): FILEDEL
  directory scope and host-created restriction, plus its ignore-path guidance
  conflicting with the inherited generic reject-path rule. Connection-only
  deletion is explicitly a hypothesis, not an established shipping contract.

- [#21](https://github.com/bbs-land/remote-imaging-protocol/issues/21): image-style
  deletion-flag contradiction, file deletion scope, IMGSTYLE flag/port defaults,
  protected-port reset and background-clear/margin write-mode policy.

- [#22](https://github.com/bbs-land/remote-imaging-protocol/issues/22): FastFont
  contour fill rule, hollow stroke width, overlapping-glyph write-mode policy
  and high-byte font character-code mapping. Type 1 ancestry alone does not
  establish RIPtel's defaults or the mapping from CP437/UTF-8 input to RFF slots.

- [#23](https://github.com/bbs-land/remote-imaging-protocol/issues/23): undefined
  COLORMODE replies conflict (`NONE` versus `-1`), and WORLDH/WORLDW syntax and
  prose use inconsistent names/possible aliases. Explicit shipping v3 world
  and RGB limits take precedence over the quoted older draft parameter tables.

- [#24](https://github.com/bbs-land/remote-imaging-protocol/issues/24): graphics
  style versus port ownership of cursor/image state, style versus environment
  ownership of color/mouse state, unspecified style bootstrap defaults and
  TEXT_XY cursor effects outside horizontal left justification; ownership,
  switching, protection and backup behavior of the border-enabled flag.

- [#25](https://github.com/bbs-land/remote-imaging-protocol/issues/25): PORTW/PORTH
  two-field DOMAIN summaries conflict with their explicit TYPE/DOMAIN prose and
  examples; PORTY1's summary also names different arguments from its syntax.

- [#26](https://github.com/bbs-land/remote-imaging-protocol/issues/26): complete
  screen SAVE/RESTORE capture set and numbered aliases, viewport/style/button
  context, active-palette slot ownership, status visibility and inherited
  RIPTERM*.SAV file side effects.

- [#27](https://github.com/bbs-land/remote-imaging-protocol/issues/27): button-style
  bootstrap defaults, mixed fixed dimensions, incompatible flag handling,
  protected auto-clip style mutation, shipping transparency-bit semantics and
  device-coordinate policy for effects/label indents.

- [#28](https://github.com/bbs-land/remote-imaging-protocol/issues/28): tone-variable
  ordering/default conflicts, T's required versus optional parameters, BLIP's
  duration example and MUSIC's literal recipe versus swept-segment evidence.

- [#33](https://github.com/bbs-land/remote-imaging-protocol/issues/33): filled
  rectangle prose mentions line patterns, while the specific border rule says
  only polygon-related borders use them and other filled-object borders are solid;
  rounded U/u pattern rules and continuity across straight/circular joins.

- [#38](https://github.com/bbs-land/remote-imaging-protocol/issues/38): rounded
  rectangle radius coordinate space/aspect conversion, oversized-radius handling,
  reversed corners and degenerate-bound behavior.

Continue unaffected work while issues remain open. File distinct additional
gaps as discovered, checking existing issues first. Issue reports do not
silently override the contract: adopt clarification through a reviewed
documentation revision and reconcile affected ledger entries/tests. Hypothesized
names alone need not be behavioral gaps. Do not guess, silently exclude
unresolved requirements or claim completion while blockers/reviews remain.

An earlier 2026-11-16 upstream status check found `main` at the pinned revision
and five open stacked clarification proposals. These are historical observations,
not a complete live inventory. Their observed heads are recorded on the
corresponding ledger blockers; none is adopted or treated as a resolution:

| Blocker | Proposal | Observed head |
| --- | --- | --- |
| #3 extended fonts | [PR #29](https://github.com/bbs-land/remote-imaging-protocol/pull/29) | `ca92e1035fcd00fe9a72bbd72ee4b7c72596bb5a` |
| #4 columns | [PR #30](https://github.com/bbs-land/remote-imaging-protocol/pull/30) | `996b1a3335b2b2500da90a7d063e27539c128586` |
| #5 headers | [PR #31](https://github.com/bbs-land/remote-imaging-protocol/pull/31) | `9b1872a575ba6c70d1d423c15cc965affd9fdb1d` |
| #6 macros/variables | [PR #32](https://github.com/bbs-land/remote-imaging-protocol/pull/32) | `e450e1d9cd85594e321cd1427833a36269d24993` |
| #7 framing/identity | [PR #34](https://github.com/bbs-land/remote-imaging-protocol/pull/34) | `a6da898d495367b16e9c3a7b2a1d5f5e7752f13d` |

These are proposal/status observations, not a semantic review or a new contract
pin. Adoption still requires reviewing the changed requirements, derivations and
affected tests before reconciling the source index.

## Work sequence

1. Establish requirements and preservation checks. The pinned source index
   and guard are installed; semantic review remains open.
2. Add a separate C17 `st_rip3` component: independent framing/numeric parsing,
   scene control, reset and recovery. Do not call the old RIP parser.
3. Add world/state/color handling, ports, viewports, tables, protection and
   backups, then drawing, write modes, clipping, copies and scrolling.
4. Add ICN/BMP/JPEG assets, bundled original FastFont/MicroANSI data, graphics
   and extended text, regions, columns and pagination. Retain provenance and
   actual payload hashes, including Git LFS materialization.
5. Add variables, macros, interaction, delivery/queries, playback, tones/WAV
   and delays through existing terminal services, with explicit cleanup.
6. Route existing RIP 3 mode exclusively to the new engine after requirements
   and blockers are resolved and application acceptance passes. Add a v3-only
   profile canvas choice: 640x480 (default), 800x600 and 1024x768 at 4:3.
   Preserve mode IDs, persistence/profile-copy/Wren contracts and all 1.54
   behavior. The v3 `$COMPAT$` environment remains inside the v3 engine.

Protocol state belongs to the terminal session thread. Narrow adapters provide
text, replies, input, prompts, choices, transfers, polling/deadlines and audio;
reuse unchanged low-level services, not legacy protocol policy. Resource lookup
is connection-first, shared resources second, then ordered user search paths.
Host writes stay within client-owned roots; fonts remain global.

**Parser error boundary:** malformed/unsupported RIP input is handled inside
the RIP parser. It must discard/recover the affected command or reference and
continue processing, never return a syntax/command error through the terminal's
session-failure path. Status-returning decoders/executors are private helpers.
The caller-facing processing entry returns no error status; optional diagnostics
are nonfatal observations. This applies to future command/framing integration
as well as the current reference processor. The 1.54 freeze remains unchanged.

## Verified v3 foundations

`rip3_number` checks the independent MegaNum/UltraNum digit codec against the
reference's literal examples, all 64 digit values, base-sensitive letter case,
fixed-width encoding/padding, values above legacy WORD width, UINT64 boundaries,
invalid bytes/arguments and unchanged outputs on failure. The codec handles
explicit-radix digit spans, not command grammars: command-specific radix,
field shortening, signed interpretation and error recovery remain separate
requirements. The existing application RIP 3 path has not changed.

The eight original RFF families, RIPscrip.maf and ten original CHR fonts are
bundled unchanged with SHA-256/size pins. The build verifies them before generating C data in the
build tree. `rip3_font_assets` rejects both same-size modifications and
truncation before publishing generated output. Original notices appear in
the program's license/provenance list. The nineteen payloads include RIPtel's
later LITT.CHR (5,151 bytes) and LCOM.CHR (12,079 bytes), which differ from the
frozen 1.54 font bank. All classic fonts have separate v3 pins/data, and no 1.54
font bank is replaced. The machine-generated ATF catalog cache is unnecessary
for the new engine's asset/style registry. Classic text has independent font
selection, layout and device stroke rendering; style-table/wire integration and
issue #17's five BOLD glyphs containing scan records remain pending.

`rip3_maf` checks explicit little-endian directory/subrecord decoding, all
30 documented cell sizes and first offsets, the literal 8x11 smiley bytes,
character-zero blank handling and character 255. A synthetic fixture verifies
non-default directory placement, every truncated prefix, invalid offsets,
dimensions and glyph sizes, invalid mode selection and unchanged results on
failure. These are file/bitmap-view checks, not terminal text rendering yet.

`rip3_rff` validates all eight RFF containers and 80 style records, including
case-insensitive names, uninitialized name padding, hollow flags and DEFAULT's
140% Ex variant. It walks all 1,792 character records with their own path/tail
lengths, independent of neighboring offsets and the unreliable SYMBOL trailer.
The seven documented damaged SYMBOL characters become blank while retaining
advance; characters 248/249 remain valid. Literal synthetic move/line/cubic
points, signed limits, aliases, hint framing, malformed containers/records and
unchanged results/cursors on errors are checked independently. Dixon's
documented advances and FA kerning are literal expectations. This decodes
outlines and metrics; glyph rasterization and the blocked extended-font wire
command are not implemented by these tests.

`rip3_bitmap` checks full Windows BMP headers and bottom-up, padded 1/4/8/24-bit
rows against independently authored pixel/palette expectations. Palette indexes
remain separate from RGB colors so later palette/transparency decisions retain
their original inputs. Tests include odd widths and unused bits/nibbles,
nonzero reserved palette bytes, short palettes, all truncated prefixes,
invalid dimensions/offsets/palette indexes and unsupported compression/header/
pixel formats, with unchanged output on failure. This is container decoding;
button hot-state selection, BMM compositing and icon conversion are not covered.
The same test also checks primary-plane BMP encoding against literal file-row
bytes at every depth, including short palettes, zero reserved/padding bytes,
the documented writer advisory fields and unchanged output pointers/sizes on
error. Secondary decode comparisons supplement those independent byte checks.

`pixel_jpeg_native` checks the baseline 8-bit libjpeg-compatible decoder with
independently constructed JPEG bitstreams (no encoder or decoder generates
the expected pixels). It checks grayscale row orientation, YCbCr 4:4:4/4:2:2/
4:2:0 color conversion, cropped MCU edges, every truncated prefix, unsupported
precision/progressive/arithmetic/CMYK/fractional sampling, invalid dimensions
and unchanged outputs on error. Decoder warning repair is rejected rather than
publishing an incomplete image. Heap-owned cleanup survives libjpeg error
callbacks. `pixel_jpeg_disabled` checks the absent-backend API in the same native
configuration; `pixel_jpeg_dependency` checks static/shared source-build policy.
The backend uses the existing dependency controls with a hash-pinned
libjpeg-turbo fallback and retained IJG notices. Runtime support validates the
header/library ABI. JPEG host commands, file lookup and canvas rendering remain
pending.

`rip3_surface` checks an independent indexed 256-color surface and palette.
All 256 default colors are checked against literal RGB expectations derived
from the pinned six-bit table, independently of the implementation's cube/ramp
construction. Tests cover the desktop master-color bit permutation, full and
partial RGB updates, closest-match/tie behavior, invalid/overlapping spans,
physical inclusive clipping, all five write modes (including literal truth
tables and 7 NOT -> 248), and unchanged state/results on errors. Stored indexes
make palette cycling affect existing pixels; an owned RGB snapshot remains
unchanged after subsequent drawing/palette changes. All three selected canvas
sizes are exercised. This is the v3 device-pixel layer; world/port transforms,
style/palette slots, host opcodes and application routing remain pending.

`rip3_surface_copy` checks explicitly fitted physical rectangle copies:
source/destination clipping before scaling, enlargement, reduction, independent
axis scaling, offscreen no-ops, extreme coordinates, all transfer operations,
and original-source semantics for overlapping copies in both directions and
with scaling/XOR. Mode 4 inverts the source for a copy, separately from level-0
NOT's destination inversion. Expected bytes are authored independently.
Port-number lookup, world transforms, zero-coordinate special forms and
protected-port state still require the port-table layer.

`rip3_geometry` checks device/world dimensions separately, the compatibility
640x350 world and 1280x960 scene world at all three device tiers, exact-ratio
forward/inverse mapping, full-frame boundary versus last pixel, negative
coordinates, larger/smaller logical frames, the 32,767 axis limit and unchanged
outputs on invalid dimensions or arithmetic overflow. Pixel mapping rounds down
to the containing pixel and leaves clipping to ports/viewports. Inverse mapping
accounts for unavoidable loss of logical-coordinate precision. This implements
frame arithmetic; host state and port/view origins remain pending.

`rip3_ports` checks the 36-entry device-pixel port table: shared overlapping
screen regions, independently owned offscreen buffers, port-zero invariants,
port/viewport origin translation, clipping/deactivation/reactivation, protected
configuration with writable contents, protected redefinition's switch-to-zero,
normal/hard reset, delete-all retention, undefined-port switch recovery,
protected copies and original-source copying between overlapping screen ports.
An independent coherent RGB capture verifies the related visible pixels and
margins. All three device tiers are exercised. A native Valgrind run of this
test reported no errors or definite/indirect leaks. This layer intentionally
accepts device pixels: world/offscreen sizing and wire-level zero-rectangle
forms await #10. Per-port text/image/query state, backup storage and host
command orchestration are not implemented by this layer.

`rip3_draw` checks the device-pixel pen and independent geometric line renderer:
all built-in masks, custom masks and zero-mask rejection, MSB-first repeating
patterns, preserved clipping phase, transparent/background gaps, odd/even
thickness, stable sides under endpoint reversal, diagonal and point-degenerate
strokes, all write modes, protected/inactive ports and extreme coordinates.
Each covered destination pixel is written once, so thick XOR strokes cannot
cancel themselves through overlapping brush samples. A coherent expected RGB
capture checks related line/gap/margin pixels. Work is bounded by the visible
stroke rectangle even for extreme endpoints and widths. The selected round-cap
raster is a geometric implementation choice, not a RIPtel pixel reproduction.
Host coordinate/thickness policy remains blocked by #11; multi-segment paths,
style slots, wire commands and scene reset orchestration remain pending.

`rip3_fill` checks custom eight-byte masks (top-to-bottom, MSB left), atomic
rejection of out-of-range bytes, explicit cyclic origins including negative
extremes, background-only/solid fills and pattern continuity across adjacent
clipped bars. Rectangle tests distinguish BAR's inclusive far edge from the
half-open resolution-independent interior, normalize opposing corners, cover
degenerate cases and confirm solid COPY borders after XOR/NOT interiors.
Invalid border state is rejected before changing the fill. Protected/inactive
ports, viewport origins and extreme coordinates are covered, with an independent
coherent RGB capture of the visible region/margins. Host tiling policy and the
conflicting preset table remain blocked by #12; style slots and wire commands
remain pending.

`rip3_path` checks open/closed stroked paths and unfilled rectangles. Shared
vertices, thick joins and self-crossings apply the raster operation once per
primitive; foreground coverage takes precedence over overlapping background
samples. Pattern phase continues across segments. Independent expected grids
cover open versus closed paths, crossings, normalized/degenerate rectangles,
clipping, extreme endpoints, repeated points, invalid input and both open/closed
4,096-vertex paths. The existing line and fill expectations still pass after
sharing the segment rasterizer. Native Valgrind reported no errors or
definite/indirect leaks in the path test. Polygon fill, world/wire decoding and
style-table orchestration remain pending.

`rip3_polygon` checks filled single/compound contours with even-odd parity:
nested holes/islands over existing colored pixels, winding reversal, duplicate
and subdivided contours, self-overlapping loops and shared triangle edges.
Independent expected grids cover half-open interior coverage, patterned fills
and patterned COPY borders after XOR interiors. Canonical reduced edge equations
make equivalent collinear edges evaluate consistently. Edge/intersection and
complete border scratch storage is allocated before drawing. Tests also cover
protected/inactive viewports, extreme signed coordinates/determinants, invalid
input with unchanged pixels, 4,096 vertices and 4,096 coincident contours in the
device kernel, plus a coherent RGB region/margin capture. Native Valgrind
reported no errors or definite/indirect leaks. Existing line/path/fill tests
still pass. Wire encoding/limits remain blocked by #13; these kernel limits
do not establish the number of vertices reachable through the documented wire
fields. Curved filled shapes and host orchestration remain pending.

`rip3_ellipse` checks direct, bounded geometric interiors without flood fill:
circles, rotated ellipses, general affine bases, positive/wrapped pie sweeps,
complementary chord segments with shared boundary ownership, empty/whole sweeps,
custom masks/backgrounds, XOR/NOT and clipped extreme radii. Literal circle/
rotation grids and an independently derived integer conic grid provide expected
pixels. Four adjacent XOR pie sectors and complementary XOR chords cover their
disks once. Constructor/error tests preserve metadata/pixels; singular/zero-axis
and inactive cases are no-ops. A coherent RGB capture checks a protected viewport
and its margins. The basis supports unequal world scaling after rotation, and
cardinal rotations use exact coefficients. This is an interior kernel with
explicit start/sweep; circle-aspect mapping and the unresolved wire angle policy
in #14 remain pending.

`rip3_ellipse_outline` checks solid curve strokes, open arcs, pie radii and
chords, including wrapped/full/empty sweeps, rotated/general affine bases,
thickness, a singular ellipse that doubles back along a diagonal, zero-radius
pen dots and positive sub-ULP sweeps. Literal circle/sector grids and independent
annulus/conic cross-sections supply expectations. One union coverage mask applies
XOR once at joins/crossings. Filled-curve tests cover all five interior write
modes with patterned fills and enabled/disabled solid COPY borders. Visible
parameter intervals are clipped before adaptive subdivision (maximum quarter
pixel chord sagitta); signed-extreme radii are checked in a tiny protected
viewport with a coherent RGB region/margin capture. Invalid state leaves pixels
unchanged. Native Valgrind reports no errors or definite/indirect leaks in this
test. Line/path/polygon expectations still pass after sharing subpixel stroke
coverage. These are device kernels; scene/wire orchestration remains pending.

`rip3_bezier` checks explicit uniform subdivision using independent rational
Bernstein samples, including negative half ties, signed extremes, one segment,
and 4,096 expanded vertices. Literal open/closed grids distinguish subdivision
counts. Smooth/jointed cubic blocks and lines share a continuous pattern phase;
XOR applies once across joins. Filled paths use the existing even-odd kernel,
with independent triangle/square interiors and optional COPY borders. Invalid
blocks/counts/expanded sizes leave owned-result metadata unchanged; invalid draw
state leaves pixels unchanged. A protected viewport's output and margins are
checked in one coherent RGB capture. Native Valgrind reports no errors or
definite/indirect leaks. This device kernel explicitly chooses nearest-pixel
sampling and a 4,096-vertex expansion bound. Those choices are not assertions
about RIPtel's unresolved wire sampling/count/error rules in #15. No command
parser or final-block policy is inferred from the kernel.

`rip3_palettes` checks all 36 independent 256-entry palette slots, initial slot
zero, lazy default initialization, selection persistence, desktop/RGB edits,
closest/direct and mapped color resolution, protected-write denial, undefined
protection no-ops, and unchanged state/outputs on invalid spans/arguments. Owned
whole-table snapshots restore the current index, all definitions/colors and
protection bits, overriding live protection as specified for backups. Repeated
restores, independent snapshot lifetime and self-restoration are covered. A
coherent RGB snapshot confirms palette cycling recolors existing indexed pixels
without modifying their indexes, while earlier owned captures remain immutable.
Native Valgrind reports no errors or definite/indirect leaks after making
self-restoration an explicit no-op. The backup adapter is covered below;
scene/wire integration and reset/intra-table-copy semantics in #16 remain pending.

`rip3_backup` checks the documented per-object backup lifecycle: one independent
base save, ten positions shared by numbered slots and a stack, LIFO restoration,
persistent base/protected slots, consumed unprotected slots, protected overwrite
and clear denial, and occupied-slot replacement at capacity. Explicit clearing
preserves protected slots unless forced. Independent areas retain separate stack
pointers. Clone/restore failure injection verifies saves and destination state
remain unchanged; ownership counters and native Valgrind verify complete cleanup
with no errors or definite/indirect leaks. Literal BACKSTAT USE/PROT strings,
including the pinned USE example and insufficient-buffer atomicity, are checked.
An actual palette-table adapter saves/restores complete current/definition/color/
protection state across base/slot/stack operations. Other table adapters and
variable/scene integration remain pending. Soft scene reset must retain backups.

`rip3_backup_copy` checks all thirteen whole-table source/destination combinations
across live tables, base, numbered slots and the stack, plus multiple destinations.
It validates all destinations, self-copy, protection and final shared capacity
before changes; failed cloning/restoration preserves the live table and every
save. POP consumes once after all copies; unprotected numbered sources are
consumed only when restoring a live table. Full-capacity POP/restore exchanges,
repeated destinations and readable protected sources are covered. Immutable
saved objects share private ownership; restoring/mutating one live offscreen
port table leaves all remaining saved pixels independent. Last-owner destruction,
explicit-clear cleanup and failure paths pass native Valgrind without errors or
definite/indirect leaks. Parsed expression dispatch is covered below; terminal
stream integration, remaining adapters and intra-table policies in #16 are pending.

`rip3_bitmap_text` checks rendering from immutable MicroANSI views with explicit
CP437 spans (including blank zero and character 255), magnifications 1/2/10,
vertical rotation, transparency/opaque cell backgrounds, one-device-pixel
shadows and all five write modes. Foreground takes precedence over shadow, and
each visible pixel applies the operation once. Synthetic glyphs supply literal
expected geometry, including multibyte rows with nonzero padding. Negative and
signed-extreme origins, inactive ports, invalid fonts/scales/lengths, empty
metrics and a 65,536-character clipped span are covered. Inverse sampling bounds
work by the visible viewport. One coherent protected-viewport RGB capture uses
the documentation's literal 8x11 CP437 smiley from the original RIPscrip.maf.
Native Valgrind reports no errors or definite/indirect leaks. This is a device
bitmap-text kernel. Classic font selection/justification and vector/outline
rendering have separate tests below; cursor/window state, UTF-8 transport
conversion and wire orchestration remain pending.

`rip3_ports_backup` checks complete device port definitions/current selection,
viewport activation, protection and independent copies of offscreen index planes.
Restoration allocates every bitmap before replacing live entries, overrides live
protection and preserves the live screen pixels. Literal offscreen grids and a
coherent RGB screen capture distinguish these two ownership rules. Repeated
restores and restoring after destroying/recreating the original port owner
verify independent saved resources. Canvas mismatch leaves live state and the
unconsumed saved slot unchanged. Native Valgrind reports no errors or definite/
indirect leaks. Cursor/image/query/clipboard metadata, not yet implemented in the
device table, must extend these snapshots with the corresponding session state;
whole protocol-level port-table backup acceptance remains pending.

`rip3_chr` checks all ten independent original CHR assets and all 2,262 glyphs,
including full-file header/table/program validation, fixed builtin font IDs,
metric bytes, repeated offsets and zero advances. Synthetic files cover moved
prefix/stroke-data offsets, a full 256-character table, signed-seven-bit extrema,
empty programs, each truncated prefix, unsupported versions and malformed
bounds/unterminated programs, with unchanged outputs/cursors on failure.
The inherited LITT A example supplies independent literal MOVE/DRAW expectations
at RIPtel's later offset. All six BOLD scan records are exposed explicitly in
the five affected glyphs, rather than silently treated as no-ops. Raw scan_flag
metadata is retained. Native Valgrind reports no errors or definite/indirect
leaks. Classic layout/scale is tested separately below; wire orchestration and
scan behavior in #17 remain pending. The frozen 1.54 decoder is not called or changed.

`rip3_stroke_text` checks classic solid one-pixel glyph strokes through explicit
baseline/device matrices, nominal unit metrics, accumulation before scaling,
missing/zero-advance glyphs, repeated-stroke/cross-glyph XOR union, transparency,
rotation/shear, clipped shadows and all five write modes. Independent literal
LITT A grids include its retraced right stem; the original I center stem checks
fractional accumulated placement without per-character truncation. A synthetic
zero-advance glyph proves advances come from its width table, not its final
pen position. SCAN in a later glyph, invalid matrices/endpoints and invalid
metrics reject before changing pixels. Inactive and protected viewports and a
coherent RGB region/margin capture are covered. Native Valgrind reports no
errors or definite/indirect leaks. Callers still supply the matrix/origin;
classic-font selection/resolution/justification and outline rendering are tested
separately below. World/cursor/style-table and wire orchestration remain pending.
SCAN painting stays blocked by #17.

`rip3_variables` checks an owned, growing user-variable table with canonical
case-insensitive ASCII names of 1..20 bytes, first-letter validation, underscore/
digit suffixes, explicit value lengths and 255-byte truncation. CP437, UTF-8 and
embedded-NUL bytes round-trip unchanged; shorter replacements clear unused bytes.
Missing and empty values remain distinct. Independent tables, 4,096 records,
resizing/deletion, sorted independent snapshots and atomic import are covered.
Malformed/duplicate imports leave caller state unchanged and clean partial
allocations. Native Valgrind reports no errors or definite/indirect leaks.
Session/persistent roles are separate table instances chosen by the interpreter;
this backend does not invent lifetime promotion, prompts or expansion policy.
The optional `st_rip3_storage` adapter adds a versioned client file format with
explicit byte lengths and deterministic ordering. It atomically replaces a
client-selected file and validates a fresh table before publishing a load.
The four `rip3_variables_file_*` fixtures verify a literal file, reload in a
separate process, replacement/deletion persistence, UTF-8 paths, malformed and
duplicate records, and failed replacement with temporary-file cleanup. Native
Valgrind reports no errors or definite/indirect leaks. This establishes backend
disk persistence; directive/macro parsing, host approvals/deletion protection,
session lifetime selection and interpreter startup integration remain pending.

`rip3_variable_reference` checks the documented post-expansion user-reference
grammar: case-insensitive names/options, the six distinct directive bits in any
order, optional decimal position/width, omitted centered axes, format/conversion
ordering, and explicit empty versus absent question/default fields. The two HLP
examples and all legal/illegal format examples have independent expectations.
Question/default text truncates at 100 original bytes, with delimiters validated
even beyond that boundary. Invalid/truncated references preserve caller output;
native Valgrind reports no errors or definite/indirect leaks. The parser leaves
built-in dispatch, macro expansion, prompting and scope policy to the future
interpreter. Prefix `=` and repeated directives return unsupported pending #6;
the parser retains both `+` and `%` without choosing precedence. It imposes
explicit representation bounds on decimal fields, not inferred RIPtel limits.

`rip3_features` checks IFS value/list behavior against an explicit per-session
capability registry. Every capability starts false; the future client adapter
must register only available services. Features and nonempty categories list in
alphabetical order without spaces, trailing delimiters or line endings. Tests
cover category changes as JPEG availability toggles, the literal emulation-list
example, all registered keywords, independent sessions, unknown queries and
output-buffer atomicity. External applications always return unsupported and
never enter lists, even if the private mask is set incorrectly. Wire parsing,
runtime service registration and reply transport remain pending.

`rip3_table_host` exercises parsed host-variable expressions against real
palette and device-port tables: whole-table `$COPY$`, `$BACKSTAT$` USE/PROT and
palette `$SCP$`/`$RCP$` with base defaults, decimal slots and PUSH/POP. Tests
verify complete palette/current/protection restoration, offscreen pixel
restoration, multi-destination source consumption, and literal status strings.
The ordinary built-in parser preserves up to 40 parameters of 12 bytes,
case-folds names, trims parameter spaces and distinguishes explicit empty
fields. Maximum-size lists, every truncated prefix, invalid late destinations,
protected targets and insufficient reply capacity leave output/state unchanged.
Native Valgrind reports no errors or definite/indirect leaks. Bindings borrow
matching typed live objects/backup areas; unbound table classes and intra-table
entry copies return unsupported. Full port metadata, remaining table adapters,
other variables, stream framing, expansion and terminal routing remain pending.

`rip3_table_objects` extends parsed expression coverage to `$CUR$`, `$INUSE$`,
individual `$ISPROT$` and multi-selector `$PROT$`/`$UNPROT$`. It checks palette
default creation, defined/undefined/protected results, current-zero behavior,
all/any nonzero entries, base/stack/slot occupancy, all-slot protection and
protection no-ops for undefined objects. Every selector/binding and reply capacity
is validated before active changes. Failed late selectors, missing bindings and
short replies preserve state/output. `$CUR(PORT,n)$` creates a full active
screen port when undefined, as specifically documented for that variable,
across all three canvases; it does not reuse SWITCH_PORT's fallback policy.
Native Valgrind reports no errors or definite/indirect leaks. Other table entry
adapters, TV lifetime lookup, aggregate ISPROT and INUSE(BACKUP) remain pending;
the last two selectors explicitly return unsupported under #16. This does not
resolve the contradictory general port-table descriptions in that issue.

`rip3_palette_host` verifies `$PALENTRY$` current/explicit selection, omitted
stop, inclusive ranges, all 256 entries, undefined-palette `-1` replies and
protected readback. Replies truthfully describe the engine's stored eight-bit
components, a precision explicitly allowed by the reference. Literal white and
desktop-color replies and a maximum-length 256-white-entry response independently
check decimal formatting, delimiters and exact buffer capacity. Invalid fields,
short buffers and absent bindings preserve output, current selection and table
state. Native Valgrind reports no errors or definite/indirect leaks. The malformed
range-example call is reported under #9; the implementation follows the explicit
syntax block. Stream expansion/routing remains pending.

`rip3_host_variables` checks a shared built-in dispatcher with borrowed current
frame, explicit capability registry and typed table bindings. Parsed `$NULL$`,
`$IFS$`, `$RESX$`/`$RESY$` and the table-variable families run through this entry
point. Literal replies verify all three canvas resolutions independently of
world dimensions, capability changes, palette saves/restores/protection/readback
and short-buffer state preservation. Missing services and external launch names
return unsupported; no version/compliance or service availability is inferred.
Native Valgrind reports no errors or definite/indirect leaks. This remains a
partial built-in executor; user-variable fallback, macro/stream/session
orchestration and terminal acceptance remain pending.

`rip3_builtin_catalog` and `rip3_builtin_inventory` verify exact ASCII
case-insensitive recognition of 174 names from the pinned 9.1 inventory,
APP/SAVE/RESTORE fixed-slot families, and separately attested processors.
The offline contract test derives its inputs from the pinned source blocks,
then exercises the compiled recognizer for every name and mixed-case spelling.
Invalid spans, embedded NULs, removed/renamed 1.54 names and invented family
extensions reject. Recognition does not imply implemented semantics: ambiguous
P/PROTECT and the four unrecovered processors remain unsupported. The shared
private dispatcher distinguishes unknown names from recognized unimplemented
built-ins, allowing future parser-owned user lookup without misclassifying a
known processor as a prompt variable. Neither status crosses the public parser
boundary.

`rip3_host_recovery` exercises the public, void-returning reference processor.
Malformed references, every truncated prefix, excessive parameters, invalid
late selectors, unknown/unsupported names, missing saves, injected clone-memory
failure and restore-I/O failure are handled internally. A valid query after each
rejection still executes, preserving live/save state. Callbacks provide replies
and optional nonfatal diagnostic categories without returning connection errors;
the parser owns capacity for the full 3,073-byte palette reply. Decoder/executor
status functions are declared only in `st_rip3_host_internal.h` for parser code
and focused tests. This is reference-boundary recovery; complete stream framing
and terminal routing remain pending and must preserve the same error contract.
Native Valgrind reports no errors or definite/indirect leaks in this recovery
fixture, including injected clone/restore failure cleanup.

`rip3_datetime` verifies nineteen resolved date/time variables through the public
nonfatal processor using one borrowed, converted local-time snapshot and a
client-selected zone phrase. Independent literal replies cover every resolved
reference example, all English month/weekday names, decimal padding, midnight/
noon, century/leap-year and year boundaries, day-of-year 001/365/366, Sunday-first
and Monday-first week 00/01/52/53, and unknown-zone NONE. No global clock, locale
or TZ setting is changed. A 4,096-byte zone phrase is emitted intact through a
borrowed reply, without the numeric/palette scratch-buffer limit. Invalid client
clock ranges and nonexistent dates emit only a nonfatal operation diagnostic;
the next valid query still works. Native Valgrind reports no errors or definite/
indirect leaks. Clock collection/conversion and batch/session wiring remain
pending client-adapter work. DATETIME stays unsupported under #18; the YEAR
handler follows its unambiguous name/meaning while the malformed syntax block
remains unresolved.

`rip3_legacy_icon` checks independent v3 decoding of legacy ICN/HIC/MSK memory
data for conversion to BMP. It preserves all sixteen indexes, copies an
explicit caller palette, decodes top-down plane 3..0 rows with MSB-left bits,
ignores pad bits and all post-pixel tail bytes, widens WORD-minus-one dimensions
before addition, and checks the BGI payload/overhead bound before allocation.
Truncations, malformed inputs and invalid supplied palettes preserve outputs.
Literal all-index rows, the documented RADIO first row, identical mask planes,
maximum width/valid-buffer boundaries and a valid ICN beginning with BM are
covered. No mask-compositing convention is inferred.

The original 94-byte RADIO.ICN and 206-byte RADIO.BMP are pinned test-only
fixtures with manifest hashes, source revision and historical notices.
`rip3_legacy_icon_assets` verifies their materialized bytes offline. Supplying
the original BMP's palette explicitly, the new decoder plus BMP encoder produces
the entire original BMP byte for byte; expectations are not generated from the
emulator. Native Valgrind reports no errors or definite/indirect leaks. Automatic
palette selection, filename conversion, file deletion/timestamps, asset lookup
and parser/session orchestration remain pending, with policy gaps under #19.
The existing 1.54 implementation and historical expectations remain frozen.

`rip3_viewport_host` verifies parsed AVP/DVP/MVP/EGW controls through the public
nonfatal processor. Defaults, CUR/ALL, explicit decimal selections, duplicate
selectors and the 40-parameter boundary preserve the current port. Invalid late
selectors reject the complete operation; a subsequent valid command executes.
Undefined ports are skipped, protected settings remain unchanged, and maximize
skips inactive viewports while filling the underlying port's full bounds. A
one-pixel viewport stays active when maximized despite its all-zero bounds.
Erasure uses the explicitly resolved current background index in COPY mode,
permits protected content, and skips inactive viewports. One owned coherent RGB
capture checks protected erasure, inactive scene retention, offscreen isolation,
single-pixel drawing and unaffected margins against independent literal colors.
Native Valgrind reports no errors or definite/indirect leaks. Graphics-style
background binding, remaining port metadata and terminal/stream integration
remain pending.

`rip3_resources` verifies the read-only storage adapter's host, shared and
ordered user-search lookup, with the first usable hit winning. Missing/empty
tiers and directories with a resource's name do not prevent fallback. All
media extensions share the same namespace, while fonts resolve only through
the separate global font directory; neither namespace leaks into the other.
Bare filename validation rejects paths, drive/stream prefixes, wildcards and
control characters, with unchanged outputs on failure. No directory is created
by lookup. Returned streams begin at zero and own their resolved native paths.

POSIX and Windows enumeration are selected in CMake, with a complete POSIX
declaration/link probe. Existing UTF-8 filesystem/path services and Unicode
caseless comparison are reused. Unicode equivalence beyond historical ASCII
names is explicit modern client policy, not a reconstruction of RIPtel's
code-page mapping. Case-colliding files reject the affected directory instead
of choosing by enumeration order; all later tiers are still tried. If none
succeeds, the first storage error is reported to the internal caller, with
allocation failures stopping immediately. These are storage statuses, not
session/parser errors. Tests exercise collision handling where the fixture
volume supports distinct case spellings. Native Valgrind reports no errors or
leaks; Windows code is reserved for CI, not claimed as locally executed.

Per-profile directories, client-root resolution of relative search entries,
configuration UI, host directory switching, deliveries, queries, playback and
command-specific extension/default/path handling remain pending. The service
does not infer a write/delete policy from read fallback. FILEDEL remains
unsupported under #20, and the existing terminal route and RIP154 implementation
remain unchanged.

`rip3_image` adds independent decoded-image rendering and per-port image styles.
Newly defined ports have automatic full-viewport areas. Explicit all-zero areas
disable viewing; other ordered device coordinates clamp to the viewport, and
changing a viewport reclamps an existing explicit area at rendering time.
Automatic areas follow its size, including one-pixel viewports. Protected ports
reject configuration changes but permit image content. Whole-table backups
include image state and remain usable after destroying the original owner.
The low-level table reset initializes port zero; the full wire environment reset
policy for protected survivors is still blocked under #21.

The device renderer accepts tagged RGB or indexed pixels with an independent
source palette, maps them to the current drawing palette, and scales by nearest
source-pixel centers. Aspect fitting centers the image with black margins;
integer dimensions round down with a one-pixel minimum, and odd extra margins
go right/below. Those geometric raster choices are explicit client policy,
not exact RIPtel pixel reproduction. The primitive uses COPY for the initial
background clear and black margins, current write mode for image pixels, and
flag 4 to skip the initial clear. It validates all source samples/palettes and
prepares the complete fitted image before changing any destination pixel.

Tests cover all five write modes, local/active palette independence, black at
a nonzero palette index, shrinking viewports, extreme coordinates, disabled
and inactive no-ops, malformed final pixels and rejected palette commits.
One coherent RGB capture checks a related scene with literal foregrounds,
margins, port/viewport offsets and an isolated offscreen image on every agreed
canvas. A second integration scene resolves caseless JPEG filenames through
the storage adapter and renders the independently authored T.81 color/grayscale
fixtures plus a literal one-bit BMP. Native JPEG decode/render coverage is
present. Native Valgrind reports no errors or definite/indirect leaks.

Memory rendering never deletes files or changes palette definitions. RGB
images ignore flag 8; indexed palette commit is explicitly unsupported.
BMP macro auto-dithering, macro/wire processing, world coordinates, image
delivery/lifetime and capability registration remain pending. The primitive's
explicit background/margin policy is not a claim that #21's shipping behavior
is resolved. No terminal route or RIP154 implementation changes are included.

`rip3_outline_text` adds RFF measurement and device geometry for all 80 bundled
style records. Measurement accumulates raw advances and optional adjacent-pair
kerning before any style scaling, checking both positive and negative int32
boundaries. Missing codes have no advance/ink; damaged records retain their
declared advance and remain blank. A selected face's horizontal percentage
scales the cumulative position and glyph together, avoiding per-character
rounding drift. Coordinates remain y-down font units until an explicit affine
matrix maps them into the viewport. Input file views are reopened/validated;
font/text bytes and caller state remain unchanged.

Cubics flatten adaptively to 1/64 device pixel using segment-distance bounds;
contours close implicitly and hinting is omitted as allowed by the reference.
The geometry API explicitly selects nonzero or even-odd filling inside each
glyph, then unites the glyph masks before applying a write mode. Hollow faces
use an explicit device-pixel thickness and the existing vector stroke kernel.
Foreground and one-pixel down/right shadow masks are both prepared before any
painting; foreground wins on overlap. Separate shadow collection preserves
shadows cast into the viewport by clipped-away foreground. The unchanged CHR
behavior now shares that prepared-mask application helper.

Independent literal fixtures check nested/reversed/coincident contours, raw
and zero-advance aliases, negative kerning, all five write modes, cumulative
fractional scaling, rotated analytic cubics, shear, hollow widths and shadows.
One coherent RGB scene is checked on every agreed canvas. Invalid late geometry,
nonfinite matrices, malformed kerning and bad style selections preserve the
scene; protected content and inactive viewports are covered. Native integration
also renders raw glyph-code spans through all 80 bundled faces. This is asset
coverage, not a Unicode/code-page mapping claim. Native Valgrind reports no
errors or leaks for both the new outline test and the existing CHR text test.

The explicit fill rule, whole-text union and hollow width are geometry-kernel
policies, not inferred RIPtel defaults. #22 remains open for those defaults and
character-code mapping; #3 still blocks the extended-font wire/style fields.
Point sizing, vertical alignment metrics, justification, transport mapping,
cursor updates and terminal integration remain pending. No old parser, shared
legacy font data or RIP154 expectations changed.

`rip3_environments` adds 36 owned environment records, storing the five table
references, world size, numeric base, coordinate width, RGB precision, mouse
pointer and baud emulation setting. Initially only zero is defined/current;
zero cannot be protected. Explicit definition/selection, protected edits and
whole-table owned backups are checked on all three canvases. Restoring bypasses
live protection, survives the original owner's destruction, and rejects a
canvas mismatch without consuming a saved slot. The frame getter combines
per-record world dimensions with the client's fixed device canvas.

The public void-returning reference processor now handles BASEMATH, COORDSIZE,
WORLD/WORLDW/WORLDH, BAUDEMUL, COMPAT and defined COLORMODE queries/updates through
the ENV binding. It distinguishes missing-entry replies (`NONE` or `-1` as
documented), validates complete changes before commit, preserves protected
records, and continues after invalid/unsupported references. WORLD(DEFAULT)
uses 1280x960 for base 36 and 4096x3072 for base 64. The explicit shipping v3
restrictions are enforced: world dimensions 1..32767 and direct RGB precision
eight. Older 65535/1..8 tables do not override those statements. COMPAT changes
only its five documented record settings and never invokes the old parser.

Tests check literal replies, noncurrent updates, minimum/maximum world fields,
all coordinate widths' boundaries, large decimal baud values within the existing
12-byte parameter limit, exact output capacity, and failed late-field recovery.
ENV entry queries/protection and generic record COPY/backup status use the
existing parser-owned table dispatcher. Native Valgrind reports no errors or
leaks. Baud settings are stored without performing playback waits; color
encoding settings do not imply physical true-color capability.

Undefined COLORMODE queries and WORLDX/WORLDY aliases remain unsupported under
#23. CUR(ENV,n), SWITCH_ENVIRONMENT, SENV and RENV runtime operations still need
complete capture/activation of referenced live tables/UI state. The explicit
record snapshots do not perform that transaction, alter pixels, or reset other
tables. Scene reset in-use policy remains under #16. Raw framing, pacing and
terminal routing remain pending; RIP154 behavior and the active route are intact.

`rip3_classic_text` adds typed classic-font selection, style validation, layout
and rendering through the existing independent coverage kernels. Font zero
selects the original native 80x43 MAF bank for each of the three canvases; fonts
1..10 select the original CHR assets. All ten vector magnifications use the
published rational factors, then scale the 640x350 reference into device pixels.
Widths accumulate before scaling. Horizontal/vertical alignment uses the
requested anchor, and vertical text rotates physical glyph geometry
counterclockwise. Shadow stays one device pixel down/right in both orientations.

The bitmap baseline projects the published font-zero reference baseline into
the native cell height. Origin rounding happens once, to nearest with positive
half ties. These are explicit geometric raster policies, not pixel-exact RIPtel
claims. Tests check all flag combinations, the three native bitmap cell sizes,
all font/magnification/canvas bindings, literal vector scale expectations,
nonzero CHR baselines, fractional advances and preserved plans on rejection.
Independently authored bitmap and vector glyphs supply two coherent RGB scene
expectations for alignment and orientation. Unsupported SCAN rejects before
painting. The native CMake build and all 49 RIP3/preservation checks pass;
native Valgrind reports no errors or leaks in the classic-text fixture.
No world frame, current cursor or graphics-style table is modified;
those bindings and wire processing remain pending, including #24's ownership
questions. The public parser error boundary and frozen RIP154 path are unchanged.

`rip3_port_query` adds PORTW/PORTH and PORTX0/Y0/X1/Y1 to the public nonfatal
reference processor. CUR/numbered selection, PORT/VIEW and WORLD/DEVICE domains
use the explicit three-field grammar and documented defaults. Device rectangles
are stored inclusively by the geometry kernel; queries return exclusive lower
right boundaries and pixel counts. Screen-port bounds are screen-relative;
viewport bounds are port-relative, and offscreen ports start at zero. Undefined
or inactive ports return zero sizes and -1 coordinates, including PORT queries.
Protection does not prevent reading or change the selected port.

WORLD queries read the bound current environment's live frame, ahead of an
optional caller frame snapshot. With no ENV binding the explicit frame is used;
no world frame is invented. Conversion uses the existing integer-ratio floor
policy, and a canvas mismatch rejects without changing state or pixels. Tests
check literal replies on all three canvases, protected/inactive/undefined and
one-pixel ports, nested viewport offsets, offscreen origins, exclusive endpoints,
live WORLD changes, exact reply capacity, missing services, and recovery after
malformed CP437/UTF-8 keyword bytes. A coherent RGB capture per canvas verifies
scene retention after the query/recovery sequence. #25's unresolved two-field
DOMAIN shorthand remains unsupported; OFFSCREEN's separate ambiguity remains
under #6. The native CMake build and all 50 RIP3/preservation checks pass;
native Valgrind reports no errors or leaks in the port-query fixture.
This does not add stream framing or change the active terminal route.

`rip3_screen` adds owned snapshots of the consistent 7.2 screen data: the full
screen index plane, explicit active-display palette and logical status-bar
visibility. These borrowed live bindings are captured by value. Saved data
survives the original screen owner, and restoring first clones a matching-canvas
index plane before committing all three fields. Each onscreen port immediately
sees the replaced shared screen. Port definitions, viewports, current selection,
protection and offscreen pixels remain owned by their separate table snapshot.

Tests use literal RGB scene expectations on all three canvases, including
inactive/protected ports and later redefined port boundaries. They verify both
status visibility values, unused palette entries, raw index-plane independence
in both directions, surviving owner destruction, and repeated restoration.
Base, protected/consumed slots and stack behavior use the shared backup pool.
Missing bindings, invalid palette data and mismatched canvas restores preserve
the live state and existing saves. Generic COPY through the public nonfatal
reference processor exercises these explicit core bindings, including recovery
after a failed restore without consuming its saved slot.

SAVE/RESTORE, all twenty numbered aliases and SAVEALL/RESTOREALL remain
unsupported without changing backup state. Their complete capture/activation
contract needs #26's clarification, alongside the unresolved style ownership
in #24. The explicit palette is not a guessed palette-table slot, and the
status value does not call a native UI. Application display/UI binding,
scene context orchestration and any required file-side-effect policy remain
pending. No frozen RIP154 data or active terminal routing changed.

The native CMake build and all 51 RIP3/preservation checks pass. All 26 existing
native/application RIP and graphics-scrollback CTests also pass with interactive
display variables cleared, including the current cache-directory fixtures.
Native Valgrind reports no errors or leaks in the screen fixture. The frozen
inventory retains its original bytes; new cache coverage notes remain in
`docs/RIP154_CONFORMANCE.md`.

`rip3_terminfo` covers RIPscrip's TERMINFO identity variable: default/NAME,
VENDOR, VERSION, LIST and unknown-keyword NONE replies. The client supplies
the three mandatory identity strings; an absent/incomplete binding is
unsupported. LIST is exactly `LIST,NAME,VENDOR,VERSION`, uppercase and sorted,
with no trailing comma or newline. Case-insensitive keywords use ASCII rules.
These are client identity values, not protocol-version/capability claims.

The public parser emits borrowed values whole, including empty values,
literal CP437/UTF-8 bytes and a 5,000-byte name, without applying the numeric
reply scratch-buffer limit or expanding macro-looking content. Private bounded
copying preserves output on insufficient capacity. Invalid syntax/extra fields
produce nonfatal diagnostics; valid references afterward still work, including
with an absent sink. Application identity/profile binding and raw stream/macro
integration remain pending. No system terminfo or frozen RIP154 code is used.
The native CMake build and all 52 RIP3/preservation checks pass; native Valgrind
reports no errors or allocations in the identity-query fixture.

`rip3_audio` adds an optional WAV playback adapter using the existing resource
resolver, audio-file decoder and client-owned audio engine. Its exclusive stream
is pulled by the existing mixer consumer; no playback thread or physical device
is created by the adapter. The client supplies stream volume and retains the
engine until after player destruction. One session thread owns playback control.
A mutex protects owned PCM and its cursor against the consumer. Source
activation never holds that mutex, and stream destruction quiesces callbacks
before PCM/mutex destruction.

RIFF/PCM validation checks declared container/chunk bounds, word padding,
format tag, sample rate, block alignment and complete frames before decoding.
The shared decoder handles 8/16/24/32-bit samples and arbitrary supported rates,
duplicates mono and uses the first two channels of multichannel files, yielding
the client's fixed 44,100-Hz stereo format. Each valid file replaces the previous
file in one channel; missing files are no-ops, and failed validation/decoding
preserves prior decoded PCM. Literal `$OFF$` and the explicit stop service clear
playback. Files remain unchanged in the common MEDIA namespace.

Independent PCM fixtures check replacement/tail retention, stop, mono and
multichannel handling, the 11,127-Hz rate and integral resampling, unknown odd
chunks, data padding, truncated headers/payloads and malformed formats. Literal
post-mixer expectations include the existing Q15 gain and limiter; they are not
generated from the v3 adapter. A pull-only engine exercises concurrent play/stop
and player destruction while a consumer remains active, followed by engine-stop
cleanup. The unavailable-decoder configuration has an explicit rejection path;
actual PCM checks require the decoder to be available. A second fixture links a
decoder-unavailable stub in the same native build and verifies that path without
changing the client's decoder configuration.

These are storage/playback service statuses for the RIP parser to consume,
never a session-processing return. Complete `1w` body processing is tested below;
raw framing/host-prefix parsing, reset/header dispatch, audio settings, IFS
readiness and active session integration remain
pending. Tone variables remain separate, including #28's unresolved recipes.
No frozen RIP154 source or existing sound implementation is changed. Headless
tests establish PCM behavior and ownership, not audible/native-device coverage.

The native CMake build and all 58 selected RIP3/preservation and shared audio
checks pass. Native Valgrind reports no memory errors or definite/indirect losses
in the PCM/concurrent-lifetime fixture, with only its three inherited file
descriptors open at exit. The native installed decoder executed the PCM cases;
the separate stub fixture executed unavailable-decoder rejection.

`rip3_commands` adds a caller-facing void command-processing entry for complete
framed command spans. It consumes syntax, unsupported-command and service
failures internally, optionally reports nonfatal diagnostics, and remains ready
for the next command. A compile-time function-type assertion prevents turning
this entry into a status-returning session API. The optional command adapter
depends on the independent core/audio components; it does not call the legacy
parser or change terminal routing.

J always reads MegaNums and defaults invalid numeric base settings to 36.
n uses the current base, accepts zero reserved-field shortening/omission, and
defaults invalid numeric coordinate sizes to two. f uses the current base and
coordinate width, validates both axes before applying them, and enforces
1..32767. The documented MegaNum dash-as-zero rule is local to wire fields;
the reusable strict codec and all legacy parsing remain unchanged. Protected
current environments reject edits. Other record values and client canvas
dimensions are retained.

1w validates its zero reserved block, preserves spaces in the filename and
reaches the WAV playback service. Missing files remain no-ops; corrupt files
produce nonfatal operation diagnostics without replacing current decoded PCM.
Literal `$OFF$` works without resource paths. Independent tests drive complete
command bodies through resource lookup to literal post-mixer PCM, with both
the installed decoder and an unavailable-decoder stub.

Tests cover the documented prologue/frame literals, all coordinate widths and
canvases, lowercase/special UltraNum digits, fallbacks, shared `$WORLD$`/BASEMATH
queries, protected state, excess/short fields, embedded NUL and SIZE_MAX input
rejection, unsupported reserved values, absent services/sinks and continuation
after errors. Only the explicit full-width f plus space-separated inline comment
case is supported; other comment-boundary forms remain under #7. Raw byte-stream
framing, escaping/expansion, packet-split orchestration, reset and remaining
commands are still required before active session acceptance.
The native CMake build and all 60 selected RIP3/preservation and shared-audio
checks pass. Native Valgrind reports no errors, lost allocations or leaked file
descriptors in the command fixture. The 93 frozen files and 41 legacy helper
definitions still match the original preservation baseline.

The complete-command adapter also implements Q/a desktop palette edits. Both
read the current ENV radix, including the local MegaNum dash rule, with only the
last numeric field eligible for shortening. Q validates all sixteen master
colours before committing; a validates its desktop index and master colour.
The bound palette table's current entry owns the edit and its protection;
ENV protection does not protect another table's contents. No implicit ENV
reference activation is inferred. Higher palette entries and other slots remain
unchanged, and syntax/service failures remain internal nonfatal diagnostics.

`rip3_palette_commands` uses literal commands, RGB values and coherent full-canvas
captures on all three canvases to verify recolouring of existing indexed pixels,
including background index zero. It covers documented Q/a examples, MegaNum and
UltraNum endpoints, shortened final fields, atomic rejection of late invalid
values, missing services, protection, shared CUR/PALENTRY replies and recovery.
Native CMake build and all 60 selected RIP3/preservation/shared-audio tests pass;
Valgrind reports no errors, leaked allocations or file descriptors in the new
fixture. The frozen RIP154 preservation check remains unchanged.

D/d decoding remains blocked: canonical metadata follows current Base Math,
while driver/corpus evidence forces UltraNum for the entire command. Component
precision below eight also needs an explicit shipping-driver rule, distinguished
from direct drawing mode. These questions are recorded in
[issue #2's palette clarification](https://github.com/bbs-land/remote-imaging-protocol/issues/2#issuecomment-6920509860).
Raw framing, reset orchestration and active v3 session routing remain pending.

`rip3_stream` adds incremental byte framing independent of the legacy parser.
It recognizes line-start ! and anywhere SOH/STX, uses an optional text-window
cursor callback after raw output, preserves bogus/escaped introducers as text,
and buffers complete opaque command spans across arbitrary packet splits.
Unquoted pipes chain commands; CR dispatches, mid-command LF is swallowed, and
an odd final backslash continues a command across physical lines. Backslash
quoting otherwise stays intact for subsequent argument/expansion processing.
The framer does not adjudicate the unresolved level-depth or ESC/query priority
rules under #7. Comments use their documented any-pipe boundary and bypass
variable expansion in the command adapter. Terminal grouping begin/end are
stateless no-ops, including nested or unmatched groups.

The public byte-processing entry returns void. Invalid API spans preserve state;
allocation failure reports one optional nonfatal diagnostic, discards the
corrupt command and tracks quoting/continuation until its next valid boundary.
A test-local allocator shim exercises initial/growth failures, recovery and
reset without adding production allocation hooks. There is no fixed line limit;
a split million-byte command exercises geometric buffer growth. Reset and
destruction discard partial commands without callbacks or accidental execution.
[File EOF delimiter policy is unresolved under #7](https://github.com/bbs-land/remote-imaging-protocol/issues/7#issuecomment-6920771540).

Literal transcripts test every split and byte-at-a-time input, CR/LF behavior,
backslash parity, comments, raw ANSI/CP437/UTF-8 byte preservation and cursor-based
introducer recognition. A byte-stream-to-command integration sequence updates
ENV/palette state, rejects an invalid command and continues to a coherent RGB
capture on every agreed canvas. The full native CMake build and all 62 selected
RIP3/preservation/shared-audio tests pass. Native Valgrind finds no errors,
lost allocations or leaked file descriptors in either stream fixture. The
frozen 93 files and 41 legacy helper definitions remain unchanged.
This is framing and body processing;
raw text-window rendering, expansion, binary-mode handoff, EOF playback policy,
ANSI control/enablement, scene reset and active v3 routing remain pending.

`rip3_port_commands` connects complete v/2p and onscreen 2P bodies to the
independent port table. Fields use current ENV radix, coordinate width and world
frame; geometry rejects mismatched device canvases. Validation completes before
mutating state. New onscreen definitions with an intersection clamp to the screen, establish a full
viewport, and apply select/deactivate/protect flags. Protected redefinition
retains the port and uses the documented fallback to port zero. 2p validates
its destination before deletion, retains protected ports in delete-all, then
selects the requested survivor or falls back to zero. Protected single-delete
failure leaves selection unchanged. Neither command activates or rewrites ENV
references, and ENV protection does not lock PORT contents.

The all-zero deactivation sentinel is checked in world fields before rounding.
A nonzero world rectangle may legitimately round to the device pixel (0,0);
the device viewport service now represents that active pixel directly. Typed
fixture setup uses explicit deactivate operations, retaining every previous
pixel expectation. Current-port geometry remains relative to its origin.
Offscreen v tests use explicitly prepared device-sized ports and do not infer
DEFINE_PORT's unresolved offscreen sizing.
[Wholly external viewport/port rectangles](https://github.com/bbs-land/remote-imaging-protocol/issues/10#issuecomment-6921326134)
also have an unspecified edge-clamp/empty/reject outcome. The wire adapter
reports unsupported without mutation for that branch. Full 2P, 2s and zero-endpoint copy
integration remain under #10/#2 and the scene-context blockers.

Literal command tests cover all four coordinate widths, both radices (including
UltraNum lowercase/#/&, MegaNum dash), full/shortened/omitted reserved fields,
all onscreen flag combinations, clipping, redefinition, protected failures,
delete-all survivors, undefined targets, invalid late fields, reversed bounds,
huge-coordinate clipping without overflow, missing services and canvas mismatch. One coherent independent
RGB capture on each canvas verifies streamed command geometry, origin-pixel
activation and retained screen pixels after port deletion. Shared CUR/PORTW
queries observe the same table state. The native CMake build and all 63 selected
RIP3/preservation/shared-audio tests pass. Native Valgrind reports no errors,
lost allocations or leaked file descriptors in the port-command fixture.
The original 93 frozen files and 41 legacy helper definitions remain unchanged.
Active terminal routing is pending.

The command adapter now executes 2C port copies with explicit source/destination
rectangles and the all-zero full-viewport shorthand. It parses both ports,
eight coordinate fields, transfer mode and optional zero reserved field before
any copy. Current ENV supplies radix/coordinate width/world projection, with
matching device canvases required. Source and destination are numbered operands;
current port selection, ENV references, viewports and protection are retained. Protected ports permit copying their contents. Modes 0..4 include
inversion of the source image for mode 4, distinct from drawing-mode NOT.

World zero sentinels are resolved before projection, preserving explicitly
specified rectangles that round to a single origin pixel. The existing device
service clips both rectangles before fitting and copies from an owned snapshot
for overlapping surfaces, including different onscreen ports sharing pixels.
Undefined/inactive operands and wholly external copy rectangles are documented
successful no-ops. Very large fields clip without intermediate projection or
viewport-origin overflow. Nonzero destination origins with both lower-right
fields zero remain unsupported under #10's unresolved verbatim-size policy.

`rip3_copy_commands` uses independently specified indexed operands, literal
transfer results and coherent RGB captures on all three canvases. It covers all
five modes, viewport origins, protected content, source/destination clipping,
scaling, full-viewport shorthands, rounded origin pixels, same-port and aliased
screen overlap, both radices, all four coordinate widths, malformed late fields,
reserved omission/shortening and recovery after invalid commands in a streamed
sequence. A test-local build of the unchanged surface implementation injects
overlap allocation failure: no pixels change and the next command succeeds.

The preserved wipe command is exercised at world 1280x960 on all three canvases.
Its literal destination `28` means 80, while the canonical explanation says 72;
[that numeric discrepancy is recorded under #10](https://github.com/bbs-land/remote-imaging-protocol/issues/10#issuecomment-6921588539).
The conflicting prose remains blocked in the ledger. The native CMake build and
all 64 selected RIP3/preservation/shared-audio tests pass. Native Valgrind reports
no errors, lost allocations or leaked file descriptors in the copy fixture.
The original 93 frozen
files and 41 legacy helper definitions still match the preservation baseline.
This is complete-body processing and independent copy service integration;
active v3 routing, unresolved 2P sizing/context and the remaining protocol remain
pending.

`rip3_styles` adds session-owned storage for the resolved ordinary drawing
attributes: raw drawing/background/fill colours, explicit device pen and fill
settings, classic font attributes and write mode. Its 36 records start with only
entry zero defined/current, using a required caller-supplied initial value.
First-use record selection copies the current attributes; previously defined
entries retain their data. Validation precedes edits, protected entries reject
changes, and entry zero is unprotectable. This service does not choose the
unresolved bootstrap defaults or activate a complete scene context.

Owned snapshots retain every stored attribute, current index and definition/
protection bits. They survive source-table destruction and later live changes;
whole restore bypasses live protection and restores undefined entries as well.
These are snapshots of the implemented attribute subset. Extended-font state,
wire thickness/fill-phase policy, cursor/image/mouse/colour-mode ownership and
reset/default orchestration remain under #3/#11/#12/#16/#24. Complete context
activation, CUR(STYLE,n) and SGS/RGS remain unsupported. The SGS/RGS paragraphs'
conflicting references to palette data are also recorded in
[issue #24](https://github.com/bbs-land/remote-imaging-protocol/issues/24#issuecomment-6922213004).

The public nonfatal host processor now exposes STYLE current-index queries,
INUSE/individual ISPROT and PROT/UNPROT metadata for bound records, alongside
existing generic snapshot COPY. Full selector lists are validated before edits;
undefined protection is a no-op. Generic backup-slot protection remains shared.
Tests exercise all 36 varied records, retained caller values, first-use copies,
protected edits, bounds/invalid attributes, independent base/slot/stack snapshots,
source destruction, metadata queries and recovery after invalid references.
Coherent independently specified RGB captures show selected attributes driving
line/rectangle/fill kernels on all three canvases. Test-local allocation failure
covers construction, initial/overwrite snapshot failure, preservation of an
existing save and public COPY recovery without a session error.

The native CMake build and all 66 selected RIP3/preservation/shared-audio tests
pass. Native Valgrind reports no errors, lost allocations or leaked file
descriptors in both style fixtures. The original 93 frozen files and 41 legacy
helper definitions remain unchanged. This is record storage and explicit kernel
integration; wire drawing/style commands and active terminal routing are pending.

`rip3_drawing_commands` adds complete `c`, `k`, numeric `W`, `L` and `R`
processing. Colour edits affect only the bound current STYLE attribute and retain
the raw value. Palette mode follows the ENV radix; direct RGB uses UltraNums with
8 bits/channel. Drawing matches RGB through the bound current palette.
Destination NOT ignores both ink colours; transparent pattern-off pixels do
not require background lookup. Protected STYLE/PORT content remains drawable,
and inactive viewports skip colour lookup and allocation.

Line and rectangle coordinates follow the current world, coordinate width and
radix, including shortened last fields. Wide integral projection preserves far
endpoints and line slopes until clipping. Explicit pen patterns retain phase
through clipping and rectangle edges; owned coverage applies each write
operation once at joins. Rectangles accept reversed corners, remain unfilled
and become single lines when degenerate. Coverage allocation failure leaves
pixels untouched; subsequent commands still execute through the public void
parser boundary. This does not choose bootstrap defaults, decode wire pen
thickness or activate ENV references (#11/#24).

Independent coherent RGB captures cover all three canvases, both radices, all
coordinate widths, default 640x350 and scaled worlds, palette/direct RGB ink,
nearest-colour ties, all five drawing modes, protected records, viewport origins,
reversed/clipped patterns, XOR corners and very large coordinates.
Malformed/unsupported inputs, missing bindings, mismatched canvases and injected
allocation failures recover without a session error. The test allocation shim
compiles the unchanged drawing implementation without adding production hooks.

Two further documentation conflicts are recorded upstream: the resulting mode
after [`W` period termination](https://github.com/bbs-land/remote-imaging-protocol/issues/7#issuecomment-6922755531)
is unspecified, and the default white colour row
[uses `@` instead of the documented UltraNum digit `&`](https://github.com/bbs-land/remote-imaging-protocol/issues/9#issuecomment-6922756001).
Period forms remain unsupported; strict UltraNum decoding retains its established
alphabet. The native CMake build and all 67 selected RIP3/preservation/shared-audio
tests pass. Native Valgrind reports no errors or leaked allocations/descriptors
in the new fixture. The original 93 frozen files and 41 legacy helper definitions
still match the preservation baseline. Active terminal routing, unresolved
context/default policies and the remaining protocol remain pending.

`rip3_font_command` adds complete numeric `Y` decoding into the bound current
STYLE's classic font attributes. Font, direction, size and flags remain
two-digit current-radix fields regardless of coordinate width or RGB mode;
the last field may shorten. All syntax, ranges and mutually exclusive flags
validate before any record edit. Invalid commands preserve the whole style,
protected styles reject edits, and protected ENV records do not prevent
changes to another table. The command retains other attributes, selection and
ENV references. Extended-font state, bootstrap defaults, text/cursor wire
orchestration and active routing remain separate under #3/#24.

Tests select every classic font, direction and magnification in both radices
and exercise every legal/illegal flag combination. Split-input sequences
recover from conflicting justification flags and drive the existing text
kernel with the selected attributes. Coherent independent RGB captures on all
three canvases use explicit synthetic ASCII/CP437 glyphs to check horizontal/
vertical orientation, right justification, magnification and one-device-pixel
shadow; this is not transport conversion or active T/@ coverage. The native
CMake build and all 68 selected RIP3/preservation/shared-audio tests pass.
Native Valgrind reports no errors or leaked allocations/descriptors in the new
font-command fixture.
The original 93 frozen files and 41 legacy helper definitions remain unchanged.

`rip3_fill_commands` adds complete custom `s`, supported stock `S` and filled-bar
`B` command bodies. Eight custom row bytes follow the current radix; colour
fields share `c/k` palette-index/direct-RGB decoding and final-field shortening.
All fields validate before a protected current-STYLE edit. Custom and supported
stock masks replace one another, retaining the caller's explicit resolved device
phase and other stored attributes. Clear stock presets 00/01/03/06/08/09/0A are
implemented. Conflicting masks 02/04/05/07/0B remain unsupported under #12;
wire tiling origin and complete border/context orchestration remain pending.

`B` projects current-width world corners, normalizes reversals and fills inclusive
device bounds without a border. Wide axis-aligned bounds clip safely without
overflow. Set/clear mask bits use fill/background ink; all used ink lookups finish
before painting. Solid/background-only fills skip unused ink and destination NOT
ignores both colours. Inactive viewports skip lookup and painting; protected
STYLE/PORT content remains drawable. Parsing and operation failures are consumed
internally and later commands execute.

Tests cover all 256 custom byte values in both radices, field/range/protection
failures, split-input recovery, palette/direct RGB and nearest-colour ties, all
five drawing modes, shortened fields, all coordinate widths, very large endpoints
and mismatched canvases. Independent coherent RGB captures on all three canvases
verify literal stock-pattern diagrams, asymmetric custom masks across adjacent
bars, explicit nonzero phase, viewport origins, clipping, reversed/degenerate
bars and default/scaled worlds. These preserve explicit resolved phase rather
than claiming the still-unspecified protocol anchoring policy.

A further pattern-00 inconsistency is recorded in
[issue #12](https://github.com/bbs-land/remote-imaging-protocol/issues/12#issuecomment-6923829771):
the table says colour zero while paragraph 320 specifies the current pen
background colour. The implementation follows paragraph 320 and keeps the
contradictory row blocked. The native CMake build and all 69 selected RIP3/
preservation/shared-audio tests pass. Native Valgrind reports no errors or leaked
allocations/descriptors in the new fixture. All 93 frozen files and 41 legacy
helper definitions remain unchanged. Full RIP3 acceptance remains pending.

The fill-command fixture now covers complete `K` bodies with an explicit borrowed
border-enabled setting. World corners normalize before a fill that excludes the
projected right/bottom coordinate lines. With borders disabled, degenerate
rectangles do nothing; with borders enabled, they draw only the border. Enabled
borders retain wide projected endpoints, prepare owned coverage before any fill
and use the resolved device pen thickness. Border ink lookup remains necessary
under NOT because borders use COPY. Missing ink/bindings and coverage allocation
failures are consumed nonfatally, leaving pixels unchanged.

The wire path follows the specific `N` paragraph's solid COPY border rule. The
more general `K` paragraph also mentions line patterns; this conflict is tracked
in [issue #33](https://github.com/bbs-land/remote-imaging-protocol/issues/33), with
the conflicting requirement blocked. The owner of the border-enabled flag is
missing from the context tables and their earlier STYLE list. Its switching,
protection and save/restore behavior are requested under
[issue #24](https://github.com/bbs-land/remote-imaging-protocol/issues/24#issuecomment-6923963940).
The command host therefore borrows an explicitly resolved flag without choosing
an owner, bootstrap value or backup policy. `N` remains unsupported; this is not
complete border/context orchestration.

Independent coherent captures on all three canvases check every fill operation
under solid COPY borders, patterned/RGB interiors, border on/off, clipping,
reversed/degenerate rectangles, explicit thickness, protected records and
default/scaled worlds. Boundary cases cover both radices, all coordinate widths,
wide endpoints, missing bindings/ink and recovery from unsupported `N` in split
input. The existing test-local drawing allocation shim injects failed rectangle
and degenerate-border coverage allocation before painting; following operations
still succeed. The native CMake build and all 69 selected RIP3/preservation/
shared-audio tests pass. Native Valgrind reports no errors or leaked allocations/
descriptors in the expanded fixture. The 93 frozen files and 41 legacy helper
definitions still match the original preservation baseline.

`rip3_curve_commands` adds complete `C`, `G` and `o` bodies, plus the explicitly
documented start=0/end=360 full-ellipse forms of `O/V`. Numeric geometry follows
the current ENV radix/coordinate width, including shortened final radii;
angle fields remain two digits. Other raw O/V angle pairs are consumed as
unsupported under #14, without choosing equal/wrapped/multi-turn policies.

Circle radius projects horizontally and then supplies equal device radii on
the three square-pixel canvases. Ellipse radii project independently in X/Y;
default-world captures explicitly distinguish those shapes. The separate
ellipse kernel now retains finite centers/bases up to +/-1024*INT32_MAX, matching
the existing stroke geometry range. Five-digit projections stay wide through
clipping, including a test where center and radius differ by one world unit
after both exceed INT32_MAX; saturation would produce a false visible tangent.

Curves use solid resolved pen thickness. Filled G/o consume the explicit borrowed
border flag and retain the current fill pattern/phase. Interiors obey the current
write mode; enabled borders always COPY drawing ink, even under NOT. All used
ink lookups and border mesh/coverage allocations precede filling. Singular
interiors do nothing; enabled degenerate borders draw their line/point. Inactive
viewports skip lookup of the border setting and inks, and skip allocation.
Parser failures remain
local with no status returned to the terminal/session caller.

Independent coherent RGB captures on all three canvases cover all five modes,
protected records, nonzero viewport origins, direct-RGB nearest matches and
backgrounds, patterned fills, explicit phase, circle aspect and ellipse scaling.
Boundary tests cover both radices, all coordinate widths, full/short final
fields, missing bindings, canvas mismatch, clipped/singular/wide geometry and
recovery after unsupported angles. Test-local builds of the actual ellipse
and drawing implementations inject both mesh allocation stages and stroke-mask
allocation failure, preserving pixels and recovering on the next command.
Constructor tests retain unchanged outputs on invalid finite/nonfinite bounds.

The native CMake build and all 70 selected RIP3/preservation/shared-audio tests
pass. Native Valgrind reports no errors or leaked allocations/descriptors in the
new curve fixture. The original 93 frozen files and 41 legacy helper definitions
remain unchanged. General arcs, complete N/context ownership, active routing and
the rest of RIP3 acceptance remain pending.

The drawing-command fixture now covers argumentless `E` viewport erase. It
resolves current background ink from the bound STYLE/ENV/PALETTE services, then
replaces the active PORT viewport in COPY. Pen/fill/write mode and world scaling
do not alter the erase. Protected content can clear; table references, selection,
attributes, viewport geometry and protection metadata remain unchanged. Inactive
viewports skip ink lookup, while an explicitly active one-pixel origin viewport
still clears. No graphics scratch allocation is needed.

Independent coherent RGB captures on all three canvases verify palette/direct
RGB backgrounds, nearest-colour ties, nonzero origins and overlapping protected
screen views. Offscreen tests verify clipping and separate screen contents.
Malformed arguments, missing services, canvas mismatch and missing RGB palette
fail without painting; split-input processing recovers on the next command.
The actual native TTY/text-window overlap and complete cursor/context behavior
remain pending, so the mixed overlap requirement is not marked complete.

The native CMake build and all 70 selected RIP3/preservation/shared-audio tests
pass. Native Valgrind reports no errors or leaked allocations/descriptors in the
expanded drawing fixture. All 93 frozen files and 41 legacy helper definitions
remain unchanged. The palette-switch wire section has also been reviewed:
its two-digit selector conflicts with the shipping Switch-family report under
#2, so `2A` remains unsupported and its conflicting syntax is now blocked in the
ledger. No reference revision or legacy routing changed.

The complete-command fixture now covers `3e` decoding into the current ENV baud
rate. Its one-to-four-digit field follows the current radix, independently of
coordinate width and RGB mode, and permits last-field shortening. Zero encodes
full speed; arbitrary rates are accepted, including the preserved `01UN` example
(2399 in base 36), rather than a preset-only list. Validation precedes protected
record edits; other values, references, selection and noncurrent records are
retained. `$BAUDEMUL$` reads the same field.

Independent literal tests cover both radix maxima, lowercase/extended UltraNum
digits, local MegaNum zero spelling, missing services, malformed/excess fields,
unsupported comments, protected edits and recovery. One-byte split input checks
the exact baud-state sequence through invalid data and a radix change. All three
canvas-backed environment tables are exercised. This is record decoding/readback;
local playback pacing, reset persistence and owned nonblocking `3D`/`$D$` delay
backlogs/resumption remain pending. The inherited `3D` command remains in scope
despite the corpus's preference for the variable form.

The native CMake build and all 70 selected RIP3/preservation/shared-audio tests
pass. Native Valgrind reports no errors or leaked allocations/descriptors in the
expanded complete-command fixture, whose audio checks use pull-only PCM. The
original 93 frozen files and 41 legacy helper definitions remain unchanged.

`rip3_rounded_rectangle` adds the missing resolved device-geometry service for
rounded rectangles. An explicit nonnegative radius joins four truly circular
quarter arcs and four straight spans. Opposing device corners normalize; this
helper requires a radius no greater than half the smaller dimension and rejects
oversized input. It does not select a wire clamping/aspect policy. Radius zero
becomes an ordinary rectangle; degenerate bounds have no interior and can draw
their outline line/point.

Fills sample pixel centers with explicit pattern phase. Straight bounds are
left/top inclusive and right/bottom exclusive; circular boundary ties are
excluded, matching the existing conic fill policy. Solid outlines prepare all
eight owned coverage pieces before painting and apply each destination pixel
once across joins. Optional borders use COPY after the fill regardless of its
write mode. The existing ellipse coverage collector is shared through a private
header; no ellipse geometry algorithm changes. The test-local allocation shims
exercise every corner mesh and every corner/edge coverage allocation stage.

Independent literal outline/interior grids and coherent RGB captures cover all
five modes on all three canvases, borders on/off, nonzero protected viewports,
fill phase, thick/singular outlines, zero/maximal radius, fractional geometry,
wide clipped corners, inactive views and unchanged pixels after invalid input or
allocation failure. Constructor failures retain the supplied output unchanged.
The native CMake build and all 71 selected RIP3/preservation/shared-audio tests
pass. Native Valgrind reports no errors or leaked allocations/descriptors in the
new fixture. All 93 frozen files and 41 legacy helper definitions remain intact.

Wire `U/u` remains unsupported. Its radius units, oversized policy and reversed/
degenerate bounds are requested in [issue #38](https://github.com/bbs-land/remote-imaging-protocol/issues/38).
The line-pattern wording is also covered under
[issue #33](https://github.com/bbs-land/remote-imaging-protocol/issues/33#issuecomment-6925249713).
This is the required geometry foundation, not complete U/u or RIP3 acceptance.

## Verification and acceptance

Configure/build with native CMake and run CTest. V3 expectations come from
resolved documentation and independent geometry/state/reply examples, never
generated from the new emulator. Cover all canvases, CP437 and the existing
UTF-8 transport, split input, reset, cancellation, EOF, malformed assets,
allocation/error recovery, transfers, timers and audio lifecycle. Use the
control bitmap backend and one coherent capture of related cells, pixels,
cursor and state. Keep physical audio disabled and verify PCM independently.
Normal tests are offline; do not use Wine or the user's desktop. CI supplies
routine platform coverage.

```sh
python3 tools/audit/rip3_compliance.py
ctest --test-dir build --output-on-failure \
  -R '^(rip154_source_freeze|rip3_.*|cterm_rip_.*|term_rip_.*|syncterm_rip154_.*|syncterm_graphics_scrollback)$'
```

The final gate is intentionally separate from always-passing development
checks:

```sh
python3 tools/audit/rip3_compliance.py --require-complete
```

Acceptance requires no unreviewed, pending or blocked entries; all upstream
blockers resolved; implementation/test evidence for every in-scope requirement;
and passing preservation/runtime tests. Commit completed, verified changes.
Do not push code or publish PRs without further instruction. Upstream
clarification issues are authorized.
