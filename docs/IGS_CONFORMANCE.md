# IGS 2.21 conformance

IGS implementation is in progress. The application-selectable IGS 2.21 mode
implements `I`'s six initialization selectors and `R` resolution selection.
`g` configures coordinate scaling and its captured resolution. `M` selects the
workstation writing mode; `C` selects independent graphical pens and `S`
changes palette colours. `H` selects hollow/solid fill, writing mode and
perimeter together. `A` selects fill styles and copies/generates user patterns.
`T` configures marker types/sizes, line styles/widths, custom masks and ends.
`P` draws the selected marker and updates the remembered Drawto endpoint.
`L` draws thin/wide lines with patterns, caps and arrowheads, remembering the
original requested start/end coordinates.
Other commands remain pending.
The [implementation goal](IGS_IMPLEMENTATION.adoc) and
[command ledger](../src/tests/igs/conformance.json) distinguish foundations
from completed commands.

The target is intended IGS 2.21 accessory serial behavior with US TOS 1.04
software graphics in all three ST resolutions. Exact intended pixels are
required. Original bugs and memory/overflow accidents are corrected safely
and will be listed here with tests. Historical delivery profiles, BIG and
CONDOR implementations are outside this goal.

Audio will implement GIST/PSG behavior and use SyncTERM MIDI facilities.
Sound tests check functionality and lifecycle, without requiring identical PCM
or reference-render comparisons. Listening is deferred to manual testing
after implementation; no auditory coverage is claimed by silent fixtures.

Timing is translated into the local domain where needed: sync-wait units
initially mean 1/60 second, and note/envelope units mean 1/200 second. Use
existing local monotonic deadlines and audio sample timing, with injected
local time in tests. Keep sync-wait conversion configurable for later PAL or
native-vsync support. There is no separate IGS clock subsystem, free-running
foreign tick counters, Atari interrupt model or CPU-cycle emulation. Drawing
does not reproduce old hardware throughput.

## Parser insertion point

IGS and RIP use the same pre-ANSI add-on-parser stage in CTerm's receive path,
ahead of base ANSI/VT52 emulation. The selected graphics parser consumes its
framing, parameters and payloads and forwards ordinary bytes to the existing
plain-input path in order. IGS is not implemented inside ANSI parsing, and
neither protocol's payloads are passed through the other protocol's parser.
Their grammars remain separate; RIP need not migrate into the shared
IGS-family grammar to use the same insertion point.

Required integration tests cover mixed ordinary text/control input, split
graphics introducers, payloads containing control-looking bytes, and reset/
disconnect recovery at this boundary, while preserving existing encoding,
hook/capture and intentional semantic-input bypass behavior. This is an
integration requirement. Native CTerm routing is covered below; complete
command execution and application integration are tracked separately below.

## Framebuffer and image formats

The authoritative screen is SyncTERM's existing bitmap framebuffer, whose
pixels already support palette indices as well as tagged RGB. Off-screen
images also use indexed pixel storage. There is no additional persistent
planar screen or planar shadow to keep synchronized.

Native bitmap data is decoded at import and encoded only when an interface
requires native bytes. Reinterpret plane-layout changes at the format-change
boundary. Ordinary drawing and `G` Boolean transfers operate on physical
colour indices without planar conversion at each blit; `X14` only selects
routing. Preserve full decoded memory-image storage, padding and retained
contents, with explicit plane-layout and row-stride metadata. A changed stride
can be a different view of the same data rather than an image conversion.

The [implementation goal](IGS_IMPLEMENTATION.adoc) records the rationale and
integration constraints. Tests must cover import, repeated indexed transfers,
palette updates, stride views and plane-layout reinterpretation. These are
requirements, not claims of completed command or renderer support.

## Coverage

The shared byte-parser foundation has native `graphics_protocol_parser`
coverage. Tests use literal framing/reader examples from reference chapters
02, 11 and 13, plus explicitly synthetic descriptors for variable-length raw
fields. They cover case-sensitive attention and identifiers, overlapping
prefixes, chains, coordinate annotations, typed loop/random/array operands,
continuations, CR/LF policy, bounded text including embedded NUL, the literal
empty `@`, raw payload ownership, suspension, source EOF and error recovery.
Every focused fixture runs whole, bytewise and at every two-part split.

