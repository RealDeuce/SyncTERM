# Terminal conformance coverage

`cterm` and the connected ANSI margins suites cover screen-clear history bounds
for ED2 and FF, with origin mode on/off, and preserved rows outside the cleared
window. Native cases also position the terminal within a larger console.
The connected CP437/UTF-8 suites check complete cell and solid-colour pixel rows
for LF, RI, SU, SD, IL and DL in small/large scroll regions, including oversized
counts and reset recovery. Native IL tests retain every input split and the
bottom-row boundary. Existing `st_console` tests cover row-ring wraps, copies
with/without a fill, both pixel phases and outside/status-row preservation.
Synthetic bottom-row streams distinguish writes ending in column 79 from
writes reaching column 80, including trailing spaces and ordinary glyphs.
With LCF off and autowrap enabled, the latter scroll immediately; a following
LF scrolls again. Enabled LCF defers wrapping; autowrap off prevents it.
Native 80x25/80x43 cases cover partial/full-height regions, status-row
reservation, split input, cursor and history counts in CP437/UTF-8. Connected
cases cover origin mode on/off, text and independently expected solid pixels.
The observed v1.10b failure to scroll on a partial-region wrap is a reference
bug, not the expected behavior. Both clients return DA
`ESC[=67;84;101;114;109;1;333c`. A captured Rainwire login includes explicit
`ESC[23;80H` followed by a space before its LF; remote artwork is not used as
an expected image.

`syncterm_atascii_glyphs` exercises all 255 drawable ATASCII bytes and quoted
end-of-line in Atari and Atari XEP80 sessions with the Atari font. Each capture
checks the glyphs, cursor and both bitmap phases, including the quarter-block
orientations and normal/inverse pairs. Native tests check every input split,
the local inverse keyboard toggle, reset during ESC quoting, binary screen
saves, a scroll at the bottom-right corner, CP437 input with the Atari font,
and encoding of all 128 base glyphs.

`syncterm_vt52_c0` checks every printable C0 slot in received Atari ST text
against its glyph index, including the Fuji halves and Bob Dobbs quadrants.
Connected bitmap captures cover all three ST resolutions with IGS on and off
and Atari ST, CP437 and UTF-8 connections. They check the cursor, NUL padding,
backspace, a raw escape operand, UTF-8 private-use input and the original TOS
Fuji raster in both pixel phases. Native tests exercise clear/home, every input split,
a logo wrapped across the right margin and the Atari ST codepage round trip for every byte.

`syncterm_control_cp437_delivery` checks each drawable CP437 byte in an
ordinary TCP text stream. The peer waits for the cell to paint and the cursor
to advance before sending the next byte. The ANSI-BBS case checks the glyph
index in all 256 slots, using doorway quoting for the eight control bytes.
The RIPv1 and RIPv3 cases exercise the drawable bytes in each mode with
RIP command recognition enabled.

`syncterm_control_transfer` checks ZMODEM detection over a live TCP connection.
The peer waits for each asterisk and each partial startup byte to reach the
terminal before sending the next. The four-byte upload and download triggers
open the file picker and start the receiver respectively. The test also checks
that terminal output resumes after the batch.

Per-BBS graphics caches retain the legacy `RIP/` and `SkyPix/` subdirectories;
APC uses the BBS cache root. The RIP interaction suites check icon writes,
file queries and playback isolation in CP437 and UTF-8. `syncterm_skypix_cache`
checks legacy `SkyPix/` cache hits and new uploads for both SkyPix variants,
encodings and status-row choices.

[Musicomm](MUSICOMM.md) has explicit local activation and raw-byte note
ownership independent of text emulation. `cterm_musicomm` checks literal
handshake/MIDI/end bytes, split input, queue drains, PSG fallback/voice stealing,
held pitches, abort and lifecycle cleanup without physical devices. MIDI
session boundaries reset inherited pan/pitch/pedals/controllers, clear software
synth effects and cached DSP output, and drain backend-reported PCM latency.
`music_stream` covers blocked/reset/error/shutdown paths and repeated silent
session boundaries; the optional FluidSynth `--reset` probe checks actual
renderer tails separately without physical output.
`syncterm_musicomm` checks the connected menu, all low bytes, recovery and
coherent blank-scene/cursor captures in ANSI and Atari ST/IGS sessions, CP437
and UTF-8. Audio and MIDI are disabled in the application fixtures. This does
not claim physical MIDI, original sender timing or Atari hardware validation.

