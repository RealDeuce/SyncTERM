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
`G` adds indexed screen/memory Boolean transfers and retained image views.
`D` draws from the remembered end and copies its requested new endpoint to
both remembered pairs, preserving native Drawto chaining.
X dispatch is in progress: `X0` scatters markers with persistent color cycling.
`X1` changes a raw physical palette register,
and `X12` changes four consecutive physical registers. `X5` configures the
flow mode, custom XON/XOFF bytes and signed repeat counts without emitting
immediately, preserving completed prefix stores and loop overrides. `X3` loads
an opaque right-button macro, sends it once with optional CR and pauses serial
acquisition until all buttons release; retained input and local UI/Wren routing
share the existing owner/event paths. `X4` defines48 retained zone rectangles
and raw payloads, clears rectangle bounds and controls strict loopback for the
future `<` consumer. `X6` configures left-only release CR/CRLF, retaining
right-only handoff into X3 and owner-managed serial/key waits. `X7` loads source
pattern slots for later A4 selection. `X2` configures both random ranges.
`X13` provides bounded signed-word math and console/graphical decimal output.
`X14` selects the persistent screen/memory transfer route used by `G`.
`X8` rotates requested or raw palette colours and saves/restores both forms.
`X15` emits anchored line sweeps with exclusive bounds and local post-line waits.
`X11` wipes or incrementally imports native bitmap bytes into indexed memory 0,
optionally displaying the completed or partial image.
`X10` sets the next Drawto origin without drawing.
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
`b` adds editable effects, saved patch restoration, immediate stop and local
GIST/PSG/console audio with session ownership and muted progression.
`n` adds priority-7 notes, local key/deadline waits and selected/all release or
stop through the same mutable patches and shared PSG.
`N` adds incremental shared-buffer loads, retained MIDI/chip replay and ranges,
local waits, flow and distinct inner/outer key handling through those existing
MIDI and note helpers.
`?` adds immediate version/cursor/pixel/resolution replies and interactive
character-cursor or marker/GEM mouse queries, with indexed pointer cleanup
and retained press/release semantics. `<` adds keyboard/zone collection and
chain-deferred replies. `&` adds stored-operand/gang loops, mixed wire fields
and cooperative local waits. Other commands remain pending.
The [implementation goal](IGS_IMPLEMENTATION.adoc) and
[command ledger](../src/tests/igs/conformance.json) distinguish foundations
from completed commands.

The target is intended IGS 2.21 accessory serial behavior with US TOS 1.04
software graphics in all three ST resolutions. Exact intended pixels are
required. Original bugs and memory/overflow accidents are corrected safely
and will be listed here with tests. Historical delivery profiles, BIG and
CONDOR implementations are outside this goal.

GIST/PSG synthesis, shared console sound and b/n/N session integration are tested.
The existing music worker now supports continuous raw MIDI writes with
retained framing/held voices and bounded software rendering; native
`music_decode` and `music_stream` checks cover this component.
N now integrates that worker and the shared six-byte note helper.
X9 command acceptance passed. `cterm_igs_script` tests the
shared-buffer loader and guarded clear, including every split, bytewise
binary input, discarded lookahead/retained holes, both capacity outcomes,
safe empty-length handling, local receive deadlines, cancellation and N
storage sharing. X9 is enabled in the dispatcher and executes the live
allocation through the existing parser, retaining the real caller's return
boundary and shared loop state. Native checks cover recursive guards,
file-mode flow/text/raw differences, local pause/abort, async waits,
deferred input echo and EOF/reset recovery, plus every wire split across
18 profiles and three scaling settings. `syncterm_igs_script` adds666
coherent application captures across those18 profiles. The shared parser's explicit
`end_text` operation preserves acquired payload and marks `ended_early`;
`graphics_protocol_stream` covers empty/partial text, chain continuation,
normal termination/limits, suspension, rejection and borrowed-view lifetime.
Generic continuation checks cover nested-source return/chain ownership.
Audio implements GIST/PSG behavior; buffered music uses SyncTERM MIDI facilities.
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

Rejected IGS commands are warnings, not session errors. The CTerm adapter
cancels the rejected grammar and its incremental payload work, retains
completed effects, and continues with the unread suffix in the same call.
Ordinary bytes in that suffix reach VT52/text handling; subsequent graphics
introducers remain recognizable. Unsupported X selectors likewise leave
their unrecognized payload to the following input stages. Successful pauses
still retain their unread suffix until resumed. Failed `z`/`f`/`F` drawing
releases serial XOFF with XON unless a reset took ownership of cancellation.
Native X10 tests cover every
split and bytewise recovery into VT52 and another IGS command; the
`syncterm_behavior_session_modes` fixture verifies both directions of the
live connection and the normal notice on subsequent remote EOF.

Verified with the native FreeBSD Release CMake build using `/usr/bin/cc`,
without compiler warnings. All 92 selected CTest checks have passing results
across the regression run and focused followups, including RIP wire fixtures,
all native CTerm IGS tests, shared grammar/images, term forwarding and the
X10/bitmap/music application fixtures. The unsafe sound-edit test explicitly
checks the following `TAIL` text in the same write. The unchanged Ignite
`KM-2ENT.IG` also completed headless playback with physical audio disabled.

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
only that operand, make no store/call/reply and allow normal chaining. Selector
descriptors reuse shared fixed-prefix and dynamic-tail descriptions. T and X
share effect-free arity preview; real random operands advance once at
complete numeric execution or entry to a stateful payload reader.
X0, X1, X2, X7, X8, X10, X11, X12, X13, X14 and X15 are implemented so far. Other valid selectors
log a warning after the selector and pass the remaining stream onward; those
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
callback reset cancels further sends/drawing. `&` remains pending;
its loop state is injected where needed by the helper tests.

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

### d line deletion

The signed count emits one ESC M pair for zero or negative values and
one pair per positive count. Every pair is processed through persistent
VT52 state, including any pending escape or parameter. ESC M moves
the rows below the cursor up within the drawable terminal and blanks
the bottom row with the raw background. Text and both indexed pixel
phases move together. The cursor returns to column zero of the original
row and explicit deletion leaves scrollback intact.

`cterm_igs_delete_lines` checks 433 native scenes including the maximum
count, distinct indexed phases, pending state, and focused input and
history cases. `syncterm_igs_delete_lines` checks 90 coherent captures
across all ST modes, Atari ST, CP437 and UTF-8, and both status choices.
It verifies original glyphs, moved graphics, RGB, cursor overlay and
reconnect.
All 21 targeted checks passed on native FreeBSD Release.

### i reverse index and line insertion

Selector zero and values above one repeat VT52 ESC I, one repeats
ESC L, and negative selectors repeat ESC alone. Counts at or below
zero execute once. Reverse index retains the column while moving up.
At the top it scrolls down and blanks the first row in the raw
background. Line insertion scrolls down from the current row and
moves to column zero. Both indexed phases and cell metadata move
together, while status and scrollback are retained.

`cterm_igs_insert_lines` checks 938 source-derived scenes, including
maximum counts, pending parameters and ESC parity, plus focused
partition and recovery cases. `syncterm_igs_insert_lines` checks 126
coherent captures across all ST modes, Atari ST, CP437 and UTF-8,
and both status choices. It shares the deletion fixture's graphics
recipe and checks indexed movement, literal glyphs, RGB, cursor
overlay and reconnect.
All 22 targeted checks passed on native FreeBSD Release.

### t timed pause

The signed unscaled seconds word is capped above 30, converted to
its unsigned 16-bit duration, and translated to a local monotonic
deadline. Zero completes on the first poll. Negative words become
finite long waits under the host-duration policy. A local key cancels
the pause and is consumed. Entry and exit independently test the
current loop setting and call the existing flow helpers when eligible.
Reset and EOF release flow and discard the suspended continuation.

`cterm_igs_pause` checks 252 duration contexts plus partitions,
keyboard paths, flow mutation, clock errors, overflow and lifecycle.
`syncterm_igs_pause` checks 74 coherent captures across all ST modes,
Atari ST, CP437 and UTF-8, and both status choices. It verifies the
flow bytes, unchanged scene, key consumption, retained suffix,
one live deadline, and reconnect after a paused disconnect.
All 18 selected checks passed on native FreeBSD Release.

### q sync wait and double-copy control

The signed count is clamped to 1..180 and contributes one extra sync
unit from the terminating iteration. Values above 9994 instead set
the retained double-copy control to 9999 minus the word and wait two
units. Ordinary values preserve that control. The total count is
converted to a local deadline using the configured sync rate, initially
60 units per second. Keys pass through during q and the input suffix
resumes at expiry.

`cterm_igs_sync_wait` checks 66,346 duration cases plus partition,
keyboard, control-lifetime and error cases. `syncterm_igs_sync_wait`
checks 145 coherent captures across all ST modes, Atari ST, CP437 and
UTF-8, and both status choices. It verifies literal scene retention,
control values, a live wait, key forwarding, no replies and reconnect.
G's double-copy use remains pending with G.
All 17 selected checks passed on native FreeBSD Release.

### X1 raw physical palette register

Two signed unscaled operands select the index and color word. Indices
above 15 clamp to 15; negative indices alias through their low four bits.
Negative colors query without changing state or sending a reply. Positive
words and zero store all raw bits; original-ST RGB uses only bits 10..8,
6..4 and 2..0. X1 preserves the separate VDI requested-RGB and desktop
palette caches. In monochrome, only register zero bit zero changes the
complementary display colors. Other raw registers remain independent.
No pixels, drawing attributes, console state, waits or flow bytes change.

`cterm_igs_raw_palette` checks 197,994 word cases including exhaustive
signed index and color sweeps. It also covers all focused partitions,
random evaluation, pending console input, invalid/partial acquisition,
EOF, timeout, initialization/reset and palette-preserving R lifetime.
`syncterm_igs_raw_palette` checks 126 coherent captures across all ST
modes, encodings and status choices, using literal RGB and pinned glyph
rows to verify recoloring of retained indexed pixels, mono polarity,
ignored register writes, unscaled operands, no reply and reconnect.
Physical audio and MIDI are disabled. References: 20/28/100/124/165.
All 17 selected checks passed on native FreeBSD Release; the final full
build completed without warnings. Full X dispatch and other selectors
remain pending.