These tests observe parser events; they do not claim IGS command execution,
raster, sound, application or BIG/CONDOR conformance. Alternate framing tests
only establish that attention bytes and outer buffered entry are configurable.

Native `igs_image` coverage checks independent literal native-byte/index
vectors in all three plane layouts, partial imports at arbitrary byte offsets,
retained full capacity/padding and layout reinterpretation. Invalid bounds,
RGB/foreign-index rejection and failed layout-change retention are checked.
Round-trip and input-partition tests supplement the independent literal
results. Images remain indexed; temporary native bytes are used only when
changing the plane layout or explicitly importing/exporting native data.
These are image-format tests, not completed `G`/`X11` commands or application
integration. The earlier planar-surface and separate-clock experiments have
been removed. `I5` and changed-colour `R` use injected local monotonic deadlines.

Native `cterm_igs_input` covers the CTerm insertion point in all ST geometries,
Atari ST byte/UTF-8 input and status on/off. Every focused split and bytewise
replay retain ordinary text/VT52 controls and consume graphics headers before
base decoding. It checks semantic-input bypass, first-text bitmap retention,
reset including an input-hook reset, source EOF, disconnect/destruction,
local header deadlines and invalid emulation/geometry. The CTerm enum value 5
is also exposed as `Graphics=IGS (2.21)` and Wren `GraphicsMode.igs221`.
This input test does not claim IGS drawing or raw-payload command execution.
The generic parser's raw-ownership tests remain separate.

### I: initialization

`I0` restores the captured entry palette and selected graphical attributes;
`I1` restores only the palette; `I2` restores only those attributes. `I0/I2`
also clear the G double-copy control and X14 route. They preserve clipping,
Drawto, scaling, source pattern slots 1..7, active user fill, user line style,
fill perimeter, text face and alignment. Source pattern 0 is restored
separately from the active user fill. Text effects become zero and text sizing
returns through pixel height. `I3/I4` apply the literal IGS/VDI palette tables
through native pen mappings, colour quantization and monochrome rules.
Values above five select `I0`; negative values are no-ops.

`I5` always restores a saved colour mode, including an already-current mode.
After one local sync unit (17 ms at the initial 60 units/second), it clears
screen indices to physical zero, restores console geometry/home/default
colours/hidden cursor, selects low wrap or medium no-wrap, and clips to the
full native screen. A saved monochrome mode only restores clipping. The wait
leaves trailing input unread until completion; reset, EOF and disconnect
cancel it. No palette or unrelated graphical-state reset accompanies `I5`.

Native `cterm_igs_init` covers all selectors, literal palettes in three modes,
selective attribute preservation, array-supplied negative values, every input
split/bytewise replay, saved/current colour-mode differences, local deadline
boundaries/configuration/overflow, cancellation, fixed canvas resize rejection
and status/history geometry. `syncterm_igs_init` uses real profile/menu/transport
paths with all ST modes, Atari ST and UTF-8, and status on/off. Each checkpoint
checks literal Z glyph pixels, both planes, cells, palette and mode together.
It verifies I5 clear/no-clear, following console output, directory geometry
restoration and the editor's IGS preset/mode/encoding persistence. Fixtures
disable audio. No drawing, loop execution or sound coverage is implied.

Graphical attributes start from a coherent port-owned workstation snapshot.
The original marker-type index mismatch, stale line ends and marker-pen cache
are corrected; I0/I2 restore intended captured values and keep the cache
coherent. Palette restoration retains the captured host RGB values, including
custom profile colours. Unavailable desktop pens map safely through its saved
depth instead of restoring undefined data. These are explicit port policies.

### R: resolution and palette selection

