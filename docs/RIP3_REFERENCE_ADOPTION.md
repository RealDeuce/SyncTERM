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

## PR #129: auto-stamp redraw and destination lifetime

Adopt fork stable `21728db6fd3ff7623e36d5ff59e8c64421f57c14`, containing
reviewed [PR #129](https://github.com/bbs-land/remote-imaging-protocol/pull/129)
head `b80b12ff7b3f32b1897e9d93bc11089938232ffc`. The actual PR head is an
ancestor of this selected stable revision, while the PR remains open upstream.
Archive SHA-256:
`e5b0224c6f32590111c647c14f245c8a57ddd33a6e79fc986db998820fc2d77f`.
Two of the same 68 normative documents change. Preserve 8247 exact
source/heading/text blocks, relocate 78, retire three and review 32 new blocks,
yielding 8279 units. Retain the retired implemented geometry paragraph's old
evidence in the adoption history; its replacement is assessed against this
milestone's independent geometry and connected redraw tests.

Verify the unchanged hashes of 59 prior derivations. The #27 note adds only its
explicit documentary follow-up, recorded with old/new digests; add the #128
source/policy note for 61 pinned derivations. Archive contents are read as data;
no original executable, probe or downloaded code is executed. The source split
contract from [PR #117](https://github.com/bbs-land/remote-imaging-protocol/pull/117)
is unchanged: after one identity scan and whole-payload leading whitespace
removal, only the first two literal `<>` pairs split filename/label/host. Quotes
and backslashes do not escape that split; inserted expansion output does not
resplit it; the remaining host source stays opaque during rendering.

PR #129 resolves B128. It specifies separate original face F, fixed hit H and
baked capture C; exact logical crop and COPY padding; retained chisel label
inset; filename/source/label stages on full redraw; provider-free feedback undo;
and the primary same-effective-destination ignore guard. A helper's PORT0
fallback never authorizes transferring a stale label continuation. The guard
supersedes earlier recorded same-target capture/fallback behavior for button
auto-stamp only. Ordinary GET_IMAGE retains its existing helper contract.

The accompanying connected runtime milestone implements those button stages,
prompt lifetime and geometry. The resulting queue is 1702 implemented,
1760 pending, 3365 unreviewed, 1360 context, 67 excluded and 25 blocked units.
TEXT_METRIC/TEXTDATA, grouped/persistent selection and other recorded unfinished
families remain pending. No comments are added to existing issues. RIP1.54,
entry-selected resolution and the exclusion of a new VT-102 mode remain intact.

## PR #133: real-font metrics and retained TEXTDATA

Adopt fork stable `9d666f8468036e5517f2f1395790b52bf6810c28`, containing
reviewed [PR #133](https://github.com/bbs-land/remote-imaging-protocol/pull/133)
head `078874549dd1df52cc1481fad6d22fb8d082fb74`. The actual open PR head is an
ancestor of this stable commit (one commit ahead, none behind). Archive SHA-256:
`fd771166381c5726458a4bde521c25912f61b663d078e220ed217d5ee7fad8f8`.
Three of the same 68 normative documents change. Preserve 8277 exact
source/heading/text blocks, relocate 2125, retire two and review 47 new blocks,
yielding 8324 units. The two retired implemented claims retain their complete
prior evidence in the adoption history. All 61 earlier derivation hashes are
unchanged; add the #130 research note for 62 pinned derivations. Archive contents
are read as data; no original executable or downloaded probe/code is run.

B130 is answered as documentation. The contract supplies real-font width and
height axes, CHR rational truncation, bitmap 80×43 measurement geometry,
unrotated global outline Hcache in the atomic shared snapshot, cold/warm
fallback, post-expansion frame selection and retained r/@ sentinel lifetime.
The connected implementation milestone records acceptance separately. Current
source splitting for `1U` remains the already adopted PR117 positional rule.
No existing upstream issue receives comments.

The implementation queue now contains 1783 implemented, 1736 pending, 3323
unreviewed, 1379 context, 67 excluded and 36 blocked units. Mixed clauses that
also require HEADER or SAVEALL/RESTOREALL remain pending. B131/B132 and the
other unfinished families are unchanged. RIP1.54's original freeze, selected
entry raster and the exclusion of a new VT-102 mode remain intact.

## PRs #134 and #135: grouped ownership and hover lifecycle

Adopt fork stable `49a69454a0bd6055c0d11bb415759d309519de33`, containing
actual reviewed PR #134 head `6797f9beabb90cd317c5414755b50a00f01a415d`
and PR #135 head `04ef07e25e8d0551cb997dda4b7f6d917bbdda9b`. Both are
ancestors of this selected stable revision (10 and 2 commits ahead, none behind).
The PRs remain open upstream. Archive SHA-256:
`b38c508c2370437675e089a349d8807e22ca2dad21db1049fa6765b878cb3a83`.
Six of the same 68 normative documents change. Preserve 8301 exact
source/heading/text blocks, relocate 676, retire 23 and review 109 new blocks,
yielding 8410 units. The two retired implemented claims retain their previous
evidence in the adoption history. Verify all 62 previous derivations: 61 retain
their hashes; #130 adds only its final consolidation checkpoint, recorded with
old/new digests. Add the #131/#132 notes for 64 pinned derivations.

B131 and B132 are answered as documentation. PR #134 specifies persistent
checkbox/radio state, preflight membership conflicts, owned committed actions,
associated definitions/publications/baselines, required radio selection and
provider-free composition/deletion. It preserves effective template values
through full SOFT while releasing their field owners. These grouped runtime
obligations remain pending; adoption does not present them as working buttons.

PR #135 specifies exact definition/C-string/1023-byte edges, handler-independent
membership, current-handler selection per callback, owned suspended source and
FIELDID, and separate deletion/focus/full-reset/backup lifetimes. Its live hover
runtime is connected in this milestone. Mouse-family save/restore, HEADER and
aggregate operations remain pending with their owning subsystems. No downloaded
code, original client or source probe is executed; no existing upstream issue
receives comments.

The implementation queue now has 1811 implemented, 1794 pending, 3316 unreviewed,
1397 context, 67 excluded and 25 blocked units. RIP1.54's frozen source baseline,
entry-selected raster and explicit exclusion of a new VT-102 mode are unchanged.

## PRs #137 and #139: grouped backups and corrected index summaries

Adopt reviewed [PR #137](https://github.com/bbs-land/remote-imaging-protocol/pull/137)
head `7f074ac5fd569938a2368d2948a0a78931bf58f0` through fork stable
`3313754a814641befacc434ca5bd4af59402b3fb` (two commits ahead, none behind).
Archive SHA-256:
`9ced3bf488a1f20956bac316845b5e1f443c4cd0836e65d271fedf7ad06d4ac3`.
Four of the same 68 normative documents change. Preserve 8403 exact
source/heading/text blocks, relocate 42, retire seven and review 44 new blocks,
yielding 8447 units. Retain all seven retired source units and the four retired
implemented claims with their previous evidence in the adoption history.
All 64 previous derivation hashes remain unchanged; add the #136 note for 65.
B136 is answered as documentation, then independently accepted through the
connected grouped-MOUSE implementation described in the conformance inventory.

PR137 explicitly completes the portable per-owner/per-slot model beyond the
original one-body serializer. Capture logical persistent selection and full
A[N] maps; exclude global B[N]/publishers, temporary interaction and feedback
caches. Restore with fresh tokens while retaining current ordinary assignments
and surviving same-kind/number group publications. Pure rebuilds run no
providers or painting. The already adopted manager/error/graphics rules stay
controlling; aggregate orchestration is assessed separately.

Review [PR #139](https://github.com/bbs-land/remote-imaging-protocol/pull/139)
head `bbccbcb42c7e5dea96f5d52fd606a8bffc37b99b`, initially before consolidation,
then pin selected fork stable `95493d2643eaecfc1a02ca4264da2c0d8a1f406e`.
The actual reviewed PR head is ancestral (two commits ahead, none behind), and
all normative hashes match it. Archive SHA-256:
`5baa384e45eb5baf1fe7cd571291fbe9c9d3557b106e478b663fcca4451828be`.
Only the variable index changes among the same 68 normative documents. Preserve
8440 exact source/heading/text blocks, relocate 44, retire seven and review nine
new blocks, yielding 8449 units. No retired implemented claims occur in this
second adoption. All 65 previous derivation hashes remain unchanged; add the
#138 note for 66. Stable consolidation updates only workflow metadata and the
new #138 research checkpoint, not the reviewed normative text.

PR139 answers issue #138 editorially: screen-only capture, current palette
binding, all twenty aliases and the correct eight-family aggregate order now
agree with the detailed accepted contract. The old SyncTERM RESTORE0 omission
is explicitly historical. No new screen runtime behavior or repeated screen
verification is required for this index correction. Full aggregate execution
remains pending. Archive contents are read as data; no downloaded code or
original client/probe is executed. No content is added to covered issue threads.

After the grouped implementation, the queue has 1954 implemented, 1752 pending,
3224 unreviewed, 1427 context, 67 excluded and 25 blocked units. Mixed aggregate,
HEADER and host-template-clearing clauses remain pending. RIP1.54's original
freeze, selected entry raster and the exclusion of a new VT-102 mode remain
unchanged.


## PR #141: full PORT-family clipboard entry reconciliation

Adopt reviewed [PR #141](https://github.com/bbs-land/remote-imaging-protocol/pull/141)
head `fc574bab8fb3cf5948a231615eaab28c724d0354` through selected fork stable
`73dfe86c6601990e931237984712197c354999fd`. The actual head is ancestral,
two commits ahead and none behind, with no subsequent normative changes.
Archive SHA-256:
`07b36afa4b63cc850ba0307a60d935544af34611d84c02caa37c3873f22c202b`.
Three of the same 68 normative documents change. Preserve 8435 exact
source/heading/text matches, relocate 425, retire 14 and review 14 replacement
blocks. The index still contains 8449 units. All 14 retired units, including
six implemented claims, retain their former evidence in adoption history.
All 66 existing derivation hashes are unchanged; add the #140 derivation for 67.
The complete archive's normative path set and original ALPHA4 text hash are
checked; downloaded files remain data and no upstream code/instructions run.

B140 is resolved. SCB/RCB explicitly address the whole PORT family, retain valid
snapshots with empty assigned pointers and never imply SCREEN restore or PCB
paste. RCB Format is corrected, per-screen-port queries are listed, and inherited
clipboard-only deletion language is historical context. Existing connected code
already follows these rules. Exact full-family/illustrative-example units remain
pending with complete PORT record/PCB acceptance; syntax, pointer and lifetime
clauses use prior independent native/connected evidence. No unchanged product
build/rendering suite is repeated for this editorial pin update. New contract
and RIP154 preservation checks pass. B142's separate SENV-list report remains
open; no existing issue receives comments and no source is pushed.

The queue has 2055 implemented, 1715 pending, 3121 unreviewed, 1465 context,
67 excluded and 26 blocked units. The original RIP1.54 freeze, arbitrary entry
raster and no-VT102/external-launch exclusions remain authoritative.


## PR #143/#145: ENV scope correction and remembered PORT copy pairs

Adopt reviewed [PR #143](https://github.com/bbs-land/remote-imaging-protocol/pull/143)
head `e83facab5d4198b1603d86d3918915cad4cd81fb` and
[PR #145](https://github.com/bbs-land/remote-imaging-protocol/pull/145) head
`1e9d95d4fa80c59dca9002b50f36ae689b3d2ac3` through selected fork stable
`f14e410915f11f64d2287eec82ebf4770c3baf41`. Both actual heads are ancestral:
seven commits ahead/none behind for PR143, two ahead/none behind for PR145.
The later commits alter workflow/research checkpoint material, not the reviewed
normative contract. Archive SHA-256:
`8f715b0b0b9e9cc1814f08a069e6ce40dc5bf16e5e51f73df8e33a800b0088cf`.
Seven of the same 68 normative documents change. Preserve 8428 exact
source/heading/text matches, relocate 866, retire 21 and review 103 new units,
yielding 8531. All retired rows, including three implemented claims, retain
complete former evidence in adoption history. All 67 previous derivation hashes
remain unchanged; add the #142/#144 derivations for 69. The complete normative
path set and original ALPHA4 hash are unchanged. Downloaded files are read as
data; no upstream instructions, code, client or probes are executed.

B142 is answered: SENV/RENV's foreign-selection list now agrees with ENV-only
ownership and the independently verified connected implementation. Existing
runtime evidence is reused for those editorial corrections. B144 is answered
by an explicitly completed portable remembered-pair contract, distinguished from
the earlier source observations: final DEVICE offsets, named producers, COPY
reversal, overlap snapshots, optional state and lifecycle/backup rules. Those
new runtime requirements remain pending; neither existing pixel transfers nor
PORT snapshots establish PCB completion.

The adoption queue has 2080 implemented, 1782 pending, 3091 unreviewed,
1486 context, 67 excluded and 25 blocked units. Subsequent implementation and
new issue146 bookkeeping are recorded separately. No covered issue receives
comments; the original RIP1.54 freeze, arbitrary entry raster and no-VT102/
external-launch exclusions remain authoritative.


## PR #147: logical remembered-pair boundary before physical raster access

Adopt reviewed [PR #147](https://github.com/bbs-land/remote-imaging-protocol/pull/147)
head `55ba94bdcc2e7b852bc62ffa409cd69aa65d4a3c` through selected fork stable
`4c8672d935e9faccdb74eb702ab7c056cf24071e`. The actual head is ancestral,
four commits ahead and none behind; subsequent changes affect workflow/research
checkpoints without normative edits. Archive SHA-256:
`409d29dd241618dfffd72035217bf91cb0125b1b0161310b5e13a5362326eb6d`.
Three of the same 68 normative documents change. Preserve 8520 exact
source/heading/text matches, relocate 68, retire 11 and review 32 new units,
yielding 8552. No implemented claims are retired; all retired rows and their
former evidence remain in adoption history. Of 69 existing derivations, only
#144 changes (explicitly linking the completed physical boundary); add #146
for 70. The full normative path set and original ALPHA4 byte hash are unchanged.
Archive contents remain data: no downloaded instructions, research checks,
original client, native probe or renderer are executed.

B146 is answered. The pair stores final logical compositor rectangles after
protocol viewport clipping, before physical storage/sample clipping. Preserve
their post-viewport mapping without physical refitting or rounding fractional
edges into the pair. A successful eligible nonempty logical transfer publishes
even with zero physical writes; logical no-ops and actual underlying failures
retain old metadata on a surviving definition. This qualifies all named
producers, PCB and backups without selecting a sampler or adding coverage state.
Existing copied pixels alone do not establish runtime pair publication or PCB;
the new/changed clauses remain pending implementation.

At adoption the queue has 2104 implemented, 1780 pending, 3083 unreviewed,
1493 context, 67 excluded and 25 blocked units. New B148's independent live-TW
reset report subsequently moves six unreviewed units to blocked, yielding
3077 unreviewed and 31 blocked; the other counts stay unchanged. Its issue
references covered reports without commenting on them. The original RIP1.54
freeze, arbitrary entry raster and no-VT102/external-launch exclusions remain
controlling. This is a reference-only change; unchanged runtime verification
is reused and the contract/preservation guards pass.


## PR149 live text-window reset completion

The selected fork stable revision advances to
[`393c5ebee54687f329c85d8b17db641e4210a18a`](https://github.com/RealDeuce/remote-imaging-protocol/tree/393c5ebee54687f329c85d8b17db641e4210a18a),
including reviewed [PR149](https://github.com/bbs-land/remote-imaging-protocol/pull/149)
head `174853b8aad354448f88335cdfb900e2469f3a8b`. The exact compare from the
previous pin is five commits ahead and none behind. The archive SHA-256 is
`1dd4fedad69ab99d0730c7cb1deddc22cf4d90418f2831a9eb196090ad863370`.

Five normative documents change in the same 68-document scope. The complete
reindex retains 8550 identical source blocks, moving 1009 line references; 110
heading-only moves preserve exact operative text and evidence, including
repeated RESET table rows matched by source order and section displacement.
There are 58 new blocks and 2 retired blocks, both fully retained in the adoption
record; no implemented claim is retired. The 8608-unit queue now contains 2190
implemented, 1747 pending, 3076 unreviewed, 1503 context, 67 excluded and 25 blocked.
All 70 previous research hashes and the original ALPHA4 text hash are unchanged;
the new #148 derivation is separately pinned. Upstream workflow text remains
reference data and supplies no instructions to this agent. No downloaded code,
original client, probe or upstream checker is executed.

B148 is resolved as a documentation question. The explicit portable profile
forces fresh ordinary user-default-font state for TW 0 without the ordinary-w
identity shortcut, deletes applied entries' fixed queries while retaining the
floating query, keeps a protected current nonzero TW selected through TBL/ALL,
and resolves CUR left to right after complete syntax validation. Per-target
resource failures retain that target and continue later valid operations;
ALL still deletes independent backups. Connection parsing, the larger ESC 7/8
snapshot and the entry raster remain separate. This does not authorize a
VT-102 mode or change the original RIP1.54 implementation.

New and reopened live-reset clauses remain pending runtime acceptance. The
contract and original preservation guards pass; unchanged native builds and
functional/rendering evidence are reused for this reference-only change. The
owned archive is removed. No existing issue receives comments and no code is
pushed or PR published.


## PR151 ANSI ESC snapshot completion

The selected fork stable reference advances to
[`26128c7043d90cd00aa95c6b44aadbfc185f6a3a`](https://github.com/RealDeuce/remote-imaging-protocol/tree/26128c7043d90cd00aa95c6b44aadbfc185f6a3a),
including reviewed [PR151](https://github.com/bbs-land/remote-imaging-protocol/pull/151)
head `685c3d5daca00837dcbef83e2ad40261e748e95b`. Stable is two commits ahead of
that head and none behind; the later commits change research/workflow checkpoint
text only. Archive SHA-256:
`6ab4b850ad5ee2e3ebc38878181bdbfba2cd7330c15ece9df384eb5c40b171ee`.

Five normative documents change in the same 68-document scope. Ordered exact-text
matching preserves 8604 source reviews and moves 409 line references. There are
71 new blocks and four retired blocks; all retired text/reasons are preserved,
and no implemented claim is retired. The 8675-unit queue contains 2261 implemented,
1740 pending, 3062 unreviewed, 1520 context, 67 excluded and 25 blocked. Seventy
previous research documents are unchanged. The existing #104 saved-state paragraph
only gains the declared #150 cross-reference and live-to-CSI overwrite explanation;
its old/new hashes and exact comparison are recorded. The new #150 derivation is
separately pinned, bringing the research inventory to 72. Original ALPHA4 text
bytes retain their recorded hash.

B150 is resolved as a documentation question. The explicit profile owns one
optional snapshot across selection, applies it only to an eligible current text
window, keeps source CSI-saved coordinates unchanged during capture, and derives
the snapshot's saved-coordinate value from the captured live cursor. Restore is
reusable and prepares logical state/query/resources before commit without repaint.
The payload excludes source/current selection, floating query, family backups,
parser/input buffers, pixels/history and native font/cache handles or resource
ownership. Logical font/metric and any already exposed binding requirements must
resolve against current compatible resources. Hard reset/session boundaries clear
the value; ordinary/extended definitions, selection/COPY/live reset, whole text-
window backups, ESC c and SOFT retain it. Mixed HEADER lifetime clauses remain
pending with HEADER integration.

This remains a reference-only acceptance: the full ANSI ESC7/ESC8 snapshot is not
yet implemented. Recognition stays in CTerm's existing ECMA-48 parser; applying
text-window definition/query state belongs to the existing window owner. Runtime
must unwind parsing before replacing its active native owner, then continue input
without a parse error escaping to the session. No VT-102 mode, second ANSI parser,
scene/damage layer or frozen RIP1.54 change is authorized. No downloaded code,
original client or upstream probe/checker runs; embedded workflow text is data,
not instructions. Contract/preservation guards pass, unchanged product build and
runtime evidence are reused, and the owned archive/review scratch files are
removed. No covered issue receives comments and no code is pushed or PR published.

## PR #154 text-window geometry and erase

The selected fork stable revision is now
[`04d1637b6e6af424a7e8e7b5ac43de9669112c5b`](https://github.com/RealDeuce/remote-imaging-protocol/tree/04d1637b6e6af424a7e8e7b5ac43de9669112c5b).
The reviewed [PR #154](https://github.com/bbs-land/remote-imaging-protocol/pull/154)
head `48171adcdc5b8c2ff24a244480266b1b0a3c6bb4` is ancestral, with two subsequent
commits and no subsequent normative edits. The SHA-256 of the local Git tar
archive for that exact revision is
`67ad035ee09ad1f4f0dcaa8cf3958deaa090ba11107ebba55c47c23b6d56386a`.

Three of the same 68 normative documents change. Ordered exact-text matching
carries 8671 source blocks, moving 1565 line references. It retires four blocks,
records their former classifications, and adds 48 for 8719 total. The previously
blocked flag-4 row retains its original wording and is reopened under the completed
profile. All 72 previous derivation hashes and the original ALPHA4 text and
license bytes are unchanged. The new #152 derivation is separately hashed.
The archive was processed in memory.

B152 is resolved by the completed type-3 profile. Geometry uses the target's
retained font and window kind, nominal grid and current cell metrics. It homes
the addressed cursor only and does not paint. Erase uses the current graphics
palette's index 0 over the full stored bounding rectangle, including margins
and independent of graphics port and viewport state. Caret operations have an
independent active-state gate. The [connected implementation](RIP3_CONFORMANCE.md#connected-header-text-window-geometry-and-erase)
records runtime evidence and the deferred startup font policy. Issue #153
remains separate. Existing upstream issue threads were left as filed.

## PR #155 port bounds reset and black fill

The selected fork stable revision is now
[`9a9f7c5eb8c13957b6a7e74af37041c047e3c4d6`](https://github.com/RealDeuce/remote-imaging-protocol/tree/9a9f7c5eb8c13957b6a7e74af37041c047e3c4d6).
Its tree is identical to the reviewed [PR #155](https://github.com/bbs-land/remote-imaging-protocol/pull/155)
head `a863f45b80fd1fd01fcfe9f38f7ab70c8d0bb555`. The SHA-256 of the local Git tar
archive for the pinned revision is `187db415b8f21fd7c43351365eff5a3330ecd720449a82a7c48c4877c06c8527`.
The archive was processed in memory.

Four of the same 68 normative documents change. Ordered exact-text matching
carries 8716 source blocks, moving 1070 line references. The review records three
retired blocks and adds 32 for 8748 total. Of the 73 previous derivations, the
earlier scene-header record only gains the two reviewed cross-references to the
completed port and text-window profiles. The new #153 derivation is separately
hashed. The original ALPHA4 text and license bytes retain their hashes.

B153 is resolved. The completed profile distinguishes full-bounds viewport
geometry reset for active unprotected ports from opaque RGB-black fill over
active ports' physical bounds, including protected ports. The fill retains
the viewport for later drawing. The [connected implementation](RIP3_CONFORMANCE.md#connected-header-port-black-fill)
records runtime evidence. Existing upstream issue threads were left as filed.

## PR #157 PORT 0 initializer

The selected fork stable revision is now
[`266a26951d211a8f9069cefa16a95da7d33b4e41`](https://github.com/RealDeuce/remote-imaging-protocol/tree/266a26951d211a8f9069cefa16a95da7d33b4e41).
It incorporates reviewed [PR #157](https://github.com/bbs-land/remote-imaging-protocol/pull/157)
head `dafe5d54a065b31a4d01726ec1e1e0f9ab7c0ffa`. The two subsequent commits add
research delivery and workflow notes. The SHA-256 of the pinned local Git tar
archive is `20c028d7652bfc9cabd3d6828f8e9d46315a6a5757bbc00139b967bb5b431334`.
The archive was processed in memory.

Four of the same 68 normative documents change. Ordered exact-text matching
carries 8744 source blocks, moving 235 line references. The review records four
retired blocks and adds 31 for 8775 total. Of the 74 previous derivations, two
change through the earlier scene-header cross-reference and the port-fill
delivery note. The new #156 derivation is separately hashed. The original
ALPHA4 text and license bytes retain their hashes.

B156 is resolved. When called, the local initializer restores full viewport
geometry, homes the graphics cursor, sets image fallback and clears the
remembered copy pair. Activation, queries and pixels persist. Bit 1 selects
port 0 and invokes it. Bit 8 preserves the logical current port and invokes it
only while that current port is nonzero. The [connected implementation](RIP3_CONFORMANCE.md#connected-header-port-zero-initializer)
records runtime evidence.

## PR #160 HARD reset field and lifetime profile

The fork stable pin is now
[`430f052d0e1d93af6a56aff343d379a5709cdcd5`](https://github.com/RealDeuce/remote-imaging-protocol/tree/430f052d0e1d93af6a56aff343d379a5709cdcd5).
It contains the reviewed [PR #160](https://github.com/bbs-land/remote-imaging-protocol/pull/160)
head `4e87d32598f4eb39ce153ad72e2ef8437871a063`. The SHA-256 of the pinned Git
tar archive is `f642e26e6395756fc871dcc3d7dd074e8113a8913e6823deb0620e2f16553a50`.
The archive was processed in memory.

Six of the 68 normative documents change. Exact-text matching retains 8771
source blocks, with 275 line references moving. There are 50 new blocks and
four retired blocks, for a total of 8821. Of the 75 previous derivations, 74
retain their hashes. The SOFT derivation adds a cross-reference to the new
HARD derivation. Both original protocol texts and the license retain their
hashes.

B158 is resolved by the published specification. The shared HARD operation
clears template slots, preserves session and persistent variables, and keeps
the current host program and partial ANSI syntax. HEADER and immediate HARD
have distinct revision and input lock outcomes. The updated acceptance rows
remain pending until the connected behavior covers the new profile.

## PR #162 modal pick-list contract

The fork stable pin is now
[`1d46feda7d82001ee6f8b7c614defaa439facb43`](https://github.com/RealDeuce/remote-imaging-protocol/tree/1d46feda7d82001ee6f8b7c614defaa439facb43).
It contains the reviewed [PR #162](https://github.com/bbs-land/remote-imaging-protocol/pull/162)
head `1549601d9c24a6f3023ffd9e7a28d9bfa08f5b8e`. The SHA-256 of the pinned Git
tar archive is `59be56d588b69d05a0ce48227b9e696697f86921a1c7ec8624504d8c5169410e`.
The archive was processed in memory.

Three of the 68 normative documents change. Exact-text matching retains
8784 source blocks, with 480 moving line references. There are 68 new and
37 retired blocks, for a total of 8852. All 76 previous derivations, both
original protocol texts and the license retain their hashes.

B159 is resolved by the published pick-list profile. It defines bounded
position, prompt, response and label parsing, modal selection in both
ordinary passes, and transmission after a response has completed both
passes. Connected pick-list support remains pending.

## PR #163 shared playback filenames

Fork stable is pinned at
[`d7b49fcb929b98ec8efa792496f985d6d55971fd`](https://github.com/RealDeuce/remote-imaging-protocol/tree/d7b49fcb929b98ec8efa792496f985d6d55971fd).
It contains the reviewed [PR #163](https://github.com/bbs-land/remote-imaging-protocol/pull/163)
head `f6da51039a5decbe348203229d12690083069219`. The pinned Git tar archive
has SHA-256 `0f810df65d6b48a705a027fee64fb317d27c6f0033a01701567025383c45ad7c`.

Four of the 68 normative documents change. Ordered exact-text matching
carries 8849 source blocks, with 507 line ranges moving. The index adds
55 blocks and retires three, for 8904 total. The 77 previous derivations,
both original protocol texts and the license retain their hashes. The new
#161 derivation is recorded separately.

The adopted profile resolves B161. It specifies the decoded 20-byte name
bound, ASCII uppercase, captured field and input substitutions, media
extensions and missing-file behavior. The HCL WAV spelling of `$OFF$`
uses ordinary lookup. The wire stop command retains its own behavior.

## PR #165 RIP_SCROLL

Fork stable is pinned at
[`328f8191f526234bbc554b381aa86972eb9d4ef7`](https://github.com/RealDeuce/remote-imaging-protocol/tree/328f8191f526234bbc554b381aa86972eb9d4ef7).
It contains the reviewed [PR #165](https://github.com/bbs-land/remote-imaging-protocol/pull/165)
head `b4ec4a1bd28eab4d448f625670cdb10b8fee87fa`. The in-memory Git tar archive
has SHA-256 `57132db332bc6fe64cc62a1c8db8166da21db660f1627466698c8e8e9dc09ac3`.

Two normative documents change. The index retains 8884 source blocks, adds
41 and retires 20, for 8925 total. All 78 prior derivations, original protocol
texts and the license retain their hashes. The new #164 derivation records
the original handler evidence and portable geometry.

The published contract resolves B164. The destination uses the current XY
width. Copy and fill use drawing-port bounds, and screen transfers preserve
the two rendered blink phases.

## PR #167 HCL bitmap presentation

Fork stable is pinned at
[`9716850d93ba720097af2a3e100e633c6797ac38`](https://github.com/RealDeuce/remote-imaging-protocol/tree/9716850d93ba720097af2a3e100e633c6797ac38).
It includes [PR #167](https://github.com/bbs-land/remote-imaging-protocol/pull/167)
head `e93ccd39e505a641549eecd987a316ce3df089ea`. The in-memory Git tar archive
has SHA-256 `76c46b9657130f27a3486b0884fac5111817475ca577a25e1310b5a56b833124`.

Three normative documents change. The index carries 8915 source blocks,
moves 51 line references, adds 35 blocks and retires ten, for 8950 total.
The 79 prior derivations, original protocol texts and license retain their
hashes. The new #166 derivation describes the original bitmap wrapper and
portable clipping and color rules. It resolves B166.

## PR #169 FILE_QUERY replies and alias sizes

Fork stable is pinned at
[`2900e843f911b05183620c110e43f381d90abc38`](https://github.com/RealDeuce/remote-imaging-protocol/tree/2900e843f911b05183620c110e43f381d90abc38).
Its tree matches [PR #169](https://github.com/bbs-land/remote-imaging-protocol/pull/169)
head `e9f962eb66419b5a8b812f159b5728e70d8efe5a`. The in-memory Git tar archive
has SHA-256 `f8a28d9d16564ef2d92c151873b4ec410cb197926974743bc8ecdbe6b7520637`.

Four normative documents change. The index carries 8934 blocks, moves 125
source line ranges, adds 40 blocks and retires 16, for 8974 total. The prior
80 derivations, original protocol texts and license retain their hashes. The
new #168 derivation resolves B168 and specifies the exact `1F` replies,
name handling and bitmap alias size conversion.