The separate [RIPscrip 3.0 implementation goal](RIP3_CONFORMANCE.md) pins its
documentation and tracks requirements and upstream blockers. Its source guard
freezes the existing RIP 1.54 implementation and historical fixtures. This is
preservation/inventory coverage, not a claim of RIPtel 3.1 compliance.
`syncterm_rip3_mouse_CP437` and `syncterm_rip3_mouse_UTF-8` cover connected
simple mouse fields, host execution, captured coordinates, deletion and native
text/graphics press feedback and the adopted PR #115 gesture/identity policy.
`syncterm_rip3_buttons_CP437` and `syncterm_rip3_buttons_UTF-8` exercise
plain `1U` buttons, native clipboard faces (including inactive/protected reads
and partial-viewport padding), classic/Dixon labels, pointer
activation, reset-before-host, shared capacity and deferred host failure
recovery on four connection rasters.
PR #117 defines positional payload splitting and half-open hit boundaries;
native tests cover raw limits, reversed/degenerate faces and viewport snapshots.
PR #123 `1k` tests cover all 256 flag combinations, disjoint contained/crossing/
outside classes, absolute normalized WORLD selectors, deletion before painting,
COPY index-0 cleanup, opaque screen-phase patterns and once-only selector paint.
They retain surviving captures, cancel deleted ones, continue failed paint attempts
and reject unsupported flags nonfatally. PR #122 supplies clipboard eligibility/
padding and per-type geometry. Literal ICON/hot rendering is covered below;
filename/label macro expansion and persistent groups are covered below.
Auto-stamp captures implemented plain/clipboard faces before their label, mutates
protected live style flags only on success, and recovers after capture/font
failures. Tests verify exported dimensions/colors and current-clipboard replacement.
`syncterm_rip3_keyboard_CP437` and `syncterm_rip3_keyboard_UTF-8` run PR #121
byte/default/wildcard vectors, event consumption, HKEY gates, pointer cancellation
and reset/delayed FIELDID lifetime through real keyboard events. Native forwarding
checks cover ANSI/RIP1/RIP3 and pointer-only disable. The same connected
fixtures cover ANSI Keyboard Action Mode and its independent scene gate,
including inactive windows, retained focus and pointer capture, half-duplex
Backspace, and built-in keyboard sends. The CTerm text-window test checks
mode controls across receive splits and existing ANSI/RIP1 mode handling.
Connected hover, grouped buttons, nonplain sizing/eligibility and enclosed
painting are covered below; see the RIP3 inventory for exact scope.
RIPv3 uses the existing ANSI engine. Adding a VT-102 emulation mode, selector
or mode-specific keymap is explicitly outside the user-approved scope, even
when upstream reference documentation or a PR describes that mode. Applicable
ANSI charset behavior is specified by PR #107 and awaits implementation; its
VT-102-only translation and vectors remain excluded.
`syncterm_rip3_connected_CP437` and `syncterm_rip3_connected_UTF-8` exercise the
normal executable over TCP with the control bitmap backend. They cover the
literal Dixon selection and drawing sequence, bold advance, nonfatal recovery,
wire `M` color-mode changes and literal scene-prologue restoration of palette
mapping after RGB, independently expected red/green pixels,
WORLD word-range/zero-state storage, asymmetric macro overflow, axis-setter
target defects and drawing recovery after restoring a positive frame,
COLORMODE precisions 1..8 through foreground/fill/erase, undefined replies,
invalid-input recovery and protected STYLE independence,
live WORLD/device queries, templates/angles, empty-input continuations, delays,
offscreen BMP export/load and clipboard paste, repeated paint over ANSI,
command-only CRLF without scrolling, and default-window deactivation with
ordinary text/ANSI suppressed while RIP graphics/replies remain live. Ordinary
`w` definitions cover all five MAF modes, scaled placement, wrap/chop, clipped
scrolling, cursor-preserving reactivation, key forwarding and local Wren
writes/reset/state. Changed definitions retain ordinary ANSI parser state and
tabs while resetting the rendering state and saved cursor to home; full
character-set support and full TW backups/reset/metadata remain pending.
`syncterm_rip3_extended_window_CP437` and its UTF-8 counterpart cover connected
`b` WORLD/device bounds, actual MAF metrics, directional font fitting, centered
placement, state/tab reset, flag-16 bounding-box clear before deactivation,
offscreen clipping and nonfatal allocation rejection. At 720×400 these tests
check nominal cell sizes and the resulting change of font in a narrow window.
At 792×592 they check the matching 799×599 bitmap bank. The connected
`syncterm_rip3_text_erase_CP437` and UTF-8 suites cover PAL0 whole-window e/ETW,
STYLE-background wire >, and ANSI-background TWERASEEOL, including full-row
extent, unchanged cursor/wrap/ANSI state and untouched extended bounding margins.
The latter follows the target's ANSI palette mapping and direct RGB, separately
from graphics colors. `syncterm_rip3_window_table_CP437` and UTF-8 cover 36 owned slots, wire 2T/CUR
selection, protection and activation gates, TWIN/ISEXTWIN, noncurrent cursor/erase
effects, and finite aggregate allocation. Native `cterm_text_window` checks
independent copied font artwork after source destruction. Full first-use and
partial-ANSI selection acceptance awaits upstream #106; those unresolved edge
requests reject nonfatally, retaining the old owner. TW backups/reset, MTW and
`syncterm_rip3_window_query_CP437` and UTF-8 cover cursor/font/geometry readback
for current and noncurrent slots on three reference rasters and 720x400. Expected
replies include inactive sentinels, actual established font metrics, explicit
DEVICE/WORLD bounds, live scaling, unclipped wide origins and unchanged pixels/
cursor/wrap. The connected RIP3 screen tests compare the first scroll at
line 42 on startup with the first scroll at line 43 after an explicit mode 0
window definition. Both encodings cover the local status preference, the
host status switch, and a graphics pixel below the text window. PR #107's separate
CSI saved coordinates remain separate from the now-connected ESC7/8 represented
text-window save, covered by `cterm_text_window`, `rip3_session` and
`syncterm_rip3_ansi_save_CP437`/UTF-8. Those tests assert old source CSI versus
captured live position, origin-aware restored CSI coordinates, independent query
ownership, atomic resource failures, repeated restore and protection, current
target selection, no repaint across geometry/font changes and following bytes
in the same batch. Coherent TCP captures cover 640x480, 800x600, 1024x768 and 720x400.
The scene header suites cover the HARD snapshot boundary and SOFT retention.
Per-window mode completion remains pending.
The header suites also cover protected-window activation and caret gates,
inactive viewport bounds retention, and shared whole-window erase bounds
and cursor homing. The header erase color and exact geometry-reset fields
remain tracked upstream.
Wire `g`/`H` address both the entry TTY and positioned windows,
including fixed-width numeric fields in both radices, native CUP clamping,
deactivated-window no-ops and nonfatal recovery; cursor-only commands retain
the artwork. `$ATW$`/`$DTW$` control the default TTY, validate complete selector
lists, retain cursor preferences and preserve artwork painted while text is
off. Undefined slots are ignored. Defined noncurrent slots retain their own
activation; protected TWs ignore ATW/DTW while permitting cursor and erase
operations. Selection retains the target activation state. Ordinary window endpoints order/clamp to their nominal font
grid before identity; tests verify the corrected font-2 extent, preserved
cursor/saved cursor/colors for equivalent definitions and unchanged visible
pixels. The native `cterm_text_window` test covers owned parser/tab buffers,
split CSI and OSC continuation after destroying the prior owner, saved home
and allocation failures. Optional pending media payloads reject redefinition
nonfatally; their transfer and G0/G1 character sets remain unfinished. The
applicable ANSI charset byte contract is now supplied by PR #107; implementation
acceptance is separate. CRLF
regressions include bottom-row cursors outside column one, positioned windows,
valid/rejected/empty/continued lines and processing-budget boundaries. The
entry's grid/raster is retained at
the three presets and at 1280x800 and 720x400. These tests disable physical
audio. Full-compliance acceptance and incomplete command families remain open.
The internal fixture descriptions below retain their narrower coverage scope;
older application-routing limitations are superseded by this connection suite.
Separate RIP3 outline-font realization resolves all 80 bundled names and prepares
world-derived DPI/scales plus device-space hollow widths. Tests cover caps,
WORD narrowing, orientation, file strength bytes and cache preservation on
failure. Literal outline T/@ rendering is implemented in the separate engine.
The outline geometry kernel accepts independent per-byte device positions and
glyph matrices, plus explicit device-axis shadows. Independent rectangle
expectations cover all sixteen direction/rotation pairs on the three canvases,
four shadow directions in five write modes, fractional/clipped placement and
allocation-failure atomicity. Ordinary metric policy and command alignment now
use the adopted portable placement contract.
Outline layout tests resolve string direction and independent font orientation,
the complete signed additive-baseline table, one-time baseline conversion,
device-axis shadow distance/direction and device advance/spacing arithmetic.
Native captures now consume those prepared positions and shadows on all canvases.
Zero-baseline alignment uses the design origin and signed total for justification;
adjusted layout advances remain separate from cached cursor metrics.
Prepared outline fonts now supply production glyph matrices, nonzero fill and
device-space hollow widths to the positioned renderer. All sixteen rotation
pairs have coherent native captures on every canvas with isotropic and unequal
X/Y DPI. Shadow/write-mode scenes use that same production path; hollow bold's
minimum width, pending-effect rejection and failure atomicity have focused checks.
Solid bold, italic and decorations now have internal implementation/test evidence
under PR #99's portable renderer-choice contract; connection integration is pending.
PR #83's signed RFF global-bbox correction is adopted. Realization transforms
all four corners through face percentage, orientation and device-axis DPI,
retaining wide y-down edges. Spacing uses their horizontal max-minus-min span.
Literal font tuples, negative upper bounds, rational half ties, zero/oversized
spans and realized-width spacing on all canvases have focused native coverage.
Prepared fonts now provide independent unkerned per-byte DEVICE advances to the
native layout/render scenes. Logical advances use the adopted rational half-up
rounding before separate string-axis DPI truncation; the published Marin F result and
authored half ties, face percentages, high/missing/damaged codes, long arrays and
transactional overflow/error paths have native expectations. This geometric
service now applies PR #88's no-kerning, effect-independent metric policy, using
X DPI for horizontal strings and Y for vertical strings under every character
orientation. Wide logical intermediates survive to final device conversion.
Context-owned cached X-width snapshots now copy all unsigned logical metrics,
DPI, spacing and bbox values independently of font/backend storage. Preparation
is atomic; failures retain the complete prior snapshot. Tests cover high/missing
bytes, damaged outlines, freed backing fonts, negative gaps, strings beyond 255
bytes, final signed-WORD wrapping and nonfatal overflow. Classic font-zero @
reads that snapshot in every direction/justification, with raw cursor anchors
and separately converted drawing anchors. Cold/warm, port/style/frame changes,
CP437/UTF-8 and recovery have coherent native captures on all three canvases.
Accepted outline T/@ calls now prepare metrics for the y-selected face before
cursor measurement, retaining the previous snapshot on preparation failure.
Native captures cover real Marin/Cn/Wd/HE selection, independent rotation and
justification, shadows and write modes. Full drawing reset and application routing
remain pending. Earlier unrelated test and legacy evidence is reused.
Effect-specific ink extents remain separate from the global coordinate box.
The separate engine now has a session owner for its live ENV/STYLE/PAL/PORT tables,
typed backups, templates, variables, font cache, stream and resumable input/query
pipeline. Its void pump absorbs parsing failures, holds queued bytes during delays
and owned prompts, and resumes local work without requiring new input. Cancel and
destruction abort owned work and stop a borrowed exclusive audio player without
destroying client services. Native wire/capture tests cover all canvases, actual
y/T/@ drawing, same-query STYLE/background changes, prompt ownership, cancellation,
isolation and all constructor-failure boundaries. These tests use a pull audio
engine without physical output; they do not claim GUI or application routing,
full graphics reset, graphical-text preprocessing or unbound table-class coverage.
Palette lifecycle tests now cover reset/default RGB state separately from
definition, protection and current selection. RESET(PAL,...) supports live and
backup selectors with full syntax/binding prevalidation; protected slots survive
aggregate reset. Live palette COPY prevalidates every destination and transfers
source protection except to zero. Reset may leave the current slot undefined:
numeric ISPROT then returns -1 while CUR protection returns zero, and drawing,
bitmap/image consumers and coherent captures retain access to its RGB values.
Table-local soft/hard reset services are covered. Later connected milestones
cover all five allowed live-entry COPY adapters; whole-scene reset/HEADER and
unfinished table payload fields remain pending.
The pinned PR #80/#81 contract confirms current-base N zero/nonzero normalization
and 2Y one-plus-two fields with ordered protection requests. STYLE-zero source
and destination requests now diagnose separately without ending processing;
tests inspect state at each diagnostic and recover through split-input drawing
on all canvases. The global disabled-graphics session gate remains pending.
Shared dollar-reference decoding now uses forty nonempty twenty-byte argument
slots, omits empty comma fields and preserves raw spaces. Numeric/keyword handlers
normalize their own bounded copies. Recovery, 20/21-byte and 40/41-argument limits,
trailing commas and widened TERMINFO/viewport scratch buffers have native coverage.
PR #85's argument-byte contract is adopted: the decoder now applies its unsigned
payload alphabet, local quote overwrite and delimiter rules before callback
normalization. Exhaustive raw/quoted byte sets, published examples, decoded
limits, UTF-8 byte counts and public nonfatal recovery have native coverage.
Double quotes, excluded punctuation and consecutive backslashes reject without
state changes. Outer ordinary/host passes and general user/prompt grammar remain
pending; local acceptance does not claim complete expansion.
RAND now uses explicit session-owned CRT generator state and the adopted wrapped
decimal/arithmetic contract, including maximum+1 at the endpoint and zeroes
prepended before a minus sign. Native vectors cover the original sequence,
independent sessions, solved endpoint seeds, oversized tokens, signed overflow,
byte validation, exact reply capacity and nonfatal error recovery. No initial
seed, global CRT state or application-route integration is assumed. PR #89's
user-variable contract is now adopted: the syntax decoder accepts session prefix
`=`, ignores repeated flags and rejects `%` atomically. Tests cover all flag subsets
in both orders with repetition, mixed prefixes and absent/empty defaults. The
resumable user-variable processor now executes the full prompt/effect matrix over
separate session/persistent tables, including memory-first shadows, persistent
destination precedence, empty values, suppression, NULL/NONE and cancellation.
Its native test covers 768 matrix combinations plus owned suspended prompt state,
format-adapter handoff, failed allocation/write/format and nonfatal recovery.
Immediate-query dispatch now uses that processor and suspends the shared input
pump until the prompt completes. Dialog/editor formatting, persistent disk
flushing, hang-up, other macro consumers and application routing remain pending. Completed macro-ordering and argument-byte
answers are retained.
The typed condition stage now compares wrapped signed decimals or literal
case-insensitive byte strings and performs the original staged Boolean reduction.
Native vectors distinguish equal AND/OR precedence, late NOT, grouping and
multiple-NOT phase boundaries. Provider calls are ordered without short-circuiting;
failed values become false, and complete preflight/allocation precedes callbacks.
Validation-only use makes no provider calls. These helper-stage tests now also
support the internal outer worker and immediate-query integration below;
graphical-text consumer integration remains pending.
PR #92 now supplies the conditional operand grammar; its private source parser
preflights complete structure without allocation/providers, owns decoded operands,
validates exact synthesized references and routes template indexes separately.
The native parser test covers all 256 initial/quoted-payload byte classes,
independent escape stages, quoted/empty literals, connector-space boundaries,
255-byte pre-trim/provider bounds, long encoded input, ordered callbacks, nested
structure, owned source and nonfatal helper recovery. Existing typed reduction is
reused. A private owned continuation now exposes one validated operand request
at a time, letting the branch owner wait for a prompt without replaying
earlier provider actions. Supplied borrowed bytes are compared immediately,
all leaves still run in source order, and resumed processing/reduction allocates
nothing. The synchronous evaluator uses the same continuation. Independent native
tests cover waiting/budgets, independent owned runs, unavailable/oversized values,
pending destruction and allocator failure. PR #98's outer scanner now implements
IF/ELSEIF/ELSE/ENDIF with pure skipped-header validation, quote-blind delimiter
lookahead, exact keyword/name/template boundaries, sixteen open frames and eight
whole passes. Named results rescan immediately; angle templates defer within a
pass and their inactive output is discarded after lookup. Internal query binding
uses real built-in/user/template providers ahead of segmentation and the two
ordinary passes. Pending prompts/delays retain queued bytes; owned cancellation
does not replay or undo completed provider effects. Native tests cover branch and
provider order, escaped delimiters, depth/bounds, failed allocation, source lifetime,
required templates, prompt response/cancel and later-input recovery on all canvases.
The owned-session fixture also exercises the public pump/prompt/response boundary.
These are internal tests; graphical text and connection integration remain pending.
PR #94's ordinary outline origin/justification policy now drives literal T/@:
design origins start at the supplied anchor, end/center use the signed ordinary
total with the final gap, selector 3 starts normally, and the baseline table is
added once. PR #95's working-response/storage limits are implemented in the
immediate evaluator, with the all-canvas boundary evidence described below.
The completed numeric/Boolean and PR #85/#89 rules remain adopted independently.
Local data references now use explicit client services for SHIFT/CTRL, MSTAT,
COLORS and captured XY. Independent native replies cover both modifier states,
device capacity independently of palette mode, signed decimal WORD padding,
current-ENV world conversion and clamped text-window cell defaults on all canvases.
Explicit WORLD/DEVICE override the cell default. A delayed compound host query
reads changed local values only when reached, with ordered resume/recovery;
real data references also feed the existing condition evaluator without short
circuit. Platform input/device adapters and full angle/application routing remain
pending; these tests query fixture state and operate no physical keyboard/mouse.
RIP3 extended-font state tests cover the fixed UltraNum `y` block, owned STYLE
fields/names, one-time baseline conversion and `q` attribute OR/protection.
The y-selected outline face now resolves on T/@ use against the bundled catalogue
or a matching immutable RFF override. Actual y face transitions cover Marin,
condensed, wide and hollow-expanded metrics/ink. Accepted outline calls commit a
logical snapshot before cursor measurement; failed preparation uses the retained
snapshot, and later raster/ink failures still perform the adopted cursor update.
Independent all-canvas captures cover sixteen rotation pairs, four selectors,
five modes, negative bbox spacing and signed baseline displacement. Unequal DPI
separates vertical layout from cached X width. Native cases also cover shadows,
RGB ink lookup, NOT without a palette, CP437/UTF-8, blank/clipped text, 700-byte
width wrapping, owned cache lifecycle, allocation failures and split recovery.
The separate renderer now also implements synthetic solid bold, italic and
decorations under the adopted PR #99 boundary. Its documented profile shears in
font coordinates, expands solid scan spans on DEVICE X and uses per-byte local
baseline bands. These change ink only: metric B, ordinary positions and cached
cursor widths remain unchanged. Glyph/band coverage shares the device-displaced
shadow and W operation, independently of vector pen, border and fill attributes.
Independent internal captures cover 15,360 rotation/DPI/effect/spacing/mode scenes,
q accumulation through CP437/UTF-8, hollow blank/damaged/missing glyphs and failed
coverage allocations without partial painting. Name/text expansion, full reset
orchestration and application routing remain pending.
These are separate-engine parser/bitmap fixtures, without GUI or native
font-device coverage.
RIP3 stream/body tests cover split input, nonfatal recovery and coherent palette/port
captures on all three canvases, including copy modes, clipping, scaling and
overlap recovery. DEFINE_PORT tests cover independent
initial flags, half-open screen/offscreen extents, ignored offscreen origins,
protected fallback and client allocation policy on all canvases, radices and
coordinate widths, with coherent captures and nonfatal split-input recovery.
Port geometry queries use the documented numeric semantics, an explicit
correction to RIPtel 3.1's inverted-success callbacks. Their shared decimal
selector/type/domain grammar and wide signed results have independent tests.
Wire line-style decoding covers all four unit/background flags, fixed field
widths, X-only world scaling, byte truncation and custom masks. All-canvas
drawing captures configure those masks through split-input `=` commands.
Separate-engine A/V tests now cover integer endpoint rays, equal-endpoint
point strokes, coincident-ray full curves and counterclockwise partial sweeps.
O consumes its angle fields and always draws a complete ellipse. Coherent
screen/offscreen captures cover all canvases, bases, coordinate widths and write
modes. Separate I/i/_ consumers cover pie/chord interiors, distinct radial
closures, equal-endpoint radial/empty branches and coincident-ray full figures.
Their coherent captures cross both N states with all five modes, custom/solid
line masks and gap flags. Literal PR #77 operands establish W fills, solid COPY
borders and N0 suppression of radial-only contours on all canvases/port kinds.
Pre-paint allocation/ink failures and split-input recovery remain covered.
Full RGB/active-color masks and active-engine integration remain pending.
Separate Z/z/t/x tests cover wrapped fixed-point Bézier sampling, omitted
samples, typed records/closures, low16 conversion and independent collection/
expansion limits. Coherent captures cover fractional WORLD projection, all
canvases/bases/widths, patterned strokes and fills, COPY borders and nonfatal
recovery. Original error-message/UI integration remains pending.
Separate &/-/]/+/[ tests cover one-degree rotated contours, distinct field
widths, order-dependent spans, pie/chord closure and multiple-traversal fill
parity. Captures cover all canvases/bases/widths, screen/offscreen contexts,
context fill phase and parser-owned recovery. Filled skewed contours now cross
both N states with all five W modes and retain patterned COPY borders, including
opaque/transparent gap checks on retraced zero-area contours. Standalone skewed
revisits now apply W separately, including repeated-turn XOR/NOT cancellation.
Numeric-profile and full color-plane acceptance remain pending.
K now uses current-mode fill followed by an enabled solid COPY border,
independently of masks/opaque gaps. Coherent captures cover all canvases,
bases, coordinate widths, thicknesses and N/flag combinations. Required border
inks resolve before a NOT fill; allocation/ink failures precede painting.
Standalone L/R/P/l/Z/z/t/&/] now collect ordered visits before painting.
Independent native vectors check crossings, adjacent retraces, subdivisions,
closed seams, thick joins and later COPY foreground/background order. All-canvas
screen/offscreen split-input captures combine repeated paths, cubic peaks,
single/repeated skewed turns, parser recovery and unchanged PORT cursors.
Separate P/l/p/< tests reconcile direct small/maximum counts, asymmetric
multi-contour count widths and the shared 8193-number budget. Coherent captures
cover holes, overlap cancellation, nested islands, patterned COPY borders,
current-mode fills and nonfatal rejection before painting. Exact original
error wording and POLYPOLY.RIP corpus/continuation replay remain pending.
Separate U/u tests cover X-world radius conversion, independent oversize fitting,
normalized/zero/wide bounds, requested stock/custom patterns at every width
for U, and current-mode fill followed by solid COPY borders for u.
Coherent captures span all canvases, bases, widths and screen/offscreen contexts;
allocation failures recover before painting. Extreme original DWORD overflow
remains pending, while the low-WORD Y-radius sign/zero cases are covered.
N now owns its border setting in STYLE; filled consumers no longer take an
external flag. Tests cover STYLE protection, fixed defaults on first selection,
independence from ENV/PORT, immutable backups and live-selection retention on
restore. Existing drawing captures use N or explicit prepared STYLE records.
The nonzero argument-domain clarification is tracked in #78; complete reset and
extended-style/context activation remain pending.
2Y and CUR(STYLE,n) now select represented style attributes and STYLE-owned
font family without moving the graphics cursor or selecting PORT/ENV. Tests
cover protection flag ordering, fixed defaults, zero-slot diagnostics with
continued effects, CP437/UTF-8 contexts and split-input recovery. Exact 2Y
field/flag documentation is tracked in #79; contradictory live-F prose remains
blocked under existing #2.
PORT now owns context-absolute integer graphics cursors. MOVE/TEXT and TEXTXY
checks cover viewport-origin changes, independent ports, signed whole-string
metrics, current-frame reconversion and backup retention. Coherent all-canvas
captures combine rendered glyphs, replies and screen/bitmap cursor state in
CP437/UTF-8. All classic TEXT_XY uses its shipped raw-coordinate stores;
font zero reads the context's cached outline width. Complete outline commands
and scene reset/integration remain pending.
Separate PIXEL/POINT tests cover all five modes and their distinct stored origins,
one-pixel versus odd-square coverage, screen/offscreen contexts, physical and
viewport clipping, both radices/widths, nonuniform mapping and recovery. Their
published 8-bit vectors include repeated operations and once-per-pixel thick
squares on screen/offscreen targets. The same coherent captures check K/u
foreground/background fills and solid COPY borders. Full RGB and active-color
masks remain pending in the separate engine.
All twelve stock fill masks and custom masks use fixed drawing-context phase
in the separate wire engine. Coherent captures cover both world scales, all
canvases/radices/widths, negative relative viewport origins, indexed modes, RGB
fills and copies that preserve painted bitmap patterns at a shifted destination.
Separate style-record tests cover explicit attributes, owned
snapshots and nonfatal metadata/COPY errors. Complete c/k/W/L/R command tests
cover palette/direct RGB ink, all drawing modes, explicit patterns, reversed and
clipped geometry, default-world mapping, wide endpoints and allocation recovery
with coherent captures on all three canvases. Numeric Y tests cover all classic
font/direction/size values, conflicting flags, protected edits and split-input
recovery; independent all-canvas text-kernel captures verify the selected
orientation, justification, magnification and shadow. Text wire/cursor integration
remains pending. Complete custom-fill/clear-stock/bar body tests cover row/colour
decoding, protected edits and recovery, all drawing modes, RGB ink, inclusive
geometry and wide coordinates. Independent all-canvas captures verify literal
stock diagrams, custom bit order and adjacent fills with explicit device phase;
conflicting masks and protocol anchoring remain pending. K body tests add
half-open interiors, explicit border on/off, solid COPY borders after every
fill mode, protected records, wide geometry and border-allocation failure
recovery. Independent RGB captures cover all canvases; the K/N pattern conflict
and N context ownership remain blocked. C/G/o and canonical full O/V tests cover
circle aspect, independent ellipse radii, all modes, protected/RGB state,
patterned interiors, COPY borders, clipping and wide geometry. Independent
all-canvas captures and mesh/coverage allocation faults check recovery; general
angle-pair interpretation remains blocked. E tests add replacement erase in
palette/direct RGB, protected overlapping screen views, independent offscreen
viewports, inactive/one-pixel cases and nonfatal split recovery. Native TTY
overlap/context integration remains pending. Complete 3e record-edit tests check
both radices, arbitrary/zero/maximal rates, protection and split-input recovery
with BAUDEMUL readback; actual playback pacing/reset integration remains pending.
Rounded-rectangle service tests add explicit circular corners, patterned fills,
all modes, COPY borders and join coverage applied once. Independent all-canvas
captures plus staged allocation faults cover clipped/wide, fractional and
degenerate geometry; U/u wire units/pattern policies remain pending. Unscaled
surface/port transfer tests add paired clipping, original-source overlap in all
transfer modes, offscreen/protected content, extreme origins and allocation
failure without mutation. Coherent RGB captures cover all three canvases;
COPY_BLIT/SCROLL wire and clipboard reset/backup integration remain pending.
PORT_COPY geometry tests now cover half-open WORLD extents, post-conversion
zero forms and equal-size coupled trimming versus independently clipped fitting.
Independent all-canvas coherent captures, a width/origin matrix, huge projected
coordinates and fit/unscaled overlap-allocation recovery cover both source kinds
and all indexed modes. General RGB transfer/device semantics remain pending.
Shared GET_IMAGE and LOAD_ICON capture tests cover destructive replacement,
selection/protection, budgets, clipping and nonfatal recovery. LOAD_BITMAP tests
add post-draw explicit/clipboard duplication, protected screen/offscreen targets,
flags0..63 in both radices and COPY wallpaper with alternate half-width row
phases. Independent screen index/palette captures on all three canvases and
fault injection cover ordering, retained keyed backgrounds and live port-zero
fallback. Bitmap codec/key tests now cover all six BI_RGB depths, translated keys,
raw-bit versus mapped-index selection, exact 24-bit RGB, keyed 16-bit rejection
and the shipped 32-bit no-selection defect. Full true-color RGB raster/device
and port-coordinate/application integration remain pending. Wire IMAGE_STYLE and IMGSTYLE tests add stored far-boundary queries, distinct
protected/inactive gates, keyword replacement, temporary-selection recovery and
old-current/port-zero image reset state. Port-service activation and byte-split native/JPEG-disabled commands verify
set/reactivate/display and full-viewport all-zero
fallback with coherent screen index/palette captures on all three canvases.
JPEG wallpaper tests now cover raw image-area seeds, retained background,
extra far rows/columns, staggered COPY phase and nonfatal post-draw failure.
Actual JPEG scenes exercise all 64 flag combinations under every vector mode.
The connected RIP_IMAGE test displays a complete progressive grayscale JPEG
and checks both blink planes at the top, bottom and outside its destination.
PORT_WRITE file tests add raw DEVICE/half-open screen crops, independent far-axis
clipping, whole-backing offscreen export, inactive/protected reads and nonfatal
undefined/empty/allocation/write recovery, with independent BMP byte/pixel
expectations across all canvases, both radices and coordinate widths 2–5.
Complete scene/header reset orchestration and application routing remain pending.
SWITCH_PORT tests now cover source/selection/destination ordering, per-pair
unprotect precedence, all port indices and flag domains, ordinary undefined-port
creation and nontransactional failed creation. CUR(PORT,n) shares its selector.
Independent all-canvas captures check drawing after selection and parser recovery;
whole-scene disabled-graphics/context binding remains pending.
Viewport producer tests now distinguish stored DEVICE geometry from visible
pixels: no screen-port-origin addition or port-bound clamp, normalized corners
and post-conversion zero deactivation. Independent all-canvas captures cover
negative relative origins, image/copy sampling through physical clipping,
viewport reads beyond declared bounds, backup state and huge virtual extents
with bounded raster staging. Complete wide-coordinate records and out-of-physical
capture/tile-source policy remain pending.
These do not change the active route. A body tests now add canonical full circles and validated
equal-angle no-ops, both bases/all coordinate widths, horizontal-radius aspect
handling, RGB/NOT, protected records, wide clipping and allocation recovery.
Independent all-canvas captures and one-byte malformed/unsupported recovery
cover those cases; general arc angle-pair interpretation remains blocked.
Ordinary P/l tests add open/closed outlines, continuing opaque/transparent
patterns, all modes/bases/coordinate widths, protected contents, RGB/NOT and
nonfatal allocation/field recovery. Independent all-canvas captures include the
exact pinned examples; integer geometry checks cover thick joins, distinct
crossing visits and wide clipping. Later count milestones reconcile wire limits. Filled p tests now add
even-odd interiors and optional patterned/thick COPY borders, all modes/bases/
coordinate widths, RGB and nonfatal preparation failures. Independent all-canvas
captures include its pinned example. A separate wide integral-path kernel checks
exact crossing arithmetic, coincident/subdivided edge parity and device-service
count boundaries; unresolved wire counts/multi-contour grammar remain blocked.
Separate 3D/input-gate tests check exact tick durations, monotonic clock wrap,
queued RIP and CP437/UTF-8 bytes, consecutive delays/radix changes, cancellation,
protected ENV and nonfatal recovery on all three ENV canvases. They use a
client-owned queue and deterministic clock; real application source adapters,
baud pacing and compound host-continuation integration remain pending.
The ordinary D reference and resumable immediate 1ESC query evaluator have
separate tests for queued delay/reply order, decimal arguments across radix
changes, CP437/UTF-8 byte retention, long identity replies and nonfatal recovery
on all ENV canvases. Optional user-variable dispatch shares that ordered source
pump: final-token prompts remain pending, accepted responses emit once, and
cancel/EOF releases only query-owned state. Each response now completes exactly two ordinary passes before transmission;
inserted text is skipped within its producing pass, and there is no third pass.
Outer controls/backslashes are deferred to pass two; argument-local quoting runs
in pass one. Published ASCII rules preserve ^@, unknown pairs and trailing
prefixes. Special keys have no atomic whitelist; raw/unrecognized brackets stay
literal and incomplete slot forms diagnose without suppressing their bytes.
Independent cases cover indirection limits, inserted angle/bracket literals,
quoted dollars, second-pass prompts and buffer-allocation failures without
partial transmission. All-canvas query fixtures cover prompt/delay
ordering, silent assignment/retrieval, empty/required/cancelled responses, blocked
source reads and following RIP/raw input. Known rejected built-ins never prompt;
P now follows the adopted user-variable path rather than a paste alias. The
independent pinned inventory and compiled catalog check that classification.
Owned templates now support [] direct blocks, [N:] raw definitions and [ABC]
chaining before expansion. Native table/query tests cover all 36 slots, intended
$?$ insertion (correcting the original unsafe offset), repeated/list-order chains,
marker-free discarded callbacks, raw 4096-byte truncation, source-order replacement,
required-selection callback/abort, lower/uppercase MegaNums, allocations and
recovery. All-canvas queries combine segmentation after ESC/backslashes with
owned delayed source and CP437/UTF-8 template bytes. $?N$ now embeds any of the
36 slots, including case-folded letters, in either ordinary pass. PR #95's
4096-byte bound applies to stored definitions, each response before pass one
and working text after every replacement. Discarded suffixes never execute or
return after later shrinking; retained callbacks and the causing operation's
side effects survive. A trailing outer backslash stays literal. Independent
all-canvas boundary cases cover the published vectors, both passes, controls,
raw UTF-8 cuts, required-empty selection/recovery and generic/user results.
Earlier structural stages still precede clipping; ordinary syntax after a
replacement is checked only when reached. Immediate-query angle preprocessing
now precedes these stages. Button-group
aggregation and highlighting, pick lists/playback, dialog presentation, application routing and
deferred queries remain pending. No physical audio or
interactive application coverage is claimed by these native parser fixtures.

