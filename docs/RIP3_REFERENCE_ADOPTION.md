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

The current pin is stable revision
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
