# RIPscrip 3.0 implementation goal

The target is the reconstructed RIPscrip 3.0 language as shipped by RIPtel
Visual Telnet 3.1, using [reference revision
04d1637b6e6af424a7e8e7b5ac43de9669112c5b](https://github.com/RealDeuce/remote-imaging-protocol/tree/04d1637b6e6af424a7e8e7b5ac43de9669112c5b/version/3.0)
from `RealDeuce/remote-imaging-protocol:stable`.
That documentation is the contract, including explicit rendering corrections
and referenced inherited formats. Exact RIPtel raster reproduction is not
required. RIPlib extensions and future proposals are outside the goal.
External application and URL launching are excluded: recognize the syntax,
do not execute it, and report unsupported capabilities truthfully.

**User scope constraint:** RIPv3 uses SyncTERM's existing ANSI engine. Do not
add a VT-102 emulation mode, parser mode, UI/profile selector, toggle or keymap
as part of this work, regardless of what reference documentation or a PR says.
This constraint takes precedence over reference adoption. ANSI behavior and
charset clarifications are evaluated for the existing ANSI engine; references
to RIPtel's separate VT-102 option do not expand SyncTERM's scope. Issue #104
remains relevant only to applicable ANSI charset/byte/reset semantics.

**Status: implementation in progress; no full-compliance claim.** Connected
RIP3 sessions now use the separate engine for its completed command families.
Incomplete families remain unsupported and nonfatal; full compliance is not a
prerequisite for connecting completed work. RIP1.54 retains its original route.
Source indexing is discovery, not adjudication or working-feature evidence.

`TW` is RIPscrip's table keyword for a **text window**. A text-window
definition records its position, size, font and activation/protection settings.

The connection owner supplies ordinary ANSI text rendering, automatic replies,
nonblocking input and continuation pumping, delays, bundled classic/outline
fonts, live ENV/STYLE/PAL/PORT/BUT/TW tables and backups, TW entry copying and live reset, bitmap/clipboard operations and remembered-pair PCB paste,
trusted per-BBS asset reads/exports, in-scene bitmap errors, basic prompts,
session/persistent variables, local modifier/capacity queries and WAV playback
through the existing mixer when audio is available. Persistent variables use
`rip3-variables.bin` in the existing RIP cache, separately from 1.54 storage.
Device geometry comes from the entry's selected raster; WORLD remains independent.
The three 4:3 presets remain supported without overriding other entry modes.
Unlisted bitmap-font resolutions use the original 640x480 MAF bank; vector and
outline scaling use the actual device dimensions. Command CRLF is suppressed,
including split CR/LF; ordinary text keeps its own line endings. Processing
failures are diagnosed and discarded inside RIP3, never returned as connection
errors. Constructor/accessor and transport failures remain distinct.

Level-0 `M` now selects the current environment's color mode on connections:
`M08` restores palette mapping, `M18` enables eight-bit direct RGB, and other
RGB precision values retain the old mode. Both numeric fields use current
Base Math; malformed fields and protected writes recover nonfatally. This
wire gate is distinct from the now-connected 1..8-bit COLORMODE variable
contract. The literal `J10|n2000|M08|fZKQO` prologue sets modes and WORLD;
it does not clear the screen. Scene reset `*` and `$RESET(SOFT)$` now clear the
physical canvas to palette-independent RGB black and reset the represented
TW/STYLE/BUT/PAL/PORT/ENV families on connected sessions. The complete reset contract
still awaits remaining header and ANSI state work.
The entry-versus-default-MicroANSI policy remains a separate open client decision.

WORLD state now follows the adopted word-range contract: wire `f` accepts
0..65535 per dimension; `$WORLD$` preserves the documented asymmetric overflow
and `$WORLDH/W$` preserves the inverted setter-target behavior. Device dimensions
stay entry-owned. Zero WORLD dimensions remain queryable and capturable, while
projection requests reject before division and recover without disconnecting.

`$COLORMODE$` now sets and reports every RGB precision 1..8, including through
protected current/noncurrent ENV records; only that precision field changes.
Undefined getters return `-1` without defining a slot. Foreground, fill,
background, graphical text and erase consumers use the selected precision,
with blue/green/red packed fields normalized to eight-bit channels before the
existing 256-color current-palette match. The wire `M` RGB gate remains eight
only. Complete ENV switching and unfinished CM command families remain pending.

Ordinary `w` definitions now configure the default text window on connected
sessions: all five MicroANSI font modes, inclusive bounds, device-sized cells,
independently scaled reference origin, wrap/chop and clipped ANSI scrolling.
The all-zero tuple deactivates output while RIP parsing, identification,
graphics and immediate replies stay live. An identical layout retains its
cursor across wrap changes and deactivation/reactivation. Parsing uses the
current ENV radix/coordinate width, prevalidates the complete tuple and absorbs
invalid font/wrap fields or renderer failures without disconnecting. Endpoint
pairs are ordered and coordinates clamp to the nominal font grid before
identity; clamping diagnoses and continues. Local scripts
and native key forwarding address the selected virtual TTY; the main canvas
retains the entry's raster. Native tests cover widths 2–5, both radices, exact
geometry/ranges and error recovery; CP437/UTF-8 connection captures cover all
five modes, placement, scrolling, wrap/chop, reactivation and script operations.
TW backups, full reset and remaining metadata queries remain pending. Slot
selection/protection and defined noncurrent native views are now connected;
wire first use copies represented state into an unprotected slot, even from a
protected source. CUR first use creates fresh full-screen defaults instead;
ordinary ANSI parsing follows the connection across selection. ANSI-only
charset designation/state lifetime is now connected; displayed glyphs remain
CP437. ESC7/8 now saves/restores the represented text-window metadata through
the existing ANSI path, independently of CSI coordinates and whole-window
backups. The saved value follows the scene header and HARD reset lifetime rules.
Remaining terminal modes stay pending. Ordinary
`w` depends only on the physical device; a stored zero WORLD does not reject it.
Changed definitions construct a fresh rendering state without clearing
underlying artwork, retaining independently owned tab stops and unfinished
ordinary ANSI ESC/CSI/string parsing. Saved cursor coordinates reset to home;
identical definitions retain their existing interpreter. Optional media and
font-download payloads reject redefinition nonfatally while retaining the old
window. Adopted
[PR #102](https://github.com/bbs-land/remote-imaging-protocol/pull/102) resolves
the ordinary-state contract and corrects the font-2 placement example.
Geometry/normalized identity, pixel preservation and ordinary ANSI continuation
are implemented. [PR #107](https://github.com/bbs-land/remote-imaging-protocol/pull/107)
supplies the applicable ANSI charset/byte contract, now implemented for the
ANSI-only designation/glyph/lifetime scope. Separate CSI-coordinate versus
ESC7/8 represented state and the header lifetime rules are connected under
the PR151 completion. The full terminal mode inventory remains incomplete. No VT-102 mode is added. UTF-8 is a client extension: incomplete decoding is
committed at RIP boundaries, so bytes cannot join across window definitions.

Extended `b` now defines the selected connected text window using wide,
unclipped WORLD-to-device bounds and the actual bundled MAF cell metrics.
Directional font fitting, automatic font selection, centered display placement,
minimum one-cell extension, wrap, cursor hiding and immediate deactivation are
connected. Flag 16 clears the complete bounding rectangle to current palette
index 0 before deactivation; omission preserves artwork. Every definition resets
live/CSI-saved cursor coordinates, rendition, margins and tab stops while retaining unfinished
ANSI input and requested cursor visibility. Wholly offscreen windows remain valid;
over-budget allocations fail nonfatally and retain the old window. TW0 cannot be
protected, so flag 32 diagnoses that request after completing the definition.
Nonzero definition protection is connected. Known cursor/font/geometry queries
are connected; ambiguous far-edge defaults (#108), startup font (#105), backups
and the newly specified ANSI charset/saved-state behavior remain incomplete. The entry's raster and root grid remain unchanged.

Wire `g` and `H` now move the active text cursor through a native ANSI service,
without injecting input bytes. `g` has two-digit fields in the current radix
regardless of XY width; `H` is argumentless. Both ignore deactivated windows.
The connected default/current window now uses full-window absolute positioning
independently of ANSI origin and both vertical/horizontal margins, as required by
adopted PR #103. Movement clears pending wrap and retains margins, modes and saved
cursor. Following text can render outside the addressing region, while later ANSI
CUP remains margin-relative. Incoming validation and renderer failures remain
inside RIP3's nonfatal boundary. Noncurrent TW cursor controls and definition protection are connected; full
table backups/reset and remaining metadata queries stay pending.
Extended `b` fitting/automatic font choice
and erase/control-variable rules follow
[PR #103](https://github.com/bbs-land/remote-imaging-protocol/pull/103), resolving
[#101](https://github.com/bbs-land/remote-imaging-protocol/issues/101) as a
documentation blocker; implementation acceptance remains separate.

`$ATW$` and `$DTW$` now activate/deactivate the default window through immediate
host queries. Omission, `CUR`, decimal slots and `ALL` use one fully validated
selector list; undefined nonzero slots do nothing. Deactivation hides the
cursor while preserving its requested visibility/shape, text state and layout.
Reactivation imports the current artwork before showing a positioned cursor,
and activation effects publish before reply fences. Defined noncurrent slots retain their own activation state. Protected TWs
ignore ATW/DTW; selection does not activate an inactive slot. Full table backups,
reset and remaining query families stay pending.

`$CON$`/`$COFF$` now control the default/current cursor through native services,
retaining its shape and requested visibility across later text and activation.
`$TWGOTO$`/`$TWHOME$` use absolute full-window coordinates independently of ANSI
origin/margins; optional `CUR` axes retain the target coordinate. Decimal values
clamp to actual dimensions before narrowing, including legal 20-digit arguments.
No-argument/only-WINNO TWGOTO is a no-op. Selectors are validated before effects;
undefined nonzero slots and deactivated windows are ignored. These controls are
callable for defined current and noncurrent windows now. Definition protection
permits cursor controls; inactive targets ignore them. TW backups/reset and
remaining metadata queries stay pending.

Wire `e` and `$ETW$` now erase the connected default/current ordinary or extended window to
current graphics palette index 0 and home its live cursor. Wire `>` erases from
the cursor cell through the full display row's last cell using STYLE background,
resolved through ENV color precision/current palette. Both paths apply solid COPY
to native cells and the matching screen index plane, independent of graphics
PORT/viewport state, W, fill pattern and ANSI background. They retain terminal
attributes, origin/margins, visibility and saved cursor. Extended bounding margins
are included in whole-window erases. `$TWERASEEOL$` now erases the connected
current/default display row through its final cell using the native ANSI
background and color mapping, independently of graphics colors. It preserves
live/saved cursor, pending wrap, rendition, margins, cursor visibility and
unfinished ANSI syntax; it leaves extended bounding margins intact. Its fully
validated selector list defaults to current; undefined/deactivated targets do
nothing. Defined noncurrent and protected target views are connected. Full TW
backups/reset and remaining geometry/font metadata queries stay pending.

`$CURX$`, `$CURY$` and `$CURSOR$` now observe a defined active target's native
one-based cursor and requested visibility without selecting or presenting it.
`$TWFONT$` reports font+1 after a protocol w/b definition. TWW/TWH and
TWX0/TWX1/TWY0/TWY1 provide stored unclipped bounding/display/cell geometry,
including wide offscreen origins, explicit DEVICE/WORLD domains and live WORLD
scaling. Omitted coordinate type returns ordinary cell coordinates or -1 for an
extended window; graphical far edges are exclusive. Inactive/undefined targets
use each variable's documented zero/NO/-1 sentinel. Ambiguous far TEXT defaults
and cell domains reject nonfatally under #108; the unredefined startup TTY does
not invent a MicroANSI font under #105. These are connected read-only services.

Full reset/header orchestration, complete text-window/button/mouse/SCREEN state,
graphical-text/name expansion, masked/positioned/formatted editors, pick lists,
playback and deferred queries remain incomplete. The historical milestones below
record their state at each commit; older statements that application routing was
pending are superseded by this connected-session integration.

Connection acceptance uses the normal executable, TCP and the control bitmap
backend in CP437 and UTF-8. It verifies Dixon selection/ink/cursor advance,
bold's unchanged advance, classic fonts, nonfatal command recovery, long
command-only CRLF runs without scrolling, isolated identification probes,
live queries, templates/angles, local continuations, delays, a real prompt,
offscreen export/load, clipboard paste and ordinary text windows. Coherent captures retain the entry's
grid/raster at all three presets and at 1280x800 and 720x400. Physical audio is
disabled. Relevant native tests cover device-frame validation, typed tables,
text, source splitting, damage/copy behavior and constructor failure cleanup
for both preset and entry-sized sessions. The stale outline rejection of a
non-preset device was replaced with zero-width rejection and an explicit
720x400 DPI expectation. Native Release CMake builds and these tests pass.
RIP1.54 preservation guards, its historical connection oracle, encoding/native
term tests and the ellipses/raster/buttons application groups pass; those
unchanged successful checks are reused after the later RIP3-only fixes.

The user approved adopting the open documentation integration PR #71. Its
reviewed inactive-viewport #21 follow-up and PR #73's DEFINE_PORT clarification
are included in the current pin. PR [#77](https://github.com/bbs-land/remote-imaging-protocol/pull/77)
now supplies the adopted drawing contract for issues #74, #75 and #76.
PRs [#80](https://github.com/bbs-land/remote-imaging-protocol/pull/80) and
[#81](https://github.com/bbs-land/remote-imaging-protocol/pull/81) supply the
adopted N normalization and 2Y field/protection/diagnostic contracts.
PR [#83](https://github.com/bbs-land/remote-imaging-protocol/pull/83) supplies
the adopted signed global font-bbox/header field correction for #82.
PR [#85](https://github.com/bbs-land/remote-imaging-protocol/pull/85) supplies
the adopted argument byte/backslash/decoded-limit contract for #84.
PRs [#88](https://github.com/bbs-land/remote-imaging-protocol/pull/88) and
[#89](https://github.com/bbs-land/remote-imaging-protocol/pull/89) supply the
adopted outline advance/cache policy and user-variable directive/storage/prompt
reconciliation for #86/#87. Their explicit interoperability choices are normative;
native index-loop, sign-extension and unsafe behavior are not reproduced.
PRs [#92](https://github.com/bbs-land/remote-imaging-protocol/pull/92),
[#94](https://github.com/bbs-land/remote-imaging-protocol/pull/94) and
[#95](https://github.com/bbs-land/remote-imaging-protocol/pull/95) now provide the
adopted conditional operand grammar, ordinary outline origin/justification and
host-response/embedding clipping policies for #90/#91/#93. Their documentation
answers are adopted independently of remaining implementation acceptance.
PRs [#98](https://github.com/bbs-land/remote-imaging-protocol/pull/98) and
[#99](https://github.com/bbs-land/remote-imaging-protocol/pull/99) now resolve the
outer angle-scanner/branch contract and portable synthetic outline-effect
boundary for #96/#97. Implementation acceptance remains separate.
PR [#102](https://github.com/bbs-land/remote-imaging-protocol/pull/102) now
resolves ordinary-window normalization, state retention/reset and pixel
preservation for #100. Its ANSI interpreter-state requirements remain partly
unimplemented; the documentation blocker is resolved independently.
[The adoption review](RIP3_REFERENCE_ADOPTION.md) and
[`reference-review.json`](../src/tests/rip3/reference-review.json) record its
provenance, scope and reopened claims. PR #103 further resolves extended-window
fitting, erase sources, absolute RIP cursor positioning and COFF omission. There
are now 7971 blocks in 68 normative
documents. Implementation milestones below describe work against the previous
pin unless explicitly reconciled with this revision. Their passing tests remain
historical behavior evidence; they do not prove the corrected contract. The
RIP1.54 baseline, original font bytes and external-launch
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
6. Route completed RIP3 work through the separate engine incrementally and
   verify it through actual connected sessions. Retain support for 640x480,
   800x600 and 1024x768 without overriding the entry's configured raster/grid.
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

**Historical prototype:** the next three paragraphs describe the earlier,
unbound screen kernel. The current connected screen-backup milestone below
replaces its capture set, palette binding and canvas policy.

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

## Polygon count and multi-contour reconciliation

P/l/p now use the adopted direct count boundaries: zero produces a nonfatal
parser diagnostic, one validates its body and draws nothing, and two retain
ordinary outline behavior without an enclosed fill area. They preserve drawing
style and cursor state. Counts remain two digits at every coordinate width;
1295 MegaNum and 4095 UltraNum vertices are reachable. Exactly N coordinate
pairs are read; the historical stale scratch pair is never accessed.

The separate engine now dispatches `<` to a multi-contour polygon decoder.
C and N1 use two digits, later Ni use XY digits, and every contour has at least
two vertices. Full grammar and the aggregate 1+C+2V<=8193 numeric budget are
validated before allocation or painting. All contours contribute to one even-odd
interior and one prepared border mask. Holes, overlapping contours and nested
islands use current-mode fill with optional patterned COPY borders. Each contour
restarts pen phase; shared border pixels apply once. Existing single-contour
entry points use the same geometry through one-contour wrappers.

`rip3_multi_polygon_commands` has 96 coherent full-device captures across all
canvases, screen/offscreen ports, both bases, widths 2–5 and unequal WORLD
scales. Independent box parity, perimeter distances and Boolean algebra check
holes, overlap cancellation, nested islands, transparent/opaque border patterns,
five indexed modes and RGB COPY. Focused cases check small counts without state
mutation, maximum single and aggregate counts, maximum contour counts, shortened
final fields, complete rejection of late malformed/oversized bodies, wide exact
crossings and every allocation failure before painting. An adopted literal
XY=3 stream exercises the asymmetric count widths and parser recovery. Original
diagnostic wording and exact POLYPOLY.RIP corpus/continuation replay remain
pending; these are not claimed by the new coverage.

Native FreeBSD Release CMake configure/build completed. Only the four affected
CTests were run: multi-polygon commands, path commands, filled-path commands and
wide polygon crossings; all passed in 0.71 seconds. Previous unaffected evidence
was reused. The source/ledger audit matches the recorded 93 files and 41 helper
pins, including documented font maintenance. No RIP1.54 source, capture,
preservation pin, reference revision or active application routing changed.
Owned temporary build files were removed and unrelated concurrent documentation
was left untouched. This review covers 57 source units: 547 implemented, 1417 pending, 4351 unreviewed, 984 context, 24 excluded, 34 blocked among 7357 blocks.
Full compliance and active-engine integration remain pending. No issue content
or code was published.

## Rounded-rectangle wire consumers

The separate engine now accepts U/u with five complete XY-width fields in the
current base. Both corner pairs normalize before wide integer conversion;
radius uses the X ratio once. Ordinary corner semiaxes fit X/Y spans
independently, including oversized radii, rather than imposing a common circular
clamp. Zero radius produces square corners. Zero extents are accepted and still
consume the operation's used inks; a radius does not expand those bounds.
The shipped low-WORD Y-radius sign/zero defect is retained. Extreme original
DWORD multiplication/doubling overflow remains an explicit pending ledger item.

U always selects its logical pen independently of N; u consumes the explicit
border binding and established device-context brush phase. Both use current
write mode and the same logical pen mapping as K, including fixed type-4
DASHDOTDOT and solid widths above one. Opaque flags partition prepared pen and
brush coverage; other flags fill then stroke with transparent gaps. Removing
the pen adds no inset or shrink. Corner curves and straight sides share one
application per covered border pixel; all fallible mesh/coverage preparation
precedes either paint pass. The existing standalone circular-radius/solid-COPY
API retains its geometry and explicit service contract.

`rip3_rounded_commands` uses 96 coherent full-device scenes across all canvases,
screen/offscreen ports, both bases, XY widths 2–5 and unequal WORLD scales.
Independent circular signed distances, perimeter lengths, fill inequalities and
Boolean operations check all logical types, flags, border states, thin/wide
pens, five indexed modes and RGB COPY. Shifted bitmap copies preserve source
brush phase. Focused checks replay the literal U/u examples and cover independent
oversize fitting, X-only radius scaling, high-radius Y sign/zero cases, maximum
five-digit WORLD=1 geometry, zero extents, all sixteen mesh/mask allocations,
unused NOT operands, absent/inactive bindings and split-input error recovery.
Exact GDI dash lengths and curved-edge pixels remain outside the target.

Native FreeBSD Release CMake configure/build passed without warnings. The new
rounded-command CTest passed in 1.31 seconds, and the affected standalone rounded
rectangle test passed. Only the new test was rerun while correcting its wire
matrix and adding boundary coverage; no broad suite or extra Valgrind run was
added. The source/ledger audit matches the recorded 93 file and 41 helper pins,
including documented font maintenance. No RIP1.54 source/capture/preservation
pin, reference revision or active application route changed. Temporary build
logs were removed and unrelated concurrent documentation was left untouched.
This milestone reviews 27 units: 566 implemented, 1414 pending, 4328 unreviewed, 991 context, 24 excluded, 34 blocked among 7357 blocks. Complete engine
integration and other pending contracts remain required; no code was published
and no existing issue content was changed.

## Border ownership and fixed style defaults

N now edits the border flag in the current graphics-style record, subject to
STYLE protection. All filled primitive consumers read that record; the external
command-host flag has been removed. ENV/PORT selection, protection and inactive
graphics do not own N. BAR and ordinary outlines retain their independent
behavior. Syntax and protection failures stay inside the void parser.

Unused STYLE entries now receive the adopted fixed normal defaults instead of
copying the departing style. A helper supplies the defaults of represented
fields, including drawing/fill 15, background 0, solid fill/pen, width 1, COPY,
classic font 0/size 1 and enabled borders. Prepared test scenes explicitly copy
custom records before selection where that is their intent; independent drawing
expectations remain unchanged. STYLE backups preserve the border flag and all
represented attributes/protection. Restore now retains and reactivates the live
current index, reproducing the shipped saved-index defect, including defaulting
that index if it is undefined in the restored table. Complete dormant font/user
fields, wire style/context activation and scene reset/header wiring remain
pending.

`rip3_border_commands` covers numeric bases, fixed count width, shortened final
fields, inactive graphics, independent ENV protection, STYLE protection/defaults,
selection, immutable backups and byte-split malformed recovery. Its all-canvas
capture distinguishes style-local border settings and confirms N never borders
BAR. The original setter at RVA 0xdcf1 also accepts every nonzero value; its
branch is exercised by N02, MegaNum NZZ and UltraNum N&& tests. The pinned prose
specifies only 00/01, so new parent-repository [issue #78](https://github.com/bbs-land/remote-imaging-protocol/issues/78)
requests the missing normalization, referencing the delivered #24/#33 work
without adding comments to those issues. Original-instruction evidence is
static; no Wine, Windows rasterization or desktop test was performed.

Native FreeBSD Release CMake configure/build passes without warnings. Eighteen
affected native CTests pass, including STYLE, border controls, all affected filled
consumers, retained text/point/font/drawing fixtures and the native image-command
fixture. Only failing/changed tests were rerun; previous unaffected results were
reused. The source/ledger audit matches all recorded 93 file and 41 helper pins,
including documented font maintenance. No RIP1.54 source, captures, preservation
pins, reference revision or active application routing changed. Scoped temporary
build logs were removed; unrelated concurrent documentation was left untouched.
The ledger reviews 36 units: 582 implemented, 1420 pending, 4302 unreviewed, 994 context, 24 excluded, 35 blocked among 7357 blocks. Full compliance remains
unfinished; no code was published and no existing issue content was changed.

## Wire and variable style selection

The separate engine now accepts 2Y and CUR(STYLE,n) through the same fixed-default
selector. Defined/protected records select without attribute changes; unused
records receive normal defaults. Font family now belongs to STYLE, replacing
the external graphics-text context flag. Y changes it under STYLE protection;
text reads the selected record at paint time. Record copies and immutable
backups include family metadata. Port, environment, cursor, encoding and
immutable classic-font overrides stay independent. Outline parameter ownership
and actual outline-command integration remain pending.

The 2Y handler validates its complete body before applying source flags 4/8,
selecting/defaulting, then applying destination flags 1/2. Unprotect runs last
within each pair. Higher bits are ignored; decoded slots >=36 do nothing before
flag effects. Protecting zero records a nonfatal diagnostic but does not prevent
selection or later flags. The original descriptor/handler establish these
behaviors, contradicting the pinned reserved-field table. New parent-repository
[issue #79](https://github.com/bbs-land/remote-imaging-protocol/issues/79) requests
that narrow clarification and identifies the remaining native-parser/disabled-
graphics evidence boundary. It references #2/#24/PR #57 without commenting on
existing issues.

`rip3_style_commands` independently checks all low flags plus ignored high bits,
source protection and undefined/writable/protected destinations at both bases
and XY widths 2–5. It covers same-slot ordering, zero-protection continuation,
invalid slot no-ops, malformed bodies, missing services, inactive graphics,
fixed defaults, the literal 2YG0 example, decimal CUR selectors and unchanged
PORT/ENV/cursor/encoding state. One coherent all-canvas capture combines wire
and variable selections with distinct fills/borders, CP437/UTF-8 text contexts
and byte-split recovery. Existing tests check atomic protected Y family changes,
invalid family metadata and immutable snapshots with mixed family values.

The attempted F consumer remains blocked by the existing B2 conflict. Original
Level-0 slot27 has argc0 and points to the bare RET at RVA0x1b2fd, as already
reported in #2, while pinned prose adopts a live solid flood. No flood renderer
or guessed wire consumption was added, and no duplicate issue/comment was filed.

Native FreeBSD Release CMake configure/build passes without warnings. The six
affected native CTests pass (0.50 seconds); only the new style-selection test
was rerun after adding source-zero diagnostic continuation coverage. Previous
unaffected evidence was reused. The source/ledger audit matches all recorded
93 file and 41 helper pins, including documented font maintenance. No RIP1.54
source/capture/preservation pin, reference revision or active application routing
changed. Scoped build logs were removed and unrelated concurrent documentation
was left untouched. The ledger reviews 50 units: 589 implemented, 1419 pending, 4262 unreviewed, 1018 context, 24 excluded, 45 blocked among 7357 blocks.
Full compliance remains unfinished; no code was published and no existing issue
content was changed.

## Port-owned graphics cursors and TEXTXY

Graphics cursors now live in PORT records as context-absolute wide integers;
the external text binding carries only encoding and an immutable font override.
MOVE converts WORLD coordinates and adds the viewport origin without needing
text/style/ink services. Text translates the stored position back to its raster
viewport, so changing viewport geometry does not move it. STYLE/ENV switches,
image-style edits and erase retain it. PORT backups restore image/selection state
while retaining each live cursor, including the documented screen-cursor restore
defect; offscreen cursor fields are not saved.

Classic TEXT advances stored X for every orientation/justification by the signed
low WORD of the whole integer string metric, retaining Y. A separate metric in
the classic font plan scales summed raw widths with integer rational arithmetic,
then truncates once; fractional raster geometry remains available. This avoids
losing a cursor pixel when a floating raster width lies just below an integer.
The obsolete horizontal/left cursor-policy hint was removed from the geometry
plan. Nonzero classic TEXT_XY retains its converted drawing anchor but stores
raw input X/Y plus viewport origin and metric. Empty TEXT is silent and empty
TEXT_XY diagnoses without moving the cursor. Outline producers and font-zero
TEXT_XY's cached-outline metric branch remain pending; the existing limited
font-zero geometric branch is not claimed as a completed original cursor rule.

TEXTXY now implements decimal port/CUR plus optional WORLD/DEVICE, current-frame
reconversion, truncation toward zero, signed16 narrowing and unpadded X:Y.
Explicit undefined/inactive ports return -1; bare omission reads the current
cursor even when deactivated. Failures remain parser-owned and preserve outputs.

`rip3_cursor` checks all canvases, both transport encodings, all ten classic
magnifications, both orientations and seven justification/shadow variants,
whole-string versus fractional raster advances, signed-WORD widths and mixed-
unit TEXT_XY stores. It reproduces the published 800x600/1280x960 example exactly.
One coherent full-canvas scene follows two independent screen/bitmap cursors,
STYLE selection, a viewport-origin change, rendered line glyphs and a shifted
bitmap copy. Separate checks cover inactive/undefined/protected state, negative
and wrapped replies, malformed/capacity/missing-service recovery, range rejection,
and 515:616 / 717:818 backup retention. Existing text pixel oracles are retained;
only cursor expectations change where the adopted contract corrects them.

Native FreeBSD Release CMake configure/build passes without warnings. Eight
affected native CTests pass: cursor, classic text, text commands, STYLE commands,
polygon state preservation, port queries, ports and port backups. Only failing
or changed tests were rerun; no broad suite, compiler matrix or extra Valgrind
run was added. The preservation/ledger audit matches all recorded 93 file and
41 helper pins, including documented font maintenance. No RIP1.54 source,
expectations, preservation pins, reference revision or active application route
changed. Scoped temporary build logs were removed and unrelated concurrent
documentation was left untouched. The ledger reviews 33 units: 607 implemented, 1414 pending, 4243 unreviewed, 1024 context, 24 excluded, 45 blocked among
7357 blocks. Full compliance and engine integration remain unfinished. No issue
content or code was published.

## Extended-font STYLE commands

The separate engine now consumes the shipped 26-character `y` argument block,
using UltraNum independently of ENV radix/coordinate width. STYLE owns the two
compatibility bytes, all 24 flags, point size, shadow distance, independent
quarter-turn rotations, converted baseline magnitude, positive spacing and a
39-byte name. Numeric unused/reserved fields are validated and discarded.
Definition accepts size zero; font realization will own its deferred render
failure. Baseline conversion truncates once using the current world Y scale;
subsequent frame changes retain that device magnitude. Normal STYLE defaults
retain classic mode, empty outline name, zero dormant fields and spacing 100.

`q` ORs current-base attributes 0..15 into the selected unprotected outline
STYLE; zero retains all flags. A new `y` replaces those fields. `Y` selects
classic mode while retaining the dormant outline state. Record selection and
owned snapshots preserve the fields and names. Protected STYLE edits and
malformed commands leave them unchanged, with failures consumed at the public
void parser boundary. Missing/unavailable literal names do not trigger font
lookup or classic fallback during definition. Macro notation remains explicitly
unsupported; expanded names and outline text production remain unfinished.

The new native `rip3_extended_font_commands` test checks literal published
examples, both ENV bases, fixed widths, all sixteen rotation pairs, maximum
fields/reserved input, size zero, name truncation/ownership, one-time baseline
conversion on all canvases, inactive PORT/protected ENV independence, attribute
OR/replacement, protected edits, malformed recovery, classic switching and
snapshots. Six affected CTests pass once: extended font commands, classic font
commands, text commands, STYLE commands, STYLE service and allocation failures.
The native CMake project builds; no unrelated renderer tests were repeated.
RIP1.54 and the active application route remain unchanged.

## PR #77 drawing-contract adoption

The fork's `stable` revision `72edbfc6c151d86a120fec3cabbde3cad32c9c0b`
includes reviewed PR [#77](https://github.com/bbs-land/remote-imaging-protocol/pull/77)
head `af5468de5f5d1e4ad91b389c5766be5ed1f1293f`. The PR is open upstream;
its complete changes are already integrated in the reference branch. This
adoption resolves local documentation blockers B74/B75/B76 without treating
those requirements as implemented. No issue received comments or other content.

The contract gives X/j all five drawing operations; filled regions use W before
an optional COPY border. K/u/G/o borders are solid. Polygon-related borders use
the requested pattern, including custom masks at supported widths. N0 removes
radial/closing outlines. Genuine standalone stroke revisits apply W again;
shared joins and redundant closure records do not automatically add visits.
Skewed geometry converts to device coordinates before rotation. Its complete
Q14 tables and staged floors are an optional compatibility profile, without
requiring native integer overflow. Graphics-text color contributions honor W;
historical FastFont fixed-copy behavior is not a protocol exception. Image
source-NOT remains distinct from drawing destination-NOT.

Fourteen existing normative documents change and two composition documents are
added. Exact source/heading matches carry 7338 block reviews, including 927 moved
line references. The 117 new or substantively changed blocks are reviewed as
pending/context. Eleven old implementation claims are replaced or removed;
five otherwise unchanged claims are reopened where the composition rules
invalidate historical rendering evidence. Seventeen documentation-blocked units
become pending. The ledger now has 615 implemented, 1555 pending, 4204 unreviewed,
1029 contextual, 24 excluded and 28 blocked units across 7455 blocks. Old milestone
tests remain historical evidence until their affected expectations and consumers
are reconciled; in particular the prior K/u logical-pen/current-W behavior does
not satisfy the new solid COPY-border contract.

The adoption record preserves the exact archive digest, all 67 document hashes,
changed claims and hashes for the new derivation and independent pixel/Q14 data.
Only the changed ledger/adoption consistency is checked for this metadata update;
the just-passed native build, six font-state tests and unchanged RIP1.54 freeze
evidence are reused. No rendering or preservation suite is rerun. The downloaded
reference tree and review scratch file are removed. Source implementation,
legacy pins and the active application route are unchanged by this adoption.

## PR #77 point and rectangle composition

The separate wire engine now applies all five W operations to X and j. Their
existing world projection, distinct origin rules, clipping and odd-square
thickness are retained. Each covered pixel receives one contribution; repeated
commands operate on the resulting destination. NOT ignores source ink and does
not require palette lookup. This milestone uses the existing indexed 8-bit plane;
full RGB and active-color-mask integration remain pending, and the unqualified
primitive requirements retain pending status rather than claiming those modes.

K and u now fill with W and then COPY an enabled solid border using Draw Color
and current thickness. Stored line patterns, opaque-gap flags and historical
single-pass behavior do not change that border or partition the fill. A NOT
interior still requires a resolved enabled border color. Border coverage and all
required inks are prepared before either phase paints; failures leave the scene
unchanged. Degenerate/border-disabled cases retain their no-fill/no-border rules.
Obsolete brush/pen exclusion paths were removed. Standalone U now honors the
requested stock/custom mask at wide as well as narrow thicknesses, without a
fixed DASHDOTDOT substitution or wide solid fallback. Stored STYLE state is
unchanged by each drawing operation.

Independent literal vectors reproduce S=3C, D=A5 results 3C/99/BD/24/5A, background
66/C3/E7/24/5A and enabled COPY border 0F in every mode. One coherent capture per
canvas combines single and repeated X/j operations, thick squares, screen and
offscreen targets, K/u patterned fills and N0/N1 borders. Rounded zero-radius
regions establish the same rectangular composition independently of edge pixels;
the separate rounded scene retains its analytic curved coverage oracle. Existing
rectangle/rounded captures were corrected from historical client pen/pass rules
to the adopted protocol expectations, including RGB ink preflight and NOT's
still-required border color. They cover both bases, coordinate widths, world
scales, clipping, degeneracy, allocation failures, field/state retention and
split-input recovery. No RIP1.54 sources, fixtures or expectations changed.

Native FreeBSD Release CMake configure/build succeeds without warnings. Six
affected CTests pass once (2.41 seconds): rounded rectangle kernel, rectangle
commands, border commands, rounded commands, fill commands and point commands.
No broad renderer/preservation suite is rerun: unchanged legacy hash evidence is
reused, and only the edited ledger consistency is checked. Temporary build logs
are automatically removed. Three published 8-bit vector units are implemented;
ten broader primitive units record their partial evidence and remaining RGB/
color-mask work. The ledger is now 618 implemented, 1552 pending, 4204 unreviewed,
1029 contextual, 24 excluded and 28 blocked among 7455 source blocks. Sector N0
radial behavior, skewed composition/revisits, complete outline font production
and scene integration remain unfinished. No issue content or code was published.

## PR #77 sector fill/border composition

The I/i/_ wire consumers now accept patterned STYLE records and every W mode.
The requested line mask/background-gap setting does not affect their solid
borders. A nonempty region uses W; an enabled border, including closing/radial
segments and a full Pie's internal radius, uses COPY. Equal-endpoint I/i has
no fill area: N1 draws its solid COPY radius and N0 consumes no ink or geometry
allocation. Equal-endpoint chords remain empty. A/V endpoint points retain W.
The old historical unconditional radial LineTo table row is classified as context,
not as a conflicting requirement to draw with N0.

The existing independent sector scene now crosses all five modes with both N
states, plus resolved-color COPY fixtures. It varies solid/custom masks and gap
flags without changing the expected solid border. Eleven angle/topology cases
include equal integer endpoints, equal projected rays/full regions, circular and
noncircular radii, cardinal closures and inverted/raw whole-turn angles. Literal
8-bit PR #77 operands use D=A5, Fill=3C, Back=66 and Draw=0F: fill results are
3C/99/BD/24/5A or 66/C3/E7/24/5A; every enabled outline copies 0F and every disabled
radial-only contour retains A5. A coherent capture covers each screen/offscreen
scene on all canvases, both bases, XY widths and world scales. Geometry oracles
remain independent radius-three annuli and pixel-center/half-plane inequalities.

Error-path tests retain pre-paint allocation/ink failures, verify N0 radial no-ops
without consuming allocation-failure hooks, and exercise valid patterned/XOR
sectors plus recovery after a malformed split-input command. Borders always need
Draw Color even when the fill uses NOT. Existing wide clipped radial and full-
Pie allocation-order checks remain intact. The native CMake project builds without
warnings. Only three affected CTests run, once: sector commands, curve commands
and border ownership; all pass. No legacy or broad renderer tests are repeated.
The edited ledger consistency is checked, while unchanged RIP1.54 preservation
results are reused. Temporary build logs are automatically removed; unrelated
concurrent documentation and the active application route remain untouched.

One published 8-bit vector is implemented, one historical API observation becomes
context and five broader requirements record their new evidence while retaining
pending status for RGB/active-color-mask integration. Totals are 619 implemented,
1550 pending, 4204 unreviewed, 1030 contextual, 24 excluded and 28 blocked among
7455 blocks. Skewed composition/revisits, complete font production, color-plane
integration and full engine acceptance remain unfinished. No existing issue
received content and no code was published.

## PR #77 filled skewed composition

Filled skewed `-`, `+` and `[` no longer reject non-COPY W or patterned line
styles. They use the existing shared even-odd fill with W, then an N-enabled
patterned COPY border with Draw/Back Color and the requested gap policy. The
polygon kernel and contour generator are unchanged. Standalone repeated-turn
XOR/NOT still requires genuine-visit stroking; the union-mask restriction is
now described as pending implementation, not as an unanswered documentation gap.

The skewed scene now crosses both N states with all five modes and resolved-
color COPY on all canvases, screen/offscreen ports, bases, XY widths and world
scales. Original-source Q14 contour literals and independent pixel-center parity
oracles are retained. Additional zero-width whole/chord/pie paths verify patterned
COPY borders independently of W and both gap settings, including retraced edges
and transparent destination retention. Allocation/ink failure tests and split-
input recovery remain covered. The native incremental build succeeds without
warnings; only `rip3_skew_commands` runs for this second change, and passes.
The three just-passed sector/curve/border tests are reused. Six broader ledger
units record partial evidence and retain pending status for RGB/active-color
masks, standalone visits and complete numeric-profile acceptance. Counts remain
619 implemented, 1550 pending, 4204 unreviewed, 1030 contextual, 24 excluded and
28 blocked across 7455 blocks. No legacy source/fixture, active route or reference
pin changes; temporary build logs are removed and no issue content is published.

## Ordered standalone stroke visits

Standalone L/R, P/l, Z/z/t and &/] now retain distinct traversal contributions
instead of collapsing them into a whole-primitive union. The native path API
uses the same collector. Coverage uses the existing geometric round caps: a
continuous component through adjoining endpoint caps is one visit; leaving the
pixel's stroke area and returning creates another. Duplicate vertices add no
interval, closed seams join cyclically, and the documented two-vertex polygon
remains a single line. Exact cap/join pixels are implementation geometry, outside
the abstract protocol matrices. Filled COPY borders and text masks retain their
separate union/aggregation policy.

A bounded per-visible-pixel record holds the first/current component and composed
operand. All geometry/allocations precede painting. COPY keeps the latest visit;
XOR/OR/AND compose source operands and NOT retains visit parity. Foreground wins
only inside a joined component. Holding the first component until the closed seam
is resolved avoids both an extra seam write and losing later COPY source order.
The repeated-turn skewed XOR/NOT rejection is removed. Parser entry points remain
void; allocation and syntax failures stay nonfatal.

New `rip3_stroke_visits` expectations independently cover PR #77's 8-bit once/
twice vectors, crossings, adjacent backtracking, subdivisions/duplicates, closed
seams, two complete turns, thick ordinary joins and mixed foreground/background
COPY order. Failure cases cover allocation, invalid vertices/counts and inactive
no-allocation paths. One coherent capture per canvas combines screen/offscreen
split-input l/P/Z/&/] scenes, cursor retention and malformed/allocation recovery.
Bézier peaks use independent Bernstein arithmetic; skewed cardinal checks compare
one closed revolution with raw 0..719's genuine 90/450 visits. Historical crossing
and retrace expectations change under the adopted protocol; wide clipping still
has its independent single-pass slope oracle. No expectations were generated
from the emulator.

Native CMake configure/full build succeeds without warnings. Eight affected
CTests pass: visits, native paths, native drawing, STYLE rendering, drawing/path/
Bézier/skewed commands. Only new/changed or initially failing tests were rerun;
passing evidence was reused. Ledger consistency is checked without repeating the
unchanged legacy freeze. Temporary build logs are removed; the active route and
RIP1.54 sources/data are unchanged. Three logical traversal/vector units become
implemented and two broader units retain pending RGB/color-mask work. Totals are
622 implemented, 1547 pending, 4204 unreviewed, 1030 contextual, 24 excluded and
28 blocked across 7455 units. Font production, complete color-plane/numeric-
profile acceptance and full engine integration remain unfinished.

## Outline-font catalogue and realization

The separate engine has a stateless outline-font realization service. It resolves
all 80 bundled style names case-insensitively, preserving suffixes and spaces,
and retains validated immutable RFF views with the selected face. Filename aliases,
trimming and default-font fallback are not inferred. Missing/invalid requests
leave the previous result unchanged. Binding externally owned RFF bytes reopens
the validated container instead of trusting mutable view metadata; byte ownership
and lifetime remain explicit. No ATF cache file or mutable global registry is needed.

Point sizes 1..4095 derive independent integer X/Y DPI by truncating 72 times
the client/world ratio before calculating font-unit raster scales. Orientation
selects X or Y DPI for hollow realization. The RFF decoder now exposes the two
metrics-block strength bytes; shipped values 4/2 and a modified 6/3 fixture feed
the prescribed low-WORD arithmetic and bold/normal caps. The resulting nominal
stamp diameter stays in device pixels, independent of the horizontal face scale.
These are prepared font values; ordinary T/@ outline placement/rendering and
cached font-zero metrics still require integration.

Native CMake configure/build succeeds without warnings. Only two affected CTests
run, once: new `rip3_outline_font` and the changed RFF decoder's `rip3_rff` test.
Coverage includes all catalogue names/faces, DEFAULT Ex's 140%, published 72-DPI
stamp cases, anisotropic orientation, BOLD/ITALIC, world-frame extremes exposing
WORD narrowing, invalid/missing requests, forged view metadata and retained prior
results. No existing text/renderer/preservation suite is rerun. Edited ledger
consistency is checked, with unchanged legacy hash evidence reused; temporary
build logs are removed. Ten formula/catalogue units become implemented, two
preambles become context and two broader text requirements retain pending
production work. Counts are 632 implemented, 1548 pending, 4191 unreviewed,
1032 contextual, 24 excluded and 28 blocked across 7455 units. Original fonts,
RIP1.54, the active application route and reference pin remain unchanged.

## Independently positioned outline glyphs

The outline geometry kernel now accepts one device-space origin per input byte.
Glyph-local transforms and horizontal face scaling do not rotate, scale or
otherwise alter those supplied origins. Layout can therefore advance an upright
string upward, or rotate its glyphs while advancing rightward. This entry point
does not reapply raw advances or pair kerning after placement. Missing and damaged
glyphs contribute no ink, while fractional positions remain available to the
rasterizer. The existing raw-font-unit text entry point shares the same geometry,
coverage and painting implementation.

Shadows use an explicit signed device displacement instead of a fixed down/right
pixel. One displaced background copy is collected independently of glyph rotation;
foreground takes precedence in overlapping coverage. Clipping either copy does
not suppress the other. All validation, geometry and mask allocation still precede
painting. Invalid later glyphs, overflowing shadow geometry and failures at every
local allocation preserve the entire destination. Neither entry point changes the
graphics cursor or caller-owned layout/font data. These remain geometry services;
ordinary command metrics, zero-baseline alignment, spacing and shadow-distance
conversion are pending.

Native CMake configure/full build succeeds without warnings. The single affected
CTest, `rip3_outline_text`, passes once. Its independent rectangle bounds cover
all sixteen string-direction/character-rotation pairs in one coherent capture per
canvas. Additional checks cover four shadow directions across character rotations
and all five write modes, foreground/shadow and glyph/glyph overlaps, fractional
placement, missing/damaged codes, viewport offsets, clipped solid/hollow shadows,
cursor preservation, inactive allocation avoidance and each geometry/fill/shadow
allocation failure. Existing outline coverage, analytic curves and all bundled
faces remain in that affected test. No unrelated passing suite or legacy freeze
was repeated; edited ledger consistency alone is checked. Temporary build logs
are removed. Seven rotation/shadow units move from unreviewed to pending, with
explicit kernel evidence and remaining command work; two table headers become
context. Counts are 632 implemented, 1555 pending, 4182 unreviewed, 1034 contextual,
24 excluded and 28 blocked across 7455 units. Reference pin, original font assets,
RIP1.54 and active application routing are unchanged.

## Outline baseline, spacing and shadow layout

The native outline layout service consumes stored STYLE values and the current
frame. String direction is a device-axis unit vector; character rotation stays
independent and selects the original font-instance mapping 0/3/2/1. Baseline
displacement applies the documented sixteen-case branch table, including its
ignored cases and reversed half-turn Y offset. Flag 0x100 reverses the stored
magnitude. Definition-time flag 0x200 does not rescale it again, including after
world-frame changes. These are additions to an already aligned zero-B origin;
font metric acquisition and zero-baseline alignment precede this operation.

Shadow distance uses the current canvas/world X ratio on both device axes,
truncates, and enforces minimum displacement one for positive wire distances.
The direction flags select signs independently of both rotations. Device advance
conversion truncates signed 1/1440-inch font metrics at the explicitly selected
DPI. Spacing then adds the per-glyph or constant font-bbox extra gap, truncating
negative adjustments toward zero. The bbox is already in device units. Signed
wide accumulation supports large advances and gaps without intermediate native
int overflow; output positions retain fractions. A complete preflight preserves
all positions and the returned layout advance on failure. That advance is not
the cached whole-string metric used by cursor stores.

The changed outline-text scenes now consume prepared policy and positions. One
coherent capture per canvas checks all sixteen rotation pairs with signed branch
offsets and P125/bbox spacing against literal rectangles. Four-direction shadow
scenes consume the prepared displacement through every character rotation and
write mode. New `rip3_outline_layout` checks every baseline branch under all four
justification selectors and both signs, frame changes, anisotropic X-only shadow
scaling, zero/minimum/maximum distances, published spacing and original F metric
examples, negative gaps, signed metric boundaries, wide spacing arithmetic and
unchanged output on errors. Native CMake configure/full build succeeds without
warnings. Only the two affected CTests run; after adding signed boundary and
prepared-shadow integration cases, only those changed tests are rebuilt/repeated.
Both pass. Other passing evidence and the unchanged legacy freeze are reused.

Ten baseline/spacing/shadow formula units become implemented; broader outline
font/command units stay pending. Two table headers become context. Counts are
642 implemented, 1552 pending, 4173 unreviewed, 1036 contextual, 24 excluded and
28 blocked across 7455 units. Font metric acquisition, glyph-matrix production,
zero-baseline alignment, cached cursor metrics and ordinary outline T/@ remain
unfinished. Reference pin, original font assets, RIP1.54 and active application
routing are unchanged. Temporary build logs are removed.

## Prepared-font glyph rendering

The separate engine now renders already positioned bytes through a realized-font
entry point. It constructs the glyph matrix from independent character quarter
turns and the font's world-derived X/Y DPI scales: font coordinates rotate first,
then device-axis scaling applies. The existing kernel applies the selected face
percentage exactly once. Layout origins, additive baseline positions, gaps and
device-axis shadow displacements are not transformed again. Realized hollow
width and nonzero interior fill are selected by production code. Font-instance
orientation must match the layout; invalid sizes, flags, DPI, scales and orientation
fail before painting. Solid bold, italic and decorations remain unfinished and
are explicitly rejected rather than silently omitted. Plain solid/hollow and
bold hollow use the prepared-font path.

Native scene tests no longer supply their own glyph matrices. One coherent capture
per canvas/DPI profile covers all sixteen rotation pairs, signed baseline offsets
and bbox spacing against analytic rectangles. A second profile doubles device X
scaling while retaining Y, checking axis order and single face scaling without
moving the supplied origins. Prepared-font scenes also cover all four shadow
directions across character rotations and write modes. Focused cases establish
nonzero winding, custom-font normal/bold hollow width 1/2, retained cursor, rejected
pending effects without allocation, invalid realizations and allocation-failure
atomicity. The original raw geometry tests remain in the affected CTest.

Native CMake configure/full build succeeds without warnings. Only the changed
`rip3_outline_text` CTest runs, once, and passes. Existing font and layout evidence
is reused. Edited ledger consistency alone is checked; the unchanged legacy freeze
and unrelated passing suites are not repeated. Six sizing/rotation units become
implemented, while broader font selection/ordinary text requirements retain their
pending integration work. Counts are 648 implemented, 1546 pending, 4173 unreviewed,
1036 contextual, 24 excluded and 28 blocked across 7455 units. Metric acquisition,
zero-baseline alignment, cached whole-string cursor metrics, effects and ordinary
outline T/@ remain unfinished. Reference pin, original font assets, RIP1.54 and
active application routing are unchanged. Temporary build logs are removed.

## Global RFF metric-label conflict

Read-only byte inspection found that the four metrics words +0x16..+0x1C equal
the union of framed glyph-record bboxes in all eight original RFF files. Their
observed order is xmin, ymin, xmax, ymax. The pinned table instead labels +0x1A
as ascender/bbox ymax and +0x1C as a width metric; corresponding RFF header labels
also assign the apparent horizontal/vertical bounds incorrectly. For Marin the
four words and glyph union both are (-78,-916,1059,250). The existing original
90-point/72-DPI oracle's device bounds (-7,-22)..(95,82) support that interpretation,
without establishing exact raster rounding or signedness for every field.

New parent-repository [issue #82](https://github.com/bbs-land/remote-imaging-protocol/issues/82)
records all eight comparisons, a read-only reproduction and the requested consumer
confirmation/correction. It references #3/PR #29 and #22/PR #55; none of those
existing issue threads was modified. A deduplication read found no existing bbox
field issue. The labels still occur at current stable head a52985019cbf4754d4d08a07b4f7755761390661;
the local normative pin remains 72edbfc6c151d86a120fec3cabbde3cad32c9c0b. That newer
stable head also contains PR #80/#81 for the recorded border/style questions;
their reference adoption is separate pending work.

Five in-scope RFF/header source units become blocked by B82. Two FF1 predecessor
rows remain contextual with an explicit cross-reference. Metric acquisition must
not treat a global X maximum as typographic ascent or the Y maximum as font width.
The newly committed prepared-font renderer does not consume these disputed words,
so its verified geometry is unaffected. Counts are 648 implemented, 1545 pending,
4173 unreviewed, 1032 contextual, 24 excluded and 33 blocked across 7455 units.
Only the edited ledger consistency is checked for this documentation update;
native source and existing passing tests are unchanged. No temporary files,
font changes, RIP1.54 changes or active-route changes are introduced.

## PR #80 and PR #81 adoption and ordered STYLE diagnostics

The reference now pins fork stable a52985019cbf4754d4d08a07b4f7755761390661,
including PR #80 head ca75dd486c3b7099c7c127edc29170a841396d1c and PR #81 head
35fd9731bb8686d8830ee0bd21fb8d67ba856c5a. The source/adoption record contains the
archive digest, all 67 document hashes, two new derivation identities and update
accounting. Four normative pages change. Heading/content matches preserve 7445
reviews, 68 line references move, and 35 new/replaced blocks are adjudicated.
The queue contains 7480 blocks. B78/B79 receive adopted documentation resolutions;
the independent RFF bbox-label question B82 remains open.

N retains current-base parsing, last-field shortening and zero/nonzero Boolean
storage under current STYLE protection. This follows the explicit protocol rule
despite the conflicting native fixed-MegaNum descriptor. Its existing normalization
code already matches the adopted contract. Tests now include shortened N2 and a
protected nonzero update, alongside N02/NZZ/N&&, ownership, backup and rendering.

2Y retains one current-base style digit and up to two flag digits. Source requests
4/8 precede selection/defaulting; destination requests 1/2 follow it. Higher bits
are ignored, protected destinations select normally, and destinations >=36 skip
all changes. The previous single status result collapsed source/destination
protect-zero diagnostics. Each zero-protection request now produces its own
nonfatal observation at the point it occurs; remaining requests and selection
continue. Optional observations do not control behavior, and processing remains
void. Callbacks may inspect state but may not reenter or mutate bound objects.

Published decimal cases 12/3/6/9/4/5/15/16 have independent state checks in both
bases. The tests distinguish a source-16/destination-zero single diagnostic from
source-zero/destination-zero's two, inspect current index and source protection
during observations, and repeat transitions without a diagnostic sink. Split-input
scenes on all three canvases recover after two zero-protection diagnostics and a
malformed command, retaining coherent pixel/cursor/encoding expectations. Inactive
viewports still permit STYLE switching. The separate global disabled-graphics
gate remains pending session integration; its combined domain/suppression unit
is not claimed implemented.

Native CMake configure/full build succeeds without warnings. General command and
border CTests pass once. The style CTest passes after correcting its new test's
source-slot setup expectation; only that failed/changed test is rebuilt/repeated.
Three focused reference/adoption guard cases and edited ledger consistency pass.
Unchanged font, drawing and legacy preservation evidence is reused. Counts are
669 implemented, 1545 pending, 4173 unreviewed, 1039 contextual, 24 excluded and
30 blocked. The archive/source data were processed in memory; scoped build logs
and adoption-test temporary directories are removed. No existing upstream issue,
original font, RIP1.54 source/fixture or active application route is changed.

## Shared ordinary-variable argument reconciliation

Before binding text expansion, the shared dollar-reference decoder is reconciled
with the adopted #6/PR #32 argument contract. Its slots now hold twenty original
bytes plus NUL, with at most forty nonempty arguments. Empty comma fields are
omitted, including trailing fields after forty real arguments; raw spaces remain
part of the decoded argument. Empty parentheses differ from a space-only argument.
Quoted-empty input is rejected. All validation precedes replacement of the decoded
result. Parser statuses remain private to the public void recovery boundaries.

Existing numeric/keyword handler families normalize their own bounded copies.
This retains their established spaced-keyword behavior while allowing future
literal/path handlers to receive original text. Empty normalized words still
occupy their original slots. TERMINFO and viewport keyword buffers now accommodate
the full twenty-byte slot, and normalizer bounds guard copied fields. Comma
omission changes cases such as DVP(2,), PALENTRY(,CUR,,15,) and TERMINFO(,): these
now reach handlers with compacted arguments. Thirteen/twenty-byte zero-padded D
durations and wide nonnegative BAUDEMUL values are no longer rejected merely by
the old twelve-byte ceiling; callback numeric bounds still apply.

The existing text-command expansion precondition remains explicit; this decoder
milestone does not claim complete expansion or prompt integration. Additional
read-only inspection found a separate argument-byte predicate and backslash branch
whose full composition is not specified by the current reference. New parent
[issue #84](https://github.com/bbs-land/remote-imaging-protocol/issues/84) requests
that byte/escape/quotation contract, referencing #6 and PR #32 without modifying
their existing threads. B84 does not reopen the delivered count/size/empty/space
answers. PR #83 now delivers the independent B82 bbox-field answers; adoption is
pending separately, with the local normative pin unchanged in this milestone.

Native CMake configure/full build succeeds without warnings. All twenty direct
consumer CTests pass. Only tests with changed/new or initially failing cases were
rebuilt/repeated; sixteen initial passes were reused. Cases cover raw space slots,
empty compaction, 20/21 bytes, 40/41 nonempty arguments, trailing empties at capacity,
quoted-empty rejection, preserved decoder outputs on errors, long keyword buffers,
callback range rejection and valid-reference recovery. Old invalid expectations
based solely on the twelve-byte limit/retained empty fields are corrected against
the adopted source; a new palette fixture now queries the intended literal white
entry. No expected image is derived from the emulator. Unchanged drawing/font/
legacy evidence is reused; only edited ledger consistency is newly checked.

The broad argument block records implemented subrules but remains blocked for
B84's complete lexical contract; the name-case unit becomes implemented and its
provenance becomes context. Counts are 670 implemented, 1544 pending, 4171
unreviewed, 1040 contextual, 24 excluded and 31 blocked across 7480 units. Scoped
build logs are removed. No existing issue, original font, RIP1.54 source/fixture,
active application route or external-launch policy is changed.

## PR #83 adoption and realized font bounds

The reference now pins fork stable f77f918229934bfc4560860e044c48aaf86e28b4,
including open PR #83 head e8591c1af34e0e47aceef354c6d80e095d72978f. The archive
digest, 67 document identities and new read-only derivation hash are recorded in
the adoption ledger. Three normative documents change; 7466 heading/content
matches preserve their reviews and 70 line references move. Thirty-eight source
blocks are new/replaced, including the expanded global-bounds field map and eight
font tuples. The replaced spacing clause gains fresh implementation evidence.
B82 receives an adopted documentation resolution; B84 remains independent.

The RFF view replaces its misleading ascender/descender names with four signed
xmin/ymin/xmax/ymax coordinates from the validated metrics block. This is the
rectangle consumed by the original engine. Header copies are not substituted
for it, and no additional header-copy consistency policy is imposed. Raw views
can inspect inverted bounds; realization rejects them without replacing the
caller's previous font. All eight original tuples have literal expectations;
custom records test negative upper bounds and signed extrema on both axes.

Font realization transforms all four global corners, applying horizontal face
percentage, independent character orientation and device-axis DPI scaling. DEVICE
edges remain y-down and use exact rational nearest rounding with half ties positive.
This is the geometric implementation's explicit edge policy, not a claim of native
hinting or pixel reproduction. Wide signed edges preserve legal custom-unit/world
realizations. Spacing's bbox reference is xmax-minus-xmin after this realization,
without an inclusive-pixel increment or substitution of glyph advance. A zero
span remains zero. The current int32 layout consumer rejects an oversized span
without wrapping/changing its output, including forged extreme prepared bounds.
Global bbox edges are not typographic ascent/descent lines or effect-specific ink
extents; ordinary metric/alignment/effect integration remains pending.

The native scene pipeline now obtains bbox widths from realized fonts before
layout. One coherent capture per canvas/DPI profile checks all sixteen direction/
rotation pairs against literal rectangles and spacing arithmetic. The authored
8x8 global rectangle with a 150% face yields widths 12/8 for upright/quarter turns;
P125 adds 3/2 to ordinary advance 8. Doubled X DPI yields widths 24/16 and adds
6/4, while supplied origins and signed baseline additions remain DEVICE values.
Separate font tests cover the published Marin upright width 102, independent
rotation/DPI cases, face scaling, half ties, collapsed zero spans, custom one-unit
em extremes, invalid rectangles and unchanged results on failures.

Native CMake configure/full build succeeds without warnings. Only three affected
CTests run, once: rip3_rff, rip3_outline_font and rip3_outline_text. All pass, as
do three focused adoption guards and edited ledger consistency. Existing argument,
drawing and legacy preservation evidence is reused. Counts are 685 implemented,
1543 pending, 4171 unreviewed, 1055 contextual, 24 excluded and 26 blocked across
7504 units. The active route, original fonts, RIP1.54 and external-launch exclusion
are unchanged. Scoped logs are removed; archive/source processing leaves no
temporary extraction tree. No existing issue thread or code publication changes.

## Unkerned per-glyph metrics and cached-width follow-up

Prepared fonts now provide per-byte DEVICE advances for the positioned text
pipeline. Each declared unsigned RFF advance is scaled by whole points and face
horizontal percentage into 1/1440-inch units, rounded to nearest with positive
half ties upward, then converted separately through X DPI with truncation.
This is an explicit geometric rounding policy, not a claim to reproduce all
native intermediate WORD overflow, hinting or final ordinary effect/kerning
semantics. Character rotation leaves the scalar unchanged; layout separately
chooses the string direction and extra spacing. Pair kerning, alignment and
cached cursor widths are not applied by this unkerned service. Bold/italic
metrics report unsupported until their producers are defined/implemented.

The service uses unsigned raw font codes, gives missing codes zero advance,
retains damaged glyphs' declared advances and publishes its output array only
after every distinct byte succeeds. It allocates no memory and uses a bounded
256-code scratch table with size_t input indexing. Legal custom font/unit/world
extremes fail before partial output when logical or DEVICE int32 limits would be
exceeded. Native expectations include the independently published Marin F
1001-logical/50-DEVICE oracle, 150%/75% face examples, separate X/Y DPI,
all character orientations, half-up-before-truncation, per-byte rather than
whole-string scaling, unsigned high bytes, missing/damaged glyphs, unkerned
pairs, 512-byte inputs and retained outputs on invalid/unsupported requests.

All sixteen direction/rotation scenes on the three canvases now obtain their
advances from the actual prepared synthetic font. Raw advance 6 at 1000 points
and 150% face yields 9 DEVICE units, doubled to 18 with X DPI. P125 adds the
already established bbox gaps 3/2 or 6/4. Literal independent rectangles use
steps 12/11 or 24/22. The scene grid is widened to contain those longer strings;
the first anisotropic capture correctly exposed overlap beyond its former
64-unit cells. No expected image is generated from the emulator.

New upstream [issue #86](https://github.com/bbs-land/remote-imaging-protocol/issues/86)
requests the independent ordinary/cached-width contract and cache lifecycle.
Read-only inspection of the preserved original DLL (SHA256
83d71b358492b15bcfd3773c30a52784881a3a49a5b6e9029d986b3cba1fd5e8)
found per-byte conversion/spacing/WORD summation at RVA 0x3fcd0, signed-byte
extension and an eight-bit input index there, unsigned realized-code range
checking at 0x527d0, and rounded integer scaling at 0x54340. These static leads
do not establish all caller limits or failed/stale cache behavior. Original
instructions were not executed. Existing #24 fixtures use controlled widths
13/37 and explicitly do not prove real font measurements. The new issue
references #3/#22/#24 and PRs #29/#55/#57/#83 without adding content to them.
The adopted cursor-store formulas and established classic paths are retained;
unresolved Woutline consumers are accurately marked blocked. The new geometric
service is not silently substituted for their width. PR #85 is recorded as a
proposed, not yet adopted, resolution of the independent B84 argument grammar.

Native CMake configure/full build passes without warnings. Only the two affected
CTests are exercised: rip3_outline_font and rip3_outline_text. The corrected
scene fixture reruns only its affected test, and the additional boundary cases
rerun only the font test. Edited ledger consistency passes; existing drawing,
argument, source-adoption and legacy preservation evidence is reused. Counts
are 685 implemented, 1538 pending, 4167 unreviewed, 1057 contextual, 24 excluded
and 33 blocked across the unchanged 7504 units. The pin, original assets, RIP1.54,
active application route and external-launch policy are unchanged. Scoped build
logs are removed; no reference extraction, probe or temporary issue body remains.
No code is pushed or PR published. The implementation goal remains in progress.

## PR #85 adoption and argument-local decoding

The reference now pins fork stable 5224b709268fbda7d38a4c40cd2635374d9a6023,
including open PR #85 head a47aa2d40405a3e156f745736601abe016766a9d. Two normative
documents change; their reviewed additions distinguish dollar-argument quoting
from outer ordinary/host passes. The exact archive and new read-only derivation
hashes are recorded in the adoption ledger. The block index retains 7502 exact
source/heading/content reviews, moves 1831 line references and adds/replaces 42
blocks, yielding 7544. No implementation claim is silently carried across changed
text. B84 is resolved in the adopted documentation; B86 remains independent.

The shared exact-reference decoder now enforces the published unsigned payload
alphabet. Double quotes, controls and excluded punctuation reject even following
a backslash. Opening parentheses remain data. The first unquoted closing
parenthesis ends the argument list. A local quote prefix occupies the next slot,
then its accepted following byte overwrites it without increasing decoded length.
This permits a literal dollar/right-parenthesis, rejects consecutive backslashes
and quoted commas, and preserves single quotes, case, raw spaces and high bytes.
The 20-byte limit counts decoded bytes; forty nonempty argument slots still omit
empty comma fields. Result publication remains transactional. Handler-specific
numeric/keyword normalization remains separate, and public processing absorbs
all syntax failures without returning a session error or invoking a handler.

Independent native expectations cover all 256 byte values both raw and locally
quoted, the published 208 plain/210 quoted representable payload values, every
distinguishing lexical example and pending-quote failure. Twenty quoted bytes
occupy forty source bytes; a twenty-first decoded byte rejects. Forty maximum
decoded arguments and following empty fields fit; a forty-first argument rejects.
CP437/high bytes are retained, and ten two-byte UTF-8 encodings fill the same
twenty-byte limit. Public recovery rejects malformed CUR references without
changing palette selection, then processes a valid locally quoted keyword and
replies from the new selection. No original client/parser code is executed.

This implements the argument stage, not the complete expansion pipeline. General
post-argument user/prompt grammar, outer backslash processing, host two-pass
replacement scheduling, macro/control/template expansion and text consumers
remain pending. Their new source blocks are explicitly pending; table labels
and provenance are contextual. Existing #6 answers remain adopted.

Native CMake configure/full build succeeds without warnings. All twenty direct
consumer CTests pass once; three focused reference-adoption guard cases and the
edited ledger consistency check pass. Unchanged font/drawing and legacy freeze
evidence is reused. Counts are 711 implemented, 1545 pending, 4166 unreviewed,
1066 contextual, 24 excluded and 32 blocked across 7544 units. Original assets,
RIP1.54, the active application route and external-launch exclusion are unchanged.
Reference processing stays in memory and scoped build logs are removed. No
existing issue thread is modified, and no code is pushed or PR published.

## RAND production and user-variable policy follow-up

RAND now executes through the public void host-reference boundary. Its bound
state belongs to one session thread and is explicitly initialized by the client;
the protocol does not promise an initial seed or a cross-session sequence. The
service advances the original 32-bit CRT recurrence and extracts its 15-bit draw,
without using or reseeding process-global libc state. The active application
adapter has not yet bound this service, so full stream/command integration is
still pending.

One to three nonempty raw digit-only arguments are converted modulo 2^32 before
the adopted range checks. Conversion handles twenty-digit tokens beyond uint64_t
without overflowing signed C. Maximum must be 1..32767; padding width and minimum
use the documented signed comparisons. The range product wraps to 32 bits before
signed division by 32767, and the final addition wraps too. Explicit unsigned
arithmetic and signed-representation decoding preserve these byte-visible
compatibility rules without implementation-defined narrowing or signed overflow.
The inclusive-span endpoint returns maximum+1. Formatting prepends zero bytes
to the complete signed number, including its minus sign; it never truncates to
the requested width. Missing provider state is reported unsupported, and every
syntax/range/capacity failure preserves both caller output and generator state.

Independent native vectors cover the original seed-one sequence, isolated session
states, modularly solved zero/midpoint/maximum draws, normal endpoint/padding,
20-digit wrapping, negative wrapped width/minimum, signed multiply/add overflow
and padding before a minus sign. Omitted fields and a locally quoted digit pass
through the new argument decoder. Raw CP437 and UTF-8 nondigits reject. Malformed
references diagnose nonfatally without advancing the generator, followed by a
successful reference. Private capacity cases reject before state/output changes;
five digits plus their terminator fit exactly. Tests exercise no original DLL,
desktop, external transmission or physical audio.

New upstream [issue #87](https://github.com/bbs-land/remote-imaging-protocol/issues/87)
isolates the earlier #6 user-variable questions that remain unspecified despite
the adopted PR #32/#71 macro answers. The current section says `%` and one use of
each directive, but also endorses `$-=NAME=value$`; retained draft prose says
repetitions are ignored and persistent scope wins. Its transparent prompt/default
and cancellation cases also need an explicit protocol choice. The #6 derivation
uses a controlled user-variable provider and does not independently prove those
policies. A read-only original prefix-classifier lead recognizes `=`; no client
instructions or prompt path were executed. The new issue references #6's existing
comment and PRs #32/#71/#85 without adding content to them. Completed evaluation,
built-in and argument-local contracts remain adopted. Only the specific disputed
rows are blocked under B87; the existing six-bit syntax decoder retains unsupported
prefix/repetition behavior without inventing scope or prompts. The broad earlier
B6 resolution is retained with an explicit follow-up link.

Native CMake configure/full build passes without warnings. The new rip3_random
test and affected rip3_host_variables/rip3_host_recovery cases pass. Added CP437
and exact-capacity boundaries rerun only rip3_random. Edited ledger consistency
passes; prior argument-byte, font/drawing, adoption and RIP1.54 evidence is reused.
Counts are 718 implemented, 1536 pending, 4155 unreviewed, 1068 contextual,
24 excluded and 43 blocked across the unchanged 7544 units. The reference pin,
original fonts, RIP1.54, active route and external-launch exclusion are unchanged.
Scoped build logs are removed. No existing issue thread is modified; no code is
pushed or PR published. Full RIP3 implementation remains in progress.

## Typed conditional comparisons and staged reduction

The condition service now implements the value and Boolean-reduction stage needed
by angle preprocessing. It consumes typed operands/operators after source lexing;
it is not a public stream/session parser. Six comparisons select signed numeric
ordering only when both complete effective values contain ASCII decimal digits
with an optional leading minus and at least one digit. Conversion wraps modulo
2^32 without signed C overflow. Other values use byte-string ordering with ASCII
case folding and unchanged high bytes. Quotes do not select a comparison mode;
the future lexer supplies already decoded literal bytes. A C-string NUL ends the
effective operand, and returned dollar text is not recursively interpreted.

The service validates the entire typed operand/operator/group structure before
allocation or provider effects. Inactive parents can run validation alone with
no provider call/allocation. Active evaluation prepares its private scratch copy,
then resolves every comparison left to right without Boolean short-circuiting.
Failed or unavailable value providers make their comparison false, including !=.
Provider bytes are borrowed only until their next call and are compared before
that call. Tokens/bindings remain unchanged; validation and allocation failures
preserve the result, with no provider side effects. Parser owners must absorb
these internal helper statuses at their existing nonfatal boundaries.

Reduction repeatedly handles adjacent Boolean/AND-or-OR/Boolean triples from
left to right, then NOT/Boolean pairs, then grouped single Booleans. Binary
reduction revisits its collapsed position; NOT does not move backward to an
earlier NOT before the next binary phase. Group reduction advances its scan.
Read-only original reducer instructions at RVA 0x4bf60..0x4c141 confirm those
phase/cursor details, consistent with the adopted derivation. No original code
is executed. This preserves equal AND/OR precedence and the shipped late-NOT
behavior instead of conventional expression precedence.

Independent native vectors cover every comparison/operator, signed wrapping,
twenty-digit values, numeric versus lexical whitespace/plus/empty cases,
CP437/UTF-8 byte order and C-string termination. Typed equivalents of all three
published N=1 precedence examples and additional multiple-NOT/group boundaries
have literal expectations. A 128-level grouped case uses iterative validation
and reduction. Provider fixtures prove ordered non-short-circuited calls,
scratch-value lifetime and failed != behavior. Deterministic allocation failure
occurs before any provider invocation; malformed complete structure/operands
reject before allocation, and a following valid evaluation succeeds.

Native CMake configure/full build succeeds, and the new rip3_condition CTest
passes. GNU make reported clock-skew warnings during the build. The new objects
and executable postdate their source files, and a subsequent timestamp inspection
found no remaining future-dated build files; the focused test exercises those
new artifacts. No redundant compiler matrix or prior test suites are rerun.
Edited ledger consistency passes; unchanged RAND/argument/font/drawing and legacy
evidence is reused. Counts are 719 implemented, 1546 pending, 4140 unreviewed,
1072 contextual, 24 excluded and 43 blocked across 7544 units. Source lexing,
IF/ELSEIF/ELSE/ENDIF branching, angle/template substitution and consumer routing
remain explicitly pending; helper tests do not claim end-to-end preprocessing.
The reference pin, original fonts, RIP1.54 and active application route remain
unchanged. Scoped logs are removed, no issue thread is modified and no code is
published. The full implementation goal remains in progress.

## PR #88/#89 adoption, ordinary metric axes and actual user prefixes

The reference now pins fork stable ab37e09a1d239f21ae5d748c5b6cddbea9522a20,
including open PR #88 head 9962a60dce7df526cfe42051d70757f3b43a9172 and open
PR #89 head 3240a25d2026d4be3275f066b898a07192884c54. Seven existing normative
documents change and the outline advance/cache page is added. Reindexing retains
7480 exact source/heading/content reviews, moves 2093 line references and reviews
151 new/replaced blocks, giving 7631 across 68 documents. No prior implementation
claim is silently retained across changed text. Both derivations have recorded
hashes; their native evidence is distinguished from explicit protocol policies.
B86/B87 are resolved in the adopted documentation, independently of implementation
acceptance. Existing issue threads receive no added content.

Ordinary outline metrics now select effective DPI by string axis: X for 0/1800,
Y for 900/2700. Character rotation remains independent. Rational face/point scaling
rounds to 1/1440-inch units per byte before device truncation. Logical intermediates
are uint64_t through conversion, removing the unnecessary int32 intermediate
restriction; a custom 5,367,316,500-logical-unit metric now produces the valid
3,727,303-device-unit result at one DPI. The existing bounded int32 layout input
still rejects oversized final device values atomically. Synthetic bold/italic/
decorations no longer reject metric acquisition or alter widths; their ink
renderers remain separately pending. No ordinary pair kerning is applied.

Native font vectors cover all sixteen string/character rotations and every low
effect combination for base and 150% hollow faces at unequal DPI. MARIN F at 90
points gives ordinary widths 50/25 for Dx=72/Dy=36, and the hollow 150% face gives
75/37. Real unsigned bytes 0x80/0xff give 15/90 at 72 DPI. FF at 10 points and
36 DPI gives two separate advances of 2, rather than whole-string width 5.
Coherent captures on every canvas now use string-axis advances but retain the
horizontal realized bbox reference for spacing. With doubled X DPI, horizontal
steps are 24/22 and vertical steps 15/13; isotropic steps remain 12/11.

The user-variable decoder now recognizes actual session prefix `=` and rejects
`%`, correcting the prior help transcription. Repeated directives are accumulated
idempotently instead of reporting unsupported. Prefix storage and post-name
default equal signs remain distinct. All 64 flag subsets in both directions with
tripled occurrences, canonical mixed/silent forms, absent versus empty defaults
and invalid percent combinations have independent expectations. The decoder
retains persistent/session bits for the future executor, where `+` must win; it
does not perform prompts or storage. PR #89's complete sixteen-row effect matrix,
memory-first shadow/copy policy, successful-empty NULL and cancellation behavior
remain pending execution work, with their explicit policy boundaries recorded.

The cache contract now completely specifies unsigned finite X-DPI snapshots,
final signed-WORD cursor handoff and atomic lifecycle/failure retention. Those
are adopted pending requirements; no cache or cursor consumer is inferred from
the completed ordinary-metric service. Earlier source grammar/condition work also
remains pending and is not replaced by this reference update. New upstream
[#90](https://github.com/bbs-land/remote-imaging-protocol/issues/90) requests the
conditional operand byte/escape/terminator/limit grammar that the existing macro
and argument answers do not specify. Read-only original operand-parser leads
show distinct quoting and length paths; no original code is executed. Its report
references completed #6/PR #32/#85/#89 without adding to their issue threads.
Only that separate lexical requirement is blocked; established comparison and
Boolean semantics remain adopted and implemented.

Native CMake configure/full build succeeds. The only build warning class is GNU
make clock skew; no compiler warning/error is emitted. The three affected native
CTests pass once: rip3_variable_reference, rip3_outline_font and rip3_outline_text.
Three focused reference-adoption guards and edited ledger consistency pass;
unchanged condition/RAND/argument/legacy evidence is reused. Counts are 737
implemented, 1625 pending, 4122 unreviewed, 1097 contextual, 24 excluded and 26
blocked across 7631 units. The original fonts, RIP1.54, active application route
and external-launch exclusion are unchanged. Archive processing stays in memory;
scoped build logs are removed. No code is pushed or PR published. Full compliance
is still in progress.

## Owned outline snapshots and classic font-zero cursor widths

The logical cache service now owns a complete 256-code metric snapshot per text
context, shared across its ports and styles. It copies the rounded logical map,
preparation-time X DPI, spacing, bbox flag and realized horizontal span; it keeps
no font-file or backend bitmap pointers. Preparation validates every field and
step before replacing the old snapshot. Failure retains the complete last good
state. Creation/reset yields invalid zero width, and disposal belongs to the
context owner. Ordinary outline commands and full drawing-reset orchestration
still need to call these services at their specified lifecycle boundaries.

Cached measurement reads unsigned codes, stops at a bounded C-string terminator,
uses the snapshot's X DPI for every direction and includes each final spacing
step. It has no 255-byte limit or intermediate WORD narrowing. Checked wide sums
reject overflow atomically, and only the final cursor value decodes the signed
low WORD. RFF metric reads are now independent of outline decoding, so damaged
or undrawable outlines retain their declared advance. Copied metrics survive
destruction of the complete backing font allocation.

Classic font-zero TEXT_XY now reads that snapshot without preparing a font. All
classic TEXT_XY orientations/justifications store raw input anchors plus viewport
origin and the selected metric, while painting uses its separately converted
anchor. Fonts 1..10 retain their own classic width; classic TEXT also retains its
classic metric. Cold font-zero TEXT_XY advances by zero. Successful outline
T/@ preparation, their accepted-call failure/cursor ordering, typographic
alignment, effects and application routing remain pending.

Independent native vectors cover every string/character rotation at unequal DPI,
MARIN F=50, ordinary vertical F=25, raw high bytes, missing-code bbox gaps,
P=125 widths 62/75, negative spacing, 260/700-byte widths and final wrap to
-30536. Failure checks cover invalid fonts, spacing/bounds, failed allocations
and late sum overflow with unchanged outputs. All-canvas CP437/UTF-8 cursor
captures check cold/warm states, raw anchors (105:87, 155:87 and 130:87), stale
metrics after frame/style/port changes, empty/inactive calls, overflow recovery
and a following classic TEXT with its own metric. Existing raster fixtures now
explicitly prepare their independent uniform-width snapshot before drawing.

The native CMake build produced the new artifacts. All six affected CTests pass:
rip3_rff, rip3_outline_font, rip3_outline_text, rip3_outline_cache, rip3_cursor and
rip3_text_commands. The existing GNU make clock-skew warning is environmental;
no compiler diagnostics were introduced. These results are reused without
repeating the passing tests. Edited requirement-ledger consistency passes.
Counts are 747 implemented, 1615 pending, 4122 unreviewed, 1097 contextual,
24 excluded and 26 blocked across the unchanged 7631 units. No reference pin,
original font asset, RIP1.54 source/fixture or active application route changes.
Temporary build logs are removed; no existing issue is modified or code pushed.
The full compliance goal remains in progress.

## Resumable user-variable prompt and storage execution

The user-variable processor now executes PR #89's complete sixteen-row matrix
below macro dispatch and above the existing owned stores. Session lookup precedes
persistent lookup independently of destination flags, including defined empty
values. A supplied default remains distinct from absence. Transparent define
selects its default even over an existing value; transparent retrieve alone
prefers an existing value. The remaining rows expose a pending prompt with an
owned copy of the reference and initial field. No stack command buffer, table
entry or UI pointer is retained across suspension.

The owner supplies two distinct borrowed stores and an optional format/editor
adapter. The adapter receives the copied reference, logical value and its source
(existing/default/accepted response) once before any storage/output. Without an
adapter, ANY/unconverted values work; other modes/conversions reject before a
prompt or write. This milestone implements matrix execution and the adapter
handoff, not the UI/editor's format rules, world-coordinate prompt placement or
mask rendering. The client still owns persistent disk flushing, hang-up cleanup
and macro/query/application integration. The processor itself has no dialog,
network or disk effects.

A successful operation writes only its selected destination; persistent wins
when both destination bits occur. Cross-store copying leaves the other store
and any session shadow intact. No destination means no update, even for an
existing variable. Storage precedes result publication. Transparent define
without retrieve suppresses replacement. Other successful empty values emit
literal NULL while storing actual empty bytes. Cancellation emits NONE only
without either transparent bit and never writes a store; required prompts
remain pending after an attempted user cancellation, but accept empty input.
Forced abort releases pending/result state for EOF/shutdown without a write or
protocol replacement, independently of required-prompt policy.

Processing and responses are void nonfatal boundaries. Busy starts preserve the
pending prompt; malformed typed input and failed formatting/storage diagnose
without publishing a partial value. Source references and initial values are
owned during suspension. Complete results are borrowed until the next operation;
no prior result is exposed after a failed new operation. Deterministic allocation
and store failures test unchanged tables and recovery through the same processor.
Response bytes truncate to 255 while preserving explicit CP437/NUL byte spans;
the enclosing macro/framing stages retain their separate C-string policies.

Independent native tests exercise all sixteen literal rows across four storage
selections, four required/masked combinations and three lookup/shadow locations:
768 combinations. Additional checks cover successful-empty/default/cancellation
and literal-sentinel distinctions, required transparent calls, copied dialog
metadata, suspended source lifetime, busy protection, adapter handoff, long/raw
responses, failed factory/format/store operations, resumed-prompt write failure,
forced abort and following successful operations. These are processor tests;
they do not claim GUI, durable disk or complete macro/query execution coverage.

While preparing ordinary outline command integration, the adopted documents
leave the zero-B origin relative to the command anchor unspecified. Existing
additive displacement tables and advances do not identify the typographic
origin or selector-specific subtraction for independently rotated strings and
glyphs. New upstream [#91](https://github.com/bbs-land/remote-imaging-protocol/issues/91)
requests that focused placement contract and independent asymmetric vectors,
referencing answered #3/#22/#24/#86 and their PRs. Their existing field, raster,
metric and cursor answers remain adopted. No existing issue receives comments;
only the additional placement obligation is blocked.

Native CMake configure/full build succeeds without compiler or build diagnostics.
The new rip3_user_variables CTest passes, including the resumed error/recovery
addition. Passing unrelated suites are reused. Edited ledger consistency passes.
Counts are 782 implemented, 1580 pending, 4121 unreviewed, 1097 contextual,
24 excluded and 27 blocked across the unchanged 7631 units. The reference pin,
font assets, RIP1.54, active application route and external-launch exclusion are
unchanged. Scoped build logs are removed; no code is pushed or PR published.
The complete RIP3 compliance goal remains active.

## Immediate queries with resumable user-variable dispatch

The immediate evaluator now routes unknown decoded user references to the
optional session-owned processor. It recognizes known built-ins first; invalid
arguments or unsupported built-ins do not enter user lookup or prompt over a
same-named stored value. Known names with unsupported extended reference syntax
reject during full-expression preflight. P is removed from the predefined
registry as the adopted documentation requires: `$P$` can prompt/retrieve a user
value and performs no paste. PCB remains the executable paste name. The pinned
inventory guard now explicitly checks this exclusion rather than adding P from
an earlier superseded help interpretation.

The query owns its immutable expression across prompts as it already did across
D delays. Its active-user pointer identifies only an operation started by that
query; a prompt at the last token still keeps the query pending. The common
source prepare/pump path waits without reading subsequent RIP or raw bytes.
After a response, the completed replacement is emitted exactly once without
rescan in this ordinary pass. A resumed result consumes one work-budget token,
including suppressed output. Required cancellation retains the pending prompt;
forced query cancellation/EOF aborts only query-owned state, preserving another
consumer's prompt and leaving the independent input-delay gate unchanged.
A value already accepted into a store is not rolled back by later query abort.

Whole-expression preflight still precedes output or storage. Unsupported tails
cannot partially apply an earlier silent assignment, and an engine already
awaiting another consumer's response cannot be captured by a new query. User
format/storage errors are consumed at the void processor boundary; following
references continue. Query processing itself returns no session error. The
client must keep the borrowed user binding stable and exclusive while pending,
supply dialog/editor behavior and coordinate UI/source shutdown.

Native query/source fixtures on all three canvases cover silent assignment,
retrieval, final prompts, caller-buffer reuse, required/empty/normal/transparent
cancellation, literal replacement bytes resembling controls/callbacks, delayed
prompt activation, blocked source reads, later radix changes and CP437/UTF-8 raw
input. Recovery cases cover unavailable formatting adapters, busy ownership,
unsupported tail atomicity, known-name precedence, excluded launch callbacks,
query-owned abort and pending destruction. No original client or GUI is run.
Full host two-pass processing, angle/template/playback dispatch, complete control
handling within references, dialog presentation, deferred queries and active
application routing remain pending; this does not claim complete query coverage.

Native CMake configure/full build succeeds without compiler/build diagnostics.
The four affected CTests pass: rip3_query_commands, rip3_host_variables,
rip3_builtin_catalog and rip3_builtin_inventory. Earlier matrix/font/legacy
results are reused. Edited ledger consistency passes. Counts are 784 implemented,
1580 pending, 4117 unreviewed, 1099 contextual, 24 excluded and 27 blocked across
7631 units. The documentation pin, original fonts, RIP1.54 and active application
route are unchanged. Temporary logs are removed; no issue thread or remote code
is modified. The full implementation goal remains active.

## Two ordinary host passes and corrected ASCII controls

Immediate host responses now complete exactly two ordinary expansion passes
before transmission. The evaluator accumulates each pass in owned bounded-growth
storage, moves the completed first buffer into immutable second-pass input, and
only publishes the completed second buffer. Producing-pass insertions are skipped;
values introduced in pass one can expand in pass two, but second-pass results
receive no third scan. Angle and bracket stages do not restart on replacements.
This replaces the earlier intermediate streaming-output behavior, including its
incorrect transmission of a prefix before a later D in the same response.
A final delay now also suspends transmission and the subsequent source stream.

Pass one retains outer backslashes and control notation while applying the
dollar parser's own local argument quoting. Pass two removes an outer quote and
skips its next byte as literal, then performs the documented ASCII control-pair
conversion. Quoting only an opening dollar still permits first-pass lookup;
quoting both dollars transmits a literal reference. Quoted argument dollars are
not terminators, and a quoted argument letter is decoded before the first-pass
callback. Known built-in precedence and optional user prompt/storage semantics
remain unchanged.

Caret/backquote decoding now preserves ^@, unknown ASCII pairs and trailing
prefixes. Only the documented letters/punctuation and doubled prefix convert;
backquote-caret produces RS while doubled caret stays literal. Decoded prefixes
are skipped rather than interpreted again. Special-key strings have no atomic
whitelist: unlisted ^[[Z also produces ESC+[Z. Lone/unrecognized brackets remain
literal. Each incomplete slot-looking bracket produces a nonfatal diagnostic
without suppressing the response. Earlier complete recognized template directives
remain unsupported by source preflight until the template stage is implemented;
this milestone does not invent an ESC/backslash exemption from that earlier stage.

Both ordinary passes and pending user responses share the bounded work pump.
A prompt introduced by the first pass can suspend the second, and its accepted
replacement is transmitted without a third expansion. Either response-buffer
allocation can fail nonfatally; unfinished bytes are discarded without sending
a prefix. Source/response/prompt ownership is released on cancellation or shutdown.
Previously executed active operations and accepted store writes are not rolled
back after later allocation failure or cancellation. No parser/session processing
entry returns an error status.

Independent native vectors distinguish one/two/three indirection levels, ordinary
versus later angle expansion, inserted bracket literals, quote-one/quote-both
host forms, argument-local dollars/letters and a second-pass prompt. Explicit
expected byte tables cover the published controls, literal ^@/unknown/trailing
forms, doubled-prefix nonrecursion, listed/unlisted keys and bracket diagnostics.
Existing all-canvas delay/prompt/source fixtures now expect buffered transmission
rather than streaming prefixes. Work-budget checks independently traverse 130
control tokens in each pass before one final send. Deterministic failures in
each buffer test no partial reply and following-query recovery.

Native CMake configure/full build succeeds without compiler/build diagnostics.
The affected rip3_query_commands CTest passes after correcting incomplete-bracket
counting and a stale one-pass prompt-budget expectation. Only that failing suite
is rerun after fixes; earlier catalog, variable-matrix, graphics and preservation
evidence is reused. Edited ledger consistency passes. Counts are 815 implemented,
1577 pending, 4089 unreviewed, 1099 contextual, 24 excluded and 27 blocked across
7631 units. Earlier angle/conditional/template/pick-list/playback stages, dialog
presentation, deferred modes and active application routing remain pending.
No reference pin, original asset, RIP1.54 or active application path changes.
Temporary logs are removed; no issue thread or remote code is modified. The
complete implementation goal remains active.

## Owned template definitions, command blocks and intended chaining

The template component owns 36 independently replaceable definitions plus a
required-selection flag per group. Definitions copy complete caller strings;
C-string NUL terminates storage, and ordinary high/UTF-8 bytes are retained.
Replacing a definition preserves its group requirement. Empty ordinary/check-box
slots resolve to an empty value, while an empty required group reports missing.
The button owner still supplies selected-group aggregation and UI state; the
query's typed callback can request selection/highlighting without implementing
those buttons or a dialog here. Missing required selection aborts the current
host program nonfatally and leaves its stored definitions intact.

The immediate evaluator now owns its complete source program independently of
the current response and segments [] direct blocks, [N:] definitions and [ABC]
chains before ordinary expansion. Numeric slot identifiers are fixed MegaNums,
including inherited lowercase folding. A complete directive is recognized after
ESC or backslash too. Literal/unrecognized brackets remain ordinary bytes.
Definitions install raw ordinary text in source order and transmit nothing;
a later definition of the same slot wins. Each direct/composed response receives
its own two ordinary passes. All remaining source survives delays/prompts and
caller-buffer reuse. Cancellation/shutdown releases program/response buffers and
only query-owned pending user state.

Chaining visits 1..36 slots in list order and substitutes every literal $?$ with
its current feed. A template without that marker discards its feed, including
any callbacks in it, before ordinary processing. Repeated slots are supported.
The implementation deliberately uses the documented intended composition as a
correction to RIPtel's unsafe wrong-buffer offset; it does not emulate corrupted
memory or label intended outputs as native-client observations. Raw composed
responses retain only their first 4096 bytes before expansion; callbacks wholly
beyond that boundary never enter an ordinary pass. Partial helper results are
not published on lookup/index/preparation failure.

New upstream [#93](https://github.com/bbs-land/remote-imaging-protocol/issues/93)
requests the separately unclear embedding/final-response boundary: whether the
4096 limit applies at insertion, pass boundaries or final decoded transmission,
which suffix callbacks survive, and how it relates to direct responses/stored
definition bounds. Existing composition-before-expansion order, two ordinary
passes and the intended chaining correction remain adopted. $?N$ embedding and
that final cap policy are explicitly pending; no existing issue is modified.

Independent native table vectors cover all slots, multiple/repeated markers,
exact inherited intended chain output, blank versus required values, 36/37 levels,
4096 truncation, full owned backing strings, alias-safe replacement, C-string
termination and failed allocation/index/size operations with unchanged results.
Native query fixtures on all three canvases cover deferred/discarded actions,
source-ordered definitions, direct/chain block mixtures, ESC/backslash boundaries,
case-folded slots, required-selection callback and recovery, CP437/UTF-8 bytes,
truncated-away D, suspended source ownership and failed definition replacement.
Too-long chains classify as invalid source before any effects; busy/runtime errors
remain nonfatal operation diagnostics.

Native CMake configure/full build and focused rebuilds pass without compiler
warnings. An initial missing test fault-source link and response/source-pointer
initialization error were corrected before acceptance. rip3_templates passes;
rip3_query_commands passes after the pointer, invalid-preflight classification
and inherited lowercase-slot fixes. Only the failing/changed query suite is
rerun; passing table and prior matrix/control/font/legacy evidence is reused.
Edited ledger consistency passes. Counts are 840 implemented, 1577 pending,
4062 unreviewed, 1099 contextual, 24 excluded and 29 blocked across 7631 units.
The reference pin, original fonts, RIP1.54 and active application route remain
unchanged. Scoped build logs are removed; no code is pushed or PR published.
The complete compliance goal remains active.

## PR #92/#94/#95 adoption and conditional source parsing

The reference now pins stable d046bb4b1f9d066b8534ac9b1369a63abee4563c,
including reviewed open PRs #92, #94 and #95. Six normative documents change,
with 7625 exact reviews retained, 2275 moved line references and 142 new/replaced
blocks: 7767 units across the same 68 documents. All 840 implementation claims
at adoption remain tied to unchanged source clauses; no new policy is inferred
from their old tests. B90/B91/B93 are resolved as documentation questions.
The exact archive and three derivation hashes are recorded in the adoption file.

The private conditional parser now consumes source after IF/ELSEIF through a real
>> terminator and returns its consumed byte count. It defines ASCII whitespace,
letter-only initial references, one case-folded MegaNum template digit, bare versus
dollar-enclosed left scanning, six operators and separate right literal rules.
Local backslash handling bypasses delimiters; the special escaped >> pair is data,
while an unescaped pair terminates even inside an unclosed quote. Right quotes
preserve whitespace, and unquoted values trim decoded trailing whitespace only.
Connector keywords require their exact ASCII-space suffix. No locale, Unicode
identifier, named C escape or ordinary-argument restriction is imported into this
separate source grammar.

Validation checks complete operand/group/operator structure without allocating
or invoking a provider, serving future inactive-parent handling. Program creation
preflights first, then copies all typed tokens/decoded bytes into one owned
allocation; caller storage can be released or reused. Both decoded operands are
bounded to 255 bytes before right trimming, with no separate smaller raw-source
limit. All parse/allocation failures preserve destination pointers and consumed
counts. These status-returning services are private below the owning void RIP
preprocessor; no terminal/session processing API is added or changed.

The provider bridge passes ordinary input as exactly $& + decoded L + $, retaining
its original case/bytes. Existing dollar decoders validate the complete synthetic
reference before callback invocation, rejecting embedded terminators and malformed
arguments without evaluating an unintended prefix. Numeric template indexes use
a distinct callback, including slot zero. Unavailable/rejected/failed providers
or results beyond 255 bytes become false comparisons, including !=; successful
empty values remain valid. Returned bytes are not expanded recursively and live
only until the next provider call. Existing typed comparison/staged reduction is
reused, resolving every comparison in order without short circuit.

Independent native cases cover the published parser-stage examples, one/two/three
backslash distinctions and the separate provider-argument pass. Exhaustive byte
sets check every possible initial byte and quoted literal payload. Additional
vectors cover raw high/control and UTF-8 bytes, keyword tabs/spaces, adjacent quoted
text, empty/unmatched groups, escaped delimiters, exact decoded/source capacities,
pre-trim overflow, failed/empty/oversized providers, ordered borrowed scratch
values, 128-level iterative nesting, owned source and deterministic allocation
failure followed by recovery. Syntax validation makes no allocation/provider call.
No original client, GUI or physical device is run.

Native CMake configure/full build and the new rip3_condition_parser CTest pass
without compiler/build diagnostics. Three adoption guards pass; unrelated typed
condition, variable/query/font/drawing and RIP1.54 results are reused. Edited
ledger consistency passes. Counts are 893 implemented, 1644 pending, 4062
unreviewed, 1119 contextual, 24 excluded and 25 blocked across 7767 units.
Full IF/ELSEIF/ELSE/ENDIF preprocessing and real provider/application binding remain
pending. PR #94 ordinary outline placement and PR #95 bounded working-text,
embedding/storage and trailing-quote policies are adopted pending implementation;
current old tests do not claim their new cases. The original fonts, RIP1.54,
active application route and external-launch exclusion remain unchanged.
Owned review files and scoped logs are removed. No existing issue is modified,
code pushed or PR published. Full compliance remains in progress.

## PR #95 host-response limits and slot embedding

The immediate host evaluator now implements $?N$ for all 36 case-folded slots
in either of its two ordinary passes. Regular empty slots insert nothing;
required empty slots request selection and abort the current host program
nonfatally, without running later blocks. Inserted values are skipped within
their producing pass; nested/cyclic references never create a third pass.

PR #95's portable policy now caps stored definitions at their first 4096 raw
ordinary bytes, each response before pass one, and working text immediately
after every successful dollar/template replacement. The processed prefix and
monotonically bounded unread suffix represent that working text without an
oversized intermediate allocation. Permanently discarded tails never return
after decoding or a smaller replacement frees space. Retained callbacks still
execute, and clipping never rolls back an accepted assignment that caused it.
Each block has its own bound; later definitions/blocks are not discarded by an
entire-source or session budget.

The previous direct-text preflight now stops after the first replacement:
ordinary suffix syntax is checked only when reached after clipping. Earlier
structural angle/pick-list/bracket checks still precede this ordinary stage.
This permits an insertion to discard unsupported/malformed ordinary references
without rejecting or invoking them. Retained incomplete references cannot borrow
a terminator beyond the boundary and fail internally without partial output or
ending the session. A trailing outer backslash stays literal, including after
clipping; argument-local dangling-escape validation remains separate.

Independent native query cases on all three canvases cover every published
4095/4096/4097 boundary/side-effect vector, both-pass growth, retained delays,
discarded assignments, permanent suffix loss, decoded-prefix capacity, raw
control/backslash cuts, byte-split UTF-8, generic identity results and accepted
user assignments whose output is clipped. Additional cases cover every numeric/
upper/lowercase slot spelling, command-submitted oversized definitions, required
selection/recovery, cycles, multiple full response blocks and post-error queries.
The separate table fixture checks capped owned storage and alias-safe replacement.
No original-client, GUI, physical-device or application coverage is inferred.

Native CMake configure/full build passes without compiler/build diagnostics.
The changed rip3_templates and rip3_query_commands CTests pass; after adding
slot/command-submission and lone-dollar edge cases, only the changed query suite
is rebuilt/rerun.
Edited ledger consistency passes. Counts are 918 implemented, 1620 pending,
4059 unreviewed, 1121 contextual, 24 excluded and 25 blocked across 7767 units.
Button-group aggregation/highlighting, full angle/conditional preprocessing,
playback, deferred modes and application routing remain pending. Prior passing
adoption, condition, drawing/font and RIP1.54 checks are reused; their inputs are
unchanged. The pin, original fonts, RIP1.54 and active application route remain
unchanged. Scoped build logs are removed. No existing issue is modified, code
pushed or PR published. Full compliance remains in progress.

## Resumable conditional values and outer-scanner follow-up

The conditional source service now prepares an owned evaluation continuation
before any provider action. It copies all parsed tokens and operand payloads;
the source and parsed program can be released while an operand awaits a dialog.
The void processing boundary examines a caller-budgeted number of prepared tokens
and stops at each valid ordinary or template request. An owner may keep that
request pending across source pumps without repeating earlier provider actions.
Supply compares borrowed bytes immediately and consumes only that request, so
the provider can reuse scratch storage and the next lookup is independently
scheduled. Every evaluated leaf still runs in source order without short circuit.

Invalid synthesized provider references become false without reaching a provider.
Unavailable/failed/oversized/invalid spans likewise become false, including !=;
successful empty values remain valid. The final reduction shares the existing
allocation-free Boolean/NOT/group phases. No allocation occurs while requesting,
supplying or reducing; complete token/payload preparation precedes all provider
actions. Pending destruction releases the owner without callbacks. The former
synchronous source evaluator now drives this same continuation, preserving its
private helper boundary and failure-atomic result publication.

Independent native cases cover zero/one-token budgets, repeated waiting pumps,
one-at-a-time requests, copied source/program ownership, simultaneous independent
runs, numeric template zero and case-folded letter slots, borrowed scratch reuse,
no short circuit and equal binary precedence, invalid references skipped before
later leaves, 255/256-byte values, successful empty spans, failed != comparisons,
pending destruction and failed preparation followed by recovery. Existing source
byte/escape/quote and typed numeric/reduction vectors exercise the shared path.
Native CMake configure/full build and the two affected rip3_condition and
rip3_condition_parser CTests pass once without build/compiler diagnostics.
Unchanged query/drawing/font/adoption/RIP1.54 evidence is reused.

Full outer preprocessing remains a separate pending owner. Read-only inspection
of the catalogued original worker found fixed IF-space/ELSEIF-space and exact
ELSE>>/ENDIF>> dispatch strings, a special backslash-before-<< path, outer
terminator/nested-introducer lookahead and explicit branch-state/depth checks.
These are not all defined by the operand parser's independently adopted byte
grammar. New upstream [#96](https://github.com/bbs-land/remote-imaging-protocol/issues/96)
requests that outer-scanner/branch contract, with exact instruction/data leads
and references to completed PRs #32/#92/#95. No existing issue is modified; no
original instruction/client, GUI, prompt or provider was executed. The reference
pin and completed operand/comparison/pass-count answers remain adopted.

Edited ledger consistency passes. Counts are 918 implemented, 1619 pending,
4057 unreviewed, 1121 contextual, 24 excluded and 28 blocked across 7767 units.
This milestone supplies resumable evaluation for the future branch owner; it
does not claim IF/ELSE text filtering, real host/user provider binding or
application integration. Original fonts, RIP1.54 and the active application route
remain unchanged. Scoped logs are removed; no code is pushed or PR published.
The complete compliance goal remains active.

## Ordinary outline T/@ selection, placement and cursor integration

The separate command engine now renders literal outline TEXT and TEXT_XY using
the current y-selected face/settings. Selection remains an owned STYLE edit;
each accepted nonempty draw realizes that exact case-insensitive name against
the bundled catalogue or a matching borrowed immutable RFF view. There is no
default-classic replacement for an unavailable face. The transport conversion
continues to map UTF-8 scalars to raw CP437 font codes. Source expansion remains
explicitly unsupported until its owning preprocessing stages are integrated.

PR #94 justification now uses the signed, spacing-adjusted ordinary total,
including the final gap: start/selector 3 subtract zero, end subtracts the total
along the string axis, and center divides by two toward zero. All sixteen string/
character rotation pairs use the design origin at the converted drawing anchor;
the existing device baseline table is added once afterward. No global-bbox edge
or tentative typographic alignment field relocates that origin. Negative totals
remain signed, and glyph rotation precedes independent device-axis scaling.

Successful requested metric preparation atomically commits the context's logical
snapshot before cursor measurement. A cold context creates that owned cache on
first successful preparation; its client destroys/resets it at shutdown/full
drawing reset. Failed font/metric preparation retains the previous snapshot or
zero when cold. Every accepted outline call performs its adopted cursor store
even when preparation or later ink/raster work fails. T advances prior stored X
only; outline @ uses the converted supplied anchor and cached X width. Classic
font-zero @ continues reading that snapshot through its separate raw-anchor path.
Layout advances and justification do not substitute for the cached cursor width.

Independent native captures on all three canvases cover sixteen rotation pairs,
four justification selectors, positive/negative bbox spacing and signed baselines,
and all five W operations: 1920 command scenes. Pure placement cases assert the
published PR #94 positive/negative offsets with the explicitly supplied fixture
bounds. A separate unequal-DPI case checks rotated ink and the distinction between
vertical ordinary steps and cached X width. Actual y transitions render Marin,
Marin Cn, Marin Wd and Marin HE, checking visible ink and independent 50/37/62/75
F widths. Other cases cover visible T output, four shadow quadrants, RGB ink,
palette-free NOT, blank/clipped text, CP437/UTF-8, cold/warm/frame-change failure
vectors, 700-byte signed-WORD cursor handoff, retained metrics after failed
lookup/validation/allocation, inactive viewports and split-input recovery.

Native CMake configure/full build passes without diagnostics. The five initially
affected outline layout/text, text command, cursor and new outline command CTests
pass. Only changed outline/layout tests are rerun after adding face/shadow/RGB/
unequal-DPI acceptance cases. The STYLE fixture's old unsupported-outline checks
now expect the documented stored-size-zero failure on use; that separately
affected STYLE suite passes. Passing unrelated source/adoption/condition/query/
font-asset/RIP1.54 checks are reused. Edited ledger consistency passes. Counts
are 958 implemented, 1571 pending, 4057 unreviewed, 1129 contextual, 24 excluded
and 28 blocked across 7767 units.

These are separate-engine parser/bitmap fixtures, without original-client, GUI
or native font-device execution. Plain solid/hollow and bold hollow use the
existing renderer; synthetic solid bold, italic and decorations remain explicitly
unsupported with accepted-call cursor/cache effects. Name/text expansion, full
drawing-reset orchestration and application routing remain pending. The active
application route, RIP1.54 and original font/assets remain unchanged. No code is
pushed or PR published, no existing issue is modified, and scoped logs are
removed. Full compliance remains in progress.

## Local-state data references and outline-effect boundary follow-up

Ordinary host dispatch now implements SHIFT/CTRL, MSTAT, COLORS and captured XY
through explicit borrowed client services. A nonblocking modifier provider is
read only when its reference executes; no default keyboard state or key injection
is substituted. Mouse-presence and destination-device color capacity are separate
bindings, independent of a palette's entries or the current ENV color mode.
Missing services remain unsupported at the existing void parser boundary.

XY formats captured signed device coordinates as minimum-four-column decimal
WORD pairs, rather than hexadecimal. WORLD converts through the current ENV's
frame when bound, otherwise the explicit fallback frame, truncating signed
products before final WORD narrowing. No drawing PORT is required. A text-window
query's omitted domain instead computes/clamps coordinates using the owner's
resolved DEVICE cell grid and formats minimum-two-column pairs; explicit WORLD
or DEVICE bypasses that default. Invalid domains/counts reject before providers,
and malformed/unavailable local data never enter same-named user-variable lookup.

Independent native cases cover both modifiers and their combinations, MSTAT
YES/NO, typical and non-power-of-two device capacities, published 297/321 and
80x25 cell examples on all canvases, nonzero cell origins, outside-window clamps,
wide signed WORDs, negative conversion truncation, current/protected ENV
precedence, absent/invalid services, bounded output and provider failure/recovery.
A delayed compound host query reads changed local values when reached and gates
later raw bytes. The existing conditional source evaluator consumes real local
data references, preserving ordered, non-short-circuit leaf evaluation and false
failed comparisons. These are fixture/provider tests, without physical keyboard,
mouse, native device or application-route coverage.

The native CMake configure/full build passes without compiler/build diagnostics.
Affected rip3_query_commands, rip3_host_variables and rip3_host_recovery CTests
pass initially. The new rip3_local_variables test finds an XY domain case-folding
omission; that is corrected, and only the failing local suite is rerun and passes.
Unchanged font/layout/query-grammar/adoption/RIP1.54 evidence is reused.

The remaining outline-effect review found that the reference establishes effect
bits, q accumulation, unchanged logical advances and hollow BOLD strength, but
does not fully distinguish mandatory decoration placement/composition from
renderer-chosen synthetic geometry. New upstream
[#97](https://github.com/bbs-land/remote-imaging-protocol/issues/97) requests that
portable boundary, explicitly excluding a pixel-identical reconstruction request
and retaining the completed PR #29/#55/#77/#88/#94 answers. Existing issues receive
no comments/content. Solid BOLD/ITALIC and underline/strike-through remain pending
their ink contract.

Edited ledger consistency passes. Counts are 972 implemented, 1572 pending,
4028 unreviewed, 1139 contextual, 24 excluded and 32 blocked across 7767 units.
Full angle/text-window/application owners and
platform local-input adapters remain pending. Original fonts, RIP1.54, the active
application route and the reference pin remain unchanged. Scoped build logs are
removed. No code is pushed or PR published. Full compliance remains in progress.

## Owned session lifetime and ordered input/query pipeline

The separate engine now exposes an owned session factory, void processing pump,
cancellation/destruction, owned-prompt response and coherent capture services.
Construction prepares the live ENV/STYLE/PAL/PORT tables and their typed backups,
one logical outline cache, template slots, private variable stores/processor and
the stream/query/input gate before publishing the session. External services are
copied as borrowed bindings; a supplied user processor or exclusive audio player
remains client-owned. Partial construction releases every completed allocation
without reading input, invoking client callbacks or stopping the borrowed player.

Each nonblocking pump resumes local query work before taking another source byte.
Delays and user prompts retain later RIP/raw input in the client queue. Pending
local work remains observable even when the source is empty, so a budgeted query
can finish without new network traffic. Processing failures stay inside the void
parser boundary. A query-owned prompt check prevents a pending delay query from
exposing or answering another consumer's prompt on a borrowed processor. Cancel
discards incomplete framing, pending query/prompt work and delays, and stops the
exclusive player without consuming queued bytes or emitting replies. Completed
drawing/store/cache effects remain; cancellation is not a full drawing reset.

The capture service observes the complete screen through the selected palette
together with current frame, selections, cursor, viewport, query/delay and cache
state. It performs no parser pumping. Host viewport erase now resolves the live
STYLE background at execution, including a STYLE change earlier in the same
query, and uses current ENV/PAL for direct-color resolution. A separately bound
pre-resolved background remains the fallback for hosts without a STYLE service.

Native wire tests cover default and changed frames on all three canvases, bytewise
drawing, y-selected Marin T/@ ink and cursor widths, classic font-zero warm-cache
handoff, same-query STYLE/erase ordering and indexed/direct backgrounds. Other
cases cover delayed required prompts, empty-source continuation, cancellation,
store/session isolation, malformed-command recovery, missing reply sinks,
foreign-versus-owned borrowed prompts and all seventeen construction boundaries.
Headless pull-engine tests verify stop and borrowed-player lifetime without
physical output; existing PCM evidence is reused.

Native CMake configure/full build and rip3_session/rip3_query_commands initially
pass without compiler/build diagnostics. Final integration review finds the stale
background binding; its focused ordering/direct-color cases and fix pass the
affected viewport-host suite. The session case catches a six-digit test color
field where the command accepts four; after correcting the fixture, only the
failed session suite is rerun and passes. The native rebuild introduces no
diagnostics. Edited ledger consistency passes;
counts remain 972 implemented, 1572 pending, 4028 unreviewed, 1139 contextual,
24 excluded and 32 blocked across 7767 units. Existing unrelated passing evidence
is reused. Text-window/button/mouse/SCREEN ownership, full drawing reset,
preprocessing, scene headers and application routing remain pending. Original
fonts, RIP1.54 and the reference pin remain unchanged. Scoped logs are removed;
no existing issue is modified, code pushed or PR published. Full compliance remains
in progress.

## Palette reset lifecycle and validated live-entry copying

Palette reset now treats stored RGB values, in-use state, protection and current
selection as separate state. A specific/current reset restores defaults and
clears a nonzero slot's definition without selecting another slot. Protected
specific/current entries reject unchanged; a table/soft reset skips them. The
table-local hard-reset service removes protection, defaults all slots and selects
zero. Slot zero remains defined and unprotectable. These table services do not
clear their separately owned backup area or claim whole-scene reset dispatch.

`$RESET(PAL,...)$` now supports omitted/current and numbered live targets, TBL,
BASE, numbered saves, SLOTS, STACK, BACKUP and ALL. The complete selector list
and all required live/backup bindings validate before any operation. Backup
clearing preserves protected numbered saves. Execution follows selector order;
an individual runtime protection failure does not roll back earlier completed
selectors. Invalid source syntax and missing bindings never partially reset a
mixed live/backup expression. All incoming failures remain inside the public
void host/query/session processing boundaries.

Live palette COPY accepts decimal/CUR source and destinations independently of
whole-table backup COPY. The source must be defined; its protection does not
prevent reading. Every destination is validated before any copy, including a
late protected, self or invalid destination. Successful destinations become
defined and inherit source protection except at zero, which stays unprotected.
Selection is retained. Numeric ISPROT still returns -1 for an undefined slot;
ISPROT(PAL,CUR) directly returns its protection bit as the adopted contract
requires. Reselecting an undefined slot defines it with defaults again.

A separate selected-palette RGB view supports the legitimate undefined-but-current
state without implicitly defining it. Session captures, bitmap writing/loading,
image presentation and error painting now use this view. Numbered reads retain
their definition checks for PALENTRY and live-entry copying. Independent tests
cover all 36 slots, default RGB samples, protected and repeated resets, hard
versus soft table behavior, copied protection/zero, multiple destinations, failed
prevalidation, current/numeric status distinctions, every save-area reset class,
missing services, zero output capacity and public recovery. All-canvas session
wire captures assert the published 5/0/-1/0 status sequence and successful
drawing while palette 5 remains selected and undefined, without losing the font
cache. These are separate-engine native tests, not application-route coverage.

Native CMake configure/full build passes without diagnostics. All fifteen
affected palette/table/host/query/session/bitmap/image CTests pass (24.35 seconds).
After adding positive base/stack/numbered-save isolation checks, only the changed
palette-lifecycle test is rebuilt/rerun and passes. Other passing evidence is
reused. Edited ledger consistency passes: 990 implemented, 1577 pending,
4003 unreviewed, 1141 contextual, 24 excluded and 32 blocked across 7767 units.
Whole-scene reset/header orchestration, other table families and application
routing remain pending. The reference pin, original assets and RIP1.54 are
unchanged; no upstream issue is modified and no code is pushed or PR published.
Scoped logs are removed. Full compliance remains in progress.

## PR98/99 adoption and exact Dixon wire regression

The pinned reference now includes reviewed open PRs #98/#99 as consolidated in
fork stable. B96/B97 are resolved documentation questions; outer angle-worker
execution and synthetic outline effects remain pending implementation. The
adoption record retains the old pin and all earlier reviews, exact reviewed heads,
archive/document/derivation hashes and source-block movement. The 68 normative
documents now contain 7867 blocks: 990 implemented internally, 1671 pending,
4003 unreviewed, 1154 contextual, 24 excluded and 25 blocked. These counts track
internal implementation evidence; they are not connection-available feature counts.

A new separate-engine session regression consumes the user's exact
`!|y00000W0M000000001a1a000000Dixon` followed by CRLF. In a fresh interpreter it
accepts the base-64 y fields while ordinary coordinates remain base 36. Selection
alone leaves the bitmap blank and cache cold. Subsequent TEXT_XY/TEXT produce
Dixon ink and cursor updates on all canvases. Independent metrics use the bundled
Dixon F advance 610/1000 em, 22 points and logical half-up width 268; X-DPI
truncation gives 13/16/21 device pixels at 640/800/1024 widths. This establishes
only the internal wire path. A normal connection cannot invoke it while the old
application route remains active; no connection-visible font feature is claimed.

Native CMake configure/full build passes without diagnostics. The changed session,
contract and preservation-guard CTests pass (three tests, 0.65 seconds). Earlier
unchanged rendering and application tests are reused. The ordinary source indexer
reproduces the adopted document inventory and review blocks. Scoped review inputs
and build logs are removed; the reference archive is read as data without running
upstream code or the original client. No existing issue is modified and no code
is pushed or PR published. RIP1.54 and original font assets remain unchanged.
Full compliance and connection integration remain in progress.

## Internal synthetic outline effects and decorations

The separate renderer now implements the low four y/q style bits under the
adopted PR #99 rendering boundary. This is internal implementation and test
coverage, not a feature available from a connection. The application still uses
the old RIP3 path. The geometry profile is documented in the public renderer
header; it is an explicit SyncTERM choice, not a claim of native pixel identity.

Italic shears design coordinates with x := x - y/4 before face percentage,
independent character rotation and device-axis scaling. Solid bold expands each
filled scan interval on both DEVICE X sides by max(1,ceil(points*X-DPI/1728)).
Hollow bold retains its previously adopted device stamp formula. Neither effect
changes the supplied metric bbox B, logical advances, spacing, justification,
glyph origins or cached cursor widths. Wider bold ink narrows visual gaps or
overlaps its neighbor; it does not move that neighbor or add to its advance.

Underline and strike-through are filled per-byte local-baseline bands spanning
the declared unspaced advance. Their font-unit offsets are floor(U/10) and
-floor(U/3), with thickness max(1,floor(U/16)), where U is units per em. They follow
the italic/character geometry, omit spacing gaps and retain their normal thickness
under bold. Present blank/damaged glyphs retain their bands; missing or zero-advance
codes do not create a band. Geometry comes from the current font/text, not a stale
cursor snapshot. No tentative alignment/font field is promoted to a band metric.

Glyphs and bands form union coverage; both have the same final DEVICE-displaced
shadow. Foreground wins where this profile's foreground and shadow overlap, and
the selected W operation applies once per covered pixel. Draw/Back colors retain
their ordinary roles. Decorations do not consume the unrelated vector pen type,
width or off-draw flag, border mode, fill pattern or Fill Color. All geometry and
coverage allocations finish before painting; errors remain inside the existing
void parser boundaries, with the adopted accepted-call cursor/cache effects.

Independent rectangle/parallelogram expectations cover all three canvases,
sixteen S/R pairs, isotropic/unequal DPI, all sixteen low effect combinations,
normal/negative bbox spacing and all five W modes: 15,360 coherent command scenes.
They assert the complete viewport image and cursor state, including clipped and
overlapping bands, blank glyphs, four shadow quadrants and nondefault unrelated
vector settings. Additional captures check q's OR accumulation through CP437 and
UTF-8 accented input. Native renderer cases check hollow italic/bold blank bands,
missing glyphs, damaged glyph advances and failure at every allocation of a
combined glyph/band/shadow operation without partial paint. Existing hollow width,
font, cache, text, cursor and session evidence remains applicable.

Native CMake configure/full build passes without diagnostics. Six of seven
initially affected CTests pass; the new effect scenes pass, but the old command
fixture still expects solid bold to be unsupported. That expectation is replaced
with accepted drawing and unchanged cursor width. The two changed outline suites,
including allocation and hollow-band cases, pass. After adding accented/q cases,
only the changed command suite is rebuilt/rerun and passes. Unchanged passing
checks are reused. Updated ledger consistency passes: 1008 implemented internally,
1644 pending, 4003 unreviewed, 1163 contextual, 24 excluded and 25 blocked across
7867 units. These are evidence-review counts, not connection-available features.
RIP1.54, original font assets, reference pin and application route remain unchanged;
scoped logs are removed. No issue is modified, code pushed or PR published.
Full compliance and connection integration remain in progress.

## Internal outer angle worker and immediate-query continuation

The separate engine now owns a resumable outer angle worker under PR #98.
It copies its bounded C-string source and owns working text, sixteen branch
frames and each eligible condition continuation across provider requests.
Its processing/supply methods are void; syntax, operation and resource failures
become internal failed state without returning a terminal/session error or
publishing partial output. Input callbacks, dialogs and transport remain with
the owner. A pending provider is never replayed or advanced by another process
call. Cyclic immediate named expansion can remain pending, but finite work budgets
keep each call bounded and permit cancellation without further evaluation.

The worker implements exact case-insensitive IF-space/ELSEIF-space and exact
ELSE/ENDIF boundaries, no opening whitespace trim, literal-single-angle behavior,
255-byte complete fallback references and single-digit angle templates. Its
lookahead ignores quotes, skips a delimiter only on an immediately preceding
backslash and rejects eligible nested introducers. Conditional syntax must end
at that same outer terminator before any operand lookup. The special introducer
escape is separate from operand and ordinary-pass decoding. Skipped IF/ELSEIF
headers validate without providers, including under inactive parents; selected
chains cannot reopen, repeated ELSE/ELSEIF-after-ELSE/stray/unclosed structures
reject, and a seventeenth frame fails before its providers. Inactive named/template
lookups retain their documented effects; deferred template output is discarded.
Named insertions rescan immediately, templates skip their inserted bytes in that
pass, and eight whole passes can succeed with deferred syntax still present.

Immediate queries now bind the worker to real built-in, user-variable and template
services before command-block segmentation, definition storage, composition or
the two ordinary response passes. Built-in retrieval normalizes the synthetic GET
prefix without falling back to same-named user records. Failed/overlong comparison
values become false; failed named lookup aborts preprocessing. Required missing
templates retain the existing selection callback/abort boundary. Required user
prompts expose only query-owned state, and D delays yield before later lookups or
source bytes. No 4096-byte whole-preprocessor cap substitutes for the later
per-response bound. Source NUL ends input. Later ordinary inserted text never
restarts preprocessing. Cancellation releases owned worker/prompt state while
retaining earlier completed store effects and the input gate's separate policy.

Independent worker tests cover all branch outcomes/provider order, inactive
lookups versus skipped operands, malformed skipped headers, keyword/template/name
boundaries, nested/escaped/quoted delimiter gates, 255/256-byte bodies, deferred
eight-pass output, exact argument-byte handoff, active/inactive depth overflow,
owned source, NUL values, cyclic yielding and every owned construction/growth
boundary. Query fixtures on all canvases cover actual built-in/user/template
dispatch, segmentation introduced by substitution, assignment ordering, prompt
response/cancellation, delay/state changes, required selection, no partial reply,
resource failure/recovery and 5000-byte preprocessing followed by separately
capped responses. The owned-session fixture additionally verifies wire IF/template
execution and an angle prompt through its public pump/response API, with queued
raw bytes and literal later ordinary angle output preserved as specified.

Native CMake configure/full build succeeds without compiler diagnostics. GNU make
reports a transient sub-millisecond clock-skew warning; it is environmental.
Five of six initially affected CTests pass; query fixtures still expecting every
angle form to be unsupported are updated to the new parser-owned diagnostic
boundary. The changed worker/query tests pass. After adding owned-session and
earlier-effect retention cases, their changed session/query tests pass; targeted
lexical additions then pass the changed worker test. Existing passing condition,
template, session and unrelated rendering/RIP1.54 evidence is reused. The final
successful session/query results were recovered from CTest's completed log after
the tool session ended; those checks were not restarted. Updated ledger consistency
passes: 1058 implemented internally, 1596 pending, 3997 unreviewed, 1167 contextual,
24 excluded and 25 blocked across 7867 units. These are internal native fixtures, not
connection-available features or GUI/device coverage. Graphical-text consumers,
pick lists, playback, button-associated aggregation and application routing remain
pending. The reference pin, original fonts and RIP1.54 are unchanged. Scoped logs
are removed; no existing issue is modified, code pushed or PR published.
Full compliance and connection integration remain in progress.

## Connected ordinary text windows

The default unprotected window now accepts nonzero `w` layouts through the
normal connection route. A private headless ANSI surface uses the original
MAF bank, the documented device-cell calculation and absolute reference-origin
placement. It imports current artwork before text batches and publishes before
ANSI replies, so capture fences include completed output and scrolling stays
inside the window. Identical bounds/font retain the parser and cursor; wrap
changes and all-zero deactivation/reactivation preserve that identity. Invalid
fields and resource failures retain the prior window and recover inside RIP3.
No legacy RIP helper is used or changed.

The active parser is reacquired for keys, scripts and native Wren polling.
Local `CTerm.write`/`reset` publish inside the virtual TTY. Native geometry and
session tests cover five font modes, exact reference/device placement, nominal
bounds, both radices, coordinate widths 2–5 and failed definition recovery.
CP437/UTF-8 TCP fixtures cover all five device rasters, five fonts, chop/wrap,
explicit and bottom-right scrolling, outside-window pixel preservation,
cursor/size queries, local writes/reset/input hooks, and artwork surviving
deactivation/reactivation. Their command-only CRLF and split-CR/LF regressions
continue to verify that RIP lines do not scroll artwork.

The native Wren fixture now expects the existing baseline-JPEG build option.
Its legacy scene-stack/ICN transfer test explicitly uses RIP1.54 on a private
console; it no longer dereferences the deliberately absent legacy engine in a
connected RIP3 session or changes later fixtures' font geometry. RIP3 block
transfers/playback remain pending and are not claimed by that legacy coverage.
Native CMake builds without diagnostics; the changed native tests, Wren suite
and connected CP437/UTF-8 checks pass. Existing unrelated passing tests are reused.
The preservation/index guard still matches all 93 frozen files and 41 helpers.

Upstream [#100](https://github.com/bbs-land/remote-imaging-protocol/issues/100)
tracks normal redefinition's unspecified ANSI-state/clearing boundary and the
font-2 example outside its declared row range. Existing issues were untouched.
The ledger records 1076 implemented, 1597 pending, 3976 unreviewed, 1167 context,
24 excluded and 27 blocked blocks. Full TW tables/protection/switching,
extended windows and the unresolved redefinition details remain unfinished.

## Connected text-cursor commands

The separate command dispatcher now decodes wire `g`/`H` and asks the session
owner to move its active TTY cursor. `g` retains fixed two-digit fields under
both current radices even when XY width changes to five; `H` does not depend
on ENV numeric fields. Valid commands on a deactivated window do nothing.
The native CTerm service starts text rendering, commits pending Unicode,
clears deferred right-margin wrap and applies CUP's clamp/origin addressing.
It updates the real console cursor and parser state without manufacturing
received text, input-hook calls or captures. A positioned window publishes its
new cursor through the same connected view adapter. Every incoming command
and service failure is still consumed at RIP3's void recovery boundary.

Independent native CTerm tests verify startup, oversized coordinates,
origin-mode margins, no input/capture effects, pending-wrap recovery and the
later cell contents. Session tests verify literal fields, both radices,
malformed tuples, inactive no-ops and a renderer failure followed by a valid
query and command. CP437/UTF-8 TCP tests cover entry-sized and positioned TTYs
on the existing five rasters, exact cursor replies, unchanged artwork,
deactivation/reactivation and subsequent text. Native configure/full build
and the five relevant CTests pass. There are no C compiler diagnostics; make
reports environmental sub-millisecond clock-skew notices on generated dependency
files. The preservation guard retains all 93 frozen files and 41 helpers.

New upstream [#101](https://github.com/bbs-land/remote-imaging-protocol/issues/101)
requests the extended fitting/auto-font/metric/bounding rules and conflicting
erase-color, activation and cursor-variable contracts. No existing issue was
modified. The ledger records 1087 implemented, 1597 pending, 3950 unreviewed,
1167 contextual, 24 excluded and 42 blocked blocks. Extended windows, full TW
tables and unresolved control-variable behavior remain pending.

## Connected default-window activation variables

Immediate queries now execute `$ATW$`/`$DTW$` through an owner-bound activation
service. Omission selects CUR; case-insensitive CUR/ALL and decimal slots 0..35
are collected, deduplicated and validated before any callback. The common
argument grammar still drops empty comma fields; a retained whitespace-only
field is invalid after normalization. Invalid later parameters cannot partly
change earlier targets. The connected owner currently defines only unprotected
slot zero; other numeric targets remain undefined no-ops. This does not claim
complete TW table, protection or switching support.

The entry TTY hides its cursor without discarding requested visibility, shape
or custom scan-line data. A positioned window imports actual screen artwork
before reactivation, avoiding stale cached pixels overwriting RIP drawings.
The adapter publishes these effects before replies and ordinary text rendering.
No cursor, ANSI attributes or layout are reset by activation. Incoming syntax
and service failures remain inside the existing void parser recovery boundary.

Native selector tests cover all 36 targets, duplicates, forty arguments,
empty/whitespace fields, callback ordering, capacity prevalidation, missing
services and resource/operation failure recovery. Owned-session tests exercise
actual default-state changes, undefined targets, malformed lists, interaction
with all-zero `w`, cancellation and rejection of external activation bindings.
CP437/UTF-8 TCP captures verify output/ANSI suppression, retained cursor
preferences, unchanged text state, invalid-list atomicity and artwork surviving
disabled text followed by activation on all five existing device rasters.
Native CMake configure/full build and seven relevant CTests pass across the
targeted runs; unchanged successful checks are reused. No C compiler warnings
were introduced. All 93 frozen files and 41 RIP1.54 helpers still match.

The ledger records 1098 implemented, 1604 pending, 3932 unreviewed, 1167 context,
24 excluded and 42 blocked blocks. Mixed requirements that also describe
protected/defined noncurrent windows remain pending. The reference pin and
upstream issues are unchanged; full compliance remains unfinished.

## PR #102 adoption and ordinary-window bounds

The reference now pins fork stable `93fd0c3bfac24fda5e76d20b33d782b27ba89e99`,
which consolidates the reviewed ordinary-window contract from open PR #102.
Only the terminal-view normative document changes. The 68-document reindex
retains 7864 exact heading/content matches, moves 94 line identities and adds
19 blocks, yielding 7883 total. Eighteen ordinary-window implementation claims
were reopened for reconciliation; no implemented block disappeared. All
previously recorded research digests are unchanged; the new read-only derivation
is recorded separately. B100 is resolved as documentation. B101 remains separate.
No existing issue receives content and no upstream client/probe code executes.

The ordinary geometry service now orders reversed endpoint pairs and clamps
all four coordinates to the selected nominal font grid before calculating
identity, extents and origin. Clamping reports a distinct nonfatal diagnostic
and continues; reversed in-range pairs need no clamp warning. Invalid fonts
and wrap values retain the old window through parser-owned recovery. Matching
effective definitions preserve the connected cursor/saved cursor/colors and
only update wrap, including when different raw inputs normalize identically.

Independent native expectations cover all five fonts, extreme and reversed
bounds, the corrected font-2 `(10,15)..(20,25)` to `(10,15)..(20,24)` example,
11-by-10 extents at reference/device resolutions, ordered identity, diagnostics
and recovery. CP437/UTF-8 TCP captures cover normalized identity, saved cursor
and colors, changed-definition homing/default gray and unchanged visible pixels
on all five existing device rasters. Native CMake configure/full build and seven
affected CTests pass, including reference/reindex/preservation guards. There
are no C compiler diagnostics; make reports environmental millisecond clock-skew
notices. Other successful tests are reused. The 93 frozen files and 41 legacy
helpers remain unchanged.

Changed definitions still allocate a fresh CTerm. Retention of tab stops,
character-set selections and unfinished ANSI syntax across that replacement
is pending implementation, as is complete acceptance of all reset-mode fields.
The existing ANSI-BBS interpreter has no independent G0/G1 character-set model;
that requirement must be implemented within the separate RIP3 route while
preserving RIP1.54. Optional partial UTF-8 decoding needs an explicit client
boundary rather than a RIPtel claim. The ledger keeps these state-table rows
pending; adoption and passing geometry tests do not establish them. The current
queue is 1102 implemented, 1615 pending, 3931 unreviewed, 1171 context, 24 excluded
and 40 blocked source blocks.

## Connected ordinary ANSI interpreter retention

Changed ordinary `w` definitions now copy unfinished ESC/CSI parsing, collected
parameters, string buffers and tab stops into the prepared new view before
publishing it. All buffer allocations finish before destination mutation;
failures leave the old connected window usable and stay within RIP3's nonfatal
diagnostic boundary. Owned buffers and parameter aliases never point into the
destroyed old CTerm. New default tab stops extend on growth; custom stops beyond
a temporarily smaller window survive. Changed definitions reset saved cursor
coordinates to home as well as the existing fresh rendering state.

The native `cterm_text_window` test completes literal truecolor CSI at every
split after destroying its source, resumes OSC strings, exercises horizontal,
vertical and default tabs, verifies saved home and injects every preparation
allocation failure. Pending sixel, macro, music and font-download payloads carry
extra resources; redefinition rejects them nonfatally rather than transferring
incomplete ownership. Their transfer and G0/G1 character-set support remain
unfinished. The source-block ledger keeps the combined tabs/charset row pending.
Connected CP437/UTF-8 tests exercise CSI completion across changed definitions,
tab retention through shrink/growth, saved home and fresh margins/cursor
visibility. UTF-8 partial decoding commits at the RIP boundary as client policy.
The queue is now 1105 implemented, 1612 pending, 3931 unreviewed, 1171 context,
24 excluded and 40 blocked source blocks.

The command-only CRLF regression also covers non-leftmost bottom-row cursors
on both the entry canvas and positioned windows. Valid, rejected, empty and
continued command lines span more than a full text window and the session's
4096-byte processing budget. Each check compares one fenced capture of all
pixels and cursor state. The user has confirmed that the CRLF scrolling issue
is fixed; further investigation is closed and these checks remain regressions.

Native CMake configure/full build and the focused interpreter, framing and
CP437/UTF-8 connected CTests pass. The existing ANSI, historical wire and
RIP1.54 preservation checks passed for this source change; the compliance
ledger check also passes with all 93 frozen files and 41 helpers unchanged.

## Connected color-mode wire command

The separate command dispatcher now implements `M mode:1 bits:1` against the
current ENV entry. Numeric validation precedes mutation. Palette mode ignores
the valid bits value; RGB mode activates only for eight, leaving the old mode
unchanged for other precision values. Other environment fields and noncurrent
records remain intact. Protection and malformed input use parser-owned
diagnostics; no incoming error escapes to disconnect the session.

Native tests exhaust every one-digit precision value in both radices, from
palette and RGB starting states, plus malformed fields, missing services,
current-slot protection and recovery. CP437/UTF-8 connections on five entry
rasters verify palette restoration by the literal scene prologue, mode/query
agreement, RGB-red and mapped-green pixels, and recovery after invalid input.
Native CMake configure/full build and all four affected CTests pass without C
compiler warnings. The reference/preservation ledger check passes as well.

The local `/synchronet/tmp/ripterm/rips/broken/` label reflects legacy-parser
classification and does not establish that a source is invalid. Initial manual
replays showed later command families drawing over a connection, with remaining
unsupported diagnostics; this was not conformance acceptance. Per user direction,
corpus testing is deferred until implementation is considered complete. The
temporary replay tool and its inspection artifacts were removed. Continue with
independent spec tests while implementing the remaining families.

Character-set work found a separate documentation gap: the baseline lists
US/UK/special G0/G1 designation under VT-102, but does not specify ANSI-versus-VT
gating, initial/invoked sets, SI/SO handling, exact byte-to-MicroANSI mapping or
3.1 reset scope. New parent issue
[#104](https://github.com/bbs-land/remote-imaging-protocol/issues/104) asks for
that contract and independent byte vectors, referencing #100/PR #102 and
#101/PR #103 without adding content to any existing issue. B104 blocks the
combined tab/charset row; tab retention itself is already connected. The pin
remains 93fd0c3. The queue is now 1114 implemented, 1613 pending, 3911 unreviewed,
1180 context, 24 excluded and 41 blocked source blocks.

## Scene-reset contract reconciliation

`RIP_RESET_WINDOWS` (`*`) is still rejected nonfatally by the separate engine,
so files relying on it retain earlier screen content. Its canonical reset list
selects every active table's slot zero and describes a black clear. The newer
palette lifecycle says SOFT retains the current palette selection and skips
protected RGB slots. The original image/port derivation proves that actual
`RESET_WINDOWS` calls SOFT `(keep_ports=0, clear=1, update=1)` and ends on port
zero, but explicitly does not prove other reset families because they were
controlled in that fixture. Header bit 8's separate zero-selection operation
also needs to be distinguished from bit 2's SOFT reset.

New parent issue
[#105](https://github.com/bbs-land/remote-imaging-protocol/issues/105) requests
the complete outer operation: current selections/protected survivors, palette
and clear bounds/color, text/default-font/ANSI state, cursor/cache/environment,
input/mouse/audio, and unaffected variables/templates/clipboard/backups. It
references #16/PR #45, #21/PR #52/#71, #24/PR #57 and the text-state follow-ups
without adding content to those issues. A lone physical clear will not be
presented as a complete scene reset. B105 blocks the reset list and header's
SOFT-reset requirement. No additional corpus testing is performed. The current
queue is 1114 implemented, 1613 pending, 3909 unreviewed, 1180 context,
24 excluded and 43 blocked source blocks; other implementation work remains
available while this contract is clarified.

## WORLD word range and macro target behavior

The adopted PR #56 contract supersedes this document's historical 32767 setter
gate. Environment storage now accepts each WORLD dimension in 0..65535.
Positive projection frames support the full word range with wide arithmetic;
zero-size records remain valid state but do not become projection divisors.
Coherent session captures report their accepted raw WORLD dimensions and
unchanged screen pixels. The wire command validates both fields before mutation
and rejects either dimension above 65535.

`$WORLD$` follows the shipped AND-shaped rejection: both oversized dimensions
reject; one oversized dimension stores the low word of each. The axis setters
follow their separate shipped defect: two-argument literal `CUR` errors,
whereas any other target text edits the current environment, including an
undefined number or invalid name. Getter target validation remains intact.
Default WORLD values and defined noncurrent updates retain their prior rules.
Incoming failures stay inside the nonfatal parser boundary.

Native tests use literal 0/1/32767/32768/65535/65536/65537 matrices for macro
storage and both wire radices, current/noncurrent slots, both axes and
CUR/numeric/undefined/invalid targets. Independent geometry expectations place
32767 in a 65535 frame at device pixel (319,239) on 640x480. Session and
CP437/UTF-8 TCP captures verify that placement, zero-state observation without
pixel damage, nonfatal drawing rejection and drawing after restoring a positive
frame on all five existing rasters. A stale font failure fixture now rejects
65536 instead of the newly valid 32768 and adds a literal wide-WORLD DPI case.
Native CMake configure/full build and ten affected CTests pass; only the failed
font test was rerun after its correction. No C compiler warnings were introduced.
Corpus replay remains deferred. RIP1.54's frozen files/helpers are unchanged.

WORLD state/query acceptance does not complete environment context switching,
full reset/header operations, COLORMODE precisions 1..7, or unspecified behavior
of original-client drawing on zero-size frames. Those requirements remain
separate in the ledger.
The current queue is 1148 implemented, 1596 pending, 3892 unreviewed,
1180 context, 24 excluded and 43 blocked source blocks.

## Connected COLORMODE component precision

The macro callback now accepts and reports RGB precisions 1..8, edits defined
targets through the documented protection bypass and leaves selection, other
fields and other records intact. Wire `M` continues using the protected setter
and its distinct bits=8 activation gate. Undefined COLORMODE getters return
`-1` without variable error or implicit definition. Syntax, undefined setters
and out-of-range precision recover nonfatally.

Existing CM consumers now carry the actual precision rather than a Boolean
eight-bit test: foreground, background, fill, lines, curves, polygons, rounded
and skewed contours, Beziers, classic/outline text and graphics-window erasure.
Nonzero precision always decodes CM fields in UltraNum, independently of global
Base Math. Packed blue/green/red N-bit components normalize to the full 0..255
range with nearest rounding; unused bits above the three fields are ignored.
The resulting RGB value uses the existing current-palette closest match in
the declared 256-color rendering model. This does not claim a truecolor backing
surface or completion of unfinished CM-bearing command families.

Native tests cover all precisions, protected current/noncurrent setters,
selection/undefined/error behavior and independent packed/RGB pairs, including
two-bit (1,2,3) → (85,170,255), three-bit (1,3,5) → (36,109,182) and the corrected
eight-bit 0x04140c. Lowercase UltraNum digits catch accidental MegaNum decoding.
Foreground, fill and background operations resolve to deliberately installed
palette colors; invalid service arguments preserve outputs. CP437/UTF-8 TCP
tests on all five existing entry rasters draw exact green points, blue fills
and red erasures at every precision. They also check M17 retaining a macro's
three-bit mode, undefined-query readback, invalid-mode recovery and a protected
STYLE not blocking ENV changes or drawing.

Native CMake configure/full build and all 38 affected CTests pass without new
C compiler warnings. The shared conversion changed multiple drawing consumers,
so their command tests ran once with the focused environment/palette/query and
connected tests. Pure geometry/kernel checks already passed and were reused.
RIP1.54's 93 frozen files and 41 helpers remain unchanged. The pin remains
93fd0c3; corpus replay remains deferred and the full goal remains incomplete.
The current queue is 1168 implemented, 1586 pending, 3882 unreviewed,
1180 context, 24 excluded and 43 blocked source blocks.

## Stable PR #103 contract adoption

The reference now pins stable ff2043e344e0c0df09d369bea076e781781345f3.
Three changed normative documents and the new extended-window derivation are
reviewed; B101 is resolved as documentation. Full-window absolute g/H acceptance
is reopened because the connected implementation still uses origin-aware CUP.
Extended b, erase controls and macro cursor controls remain pending. B104/B105
are unchanged. No corpus replay runs before full implementation.

Exact source matching retains 7864 reviews, moves 1478 line identities and
replaces 19 old blocks with 43 new ones, giving 7907 total. Nine unchanged cursor
metadata claims and two changed cursor behavior claims are reopened/removed;
the adoption record retains their former evidence. The queue is now 1157
implemented, 1641 pending, 3873 unreviewed, 1184 context, 24 excluded and 28 blocked.
Native CMake configure/build and the three reference/preservation CTests pass;
strict reindexing against the extracted pinned source preserves the ledger byte
for byte. Already passing drawing tests are reused for this documentation-only
milestone. RIP1.54's frozen files/helpers remain unchanged.

## Connected absolute RIP cursor positioning after PR #103

Wire g/H on the entry TTY and positioned default/current window now address the
full text window independently of ANSI origin mode and both scrolling-margin
axes. Clamp before narrowing, commit Unicode, clear pending wrap and preserve
origin/margins, colors and saved cursor. Existing radix/field validation,
deactivated-window no-ops and nonfatal parser/renderer diagnostics remain.
Complete TW tables, protected-definition lifecycle and TWGOTO/TWHOME macros are
still unfinished; their broad behavior paragraphs remain pending.

An explicit RIP3-session-owner flag confines the native rendering adaptation.
The console cursor uses full physical window bounds; the separate ANSI addressing
rectangle still supplies origin-relative coordinates and erase limits. Signed
coordinate conversion permits a cursor above/left of that rectangle. Text to the
right of the horizontal margin reaches the physical edge and can wrap without
scrolling the ANSI region; ordinary text inside the margins still scrolls that
region. Direct/legacy CTerm paths retain their previous addressing behavior.

Independent native tests first reproduced origin-mode home failure and then
caught text suppression beyond the horizontal margin. CP437 and UTF-8 cell
expectations now cover full-window endpoints, movement without repaint, pending
wrap, text outside all margins, outside-margin wrapping, confined scrolling,
ANSI CUP and saved-cursor restoration, and EL's retained addressing limit.
Connected CP437/UTF-8 captures exercise entry grids and positioned windows with
both margin axes on all five existing rasters, including unchanged artwork and
following text at full-window corners. The initial test incorrectly expected
bright white from the root legacy color alias; the corrected fixture explicitly
requests normal ANSI gray (0xa8a8a8), independently of the cursor expectations.

Native Release CMake builds and affected CTests pass without new compiler
warnings. The final run includes both connected suites, native CTerm/text/stream
checks, legacy RIP live rendering and the frozen-source guard. Earlier unaffected
term/legacy Unicode/oracle checks remain valid. The contract checker accepts the
updated evidence. RIP1.54's 93 files/41 helper definitions remain frozen.
Eleven command metadata/basic-activation blocks are accepted again; broad TW
protection/macro paragraphs remain pending. The queue is 1168 implemented, 1630
pending, 3873 unreviewed, 1184 context, 24 excluded and 28 blocked. The reference
remains ff2043e; reset orchestration (#105), extended b and the larger goal remain
incomplete. Existing-art replay is still deferred.

## Connected cursor-control variables

CON, COFF, TWGOTO and TWHOME now execute through immediate host-command queries
on real connections. Their fully validated decimal selectors share ATW/DTW's
current/number/ALL list handling where documented. CON/COFF omit to CUR, accept
lists and ignore undefined/deactivated targets. TWHOME accepts one optional
current/number target; TWGOTO accepts one target and up to two optional decimal
coordinates or CUR axes. No arguments or only WINNO is a validated no-op.

Cursor position requests retain optional axes of their chosen target and clamp
against its actual full-window dimensions before narrowing. Legal 20-digit
coordinate arguments beyond uint64 saturate to UINT64_MAX, which is still beyond
all accepted device/window dimensions; remaining characters are validated before
any callback. They therefore reach the nearest border without byte/word wrap.
ENV base math does not change macro decimal data. Native movement retains ANSI
origin/margins and saved cursor, and remains distinct from ANSI CUP.

A RIP3-only native visibility operation retains the visible cursor mode and
style, changes no received bytes, and survives later ordinary text. Its saved
mode resets with ANSI reset. The adapter restores any pending ATW renderer gate
before a same-expansion CON/COFF effect, so a previously saved visible cursor
cannot overwrite a subsequent COFF at the reply fence. Deactivated cursor
requests change neither live position nor saved visibility.

Host tests first reproduced unsupported TWGOTO, then exercised noncurrent
callback targets, chosen-target CUR axes, full-range dimensions, 20-digit clamp,
late syntax rejection before current lookup/effects, undefined/inactive gates,
selectors, missing services and failure recovery. Session tests verify native
callback routing, no-coordinate no-ops and nonfatal malformed/IO/resource errors.
Native CTerm tests cover visibility/style persistence, later text, unchanged
position/attributes/origin, pending wrap and reset. A dedicated connected suite
covers CP437 and UTF-8 on all three reference rasters, both the entry grid and a
positioned 8×6 window. One reply-fenced capture checks each related scene/state;
it covers both margin axes, unchanged pixels for control operations, saved
cursor, invalid trailing arguments, decimal data in UltraNum mode, inactive
requests, reactivation ordering and following text. Wire g still reaches the
full-window border through the shared adapter. Physical audio is disabled.

Native Release CMake configure/full build and all 13 affected CTests pass without
new compiler warnings, including the ledger checker and RIP1.54 source guard.
The 93 frozen files and 41 helper bodies remain unchanged. Source metadata/basic
predicates gain 19 implemented blocks; 11 descriptive/historical blocks are
classified as context. The full noncurrent-window/protection paragraphs remain
pending: only the default/current slot has a connected owner, and other slots
are undefined until the complete TW table and owned-view lifecycle are added.
The queue is 1187 implemented, 1634 pending, 3839 unreviewed, 1195 context,
24 excluded and 28 blocked. The pin remains ff2043e, existing-art replay stays
deferred and full reset/header orchestration (#105) remains unresolved.

## Connected graphics-color text-window erasure

Wire e/> and host ETW now execute on connected entry and ordinary positioned
current/default windows. Whole erase uses index 0 of the current graphics PAL
and homes only the live cursor, even with zero WORLD or RGB input mode selected.
Wire > resolves STYLE background through ENV component precision/current PAL,
erases the cursor-inclusive full display row and retains its cursor/pending wrap.
ANSI background does not select either paint. RGB resolution uses the existing
256-index renderer; broader truecolor-surface acceptance remains unfinished.

A narrow absolute screen fill applies COPY to the shared RIP index plane without
selecting/changing PORT, viewport, protection, fill pattern, write mode, cursor
or graphics state. It marks damage even for unchanged indexes, so later ANSI ink
is overwritten by repeated identical erase. The adapter presents preceding
graphics before importing a native ANSI surface, paints solid RGB blank cells
without inherited underline/inverse decoration, and returns the same physically
clipped DEVICE region to the session's backing-plane fill. Whole erasure homes
via the native full-window cursor service. Native rendition, modes, margins,
saved cursor and visibility remain untouched. Geometry stays wide until screen
clipping. Undefined slots and deactivated TTY ignore valid operations; argument
validation and renderer failures remain within the nonfatal parser boundary.

Independent native tests cover a protected/inactive current offscreen PORT,
physical clipping, unchanged offscreen pixels/selection, repeated same-index
damage, distinct palette-0/STYLE paint, ENV precision,
late selector rejection, missing services and IO/resource failures. CTerm tests
check literal RGB pixels, no decoration leakage, full versus EOL geometry,
retained attributes/origin/margins/saved cursor, and pending-wrap behavior in
both encodings. Connected CP437/UTF-8 fixtures cover all three reference rasters,
both the entry grid and an ordinary positioned 8×6 window. They distinguish red
graphics index 0, blue STYLE background and red ANSI background while graphics
viewport is inactive, W is XOR and fill is empty. Palette edits after erasure
prove backing indexes changed; repeated paint removes intervening CP437/UTF-8
ink. Border strips/corners retain independent green artwork. ETW in RGB mode with
WORLD=0, saved-cursor restore, inactive requests and malformed command recovery
are checked through one reply-fenced capture per related scene/state. Physical
audio is disabled and no existing-art corpus is replayed.

One synthetic setup initially supplied four-digit f fields while XY width was
two. Correct the setup streams to the documented current-width fields without
changing erase expectations. The 13 affected native/connected CTests pass after
that correction; only the failed native test and changed connected fixtures
were rerun. The ledger checker also passes, for 14 affected checks total. Native
Release CMake configure/full build has no new compiler warnings, and the 93
frozen RIP1.54 files/41 helper bodies remain unchanged.

Eighteen source metadata/basic-color/gate blocks are accepted and thirteen
attribution/description blocks classified as context. Complete extended-b
bounding margins, defined noncurrent TW/protection lifecycle, and the separate
ANSI-background TWERASEEOL family remain pending; their broad paragraphs are not
accepted by this milestone. The queue is 1205 implemented, 1632 pending, 3810
unreviewed, 1208 context, 24 excluded and 28 blocked. The reference remains
ff2043e and full scene-reset/header orchestration (#105) remains unresolved.

## Connected extended text windows

Level-0 `b` now reaches the native window renderer in live connections. All
numeric fields use the current radix and coordinate width; validation includes
shortened or omitted final reserved fields and unknown flags before effects.
The fitter floors both WORLD endpoints independently, orders exclusive bounds,
ignores graphics PORT/viewport state and retains wide coordinates until actual
screen clipping. It follows the documented directional transition table using
the original requested count at every step, then reduces counts or extends an
undersized axis to one cell. Fitting and glyph rendering share the immutable MAF
provider, including the 1024x768 20x24 font-4 cells and 640x480 fallback bank.

The native adapter reinitializes each accepted extended definition, including
identical ones. Pending ANSI syntax and global client options survive; tabs,
rendition, origin/margins and live/saved cursor reset. Cursor hiding persists
across definitions unless explicitly enabled. Flag 16 paints the entire adjusted
bounding box in both native pixels and the graphics index plane before flag 4
deactivates output. Whole-window `e`/ETW now cover extended margins too. Nonzero
TW tables/protection/query variables and complete charset reset remain pending;
this milestone does not accept the broad mixed requirements for those features.
The separate scene-reset `*` contract remains unresolved under #105.

Independent native vectors cover all directional transitions, floor/reversal,
odd centering, minimum dimensions, unavailable metrics, zero WORLD and coordinates
beyond 32 bits. Real TCP/control-bitmap captures in CP437 and UTF-8 exercise
640x480, 800x600, 1024x768 and the 720x400 fallback without changing the entry's
raster/grid. They cover repeated definitions, partial ANSI, tab reset, clear
ordering, hidden/deactivated cursors, palette-sensitive margin erasure, offscreen
placement, allocation rejection, recovery and non-ASCII text. Physical audio is
disabled and existing RIP-art corpus replay remains deferred.

The native Release CMake configure/build and 19 affected CTests pass, including
existing connected window/cursor/erase suites and the RIP1.54 source guard.
Only the strengthened split-input/reserved-field session test was repeated after
its test edit; unchanged passing suites were reused. The edited ledger check
also passes, for 20 distinct affected checks. No compiler warnings were introduced.
The queue now contains 1232 implemented, 1618 pending, 3788 unreviewed, 1217 context,
24 excluded and 28 blocked blocks. The reference remains ff2043e; frozen RIP1.54
sources/helpers, baseline and historical assets are unchanged. Task-owned build
logs are removed after review; no code or issue updates are published.

## Connected ANSI-background end-of-line erasure

`$TWERASEEOL$` now calls the connected target's native ANSI renderer. It shares
selector parsing with ETW, including case-insensitive CUR/ALL, decimal slots,
deduplication and whole-list validation before even looking up the current slot.
Its separate owner callback uses the native ANSI background rather than graphics
STYLE or PAL0. Indexed backgrounds retain the configured ANSI color mapping;
direct ANSI RGB is preserved. Like ordinary ANSI output, it paints the terminal
surface without rewriting the separate graphics index plane. Complete SCREEN/
truecolor composition remains a separate unfinished family.

The native erase covers the cursor-inclusive full display row independently of
ANSI origin/scrolling margins and graphics PORT/viewport, W or fill state. Solid
blank cells remove prior glyph/underline ink without changing rendition or
hyperlink state. Live/saved cursor, requested visibility, pending wrap and
unfinished received ANSI syntax survive. Current/default root, ordinary w and
extended b windows are connected; full defined noncurrent TW views and protection
still require the pending TW table. Mixed requirements describing that lifecycle
remain pending rather than being accepted from the default-window tests.

The host test first failed on the previously unsupported variable. New native
expectations cover a deliberately remapped ANSI palette entry, direct RGB,
full-row erasure outside margins, hidden cursor, saved position, pending wrap,
partial CSI and both encodings. The real TCP/control-bitmap erase suites cover
all three reference rasters in CP437 and UTF-8, including Custom's existing
monochrome root mapping, color MicroANSI windows, untouched extended margins,
non-ASCII ink, invalid selectors with no effects, deactivation and reply-fenced
recovery. Audio remains disabled. The pending-wrap case explicitly enables wrap
on its extended definition.

Native Release CMake configure/build completes without warnings. Nine distinct
affected CTests pass: text-window host, session, native CTerm, both connected
erase and both extended-window suites, the RIP1.54 source guard and the edited
contract ledger. Only the new tests were repeated while their palette/wrap
fixtures were corrected; unchanged passing suites were reused. Task-owned logs
and failure fixtures are removed. The queue is 1236 implemented, 1617 pending,
3781 unreviewed, 1221 context, 24 excluded and 28 blocked blocks. The reference
remains ff2043e, and no legacy source/assets, freeze baseline or issue content
changes. Full compliance and scene reset (#105) remain incomplete.

## Connected text-window selection, protection and noncurrent operations

The session now owns 36 TW slots with independent definition, activation and
protection state. Wire `2T` and `$CUR(TW,n)$` select existing windows without
redefining them; an ordinary unprotected first-use copy prepares independent
native window state/fonts/tabs before publishing the new slot. Each view retains
its geometry, live/saved cursor, rendition, margins, wrap and requested cursor
visibility. The entry's root grid/raster remains unchanged. Native font copies
own their rasters/mappings after source destruction and never hold two console
locks simultaneously. All private window pixel planes share one finite budget;
failed construction/selection retains the prior selected slot and connection.

`INUSE(TW,...)`, individual `ISPROT`, `PROT`/`UNPROT`, TWIN and ISEXTWIN are
connected to the slot owner. Generic table numeric selectors remain 1–35; CUR
addresses working slot zero for protection queries. Cursor/erase selectors use
0–35 as documented. Protected definitions reject ordinary/extended redefinition
and all-zero w. ATW/DTW skip protected slots, including a protected inactive slot;
CON/COFF, TWGOTO/TWHOME and e/ETW/TWERASEEOL remain permitted. Noncurrent targets
receive their own native cursor/color/layout state while the selected cursor is
restored over freshly imported artwork, without replaying stale pixels. Extended
flag 32 now protects nonzero slots after definition/clear; zero remains
unprotectable. ETW(ALL) homes every active target without changing selection.

[Issue #106](https://github.com/bbs-land/remote-imaging-protocol/issues/106) requests
the unresolved first-use copy/protection and partial ANSI selection boundary,
referencing #100/#101 and PR #102/#103 without modifying those issues. Selection
from a protected source into an unused slot and selection across unfinished
ordinary ANSI reject nonfatally until a contract is available; later completion
on the old owner still works. The broad first-use paragraph is blocked, not
accepted from the common-copy tests. Exact charset/reset acceptance (#104), full
TW backups/entry COPY/reset, MTW, and remaining coordinate/font metadata queries
stay pending. Full scene reset remains under #105.

Independent native tests cover owned font artwork after source destruction,
separate tab/color buffers, saved/live cursor and margins, selected-slot metadata,
protection gates and nonfatal selection/resource failures. Real TCP/bitmap tests
exercise all 36 slots on each reference raster in CP437 and UTF-8, root and
MicroANSI font preservation, inactive first-use state, protected definitions,
protected activation gates, noncurrent cursor/erase effects, extended flag-32
protection, ETW(ALL), both numeric bases and cumulative allocation rejection.
Physical audio is disabled and existing RIP-art replay remains deferred.

The native Release CMake configure/build and 20 distinct affected CTests pass,
including existing connection/cursor/erase/extended-window suites and the frozen
RIP1.54 guard. Only the changed selection/protection checks were repeated after
the final focused corrections; unaffected passing suites were reused. Compiler
logs contain no warnings. The queue is 1283 implemented, 1585 pending, 3752
unreviewed, 1234 context, 24 excluded and 29 blocked blocks. Stable remains
ff2043e. Task-owned build logs, issue payload and failure fixtures are removed;
no code is pushed and no existing issue receives comments.

## Scope constraint: existing ANSI engine only

The user explicitly ruled out adding a VT-102 mode through the RIPv3 work,
regardless of upstream documentation or PR content. This policy is recorded
above as a contract exception and in blocker B104's implementation scope.
The reference's VT-102-only mode/keymap rows are excluded; shared ANSI commands,
CP437 handling and applicable ANSI charset questions are reviewed independently.
No parser, native terminal behavior, profile option or UI is changed by this
scope record, and no existing upstream issue is modified.

The queue is now 1283 implemented, 1585 pending, 3750 unreviewed, 1234 context,
26 excluded and 29 blocked blocks. The source pin and RIP1.54 freeze remain
unchanged. Native CMake configure/build and the edited contract check pass;
previous successful runtime tests are reused for this documentation-only change.

## Connected window metadata and ANSI-only PR #107 adoption

The current/default/noncurrent metadata families now use a read-only native
snapshot callback. It observes target cursor coordinates and requested visibility
without selecting a slot, presenting a surface or changing pending wrap. Protocol
w/b definitions supply established font and unclipped geometry. TWW/TWH support
cell counts, bounding/display/cell DEVICE sizes and current-WORLD conversion;
TWX0/TWX1/TWY0/TWY1 return ordinary stored cell coordinates when type is omitted,
-1 for that extended-window request, and explicit graphical DEVICE/WORLD edges.
Graphical far edges are exclusive. Division decomposes the integer ratio before
multiplying, so legal wide offscreen origins do not overflow an intermediate
pixel-times-WORLD product. Neither graphics PORT clipping nor physical screen
clipping changes query geometry. Inactive/undefined replies do not call the
native snapshot service. Syntax/reply/resource failures stay inside RIP3.

[Issue #108](https://github.com/bbs-land/remote-imaging-protocol/issues/108) records
conflicting omitted-domain and COLS/ROWS grammar. Far-edge explicit TEXT without
a domain, and its disputed cell-domain forms, remain unsupported/nonfatal.
Known explicit DEVICE/WORLD forms and ordinary omitted-type coordinates work
now. TWFONT works after a protocol definition; the client's unredefined startup
font remains unsupported under #105. MTW and TW backup/copy/reset still need
implementation. No broader completed-family or full-compliance claim is made.

The fork stable reference advanced to ba97c882b2d5ea13fd5310431f3442fc9cbc9f9f,
including reviewed PR #107 head 16164ae796c6d81da4e7488b0d47900dd7c59461.
Its ANSI-only contract resolves B104 as documentation, independently of runtime
acceptance. All VT-102-only translations/vectors remain excluded by the user's
scope constraint. The new separate CSI s/u coordinates versus ESC7/8 full-TW
snapshot contract reopens the earlier saved-cursor claim; old passing native
ESC8-home fixtures do not establish that new behavior. Original RIP1.54 source,
assets, expectations and preservation baseline are unchanged.

The native Release CMake configure/build and 12 distinct affected CTests pass,
including host parsing/session failure recovery, new CP437/UTF-8 TCP query suites,
the existing connected 36-slot suites, source freeze and reference-adoption
checks. Literal geometry/cursor/font replies cover three reference rasters and
the 720x400 MAF fallback, live WORLD changes, zero-WORLD recovery, hidden cursors,
noncurrent reads, all five defined fonts, inactive sentinels, unchanged pixels/
cursor/wrap and origins beyond 32 bits. The native vector also checks a ratio
whose direct intermediate product would exceed 64 bits. Physical audio is off.
No existing-art corpus replay is performed. Strict reindexing of the downloaded
reference preserves the edited ledger byte-for-byte. The queue is 1383 implemented,
1594 pending, 3584 unreviewed, 1307 context, 67 excluded and 36 blocked blocks
across 7971 units. Owned archive/extraction, issue payload and logs are removed;
no existing issue is modified and no code is published.

## Adopted outer reset and connected selection continuation

Fork stable 568d8063c68bd009cf687fb3809f547101949727 incorporates reviewed
[PR #109](https://github.com/bbs-land/remote-imaging-protocol/pull/109) and
[PR #110](https://github.com/bbs-land/remote-imaging-protocol/pull/110).
B105 and B106 are resolved as documentation. The outer SOFT contract specifies
physical RGB-black clearing before palette reset, retained PAL selection,
family-specific protection and old-current ANSI/ENV effects, preserved backups,
variables/templates, input lock and audio. That orchestration is still pending;
`*` is not accepted as working. No existing issue receives additional comments.

Connected selection now follows the distinct first-use paths: wire `2T` clones
represented native window state into an independently owned unprotected slot,
including when its source is protected; `$CUR(TW,n)$` creates an unused slot with
fresh full-screen ordinary geometry, normal attributes/cursors/margins and an
empty explicit tab map. The session accepts a client default font/wrap policy;
the current application supplies font 0 and wrap enabled. This does not change
the entry raster, initial TTY grid or existing defined slots. Existing ANSI-BBS
tab behavior remains in use. Existing protected targets remain selectable and
inactive wire clones remain inactive.

Ordinary unfinished ESC/CSI and supported OSC syntax now follows the connection
into an existing, cloned or fresh default window. Tab maps and other per-window
state remain independent; preparation failures retain the previous selection and
recover inside RIP3. Source buffers can be destroyed after native preparation
without invalidating continuation. Selection imports current artwork before
presenting the target cursor; it copies no cells/history and preserves pixels.
Charset designation, resident TW queries and the interpreter-owned ESC7/8
snapshot remain pending. The focused saved-coordinate tests now use CSI s/u,
so their results do not imply that the larger ESC7/8 snapshot is implemented.
No VT-102 mode, selector, toggle or keymap is added or authorized.

The native Release CMake configure/build and 13 distinct affected CTests pass,
including split CSI/OSC at every boundary, CSI saved coordinates, fresh defaults,
protected-source cloning, resource recovery, host/table services, connected
CP437/UTF-8 selection and geometry queries, and preservation/adoption guards.
Connected captures use three reference rasters; geometry queries also cover
720x400. Physical audio is disabled; existing art replay remains deferred.
No compiler warnings are introduced. Strict reference reindexing is byte-identical
and all 51 derivation hashes match. The queue is 1394 implemented, 1645 pending,
3578 unreviewed, 1317 context, 67 excluded and 33 blocked across 8034 units.
All 93 frozen files and 41 helpers match the unchanged baseline and its two
historical exceptions. Task-owned archives, extraction, scripts, logs and failure
fixtures are removed; no code is pushed or PR published.

## Connected SOFT reset and native shared text raster

Wire `*` and `$RESET(SOFT)$` now invoke one connected reset service. It discards
unprotected live nonzero ports and text windows, preserves protected survivors,
selects TW/STYLE/PORT/ENV 0 and retains PAL selection, even when the selected PAL
becomes undefined. The physical screen clear is literal RGB black before PAL
defaults are applied. A protected current PAL with red entry 0 therefore leaves
a black cleared canvas; later indexed drawing still uses its red entry 0.
The private v3 graphics surface retains the literal clear across palette uploads,
COPY and backups. Indexed writes replace it normally; this does not claim general
RGB raster-operation support.

The old-current native ANSI window receives represented color/rendition, full
vertical margins, saved-coordinate and default-tab resets before TW0 is defined.
An inactive old-current window keeps its live cursor; protected noncurrent
windows keep their state. Pending ordinary ANSI syntax follows the connection
into TW0; its existing explicit tabs survive redefinition when it was noncurrent.
STYLE protection follows the portable contract, and protected departing ENV and
old-current PORT image-style exceptions are applied. Named variables, templates
and separately owned represented backups survive. A deleted clipboard loses its
assignment; a protected clipboard and its pixels survive. Invalid reset fields
recover inside RIP3 and leave the connection usable.

Positioned text windows now use the existing console rasterizer directly over
the display's two pixel planes. They own their logical cells, fonts and ANSI
state, without another pair of pixel buffers or a full-image import/export on
each write. All raster access uses the parent's state lock and circular pixel-row
mapping. The existing cell graphics/dirty flags and dirty-row range publish
changed cells to the parent; there is no second damage accumulator or revision
counter. Plain ANSI and RIP1 cell writes have no added damage-tracking hook.
Parent raster replacement is busy while a view borrows it; views are destroyed
before the parent. Positioned scrolling clips both planes without changing the
parent's existing full-screen/status-row scrolling optimizations.

Local drag selection reads the current native grid and font mapping, translating
mouse pixels using its origin and cell size. Host input and clipboard ownership
stay on the display console. Selection completion and interruption restore both
pixel planes. Suspended terminal UI keeps its existing screen selection path.
The application bell remains unchanged. Rendering vectors send no sound commands;
test fixture audio policy is independent of the application's bell behavior.

Independent connected reset captures cover CP437 and UTF-8 on 640x480, 800x600,
1024x768 and 720x400, including unused physical edge space, protected active and
inactive windows, selected PAL retention, old-current image style, surviving
backups/templates/variables, clipboard deletion and split ANSI continuation.
Connected selection captures include non-ASCII text, a literal pipe, reset from
an entry grid, an arbitrary pixel origin, completion and cancellation. A native
test checks existing dirty bits, clipped partial cells, offscreen 64-bit origins,
both planes across parent circular wrap, scrolling, snapshots and raster
replacement rejection.

The native Release CMake configure/build and 25 distinct affected CTests pass,
including the native X11 test on a private Xvfb, ANSI stream consistency, RIP1
historical live/wire pixel expectations and both preservation guards. Native
STYLE/ENV/surface reset checks also pass. All 93 frozen files and 41 helpers
still match the unchanged baseline and its two historical exceptions. No new
compiler warnings are present. Paired measurements against `d66edaf5` show
MicroANSI single-cell reply latency falling from 16.65 ms to 0.24 ms, with no
detected console CPU or RIP1 graphics slowdown in the measured workloads.
Full measurements, native reply-fence limits and reproduction commands are in
[`performance.adoc`](performance.adoc#_2026_11_20_rip3_shared_raster_comparison).

Full `*` compliance is not claimed: button instances, mouse fields,
HEADER revision/input lock, resident queries, charset/insert-mode state and the
client's default font/wrap selection remain pending where applicable. No VT-102
mode is introduced. That milestone's source queue had 1406 implemented, 1632 pending,
3578 unreviewed, 1318 context, 67 excluded and 33 blocked units. Incomplete clauses
retain their implemented subset and test evidence without being marked complete.

## Connected button-style records and lifecycle

The separate RIP3 session now owns all 36 button-style records and their typed
backup area. Wire `1B` edits the selected style; `2B` and `$CUR(BUT,n)$` select
defined records or install the fixed defaults for ordinary first use. The wire
parser accepts current coordinate widths 2–5 and both numeric bases, converts
width/height and bevel once in the definition-time WORLD, and stores the device
WORD values. Bevel uses X scale only, and no-BEVEL styles store size zero. All
seven colors use palette indices, including in RGB mode. Multiple type bits and
inapplicable flags remain stored; `other_port` is ignored. Rendering precedence
and creation-time clipboard checks await the button renderer.

Current selection, in-use/protection queries, slot protection, `$SBS$`/`$RBS$`,
generic whole-table `$COPY$`, and BUT live/backup `$RESET$` selectors reach those
same records on connections. Snapshots own the complete table and its saved
current index; restoration bypasses live protection and restores that index,
unlike the separate STYLE restore defect. Numbered protection/consumption and the
shared ten-slot/stack capacity use the existing backup owner.

Numeric and table resets default eligible records and clear nonzero in-use
flags. SOFT uses that ordinary reset and separately selects zero, keeping
protected live styles and independent snapshots. Current-entry reset instead
safely models the published RIPtel defect: records and protection remain intact,
selection becomes zero, and default-record bytes overwrite the first 26 in-use
bytes. No invalid memory copy or native structure layout is reproduced.

New [issue #112](https://github.com/bbs-land/remote-imaging-protocol/issues/112)
asks for subsequent writes, selection and protection when a current or protected
record has become undefined. It references #27/PR #61/PR #71 and #16/PR #45;
none of those existing issues receives a comment. Undefined-current writes,
protected-but-undefined first use and undefined-current protection queries
are rejected nonfatally rather than acquiring an unreviewed behavior. Complete
clauses affected by B112 retain their pending/blocked status.

Independent native expectations cover all 65,536 low-word flag values, every
flags-2 byte, fixed defaults, protected edits, definition-time nonuniform scaling,
RGB-mode palette fields, safe zero-WORLD rejection, shipped reset metadata and
snapshot restoration. CP437/UTF-8 connection fixtures exercise all 36 slots,
widths 2–5, both numeric bases, base/slot/stack aliases, protected saves,
RESET(ALL), SOFT retention and malformed-input recovery on the three reference
rasters plus 720x400. Definition/selection captures retain both pixel planes.
The native Release CMake configure/build and 11 distinct affected CTests pass,
including existing table/palette and SOFT regression checks and both preservation
guards. No compiler warnings are introduced, and no ANSI music tests are run.

Button creation (`1U`), faces/effects/labels, icon/hot-state rendering, click and
hotkey dispatch, groups and auto-clip remain unimplemented. Connected style
management does not constitute usable clickable buttons. Intra-table live-entry
COPY and B112 state edges also remain pending. The queue now has 1448 implemented,
1638 pending, 3528 unreviewed, 1318 context, 67 excluded and 35 blocked units.

## Connected simple mouse fields

`1M` fields with `clr=0` and no identity prefix now participate in real connected
mouse input. The session owns one bounded 128-entry region list, with newest
definitions winning overlaps. Geometry uses the current WORLD and PORT0's
viewport, even while another screen or offscreen port is current, then remains
in absolute DEVICE coordinates after later viewport changes. `1K`, `$MKILL$`
and SOFT delete those records. The same owner is intended for mouse buttons;
`1U` creation and button rendering are still pending.

Clicks run an independent copy of the field program through the connected host
evaluator, including control notation, UTF-8/CP437 transport, active variables
and delay continuations. A program may delete its own field safely. Captured
`XY(DEVICE)`/`XY(WORLD)` values come from the committing event and persist across
its delay; a later click cannot overwrite a pending host program. Rejected
definitions, unsupported operations and host failures stay below the nonfatal
parser/input boundary. No mouse-device-presence value is invented for MSTAT.

Press feedback uses the actual native text/graphics raster and both blink
planes. Only the currently pressed rectangle has a temporary capture, preserving
original palette indexes. Incoming drawing removes the effect, updates the
normal shared raster, then reapplies it at the batch boundary. Individual wire
bytes do not repeatedly capture or repaint the rectangle. Inversion follows
the console's existing circular-row mapping and cell damage; ordinary ANSI and
RIP1 render paths gain no damage tracking or rendering buffer. The effective
session mouse mode reaches Wren's existing `CTerm.mouseMode`, so the default
drag-selection hook leaves field gestures to the RIPv3 owner. Custom hooks and
the remote-mouse gate retain precedence; consumed events cancel field capture.

New [issue #113](https://github.com/bbs-land/remote-imaging-protocol/issues/113)
requests the missing press/release/cancellation contract, identity grammar,
FIELDID lifetime and inactive/empty geometry rules. It references earlier
issues/PRs without commenting on them. Until resolved, the explicit client
gesture policy captures on left press, commits release inside and cancels
release outside. `ID=n:` definitions, inactive/wholly clipped definitions and
reversed corners are rejected nonfatally. Clear-and-zoom (`clr=1`), identity,
hover queries, selective deletion, mouse backups, pointer shapes, buttons and
remaining host facilities are not complete. The queue has 1460 implemented,
1648 pending, 3500 unreviewed, 1318 context, 67 excluded and 41 blocked units.

Independent tests exercise the shared capacity, newest-first hits, self-deleting
programs, numeric bases and coordinate widths, nonuniform WORLD projection,
PORT0 placement with another/offscreen port selected, viewport clipping and
retained geometry, delayed captured XY, SOFT deletion and malformed recovery.
CP437/UTF-8 bitmap connections also verify native text/graphics feedback, both
blink planes and incoming drawing during a press. A native console test checks
indexed/RGB inversion, large clipped extents and preservation of circular-row
mapping and existing cell flags. The button-style fixture's radix selector now
uses the required base-36 `J1S` encoding for 64, correcting its earlier decimal
test input. Rendering fixtures send no audio commands; existing ANSI music tests
are not part of this milestone's verification.
The native Release CMake build introduces no warnings. All 15 selected CTests
pass, including the CP437/UTF-8 RIP1 raster oracles, existing selection/reset
fixtures, corrected button-style fixtures and the three preservation/contract
guards. The native bitmap test covers both text-only and full-raster scrolling,
fixed right/bottom margins and restoration of the original indexed planes.

## PR #111/#114/#115 implementation follow-through

The pin now advances to fork stable
`ebd81bcbb29eeca2d2c283d40221cbabb3cee531`. The adoption record retains the
reviewed PR heads, complete source/claim migration and 54 derivation digests.
Documentation answers for B108/B112/B113 are adopted without treating unfinished
runtime clauses as complete.

Explicit TEXT far-edge queries now use WORLD by default. COLS/ROWS are syntax
errors for position queries; dimension queries retain only their matching count
domain. CP437/UTF-8 connections run the PR111 literal 800x600 ordinary and
extended definitions and all published position/dimension result combinations,
querying noncurrent windows without changing pixels or cursor state. Native
checks retain invalid-call and inactive/undefined behavior.

Successful `1B` defines an unprotected current slot even after a numeric/table
reset made it unused. Invalid definitions leave it unused. Selection of an
undefined stale-protected slot installs all fixed defaults, defines it and
clears protection; defined protected records retain their state. ISPROT(BUT,CUR)
reads raw protection independently of definition, while numeric ISPROT retains
its undefined sentinel. Native tests verify retained supplied fields; connected
queries run the published state sequences. The examples' final `1U` rendering
remains pending and is not claimed by those style tests.

Simple fields parse exactly one case-insensitive `ID=` prefix, one to five ASCII
decimal digits and range 0..32767. Invalid prefixes retain their entire original
payload with identity -1. Duplicate IDs do not merge instances or retarget an
existing press. FIELDID reads an immutable callback snapshot, survives deletion
of its source field and is unavailable in unrelated queries. The logical host
program carries that snapshot across its internal budget/delay/prompt yields;
each execution pump restores the caller context before returning. Later wire
work does not inherit it. Cancellation discards pending field-program context.
Hover dispatch and nested playback still require their respective owners and
remain pending; primitive context save/restore is not full playback coverage.

The native Release CMake build is warning-free. Fifteen distinct affected
CTests pass: native owner/state/command/geometry/session tests, CP437/UTF-8
connected mouse/style/geometry suites and preservation/contract guards. Session
allocation fault injection now covers the button-style owner, its backup and
mouse owner as well as all previous construction boundaries. Every injected
failure retains the caller result and releases owned resources. The queue has
1511 implemented, 1693 pending, 3497 unreviewed, 1278 context, 67 excluded and
25 blocked units. No full RIP3 compliance claim is made.

## Connected plain buttons and PR #117 payload contract

The reference pin advances to fork stable
`1b8ab9928a9fd9071556cf0228ea69a496db3e72`, including reviewed PR #117.
B116 is answered. All 68 normative documents remain indexed: 8039 exact
source/heading/text matches, 513 relocated blocks, 32 retired blocks and 60
new blocks yield 8099 units. No prior implemented unit was removed. The 55
pinned derivations include the new #116 note and the explicitly recorded
correction of #112's label example; its style-state proof is unchanged.

Plain `1U` construction is now dispatched from connected sessions. It uses the
current button style, live ENV coordinate/radix rules and existing graphics/font
primitives. Complete fixed dimensions remain DEVICE values; mixed-zero sizes
use both command far corners. Nonmouse decorations draw in the current port
without consuming mouse records; mouse buttons draw in PORT0 and share the
existing 128-entry field owner with `1M`. Button painting preserves the current
port and its text cursor; preparing a Dixon label does not populate the ordinary
outline cursor-width cache. No new screen buffer or damage-tracking layer is
introduced, and the ANSI/RIP1 drawing paths are unchanged.

PR #117's pure payload parser scans one identity prefix, removes leading ASCII
whitespace from the remaining whole payload and splits only the first two
literal `<>` pairs into fixed icon/label/host positions. Icon source is bounded
to 127 bytes and label source to 255 bytes before expansion. `Style B` is an icon
slot; `<>Style B` is the label form. `<><><>` retains literal `<>` as its host.
The corrected #112 command now visibly renders its red label in the connected
style-state test. Invalid raw fields fail before painting or field insertion.

Host source remains opaque at definition, including for preselected buttons.
Pointer release invokes the existing staged HCL owner in the current field
context/environment. Native tests fill template 3 with 4096 X bytes and verify
that subsequent delay, assignment and malformed RAND suffixes are clipped
without execution. Connected tests verify that a reached activation error keeps
the field and prior assignment effect, and the next activation still executes.
Reset buttons invoke outer SOFT before the copied host program; the callback's
FIELDID survives deletion of its source. Parser diagnostics remain nonfatal.

Simple fields and plain buttons now store half-open absolute DEVICE hit bounds.
Native/connected checks cover exclusive far edges, no near halo, bevel included
and recess excluded. Dynamic plain corners normalize in WORLD before conversion;
zero/collapsed faces skip before bevel. Inactive/empty effective destinations,
wholly clipped or merely touching intersections create no field. An unrelated
inactive/offscreen current port cannot block a valid PORT0 mouse definition.
Creation rectangles remain snapshots across later viewport movement/deactivation,
including while a gesture is held. Generic PORT renderer bounds remain inclusive
internally; adapters convert their far edges once.

Solid/pattern/no-fill faces, bevel/recess and basic classic/outline labels have
independent native and connected expectations. The code also renders chisel,
sunken, label orientations/indents, drop shadow and first hot-character
underline/highlight through existing primitives; detailed font/effect coverage
remains pending where the ledger does not yet support a complete claim.
ICON/clipboard/hot/auto faces, radio/check groups, keyboard/default-Enter
activation, graphical filename/label expansion, hover and selective deletion/
mouse-table save/restore remain unfinished. Literal filename validation is
shared with existing resource lookup; filename expansion is not claimed.

Native Release CMake builds and the affected native/CP437/UTF-8 CTests verify
this milestone. Connected button tests cover 640x480, 800x600, 1024x768 and an
arbitrary 720x400 raster. The ledger has 1565 implemented, 1711 pending,
3444 unreviewed, 1287 context, 67 excluded and 25 blocked units. The frozen
RIP1.54 implementation and fixture baseline remain unchanged. Full RIP3
compliance is not claimed.

## Connected clipboard button faces and remaining input/sizing questions

`1U` now reads an assigned active clipboard port's positive viewport for a
CLIPBOARD face. It precedes PLAIN when both bits are present. A missing/stale
clipboard rejects before painting or field insertion; `other_port` remains
ignored. The shared PORT blitter copies existing indexed pixels in COPY mode,
including overlap, without a new button image plane. Plain-only NOFILL/PATTERN
bits do not suppress or replace this image. Existing button effects, labels,
PORT0 mouse ownership and copied deferred host actions use the resulting face.

The inherited ALPHA 4 section 3.4.2.3 explicitly limits fixed style dimensions
to plain buttons and describes the entire clipboard viewport as the image.
Its immutable text digest is
`0da8a34644fa7c0ac7bf457ddfb6fe12288c1bff17c966f3d2b792af57c01982`.
The active-source implementation takes native DEVICE viewport dimensions,
ignores command far corners/style size and projects only the anchor through
live WORLD. Native expectations cover a cropped 3x2 source, protected source
contents, a 50x40 style, WORLD changes, half-open hit edges, aliased overlapping
copy, retained clipboard/current PORT and rejection of inactive sources.
Connected tests create a 5x3 clipboard via GET_IMAGE, erase its original screen
source, draw/click the copied face, and use PORT0 with another bitmap current.

The reconstructed size paragraph omits the inherited plain-only qualifier;
its nonplain inactive/physically clipped source and hot-image size cases also
need reconciliation. New parent issue [B119](https://github.com/bbs-land/remote-imaging-protocol/issues/119)
asks for an explicit per-type table. The affected full clauses remain blocked,
rather than treating the currently usable active viewport path as full image
button compliance. Inactive or wholly empty clipboard sources remain explicitly
unsupported and recover nonfatally. ICON/hot/auto faces and grouped buttons
remain unfinished.

New parent issue [B118](https://github.com/bbs-land/remote-imaging-protocol/issues/118)
asks for keyboard matching/case, NUL/high bytes, Enter/default/any-key priority,
consumption, commitment/repeat, callback coordinates and interaction with a held
pointer gesture. The existing pointer contracts expressly leave keyboard rules
separate; those buttons are not yet connected to keyboard input. Both issues
reference earlier answered issues/PRs without adding comments to them. The
stable pin remains `1b8ab9928a9fd9071556cf0228ea69a496db3e72`.

The native Release CMake build is warning-free. Six affected CTests pass:
the native button suite, both connected button encodings across four rasters,
and the contract/preservation guards. The queue now has 1565 implemented,
1710 pending, 3441 unreviewed, 1287 context, 67 excluded and 29 blocked units.
Full compliance remains unfinished; the RIP1.54 freeze is unchanged.

## Connected deletion-only enclosed fields

`1k` now reaches the shared field owner from connected sessions. Definitions
retain whether they came from a simple `1M` field or a mouse-active `1U` button;
exclusion bits 8/16 therefore work independently of public IDs. Mouse-table
compaction retains allocated surviving instances. Deleting another field does
not remove or move a held field's feedback/capture; deleting the captured
instance removes feedback and cancels before freeing it. A replacement with
the same ID/rectangle must receive a fresh physical press. Nonmouse decorations
remain outside the shared field table.

The currently implemented deletion-only policy uses positive half-open selectors
and contained/positive-overlap/disjoint predicates, combining region bits 1/2/4
as a union. Touching edges have no positive overlap; an equal rectangle is
contained. No region bits means no deletion. Selector coordinates project through
current ENV and PORT0's stored viewport origin without clipping or relocating
existing global hit rectangles, and without changing current PORT/STYLE/cursors.
Deletion leaves scene pixels intact apart from removing transient press feedback.

New parent issue [B120](https://github.com/bbs-land/remote-imaging-protocol/issues/120)
requests explicit reconciliation of the old “non-intersecting” description,
region combinations and edges, selector origin/empty/inactive behavior, unknown
flags and optional paint context/order. The corresponding full clauses remain
blocked; the local predicate policy above is not claimed as a resolved reference
answer. Bits 32/64 still reject nonfatally before deleting or touching feedback.
Bit 128 alone has no visual effect. Complete painting remains unfinished.
No prior issue receives further comments, and the stable reference pin remains
`1b8ab9928a9fd9071556cf0228ea69a496db3e72`.

Native tests cover all 32 region/type combinations over contained, equal,
partially overlapping, touching and disjoint fields of both kinds, plus deletion
and replacement during a held gesture. Session tests deliver the wire operation
one byte at a time, use literal radix-64/XY5 input, and recover after malformed,
collapsed and unsupported-paint requests. Connected CP437/UTF-8 tests verify
unchanged complete pixel planes on deletion, survivor capture during compaction,
feedback removal on captured deletion, no gesture inheritance and subsequent
host replies across four device rasters. Native Release CMake is warning-free
and ten affected CTests pass, including the contract/preservation guards.
RIP1.54 sources/fixtures remain frozen;
no new damage layer or normal ANSI/RIP1 rendering operation is introduced.

The review queue is 1574 implemented, 1709 pending, 3424 unreviewed, 1287 context, 67 excluded, 38 blocked units. Full RIP3 compliance remains unfinished.

## Connected plain/clipboard auto-stamp

`1U` accepts AUTOCLIP 64 for its implemented plain and active-clipboard faces.
It renders face/effects, captures the original face plus bevel through the
existing shared clipboard helper, and then renders the label. The capture
excludes the extra recess and label. Its dimensions retain the helper's extra
allocation unit; the interaction rectangle retains its own creation-time bounds.
Clipping does not replace the original capture rectangle with the smaller hit
intersection. No additional rendering plane or damage map is introduced.

Successful capture writes only the actual current button-style record's flags,
including when protected. It removes the persistent recess and applies the
published `(field_flags & 0x7c37) | 1` conversion. Definition/protection/current
metadata, colors, group, dimensions and the stored bevel value are retained.
Local field flags use that conversion for subsequent label work; later buttons
use the clipboard face. Protected explicit `1B` edits still reject normally.

Capture reuses an assigned clipboard or selects the first free nonzero port,
with the existing client pixel budget and helper-owned replacement semantics.
A protected target or exhausted slot table rejects without a successful flags
commit. Allocation/budget failure after replacement can delete the old bitmap
and clear its assignment. Earlier painting is retained; no mouse field is
installed on these failures. Missing capture bindings reject before painting.
Replacing the current clipboard selects PORT0 as the helper's new source;
constructor cleanup preserves that fallback, including on failure. Labels use
the resulting live context rather than contaminating the new clipboard image.

Font realization now belongs to the later label stage. A missing font cannot
roll back a successful clipboard/flags commit. A subsequent label-free clipboard
button can still use that image. All parser/operation failures remain nonfatal
and are tested with continued queries and button activation in the connection.

Native tests cover protected/unprotected styles, complete retained attributes,
plain and clipboard capture, recess/label exclusion, no-free-slot/protected/
budget failures, destructive current-target replacement, missing bindings and
late font failure. Connected CP437/UTF-8 tests exercise protected auto-stamp,
style backups, original versus copied hit bounds, failure recovery, current-
clipboard replacement and missing-font recovery on all four existing device
rasters. Exported bitmap dimensions and literal pixel indices provide independent
geometry/color expectations: the captured 107x47 beveled face excludes black
recess/red label, and 21x17 replacement/font-failure images retain their expected
uniform source colors. Expected RIPtel rasters are not generated from this engine.

Native Release CMake is warning-free. Seven distinct affected CTests pass,
including the contract/preservation guards. The source units for full multi-type
auto-stamp remain pending with explicit partial evidence until ICON/hot/group
rendering is connected; those modes are not claimed. B118/B119/B120 remain open.
The queue has 1574 implemented, 1711 pending, 3422 unreviewed, 1287 context,
67 excluded and 38 blocked units. The stable pin and frozen RIP1.54 implementation
remain unchanged. Full RIP3 compliance remains unfinished.

## PR #121 and connected keyboard buttons

The pin advances to fork stable `915ca0f3292d6ce4a74c98091cd15e8447251cef`,
including reviewed PR #121. B118 is answered. Preserve all 8099 prior source
blocks, relocate 191 and add 32, giving 8131 units in the same 68 documents.
No earlier implemented claim is retired. All 55 previous derivation hashes are
unchanged; the new keyboard note is the 56th. The adoption record distinguishes
original input/reverse-scan evidence from the explicit portable profile.

Mouse-active `1U` instances capture their byte hotkey and default-Enter flag at
creation. A matching byte consumes the event and starts the same copied host
program/reset/FIELDID pipeline as a pointer action. Simple fields, decorations
and later current-style changes do not supply a binding. Lookup folds ASCII on
both sides, treats 0 as unbound, matches 128..254 exactly and uses 255 only as a
wildcard. Explicit matches outrank CR defaults, which outrank wildcards; newest
instances win within each class. LF is not an implicit Enter default.

The real terminal event path supplies one CP437 character/control byte per
translated event. TEXT uses the existing Unicode-to-CP437 mapping; numeric entry
retains the connection encoding's existing interpretation. ASCII KEY_CODE events
honor their control modifiers; Alt/GUI shortcuts remain client policy. Raw
physical/key-up and semantic noncharacter events never become byte candidates.
The arrow's later terminal escape sequence is not reprocessed as hotkeys.
Unmatched events retain their normal forwarding/case. Empty/error actions still
consume their winning event. The native forwarding test now also covers RIP3
alongside both ANSI/RIP1 encodings, including pointer-only disable independence.
No ANSI/RIP1 rendering code or per-cell damage state changes.

HKEYOFF/HKEYON operate on the live field owner and do not alter pointer bindings.
Before a winning keyboard callback, copy its source/identity and cancel a held
pointer capture. Preserve physical button state, so a later release cannot
click or hand that gesture to a replacement. Keyboard feedback reaches the
existing native rectangle-inversion service momentarily, without a fixed sleep;
cleanup checks a surviving instance's monotonic token rather than reading a
freed pointer or matching a public ID. Deletion/reset and same-ID/address reuse
cannot cause stale cleanup. This adds per-instance keyboard metadata, not a
rendering buffer or damage layer.

Keyboard callbacks supply no synthetic click coordinate. The frontend records
actual pointer events (including ignored/disabled/wheel events), and keyboard
activation leaves that position unchanged. Connected XY queries prove this
behavior. The existing logical host continuation retains FIELDID across delays,
restores outside context on return, and can delete/reset its source field safely.
The connection remains alive after reached malformed providers and can activate
the same binding again. Pointer regressions retain the established press/release,
clipping, native feedback and deletion behavior.

Independent native matching tables and real CP437/UTF-8 connections execute the
published seven literal commands and priority results, NUL/255/high bytes,
ASCII versus high-byte case, semantic Up forwarding, raw/repeated/key-up events,
current-style independence, empty/error consumption, HKEY toggles, held-pointer
cancellation, replacement, reset-before-host and delayed self-deletion/FIELDID.
Full HEADER input-lock/normal-forwarding suppression remains pending with scene
headers. Composite X/Y/M/restore/playback clauses and the inherited physical
Scroll Lock indicator promise are not claimed by XY/bitmap tests. ICON/hot,
grouping, hover and B119/B120 also remain unfinished.

Native Release CMake is warning-free and thirteen distinct affected CTests pass,
including the source/contract guards. The queue has 1607 implemented, 1715 pending,
3412 unreviewed, 1294 context, 67 excluded and 36 blocked units. Full RIP3
compliance is unfinished; RIP1.54's recorded baseline remains unchanged.

## Image-source eligibility and selective field cleanup (PRs #122/#123)

Adopt stable `eb346cb73cf01f66ba88c8fa0dc2a134756a2302` and both reviewed
source heads; the [adoption record](RIP3_REFERENCE_ADOPTION.md) pins archive,
document and derivation hashes. B119/B120 are documentary answers, not continuing
blockers. Runtime work is tracked separately: ICON/hot sizing, retained redraw,
image/label expansion, groups and hover are still pending.

Connected clipboard buttons now read inactive/protected defined sources with a
positive, partly backed viewport. Their full native viewport determines the face,
effects, label and hit bounds, ignoring complete/mixed style dimensions and all
far values. Available pixels keep their viewport-relative offset; missing pixels
are COPY palette-index-0 padding. The existing surface transfer snapshots aliasing
before padding, avoiding source corruption; no persistent raster or damage layer
is added. Existing fields keep their creation geometry after source changes.

Connected `1k` now uses normalized current-WORLD corners as absolute DEVICE
coordinates, independent of PORT0 origin/activation or an unrelated current port.
Contained, crossing (overlapping but not contained), and outside classes are
disjoint; region flags union them before simple/button exclusions. Empty/collapsed
selectors do nothing, including flag 4, and unsupported bits reject before changes.

The owner snapshots selected rectangles, removes selected temporary depression,
cancels only selected capture and deletes the complete selection. Painting follows:
64 overrides 32; individual rectangles paint newest first, while 128 paints the
selector interior exactly once even with no matches or both type exclusions.
No region bits means no painting. Index-0 clear and opaque pattern use COPY,
physical-screen clipping and absolute eight-device-pixel phase. Current PORT,
STYLE, write mode and graphics cursor remain intact. All later paint attempts run
after a failure; no field is restored and surviving capture stays with its instance.
Every status remains below the void parser/session processing boundary.

Independent native tests cover all 256 flag combinations, covering/touching/equal
rectangles, reversed/empty/collapsed/offscreen selectors, mask/color preflight,
newest-first failure continuation and surviving held capture. Connected CP437 and
UTF-8 tests on 640x480, 800x600, 1024x768 and 720x400 check inactive source reads,
logical padding/hits, capture survival through painting, remapped palette index 0,
COPY despite XOR W and absolute patterned cleanup with unrelated current ports.
Native session tests retain every-byte split input and nonfatal malformed recovery.
The release build is warning-free; seven relevant native/guard tests and six
connected mouse/button/keyboard tests pass. The ordinary ANSI/RIP1 rendering code
and frozen RIP1.54 sources/fixtures are unchanged.

The reviewed queue now has 8185 units: 1625 implemented, 1730 pending,
3401 unreviewed, 1337 context, 67 excluded and 25 blocked. This remains an
incomplete RIP3 implementation, with 58 pinned derivation notes and 68 normative
documents. Existing source/provenance history is retained above; this milestone
supersedes the earlier provisional `1k` and inactive-clipboard limitations.

## Connected ICON and HOT button images

Literal ICON `1U` definitions now load through the trusted existing media loader.
ICON wins the effective face-type choice; its filename is required and raw payload/
name constraints remain preflight. Normal images use the existing default `.BMP`
rule; selected HOT images try the paired `.BMH` quietly before normal fallback.
All images are opaque COPY with nearest current-palette matching; flags2 bit 128
adds no transparency. Failure stays inside the void processing/input boundary.
No file, font or redraw error becomes a transport/session return status.

The first successfully chosen creation image supplies natural dimensions scaled
through the captured WORLD. Style dimensions and transmitted far values do not
fit/reorder an ICON face. A successful initially selected hot image can choose a
larger initial face without requiring an unused normal image. Every subsequent
normal/hot redraw fits that stored face, preserving hit bounds, effects, label,
creation WORLD and PORT0 viewport geometry after live context changes. A later
redraw restores current port, viewport/activation, cursor and configuration on
both success and failure. RECESS paints only during initial creation; later
redraws leave that surrounding scene alone.

The field owner takes an optional visual only on successful definition. Its
synchronous redraw happens on selection transitions, not every pointer move or
received byte. Deletion/reset/shutdown cancels selected feedback before destroying
the owned context. Keyboard commitment publishes momentary image changes before
running its action; renderer failure diagnoses nonfatally without losing the
field, its host source or release cleanup. The session revision advances for
visual redraws so the existing presenter actually publishes them.

Successful hot fallback clears HOT and sets INVERT only in that field. A later
hot file does not re-enable it; new fields still read the unchanged style. RGB
complement is nearest-matched into the existing indexed palette, with first-index
ties, as client rendering policy. It is separate from the indexed drawing NOT
operator. This preserves indexed capture/export and uses the existing surface and
damage planes, without adding a persistent raster, damage tracker or alternate
ANSI path. Plain/simple native feedback keeps its existing implementation.

Auto-stamp also accepts ICON/initial-HOT faces: existing capture/style mutation
runs before label realization, then later buttons use the converted clipboard
style. Existing budget/protection/label failure boundaries remain intact.

Independent generated BMP fixtures and explicit expected colors/geometry run on
640x480, 800x600, 1024x768 and 720x400 connections in CP437 and UTF-8. They cover
COPY despite other W modes, no transparent key, collapsed natural dimensions,
preflight preservation, captured WORLD/viewport/font state, differently sized hot
images, selected/fallback behavior, malformed-hot fallback, field-local flags,
initial-only recess, pre-label exports, keyboard actions, deletion while held and
a normal-load failure after a valid hot-only creation. Native tests cover ownership
rejection, failed redraw recovery, callback self-deletion, shutdown, palette/clip/
damage and context restoration after failure. A fixture correction accounts for
WORLD conversion of bevel and the explicitly configured corner index 7; expected
scene colors are derived from fixture inputs, not emulator-generated rasters.

Seventeen relevant native/connected tests pass in the native Release configuration,
including ordinary ANSI/RIP1/RIP3 key forwarding and the RIP1.54 source/contract
guards. No frozen RIP1.54 source/fixture changes or VT-102 mode are introduced.
The reference remains stable `eb346cb73cf01f66ba88c8fa0dc2a134756a2302`; the #27
derivation read for this milestone matches its pinned digest. The reviewed queue
has 8185 units: 1638 implemented, 1724 pending, 3394 unreviewed, 1337 context,
67 excluded and 25 blocked. Filename/label macro expansion, grouped selection,
selected plain/clipboard images, hover and other unfinished families remain
pending; full RIP3 compliance is not claimed.

## Connected native mouse-pointer state and graphical-consumer follow-ups

`1c` now stores the canonical documented styles 0..6 in current ENV using its
protected setter; graphics STYLE protection is irrelevant. Two base-math style
digits are independent of XY width. The corpus's omitted reserved suffix and
partial/full zero suffixes are accepted; malformed bodies, unknown styles and
unsupported nonzero reserved values are discarded inside the parser. `MCURSOR`
uses an optional decimal argument, defaults to zero and bypasses ENV protection.
It accepts stored value 7 as documented, without claiming an appearance for it.

The connection resolves the seven documented styles to native arrow, watch,
crosshair, text, pointing-link, stop/raised-hand and wait equivalents. Existing
console pointer enum values remain unchanged. SDL/X11/GDI use stock equivalents;
Wayland supplies distinct device rasters and Quartz adds lazy public image/path
busy indicators alongside stock cursors. Native platform appearance is client
rendering policy; exact original cursor pixels are not required. Shape changes
retain pointer visibility, position, field capture and input enable; they paint
no text/graphics pixels and work without a usable WORLD projection. SOFT resets
presentation to the default pointer. A stored 7 diagnoses unsupported presentation
once per transition and retains the prior native shape pending documentation.

Link-hover cursor restoration now uses the RIP3 pointer preference instead of
forcing the ANSI I-bar. Inspection exposed a related existing mismatch: link
lookup read the display's cells while RIP3 text could have a different grid and
pixel origin. Hover and click metadata now resolve the actual event pixels
against the borrowed active RIP3 text owner, including view clipping and live
font dimensions. Ordinary ANSI/RIP1 retain their display-cell path and existing
local URL policy. No new public Wren method or separate text/damage layer is
introduced; this completes the existing hyperlink-owner integration, not RIP's
still-pending entry/exit host-query dispatch.

Two documentation gaps were filed as new parent issues, without commenting on
answered issues:

- [B124](https://github.com/bbs-land/remote-imaging-protocol/issues/124) follows
  #6/PR #32 and #116/PR #117: per-consumer filename/label callback permissions,
  escaping/output bounds and prompt continuation are not fully specified by the
  generic one-pass rule. Graphical expansion remains pending rather than borrowing
  the two-pass host pipeline or executing the stored host slot at creation.
- [B125](https://github.com/bbs-land/remote-imaging-protocol/issues/125) follows
  #24/PR #57: MCURSOR explicitly accepts 7 but the shape table ends at 6; corpus
  prose also disagrees with the canonical table for 4/6. The known table is wired,
  while those mapping claims remain blocked. No invented eighth shape is exposed.

The #6/#116/#24 derivations match their pinned hashes; the reference revision
remains `eb346cb73cf01f66ba88c8fa0dc2a134756a2302`. Native tests cover fixed-width/
truncated cursor commands, protected ENV versus MCURSOR bypass, stored 7, invalid
recovery, backend enum bounds, Wayland shapes and stock X11 changes on a private
Xvfb display. Connected CP437/UTF-8 tests cover the mapped shapes, unchanged planes,
zero WORLD, STYLE protection, continued field input, default reset, OSC8 hand
presentation and restoring the protocol cursor at unaligned view boundaries.
Existing ANSI hyperlink and RIP3 mouse regressions pass. The native Release build
is warning-free, with 17 relevant tests passing including source/contract guards
and ANSI/RIP1/RIP3 forwarding. GDI/Quartz source and native tests are updated;
those platforms were not executed locally. No Wine or user-display test runs.

The 8185-unit queue now has 1655 implemented, 1723 pending, 3366 unreviewed,
1344 context, 67 excluded and 30 blocked units. Full RIP3 support remains
incomplete. The RIP1.54 freeze, entry raster policy, absence of a new VT-102 mode,
and all unrelated user work remain unchanged.

## PR126/127 adoption and connected cursor 7

The current pin is fork stable `a8570522e02f4b6753afea9cb9a6de3565009f1a`,
containing both actual reviewed PR heads. The [adoption record](RIP3_REFERENCE_ADOPTION.md)
retains the archive digest, exact source migration and original derivation hashes.
B124/B125 are answered documentation; their earlier open-gap descriptions above
are historical. Graphical expansion now has explicit permissions, prefix bounds,
escaping and prompt/redraw lifetime rules, with unfinished runtime work remaining
pending. Positional raw `1U` splitting already matches PR117 and is unchanged.

Connected wire `1c` and MCURSOR now select all eight documented logical values.
Seven is the visible prohibited symbol and never suppresses input or cancels an
existing press. Five uses a raised palm, separately from the pointing finger
and prohibited shape. X11/Wayland share monochrome equivalents; SDL/GDI use a
custom palm and native prohibited cursor; Quartz uses openHand/not-allowed.
The existing arrow, I-bar and link artwork is unchanged. Native cursors are
owned by their existing backend and released on shutdown. No scene or damage
buffer is added.

A backend failure selects the default arrow while the ENV retains its logical
request. The terminal's hover preference follows that actual presentation.
Native receive-pump tests inject a backend lacking cursor 7 and verify arrow
fallback and continued connection operation. Independent connected CP437/UTF-8
captures verify all eight values, invalid-input recovery, unchanged pixel planes,
prohibited selection during a held press, a successful inside release, default
reset and OSC8 hover restoring cursor 7. Every-byte split parser tests cover
`1c070000`; native checks retain protection/default/range behavior and distinct
Wayland glyphs. X11 runs on a private Xvfb display. GDI/Quartz are not executed
locally. The RIP1.54 source freeze and ordinary ANSI/RIP1 forwarding remain
covered by their existing focused guards/tests.

The 8250-unit queue now has 1664 implemented, 1775 pending, 3365 unreviewed,
1354 context, 67 excluded and 25 blocked units, with 60 pinned derivations and
68 normative documents. The native Release build is warning-free, and twelve
relevant native/connected tests pass, including the nonfatal fallback case.
Full RIP3 remains incomplete; graphical provider and
prompt continuations, grouped buttons and other pending families are not
claimed by this cursor milestone.

## Connected graphical text expansion

Ordinary connected `T` and `@` now run the PR126 angle preprocessor followed
by one ordinary expansion pass. The numeric `@` prefix is decoded before any
provider can change COORDSIZE/Base Math; the remaining text uses live permitted
setter effects and the existing font/port rendering paths. Text that survives
expansion literally, including disabled references and quoted dollar signs,
reaches glyph conversion without being rejected as unexpanded syntax.

The shared worker uses the complete 84-name class-1/class-3 registration gate.
Active-only WORLD/MKILL and other recognized blocked, rejected or unavailable
ordinary callbacks remain literal with no user fallback. Ordinary template-slot
embedding and playback remain literal; angle templates and permitted conditional
providers retain their existing scanner behavior. User results are skipped after
insertion, outer quotes are consumed once, trailing backslashes disappear, and
host control-pair decoding is disabled. This never executes a button host slot.

The 1022-byte text prefix is enforced on entry and every angle/ordinary
replacement, permanently discarding suffixes before scanning them. Each value's
NUL terminates that value alone. Native byte input and actual UTF-8 wire input
count bytes directly. The terminal's normalized codepage source recovers the
original single-byte budget from its UTF-8 representation, including quote and
angle insertion; this prevents non-ASCII CP437 from consuming twice its budget.
Plain strings use the reusable owned string buffer and skip angle allocation.
No scene buffer or console damage layer is introduced.

Permitted prompts suspend the owned continuation without renderer/table locks.
The input source retains later wire bytes. The terminal retains physical events
in their original order until rendering settles, while local exit stays usable.
If saving an event fails, it abandons only the render before removing that event
from the console queue; provider effects and the original input order survive.
Completion/cancellation resumes once; shutdown releases the worker and saved
events. Hard expansion failures paint none of that argument and never return a
connection error. Earlier user-variable effects are retained after a hard abort.

Independent worker tests cover the permission matrix, all four consumer policies,
angle/template versus ordinary rescanning, escaping, byte bounds, value NULs,
prompt outcomes, cancellation and nontransactional hard failure. Those tests are
policy evidence: only T/@ are connected by this milestone. Live CP437/UTF-8
bitmap sessions verify cursor advances and ink, original-encoding byte boundaries,
setters after decoded coordinates, discarded effects, and unchanged pixels/field
activation after preprocessing failure. Native session tests split every byte;
receive-pump tests retain ordered physical keys across a required prompt and
keep local hangup usable with a queued event. Existing host-query and literal
text-command regressions cover the preserved paths.

Button filename/label creation/redraw continuations, their captured destination/
instance lifetime and post-auto-stamp labels remain pending. TEXT_METRIC and its
TEXTDATA ownership/bypass also remain pending; their private worker policies are
not advertised as connected features. Existing unsupported editor/provider modes
and separate storage/parameter limits are not completed by this milestone.
The native Release build is warning-free. Fifteen relevant tests pass, including
ordinary ANSI control checks and RIP1.54 preservation guards.
The 8250-unit queue has 1670 implemented, 1769 pending, 3365 unreviewed,
1354 context, 67 excluded and 25 blocked units. Full RIP3 remains incomplete.

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
For non-audio changes, select the affected tests instead of repeatedly replaying
the existing ANSI music tests. Audible checks are appropriate when testing a new
feature that produces sound.

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

## Connected filename/label stages and PR #129 auto-stamp contract

The reference now pins fork stable
`21728db6fd3ff7623e36d5ff59e8c64421f57c14`, including reviewed
[PR #129](https://github.com/bbs-land/remote-imaging-protocol/pull/129) for
[issue #128](https://github.com/bbs-land/remote-imaging-protocol/issues/128).
The adoption record retains the archive/document/derivation hashes, exact block
migration and retired implementation evidence. This answers B128; no existing
issue receives comments. The already adopted PR117 positional `1U` split is
unchanged, with new explicit quote/backslash, inserted-delimiter and normalized
codepage raw-byte boundary vectors.

Connected `1U` creation and requested full redraws now use the same bounded
angle/ordinary worker as T/@ for the nonempty common filename and label slots.
A non-ICON first slot is prepared without loading an icon. The original raw
sources are retained, while definition-time WORLD, geometry, style, font and
viewport/destination identity remain captured. Filename providers precede
face/effects and capture; label providers follow them. Completed live setter
and clipboard/style effects are preserved across suspension or later failure.
The opaque third host slot executes only after an actual committed activation.

A shared resumable renderer owns its working descriptor while a field or
prompt can disappear. Scene and destination generations prevent drawing into
replacement ports, resurrecting private viewport state or admitting a pending
field into another scene. Admission rechecks the shared 128-field capacity.
Keyboard/release activation owns its original source and identity snapshot,
waits for optional redraw/query stages, and commits once even if feedback fails
or the field is deleted. Destruction and forced cancellation clear temporary
feedback without providers or a new activation. Parser/resource/provider errors
remain below void parser/session processing and never terminate the connection.

Physical release/focus-loss/cancel information received by the real modal Wren
editor is retained against the original unique guest press token and appended
to the existing ordered input FIFO. Later dialog clicks are not replayed as
guest clicks. The bridge uses the existing console-event/poll ownership and one
embedded terminal-event node; it adds no guest Wren API, scene pixel buffer or
damage tracking layer. Ordinary ANSI/RIP1 rendering retains its existing path.

PR129 auto-stamp keeps F for the label, H for input and C for the baked face and
bevel, excluding the recess and label. Capture uses C's exact logical dimensions,
places available source pixels at their C-relative offsets and pads unavailable
pixels with index 0/COPY. Full redraws re-prepare the filename, read the current
clipboard into the same C with crop/padding, then expand the current label;
they do not reload a former ICON or recapture. Clearing CHISEL does not change
its captured label inset. Same-effective-destination auto-stamp skips deletion,
allocation and converted-style writes, then draws the label on the original
port. Destination replacement cancels the stale continuation without a PORT0
handoff. Ordinary `1C` GET_IMAGE allocation/copy behavior remains unchanged.

Independent native vectors cover F/H/C, source aliases and padding, once-only
capture/style writes before a prompt, stale destination/scene, field deletion,
capacity exhaustion, provider-free cleanup and committed-action lifetime. The
connected button/icon cases run in CP437 and UTF-8 across four entry rasters,
using independently encoded BMP/BMH assets and direct geometry/color checks.
They cover live filename and label changes, fixed redraw geometry/crop,
real modal release handling and a usable connection after
malformed definitions or later failures. Existing connected keyboard, mouse and
T/@ cases, normal ANSI controls and the relevant native parser/query/port tests
pass. Native raw-slot vectors check original codepage wire-byte budgets.
Fixture corrections distinguish wire extent boundaries from inclusive
native endpoints and allow label ink outside its face; expectations are not
generated from this emulator. All 25 distinct relevant checks pass, including the source freeze, contract and
preservation guard. Builds use the one native Release CMake tree.

The 8279-unit queue now has 1702 implemented, 1760 pending, 3365 unreviewed,
1360 context, 67 excluded and 25 blocked units. The whole generic prompt-lifetime
rows remain pending where they include unfinished TEXT_METRIC/TEXTDATA or
advanced frontend editor/format modes. Grouped/persistent selection, initial
selected plain/clipboard image policy, hover and other recorded table/playback
work remain pending. This milestone does not establish full RIP3 compliance.
The RIP1.54 baseline and historical fixtures are not advanced.

## Connected TEXT_METRIC width and retained expansion

Level-zero `r` now reaches the separate engine's session parser. It decodes the
one-digit mode/domain before providers can change Base Math, consumes the four
reserved bytes without interpreting them, and rejects invalid prefixes before
expansion or retained-text replacement. Its arguments share the graphical
worker used by T/@ and button filename/labels. Metric ordinary unknown user
forms remain literal; angle user/template lookups can suspend through the same
real prompt and ordered input path. Completed replacements own their bytes.

Exact case-insensitive `$TEXTDATA$` input uses the previously retained result
without another angle or ordinary pass. Other arguments retain their bounded
expanded C-string prefix before measurement, including successful empty output;
a later decoding/measurement failure does not restore the old result. Hard
expansion failure and cancellation preserve the last completed text and send no
metric reply. Retained text is session-owned, independent of user-variable
storage and table selection. No general TEXTDATA callback is invented.

Width modes 0/2 are callable from actual connections. They reuse the existing
classic metric and logical outline width services, returning signed decimal
whole-string or independent byte widths with a final CR. Narrow the device
result to a signed WORD, perform signed WORLD/device conversion with truncation,
and narrow again. Replies are assembled before transmission. Measurement adds
no pixels and does not move the graphics cursor. Normalized codepage input is
recovered to original font bytes; actual UTF-8 wire input currently retains the
literal byte-oriented metric profile. This differs from Unicode glyph counting
and remains explicit in the transport question below.

This is a connected width/expansion milestone, **not complete TEXT_METRIC
compliance**. New [issue #130](https://github.com/bbs-land/remote-imaging-protocol/issues/130)
requests the portable shipped-font width/height/axis and font-initialization
contract, including prior/cold cache behavior, viewport/zero-WORLD gates,
TEXTDATA initial/reset/visibility lifetime, provider-stage snapshots and the
UTF-8 adaptation. The existing synthetic width-3/height-7 oracles do not
establish real-font height or reset semantics. Earlier #5/PR31, #86/PR88 and
#124/PR126 answers remain adopted; their issues receive no comments. Height
modes 1/3 and the sentinel before any completed assignment remain unsupported
inside the parser. Current stored text survives cancellation/table selection;
full reset/backup behavior is awaiting that clarification. Existing outline
width services are reused, with their complete metric binding still recorded
under B130 rather than inferred as new source facts.

Native tests cover controlled bitmap width, the independently documented Marin
F metric, per-byte results, repeated signed narrowing, NUL/prefix handling,
retained sentinel bypass, raw versus normalized byte profiles and no partial
reply after a decoding failure. Public session tests split the wire one byte at
a time and check early validation, prompt/cancel ordering, retained effects,
recovery and one coherent unchanged pixel/cursor capture. Real CP437 and UTF-8
connections at 640×480 and 720×400 check decimal replies, byte budgets, template
and ordinary-provider differences, an actual angle prompt and ordered later
wire input. The existing T/@, keyboard, normal ANSI controls and relevant
native parser/query/forwarding checks pass in the one native Release CMake tree.
All 17 distinct relevant checks pass, including the contract and preservation
guards.

The unchanged reference pin is `21728db6fd3ff7623e36d5ff59e8c64421f57c14`.
The 8279-unit ledger is now 1727 implemented, 1742 pending, 3341 unreviewed,
1366 context, 67 excluded and 36 blocked. Eleven source units explicitly track
B130 instead of hiding these gaps in an implementation claim. Advanced frontend
user editor/format modes, grouped/persistent selection, selected plain/clipboard
image policy, hover and remaining table/playback work also remain pending.
RIP1.54 sources, historical expectations and baseline are unchanged.

## Initial selected images for plain and clipboard faces

The definition-time `1U` selected-image bit now applies to PLAIN and CLIPBOARD
as well as the existing ICON path. Invertible faces use the same one-time RGB
complement over their stored image/hit footprint. Bevel pixels and clipboard
padding participate; the recess and source clipboard do not. Auto-stamp still
captures before the final selected-image inversion. This changes only the
initial image; creation never evaluates or executes the opaque host source.
The existing redraw and input paths handle subsequent press, outside release,
inside activation and translated keyboard input without double inversion.

A focused native expectation demonstrates the old ICON-only guard's failure
and passes with the generalized image bit. Connected CP437 and UTF-8 button
cases pass on 640×480, 800×600, 1024×768 and 720×400 entries. They check initial
pixels, bevel/recess and clipboard padding, outside-release cancellation,
once-only pointer/keyboard activation, and unchanged user storage until an
actual activation. Native tests also verify that a captured auto-stamp source
contains its unselected face before the displayed complement. No new renderer,
buffer, damage path, mode list or configuration is added. The existing ICON
condition has identical results and its passed evidence is retained. Normal
ANSI/RIP1 code is unchanged.

Persistent radio/checkbox selection is a separate unfinished family. New
[issue #131](https://github.com/bbs-land/remote-imaging-protocol/issues/131)
requests its selection/feedback commitment, per-instance associated-template
ownership, checkbox OFF processing, required-radio lifetime and interaction
with suspended deletion/reset/replacement. The inherited group examples and
newer opaque-creation/once-only input contracts are retained. In particular,
this is not permission to execute a preselected field's host source on receipt.
Grouped definitions continue to fail inside the void parser until their
complete connected behavior is implemented. Existing issues receive no comments.

The reference pin remains `21728db6fd3ff7623e36d5ff59e8c64421f57c14`.
The 8279-unit ledger is 1729 implemented, 1741 pending, 3328 unreviewed,
1369 context, 67 excluded and 45 blocked. Nine group source units now explicitly
track B131. B130's complete metric/font/height/state rules, advanced frontend
user editor/format modes, hover and remaining table/playback work also remain
unfinished. RIP1.54's sources, baseline and historical expectations remain intact.

## Connected mouse-field entry and exit queries

Wire `1ESC` modes 5/6 now copy unexpanded global entry/exit expressions into
the mouse owner. Definitions execute no providers. Exact case-insensitive
`$OFF$` removes the corresponding expression. Physical movement invokes
identified fields' exits before entries, newest first in each pass, with each
callback's FIELDID and movement point. Both simple fields and mouse buttons
participate; anonymous fields do not. Click press/release and keyboard capture
remain independent, including movement while a button is held.

A bounded event snapshot stores stable instance tokens, not pointers into a
mutable field array. Deleted instances are skipped, new/reused-ID fields wait
for the next movement, and a dispatched callback owns its source through
query replacement, deletion and suspension. The existing session query worker,
FIELDID scope and ordered input FIFO serialize prompts and delays before later
callbacks or wire input. Self-deletion does not remove the active callback's
identity or synthesize removed-field exits. Temporary button redraw can finish
before its hover callback without transferring the held click. Invalid stored
expressions are diagnosed inside the parser and leave the connection usable.

Ordinary motion with no handlers updates membership in one pass; unchanged
membership creates no pending traversal. Only callbacks allocate action copies.
This adds required field/event metadata, with no screen pixel buffer, damage
map, alternate terminal mode or normal ANSI/RIP1 rendering path. The existing
mouse owner releases stored expressions and pending actions at shutdown.

The pinned original TELKILL.FN bytes explicitly issue `1K` followed by separate
mode-5 and mode-6 `$OFF$` commands. They were read as data, without executing an
original scene/client. New [issue #132](https://github.com/bbs-land/remote-imaging-protocol/issues/132)
requests the full definition/membership lifecycle for absent/OFF handlers,
replacement while already inside or suspended, resets/restores/input gates,
empty definitions and numeric/storage edges. Current basic dispatch is
connected; those edge policies remain explicitly unaccepted under B132.
Stored-but-unexecuted click query modes 1–4 and their numbered table ownership
are also still pending. No old issue receives comments.

Native tests cover overlap and exit-first ordering, half-open edges, anonymous
fields, independent membership, same-ID replacement, deletion, source ownership,
suspension and allocation failure. Injected allocation failures preserve old
query definitions or consume the failed action nonfatally; destruction releases
all owner allocations. Public session tests retain FIELDID through self-deletion
plus a delay and through successive prompts. Live CP437/UTF-8 cases on 640×480
and 720×400 entries verify pointer metadata without pixel changes, simple/button
hover, held click ownership, two actual menu-editor prompts with IDs 8 then 7,
ordered later wire input, `$OFF$`, field clearing and malformed-handler recovery.
Existing mouse, keyboard and normal ANSI controls pass. The final native build
is warning-free; the missing mouse API include found during integration is fixed.

At unchanged pin `21728db6fd3ff7623e36d5ff59e8c64421f57c14`, the 8279-unit
ledger contains 1740 implemented, 1732 pending, 3323 unreviewed, 1370 context,
67 excluded and 47 blocked units. B130's full metrics, B131's persistent grouped
selection/template lifetime, B132's hover edges, advanced frontend user editor/
format modes, numbered resident-query storage and remaining table/playback
families prevent a full-compliance claim. RIP1.54's baseline and expectations
are unchanged.

## PR #133 metrics and retained graphics text

The current pin is fork stable
`9d666f8468036e5517f2f1395790b52bf6810c28`, containing reviewed
[PR #133](https://github.com/bbs-land/remote-imaging-protocol/pull/133).
B130 is answered as documentation. The adoption record retains the archive
hash, exact source-block migration, retired implementation evidence and all
62 derivation hashes. Existing issue threads receive no comments.

Connected r now supports all four width/height modes. CHR metrics use exact
size ratios and the actual device frame, with whole-string versus per-byte
truncation. Bitmap metrics use size times floor(device width/80) and
floor(device height/43), independently of the raster glyph bank or override.
This corrects metric width on custom modes such as 720×400; ordinary drawing
keeps its existing font/raster behavior. Outline height uses the unrotated
signed global font bbox, rounded in 1/1440 inch before Y DPI. Ordinary outline
text and metrics atomically prepare the same width/height snapshot; failed
realization or an inactive viewport retains both, with cold zero. A positive
WORLD that truncates effective DPI to zero prepares valid zero metrics. Replies
narrow to signed WORD before current-axis WORLD conversion and narrow again.

TEXTDATA starts empty outside all tables. A successful metric expansion replaces
it before font/empty checks; a hard expansion failure retains its predecessor.
Exact raw C-string r/@ sentinels read without re-expansion. T uses normal user
lookup, including a distinct user variable named TEXTDATA. A nonempty outline
expression expanding empty can prepare/reply; literal-empty outline input
silently clears the data without preparing. Full SOFT cancels old graphical
ownership and clears retained text, so a late prompt response cannot resurrect
it. Table backups/restores and viewport erasure do not rewind it. HEADER and
aggregate SAVEALL/RESTOREALL clauses remain pending with those operations.

Independent native vectors cover MARIN 90/10, TRIP.CHR sizes 4/2, bitmap size 2,
font axes/rotations, protected/inactive state, signed height overflow, final
versus per-byte truncation, corrupt classic preparation and atomic fallback.
Public bytewise session tests cover exact sentinel use, separate user lookup,
normal versus forced prompt cancellation, reset while suspended, late response
rejection and later wire recovery. Connected CP437/UTF-8 tests use both 640×480
and 720×400, real prompts and post-prompt frame setters, no drawing/cursor change
from queries, ordinary-outline cache preparation and full-reset empty state.
Existing graphical/reset and ANSI controls, native text/font/cache paths and
RIP1.54 preservation checks pass in the single native Release CMake build.

The 8324-unit queue contains 1783 implemented, 1736 pending, 3323 unreviewed,
1379 context, 67 excluded and 36 blocked units. Full RIP3 compliance remains
unfinished, including grouped button/template state, hover lifecycle, resident
query modes 1–4, advanced frontend formats and remaining scene/table/playback
families. No new pixel/damage layer or terminal mode is introduced.

## PR134/135 adoption and connected hover lifecycle

The current pin is fork stable
`49a69454a0bd6055c0d11bb415759d309519de33`, containing reviewed
[PR #134](https://github.com/bbs-land/remote-imaging-protocol/pull/134) and
[PR #135](https://github.com/bbs-land/remote-imaging-protocol/pull/135).
B131/B132 are documentary answers. The adoption history records actual PR heads,
archive/document hashes, 64 derivations and exact source-block migration. PR134's
grouped selection, associated-template ownership/assembly and required-radio
rules remain pending runtime work. No existing issue receives comments.

Live mode-5/6 definitions now reject empty text and sources over 1023 protocol
bytes before changing the old handler. Exact case-insensitive C-prefix `$OFF$`
removes only that handler; quoted, whitespace or suffix spellings stay raw
sources. Normalized codepage bytes retain their original byte budget. The
numeric window and reserved fields are consumed/validated but their values are
ignored for these modes. Immediate-mode validation retains its previous path.
All failures remain nonfatal below the void parser/session boundary.

Every field's membership is maintained independently of handler availability
and numeric ID. Transitions commit membership before consulting the current
handler; each callback owns its source/FIELDID, so earlier callbacks may replace
or remove later handlers safely. Stable-instance tokens retain exit-before-entry
and newest-first ordering; deleted/replaced fields do not inherit an old action.
Anonymous fields maintain membership but invoke no shared query. Absent-handler
motion keeps its single-pass path and allocates no host action.

Focus loss and disabled pointer delivery clear membership, capture and the
remaining uncommitted traversal without synthetic exits. Already committed
callbacks keep their owned source and FIELDID. Ordinary capture-only cancellation
and pure rendering/selection do not clear membership. The actual modal editor's
existing observer now applies focus boundaries even without a held guest press;
its original gesture-tail FIFO remains intact. Full SOFT deletes fields and
clears both handlers, while `1K`/MKILL retain the handlers for future fields.
Mouse-family backup/restore, HEADER/input-lock orchestration and their mixed
source clauses remain pending; they are not inferred from these live paths.

Native published vectors cover no/entry/exit/both handlers, duplicate IDs,
per-callback OFF/replacement, exit-to-entry replacement, pending focus/reset,
source limits, C-prefix sentinel forms, allocation failures and destruction.
Public session tests cover numeric framing, normalized sources, callback FIELDID
after full reset and later wire recovery. Actual CP437/UTF-8 connections at
640×480 and 720×400 cover focus boundaries, a real prompt losing focus, no
synthetic/replayed callbacks, full SOFT clearing and retained click/button
ownership. Existing mouse/keyboard/reset and ANSI control regressions pass.
The native CMake build and RIP1.54 contract/preservation guards are checked.

The 8410-unit queue has 1811 implemented, 1794 pending, 3316 unreviewed,
1397 context, 67 excluded and 25 blocked units. Full RIP3 compliance remains
unfinished, including PR134 grouped behavior, stored click modes 1–4 and the
remaining scene/table/playback families. No new renderer buffer/damage layer,
terminal mode or normal ANSI/RIP1 rendering path is introduced.


## Connected grouped selection and template ownership

At the unchanged PR134/135 pin, grouped `1U` buttons now participate in live
pointer and translated-key sessions. Checkbox activation toggles persistent
state; radio activation selects one stable instance and clears its peers before
optional redraw or host processing. An already selected radio still activates
once. Creation uses the initial selection for artwork and state without parsing
its host source. Ungrouped buttons ignore that initial flag and remain momentary.
Conflicting group kinds reject before providers or painting, with admission
rechecked after suspension. A newly selected radio does not transfer or cancel
an older held instance's temporary preview; outside release restores the then
current committed state.

The existing template owner now retains a per-slot unexpanded definition map
for each stable grouped instance. Only reached definition blocks write it, after
angle processing and before ordinary expansion; OFF checkboxes still process
their own source. Selected contributions assemble in creation order, retaining
the first 4096 bytes without modifying the individual bodies or running providers.
Ordinary baselines and one current publisher per slot preserve later explicit
assignments and other groups' publications. Deletion rebuilds current group
publications; last-member deletion releases them to the ordinary baseline.
SOFT first preserves effective bytes as ordinary definitions, then deletes
fields. A pending old continuation retains its source/FIELDID but skips any
associated write whose original stable owner has disappeared. Duplicate numeric
IDs never retarget an association or redraw.

A live radio group requires a selected member when its numbered template is
referenced, independently of slot contents. Selected empty radio bodies are
valid; ordinary bytes do not satisfy an unselected live group. The last member's
deletion removes the requirement. HKEYOFF and pointer delivery gates retain
selection. Missing choices abort only the current host lookup/program, without
synthetic clicks, automatic retries or connection errors.

Optional feedback uses the existing button renderer. Full redraws retain their
filename/label provider stages; cancellation uses the existing RGB complement
or prepared ICON and label resources without providers, file reads or auto-stamp.
No new scene-pixel or damage layer is introduced. Peer-token storage is allocated
only for grouped actions, preserving the smaller ordinary hover allocation.

Native tests cover selection before failed redraws, suspended peer feedback,
same-ID replacement, stale writes, publication supersession, allocation failures
and source-body/aggregate bounds. Public sessions cover SOFT during a committed
prompt. Independent connected CP437/UTF-8 tests at 640×480 and 720×400 cover the
published checkbox transitions, radio selection and empty/missing choices,
keyboard repeats, mixed-kind rejection without painting, held-preview mutation,
real prompts, post-angle/ordinary-late bodies, provider-free rebuild and SOFT
preservation. Existing plain/clipboard/ICON, hover, keyboard and ANSI controls
pass. Twenty-two distinct relevant CTest checks pass in the single native
Release configuration, including the contract and RIP1.54 preservation guards.

The 8410-unit queue now has 1855 implemented, 1747 pending, 3316 unreviewed,
1400 context, 67 excluded and 25 blocked units. Explicit mouse-family backup/
restore, host-template clearing, HEADER/global orchestration, resident click
modes 1–4 and other scene/table/playback work remain unfinished. Mixed source
clauses including those operations stay pending. Existing issue threads receive
no comments; PR134/135 are already in the pinned stable reference. RIP1.54's
baseline and expectations remain unchanged.


## Mouse definition backups and input boundaries

The unchanged reference pin now drives connected `$SMF$` / `$RMF$` and generic
`COPY(MOUSE,...)` for simple fields and ordinary PLAIN/CLIPBOARD/ICON buttons.
The existing backup manager supplies reusable BASE, numbered slots, independent
PUSH/POP, protection, shared slot/stack capacity and status queries. Snapshots
own raw sources, absolute bounds, button configuration and both hover handlers,
including saved absence. They save no screen pixels, current physical input or
hover membership. Saving does not cancel a held press or run providers.

Applying a restore first cancels capture, temporary feedback and uncommitted
hover traversal. This boundary remains even if later allocation/copy preparation
fails; earlier live definitions and handlers remain usable on that failure.
A request rejected before applying, such as an absent slot or invalid selector,
retains interaction. Successfully restored fields get fresh stable tokens and
outside membership, even with identical numeric IDs and geometry. The old
physical release cannot activate them. Already committed source and FIELDID
survive self-replacement and a later actual editor prompt, without retargeting
cleanup or replaying the activation. Restoring definitions runs no filename,
label or host provider and does not repaint the scene.

Button descriptors are independently owned. Their immutable, already decoded
ICON resources use reference counts across snapshots, retaining provider-free
feedback undo without copying a screen/clipboard plane. Restored descriptors
bind their private text context and current destination generation. Detached
snapshot destruction runs no rendering/input/provider work; session teardown
destroys backups before the services they borrow.

`RESET(MOUSE)` defaults to TBL. Explicit TBL/BASE/S0–S9/SLOTS/STACK/BACKUP/ALL
selectors validate before changing anything, retain protected snapshots and
keep live definitions separate from backup storage. Field-only reset retains
hover handlers. Full SOFT still deletes live fields/handlers while retaining
independent backups, so a later RMF can restore their owned definitions.

**At that milestone, grouped capture remained unsupported.** The new
[issue #136](https://github.com/bbs-land/remote-imaging-protocol/issues/136)
asks which committed selection/A[N] state is captured and how restoration
interacts with live ordinary B[N] and group-publication tags. It references
PR134/135 without editing or commenting on their covered issues. If any live
field is grouped, SMF/COPY from TBL rejects nonfatally before overwriting the
previous snapshot or changing live interaction. This does not change the live
grouped selection/template behavior completed in the preceding milestone.
Complete mouse-family capture/restore clauses remain blocked under B136;
RESTOREALL/SAVEALL and HEADER orchestration remain pending with those operations.

Thirty distinct relevant CTest checks pass in the native FreeBSD amd64 Release
CMake configuration. Independent native vectors cover every mouse-owner
allocation failure during capture/restore, failed visual-copy cleanup,
constructor/teardown ownership, rejected versus applying restore boundaries,
saved absence, no providers, fresh instances and suspended traversal. Actual
CP437/UTF-8 sessions at 640×480 and 720×400 cover held saves/restores, generic
copy, protection, SOFT lifetime, all three ordinary face types, hot feedback,
coherent no-repaint captures and a self-replacing host continuation through a
real prompt. Existing grouped/plain/clipboard/ICON/hover/keyboard/ANSI and typed
table checks pass, along with RIP1.54 contract/preservation guards.

The 8410-unit queue now has 1885 implemented, 1739 pending, 3282 unreviewed,
1407 context, 67 excluded and 30 blocked units. Full RIP3 compliance remains
unfinished. The RIP1.54 baseline and historical expectations are unchanged.


## Connected physical screen saves and numbered aliases

The current pinned screen contract is connected through `$SAVE$`, `$RESTORE$`,
all twenty fixed numbered aliases and generic `COPY(SCREEN,...)`. Explicit
backups capture the physical PORT0 scene and current-palette RGB, including
native ANSI output and both blink planes. They save no table selection,
viewport, styles, text-window/mouse definitions or status-bar visibility.
Restore overwrites RGB in the palette entry current at restore time, including
protected or selected-but-undefined entries, while retaining its flags/index.
Saved dimensions control an unscaled clipped copy; no canvas resize or port
identity change occurs. Buttons created after SAVE remain usable after RESTORE.

Native composition is captured only for explicit backup operations. The adapter
uses the existing presented graphics image and console raster to preserve raw
RIP indices and literal reset black while incorporating native ANSI RGB. There
is no new resident scene/damage layer or per-character capture. Restore uses the
existing pixel-restore and raster-view adoption paths, preserving both native
planes and marking cells through the console's existing external-pixel/damage
flags. Native cell writes and circular scrolling after restoration are covered.
An indexed BMP consumer now matches restored RGB through its explicit export
palette rather than truncating the RGB word to an index; ordinary indexed reads
and write formats retain their existing paths.

BASE remains reusable. Numbered and stack saves share the existing family
capacity/protection manager. Numeric RESTORE requests numbered deletion even
after a later presentation failure; protected slots survive. Failed POP retains
its stack entry. Valid failed action callbacks produce no variable value/error,
so a following ordinary suffix still executes; private lower-level diagnostics
remain available. Invalid arguments reject before backup effects and recover
inside RIP processing. `RESET(SCREEN)` clears physical palette index 0 without
changing PORT/TW/style state; backup selectors clear their own save areas and
retain protected snapshots. SOFT preserves independent screen backups.

Independent native tests cover all three presets plus 720×400, unscaled
cross-size clipping, current PAL5→PAL17 restoration, protected/undefined targets,
port/viewport/offscreen retention, both planes, every alias and argument form,
failed/invalid capture retaining earlier saves, presentation failure and the
distinct numbered/POP consumption rules. A bytewise public-session stream
checks actions and continued replies, paired-service validation and constructor/
teardown ownership. Real CP437/UTF-8 connections at 640×480 and 720×400 cover
encoded native text, blink planes, later palette recoloring versus literal
black/native RGB, retained live grouped/button state, cursor/scroll behavior,
and an independently decoded one-pixel BMP export. Thirty distinct relevant
CTest checks pass in the native FreeBSD amd64 Release build, including image/
clipboard, reset/text-erase/mouse-backup, ANSI controls and RIP1.54 preservation.

The variable index still repeats superseded capture fields and aggregate order.
New [issue #138](https://github.com/bbs-land/remote-imaging-protocol/issues/138)
records that editorial follow-up; specific corrected 5.1/7.2 entries control the
implementation. Existing covered issue threads receive no comments. PR #137 is
now available for B136 grouped capture and awaits review/adoption; current live
group behavior and the explicit grouped-SMF rejection are unchanged in this
milestone. Full TW backups, eight-family SAVEALL/RESTOREALL, HEADER/hard reset,
stored query modes and remaining table/UI/playback families are still pending.

The 8410-unit queue now has 1919 implemented, 1747 pending, 3225 unreviewed,
1422 context, 67 excluded and 30 blocked units. Full RIP3 compliance remains
unfinished; RIP1.54's baseline and historical expectations are unchanged.

## Grouped mouse backups and current template publication

Adopt [PR #137](https://github.com/bbs-land/remote-imaging-protocol/pull/137)
and [PR #139](https://github.com/bbs-land/remote-imaging-protocol/pull/139) through
selected fork stable `95493d2643eaecfc1a02ca4264da2c0d8a1f406e`.
The earlier grouped-SMF rejection is superseded. Connected SMF/RMF and
whole-MOUSE COPY now include ordered fields, persistent selection and complete
deep-owned per-owner A[N] maps, including OFF and initially selected owners,
cross-slot bodies and defined-empty entries. Each body retains its own 4096-byte
cap; a capped effective aggregate never replaces its underlying bodies.
Shared hover handlers remain independently owned, including saved absence.

Snapshots exclude ordinary B[N], slot publishers/effective values, global input
settings, gestures, membership, continuations and feedback caches. Restore keeps
the current ordinary assignments and publisher frame across its own teardown.
A current group publisher survives only if the same kind and group number occur
in the restored fields. It then composes restored selected bodies in creation
order. Vanished/type-changed publishers release to current baselines; saved maps
cannot steal another group's slot or resurrect old publication tags. SOFT's
preserved ordinary values remain ordinary through later RMF.

Restored definitions receive fresh tokens and start outside, without a held
press. Restoring itself runs no filename, label, host or template providers,
loads no images, performs no auto-stamp and adds no button painting. It can
therefore restore logical ON while existing pixels remain OFF. Required radios
are checked only on a later lookup; selected-empty succeeds. Old committed
sources retain FIELDID and continue once, but their stale tokens cannot write
associations into restored fields with matching public IDs.

Preparation owns all replacement allocations before swapping complete maps.
Failure after entering restore retains complete old owners/maps and cancels old
input; rejected requests before application retain capture. The swap preserves
surviving publications before destroying old fields. Reusable BASE and
backup-to-backup copies remain immutable through later restored-owner mutation.
All snapshot copies discard decoded ICON and expanded filename/label feedback
resources. The first actual preview after restore prepares fresh undo resources
within its regular render operation; cancellation remains provider/file-free.
Deleting definitions does not erase persistently selected button pixels.

`rip3_templates` and `rip3_mouse_backup` verify deep ownership, defined-empty
entries, OFF maps, initially selected fields, cross-slot/other-group/type-change
publication, raw TIME bytes, caps, stale identities and allocation failures.
`syncterm_rip3_mouse_backup_CP437` and
`syncterm_rip3_mouse_backup_UTF-8` exercise actual TCP sessions at 640×480 and
720×400. They include the literal PR137 AB/XY → Z/XY → AB/XY sequence, reusable
and copied backups, held ON/preview OFF capture, coherent no-paint checks,
current HKEYOFF, radio selection, SOFT publication, a self-replacing grouped
prompt and cold HOT ICON feedback after files change. Existing grouped,
plain/ICON/clipboard buttons, hover/keyboard/session and normal ANSI controls
remain covered. These are protocol/state and bitmap tests, not exact RIPtel
pixel or native display claims.

The 8449-unit queue now contains 1954 implemented, 1752 pending, 3224 unreviewed,
1427 context, 67 excluded and 25 blocked units. B136 is answered by PR137; the
stale index in issue #138 is answered by editorial PR139 without changing the
detailed screen contract. Existing issue threads receive no added content.
Full TW backups, eight-family SAVEALL/RESTOREALL, HEADER/hard reset, stored
query modes and remaining table/UI/playback work stay pending. Mixed paragraphs
that include aggregate orchestration remain pending even where their MOUSE
clauses pass. RIP1.54's original freeze, entry-selected raster, existing ANSI
route and explicit exclusion of a new VT-102 mode remain intact.

## Stored viewport and text-window query definitions

Connected `1` + ESC query modes 1–4 now retain owned raw source under the
already pinned RIPtel 3.1 contract. Modes 1/2 store one global floating PORT/TW
expression and ignore the consumed numeric target. Modes 3/4 address existing
unprotected screen PORT/TW entries in 0..35, without selecting or creating them.
Offscreen PORTs cannot own resident expressions. Every stored definition keeps
its opaque C-string prefix: no angle, variable, control or host work runs on
storage. Empty sources reject; exact case-insensitive `$OFF$` removes; other
sources must fit 1023 original protocol units. Invalid/framing/resource failures
remain inside the void parser/session boundary and retain the old definition.

RIPtel stores these four modes but does not dispatch them on clicks or movement.
SyncTERM retains that documented omission. There is no inferred historical
click order or same-click mutation policy. Immediate mode 0 and hover modes 5/6
continue to execute through their existing paths, as do actual mouse fields.
The pinned issue #51 derivation was read with its recorded hash verified; no
original client or downloaded probe was executed for this implementation.

PORT owns its floating and numbered queries. Whole-PORT save/restore now
independently copies them along with its represented table and offscreen
pixels. Replacement preparation completes before the live table changes;
query-copy failures preserve prior live definitions/generations and reusable
saved state. Deletion/redefinition frees the removed record's query. Full SOFT
clears floating/PORT0 and deleted unprotected entries; protected entries retain
their own definitions, and independently saved sources survive.

TW owns its floating expression and each entry's source. Wire `2T` first use
copies that source independently before publishing a destination; a failed
query copy or native view preparation keeps the original selection/source.
First `$CUR(TW,n)$` has no resident expression. Existing targets retain theirs,
and ordinary geometry changes do not expand it. SOFT clears floating/TW0 and
deleted entries, retaining protected entries. Session cancellation retains
completed definitions; destruction releases all owned sources. No persistent
scene/raster/damage layer or ordinary-byte rendering work is added.

`rip3_resident_query` tests exact C-prefix/sentinel/size edges, converted
single-byte accounting, ownership and allocation failures. `rip3_ports_backup`
checks every floating/entry query-copy failure during capture and restore,
protected/reset/delete/offscreen lifetimes and independent saved strings.
`rip3_session` checks actual wire storage bytes, rejected writes, first-use
copy/default behavior, callback/resource failures, split input, ignored numeric
fields, SOFT and cancellation. Native allocation tracking verifies all resident
sources are released. Connected CP437/UTF-8 tests cover 640×480 and 720×400,
overlapping stored targets, no paint/provider work, non-execution at interior
and boundary points, generic PORT COPY and nonfatal recovery. Immediate query,
hover entry/exit and field-click positive controls prove the input paths are
active. Existing window-table, hover and ANSI control checks remain passing.

The queue now contains 1972 implemented, 1749 pending, 3201 unreviewed,
1435 context, 67 excluded and 25 blocked units, still 8449 total. Full TW
backups, individual TW-entry COPY, clipboard selection in full PORT snapshots,
eight-family aggregate save/restore, HEADER/hard reset and remaining work are
pending. Mixed source paragraphs mentioning those operations remain pending.
RIP1.54's freeze, selected device raster, existing ANSI route and no-VT102
constraint remain unchanged; no full-compliance claim is made.

## Full text-window definition backups

Connected STW/RTW now save and restore all 36 possible TW records, current
selection and activation, protection/layout state, and floating/per-entry raw
query definitions. BASE is reusable; numbered/stack saves, status/protection,
whole-table COPY and backup-only RESET(TW,...) use the existing shared manager.
Protected saves survive clearing; unprotected slot restores/copies into TBL and
POP consume their source under the manager contract. Mixed reset requests are
fully prevalidated before clearing any snapshot. Individual TW-entry COPY and
live TW reset remain separate pending operations.

The native capture contains cursor and CSI-saved coordinates, ANSI attributes
and direct colors, rendition/modes, requested cursor visibility/style, margins,
tab maps and independently owned font resources with selector mappings. It
contains no text cells, displayed screen pixels, history, renderer damage or
old connection parser input. Font captures deduplicate console-local identifiers
within each definition; restore registers them under fresh native identifiers.
The saved font data survives destruction of its original console, and reusable
snapshots remain immutable through later live rendering or definition changes.

Restore prepares complete logical records, queries, native views and fonts
before publishing them. On failure, release prepared resources and retain the
old complete definitions/selection and saved objects. The three native sink
callbacks are all-or-none; actual connections bind all three. Native standalone
clients may leave that service unbound and receive nonfatal unsupported actions.
No parse/restore failure returns through the void connection/session boundary.

New live views use the existing shared-raster console and external-pixel/damage
flags. Restoring changes definitions/cursor presentation without replaying saved
art, resizing the entry raster or restoring PORT/PAL/STYLE/ENV/WORLD state.
It keeps the current partial ANSI sequence and continues it on the restored TW;
old parser input is not part of a backup. UTF-8 retains the existing external
text-boundary policy. SOFT keeps independent saved copies. Inactive/protected
views retain their logical state, while subsequent valid activation and text
use the restored native owner normally. No ordinary-byte capture or additional
resident scene/damage layer is added.

`rip3_session` checks owned metadata/query copies, slot 35, immutable BASE,
protected slots, stack/copy/backup reset, native/record preparation failures,
callback binding validation and release. `cterm_text_window` independently
checks saved scalar state, tabs, direct colors, parser separation and failure
at every metadata/font capture/register boundary. `st_console_bitmap` checks
font data surviving source-console teardown and repeated independent register.
The connected CP437/UTF-8 suites exercise 640×480, 800×600, 1024×768 and custom
720×400 through TCP. They preserve the entry font on a predefinition save,
restore different ordinary/extended windows and requested cursor state, compare
both scene planes for no repaint, retain other table/WORLD selections, resume
live CSI, and verify inactive restoration, protected/consumed saves and reset.
A deferred right-edge wrap after restore scrolls current red artwork and fills
the new row with saved blue background, exercising the native scrolling path
without replaying saved cells. RIP-only fences leave the tested ANSI state intact.
Existing ANSI controls and the RIP1.54 preservation boundary remain covered.

The 8449-unit queue now has 1993 implemented, 1747 pending, 3177 unreviewed,
1440 context, 67 excluded and 25 blocked units. Full TW backup clauses are
accepted; full PORT clipboard selection, individual TW-entry COPY/live reset,
eight-family aggregate operations, HEADER/hard reset and remaining families
are pending. Source pin, original RIP1.54 freeze, arbitrary entry raster and
no-VT102 scope remain unchanged; no full-compliance claim is made.


## Connected PORT backup selection and protected survivors

At unchanged stable pin `95493d2643eaecfc1a02ca4264da2c0d8a1f406e`,
`SCB` / `RCB` now address the existing PORT backup family through real sessions.
BASE, numbered slots, PUSH/POP, protection and whole-PORT COPY use the same
manager and immutable snapshots. The PORT owner borrows the session's existing
clipboard selection; snapshots store its value, never a pointer into the source
session. Restore publishes that value only after preparing the complete saved
bitmap/query replacement. Unset selection and assigned port zero remain distinct;
an assigned snapshot requires a bound destination. Source-session teardown and
failed capture/restore cannot invalidate the saved tuple or change live selection.

The #26 contract also corrects the former blanket replacement of live entries.
Saved defined entries replace protected live targets, but protected live ports
absent from a snapshot survive with their viewport, pixels and generation intact.
Unprotected absent entries disappear. Saved current PORT, image/query records
and owned offscreen pixels restore; shipping cursor exceptions remain. PORT
backups contain no screen pixels, and restore leaves current PAL/STYLE/WORLD
and the entry raster alone. These explicit operations add no work to ordinary
ANSI or RIP1.54 rendering, and no scene/damage layer is introduced.

Native PORT tests check exact protected offscreen survivor pixels/viewports,
identity, unprotected cleanup, immutable restored contents, separate-owner
restoration, empty/zero/assigned selection, allocation failures and canvas
conflict. Connected CP437/UTF-8 tests cover 640×480, 800×600, 1024×768 and custom
720×400: selection changes from port 5 back to saved port 2, a subsequent `1P`
pastes the saved red image, both live screen planes stay unchanged by `RCB`,
other families retain their selections, and empty-selection snapshots retain
usable PORT definitions. Slots/stack/protection/generic COPY, SOFT lifetime and
invalid or missing nonfatal recovery are exercised. Seven distinct relevant
CTests pass (PORT, table host, session, both connected encodings, contract and
preservation). The native application/affected targets build in the single
FreeBSD Release configuration; Make reports host timestamp skew, with no
introduced C compiler warnings. Earlier unrelated passed suites are reused.

New [issue #140](https://github.com/bbs-land/remote-imaging-protocol/issues/140)
requests reconciliation of the inherited SCB empty-clipboard deletion paragraph
and narrow SCB/RCB descriptions with the accepted full PORT family. It references
#26/PR60 and #138/PR139 without commenting on their covered threads. The runtime
follows the explicit full-family rule; the conflicting source paragraph remains
blocked in the ledger. Remembered PORT_COPY source/destination rectangles and
`PCB` are still pending, so neither complete PORT backups nor `SAVEALL` /
`RESTOREALL` are claimed complete.

The 8449-unit queue now has 2004 implemented, 1748 pending, 3159 unreviewed,
1445 context, 67 excluded and 26 blocked units. Original RIP1.54 freeze, arbitrary
entry raster and no-VT102 scope remain unchanged. Full RIP3 compliance remains
unfinished.


## Connected eight-family backup composition

At unchanged stable pin `95493d2643eaecfc1a02ca4264da2c0d8a1f406e`,
`SGS`/`RGS` and `SENV`/`RENV` now use their existing typed families through
connected sessions. STYLE restore retains the live current style index while
restoring represented records. ENV restore reinstates its own settings/table
and current ENV index; current STYLE/BUT/PORT/TW/PAL selections remain live.
ENV selection/activation commands remain separate unfinished work.

`SAVEALL` and `RESTOREALL` now visit TW, PORT, MOUSE, SCREEN, PAL, BUT, STYLE,
ENV in that exact order. Each failure is diagnosed privately and later families
still run, without rollback. Valid aggregate actions return no value or variable
error. Numbered `RESTOREALL` applies all eight snapshots through a narrow
retained-slot service, then attempts all eight deletes in the same order, even
for failed restores. Protected snapshots survive deletion. Ordinary COPY and
single-family manager consumption are unchanged. BASE is reusable; PUSH/POP use
each family's independent capacity and success-dependent stack consumption.

Native event logs assert literal save/restore/delete order and detect any early
numbered consumption from inside later restore callbacks. They cover failed,
protected, missing and wholly failed families; non-atomic save continuation;
independent stacks; reusable BASE and preflight errors. The retained-slot API's
invalid/missing/failure paths are covered. ENV native tests replace old unsupported
expectations with real host actions, including restoration of slot 35.

Connected CP437/UTF-8 suites run all three presets and custom 720×400. Coherent
captures/replies verify restored screen colors, offscreen contents, native ANSI
attributes/cursor, button keyboard commitment, whole PAL/BUT selections, STYLE's
live-index defect and ENV-owned values. An absent PAL backup still permits the
screen step to overwrite the current palette's RGB and later ENV restoration.
Both encodings exercise protected saves, numbered consumption, independent
stacks and nonfatal missing/invalid action recovery. Nine distinct relevant
CTests pass, including contract/preservation guards, in the one native FreeBSD
Release CMake configuration. The application and affected targets build without
introduced C compiler warnings; Make emits timestamp-skew notices. Unrelated
passed rendering suites are reused. No ordinary-byte or drawing hot path changes.

New [issue #142](https://github.com/bbs-land/remote-imaging-protocol/issues/142)
requests reconciliation of SENV's inherited foreign-selection list with the
accepted #24/PR57 ENV ownership. That list remains blocked; runtime follows the
explicit corrected table boundary. [PR #141](https://github.com/bbs-land/remote-imaging-protocol/pull/141)
is recorded as a proposed answer to #140, awaiting a separate exact reference
adoption. No covered issue receives comments.

The 8449-unit queue now has 2057 implemented, 1712 pending, 3121 unreviewed,
1465 context, 67 excluded and 27 blocked units. Wrapper composition acceptance
is separate from complete payload inventories: remembered PORT_COPY rectangles /
PCB, remaining STYLE scope, ENV selection, HEADER and many other requirements
remain unfinished. RIP1.54's original freeze, arbitrary entry raster and the
no-VT102/external-launch exclusions remain unchanged. No full-compliance claim
is made.


## PR #141 adoption: explicit full-family SCB/RCB scope

The exact pin advances to fork stable
`73dfe86c6601990e931237984712197c354999fd`, incorporating reviewed PR141
head `fc574bab8fb3cf5948a231615eaab28c724d0354`. The
[adoption record](RIP3_REFERENCE_ADOPTION.md) retains archive/document/derivation
hashes and complete retired source evidence. SCB/RCB's empty-pointer/full-family
wording now agrees with the existing connected implementation; B140 is answered.
No runtime or legacy behavior changes. Complete PORT definitions and the new
joint example remain pending with the remembered-copy/PCB inventory; current
syntax, pointer and lifetime evidence is preserved. B142 remains independent.
Contract and preservation checks pass; unchanged build/rendering evidence is
reused. No upstream issue receives comments and no source is published.

The queue remains 8449 units: 2055 implemented, 1715 pending, 3121 unreviewed,
1465 context, 67 excluded and 26 blocked. Full RIP3 compliance remains incomplete.


## PCB remembered-pair contract follow-up

New [issue #144](https://github.com/bbs-land/remote-imaging-protocol/issues/144)
references the resolved #140/PR141 scope and #10 PORT_COPY geometry without
adding content to covered reports. PCB establishes destination-port ownership
and basic reverse copying, but its syntax defaults to CUR while the prose uses
the assigned clipboard. Stored units/normalization/clipping stage, producers
(GET_IMAGE, bitmap duplication, auto-stamp, PUT_IMAGE and PCB), update timing,
uninitialized pairs, transfer mode and lifecycle are not specified sufficiently
to select observable behavior. The preserved legacy `rv_paste` simply blits its
single clipboard at stored coordinates; it does not supply the missing v3 port
model or authorize changes to RIP1.54.

Four directly ambiguous PCB blocks are now blocked under B144; known invocation
and gate requirements remain pending. Completed PORT_COPY pixel transfers and
connected SCB/RCB/aggregate backups remain usable. No guessed pair/default/
recovery rule is installed and no PCB completion is claimed. Other unfinished
RIP3 work can proceed independently while the documentation answer is pending.
The source queue has 2055 implemented, 1719 pending, 3109 unreviewed, 1469
context, 67 excluded and 30 blocked units (8449 total). The reference pin and
original RIP1.54 freeze remain unchanged.


## Connected ENV selection and live-entry COPY

At unchanged pin `73dfe86c6601990e931237984712197c354999fd`, actual sessions
now execute `$CUR(ENV,n)$`, wire `2E` and numeric/CUR-source ENV entry COPY.
Protocol activation defaults an unused target to the published basic environment
(640×350 WORLD, base 36, width 2, palette mode and pointer 0), then reproduces the
specified departing-record base/RGB/coordinate-width overwrite before selecting
the target. That overwrite bypasses departing protection; target protection
does not block selection. Each record retains its own WORLD, pointer and baud.
Current ENV remains the single active source: no second mode cache or renderer
layer is introduced. The existing native pointer refresh reads the new record.
The lower-level selector remains a prepared-record operation; wire/CUR always
use protocol activation. Other current tables, graphics cursor/image state,
ANSI interpreter and current scene pixels do not change.

ENV entry COPY validates the defined source and every numeric/CUR destination
before writing anything. Protected sources are readable; a late protected,
invalid or self destination rejects the entire expression. Successful copies
carry definition/protection, clear protection at zero, permit repeated targets
and retain the current index. Copying into current changes its normally consumed
settings without activating unrelated table selections. The shared PAL selector
decoder keeps its established behavior. These value operations allocate nothing.

Native ENV tests check the literal 0→5→0 example, protected departing write-back,
unused slot 35, preserved custom raster, copy flags/zero/repeated destinations,
all preflight failures and zero WORLD. Wire tests validate exactly two current-
base digits independent of coordinate size, every input split and continuation
after malformed commands. Native table/session tests cover shared dispatcher
and lifetime bindings. Connected CP437/UTF-8 tests run 640×480, 800×600, 1024×768 and
custom 720×400, comparing both screen planes around activation and asserting
pointer presentation, other selectors, native cursor/rendition, stored settings,
protected wire setters versus variable bypasses, base 64/four-digit pixel output,
nonzero SENV/RENV selection, SOFT and zero-projection/invalid-input recovery.

Eight distinct relevant CTests pass, including contract and RIP154 preservation
guards. The one native FreeBSD Release CMake configuration uses `/usr/bin/cc`;
application and affected test targets build without introduced C compiler
warnings. Unrelated passed rendering suites are reused. No frozen RIP1.54,
ordinary ANSI renderer, scene ownership or external-pixel scrolling code changes.
PR143 is recorded as a proposed B142 answer awaiting exact reference adoption;
no covered issue is modified. HEADER, shared COPY for remaining families and
B144's remembered-pair/PCB work remain unfinished.

The 8449-unit queue now has 2071 implemented, 1713 pending, 3092 unreviewed,
1476 context, 67 excluded and 30 blocked units. Arbitrary entry raster and the
no-VT102/external-launch exclusions remain authoritative; full RIP3 compliance
is not claimed.


## PR143/145 adoption and connected STYLE/BUT entry COPY

Advance the selected stable pin to `f14e410915f11f64d2287eec82ebf4770c3baf41`.
The [adoption record](RIP3_REFERENCE_ADOPTION.md) retains actual PR heads,
ancestry, archive/document/derivation hashes and every retired source unit's
former evidence. PR143 answers B142's SENV foreign-selection list; existing
connected ENV evidence applies without new runtime changes. PR145 answers
B144 with an explicit portable remembered-pair/PCB contract. Its new producer,
metadata and reverse-copy requirements remain pending implementation.

New [issue146](https://github.com/bbs-land/remote-imaging-protocol/issues/146)
references covered #144/PR145 without commenting on them. Oversized logical
viewports can reach a physically clipped projected transfer: the kernel keeps
the original fit and may skip out-of-backing source samples. Physical edges can
map to fractional extent boundaries. The new pair contract needs to distinguish
logical final rectangles from physical backing clipping and specify integer
correspondence if physical clipping belongs in the pair. Two directly affected
geometry units are blocked; accepted pixel kernels are unchanged and other
RIP3 work proceeds independently.

Actual sessions now execute numeric/CUR-source `$COPY(STYLE,source,dest,...)$`
and `$COPY(BUT,source,dest,...)$`. Each validates the source and every target
before writing any value. Undefined sources are not implicitly selected;
protected sources remain readable. Protected targets, self-copy and invalid
indices reject without partial definitions, including raw protection retained
on an undefined BUT entry by the shipped reset defect. Repeated writable targets
are permitted. STYLE copies its owned represented fields and source protection,
except zero stays unprotected. BUT copies its owned definition and explicitly
marks every destination defined/unprotected. Current indices and other tables
remain live; copying into current immediately changes the existing consumers.
These are value operations with no allocations, new renderer buffers, damage
tracking or pixel work. All failures stay below the void RIP/session boundary.

Independent native tests compare every represented STYLE/BUT field and copied
value ownership, zero/repeated/current destinations, protection and complete
failure preflight. The STYLE host test's obsolete unsupported SGS/RGS assertion
now verifies successful restore and the retained live-current-index exception.
Connected CP437/UTF-8 tests exercise four rasters (640×480,800×600,1024×768 and
custom720×400), compare both planes around COPY, and assert actual copied point/
fill colors, button dimensions/face/hotkey output, editable copied BUT records,
preserved resident button appearance, other selectors, graphics cursor, native
ANSI cursor/rendition and recovery after malformed references. Expected geometry
and colors follow the contract independently of generated emulator images.

Six distinct relevant native/connected CTests pass, followed by the contract and
original RIP154 preservation guards. The one native FreeBSD amd64 Release CMake
configuration uses `/usr/bin/cc`; the application and affected targets build
without introduced C compiler warnings (the build driver reports clock skew).
Unrelated passed rendering suites are reused. No ordinary ANSI renderer, frozen
RIP1.54 source, pixel kernel or scrolling optimization changes. No speed or
native-device claim follows from these functional tests. TW live-entry COPY,
remaining STYLE/PORT inventories, HEADER and remembered pairs/PCB are unfinished.

The 8531-unit queue has 2087 implemented, 1774 pending, 3088 unreviewed,
1488 context, 67 excluded and 27 blocked units. Full RIP3 compliance remains
incomplete. The original RIP1.54 freeze, arbitrary entry raster and no-VT102/
external-launch exclusions remain authoritative.


## Connected TW entry COPY

At unchanged pin `f14e410915f11f64d2287eec82ebf4770c3baf41`, actual sessions
now execute numeric/CUR-source `$COPY(TW,source,dest,...)$`. All five allowed
live-entry COPY families have connected adapters. Validate a defined source
and every destination's range, protection and non-self identity before invoking
any helper; mixed entry/backup destinations reject before changing earlier
entries. A protected source remains readable, repeated writable destinations
are allowed, current selection stays live, and source protection follows each
successful TW definition copy except zero remains unprotected.

TW entry copying uses the existing native definition capsule and shared-raster
window factory. Each destination receives independently owned represented
native metadata/font resources, stored ordinary/extended geometry, active state,
cursor/rendition/modes/margins/tabs and resident query. Copying into current
preserves pending connection ANSI input. No cells, history or scene pixels are
copied, and no renderer buffers or damage layer are added. Other table selections,
WORLD and the entry's raster remain live. Copying the unredefined entry TTY
retains its actual native font/geometry; it does not decide the separate startup
MicroANSI/default policy or invent a TWFONT number for an unknown mode.

Permission validation is distinct from later resource failure. A native helper
failure diagnoses privately and leaves that destination unchanged while later
destinations continue. After a native definition succeeds, the logical record
is committed before allocating its resident query. Query allocation failure
leaves the copied definition with an empty query, as the accepted contract
requires; it does not restore an old query or stop later copies. No stored query
is executed during any copy. All parse/service failures stay below the void
RIP/session boundary, without becoming connection errors.

Native session tests verify source/query independence, active/current/zero/35/
repeated targets, complete permission and syntax validation, and injected native
helper/query-allocation failures with continuation and full cleanup. Connected
CP437 and UTF-8 tests cover 640×480,800×600,1024×768 and custom720×400. They exercise
the literal numeric and CUR examples, unchanged coherent primary/alternate planes
at COPY, original entry fonts, copied ordinary and extended layouts, one-based
font queries, cursor shape/visibility/live and saved coordinates, tabs, margins,
wrap/rendition, literal RGB glyph output, inactive copies, independent subsequent
edits and a split ANSI CSI across replacement of the current native owner.

Six distinct relevant CTests pass: native session/table-host, both connected
encodings, contract and original RIP154 preservation. The one native FreeBSD
amd64 Release CMake configuration uses `/usr/bin/cc`; the application and affected
test targets build without introduced C compiler warnings (build-driver clock
skew warnings persist). Existing independently verified definition-capsule,
resource and unrelated rendering evidence is reused. The original ANSI renderer,
RIP1.54 sources, scene ownership and scrolling optimizations are unchanged.

The 8531-unit queue now has 2104 implemented, 1761 pending, 3083 unreviewed,
1489 context, 67 excluded and 27 blocked units. These source-unit acceptances
cover COPY grammar/adapters and represented record/query transfer; unfinished
charset/ESC7/8, live TW reset, full STYLE/PORT inventories, HEADER, remembered
pairs/PCB and the startup font policy remain separate. B146's physical clipping
question remains open; no covered upstream issue is modified. Arbitrary entry
raster and the no-VT102/external-launch exclusions remain authoritative. Full
RIP3 compliance is not claimed.


## PR147 physical-boundary answer and live TW reset follow-up

The exact selected reference advances to fork stable
`4c8672d935e9faccdb74eb702ab7c056cf24071e`, incorporating reviewed PR147
head `55ba94bdcc2e7b852bc62ffa409cd69aa65d4a3c`. The
[adoption record](RIP3_REFERENCE_ADOPTION.md) retains archive/document/derivation
hashes, ancestry and complete retired source evidence. B146 is resolved as a
documentation question. Remembered pairs use final logical rectangles after
viewport clipping and before physical storage/sample clipping. Physical bounds
must preserve the established mapping; successful logical completion publishes
those rectangles even when no physical pixels are written. A logical no-op or
actual underlying failure does not publish merely because an outer helper
returns success. This requires no physical coverage/damage layer, rounding of
fractional edges or new sampling rule. Runtime pairs/PCB remain pending; no
working feature is claimed from this reference adoption.

While preparing live `$RESET(TW,...)$`, new
[issue148](https://github.com/bbs-land/remote-imaging-protocol/issues/148)
records the remaining unstated zero-slot font/kind/native-state and resident-query
rules, table-reset selection with a protected current window, and CUR resolution
order. The paragraph's full-screen/no-clear description is not a complete
state transition, and MTW, ordinary definition, first-use and outer SOFT have
different explicit retention/default rules. No equivalence is guessed. The
new issue references #16/#100/#101/#105/#106 and their covered PRs without adding
comments to them. Six directly affected TW reset units are blocked under B148;
known backup deletion and connected TW COPY/STW/RTW stay separate and usable.
Mixed requests containing an unsupported live TW reset remain nonfatal and
reject before changing earlier backup selectors. Other RIP3 work continues.

The source queue now has 8552 units: 2104 implemented, 1780 pending,
3077 unreviewed, 1493 context, 67 excluded and 31 blocked. No runtime or frozen
RIP1.54 code changes. Contract and original preservation guards pass; unchanged
native build and functional/rendering evidence are reused for this reference-only
update. All new pair clauses remain pending runtime acceptance. Arbitrary entry
raster and the no-VT102/external-launch exclusions remain authoritative. Full
RIP3 compliance is still incomplete.


## Connected PCB and remembered port-copy pairs

`$PCB$` and `$PCB()$` now paste from the assigned clipboard port; explicit
ASCII case-insensitive CUR or decimal 0..35 chooses that source instead. Empty
comma fields follow the existing argument parser. Invalid nonempty selectors
fail before copying; absent/dangling pointers, missing pairs and ineligible
ports are nonfatal no-ops without fallback, creation or pointer repair. `$P$`
remains ordinary user-variable lookup. These operations run through actual
connected host expansion, with no success Boolean or session error return.

Each port privately owns optional half-open DEVICE source/destination offsets.
Successful logical transfers publish on the receiving port; physical clipping
or zero sample coverage never shrinks those offsets or refits the mapping.
The shared copy geometry retains its initial equal-size coupled trimming and
unequal-size independent fitting. PCB reverses the source's old offsets into
current viewports under COPY, independent of WORLD, radix and global W.
Source pixels/pair are read before overlap writes. A failed transfer keeps the
old pair; a newly allocated but uncopied capture has none. Protected defined
content targets permit publication. Selection, clipboard pointer, unrelated
tables, text cursors and image-style state retain their existing rules.

Wire PORT_COPY, GET_IMAGE, shared LOAD capture/explicit duplication, initial
button auto-stamp and PUT_IMAGE now publish through their existing transfer
stages. Ordinary drawing/erase, file rendering, placeholder/wallpaper and later
button redraws preserve metadata. Whole PORT backups and backup copies own
both rectangles and absence, alongside the previously accepted complete
PORT definition/query/offscreen-content/current/clipboard-pointer payload.
Restore applies the saved pair after backing recreation; protected live slots
absent from a snapshot keep their pairs. Unrelated SCREEN saves do not own it.
New definitions/deletion/backing replacement and the SOFT/HARD core apply their
documented clearing rules. Mixed lifecycle clauses involving unfinished HEADER
or live viewport-only RESET remain pending.

A new independent connection test first exposed stale graphics pixels when
capturing ordinary ANSI output. Screen-source transfers now take a temporary
snapshot of only the requested physical region and merge native ANSI pixels
using existing console readback and paint bits. Pending graphics writes from
the same compound command take precedence. Existing matching graphics indexes
stay indexed; ANSI truecolor remains RGB. There is no new resident scene buffer
or damage map, and this readback does not run for ordinary text rendering or
offscreen sources. Other screen-reader/export consumers are outside this
specific acceptance; no general rendering-speed claim is made.

Eight relevant native suites pass, plus connected PCB fixtures in CP437 and
UTF-8 on 640×480, 800×600, 1024×768 and custom720×400. Independent tests cover
PR147's physical-boundary vectors on both axes, zero physical writes, original
copy geometry, no-pair/selector cases, WORLD/radix/mode independence, moved
origins, literal overlap BCDD/BBCD, protected content, allocation/read failures,
GET_IMAGE3×3→4×4, nonzero source-viewport PUT_IMAGE, auto-stamp, duplicate/backup
ownership and ordinary ANSI truecolor capture. The native boundary extension
also checks bounded readback without refitting a physically truncated source.
All connection cases continue afterward. Existing independent PORT backup/COPY
and file-load producer evidence is reused.

The application and affected tests build in the one native FreeBSD amd64
Release CMake configuration using `/usr/bin/cc`, without introduced C compiler
warnings. Contract and original RIP1.54 preservation guards pass. The queue now
has 8552 units: 2190 implemented, 1691 pending, 3077 unreviewed, 1496 context,
67 excluded and 31 blocked. Full RIP3 compliance, HEADER, live TW reset under
B148, remaining reset/ANSI fields and startup-font policy remain incomplete.
RIP1.54's original freeze, arbitrary entry raster and no-VT102/external-launch
constraints remain authoritative.


## PR149 live TW reset reference adoption

The exact selected stable reference is now
`393c5ebee54687f329c85d8b17db641e4210a18a`, including reviewed PR149 head
`174853b8aad354448f88335cdfb900e2469f3a8b`. The [adoption record](RIP3_REFERENCE_ADOPTION.md#pr149-live-text-window-reset-completion)
retains the complete inventory, hashes, moved reviews and retired source text.
B148 is answered by explicit fresh-zero defaults, fixed/floating query lifetime,
protected current selection, dynamically resolved CUR, whole-list validation and
per-target failure continuation. These are portable state rules, distinct from
historical native-client observations. They preserve arbitrary entry raster,
connection parsing and the original RIP1.54/no-VT102 boundary.

Live `$RESET(TW,...)$` remains pending runtime integration and independent
connected acceptance; adopting its answer does not make the command usable.
The 8608-unit queue contains 2190 implemented, 1747 pending, 3076 unreviewed, 1503
context, 67 excluded and 25 blocked. No implemented source claim is discarded;
all 70 earlier derivations and original ALPHA4 bytes remain unchanged. Contract
and original preservation guards pass. No unchanged product build or rendering
suite is repeated for this reference-only update, and the owned archive is
removed. Other RESET/HEADER work and full RIP3 compliance remain unfinished.


## Connected live text-window reset

`$RESET(TW,...)$` now reaches actual sessions for numeric/CUR/TBL live targets
and their combinations with BASE, numbered slots, SLOTS, STACK, BACKUP and ALL.
The full selector list and required bindings are validated before any effect.
CUR resolves from live selection when reached; a protected/undefined target is
a no-op and does not stop later operations. TBL visits zero then nonzero slots
in order. ALL also clears independent TW backups, retaining protected save slots.

Resetting zero forces a fresh ordinary native window using the configured
default MicroANSI font/wrap and full nominal geometry. It bypasses ordinary-w
identity and does not invoke outer SOFT or MTW. The current adapter uses the
existing fresh-window constructor for normal colors/rendition/margins, both
cursor coordinates, explicit tabs and requested cursor visibility/shape. Native
resources are prepared before replacing the logical definition or fixed query.
A nonzero applied target is deleted; only deleting current selects the existing
zero, retaining its kind, font, state and activation. Numeric zero does not
change another current window, including protected/inactive current. TBL/ALL
retain a protected current nonzero window. Successful replacement/deletion drops
the target's fixed query; protected/undefined no-ops retain it. The floating TW
query survives, no providers run, and unrelated families/backups remain live.

Target resource failures retain that logical target/query/selection, diagnose
inside RIP3 and continue later valid selectors. Earlier completed actions remain;
ALL still reaches backup deletion. This supplies no Boolean or session error.
Native tests inject a first deletion failure followed by dynamic CUR retry and
a failed zero replacement followed by ALL's remaining deletions/backup clear.

Both connected encodings pass on 640×480, 800×600, 1024×768 and custom720×400,
including forced extended/inactive and identical-geometry zero replacement,
protected-current TBL/ALL, existing inactive extended-zero fallback, dynamic CUR,
late-invalid-selector rejection, unchanged pixels, native default colors and
cursor/tab state, actual CP437/UTF-8 glyph output and partial CSI continuation.
The existing TW backup fixtures now use a late invalid selector for their
prevalidation check and pass in both encodings. Native tests cover all five
configured default font grids and both wrap choices, query ownership/lifetime,
selection, recoverable failures and cleanup. These are eight distinct relevant
CTest suites including contract and original RIP1.54 preservation. The native
FreeBSD amd64 Release CMake application/session/table-host build uses
`/usr/bin/cc` and introduces no C compiler warnings. Previously accepted native
constructor/interpreter/capsule evidence is reused.

This is connected behavior for represented native fields, not acceptance of the
entire fresh-state profile. G0/G1 designation/invocation, the larger ESC7/8
snapshot and remaining per-window mode acceptance remain pending. Connection
partial CSI preservation does not prove the missing larger snapshot. The
frontend currently supplies default font 0; the user's deferred startup/default
font policy remains unresolved. Broad clauses/vectors containing those fields
stay pending. The 8608-unit queue now contains 2238 implemented, 1703 pending,
3069 unreviewed, 1506 context, 67 excluded and 25 blocked. No ordinary ANSI/RIP1.54
renderer, scrolling optimization, resident scene or damage tracking is changed;
no speed benchmark is claimed. HEADER/other RESET families and full RIP3 remain
unfinished, with arbitrary raster/no-VT102/external-launch constraints intact.


## Connected ANSI-only RIP3 character-set state

Six ANSI escape sequences now designate G0/G1 as UK, US or special graphics
on actual RIP3 connections. They record the selected final byte without ink or
cursor movement; unknown finals retain the previous value. Initial/fresh sets
are US/US and invocation remains G0. Display stays in the CP437 bank, including
SI/SO's music-note/sun glyphs without a shift to G1. This implements the adopted
RIPtel ANSI profile; no VT-102 mode, selector or glyph translation is added.

The existing native window records now own two designation bytes. Ordinary
changed/identical definitions and deactivation retain them; extended definitions
and fresh CUR/live-zero reset use US/US. Selection retains target values, while
wire first-use cloning, entry COPY and whole TW backups carry independent
values. ESC c and SOFT retain the stated sets: SOFT uses TW0's sets for its
ordinary redefinition and preserves each protected surviving window's own sets.
CSI s/u does not restore designations. Existing temporary/native definition
capsules carry the fields; no resident scene, damage map or rendering layer is
introduced. Coherent test-control state exposes the two final bytes for tests.

Split-input tests exposed existing parsing of A/B after a parenthesis as fresh
C1 controls. The correction applies only to RIP3 ANSI designation parsing;
other routes retain their previous sequence handling. Normal glyph rendering
is unchanged. No speed benchmark is claimed.

Native tests cover all six designations at every split across a retired source
owner, unknown finals, literal bank glyph identities, CSI/ESC-c/preliminary-reset
state, selection versus clone/fresh defaults, owned snapshot restore after source
destruction, and unchanged OFF/RIP1 designation handling. Connected CP437/UTF-8
fixtures pass on 640×480, 800×600, 1024×768 and custom720×400, checking logical
state, pixels/cursor, ordinary/extended/live reset, selection/clone/COPY/backup,
protected survival and partial designation continuation. The literal ANSI vector
has a separate immutable MicroANSI asset bitmap oracle at 640×480; it does not
use an emulator-generated expected image or assert RIPtel pixel parity. These
five distinct CTests include contract and original RIP1.54 preservation. The
native FreeBSD amd64 Release CMake application/affected target build uses
`/usr/bin/cc` and introduces no C compiler warnings; unrelated rendering and
previous reset/backup verification are reused.

New [issue150](https://github.com/bbs-land/remote-imaging-protocol/issues/150)
requests the missing full ESC7/ESC8 snapshot payload, restore target, initial/
reusable lifetime, protection/activation and resource/owned-query boundaries.
A native 0xf1-byte copy is not a portable field or pointer-ownership contract.
The issue references covered #104/PR107, #106/PR110 and #148/PR149 without adding
comments there. Five directly affected source units are blocked under B150.
Existing simple ANSI cursor handling does not establish full TW snapshot
compliance; no full ESC7/8 implementation is claimed. The 8608-unit queue now
contains 2254 implemented, 1680 pending, 3069 unreviewed, 1508 context, 67 excluded
and 30 blocked. Remaining reset/insert-mode fields, HEADER, startup/default-font
policy and many other families remain incomplete. The selected pin, original
RIP1.54 freeze and arbitrary-raster/no-VT102/external-launch scope are unchanged.


## Connected viewport-only reset

`$RESET(VIEW)$` now resets the current drawing viewport. Explicit CUR,
decimal 0..35 and TBL are supported; all selectors and the live PORT binding are
validated before effects. A reset restores the full dimensions of each defined,
unprotected drawing port and activates its viewport. Protected/undefined targets
are no-ops. This is distinct from MVP, whose existing inactive-viewport skip is
preserved. VIEW uses existing PORT state and has no separate backup family;
ALL/BASE/slot/stack/backup selectors are invalid here and cannot apply a valid
prefix before rejection.

The operation retains pixels, the current drawing port, assigned clipboard,
resident fixed/floating queries, graphics cursor, image-style tuple and remembered
copy offsets. No query executes and no pair is published. Screen ports keep
their port origin and physical extent; offscreen ports keep their backing size
and contents. Other text windows/tables/backups remain unchanged. This host
variable changes drawing-port configuration; ANSI escape parsing remains in
CTerm and is untouched.

Native port/table-host/viewport-host suites and connected CP437/UTF-8 fixtures
pass. Coherent connection captures cover all four existing rasters, inactive
activation versus MVP, zero WORLD/base64 independence, implied/numeric/repeated/
TBL selectors, protected/undefined entries, late-invalid rejection, no paint,
retained ordinary text/foreign selections and actual PCB reuse after reset.
The screen sub-port case verifies its first/last included pixels and first
excluded far-edge pixel. Native tests additionally inspect retained query bytes,
copy rectangles, clipboard and cursor/image metadata, plus missing-binding
recovery. The drawing cursor expectation includes its existing viewport-origin
contribution; pixel command X uses its documented context-absolute coordinates.
Previously accepted PCB/full-reset lifecycle evidence is reused. Mixed HEADER/
HARD clauses remain pending despite the completed viewport-only preservation.

The seven distinct relevant CTests include contract and original RIP1.54
preservation. The one native FreeBSD amd64 Release CMake application/affected
target build uses `/usr/bin/cc` without introduced C compiler warnings. No
CTerm, ordinary ANSI or RIP1.54 renderer, scene ownership or damage tracking is
changed; no speed benchmark or additional platform/compiler build is claimed.
The 8608-unit queue now contains 2261 implemented, 1678 pending, 3062 unreviewed,
1510 context, 67 excluded and 30 blocked. Full RIP3, other RESET families, HEADER,
full ESC7/ESC8 snapshot under B150 and the deferred default-font policy remain
unfinished. Arbitrary entry raster and no-VT102/external-launch scope remain.


## PR151 ANSI ESC snapshot reference adoption

The selected stable reference is now
`26128c7043d90cd00aa95c6b44aadbfc185f6a3a`, including reviewed PR151 head
`685c3d5daca00837dcbef83e2ad40261e748e95b`. The [adoption record](RIP3_REFERENCE_ADOPTION.md#pr151-ansi-esc-snapshot-completion)
retains exact inventory/hash/retired evidence. B150 is answered by an explicitly
completed logical payload, captured-live-to-CSI overwrite, eligible current-target
restore, owned query/resource resolution, reusable lifetime and atomic/no-paint
failure rules. Hard/session clearing is distinct from retaining SOFT/live-reset/
selection/COPY/whole-text-window-backup behavior. Stored native pointers or a
historical record byte count are not the payload contract.

The full ESC7/ESC8 runtime remains pending, including safe continuation after
replacement of the current native window, compatible logical font resolution and
independent query ownership. Existing whole-text-window backup capsules own
font resources, so they cannot be used unchanged as the ESC snapshot payload.
ANSI escape recognition remains in CTerm; it is not moved into RIP framing. Existing designation/CSI/window/reset/backup features stay
usable. New clauses are pending runtime acceptance, with original source facts
separate from portable completion. No implemented claim is retired. The 8675-unit
queue contains 2261 implemented, 1740 pending, 3062 unreviewed, 1520 context,
67 excluded and 25 blocked. Contract and original preservation guards pass;
unchanged builds/rendering suites are reused for this reference-only change.
Original RIP1.54, arbitrary entry raster, the no-VT102/external-launch scope and
the user-deferred startup/default-font policy remain controlling. Owned reference
archive/review scratch files are removed, and full RIP3 compliance is incomplete.


## Connected ANSI ESC7/8 text-window state

CTerm now recognizes ESC7/8 on the existing ANSI route and yields after the
completed sequence with a successful consumed-prefix count. The connection
owner handles the action after native input unwinds, then feeds following bytes
to the resulting selected native window. No ECMA parsing moved into RIP framing,
and command/resource failures remain diagnosed and consumed below the void
session boundary. Ordinary ANSI/RIP1/non-owner handlers keep their existing
cursor behavior; CSI s/u retains its separate per-window coordinates.

One optional interpreter-owned save independently owns the logical definition,
fixed query and represented native metadata. It captures the live cursor for
both restored live and derived CSI-saved coordinates without overwriting the
source's previous CSI save. Geometry/metrics and logical font selectors survive
source reset/deletion; restore resolves compatible current font resources and
never owns old font bitmaps or handles. Query and native preparation precede
publication. An absent save, inactive input or protected target does nothing;
protected sources can be saved, protection carries to nonzero targets, and zero
stays unprotected. Repeated restore retains the save. Floating queries,
connection encoding and selected window index remain live. Existing native
raster views preserve pixels without clear/redraw/relocation or history replay;
no scene buffer or damage tracker is added.

`cterm_text_window` covers independent CSI/live coordinates, all ESC7 splits,
origin-aware coordinate installation, explicit tabs, direct colors, designation
state, metadata allocation failures, missing live font resources and original
ANSI/RIP1/non-owner behavior. `rip3_session` covers atomic native/query failures,
old/absent saves, ownership, floating-query exclusion, protection and teardown.
`syncterm_rip3_ansi_save_CP437` and UTF-8 use coherent TCP control-bitmap captures
at 640x480, 800x600, 1024x768 and 720x400: entry font, ordinary font2 80x25, changed
extended geometry/font, requested hidden cursor, WORLD independence, same-batch
and split ESC8 followed by accented text, source deletion, CSI/ESCc, SOFT/live
RESET ALL, entry COPY and independent whole-window backups. The existing CP437
whole-text-window backup fixture also passes after the shared capsule change.

The queue now contains 2306 implemented, 1695 pending, 3062 unreviewed, 1520 context,
67 excluded and 25 blocked units (8675 total). 45 clauses gain scoped runtime
acceptance. Complete per-window mode support, MTW-specific lifetime vectors,
HEADER/hard-reset lifetime integration and currently unexposed binding vectors
remain pending. Full RIP3 compliance is incomplete. The original RIP154 freeze
and user-deferred startup/default-font policy are unchanged; no speed claim is
made from these functional checks.


## Connected scene header and input lock

The RIP3 session decodes level-0 `h` in base 64 and validates every segment
before applying any effect. It accepts the pinned revision and flag ranges,
wire continuations, and pipe, CR, LF, and form-feed terminators. A zero segment
count ignores additional decoded fields. The historical type-9 argument stride
is evaluated within bounds so the standalone invalid form and documented mixed
form retain their distinct outcomes. The enclosing session handles runtime
failures and continues with subsequent segments.

The connected path now applies SOFT and the represented HARD reset operations,
selects the six table zeros, and runs the typed segments in wire order. Those
segments can select and prune live tables, update environment settings and
viewport bounds or activation, clear selected backup areas, stop waveform
playback, and clear the physical screen to RGB black. Numbered backup deletion
skips protected saves and clears one existing slot per segment. The scene input
lock consumes keyboard and mouse events and selects the wait pointer. A valid
`#` or subsequent header releases it. Incoming commands and replies continue
during the lock.

An accepted hard header or `RESET(HARD)` clears the ANSI text-window save
before fallible setup. SOFT and headers without a hard reset retain the save.
Native tests inject a failed hard setup and verify that a later environment
segment still applies. Headless TCP tests exercise the real terminal input and
screen in CP437 and UTF-8 at 640x480, 800x600, 1024x768 and 720x400.

The type-3 window segment has connected coverage for active-state and
caret gates, current versus all targets, protected entries, fixed and
floating query cleanup, and base, numbered and stack backup cleanup.
The [completed type-3 profile](#connected-header-text-window-geometry-and-erase)
also covers retained-font geometry, target-local cursor home and the current
palette's index 0 erase.

Header viewport bounds reset now uses the existing operation that skips
inactive and protected viewports. The TCP fixture verifies that a reactivated
viewport retains its narrow clip until an active bounds reset. The original
clipping-application branch also paints black, so issue #153 requests the
pixel and clipping boundary for screen and bitmap ports.
[Upstream issue #153](https://github.com/bbs-land/remote-imaging-protocol/issues/153)

Physical port deletion, palette upload parity and the full HARD state
inventory also remain pending.


## Connected refresh expressions

`2R` validates four reserved digits in the current base and stores the
expression on the connection. It produces no reply at definition time.
`$REFRESH$` and Alt+F5 expand the stored expression using values at the time
of the request and send it as a separate host reply. A delay or prompt in
that expansion suspends the caller and the reading of subsequent commands.
Membership in the ordinary host expansion classes still controls where
the variables can execute. The refresh evaluator uses ordinary control
notation and live lookups with template slots disabled.

`$NOREFRESH$`, an empty definition, `$OFF$`, SOFT, HARD and header flag 16
clear the binding. A malformed or failed replacement leaves the previous
binding available. Headers without a reset or flag 16 retain the binding.
The local shortcut respects the scene input lock.

Static inspection confirmed the original slot 117 handler at RVA
`0x46bd9`, assignment at `0x3e43c`, and the independent send path at
`0x3e4bb`. The ordinary `REFRESH` callback at `0x29031` calls that sender
and emits no inline replacement. These traces used the same verified
driver cited in the scene header review. They did not involve executing
any original binaries.

The native session tests cover ordering through delays and prompts,
cancellation, failed allocation, template and graphical exclusions, and
recursive invocation. TCP tests cover both wire encodings, control bytes,
late expansion and the connection's key-event handler on 640x480 and 720x400
rasters.
Pick-list evaluation remains pending.

The queue contains 2396 implemented, 1686 pending, 2976 unreviewed, 1520
context, 67 excluded and 30 blocked units.


## Connected HEADER text-window geometry and erase

Type-3 flags 1, 4, 1024 and 2048 now apply the retained-font maximization
through the live text-window owner. Each eligible window keeps its font,
ordinary/extended kind, ANSI definition and queries. Its nominal grid uses the
existing device cell metrics, with both rectangles at device origin and full
vertical margins. The addressed window's live cursor homes. Its CSI-saved
position and requested caret state persist. The operation prepares a replacement
view through the existing definition-copy machinery without copying or
repainting scene pixels. Native resource failure preserves the target and
allows later targets in the header to proceed.

A header that explicitly maximizes an unredefined entry window uses the default
mode's nominal grid with the entry window's existing metrics and font binding.
The configured entry raster and its unassigned MicroANSI mode remain intact.

Flags 4096 and 8192 use the documented shared whole-window erase with current
graphics palette index 0. Connected captures distinguish that color from ANSI
and STYLE backgrounds and verify the centered margins, physical screen
placement, offscreen port selection, activation and protection behavior. Erase
homes the target's live cursor and retains its CSI-saved position. The existing
caret flags retain their independent active-state gate.

The header fixtures exercise CP437 and UTF-8 on 640x480, 800x600, 1024x768 and
720x400. They verify retained font and kind, stage-local activation, ordinary
corners, target-local homing, no geometry repaint, ANSI attributes and tabs,
and CSI cursor recovery. The native session fixture checks failure continuation
and fixed/floating query retention. Native CTerm definition and connected TW
COPY tests cover the reused definition transfer. Independent vectors for the
full set of retained ANSI settings, resource bindings and snapshots are pending.
The type-2 graphics viewport clipping contract remains separately tracked in
issue #153.

The queue contains 2431 implemented, 1688 pending, 2976 unreviewed,
1531 context, 67 excluded and 26 blocked units.
