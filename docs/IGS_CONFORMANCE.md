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
`D` draws from the remembered end and copies its requested new endpoint to
both remembered pairs, preserving native Drawto chaining.
X dispatch is in progress: `X10` sets the next Drawto origin without drawing.
Its coordinate classes are corrected to normal X/Y scaling as described below.
`z` draws connected 2–128-point polylines without changing either remembered
position pair, with serial XOFF/XON around drawing. `Z` fills inclusive
rectangles with native built-in/user patterns and no extra perimeter.
`B` adds bars with optional fill-colour perimeter and filled rounded boxes.
`U` adds rounded outlines, positive-flag rounded fill and negative-flag no-ops.
`O` adds filled circles with native pixel aspect and adaptive integer curves.
`Q` adds ellipses with independent X/Y radii through the shared filled-curve path.
`K` adds open circular arcs with line styles, widths, joins, caps and arrowheads.
`J` adds elliptical arcs with independently supplied X/Y radii through the same path.
`V` adds patterned circular pies closed through their center with optional perimeter.
`Y` adds elliptical pies with independently supplied X/Y radii through that path.
`f` fills counted 1–128-point polygons with the same native spans/perimeter and
serial XOFF/XON around drawing.
`F` adds seed-colour flood fill with native ordered runs, stored bounds and
independent blink phases through the indexed raster writer.
`E` selects graphical text effects, canonical point size and rotation; `W`
renders original system glyphs with software effects, rotations and underline.
`c` sets physical console colors through persistent native VT52 parsing.
`p` positions the console using unscaled word operands and the same persistent
VT52 state, including byte wrapping and pending-sequence composition.
`m` adds repeated home and directional motion with native signed count and
selector rules through that same VT52 state.
`r` adds one-shot cursor save and restore shared with raw VT52 j and k.
`k` adds counted cursor visibility and serial destructive backspace.
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

### D: Drawto chaining and original endpoints

`D` consumes two X/Y operands through the shared point description and calls
the existing line renderer from the remembered endpoint. Only the new
coordinates are evaluated by g/array/random handling; old coordinates stay
in pixels across g/R changes. After successful drawing, both remembered
pairs equal the requested new endpoint. Native arrow shortening affects only
private raster points. Thus `L` supplies its original requested tip and `P`
supplies its centre as the next D origin. Both reverse-transparent and clipped
requests still update positions after ordinary completion.

Patterns, modes, colours, widths, caps, heads, clipping, last-segment state,
independent blink phases and host status protection share L's native renderer.
Each D starts a new polyline invocation; repeated joins retain separate XOR
visits. Workstation attributes, palette, banks, console cursor, g and memory
controls are preserved, with no new waits or replies. I/R preserve points;
whole reset/reconnect clear them. Suspended R processing leaves following
D input unread until the local wait and geometry update complete.

The port publishes positions only after complete valid input and successful
rendering, consistent with L's atomic acquisition policy. Partial/invalid
headers, EOF and local timeout preserve the previous pairs and scene. Renderer
failure preserves positions and propagates its status. This deliberately
omits the native caller's early/intermediate position stores. Shared
loop-context execution remains pending.

`cterm_igs_drawto` replays 3321 independent source-derived line cases through
D using actual P/rejected-L origins and both initial blink phases. It checks
full native-capacity hashes and final positions across all styles, odd widths,
end pairs, modes and depths, including clipping, reversals, degenerate lines,
arrows and inherited cap state. Expected pixels are the checked-in independent
data, never observed L rendering. Shared helpers retain one fixture/observation
implementation. Literal chain pixels, every focused split/bytewise input,
L/P positions, error/cancellation/reset paths, renderer failure, I/R/I5 lifetime,
R suspension, g/array/random ordering and extreme/status cases supplement it.
`syncterm_igs_drawto` checks 36 source-derived scenes plus literal chains,
arrowed-L/visible-P origins, all-mode text overlays and normalized bottom
edges. Each capture combines cells, both pixel phases, palette, cursor,
four-coordinate state, attributes and RGB across all ST modes,
Atari ST/CP437/UTF-8 and status choices. No replies and partial-input
disconnect/reconnect are covered with physical audio disabled. X selectors,
other primitives, loops and sound remain pending.

### X dispatch and X10: non-drawing Drawto origin

X reads one unscaled selector without clamping. Values outside 0..15 consume
only that operand, make no store/call/reply and allow normal chaining. Fixed
selector descriptors reuse the shared command/point descriptions. T and X
share effect-free arity preview; selector/random values advance once when
the command completes. Only X10 is implemented so far. Other valid selectors
return NOT_SUPPORTED after the selector and leave payload unread; those
handlers and full X dispatch/loop conformance remain pending.

X10 sets only the remembered end pair, preserving the start, attributes,
clipping/bounds, last-segment ownership, palette, banks, g snapshot, memory
controls, console/cursor and pixels. It adds no wait or reply and supplies
the next D origin. Partial/invalid acquisition does not publish intermediate
positions; EOF, local timeout and reset cancel pending fields. I/R preserve
the points; full reset/reconnect clear them.

**Deliberate scaling correction:** the original binary reads coordinate
classes 0/1. The manual describes X10 as equivalent to L/P positioning without
drawing and g as covering all X/Y coordinates. The port treats the 0/1 pair
as an off-by-one call-site bug and uses classes 1/2 with the shared X/Y
description. Normalized 5000,5000 reaches the centre; g2 doubles only mono Y.
Array indices scale before lookup, and loaded/random values are not rescaled.
R/I5 retain g's captured resolution until g is reissued. The source's actual
0/1 behavior remains recorded in reference 19; this correction follows the
goal's intended-behavior and bug-fix policy.

`cterm_igs_x10` uses literal scalar vectors across every resolution/g mode,
dual-phase invariance, signed/lookup boundaries, invalid-selector arity,
pending-selector errors and byte ownership. It tests every focused split/
bytewise chain, random selectors and coordinates committing once, partial/
malformed input, EOF/timeout/reset, I/R/I5 lifetime and R suspension. Shared
fixture extraction retains L/D observation tests without duplicated vectors.
`syncterm_igs_x10` checks coherent literal scenes, indexed phases, cells,
palette, cursor, position/scaler state and RGB in all ST modes,
Atari ST/CP437/UTF-8 and status choices. It includes captured geometry,
invalid-selector chains, an exact following D, no replies, partial disconnect
and reconnect with physical audio disabled. Completing X10 does not complete
the parent X command or its other selectors.

## Connected polylines (`z`)

`z` clamps its unscaled signed count to 2–128 and consumes exactly that many
X/Y pairs. Oversized discarded pairs remain ordinary following input.
Count/coordinate random operands commit once, including across split input.
The shared L/D/z renderer retains segment pattern phase, nonfinal XOR endpoint
ownership, clipped final-segment state, wide shaft/join ordering and private
arrowhead shortening/propagation across short or repeated points. `z` leaves
both remembered Drawto pairs, attributes and clipping unchanged.

