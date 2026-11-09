# IGS 2.21 conformance

IGS implementation is in progress. The application-selectable IGS 2.21 mode
implements `I`'s six initialization selectors. Other commands remain pending.
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
been removed. `I5` uses an injected local monotonic deadline.

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
