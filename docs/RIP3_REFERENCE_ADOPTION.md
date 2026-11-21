# RIP3 reference adoption

The user authorized adopting the open [documentation integration PR
#71](https://github.com/bbs-land/remote-imaging-protocol/pull/71) at commit
[`a9e0c2f490714d82a0827675ffbc074aa5ae00e6`](https://github.com/bbs-land/remote-imaging-protocol/tree/a9e0c2f490714d82a0827675ffbc074aa5ae00e6).
It replaces reference `2fb17724b6122a5ad5cd1df38b69b8cce3a7079f` for the separate
RIP3 engine. The PR was open, not merged, at adoption. Later commits are not
silently included. Existing upstream issues must receive no further content.

The reviewed inactive-viewport follow-up is now adopted at
[`5ae9d165b1ac3e36161847e68db0d763228b779b`](https://github.com/bbs-land/remote-imaging-protocol/commit/5ae9d165b1ac3e36161847e68db0d763228b779b).
It changes the viewport and JPEG pages plus the #21
derivation: wire 1i updates inactive/protected current ports, and reactivation
retains that configuration. IMAGE display and IMGSTYLE keep separate gates.
That update indexed 7353 blocks in the same 65 documents. The
adoption record's `updates` records this step; its claim-accounting IDs remain
tied explicitly to the initial reviewed revision. Both downloaded source trees
were independently reindexed and their temporary directories removed.

The reviewed DEFINE_PORT clarification is PR [#73](https://github.com/bbs-land/remote-imaging-protocol/pull/73)
at [`ea8eacdf83593d0e5ccdb706a9c065ffca2cda9c`](https://github.com/bbs-land/remote-imaging-protocol/commit/ea8eacdf83593d0e5ccdb706a9c065ffca2cda9c),
reviewed after the user supplied it as the answer to issue #72. It was open at
adoption. The direct comparison with the previous pin changes seven normative
pages; six have only formatting changes. The substantive DEFINE_PORT changes
prioritize the published protocol over conflicting DLL observations: all four
initial flags act independently, offscreen near coordinates are zeroed before
sizing, protected redefinition selects port zero, and allocation-failure final
state has no documented guarantee. No viewport contract changed. Documentation
blocker B72 is resolved locally; implementation acceptance is tracked separately
in [the DEFINE_PORT milestone](RIP3_CONFORMANCE.md#define_port-protocol-implementation).

The PR #73 index contained **7357 blocks in 65 documents**. Exact and separately
verified formatting-only matches retain their review evidence. New or changed
DEFINE_PORT requirements remain pending. The adoption record retains the prior
pin, archive digests, 35 derivation identities and hashes of both original
protocol text sources. The original PR #71 claim accounting remains explicitly
tied to its initial revision. Both reference archives were independently
reindexed in automatically removed temporary directories; no upstream probe
code ran and no GitHub issue was modified.

The reference source moved to
[`RealDeuce/remote-imaging-protocol:stable`](https://github.com/RealDeuce/remote-imaging-protocol/tree/stable),
pinned at `c9a66b27edc93d4522b21510c1379eb10230f85d`. All 65 normative documents,
34 of 35 recorded derivations and both original protocol text sources are
byte-identical to the reviewed PR #73 reference. The remaining derivation changes
only its delivery-status link-check total, from 5,441 to 5,440. The complete
reindex preserves every source block and review. Later branch movement requires
a new review and exact pin.

Documentation issues continue to be filed against the
[parent repository](https://github.com/bbs-land/remote-imaging-protocol/issues),
not the reference fork. Once an issue has a PR, put additional findings in a new
issue referencing the related issue and PR. The user's existing instruction
against adding content to existing issues remains in effect.

The target remains RIPtel 3.1's RIPscrip 3.0 behavior, with geometric/spec
compliance rather than pixel reproduction. The RIP1.54 source/data baseline
remains `e49ee49a0ee97b397dd190eb4e3ab3fe3bbffb7a`; upstream corrections to 1.54
reference prose do not authorize changing the frozen local implementation,
assets or expected results. External application and URL launching remain
excluded. No code is pushed and no PR is published by this adoption.

## Drawing-contract update from PR #77

That adoption used stable revision
[`72edbfc6c151d86a120fec3cabbde3cad32c9c0b`](https://github.com/RealDeuce/remote-imaging-protocol/tree/72edbfc6c151d86a120fec3cabbde3cad32c9c0b).
It includes open [PR #77](https://github.com/bbs-land/remote-imaging-protocol/pull/77)
head `af5468de5f5d1e4ad91b389c5766be5ed1f1293f`, answering issues #74–#76.
Review covered the changed normative text, both new composition pages and their
source/inference boundaries. The exact downloaded archive digest is
`9a1b8592ad4dfcb953a0103f12393ddb63c23da23934ca7f834f2e3065e760a7`.

Fourteen normative documents change and two are added: the index now contains
7455 blocks in 67 documents. Exact source/heading matches retain 7338 reviews;
927 line references move. Eleven implementation claims are replaced/removed and
five otherwise unchanged claims are reopened. Seventeen requirements become
pending after the documentation answers resolve B74/B75/B76. The new contract
prioritizes protocol write operations, N0 suppression and COPY/solid or patterned
filled borders over conflicting historical client observations. It supplies
optional Q14 geometry without requiring integer-overflow reproduction. The
[implementation inventory](RIP3_CONFORMANCE.md#pr-77-drawing-contract-adoption)
records pending consumer changes; adoption is not implementation acceptance.

Only ledger/adoption consistency is newly checked. The native build, affected
font tests and legacy preservation results from the preceding implementation
remain valid and are reused. No upstream checker or client is executed. The
owned archive/extraction and scratch metadata are removed. No existing issue
is modified, and no code is published.

## Border/style update from PRs #80 and #81

That adoption used fork stable
[`a52985019cbf4754d4d08a07b4f7755761390661`](https://github.com/RealDeuce/remote-imaging-protocol/tree/a52985019cbf4754d4d08a07b4f7755761390661).
It includes open [PR #80](https://github.com/bbs-land/remote-imaging-protocol/pull/80)
head `ca75dd486c3b7099c7c127edc29170a841396d1c` and open
[PR #81](https://github.com/bbs-land/remote-imaging-protocol/pull/81) head
`35fd9731bb8686d8830ee0bd21fb8d67ba856c5a`, answering #78/#79. Archive digest:
`c66c70ba2ebf4be9a2b6d8585c8a258b2c7800dea4918f3f2f04464ef1a17b08`.
Both new derivation documents have recorded hashes and source-boundary review.

Four normative documents change; the inventory remains 67 documents. Matching
heading/content blocks retain 7445 reviews, with 68 moved line references. There
are 35 new/replaced source blocks and 7480 total. Three replaced implementation
claim IDs were table cells with changed layout/format spelling; their replacement
rows receive evidence under the newly reviewed contract. Existing unrelated
claims are retained. N uses the explicit protocol current-base rule over the
native fixed-MegaNum descriptor; native normalization alone does not establish
UltraNum wire support. 2Y uses one plus two current-base digits and independent
ordered protect-zero diagnostics. The global graphics-processing gate remains
pending session integration, separate from inactive viewport behavior.

The [implementation inventory](RIP3_CONFORMANCE.md#pr-80-and-pr-81-adoption-and-ordered-style-diagnostics)
records changed code/tests and remaining work. Only affected native tests and
focused adoption guard cases are required; unchanged font/render/freeze evidence
is reused. No upstream issue was modified. Issue #82 remains independently open;
these PRs do not address its RFF bbox labels. No temporary archive or extracted
tree is left behind: archive and selected source data were processed in memory.

## Signed font-bbox update from PR #83

That adoption used fork stable
[`f77f918229934bfc4560860e044c48aaf86e28b4`](https://github.com/RealDeuce/remote-imaging-protocol/tree/f77f918229934bfc4560860e044c48aaf86e28b4),
including open [PR #83](https://github.com/bbs-land/remote-imaging-protocol/pull/83)
head `e8591c1af34e0e47aceef354c6d80e095d72978f`. Archive digest:
`8b8b8ab08f944d441f29a063e7c910e7c7193490806ba0b129ae0d02e2694fc8`.
The new field/signedness derivation has a recorded hash and explicit read-only
evidence boundaries. No new original-engine execution or exact hinting guarantee
is inferred from its correction.

Three normative documents change in the unchanged 67-document inventory. The
index preserves 7466 heading/content matches, moves 70 line references and reviews
38 new/replaced blocks, yielding 7504 total. The replaced spacing claim is
reconciled with the new realized horizontal-span producer and independent native
captures. B82 is resolved in the pinned contract. FF1 predecessor/header-copy
material remains contextual; the native decoder/realizer consumes the canonical
signed metrics rectangle. B84's argument-byte/escape question remains pending.

The [implementation inventory](RIP3_CONFORMANCE.md#pr-83-adoption-and-realized-font-bounds)
records decoder, realization, rounding policy, spacing integration and affected
tests. Unchanged font bytes, legacy preservation and argument-parser evidence are
reused. No existing issue thread is modified and no code is published. Scoped
build logs are removed; no reference archive/extraction artifact is retained.

## Dollar-argument grammar update from PR #85

That adoption used fork stable
[`5224b709268fbda7d38a4c40cd2635374d9a6023`](https://github.com/RealDeuce/remote-imaging-protocol/tree/5224b709268fbda7d38a4c40cd2635374d9a6023),
including open [PR #85](https://github.com/bbs-land/remote-imaging-protocol/pull/85)
head `a47aa2d40405a3e156f745736601abe016766a9d`. Archive digest:
`3456c9cbcd3e591b50e6f63c035e02c947d47450bdfa81cd0b645d32485cf7c1`.
The argument-state/byte-predicate derivation has its own recorded digest. Only
two normative documents change in the same 67-document inventory. The complete
source-block reindex retains 7502 exact reviews, moves 1831 line references and
adds/replaces 42 blocks, for 7544 total. Existing implementation claims all match
unchanged source; the replaced B84 argument block receives fresh local-decoder
evidence rather than carrying its old blocked state.

The reviewed contract specifies accepted payload bytes, local quote overwrite,
delimiter precedence and decoded-byte limits, and distinguishes these from outer
ordinary/host pass handling. B84 is resolved in the adopted reference. Existing
#6 count/slot/space answers, earlier references, original assets and B86's font
metric/cache questions are unchanged. The [implementation milestone](RIP3_CONFORMANCE.md#pr-85-adoption-and-argument-local-decoding)
records completed local argument parsing, affected tests and explicitly pending
outer expansion work. It does not claim full ordinary/host processing.

Only affected native tests, three reference guard cases and ledger consistency
are checked. Prior font/drawing/legacy evidence is reused. The archive is processed
in memory without extraction; scoped build logs are removed. Existing upstream
issue threads receive no comments or changes. No code is published.

## Outline metrics and user-variable policy from PR #88/#89

That adoption pinned fork stable
[`ab37e09a1d239f21ae5d748c5b6cddbea9522a20`](https://github.com/RealDeuce/remote-imaging-protocol/tree/ab37e09a1d239f21ae5d748c5b6cddbea9522a20),
including open [PR #88](https://github.com/bbs-land/remote-imaging-protocol/pull/88)
head `9962a60dce7df526cfe42051d70757f3b43a9172` and open
[PR #89](https://github.com/bbs-land/remote-imaging-protocol/pull/89) head
`3240a25d2026d4be3275f066b898a07192884c54`. Exact archive digest:
`d3fb8b6046b8102cc7c10612daa25fad6de306f37423b0c4089db6571b79dcd0`.
Both new derivations have recorded digests and explicit evidence/policy boundaries.

Seven existing normative documents change; the new outline advance/cache page
increases the inventory to 68. The complete block reindex retains 7480 exact
reviews, moves 2093 line references and adds/replaces 151 blocks, giving 7631.
The update record preserves prior accounting and records no removed implementation
claim. New/replaced text is reviewed independently rather than silently inheriting
its old classification. B86/B87 are resolved in the pinned documentation;
implementation acceptance remains separate.

The reviewed metric policy separates ordinary string-axis DPI from cached X-width
and prescribes wide per-byte rounding, no ordinary kerning, effect-independent
advances and atomic context-wide snapshot lifetime. User-variable policy corrects
session storage to `=`, rejects `%`, ignores repetitions and specifies memory-first
lookup, persistent precedence, the complete prompt/effect matrix and distinct
empty/cancelled/suppressed output. The explicit portable choices are normative
without being attributed to unexecuted original client paths. Existing #3/#6/#22/
#24/#82/#84 answers remain in force.

The [implementation milestone](RIP3_CONFORMANCE.md#pr-8889-adoption-ordinary-metric-axes-and-actual-user-prefixes)
records corrected ordinary metric axes/wide arithmetic and user prefix decoding,
three affected native tests, the adoption guards and pending cache/prompt execution.
Prior unrelated evidence is reused. Original assets and RIP1.54 remain unchanged.
The separate conditional operand grammar is requested in parent issue #90;
adopting these PRs does not reopen their completed contracts or modify their issues.
Archive processing is in memory and scoped build logs are removed. No existing
issue thread is modified and no code is published.

## Conditional, outline-placement and response-limit updates

The preceding pin was fork stable
[`d046bb4b1f9d066b8534ac9b1369a63abee4563c`](https://github.com/RealDeuce/remote-imaging-protocol/tree/d046bb4b1f9d066b8534ac9b1369a63abee4563c).
It includes reviewed open PR #92 head `63e6e1541de7fe99693efeadcc1e5cb11fd3e801`,
PR #94 head `74ac57b4e7cbd70519eff5d39f04accd60cc5722` and PR #95 head
`485806ce6fb9fd10957e634295e725e7f84a5291`. Exact archive SHA256 is
`77203e133e339d8f5ff2e8d3fa2a82506d98a77bb0950c5bef0097d353f95f92`.
Three added research derivations have recorded hashes and read-only evidence
boundaries. No original client/probe code is executed by this adoption.

Six normative documents change in the unchanged 68-document inventory. Reindexing
retains 7625 exact source/heading/content reviews, moves 2275 line references and
reviews 142 added/replaced blocks, yielding 7767. No implemented source claim is
removed. New explicit cross-cutting policies still require their own implementation
and independent tests; prior green tests do not establish new response bounds,
outline origin placement or complete preprocessing.

B90/B91/B93 are resolved in the documentation, separately from implementation.
PR #92 defines conditional ALPHA/WS classes, bare/dollar/template operands,
independent local escaping, exact synthesized provider input, connector-space
boundaries and safe 255-byte decoded/provider limits. PR #94 selects a design-origin
baseline anchor, signed ordinary-total justification and truncation of center
halves; it does not reinterpret tentative RFF alignment fields. PR #95 caps stored
definitions and response entry/per-replacement working text at 4096 bytes, removes
discarded suffix effects, preserves a trailing outer backslash and retains exactly
two ordinary passes. These explicit portable policies supplement the existing
contracts without requiring native unsafe behavior.

The conditional source-parser milestone below implements the newly specified
lexing/typed-provider bridge. Complete IF/ELSE branching and actual consumer
providers remain pending, along with the new outline-placement and response-cap
integration. Three focused adoption guards pass; the normal indexer independently
reproduces the source blocks/document hashes. Both review archives are processed
in memory, selected temporary review inputs and build logs are removed, and no
existing issue receives content. No code is published.

## Outer angle worker and portable outline effects adoption

The current pin is fork stable
[`b8c70aabbb1f1c5f8138d5281bfcbe27a3f602c3`](https://github.com/RealDeuce/remote-imaging-protocol/tree/b8c70aabbb1f1c5f8138d5281bfcbe27a3f602c3).
Reviewed open PR #98 head is `38c0809af6469c0019aafea5b772f644df2b329f`;
PR #99 head is `b120bfa52e6172f56033fcc187792219e416f15d`. Both are integrated
in fork stable. Exact archive SHA256 is
`c913830c6b5944b49f7073cab96b21ad56d43790766aa579a5febc0e1d2d99ba`.
Five normative documents change within the same 68-document inventory.
Reindexing retains 7766 exact source/heading/content reviews, moves 488 line
references and reviews 101 added/replaced blocks, yielding 7867 blocks. No
implemented source claim is removed. New obligations remain pending their own
implementation and independent acceptance.

PR #98 specifies exact outer keywords, no implicit trimming, escaped-introducer
and quote-blind delimiter lookahead, matched conditional boundaries, pure syntax
validation before provider eligibility, strict IF/ELSEIF/ELSE state and sixteen
open IF frames. It preserves the distinct operand grammar and eight wrapper
passes. PR #99 requires bold/italic/decorated output while leaving synthetic
ink geometry and overlap grouping to a described renderer profile. Metric B,
logical/cached widths and origins remain independent of effects; Draw Color/W,
independence from unrelated vector attributes and the effected glyph's displaced
shadow remain mandatory. No universal native synthetic pixel oracle is invented.
B96/B97 are resolved as documentation questions; their implementation stays
pending. Existing issue threads receive no content. The archive is read as data,
selected review files are removed after indexing, and no upstream code or original
client/probe is executed. RIP1.54, original fonts and application routing remain
unchanged.

## Source and review evidence

[`reference-review.json`](../src/tests/rip3/reference-review.json) records the
exact archive digest, old pin and local ledger commit, the normative document
identities (initially 65, now 67), issue-derivation identities, changed/new paths, and the
implementation claims carried or reopened. The downloaded archive was extracted
inside an automatically removed temporary directory. No upstream probe code was
executed. The ordinary pinned-source indexer independently reproduced every new
document identity and source block from that extracted tree.

The index retains its normative scope: v3 command/techspec pages, referenced
v2/v1.54 techspecs, the implementation/glossary pages and baseline ANSI reference.
The three added normative documents specify arc consumers, Bezier consumers and
bitmap/mask deltas. Intermediate research leads and probe-progress notes are
provenance, not additional normative requirements; their final derivations are
linked and hashed separately. The upstream backlog maps the 34 resolutions to
their original-artifact evidence and focused PRs.

The initial adoption indexed **7352 source blocks** across **65 documents**; 44 previous
documents changed and none was removed. Exact source/heading/content matches
preserve prior review references despite line-number shifts. Changed source is
new review work, not a silently overwritten old requirement. Git retains the
complete old ledger at local commit `477ce4f`.

## Conformance claims reopened

Of the previous 1000 implemented blocks, 12 concern unchanged JPEG/WAVE format
and MicroANSI container documents and retain their existing independent evidence.
Another 917 exact-text claims occur in changed documents and are reopened as
pending: unchanged wording alone does not prove unchanged semantics. The other
71 implemented blocks were replaced or changed and are part of the new review
queue. Existing implementation paths and tests are retained where the source
block matches. These changes withdraw claims, not implementation code or tests.

At adoption the queue has 12 implemented, 1625 pending, 4799 unreviewed, 875
contextual, 24 excluded and 17 blocked source blocks. The remaining blocked rows
refer to the earlier broad issue #2, which is outside PR #71's explicit closing
list. The 34 adopted issue resolutions are documentation answers; source review,
implementation and independent acceptance tests remain required. Issue #21 has
a separately recorded follow-up, now answered by the reviewed update above.

Important reconciliations identified during adoption:

| Area | Corrected contract and implementation consequence |
| --- | --- |
| JPEG display / #21 | JPEG rows use COPY regardless of vector write mode. The optional clear replaces the fitted rectangle with black; aspect margins retain their old pixels. The current general-mode/background-clear/black-margin kernel must change. |
| IMAGE_STYLE / #21 | Wire `1i` orders corners, accepts flags 0..63 and can change a protected current port. All-zero coordinates do not disable viewing. DELETE is stored without presentation deletion; wallpaper adds bits 16/32. The old draft-based disable tests cannot become new wire expectations. |
| Clipboard / #42 | Ordinary LOAD_ICON failure retains clipboard selection and resources while attempting a placeholder. The current callback that requests clipboard clearing on such failure must be removed or replaced. GET_IMAGE has separate failure behavior. |
| Text cursor / #6, #24 | TEXTXY is a current per-port cursor query with decimal serialization, producer-dependent unit conversions and integer string advances. Fractional layout positions alone are not the query cursor; existing movement/advance tests need reconciliation. |
| Tables, fonts, geometry and queries | The adopted pages provide the previously missing ownership, bootstrap/reset, font, arc/Bezier, polygon, fill, input, backup and query contracts. Existing prerequisite kernels remain available, but prior green tests do not establish the corrected wire/state behavior. |

The inactive-viewport wire IMAGE_STYLE question was posted before the user
requested no further issue content. Its [existing comment](https://github.com/bbs-land/remote-imaging-protocol/issues/21#issuecomment-6935800437)
is answered by the reviewed follow-up above. No additional issue content was
posted. Documentation resolution does not imply that the wire setter, activation
and display integration are already implemented.

## Ordinary text-window update from PR #102

The current pin is fork stable
[`93fd0c3bfac24fda5e76d20b33d782b27ba89e99`](https://github.com/RealDeuce/remote-imaging-protocol/tree/93fd0c3bfac24fda5e76d20b33d782b27ba89e99),
following `b8c70aabbb1f1c5f8138d5281bfcbe27a3f602c3`. It consolidates the
ordinary-window contract reviewed in open
[PR #102](https://github.com/bbs-land/remote-imaging-protocol/pull/102), head
`b23a9433c240d39a8ecd4e772e2774da3c2cb077`, normative commit
`862c98687d5ae5862929a80038f0f05d49e7f8cc`. Exact archive SHA-256:
`7f0bb1a52a616c0fc1228f0715a4089e75be0d5e7adcbf533499b53386df7ad0`.

Only `1.4-terminal-ansi-view.md` changes in the 68-document normative inventory.
The complete reindex matches 7864 existing blocks by heading/content, moves 94
line identities and adds 19 blocks, for 7883 total. Eighteen ordinary-window
implementation claims reopen for reconciliation; no implemented block is
removed. Earlier adoption records and their initial claim accounting remain
preserved; the latest `updates` entry records this movement separately. All 46
previously recorded derivation digests match the archive. The new ordinary-window
derivation is pinned at SHA-256
`bc6635c64b7455f1e849fd4ab4cbed10dd9cd2ffdaeb5b7c758946da959f0db8`.
An independent strict reindex from automatically removed selected-document
extraction reproduces the reviewed ledger exactly.

The contract orders/clamps bounds before identity, supplies an explicit
changed/identical state matrix, preserves existing pixels and separates the
global ANSI parser state from window definition. The font-2 example ends at
row 24. Changed definitions reset cursor/rendition/margins/window modes but
retain tabs, character-set selections and unfinished ANSI syntax. All-zero
deactivation retains geometry/state; identical definitions reactivate without
reset. Optional UTF-8 decoder state is explicitly outside the original protocol.
B100 is resolved as documentation; B101 is unaffected. Geometry normalization
and its connected acceptance are implemented, while the remaining ANSI-state
work stays pending in the [implementation inventory](RIP3_CONFORMANCE.md#pr-102-adoption-and-ordinary-window-bounds).
No original client instructions, native probes or UI execute. RIP1.54, original
font bytes and external-launch exclusion remain unchanged. Issue #100 receives
no more content; future findings reference its PR from new parent issues.

## Stable ff2043e: extended text-window and cursor contract

Adopt fork stable
[`ff2043e344e0c0df09d369bea076e781781345f3`](https://github.com/RealDeuce/remote-imaging-protocol/tree/ff2043e344e0c0df09d369bea076e781781345f3),
including [PR #103](https://github.com/bbs-land/remote-imaging-protocol/pull/103)
head `ed0f290ac4227716c4c158c65b093864f30eccc8`. The downloaded archive is
2,130,213 bytes, SHA-256
`107319bba6b5bb61d767c21596fbe65b95a885c655145b5e3958e114349bc945`.
Three normative documents change; the 68-document inventory is unchanged.

Match source blocks by document, heading and exact text, consuming duplicate
matches in order. Preserve 7864 reviews, including 1478 moved line identities;
19 old blocks are replaced by 43 new blocks, yielding 7907 total. Reopen nine
unchanged g/H metadata claims and remove two changed behavior claims because
PR #103 requires full-window absolute positioning even with ANSI origin mode
and margins. The update record retains their previous implementation/test
evidence. New normative blocks remain pending; reviewed attribution/table
structure is context. No changed behavior is accepted merely because old tests
passed.

The contract now specifies absolute exclusive WORLD/device bounds, loaded MAF
metrics and directed first-fit selection for extended b; validation, one-cell
extension, state reset and flag order; palette-index-0 e/ETW/flag16 erasure;
STYLE-background wire > versus target-ANSI-background TWERASEEOL; full-window
absolute g/H/TWGOTO/TWHOME; and omitted COFF hiding the active cursor. B101 is
resolved as documentation; those runtime requirements remain pending. B104
character-set mapping and B105 reset semantics remain separate blockers.

Pin the new issue-101 derivation and the corrected issue-100 derivation digest.
The latter corrects its .rdata section bias to RVA minus 0x1000 and records
workflow notes; its section-aware ANSI-state conclusions are unchanged. All
46 other previously pinned derivations match. No original-client code executes,
legacy behavior changes or existing issue comments are involved.

## Acceptance

The source guard checks that the pinned revision and complete document inventory
match the adoption record, and normal reindexing retains that record. Focused
tests reject missing/stale adoption records and changed document inventories.
The unchanged RIP1.54 freeze check is still mandatory. Native build/CTest results
for this adoption are recorded in `RIP3_CONFORMANCE.md`; legacy green fixtures are
historical behavior evidence until reconciled with the adopted contract.

### LOAD_BITMAP duplication/wallpaper implementation follow-through

The separate `1b` engine now follows the adopted #42/#58 ordering: initial
paint/palette commit, full requested-rectangle duplicate, then COPY wallpaper
on the live current viewport. Explicit deletion/protection and creation/copy
failures retain their documented nontransactional state effects. Flags0..63,
including stagger32 with wallpaper16, are accepted; higher values reject before
lookup. The earlier draft-era clipped transactional duplicate path was removed
from the painter. Detailed independent coverage and remaining color-key,
coordinate-producer and application-integration limits are recorded in
[`RIP3_CONFORMANCE.md`](RIP3_CONFORMANCE.md#load_bitmap-post-draw-duplication-and-wallpaper-reconciliation).
The reference pin and recorded document/research identities are unchanged.

## ANSI-only character-set update from PR #107

The reviewed fork stable revision is
[`ba97c882b2d5ea13fd5310431f3442fc9cbc9f9f`](https://github.com/RealDeuce/remote-imaging-protocol/tree/ba97c882b2d5ea13fd5310431f3442fc9cbc9f9f),
including open PR [#107](https://github.com/bbs-land/remote-imaging-protocol/pull/107)
head `16164ae796c6d81da4e7488b0d47900dd7c59461`, specification commit `563ee596`.
Archive SHA-256: `2be54ba1d4d3f1524084cccfa707309364b034f7145fbdb53606a27ec0deb931`.

Review covers both changed normative documents and the new read-only original
byte/handler derivation. The inventory remains 68 documents, now 7971 blocks:
7907 exact source/heading matches retain their evidence, 170 references move,
and 64 new units are reviewed separately. All 48 previous derivation hashes are
unchanged; the #104 derivation becomes the 49th. B104 is resolved as documentation,
with runtime acceptance pending. CSI s/u saved coordinates and ESC7/8 full-TW
snapshots are distinct, reopening the earlier shared saved-cursor claim. Existing
successful native ESC8-home tests do not prove the corrected contract.

The user's explicit no-VT-102-mode constraint takes precedence over this PR.
Only applicable ANSI designation/byte/state behavior is in scope; VT-only
mapping tables and vectors are excluded. No mode, selector or keymap is added.
The ANSI-only interpretation, exclusion reasons and adoption provenance are in
the ledger. #105 outer reset and #106 selection remain separate; the new #108
geometry-query conflict is filed against the parent without modifying old issues.

Native Release CMake build and affected query/slot tests pass. Reference-adoption
and source-freeze guards pass; strict reindexing retains the edited ledger
byte-for-byte. No original driver/probe/UI is executed. The owned archive,
extraction and logs are removed. No code is pushed or PR published, and RIP1.54's
baseline, sources and historical assets remain unchanged.

## PR #109/#110: outer SOFT reset and text-window selection

Adopt fork stable `568d8063c68bd009cf687fb3809f547101949727`, consolidating
reviewed PR #109 head `773da350de5198164df4bc3680346f274703aa99` and PR #110
head `368fe26b6b18f557517fed6ee53d46e5da13c709`. The archive SHA-256 is
`b180864fd09bde670d871a1e8b8a43596699e180db14728ae21d0c964d561e26`.
Only four of the 68 normative documents change. Match 7963 unchanged blocks
by source/heading/text digest, relocate 301, remove eight superseded blocks
and review 71 new blocks, yielding 8034 units. No prior implemented block is
removed. Verify all 49 previous derivation hashes and add the two new notes,
without executing foreign code, probes or client/UI instructions.

B105's reset answer replaces the blanket zero-selection list with per-family
selection/protection and lifetime rules. Physical clear is RGB black before
palette reset; PAL current remains selected, even if undefined afterward.
Protected live records, old-current ANSI/image/ENV effects, independent backups,
variables/templates, input lock and audio have explicit separate policies.
Reset/header orchestration remains local implementation work; `*` is not claimed.

B106's answer distinguishes wire first-use cloning from CUR's fresh full-screen
defaults. Protected sources do not protect new implicit wire destinations;
existing protected entries remain selectable. Partial ordinary ANSI syntax
belongs to the connection and completes in the new target, without copying
cells, pixels or history. The connected implementation now covers those paths
for represented fields. Charset designation, resident queries and the larger
interpreter-owned ESC7/8 snapshot remain pending. The user's no-VT-102-mode
constraint takes precedence over every retained client-mode reference.

Native Release CMake build and 13 distinct relevant CTests pass, including
CP437/UTF-8 connected selection/query suites, parser/host services, source freeze
and adoption guards. Strict reindexing preserves the reviewed ledger exactly.
The ledger is 1394 implemented, 1645 pending, 3578 unreviewed, 1317 context,
67 excluded and 33 blocked; source indexing is not a full compliance claim.
Temporary owned artifacts are cleaned, original RIP1.54 remains frozen, and
no existing issue, code push or PR publication is performed.

## PR #111/#114/#115: geometry, style state and mouse identities

Adopt fork stable `ebd81bcbb29eeca2d2c283d40221cbabb3cee531`, consolidating
reviewed actual heads `153e5a333533b09367421c3f7be988325e41d243` (PR #111),
`ff10494927f32cbbdaaa0161fd7801998012214a` (PR #114) and
`9f398ede336649d2a6f655d85b6b4ef262322ae8` (PR #115). Archive SHA-256:
`1b609b0c1cdf022142c854b81cedda845cd3e54ad3ceb2b10ba0bff398502af7`.
Six of the same 68 normative documents change. Preserve 7909 exact
source/heading/text matches, relocate 1664, and review 162 new blocks, yielding
8071 units. Record the retired text and its 63 previous implementation claims;
reconcile replacement claims against the new runtime expectations. All 51
previous derivation hashes remain unchanged; pin the three new derivations.

PR #111 explicitly reconciles intended geometry-query grammar rather than
copying the obsolete decoder's acceptance errors. Explicit TEXT corner queries
default to WORLD, position queries never accept COLS/ROWS, ordinary legacy far
cells are inclusive and graphical far edges exclusive. PR #114 defines successful
1B writes into unused current records, first-use clearing of stale protection
and raw-current versus numeric ISPROT gates. PR #115 supplies shared gesture
ownership, exact one-prefix ASCII identity grammar, callback-scoped FIELDID and
separate hover/mutation policy. Documentation resolutions are recorded for
B108/B112/B113; runtime acceptance remains clause-specific. Hover, restored
mouse tables, nested playback and incomplete geometry edges remain pending.

Connected tests now execute the literal ordinary/extended geometry tables,
undefined-style redefine/reselect and stale-protection sequences, identity
boundaries, self-deletion and field-program isolation. Native session fault
coverage includes the button-style/mouse constructors and their independent
backup. Strict source reindexing preserves the edited ledger byte-for-byte;
the unchanged RIP1 freeze and reference-adoption guards remain mandatory.
No original driver, probe or UI executes, and no old issue receives comments.

## PR #117: positional button payload and portable hit geometry

Adopt fork stable `1b8ab9928a9fd9071556cf0228ea69a496db3e72`, containing
reviewed PR #117 head `8b76c83c0980b44ed0c8aba6c96a0f9113ef9987`.
Archive SHA-256:
`782a19012ccd898084fbafeb3a4e52cf08821b9dcf5eef9212d849ab2fdd7e75`.
Three of the same 68 normative documents change. Preserve 8039 exact source/
heading/text matches, relocate 513, retire 32 and review 60 new source blocks,
yielding 8099 units. No prior implemented claim is removed. Verify the 53
unchanged previous derivations, record the old/new digest for #112's explicit
label correction, and pin the new #116 derivation, giving 55 notes.

PR #117 answers B116 with fixed icon/label/host positions, the first two literal
`<>` delimiters, one ID scan before leading ASCII trim, source byte limits and
opaque stored host source. Validation belongs to the stages reached at
activation; no speculative raw-host/provider pass is permitted. The corrected
`<>Style B` example keeps #112's numeric/state proof and now tests actual label
rendering. `<><><>` has literal host `<>`.

Its explicit portable input policy uses positive half-open stored rectangles
for fields/buttons. Dynamic plain corners normalize before conversion and
zero device area skips before effects. Effective destination is PORT0 for mouse
geometry, current PORT for decorations. Empty/inactive/clipped or touching
mouse definitions consume no slot and paint nothing. Existing field geometry
is a creation snapshot and survives later viewport movement/deactivation. This
policy does not claim the obsolete driver's near-edge tolerance or inclusive
hover parity; hover dispatch itself is still pending locally.

Plain buttons are connected to the session owner and tested over TCP in both
encodings; shared capacity, reset/context lifetime, opaque malformed actions,
clipped suffixes, raw limits and geometry receive independent expected results.
Image/clipboard faces, grouping, hotkeys and graphical macros remain pending.
No original driver/client/probe executes and no issue receives further comments.
The RIP1.54 freeze and the user's exclusion of a VT-102 mode remain authoritative.

## PR #121: button keyboard activation

Adopt fork stable `915ca0f3292d6ce4a74c98091cd15e8447251cef`, reviewed against
PR #121 actual head `073317e7f3c11b41577e5c62ebff9490cce9f6dd`.
Archive SHA-256:
`dc7b064bd75e9aec514e5b66419af95f1cf274847d89b537d4ae3ef65bd1feea`.
Two of the same 68 normative documents change. All 8099 previous source blocks
match exactly; 191 relocate and 32 new blocks produce 8131 units. No old block
or implemented claim is removed. All 55 previous derivation digests match;
the new #118 note gives 56 notes. No original client/probe code executes.

The reference labels its keyboard profile as portable completion, separating it
from original input-only uppercasing/reverse-scan evidence. It defines CP437 byte
candidates, symmetric ASCII folding, zero unbound, exact high bytes, byte-only
wildcard, explicit/default-CR/wildcard classes and newest ties. Commitment is
immediate per delivered translated event; key-up/raw physical notifications do
not create extra activations. Matching empty/failing actions consume input.
Keyboard action snapshots cancel pointer capture without synthesizing pointer
coordinates/buttons and preserve FIELDID through reset/deletion/yields.

B118's documentation answer is adopted. Connected native ownership and terminal
input exercise the literal seven-button vectors, independent input gates,
priority/deletion, repeated events, high bytes, noncharacter forwarding,
self-deletion, reset and delayed FIELDID. HEADER input-lock integration, complete
mouse-query/table-restore/playback families, native indicators and unavailable
button face/group modes remain separate pending clauses. No new emulation mode
or change to the RIP1.54 freeze is authorized. Existing issues receive no comments.


## PRs #122 and #123: image geometry and selective cleanup

Adopt fork stable `eb346cb73cf01f66ba88c8fa0dc2a134756a2302`, containing
PR #122 actual head `4a7f8d69701b653fea6296cc727f377c8d357d4f` and PR #123
actual head `8852ffa3c6f786f65aac54f6326838913738f38c`. Both source heads are
ancestors of stable (behind zero). Archive SHA-256:
`6187979304b66c6f565f7fa8157c5fb3d0da46ec556a9d2f84dd872c2abd6f66`.
Two of the same 68 normative documents change. Preserve 8105 exact source/
heading/text matches, relocate 93, retire 26 and review 80 new units, giving
8185 source blocks. Retain the six retired implementation claims and evidence
in adoption history, reopening their replacements until tested. All 56 previous
derivation hashes match; the #119/#120 notes give 58. No original code executes.

[PR #122](https://github.com/bbs-land/remote-imaging-protocol/pull/122) answers
B119: style sizes apply only to PLAIN; clipboard faces use the whole native
source viewport, with inactive/protected reads and palette-index-0 padding.
ICON dimensions scale through captured creation WORLD; the first chosen image
fixes later normal/hot geometry. The clipboard correction is connected and tested.
ICON/hot rendering remains pending, with those complete clauses left open in the
runtime ledger. Internal inclusive viewport rectangles adapt the reference's
half-open stored extents without changing the wire viewport endpoint rule.

[PR #123](https://github.com/bbs-land/remote-imaging-protocol/pull/123) answers
B120: contained/crossing/outside are disjoint; selector corners normalize in
current WORLD and convert to absolute DEVICE coordinates, without a viewport
origin. Empty selectors do nothing; unsupported flag bits reject before mutation.
The whole selection is deleted before per-field newest-first paint, or one
selector-interior paint with 128 regardless of match count/type exclusions.
64 wins 32; cleanup uses physical-screen COPY, index 0 or opaque fill/background
at absolute device pattern phase. Paint failures do not restore fields, suppress
remaining paints or steal a survivor's capture. This explicit portable completion
replaces the earlier provisional overlap/origin/paint-rejection behavior.

Native tests cover all 256 flag combinations, callback ordering/failure, capture
identity, source eligibility/padding and nonfatal wire recovery. Connected CP437/
UTF-8 tests cover four entry rasters, palette remapping, current/offscreen/inactive
viewport independence and both feedback planes. Strict reference reindex retains
the reviewed ledger byte for byte. RIP1.54's freeze remains unchanged. No VT-102
mode is added; existing upstream issues receive no comments.


The subsequent ICON/HOT implementation milestone uses this same pin and the
unchanged #27/#119 derivations. Literal ICON/HOT creation, captured geometry,
paired fallback, field-owned redraw and pre-label auto-stamp are now connected
and tested in both encodings on four rasters. The implementation ledger records
that follow-up separately from documentary adoption. Image/label macro expansion,
grouped selection and other unfinished families remain pending; the earlier
adoption-time ICON/HOT limitation is superseded by this runtime milestone.

## PRs #126 and #127: graphical consumers and canonical cursors

Adopt fork stable `a8570522e02f4b6753afea9cb9a6de3565009f1a`, containing
reviewed PR #126 head `c76013469772e7f5e4ea5fca0255e761f9952ffe` and
PR #127 head `635d3f095360cf66e545a54ffed57ddd798bbc6a`. Archive SHA-256:
`a655f4c3e9a778a78767f9223b9fe825c3f6c63d41744cdbb6aa3fc211c8bd0e`.
Both actual PR heads are ancestors of this selected stable revision; the PRs
remain open upstream. Six of the same 68 normative documents change. Preserve
8170 exact source/heading/text blocks, relocate 2288, retire 15 and review
80 new blocks, yielding 8250 units. Retired implementation evidence is retained
in the machine-readable adoption history; replacement cursor claims are tied
to this milestone's independent tests. Verify all 58 prior derivation hashes
and add the two #124/#125 notes, for 60 pinned derivations.

PR #126 resolves documentation blocker B124. Filename/label/T/@ and metric
have explicit angle and one-pass ordinary stages, the complete 84-name callback
gate, per-stage prefix/NUL/escaping rules, redraw timing and suspended prompt
ownership. This adoption does not mark those unfinished rendering consumers
implemented. The already connected positional `1U` splitter remains unchanged:
only the first two literal delimiters split the raw payload, and the host
remainder stays opaque during rendering.

PR #127 resolves B125 and defines one shared 0..7 mapping. Value 5 is a raised
palm; value 7 is a visible prohibited symbol, with default-arrow fallback while
retaining the requested logical value. Wire validation now accepts 7. Native
adapters and connected tests use that contract, including held field capture,
link-hover restoration and a failing backend. Windows/macOS artwork is selected
in source and left to native CI; only the native POSIX configuration is run
locally. No original executable or downloaded code is executed. No comments
are added to existing issues, and no upstream state is modified.

The resulting queue has 1664 implemented, 1775 pending, 3365 unreviewed,
1354 context, 67 excluded and 25 blocked source units. The RIP1.54 baseline,
entry-selected raster and explicit absence of a new VT-102 mode remain intact.