Serial drawing brackets a successful render with XOFF/XON when the respective
current loop mode is not exactly 1. Each helper gates once on flow enable,
then reads its signed count and byte per queued response. Initial/full-reset
configuration is enabled, bytes 19/17 once each (original accessory DATA).
I/R preserve it. SyncTERM's response transport owns readiness; no foreign
BIOS poll or device delay is reproduced. A failed draw sends no extra XON;
callback reset cancels further sends/drawing. X5 and `&` remain pending;
their relevant native state is injected for the helper tests.

`cterm_igs_polyline` includes 1,692 independently derived complete raster cases
in all resolutions and modes, with separate initially clear/set phases,
standard/custom patterns, wide joins, all cap combinations, clipping,
closed/retraced/repeated and short-prefix paths. The checked-in
[vector provenance](../src/tests/igs/polyline-vectors.json) and maintainer-only
[generator](../src/tests/igs/generate_polyline_vectors.py) identify the
reference's independent arithmetic/per-bit/circle/arrow models; no expected
pixels come from SyncTERM. Literal corners/edges, signed count clamps,
128-point capacity/discarded suffix, every focused split/bytewise input,
once-only random count/coordinates, partial/error/EOF/timeout/reset recovery,
I/R/I5 and suspended R supplement the matrix. Flow tests check default/custom
bytes, repetitions, zero/negative counts, loop/gate changes, before/after pixels,
callback reset and failed drawing.

`syncterm_igs_polyline` checks 36 source-derived connected-line scenes and
literal overlays/edges/following-D cases across all ST modes, Atari ST,
CP437 and UTF-8, and both status choices. Related raw phases, cells, palette,
position/attributes and RGB use one coherent capture. Exact flow replies,
partial disconnect and fresh reconnect/default flow are checked with physical
audio disabled. References: 15/20/43/44/46/50/51/137. Superseded planar/clock
experiments remain outside the active tests. The complete IGS goal is unfinished.

## Inclusive rectangle fill (`Z`)

`Z` consumes four X/Y coordinates, orders a private rectangle and includes
both endpoints, including single-pixel/row/column cases. It uses the fill pen
and adds no perimeter even when the perimeter attribute is set. It preserves
Drawto pairs, attributes, clipping, last-segment state, palette, RNG and caller
values, with no wait, flow helper or reply. All 38 built-in fills use original
US TOS1.04 rows; the active single-plane user pattern uses its copied 16 rows.
Canonical Y masks and X bits anchor to absolute screen coordinates through
clipping. Source-bank changes after `A4` cannot change the active pattern.
Both indexed phases use the four native writing modes; XOR ignores pen colour
and toggles every active physical bit at set pattern bits.

`cterm_igs_rectangle` checks 1,992 independently derived complete native-frame
cases over different initial plane bits, every built-in fill plus user rows,
all modes/resolutions, corner orders, clipped/rejected rectangles, perimeter
settings, last state and fringe/degenerate/negative-coordinate cases.
[Fixture provenance](../src/tests/igs/rectangle-vectors.json) and the
[maintainer generator](../src/tests/igs/generate_rectangle_vectors.py) record
original literal tables, ROM identity and independent coordinate/per-bit
models. Tests do not depend on the reference tree or generate expected pixels
from SyncTERM. Literal checks cover pens, signed extrema, normalization/g2,
array-index transform ordering, every focused split/bytewise input, random
operands/seeds, unchanged incomplete/error phases, EOF/timeout/reset,
suspended R and I/R/I5, invalid state, copied-user ownership and no replies.

`syncterm_igs_rectangle` checks 60 source-derived scenes plus literal user rows,
glyph overlays in every writing mode, status/bottom edges and a following D.
All ST modes, Atari ST/CP437/UTF-8 and status choices use coherent related
pixels below the token row, both native phases, cells, palette, state and RGB.
Partial disconnect and fresh reconnect restore independent state. Physical
audio is disabled. References: 15/29/47/131/134.

The atomic parser rejects incomplete/invalid acquisitions before drawing.
Outside-canvas writes are clipped safely rather than reproducing native
adjacent-memory writes or address wrap. Inverted clip bounds are a no-op;
impossible active pattern selectors return an error before drawing. These
port safety policies do not emulate arbitrary native raw-memory states.
Other commands/selectors and the complete IGS acceptance remain pending.

## Bars and filled rounded boxes (`B`)

`B` acquires four X/Y values and an unscaled flag. Zero selects an inclusive
bar plus optional solid one-pixel fill-colour perimeter; any nonzero signed
flag selects rounded fill. Both forms ignore graphical line pen/style/width/
caps and preserve positions, attributes, palette, clipping and caller values.
No wait, reply or flow helper is added. Rounded construction uses native
aspect-limited radii, the exact three quarter-sample trig values/divisor32767
truncation and the full 21-point sequence with repeated closure vertices.
Patterned polygon spans and optional solid borders retain native ordering,
rounding, anchoring and XOR overlaps, including degenerate dimensions.

A bar without perimeter preserves last-line state; its executed border finishes
true. Rounded fill clears it before clipping and leaves false after vertical
rejection or with no perimeter. An executed rounded border finishes true even
when its final edge repeats a point. Source rows copied by `A4` remain active
after a source-bank edit. The existing safe wide-arithmetic/atomic acquisition
policy applies: no wrapped dimensions, corrupt native memory or stale locals.
Host/status clipping discards writes after segment traversal, preserving
native endpoint ownership instead of creating a new endpoint at the host edge.

`cterm_igs_box` has 2,688 independently derived complete native-frame cases
with different primary/alternate initial plane bits, every built-in fill plus
user rows, all resolutions/modes, bar/rounded/perimeter combinations, corner
order, repeated/narrow/zero dimensions, clipped/rejected/inverted bounds,
partially outside unclipped paths and last-line state. Line width 39, custom
masks, caps and other pens cannot change these fill pixels. The independent
[fixture provenance](../src/tests/igs/box-vectors.json) and
[generator](../src/tests/igs/generate_box_vectors.py) record reference geometry,
rational-span, endpoint and Boolean models and ROM identity; no expected
pixels are generated by SyncTERM. Literal tests cover the hand-derived corners,
signed/unscaled flags, once-only random operands/flag, five-field partitions,
partial/error/EOF/timeout/reset, signed extrema, array scaling, I/R/I5 and
suspended R, copied-user ownership and retained state.

`syncterm_igs_box` adds 96 source-derived scenes, user fills, glyph overlays
with/without borders in all modes, status/bottom edges, following D and
partial disconnect/reconnect. All ST modes, Atari ST/CP437/UTF-8 and status
choices use coherent related native phases, cells, palette, state and RGB;
physical audio is disabled. References: 15/46/47/48/49/131/134. Remaining
commands/selectors and complete IGS acceptance stay pending.

## Rounded outlines and signed routing (`U`)