`R` reads both unscaled arguments before applying effects. Only requests 0
and 1 can change a colour mode; all other requests retain the current mode.
Monochrome cannot switch to colour through `R`. A current-mode or invalid
request preserves console position, attributes, wrapping and scene without a
wait or clear. Every completed `R` restores full-screen clipping. After any
mode switch and clipping, palette 1 restores the entry snapshot, 2 applies
IGS defaults and 3 applies VDI defaults. Other palette values make no palette
call. The entry resolution, graphical attributes, scaling/captured resolution,
Drawto, G double-control and X14 route remain unchanged.

Host display scaling adjusts to keep the physical window size unchanged during
low/medium switches, including fractional scales and reduction below native
width. The full canvas fits that window. For example, low at 3x becomes medium
at 1.5x; a 320x240 window remains 320x240 when medium needs a 0.5x horizontal
scale. This display policy is separate from `g`'s coordinate scaler. Other
console modes retain their existing window-sizing policy. I5 uses the same
host mode helper.

A changed colour mode uses the same one-sync-unit local deadline and console
reset as `I5`. Its pending palette is applied after the mode change, before
following wire bytes resume. Partial/invalid arguments have no command effects;
timeout, EOF, reset and disconnect discard pending input/work. Palette and clip
completion do not run after a failed mode preparation.

The common I5/R mode helper prepares both off-screen images before changing
the console. At a changed plane layout, it replaces their indexed representation
while preserving every native byte, including padding and retained capacity.
This is a format-boundary operation; native bytes are temporary and no planar
shadow is retained. Image objects and their remembered interpretation remain
owned by the engine. Pixel borrows end at a successful layout change. Same-mode
`R` does not change image storage. `G`/`X11` command execution remains pending.

Native `cterm_igs_resolution` checks 210 resolution/palette/status combinations,
including negative array-supplied values, mono lock, same-mode retention,
palette ordering across the local deadline, clipping and unrelated-state
preservation. Status cases include a host-writable status subterminal. Chained
commands run bytewise and at every two-part split through multiple suspended
switches. It checks complete memory-byte retention and literal decoded indices,
history-ring content/geometry, malformed and timed-out second arguments,
EOF/reset/disconnect cancellation and failed second-image preparation without
console/palette/first-image mutation.

`syncterm_igs_resolution` checks real transport in all ST entry modes, Atari ST,
CP437 and UTF-8 with status on/off. Each capture checks literal glyph/clear
pixels in both planes, cells, all sixteen palette entries, mode geometry,
cursor position, wrapping and history width. It checks current/invalid requests,
both colour transitions, all palette classes, directory restoration and a fresh
mode/palette after reconnect, with unchanged 320x240 and 960x720 output and
destination dimensions across R. Native scaler/bitmap tests check reduced and
mixed-axis scaling, incremental damage, complete quadrants and sizing policy;
the X11 mode fixture checks physical window dimensions and rendered edges at
fractional and sub-native scales on a private Xvfb server.
These fixtures disable audio and use ASCII
protocol/text checkpoints; they do not establish complete character-set,
graphical-text, drawing or sound conformance.

### g: coordinate scaling

`g` consumes an unscaled mode, normalizes values outside 0..2 to zero, and
captures the current ST resolution even when selecting mode zero. It does
not change resolution, palette, scene, console position, Drawto or graphical
attributes. `I0/I2`, `R` and `I5` preserve this captured scaler state; issuing
another `g` captures the then-current mode. Whole-terminal reset disables
scaling and captures its current resolution; a fresh connection starts there.

Mode zero leaves numeric arguments unchanged. Mode one maps positive decimal
X/Y inputs on the manual's 0..9999 logical canvas to the captured resolution's
inclusive pixel maxima, rounded to nearest. Mode two doubles positive Y only
for captured monochrome; X and unscaled arguments are unchanged. The generic
parser keeps the per-parameter axis tag. The common IGS numeric evaluator
applies it to decimal constants and array indices before lookup, without
scaling loaded array values or random substitutions again. Loop operand
execution remains a separate pending step.

