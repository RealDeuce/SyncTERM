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
  escaping and identification.
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
for the new engine's asset/style registry. Classic text has device stroke rendering; font-style/layout integration and
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
bitmap-text kernel; font selection/justification, vector/outline text, cursor/
window state, UTF-8 transport conversion and wire orchestration remain pending.

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
leaks. Classic layout/scale/wire orchestration and scan behavior in #17 remain
pending; the frozen 1.54 decoder is not called or changed.

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
font-style/resolution/world policy, justification/cursors, outline text and wire
orchestration remain pending. SCAN painting stays blocked by #17.

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