`U0` draws a rounded outline using the line pen/style/width; a positive flag
draws rounded fill, and a negative flag consumes all five arguments without a
primitive. The manual reverses the normal zero/positive meanings; the original
2.21 binary/GDP paths govern this implementation. Flags are unscaled signed
values. Suppressed drawing still evaluates valid random operands once.

B/U share the exact native 21-point construction. Thin outline traversal has
20 segments and no polygon-added final edge. Wide outlines retain native shaft/
join order. Endpoint arrows/outer cap discs are suppressed for the call while
the original cap fields remain unchanged, including on failure. Fill attributes
cannot alter outlines; positive U reuses B's fill/perimeter behavior. Both
preserve Drawto pairs, attributes, palette and clipping, with the native
last-segment result and no reply/wait/flow call. The established atomic input,
wide arithmetic and post-traversal host/status write bounds apply.

`cterm_igs_round_box` checks 1,845 independent complete outline frames in both
indexed phases and reuses 1,344 independent rounded-fill frames from B for
positive U. Coverage includes all modes/resolutions, seven line styles, every
endpoint pair, native odd widths, degenerate/repeated paths, clipping/rejection,
inverted bounds, outside unclipped paths and retained state. Negative flags
check unchanged phases/attributes at all g modes, even with unusable rendering
attributes. Focused partitions/bytewise input, random coordinates/flags,
EOF/timeout/error/reset, g2 pixels, I/R/I5/suspended R and cap-preserving errors
are covered. The [provenance](../src/tests/igs/round-box-vectors.json) and
[maintainer generator](../src/tests/igs/generate_round_box_vectors.py) identify
independent geometry/circle/span/endpoint/bit models and reused B fixtures;
no expected image is produced by SyncTERM or needs historical runtime assets.

`syncterm_igs_round_box` checks 84 outline scenes, including 24 cases where
console text replaces part of thin/wide graphics, plus 48 shared positive-fill
scenes. All ST modes, Atari ST/CP437/UTF-8 and status choices use coherent
native phases/cells/palette/state/RGB, with independent complete glyph-cell
replacement. It also checks bottom/status edges, following D, no replies,
partial disconnect/reconnect and fresh state, with physical audio disabled.
References: 15/43/44/46/49/50/134. Remaining commands/selectors and full IGS
acceptance stay pending.

## Filled circles (`O`)

`O` reads center X/Y and an X-class radius through the shared numeric reader.
Native GDP4 derives the signed Y radius with aspect 338/372, 169/372 or 372/372,
chooses 32–128 curve steps from the larger signed radius, and fills the S+1
point polygon. It retains the explicit final point and polygon-added closing
edge, including repeated zero-radius visits and ordered XOR endpoints. The
fill pen, active pattern, perimeter and writing mode apply; line attributes
cannot alter the circle. Both remembered position pairs and all attributes,
palette and clipping remain unchanged. There is no reply, wait or flow helper.

The original 92-word sine table (including pad), quadrant folding and integer
interpolation are shared with B/U's rounded points. Native divisor 32767 radius
products truncate; replacing this with floating-point trigonometry or ordinary
nearest rounding would alter pixels. O's generated angles are 0..4500, so this
path never accesses the original negative-angle ROM window. Signed coarse
rejection happens before polygon entry and preserves incoming last-line state.
Executed polygon fill clears it; executed perimeter sets it. Negative radii
retain these signed rules. Wide safe geometry corrects accidental coordinate/
radius wrap and CPU faults; host/status bounds discard writes after traversal.

`cterm_igs_circle` checks 3,126 independent complete native-frame cases in both
indexed phases: all 39 built-in/user selections, every mode/resolution/perimeter,
all adaptive step counts 32..128, small/zero/negative/large radii, clip rejection,
inverted bounds and partially outside unclipped paths. It checks retained
positions/attributes/palette/arrays/seeds, all 16 pens, extreme signed radii,
status safety, focused partitions/bytewise input, random operands, incomplete/
invalid recovery, reset and I/R/I5 lifetime. The
[fixture provenance](../src/tests/igs/circle-vectors.json) and
[maintainer generator](../src/tests/igs/generate_circle_vectors.py) identify
independent curve/span/endpoint/whole-word Boolean models and ROM/table hashes.
Expected data never comes from SyncTERM output; ordinary tests require none
of the external reference assets.

`syncterm_igs_circle` checks 48 independent scenes plus literal glyph overlays,
console-cell replacement, normalized/status bottom pixels, following D,
no replies and partial disconnect/reconnect. All ST modes, Atari ST/CP437/UTF-8
and status choices use coherent phases/cells/palette/state/RGB, with physical
audio disabled. References: 15/46/48/49/131/138. Other commands/selectors and
full acceptance remain pending.

## Filled ellipses (`Q`)

`Q` reads center X/Y and independent X/Y radii through the existing four-field
coordinate description. Y radius follows g's Y conversion; array values are
loaded after index conversion and are not transformed again. IGS's workstation
opens with transformation 2, so native GDP5 retains the supplied Y radius.
There is no circle pixel-aspect conversion or positivity clamp. Both Drawto
pairs remain unchanged. References: 15/31/46/48/49/131/138.

O/Q share signed coarse rejection and its incoming last-state preservation,
32–128 steps selected from the larger signed radius, original integer trig,
patterned polygon fill and solid fill-colour perimeter. Q's interior angles
sweep 3600 but its explicit final angle is 0, whereas O uses 3600. Both retain
that final point and the polygon-added closing edge for ordered XOR behavior.
Line attributes are retained and ignored; palette/clipping/arrays/banks/seeds
stay unchanged. Established safe widened geometry, atomic acquisition and
post-traversal host/status bounds apply. No reply, wait or flow helper is added.

`cterm_igs_ellipse` checks 3,660 independent complete native frames in both phases:
all 39 fill selections, every mode/resolution/perimeter, tall/wide/zero/mixed-sign
radii, all 97 step counts selected independently by each axis, clip rejection,
inverted bounds and partially outside unclipped paths. Literal checks add
all 16 pens, extreme signed radii, status protection, g2 center/radius Y behavior,
indexed operand ordering, focused splits/bytewise input, once-only randomness,
EOF/timeout/error/renderer recovery, reset and I/R/I5 lifetime. The
[provenance](../src/tests/igs/ellipse-vectors.json) and
[generator](../src/tests/igs/generate_ellipse_vectors.py) use the shared
[independent oracle](../src/tests/igs/filled_curve_oracle.py); circle hashes and
application runs regenerated unchanged after extraction. Ordinary tests do not
need external assets and expected pixels never come from SyncTERM.

`syncterm_igs_ellipse` and `syncterm_igs_circle` share one coherent capture
recipe with separate independent scenes. Q adds 48 source-derived scenes plus
literal zero-radius glyph overlays, complete console-cell replacement,
bottom/status pixels, following D, no replies and partial disconnect/reconnect.
All ST modes, Atari ST/CP437/UTF-8 and status choices check indexed phases,
cells, palette, state and RGB with physical audio disabled. Other commands/
selectors and complete acceptance remain pending.

