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

The current pin is fork stable
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