### X12 raw physical palette groups

An unscaled signed group is capped only above three. Its signed word
base and upper bound cause groups -8193 and -24577 to read no colors.
Every other group consumes four unscaled signed colors, using the same
raw register service as X1 in ascending index order. Negative groups
alias to physical registers; negative colors remain query-only. Raw
unused bits, requested-cache independence and mono polarity follow X1.

Dynamic selector payloads share the descriptor callback interface.
Private numeric-state previews include selector evaluation; random
operands commit once after complete acquisition. Incomplete, malformed
or timed-out headers publish no partial palette writes, following the
adapter's header policy. The original writes between color reads.
No waits, flow bytes, drawing or console effects are added.

`cterm_igs_palette_group` checks 67,048 group cases, including every
signed group, all modes/status/scaling states and mixed query/write
colors, plus input partitions, random evaluation, state retention,
recovery and I/R/I5/reset lifetime. `syncterm_igs_palette_group` checks
126 coherent captures across all ST modes, encodings and status choices,
using literal RGB and pinned glyph rows to verify group order, upper
cap, retained indexed pixels, mono polarity, no reply and reconnect.
Audio and MIDI are disabled. References: 19/22/28/100/124/165.
All 16 selected checks passed on native FreeBSD Release; full CMake
builds completed without warnings. Full X dispatch and other selectors remain pending.

### X7 persistent pattern rows

X7 reads an unscaled slot and sixteen shared-reader strings. It uppercases
x in the first sixteen shared bytes and assembles X bits from column zero
at bit 15. Scanning continues past NUL into the retained suffix. Each
completed row updates the source slot before the next row starts. A4 copies
a source slot into the active user pattern independently; X7 performs no
drawing or implicit selection.

W and X7 share opaque text acquisition, retained-window copying, reuse,
local per-string deadlines and loop-@ XON/count handling. Z reuse applies
to one row. Selector/slot random operands commit once before row input.
Invalid slots outside 0..7 fail safely without native aliases or memory
escapes. Timeout/EOF discard only the incomplete current string and retain
completed rows and the prior completed shared window. Reset clears pending
input; normal I/R pattern lifetime is preserved.

`cterm_igs_pattern_load` checks 144 full loads across slots/modes/status/
scaling, 253 byte windows, literal stale-suffix/NUL/128-byte/control rows,
uppercase edits, input partitions, random evaluation, completed-row retention,
invalid-slot/error recovery, loop reuse, XON and reset during output.
`syncterm_igs_pattern_load` checks 108 coherent captures across all ST modes,
encodings and status choices, including exact Z use of selected masks,
source/active copy independence, glyphs, indexed phases, palette, cursor,
RGB, no replies and partial disconnect/reconnect. Physical audio and MIDI
are disabled. References: 11/20/124/130/131. All 15 selected checks and
the final focused clock-error check passed on native FreeBSD Release;
full CMake builds completed without warnings. Full containing-loop command
integration and remaining X selectors stay pending.

### X2 random-function ranges

Two unscaled signed operands update r.lower and conditionally r.upper.
The first equality check precedes clamping a negative lower to zero; the
second follows it and decides whether to acquire R.upper after copying
R.lower. Later random operands see those ordered updates. Both inverted
pairs are repaired independently, including R when it was not selected.
The maximum lower 32767 is repaired to a valid singleton, correcting the
original negative wrap. I/R preserve ranges; full reset/reconnect restore
both pairs to 0..199.

Private arity preview and complete-header execution share the ordered
updates. Incomplete or rejected headers publish neither range changes
nor random advancement, following the adapter header policy instead of
native partial stores. Sampling allows one full relevant generator period
(2^16 byte or 2^24 signed-word calls), so valid narrow ranges are not
falsely rejected by the former 65,536-call limit. Impossible intervals
fail promptly. No drawing, console, flow or wait effects are added.

`cterm_igs_random_range` checks 75,238 cases, input partitions, state
retention, ordered random acquisition, independent repairs and lifecycle.
A literal singleton case needs 89,478 calls and checks the final seed.
`syncterm_igs_random_range` checks 126 coherent captures across all ST
modes, encodings and status choices, with exact range state, retained
scene, unscaled arguments, no reply and reconnect. Audio and MIDI are off.
References: 20/124 and IG221.TXT. All 17 selected checks and the final
focused R-retention check passed on native FreeBSD Release; full CMake
builds completed without warnings. Full X and loop integration remain pending.

### X0 marker scatter and color cycling

The X/Y base and extents use coordinate scaling; count is unscaled and
signed. Each marker samples independent Random low bytes, resampling only
rejected coordinates. The ninth pass still samples rejects before forcing
both coordinates to zero. Large extents retain byte-sized distribution;
negative extents reject all samples. This uses the common RNG source,
independently of configured r/R ranges, with no waits or flow output.

Zero extents in color modes set both cycle words from scaled base X and
bypass restoration on the serial path. Mono takes ordinary scatter.
Nonzero cycles advance through the mode's color limit before each marker,
then select a validated native marker color without changing C's cache.
Ordinary scatter restores only a nonzero cached pen, including for a
nonpositive count. C/I/R/I5 retain cycle words; reset/reconnect clear them.
P and X0 share raster policy, with P alone storing its Drawto endpoint.
Host primitive errors stop the loop and restore a nonzero cache while
retaining completed pixels, RNG and cycle changes.

`cterm_igs_scatter` checks 1,152 dual-phase raster cases from literal points,
seeds and marker masks, plus every signed setup word, cycles/cache rules,
mode and argument boundaries, g1/g2, partitions, random evaluation,
EOF/timeout/error recovery and lifecycle. `syncterm_igs_scatter` checks
126 coherent captures across all modes, encodings and status choices,
including consecutive RNG calls, all shapes/modes, exact indexed pixels
and RGB, XOR over a glyph, cycling, cache-zero retention, normalized
coordinates, unchanged Drawto pairs, no replies and reconnect. Audio and
MIDI are off. References: 20/43/44/52/124/129/136 and IG221.TXT.
All 20 selected checks passed on native FreeBSD Release; the final full
CMake build completed without warnings. Full X and containing-loop command integration remain pending.

### X13 signed-word math and decimal output

`cterm_igs_math` and `syncterm_igs_math` cover operations 0..7 with typed
unscaled arithmetic fields and X/Y graphical coordinates. Operations 0..5
store only after complete acquisition: assignment, add, subtract, multiply,
signed quotient and signed remainder. Word results wrap explicitly;
quotient truncates toward zero and remainder follows the dividend's sign.
Division by zero and array access outside 0..127 report OUT_OF_RANGE without
a destination store. Native memory aliases and CPU exceptions are excluded.
The -32768/-1 quotient is -32768; its remainder is mathematical zero,
correcting the original overflow-register accident.

Operation 6 clears inclusive start..last, caps a positive start to 127 and
an exclusive end to 128, rejects negative starts, and leaves inverted or
negative-ended ranges alone. The host-domain endpoint increment corrects
the original last=32767 wrap. Unsupported operations consume only their
operation; unsupported print kinds consume kind/index without accessing the
array or coordinates. All retain normal chaining.

Operation 7 kind 0 sends signed decimal then CRLF to the existing VT52
console renderer, composing with pending escapes. It leaves W's window
alone and sends no reply. This represents the accessory's normal local
stdout profile, without foreign FILE internals or redirected process output.
A digit-output failure stops before the newline. Kind 1 formats decimal/NUL
into the shared W/X7 window before X/Y acquisition, preserving the old tail.
The evaluated prefix and RNG advancement are retained once across splits.
Only complete coordinates draw with current graphical text attributes;
Drawto and console state stay unchanged. No W reader/count/XON/text deadline
is entered. EOF, numeric timeout and malformed coordinates retain formatting
without drawing; reset/reconnect clear the transient prefix, window and array.
I/R preserve math. Loop-context operand acceptance remains pending.

Native coverage includes every signed word for all six arithmetic operations,
every signed destination index, mode/status/scaling cases, clear boundaries,
console wrap/scroll and pending escapes, literal graphical glyphs, W/X7 reuse,
focused partitions/bytewise input, exact prefix/coordinate RNG seeds, clock
failure and cancellation/lifecycle recovery. Application coverage has 144
coherent scenes across all modes, Atari ST/CP437/UTF-8 and status choices.
Pinned independent glyph/effect oracles and literal decimal results check
cells, both indexed phases, palette and RGB, rotations/effects, normalized
coordinates, Drawto retention, no replies and partial disconnect/reconnect.
Audio/MIDI are disabled; no device/audio coverage is claimed.

### G indexed raster transfers

`cterm_igs_grab`, `igs_image` and `syncterm_igs_grab` cover G0 screen copies,
G1 capture, G2 replay, G3 explicit memory pieces and G4 memory self-copies.
Two unscaled fields precede typed X/Y coordinates; dynamic arity shares
existing descriptor data. Header effects publish after complete acquisition.
G uses actual resolution limits while g retains its own coordinate snapshot.

Both memory banks retain their capture coordinates and full decoded native
capacity. G rebuilds their separate width/height/word-stride/plane views on
every call. Capture coordinates are not rebased and descriptive width/height
do not clip addresses inside the allocation. G0 leaves its selected memory
descriptor as a screen alias until the next G; G1 routes 4/5 update capture
coordinates despite making no raster call. Native tests cover the complete
operation/route matrix. X14 wire routing and its application scenes are
covered in the completed selector entry below.

All sixteen Boolean operations use physical indices in both host blink
phases. Logic above 15 selects replace; negative logic leaves pixels alone.
Screen destinations use workstation clipping. A two-group indexed source
cache preserves shifted overlap and row aliases; there is no per-blit
native-plane conversion or whole-source snapshot. Source/destination bank
ordering is memory0, memory1, logical screen, representing ordinary accessory
allocation without a foreign pointer/memory model. G preserves Drawto,
last-line ownership, graphics attributes, palette and console state.