## Circular arcs (`K`)

`K` reads center X/Y, X-class radius, and two unscaled degree angles. It shares
native circular aspect, signed coarse rejection and integer curve generation
with O/Q, then runs the open thin/wide line path. Every sweep, including zero
or narrow sweeps, uses 32–128 radius-derived steps and S+1 points with explicit
begin/end angles. The renderer's private 129-vertex capacity is separate from
z's unchanged serial 128-point limit. Line patterns/phase, XOR endpoints,
wide joins and normal caps/arrows apply; fill attributes are retained and
ignored. Thin shafts precede heads; wide heads shorten only private points.
Coarse rejection preserves incoming last state; later line/polygon/cap work
sets its own state. Both Drawto pairs, attributes, palette/clipping, banks,
arrays and seeds remain unchanged by rendering; random operands still advance
once during numeric evaluation. No reply/wait/flow helper is added.

The native sweep rule subtracts begin from end and adds one 3600 turn if
negative. Wide degree/angle arithmetic preserves that rule without accidental
word wrap; periodic reduction occurs only at sine/cosine lookup. It avoids
reads before the original sine table and signed-word cosine overflow, rather
than drawing foreign ROM-derived shapes. Native safe-domain integer pixels
remain exact. This deliberately corrects corrupt/overflow behavior for negative
or large angles. Existing safe geometry and host/status bounds apply after
native traversal, including to heads/caps.

`cterm_igs_arc` checks 3,876 independent complete native frames in both phases:
all styles, endpoint pairs, odd widths, modes/resolutions, sweep/step boundaries,
zero/negative/large radii, clipping/rejection/inverted bounds and outside paths.
Its 192 corrected-angle cases use a common whole-turn shift of both endpoints
to obtain source-derived positive-domain geometry with identical interpolation
and sweep. Literal tests cover pens, zero-sweep XOR parity, extreme sums,
coarse rejection before renderer validation, axis/array/unscaled-angle ordering,
focused partitions/bytewise input, once-only randomness, invalid/incomplete/
renderer recovery, I/R/I5 and reset. The
[provenance](../src/tests/igs/arc-vectors.json) and
[maintainer generator](../src/tests/igs/generate_arc_vectors.py) compose independent
curve/arrow/circle/span/line/Boolean oracles. Ordinary tests need no external
assets, and expected images never come from SyncTERM output.

`syncterm_igs_arc` checks 84 independent scenes, including 24 console-after-graphic
replacements, plus glyph overlays, bottom/status pixels, following D, no replies
and partial disconnect/reconnect. All ST modes, Atari ST/CP437/UTF-8 and status
choices use coherent cells/phases/palette/state/RGB with audio disabled. The
complete glyph-cell replacement recipe is shared with U. References:
15/43/44/46/48/49/50/51/138. Remaining commands/selectors and full acceptance
stay pending.

## Elliptical arcs (`J`)

`J` reads center X/Y, independent X/Y radii, and two unscaled degree angles.
Its coordinate prefix is shared with Q, and its angle-tail callback with K.
Arity follows operand-axis metadata without evaluating numbers. Y radius follows
g's Y conversion; loaded array values retain their words after index conversion,
while angle indices remain unscaled. The IGS workstation opens in transformation 2,
so the supplied Y radius is preserved without circular aspect conversion.
Native line width/cap/arrow geometry still uses device pixel metrics.

K/J share signed coarse rejection, 32–128 radius-derived steps, explicit
endpoints and open thin/wide line styles, joins, caps and arrowheads. Either
radius can determine subdivision. The established safe periodic angle correction,
wide geometry and post-traversal host/status bounds apply. Both Drawto pairs,
attributes, palette/clipping, banks, arrays and seeds stay unchanged by rendering;
random operands advance once during evaluation. There is no reply/wait/flow call.

`cterm_igs_ellarc` checks 4,659 independent dual-phase native frames covering
styles/caps/odd widths/modes/depths, tall/wide/zero/mixed-sign radii, sweep and
step boundaries driven by both axes, clipping/rejection/inverted bounds and
outside paths. 192 cases cover corrected large/negative angles through common
whole-turn shifts into the safe native domain. Literal checks add pens, zero-
sweep XOR parity, extreme sums/rejection, six fields, g2 Y-center/Y-radius
behavior, array/angle ordering, focused splits/bytewise input, once-only RNG,
invalid/incomplete/renderer recovery, I/R/I5 and reset. The
[provenance](../src/tests/igs/ellarc-vectors.json) and
[generator](../src/tests/igs/generate_ellarc_vectors.py) use the shared
[independent arc oracle](../src/tests/igs/arc_oracle.py). Extraction regenerates
K's 3876 native hashes and 84 scene recipes unchanged. Ordinary tests require no
external assets, and expected pixels never come from SyncTERM output.

`syncterm_igs_ellarc` shares its coherent capture recipe with K. Its 96 independent
scenes include 24 console-cell replacements and 12 tall-ellipse scenes; extra
literal glyph overlays, bottom/status pixels, following D, no replies and
partial disconnect/reconnect cover all ST modes, Atari ST/CP437/UTF-8 and
status choices. Cells/phases/palette/state/RGB come from one capture, with
physical audio disabled. References: 15/31/43/44/46/48/49/50/51/138. Other
commands/selectors and complete acceptance remain pending.

## Circular pies (`V`)

`V` shares K's X/Y/X prefix and the two-unscaled-angle tail. After five-field
acquisition, native circular aspect and 32–128-step integer tessellation emit
S+1 arc points, then append the center. The S+2-vertex polygon uses the fill
pen/pattern/writing mode and optional thin solid perimeter, preserving both
radial-edge visits and ordered XOR behavior. Fill-point/intersection capacity
is 130; outline and serial limits stay unchanged. Line attributes are retained
and ignored. Both Drawto pairs, palette/clipping, banks, arrays and seeds remain
unchanged by rendering; numeric random operands advance once. No reply/wait/
flow call is added.

O/Q/V share signed coarse rejection, scanline fill, perimeter and last-state
transitions. Existing wide geometry, degree/angle arithmetic, periodic trig-only
correction, native single negative-delta adjustment and post-traversal host/
status write bounds apply. There are no pre-table ROM reads or overflow-driven
foreign addresses.

`cterm_igs_pie` checks 3,819 independent dual-phase native frames: all 39 fills,
all modes/resolutions/perimeters, convex/reflex/wrapped/zero/full/multi-turn
sweeps, signed/radius/step boundaries, coarse versus polygon rejection, inverted
clips and outside paths. 192 corrected-angle cases use common turn shifts into
the safe native domain. Literal checks cover pens, unusable ignored line state,
extreme sums/radii, status protection, coordinate/array/unscaled-angle ordering,
focused splits/bytewise input, once-only randomness, invalid/incomplete/renderer
recovery, I/R/I5 and reset. The [provenance](../src/tests/igs/pie-vectors.json)
and [generator](../src/tests/igs/generate_pie_vectors.py) reuse the shared
[independent filled-curve oracle](../src/tests/igs/filled_curve_oracle.py).
All O/Q native hashes and application recipes regenerate unchanged. Expected
pixels never come from SyncTERM output, and ordinary tests need no external assets.