The shared graphics parser supports an optional owner-defined numeric reader.
`graphics_protocol_number` checks opaque byte ownership, every split, signed
values, supplied-value bypass, input-free completion, waits, raw arity and
error/EOF/cancel recovery. The existing IGS reader remains the default.

The BIG ordinary reader now covers destructive WORD construction and exact
finite scaling in `big_number`: 54 literal split-input cases, all WORDs
against a binary-rational oracle, count wrap, timeout/BEL events and recovery.
The shared RNG core is checked in `big_random` against all 32768 outputs,
signed rejection bounds and equivalent state streams under different poll
partitions. The stored evaluator is checked in `big_loop` with independent sequences,
reachable counter wrap, source changes within a command, unscaled stored
values, suspension prefixes and error/EOF recovery.

The `big_numeric` callbacks compose those sources with local
deadlines, ordinary final-r expansion, native clock access, delayed-key
checkpoints and exact partial error consumption; split-input/raw/EOF fixtures
cover ownership and recovery. Initial native CTerm input integration is
covered below; full application/command acceptance remains pending.

`big_state` checks the I/R logical reset order, retained application/frame
owners, palette-only I1, cached versus installed resolution and host-failure
prefixes. `big_profile` checks all three video/font profiles against original
ROM identities, the restored custom patterns and the shared 64-colour EGA
table. RIP now consumes that same immutable palette data; its native tests
and coherent CP437/UTF-8 raster captures passed. Indexed page/image storage,
its remaining native bindings and BIG command completion remain pending.

`cterm_big` checks the native pre-ANSI BIG path with production numeric
callbacks and actual I0/I1 indexed-console effects. Independent CP437/UTF-8/
status/split fixtures check IBM8x14 glyph rows, full-block pixels, palette,
cursor, frame locals, ff/NUL ownership, late-byte deadlines and EOF/reset/
semantic-hook recovery. Shared parser header-completion tests cover retained
identifiers, chaining, suspension and error prefixes. Wire & and native key/input integration are recorded below; complete command
acceptance remains pending.

BIG is selectable as profile/Wren value6. BBS codec and Wren tests preserve
previous values, cover its presets and property bounds, and persist entries/
defaults. `syncterm_big_input` uses coherent control-bitmap captures for all
three initial profiles, CP437/UTF-8, status and reconnect, I0/I1 effects,
exact original console fonts, both indexed blink planes, palette/cells/cursor,
DSR replies, RGB samples, fallback profiles and menu restoration. Physical
audio/MIDI are disabled. Complete command acceptance remains0/45.

`big_control` checks original repeat/flow/chain/reply ordering, both WORD count
domains, literal raw bytes, live reads, suspended prefixes and errors. The
CTerm fixture covers actual repeated/gang bodies with shared numeric sources,
source-to-wire seams, bounded work, output origins and reset/EOF recovery.
The optional shared-parser unknown-command handler owns the same accepted
suspension boundary without guessing payload. Application captures check
unknown chaining and final footer state. Wire &/text acquisition is covered
below; W rendering, < production and audio synchronization remain pending.

Native `cterm_big` key tests cover stored-loop delay probes and post-value
03/18 cancellation, zero/scan ordering across distinct readiness/read
boundaries, all native key entry kinds, CP437 conversion and UTF-8/modifier
replay. BIG/IGS share native event replay with separate protocol consumers.
Unread keys retain order behind finishing flow; saturation stays local;
reset/EOF and callback reset discard unread work. The standalone key cases inject
stored setup; wire & coverage is recorded below. Physical keyboard/pointer
coverage is not claimed.

Wire & setup is now covered by `big_stream`, `cterm_big` and the expanded
application fixture. Independent expectations cover immediate header/raw/
text/table stores, original suppression/step/count/source semantics, load-
abort zero filling, publication before XOFF,2048 actual stored slots, gang
copy bounds and fixed/selected-& bodies. Local auxiliary/text deadlines
preserve late-byte, cap/NUL/reuse/timeout ownership. Partial-pair timeouts
retain body entry. The session adapter recognizes full accepted pauses and
retains following bytes. The98 coherent application scenes include56 fixed/
gang loops and14 native Ctrl-C delayed loops with exact flow, current-value
effects and resumed buffered bytes. No physical audio/device or full command
acceptance is claimed; <, W rendering and music synchronization remain pending.

Wire R is covered by `cterm_big_resolution` and70 additional coherent
application scenes (168 total). Independent native split/bytewise CP437/
UTF-8/status cases check all three full indexed ROM scenes, geometry/cursor,
same-mode reinstall, full-WORD normalization, class0 source ownership,
retained application/frame/ratio state, deferred replies, stored R1..3,
returning-error/cache prefixes and EOF/reset/timeout. Application sentinels
precede R and fresh glyphs follow without ED, verifying clear/default/font
transitions in the same capture. Complete command acceptance remains pending.

Wire g stores its full class0 WORD without clamping or drawing. Native
split/bytewise cases cover encodings/status, special WORDs, I/R lifetime,
delayed stored values, replies and EOF/reset/timeout. Production numeric
callbacks probed with wire g/R state retain exact Y5->8/11 and cache-versus-
surface semantics; this does not claim a live coordinate drawing consumer.
70 added coherent g scenes (238 total) check complete flags and I1/R
retention with the unchanged full scene. Full command acceptance remains pending.

X dispatch/X6 now provide a live class2 consumer: exact full-WORD selector6,
immediate X store before Y, current-field signed clamps,12/6 defaults and
I/R/partial-input lifetime. Native cases cover split/bytewise encodings/status,
points/cursor, unknown selectors/continuation/replies and source-to-wire
transitions. Actual g2/g3 X6 Y5 gives8/11; stored Y5 bypasses that scaling.
70 added coherent step scenes (308 total) retain exact scene pixels and state.
Remaining X selectors and interactive pointer input remain pending; X completes
only after all nine selectors. No full command or physical-device claim is made.

X4 implements counted opaque zone loading, immediate coordinate stores,
special clear/loopback ids, live lengths, retained tails, conditional XON and
fresh per-helper deadlines, including its extra discarded read. Native tests
cover split/bytewise input, source/scaling/flow/prefix/error/reset/EOF and
bounded corruption.84 coherent session scenes (392 total) check zone data
and lifetimes with exact unchanged scene pixels/cells/palette/cursor/state.
Pointer hit testing and interactive acquisition remain pending. Negative ids
and actual table escapes fail at the accessed boundary, preserving accepted
prefixes instead of overwriting foreign DOS state.

Wire T/P now use the shared thin BGI kernel with original BIG clip/stroke/
point and polymarker schedules. `big_draw` checks literal shape masks, XOR
visits, phase/ties, WORD clips, copied dots and failure prefixes. Native wire
cases cover all modes/encodings/status, T normalization and partial stores,
P colour-before-input/final CP, source/scaling/EOF/reset/timeout and independent
blink-plane XOR.392 new coherent scenes (784 total) add all markers in all
three modes and patterned/rejected/zero-mask/width2 cases to the earlier
indexed scene/state/reply oracles. Pointer/page binding and remaining command
composition stay pending. Routine development tests are BIG-only; final goal
verification retains the full regression suite.

`big_collect` adds independently tested keyboard/zone collection operators:
all WORD type/options values,129-byte/count-wrap storage, raw bytes, echo
prefixes, signed first-hit/default/strict zones, unsigned cross-cell copies,
bounded failures and exact replies through the existing footer. Literal
movement vectors check X6 WORD overflow and asymmetric clamps. Native <
framing/echo/keyboard/mouse/pointer integration remains pending; the new
component tests do not claim bitmap or physical-device input coverage.

Native < now owns its three fields and local acquisition, retaining buffered
wire tails until completion. `cterm_big_collect` checks early append/key state,
raw getch/private scans, literal echo/wrap/deletion, counted replies/loop/chain,
pointer XOR/helper points, keyboard/mouse order and stationary reports,
UI/disabled/outside/status suppression, stale/overflow events and normal/error/
reset/EOF/destruction cleanup. Native mouse mode4 exposes active BIG pointer
ownership.42 coherent keyboard/echo/hybrid scenes bring the application oracle
to826 full indexed scenes with cells/fonts/palette/cursor/point state and replies.
Remaining page/image/audio composition and final full-suite verification are
pending; routine tests remain BIG-only.

Native BIG L/D/d now use the existing original clip/stroke/point adapters and
indexed sink. `cterm_big_line` checks split/bytewise input, colour before
acquisition, L/D endpoint and pattern order, the relative Y-sign defect,
source/scaling/repeat/deferred replies, point/error prefixes, per-plane XOR
and recovery.70 added coherent application scenes bring the oracle to896;
all modes/encodings/status choices retain full indexed planes, ROM text,
palette, points, cursor and exact replies. Page/image/audio composition and
final full-suite verification remain pending.

BIG m moves the three graphics point owners through existing WORD/origin
adapters without drawing or moving the console cursor. `cterm_big_line`
checks variable arity, full/negative selectors, original relative Y-sign
behavior, source/scaling seams, point/attribute/pixel retention and partial
failure/recovery.42 added coherent application scenes bring coverage to938:
absolute/relative m preserve the scene and subsequent D starts at the new
point. Final global page/image/audio composition and the full suite remain
pending; routine tests remain BIG-only.

BIG M selects the installed line-writing bit and then updates the application
cache; only inputs2/3 select XOR. `big_state` independently checks all65,536
WORDs. Native tests verify split/bytewise input, COPY/XOR pixels, source/flow/
failure prefixes and M/I1/R/pointer/I0 lifetime.56 coherent scenes bring the
application oracle to994, observing both mode owners, XOR line cancellation,
COPY dots under M3 and R's actual0/cache1 split with unchanged full scene,
palette, points, cursor and exact replies. Global page/image/audio composition
and final full-suite verification remain pending; routine tests are BIG-only.

BIG C stores one of four full application colour WORDs after both class0
reads, without installing BGI state or drawing. Exhaustive selector/colour
checks and native split/source/lifetime/partial-input cases preserve delayed
line/marker/fill/text ownership.56 new coherent scenes bring coverage to1050,
observing application WORDs, public draw BYTE, device pen and installed fill
with complete indexed planes/text/palette/points/cursor/replies; coloured
swatch interiors also verify composed RGB. Fill/text consumers, page/image/
audio and final full-suite verification remain pending; routine tests are BIG-only.

BIG A installs distinct public/lower/device fill owners and an indexed8x8
pattern tile without drawing. Built-in/custom validation, copy order, cached
colour, error prefixes and I/R lifetime have independent component/native
tests. RIP and BIG share the unchanged96-byte mask table.56 added coherent
scenes bring coverage to1106, observing selected fill and literal cached rows
with the full indexed/text/palette/points/cursor/replies/RGB scene. Fill
rendering consumers and remaining global composition are pending; only BIG
tests run during iteration, with RIP/full regressions deferred to the end.

BIG X1 now edits/restores retained custom rows with conditional arity,
immediate BYTE prefixes and a fixed eight-read loop across source changes.
It leaves installed patterns and pixels unchanged until A resubmits them.
Component/native cases cover full slot/flag domains, restore data, live
locals, bounds/cancellation, split input, timeouts/source/reply/I/R behavior.
56 new coherent scenes bring coverage to1162, adding retained slot bytes
and editor ownership to existing full indexed/text/palette/points/cursor/
fill-cache/replies/RGB captures. Remaining consumers/global composition and
the final full suite remain pending; routine tests stay BIG-only.

BIG's original direct-bar primitive now has independent WORD clip, literal
pass-order and exhaustive row-mask/endpoint-phase tests in `big_bar`. Opaque
holes copy pen0; the current device pen/active mask are distinct from the
latched tile, line XOR is ignored, and all point owners are preserved. Native
indexed failures retain the accepted prefix. No wire B binding is claimed;
its bar3d and input/attribute paths remain pending. Iteration stays BIG-only,
with the full suite deferred until the end of the goal.

BIG's shared DDO point producer has independent `big_producer` coverage for
all mode bytes/configured buffer-size WORDs, first-point duplicate/8001 marker
rules, run wrap and original versus native failure prefixes. Point mode2
retains carry-clear rejection and actual AX/BX/CX/DX returns, including the
batched pre-write-byte boundary. It leaves all point owners unchanged;
no native B/curve binding or planar framebuffer is claimed. Native read-byte,
bar3d and curve composition remain pending; routine tests stay BIG-only.