Captured memory can retain an alternate host blink phase. Native imports
update both phases; native exports select primary. Existing R format-edge
conversion now preserves both phases transactionally. I preserves buffers
and capture coordinates; reset/reconnect clear both and their transient views.
Native images continue to retain 32000 bytes of decoded capacity and padding.

Only screen copies and capture/replay/piece routes 0/1 support q's second
copy. Nonzero controls <=1 repeat immediately; larger controls wait control-1
local sync units through the existing pause/poll path, then copy current
pixels before releasing the input chain. G4 and routes >=2 do not repeat.
EOF/reset cancel pending copies; backend/clock errors retain completed
copies and cancel pending ownership. G adds no flow-control output or
old-hardware throughput delay.

Safe deviations reject operations above 4 and G2 routes 2/3, which otherwise
use unread native frame words; reject nonpositive active strides, unequal
unclipped extents and out-of-capacity accesses; use wide derived extents
instead of accidental word overflow; initialize new images to zero; and
protect the physical host canvas/status boundary even with clipping off.
Source clipping preserves displacement. Foreign RGB/palette values in
active native regions fail safely. Foreign MFDB/inquiry scratch/reserved
words, stdio/process pointer aliases and CPU faults are not modeled.

Native tests independently check 12,096 complete dual-phase screen scenes
and 480 source-derived native-model memory scenes, including shifted/self
copies and aliased rows. They also check routes, unrebased captures/views,
repeat timing and fresh reads, parser partitions/bytewise input, scaling,
status/clipping, malformed/partial/EOF/timeout, errors and I/R/reset lifetime.
The application fixture combines all sixteen logic functions with literal
font masks, capture/replay, hidden memory edits, delayed repeats, normalized
coordinates, status edges and partial disconnect/reconnect, with audio/MIDI
disabled. Each frame checks cells, indices in both phases, palette/RGB,
cursor, Drawto and memory-view/pending-copy state together.

### X14 persistent G routing

`cterm_igs_routing` and `syncterm_igs_routing` cover one unscaled signed
route field: values above 5 select zero, negative values pass unchanged.
The fixed shared descriptor/evaluator commits a complete selector/route
header once, including array/random operands. X14 itself does not touch
pixels, images, remembered capture/view metadata, palette, graphics state,
Drawto, console parsing/cursor or timing, and sends no reply.

Subsequent G commands honor the selected route. G1 and G3 have opposite
cross-bank directions for routes 2/3; G4 self-copies only routes 0/1; G0
copies only routes 0/1. G1 routes 4/5 remember coordinates without copying,
whereas G3 routes 4/5 capture screen pieces. Negative routes disable copies.
G preserves the route. I0/I2 restore zero; I1/I3/I4/I5 and R retain it;
full reset/reconnect restore zero. A setter queued after a delayed repeated
G executes only when that G completes. Incomplete/error headers preserve
the last completed route under the existing adapter cancellation policy;
containing-loop operand support remains pending with those entries.

Native tests cover 65,734 route-value cases, all signed words and mode/
status/scaling representatives, unchanged screen/image/state snapshots,
focused split and bytewise chains, array/random operands with exact seeds,
clock independence, no replies, malformed/partial/EOF/timeout and I/R/reset
lifetime. Explicit G1 routes 4/5 checks verify metadata changes without
pixels, and G2 checks verify capture using retained extents. Application
coverage is 162 coherent scenes across all modes,
Atari ST/CP437/UTF-8 and status choices. Independent pinned A/b/c masks
expose both bank directions, selected memory inversion, disabled routes,
negative/upper values and fresh defaults after partial disconnect/reconnect.
Cells, both index phases, palette/RGB, cursor, Drawto and route/view metadata
are checked in the same frame, with audio/MIDI disabled.

### X11 native bitmap wipe and serial load

X11 acquires unscaled operation/section words, caps sections above 8 at 8,
and leaves negative sections empty. Operation 0 consumes a fill word and
writes its low byte over all 32000 bytes (section 0) or a 4000-byte band
(sections 1..8). The original fill 256 draws one raw Random byte per position,
including when numeric r/R first evaluates to 256. Wipe does not display.
Operations 1/2 accept exactly the selected raw byte count, with no terminator
or discarded byte; imported prefixes immediately update indexed memory 0.
Only operation 1 displays, once, with replace logic 3. X14 routing, q repeats,
g scaling, palette, Drawto and memory 1 are unaffected.

Every completed operation, including unsupported operations and negative or
partial loads, refreshes memory 0's remembered rectangle and device-form
view using actual resolution: full screen or 25/50-line bands, descriptor
width/height without an added pixel, word width 20/40 and 4/2/1 planes.
Native bytes decode at the image boundary into the existing indexed storage;
there is no planar framebuffer or per-blit format conversion.

The shared parser now offers a synchronous incremental BYTES observer and
an explicit owner request to end that field early. Field offsets, borrowed
views, accepted-byte/error ownership and unread suffixes remain generic.
Raw bytes also remain in the bounded final argument view. Normal parser EOF
still rejects incomplete fields. X11 alone chooses to complete an entered
raw field at source EOF, retaining its imported prefix and old remainder.
Header previews do not publish RNG state; entered raw headers evaluate once.

Each receive uses a fresh 5000-ms local deadline. Expiry stores ASCII 42,
emits BEL through the existing VT52 console parser and increments the
accumulated miss count; successful reads retain that count. The third miss
stores its marker before ending the field and displaying for operation 1.
A byte presented after expiry is consumed but replaced by the marker.
Idle polling supplies a marker without consuming later unobserved input.
Completed/aborted operations 1/2 clear errors, even with a negative section;
wipes and unsupported operations retain them. No flow helper or keyboard
abort is involved. Full reset cancels without display and drops images.

Intentional corrections: use local deadlines instead of foreign absolute
clock buckets, saturate misses at 3 instead of emulating accidental byte wrap,
keep negative geometry in safe host arithmetic instead of foreign word
overflow, and retain the old suffix at explicit raw EOF instead of reading
uninitialized script stack data. Resolution comes from stable owned host
state rather than repeated hardware inquiries that could disagree. Raw host
errors preserve imported prefixes and cancel parser ownership; a failed
poll clock leaves the load retryable. Random wipes commit their seed only
for successfully imported chunks. Containing script/loop execution remains
part of its own unfinished entries.

Native tests cover 312 section/fill combinations across all resolutions,
independent big-endian word-to-index expectations for both retained phases,
full/band/negative loads, immediate partial import and deferred display,
status clipping, exact random seeds/bytes, bank 1 isolation, every focused
header split and bytewise payload, word/band/end boundaries, opaque controls,
chaining, incomplete/invalid/EOF/timeout recovery, accumulated/interleaved
misses, late-byte replacement, pending VT52 parameters, clock/import/display
failures, reset and a resolution change during loading.

The application fixture checks 191 coherent captures over all ST resolutions,
Atari ST/CP437/UTF-8 and status choices, plus a binary Telnet session.
Independent native-byte and pinned glyph expectations check full indexed
phases, cells, palette/RGB, cursor, route/descriptor state, retained bank 1,
random wipes, raw command-looking data, a real receive timeout and fresh
reconnect after a partial load. Audio/MIDI are disabled. Tests do not depend
on external ROMs or the reference tree.

References: chapter 22's X11 receiver/descriptor contract, chapters 18/19 and
IG221.TXT's X11 entry. `graphics_protocol_stream`, `cterm_igs_bitmap` and
`syncterm_igs_bitmap` cover this step.

### X8 palette rotation and snapshots

X8 reads four unscaled signed words: first, last, times and delay. If either
endpoint exceeds 15, subtract 15 from both as signed words and select raw
registers for this command and the persistent restore choice. Otherwise use
requested VDI colours without clearing that restore choice. Endpoints outside
0..15 then become 15, including negative endpoints. The special 1,1,1,1
request, tested before clamps and after decoding, captures all 16 requested
triples and raw query words. Encoded 16,16,1,1 captures both too. There is no
rotation wait when the endpoints are equal.

For distinct endpoints and positive times, retain the reference's times+1
passes. Query the last colour, then each adjacent source before its local
sync wait; apply the destination write afterward. Include the extra endpoint
query/write and final replacement from the saved colour, yielding
abs(first-last)+2 waits per pass. Raw queries mask to 0x777 and wrap physical
indices to four bits, including the extra -1/16 query; requested queries
and writes use the logical pen namespace and existing VDI quantization.
Pixels, graphical pens, Drawto, image banks and console parsing do not move.
Palette changes recolour existing indexed pixels through the normal backend.

After every pass, convert the unsigned delay word to local milliseconds at
200 units/second. Zero delay completes without a timer. A local key is
retained through the current rotation's sync waits, shortens the post-pass
delay, and ends the command after that pass. Consume the first cancellation
key without sending it remotely. Queued graphics/text bytes remain unread
until completion; no XOFF/XON helper is called. Negative times perform no
rotation, while times 0 restores all 16 entries from the selected snapshot,
ignoring endpoints and delay, then clears the raw restore choice. Raw
restore leaves the requested cache unchanged. A later ordinary restore
therefore selects the requested snapshot. Snapshot storage begins at zero;
I/R preserve it and the restore choice; full reset/reconnect clear both.
EOF or a host error cancels unfinished rotation, retaining completed writes
and snapshots; a failed poll-clock read leaves the wait retryable.

Port corrections: use local deadlines rather than low-word foreign-clock
wrap, count passes in the host domain so times 32767 terminates after 32768
passes and cancellation also works at 32766/32767. Preserve native invalid
requested-query retention within the command, starting that output triple
at zero rather than using unspecified shared VDI scratch from another
command. Never read an out-of-range host pen or invent a clamped query.
The serial snapshot namespace is sufficient for the selected accessory
serial target; containing nested-script/loop execution is tracked by its
unfinished commands. No CPU cycles or foreign interrupt schedule are modeled.

