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

Uncommitted experimental `igs_surface` coverage checks source-derived planar
words, physical pixel indices, all three screen geometries, mode
reinterpretation without conversion, padded MFDB row strides, all sixteen raster-logic truth tables,
neighbor preservation and invalid-operation bounds. It establishes the
storage primitive, not complete IGS drawing or raster-copy commands. The
decision above supersedes its proposed use as framebuffer storage; any retained
planar helper is limited to image-format boundaries.

Uncommitted experimental `igs_clock` tests check counters derived from supplied
local timestamps and phase-preserving waits. The separate clock abstraction
is superseded by local deadline/sample timing with unit conversions. These
tests do not establish command-specific timing or audio playback. The selected
timing design and application are not integrated yet.

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