BIG B now implements its five/six-field input and attribute prefixes, opaque
bars and original bar3d inset/front/side/top schedules. WORD projection,
top BYTE, orientation, patterns/XOR, producer register effects and distinct
point owners have independent component/native expectations. Native tests
cover split/profile, source/scaling/flow/reply and timeout/EOF/reset/error
prefixes; batched byte reads reconstruct all four planes at the indexed
boundary.84 coherent application scenes bring coverage to1246, checking
opaque/XOR/box/top scenes with existing complete text/pixels/palette/points/
fill/cursor/replies/RGB observations. Page/image/curve/audio composition and
full45-entry acceptance remain pending; routine tests remain BIG-only.

BIG's flood kernel is synchronous without periodic work-budget yields;
framebuffer callbacks must not block or wait. `big_flood` checks original
WORD spans, directional/LIFO/pattern suppression, clipped/inverted probes,
whole-band plane-mask order, all mask/pen combinations and partial errors.
The bounded native frontier explicitly corrects unsafe SS:BP underflow;
overflow preserves pre-push state and reports BGI-7 after pending work.
Native F now binds three-field acquisition, clamps, fill-only attributes,
current selected border after XOFF, separate live loop tests around paired
flow and unchanged points. A512-record bounded native frontier and checked
indexed byte/masked pen writes integrate the kernel with both blink planes.
Native tests cover partitions/profiles, waits, source/replies, cancellation,
overflow, error prefixes and recovery.70 added application scenes bring BIG
coverage to1316, including solid/checker/empty F and rejected X4 followed by
valid graphics/ANSI/DSR on the same connection. Z and page/padding/configured
buffer composition remain pending. Routine verification stays BIG-specific.

BIG H now acquires one class0 value, stores the full WORD and clamps only
signed negatives to0. All65536 WORDs and retained state under uninstalled
BGI/old errors have independent expectations. Native partitions/profiles,
whole indexed planes, prefix/timeout/reset/EOF, I/R lifetimes and blocked/
deferred replies are covered.28 coherent application scenes bring BIG to
1344, verifying H256/negative0 while retaining complete scene/state/replies/
diagnostics and RGB. Z/O/Q/V/Y consumer composition and final full-suite
acceptance remain pending; routine tests remain BIG-specific.

BIG Z now composes four standalone outline strokes with the existing indexed
flood, gated after outline by the full H WORD. It preserves all point owners,
uses the wrapped first-corner seed and adds no own flow calls. Component and
native checks cover order/phase, H0/256, reversed/clipped/wrapped/degenerate/
thick/pattern/XOR cases and acquisition/source/error recovery.84 coherent
scenes bring BIG coverage to1428 with complete prior scene/state/replies/
diagnostics/RGB observations. Page/padding/configured buffers and global
composition remain pending; only BIG tests run during development.

BIG's indexed address/storage dependency now distinguishes active CPU
segment/WORD addresses from BIOS display-start/padding metadata, using the
visible bitmap as its authoritative window and borrowed backing elsewhere.
`big_video` checks every canonical aperture pixel and all pixels in40
display layouts, original BYTE/WORD arithmetic, padding, blink values and
precise extent-error prefixes. Native B segment-return expectations remain
covered; G wire/page/image/console/presentation binding is still pending.
No additional application scene or hardware/device acceptance is claimed.

BIG G4/G5/G6 now bind remembered memory, actual active and actual visual page
selection with original BYTE/WORD/resolution/publication rules. All graphics
consumers and batched reads use indexed active memory; console text/cursor
uses visual memory with four cursor banks. Both blink values and padding
persist, while host status artwork stays outside the video view and source
rows behind it stay in backing. Native tests cover all page WORDs, split/
profile/source/loop/reply cases, stride/display aliases, mode/I1 lifetimes
and nonfatal save/load/CPU-extent failures.140 new coherent application scenes
bring BIG to1568, checking complete retained pixels/cells/palette/state/points/
cursor/replies/diagnostics/RGB. G0..3 images/global acceptance remain pending.

BIG's image dependency now supplies the original WORD size query, four-byte
header, plane3..0 format and ordered capture over the shared indexed memory
interface. Conversion occurs at import/capture edges; source-byte reads use
cached indexed pens, including retained tails, with actual fixed-segment
cursor/extent checks. `big_image` has independent literal bytes, every size
delta, alignments/rows, partial failures, malformed heights and source/buffer
wrap expectations. Native G0..3/paste/lifetime integration and application
image scenes remain pending; existing1568 coherent scenes are unchanged.

BIG's image paste dependency now preserves physical right rejection,
unsigned bottom crop, ADD/JL flags and temporary header/error prefixes.
`big_image_paste` uses independent truth tables, full dual-phase memory and
byte/plane traces, every WORD destination/mode, alignment/boundary/crop cases,
source/destination wrap and checked partial failures. Indexed source reads
avoid packed decoding at blits; the Boolean kernel is shared with RIP's
indexed branch under reference83's matched domain. Native G0..3 ownership/
wire/page binding and application image scenes remain pending. Non-BIG
regressions are deferred to the final suite; existing1568 scenes are unchanged.

BIG now binds G0..3 through the common pre-ANSI numeric parser, preserving
argument/page/gate/mode prefixes, G1's original-corner/cache3 behavior and
G3's repeated active selection. An engine-owned image survives I/R/attention/
EOF/reset and starts defined/zeroed on reconnect. Independent native tests
cover splits/profiles/loops, overlap, separate pages/blink values, error and
partial-write prefixes, and indexed source behind protected host UI.154 new
coherent scenes bring BIG to1722 with complete pixels/cells/palette/state/
points/cursor/pages/replies/header/payload/RGB expectations. Native G images
are implemented; global45-entry acceptance and final regressions are pending.

BIG S now reads both class0 arguments before its shared feature gates, then
preserves original count/sign/silent-return and cache-before-device behavior.
Raw cache bytes remain distinct from six-bit physical palette colors; S0
changes logical background/palette0 while indexed pixels/images stay intact.
Independent exhaustive state tests and native split/profile/lifetime/error
fixtures verify all raw bytes against a separate RGB formula.98 new coherent
scenes bring BIG to1820, checking live/saved-image recoloring, raw cache,
silent rejection, capture under palette aliases and I1 restoration alongside
the complete prior scene/state/replies. X8/global/final regressions are pending.

BIG X8 now implements capture/restore and the original inclusive palette
walks with per-setter snapshots and a stale final wrap. Unsigned millisecond
delays use local monotonic deadlines; key cancellation follows the full wait.
Signed WORD count wrap, current flow/loop owners and the common footer are
preserved. Explicit zero-owned padding replaces the source's uninitialized
upper-endpoint byte. Independent component/native cases cover all endpoint
pairs, splits/profiles, keyboard results/scans, flow retries, deadline and
EOF/reset/error prefixes.126 new coherent application scenes bring BIG to1946,
checking palette/local/saved/RGB results and unchanged indexed pixels/images
alongside cells/points/fill/pages/cursor/replies. N/audio/global composition
and final cross-emulation regressions remain pending.

BIG X5 now configures the existing flow helpers through class0 incremental
reads. Post-read loop override, byte-before-count stores, independent live
mode checks and signed default/count rules follow the original caller.
Independent exhaustive component/native tests cover wire splits, partial
effects and lifetimes, source changes, actual counted NUL/ff output,
resumption and the common footer.126 new coherent captures bring BIG to2072,
checking current settings and exact flow/replies with the full unchanged
scene/state/images/RGB. Remaining commands/global45-entry and final
cross-emulation regressions remain pending.

BIG X2 now seeds the shared original generator before either class0 bound
read, publishes raw FROM before TO, and performs signed repair afterward.
Local wall seconds supply the low seed WORD; delays retain monotonic time.
Independent native tests cover full bound domains, partial/pending random
reads, source changes, WORD wrap/equality, lifetimes and nonfatal errors.
126 new coherent scenes bring BIG to2198, checking seed/range/RNG progression
with unchanged scene/image/state and exact replies. X0/speaker/global45-entry
and final cross-emulation regressions remain pending.

BIG X0 now installs colour/reseeds before its five incremental reads, scales
two shared random draws per attempt with signed WORD ranges, and calls the
existing indexed point/direct-marker adapters. It preserves helper CP tails,
point COPY, clipping without resampling, zero/negative counts and failure
prefixes. Independent component/native expectations cover the full signed
range domain, literal samples, profiles/partitions, hidden pages, source and
lifetime behavior.126 new coherent scenes bring BIG to2324 with authored
zero-range geometry, separate RNG checks and the complete retained scene/
images/state/replies/RGB. Speaker/global45-entry and final regressions remain
pending.

BIG X3 now installs custom text ratios after four completed class0 reads.
Unsigned maxima, returning size0, saved/effective/cache distinctions and
original1/1 initialization follow the source. No glyph/pixel/point operation
is added. Independent component/native tests check full input WORD domains,
partial/error/source/lifetime paths and complete unchanged scene data.
126 new coherent captures bring BIG to2450, checking text caches/ratios and
retained scene/images/state/replies/RNG. E/W font rendering, audio/global45-entry
and final cross-emulation regressions remain pending.

Historical coverage, superseded by the current-font policy below: BIG
previously embedded the four V100 stroke fonts and registered their
borrowed bodies at entry. Source SHA-256/compiled CRC checks, every glyph/
record and malformed-access fixtures verify the separate common CHR format
accessor and registry lifetimes. All2450 existing coherent scenes additionally
assert count10, mask0f, loaded0 and inactive body alongside unchanged scene/
state data. E selection and W rendering remain pending; this data/startup
coverage does not claim graphical-text conformance.

BIG E now installs original borrowed fonts with separate selected/loaded/
active/DDO/header/bitmap-device owners, late three-field clamps, original
positive/custom ratios and BYTE metrics. Font0/I0/R retain resources, custom
ratios and remembered header/id while resetting bitmap style; I1 retains
text. Native full-WORD/split/profile/source/lifecycle/error tests and182 new
coherent application scenes (2632 total) check exact text state alongside
the retained complete pixels/cells/palette/images/points/pages/replies/RNG.
Missing resources and unsafe accesses use documented checked nonfatal
fallbacks. Native E is implemented; W rendering, global45-entry composition
and final cross-emulation/full-suite acceptance remain pending.

BIG's direct stroke-text dependency now has literal Small AA COPY/XOR masks,
metric/decoded-advance distinctions, vertical/orientation/justification,
missing/zero-width glyphs, current bindings, line-cache restoration and
failure-prefix coverage in big_text_stroke. Shared CHR width/program accesses
preserve original failure ordering and shared thin raster reuse is retained.
All2632 existing coherent scenes additionally assert private line/text caches.
W bitmap source/raster, stream integration and full command acceptance remain
pending; these tests do not claim W wire coverage.

CGA 80x25 adds a 640x200 PC raster with 80x25 8x8 cells, 4:3 display aspect,
PC blink and cursor scanlines6..7. Stable mode32 appears before EGA in ordered
screen/canvas pickers without renumbering existing mode values or the
ID-indexed Wren name catalog. Native mode/startup/settings/profile/connection
and Wren tests cover geometry, ordering and persistence. `syncterm_cga_mode`
checks eight coherent CP437/UTF-8, status on/off and reconnect captures with
literal full-block rasters in both blink phases, palette/cells/cursor and RGB
interior samples. Physical audio/MIDI are disabled. This is the first
prerequisite for [BIG](BIG_CONFORMANCE.md); no BIG commands are claimed yet.