This intentionally corrects the original `g1` integer-division bug described
in reference chapters 02/11/12. The original divides 9999 by the input as an
integer before converting to float, collapsing the upper half of the logical
range to the screen edge. SyncTERM follows IG221.TXT's intended 10000x10000
canvas: X=5000 maps to 160/320 in low/medium, rather than 319/639. The bounded
integer mapping needs no emulated compiler floating-point arithmetic. Invalid
literal/index magnitudes above the reader's 9999 limit fail safely; accidental
word overflow and software zero-denominator artifacts are not reproduced.
Display scaling/window sizing is independent of this protocol state.

`cterm_igs_scaling` checks literal mappings for all axes, modes and ST
resolutions, mono doubling, stale capture through `R`, disabled/invalid-mode
recapture, reset, selective `I` preservation and unrelated state. Synthetic
X/Y schemas exercise the same parser/evaluator used by real commands, with
every split/bytewise input, continuations/CRLF, lookup ordering and clamping,
random substitutions and bounded failures. Actual `g` chains and partial,
timeout and malformed-input recovery run through CTerm.

`syncterm_igs_scaling` checks real `g` state and no immediate raster/palette/
window effects in one coherent capture with both planes and literal Z rows.
All ST entry modes, Atari ST/CP437/UTF-8 and status choices cover preservation
through `I`/`R`/`I5`, mode-zero/invalid-mode recapture and fresh reconnect.
Read-only control-backend fields observe actual/captured resolution and mode;
they do not introduce Wren APIs or product settings. Audio is disabled.
These tests do not claim any pending drawing or loop command is implemented.

### M: workstation writing mode

`M` consumes one unscaled numeric operand. Modes 1..4 select replace,
transparent, XOR and reverse transparent; all other signed values select
replace. The existing workstation attribute stores the public 1..4 values,
without an additional IGS cache. This is the M caller/native-mode contract
in reference chapters 15/129 and the mode names in IG221.TXT.

The command produces no immediate pixels, palette changes, replies, console
changes or waits. Pens, patterns, fill/perimeter, Drawto, clip, scaling and
memory routing are preserved. `I0/I2` restore the captured writing mode;
palette-only `I` selectors, `I5` and `R` preserve it. Whole reset and reconnect
start in replace mode. Primitive-specific pixel composition remains pending
with the drawing commands that consume this attribute.

`cterm_igs_writing_mode` checks the four modes and invalid/signed bounds,
literal/array/random operands under all scaling modes, unrelated attributes,
palette and both pixel planes, selective `I` restoration, `R` suspension/
ordering, whole reset and every focused split/bytewise input. Incomplete
EOF, local timeout, five-digit failure and following-command recovery retain
the last complete mode. `syncterm_igs_writing_mode` covers all ST modes,
Atari ST/CP437/UTF-8 and status choices through coherent captures of literal
Z pixels, cells, palette, cursor and read-only IGS state. It verifies no
immediate scene/window effect, no replies and a fresh reconnect; audio is off.

### C: graphical pens

`C` reads kind and pen as unscaled operands, then changes only the selected
marker (0), line (1), fill (2) or graphical-text (3) pen. A kind above 3
selects marker; a negative kind consumes both operands and changes nothing.
IGS maps pens above 15 to zero. The workstation then maps negative or
unavailable pens to logical pen 1, using the current 16/4/2-colour limit.
Only marker selection updates the marker cache. It retains the request after
IGS's upper clamp, separately from the effective marker pen. No palette
register or console colour changes.
These are the two validation stages in reference chapters 15/29/129.

The port stores effective logical pen numbers in its workstation attributes.
Drawing will apply the fixed MAP_COL mapping and active-plane mask. `R` and
`I5` retain selections without revalidating them against the new depth, so
the native physical colour bits remain recoverable. A subsequent `C` uses
the new depth. `I0/I2` restore entry pens and the coherent marker cache;
palette-only `I` selectors preserve them. Whole reset/reconnect start at pen 1.

`cterm_igs_pen` covers every ordinary pen, upper/signed invalid values,
negative/upper kinds, independent state preservation, all scaling modes,
literal/array/random operands, cache differences, resolution changes,
selective initialization and reset. Chains run at every split and bytewise;
partial kind/pen input, timeout, malformed magnitude and following-command
recovery preserve the last complete selection. `syncterm_igs_pen` verifies
the real attribute command with coherent literal Z pixels, cells, palette,
cursor and pen state in all ST modes, Atari ST/CP437/UTF-8 and status choices.
It covers I/R lifetime, unchanged windows/scenes, no replies and reconnect.
Audio is disabled. Primitive pen-to-pixel rendering remains pending with
the drawing commands; this attribute step draws no pixels itself.