`syncterm_igs_pie` shares the O/Q coherent capture recipe. Its 120 independent
scenes include 24 console-after-graphic replacements; extra glyph overlays,
even zero-radius pie XOR parity, bottom/status pixels, following D, no replies
and disconnect/reconnect cover all ST modes, Atari ST/CP437/UTF-8 and status
choices. Cells/phases/palette/state/RGB use one capture, with physical audio
disabled. References: 15/43/44/46/48/49/131/138. Other commands/selectors and
full acceptance remain pending.

## Elliptical pies (`Y`)

`Y` shares Q/J's X/Y/X/Y prefix and K/J/V's two-unscaled-angle tail. Six-field
acquisition preserves coordinate/index/unscaled-angle ordering; random operands
advance once each. In IGS's transformation 2 workstation, Y radius is supplied
independently and follows g's Y conversion, with no circular aspect adjustment.
V/Y share degree/sweep preparation and the common patterned fill path.

Either signed radius determines 32–128 steps. The explicit arc endpoints,
center append, 130-vertex capacity, radial closure, scanline patterns and solid
fill-colour perimeter retain native traversal/XOR semantics. Line attributes
are ignored without changing them. Both Drawto pairs, palette/clipping, banks,
arrays and seeds remain unchanged by rendering. There is no reply/wait/flow call.
Established safe periodic-angle corrections, wide geometry and post-traversal
host/status bounds apply; other primitive/serial limits stay unchanged.

`cterm_igs_ellpie` checks 5,286 independent dual-phase native frames: all 39 fills
with tall/wide radii, all modes/resolutions/perimeters, sweep and signed-radius
boundaries, all 97 step counts driven by both axes, clipping/rejection/inverted
bounds/outside paths and 192 corrected-angle cases. Literal checks cover pens,
ignored invalid line state, extreme safe sums/radii, status protection, g2
Y-center/Y-radius behavior, array/angle ordering, focused splits/bytewise input,
once-only six-field randomness, invalid/incomplete/renderer recovery, I/R/I5
and reset. The [provenance](../src/tests/igs/ellpie-vectors.json) and
[generator](../src/tests/igs/generate_ellpie_vectors.py) reuse the existing
[independent filled-curve oracle](../src/tests/igs/filled_curve_oracle.py).
O/Q/V expected native bytes and application recipes remain unchanged. Ordinary
tests require no external assets, and expected pixels never come from SyncTERM.

`syncterm_igs_ellpie` shares the O/Q/V coherent capture recipe. Its 132 independent
scenes include 24 console-cell replacements and 12 tall ellipses; extra glyph
composition/parity, bottom/status pixels, following D, no replies and partial
disconnect/reconnect cover all ST modes, Atari ST/CP437/UTF-8 and status choices.
Cells/phases/palette/state/RGB come from one capture, with physical audio
disabled. References: 15/31/43/44/46/48/49/131/138. Other commands/selectors and
full acceptance remain pending.

### f: counted filled polygons

`f` clamps signed counts to 1..128 and reads exactly that many X/Y pairs.
Its descriptor/evaluator/flow dispatch is shared with `z`, which keeps its
2..128 limit. Count preview has no RNG side effects; complete acquisition
precedes serial XOFF, rendering and XON. Current loop mode is retested before
each flow operation. Incomplete/error/reset paths preserve parser ownership.

Rendering shares the indexed patterned polygon path with O/Q/V/Y: native
orientation-sensitive intersection rounding, descending inclusive spans,
absolute pattern anchoring, all 39 fills, physical fill pen and optional solid
fill-colour perimeter. Line attributes are ignored. One/two/repeated/already-
closed lists follow native closure rules on private data. Drawto and retained
attributes/palette/banks/math state stay unchanged; last-line state follows
native vertical rejection and border execution. Wide safe arithmetic and host/
status clipping avoid original wrapped-address/scratch-buffer accidents.

`cterm_igs_polygon` checks 4,056 independent dual-phase full native frames,
including concave/self-crossing/reversed edges, every fill/mode/depth/perimeter,
127/128 points and 128 simultaneous intersections, clipping/rejection/inverted/
one-row bounds and disabled clipping. Literal and serial tests cover count-one
behavior, exact oversized-request ownership, pen mapping, coordinate modes,
wide extremes, every focused split and bytewise chain, once-only random count,
flow byte/count/order/live mutations/loop guards/reset interruption, maximum
flow counts and EOF/timeout/numeric/renderer/reset recovery, plus array-index
ordering and I/R/I5/suspended-R lifetime.

`syncterm_igs_polygon` checks 168 independent scenes (24 console replacements),
plus glyph/XOR/bottom/status/following-D/exact-flow/disconnect/reconnect in all
ST modes, Atari ST/CP437/UTF-8 and status choices, using coherent captures and
physical audio disabled. [Fixture provenance](../src/tests/igs/polygon-vectors.json)
and the [maintainer generator](../src/tests/igs/generate_polygon_vectors.py)
share the [independent polygon/curve oracle](../src/tests/igs/filled_curve_oracle.py).
Ordinary tests need no external assets; expected pixels never use SyncTERM
output. References: 15/43/44/46/131/137. Other commands and full acceptance
remain pending.

### F: seed-colour flood fill

`F` reads X/Y through the common evaluator and shares f/z's live serial
XOFF/render/XON/loop/reset path. Drawto is unchanged. The fill uses native
live constant-colour run probes, initial upward discovery, queued directions,
first-hole/collision/tail-compaction and overhang repairs. Inclusive spans
use physical fill pen, all 39 anchored patterns and writing modes; perimeter
and line attributes are ignored. Stored bounds apply even with clipping
disabled. Rejected seeds retain last state; accepted setup clears it.

Each blink phase probes and paints independently, then both publish together.
The queue is owned/growable instead of native shared scratch and its 640-entry
limit; a source-derived 32-active-run comb exercises growth. Bounds intersect
the real host/status canvas, guard reads are omitted, and safe arithmetic
replaces native lower-Y overflow/address accidents. Foreign RGB/index seeds
are skipped per phase. The reachable VDI103 hook is retfalse; no extra host
keyboard polling or arbitrary Line-A quit/boundary-colour interface is added.

