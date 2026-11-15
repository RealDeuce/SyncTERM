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

The 26 existing native/application RIP and graphics-scrollback CTests passed
at the baseline on native FreeBSD with the existing Release CMake configuration.
They cover CP437/UTF-8, historical captures, both pixel phases, replies,
interaction and reset/variable/asset/transfer paths. This does not establish
new RIP3 coverage, physical audio or Windows execution. The same 26 unchanged
runtime expectations passed again after the initial v3 font/image/device-pixel
foundations, using the rebuilt native application and terminal-test binary.
The preserved source/data hashes and helper definitions still match the original
baseline.

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
  ordering and uncertain predefined variables.
- [#7](https://github.com/bbs-land/remote-imaging-protocol/issues/7): framing,
  escaping and identification.
- [#8](https://github.com/bbs-land/remote-imaging-protocol/issues/8): inherited
  BMM mask raster and compositing convention.
- [#9](https://github.com/bbs-land/remote-imaging-protocol/issues/9): conflicting
  RGB component example and incorrect C palette-index expression.
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

Continue unaffected work while issues remain open. File distinct additional
gaps as discovered, checking existing issues first. Issue reports do not
silently override the contract: adopt clarification through a reviewed
documentation revision and reconcile affected ledger entries/tests. Hypothesized
names alone need not be behavioral gaps. Do not guess, silently exclude
unresolved requirements or claim completion while blockers/reviews remain.

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

## Verified v3 foundations

`rip3_number` checks the independent MegaNum/UltraNum digit codec against the
reference's literal examples, all 64 digit values, base-sensitive letter case,
fixed-width encoding/padding, values above legacy WORD width, UINT64 boundaries,
invalid bytes/arguments and unchanged outputs on failure. The codec handles
explicit-radix digit spans, not command grammars: command-specific radix,
field shortening, signed interpretation and error recovery remain separate
requirements. The existing application RIP 3 path has not changed.

The eight original RFF families and RIPscrip.maf are bundled unchanged with
SHA-256/size pins. The build verifies them before generating C data in the
build tree. `rip3_font_assets` rejects both same-size modifications and
truncation before publishing generated output. Original notices appear in
the program's license/provenance list. No 1.54 font bank is replaced.

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
explicit start/sweep; border strokes, circle-aspect mapping and the unresolved
wire angle policy in #14 remain pending.

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