The native palette_rotation test uses an independent integer palette model
for 2022 range/mode/delay cases: canonical endpoints in both directions and
namespaces, mixed/negative/encoded endpoints, all ST resolutions, 50/60/1000-Hz
local sync conversions and zero/positive/unsigned-negative delays. It checks
every intermediate palette write, actual backend palette and unchanged full
indexed phases, including retained off-screen data and status clipping.
Further checks cover both snapshot representations, restore before capture,
sticky raw selection, requested-cache preservation, restore after a mode
change, I/R/reset/EOF lifetime, four key-entry APIs, held queries across a
local palette change, maximum-count completion/cancellation, once-only
random selector/operands, every focused split and bytewise chain, partial/
invalid/timeout recovery and clock/rate/deadline-overflow failures.

The application palette_rotation fixture uses independent palette algebra
and pinned A glyphs for 288 coherent captures across all ST modes, Atari
ST/CP437/UTF-8 and status choices. Each capture checks cells, full indexed
phases, palette/RGB, requested/raw colour values, cursor and rotation/restore
state together. It tests completed rotations and restores, a long post-pass
wait with queued commands, keyboard cancellation without a remote key,
disconnect while active and fresh zero-snapshot restore after reconnect.
Audio and MIDI are disabled; tests require no external ROM or reference tree.
Test-control capture capacity grows from 128 to 256 fields to accommodate
these observations together with existing terminal/transfer state; palette
values are grouped into two text fields.

References: chapter 22's addressed X8 rotation/receiver contract, chapter 28's
US TOS 1.04 palette query/set rules, and IG221.TXT's X8 entry.

`cterm_igs_palette_rotation` and `syncterm_igs_palette_rotation` cover this
step; the X dispatcher and complete IGS acceptance remain in progress.

### X15 anchored line sweeps

X15 reads nine unscaled words: anchor X/Y, initial endpoint X/Y, axis,
starting scalar, add selector, step and wait. The shared fixed-prefix table
now supplies all nine NUMBER descriptions; no new grammar is introduced.
Normalize step below 1 to 1 and wait to 0..60. Initialize both remembered
coordinate pairs from the four supplied coordinates before checking axis
or bounds. Axis 0 moves X and axis 1 moves Y; any other signed word does no
rendering or waiting and retains the initialized coordinates. A nonzero
add word, including a negative word, selects increasing motion.

For an entered sweep, test the scalar before each draw: increasing requires
scalar<the initial endpoint coordinate, decreasing requires scalar>it.
The bound is excluded. Emit one separate two-point polyline from the fixed
anchor to each scalar, updating the shared original endpoint before the
call. Reuse the existing renderer's patterns, writing modes, line widths,
caps, arrows, clipping, blink phases and per-primitive pattern reset. The
renderer works on private points, so arrow shortening does not change the
remembered coordinates. For start 2, bound 8 and step 3 the emitted endpoints
are 2 and 5; following D starts at 5. Empty sweeps retain the supplied endpoint.
No palette, image, attribute, flow-helper or console-parser change is added.

A nonzero wait follows every emitted line, including the last, and uses
exactly that many local sync-wait units; q's additional unit does not apply.
There is no initial wait or wait for an empty sweep. Keep evaluated operands
in owned sweep state while the common parser is suspended. Resume at the
next scalar only after the deadline, leaving following wire bytes unread.
Keys follow their normal send path and do not abort or shorten this wait.
Wait 0 draws the whole finite sweep without reading the IGS deadline clock.
EOF/error cancellation retains already drawn pixels and the last published
original coordinate pair; full reset/reconnect cancel and restore initial
session state. A failed poll-clock read leaves an active wait retryable.

Correct the original signed-word scalar wrap by calculating the next scalar
in a wider host integer. Stop at the first mathematical bound crossing,
without drawing an out-of-range wrapped coordinate or imposing an arbitrary
iteration limit. This fixes both nonterminating word cycles and extra
wraparound passes, while preserving ordinary sweep order. At most 65535
lines are possible for signed-word endpoints and minimum step 1. Host
renderer/scheduling errors retain the endpoint published before that call,
consistent with X15's source order; they do not fabricate a successful sweep.
Containing-loop operand integration remains tracked by the unfinished loop
commands.

The maintainer-only generate_sweep_vectors.py composes separate primitives
from the independent source-derived rational/bit/circle/arrow oracles,
carrying explicitly seeded framebuffer words and last-line state between
calls. It pins six oracle hashes and the US TOS 1.04 ROM identity. The 600
native cases cover both directions/axes, all resolutions and writing modes,
seven thin styles, wide shafts, caps/arrows, clipping, XOR crossings and
inherited last-line state; both initial zero/all-one phase hashes cover
all 32000 bytes. Wide cases use valid solid-style T requests; the test
asserts selected width/style before checking pixels. Ordinary tests require
only the checked-in header and JSON, without the reference tree or ROM.

Further native tests cover literal overlapping/degenerate lines and D,
status boundaries, every unsupported signed axis word, exact wait counts
at 50/60/1000-Hz local conversions, normalized steps/waits, exclusive/equal/
wrong-direction bounds, full signed-word spans and formerly nonterminating
steps. Partitioned/bytewise chains and random selector/start operands check
once-only evaluation and unread suffixes. Partial/invalid/EOF/timeout,
zero-wait clock independence, clock/rate/deadline failure, failed render,
reset and key forwarding have focused tests.

The application fixture checks 451 coherent captures across all ST modes,
Atari ST/CP437/UTF-8 and status choices with audio/MIDI disabled. It combines
36 source-derived sweep scenes with literal console glyphs, ordered XOR/
patterned overlays, D from the retained endpoint, later console overwrite,
empty/unsupported sweeps, a real local wait with queued commands and a
forwarded key, active disconnect and fresh reconnect. One capture checks
all drawable indexed pixels in both phases, cells, palette/RGB, cursor,
original coordinate pairs, line attributes and parser/wait/image-route state.

References: chapter 19's addressed X15 state machine and IG221.TXT's X15
entry, plus the existing line/polyline raster contracts in chapters 43/44/
46/50/51. X and final acceptance remain in progress.

`cterm_igs_sweep` and `syncterm_igs_sweep` cover this step; vector provenance
is recorded in `src/tests/igs/sweep-vectors.json`.

### GIST software-voice foundation

The sound foundation includes a tested GIST software-voice engine in
st_igs. It owns three copied 56-word parameter records, their six envelope/
modulation accumulators and a masked PSG-register mirror. Original 20-patch,
85-pitch and 16-factor tables are checked in with source fingerprints. The
identical effect 3/11 contents occupy distinct source rows; later mutable
patch banks must preserve that independence. Initialization creates a silent
host state with mixer 0x3f and zero gain registers.

The engine implements automatic voice allocation and priority rejection,
stop-before-copy, pitch folding from the original table, volume override,
per-voice tone/noise enables and mixer preservation. Stop emits immediate
zero gain; release schedules the duration/release transition and leaves the
accumulators intact. Accepted source parameters are copied forward, so
later patch edits do not silently alter an already active voice. A valid
self-alias observes stop before the duration read. Duration 0 leaves the
selected voice stopped. Oscillator phases are not owned or reset here.

A caller invokes one update at each converted local 1/200-second boundary.
There is no clock, timer, origin or free-running foreign tick counter in
this engine. Updates visit C,B,A and apply the original signed fixed-point
amplitude/tone/noise envelopes, reflected modulators, tone step replacement,
duration/release order and raw gain/tone/noise conversions. Preserve every
ordered raw PSG write through a synchronous observer, including immediate
zero followed by another gain and shared-noise overwrites. The register
mirror applies the chip masks separately. No PSG GPIO or machine-bus behavior is
introduced. The observer must not mutate or reenter the engine.

Safe-port policy: retain an edited volume word but bound its dynamic
multiplier-table index to 0..15. Do not read outside that host table to
reproduce a native escaped factor lookup. Preserve otherwise defined raw
arithmetic, including negative gain bytes that set the hardware-envelope
bit and the signed-byte noise guard. Other native RAM/code/MMIO escapes
remain outside this component, under the goal's memory-safety policy.

The standalone igs_gist CTest uses 1044 independent immutable-field update
vectors, 160 full API vectors and explicit register-order/priority/release
checks. Vectors include all original patches, raw edited words, recognized
and unrecognized phase bytes, carry/clamp/replacement boundaries and exact
masked register state. Literal checks cover all 65536 pitch/control words,
copy/source independence, self-alias stop ordering, free-record priority
rejection, C/B/A noise precedence, fixed versus dynamic gain, safe factor
bounds, invalid voices and stop-all order. All 20 original automatic effects
reach completion under bounded test observation. No elapsed host waits or
PCM matching supply expected results; ordinary tests require no external
reference tree or ROM.

These GIST record/register tests alone do not establish waveform synthesis
or session/audio lifecycle behavior; later foundation steps below track
those responsibilities. No command status is advanced. These register/record
tests claim no PCM, audible or physical audio-device coverage. References:
chapters 06, 56, 57 and the b caller contract in chapter 24. Source identities
and test counts are recorded in src/tests/igs/gist-vectors.json.

## Deliberate corrections

The reusable byte grammar reports a bounded failure and cancels its pending command on
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
X13 now implements array math; loop-context operands remain pending.
The CTerm adapter recovers from incomplete source EOF by discarding partial
grammar and logging a diagnostic. Incomplete header input has a five-second
local timeout after progress; expiry discards the header. An ordinary held G
prefix alone has no header timeout. No Atari bucket/wrap arithmetic is used.


### Live PCM source foundation

The audio mixer now accepts a continuous source on an explicitly owned stream.
Its existing consumer requests a bounded prefix of stereo PCM at the normal
44100-frame/second output format, then applies the existing stream gain,
clipping and device-acknowledgment path. A short return deactivates the source;
an invalid oversized return stops that stream with an error. Source buffers
are consumed directly by that pull rather than queued ahead by another thread.
This lets later PSG register changes take effect at the next consumed samples.