### S: palette components

`S` reads pen, red, green and blue as unscaled operands. A pen above 15
becomes zero. Each component 1..7 maps to 142, 285, 428, 571, 714, 857 or
1000; every other signed value becomes zero. The existing workstation
palette service retains requested triples and uses the physical pen map,
active-depth pen limit and three-bit ST DAC. Captured entry/default palettes
remain separate from the current requested/realized palette.

Monochrome retains requested triples even when the visible palette stays
unchanged. Only all-black and all-white triples change polarity; mixed or
middle triples do not choose a nearest gray. All negative palette pens are
ignored safely, including low-byte aliases such as -256 and -32768. This
deliberately avoids the original byte-width check's indexed-memory/bus
accidents. Out-of-depth positive pens are ignored; they do not use `C`'s
fallback pen. These rules follow reference 15/28 with that explicit safety
correction.

Existing indexed pixels recolour through the palette without rewritten
indices or cells. Console colours, graphical pens/attributes, Drawto,
clipping, scaling and memory controls remain unchanged, with no replies or
waits. `I2`, `I5` and `R` palette zero retain the palette; `I0/I1`, whole
reset and reconnect restore the captured entry palette.

`cterm_igs_palette` checks literal component/request/DAC tables, every pen's
physical mapping at all three depths, signed invalid values and negative
aliases, random/array operands under every scaling mode, unchanged state/
indices, requested versus visible mono values, I restoration, split/bytewise
chains, partial input, EOF, timeout, malformed magnitude and reset/recovery.
`syncterm_igs_palette` uses coherent cells, both raw planes, full palette,
state and composed RGB to verify recolouring of an already-drawn literal Z,
background changes, mono polarity, I/R lifetime, retained window dimensions,
no replies and reconnect. All ST modes, Atari ST/CP437/UTF-8 and status
choices run with audio disabled and nearest display scaling. Drawing and
sound commands remain pending.

### H: hollow and solid attributes

`H` reads one unscaled flag. Negative flags do nothing; flags above one
select one. `H1` sets hollow interior 0, transparent writing mode 2 and
perimeter on, in that order. `H0` sets solid interior 1, replace mode 1 and
perimeter off. There is no separate hollow flag or H cache. A subsequent
`M` changes writing mode independently. Fill pen, stored fill index and
user pattern are preserved; selecting hollow/solid does not revalidate the
old index. These are the state contracts in reference chapters 15/29/129.

`H` draws no pixels and changes no palette, console colours, unrelated
attributes, Drawto, clipping, scaling or memory controls. It sends no reply
and adds no wait. `I0/I2` restore the captured interior and writing mode
but preserve perimeter. Palette-only `I`, `I5` and `R` retain the selected
triple; whole reset/reconnect restore the complete entry attributes,
including perimeter. Each drawing primitive's use of these attributes is
covered with that primitive when implemented.

`cterm_igs_hollow` checks flags and signed bounds, literal/array/random
operands under all scaling modes, old fill index/user pattern preservation,
unrelated attributes and both pixel planes, `M` interaction, selective/full
initialization and resolution suspension/ordering. Focused H/M chains run
at every split and bytewise; partial input, EOF, timeout, malformed magnitude
and reset/recovery preserve completed state. `syncterm_igs_hollow` checks
the actual three attributes with coherent literal Z pixels, cells, palette,
cursor and IGS state, in all ST modes, Atari ST/CP437/UTF-8 and status choices.
It covers M/I/R lifetime, unchanged scene/window, no replies and reconnect,
with audio disabled. This is attribute coverage; pending primitive and
auditory conformance is not claimed.

### A: fill styles and user patterns

