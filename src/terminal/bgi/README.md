# Shared BGI data and thin-line stepping

The immutable 64-colour EGA RGB16 table and sixteen default pen codes were
extracted verbatim from `cterm_rip_raster.c`. BIG reference chapters 26 and
83 identify the same data. `big_profile` independently checks every EGA entry
against its six colour-bit weights and checks the literal default map.

RIP and BIG keep separate protocol state and wire grammars. Only proved
matching data or used kernels belong here; this is not a second framebuffer
or a general BGI emulation library.

`st_bgi_thin_line` is the candidate/pattern loop extracted from RIP's
`draw_line_to`; both RIP line paths and BIG consume it. It preserves upper-Y
ordering, equal-Y caller direction, inclusive endpoints, decision-zero minor
steps and the caller's16-bit phase. Callers supply already-clipped coordinates,
colour/write policy and failure handling. RIP retains its own clipping and
thickness; BIG supplies its different fixed-slope WORD clip and stroke schedule.
The four same-index builtin line patterns are shared immutable data too.

The twelve same-index8x8 fill masks now use `st_bgi_fill_patterns` in both
RIP and BIG. All96 bytes and initializer text match the extracted RIP table;
BIG chapters11/50/83 identify the original matching masks. `big_fill` checks
them against an independent literal table. BIG keeps application slots,
library/driver copies, colour validation and the installed indexed8x8 tile
in its own adapter. A tuple-colour update does not rebuild that tile. RIP's
consumers retain their existing state/assignment behavior. Routine goal tests
are BIG-only; RIP regressions are deferred to final full-suite verification.

`big_draw` checks hand-authored point traces, pattern phase and BIG's literal
marker masks/XOR visits; RIP's existing raster/oracle/capture expectations
remain unchanged. BIG protocol/state adapters belong in `../big`, not here.