`cterm_igs_flood` checks 3,660 independently derived full native frames with
different phase topologies, all fills/modes/depths, ignored perimeter, varied
seeds and stored/clipped/rejected/inverted/one-row bounds. Literal and serial
checks cover all pens, protected status/escaped bounds, queue growth, focused
splits/bytewise chains/two-field RNG, flow byte/count/order/mutation/loop/reset/
maximum counts, EOF/timeout/numeric/renderer recovery, index ordering, g2 Y,
I/R/I5/suspended-R lifetime, negative rejected seeds and foreign phase pixels.

`syncterm_igs_flood` checks 168 independent component scenes, including 24 full
console replacements, plus enclosed glyph/XOR/perimeter-ignore, bottom/status,
following D, exact flow and disconnect/reconnect in all ST modes, Atari ST/
CP437/UTF-8 and status choices. Coherent captures check cells/phases/palette/
state/RGB; physical audio is disabled. Explicit physical-colour run data
extend the shared capture checker; older ink-only cases retain their recipes.
[Fixture provenance](../src/tests/igs/flood-vectors.json) and the
[maintainer generator](../src/tests/igs/generate_flood_vectors.py) use independent
four-neighbor components/per-plane equations, not native queue simulation or
SyncTERM output. Ordinary tests require no external assets. References:
15/44/68/131/137. Other commands/selectors and final acceptance remain pending.

### E: graphical text attributes

`E` reads three unscaled effects/points/rotation operands, then masks effects
to 31, resets rotations above 4, applies signed quadrant rounding and selects
a canonical source/doubled font after clamping point requests to 8..20. The
six effective point/top pairs are 8/4, 9/6, 10/13, 16/9, 18/13 and 20/27.
Face/alignment/text pen and unrelated attributes stay unchanged. E sets point
mode, draws nothing, moves no cursor/Drawto and emits no replies/flow/waits.
The existing height field holds font top, not full bitmap/cell height; no IGS
cache of caller-local metric output words is added.

Rotation 4 retains the native 3600 attribute. Wide negative arithmetic avoids
word-wrap accidents while retaining defined signed rounding. W must normalize
periodic angles at use instead of native stale-origin/uninitialized/escaped-
RAM behavior. I0/I2 restore coherent effective point size with saved pixel
height; palette-only I and R/I5 preserve the text selection, while reset and
reconnect restore entry state. Canonical built-in face1 sizing is used; no
arbitrary foreign font rings or pointer aliases are introduced.

`cterm_igs_text_attributes` executes 219,072 literal/algebraic matrix cases:
all signed word values in the three argument roles across ST modes, plus
all 32 masks/request8..20/rotation0..5/scaling0..2 cross-products. Full indexed
phases, palette/banks/other attributes/Drawto/seeds remain unchanged. Focused
splits/bytewise mixed-text chains, exactly three RNG draws, unscaled index
clamping, invalid/incomplete recovery, I0/I1/I2/R/I5/suspended-R/reset and
independent blink-pixel checks supplement those cases.

`syncterm_igs_text_attributes` checks 120 independent state scenes in all ST
modes, Atari ST/CP437/UTF-8 and status choices, with physical audio disabled.
Coherent captures combine test-only text state with unchanged console glyphs,
pixels/phases/palette/cursor/other graphics state/RGB. I1 preservation, I0/I2
restoration, following D, no replies and partial disconnect/fresh reconnect
are checked too. Expectations come from literal source data and arithmetic,
not SyncTERM output. These establish E state and console retention; W glyph
pixels and graphical text effects are covered below. References:15/29/30/139/144.

### W: graphical text

`W` acquires two typed X/Y operands and an opaque128-byte text window.
CR/LF are skipped; @ terminates, empty @ is literal, and a trailing @ after
128 bytes belongs to subsequent input. Embedded NUL stays stored but ends
visible text. ANSI/UTF-8/G# bytes are glyph data. Ordinary W sends no reply,
flow or wait. Loop text reuse and first-@-text XON are supported with native
guards; tests supply the context until containing loop commands are complete.

An optional synchronous shared-parser field callback starts acquisition once,
before payload. IGS uses an absolute local five-second deadline, with no
extension from progress. Partial/invalid/expired input is discarded instead
of rendering stale tail bytes. Successful text survives I/R; reset/reconnect
clears it. The loop counter saturates at host capacity instead of native word
wrap. Timeout uses the existing beep request; headless tests disable audio.

The three original TOS fonts supply768 decoded source glyphs and six source/
exact-double metric records shared with E. Import cross-checks packed forms,
offsets and independent row exports; provenance pins ROM/font/mask hashes.
A bounded decoded-mask renderer implements native thickening, skew, outline,
rotation, clipping and light phases, composing both indexed blink phases.
Underline reuses the segment raster with native mask/LastLine behavior.
W preserves text/line/fill attributes, Drawto/LastLine, palette/banks/math and
seeds beyond normal operand evaluation. Safe periodic angles replace foreign
stale/escaped RAM; host/status writes remain bounded. There is no planar
screen, foreign scratch-memory model, arbitrary loaded-font API or hardware
clock requirement.

`cterm_igs_text` checks22,554 independent complete dual-phase native frames:
32 effect combinations, six sizes, quadrants, writing modes, all ST depths,
clip/corner/inverted/disabled cases, all768 source glyphs, all18 alignments
and corrected periodic angles. Separate font-array and line-bit oracles
construct expectations without SyncTERM output; ordinary tests use checked-in
assets only. Literal clip-before-bold/glyph rows, NUL/CRLF/empty/limit/opaque
payloads, every focused split/bytewise and once-only RNG, reuse/XON/reset,
absolute timeout/EOF/clock failure/error, off-screen advance, coordinates,
status and font/window lifetime add focused coverage.

`syncterm_igs_text` has186 independent full-scene expectations, including24
console-cell replacements after graphics. Coherent captures check glyph
interactions, six sizes, combined effects/rotations/modes, raw/boundary text,
indexed phases/cells/cursor/palette/state/RGB, a following D, partial disconnect and fresh
reconnect in all ST modes, Atari ST/CP437/UTF-8 and status choices. Physical
audio is disabled. References:15/30/32–38/43/44/139/140/144. Full containing
loop commands, other commands/selectors and final acceptance remain pending.

Verification: native FreeBSD /usr/bin/cc Release configure/full builds passed
without warnings. All91 selected terminal/graphics/Wren/application regressions
passed. An additional clock-error test reproduced a lost poll deadline after
text entry; after correction, all three affected parser/native-W/application-W
suites passed again, including the full matrix, scenes and following-D checks.
The ledger now has27 completed entries; X and final acceptance remain unfinished.

### c: console colors

`c` reads two unscaled words before emitting console bytes. Selector0 emits
ESC c/color, selector1 emits ESC b/color, and upper selectors select0. A
negative selector consumes color but emits only ESC. The color's low byte
has no+32 adjustment or VDI pen translation. No palette/VDI setter, host reply,
flow or wait occurs. Raw foreground/background retain word(byte-32); physical
pixels use the corresponding low bits through the indexed palette.