`A` reads interior, index and border as three unscaled operands before
changing state. Invalid interiors become hollow (0). Standard pattern (2)
indices are 1..24; other non-user interiors use 1..12. Invalid style indices
become 1. Border is false only for zero, including signed nonzero values.
Writing mode and fill pen remain unchanged. These are the caller and native
style rules in reference 15/29/130 and IG221.TXT.

For user interior 4, indices 0..7 copy sixteen source-bank rows into an
independent active user fill. Index 8 consumes one shared random result,
then performs sixteen unsigned 32-bit updates with multiplier 0x1afb and
increment 0x1fccd, storing the low word of each update in bank zero before
copying it. Index 9 or above also consumes one shared random result, but
restores the literal default bank-zero rows before copying. Other banks
are retained. Negative user indices safely select zero.

The original user branch erroneously passes a pointer to scalar `vsf_style`,
making its stored index depend on the native load address. SyncTERM removes
that call: user-pattern selection leaves the previous non-user style index
unchanged and copies the requested rows. No native pointer or address-wrap
artifact is emulated. This explicit correction leaves pattern/hatch index
selection independent of user-pattern storage.

The existing IGS-owned random source is shared with numeric `r/R`: full
reset starts its 32-bit state at one; updates use multiplier 0xbb40e62d and
increment one, returning bits 8..31. Zero advances to one without foreign
clock reseeding. Other OS consumers/native clock observations are outside
this port policy. A's separate pattern seed starts at zero. References 124
and 130 supply the two recurrences and independent literal vectors.

`I0/I2` restore source bank zero and entry attributes but preserve active
user fill and both random seeds. `R/I5` retain banks, active fill and seeds;
full reset/reconnect restore the complete entry state, default bank zero,
zeroed other banks and solid active user rows. `A` draws no immediate pixels,
changes no palette/console colours and sends no replies or waits. Its
pattern masks and style index will be consumed by later drawing commands.

`cterm_igs_fill` checks signed/upper bounds, all style limits and borders,
all eight user-bank copies and source/active independence, literal random
rows and shared-source progression, selective initialization, resolution
lifetime, full reset and every focused split/bytewise input. Partial
acquisition, EOF, timeout, malformed magnitude and recovery retain completed
state and do not consume a random draw. `syncterm_igs_fill` observes actual
interior/index/perimeter, all sixteen active rows and both seeds alongside
coherent literal scene/cell/palette/cursor checks in all ST modes,
Atari ST/CP437/UTF-8 and status choices. It checks I/R lifetime, unchanged
scene/window, no replies and reconnect with audio off. Primitive rendering,
loop execution and sound remain pending.

### T: marker and line attributes

`T` reads one unscaled kind first. Kind outside 1..2 ends the command
without reading style or size. Remaining bytes retain their normal chain/
ordinary-text ownership. Valid kinds read two additional unscaled operands.
The existing generic parser's dynamic-parameter callback previews kind on
a private state copy; real random/operand state advances exactly once when
the completed command executes. Invalid kinds produce a distinct one-argument
view for the later `&` executor; loop/repetition-guard execution remains
pending with that command.

Style outside 1..7 becomes one. Marker kind 1 clamps size to 1..8 (otherwise
one), stores height `size*11`, and uses native types 1..6. Requested marker
type seven selects native default type three. Line kind 2 changes an ordinary
type immediately, even when width is invalid. Only widths 1..41 change width;
solid widths clamp to 40 and round down to an odd value (maximum 39), while
all patterned styles force width one.

Custom line type seven is selected only in that valid-width branch. Sizes
1..16 read source-bank 6 rows 0..15, and 17..32 read bank 7 rows 0..15.
A zero row uses fallback mask 0xaaaa or 0x5555 respectively; nonzero words
are retained without byte reversal. Sizes 33..41 select the existing custom
mask. Endpoint-only requests do not themselves change width/mask; for type
seven they also retain the previous type. Selector zero sets square ends;
50..54 and 60..64 use the exact arrow/round/square table in reference 15/128.
Other size words leave ends unchanged. Width processing cannot trigger an
endpoint selector after forcing patterned widths to one.