Attach/replacement discards pending frames and leaves the source inactive.
Activation wakes the existing consumer; clear pauses the source and cancels
pending acknowledgments while retaining its attachment. An active source has
unbounded buffered length and drain reports BUSY, like an indefinite loop.
Queued append is BUSY while a source is attached. Detach, destruction and
engine stop wait for an in-flight callback to finish. The caller owns the
context, must keep it alive until detach/destruction, and must release any
callback-needed lock before calling an audio API. The callback runs under
mixer/stream locks and must return promptly without calling audio APIs.
No new worker, foreign clock or timer is introduced.

The st_audio_source test checks finite-prefix sample delivery through the
existing mix path, chunk boundaries, idle/active transitions, invalid returns,
acknowledgment and clear/replacement behavior, and data-ready wakeup. Predicate-
gated concurrent tests prove detach, destruction and stop cannot return while
a callback still uses its context. st_audio checks the public wrappers with
literal expected PCM through the device-free pull runtime and verifies clear
and stopped-runtime behavior. These tests cover the audio infrastructure; PSG synthesis and IGS session
responsibilities are tracked separately below.

Native FreeBSD Release CMake configure and full build passed; the final build
had no warnings. All ten selected CTest checks passed: st_audio_source,
st_audio_mixer, st_audio, st_audio_synthesis, music_player, igs_gist,
cterm_apc_audio, cterm_skypix_audio, cterm and term. No command count advances
and no physical audio-device or audible coverage is claimed. The GIST engine
component is committed as 4e5e0a0; the sound foundation remains in progress
with 51/63 command entries complete.


### PSG synthesis foundation

st_igs now contains a device-free PSG renderer for SyncTERM's native signed
16-bit stereo at 44100 frames/second. It carries three tone phases and the
shared noise/envelope phases as exact fractions of one local output frame.
Period conversions use the nominal tone/envelope and noise frequency
relationships at the write/use sites. No clock origin, foreign tick loop,
CPU timestamp or bus schedule exists. Render calls advance generators even
when their output is gated or their gain is zero.

Masked registers preserve raw zero words, treating period zero as one at use.
Ordinary writes preserve phase, output bits and noise state. Each R13 write,
even an unchanged shape, restarts the shared envelope alone. Separate low/high
period writes immediately update the currently composed period. If a shorter
period lies behind carried elapsed phase, one overdue edge resolves at the
next local output sample; this is an explicit sample-boundary approximation,
not a claim of native sub-sample bus timing. GPIO registers store bytes only.

The renderer implements the shared 17-bit old-bit-output Galois noise sequence,
all 16 envelope shapes with repeated endpoints, fixed-to-5-bit level mapping
and the tone/noise gates. Both inputs disabled leave a constant level, allowing
software gain changes and DC filtering. Muting, stopping a GIST voice or writing
a period does not reseed noise or reset another oscillator.

Waveform edges are integrated over each local output frame, using the pinned
single-channel logarithmic amplitudes. Their sum is normalized to the signed
16-bit range, followed by a carried mild low-pass and DC blocker. Mono output
is duplicated to stereo. Mix/filter arithmetic and source fingerprints are
explicit in src/tests/igs/psg-synthesis.json. These are host synthesis choices;
the separate canonical nonlinear three-channel/48kHz reference PCM profile
is not a byte-identity target. The goal does not require reference waveform
comparison or manual listening as a completion gate.

The standalone igs_psg CTest checks 4096 masked register writes, ignored
addresses, 12288 tone-period/phase profiles, every 16-bit envelope-period word,
all noise periods and zero/one equivalence. Literal D/U/L/H blocks check all
16 envelope shapes at 2400 local sample positions each; 32768 gate/level
profiles, 32 independently calculated DC amplitude probes and a hand-derived
shared-noise trace cover output selection. Functional checks cover each tone's
1000Hz output, settled DC rejection, filter tail after stop, unchanged-shape
retrigger, reduced-period carry and exact partition invariance at fixed local
register-write positions. All 20 original GIST patches produce PCM with
updates translated to alternating 221/220 local-frame intervals; stop retains
phase and the filter tail settles. Ordinary tests need no external tree,
ROM, sound device, reference PCM capture or host-time waits.

Native FreeBSD Release CMake configure and full build with /usr/bin/cc passed;
the final full build had no warnings. All 14 selected CTest checks passed in
13.41 seconds: igs_psg, igs_gist, igs_image, shared parser/stream, audio source,
mixer, runtime and synthesis, music player, APC/SkyPix audio, CTerm and term.
igs_psg takes about 0.1 second. The live-source component is committed as
2ed84d7. The command ledger at that component boundary was 51/63. Session ownership, disabled-audio
progression, console interaction and b integration are completed below;
n is completed below; N/MIDI integration remains pending. No auditory or physical
audio-device coverage is claimed. References: chapters 53/54/56/57.


### b: editable effects and session audio

The b command is complete: 52/63 command entries are now implemented and
independently tested. Its unscaled selector uses the shared parser's existing
fixed prefix and dynamic parameter descriptions: one field ordinarily, seven
for b20 and two for b22/b23. Arity previews copy numeric state; real operands,
including rejected random samples, advance once at execution. No sound policy
is added to the reusable parser or another VT52 parser.

Twenty mutable 56-word source patches and twenty separate saved records are
copied once when a session first needs sound. Identical original effects 3/11
keep distinct records. Source edits and restoration do not change an already
copied GIST voice. I/R graphics changes retain sound state; full reset and
reconnect discard it and establish fresh original banks. b22 reads the current
saved record, rather than substituting constants. The safe port has no native
accessory-open counter or memory-escape recapture mechanism.

Selectors above 23 become effect 0; negative selectors are no-ops. Effects
0..4 call GIST once when multiplier is zero, otherwise max(0,signed-low-word
(multiplier*200)) times; effects 5..19 call once. Each invocation receives the
current source pointer, automatic voice, volume/pitch -1 and priority 1. A
rejected voice allocation does not end the loop. These are immediate trigger
calls with no added wait, throughput delay or foreign CPU timing. b23 retains
all signed words <=16, including negatives, and maps larger values to 5.

b20 consumes play/patch/word/negative/thousands/ones, caps positive patch/word/
thousands to 19/55/32, constructs and optionally negates the defined 16-bit
word, and stores it before optional playback. A negative patch does nothing.
A negative word for a valid patch rejects with OUT_OF_RANGE after consuming
the arguments, with no store or play and the unread suffix preserved. This
corrects native RAM/code/MMIO escapes, including wrapped -32768/-32767 aliases.
b21 immediately stops A/B/C in order without resetting noise or oscillator
phases. b22 defaults invalid patch choices to 0 and restores only that source.
Neither operation clears unrelated music or effects streams.

A session-owned audio model feeds one shared PSG from GIST and the original
US TOS 1.04 BEL/keyclick tables. A dedicated lazy stream uses the ordinary
-12dB effects base and the existing audio consumer. It remains continuous
after effects finish, preserving generator phases and filter tails, until
EOF/reset/disconnect or output disable quiesces it. It does not use CTerm's
shared FX/music streams, start a worker or block serial input. The consumer
acquires the model mutex after mixer/stream locks; the owner releases that
mutex before every audio/runtime API and destroys the stream before freeing
its callback context.

Local sample fractions schedule GIST at 200 updates/second. The fixed console
tables are serviced at 50/second, initially after 15ms, following GIST when
both are due. New console triggers obey quiet and the driver's post-update
active-voice audio gate. The enabled-by-default Program Settings option
"Atari ST Key Clicks" toggles local clicks immediately and persists across
sessions as `[SyncTERM] AtariSTKeyClicks`. Disabling it leaves BEL and remote
sound effects enabled; terminal reset retains the local preference.
A previously accepted console table still writes
the shared PSG while a new effect suppresses further triggers. Bell/click
preserve R7's upper bits; click writes R13 before R11/R12 and retains B/C gains.
Only these used fixed tables are needed; no arbitrary Dosound pointer/memory
interpreter, GPIO wiring or foreign keyboard-repeat service is introduced.

Disabled/stopped/unavailable output opens no device and retains command/edit
behavior. Local monotonic elapsed time advances software control and PSG
phases without generating discarded PCM. Cumulative ceiling conversion avoids
delaying an exact 5ms deadline another millisecond. Closed-form local phase
reduction and GF(2) noise jumps handle 64-bit unheard frame spans, including
long idle periods on a 32-bit host. Unheard filter history is discarded, so
expired envelopes/tails do not replay on re-enable. Clock failures, rollback
and unrepresentable intervals return bounded errors; device failures preserve
logical playback and source edits. A failed clock read while switching from
PCM consumption to local deadlines leaves the new anchor pending, preventing
double-counting when the clock recovers. EOF stops voices and the stream while
retaining current source banks; full reset frees the model.

The native cterm_igs_audio test checks 140 effect/multiplier profiles, every
signed multiplier word, all 1120 patch edit positions, clamps/word arithmetic,
priority rejection, stop order and independent mutable/saved/copied records.
It covers all input partitions and bytewise input, random arity, partial/
invalid/EOF/timeout recovery and exact unread-suffix ownership. Local pull
checks cover 221/220-frame note boundaries, shared console/GIST output, mute/
re-enable without old sounds, I/R/source persistence, independent FX survival,
model allocation/backend/clock errors and concurrent pull/edit/restore/reset/
shutdown. The PSG component additionally compares fast unheard advancement
with rendering and checks subdivision of maximum 64-bit intervals.

The application fixture checks 198 coherent control-bitmap captures across
all ST resolutions, Atari ST/CP437/UTF-8 and both status-line choices. Original
patch words come from a pinned independent extraction in effect-vectors.json;
expected scene glyphs/palettes are existing independent fixtures. Each capture
combines locked sound state with exact indexed/RGB pixels, cells, palette and
cursor. Include separate identical-patch edits, source restoration/clamps,
short muted effect expiry, graphic initialization, active disconnect and fresh
reconnect. AudioModes=0 and MidiModes=0 prevent physical output; native PCM
checks use the device-free pull runtime. No auditory/device or reference-PCM
waveform coverage is claimed.