IGS shares the existing VT52 handlers and raw-parameter reader with a strict
TOS one-byte ESC accumulator. Pending ESC/Y-row/Y-column/FG/BG state can
consume the same generated bytes differently. Native output commits one call
at a time using Atari bytes, with wire codepage/UTF-8/LF expansion kept outside
that operation and restored on errors. Generated operations are not replayed
through received-scalar hooks or raw-wire logging. Standalone VT52 keeps its
existing compatibility grammar.

Physical color bits carry no ANSI blink/intensity or font-bank semantics.
Reverse affects glyph colors; scroll blanking uses the retained BG word.
Fixed eight-cell TAB clips at the edge. Native Y parameters use unsigned-word
subtraction/clamping within the host viewport, including status protection.
No foreign console-function pointers, hardware memory or cycle model is added.

`cterm_igs_console_color` has1,179,648 algebraic word-state cases,1,536 literal
color-pair/reverse/depth glyph cases and3,696 independent complete-frame prior
state/control/wrapping/status cases. Both phases are checked. Literal original
font rows and a source-state interpreter supply expectations. Focused split/
bytewise/RNG, pending negative-selector ESC, incomplete/invalid/local-timeout
recovery, I/R/reset lifetime, no replies and output-error scope tests supplement
the matrices. Another2,688 cases check all224 printable bytes through both
wire modes and console fonts. They exposed DEL being dropped by Unicode
control interpretation; generated printable bytes now use literal one-cell
placement. The high-background literal exposed an ANSI blink mapping error;
a native control test exposed LF expansion leaking into generated output.

`syncterm_igs_console_color` has360 source-derived wire-reachable scenes in
all ST modes, Atari ST/CP437/UTF-8 and status choices. Coherent captures check
raw colors, pending parser state, cursor/cells/indexed phases/palette/RGB and
other graphics state. An existing W glyph survives color changes and moves
with native scrolling. Unique non-drawing X10 fences preserve pending console
state; captures already use logical pixel rows. Partial disconnect and fresh
reconnect check clean color/parser state. Physical audio/MIDI stay disabled;
BEL cases make no audible-device or synthesized-waveform claim.
`syncterm_igs_console_color_del` adds six independent DEL scenes through
the full mode/encoding/status matrix.
References:14/15/66/195. Other commands/selectors and final acceptance remain
pending.

Verification used native FreeBSD /usr/bin/cc Release. Configure/full builds
passed without warnings. All93 selected regressions passed before the final
literal-byte correction. The expanded native suite,360-scene application suite
and separate six-scene DEL suite passed afterward. Expected pixels/advance
were retained when fixing blink, LF expansion and DEL; captured planes are
read in their documented logical row order. The ledger now has28 completed
entries, with X and final acceptance unfinished.

### p: console positioning

`p` acquires column then row as unscaled words before output. It emits
ESC Y, low-byte(row+32), low-byte(column+32) using defined unsigned addition
and the existing native console output helper. It adds no parser or console
state. Normal-state Y handling performs unsigned word subtraction and clamps
within the host viewport, including status protection. For example,
`p(300,400)` reaches column 44 in 80 columns or 39 in 40, and the bottom row;
`p(-32,-32)` also reaches the bottom-right corner. Values below32 in native
parameter bytes wrap to large unsigned coordinates rather than home.

The four bytes enter whatever console state is already pending. They can
become raw parameters, glyphs or controls, and can leave another ESC/Y/color
parameter pending. Direct cursor assignment would lose that behavior.
Neither decimal values nor loaded math words are g-scaled. No reply, serial
flow, video wait, palette change or VDI attribute call is introduced.

`cterm_igs_position` checks 157,252 algebraic coordinate cases: one exhaustive
signed-word sweep in each role, then all byte classes with negative/positive/
carry representatives and signed extremes across every mode/status/g setting,
with unchanged indexed phases/graphics state. Another 4,452 full-frame cases
compose all six console states and a wrapping bottom-row context with signed,
byte/control boundaries and reverse/status choices. Another 6,144 frames cover
all 256 byte values in both roles with pending foreground state, both reverse
settings and Atari/UTF-8 wire modes. Source-state equations and original TOS
font rows supply expectations. Split/bytewise chains, array operands,
exactly-once RNG, full acquisition before output failure, incomplete/invalid/
local-timeout recovery, loop context, I/R/reset and error-scope restoration
supplement those matrices.

`syncterm_igs_position` adds 360 source-defined wire scenes across every ST
mode, Atari ST/CP437/UTF-8 and status choice. It shares c's capture readout,
with its own p state oracle and pinned source font rows. Coherent checks
include raw colors, pending parameters, cursor/cells, both indexed phases,
palette/graphics state/RGB, and existing W graphics through scrolling.
Non-drawing X10 fences preserve pending console state; captures are already
in logical row order. Partial disconnect/fresh reconnect checks recovery.
Physical audio and MIDI stay disabled. References: 14/66/124/195.

Native FreeBSD /usr/bin/cc Release configure/full builds passed without
warnings. Both focused suites and all 96 selected regressions passed,
including every CTerm test and all 30 IGS application suites. The ledger has
29 completed entries; X and final acceptance remain unfinished.

A performance follow-up retains all pending-state/full-frame and application
cases. It exhausts word conversion once per coordinate role, then checks
every byte class with negative/positive/carry representatives and signed
extremes across the remaining mode/status/g combinations. Complete expected
frames use sparse glyph writes rather than checking every cell against every
pixel. Shared captures serialize pixel chunks and receive into one buffer.
All 40 relevant regressions passed afterward. Native p fell from 58.87 to
17.82 seconds and application p from 290.59 to 213.22 seconds locally; the
same 30 IGS application suites used about 23 percent less summed time.

### m: repeated console movement

`m` acquires selector and count as unscaled signed words before output. It
emits ESC H/A/B/C/D for selector 0/1/2/3/4; upper selectors select home and
negative selectors emit ESC alone. A count at or below zero still executes
once. Positive counts repeat exactly, including home. Movement at a normal
viewport edge stops without scrolling. Pending VT52 input can instead
consume parameters, print a letter, wrap/scroll or leave another escape
pending. ESC-only repetitions preserve their observable odd/even behavior.
The first output failure stops the command with its completed prefix intact.
No new console state, graphics attributes, palette actions, replies, flow
or timed waits are introduced.

`cterm_igs_move` checks 70,404 algebraic cases with one exhaustive selector
sweep, count/origin classes through mode/status/g choices and a bounded set
of maximum-count cases. Another 1,764 complete frames cover the six console
states and bottom-row wrap, reverse video, signed selector classes and one,
two and three repetitions. Expected pixels use the shared source-state model
and original TOS glyphs. Split/bytewise chains, pending ESC followed by
ordinary text, exactly-once RNG, invalid/incomplete input, local timeout,
I/R/reset, loop context and output-error recovery are covered.