`I0/I2` restore captured types, dimensions and ends but preserve the uploaded
user-line mask. `I5/R` preserve the selected attributes; full reset/reconnect
restore entry state, including mask 0xffff. Pens, fill/text state, palette,
Drawto, clipping, scaling, source banks and memory controls remain unchanged.
`T` produces no immediate pixels, replies or waits. Reference 29/52 supplies
the native sizing rules; primitive raster use remains pending.

`cterm_igs_type` uses literal marker/width/end tables, every custom row and
both zero-mask fallbacks under all scaling modes and ST resolutions. It
checks unrelated state/indices, I/R lifetime, full reset, every focused split
and bytewise input, random kinds committing once, invalid-kind byte ownership,
partial acquisition, EOF, timeout and malformed-input recovery.
`syncterm_igs_type` captures actual marker/line fields with coherent literal
scene/cell/palette/cursor expectations across all ST modes,
Atari ST/CP437/UTF-8 and status choices. It covers conditional arity, mask
fallback/retention, endpoint-only requests, I/R lifetime, unchanged scene/
window, no replies and fresh reconnect with audio disabled. These tests do
not claim primitive rendering, loop execution or sound conformance.

### P: native markers and remembered position

`P` reads two axis-aware coordinates through the existing shared parser and
`g`/array/random evaluator. After successful rendering it records the requested
centre as the next Drawto endpoint, including fully clipped and reverse-mode
markers. Incomplete or rejected commands leave the previous endpoint intact.

All six native shapes use ordered, independent line components from the ROM
tables. Both axes scale by T's size 1..8; dots remain one pixel. Segments use
solid width one, square ends and the marker pen, preserving the selected line
style/width/ends and every other workstation attribute. Replace/transparent
plot the physical pen. XOR complements the active 4/2/1 plane bits regardless
of pen, with independent crossings, repeated closure points and the native
nonfinal endpoint rules. Reverse transparent leaves all pixels unchanged.
Both blink phases retain their own pixels. No planar screen is introduced.

Markers force inherited clipping bounds without changing the stored record
flag or bounds. The implementation preserves bounded native intersection
rounding (including its signed remainder borrow), edge priority, X ordering
and X-major ties. Host arithmetic safely avoids native coordinate wrap,
division exceptions and raw-word clipping cycles; invalid/inverted bounds
reject drawing and a bounded guard prevents nonconvergence. The host status
row is protected after native segment clipping and rasterization, preserving
line phase and the requested Drawto centre. It occupies the last row inside
the selected native resolution. Foreign host pixels outside the IGS palette
namespace are preserved by XOR, since they have no native plane bits.

`cterm_igs_marker` covers every shape/size/mode at all resolutions with five
pen choices, independent blink phases, a separate rational line oracle and
literal cross/diamond clip vectors. It checks inherited clipping with the
record flag off, negative rounding, boundaries/status, signed/extreme centres,
Drawto, attribute preservation, g/array ordering, split/bytewise input,
EOF/timeout/malformed recovery and reset. `syncterm_igs_marker` verifies
literal marker rows, glyph overlays, both indexed phases and composed RGB
from one capture across all ST modes, Atari ST/CP437/UTF-8 and status choices.
It includes maximum-size and normalized edge markers, retained pens through
R, no replies and partial-input disconnect/fresh reconnect. Sound is disabled.
Other primitives, loops and sound remain pending.

### L: patterned shafts, wide geometry and arrowheads

`L` reads four X/Y-classified operands through the existing shared point
description and evaluator. It records the original start/end pairs after
successful drawing; arrowheads shorten private points only. `P` changes only
the final pair. `I/R` preserve both; whole reset/reconnect clear them.
Incomplete/invalid acquisition leaves all positions and pixels unchanged,
an explicit port policy replacing the native caller's intermediate stores.

Thin shafts use all six native line masks and the custom mask, the physical
line pen, all four writing modes, inherited clipping, absolute-X horizontal
pattern alignment, original-direction vertical sampling, X-ordered slopes and
X-major ties. Native bounded clip rounding is retained. Replace clears pattern
gaps, transparent/XOR skip them, and reverse transparent draws only gaps.
Thin arrows follow the patterned shaft; round-only ends have no extra pixels.