Native FreeBSD Release CMake configure and full build with /usr/bin/cc passed;
the final full build had no warnings. All 30 selected CTest checks passed in
59.47 seconds: shared parser/stream, GIST/PSG/images, audio mixer/source/runtime/
synthesis, music, APC/SkyPix audio, IGS input/I/R/g/cursor/t/q/math/X8/X15/b,
CTerm ANSI/VT52/keyboard, RIP parser, SkyPix, term/key forwarding and the b
application fixture. Native b took 0.27 seconds; PSG 0.13 seconds; application
b 19.73 seconds. The prior PSG component completion commit is 8dad5c7.
The sound foundation stays in progress for N and MIDI integration; n is
completed below. Remaining loops/selectors/final acceptance are unfinished.
References: chapters 06/24/53/55/56/57.

Completion commit for b: 53d1e31 (52/63 at that step).


### n: shared notes and interruptible local waits

The n command is complete, bringing the ledger to 53/63. Six unscaled
arguments use the existing shared parser descriptions: effect, voice, volume,
pitch, delay and stop. The session audio component exposes the same helper
for later N byte-note records. It normalizes private copies and leaves caller
values, mutable patches and saved snapshots unchanged; playback reads current
b20-edited sources rather than a separate bank or note-specific oscillator.

Clear negative effect to 0 and cap effects above 19 to 19, preserving the
no-trigger sentinel 125. Clamp voice to 0..2, volume to 0..15, pitch only above
255, negative delay to 0 and stop to 0..4. Positive pitch and effect !=125 call
GIST once with the explicit voice/volume/pitch and priority 7. Other requests
still wait and stop. Rejected allocation does not skip those stages. b's
multiplier does not apply; the source's duration is not replaced by the delay.
Pitched GIST control remains sustained until a later release/stop action.

Trigger precedes the local start sample, including for zero delay. A positive
delay becomes a checked monotonic deadline at five milliseconds per unit and
suspends the parser without consuming queued following bytes. A zero or
negative delay completes synchronously and does not consume a key. Correct
the native low-word clock-wrap early exit: crossing the old wrap boundary
does not shorten the local delay. There is no busy wait, sync-wait conversion,
flow-control wrapper, foreign clock/counter or CPU/interrupt schedule.

One key of any value ends a pending delay; Ctrl-C, Ctrl-X and + are ordinary
cancellation keys here. The key is consumed, the stop action still runs, and
outer-loop state is retained. A later key is forwarded normally. Stop 0 does
nothing; 1/2 release/stop the selected voice; 3/4 release/stop A,B,C in order.
Release schedules GIST's duration/release transition and does not immediately
zero gain. No MIDI all-notes-off or clearing of unrelated streams is added.
The owner holds only voice/stop/key state alongside its existing local wait
deadline; the audio consumer continues independently under the model mutex.

A clock/deadline-construction failure retains completed trigger actions
without fabricating an elapsed stop or leaving an unarmed wait. A transient
clock error while polling retains the pending deadline/key for retry. EOF/
disconnect cancels the wait and stops the chip; reset/reconnect clears its
state and source ownership. Disabled/unavailable audio still honors logical
notes, deadlines and stop actions without opening a device. The common stream,
muted phase advancement and callback-quiescence implementation is reused.

The cterm_igs_note CTest failed at the absent trigger before implementation.
Its 235 immutable cases come from the independent note-helper contract: seven
explicit fixtures, all six fields at eighteen signed-word boundaries, all
twenty source patches and one hundred fixed-seed requests. They check private
normalization, source/caller ownership, priority-7 arguments, mandatory start
sample and exact release/stop calls. Literal cases verify mutable source and
b22/copy independence, b priority rejection, release PCM, 221/220 local sample
boundaries, every stop action, maximum deadlines and the corrected wrap edge.

Additional tests cover all input partitions, bytewise/chained input, random/
array operands, unscaled constants in every scaling mode, and unchanged pixels,
palette, cursor and drawing state in all ST modes/status choices. They exercise
arbitrary key forms, deadline/key coincidence, later forwarding, EOF/timeout/
invalid/reset recovery, allocation/backend failures, clock failures before and
after trigger, retained key retry and maximum timestamp handling. Expected
helper data and source identities are in note-vectors.json; ordinary tests
need no external source tree, ROM or historical executable.

The application fixture adds 306 coherent control-bitmap captures across all
ST modes, Atari ST/CP437/UTF-8 and both status choices. A literal editable patch
with a two-unit source duration demonstrates sustained control independent of
note delay, effect-125/zero-pitch no-trigger behavior, selected/all release and
stop, normalization, long key-cancelled waits and queued-input resumption.
Captured voice fields and source-bank fingerprints share each frame with exact
indexed/RGB pixels, cells, palette and cursor. A consumed + never reaches the
host; a key after completion does. Active disconnect and fresh reconnect check
clean note/bank ownership. AudioModes=0 and MidiModes=0 disable physical output;
PCM functionality is tested separately through the device-free pull runtime.
No auditory, physical-device or reference-waveform coverage is claimed.

Native FreeBSD Release CMake configure and full build with /usr/bin/cc passed;
the final full build had no warnings. All 32 selected CTest checks passed in
86.51 seconds: shared parser/stream, GIST/PSG/images, audio mixer/source/runtime/
synthesis, music, APC/SkyPix audio, IGS input/I/R/g/cursor/t/q/math/X8/X15/b/n,
CTerm ANSI/VT52/keyboard, RIP parser, SkyPix, term/key forwarding and application
b/n. Native n took 0.34 seconds; application n took 25.87 seconds. The sound
foundation stays in progress for N and MIDI integration. Buffered music,
remaining loops/selectors and final acceptance are unfinished. References:
chapters 06/23/57/182; prior b completion commit 53d1e31.

Completion commit for n: 4f868b5. The goal remains active at 53/63.


## Right-button macros (`X3`)

Chapter21 defines both configuration and the main-loop consumer. Operation0
only disables; >2 selects0 and negative words are no-op. Operation1 enables
before its low-byte CR reader. Operation2 publishes enable, low-byte CR, then
length truncated to a signed byte and clamped1..80. Thus128/255 become1,
-200 becomes56 and256 becomes1. Each store survives a later failed reader.
It acquires exactly n opaque bytes plus one discarded byte, without leading
separator skip or delimiter test. Old suffix bytes survive. Exact loop1 calls
the existing XON helper before acquisition even when enable is0. Every raw
slot, including the discard, uses its own local5s timeout returning space/BEL;
raw control bytes and local cancellation keys do not abort this reader.

Right-only (native button2) and enabled byte1 send the macro once, reloading
its current signed length, payload byte and final CR flag at their use sites.
The wire contains unchanged low bytes and optional CR, with no LF or text
conversion. An owner-managed hold preserves unread serial suffix and queued
key events until all buttons release; left-only/both keep the hold. Existing
transport owns readiness. UI/status/outside presses cannot arm a macro;
release anywhere and disabled-forwarding synchronization prevent stuck holds.
Effective mouse mode3 preserves the Wren mouse gate/indicator, avoids local
paste/selection on macro clicks, and retains wheel-up scrollback. Full reset
and reconnect clear the initially zero macro, while I/R preserve it.

The native test covers all256 opaque byte values,18 signed length boundaries,
max80 sends, incremental/retained/discard/timeout/EOF/error stores, exact-loop
flow, numeric RNG,40 partitions plus bytewise input over18 graphics profiles,
dual-phase indexed pixels/palettes/attributes/cursor, pending click/suffix
ownership, current-state callback changes and reset/release during send.
The application test covers423 coherent scene/state captures over all three
ST modes, Atari ST/CP437/UTF-8 and status choices, with real raw socket output,
queued Unicode text, mouse disable/scrollback/status routing, disconnect during
a load/hold and fresh reconnect. N and X3 share the input component's existing
bounded key FIFO; replayed UTF-8 text retains NFC and its normal fallback path.
Physical audio/MIDI are off. Provenance: `src/tests/igs/macro-vectors.json`.
Corrupted positive lengths cannot read beyond80 payload bytes; native RAM
escapes, low-word wrap artifacts and device polling are deliberately excluded.
Transport EOF cancels a partial load without manufacturing more raw bytes.

Native FreeBSD Release CMake configure/full builds passed without warnings.
All33 selected regressions passed; after a raw-key ownership fix all25
selected checks passed, then the final timeout-order fix passed native
X3/N/X5 and the423-capture X3 application run. Final native X3 took0.26s and
application X3 took40.89s. The ledger is now56/63 complete; parent X and
remaining command/final acceptance work stay unfinished.

Completion commit for X3: 502cf25. The goal remains active at56/63.


## Zone definitions and payloads (`X4`)

Chapter21 defines X4's producer independently of the still-pending `<` zone
selection/reply consumer. Unscaled selectors above47 become47, except9999
(clear all four words of every rectangle to-1),9998 (strict loopback on) and
9997 (off). Those controls read no extra operands or raw bytes and emit no
flow. Clearing bounds preserves every stored length/payload and loopback.
Initial bounds, payloads, lengths and loopback are zero from accessory BSS/
DATA. Full reset/reconnect restore that state; I/R preserve it.

Ordinary definitions store X/Y/X/Y signed words individually before the next
reader, using existing coordinate scaling and numeric sources. Bounds are
neither reordered nor clipped. The unscaled signed WORD length clamps1..80;
255/256 become80, while X3's signed-byte clamp produces1. Exactly n opaque
bytes are stored and one further byte is discarded without delimiter checking,
leading separator skip, NUL padding or suffix clearing. Exact loop1 calls XON
before the first auxiliary receive deadline. X3 and X4 share this service:
each raw slot, including the discard, times out independently at local5s,
returns space/console BEL and continues. Local keys wait through loading
without aborting it and resume through their original encoding path.
EOF/rejection/timeout cancels pending readers while keeping completed stores.