`syncterm_igs_move` checks 147 wire-reachable scene inputs in all ST modes,
Atari ST/CP437/UTF-8 and status choices. Coherent captures compare cursor,
pending parameters, colors, cells, both indexed phases, palette/graphics
state and RGB, including a pre-existing W glyph through scrolling. The shared
c/p/m readout compares exact word bytes. X10 fences preserve pending state,
and partial disconnect and fresh reconnect check recovery. Physical audio
and MIDI stay disabled. References: 14/66/124/195.

Native FreeBSD /usr/bin/cc Release configure/full builds passed without
warnings. The test failed before implementation, then all 13 other targeted
checks and the corrected native m CTest rerun passed. The m application
suite took 55.78 seconds and the native suite 8.80. Earlier expected scenes
and references are unchanged. The ledger has 30 complete entries; X and
final acceptance remain unfinished.

### r cursor save and restore

Selector zero and selectors above one emit ESC j. Selector one emits ESC k
and a negative selector emits ESC alone. The shared VT52 parser interprets
these bytes in its current state. In IGS mode, a successful save replaces
the stored position. Restore consumes the saved-position flag, so a later
restore homes the cursor until another save. An out-of-viewport save also
falls back to home. Standalone VT52 restores remain repeatable.

VT52 initializes the flag and native colors through a common reset helper.
Actual color-mode initialization clears the flag while retaining the
stored coordinates, including I5 at the current color resolution.
Same-mode R, monochrome I5 and I0 through I4 preserve the slot. A terminal
reset clears the shared coordinates and a fresh session starts with an
empty slot.

`cterm_igs_cursor_save` checks 672 complete source-defined scenes across
the console parser states, bottom-row wrap, reverse video, saved and
unsaved positions, signed selector classes and mode/status combinations.
It also covers raw VT52 interoperability, input splits and bytewise
delivery, lifetime, output errors and standalone VT52.
`syncterm_igs_cursor_save` checks five literal wire scenes and one
reconnect scene in each of 18 mode/encoding/status profiles. Source glyphs
and full phase comparisons account for literal j and k emitted in pending
states. All 14 targeted checks passed on native FreeBSD Release.

### k cursor and destructive backspace

Selectors zero and one dispatch native ESC f and e. Upper selectors
select f and negative selectors emit ESC alone. Hide increments the
depth, which saturates at the word limit. Forced enable clears the
depth and starts the Atari ST presentation blink phase. Position
changes restart that phase. The presented full-cell cursor complements
live indices in the mode's palette bank and then resolves RGB.
Indexed drawing, cell data and raster capture use the underlying surface.
Injected presentation timestamps share a local epoch during tests.

Selectors two and three enable and disable destructive backspace. An
accepted ordinary serial BS is processed first. With the setting enabled,
the same VT52 parser then receives space and BS. These bytes can use
the raw parameters, attributes and cursor position left by the original
byte. Graphical payloads and semantic text follow their existing paths.
Mode initialization restores hidden depth one. I and R retain the erase
setting, which SyncTERM clears on terminal reset and reconnect.

`cterm_igs_cursor` covers count, scope, partitioned input, pending
parameters, raw VT52, resolution, reset and recovery. `syncterm_igs_cursor`
uses original TOS glyph rows and 180 coherent mode/encoding/status
captures. It verifies both indexed phases, full-cell complement in RGB,
the 499/500 ms boundary, movement, live graphics, erase and reconnect.
All 27 targeted checks passed on native FreeBSD Release.

### v reverse video

Selector one emits VT52 ESC p. Zero and selectors above one emit ESC q.
A negative selector emits ESC alone. The same VT52 state interprets raw
wire bytes and these generated bytes, including any pending parameters.
Reverse swaps the foreground and background of new glyphs. Existing
indexed content and the raw color words retain their values. A new row
after scrolling uses the raw background. Terminal reset and actual
color-mode initialization clear reverse.

`cterm_igs_reverse` checks 336 complete indexed scenes and focused
partition, acquisition, error and lifetime cases. `syncterm_igs_reverse`
checks 72 coherent captures across all three ST modes, Atari ST, CP437
and UTF-8, and both status choices. The captures check literal TOS
glyphs and colors, indexed phases, RGB, retained graphics, scrolling,
and fresh console state after reconnect.
All 18 targeted checks passed on native FreeBSD Release.

### w console wrapping

Selector one emits VT52 ESC v. Zero and selectors above one emit ESC w.
A negative selector emits ESC alone. Pending console parameters and
escapes can consume those bytes. With wrapping off, a glyph at the right
edge leaves the cursor in that cell for overwrite. With wrapping on,
the cursor moves immediately to the next row. A bottom-right glyph and
existing graphics scroll up, and the cursor moves to column zero on the
new row, blanked with the raw background. Actual color-mode
initialization sets wrapping on for low and off for medium.

`cterm_igs_wrap` checks 336 complete indexed scenes, split input,
recovery and lifetime. `syncterm_igs_wrap` checks 90 coherent captures
across all three ST modes, Atari ST, CP437 and UTF-8, and both status
choices. The captures include literal glyphs, both indexed phases, RGB,
immediate wrap, overwrite, scrolling, and reconnect after a partial
command.
All 17 targeted checks passed on native FreeBSD Release.

### l line erasure

Selectors zero and above two emit VT52 ESC l for the whole row. One
emits ESC o for start through cursor and two emits ESC K for cursor
through end. Negative selectors emit ESC alone. The persistent VT52
state interprets these bytes. Native erasure replaces text and graphics
in the inclusive range with the raw background, even in reverse video.
Partial erase retains the cursor position. Whole-line erase uses the
incoming hide depth as its zero-based column, bounded at the right
edge. The hide count is preserved and the local cursor overlay is
redisplayed after the erase.

`cterm_igs_line_erase` checks 336 indexed erase scenes and 156 pending
console compositions, plus partition, recovery, reset and standalone
VT52 cases. `syncterm_igs_line_erase` checks 90 coherent application
captures across all ST modes, Atari ST, CP437 and UTF-8 and both status
choices. It verifies text, graphics, both indexed phases, RGB, visible
cursor inversion and reconnect.
All 20 targeted checks passed on native FreeBSD Release.

### s screen erasure

Selectors zero, one and two emit VT52 ESC E, d and J for whole, prefix
and suffix erasure using the raw background. Three performs the VDI
clear to index zero with the current palette and pending console state.
Four sends ESC E before that clear. Selector five preserves the leading
ESC and both original string operations, including the low-mode
foreground adjustment. With an initially normal parser, q prints
before ESC E and can scroll into the saved page. A pending escape can
change that sequence's effect on reverse video.

`cterm_igs_screen_erase` covers 498 source-derived scenes plus focused
scrollback, partition and recovery cases. `syncterm_igs_screen_erase`
checks 126 coherent captures for text and graphics, indexed phases,
palette, RGB, pending state and reconnect across all ST modes, Atari
ST, CP437 and UTF-8, and both status choices.
All 24 targeted checks passed across two native FreeBSD Release runs.

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
