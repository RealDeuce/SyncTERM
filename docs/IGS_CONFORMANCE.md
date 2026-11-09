# IGS 2.21 conformance

IGS implementation is in progress. No graphics command or selectable IGS mode
is supported yet. The [implementation goal](IGS_IMPLEMENTATION.adoc) and
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
integration requirement, not a claim of completed IGS routing.

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
been removed; local timing integration remains pending.

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
been completed. Array evaluation and command-specific validation are pending.
