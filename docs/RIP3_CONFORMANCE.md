# RIPscrip 3.0 implementation goal

The target is the reconstructed RIPscrip 3.0 language as shipped by RIPtel
Visual Telnet 3.1, using [reference revision
c9a66b27edc93d4522b21510c1379eb10230f85d](https://github.com/RealDeuce/remote-imaging-protocol/tree/c9a66b27edc93d4522b21510c1379eb10230f85d/version/3.0)
from `RealDeuce/remote-imaging-protocol:stable`.
That documentation is the contract, including explicit rendering corrections
and referenced inherited formats. Exact RIPtel raster reproduction is not
required. RIPlib extensions and future proposals are outside the goal.
External application and URL launching are excluded: recognize the syntax,
do not execute it, and report unsupported capabilities truthfully.

**Status: implementation in progress; no full-compliance claim.** The existing
RIP 3 implementation remains active until the separate engine passes acceptance.
Source indexing is discovery, not adjudication or working-feature evidence.

The user approved adopting the open documentation integration PR #71. Its
reviewed inactive-viewport #21 follow-up and PR #73's DEFINE_PORT clarification
are included in the current pin.
[The adoption review](RIP3_REFERENCE_ADOPTION.md) and
[`reference-review.json`](../src/tests/rip3/reference-review.json) record its
provenance, scope and reopened claims. There are now 7357 blocks in 65 normative
documents. Implementation milestones below describe work against the previous
pin unless explicitly reconciled with this revision. Their passing tests remain
historical behavior evidence; they do not prove the corrected contract. The
RIP1.54 baseline, original font bytes, active application route and external-launch
exclusion remain unchanged. No further content is to be added to existing
upstream issues.

The reference fork supplies documentation only. File new documentation issues
against `bbs-land/remote-imaging-protocol`; reference related issues and PRs in
each new issue. Once an issue has a PR, do not add further comments to it. The
existing instruction against modifying existing issues remains in effect.

Initial adoption verification used the existing native Release CMake configuration.
Configure/build and all 93 selected RIP3/preservation/JPEG/shared-audio/codepage
tests passed (4.62 seconds). The updated preservation/adoption guard also passed
its five focused tests, including rejection of a stale pin/document inventory
and retention of adoption provenance during reindexing. The completion gate
correctly rejects the unfinished queue. All 93 frozen files and 41 legacy helpers
are unchanged. Download/extraction directories, test fixtures and logs were
removed. No native GUI, physical audio or Windows execution is claimed.

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

A separately developed RIPv3 text-video-mode repair landed as `4c230452` and
is recorded in the manifest's `maintenance` entry, with previous checksums
retained. Its two
updated pins cover the RIPv3 branch of `rip_text_font` and a supplemental
capture-inventory entry. RIPv1's original grid/status behavior and all
historical images, expectations and exclusions are declared unchanged by that
maintenance record, which reports 26 passing RIP regressions. The isolated fill
milestone below verifies the original hashes separately; final acceptance must
also account for this independently landed maintenance boundary.

The follow-up maintenance repair removes the forced 640x350 video switch.
RIPv3 retains the selected raster/aspect and derives its console grid from
the current text cell dimensions, including default-font reset. Its expanded
resolution/status regressions cover the existing live font path; the separate
engine's MicroANSI bank and rendering integration remain pending.

After the follow-up font repair `8ea0fddb`, the 26 native/application RIP and
graphics-scrollback tests and all three preservation/contract checks pass
again (29 tests, 129.05 seconds). The guard report now says that recorded
checksums match and identifies the two maintained pins; it does not imply
that those pins still equal the original baseline. No validation rules or
preservation checksums changed in this reporting update.

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

The following list records historical questions raised against the previous
pin. PR #71 provides the adopted documentation answers for 34 of them, including
the reviewed newer #21 follow-up; local implementation/review remains pending.
The broad earlier #2 remains separately tracked in the ledger. Descriptions in this list
are the original questions, not a claim that the adopted PR lacks their answers.

Historical clarification issues:

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
  polygon counts versus the 4,096-vertex limit, undersized-count behavior and
  multi-contour coordinate widths.
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

- [#42](https://github.com/bbs-land/remote-imaging-protocol/issues/42): conflicting
  GET_IMAGE clipboard-slot searches, protected candidates, previous clipboard
  pointer and port/pixel state after failure; LOAD_BITMAP protected-target and
  fully clipped duplicate behavior; LOAD_ICON failure clearing and error-box
  ordering.

- [#46](https://github.com/bbs-land/remote-imaging-protocol/issues/46): D's
  milliseconds summary conflicts with its detailed 60ths-of-a-second rule.

- [#51](https://github.com/bbs-land/remote-imaging-protocol/issues/51): conflicting
  resident text-window/viewport query scan order and eligibility after earlier
  actions during the same click.
- [#53](https://github.com/bbs-land/remote-imaging-protocol/issues/53): HCL
  special-key literal brackets conflict with the no-bracket paragraph; undefined
  control-notation input and malformed sequences.
- [#54](https://github.com/bbs-land/remote-imaging-protocol/issues/54): opposing
  SWITCH_PORT protection flags and flag effects on an undefined destination.
- [#58](https://github.com/bbs-land/remote-imaging-protocol/issues/58): canonical
  LOAD_BITMAP example flags `0G` = 16, absent from the enumerated flag table.

Continue implementation from the adopted answers. Do not add content to existing
issues, per the user's latest instruction. File distinct additional gaps only
when they are not answered by the adopted contract or its pending update. Reports do not
silently override the contract: adopt clarification through a reviewed
documentation revision and reconcile affected ledger entries/tests. Hypothesized
names alone need not be behavioral gaps. Do not guess, silently exclude
unresolved requirements or claim completion while blockers/reviews remain.

An earlier 2026-11-16 upstream status check found `main` at the pinned revision
and five open stacked clarification proposals. These are historical observations,
not a complete live inventory. Their observed heads are recorded on the
corresponding ledger blockers. This was their status at that historical check;
their integrated answers are now adopted through PR #71:

| Blocker | Proposal | Observed head |
| --- | --- | --- |
| #3 extended fonts | [PR #29](https://github.com/bbs-land/remote-imaging-protocol/pull/29) | `ca92e1035fcd00fe9a72bbd72ee4b7c72596bb5a` |
| #4 columns | [PR #30](https://github.com/bbs-land/remote-imaging-protocol/pull/30) | `996b1a3335b2b2500da90a7d063e27539c128586` |
| #5 headers | [PR #31](https://github.com/bbs-land/remote-imaging-protocol/pull/31) | `9b1872a575ba6c70d1d423c15cc965affd9fdb1d` |
| #6 macros/variables | [PR #32](https://github.com/bbs-land/remote-imaging-protocol/pull/32) | `e450e1d9cd85594e321cd1427833a36269d24993` |
| #7 framing/identity | [PR #34](https://github.com/bbs-land/remote-imaging-protocol/pull/34) | `a6da898d495367b16e9c3a7b2a1d5f5e7752f13d` |

Those historical proposal/status observations were not a contract pin. The new
pin and adoption process are documented in RIP3_REFERENCE_ADOPTION.md; individual
requirements and affected expectations still require reconciliation.

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
header/library ABI. The image and literal RIP_IMAGE milestones below add canvas
rendering, file lookup and ordinary 1p processing; delivery and macros remain
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
does not infer a write/delete policy from read fallback. The explicit host-directory
writer is described below. FILEDEL remains
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
BMP macro auto-dithering, image-style wire/world processing and image
delivery/lifetime remain pending. Literal 1p processing is covered below. The primitive's
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

`rip3_blit` adds unscaled indexed transfers to the separate surface/port services.
The original source corner maps to an explicit destination corner; clipping
either side removes matching pixels from both, retaining their translation.
Clipped rectangles of equal size reuse the existing transfer-mode and owned
overlap-snapshot implementation. All five transfer modes operate on indexes,
including mode 4's source inversion. Source pixels outside the destination
remain unchanged. Extreme requested endpoints are clipped in wide arithmetic;
work and scratch allocation depend on visible pixels.

The port service uses explicit viewport-relative device coordinates and a full
source-viewport shorthand. Protected ports permit content transfer; undefined
or inactive operands are no-ops. Current selection, viewport/protection/image
metadata and unrelated pixels are retained. Offscreen buffers remain independent;
different screen ports share the same underlying snapshot on overlap. These
typed services do not select a clipping policy for unresolved wire branches,
bypass viewports, clear uncovered areas or define a clipboard pointer.

Independent literal pixel-position maps, logical operands and coherent RGB
captures cover both clipping directions, source clipping that shifts the
destination, all transfer modes, overlapping moves in both directions, identity
copies, aliased screen ports, offscreen transfers and protected content on all
three canvases. Tests include active one-pixel viewports, last device pixels,
inactive/undefined ports, invalid input, extreme origins and allocation failure
before mutation. Clipboard bodies remain pending: their two slot-selection
algorithms conflict under issue #42. The COPY_BLIT example also contradicts its
numeric payload, now reported under
[issue #10](https://github.com/bbs-land/remote-imaging-protocol/issues/10#issuecomment-6926090826).
The pin and source evidence remain unchanged; this service milestone does not
claim full COPY_BLIT/SCROLL/clipboard wire support or change the active route.
The native CMake build and all 72 selected RIP3/preservation/shared-audio tests
pass. Native Valgrind reports zero errors, no retained heap allocations and only
the three inherited file descriptors. All 93 frozen files and 41 legacy helper
definitions remain unchanged. Verification logs use automatically removed
temporary directories rather than accumulating in `/tmp`.

The complete-body processor now also recognizes `A`'s explicitly documented
full-circle (`start=0,end=360`) and equal-angle cases. Three current-width XY
fields surround two fixed two-digit current-radix angles; the final radius may
shorten. The full-circle radius uses horizontal world scaling on both device
axes, with solid resolved thickness independent of the stored pen pattern.
Equal angles, including equal values beyond 360, are a successful no-op after
all fields validate. This path needs no style, port, palette, ink or graphics
allocation. Other pairs remain unsupported under issue #14; O/V's equal-angle
policy is unchanged and full general-arc compliance is not claimed.

Expanded `rip3_curve_commands` tests use literal annulus maps and coherent RGB
captures on all three canvases for all five write modes, both numeric bases,
coordinate widths 2..5, short final radii, unequal X/Y world scales and protected
style/port records. RGB tests check nearest-palette ink and destination NOT
without a bound palette. Equal-angle tests cover zero, 180, 360 and maximal
two-digit angles, maximal radii, missing render services and untouched allocation
faults. Malformed final fields still fail internally. One-byte stream input
continues through malformed/unsupported arcs and equal-angle no-ops to a valid
full circle. Mesh/coverage allocation faults, inactive ports and wide unsaturated
centers/radii are also covered. No legacy parser, state or routing is changed.
The native CMake build and all 72 selected RIP3/preservation/shared-audio tests
pass. Native Valgrind reports zero errors, no retained heap allocations and only
the three inherited file descriptors in the expanded curve fixture. The frozen
93 files and 41 helper definitions still match the original RIP1.54 baseline;
all pinned source blocks and document identities are retained. Temporary logs
were removed after verification.

Ordinary `P` and `l` bodies now decode a two-digit direct vertex count followed
by current-width/current-radix XY pairs. The final Y field may shorten. Every
field and the declared payload length validate before any decoded-vertex or
stroke-mask allocation. World endpoints retain their full integral projection
until clipping; no coordinate saturation changes edge slopes or mask phase.
P closes the outline without filling it; l omits the closing edge. Both use
the bound resolved pen, continue phase across segments and apply each covered
pixel once, including shared joins, self-crossings and repeated vertices.

Drawing respects the current write mode, direct/indexed ink and protected
contents. Transparent masks and solid pens skip unused background lookup; NOT
needs neither ink. Inactive viewports skip colour lookup and allocation after
body validation. Current STYLE/PORT/PAL bindings and ENV references are retained.
All private statuses are consumed by the existing void parser boundary; a late
bad coordinate or allocation failure discards only that command.

`rip3_path_commands` adds independent literal path/mask maps and coherent RGB
captures on all three canvases for both bases, widths 2..5, all five modes,
opaque/transparent patterns, protected records and short final fields. Tests
replay the exact pinned P/l examples against independently decoded world
vertices and integer segment-distance expectations. Other cases cover thick
joins, self-crossings, repeated vertices, partial clipping with retained phase,
wide endpoints with an independent line-distance inequality, nearest RGB ink,
NOT without a palette, inactive views and unchanged pixels after malformed
fields or both allocation stages fail. One-byte streams continue from invalid
and unsupported commands into a valid path. The largest two-digit direct
counts (1295/4095) are exercised without writing repeated XOR pixels twice.

This branch supports P counts at least 3 and l counts at least 2. Smaller
counts remain nonfatal unsupported cases; their wire behavior is requested in
[issue #13](https://github.com/bbs-land/remote-imaging-protocol/issues/13#issuecomment-6926736265).
The stated 4096-vertex encoding, multi-contour layouts and l's N-versus-N-1
wording remain blocked under #13/#11. Filled `p` integration follows below.
No full path-boundary compliance or active application routing is claimed.
The native CMake build and all 73 selected RIP3/preservation/shared-audio tests
pass. Native Valgrind reports zero errors, no retained heap allocations and only
the three inherited file descriptors in the new path fixture. All 93 frozen
files and 41 legacy helper definitions still match the RIP1.54 baseline; the pin,
source blocks and document identities are unchanged. Temporary logs were removed.

Ordinary filled `p` bodies now share the P/l count/XY decoder and validation
boundary. They require an explicit active border binding, retain wide integral
world projections and fill using the current pattern/inks/write mode. Enabled
polygon borders retain their resolved pen pattern and use COPY after the fill.
All used inks resolve before vertex, edge, crossing and stroke-mask allocations;
allocation failure cannot leave a partial fill. Solid/background-only fills skip
unused operands. NOT skips fill inks but still resolves any enabled COPY-border
inks. Protected contents remain writable; no table references or attributes are
implicitly selected or edited. The parser consumes every failure nonfatally.

The private `st_rip3_polygon_fill` service handles one integral path with finite
coordinates up to +/-1024*INT32_MAX. Its local two-limb magnitude arithmetic
retains exact determinants and cancellation without compiler integer extensions
or assumptions about long-double precision. It computes clipped pixel crossing
boundaries directly from rational edges at pixel centers, sorts those integral
boundaries and fills paired half-open spans. This preserves even-odd parity for
coincident/reversed/subdivided edges and repeated loops. Normal visible quotients
use 64-bit division; a bounded integer search covers large offscreen viewport
limits. The existing 32-bit polygon kernels are unchanged.

`rip3_polygon_wide` checks independent all-canvas fill/border captures for all
five modes, pattern phase, thick borders and protected viewports. Literal exact
crossing expectations derived with Python Fraction cover 22 endpoint/scanline
cases plus reversal, including offscreen limits without enormous allocations.
Wide determinant cancellation, sample-center ties, reversed/subdivided paths,
double-loop parity, invalid/fractional input, inactive views and every allocation
stage are exercised. The typed kernel's one-/two-point degeneracies and 4096
vertices are checked separately from unresolved wire count semantics.

`rip3_filled_path_commands` verifies both bases, coordinate widths 2..5, short
final fields, all fill modes, patterned/thick COPY borders, protected contents,
used/unused inks and direct RGB matching on all canvases. The exact pinned p
example is compared with independently decoded literal vertices and an integer
pixel-center ray-crossing oracle. Tests cover 1295/4095 direct counts, wide
unclamped edges, parity cancellation, NOT without a palette, inactive/missing
bindings, malformed bodies and all preparation failures followed by recovery.
Full 4096 wire encoding, undersized-count policy and multi-contour grammar remain
blocked under #13. This is single-contour p integration; the active application
route and the frozen RIP1.54 implementation are unchanged.
The native CMake build and all 75 selected RIP3/preservation/shared-audio tests
pass. Native Valgrind reports zero errors, no retained heap allocations and only
the three inherited file descriptors in both new fixtures. All 93 frozen files
and 41 legacy helper definitions remain unchanged, as do the pin, 6548 source
blocks and 62 document identities. Temporary verification logs were removed.

`3D` now decodes one to four current-radix digits and starts an explicitly bound
input gate for ticks/60 seconds. The duration rounds upward to nanoseconds, with
zero acting immediately. ENV is read without edits, including protected records;
malformed/commented or unbound commands recover through the existing void
processor. The distinct macro D variable and baud-rate pacing remain separate.

`st_rip3_input` is a nonblocking, budgeted source pump owned by one session thread.
The client owns the receive/file queue and retains bytes until the pump takes
them. A delay stops all following RIP/raw-text delivery before another source
byte is taken. The owner can use the relative wait to bound its normal poll
timeout, then resume in order. The driver copies its borrowed callback table,
owns only gate state, allocates nothing during processing and neither sleeps nor
starts a thread. Reset cancels the gate without reading/resetting the borrowed
queue or parser; destruction invokes no callbacks. Unsigned elapsed arithmetic
handles the supplied monotonic nanosecond counter's wraparound.

`rip3_input` verifies exact independent durations through UINT32_MAX typed ticks,
last-nanosecond boundaries, clock wrap, budget/empty-source behavior, arrivals
while paused, absence of read-ahead, cancellation and borrowed-source lifetime.
`rip3_delay_commands` connects the driver to the real stream framer/void command
processor on all three ENV canvases. Deterministic clock/queue expectations check
consecutive delays and radix changes, queued arrivals, CP437 and UTF-8 raw bytes,
zero/short/maximal fields in both bases, the literal 002D example, protected ENV
and unchanged baud/reference values. Malformed/unsupported commands retain the
existing gate, and later valid input continues. These fixtures use no real-time
sleep, display or physical audio.

The real terminal/network and local-scene source adapters remain future routing
work. These component tests establish the queue/gate contract, not buffering in
the still-active application route. Full reset/context ownership and local-file
orchestration are not inferred here. The frozen RIP1.54 implementation and active
RIP3 route remain unchanged.
The native CMake build and all 77 selected RIP3/preservation/shared-audio tests
pass. Native Valgrind reports zero errors, no retained heap allocations and only
three inherited file descriptors in both new fixtures. All 93 frozen files and
41 legacy helper definitions still match the RIP1.54 baseline; the reference pin,
source blocks and document identities are unchanged. Temporary logs were removed.

The ordinary `$D(duration)$` processor requires one decimal argument in
1..65535, emits no text and starts the bound input gate. It follows the explicit
detailed 60ths rule and the agreeing inventory; the conflicting milliseconds
summary remains blocked under #46. Invalid arity, values, capacity or missing
bindings leave the existing gate unchanged.

Immediate level-1 ASCII-27 queries now use a session-owned evaluator. Mode zero,
the unused window selector and zero reserved digits decode in current ENV radix.
The evaluator preflights and copies the complete supported expression before
side effects. It accepts literal CP437/UTF-8 bytes, documented control notation
and concatenated ordinary known built-ins. It executes references in sequence,
retains its cursor across D delays and reads current host state when resumed.
Long borrowed TERMINFO replies pass through whole. Nested variable errors stay
inside the void host-variable boundary and do not prevent later actions.

The input driver has an optional bounded continuation callback before fetching
more bytes, including when its source is empty. It rechecks the gate afterward,
so a final D still pauses later input after the query has completed. A pending,
ready continuation must be pumped again even without incoming network bytes.
Resume allocates nothing and starts no thread or sleep. Cancellation releases
owned text; the session owner separately resets its input gate/queue/parser.
Borrowed services and sinks must outlive the evaluator. A busy submission keeps
the existing program rather than replacing it.

Both caret and backquote translate ASCII controls, lowercase control letters
fold as in the documented query example, and doubled prefixes emit literals.
Length-delimited replies retain NUL. The seven listed special-key sequences
emit ESC, `[`, and their key letter. The absolute no-literal-bracket paragraph
conflicts with those examples; upstream #53 tracks its interpretation and the
undefined control-notation cases. Only the explicitly listed complete key forms
are accepted. Other brackets, unknown controls, macro/user/prefix references,
pick lists, templates and transport unquoting reject before partial execution.
Deferred modes and the rest of HCL remain pending. External launch names stay
unsupported and have no action.

`rip3_query_commands` drives the real byte framer, command and variable parsers,
evaluator and input driver on all three canvases. Independent expected bytes,
times and state cover literal/compound replies, all 32 ASCII control values,
lowercase and doubled prefixes, both forms of each special key, multiple and
final delays, the last nanosecond before expiry, queued arrivals, CP437/UTF-8,
state changes before/after suspension and later commands, long identity replies,
nonfatal recovery, unsupported tails before any effect, caller-buffer reuse,
bounded yields, empty-source resume, allocation failures and pending cleanup.
No host connection, physical display/audio or real-time sleep is used.

The native CMake build and all 78 selected RIP3/preservation/shared-audio tests
pass. Native Valgrind reports zero errors, no retained heap allocations and only
three inherited file descriptors in the expanded query fixture. The detailed
D stop-everything-except-sound claim still requires interaction/application
integration; these component checks do not establish that behavior. Resident
queries remain pending under #51. The active application route and frozen
RIP1.54 implementation are unchanged. All 93 frozen files and 41 legacy helper
definitions still match the baseline; the pin, source blocks and document
identities are retained. Temporary verification logs and issue payloads are
removed automatically.

The level-1 `1P` clipboard paste command now reads an explicit resolved clipboard
selection. It decodes current-width/current-radix XY, mode 00..04 and a zero
reserved digit. Reserved omission permits the mode's documented final-field
shortening. It copies the selected port's entire current viewport without
scaling, placing its upper-left at the projected point relative to the current
viewport and clipping the destination. Mode 04 inverts source pixels. It retains
uncovered pixels, current port, all port/ENV configuration and the clipboard
selection, and permits protected content to be copied.

A cleared selection or same-port source is a successful no-op. A missing binding
is unsupported; an invalid typed port fails nonfatally. A dangling selection or
inactive clipboard viewport is unsupported without guessing its lifecycle or whether deactivation permits
pasting the stored rectangle. These cases are detailed in [issue #42 comment
6930630327](https://github.com/bbs-land/remote-imaging-protocol/issues/42#issuecomment-6930630327).
GET_IMAGE allocation/selection, reset, failure/protection policy and clipboard
backups remain under #42. The binding selects no automatic defaults and does not
claim a complete clipboard lifecycle or active application routing.

`rip3_paste_commands` uses independent expected index values and coherent whole
canvas snapshots on all three canvases. Split-input commands cover all five
transfer modes, source/destination viewport offsets, source-subarea changes,
destination-edge truncation, protected source/destination ports and recovery
from an invalid mode to a later paste. The canonical literal `1PV4OM000` maps
1120,886 in a 1280x960 world to 560,443 / 700,553 / 896,708 on the three canvases;
the copied two-by-two tile remains two-by-two. Additional checks cover both
bases, coordinate widths 2..5, optional/short fields, huge external projections,
cleared/same-port/inactive-destination no-ops, unsupported inactive sources,
missing/dangling bindings, onscreen aliasing and an atomic allocation failure followed by successful recovery. These are
separate-engine component captures, without physical audio or a user display.

The native CMake build and all 79 selected RIP3/preservation/shared-audio tests
pass. Native Valgrind reports zero errors, no retained heap and only three
inherited descriptors in the new paste fixture. All 93 frozen files and 41
legacy helper definitions, all 6548 source blocks, 62 document identities and
the reference pin are unchanged. Temporary verification logs were removed.

`2s` now handles the complete three-digit port-number/flags form used by the
corpus and shared by the conflicting layouts. Current ENV supplies the radix;
the bound PORT table supplies the old/current port. Consistent source and
destination protection flags apply before selecting the requested port, with
port zero immune to protection edits. Coherent flags also work when selecting
the current port. An undefined destination with zero flags falls back to port
zero and later drawing continues. It retains viewport geometry, image settings,
pixels and ENV references. This does not activate missing cursor/style/ENV
context state under #24.

The longer reserved-field forms remain under #2. Conflicting protect/unprotect
bits, opposing source/destination flags on the same port, and nonzero flags on
an undefined destination lack specified before/after behavior; new issue #54
requests that contract. These cases decline before any selection/protection
change. They are tracked as incomplete behavior, not newly invented language
rules. Public processing stays void and consumes all failures internally.

`rip3_switch_commands` checks all four initial protection states and nine
consistent distinct-port flag combinations, coherent same-port changes, port-zero
immunity, no-flag undefined fallback, corpus literals, both radices, case-folding
and port-number endpoints. Invalid/unsupported/missing-binding commands retain
selection and protection; protected ENV references remain unchanged. Split-input
scenes switch between an offset onscreen viewport, an offscreen port, a disabled
viewport and port zero, then execute real `1P` commands. One coherent capture per
canvas checks exact scene pixels alongside final selection/protection/viewport
state and offscreen content. A rejected same-port flag combination is followed
by a successful switch and paste. No physical audio, user display or real-time
sleep is used.

The native CMake build and all 80 selected RIP3/preservation/shared-audio tests
pass. Native Valgrind reports zero errors, no retained heap and three inherited
descriptors in the new switch fixture. All 93 frozen files and 41 legacy helper
definitions still match the RIP1.54 baseline. The source blocks, document
identities and reference pin are unchanged; temporary verification logs and the
upstream issue payload were removed.

`st_rip3_resource_write` adds the storage dependency for bitmap export. It takes
complete bytes and a bare UTF-8 filename, using the explicitly bound trusted
host directory. It creates that hierarchy if absent, preserves an existing
unique regular file's spelling through caseless matching, and uses the existing
atomic file-replacement service. Empty files are supported. Invalid names or
UTF-8 reject before creating directories. Directory/link/reparse/non-file and
ambiguous-name collisions reject; replacement failures retain prior contents.
There is no write fallback to shared, font or search tiers, even if the host
directory is missing or unusable. Existing read lookup retains its separate
fallback and file-type policy.

The caller must supply a client-owned session directory; this helper does not
choose a profile, accept a host-supplied directory, establish the client's root,
or implement directory-switching, transfer or deletion policy. These remain
pending. This storage helper does not capture pixels or implement a bitmap-export
command. The `1W` path described below now composes capture, encoding and writing;
Complete `2W` behavior and active application routing remain pending. Storage statuses remain for
the owning parser to consume internally.

`rip3_resource_write` checks a literal BMP byte sequence without using the
production encoder to create its expectation. It covers byte-preserving writes,
case-insensitive replacement, Unicode equivalence, empty files, missing-directory
creation, malformed names/encoding before creation, case-colliding entries where
the volume permits them, directory collisions, injected replacement failure and
later recovery, and missing/misconfigured host directories. Existing and dangling
symbolic-link cases run where the fixture can create them; the native FreeBSD
fixture exercises both. Shared/search/font bytes and an external sentinel remain
unchanged. The CMake fixture removes its complete directory after either success
or failure. Temporary Valgrind fixtures/logs also use automatic cleanup.

The native CMake build and all 81 selected RIP3/preservation/shared-audio tests
pass. Native Valgrind reports zero errors, no retained heap and three inherited
descriptors for the resource writer, including both link cases. All 93 frozen
files and 41 legacy helpers remain unchanged, as do all 6548 source blocks,
62 document identities and the reference pin. Verification logs and filesystem
fixtures were removed automatically. No Windows execution is claimed.

`1W` now validates its current-radix reserved zero and writes the explicitly
selected active clipboard viewport through the host-directory storage service.
It takes the bound current palette, captures an owned index raster and palette,
encodes an uncompressed bottom-up 8-bit BMP, then attempts atomic replacement.
The encoder/storage representation preserves all 256 palette indexes; this is
a valid documented BMP encoding, not a claim to reproduce RIPtel's exact file
bytes or default bit depth. Filename bytes and extensions remain literal,
including the canonical `filename.icn` example and a filename of `$OFF$`.
Spaces, Unicode names and caseless overwrite use the established resource
namespace. No graphics write mode is applied to captured pixels.

The typed capture accepts a named port and an optional viewport-relative device
rectangle, clipping without scaling. It owns its raster/palette independently
of later source edits/destruction and does not select a port or change any
configuration. Its device geometry does not infer `2W`'s world-coordinate or
sentinel rules. Cleared clipboard pointers make `1W` a no-op; dangling/missing
bindings and inactive source viewports remain unsupported under #42. Export
permits protected/current clipboard ports and onscreen port aliases, retaining
port/palette selection, protection, source contents and ENV references. GET_IMAGE
pointer selection/reset/deletion/backup policy and the active route remain
pending; no complete clipboard lifecycle is claimed.

`rip3_bitmap_write_commands` drives split-input framer/command/capture/encoder/
storage processing on all three canvases. Expected file headers, RGB quads,
bottom-up rows, padding and six index values are written independently of the
implementation's encoder/decoder. Checks cover offscreen and offset onscreen
viewports, protected source/palette, current-source export, both radices,
noncurrent ENV references, literal extensions/names, caseless overwrite and
reserved/filename errors. Capture-object, pixel-buffer, encoding and replacement
failures retain the previous file, and subsequent exports work. Cleared/dangling/
inactive/invalid selections and missing services remain nonfatal. Typed-capture
checks cover clipping, empty/reversed areas, invalid palettes, unchanged results
on failure, and owned pixels/palette after the original source is destroyed.
The CMake and temporary verification fixtures remove their filesystem artifacts.

`2W` now decodes a named source port, four current-width/current-radix world
coordinates, four reserved zero digits and a literal filename. It shares the
owned capture/encoding/writer transaction with `1W`, independently of clipboard
state. Raw all-zero coordinates select the full viewport. Zero x1/y1 select the
remainder from the projected upper-left; explicit inside rectangles keep their
inclusive ending pixels. Sentinel recognition happens before projection, so
positive endpoints rounded to zero remain an explicit one-pixel area. Wide
projection retains wholly external origins until they become documented no-ops,
without narrowing/wrapping them into a source pixel. Undefined ports are also
no-ops, without requiring palette/storage bindings. Source/current selection,
protection, contents and ENV references are retained.

The paragraph describing a partly external ending corner switches from
lower-right to lower-left terminology and does not distinguish resetting both
ending axes from clipping only the external one. [Issue #10 comment
6931396493](https://github.com/bbs-land/remote-imaging-protocol/issues/10#issuecomment-6931396493)
requests concrete image dimensions and inactive-source behavior. These branches
remain unsupported before any replacement; the generic capture kernel's clipping
rule does not silently decide the wire contract.

The existing bitmap-write fixture now verifies both commands. On each canvas,
independent BMP expectations cover whole/remainder/inside exports, single rows
and columns, the canonical `2W5000020200000FILENAME.BMP` example (a 73x73 region
in an explicitly equal world/device frame), widths 2..5, both bases, port 0/35,
noncurrent sources and offset viewports. It checks zero sentinels before rounding,
huge external coordinates, invalid/reversed/reserved/filename input, undefined
and outside-origin no-ops, unsupported inactive/partial-ending cases, and later
valid exports. Capture and replacement failures retain the previous BMP. The
expanded expectations inspect complete headers/palette/raster/padding without
using the implementation to create expected bytes.

The native CMake build and all 82 selected RIP3/preservation/shared-audio tests
pass. Native Valgrind reports zero errors, no retained heap and three inherited
descriptors in the combined bitmap-export fixture. All 93 frozen files and 41
legacy helpers still match RIP1.54's baseline; the source blocks, document
identities and reference pin are unchanged. Temporary logs and file fixtures
were removed. The active application route is unchanged.

The independent BMP file loader now combines media lookup with the existing
memory decoder. It appends `.BMP` only when an extension is absent and retains
explicit filenames/extensions. It validates the BMP file header's declared
length against actual file size before allocating that file buffer, reads only
the declared container, and preserves the caller's result on every failure.
All streams/buffers close/free before return. Contents and modification times
are retained; trailing bytes are ignored. Legacy conversion/deletion, deliveries,
file-error UI and numeric load-command dispatch remain separate work.

The new explicit bitmap-stamp kernel uses the requested device rectangle for
nearest pixel-center sampling, then clips visible coverage without rescaling
the surviving portion. Wide integer endpoints retain projection up to
+/-1024*INT32_MAX; rare products beyond uint64_t use exact bounded remainder
arithmetic. It maps indexed/RGB colors through the supplied active palette,
applies all five transfer modes, and compares indexed transparency against
original indexes versus RGB transparency against mapped destination indexes.
Source inversion happens after transparency comparison. It ignores image-style
clearing/fitting/disable flags and retains source, palettes, current port,
viewport/protection/image metadata and uncovered pixels. Validation and all
scratch allocation precede drawing. Inactive/empty coverage remains a no-op.
Palette adoption, dithering, duplicate ports and wire placement defaults are
not silently implemented by this resolved rendering API.

The pinned `1b0000HS0Y000G000000STRIP6.BMP` example contains flag `0G` = 16,
but its flag table defines only 1/2/4/8. Issue #58 requests that missing behavior
or a corrected example/layout. The reader/renderer do not interpret that flag,
and full `1I`/`1b` command/context/prompt orchestration remains pending.

`rip3_bitmap_stamp` checks independent full-canvas expectations on all three
canvases for five modes, scaling, positive/negative clipping origins, indexed
and RGB transparent holes, equal-RGB/different-index transparency, source
inversion, protected/disabled-image-style ports and unchanged margins/state.
Boundary tests cover allocation failure before effects, invalid source/destination
values, far-offscreen geometry, no input access for inactive viewports and literal
exact-rational sample results whose products exceed uint64_t.
`rip3_bitmap_file` checks authored BMP bytes, default and literal extensions,
Unicode/case/tier lookup, ignored trailers, truncated/bad/compressed files and unsupported DIB headers,
invalid sizes/names, allocation/decode failures and later recovery. Its loaded
RGB bitmap also stamps an offscreen destination and can be
freed afterward while rendered pixels remain valid. CMake and temporary fixtures
remove their complete filesystem trees after use.

The native CMake build and all 84 selected RIP3/preservation/shared-audio tests
pass. Native Valgrind reports zero errors, no retained heap and three inherited
descriptors in each new fixture. All 93 frozen files and 41 legacy helpers,
all 6548 source blocks, 62 document identities and the reference pin remain
unchanged. Temporary logs and issue payloads were removed. Neither active
application routing nor RIP1.54 behavior changed.

### LOAD_BITMAP command placement and nonfatal file failures

The separate command dispatcher now connects `1b` to BMP media loading and
stamping. It decodes the current ENV coordinate width/radix, projects WORLD
coordinates without narrowing far endpoints, and applies all five transfer
modes in the current viewport with the current bound palette. A raw zero
lower-right pair selects native file pixel dimensions, independently of WORLD
scale. Explicit inclusive rectangles stretch the source before viewport clipping.
Neither STYLE/image-style settings nor ENV table references select this path's
port/palette. Protected contents can draw, and an inactive viewport ignores the
load without consulting files, palettes or UI services.

Indexed and mapped-RGB transparency are connected. Same-port duplication is
ignored as documented, and a true-colour file ignores the palette-adoption bit.
Actual duplication/clipboard selection, indexed palette adoption, dithering and
unknown flags remain unsupported before painting. The flag-16 example stays
blocked under #58; no undocumented meaning is inferred.

Active loads require a borrowed void bitmap-error UI sink. Lookup/read/decode
failures request an error box using the current port and complete projected
rectangle, or a suitable UI-selected size for a verbatim load. Malformed names
and memory failures use nonfatal diagnostics without a file-error UI request.
All helper statuses are consumed inside the public void command boundary;
neither file nor parser errors end a session. Actual application box rendering
and routing are still pending: a recorded callback is not native UI coverage.

`rip3_bitmap_load_commands` uses independently authored indexed and 24-bit BMPs
with full coherent expected scene captures on all three canvases. It covers
five modes, scaled/native placement, right-edge clipping, explicit single-column
rectangles, transparency before source inversion, protected ports, disabled
image styles and retained table state. Both numeric radices and widths 2..5
are exercised with WORLD scaling, a protected nonzero ENV record, ignored unused
fields and projections beyond int32_t. Missing/unsupported files, malformed or
unsupported fields, missing services, loader/decode/staging allocation failures
and a valid split-input load after earlier syntax/file failures are checked.

The native CMake build and all 85 selected RIP3/preservation/shared-audio tests
pass. Valgrind reports zero errors, zero retained heap and only three inherited
descriptors for the new command fixture. All 93 frozen files and 41 legacy
helpers still match the original baseline. The 6548 source blocks, 62 document
identities and reference pin remain unchanged. Test directories and temporary
verification logs are removed after use. Active routing and RIP1.54 are unchanged.

### LOAD_BITMAP indexed palette adoption

`1b` flag 2 now adopts indexed BMP palettes in the separate engine's 256-entry
palette model. It prepares a replacement prefix of the current bound palette,
retains entries not supplied by the file, and renders with exact source indexes.
Equal-RGB indexes remain distinct for transfer operations and transparency.
Existing screen indexes retain their values and acquire the newly adopted RGB
colors. Other palette slots, current table selections, ENV references and port
configuration remain unchanged. Protected palettes reject adoption before any
painting; protected drawing-port contents can still change.

The renderer validates that an explicitly adopted palette matches the complete
source prefix. It stages all visible pixels before painting. Only successful
rendering commits the prevalidated palette span, with no callbacks, allocation
or ownership transfer between preflight and commit. Staging failures retain
both table and image contents. An indexed palette adopted successfully suppresses
flag-4 dithering, including when transparency or source inversion is requested.
RGB files still ignore adoption; dithering without adoption, duplicate-port
orchestration, file-error box rendering and active application routing remain
unfinished. The complete flag-2 requirement remains pending for lower-capacity
and no-hardware-palette device policies. No additional native display/color mode
is claimed by these tests.

The expanded `rip3_bitmap_load_commands` fixture uses independent 1-, 4- and
8-bit BMP headers, palette tables and packed rows on all three canvases. Cases
include duplicate RGB entries, partial palette tables, all five transfer modes,
indexed transparency, dither suppression, palette protection and allocation
failure before commit. A coherent owned full-canvas capture records raw indexes
and the resulting palette together, covering unchanged and recolored backgrounds
as well as image pixels. Pure stamp tests reject mismatched adopted prefixes
and attempts to adopt an RGB source. No expected images are generated by the
renderer under test.

The native CMake build and all 85 selected RIP3/preservation/shared-audio tests
pass. Native Valgrind reports zero errors, no retained heap and only three
inherited descriptors in both load-command and stamp fixtures. All 93 frozen
files and 41 legacy helpers match the original preservation baseline; all
6548 source blocks, 62 document identities and the reference pin are unchanged.
Temporary fixtures/logs were removed. Active application routing and RIP1.54
behavior remain unchanged.

### LOAD_BITMAP ordered dithering and transparency

`1b` flag 4 now enables dithering for indexed and RGB files in the separate
engine's current palette model. Successful indexed adoption still suppresses
it; RGB files ignore adoption and retain the dither request. Protected palettes
can be used for dithering without editing them. Transfer modes and transparent
holes operate on the prepared result, with source inversion last. Indexed
transparency retains the original source index; RGB transparency compares the
mapped/dithered destination index, allowing the documented grainy boundary.

The reference does not prescribe a dithering raster algorithm; exact RIPtel
pixels are outside this goal. This renderer uses an ordered 8x8 threshold
matrix. It starts with the nearest palette color and selects a mixing partner
whose projected RGB mixture most reduces mean squared error. Integer-rational
weights select between those colors. Exact matches stay exact, equal choices
retain the first palette index, and a palette with no distinct usable partner
reduces to its nearest color. All products fit uint64_t. Pattern phase is
relative to the original requested device rectangle, before clipping, so a
clipped bitmap retains the same surviving pattern. Source sampling still uses
the existing exact nearest-center geometry. Indexed color plans are prepared
once per palette entry; pixel staging finishes before painting, preserving the
existing nonfatal failure boundary and avoiding palette changes.

`rip3_bitmap_stamp` adds literal colored 8x8 expectations for indexed/RGB
quadrants scaled from a 2x2 source on all three canvases. Full owned index-plane
and palette captures check five transfer modes, exact matches, both transparent
branches, source inversion, clipping phase, unchanged margins/source buffers,
protection and pre-paint allocation failure. Degenerate palettes and invalid
color-selection modes are checked separately. `rip3_bitmap_load_commands` adds
independently authored BMP loads with literal grayscale dither expectations,
RGB adoption-bit ignoring, transparency with source inversion and retained
protected palettes. The new wire assertions first failed on the prior unsupported
flag-4 implementation before the behavior changed.

Full flag-4 device policies outside the current palette model, duplicate-port
orchestration, error-box drawing and active application routing remain pending.
The undocumented flag-16 example remains blocked under #58.

The native CMake build and all 85 selected RIP3/preservation/shared-audio tests
pass. Valgrind reports zero errors, no retained heap and only three inherited
descriptors in each bitmap fixture. All 93 frozen files and 41 legacy helpers
remain unchanged, as do the 6548 source blocks, 62 document identities and
reference pin. Temporary fixtures/logs were removed. Active routing and RIP1.54
behavior are unchanged.

### LOAD_BITMAP explicit final-image duplication

Flag 1 now duplicates into an explicitly numbered, unprotected target port.
The staged bitmap contains the final visible rectangle after original-geometry
scaling, palette mapping/dithering, transfer operations and viewport clipping.
Transparent holes include the prior destination pixels. Successful replacement
creates an offscreen port with those exact physical dimensions, a full active
viewport and automatic image style, whether the target was undefined, offscreen
or an onscreen alias. Redefining an onscreen target does not clear its underlying
screen pixels. The drawing selection, source configuration, other ports and
explicit clipboard binding remain unchanged. Same-port duplication is ignored.

The renderer allocates and prepares the duplicate before painting anything.
A narrow port-table installer consumes the prepared owned surface without a
second allocation or selection change. Its protected-target rejection preserves
caller ownership; the wire layer declines that unresolved branch before file
access. Loader, source staging and duplicate allocation failures retain source
pixels, palette and target definition/content. The allocation-free duplicate
commit precedes the existing allocation-free palette-adoption commit under the
same exclusive session ownership; no callbacks intervene.

[Issue #42's LOAD_BITMAP clarification request](https://github.com/bbs-land/remote-imaging-protocol/issues/42#issuecomment-6932847506)
asks whether protected targets reject the whole load, permit only painting or
bypass protection, and whether they inherit DEFINE_PORT's selection fallback.
It also asks what a completely clipped duplicate does to an existing target.
Clipboard selector zero still depends on the original clipboard selection and
lifecycle questions. These branches remain unsupported; the complete flag-1
requirement is blocked rather than claimed implemented. No empty-port or
implicit clipboard-pointer policy is invented.

`rip3_bitmap_load_commands` checks independent duplicate indexes on all three
canvases for five transfer modes, source inversion, transparent background,
scaled images, right/bottom clipping, mapping/dithering/adoption, target resizing
and target viewport/image-style reset. It verifies new/reused/overlapping onscreen
targets, retained drawing selection/clipboard binding, onscreen-alias pixels and
an offscreen drawing source. Fault cases cover file-buffer, pixel-staging and
duplicate-surface allocation, protected-target preflight without file access,
empty coverage and later recovery; an owned source capture confirms every pixel
survives rejected attempts. `rip3_bitmap_stamp` adds duplicate checks for clipped
negative-origin dither phase and invalid target numbers. `rip3_ports` tests
ownership transfer, protected rejection and nonzero target bounds.

The native CMake build and all 85 selected RIP3/preservation/shared-audio tests
pass. Native Valgrind reports zero errors, zero retained heap and only three
inherited descriptors in both load-command and stamp fixtures. All 93 frozen
files and 41 legacy helpers remain unchanged, as do the 6548 source blocks,
62 document identities and reference pin. Temporary fixtures, issue payloads
and verification logs were removed. Active routing and RIP1.54 are unchanged.

### LOAD_ICON ordinary placement and reference scaling

The separate command dispatcher now connects `1I` with clipboard=0 to BMP media
lookup and stamping. It validates current ENV coordinate width/radix, two-digit
mode, clipboard/scale choices and reserved zero before effects. XY is projected
through the current WORLD. Scale zero uses native file pixel counts; scale one
projects the image size from a fixed 640x350 reference independently of that
WORLD. Positive size counts round down consistently with the existing geometry
mapper. All five transfer modes, current bound palette and viewport clipping
apply. This branch does not adopt/dither palettes or consult image-style fitting,
clearing or disable settings. Protected content permits drawing, and inactive
viewports ignore valid commands before file or clipboard/UI services.

Active ordinary loads require both a bitmap-error UI sink and a typed void
clipboard-clear sink. Valid file/read/decode failures deliver a clear request
and a suitable error-box request even when copying was disabled. Invalid names,
syntax/unsupported fields and memory failures remain nonfatal diagnostics without
those file-error requests. Failed staging retains pixels; a later command still
runs through the same public void boundary. The borrowed clipboard owner may
update its own state during clearing but must retain host bindings and avoid
parser reentry. The new callback is an ownership interface, not evidence of a
complete native clipboard clear or graphical error box.

[Issue #42's LOAD_ICON clarification request](https://github.com/bbs-land/remote-imaging-protocol/issues/42#issuecomment-6933073823)
asks what clearing clipboard contents means separately from clearing its pointer:
port deletion, pixel erasure or a separate image; how protected/current/onscreen
or dangling targets behave; and where/when the error box appears. Clipboard=1
still depends on the unresolved GET_IMAGE selection policy. These paths remain
blocked, and automatic legacy ICN conversion/deletion stays under #19. The BMP
branch neither deletes arbitrary ports nor changes the preserved 1.54 engine.

`rip3_bitmap_load_commands` adds full owned index/palette captures for ordinary
1I loads on all three canvases. Independent expectations cover the exact published
`1I001101010button` example, five transfer modes, indexed/RGB sources, native
sizes, reference scaling, protected ENV/port/palette state, disabled image style
and right/bottom clipping down to one visible pixel. An authored 35-row icon with
10/15/10-row bands verifies the documented 48-row result at 640x480 and literal
60/76-row results at 800x600/1024x768; changing WORLD dimensions affects placement
without changing those reference sizes. Both radices and widths 2..5 are checked.
File/malformed/unsupported/memory failures, missing services, inactive viewports
and split-input recovery exercise owner-clear/UI requests and preserved state.
The new ordinary-load assertions failed against the prior unsupported command
before production behavior changed. Recorded requests do not establish native
UI, clipboard-port clearing, automatic legacy conversion or active routing.

The native CMake build and all 85 selected RIP3/preservation/shared-audio tests
pass. Valgrind reports zero errors, no retained heap and only three inherited
descriptors for the combined bitmap/icon command fixture. All 93 frozen files
and 41 legacy helpers still match the original baseline. All 6548 source blocks,
62 document identities and the reference pin are unchanged. Temporary logs,
issue payloads and filesystem fixtures were removed. Active routing and RIP1.54
behavior remain unchanged.

### Concrete in-scene bitmap file-error boxes

`st_rip3_bitmap_error_show` now supplies a concrete void callback for the
bitmap-error sink. A valid missing-file command can paint the actual box rather
than merely record a request. An explicit rectangle retains its complete
inclusive device geometry before viewport clipping; an unknown-size load gets
a suitable message/filename box at the viewport origin. Clipping does not add
fake borders at viewport edges. The original canvas-native v3 MAF font is used
by default, with an optional immutable custom font for embedding and independent
expectations. Filename UTF-8 is decoded strictly and mapped through the existing
CP437 converter; each unmappable scalar occupies one fallback glyph. No control
sequences are executed and the original bundled font bytes are unchanged.

The renderer uses the current palette's nearest black and most contrasting
entry, without editing the palette. It ignores drawing STYLE, write mode,
image-style fitting/disable settings and ENV references. Protected content may
draw. Current selection and all port/palette/ENV configuration remain unchanged.
It stages the complete visible box before COPY painting, so malformed inputs,
invalid fonts, missing services or allocation failure leave pixels unchanged.
An inactive or no-longer-current requested port is ignored. All paths return
void; the owning parser still consumes the original file failure and can process
the next command. Clipboard clearing/current-port/error-box ordering under #42
is not resolved by this graphics service.

`rip3_bitmap_error_ui` uses independent authored glyphs and literal full-canvas
index/palette expectations on all three canvases. It drives real missing-file
`1b` commands through the parser into the concrete renderer, covering explicit
and default dimensions, negative-origin clipping, wide rectangles without fake
edge borders, retained margins and protected/disabled-image-style ports. It also
checks remapped palette indexes, native bundled fonts, CP437 e-acute, a four-byte
Unicode scalar using one fallback glyph, invalid text/font/geometry, inactive or
mismatched ports and staged allocation failure. A later baud-setting command
succeeds after both normal file failure and UI allocation failure. These are
in-scene bitmap expectations, not proof of modal native-window behavior or active
application routing; those remain part of final engine integration.

The native CMake build and all 87 selected RIP3/preservation/shared-audio/codepage
tests pass. Valgrind reports zero errors, no retained heap and only three
inherited descriptors in the new error-UI fixture. All 93 frozen files and
41 legacy helpers match the original baseline; all 6548 source blocks,
62 document identities and the reference pin remain unchanged. Temporary logs
were removed. Active application routing and RIP1.54 behavior are unchanged.

### Wide classic-text placement before wire integration

The classic bitmap/CHR text geometry no longer narrows projected positions to
signed 32-bit coordinates before justification or clipping. A text-specific
device-position type retains finite wide coordinates and fractional pen positions
through layout. Origins are rounded only after justification, using the existing
nearest/half-positive rule. Direct bitmap placement uses that same rule; direct
CHR baselines retain fractions through matrix transformation and stroke coverage.
Text placements and generated strokes accept magnitudes up to
1024*INT32_MAX, consistent with the separate engine's wide path/image geometry.
Nonfinite or out-of-contract positions reject before painting; ordinary integer
coordinates retain their previous expectations. Geometry for the frozen 1.54
engine and its shared BGI implementation is untouched.

`rip3_classic_text` first reproduced the old placement rejection with a new
wide-anchor test before the range implementation changed. Its expanded full-canvas
expectations on each canvas include a 32769-cell authored font span with advance
2147483646 pixels: right justification brings a 2147483648.25 anchor back to
visible device x=2 without truncation. Fractional CHR advances and anchors are
combined before rounding, including an exact negative half tie. Invalid positions
preserve the complete prior plan.

`rip3_stroke_text` adds an independently authored rectangular CHR glyph whose
large inverted matrix brings strokes from beyond the previous coordinate bound
back into view. A fractional baseline shifts the visible stem correctly; one
coherent full-canvas capture checks ink, one-pixel shadow, foreground precedence,
protected viewport translation and unchanged margins. `rip3_bitmap_text` adds
literal clipped glyph expectations for positive/negative half ties, large
positive/negative offscreen origins and nonfinite/range failures. All three
fixtures cover all agreed canvases; original font/style/shadow/capture tests
remain unchanged apart from position types and the intentionally enlarged range.

This is a prerequisite for m/T/@ command dispatch, not a claim that those commands
or cursor/encoding/macro orchestration are implemented. Cursor ownership, reset
activation and non-left TEXT_XY behavior remain under #24. No terminal/session
processing error interface is introduced by these font services.

The native CMake build and all 87 selected RIP3/preservation/shared-audio/codepage
tests pass. Native Valgrind reports zero errors, no retained heap and only three
inherited descriptors in each of the three updated text fixtures. All 93 frozen
files and 41 legacy helpers remain unchanged, as do the 6548 source blocks,
62 document identities and reference pin. Temporary logs were removed. Active
routing, original font assets and RIP1.54 behavior remain unchanged.

### Classic MOVE/TEXT/TEXT_XY command dispatch

The separate dispatcher now connects `m`, literal classic `T`, and the documented
horizontal-left `@` branch to wide text geometry. The caller supplies the live
device cursor, input encoding and resolved font family explicitly. m projects
current-radix/width WORLD coordinates into that cursor without drawing. T reads
it; @ supplies an anchor and commits position/advance only after rendering
succeeds. Logical advances retain fractions. Other T orientations/justifications
retain the cursor; their @ forms remain unsupported before effects under #24.
Neither this binding nor the command handler chooses PORT/STYLE cursor ownership,
bootstrap, backup or reset activation. E and non-text geometry retain the cursor.

The current STYLE supplies classic font attributes and raw ink/write mode. An
optional immutable override supplies that numbered font; other numbers resolve
to the original bundled canvas font. Successful Y selects the classic family
when a text context is bound; protected/rejected Y changes neither family nor
cursor. Active extended-family text remains unsupported. Direct RGB ink resolves
through the current bound palette; unused shadow background and NOT-mode colors
are not looked up. Glyph staging/validation finish before the cursor is committed,
so unsupported SCAN records, malformed inputs, missing services and allocation
failures cannot leave an advanced cursor or partially accepted text command.

Input encoding is explicit CP437 or strict UTF-8 mapped to original CP437 glyphs,
with one fallback glyph per unmappable scalar. Whole text is validated before
painting. Unexpanded macro, variable, control-notation, template/pick-list and
quoting introducers are declined before output; general expansion/transport
unquoting remains separate. Empty T retains the cursor; empty horizontal-left @
positions it without ink. Inactive viewports ignore valid move/text commands
before consulting text/style/encoding services. Public processing remains void,
with every private service failure consumed as a nonfatal command diagnostic.

`rip3_text_commands` uses authored bitmap and CHR glyphs with independent expected
index/palette captures on all three canvases. Byte-split scenes cover ASCII,
CP437 e-acute/full-block and corresponding UTF-8 characters, spaces, fallback,
T continuation, @ placement and preserved table selections/references/protection.
Literal published m/T/@ examples, current-radix widths 2..5, shortened final MOVE
fields, WORLD projection, huge offscreen anchors, five transfer modes, shadows,
RGB/NOT/unused-background paths and non-advancing T orientations are checked.
A fractional CHR advance survives successive T calls; valid Y activates the classic
family, while protected Y and BOLD SCAN rejection preserve state. UTF-8-buffer and
stroke-staging allocation failures retain both pixels and cursor; malformed split
input recovers into later m/T commands. The initial MOVE test failed against the
prior unsupported dispatcher before implementation changed.

These are explicit-context classic literal commands, not complete text expansion,
extended-font, cursor-reset/ownership, TEXTXY-query or active application coverage.
The original font assets, 1.54 engine and active route are not changed.

The native CMake build and all 88 selected RIP3/preservation/shared-audio/codepage
tests pass. Valgrind reports zero errors, no retained heap and only three
inherited descriptors for the new text-command fixture. All 93 frozen files and
41 legacy helpers remain unchanged; all 6548 source blocks, 62 document identities
and the reference pin are unchanged. Temporary logs were removed. Active routing,
original bundled font bytes and RIP1.54 behavior remain unchanged.

## Literal JPEG file display

`st_rip3_jpeg_load` connects the existing baseline JPEG decoder to the trusted
MEDIA lookup order: host, shared, then configured search entries. Names are bare
UTF-8 and matched caselessly; extensions are literal. No extension-completion
rule is inferred. The loader checks the physical length and complete read,
closes its stream before publishing an owned RGB image, and preserves the output
on file, decode, allocation or close failure. An unavailable decoder is reported
before file access. Sources are never rewritten or deleted.

The void command processor now handles literal `1p` with the current ENV radix
and four reserved zero digits. It loads before drawing into the current port's
explicitly bound DEVICE image style, using the bound current palette and STYLE
write mode. Protected port/style/palette records permit content display, with
their selections, metadata and ENV references retained. JPEG flag 8 is ignored.
COPY display supports stretching and aspect-ratio margins. Other modes require
skip-clear and no aspect preservation: #21 still leaves the clear/margin write
mode unspecified. Active deletion flag 2 and disabled-view deletion ambiguity
are declined before loading or painting. Inactive viewports and disabled images
without disputed flags ignore valid commands before asset/style/palette access.

Unexpanded variables, conditionals, control notation and quoting are declined
before file access. Invalid names, missing/corrupt/truncated files, absent JPEG
support and all staged allocation failures stay inside the nonfatal parser
boundary; no session-ending status is returned. General expansion, `1i` and
IMGSTYLE wire/world semantics, delivery, deletion and reset/PORT-STYLE ownership
remain pending or blocked under #21/#24. Active application routing is unchanged.

`rip3_image_commands_native` and `rip3_image_commands_disabled` run in the same
native build. They exercise the authored DC-only color and grayscale JPEGs,
all supported subsampling profiles, host-over-shared precedence, ordered search,
Unicode caseless names, literal extensions and unchanged source time/size. Fault
injection checks complete-read/short-read/I/O/size/close failures and owned-image
cleanup, filename/file-buffer/decode/draw-staging allocation failures, preserved
outputs and recovery. Byte-split 1p streams produce independently expected full
index/palette captures on all three canvases. The write-mode/flag matrix checks
supported output and rejection of unresolved clear/margin combinations without
scene changes. RGB background handling, unused backgrounds, disabled/inactive
no-ops and a later valid command after a bad JPEG are included.

The unresolved TEXTXY query remains unsupported. [Issue #6 now includes concrete
questions](https://github.com/bbs-land/remote-imaging-protocol/issues/6#issuecomment-6935322623)
about start versus end position, MOVE/empty-text effects, coordinate units,
serialization and fractional CHR advances; its hypothetical reply is not adopted.

The native Release CMake configure/build and all 93 selected tests passed
(RIP3, RIP154 source preservation, JPEG decoder/dependency, shared audio and
codepage). Both new command fixtures report zero Valgrind errors, no retained
heap and only three inherited descriptors. The 93 frozen files and 41 legacy
helpers still match the original baseline. All 6548 immutable source blocks,
62 document identities and the reference pin are retained; the review queue
contains 1000 implemented, 724 pending, 455 blocked, 973 contextual, 24 excluded
and 3372 unreviewed blocks. Verification logs and fixture directories were
removed. These results establish this separate-engine milestone, not full
compliance or active application/physical-device coverage.

## JPEG presentation reconciled with the adopted reference

The #21 inactive-viewport follow-up at `5ae9d165b1ac3e36161847e68db0d763228b779b`
is now reviewed and pinned. It says that wire 1i updates inactive/protected current
ports and that activation retains the new image style. The source index was
independently reproduced from the complete pinned archive, now 7353 blocks in
65 documents. The existing #21 follow-up is recorded as documentation-resolved;
wire setter/activation integration remains pending. No issue content was added.

JPEG presentation now follows the corrected contract. The renderer uses COPY,
independently of vector STYLE write mode and background. Its optional initial
clear replaces only the fitted image rectangle with the palette's black entry.
NOCLEAR suppresses that operation. Aspect-ratio margins retain their previous
pixels, and source pixels are copied over the fitted rectangle. Preparation and
allocation still finish before any paint, so failures cannot partially clear
the scene. The decoded-image API no longer accepts a vector write mode or
background argument that JPEG does not use.

Literal 1p no longer requires or consults a STYLE binding and accepts display
with DELETE set. No image-style bit deletes a displayed or skipped JPEG, and
USEPAL remains ignored for RGB JPEGs. The explicit internal inhibit state skips
asset lookup without deleting files; it is not an interpretation of wire 1i's
all-zero coordinates. Wallpaper remains unsupported before loading/painting.
The raw-zero/one-pixel producer and physical fallback, wire 1i/IMGSTYLE, reset,
delivery, wallpaper and full port-coordinate reconciliation remain pending.

The updated expectations fail against the prior implementation before it changes.
The byte-split command matrix now exercises all 16 low-bit flag combinations
against all five vector write modes on all three canvases, expecting COPY output
and retained margins throughout. Source size/time remains unchanged with DELETE.
Missing STYLE bindings and unused/out-of-range STYLE backgrounds do not prevent
JPEG display. Filename, file-buffer, decoder and draw-staging allocation failures
occur before any clear. The normal disabled-JPEG backend and later-command
recovery remain covered.

The fixture records actual clear calls as well as complete index/palette captures:
black replacement, COPY mode, the fitted bounds, and suppression by NOCLEAR.
Black at palette index 7 is checked independently of index zero. Literal wide
and tall 10x5/5x10 inputs reproduce the reference's two measured fitted rectangles
on all three canvases. GDI's exclusive far edges are converted to this kernel's
inclusive representation at the expectation boundary. These checks establish
geometric/side-effect behavior, not exact JPEG sampling or Windows realization.

The native Release CMake configure/build and all 93 selected tests pass
(5.78 seconds). Both command fixtures report zero Valgrind errors, no retained
heap and only three inherited descriptors. The rendering fixture also reports
zero errors and only three inherited descriptors; its sole retained allocation
is a suppressed 4096-byte C-library buffer. All 93 frozen files and 41 legacy
helpers still match the original baseline. The current ledger has 22 implemented,
1626 pending, 4789 unreviewed, 875 contextual, 24 excluded and 17 blocked blocks.
All source/archive/verification temporary directories and logs were removed.
Active application routing and the frozen RIP1.54 implementation remain unchanged.

## LOAD_ICON failure retention after reference adoption

The inherited clipboard-clearing LOAD_ICON path is removed. Ordinary load/decode
failure now requests a placeholder on the current drawing port for both
clipboard flag values, retaining the clipboard pointer, current selection and
all port definitions/resources. Protected, screen/offscreen, current and undefined
targets are not deletion candidates on this path. Invalid filenames remain
nonfatal diagnostics without a placeholder request. Buffer/decode allocation
failure may prevent drawing but makes the same placeholder request. Rendering
failure in the clipboard=0 path also requests it, before returning only through
the parser's optional nonfatal diagnostic. No session-ending status escapes.

The obsolete clipboard-clear callback and its host binding are removed. Missing
clipboard bindings do not prevent ordinary failure handling. A placeholder may
change current pixels, including a screen clipboard alias; a separate offscreen
clipboard retains its pixels. A placeholder that cannot draw still leaves all
selection/definition state intact. Missing UI bindings decline before loading.

At that failure-retention milestone, successful clipboard=1 still awaited the
shared GET_IMAGE helper and was declined before painting. Its replacement/allocation
failure and same-source behavior were pending implementation; they must not
be reused for an ordinary LOAD_ICON file failure. The literal success path,
port-coordinate reconciliation, legacy conversion and active application
integration remain separate. The relevant source block stays pending until
the complete command/capture interaction is verified.

`rip3_icon_failure_commands` adds 216 independent cases over the three canvases,
both clipboard flags, onscreen/offscreen current sources, absent/unbound/dangling
clipboard pointers, current targets and protected/unprotected onscreen/offscreen
targets, with both a drawing and non-drawing placeholder. The controlled UI sink
checks pointer/current state at invocation and paints one authored marker.
Complete index/palette captures check that marker and untouched screen/offscreen
content; field-wise snapshots check all 36 port definitions, ENV references and
palette selection/protection. This is controlled placeholder-request coverage,
not a new claim about native application UI rasterization.

The initial fixture fails against the old clearing callback. Further cases cover
bad files, file-buffer/decode/staging allocation failure, invalid names, valid
clipboard=1 deferral without paint and byte-split recovery into a valid authored
one-pixel BMP and later base-math command. Existing 1I load/scale/transfer and
concrete bitmap-error UI tests are retained, with expectations reconciled to the
adopted failure contract rather than the superseded draft.

The existing native Release CMake configure/build and all 94 selected tests
pass (7.38 seconds). The new failure fixture reports zero Valgrind errors,
no retained heap, 6815 allocations/frees and only three inherited descriptors.
All 93 frozen files and 41 legacy helpers are unchanged. The 7353 source blocks,
65 document identities and reference pin are unchanged; the failure block is
reviewed as pending with this partial implementation/test evidence, rather than
claimed complete. All owned temporary logs and fixture directories were removed.

## Shared GET_IMAGE capture and LOAD_ICON integration

`st_rip3_clipboard_capture` now implements the shared capture lifecycle from the
adopted #42 contract. One session owner holds a mutable clipboard selection and
binds an explicit offscreen pixel budget. Assigned targets are reused exactly;
otherwise the first unused slot 1..35 wins, irrespective of existing offscreen
ports. Full tables fail without deleting anything. Protected assigned targets
and explicitly assigned zero reject without changing pointer or selection.

An unprotected assigned target is deleted before checking the post-deletion
budget and allocating its replacement. Failure clears the pointer and leaves
the deleted target undefined. Unrelated definitions/pixels survive. Deleting a
current target selects port zero; the source is read from the live selection
after allocation, so the documented destructive same-source case copies from
port zero and leaves it current. No old bitmap/source index is saved for rollback.
Allocation counts use offscreen device pixels, with reclaimed target area removed
before accounting. No universal display-sized budget is inferred.

The input rectangle uses exclusive device far edges. The helper's extra low-WORD
allocation unit is intentional. A wire (1,1)..(3,3) capture at unit mapping copies
a 3x3 source into a 4x4 bitmap. Source clipping occurs before stretching to the
new allocation. A completely outside source or a downstream copy failure retains
the newly allocated clear clipboard and reports success, as the shared driver
helper does. Nonzero wrapped allocation counts are supported. Wrapped zero counts
remain unsupported before mutation: the pinned derivation disclaims a native
zero-dimensional allocation result, so this edge remains pending rather than
being inferred from an external graphics API.

Wire 1C now decodes current ENV radix/coordinate width, normalizes corners,
projects them through the current WORLD and adds the caller's first far-edge
increment before shared capture. Reserved omission and final-coordinate
shortening are supported. Inactive viewports ignore valid captures before
clipboard/budget access. Missing bindings and malformed commands cannot mutate
the clipboard. Public processing remains void; service failures remain optional
nonfatal diagnostics. Read-only PUT_IMAGE/WRITE_ICON consumers use the same
session selection without modifying it.

LOAD_ICON clipboard=1 now paints first, then calls the shared helper with the
renderer-requested unclipped rectangle. Protected-target rejection and allocation
failure occur after painting and do not request a missing-file placeholder.
Same-source capture uses the same deletion/fallback defect. Wholly clipped valid
images can replace an old clipboard with a newly allocated clear bitmap.
Ordinary load/renderer failure still requests its placeholder before capture and
retains clipboard state. Missing capture bindings decline a valid file before
painting without suppressing ordinary file failures.

`rip3_clipboard` checks every documented target-selection example, protected
screen-zero/current targets, full tables, exact budget reclamation, allocation
failure, partial/empty clipping, copy failure and nonzero low-WORD wrapping.
Independent literal 3x3-to-4x4 indexes, viewport offsets, radix 36/64 and widths
2..5 are checked on all three canvases; complete source index/palette captures
verify that capture does not alter screen contents. Byte-split 1C streams cover
normal/reversed corners, shortening, malformed input and later recovery.

The LOAD_ICON fixture also checks successful capture from onscreen/offscreen
sources, protected and current targets, first-unused/assigned selection, clear
outside captures, post-paint budget failure and pre-capture renderer failure on
all three canvases. Its integration expectation failed against the previous
unconnected clipboard=1 path before that path changed. These are independent
geometric/index expectations; original Windows interpolation is not an oracle.
Reset/whole-port backup ownership, other capture callers, complete port-coordinate
producer reconciliation, legacy conversion and active application routing remain
pending. The original RIP1.54 implementation and active route are unchanged.

The native Release CMake configure/build and all 95 selected tests pass
(7.64 seconds). Valgrind reports zero errors, no retained heap and only three
inherited descriptors for both the clipboard and expanded LOAD_ICON fixtures.
Their allocation/free counts are 599/599 and 8891/8891 respectively. All 93
frozen files and 41 legacy helpers still match the original baseline. All 7353
source blocks, 65 document identities and the reference pin are unchanged;
42 blocks are implemented, 1624 pending, 4768 unreviewed, 878 contextual,
24 excluded and 17 blocked. Temporary verification logs and fixture directories
were removed. No active application, Windows, native-device or reset/backup
coverage is inferred from these separate-engine tests.


## LOAD_BITMAP post-draw duplication and wallpaper reconciliation

This milestone uses the adopted `5ae9d165b1ac3e36161847e68db0d763228b779b`
contract, including the #42 capture and #58 wallpaper derivations. It supersedes
the earlier explicit-duplication milestone's staged, clipped, transactional
replacement policy and its unresolved flag16/clipboard statements. Those earlier
entries retain their historical provenance; their superseded expectations are
not acceptance criteria for the adopted behavior.

`1b` now accepts flags 0..63 and rejects higher values before filename access or
any visible effects. Flag32 alone is accepted without wallpaper. Initial
palette/key/dither/transfer processing paints the image first and commits palette
adoption before duplication. The painter no longer owns a second duplicate-port
path. File/initial-render failures request the bitmap-error sink and exit before
duplication or wallpaper; failure to stage the UI itself remains nonfatal.

Flag1 uses the full requested device rectangle even if partly or wholly outside
the viewport. Duplicate0 invokes shared GET_IMAGE capture, including its extra
allocation unit and destructive same-source behavior. Explicit current and >35
selectors skip only duplication. A different defined explicit target is deleted
before allocating its replacement, clearing a matching clipboard assignment;
an undefined target skips deletion and retains a dangling assignment. Explicit
replacement does not assign a new clipboard. A protected target retains its
resources, pointer and current selection, with the initial draw/adoption already
visible, and prevents wallpaper. This also applies to a screen target sharing
pixels with the initial draw. Creation failure leaves the old target deleted
and still reaches wallpaper; copy failure retains the newly clear allocation.
The documented outside 4x2 image produces a clear explicit 4x2 bitmap or a clear
clipboard 5x3 bitmap.

Flags16/32 snapshot and repeat the composited seed through the live current
viewport with COPY. Palette, dither, key and initial ROP are not reapplied;
background retained through keyed holes becomes seed pixel data too. Far seed
edges are trimmed to the viewport before repetition. Alternate rows shift left
by floor(trimmed width/2), relative to the seed row in both directions. Invalid
seed origins/reversed rectangles reject without additional painting. Inactive
viewports and seeds already covering the full viewport need no staging.
Wallpaper allocation failure retains already completed draw/duplication effects.
The clipboard path can change current to port zero, including on allocation
failure; wallpaper rereads that port and its viewport rather than restoring the
old source.

`rip3_wallpaper` uses independent literal odd/even/single-width grids, rows above
and left of the seed, far clipping, protected content, inactive/full-viewport
no-ops and staging failure. The expanded `rip3_bitmap_load_commands` fixture
checks all 64 flag combinations in both radices, invalid high flags before file
access, explicit/clipboard capture sizes, retained or cleared assignments,
protected screen/offscreen targets and file/render/create/copy/wallpaper failure
ordering. Byte-split wallpaper streams use independent XOR/key/background seeds
and complete screen index/palette captures on all three canvases. Separate
same-source capture cases verify the live port-zero fallback and its different
viewport. Existing placement/transfer/adoption/dither/error/recovery tests remain.

The ledger accepts these operation/order/phase blocks separately from the full
LOAD_BITMAP contract. Corrected depth-specific color-key translation, additional
bitmap depths/codecs, complete WORLD/port coordinate producer reconciliation,
wrapped-zero shared capture allocation, reset/backup ownership and active
application routing remain pending. These tests do not establish native device,
Windows, auditory or exact RIPtel pixel reproduction. RIP1.54 and the current
application route are unchanged. No content was added to upstream issues.

The existing native FreeBSD Release CMake configure/build with `/usr/bin/cc`
passes without new compiler warnings. All 96 selected tests pass (7.68 seconds).
Valgrind reports zero errors, no retained heap and only three inherited file
descriptors for the wallpaper and expanded bitmap-command fixtures; allocation/
free counts are 17/17 and 15342/15342. All 93 frozen files and 41 legacy helpers
still match the original baseline. All 7353 source blocks, 65 document identities,
reference/adoption evidence and the RIP1.54 capture inventory are unchanged.
The reviewed queue now has 53 implemented, 1625 pending, 4754 unreviewed,
880 contextual, 24 excluded and 17 blocked blocks. Owned temporary logs/assets
and the CMake fixture directory were removed.


## Bitmap depth and color-key reconciliation

This milestone follows the adopted bitmap-mask delta and #8 derivation at the
unchanged reference pin. The file decoder/encoder now supports all six profiled
BI_RGB depths: 1/4/8/16/24/32. RGB555 expands components with floor(v*255/31);
encoding quantizes RGB to the nearest five-bit component. This is the client's
component policy, without a historical raster-rounding claim. Bit 15 in RGB555
and the high byte in 32-bit files are unused, with no alpha semantics. Canonical
output zeroes those bits/bytes and padding. Independent packed bytes exercise
colors, row orientation, stride, truncation and malformed headers, with authored
canonical output expectations rather than round-trip-only evidence.

Wire key values remain limited to 0..255 when flag 8 is set. Translation uses
the drawing palette before image-palette adoption: below the initialized direct
limit the index remains unchanged; otherwise its stored RGB selects the first
exact match, or the last equal nonzero minimum. The explicit translator supports
the documented limits 2/16/236. The current 256-entry drawing device uses the
normal limit 236; ENV RGB input precision is not a device-depth selector. All
256 inputs at all three limits are independently checked, including duplicate
colors on either side of the boundary and retained output on invalid arguments.
A wire adoption case distinguishes translation before versus after adoption.

Key classification now follows depth rather than a universal file-index test:
1-bit compares the source bit to zero/nonzero translated key; 4/8-bit compares
nearest output-palette indexes before dithering or transfer inversion; 24-bit
compares exact source RGB to the output palette's key entry. Nearby RGB which
quantizes to that same output index remains opaque. The shipped 16-bit key
path fails before painting, and the shipped 32-bit path marks no pixels
transparent even at an exact match. Adopted drawing indexes can remain distinct
while the separate key map collapses equal RGB entries. The previous raw-index
and post-dither RGB expectations are superseded, not preserved as conformance
oracles for the corrected contract.

`rip3_bitmap_key` uses authored index/selection tables for all modes, native,
up/downscaled and left-clipped placements, including zero/nonzero 1-bit keys,
nearby RGB, inverted/duplicate palettes, dithering and adoption. Complete screen
index/palette captures on all three canvases check retained keyed backgrounds,
unchanged viewport margins and source objects. The expanded load fixture checks
all profiled depths through file/wire loading, explicit BMM use, ignored malformed
same-name companions, missing explicit BMM handling, successful keyed-result
duplication and ordinary RGB555/32-bit LOAD_ICON. A keyed RGB555 failure retains
the prior clipboard, skips duplicate/wallpaper, requests the normal error sink
and permits the following byte-split command to paint. All public parsing remains
void, with failures consumed locally as optional diagnostics.

This accepts the documented format and selection behaviors, without claiming
the full true-color drawing-device contract. The current surface performs ROPs
on mapped output indexes. Component-wise RGB raster operations, physical palette
realization, complete file-role/conversion/playback/button consumers and full
port-coordinate/application integration still need implementation and acceptance.
The ledger records those RGB ROP paragraphs as pending. Supporting the codec
or key selector is not evidence that those broader scene requirements work.

The existing reference already answers every #21 documentation question,
including inactive `1i` updates on protected ports. Its open GitHub state does
not make those answers unresolved; remaining setter/macro/reset work is local
implementation work. A read-only review found the newer PR #71 commit
`61b8fedfe9d3a12442f65a73089a4f008037c519` changes prose spacing. The adopted
pin remains unchanged. No issue comments or requests were added.

The existing native FreeBSD Release CMake configure/build with `/usr/bin/cc`
passes without new warnings. All 97 selected tests pass (8.49 seconds), and the
final expanded wire fixture also passes after adding keyed-result duplication
and ordinary LOAD_ICON cases. Valgrind reports zero errors, no retained heap
and only three inherited descriptors for the codec, key and load fixtures;
allocation/free counts are 51/51, 750/750 and 41601/41601. All 93 frozen files
and 41 legacy helpers remain unchanged. The 7353 source blocks, 65 document
identities, reference/adoption evidence and RIP1.54 capture inventory are intact.
The queue now has 62 implemented, 1635 pending, 4729 unreviewed, 886 contextual,
24 excluded and 17 blocked blocks. Owned temporary logs/assets and CMake fixture
directories were removed. The full compliance goal remains in progress.


## IMAGE_STYLE wire, macro and reset-state reconciliation

The separate engine now connects wire `1i` and the `IMGSTYLE` callback to one
port-owned stored device record. The setter orders WORLD corners, projects once,
and stores the extra far-edge unit. It preserves unclipped boundaries so queries
can return the documented result rather than reconstructing the original input.
Default records contain the actual all-minus-one viewport-at-display marker and
flags zero. The distinct internal raw-zero inhibit remains available; all-zero
wire coordinates produce enabled `(0,0,1,1)` instead.

Wire `1i` accepts flags 0..63 and replaces the old flags even on protected or
inactive current ports. The current ENV supplies XY width and radix without
activating its referenced port/palette/style. The documented full reserved block,
shorter zero blocks, omission and last-field shortening are accepted. Invalid
fields and unrepresentable conversions retain the old image record and current
selection. The canonical full-reserved example is independently decoded.

`IMGSTYLE` has its separate callback behavior: decimal port or CUR, decimal WORLD
coordinates, and case-insensitive ASPECT/DELETE/NOCLEAR/USEPAL keywords. Keywords
OR together and replace the flags; none means zero. A target may be screen or
offscreen. Undefined/inactive targets query and set as `-1` without mutation or
error; protected enabled targets query normally, while setting returns no value
and makes no change. A valid setter temporarily selects the target and always
restores the old selection, including when conversion fails. Queries produce
five unpadded colon-separated fields, converting stored device boundaries through
the live current WORLD without undoing the far increment. The native example
`20:40:222:242:5` is an independent expected reply.

The port-reset component now resets the old current port's image record even
when protected, and resets port zero independently. Other protected surviving
ports retain their image state; unprotected nonzero ports are removed. Hard
reset removes protected nonzero ports too. These are image/port component tests,
not acceptance of the complete RESET_WINDOWS/HEADER family dispatch. Other reset
families and header/global-state orchestration remain pending.

The focused `rip3_image_style` fixture covers both radices, XY widths 2..5,
all accepted wire flags and invalid 64, reserved lengths, reversed corners,
protected/inactive updates, every keyword combination, literal queries, undefined
targets, invalid arguments and temporary-selection recovery on all three canvases.
New fixtures deactivate/reactivate through the port service around byte-split
`1i`/`1p` commands on a protected port: an intervening inactive image accesses no allocator/file/decoder,
then reactivation paints the new area. Native and disabled JPEG variants exercise
both capability paths. Independent complete screen index/palette captures check
the new fit and retained margins. Full-viewport all-zero wire style reaches the
native decoder and physical-size fallback on each canvas, with authored JPEG
DC values and independently calculated sampling thresholds.

Existing raster fixtures retain their literal expectations. Their setup helper
now expresses an inclusive visible pixel area as the stored record consumed by
the JPEG presentation chain; wire/macro state oracles call the raw interface
directly. A thin-area minimum-fit fixture uses a positive far coordinate to
avoid the documented nonpositive-far physical fallback. The old draft setter's
configuration clipping/protection expectations are replaced by the adopted raw
record contract. Port backup tests retain exact image records.

JPEG wallpaper, received-file/export and full playback/host-expansion integration,
complete sub-viewport/offscreen fallback geometry, other coordinate producers
and whole-scene activation/reset remain pending. #21 supplies the required
answers; none of these implementation tasks is a request for more documentation.
RIP1.54, its capture inventory, font assets and active application route are
unchanged. No upstream issue content was added.

The existing native FreeBSD Release CMake configure/build with `/usr/bin/cc`
and all 98 selected tests pass (9.66 seconds), without compiler warnings in the
final build. Valgrind reports zero errors, no retained heap and only three
inherited descriptors for the style, native JPEG-command and disabled JPEG-command
fixtures; their allocation/free counts are 54/54, 17166/17166 and 127/127.
All 93 frozen files and 41 legacy helpers remain unchanged. The 7353 source
blocks, 65 document identities, reference/adoption evidence and RIP1.54 capture
inventory are intact. The queue now has 76 implemented, 1633 pending,
4717 unreviewed, 886 contextual, 24 excluded and 17 blocked blocks. Owned
temporary logs/assets and CMake fixture directories were removed.


## JPEG image-area wallpaper

JPEG presentation now applies image-style bits 16/32 after a successful initial
image draw, using the shared device-pixel COPY tiler. Initial palette mapping,
aspect fitting, black clear and JPEG decoding happen once. Wallpaper copies the
resulting raw image-area pixels, including retained background, across the live
viewport. Bit 32 alone has no effect. No image-style bit deletes the JPEG or
changes its palette. File/load/initial-staging failure skips wallpaper; wallpaper
staging failure retains the completed initial image and reports only an optional
nonfatal diagnostic. A later valid command continues through the void parser.

The wallpaper seed is the **stored image-area record**, before the JPEG consumer's
far-edge reductions, rather than the fitted JPEG rectangle. This distinction is
confirmed by a read-only static review of the unchanged original RIPSCRIP.DLL
(SHA-256 `83d71b358492b15bcfd3773c30a52784881a3a49a5b6e9029d986b3cba1fd5e8`).
Common presentation tests successful display at RVA 0x38387 and calls 0x3f12f when
bit 16 is set. That wrapper builds a RECT directly from the current port's
+0x4a/+0x4e/+0x52/+0x56 fields at 0x3f156–0x3f166, then passes it unchanged to
0x3ea2e at 0x3f1ab. The helper computes width/height by subtracting RECT boundaries
at 0x3ead3–0x3eaeb. No JPEG far reduction or aspect-fit rectangle replaces those
fields on the wallpaper path. This supports the adopted #21/#58 derivations,
without changing the normative reference or claiming original Windows rasters.

The independent seed fixture makes the representation difference explicit:

| Operation | Device boundaries (exclusive far) | Size |
| --- | --- | --- |
| Stored image-area record / wallpaper seed | (2,3)–(7,8) | 5×5 |
| Initial reduced JPEG target | (2,3)–(6,7) | 4×4 |
| ASPECT fit for its 2×1 source | (2,4)–(6,6) | 4×2 |

The extra far row/column and aspect margins remain background in the seed and
are repeated as pixels. The half-width stagger is based on this raw tile's
trimmed width, with alternating phase above and below the seed row. Shared
wallpaper staging snapshots the seed before copying, with no second aspect fit,
clear, palette lookup or vector ROP. A full-viewport seed needs no extra staging;
a raw default marker fails rectangle reconciliation after the initial draw,
instead of silently becoming a fitted seed. Wire all-zero style still displays
through physical-size fallback, then its stored 1×1 wallpaper seed repeats the
top-left pixel over the viewport, for both plain and staggered flags.

`rip3_image_wallpaper` checks independent literal 5×5 seed/phase tables on protected
screen and offscreen ports across all canvases, retained viewport margins and
source/palette/selection/record state, ignored bit 32, clear counts, initial and
post-draw allocation failure, invalid source, inhibit/inactive no-ops, full-area
bypass and raw-marker rejection. Its positive test fails against the previous
unsupported path before the implementation changes. The actual JPEG wire fixture
now covers every one of the 64 flag combinations under all five vector write
modes on all three canvases, using independent full-screen index/palette captures
and a distinguishing 19×19 raw tile around an 18×18 JPEG target. Extra tests cover
one-pixel wire seeds, missing/bad files, unavailable decoders, initial/post-draw
allocation failure and later recovery. A post-draw rectangle error is classified
as an operation failure rather than an invalid filename. Native and deliberately
disabled JPEG variants retain distinct capability expectations.

Literal image rendering, ordinary style setters and wallpaper now share these
rules in the separate engine. Complete host playback/macro expansion, received
image/export consumers, sub-viewport/offscreen physical fallback, general port
coordinate reconciliation, full scene resets and active application routing
remain pending. This is implementation work under documented answers, not a
request to extend #21. RIP1.54 and its historical captures remain unchanged.

The existing native FreeBSD Release CMake configure/build with `/usr/bin/cc`
and all 99 selected tests pass (18.03 seconds), without new compiler warnings.
Valgrind reports zero errors, no retained heap and only three inherited
descriptors for the new renderer, expanded native JPEG-command and disabled
JPEG-command fixtures; allocation/free counts are 1079/1079, 63723/63723 and
140/140. All 93 frozen files and 41 legacy helpers remain unchanged. The 7353
source blocks, 65 document identities, reference/adoption evidence and RIP1.54
capture inventory are intact. The queue now has 80 implemented, 1630 pending,
4715 unreviewed, 887 contextual, 24 excluded and 17 blocked blocks. Owned
temporary logs/assets and CMake fixture directories were removed. No upstream
issue content, active application route or original artifact was modified.

## PORT_WRITE source geometry reconciliation

`2W` now follows the adopted original-client screen/offscreen distinction. Its
four coordinate fields use the current radix and width but remain raw DEVICE
offsets from the stored viewport origin. They are not projected through WORLD.
Screen crops have half-open far boundaries, with independent right/bottom
clipping. All-zero input selects the stored viewport; two zero ending fields
select its remainder from the requested start. Explicit nonzero rectangles must
have strictly increasing axes before clipping, without adding an inclusive
pixel. For an 8×4 viewport, (1,1)–(9,2) exports 7×1 and (1,1)–(2,9) exports 1×3.

Offscreen export deliberately ignores the validated crop and serializes the
whole backing bitmap. A 10×6 backing allocation with an 8×4 viewport exports
10×6. Defined inactive and protected sources remain readable in either path.
Undefined and explicit empty/reversed sources diagnose within the void command
parser and preserve the output file. Allocation, encoding and replacement
failures also preserve the previous file; later valid commands still execute.
No export changes port selection, definitions, viewport/image state, protection,
source pixels, active palette or ENV references. The existing separate-engine
`1W` viewport-capture policy is retained for its own later reconciliation.

The contract comes from the pinned chapter 7 PORT_WRITE section and the adopted
`issue-10-port-geometry.md` post-PR results, including original handler
0x4699a–0x46bd8, writer 0x27a90 and whole-bitmap path 0x27c8d–0x27ce2. A bounded,
read-only disassembly check confirms the handler's raw-coordinate arithmetic,
strict explicit bounds and independent far clipping. No original DLL bytes,
upstream issue content or pinned reference were changed.

`rip3_bitmap_write_commands` now checks 3456 exports across all three canvases,
screen/offscreen sources, all protection/activation combinations, both radices,
coordinate widths 2–5 and three different WORLD sizes. Its independent labeled
source pixels, BMP headers, palette bytes, bottom-up rows and padding distinguish
half-open crops from inclusive capture and whole backing from viewport capture.
The new whole-backing expectation fails against the previous implementation.
Additional tests cover nonzero screen-port/viewport origins, current port zero,
named ports zero/35, the preserved chapter example, base64-only digits, huge
raw coordinates, unchanged source/ENV/palette state and byte-split error recovery.
Both bitmap-allocation stages, encoding and atomic replacement are faulted for
both source kinds while checking previous file bytes. Direct raw-capture tests
cover inactive protected sources, invalid/external/undefined rectangles, owned
pixels/palette and lifetime after source destruction. Existing `1W` tests remain.

This milestone reconciles the documented export geometry in the separate v3
engine. Full true-color device/export representation, filename macro expansion,
clipboard export lifecycle and active application routing remain pending
implementation; none is a request for further #21 documentation. RIP 1.54,
its frozen helpers and historical capture inventory remain unchanged.

The existing native FreeBSD Release CMake configuration with `/usr/bin/cc`
configures/builds without new warnings; all 99 selected tests pass (26.90 seconds).
Valgrind reports zero errors, no retained heap, 228215 allocations/frees and only
three inherited descriptors for the expanded export fixture. The preservation
check retains all 93 files and 41 helpers. Reference/adoption identities, the
7353 source blocks and RIP1.54 capture inventory remain intact; only PORT_WRITE
review metadata changed. The queue now has 97 implemented, 1623 pending, 4703
unreviewed, 889 contextual, 24 excluded and 17 blocked blocks. Owned temporary
logs/assets and CMake fixture directories were removed. No changes were pushed.

## PORT_COPY extent and clipping reconciliation

`2C` now uses the adopted half-open extent contract. WORLD corners are projected
once, with wide intermediate coordinates retained until clipping. Both source
and destination all-zero forms are resolved **after conversion**. A fully zero
destination selects its whole viewport before considering the zero-ending
verbatim shorthand; the latter instead adds the source's original physical
width/height to the destination origin. Nonzero raw coordinates that round to
zero therefore receive the shipped zero-form behavior.

The initial physical extents choose the copy path before any viewport trimming.
Equal-size copies shorten the source/destination far boundaries together and
retain one source pixel per destination pixel. Unequal-size copies clip each
rectangle independently, then fit the surviving source to the surviving
destination. For a 4×1 source and 2×1 destination at X=1 with zero ending fields,
only the first source pixel reaches destination X=1; an all-zero destination
instead fits all four pixels to the two-pixel viewport. The same coupled rule
applies to explicit equal-size rectangles. Empty/reversed projected rectangles
diagnose within the void command parser. Undefined/inactive ports and valid
wholly external rectangles produce no copy. Protection permits content copying;
current selection, protection and stored viewport records are retained. Overlap
reads original source pixels before drawing.

This interpretation comes from the pinned chapter 7 and adopted
`issue-10-port-geometry.md`, checked against read-only original DLL instructions
at handler 0x46372, post-conversion zero tests 0x4656e/0x465ba, compositor size
decision 0x13533–0x13555 and coupled far trim 0x13456. The original artifact's
SHA-256 remains `83d71b358492b15bcfd3773c30a52784881a3a49a5b6e9029d986b3cba1fd5e8`.
No new normative pin or upstream issue content was introduced.

The command converts its final clipped rectangles to the existing device
kernel's inclusive representation only at the call boundary. Shared generic
fit-copy, unscaled blit and clipboard services retain their current contracts.
Wide projected ends that differ above INT32_MAX remain unequal: a distinguishing
four-pixel source/two-pixel target fixture fits the complete source when those
ends differ, and copies its first two pixels when they are equal. Early
saturation would merge those branches and fail the independent expectation.

`rip3_copy_commands` adds ten literal geometry/pixel cases packed into one
coherent full-device capture per mode/source-kind scene on every canvas. They
cover both axes, source and destination clipping, explicit and synthesized equal
sizes, fit/full-view precedence, one-pixel extents and protected screen/offscreen
sources. A 25920-copy matrix varies source/destination widths 1–8, every valid
horizontal destination origin, three destination forms, both destination kinds,
all five indexed modes and all canvases. Fit matrix sources are uniform so their
coverage checks do not assume Windows filtering; distinct unscaled pixels expose
accidental refitting. Both radices and widths 2–5 exercise projected-zero forms;
additional tests retain base64-only digits, source/selection state, reserved
lengths 0–5, huge coordinates and the canonical wire literal. Both fit and
unscaled overlap snapshots are faulted before drawing, with unchanged scene
bytes and successful subsequent commands. Byte-split input also recovers from
invalid mode and empty projected geometry within a coherent captured scene.

Older authored scene pixel expectations are retained by correcting their wire
far fields to exclusive boundaries. The preserved chapter literal now has its
documented 40×480, 50×600 and 64×768 footprints on the three canvases, without an
extra far pixel. No RIP1.54 expected image, capture inventory, frozen source or
helper was changed.

Geometry is reconciled in the separate engine; complete PORT_COPY compliance is
not claimed. Its six general transfer-mode ledger blocks remain pending because
the device still stores 8-bit indexes. Grayscale/index arithmetic does not prove
COPY/XOR/OR/AND/inverted-source results for arbitrary RGB palettes. Full-color
device representation, generic grammar/comment reconciliation and active
application routing remain implementation work under the existing reference.

The existing native FreeBSD Release CMake configure/build with `/usr/bin/cc`
passes without new compiler warnings. All 99 selected tests pass (26.76 seconds).
Valgrind reports zero errors, no retained heap, 531 allocations/frees and only
three inherited descriptors for the expanded command fixture. All 93 frozen
files and 41 helpers remain unchanged. Source/pin/adoption identities and the
RIP1.54 capture inventory are intact; only PORT_COPY review metadata changed.
The 7353-block queue now has 114 implemented, 1614 pending, 4695 unreviewed,
889 contextual, 24 excluded and 17 blocked entries. Owned temporary logs were
removed. No active application route, original artifact or upstream content was
modified, and no changes were pushed.

## SWITCH_PORT selection and protection reconciliation

`2s` now uses the adopted three-character target/flags descriptor without a
reserved field. Target validation precedes every protection change. Higher flag
bits are accepted; only bits 1/2/4/8 act. The original order is source-protect,
source-unprotect, select/create, destination-protect, destination-unprotect.
Both conflicting pairs therefore end unprotected, and destination actions win
when source and destination are the same port. Defined protected and inactive
ports remain selectable. Port zero ignores all protection writes.

Ordinary undefined destinations are created as default full-screen **screen**
ports, then selected. Construction uses physical display dimensions, independent
of the current WORLD, ENV reference or port-zero viewport/image record. A new
`st_rip3_ports_select` service is shared by `2s` and `CUR(PORT,n)`; the existing
strict device switch service retains its undefined-target contract for its other
callers. Creation failure selects zero, preserves the failed target and retains
already-applied source flags. Destination actions refer to actual fallback zero,
never the requested absent target or old source. The private service result is
consumed by the void command/macro boundary as an optional nonfatal diagnostic.

The adopted `issue-54-switch-port-precedence.md` supplies handler 0x468eb,
protection 0x3378a, selector 0x3393c, constructor 0x3326f and native table evidence.
The new focused fixture reproduces all eight normative examples, including a
protected-but-undefined target for the failure branch. A test-only implementation
file creates that raw state; no production setter is exposed, and no claim is
made that ordinary protection commands can create it. All source/target indices,
low flag combinations and valid initial protection combinations are checked for
screen/offscreen/ordinary-absent/stale-protected targets on every canvas, with
all unrelated port records compared after every command. This is 235216 cases
per canvas, 705648 total. Port zero starts with
an inactive sub-viewport and nondefault image state, while a protected ENV has
unrelated references and WORLD size 1×1, distinguishing fresh physical defaults
from copied live state. Additional tests exhaust both two-digit flag domains,
including all higher values in base36 and base64 (32352 additional cases),
same-port precedence, zero
immunity and invalid-target recovery. Independent coherent all-canvas captures
check drawing through existing screen/offscreen and newly created screen ports,
retained inactive geometry, selection and byte-split recovery.

Cursor/style/device binding, disabled-graphics processing and active application
routing remain separate implementation work. The ledger keeps the combined
disabled-graphics requirement pending even though its nontransactional state
outcomes are tested. This milestone does not claim complete context integration.
The command API comments also reflect the already-reconciled `2C`/`2W` geometry;
their executable paths are unchanged by this milestone.

## New DEFINE_PORT documentation discrepancy

Bounded read-only review of the unchanged original DLL identified a distinct
DEFINE_PORT gap, filed as [#72](https://github.com/bbs-land/remote-imaging-protocol/issues/72)
after checking related issues #10/#16/#54 and the issue list. No existing issue
was edited. The pin remains `5ae9d165b1ac3e36161847e68db0d763228b779b`; open PR #71's
head was still `61b8fedfe9d3a12442f65a73089a4f008037c519` and was not adopted.
The artifact SHA-256 remains
`83d71b358492b15bcfd3773c30a52784881a3a49a5b6e9029d986b3cba1fd5e8`.

The static trace finds only bit 1 passed to initialization and bit 2 used for
selection, despite the draft's promised bit-4 deactivation and bit-8 protection.
The handler computes low-WORD endpoint differences before an offscreen origin
is zeroed, conflicting with an interpretation that nonzero near coordinates are
ignored before sizing. The protected-redefinition branch appears to diagnose
without selecting zero, and unprotected replacement deletes the old entry before
allocation with a separately restored current index on failure. Issue #72 gives
exact RVAs and requests observable native state outcomes. These are static
findings, not a newly executed original-client outcome matrix. The four directly
affected source blocks are now blocked on that issue. Ordinary extent answers
in #10 and all of #21 remain documented; no additional content is requested on
those issues. DEFINE_PORT and raw viewport producer reconciliation remain
unfinished rather than being claimed by passing historical tests.

The existing native FreeBSD Release CMake configure/build with `/usr/bin/cc`
passes without new compiler warnings. All 99 selected tests pass (26.67 seconds).
After removing success-only fixture logging, the five focused selection/table/
preservation tests pass again (0.83 seconds). Final Valgrind reports zero errors,
no retained heap, 470480 allocations/frees and three inherited descriptors for
the expanded switch fixture, without suppressed contexts. All 93 frozen files
and 41 helpers remain unchanged. Source/pin/adoption identities and the RIP1.54
capture inventory are intact; only switch/DEFINE_PORT review metadata and the
new B72 blocker changed. The 7353-block queue now has 138 implemented, 1609
pending, 4663 unreviewed, 898 contextual, 24 excluded and 21 blocked entries.
Owned temporary request/log directories were removed. Unrelated concurrent CI
edits were left untouched. No active application route or original artifact was
modified, and no changes were pushed.

## Viewport DEVICE producer and physical raster clipping

Wire `v` now normalizes both WORLD axes, projects once and checks the zero
sentinel after conversion. Converted all-zero input deactivates while retaining
viewport geometry and image state. A nonzero replacement reactivates. The new
DEVICE setter stores the supplied inclusive rectangle without adding a screen
port's origin or intersecting with declared port bounds. The table represents
that same rectangle relative to its bounds, allowing negative relative origins;
conversion back to DEVICE recovers the original stored coordinates exactly.
The existing bounded device-fixture setter keeps its separate contract.

This follows the adopted issue-11 viewport producer and original handler
0x20f87/helper 0x33cc6: normalization, far-boundary increment and direct storage
at record+0x3a. Internal rectangles already use inclusive last pixels, so their
representation needs no second far increment. The independent fixture converts
WORLD (80,80)–(118,118) at 2:1 to DEVICE [40,40,60,60), then plots at (41,41).
It fails against the former port-relative producer, which incorrectly adds the
port's (25,25) and plots at (66,66). The original DLL identity is unchanged;
read-only instructions were inspected, without Windows/GUI execution.

Stored geometry and visible pixels are separate. The port-info getter derives a
viewport-relative physical clip without changing stored origin or dimensions.
Stroke, fill, polygon, ellipse, rounded-rectangle, text and bitmap preparation
use that clip to bound work. JPEG fitting retains the original stored area and
samples from the original fitted rectangle while staging only visible output.
Port copies likewise retain their original fit/sample coordinates through
physical clipping; overlap snapshots contain only actual source pixels.
Physically absent source samples leave the old destination unchanged. Generic
surface clip-then-fit and unscaled blit APIs keep their existing policies.

Viewport-relative reads now reach the underlying screen/offscreen surface
without restricting them to declared port bounds. Active and stored-viewport
capture adapters share this reader, and `2W` screen crops use its viewport-relative
coordinates. Wallpaper snapshots use it too, with output limited to the physical
clip while retaining raw tile phase. A seed/capture crossing the physical source
still declines rather than shrinking requested dimensions or inventing padding.
Those consumer cases remain unfinished. Numeric query arithmetic now checks
exclusive-boundary/count narrowing; the adopted callback/query semantics remain
separate reconciliation work.

`rip3_viewport_commands` adds independent full-device captures on every canvas
for direct origins, negative port-relative origins, capture/copy/wallpaper reads
outside declared bounds, image placement, normalized corner orders, both radices,
all coordinate widths, zero deactivation and full-frame corpus input. Backup
restoration retains unclipped geometry, protection, activation and image flags.
Physical-edge fixtures distinguish original ten-pixel sampling from incorrectly
refitting to two visible pixels, under all five indexed transfer modes and both
image painters. Other fixtures cover wholly external active viewports, missing
physical source samples, overlapping-copy allocation failure/recovery, enormous
stored rectangles with only screen-sized staging, and nonfatal image allocation
failure. Pixel/palette expectations are authored geometry tables, not generated
from the implementation. Existing viewport scene expectations now use direct
DEVICE origins and converted-zero deactivation; RIP1.54 expectations are intact.

The current stored viewport plane remains int32_t. Larger legal WORLD
projections reject nonfatally before mutation instead of wrapping or saturating
the stored rectangle. Complete wide records, physical-outside capture/tile-source
policy, POINT/PIXEL-specific wire behavior and scene/application integration
remain pending. The ledger keeps those combined requirements pending rather
than claiming full viewport compliance. This milestone still uses pin
`5ae9d165b1ac3e36161847e68db0d763228b779b`; the newly supplied PR #73 is reviewed
separately for adoption and does not silently change this verification baseline.

Native FreeBSD Release CMake configure/build with `/usr/bin/cc` passes without
new warnings; all 100 selected tests pass (33.72 seconds). The expanded viewport
fixture passes again after additional transfer-mode checks (0.28 seconds for its
focused run). Valgrind reports zero errors, no retained heap and three inherited
descriptors for viewport/copy command fixtures; allocation/free counts are
156/156 and 531/531. All 93 frozen files and 41 helpers remain unchanged, as do
source/pin/adoption identities and the RIP1.54 capture inventory. Only viewport
review metadata changed: 147 implemented, 1603 pending, 4660 unreviewed,
898 contextual, 24 excluded and 21 blocked entries among 7353 source blocks.
Owned temporary logs were removed. No application route or original artifact
was changed, no existing issue was edited, and no changes were pushed.

## DEFINE_PORT documentation adoption: PR #73

PR #73 at `ea8eacdf83593d0e5ccdb706a9c065ffca2cda9c` resolves documentation
blocker #72. The protocol contract takes precedence over the conflicting DLL
findings recorded above. Flags 1/2/4/8 are independent; offscreen near coordinates
are ignored before sizing; protected redefinition preserves the port and selects
zero. Resource-failure rollback, current port and clipboard final state are
unspecified, leaving client policy rather than a required DLL defect.

The direct comparison with the previous pin changes only DEFINE_PORT semantics;
six other normative pages have independently checked formatting-only changes.
All 65 document hashes and 7357 source blocks were reproduced from the pinned
archive. Existing exact/formatting-only evidence is retained; DEFINE_PORT work
remains pending. The queue contains 147 implemented, 1613 pending, 4658 unreviewed,
898 contextual, 24 excluded and 17 blocked entries. B72 is resolved locally;
GitHub issue content is unchanged. RIP1.54's 93 files and 41 helpers still match
the original baseline. Verification and adoption provenance are recorded in
`reference-review.json`; owned temporary archives were removed.

Native FreeBSD Release CMake configure/build with `/usr/bin/cc` passes without
compiler warnings. All 100 selected tests pass (32.35 seconds), including the
source/adoption guards; the five focused guard tests also pass independently.
No application route, frozen RIP1.54 data or original artifact changed.

## Stable reference branch

The user designated `RealDeuce/remote-imaging-protocol:stable` as the consolidated
reference source and retained the parent repository for issues. Its exact head
`c9a66b27edc93d4522b21510c1379eb10230f85d` has the same 65 normative documents and
7357 source blocks as the reviewed PR #73 pin. Thirty-four derivations and both
original protocol texts are unchanged; the #72 derivation changes only a link
checker total in its delivery status. No requirement or review state changes.
The adoption record retains both repository identities and exact archive hash.
The guard now rejects changes to the source repository, reference branch or
parent issue destination, and normal reindexing preserves those distinctions.
Temporary source archives are removed after the independently checked reindex.
Native Release CMake configure/build passes without compiler warnings. The
source freeze, contract and preservation-guard CTests pass, including six focused
Python guard cases. All 93 frozen files and 41 legacy helpers are unchanged.

## DEFINE_PORT protocol implementation

Wire `2P` now follows the stable branch's protocol contract for screen/offscreen
definitions. It projects extent boundaries once, converts to the device table's
inclusive last pixels, and zeroes offscreen near coordinates before sizing.
The canonical 1280×960 world request allocates 640×480 on that device, with no
extra edge. Larger offscreen allocations are not clamped to the display.
Flags 1/2/4/8 establish storage, selection, viewport activity and protection
independently. Protected redefinition retains the target and selects port zero
before resource admission. Flag 1 does not assign or clear the clipboard pointer.

Explicit client policy rejects reserved flags, nonpositive projected dimensions,
out-of-range allocations and wholly external screen definitions. Partial screen
definitions intersect the display. Offscreen allocation uses the supplied pixel
budget, counting the final table without the replaced target. A replacement is
staged before committing; failed admission/allocation retains old geometry,
pixels, viewport/image state, selection and clipboard assignment. This is a
client recovery policy, not a documented protocol rollback guarantee. Allocation
does not use the conflicting DLL's low-WORD wrapping or dangling current index.
Public command/stream parsers remain void and consume every failure internally.

`rip3_define_commands` supplies independent full-device captures across all three
canvases, both radices, coordinate widths 2–5 and all 16 flag combinations. It
checks exact edge pixels, inactive/protected initial state, offscreen visibility
through explicit copies, replacement defaults, ignored large origins, reserved
field shortening, odd projection, display clipping and 65,536-pixel width without
wrapping. Protected fallback, missing/exhausted budgets, both surface allocation
failures and byte-split recovery have explicit state expectations. The fixture
first failed against the old extra-pixel screen producer. Existing command tests
now distinguish half-open port bounds from inclusive viewport coordinates.
PORTW/PORTH's defective shipped callbacks, complete scene/context behavior and
active application integration remain separate pending work.

Native FreeBSD Release CMake configure/build with `/usr/bin/cc` passes without
compiler warnings. All 101 selected RIP3/preservation/JPEG/shared-audio/codepage
tests pass (33.25 seconds), followed by all 26 unchanged native/application RIP
and graphics-scrollback expectations (104.89 seconds), with interactive display
variables cleared and application audio disabled. Valgrind reports zero errors,
no retained heap, 1595 allocations/frees and only three inherited descriptors for
the new fixture. No GUI, physical audio or Windows execution is claimed for the
separate engine. All 93 frozen files and 41 legacy helpers remain unchanged.
The ledger reconciles 22 DEFINE_PORT units: 169 implemented, 1591 pending,
4658 unreviewed, 898 contextual, 24 excluded and 17 blocked out of 7357 blocks.
The stable pin, adoption identities, RIP1.54 capture inventory, original artifact
and active application route are unchanged. Temporary logs were removed; no
existing issue was modified and no changes were pushed.

## Port query grammar and numeric correction

PORTW/PORTH and PORTX0/Y0/X1/Y1 consistently accept the shared decimal selector,
PORT/VIEW type and WORLD/DEVICE domain grammar. Only trailing fields are optional;
a domain in the second position rejects as invalid rather than remaining an
unresolved shorthand. Leading zeroes and case-insensitive keywords work under
base64 too; numeric selectors remain decimal. Syntax errors remain inside the
void reference processor and do not latch a session failure.

These queries deliberately implement the documented intended numeric semantics,
correcting RIPtel 3.1's all-six inverted-success callbacks. The stable reference's
full-world query section explicitly permits identifying such a correction. Its
original expression-preserving failures remain recorded as historical behavior;
the new engine does not claim to reproduce them. Counts and exclusive ends stay
wide through signed world conversion: a stored INT32_MAX last pixel can report
2147483648 device pixels or a larger world count. Negative port-relative origins
retain floor rounding. Undefined/inactive targets retain the intended 0/-1
sentinels; protection does not block reads.

`rip3_port_query` independently checks 44,928 name/selector/case/optional-prefix
combinations against literal geometry tables, with base64, current slots 0/1/35,
every decimal slot, CUR/cur and leading-zero selectors. Existing all-canvas
captures verify unchanged pixels/state alongside replies, live ENV/frame
precedence and nonfatal malformed CP437/UTF-8 references. Additional literals
cover full 2^31 dimensions, large world results and negative fractional origins.
Ordinary macro expansion and empty-comma collapse remain pending in their
combined requirement; this API accepts already expanded references.

Native FreeBSD Release CMake configure/build with `/usr/bin/cc` passes without
compiler warnings. All 101 selected RIP3/preservation/JPEG/shared-audio/codepage
tests pass (33.72 seconds). All 93 frozen files and 41 legacy helpers match the
original baseline. Source/adoption identities, historical RIP1.54 captures and
the active application route are unchanged. Verification logs were removed.

## New PIXEL/POINT write-mode question

New parent issue [#74](https://github.com/bbs-land/remote-imaging-protocol/issues/74)
asks for the effective X/j mode matrix. The stable entries list Write Mode, while
read-only original instructions call SetPixel (`0x1e2a8`) and a solid-brush
FillRect (`0x1e405`). Their API contracts suggest replacement, but that inference
does not establish native RIPtel outcomes or override protocol intent. The issue
references the resolved coordinate/thickness work in #11 / PR #39 and the distinct
filled-rectangle work in #33 / PR #63. No existing issue was modified. The two
attribute requirements are blocked on this mode decision; the resolved placement
and width contracts can proceed. The original artifact's SHA-256 still matches
`83d71b358492b15bcfd3773c30a52784881a3a49a5b6e9029d986b3cba1fd5e8`.
Only bounded static disassembly was performed; no original DLL instructions ran.
The ledger now has 210 implemented, 1558 pending, 4631 unreviewed, 915 contextual,
24 excluded and 19 blocked units among 7357 source blocks. Historical defective
query replies are explicitly contextual; the combined macro-collapse requirement
remains pending. The new mode question blocks only the two combined X/j attribute
requirements. No changes were pushed.

## LINE_STYLE wire units and byte truncation

The separate parser implements `=` with fixed 1/1/4/2 fields, independent of
XY width. Flags 0/1 take device thickness, while 2/3 convert using only X scale.
The decoded width is clamped to at least one before conversion and again after
conversion, then truncated to the low byte. The device pen stores an effective
pixel width, so cosmetic byte zero becomes width one; this also preserves
POINT's radius-zero meaning without changing the positive-width kernel API.
Only the entire flag value 1 enables background off pixels; 3 does not.

Stock masks use the existing documented patterns; a custom pattern must have
on bits and fit 16 bits. Stock selections ignore user_pat. Field/range errors
and protected STYLE edits retain the complete old record; attribute configuration
does not require a PORT binding. Public parser entry points remain void.

Independent literal tables exercise 6000 flag/width cases across three canvases,
two radices, XY widths 2–5 and five uniform/nonuniform frame profiles. Boundaries
include decoded zero, 255/256/257, 511/512 and both two-digit maxima. Other cases
cover stock/custom masks, ignored user patterns, MegaNum dash zeroes, final-field
shortening, malformed input, missing bindings and protection. Existing all-canvas
coherent stroke captures now obtain the custom pattern/background flag through
byte-split wire `=`; the thick-line fixture also uses the wire command. The
earlier parser failed the new expectations as unsupported before implementation.
Exact GDI edge/cap reproduction remains outside the target. Header/reset defaults
and the separate X/j write-mode question #74 remain unfinished.

Native FreeBSD Release CMake configure/build with `/usr/bin/cc` passes without
compiler warnings; all 101 selected tests pass (33.34 seconds). The ledger now
has 236 implemented, 1544 pending, 4616 unreviewed, 918 contextual, 24 excluded
and 19 blocked units among 7357 source blocks. Twenty-six line-style units are
reconciled; the combined reset requirement remains pending. All 93 frozen files
and 41 legacy helpers still match the original baseline, and no RIP1.54 capture,
active application route, source pin or adoption identity changed. Temporary
verification logs were removed. No additional issue content or code was published.

## PIXEL/POINT coordinate and square consumers

The separate parser recognizes `X` and `j` and implements their resolved COPY
geometry. Both project each WORLD axis once. X attempts one context-absolute
device pixel, retaining the documented omitted viewport origin; clipping still
uses the stored viewport. j offsets by that origin once and fills an odd solid
square with radius floor(effective thickness/2), ignoring line/fill patterns.
The default screen-subport viewport already includes its screen origin; a later
wire `v` replaces it with directly converted DEVICE coordinates. No second port
origin is added. Offscreen drawing uses its own bitmap.

Projection, origin subtraction and square expansion remain wide until clipped
to the viewport's derived physical intersection. Huge legal input cannot wrap or
saturate into a visible pixel, and huge stored viewports do not allocate staging
storage for these primitives. Protected port/style state permits drawing while
retaining configuration. Inactive or wholly invisible operations are no-ops.
Visible non-COPY operations remain explicitly unsupported pending #74; the two
attribute requirements and complete command-inventory claims remain blocked or
pending. This milestone does not invent a mode policy or claim full X/j support.

`rip3_point_commands` authors coherent complete-device expectations for every
canvas, including an offscreen scene copied into the same final capture. Cases
distinguish default and replaced origins, clipped unoffset X attempts, stored
viewports outside declared port bounds, near/far viewport clipping, solid even/
odd thickness squares, byte wrapping and one-pixel X coverage. A further 192
captures span both radices, XY widths 2–5 and eight thickness boundary values.
Independent compatibility/nonuniform mappings, physical-edge/INT32_MAX viewport
cases, huge coordinates, RGB/indexed ink, protected references, missing bindings,
malformed CP437/UTF-8 input and valid recovery are covered. The parser initially
failed these expectations as unsupported before implementation. Public process
entry points remain void; all rejected modes and malformed fields are handled
internally. Active application routing and complete scene/context remain pending.

Native FreeBSD Release CMake configure/build with `/usr/bin/cc` passes without
compiler warnings. All 102 selected RIP3/preservation/JPEG/shared-audio/codepage
tests pass (35.31 seconds); the expanded point fixture passes again after adding
the compatibility/nonuniform cases (0.44 seconds). Valgrind reports zero errors,
no retained heap, 510 allocations/frees and three inherited descriptors. The
ledger reconciles 17 geometry units and retains the mode blocker: 253 implemented,
1537 pending, 4604 unreviewed, 920 contextual, 24 excluded and 19 blocked among
7357 source blocks. All 93 frozen files and 41 legacy helpers remain unchanged,
as do the historical RIP1.54 capture inventory, stable pin/adoption identities,
original artifact and active application route. Temporary logs were removed.
No existing issue was modified and no changes were pushed.

## Canonical fill masks and drawing-context phase

`S` now accepts every preset 00..0B using the stable reference's canonical eight
row bytes. The formerly unsupported 02/04/05/07/0B masks are implemented, including
04's corrected 0E row and 0B's 22 rows. `S` and `s` still replace only the mask
and fill ink; invalid/protected edits retain the record, and border state remains
independent. Palette/direct-RGB decoding and current background selection are
preserved.

Wire BAR, filled-rectangle, polygon and filled-circle/oval consumers now derive
phase from the selected drawing context's (0,0) for each paint. The mask keeps
its eight-device-pixel period after world conversion. Port/viewport origins,
primitive starts and clipping do not restart it. Screen ports share screen phase;
each bitmap has local phase. Copies move the painted pixels without re-tiling.
The helper converts this phase into the device kernel's viewport-relative
representation, with unsigned residues for negative/extreme relative origins.
It does not mutate STYLE metadata or change the explicit device-kernel phase API.
The legacy RIP_FILL solid exception and unsupported primitive/header/context
integration remain separate work.

`rip3_pattern_commands` compares independent literal pictures for every preset
under all five indexed modes and RGB COPY. Its 96 matrix captures span three
canvases, both numeric bases, XY widths 2–5, two world scales and screen/offscreen
contexts, including negative port-relative viewport origins. Additional coherent
consumer captures cover the exact reference phase example, adjacent bars,
rectangle/polygon/ellipse masks and a viewport change without another `s` command.
The bitmap copy lands at a different destination phase, retaining source samples.
Unrelated drawing attributes and caller-owned kernel phase remain unchanged.
Existing diagonal/chequerboard expectations now use authored context coordinates
instead of the old caller-relative phase. RIP1.54 expectations are untouched.

The new phase fixture failed against the old implementation before the fix.
Native configure/build and the shared-tree functional tests passed, but its two
preservation checks detected concurrent edits to frozen `rip_text_font` in
`src/terminal/cterm/st_cterm.c`, outside this milestone. Those edits, their header
and test changes were left untouched. Verification therefore used an isolated
snapshot of `5398e67e` plus only this milestone, in a scoped temporary directory.
The same native FreeBSD Release compiler/configuration was used; no alternate
toolchain, GUI, physical audio or Windows execution was added.

The isolated configure/build passes without warnings, all 103 selected tests
pass (22.47 seconds), and preservation confirms all 93 files and 41 helpers match
the original baseline. Valgrind reports zero errors, no retained heap, 273
allocations/frees and three inherited descriptors for the expanded pattern
fixture. Its temporary source, build and logs were removed. The ledger reconciles
64 fill units: 317 implemented, 1491 pending, 4580 unreviewed, 926 contextual,
24 excluded and 19 blocked among 7357 source blocks. Pin/adoption identities,
capture inventory, original artifact and the committed active application route
are unchanged. No issue content or code was published. Concurrent terminal,
freeze-manifest and supplemental-capture updates remain outside this milestone;
its commit retains the original guard, hashes and preservation baseline. The
independent maintenance exception is outside this milestone's acceptance claim.

## Axis-aligned A/V/O consumer reconciliation

The stable reference's arc consumer contract supersedes the earlier restricted
0..360/equal-angle policy described above. The separate engine now accepts all
valid two-digit angle pairs for A/V, reduces each endpoint independently and
uses the original five-degree integer sine table after device conversion.
A uses its horizontal circular radius; V uses the larger converted radius for
its endpoint rays, then intersects those rays with the actual ellipse.

Identical integer endpoints produce a solid geometric pen point at the raw
endpoint, before ellipse projection. Thus A/V 0..360 and 450..810 are point
strokes rather than full curves. Different integer endpoints on the same ray
produce a complete curve: radius-three 0..1 and 0..719 exercise that distinction.
Other distinct rays advance counterclockwise through the appropriate radial
ellipse intersections. O consumes and validates its angle fields but ignores
their values and always requests the complete ellipse. These commands ignore
line patterns/background painting while retaining thickness and Write Mode.

The narrow `st_rip3_arc_axis` adapter retains wide signed integer rays, compares
primitive directions without overflowing cross products, and converts radial
bearings to ellipse parameters. Its cross/dot angle calculation preserves tiny
positive spans on thin ellipses. The existing geometric ellipse kernel's API
and zero-sweep policy are unchanged. A single center-collapsed ray is an empty
client degeneracy; two identical collapsed rays produce a center point. Singular
ellipses use ordinary ray bearings and the existing degenerate curve kernel.
These explicit geometric policies do not claim original GDI caps, singular API
behavior or exact pixel reproduction.

`rip3_arc` checks literal rays across quadrants, turns, table quantization,
noncircular parameter conversion, zero radii, wide bounds and tiny spans.
`rip3_curve_commands` adds 96 coherent screen/offscreen matrix captures across
three canvases, both bases, XY widths 2–5 and two unequal WORLD scales. Each
capture combines point/full/partial/O cases under five indexed modes plus RGB
COPY, short final fields and deliberately gapped pens. Additional checks retain
all earlier complete-figure coverage, exercise partial A/V grammar, odd/even
point thickness, invisible wide endpoints, missing services, malformed recovery,
inactive viewports and allocation failure before drawing. Public parser entry
points remain void; diagnostics never propagate a session-ending parse error.

Native FreeBSD Release configure/build passes without new warnings. All 97
RIP3/preservation CTests pass (33.24 seconds), and Valgrind reports no errors or
leaks for the expanded curve fixture. The earlier 29-test legacy/preservation
run is recorded above. This milestone changes no legacy source, capture,
preservation pin, reference revision or active application routing. Scoped
build logs were removed. No upstream issue content or code was published.
The ledger reviews 73 units: 346 implemented, 1504 pending, 4526 unreviewed,
938 contextual, 24 excluded and 19 blocked among 7357 blocks. Shared requirements
covering I/i/_, skewed arcs and other unimplemented consumers remain pending;
full RIP3 compliance and active-engine integration are not claimed.

## Axis-aligned filled pie/chord consumers

The separate engine now dispatches I/i/_ through the same fixed-width parser,
WORLD projection and integer-ray adapter as A/V. I uses its horizontal circular
radius; i/_ use independent ellipse radii. Distinct endpoints form a PIE sector
or CHORD segment from radial ellipse intersections. Equal computed endpoints
produce a raw center-to-endpoint line for I/i, even with borders disabled, and
no drawing for _. A quantized full Pie from different endpoints on the same
ray retains its internal border radius; a full Chord has no internal chord.
Raw full turns still take the earlier equality branch rather than becoming a
filled ellipse. The generic device ellipse API's topology remains unchanged.

The positive geometric cases are implemented with a solid logical pen:
unbordered interiors use the current fill pattern/context phase and all five
write modes; COPY borders use draw color/thickness and the curved/closing edges.
The equal-endpoint radial fallback uses the current write mode and does not
consume fill colors or N. Full-Pie radius scratch and ellipse scratch are both
prepared before any pixels change. Clipped geometry remains wide through the
existing stroke collector. All failures stay within the void public parser.

A new documentation gap is tracked in
[#75](https://github.com/bbs-land/remote-imaging-protocol/issues/75), separately
referencing #14/PR #43, #33/PR #63 and #74. The angle contract is adopted and is
not reopened. The gap concerns the original logical pen, ROP, background flag
and fill/outline passes: the inherited COPY-border rule and solid-curve prose
do not settle the observed I/i paths. Read-only inspection of the unchanged
original DLL identifies current STYLE ROP setup at RVA `0xe6e9`, current-pen
selection at `0x13cfc`, and single/two-Pie paths at `0x119e3`/`0x11a5d` and
`0x12492`/`0x12510`. The published arc probe substitutes that boundary. The issue
asks for explicit pass/operand semantics and a small independent pixel matrix;
this milestone makes no Windows-raster claim. Patterned I/i/_ pens and
non-COPY sector borders remain unsupported with nonfatal diagnostics, rather
than implementing a guessed contract. Existing G/o behavior is not reconciled
by this milestone. The reference pin remains `c9a66b27`; the fork's stable head
was checked and still matches it. No existing issue received a comment.

`rip3_sector_commands` failed against the prior implementation before the
handlers were added. Its 96 coherent full-device matrix captures combine
circular/elliptical sectors, minor/major chords, wrapped halves, raw equal/full
turns, coincident-ray full figures and a radial endpoint outside a narrow ellipse.
They cover three canvases, screen/offscreen contexts, both bases, XY widths 2–5,
two unequal WORLD scales, short final fields, context-phased custom fill,
five unbordered indexed modes, RGB COPY and solid COPY borders. A shifted bitmap
copy preserves source fill phase. Focused checks cover malformed/split input,
missing services, inactive ports, unsupported #75 combinations followed by valid
commands, unused ink/N/scratch on equal chords, raw wide radial clipping and
each radial/ellipse allocation failure before painting.

Native FreeBSD Release configure/build passes without new warnings. All 98
RIP3/preservation CTests pass (34.67 seconds). Valgrind reports zero errors,
6,799 allocations/frees, zero retained heap and three inherited descriptors for
the expanded fixture. Owned temporary issue/build logs are removed. All 93
recorded file pins and 41 helper pins match, including the independently
recorded font-maintenance exceptions; this milestone changes no RIP1.54 source,
historical expectations, preservation manifest or active application routing.
The ledger reviews 51 source blocks and adds B75: 369 implemented, 1490 pending,
4509 unreviewed, 938 contextual, 24 excluded and 27 blocked among 7357 blocks.
Complete filled-curve style/mode coverage, skewed curves and active-engine
integration remain pending. No code or PR was published.

## Original integer Bézier consumers

The earlier nearest-rounded, N+1-point prerequisite kernel above has been
replaced by the adopted #15/PR #44 numerical and wire contract. Its former API
had no application consumers. The separate engine now dispatches Z/z/t/x and
shares the original fixed-point generator and typed-record expansion.

Controls are converted to integer device coordinates before sampling. The
sampler uses eight fractional bits, explicit modulo-32-bit subtraction/products
and signed truncating division at each interpolation level, then floors and
narrows interior outputs to signed16. P0 retains its input integer and P3 narrows
to signed16. N>=2 submits P0, samples i/N for 1..N-2, then P3: the penultimate
sample is deliberately absent. Standalone Z accepts 0/1 as non-drawing counts
and retains the full two-digit range through 4095 without poly reduction.

The z/t/x parser consumes only num/count headers and the seven literal type
digits. Point records contribute their unsigned16 first pair; cubic records
retain converted P0/P1/P2 and take unsigned16 P3 from the next record's first
pair. Closed z/x permit a terminal cubic returning to the first record; open t
rejects it before any drawing. N=1 contributes only the cubic's P0, and repeated
join endpoints are preserved. The independent 8193-number collection limit
and common-N reduction to at most 4094 submitted points are both enforced.
Paths with fewer than two submitted points draw nothing, matching both native
APIs' minimum. The adapter uses wide WORLD division followed by explicit
device-word conversion; it does not claim native multiplication-overflow parity
before that division.

Unfilled Z/t/z preserve line pattern, thickness, background-gap policy and Write
Mode across the generated path. Filled x uses the existing even-odd polygon
kernel and drawing-context fill phase, with optional patterned COPY borders as
specified by x's own entry. All parse, expansion and paint failures are consumed
inside the void public parser; malformed/oversized vectors cannot paint a
prefix. The final choice of original silent common-consumer errors versus the
num=0 message remains pending with diagnostic/UI integration. Existing GDI
edge/cap and repeated-XOR pixel defects are outside the reproduction target.

`rip3_bezier` has independent literal fixed-point examples, negative floors,
signed/unsigned narrowing, wraparound discontinuities, N=0/1/2/4095, terminal
closure, duplicate joins and exact input/generated budget boundaries. Literal
complete original z/t vectors submit 203/81 points. `rip3_bezier_commands` adds
96 coherent full-device screen/offscreen captures over all canvases, both
bases, XY widths 2–5, two fractional unequal WORLD mappings, five indexed modes
and RGB COPY. The independent arch/triangle grids distinguish N=2 from N=4 and
conversion-before-sampling. Tests also cover all type digits, short last fields,
filled mode/border differences, patterned phase and COPY gaps, full counts,
malformed/terminal/overflow rejection, missing services, inactive ports,
nonfatal split-input recovery, and each owned allocation before painting.

Native FreeBSD Release configure/build and the 99-test RIP3/preservation run
pass. Valgrind reports zero errors, no retained heap, 19,341 allocations/frees
and three inherited descriptors for the expanded wire fixture. A subsequent
correction enforces Polygon's two-point minimum as well as Polyline's; only the
affected wire test was rebuilt/rerun after adding its focused no-ink/no-N cases.
The broad suite and Valgrind were not repeated for that correction. Scoped
verification logs were removed. All 93 recorded file pins and 41 helper pins
matched; no RIP1.54 source, historical expectation, manifest, stable reference
or active application route changes in this milestone. The ledger reviews 98
blocks: 440 implemented, 1482 pending, 4425 unreviewed, 959 contextual, 24 excluded
and 27 blocked among 7357 units. No issue content or code was published.

## Skewed ellipse contours and wire handlers

The separate engine now dispatches &/-/]/+/[ with their distinct field layouts.
& keeps two-digit radii independently of XY width; the other commands use XY
radii. Rotation/start/end remain two digits in current base math, with only the
last rotation field eligible for shortening. Center and radii undergo WORLD
conversion before rotation, preserving wide geometry until physical clipping.

`st_rip3_skew_span` implements the adopted order-dependent normalizer, including
0..719 retaining two traversals and 0..720 reducing to one, displaced equal
450..450 becoming a full turn, and 360..0 becoming zero span. The owned contour
preserves every integer-degree sample and duplicate: ] supplies S..E, + appends
the first point, [ appends center then first point, and &/- supply exactly 360
points with polygon closure. A zero-span ] draws nothing; zero-span filled
contours retain their documented degeneracy rather than sharing an empty rule.
Full [ retains its internal radius, and double-traversal fills retain even-odd
cancellation instead of becoming a single ellipse.

Read-only original-generator inspection establishes two Q14 floor stages,
parameter sampling then counterclockwise rotation. The embedded 91-value quarter
table plus three asymmetries reconstruct both original 360-word tables exactly:
sine RVA `0x7b638` / file offset `0x79c38`, SHA256
`a08fb7e140b46078a0992959300412410fb2caa53f3f1c689dfcebdf5d4132fc`;
cosine RVA `0x7b098` / file offset `0x79698`, SHA256
`f4bde71f06d692151350f9837c06b7f12de444cccdd1d49cec9dd35db3beee72`.
The implementation uses wide integer products rather than native overflowing
32-bit products. This retains valid maximum wire geometry without wrapping
far ellipses into the visible viewport. The existing v3 stroke/polygon coordinate
limits suffice; no shared kernel limits were changed.

[#76](https://github.com/bbs-land/remote-imaging-protocol/issues/76) requests the
missing normative numeric/conversion and effective pen/background/ROP/pass
contract, separately referencing #14/PR #43 and #75. The normalizer and contour
answers remain adopted. This milestone supports single-turn patterned outlines
in all five modes, unbordered fills in all five modes, and solid COPY borders.
Multiple-turn XOR/NOT outlines and other filled-border combinations remain
unsupported with parser-owned diagnostics while #76 is unresolved. Exact GDI
edge/cap pixels and native arithmetic-overflow reproduction are not claimed.

`rip3_skew` checks literal normalization, table/asymmetry coordinates, rotated
points, counts, closures and repeated samples. `rip3_skew_commands` has 96
coherent full-device matrix captures over three canvases, both bases, XY widths
2–5, two unequal WORLD scales, screen/offscreen contexts, five indexed modes,
RGB COPY, short final fields and context-phased custom fills. Expected grids
come independently from original table data, unit-pen capsule inequalities and
rational pixel-center even-odd tests, not from SyncTERM output. Copy destinations
change checkerboard parity, proving source-phase preservation. Focused cases
cover malformed/unsupported recovery, zero spans, continuing patterned phase,
inactive/missing services, unused inks, each allocation before painting and
maximum projected ellipses remaining clipped away.

Native FreeBSD Release CMake configure/build passes without new warnings.
Only the two new skew CTests and the source/ledger audit were run for final
verification; no broad regression or additional Valgrind run was added. All
recorded 93 file pins and 41 helper pins match, including the separately recorded
font-maintenance exceptions. No RIP1.54 source, expectation, preservation pin,
reference revision or active application routing changes. Owned temporary issue
and build files were removed; unrelated concurrently created documentation was
left untouched. The ledger reviews 81 units and adds B76: 492 implemented,
1441 pending, 4393 unreviewed, 973 contextual, 24 excluded and 34 blocked among
7357 blocks. Complete skewed pen/mode acceptance and engine integration remain
pending. No existing issue received comments and no code was published.

## Filled-rectangle logical pen and pass reconciliation

K now follows the adopted #33/PR #63 exception rather than the earlier solid
COPY-border prerequisite described above. STYLE retains the original selected
line type 0–4 alongside the resolved ordinary/custom mask. The `=` decoder
updates both atomically; style validation, selection/copy/protection and owned
backup/restore preserve that type. The stored custom bits remain available to
other drawing consumers and are not rewritten by K.

K maps the selected type to SOLID/DOT/DASHDOT/DASH/DASHDOTDOT, with custom type
4 selecting fixed DASHDOTDOT rather than its arbitrary mask. Effective widths
above one select a solid pen; cosmetic byte-zero width already resolves to one
at the existing wire boundary. Geometric dash coverage uses explicit repeating
runs (`#`, `#.`, `###.#...`, `####....`, `###.#.#.`); exact GDI dash lengths,
caps and edge-pixel reproduction are outside the target. Both brush and border
use the current Write Mode. NOT therefore requires no palette/color lookup for
either operand, including opaque pen gaps.

Flags exactly 1 take the combined brush/opaque-pen path: prepared pen coverage
is excluded from the interior fill, then the pen applies the operation once.
Flags 0/2/3 fill first and then draw the current pen with transparent gaps,
retaining the two sequential operations where fill and pen overlap. All used
inks and owned coverage are prepared before painting. Border-free degenerates
remain successful no-ops, and normalized/wide endpoints and context fill phase
retain their existing geometry. No parser error propagates to the session.
The standalone device filled-rectangle kernel keeps its explicit solid COPY
border API; wire K now resolves its different policy before using that kernel's
fill-only path.

`rip3_rectangle_commands` combines five logical types, four flags, widths one
and three, both border states and five indexed modes plus RGB COPY in each
coherent scene. Its 96 full-device captures span all canvases, both bases, XY
widths 2–5, unequal WORLD scales, reversed/shortened fields and screen/offscreen
contexts. Expected pixels come from independent perimeter/run descriptions and
Boolean-operation algebra, with explicit single/two-pass overlap operands.
Shifted copies preserve the source checkerboard phase. Focused cases distinguish
custom masks 1/65535 from K's fixed type-4 pen, verify NOT with absent palettes,
allocation failure before fill, unused degenerate inks, and malformed recovery.
Existing fill/RGB expectations now use current-mode borders, including two NOT
operations restoring a region. Style tests cover all type values through copies
and immutable snapshots and reject invalid metadata without mutation.

Native FreeBSD Release CMake configure/build passes without warnings. The five
affected CTests pass: rectangle commands, fill commands, drawing commands,
pattern commands and styles (0.62 seconds total). No broad regression or extra
Valgrind run was added. The source/ledger audit matches all recorded 93 file
pins and 41 helper pins, including the separately recorded font-maintenance
exceptions. No RIP1.54 source/capture/preservation pin, reference revision or
active application route changes. Temporary build logs were removed and
unrelated concurrent documentation was left untouched. The ledger reconciles
16 units: 503 implemented, 1434 pending, 4388 unreviewed, 974 contextual,
24 excluded and 34 blocked among 7357 blocks. Other filled primitive exceptions,
bootstrap/N lifecycle and active-engine integration remain pending. No issue
content or code was published.

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