RIP, IGS and BIG command rejections are nonfatal warnings at the graphics input
stage. Native recovery tests and `syncterm_behavior_session_modes` verify
following graphics and ANSI/VT52 handling, continued bidirectional connection
use, and the normal notice on later remote disconnect.
RIPv1 now retains unfinished ANSI prefixes across SOH/STX graphics commands,
then applies completion suppression before resuming ANSI. Native split/bytewise
tests and the CP437/UTF-8 RIP wire suites check independent replies, cell colours,
cursor and both pixel phases; see [RIP154_CONFORMANCE.md](RIP154_CONFORMANCE.md).
RIP numeric fields now share signed base-36/low-word decoding and explicit
consumer byte/word conversions and index bounds. Exhaustive native word tests,
split-input vectors and coherent CP437/UTF-8 wire captures cover independent
numeric state, replies and dual-phase pixels, including the corrected 0.005
Bézier fallback. Remaining reference differences and safe rejection policies
are recorded in [RIP154_PARSER.adoc](RIP154_PARSER.adoc).
RIP `1G` now copies absolute screen regions with RIPv1 alignment quirks and
keeps its historical whole-plane, overlap, split-input and clipboard tests.
The separate RIPscrip 3.0 command, RIP_SCROLL, uses drawing-port bounds and
supports seven exposed-source fill modes. Connected CP437 and UTF-8 tests
cover absolute WORLD conversion, overlap and clipping, offscreen ports and
independent ANSI blink planes. [RIPv3 scroll coverage](RIP3_CONFORMANCE.md#connected-rip_scroll)

RIPv3 `1g` copies a rectangle to a new X/Y position and applies its fill
mode to the exposed source. Connected CP437 and UTF-8 tests cover diagonal
overlap, one-pixel copies, clipping, offscreen ports and independent ANSI
blink planes. [RIPv3 copy blit coverage](RIP3_CONFORMANCE.md#connected-rip_copy_blit)

RIPv3 `w` changes the positioned text window. The entry console retains its
selected raster, grid and local status row. Connected CP437 and UTF-8 captures
cover all five window fonts on 640×480, 800×600, 1024×768, 1280×800 and 720×400,
including font-specific cells, artwork in the default 640×350 WORLD, ANSI
resumption, and scene reset. The RIPv1 wire fixtures retain their original
canvas, status-font and historical pixel expectations.

RIPv3 HCL bitmap playback uses the current port and its stored image
rectangle. Connected CP437 and UTF-8 tests cover clipped screen and offscreen
draws with the existing indexed palette and dither renderer. [RIPv3 bitmap coverage](RIP3_CONFORMANCE.md#connected-hcl-bitmap-playback)

RIPv3 `$HOSTDIR$` and `2<ESC>` select a directory within the connected BBS's
RIP cache. Media lookup falls back to the read-only Global RIP Directory.
Connected fixtures check named overrides, bitmap reads and writes, `$FILEDEL$`
of cached files, protection of global and other-BBS files, and recovery after
malformed names. [RIPv3 host directory coverage](RIP3_CONFORMANCE.md#connected-ripv3-host-directory-selection)

RIPv3 `1F` reports resource metadata through the active connection.
Connected CP437 and UTF-8 fixtures cover exact reply bytes and bitmap alias
sizes. Native fixtures exercise open and metadata failures. [RIPv3 file-query coverage](RIP3_CONFORMANCE.md#connected-rip_file_query)

`$FILEEXISTS(filename)$` provides a presence result in RIPv3 query and
graphical fields. Connected CP437 and UTF-8 fixtures verify the cache lookup,
ordered alias rewriting and literal recovery from rejected calls. [RIPv3 FILEEXISTS coverage](RIP3_CONFORMANCE.md#connected-fileexists)

Wire `1D` defines variables in live RIPv3 sessions. The connected tests
exercise literal defaults, byte limits, confirmation, cancellation and
ordered queries in CP437 and UTF-8. Reconnection verifies persistence and
session-only overrides. [RIPv3 RIP_DEFINE coverage](RIP3_CONFORMANCE.md#connected-rip_define)

RIPv3 `D`, `d` and `2A` update and select the current drawing palette.
Live CP437 and UTF-8 fixtures verify six-bit PALENTRY replies, protection,
copy and restore, and recoloring of existing indexed pixels at portable
display precision. [RIPv3 palette coverage](RIP3_CONFORMANCE.md#connected-drawing-palette-commands)

RIPv3 tone variables use the mixer and resume the invoking query after
playback. Native request and PCM fixtures cover the tone sequences and
cancellation. Live CP437 and UTF-8 fixtures verify expansion and invalid
argument recovery with audio disabled. [RIPv3 tone coverage](RIP3_CONFORMANCE.md#connected-tone-variables)

RIPv3 `1R` and `$>NAME$` play cached scenes through the live console. Connected
CP437 and UTF-8 fixtures check nested return, persistent drawing state, local
ANSI text and partial sequence isolation, child reply ordering, `3e` pacing,
mouse actions and recovery after missing or recursive files. A `.TXT` scene
with an active article still executes its RIP commands and routes ordinary
text into that article. [RIPv3 scene playback coverage](RIP3_CONFORMANCE.md#connected-rip_read_scene)

RIPv3 `1T`, `1t` and `1E` lay out prewrapped text in global column slots.
Connected CP437 and UTF-8 tests verify original MicroANSI glyphs in both pixel
planes, literal text, viewport clipping and suppression, signed justification,
column transitions and recovery. Live Dixon and LITT.CHR cases independently
check stroke or outline placement, fit and advance. [RIPv3 column coverage](RIP3_CONFORMANCE.md#connected-prewrapped-column-text)

RIPv3 `1e` and nonzero-article `1T` accept inline or cached text. The same
connected fixtures check wrapping, search highlighting, definition changes
and return to ANSI at `1E` or `#`. [RIPv3 article coverage](RIP3_CONFORMANCE.md#connected-raw-article-flow-and-search-highlighting)

RIPv3 `1A` switches between independent article cursors and keeps their overflow
streams open. Connected CP437 and UTF-8 fixtures verify interleaved articles,
saved source replay, article zero, and malformed selectors.
[RIPv3 selection coverage](RIP3_CONFORMANCE.md#connected-article-selection)

RIPv3 `$OVERFLOW$` drives continuation scenes through the live connection.
Connected CP437 and UTF-8 fixtures click a pager field and verify saved-page
rendering, boundary recovery, reset scope and cache cleanup. Native tests
verify the two boundary notices and page-index truncation.
[RIPv3 pagination coverage](RIP3_CONFORMANCE.md#connected-article-pagination)

BIG's `cterm_big`/`syncterm_big_input` additionally verify every input split,
continued same-connection graphics/ANSI/replies and explicit last-error/count
diagnostics. Its native collection/fill/line/bar fixtures check retained
failure prefixes with successful terminal input/polls; pending commands,
sources, flow, replies and deferred pointer errors are cancelled on rejection.

Connected IGS tests verify that the transport owns flow control. They fence
drawing and playback with a version query and check the exact reply bytes,
including with custom X5 characters and large repeat counts. Serial profile
and driver tests cover the configured handshaking modes.

IGS 2.21 work is tracked separately in [IGS_CONFORMANCE.md](IGS_CONFORMANCE.md)
and [IGS_IMPLEMENTATION.adoc](IGS_IMPLEMENTATION.adoc). The shared graphics
byte-parser foundation has native split-input and raw-ownership coverage;
native image-format tests check indexed storage and boundary conversions.
X9 native and coherent application acceptance passed. Native tests cover its shared N/script buffer
loader, guarded execution/clear, retained holes, capacity, local deadlines,
file-style readers/flow, shared loop state, local input and cancellation.
The shared parser explicitly supports source-ended text and accepted return
boundaries without a second parser. Application X9 covers972 coherent captures
across18 profiles, including script scenes, waits/key ownership and reconnects.
All63 command entries, six required foundations and final IGS goal acceptance
are complete; the removed planar/clock experiments remain superseded.
Serial/file dispatch regression coverage now includes independent X8 requested
snapshot banks, shared raw snapshots, file X11 error retention, source-specific
X0 restore and F/f/z/t/N flow, and invalid-T direct chain exits. Expanded X9
coverage has972 coherent application captures. The common numeric reader now
performs the ordered r/R tests after array lookup, relative encoding and loop
evaluation, preserves stored-load suppression and avoids re-evaluating supplied
values. Native tests retain exhaustive signed-word coverage with independently
pinned sentinel results. The X2 application test adds54 coherent captures
across18 profiles for dynamic array selectors, operands and loops with literal
Random states. Final acceptance has230 distinct passing CTest checks, including
all126 IGS native/application checks, ANSI/VT52, RIP, SkyPix, profiles, Wren,
audio, graphics lifecycle and four native X11 checks on a private Xvfb display.
The final native FreeBSD Release CMake build has zero diagnostics. Physical
audio/MIDI were disabled in headless fixtures; the
[manual audio checklist](IGS_CONFORMANCE.md#manual-audio-checklist) is supplied.
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
original integer-division bug; drawing and stored-loop tests also exercise it.
`M` has native mode-selection/reset/input-boundary tests and coherent bitmap
checks for an unchanged scene, palette and window, across ST geometries,
Atari ST/CP437/UTF-8 and status choices. Primitive-specific raster tests cover
the writing modes during drawing. `C` covers independent
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
also check a following D. X dispatch covers all16 selectors; out-of-range
selectors are one-argument no-ops with normal chaining and no payload.
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
`O` adds aspect-corrected filled circles with original integer trigonometry,
adaptive 32–128-step polygon construction, fill patterns/perimeters and native
signed-radius rejection. Its 3,126 independent dual-phase cases cover all
fills/modes/depths and step counts; coherent application scenes cover glyph
composition/replacement, status/bottom pixels, following D and lifecycle in
every ST mode, encoding and status choice.
`Q` adds ellipses with independent X/Y radii using the shared filled-curve path.
Its 3,660 independent dual-phase cases cover all fill/mode/depth combinations,
zero/mixed-sign/tall/wide radii and every adaptive step count driven by each
axis. Shared circle/ellipse application captures cover independent scenes,
glyph composition/replacement, retained state, status and lifecycle throughout
the ST mode/encoding/status matrix.
`K` adds open circular arcs through native curve construction and existing
thin/wide lines, joins, caps and arrows. Its 3,876 independent dual-phase cases
cover native geometry/composition and safe periodic corrections for large or
negative angles. Coherent scenes include console-cell replacement, glyph
interaction, retained Drawto state, status protection and lifecycle across
all ST modes, encodings and status choices.
`J` adds elliptical arcs with independent X/Y radii and shared K/J angle-tail,
curve and line/cap execution. Its 4,659 independent native cases cover both
radius axes, styles/caps/widths, signed geometry and safe angle handling.
The shared application recipe checks 96 independent scenes plus glyph/state/
status/lifecycle interactions throughout the ST mode/encoding/status matrix.
`V` adds patterned circular pies with native center append, radial closure and
optional fill-colour perimeter. Its 3,819 independent dual-phase cases cover
fills/modes/depths, sweep/radius/step boundaries, clipping and safe angles.
Shared filled-curve captures check 120 independent scenes, text replacement,
glyph/XOR parity, status protection and lifecycle in every ST mode/encoding.
`Y` adds elliptical pies with independent radii through shared V/Y preparation
and patterned curve fill. Its 5,286 independent dual-phase cases cover both
radius axes, fills/modes/depths, clipping and safe angles. Shared captures check
132 independent scenes, including text replacement and tall ellipses, plus
state/glyph/status/lifecycle checks throughout the ST mode/encoding matrix.
`f` adds counted 1–128-point filled polygons through the shared patterned fill
path and z's count/evaluator/serial-flow dispatch. Its 4,056 independent
full-frame dual-phase cases include degenerate/concave/self-crossing polygons,
128 simultaneous intersections and native orientation-sensitive rounding.
Application captures check 168 independent scenes, text replacement, glyph/
state/status/flow/lifecycle throughout the ST mode/encoding/status matrix.
`F` adds seed-colour flood fill with native ordered live runs, stored bounds,
ignored perimeter, independent blink phases and an owned growable queue.
Its 3,660 independent dual-phase frames and 168 application scenes cover
fills/modes/depths, distinct phase topology, queue growth, text/graphics,
status/coordinates/state/flow and lifecycle through the ST mode/encoding matrix.
`E` adds graphical text attributes with native effect masking, six canonical
point/top selections and safe signed rotation state. Its 219,072 expected
state cases and 120 coherent application scenes verify sizing/rotation/effects,
unchanged console/graphics pixels, default restoration, parser and lifecycle.
`W` adds original graphical glyphs, source/doubled fonts, combined effects,
quadrant rotation, native clipping/light phases and underline. Its 22,554
independent dual-phase frames and 186 full application scenes cover all ST
modes, opaque/bounded text, console overlays/replacement, coordinates/status,
reader reuse/handshake, absolute local deadlines and recovery/lifetime.
`c` adds raw physical console colors through persistent native VT52 state.
Its 1,179,648 word cases, 1,536 color-pair cases, 3,696 complete-frame contexts
plus 2,688 printable-byte cases and 366 application scenes cover reverse
video, pending sequences, controls,
scrolling graphics, physical attributes, raw bytes and recovery/lifetime.
`p` adds unscaled console positioning through that same VT52 state.
Its 157,252 word/byte-class cases, 4,452 complete-frame composition cases,
6,144 byte-role frames and 360 coherent application scenes cover wrapped
coordinates, pending controls/colors, status boundaries, unchanged graphics,
scrolling and parser/error/reset/disconnect recovery.
`m` adds repeated VT52 movement with signed selector/count behavior.
Its 70,404 algebraic cases, 1,764 complete-frame contexts and 147 application
scene inputs verify zero/negative counts, maximum repetition, ESC parity,
pending parameters, edge behavior, both pixel phases and recovery.
`r` adds one-shot Atari cursor restore with a home fallback through
the shared VT52 save slot. Coverage includes 672 native scenes and
108 application captures with literal glyphs, pending parameters,
mode initialization and reconnect.
`k` adds counted Atari cursor hide and forced enable with live index
inversion in the presenter. Native input cases and 180 coherent application
captures cover blink, movement, drawing under the cursor and serial
destructive backspace through the pending VT52 state.
`v` controls Atari reverse video through the same VT52 handlers. Its 336
native scenes and 72 coherent application captures cover pending escape
and parameter states, retained text and graphics, raw background on
scroll, and reset and reconnect.
`w` controls Atari wrapping through VT52. Its 336 native scenes and 90
coherent application captures cover pending console input, immediate
right-edge wrap, overwrite, scrolling with graphics, status row
boundaries, and reconnect.
`l` adds three inclusive Atari VT52 line erasures with raw background
color and a depth-dependent destination column for whole-line erase.
Its 492 native scenes and 90 coherent application captures check the
erase range, graphics and text, cursor presentation, status bounds and
reconnect.
`s` adds full and partial Atari VT52 screen erasure, a VDI clear to
index zero, and the original mode-dependent color-reset sequence. Its
498 native scenes and 126 application captures cover erase ranges,
palette preservation, pending state, scrollback and reconnect.
`d` adds repeated VT52 line deletion. Its 433 native scenes and 90
application captures cover signed counts, pending state, indexed text
and graphics movement, raw background, cursor, history and reconnect.
`i` adds repeated reverse index and line insertion through the same
VT52 state and indexed row movement. Its 938 native scenes and 126
application captures cover top and bottom edges, signed counts,
pending input, raw background, cursor and reconnect.
`t` adds a cancellable local pause with flow control and retained
input. Its 252 native duration contexts and 74 coherent application
checkpoints cover deadline boundaries, key consumption, suffix
resumption, errors, reset and reconnect.
`q` adds sync waits and retained double-copy control. Its 66,346
native duration cases and 145 coherent application checkpoints cover
count boundaries, control lifetime, key forwarding, suffix resumption
and reconnect.
`X1` adds raw palette registers, signed index aliases, negative-color queries,
complete raw-word retention and mono polarity, preserving the VDI color cache.
Its 197,994 native word cases and 126 coherent application captures verify
retained indexed pixels, unscaled operands, parser recovery and reconnect.
`X12` adds four-register groups through the same raw palette service,
including negative group aliases and two zero-color arities. Its 67,048
native cases and 126 coherent application captures cover group order,
query/write mixtures, parser partitions, retained pixels and reconnect.
`X7` loads sixteen source-pattern rows through the shared W text reader.
Completed rows retain their shared-window edits; A4 selection makes an
independent active copy. Native reader/partition/lifecycle checks and 108
coherent application captures verify pattern use by Z, including all ST
modes, encodings and status choices, with audio and MIDI off.
`X2` configures both random ranges with ordered operand evaluation and
conditional arity. Its 75,238 native cases and 126 coherent application
captures cover range repairs, input partitions, retained scene and reconnect.
Valid narrow ranges now allow a full relevant generator period.
`X0` adds byte-sampled marker scatter, bounded resampling and persistent
color cycling through the shared P raster primitive. Its 1,152 native
raster cases, signed-word/state tests and 126 coherent application captures
cover shapes, writing modes, cycles/cache rules, normalized extents,
retained Drawto state, XOR over text and reconnect.
`X13` adds bounded signed-word array math and decimal console/graphical
output through the existing renderers. Native exhaustive word/index tests
and 144 coherent application captures check arithmetic, safe division and
bounds, staged W/X7 buffer reuse, typed coordinates, exact pixels/cells/RGB,
input partitions, recovery and reconnect, with audio and MIDI off.
`G` adds all sixteen indexed Boolean screen/memory transfer operations,
retained capture coordinates and stride views, phase-preserving images and
local second-copy deadlines. Independent native pixel/row-alias vectors and
coherent application scenes cover capture/replay, hidden memory edits,
clipping/status protection, input recovery and reconnect.
`X14` selects G's persistent transfer route. Native signed-word and lifetime
tests plus 162 coherent application captures verify both banks, opposite
G1/G3 cross-memory directions, self-copy selection, disabled routes and
reset/reconnect defaults without immediate raster or console side effects.
`X11` adds native-byte bitmap wipes and incremental raw imports into indexed
memory 0, with optional single display, retained image suffixes and local
receive deadlines. Independent native tests and 191 coherent application
captures cover all ST modes, encodings/status choices, random fills, timeout
accumulation, opaque controls, binary Telnet escaping and reconnect. The shared
parser exposes generic raw-field observation/early completion; bitmap policy
remains in CTerm. Physical audio and MIDI are disabled in these fixtures.
`X8` adds requested/raw palette rotation, dual snapshots and sticky restore
selection through the existing palette backend and local deadlines. Native
checks verify every intermediate write across 2022 independent cases plus
input/lifecycle/error/max-count coverage. Its 288 coherent application
captures check unchanged indexed scenes, palette/RGB/cache state, queued
commands, cancellation and reconnect across all ST modes/encodings/status
choices, with audio and MIDI disabled.
`X15` adds anchored sweeps through the existing polyline renderer, with
exclusive unscaled bounds, original endpoint retention and exact local
post-line waits. Wider host arithmetic fixes wrapped/infinite sweeps.
Independent 600-case native compositions and 451 coherent application
captures cover ordered patterned/wide/arrow/XOR drawing, console overlays,
Drawto, status/input/error/lifecycle boundaries and key forwarding during
waits, across all ST modes/encodings/status choices with audio/MIDI off.
The GIST software-voice foundation adds standalone register/record tests:
1044 independent envelope cases, 160 API cases, all pitch/control words and
original-patch lifetime checks. These do not establish PCM playback or b/n/N
command support; PSG synthesis has separate functional tests below.
Live audio-source tests additionally cover sample delivery and acknowledgment,
clear/detach and concurrent stream destruction/engine stop with an active
callback, using the existing consumer and device-free pull runtime.
PSG tests add literal register/envelope/gate/noise expectations, rational
local-sample phase/frequency checks, DC probes/filter tails, partition
invariance and all 20 original GIST patches. No foreign clock or reference
PCM-byte identity is required. The b command now adds editable source/saved
patch banks, exact trigger counts, restoration and stop-all, through a dedicated
session stream shared by GIST and console sounds. The Program Settings
"Atari ST Key Clicks" toggle suppresses local clicks independently of BEL and
remote effects, applies to live sessions, and survives terminal reset and
application restart. Native audio, settings/runtime and headless editor tests
cover these preference paths; physical-device audibility is not claimed.
Disabled audio advances on
local deadlines without PCM; fast phase/noise jumps prevent replay on re-enable.
Native input/lifecycle/error tests and 198 coherent application captures cover
all ST modes, Atari ST/CP437/UTF-8 and status choices, with physical audio/MIDI
off. The n helper adds private normalization, priority-7 mutable-patch notes,
local deadline/key waits and selected/all release/stop, with 235 independent
helper cases and 306 coherent all-mode/encoding/status application captures.
It corrects low-word clock-wrap early exits and preserves source/caller state,
queued input and key ownership. N adds retained raw MIDI/chip buffers, byte
alignment, replay ranges, distinct inner/outer keys, flow and local deadlines
through those existing helpers. Native buffer/backpressure/lifecycle tests and
342 coherent all-mode/encoding/status application captures check opaque input,
retained records, unchanged scenes and reconnect. No audible/device coverage
is claimed.
X5 now configures flow mode and custom XON/XOFF bytes/counts through staged
numeric fields. Literal native cases and 288 coherent application captures
cover signed/loop rules, partial stores, unchanged scenes and reconnect across
all modes/encodings/status choices with physical output disabled. Standalone
checks retain the Atari serial byte/count expectations. Connected checks
verify that the version reply follows each command without any flow bytes.
X3 now loads and sends opaque right-button macros, retaining its individual
configuration stores and old payload suffix. All-buttons release resumes the
serial suffix and queued original key events. Literal native coverage and423
coherent all-mode/encoding/status application captures check exact raw bytes,
unchanged pixels/palettes/glyphs, local/Wren mouse routing, UTF-8 key replay and
disconnect/reconnect with physical audio/MIDI disabled. N and X3 share the
owner-side input queue. Complete command, loop and input coverage is recorded
in the IGS ledger.
X4 adds48 retained zone definitions, individual scaled coordinate stores,
signed-word length clamping, raw auxiliary loads/discard and strict-loopback
controls for the `<` consumer. Native literal tests plus432 coherent
all-mode/encoding/status application captures verify opaque data, unchanged
scenes, queued keys and EOF/reconnect, sharing X3's auxiliary service with
physical sound/MIDI disabled. Negative zone memory escapes are rejected.
X6 adds exact left-only release CR/CRLF and immediate right-only handoff to
X3, with shared owner-side key/serial suspension. Native byte/word/split/
callback/lifetime cases and675 coherent all-profile captures verify raw output
order, unchanged scenes and Wren/UI/disabled/scrollback/reconnect behavior.
Only mode byte2 appends LF; initial both-buttons does not enter the left hold.
`?` adds raw version/cursor/pixel/resolution replies and owned interactive
cursor/marker/GEM input. All8 original AES forms use indexed save-under and
exact native masks/hotspots. Native independent form/reply/lifecycle cases
and864 coherent all-profile captures cover overlay cleanup, prior mode/host
visibility, press versus release state, queued keys and reconnect. Uninitialized
motion, write-mode restore and malformed decimal bugs are corrected explicitly.

`<` collects keyboard or X4 zone input and defers its raw reply until the
whole graphics chain exits. Keyboard echo uses the existing VT52 parser;
mouse forms share indexed pointer rendering with `?`. It retains129 printable
bytes, exact press-zone selection and strict retry, with safe Delete bounds
and cleanup on EOF/reset/error.

Shared graphics-parser tests additionally cover source-supplied typed numbers,
retryable waits and exact handoff to wire fields, plus direct/chained command
selection and suspended-view ownership. IGS `&` now uses these sources for
stored-operand and gang execution, with local deadlines and pending console input serviced between
commands. Expiring the operand range can hand remaining fields to the wire.
Independent native traces/scenes and 847 coherent application captures cover
flow, text/raw ownership, deferred replies, cancellation and reconnect.

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
Primitive rendering edits the native indexed/RGB raster in place and preserves
untouched phase pixels. Native tests check exact damage, canvas/stride boundaries,
transparent glyph revision and exhaustive brush minterms/masks; application tests
check literal corner clipping. See the SkyPix raster implementation notes for the
remaining restoration, depth and scrolling snapshots.
The RoF caller-header regression retains a native font-reset baseline across
horizontal ANSI movement, so the following line cannot erase the lower glyph
rows. Literal complete-scene expectations cover both phases, variants,
CP437/UTF-8, status choices and 640x200/640x400; native tests cover every input
split and horizontal/vertical movement aliases.
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
The RIP cases include an actual RIP_PIXEL command. The RIPv3 case scrolls
three 19-pixel MicroANSI rows and checks the original scanlines in the
16-pixel display grid for both live and saved history. SkyPix includes native Pearl
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

`cterm_music_render` holds foreground ANSI playback, APC audio waits on
channels 0, 1, 2 and 15, BEL playback, response output and application APC
hooks while the real presentation worker flushes independent pixel damage
and renders both cursor blink phases through the shared bitmap presenter.
CP437 and UTF-8 cases retain bytewise music input and following-text ordering,
then check normal completion, flush cancellation, audio shutdown and reset
recovery. APC dispatch closes the active parser display's batch before cache
I/O or callbacks, including APCs routed from the status display to its parent.
BEL and response callbacks likewise release their batch for playback or
transport backpressure. These are native headless rendering tests using
pull-only audio, without physical audio or MIDI output.

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
preserve cell pairs and cursor advancement. A keycap that widens at the right margin wraps
and leaves a space with the current colours over the original character.
At 8×8, the VS15 watch narrows in place and its former continuation becomes a
styled space under the cursor. Native tests also cover narrowing at the bottom
right with deferred wrap and after an immediate scroll.
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
RIP raster checks compare both blink phases against the historical graphics
references and add literal packed-index, viewport/stride, RGB fallback and
clipboard expectations. Flood fill restores v1.10a's two-phase viewport output;
the single-plane-source blit contract is checked explicitly.

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
| `SGR` | 970 | attributes | Logical RGB cell colours and rendered planes. Connected RIP3 tests also check indexed foreground and background colours on entry in Custom screen mode. |
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
| `SGRNeg` | 1569 | attributes | Cell colours and rendered glyphs with intensity applied before reversal, including repeated SGR 7, SGR 22, SGR 27 and reset |
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

MIDI's `music_parse`, `music_decode`, `music_calibrate`, `music_player` and
`music_stream` tests
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
Live MIDI shares the worker and decoder, retaining partial messages, running
status, SysEx and held voices across writes. Literal split-input tests cover
wire recovery and realtime; injected-sink tests cover bounded continuous PCM,
raw/file/ANSI transitions, copied FIFO ownership/backpressure, completion
positions, flush/fade/error recovery and in-flight shutdown quiescence.
IGS N uses this queue; its parser/session coverage is tracked separately in
the IGS inventory. No physical-device coverage is implied.
Optional final-output captures support independent loudness comparisons;
unweighted RMS is not used to claim equal perceived loudness.
Manually selected synth checks compare onset and sustained levels across
eight pitches and verify dry ANSI/file-effect restoration where supported.
They render into discarded buffers or the pull-only mixer without devices.
The `extensions` control group checks feature 102 with MIDI disabled, rejected
cache-path recovery and seven-octave ANSI capture recovery in CP437/UTF-8.
These tests do not claim native MIDI-device or auditory coverage. MIDI and
PCM masks are both disabled in shared headless application fixtures.


BIG and RIP now share current bitmap/CHR font data and actual glyph kernels.
Historical V100 copies were removed by user decision; no compatibility font
variant is required absent artwork demonstrating a need. Font-version metric
changes are intentional. BIG's bitmap/outtextxy primitives have source-order,
placement/count/point and failure coverage; W wire integration remains pending.
BIG also retains normal ANSI cursor visibility through entry and I/R, with
native visible/hidden and coherent connection/remote-hide checks.


Native BIG W now integrates its existing text reader with current shared
fonts and glyph kernels at the common pre-ANSI boundary. It preserves
colour-before-text ordering, opaque byte/count/timeout/reuse/flow semantics,
separate graphics points, bitmap COPY versus stroke XOR, pages/images and
nonfatal partial effects. Native full-plane/profile/split/lifecycle/error
checks and224 new coherent scenes (2856 total) cover the complete wire path
and retained scene/state/replies. Reset during timeout cancels drawing.
Global45-entry composition and the final full regression suite remain pending.

BIG t now drains local keys before its shared class0 numeric operand, clamps
signed seconds to1..30 and pauses parsing with a local monotonic deadline or
keypress. Existing session/connection buffers retain the unread tail. BIG
sends no automatic XON/XOFF or custom X5 flow bytes; X5 remains protocol state.
The shared session pacing policy does not accumulate baud credit during a
parser pause or its resume tick, keeping the buffered backlog paced.
cterm_big_pause covers split/profile/scene/state/source/reply and recovery
semantics; big_session_pacing exercises a real POSIX shell connection in both
encodings, paced/unpaced. The coherent BIG fixture now contains2884 scenes.
Other emulation/session regressions remain deferred to the goal's final suite.

BIG ? now implements version/late-registration, cached-resolution and retained
music-buffer availability inquiries through the existing pre-ANSI boundary.
Replies use automatic response origin; deferred counted input keeps user
origin. Native cterm_big_query tests independent byte requests/current reads,
all WORD/byte cache values, split/profile/full-scene retention, readiness/retry,
partial/error/reset/source/footer composition.98 new coherent BIG application
scenes bring coverage to2982. Sound availability currently0 reflects the
pending native music-buffer allocation/playback foundation. No query serial
flow control or foreign UART/clock model is introduced.

The common BGI component now owns the existing91-word sine table and exact
sine/unsigned-radius product used by RIP and BIG. BIG adds its own wrapped
WORD cosine, quadrant metric and unsigned-radius/signed-aspect scalars.
big_curve_scalar checks independent literal/table/endpoint, exhaustive WORD
and boundary/error expectations. K wire/rendering integration is pending;
no BIG scene or RIP capture baseline changed. RIP and other shared-code
regressions remain deferred until the goal-final full suite.

BIG's decimal-outline arithmetic now has an independent request-trace fixture,
big_curve_decimal: original zero/metric/order/duplicate rules, checked/wrapped
32-bit recurrence, logical half, captured WORD geometry and error prefixes.
It does not bind native batch/producer/current-point/arc state or register K
on wire yet. The existing2982 application scenes and other emulation baselines
are unchanged; full regressions remain deferred to goal completion.

BIG decimal curves now bind the request generator to the existing indexed
producer and shared bar3d move gate. big_curve_draw independently checks full
indexed scenes, current colour/batch methods, observable returned point values,
all three point owners and partial/error prefixes. No planar shadow is added.
The ordinary native sixteen-pen binding is verified at component level; curve
fronts, arc state, angular buffers and K wire/CTerm acceptance remain pending.

BIG's selected regular-outline front now preserves zero-span/stale-offset
bookkeeping, original endpoint shortcut thresholds and ordered arc publication/
producer clear. big_curve_regular checks literal records, full indexed scenes,
WORD/wrapped-angle boundaries, current publication reads and error prefixes.
Direct original A55 bytes show separate offset/published storage, correcting
the reference prose's repeated-zero record-drift claim. Angular buffers,
public wrapper and K wire/CTerm integration remain pending.

BIG lower outline drawing now includes the angular buffered route and original
ordered producer flush. big_curve_angular checks independent indexed scenes,
363 closed-turn records/1098 XOR writes, duplicate strokes, marker/point/style
owners, capacity/error prefixes and WORD-loop termination. Mode-state tests
check literal29/50/69 metrics. A true infinite signed angle loop becomes a
checked native error after a complete WORD turn. Public K wrapper, owned
buffer/wire/CTerm integration and sector composition remain pending; no new
application scene or non-BIG regression claim is added.

BIG public ellipse/circular helpers now preserve lower center placement,
ffff query order, DDO-only successful restoration and failed-operation
prefixes. Library aspect state follows native mode installation and retained
I1/skip/failure paths. big_curve_public independently checks full indexed
quarter/shortcut/decimal/thick scenes, all three aspect profiles, query/point
owners and error boundaries. Native graph-buffer/CTerm ownership and K wire/
application acceptance remain pending.

BIG K circular arcs are now wired at the shared pre-ANSI boundary with original
operand classes, late colour publication, public/query/aspect/point ownership,
decimal/angular rendering and source/footer recovery. One owned graph workspace
backs mutually exclusive native curve/flood scratch views. cterm_big_curve
checks coherent full indexed planes, split/profile/page/source/lifecycle and
nonfatal error cases; the application fixture adds98 related arc scenes and
retains all prior pixel/font/palette/image/state/RNG expectations. Native K
integration is implemented; global/final regressions remain pending.

BIG J explicit-radius arcs now share the K native indexed renderer and
pre-ANSI/source/footer/recovery boundary. Six original operand classes keep
RY scaling distinct from circular aspect conversion. cterm_big_curve checks
independent full planes, points/records/replies, numeric lifecycle, geometry,
scaling/source bypass, active pages and nonfatal recovery across all profiles.
The application fixture adds98 J scenes,3178 total, retaining the complete
preceding K and native graphics scene. Native J integration is implemented;
remaining command/audio/global and final regression acceptance stay pending.

BIG O circles now use the existing circular outline followed by conditional
center-seeded boundary flood. Fill is installed even when hollow; normal
empty outlines still permit flood. Independent cterm_big_curve tests cover
literal aspect/outline/interior pixels, point owners, partial input/errors,
patterns/pages/source and shared scratch. The application fixture adds98 O
scenes,3276 total, retaining every preceding K/J and graphics-state expectation.
Remaining commands, audio/global acceptance and final regressions are pending.

BIG Q explicit ellipses now share the existing outline/center-flood path,
with line colour installed before numeric input and fill installed only
after a normal nonhollow outline. Independent native cases check early and
reader-changed colour, partial/error fill prefixes, explicit radii/scaling,
full indexed planes/point owners, pages/scratch and recovery across profiles.
The application fixture adds98 Q scenes,3374 total, retaining all preceding
K/J/O and graphics-state expectations. V and global/audio/final acceptance
remain pending.

BIG sector prerequisites now include the original polygon scan converter.
big_polygon independently checks indexed bar scenes and literal row/span/
colour/marker/scratch/WORD-arithmetic/error state, including shared-buffer
aliases and captured colour restoration. Raw-line requests are distinguished
from bars; native physical binding and V/Y sector/wire acceptance remain
pending. No new application scene or non-BIG regression claim is added.

BIG sector buffer stages now compose polygon admission, both fill/outline
orders and piece closure with current-count subtraction. big_curve_stages
checks independent indexed scenes, exact call order and mode/count/point/
error prefixes, including empty fill. V/Y construction, physical binding
and wire/application acceptance remain pending; no new capture is claimed.

BIG lower sector construction now composes shared decimal/angular work,
polygon stages and producer-gated radials with original normal/alternate
fronts and separate point/arc ownership. big_curve_sector checks independent
indexed grids, spans/records and failure prefixes. Physical polygon binding,
public V/Y wrappers and wire/application acceptance remain pending; no new
application capture or non-BIG regression result is claimed.

BIG polygon/stage/lower-sector fixtures now use production physical span
operators. Ordinary spans retain clipped opaque patterns; alternate spans
retain raw line patterns/modes without viewport clipping or thickness.
big_polygon_span checks independent grids/write prefixes and the explicit
out-of-geometry native failure boundary. CTerm V/Y binding and application
acceptance remain pending; no new application capture is claimed.

BIG V circular pieslices now use the shared pre-ANSI parser/numeric source,
public aspect/sector wrappers and original producer/polygon schedules through
native indexed active pages. Independent split/profile, fill/outline/error/
lifecycle and coherent application expectations check distinct point/arc
owners and normal-return current-fill application. V equal angles retain two
radials and ffff is not a query. Y and global/audio/final acceptance remain
pending; physical audio/MIDI are disabled in the headless fixtures.

BIG Y explicit-radius sectors now share V's native fill/line/sector schedule,
using their own class1/2 radii and the public explicit-sector wrapper. Native
split/profile/scaling/lifecycle and coherent application tests independently
check tall/wide/interior/zero-axis output, point/arc owners and error recovery.
All six BIG curve commands have native wire integration; p/s/X7, audio/global
and final-suite acceptance remain pending. Physical audio/MIDI stay disabled.

BIG p now applies cached byte/window console positioning through the shared
pre-ANSI numeric parser and existing native page/cursor owner. Independent
WORD, split/profile, saved/visible-page, lifecycle and coherent captures check
actual coordinates without moving graphics points or changing pixels/cursor
visibility. Checked native-window failures retain the page0 save and recover
without disconnecting. s/X7, audio/global/final acceptance remain pending.

BIG s now distinguishes full allocated BGI active-page clears with graphics
point reset from cached console-window blanking on the displayed page.
Independent byte/plane/pass/tail and native split/profile/application checks
retain different cursor/home/attribute owners, partial-mode3 extents and
nonfatal failure prefixes. X7/audio/global and final-suite acceptance remain
pending; physical audio/MIDI stay off in headless fixtures.


BIG X7 now stores its raw class0 WORD and applies a resumable music-progress
gate at each acquired pair, before fields/operands/unknown dispatch. Internal
body repeats bypass it; chains resnapshot. Live LOW/HIGH/scratch release,
flag retention, queued-disable/key/tail ownership and cancel/reset/EOF/error
boundaries are independently tested. The serialized playback-progress owner
is ready for audio integration; b/n/N and actual playback remain pending.
The coherent BIG fixture adds112 X7 scenes with all earlier captures retained;
headless physical audio/MIDI remain off. See BIG_IMPLEMENTATION.adoc for the
current non-audio publishing checkpoint and deferred full-suite requirement.


BIG's standalone YM3812 foundation now uses the MIT Opal reduction with nine
channels/eighteen operators/four waveforms and no OPL3 banks/pairing/stereo.
Original WSE/CSM additions have independently authored control and PCM tests;
wide mono sums preserve headroom before future source normalization. A BIG
headless application fixture verifies the complete MIT license through the
normal UI. b/n/N playback, real X7 callback progress, ?2 allocation and level
calibration remain pending; no physical audio/device coverage is claimed.


BIG's shared b/n speaker source now uses an averaged square with a carried
one-pole low-pass and fixed source gain0.279224085. Independent native tests
cover phase/frequency/stop/skip/partition/error state, literal PCM, harmonic
softening and weighted reference level through the real device-free mixer.
Normalization is measured during development and hard-coded; no BIG runtime
calibration is added. b/n wire and stream ownership remain pending.


BIG n notes/sweeps now use the shared fixed-gain speaker source on existing
logical channel1. Independent tests cover dynamic class0 acquisition,
endpoint-exclusive signed WORD sweeps, retained low-frequency requests,
unsigned local-millisecond waits, split input and complete retained scenes;
real session pacing holds the backlog without XON/XOFF. Device-free mixer
checks include channel controls, phase/mute/stop/FX handoff and callback
shutdown. Deferred failures preserve ordinary ANSI input. b/N playback,
continuous-source fade-out flush, actual music progress/allocation and the
full-goal regression remain pending; no physical audio coverage is claimed.


Native FreeBSD CMake Release configure/full builds (/usr/bin/cc, build/) passed without compiler/source diagnostics. All64 BIG-specific native CTests passed in76.01s. After bounding the test consumer demand and grouping audio inspection, the final focused speaker/note/pacing checks passed in3.58s (note0.76s, pacing2.71s). syncterm_big_input passed in199.26s total (test199.19s),4046 coherent scenes including112 new n scenes with all earlier expectations retained. The first application attempt exposed capture-field exhaustion; related audio fields were grouped without weakening any scene assertion. No non-BIG tests ran and no physical audio/MIDI/auditory coverage is claimed. Native n integration is verified; b/N, continuous-source fade-out flush and goal-final acceptance remain pending.

Final native full rebuild after const-correct inspection cleanup was warning-free; cterm_big_note passed again in0.81s total (test0.72s).

Native n/channel1 integration commit: `a999eae`. The full BIG goal remains active; b/N and the recorded audio/global/final-suite requirements are still outstanding.


BIG channel1 continuous speaker sources now honor APC fade-out flush using
the existing mixer overlay envelope. Pulls stop at the finite sample endpoint;
channel/source gain stays fixed. Buffered counts and drain include pending
fade work/backend acknowledgment, while clear/replacement/stop wake drains.
Independent BIG tests cover literal envelope samples, pull partitions, exact
callback limits, large/zero/replaced fades, queued-stream preservation and
concurrent completion/cancellation; the native n fixture covers actual APC
O=4f and final idle/gate behavior. b/N and goal-final acceptance remain pending.


Native FreeBSD CMake Release configure/full rebuilds (/usr/bin/cc, build/) passed without compiler/source diagnostics. Final4 focused BIG CTests passed in3.60s: big_speaker_stream0.01s, big_speaker0.03s, cterm_big_note0.77s, big_session_pacing2.71s. The native APC fade case failed before implementation. No physical audio/MIDI/auditory coverage or non-BIG tests are claimed; unchanged4046 bitmap application scenes were not rerun. Shared audio regressions remain part of the goal-final full suite. b/N and full-goal acceptance remain outstanding.

Continuous-source fade-out flush implementation commit: `a4a4b1e`. b/N and full-goal acceptance remain pending.


BIG b now plays all twenty original procedural effects through the shared
fixed-gain channel1 speaker. Original F/Z/D/WORD/RNG order is independently
tested; the user-selected native timing grants one44.1kHz sample to each
frequency request, including no-ops, plus explicit delay spans. Pure tests
pin all selector WORDs, literal schedule boundaries/counts and closed-form
RNG state. Native coherent tests cover three modes, CP437/UTF8/status, splits,
stored loops, retained scenes/replies, physical mute, parser/key backpressure,
shared-RNG handoff and concurrent reset/teardown. Real session pacing covers
b/t/n tails without flow bytes. N and full-goal regressions remain pending.


Native FreeBSD CMake Release configure/full rebuilds (/usr/bin/cc, build/) passed without compiler/source diagnostics. All67 BIG-specific native CTests passed in66.99s, including cterm_big_effect1.43s, cterm_big_note0.79s and real b/t/n session pacing4.06s. syncterm_big_input passed in223.97s total (test223.92s),4102 coherent scenes including56 new b scenes; every earlier expectation remains. The native stored-repeat fixture was corrected to reflect TO being reached during the next operand read before its playback completes. No non-BIG tests or physical audio/MIDI/auditory coverage are claimed. b/n are natively integrated; N playback/resources/progress, the manual checklist and full-goal regression/acceptance remain pending.

Native b procedural-effects integration commit: `534a350`. The full goal remains active for N music/resource/progress integration, the manual checklist and global/final-suite acceptance.


BIG N now has independent WORD/pair/slice helpers, a resumable original
record callback sharing X7's state definition, and the complete175-write
normal warm sequence. Tests retain post-endpoint continuation, stale loaded
versus allocated data, signed32/WORD boundaries, zero delays, selected I/O
retry/error prefixes and progress-gate composition. Literal reset data and
actual Opal tests cover waveform enable and preserved chip history. Wire N,
raw loading, native channel0 playback/controls/gain/lifecycle and final global
acceptance remain pending.


Native FreeBSD CMake Release configure/full builds and final rebuild (/usr/bin/cc, build/) passed without compiler/source diagnostics. All69 BIG-specific native CTests passed in64.20s. After completing the independent warm timer/status invariants, all5 focused music/warm/Opal/X7 checks passed in0.56s total (big_music0.01s, big_music_warm0.00s, cterm_big_sync0.45s). No physical audio/MIDI, native wire N, music allocation/PCM gain/stream ownership or non-BIG regression acceptance is claimed. The unchanged4102 application scenes were not rerun for this standalone dependency. Full-goal N integration and the goal-final full suite remain pending.

N record/arithmetic/warm dependency implementation commit: `d417771`. Native wire loading/playback, actual music resources/progress, owning channel0 controls/fixed gain and full-goal acceptance remain pending.

BIG's N foundation now includes continuous signed32 PCM through existing
channel gain/summation/limiting and an owning st_music source handoff on
channel0. Queued/device PCM remains signed16. Volume/ramp state, finite
fade/drain acknowledgment, context-matched quiescent detach and muted logical
ownership use existing controls. Independent BIG tests cover every signed16
amplitude, wide headroom/cancellation, literal fades, source/worker quiescence,
ANSI/live-MIDI takeover, rejected input and failure/shutdown paths. Native N
wire playback/resources/progress/fixed gain and final full-suite acceptance
remain pending; no physical device or auditory coverage is claimed.

Native FreeBSD CMake Release configure/full build (/usr/bin/cc, build/) passed without compiler/source diagnostics. All71 BIG-specific native CTests passed in64.18s, including big_music_channel0.01s and big_music_pcm0.07s. No physical audio/MIDI, audible-device tests or non-BIG regressions are claimed. The unchanged4102 application scenes were not rerun for this standalone ownership/headroom dependency. N wire loading/playback, actual resources/progress, fixed OPL2 gain and full-goal acceptance remain pending.

Wide PCM/channel0 ownership foundation implementation commit: `69eb9d6`. Native N wire loading/scheduling, actual music resources/progress, fixed OPL2 gain and full-goal acceptance remain pending.

N's standalone component now checks its actual65534-byte storage independently
of loaded length and converts delay arms to local44.1kHz spans with fractional
carry. Rate changes rescale remaining local time; due record work stays due.
Independent tests fix reference writes at samples91/459 across work/sample-span
partitions, and cover live/stale bytes, bounds/error prefixes, maximum rate
arithmetic, zero/subsample delays, missing markers, cancellation and X7.
No foreign timer/IRQ/BIOS clock, automatic keyoff or artificial hold is added.
Native wire N/resource/source integration and full-goal acceptance remain open.

Native FreeBSD CMake Release configure/full build (/usr/bin/cc, build/) and final focused rebuild passed without compiler/source diagnostics. All8 focused BIG music/Opal/X7 CTests passed in0.58s, including the new big_music_schedule0.01s and native cterm_big_sync0.41s. Independent schedule/storage expectations were added before implementation; the initial focused link failed on the missing new functions. No physical audio/MIDI, N wire playback, non-BIG regressions or unchanged4102 application scenes were run for this standalone component. Native buffer allocation/publication, N wire loading/source integration, fixed OPL2 gain and goal-final acceptance remain pending.

Checked music storage/local sample schedule implementation commit: `9bb725e`. Native N wire/resource/source integration, fixed OPL2 gain and full-goal acceptance remain pending.

Native BIG N0..5 now integrates allocated music availability, opaque per-byte
loading/timeouts, live replacement, slice/start/warm/rate gates and OPL2 PCM
on owning channel0. X7 uses actual callback progress; b/n/X8 share current-
rate/cache restoration. Muted progression, source controls, natural release
versus held notes, ANSI takeover and concurrent reset/destruction have
device-free tests. Nine N scenes per application profile check coherent
pixels/cells/palette/retained graphics, replies and state. Fixed gain3.25718
matches the existing steady ANSI/beep reference within0.0001dB; nine-carrier
headroom and encoded attenuation are checked through the mixer. No runtime
calibration, foreign system clock, new worker or MIDI decoding is introduced.
Physical audio/MIDI/auditory coverage is not claimed. The manual checklist is
in BIG_IMPLEMENTATION.adoc; goal-final full regressions/global acceptance remain.

Native FreeBSD CMake Release configure/full build (/usr/bin/cc, build/) passed without compiler/source diagnostics. All73 BIG-specific native CTests passed in67.79s after source idle/release and paced-concurrency fixes. The final X8 music-rate bridge was then verified by all4 focused rotation/X7/n/N tests in2.84s (cterm_big_music0.61s). syncterm_big_input passed in233.37s total (test233.31s), retaining4102 prior coherent scenes and adding126 N scenes (nine across14 profiles). The new natural-release assertion failed before its implementation. Independent PCM measures show fixed gain3.25718, +0.000056dB weighted reference difference and peaks3330/single,23753/nine,1881/nine TL32. No physical audio/MIDI, audible-device or non-BIG regression coverage is claimed. A manual audio checklist is supplied. All45 command bodies are natively registered/implemented; command/global audit and the goal-final full suite remain pending.

Native N0..5 buffer/source/controls/lifetime integration commit: `fdc8268`. All45 command bodies now have native integration. Full command/global acceptance audit and the goal-final complete regression suite remain pending; the unlimited BIG goal stays active.


BIG final native acceptance completes all45 command entries and seven
foundations. The original36-command dispatch plus X0..X8 matches the ledger;
all101 recursively named ledger tests passed. Shared current RIP font/data/
glyph kernels retain BIG-specific layout/clipping/state adapters. Indexed
pages/images convert formats at the edges; numeric/source/reply, hybrid
pointer, nonfatal recovery, backpressure and paced resume have coherent
native coverage. N uses actual allocated storage and OPL2 channel0 with real
X7 progress; b/n use the shared softened speaker channel1 and fixed gains.

The complete562-test FreeBSD native CMake Release inventory has561 passes
after an independent emoji test-loader import correction (134d5a0), plus one
external Wayland skip without a compositor. Internal Wayland and all three
private-Xvfb X11 tests passed. All IGS/ANSI/VT52/RIP/SkyPix/profile/Wren/audio/
graphics-lifecycle checks passed; BIG's application test passed4228 coherent
scenes in233.67s. A clean final configure/full rebuild and eight focused
parser/font/native-N/generator checks passed. No physical audio/MIDI,
auditory or external Wayland coverage is claimed. The standalone BIG goal
records the complete command review, approved adaptations and manual audio
checklist; earlier paragraphs retain their historical progress statements.

Final BIG native acceptance commit: `0390c49`.

`syncterm_rip3_icon_buttons_CP437` and `syncterm_rip3_icon_buttons_UTF-8` use
independently encoded BMP/BMH files on four entry rasters. They cover ICON COPY/
current-palette matching, ignored style/far geometry, creation-WORLD scaling,
normal/hot size retention, captured viewport/font/label state, initially selected
hot images, quiet fallback with field-local flags, selected inversion, auto-stamp
before labels, initial-only recess, keyboard activation, held deletion and
nonfatal later asset failure. Mouse-owner tests verify visual ownership through
rejection, failed redraw, cancellation, self-deletion and shutdown. Surface tests
check clipped RGB-complement matching and existing damage; port tests verify
captured-context restoration after failure. No persistent scene/damage layer is
introduced. Filename/label macro expansion and auto-stamp continuations are
covered by the connected milestones below, as are initial selected plain/
clipboard images; grouped/persistent selection remains pending.

`syncterm_rip3_pointer_CP437` and `syncterm_rip3_pointer_UTF-8` cover connected
`1c`/`MCURSOR` shapes, unchanged pixel planes, zero WORLD, fixed cursor-field
widths, STYLE-protection independence, invalid-input recovery, default reset and
continued mouse-field activation. OSC8 hover reads the current RIP3 text owner
using event pixels, including unaligned view boundaries, and restores the RIP3
pointer after leaving a link. Ordinary ANSI hyperlink tests retain the existing
path. Native tests cover ENV-protection differences, optional reserved zero
fields, all eight wire/variable cursor numbers, invalid ranges, distinct Wayland
rasters, and missing-shape arrow fallback through the real receive pump. Connected
tests select prohibited cursor 7 during a held press and retain valid release and
link-hover restoration. Native X11 presentation runs on a private Xvfb display.
Windows/macOS sources and tests are updated but were not run locally. Reviewed
[PR #127](https://github.com/bbs-land/remote-imaging-protocol/pull/127) answers
issue #125 with canonical 0..7 mapping, including raised palm 5 and prohibited 7.
[PR #126](https://github.com/bbs-land/remote-imaging-protocol/pull/126) answers
issue #124's graphical-expansion contract; its unfinished runtime consumers
remain pending. RIP hover host-query execution is covered below; full hover
lifecycle and HEADER input-lock orchestration remain pending.

`syncterm_rip3_graphical_CP437` and `syncterm_rip3_graphical_UTF-8` cover
connected T/@ angle preprocessing and one ordinary graphical expansion pass,
provider eligibility/literal rejection, quoted/control spellings, template
rescanning, decoded coordinate prefixes, discarded suffix effects, original
wire-byte budgets after codepage normalization, ink/cursor updates and nonfatal
failure preserving existing fields. `rip3_graphical` supplies independent policy,
NUL, prompt/cancellation and retained-effect vectors; `rip3_angle` preserves host
scanner coverage, and `rip3_builtin_inventory` checks all 84 published eligible
names plus known ineligible names. `term_key_forward` exercises the real receive
pump's required render prompt, ordered deferred events and local shutdown.
Filename/label bindings now also run through connected creation and full
redraw; all metric modes and retained text are covered below. HEADER and
aggregate save/restore orchestration remain pending. Existing ANSI/RIP1 rendering uses its
prior paths and console damage/scroll tracking.

The button/icon connected cases additionally verify live filename/label values,
fixed creation geometry across setter effects, PR129 separate face/hit/baked
rectangles, exact logical auto-stamp crop/padding, same-destination capture skip,
and current clipboard content on full redraw. A real menu editor receives a
held guest release, resumes the original button, and ignores later dialog clicks
as guest input. Native tests cover opaque host sources, quote/backslash split
rules, inserted delimiters, original codepage raw-slot limits, once-only capture
before a label prompt, destination replacement, capacity exhaustion and committed
action lifetime through deletion/redraw failure. The modal bridge retains only
the original physical tail against its press token in the existing input FIFO.
No new pixel/damage layer or guest scripting API is added. Parser/provider and
rendering failures stay local to RIP3; the connection remains usable.

The connected RIPv3 graphical tests in CP437 and UTF-8 draw all five BOLD.CHR
characters containing SCAN between ordinary letters. A single exchange checks
the final graphics cursor and the visible strokes, including the hollow colon
and equals contours on the 640×480 bitmap.

`rip3_text_metric`, public session tests and the connected
`syncterm_rip3_metric_CP437` / `syncterm_rip3_metric_UTF-8` cases cover live
level-zero r modes 0–3, exact real-font width/height axes, signed-WORD/world
conversion, independent byte replies, ignored reserved bytes and early numeric
rejection. PR133 defines CHR rational truncation, device-derived bitmap 80×43
measurement geometry and the shared atomic outline X/Hcache. Cold/warm failed
font preparation, inactive/protected state, empty input, no query drawing/cursor
movement, truncated-zero DPI and signed height overflow have independent native
expectations.
Connected cases use 640×480 and 720×400 in both encodings, real prompts and
post-prompt frame setters. Normalized codepage and raw UTF-8 wire bytes have
explicit separate profiles. Exact r/@ sentinels bypass expansion; ordinary T
resolves a distinct user TEXTDATA through normal lookup. Full SOFT cancels old
graphical ownership and clears retained text; a late prompt response cannot
resurrect it. Table backups/restores and viewport erase retain it. The
[PR133 adoption](RIP3_REFERENCE_ADOPTION.md) answers issue #130; mixed HEADER
and SAVEALL/RESTOREALL clauses remain pending with those operations.

The connected button cases also verify the initial selected-image flag for
plain and clipboard faces, including bevel/recess boundaries, protected/inactive
source padding, no double complement, outside cancellation and later pointer/
keyboard activation with opaque creation-time host source. Native tests cover
auto-stamp capture before the final selected-image inversion. Persistent
radio/checkbox state and associated-template lifetime remain separately under
[issue #131](https://github.com/bbs-land/remote-imaging-protocol/issues/131).

`rip3_hover`, public session tests and `syncterm_rip3_hover_CP437` /
`syncterm_rip3_hover_UTF-8` cover connected query modes 5/6: handler-independent
membership, exits before entries/newest-first overlap, duplicate IDs and stable
instances, half-open edges, current handler selection per callback and owned
FIELDID/source through replacement/deletion/suspension. PR135 adds exact OFF,
empty/1023/1024-byte definitions, normalized codepage bounds, ignored numeric
window/reserved values and nonfatal recovery. Focus/pointer-disable boundaries
clear membership and uncommitted traversal without synthetic exits, retaining
committed callbacks. Real modal editor focus loss works without a held guest
press. Full SOFT clears both handlers; field-only deletion retains them.
Actual CP437/UTF-8 sessions cover both 640×480 and 720×400 plus held click/button
ownership and coherent unchanged-pixel checks. Native allocation injection and
pending-focus/reset cases cover ownership/error paths. Complete mouse-family backup/
restore, HEADER/input-lock operations and stored click modes 1–4 remain pending
with those subsystems. PR134 grouped selection/template state is covered below.


`syncterm_rip3_groups_CP437` and `syncterm_rip3_groups_UTF-8` exercise PR134
through actual TCP sessions on 640×480 and 720×400 entries. Independent byte and
pixel expectations cover persistent checkbox/radio state, opaque initial
selection, momentary ungrouped buttons, release cancellation, selected-radio
reactivation/repeats, duplicate numeric IDs and mixed-kind rejection before
providers or painting. A newly selected radio changes peer state while a held
older member retains temporary feedback until outside release. Template vectors
cover per-owner/per-slot last-definition storage, creation-order concatenation,
ordinary baseline/publication precedence, cross-slot contributions, deletion and
SOFT preservation. Actual prompts retain FIELDID but skip deleted-owner writes;
post-angle bodies defer ordinary providers until use, and pure rebuilds execute
none. Assembly retains 3000+1096 bytes without truncating the underlying bodies.
Empty selected radios succeed; a live radio group with no selection fails even
with ordinary text, and last-member deletion removes the requirement. Native
mouse/template/session tests add failed-redraw commitment, suspended feedback
with same-ID replacement, allocation-failure preservation and full SOFT during
a grouped host prompt. Existing plain/clipboard/ICON tests use grouped initial
selection under the clarified contract. Explicit
host-template clearing and HEADER operations remain separate pending work.


`syncterm_rip3_mouse_backup_CP437` and `syncterm_rip3_mouse_backup_UTF-8`
cover connected SMF/RMF, whole-table COPY, status/protection and RESET(MOUSE)
for simple fields and ordinary button definitions on 640×480 and 720×400.
They compare coherent scene planes to verify no painting/provider work during
save or restore, except required removal of temporary pressed feedback. They
check save while held, canceled old release after applying restore, fresh hover
membership with saved E/X or saved absence, reusable BASE, consumed/protected
numbered saves, PUSH/POP, SOFT retaining backups and clearing live handlers,
shared handlers retained by field-only reset and protected snapshots retained
by backup reset. Ordinary PLAIN, CLIPBOARD and HOT ICON definitions survive
live-field destruction; fresh file resources prepared on actual preview support provider-free feedback
undo without saving screen pixels or retaining snapshot feedback caches. A callback restores its own definition,
suspends in the actual editor, then finishes its owned source/FIELDID once;
subsequent input uses the restored definition. Grouped capture/publication is
covered by the PR137 extension described below.
`rip3_mouse_backup` adds allocation and visual-copy failures, no-op versus
applying restore boundaries, fresh tokens and suspended traversal invalidation;
`rip3_session` covers the new owner's construction failure/teardown boundaries.
Aggregate restore and HEADER behavior
remain unaccepted; no changes to normal ANSI/RIP1 rendering are introduced.


`syncterm_rip3_screen_CP437` and `syncterm_rip3_screen_UTF-8` cover connected
screen-only SAVE/RESTORE, all twenty numbered aliases, generic COPY/status,
protection, PUSH/POP, backup reset and SOFT lifetime at 640×480 and 720×400.
Independent literal colors and coherent scene captures cover native encoded
ANSI text/both blink planes alongside indexed RIP graphics and literal reset
black. The PAL5→PAL17 vector restores saved RGB into the currently selected
protected palette without changing its flags/index or current PORT; native
component tests add selected-but-undefined targets, all presets and unscaled
cross-size clipping. Physical capture/restore ignores current-port/viewport
clipping and retains live fields/group selection, style and offscreen state.
Native cell writes and circular scrolling after restore use the existing console
external-pixel flags; restoration preserves current cursor state and both planes.
A literal one-pixel BMP export verifies restored native RGB is palette-matched,
not truncated to an index. Native tests add failed/invalid capture, immutable
saved data, later presentation failure, numbered deletion even on failure versus
success-dependent POP, argument rejection and continued host suffixes after valid
failed actions. Public session tests feed actions bytewise and cover paired
native-service validation and failure/destruction boundaries. Aggregate
SAVEALL/RESTOREALL, full TW/grouped mouse capture and HEADER remain separate
unfinished operations; this is not full RIPtel compliance.


The PR137 extension to both connected mouse-backup tests accepts persistent
grouped selection/maps through real TCP sessions, in CP437 and UTF-8 at 640×480
and 720×400. Literal documented checkbox commands independently produce
AB/XY, later ordinary Z/XY and rebuilt AB/XY across reusable restores. Tests
cover copied snapshots, current cross-slot publication, absent/type-changed
publishers, OFF-owner maps, held-preview versus committed selection, zero
implicit provider work, fresh tokens, saved radio selection, HKEYOFF and SOFT
ordinary-publication preservation. Coherent captures prove restore does not
repaint saved button art; logical selection can intentionally differ from
current pixels until actual redraw. A grouped callback restores matching public
IDs, suspends in the editor and completes FIELDID/direct output without writing
associations into its replacement. Separate 3000/2000-byte bodies survive the
4096-byte aggregate limit. Native template tests add defined-empty/absent maps,
surviving empty other-group publication, literal TIME preservation and failure
at every capture/prepare allocation. ICON files are absent during restore and
changed before later input, proving snapshots exclude feedback resources and
cold previews prepare fresh cancel artwork. Existing native/connected button,
group and ANSI checks pass; no additional console pixel/damage layer is added.
PR139 reconciles the backup index with the existing detailed screen/aggregate
contract. Full aggregate orchestration and HEADER behavior remain pending.


`syncterm_rip3_resident_query_CP437` and
`syncterm_rip3_resident_query_UTF-8` cover actual query modes 1–4 through TCP,
with overlapping floating/numbered PORT/TW definitions at 640×480 and 720×400.
Coherent captures show storage does not paint. Physical movement/press/release
at interior and boundary points produces no stored-query output or side
effects, matching RIPtel 3.1's absence of click dispatch. Immediate queries,
hover entry/exit and field-click actions provide positive controls in the same
connection. Tests cover protected/undefined writes, generic PORT backups,
first-use TW selections, source-size/malformed recovery and continued connection
use. Native session tests inspect actual stored bytes and independent first-use
query copies, including split input, ignored selectors, allocation/native-view
failure, protection, SOFT and cancellation. `rip3_resident_query` verifies
C-prefix/empty/OFF/1023-unit boundaries, encoded counting and failure
preservation; `rip3_ports_backup` independently fails every resident query copy
in capture/restore and checks saved/live state and protected/deleted/offscreen
ownership. Allocation tracking checks release on failure and teardown. No
historical deferred-query click execution is added. Full TW backups, individual
TW-entry COPY and aggregate orchestration remain pending.


`syncterm_rip3_text_backup_CP437` and `syncterm_rip3_text_backup_UTF-8`
cover full STW/RTW and whole-table COPY/status/protection/backup RESET through
TCP at all three presets plus custom 720×400. Tests preserve the original
entry font/raster on an early save, restore ordinary and extended layouts,
current selection/protection/activation, ANSI cursor/saved coordinates, margins,
wrap/origin, direct colors and native font output. Coherent captures compare
both scene planes to prove saved text/graphics are not restored or repainted;
PAL/STYLE/PORT/WORLD state remains live. RIP-only acknowledgements preserve
partial CSI and deferred wrap. Post-restore scrolling retains current red
artwork and uses the saved blue ANSI background for the vacated row, covering
existing external-pixel/row-mapping behavior without new damage tracking.
Protected save clearing, reusable BASE, slot/stack consumption, SOFT lifetime,
inactive input discard and nonfatal recovery are checked. Native session tests
add slot-35 ownership, failed record/native preparation and callback validation;
CTerm tests fail every metadata/font boundary and verify independent tabs,
colors and parser state. Console bitmap tests prove font resources survive
source teardown. Saved definitions contain no cells, screen pixels or history.
Individual TW-entry COPY/live reset, aggregate orchestration and HEADER remain
pending; RIP1.54's original freeze is unchanged.


The connected `syncterm_rip3_port_backup_CP437` / `_UTF-8` suites now exercise
`SCB`/`RCB` and generic whole-PORT COPY at all three presets and custom720×400.
They distinguish saved versus live clipboard selection and offscreen pixels,
verify a subsequent wire `1P` uses the restored selection, retain protected live
ports absent from the save while removing unprotected absent entries, and compare
both screen planes coherently for no repaint. Empty selection still restores
usable PORT definitions. Other table selections, slot/stack/protection lifetime,
SOFT and invalid/missing recovery are checked. Native tests add assigned-zero,
source-owner teardown, allocation failures, canvas conflict and exact protected
survivor pixels/viewport/generation. Remembered PORT_COPY rectangles/PCB and
aggregate backup orchestration remain pending; issue140 records the stale
empty-clipboard deletion paragraph. RIP1.54's freeze is unchanged.


The connected `syncterm_rip3_aggregate_CP437` / `_UTF-8` suites now exercise
`SGS`/`RGS`, `SENV`/`RENV` and `SAVEALL`/`RESTOREALL` at all presets and custom
720×400. Coherent observations check screen colors, offscreen contents, ANSI
window metadata, button input, separate table selections, ENV-only settings,
protected/consumed saves, independent stacks and continued execution after a
missing PAL family. Native backup/table-host tests independently assert exact
TW/PORT/MOUSE/SCREEN/PAL/BUT/STYLE/ENV dispatch, all restores before numbered
deletions, failed/protected/missing/all-failed continuation and invalid preflight.
Native ENV tests restore actual table/index/value state through SENV/RENV.
These tests accept the wrapper composition and represented payloads, not the
unfinished complete PORT/STYLE inventories or full RIP3 compliance. Source pin
and RIP1.54 preservation remain unchanged; issues140/142 retain their separate
editorial status, with PR141 awaiting adoption.


The connected `syncterm_rip3_environment_CP437` / `_UTF-8` suites exercise
`CUR(ENV,n)`, fixed-two-digit `2E` and live ENV entry COPY on all presets and
custom 720×400. Independent replies/captures verify the documented departing-
record write-back, protected and first-use selection, native pointer refresh,
separate current table selections, unchanged ANSI state/scene/raster, actual
base 64/four-digit plotted coordinates and wire-protection versus host-variable
bypasses. Late invalid/protected/self copy targets produce no partial definitions;
zero/current destinations have explicit metadata/value expectations. Nonzero
ENV backup restore and SOFT retain their own lifetimes. Selecting zero WORLD
allows queries, locally rejects projection and accepts the next wire selector
without disconnecting. Native ownership/state/copy and every-split wire tests
cover the corresponding kernel boundaries. No ordinary ANSI/RIP1.54 rendering
or scrolling path is changed; HEADER/other live COPY families remain pending.


`st_rip3_style_copy_connected_test.py` exercises live STYLE/BUT entry COPY via
actual RIP3 TCP sessions in CP437 and UTF-8 at the presets and custom720×400.
It checks full permission preflight, undefined/current/repeated/zero targets,
STYLE-carried versus BUT-cleared protection, actual copied drawing/fill colors
and button dimensions/face/hotkey actions. Coherent captures assert no paint
at COPY, retained independent selectors/graphics cursor/native ANSI rendition
and raster, and nonfatal malformed-reference recovery. Native tests compare
all represented record fields and raw protection on hidden undefined BUT
entries. These value copies use the existing session-owned records and add no
renderer/damage layer. Shared TW-entry COPY, HEADER and remembered PORT pairs/
PCB remain pending; the specific physical clipping extent question is tracked
in new issue146 against PR145. The selected reference pin is
`f14e410915f11f64d2287eec82ebf4770c3baf41`; RIP1.54 remains at its original
preservation baseline. See the detailed RIP3 conformance/adoption inventories.


The connected window-table test writes two MicroANSI blanks, paints graphics
over them, and repeats both blanks. One capture checks both blink planes for
the text background at each destination and the surviving artwork between
them, in CP437 and UTF-8 at 640×480, 800×600, and 1024×768.

`st_rip3_window_copy_connected_test.py` now covers TW entry COPY over actual
RIP3 TCP connections in CP437 and UTF-8 at 640×480,800×600,1024×768 and custom
720×400. It asserts literal numeric/CUR examples, source/destination protection,
zero/repeated/current targets, complete rejection of invalid requests without
partial changes, independently retained native geometry/fonts/cursors/rendition/
tabs/margins/activation, and copied ordinary/extended windows. Coherent captures
compare both planes at COPY; subsequent glyph output and a split ANSI CSI show
that new definitions are actually used and connection parsing survives current
owner replacement. Native session tests inject query/native-helper resource
failures and verify the documented continuation/empty-query behavior and cleanup.
All five allowed live-entry COPY families are connected. This reuses existing
native definition capsules and shared-raster views without copying cells/history/
pixels or adding scene/damage tracking. Live TW reset, charset/ESC7/8, HEADER,
remaining PORT/STYLE inventory and remembered pairs/PCB remain unfinished;
original entry font policy is separate. RIP1.54 retains its original preservation
baseline and no ordinary ANSI renderer/scrolling path changes.


`st_rip3_pcb_connected_test.py` now invokes remembered-pair paste over real
RIP3 TCP sessions in CP437 and UTF-8 on the three existing raster presets and
custom720×400. Literal pixel/state expectations cover explicit/default/CUR
selection, decimal parsing independent of Base Math, COPY independent of W,
DEVICE geometry independent of WORLD, inactive/absent/invalid requests, moved
viewports, repeated overlapping BCDD/BBCD, GET_IMAGE and PUT_IMAGE producers,
SCB/RCB pair ownership and initial button auto-stamp. A native ANSI truecolor
cell is captured and pasted back with its current pixels; compound button
capture also proves pending graphics are not replaced by stale native ink.
Coherent captures retain selectors, text/graphics cursors, rendition and raster;
malformed calls leave the connection usable. Native `rip3_pcb` adds both-axis
logical/physical boundary and zero-write vectors, helper/read/overlap failures,
protected destinations, source movement, saved pair/absence/protected survivors,
SCREEN exclusion and core lifecycle. Screen-source copies use bounded existing
console readback and existing paint bits; ordinary text rendering and offscreen
sources gain no readback or tracking layer. Mixed HEADER/live viewport-reset
clauses remain pending. See the latest RIP3 inventory for current scope and pin;
RIP1.54 retains its original preservation baseline.


`st_rip3_window_reset_connected_test.py` covers live RESET(TW) over actual TCP
connections in CP437/UTF-8 on the presets and custom720×400. Coherent captures
assert forced fresh ordinary zero state from extended/inactive/identical layouts,
no repaint, normal native colors and CSI cursor/tab/margin/rendition/cursor-request
state, protected-current retention through zero/TBL/ALL, existing inactive zero
fallback, left-to-right CUR, complete late-syntax rejection and partial CSI
continuation. Native session tests cover five configured font grids/wrap choices,
fixed versus floating query lifetime without evaluation, protected/undefined
no-ops, independently owned backups, per-target failure/retry/ALL continuation
and cleanup. The existing TW backup tests use an actually invalid late selector
for their no-prefix-effects expectation; both encodings pass. No ordinary text
renderer or scene/damage tracker changes. This acceptance leaves G0/G1, larger
ESC7/8, remaining per-window mode verification and the deferred frontend startup/
default-font policy pending; it is not full TW reset or RIP3 compliance.


`st_rip3_charset_connected_test.py` covers ANSI-only G0/G1 designation state on
actual CP437/UTF-8 RIP3 TCP connections over the preset and custom720×400 rasters.
The literal #/music-note/q/sun/#/box vector stays in the CP437 bank; the640×480
pixel oracle reads the original MicroANSI font asset, independently of emulator
output. Coherent captures verify no designation ink/cursor movement, unknown
finals, ordinary and extended definition/identity, activation, fresh reset,
selection versus first-use clone, entry COPY, owned TW backup, different
protected/current/zero state across SOFT/TBL and split designation continuation.
Native `cterm_text_window` exercises every six-sequence split across source
teardown, independent glyph identities, reset/coordinate/definition/snapshot
lifetimes and unchanged OFF/RIP1 handling. Charset state adds two metadata bytes;
no emulation mode or pixel/damage layer is added. Full ESC7/ESC8 snapshot semantics
remain pending under issue150; ordinary CSI saved coordinates remain distinct.
RIP1.54's original preservation boundary and ordinary rendering remain intact.


`st_rip3_view_reset_connected_test.py` invokes RESET(VIEW) over actual CP437/
UTF-8 RIP3 TCP sessions on preset and custom720×400 rasters. It checks full
current/numeric/TBL port viewport bounds and activation, preserved MVP inactive
semantics, zero WORLD/radix independence, protected/undefined no-ops and complete
late-invalid-selector rejection. Coherent captures retain pixels, ordinary text
state and foreign selections; actual PCB after reset proves copy geometry and
offscreen contents survived. A screen sub-port's absolute origin and included/
excluded far-edge pixels check full-port bounds. Native viewport tests inspect
cursor/image/pair/query/clipboard state and unavailable-binding recovery, while
native port/table-host suites retain their existing coverage. This is a drawing
viewport configuration change; CTerm ANSI parsing and RIP1.54 rendering are
unchanged. Other RESET/HEADER and the full ESC7/8 snapshot remain unfinished.