Negative zone indices are safely rejected before the first rectangle store;
they are native wrapped RAM/code escapes, not extra zones or aliases to47.
Their unread suffix follows the existing nonfatal graphics/plain recovery.
Transport EOF does not fabricate missing bytes; no foreign clock, BIOS polling,
legacy file-source EOF behavior or hardware throughput wait is added.

`cterm_igs_zone` covers all48 slots,14 signed length boundaries, all256 raw
values, maximum80 bytes, untouched slots/suffixes, controls/loop guards,
individual stores, numeric/EOF/timeout/RNG/reset recovery, delayed-flow first
deadline and reentrant reset. All44 partitions plus bytewise input over18
mode/encoding/status profiles and162 captured-resolution/scaling cases retain
full dual-phase pixels, palette, attributes and cursor. An X3 click sampled
during loading waits for X4 completion and retains the serial suffix and local
key queue until release. `syncterm_igs_zone` adds432 coherent control-bitmap
captures across all modes, Atari ST/CP437/UTF-8 and status choices, checking
independent glyph/index/RGB and every zone's explicit serialized state.
Application negative values are actual X13 subtraction/lookup operands; a
minus prefix is an IGS loop expression. The fixture covers opacity/retention,
controls, incremental stores, queued Unicode input and partial EOF/reconnect.
Physical audio/MIDI are disabled; ordinary tests are self-contained. Provenance
and reference hashes are in `src/tests/igs/zone-vectors.json`.

Native FreeBSD Release configure/full CMake builds were warning-free. All30
selected regressions passed in107.15s, including shared grammar, IGS input/
state/auxiliary/music, CTerm ANSI/VT52/keyboard, RIP, SkyPix, term, Wren and
application X3/X4/X5/N. Native X4 took0.26s and the432-capture application run
28.26s. The ledger is57/63 complete; X dispatch, `<` and final acceptance remain
unfinished.

Completion commit for X4: 542fbb4. The goal remains active at57/63.


## Left-button release output (`X6`)

Chapter20 defines the mode store and actual outer-loop consumer. One unscaled
operand stores its low byte, without clamping or immediate transmission.
The original accessory DATA byte is0. I/R retain it; full reset/reconnect
restore0. The accepted selector is cached before acquiring the mode, avoiding
repeat random/array evaluation. EOF/timeout/rejection retains the prior mode.

Any nonzero byte plus exact left-only button word1 enters a hold, with no
output while it remains1. Initial both3 does not enter. Once the word is any
other value, send raw USER CR, reload the current mode and send LF only for
byte2. Values255/1/3 all send only CR;256 disables and258 selects CRLF after
byte truncation. Changing the mode during the CR send affects LF but cannot
remove the already-committed CR. Existing transport owns readiness; there is
no additional clock, native polling, debounce or hardware throughput wait.

The final sample survives for the immediately following X3 branch. Right-only2
can send the separate macro after CR/LF; its own hold still requires all
buttons up. Left-to-both ends X6 immediately. Serial suffix and original-key
input remain queued in native order until the relevant holds finish. Input
sampled during commands/chains/raw loads/local waits waits for their outer
boundary. Reset during output interrupts remaining bytes safely; transport
EOF cancels a hold without a fabricated release CR. Local UI/viewport/status/
disabled presses cannot arm a macro or left hold, while release anywhere is
accepted. Effective mode3 covers either X3/X6, preserving Wren selection,
paste, status/disable gates and local wheel-up scrollback. The API manual is
updated. The future `<` command retains its own separate input-loop scope.

Native `cterm_igs_left_mouse` checks all256 bytes,16 signed word boundaries,
17 split positions and bytewise chains over18 mode/encoding/status profiles
and54 scaling variants. Full dual-phase pixels/palette/attributes/cursor stay
unchanged. It also covers release masks, X3 handoff, key/serial ownership,
raw/q/command suspension, fixed selector/RNG, numeric/error/EOF/I/R/reset,
post-CR live mode changes and reentrant reset/release. The application test
adds675 coherent captures across all ST modes, Atari ST/CP437/UTF-8 and status
choices. Exact socket bytes and explicit Wren output fences prove no early or
repeated CR, exact CRLF/macro order, Unicode queue/remote-disable ordering,
local mouse/scrollback gates and held/partial EOF/reconnect, alongside the
independent glyph/index/RGB/state scene checks. Physical audio/MIDI are off.
Ordinary tests are self-contained; provenance: `src/tests/igs/left-mouse-vectors.json`.

Native FreeBSD Release configure/full builds passed; the final build was
warning-free after transient generated-dependency timestamp warnings. All34
selected checks have passing results across the regression run and focused
rerun, which corrected an old X10 unsupported-selector inventory without
changing its chain-cancellation expectation. Final X10/native X6 took10.20s/
0.22s; the675-capture X6 application matrix took68.69s. The ledger is58/63
complete; X dispatch, remaining commands and final acceptance are unfinished.

Completion commit for X6: 7fb2643. The goal remains active at58/63.


## Version, cursor, pixel and resolution queries (`?`)

Chapter26 defines all four branches; chapters73/74 supply the original US
TOS mouse forms. All operands are unscaled. Query0 replies `2.21` without a
terminator;3 replies `0:`, `1:` or `2:`. Query1 takes a wait word and returns
column+31,row+31,buttons+32 (one-based cursor inputs). Query2 takes a style
word and returns canonical decimal `X,Y,button:`. Unsupported selectors are
recognized no-ops with one-operand arity. Replies use the existing raw
response transport, with no XON/XOFF or native readiness-loop emulation.

Immediate1/2 report current cursor/device state. Interactive1 moves the
existing VT52 cursor once per changed axis per sample; it retains press
buttons and queries the final cursor after release. Motion during release
does not move that cursor. Interactive2 styles1/2/negative use the current
polymarker in physical XOR. It erases/redraws only in the press loop and
remains at its last press position while acquiring release coordinates.
Styles3..10 use all eight original US AES forms and hotspots, with exact
mask/data and native physical palette indices; GEM follows release movement.
The final reply combines press buttons with release coordinates. Query
clicks do not trigger outer X3/X6 behavior. The parser and original key FIFO
retain unread/chained serial input and keys until completion, with replies
before forwarded keys and the remaining chain before its queued keys.

The indexed framebuffer remains authoritative. Small dual-phase save-under
restores GEM pixels; raw v_pmarker preserves the P endpoint. Cleanup restores
the actual prior writing mode and host pointer visibility on success, EOF,
reset or error. It clips to the current drawable viewport, preserving newly
reserved UI rows. Frontend pixel state is observed before local hooks/gates,
without arming gestures; UI/disabled presses stay suppressed and synchronized
release takes the latest usable position. Position defaults to0,0 until the
first native viewport sample; cell-only events do not invent pixel positions.

Explicit bug corrections follow the goal: seed character-motion baseline
from the initial sample rather than uninitialized stack; restore prior mode
instead of marker height/GEM XOR leak; fix thousands zero padding and cap
both decimal coordinates to0..9999. Retain native US form mapping (style4 is
the TOS text form despite the manual label). Reject unsafe styles above10
instead of dereferencing surrounding/NULL form data. No CPU/IRQ/VBL/hardware
visibility counters, save guard-word escapes, foreign clocks or throughput
waits are emulated. Transport EOF never fabricates a click/reply.

`cterm_igs_query` checks literal replies, numeric/style boundaries, state/RNG,
EOF/timeout/error/reset, all23 partitions and bytewise chains over18 profiles
and54 scaling variants. All768 form/depth/status/alignment/edge cases compare
both phases to a separate per-pixel oracle using original74-byte MFORM test
inputs. Tests also cover marker timing, endpoint/mode/visibility restoration,
chained keys, query-owned X3/X6 clicks, synchronized release coordinates and
status-row reservation during cancellation. `syncterm_igs_query` adds864
coherent all-mode/encoding/status captures with independent glyph/palette/
index/RGB/state expectations, all eight pointer forms, exact real replies,
character motion, press/release semantics, Unicode keys and pointer-visible
EOF/reconnect. Physical audio/MIDI are off. Assets and source identities:
`src/tests/igs/query-vectors.json`, `src/tests/igs/mouse-forms.json`.

Native FreeBSD Release configure/full builds passed using `/usr/bin/cc`;
final review and focused builds were warning-free. All39 selected CTest
checks passed in212.71s, including864 query captures in69.08s, with console,
shared parser, IGS, ANSI/VT52, RIP, SkyPix, term/input and Wren regressions.
After correcting the native fixture to cover all16 horizontal alignments,
the rebuilt query test passed in1.14s with768 form cases. The ledger, active
tests, source hashes and original form inputs were validated. The ledger is
59/63 complete; parent X, remaining commands and final acceptance are pending.

Completion commit for `?`: 18f97f3. The goal remains active at59/63.

## Keyboard and zone collection (`<`)

Chapter27 adds one-key, CR-terminated keyboard and mouse-zone input. The
three unscaled operands retain low-byte CR and signed type as each is read.
Types<=0 read one complete key word;1 retains up to129 printable bytes.
Control/extended words do not terminate a string except exact CR. Options2/3
suppress pending output, with ordinary echo only for2. Backspace always emits
its native VT52 erase sequence. Delete clamps at zero, correcting native
memory underflow; payloads retain their suffix and have no NUL terminator.

Mouse types2/>10 use the current polymarker;3..10 share the original AES
forms with `?`. Retain press coordinates through release, test all48 signed
inclusive rectangles in order, and copy the first hit's binary payload.
A miss selects47 by default, or rings console BEL and retries in strict mode.
No keyboard key aborts mouse selection. Restore pixels, actual mode and prior
host visibility on completion/cancellation. Reject impossible zone lengths
above80 instead of reading adjacent native objects. UI/disabled input gates,
indexed save-under and current viewport/status clipping remain in effect.