Wide lines use native circle rows/aspect averaging and perpendicular offsets,
solid bordered shaft polygons and ordered cap discs. Arrowhead triangles
precede the wide shaft and shorten its private endpoints. Begin/end arrow
ordering, minimum lengths and nested rounding remain native. Polygon fill
rows/intersections and thin bordered endpoints retain their independent rules,
including repeated XOR toggles, degenerate shafts and zero-length cap behavior.
No host ideal-width or repeated-stroke substitute is used.

Last-segment ownership persists across raster calls and is initialized true
at full reset. Polygon vertical rejection leaves it false; a later initial
cap can inherit its nonfinal-XOR endpoint behavior. Thin polylines/perimeters
and `P` establish their own final ownership. `I/R` preserve it. Disc spans
always activate the inherited clip bounds; other line/polygon paths use the
workstation flag. The host status row is protected after native raster phase.
Workstation attributes, palette, banks, console cursor and memory controls
are preserved. Each blink phase retains its own pixels in the existing bitmap.

Wide host arithmetic avoids native overflow, division faults and stale-local
or out-of-buffer behavior. Circle data is derived from current width and
resolution without ambient TOS cache state. A full signed-word diagonal is
checked against its mathematical clipped pixels. Foreign host pixels retain
`P`'s XOR policy. This does not emulate CPU/stack/chip timing or native memory
accidents.

`cterm_igs_line` checks 3321 source-derived cases, both all-zero/all-one initial
phases, seven styles, odd widths 1..39, all end pairs, modes, depths, reversed
and degenerate segments, clipping, arrow thresholds and inherited cap state.
Hashes cover all 32000 native bytes at the observation boundary. The
[provenance ledger](../src/tests/igs/line-vectors.json) pins the independent
oracles and source fingerprint; maintainer generation runs neither SyncTERM
nor Atari instructions, and ordinary tests require no external tree/ROM.
Literal pixels, Drawto, g/array/random ordering, every focused split/bytewise
input, EOF/timeout/malformed recovery, I/R retention, status and reset are
checked separately. `syncterm_igs_line` observes coherent raw phases, cells,
palette, cursor, state and RGB for 36 derived scenes, literal text overlays
and bottom edges across all ST modes, Atari ST/CP437/UTF-8 and status choices.
No replies, partial disconnect and reconnect are covered with physical audio
disabled. `D`, other primitives, loops and sound remain pending.

## Deliberate corrections

The parser reports a bounded failure and cancels its pending command on
invalid grammar/resource use. Bytes already delivered to text or completed
command callbacks remain delivered, and `consumed` identifies the unread
suffix. The next feed begins attention scanning unless a successfully
accepted command is suspended. Explicit cancellation drops partial fields;
explicit EOF rejects them rather than synthesizing delimiters. A held ordinary
attention prefix is emitted only at explicit source EOF, never a read boundary.

The following parser policies intentionally avoid original reader accidents:

- More than the dialect's digit limit is rejected at the numeric delimiter,
  rather than allowing a wrapping digit counter or an accidental zero value.
- `+`, `-` and `!` are accepted only as an initial loop-expression prefix.
  Embedded/repeated prefixes are rejected; the original weighted digit-slot
  behavior of `12+3` is not reproduced.
- Continuation skips exactly two bytes and resumes ordinary decimal
  accumulation. It does not repeatedly weight old digit slots.
- A loop/random symbol consumes one trailing byte, without the original
  sequential reinterpretation of another symbol in that trailing position.
- Partial timed-out strings and invalid EOF never expose stale buffer tails.

These are parser port policies, not claims that associated IGS commands have
been completed. Constant and array values and the initial random ranges can
be evaluated;
loop-context operands and command-specific array/math commands remain pending.
The CTerm adapter recovers from incomplete source EOF by discarding partial
grammar and logging a diagnostic. Incomplete header input has a five-second
local timeout after progress; expiry discards the header. An ordinary held G
prefix alone has no header timeout. No Atari bucket/wrap arithmetic is used.