The collected reply waits for the enclosing graphics chain to exit; another
`<` replaces it. Clear pending before output and reread live count/CR after
each byte. Send through the existing user-input transport before unused queued
keys. EOF/reset/error cancels unfinished work without fabricating input.
File-source echo remains part of X9 integration. Physical audio/MIDI are off
in fixtures; source identities are in `src/tests/igs/collect-vectors.json`.

`cterm_igs_collect` checks768 one-key type/value combinations, full-word
boundaries, host Delete translation, all echo options and129-byte editing,
split/bytewise chains over18 profiles and54 scaling variants, mutable partial
operands/RNG, deferred/replaced/asynchronous replies, EOF/timeout/reset and
reentrant polling/live-count callbacks. All48 zone indices, inclusive first
hit, reversed/empty/default/strict cases, NUL payloads, queued keys, X3/X6
ownership and pointer cleanup are covered. Focused `?` regressions also prove
outside/newly claimed presses stay suppressed through held movement.

`syncterm_igs_collect` adds1404 coherent all-profile captures with independent
glyph/palette/index/RGB/state expectations, all8 pointer forms, real raw replies
and Wren ordering fences. It covers keyboard echo with host Delete/Backspace,
strict retry, disabled forwarding/re-enabled drags, queued Unicode and held
pointer EOF/reconnect. Headless fixtures make no auditory/device claims.

Native FreeBSD Release CMake configure/full builds passed using `/usr/bin/cc`;
the final full build had zero diagnostics after early transient dependency
timestamp warnings. All41 selected checks passed in274.39s. After the final
callback/Delete/drag fixes and rebuild, all17 affected checks passed in104.64s,
including1404 collection captures in104.59s,864 query captures and the native
collector in0.43s. Independent failures demonstrated each corrected input
bug. Ledger IDs/order, active tests/dependencies/references and source hashes
were validated. The ledger is60/63 complete; parent X, remaining commands,
X9 file-source echo and final acceptance remain pending.

Completion commit for `<`: fab4fda. The goal remains active at60/63.

## Stored-operand source support in the shared parser

Chapters11/13 require a loop to supply evaluated numeric values and then
hand later fields of the same command back to the wire. The shared grammar
now supports that per-field handoff, explicit typed values and retryable
source waits without wire consumption or repeated field entry. Direct command
selection reuses the existing descriptors and boundaries without invented
framing bytes, including replacement of an accepted suspended command.
Failed selections preserve pending grammar and borrowed views.

`graphics_protocol_source` independently covers split/bytewise handoff,
source retries, signed int bounds, dynamic fields, text/NUL/raw opacity,
early-end suffixes, zero-byte fields, EOF/cancel/errors/limits, direct/chained
commands and suspended-view lifetimes. The parser adds no Atari arithmetic,
loop executor, clocks or workers. This is supporting parser functionality;
`&` execution remains pending and the command ledger stays60/63 complete.

Native FreeBSD Release configure/full builds passed using `/usr/bin/cc`;
the final full build was warning-free. The mixed-source test failed before
implementation. All30 selected CTest checks passed in154.92s, including the
source/parser/stream tests, native IGS/ANSI/VT52/RIP/SkyPix, term/input,
profile/Wren and application text/bitmap/flow/pattern-load regressions.
Active tests/dependencies/references were validated. All63 command entries
are unchanged, with60 complete; the full goal remains active.

Parser source support commit: 4adf6fa. The goal remains active at60/63.

## Stored-operand loops and gang dispatch (`&`)

Chapters11/13 add staged unscaled setup, a raw target or retained128-byte gang,
raw kind, count clamped1..2048 and stored typed operands. Preserve accepted
prefixes, shared text/math state and suppressed r/R load values. Numeric
source calls implement varying/reverse-distance/relative/XOR/constant stepping,
last-index termination, local key consumption and final-W text reuse. A final
operand still reaches its command; subsequent fields switch to the wire.
Typed values are not scaled or evaluated twice.

The existing descriptors and one parser execute every body. Gang selection
is a numeric call with invalid indices normalized to0 and old bytes after
NUL preserved. Unknown fixed/gang targets keep their distinct reader and exit
semantics; fixed & never recursively initializes. Async completion returns
to the loop owner. Local monotonic deadlines implement200 delay units/second;
cooperative budgets keep long loops responsive without a total iteration
limit, foreign clocks, throughput waits or new workers. Ctrl-C/Ctrl-X cancel
at an actual numeric poll. Reject accidental arithmetic overflow and saturate
observation counters. Mode flags without a configured array supply no stored
values; canonical & always creates a nonempty array.

Existing flow helpers balance setup/exit, retaining W/@ and raw-reader flow
rules. Deferred < replies wait for the enclosing dispatch; repeated collection
replaces pending bytes. EOF/reset/error clears active execution and reader
ownership. X9's file-source integration and final acceptance remain pending.

`cterm_igs_loop` checks independent literal evaluator traces, dual-phase
marker/glyph scenes, split/bytewise input over18 profiles/54 scaling variants,
fixed/gang/XOR/relative/mixed-wire arity, retained gang text, RNG, W/@/raw X3,
2048 operands, delays, cooperative cancellation and partial/error/EOF/reset
paths. `syncterm_igs_loop` supplies846 coherent all-profile captures with
independent glyph/palette/index/RGB/state expectations and real socket
flow/replies, repeated < replacement, key wake/abort and reconnect. Headless
fixtures disable physical audio/MIDI; vectors and source identities are in
`src/tests/igs/loop-command-vectors.json`.

Native FreeBSD Release configure/full builds passed using `/usr/bin/cc`;
the final full build was warning-free. All80 selected checks passed in298.40s,
including all native IGS tests and846 loop captures in48.81s. A failing inner
pause-key case preceded its ownership fix; final pause/palette cases pass.
An intermediate full relink passed14 affected checks in8.30s. Extended
native loop checks passed in0.42s, also checking unsigned/saturated
deadlines, checked-error suffix recovery, exact RNG and mouse query/zone bodies.
Ledger/tests/references/source identities were validated. The ledger is61/63
complete; X9, parent X and final acceptance remain pending.

The final queued-key regression also preceded its fix: active pause/palette/
standalone note helpers consume keys already waiting after numeric polls,
while N retains its distinct FIFO handling. A real &/n case uses pull audio
and proves note stop/cancellation without physical output. After the final
warning-free build, all16 affected checks passed in49.52s, including846 loop
captures in49.45s, application music in25.31s, native loop in0.39s and native
note in0.41s. The full goal remains active at61/63.

Completion commit for `&`: 1bfcfb1. The goal remains active at61/63.

### X9: shared script/music loading and guarded execution

X9 reads its unscaled operation with a committed selector/operand prefix,
so random operands are evaluated once despite dynamic arity and split input.
Operation0 loads the existing10001-byte N/script allocation. Both pipes are
stored; terminator lookahead is consumed/discarded, failed lookahead leaves
retained holes, and capacity is checked after that lookahead. The native
final decrement excludes the last payload byte; fixtures use an explicit
padding byte. Empty `||}` retains length0 safely. Loads retain old suffix,
music count and cursors, do not poll local abort keys or send flow bytes,
and use local5000ms receive deadlines for serial slots. Operation2 clears
the fixed allocation and N count only for a nonempty script outside execution.

Operation1 executes the live allocation through a serialized file attention
owner and the existing graphics parser. Generic accepted continuations keep
the actual caller's chain boundary while ending the old borrowed view; no
argument snapshot, second parser or loop operand stack is created. Nested
X9 load/execute/clear still reads its operation and clears the help gate,
but is otherwise guarded. Other commands can modify the shared allocation.
The file path shares command descriptions, typed operands and shared & state.
File & omits serial setup/final flow calls and new-text-counter reset; file
graphics-entry exit clears global loop mode. Deferred < replies echo live
sent bytes and CR through the existing VT52 console after the whole chain.

File text ends at NUL/EOF without appending a new NUL, preserving its old
suffix. File X3/X4/N/X11 raw readers do not use serial receive clocks or
timeout substitutions. Ordinary source bytes use US Atari console semantics
through the existing VT52 parser, with no destructive-backspace expansion
or final held-G flush. Command/chain NUL skips the body after acquiring both
words. Each outer pass consumes one local keyboard word; Ctrl-S waits for
exactly Ctrl-Q, including after Ctrl-C. Ordinary Ctrl-C ends a bounded source.
Inner waits, notes and interactive queries/collection retain key ownership.
Async completion and EOF/reset/error/disconnect unwind readers and saved
serial flow once, preserving the real caller boundary and unread wire bytes.

Safety corrections reject allocation escapes and counter overflow, retain
zero for the native empty-load underflow, and cancel truncated numeric/raw
fields or command pairs rather than supplying repeated artificial zero
words. File text still completes early with an explicit EOF acquisition count.

Native FreeBSD Release configure/full builds with /usr/bin/cc passed; final
builds had no warnings/errors. All67 selected native checks passed in111.12s.
Final extended native X9 passed in0.43s, including all splits/bytewise input
across54 mode/encoding/status/scaling variants, literal US console output,
loader/clear/guard/capacity/deadline cases, nested shared loops, opaque bitmap
loads, live callback mutation, query/resolution waits and reset during reply.
Application X9 passed666 coherent captures in42.17s across18 profiles using
13 handwritten scene vectors plus load-only, wait/key and reconnect checks.
Related18 native/application regressions passed in115.71s. Indexed phases,
RGB/palette, cells and state share one coherent capture. The source-vector
artifact pins five source identities and uses no external runtime assets.
Headless tests disable physical audio/MIDI and claim no auditory coverage.
The ledger is62/63 complete; parent X and final goal acceptance remain pending.

Final source-switch review also verifies cooperative yields across130 short
calls inside a serial loop and across independent callers, with no total
iteration cutoff. A pending accepted call retains its local key and wire
suffix. Budget/key regressions failed before their fixes. After the final
warning-free full build, all68 acceptance checks passed in135.80s, including
all native IGS tests and all666 application captures in42.17s. Native X9
passed in0.52s (0.49s standalone). Parent X and final goal acceptance remain
unfinished.
