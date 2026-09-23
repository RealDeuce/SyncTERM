# Repository-wide audit (review only)

## Authority and baseline

This report follows the audit-only goal explicitly approved by the user.
It supersedes the remediation/completion policy in the earlier REVIEW.md,
which remains historical evidence, not authorization to change implementations.
Only tests and audit reports may be changed. Do not modify production code,
headers, build configuration, generators, or existing product documentation.
Do not commit, push, or revert without separate explicit approval. Findings
need not be fixed for this audit to be complete.

Baseline HEAD: 36c584eede5496887d9376c4c56b32d5e974d820.
Pre-existing uncommitted changes preserved at audit start:

- str_list/CMakeLists.txt
- str_list/tests/str_list_failure_test.c
- str_list/tests/str_list_test_array.c (untracked)

Those changes predate this approved audit and are not new audit modifications.
The user subsequently explicitly authorized committing that test change;
it was committed as fdeab0d. This was not permission to commit subsequent audit
work. The user then separately authorized committing the pending audit tests;
hex/UTF-8 boundary tests and the controlled-clock test were committed as
106e869. A further explicit request authorized committing the pending audit
tests as 2c37d3b, including deliberately failing regression cases. That request
does not authorize committing subsequent work. A subsequent explicit request
authorized commit 21a2789: connection, INI, value conversion and audio mixer
audit tests. That permission likewise does not cover later changes.
This report remains uncommitted.
Current HEAD at final verification is 4d920ab (the existing counted-password
test commit). This records repository state, not new authority to commit.
Previously committed fixes and tests can supply evidence but do not establish
repository-wide coverage. The legacy tree is read-only reference.

## Final audit disposition

The repository-wide audit is complete. This is not an implementation approval
or release certification. The consolidated API/design assessment, priority
groups and all 8 outstanding finding summaries are in
AUDIT_INDEX.md; full evidence, recommendations and limitations remain here.

| Objective requirement | Authoritative evidence and disposition |
| --- | --- |
| Entire repository, not only recent changes | git ls-files identifies 47 top-level source directories and 12 root files; every directory has an inventory row, no omissions. Per-component reviews cover local implementations, headers, tests/build and associated data/docs. |
| API design, architecture and library layout | AUDIT_INDEX boundary/coupling/ownership assessment plus component public-contract and CMake dependency reviews; A007, A088/A091 and host-contract findings. Cohesive decomposition is recognized; no redesign authorized. |
| Correctness, ownership/lifetime and concurrency | Component lifecycle/error reviews, normal and injected tests, explicit GUI/host ownership closure; source and reproduced failures are qualified individually. A023 illustrates a remaining timing and reclamation risk. |
| Portability and all platform backends | POSIX/Windows sources and every audio/console backend reviewed; native, MinGW cross-build, focused Wine and unavailable runtime evidence kept separate. Capability probes/standalone targets reviewed. No unsupported platform pass inferred. |
| Performance | Scaler/renderer benchmark source and measured cases reviewed, including common mixed-axis scaling; A011/A023 and backend pacing risks distinguished from measured regressions. Reconstructed legacy benchmark limitations explicitly recorded. |
| Required legacy behavior | Read-only comparisons cover fonts/catalog/maps, modes, palette capacity, Prestel versus Mode 7, command line, console/input and settings behavior, plus explicit parity limits. Frozen CTerm chapter cmp succeeds. |
| Wren scripts and bindings | Complete local source/signature/manual reviews and isolated shipped-script suites and host fixtures. Connected/SFTP production integration missing from this application is reported, not invented or silently counted as working. |
| Build logic, generators and documentation | Root/component CMake and generators reviewed; standalone/probe failures recorded; pinned Unicode/font regeneration and table comparisons documented; root manuals/license/development guidance covered. Wren VM vendored internals limited to integration boundaries as requested. |
| Inventory and consolidated findings | 8 outstanding detailed headings are matched in the index; every finding has severity/qualification and recommended direction. 3 medium, 3 low, 2 unresolved concerns; not all runtime reproductions. |
| Relevant tests where practical | Complete native Clang and GCC builds have succeeded. The current configured native tree registers 118 tests. The latest complete verification is recorded in AUDIT_INDEX.md. Isolated regressions, sanitizers/provider/fallback and cross-compile evidence retained in detailed entries. |
| Explicit uncertainty | Each component records unavailable platforms, unexecuted failure paths and measurement limits; A007/A011 unresolved. Source-level risks are not called verified crashes. These are audit outcomes, not claims of implementation correctness. |

Historical pending-source lists below are superseded by subsequent full reads,
inventory dispositions and the application/console/Wren-host closure sections.
Remaining fault injection, exotic-platform behavior, visual/input-method cases,
complete legacy equivalence and performance breadth are explicitly limited
evidence, not silently successful checks. No requirement to remediate or to
prove absence of every defect was substituted for the approved audit objective.

## Coverage inventory

The numbered-finding navigation table is in [AUDIT_INDEX.md](AUDIT_INDEX.md).
It is the consolidated findings summary. Historical progress notes
below retain the state at the time they were written; the inventory and newer
explicit dispositions take precedence over obsolete pending-source lists.
Current worktree inspection shows audit modifications confined to tests and
the two audit reports. No production, header, build, generator or product-doc
changes were made by this consolidation pass. Every tracked top-level source
directory has an inventory row; that mechanical check does not prove every
integration is reviewed. Application, console and Wren-host integration now
have explicit dispositions below; final inventory/report verification is above.

Latest native verification: the complete configured Clang build succeeds.
CTest executes all 118 registered tests: 117 pass and the unavailable Wayland
display test skips. SDL runs with its configured dummy driver; this is not
real-window verification. Remaining standalone audit cases are not registered
in CMake and therefore are not counted among these 118 tests.

Each directory row includes all tracked sources, headers, backends, generators,
tests, and local documentation. Cross-component callers are reviewed with the
owning component; reviewing a helper alone does not establish caller correctness.
For every area assess API usability, cohesion/dependencies, correctness,
ownership, errors, concurrency, portability, performance, and required behavior.
Completion requires every row dispositioned and a consolidated findings report,
not the absence of failing tests. New findings will receive stable A-numbers.

| Area | Coverage | Notes |
| --- | --- | --- |
| Root CMake, configuration/type headers, .gitignore | Reviewed | Root source read; fresh native configure with testing/Unifont disabled succeeds; component probes and application packaging tracked in owning rows |
| Root manuals, license and development guidance | Reviewed | Manual.txt, Wren.adoc, LICENSE, REVIEW.md and development guidance read; cterm.adoc intentionally frozen and identical to legacy by cmp; Console.adoc covered with console; contract mismatches A088 and A090 and licensing/distribution notes below |
| app/ | Reviewed | All local implementation/header/build/test read; A088, A091; parser and 28 fixed-mode legacy geometry checked; startup/host ownership boundary dispositioned below; native startup tests pass, GUI/platform and future connected-loop limits explicit |
| app_paths/ | Reviewed | Native default/override tests pass; other platforms source-only except MinGW compile |
| array_list/ | Reviewed | A007 unresolved portability concern; native and optimized tests pass |
| audio/ | Reviewed | All local backends/synthesis/build/tests read; A023 callback risk; native fake tests pass, MinGW WASAPI compile only; actual device/platform gaps below |
| audio_mixer/ | Reviewed | All local sources/tests/build read; native continuity tests pass; device integration assigned to audio; ramp/performance/lifetime gaps below |
| bbs_list/ | Reviewed | All local sources/header/tests/build read; native list/cache/web tests pass, including cache confinement and borrowed mutation inputs; MinGW cache/list tests also pass under Wine; integration/fault gaps below |
| codepage/ | Reviewed | All local source/header/build/test read; exhaustive graphic roundtrip and expanded control/invalid-input tests pass native Clang/GCC; MinGW compile only |
| connection/ | Reviewed | Shutdown stress and platform runtime gaps documented |
| console/ | Reviewed | All local implementation/backends, tests, benchmark, build and docs read; GUI closure notes, native/isolated tests and platform-only risks distinguished; remaining Wren consumer questions belong to wren_host |
| dynamic_library/ | Reviewed | Native fixtures pass; Windows compiled, not executed |
| event/ | Reviewed | Native repeated tests, MinGW compile; failure-injection limitations below |
| file_lock/ | Reviewed | Native coordination and controlled error-path tests pass; Windows source/compile only |
| file_map/ | Reviewed | Native stream-lifetime test passes; Windows source/compile only |
| filesystem/ | Reviewed | Native expanded tests and Windows root tests pass; other Windows behavior is source/compile reviewed |
| fonts/ | Reviewed | API/generator/build/tests/docs reviewed; 98 legacy bitmaps and all 46 catalog metadata entries match; mapping bounds/legacy comparison and semantic limits below; native tests pass |
| hex/ | Reviewed | Native tests and MinGW build; caller integration tracked separately |
| ini/ | Reviewed | All local sources/tests/build read; native tests pass; consumer integration tracked with owning components; failure injection/performance gaps below |
| io_error/ | Reviewed | Sources/header/build and caller adapters reviewed; native test passes; 28 Win32 mapping cases added and MinGW compile/link verified, Windows execution unavailable |
| list_crypto/ | Reviewed | All local code/tests/build read; expanded counted-password tests pass natively; MinGW compile only; provider/resource/erasure limits below |
| name_value_list/ | Reviewed | Ownership/sorted contracts checked; allocation injection remains a limitation |
| path/ | Reviewed | Native boundary and controlled Windows growth tests pass |
| pixels/ | Reviewed | All local API/implementation/filter/build/docs/tests/benchmark read; expanded upscale edge tests pass native/ASan/UBSan, MinGW compile; measurement limits below |
| profile/ | Reviewed | Ownership/copy source and native tests; allocation-failure injection remains a test gap |
| scripts/ | Reviewed | All current script implementations/tests read, including main menu, editors, REPL, common bindings module and self-test driver; isolated suite results and runtime/cross-script gaps documented below; older remaining-source lists superseded |
| semaphore/ | Reviewed | Native repeated tests, MinGW compile; failure-injection limitations below |
| serial/ | Reviewed | Native PTY capability and I/O tests pass; Windows builds and reports the PTY test as skipped |
| settings/ | Reviewed | All local sources/header/tests/build read; global caller discipline and failure injection gaps remain below |
| socket/ | Reviewed | Loopback native tests pass; Windows compile only |
| str_list/ | Reviewed | Native failure injection and alias-input cases pass; MinGW compile |
| string_compat/ | Reviewed | Native and opt-in fallback tests pass; MinGW compile |
| string_util/ | Reviewed | Exhaustive byte classification/trim tests pass natively; MinGW compile |
| tcp_client/ | Reviewed | Native algorithm and real loopback/refusal tests pass; Windows runtime exercised under Wine |
| theme/ | Reviewed | All local sources/header/tests/build read; catalog/editor/error cleanup regressions are covered; remaining host/network integration and failure-injection gaps are below |
| threads/ | Reviewed | A011; native and forced pthread fallback tests pass; Win32 backend compiled |
| time/ | Reviewed | Controlled-clock and existing native tests; Windows source-only review |
| unicode_grapheme/ | Reviewed | Public contracts/generator/runtime/build/docs/tests reviewed; pinned regeneration identical, generated trie bounds/order checked; expanded native tests pass, MinGW compile only |
| unicode_normalization/ | Reviewed | Public contracts/generator/runtime/build/docs/tests reviewed; pinned regeneration identical, table bounds/order checked; exhaustive Hangul roundtrip passes natively, MinGW compile only |
| unicode_width/ | Reviewed | Header/generator/runtime/build/docs/tests reviewed; pinned regeneration identical, exhaustive scalar consistency passes; font/grapheme integration tracked separately |
| unifont/ | Reviewed | API/build/test/docs/license/shared generator reviewed; identical regeneration and exhaustive UNSCII overlay test; enabled/disabled native builds verified; packaging/Wren display tracked with owners |
| utf8/ | Reviewed | Expanded native tests and MinGW build; caller integration tracked separately |
| value_convert/ | Reviewed | All local sources/tests/build read; native exact rounding and date-precedence checks pass; exotic formats and numeric performance unverified |
| webget/ | Reviewed | All local source/header/test/build read; exact-URL cache identity and single-owner transport cleanup covered; remaining protocol/TLS/fault gaps below |
| windows_utf/ | Reviewed | Source and MinGW compile; Windows runtime not verified |
| wren_host/ | Reviewed | All local implementation, public/internal headers, CMake/generator and integration tests read; isolated shipped-script and host-contract cases run; mutation/error/input/font/ownership integration dispositioned below; runtime/fault limits explicit |
| wren_vm/ | Reviewed at integration boundary | Full legacy diff, build/configuration, license, REPL private API coupling and minimal consumer link checked; upstream internals not exhaustively audited |
| xptls/ | Reviewed | All local source/header/build/test files read; broader TLS/platform coverage gaps below |
| xptls_crypto/ | Reviewed | All local headers/providers/clear implementations/build/tests read; expanded tests pass OpenSSL, Botan and none; MinGW compile only; contract/resource gaps below |

## Root documentation review and consolidated follow-ups

### Wren-host integration coverage closure

All previously identified host follow-ups now have findings or explicit
dispositions. The Wren skill and local manual guided these comparisons; the
modern isolated host harness, not the legacy executable, supplied runtime
evidence. All new fixtures use fresh temporary paths and a headless console.

- Window mutation bindings discard native failures, including
  invalid window bounds and write/scroll failures. This is a source-only error-
  reporting concern: setter return conventions and recoverable write failures
  need an explicit contract; not every void-style setter is thereby broken.
- Theme selection/deletion failure behavior is covered for local and cloud
  themes. General OOM/failure rollback remains untested.
- Host/VM lifetimes, borrowed config and synchronous dispatcher use follow the
  application ownership review. Multiple logical consoles do not make Wren
  thread-safe. Actual connected-session event scheduling/SFTP capabilities
  remain absent or unavailable as A088 documents; this audit did not invent
  that consumer or count menu execution as proof of it.

Remaining work is final repository-wide inventory/evidence reconciliation,
not unseen host source or a requirement to implement these findings. Review
completion does not imply that every documented API currently works.

### GUI backend coverage closure

The earlier full source/test/build reads are supplemented by the current
lifecycle recheck. Their historical "remaining source" statements are
superseded; unavailable runtime/fault/performance checks are limits, not passes.

- Quartz destroy_state removes its entry, clears view/delegate consoleState
  pointers and window.delegate before closing/releasing objects. Normal
  destruction therefore has an explicit callback-detachment mechanism; a
  generic claim of inevitable late-callback UAF is unsupported. Dispatcher
  shutdown/submission failure, asynchronous fullscreen transitions, input
  composition and focus/surrogate sequences remain unexecuted macOS risks.
- GDI synchronously submits destruction and joins its message thread, but
  ignores delivery/destruction results. Class registration uses the executable
  module and retains the class; DLL unload/reload remains a lifecycle design
  concern tied to the missing application DLL boundary A091. No relevant
  DPI-awareness setup was found in app/GDI source; monitor/DPI transitions,
  failed message delivery and mixed-axis resizing require Windows runtime
  verification. Cross-compilation establishes neither behavior nor a crash.
- SDL dispatcher/borrowed-command lifetimes depend on callers destroying their
  consoles and stopping submissions before the application runner exits.
  Application ownership review follows that order. Dummy-driver tests cover
  normal lifecycle, not renderer reset, accelerated scaling-quality changes,
  multiple vsynced windows, input methods or monitor changes. Immediate event
  drawing is a measurement target, not proof of a throughput regression.
- X11 retains/coalesces exposure output. Default-visual mask conversion has no indexed-visual
  allocation path; legacy x_events.c contains XAllocColor use. Indexed-visual
  parity remains unverified rather than declared supported or intentionally
  removed. Clipboard INCR/fallback, alternate modifier maps and unusual visual
  classes remain source-level compatibility risks documented in its detailed
  review; no physical clipboard/input was exercised.
- Wayland protocol-version/seat-removal/repeat/compose limitations remain
  source-level compatibility risks. Compositor positioning restrictions are
  not substitutes for implementing supported behavior or lifecycle handling.
- Libretro's presenter is reviewed separately from an application core. A091
  covers the absent application ABI;
  emulator-driven cadence/blinking cannot be declared verified without that
  consumer. No new host was built during the audit.

Console coverage is now dispositioned, including header/layout, cell/font/
raster/presenter responsibilities, backend ownership and failure paths, legacy
comparisons, build dependencies and the actual limits of tests/benchmarks.
This does not declare the implementation complete or every platform correct.
Remaining Wren event/selector/error contracts remain in the host inventory.

### Console move and platform-risk dispositions

Rechecked `console_text.c:781` move_cells, `console_pixels.c:529` cached raster
moves and Console.adoc's text/pixel contracts. Generic moves snapshot cell
records and write them through semantic cell setters, normalizing cropped
wide-pair boundaries. They do not copy the source's final pixel planes.
Scrolling and line insertion/deletion instead move cached rasters with their
cells, including the circular full-width text-plane path. The manual promises
snapshot *cell* moves and explicitly describes cached rasters for scrolling;
it does not promise generic pixel-overlay moves. Therefore this difference
alone is an API clarification/design concern, not a confirmed regression.
Callers requiring final-raster copies must not infer them from cell movement.
Overlay/overlapping-move equivalence has not been runtime-tested. No measured
generic move allocation/performance regression is claimed.

Windows text source recheck confirms that KEY_EVENT_RECORD repeat counts are
reduced to a physical-event boolean while text/key-code events are emitted
once per record. Ctrl text below U+0020 is filtered, but physical events remain
available: shortcut loss cannot be asserted without the relevant consumer.
Pending surrogate state is not cleared on focus/activation, and output emits
surrogate halves according to continuation cells. Non-BMP narrow-cell output
therefore needs a defined fallback policy. These remain source-level input/
output contract risks; the existing Windows test does not exercise them.
RGB uses fixed thresholds and indexed colors use four bits; mutable-palette
approximation, bold/blink/conceal policy, host buffer-versus-window resize and
partial resize rollback remain explicitly unverified platform behavior, not
passing tests.

### ANSI terminal-ownership disposition

ncurses has its own `curses_in_use`
guard, but stdio ANSI initialization has no corresponding ownership guard
and snapshots/restores stdin's termios independently for each display. Two
ANSI displays could compete for input and restore modes out of order; mixed
ANSI/ncurses displays are not protected by the ncurses-only guard either.
Logical consoles sharing one display do not have this issue. Multiple
stdio-display creation was not executed; treat exclusive terminal ownership
as an unresolved API/lifetime constraint, not a claimed supported use case.
Likewise ANSI's setupterm probe does not restore/release terminal state before
closing its dynamic-library handle. Interaction with an existing curses
screen and allocation lifetime remain source-only integration risks, not
verified crashes or quantified leaks.

### A098 — Shipped UI demo imports an absent module

Severity: low (confirmed demo integration gap, not a menu-startup failure).
scripts/ui_demo.wren:30 imports transfer_app, which is absent from the script
tree and embedded module list. Interpreting the actual shipped demo source
through the production loader again fails on that import in a fresh isolated
script root. The demo itself is not embedded, so a failed bare import alone
would not establish this finding. Recommend supplying/documenting a supported
invocation and its dependencies; connected-only modal/transfer calls remain
additional context requirements and are not tested past the missing import.

This entry consolidates a previously recorded unnumbered gap rather than a
newly discovered regression.

### A095 — Distribution notice assembly is not implemented

Severity: low (confirmed packaging/documentation gap; no legal compliance
determination). Root LICENSE correctly limits its BSD default to material
without a different notice and explicitly identifies the enabled-Unifont
binary policy. unifont/LICENSE.GPL-2.0-or-later and wren_vm/LICENSE provide
local GPL and MIT texts. fonts/UPSTREAM.md identifies the selected UNSCII
sources and excludes unscii-16-full. The three Unicode UPSTREAM.md files name
Unicode License v3, with a link in the width component, but no local Unicode
license text was found in the tracked source inventory. Likewise, several
retained sources identify LGPL licenses (str_list, audio synthesis/beep,
audio_mixer and pixels/st_xbr.c), but no corresponding full LGPL text was
found. st_xbr.c's retained notice expressly refers readers to such a copy.

All component/root CMake files were searched for install/CPack rules; none
assemble a distributable application or its third-party notices. st_pixels
unconditionally includes st_xbr.c, so disabling Unifont alone is not evidence
that all included material has the root BSD license. The root's exception
wording must not be flattened into an all-BSD claim. The host build-options
entry identifies enabled Unifont's GPL binary policy but is not a replacement
for a complete dependency/license inventory.

Recommendation: before distribution, assemble and verify the applicable
notices/texts and chosen-provider inventory alongside actual deliverables,
including statically linked components. This records observable source/build
packaging facts only, not a legal judgment about license compatibility,
relinking obligations, or a particular binary distribution. No notices,
licenses, generators, build options or existing documentation were changed.
This dispositions earlier pending license-packaging notes as an outstanding
product task, not an unperformed claim of packaging verification.

### Text-backend failed-output cache reconciliation

Revisited the earlier concern that ANSI updates drawn-cell state before its
output callback succeeds. console/console.c's present_locked returns on
backend failure before clearing the console's dirty flags. On retry,
console_ansi.c:473's predicate therefore does not skip changed dirty cells,
even though its private drawn cache already matches. Strengthened
test_snapshot_drains to require the actual `abc` payload after its injected
write failure and successful retry, not merely foreground/background escapes.
The new assertion passes. This rules out the simple all-or-nothing callback failure
scenario as a new defect; it does not establish recovery after arbitrary
partial escape-sequence writes, failed physical terminal clearing, or hangup.

Native curses likewise skips only clean matching cells and propagates its
move/attribute/write/refresh errors (with the documented-in-code bottom-right
write exception) to the shared presentation path. Its source ordering alone
does not demonstrate permanent cache loss on a failed presentation. No curses
write-error injection was performed. Keep physical-terminal corruption and
partial-write recovery as explicit verification limits rather than treating
the historical source concern as a confirmed defect.

The earlier backend source-pass lists are chronological, not current claims
that the ANSI/curses tests or GUI source files remain unread. Later entries
record their completed source/test reviews. Windows input repeats, control
keys, pending surrogate behavior, palette approximation and host resize are
still source-level concerns with no Windows runtime evidence; cross-compiling
their tests does not resolve those questions. These limits must survive final
consolidation rather than being converted into passing-platform claims.

### Application/host ownership and teardown reconciliation

Reread app/syncterm.c ownership against st_wren_host_create/destroy,
st_wren_picker_host_init/dispose, per-VM log disposal and all four local
foreign-finalizer families. The host copies its config structure but borrows
display, consoles, paths and string pointers. In the application those refer
to objects/argv/static strings whose lifetime covers host destruction. Picker
call/class handles are released before its VM; menu handles and cached log
handles before the menu VM. The host and theme manager remain alive during
both VM finalizations; BBS lists, themes and selected profile are disposed
afterward. The app subsequently destroys the consoles/display and shuts down
settings/paths. Inspected finalizers release owned profile/settings/document,
surface storage, snapshot and path data; no new use-after-free is demonstrated
by this ordering. Font-state aliasing in the single-console compatibility path
is a value copy of inline arrays, not two owners of a heap allocation.

Creation uses zero-initialized state and funnels partial-failure cleanup
through destroy. Allocation failure at every individual VM operation has not
been injected.
Existing headless wren_host and wren_picker CTests pass again (2/2). This is
not evidence that all script-abort cases recover, nor permission to treat a
Wren fiber as a concurrently executing thread. Wren skill/guidance was used
to check VM ownership and cooperative execution assumptions; no script or
binding implementation was modified.

API-contract concerns: the header expressly borrows console/paths but should
also state display, picker-console and optional string lifetimes, and that
host/VM operations require caller serialization. The application currently
provides that serialization. No concurrent-safe host claim is inferred from
the console's locks. launch_script is retained in config but has no current
consumer, consistent with the unimplemented connected runtime (A088), not an
additional independently working connected-script path.

Startup policy disposition: title-setting errors and web-cache refresh status
are ignored by app; refresh cleans its temporary overlay/snapshot/arrays
before returning and can leave existing caches available after a failure.
This is best-effort startup behavior, not proof refresh succeeded. Making
such failures fatal is not prescribed by the audit.
These checks close the application row's earlier ownership/integration
questions while leaving broader host-script/backend integration in their own
rows. Actual Windows/macOS/Haiku application execution, GUI fallback under
host failures, and unload/reload are unverified, not claimed as passing.

### Application fixed-mode geometry reconciliation

Compared legacy syncterm.c screen_to_ciolib with conio/vidmodes.c and the new
app/st_console_startup.c table. Added test_legacy_mode_geometry to the existing
app startup test, with expected values derived from the legacy table rather
than copied from the implementation under test. All 28 fixed application
screen modes pass for columns, rows, cell width/height, full raster dimensions,
aspect numerator/denominator, and default cursor start/end. This includes
43-row non-text margins, EGA/VGA, LCD, both 132-column aspect variants,
Commodore, Atari, Prestel/Mode 7 and Atari ST modes. CURRENT and CUSTOM are
policies, not fixed legacy table entries, and are not counted as fixed modes.
The eight -l row choices were source-compared separately; 14/21 use the legacy
640x224/336 geometry and the other six agree with their corresponding modes.

Native rebuilt app_startup passes. An initial fixture omitted the positive
custom dimensions required by startup_configure even for fixed modes; that
test setup was corrected, not counted as a production failure. The expanded
test does not compare palette selection, legacy mode flags, chosen fonts,
blink timing, real-window sizing, or CURRENT's interaction with a terminal's
reported size. This supersedes the earlier pending all-mode *geometry* check,
not the remaining application/host integration review. No production/build
or product-documentation changes and no commits.

### Console mutation contract follow-up

Normal rectangle fill and move-clear
explicitly reject wide fill cells; generic move snapshots and normalizes
source boundaries and retains EXPLICIT_GLYPH through the cell setter. This
source review does not establish arbitrary pixel-overlay move preservation.
Direct put/clear/scroll and ordinary cell writes lack the Unicode scalar
validation used by public font lookup and complete-cell writes, while the
manual explicitly promises validation only for UTF-8 strings. Record that
inconsistency as an API-contract concern, not an asserted requirement to
choose rejection rather than replacement. Clear/scroll codepoint fills also
lack the wide-fill rejection used by rectangle fill. Their intended policy
needs specification; existing window/pair findings should not be hidden by
silently restricting inputs in tests. These observations disposition the
earlier source questions without claiming those paths are correct or fixed.

### Error translation coverage follow-up

Reread both io_error implementations, public header and CMake/test, and checked
the file_map, file_lock, filesystem and serial Windows forwarding adapters.
The public contract intentionally leaves operation-specific interpretation
with callers; unknown native errors collapse to IO_ERROR_IO. The Win32
argument is unsigned long, matching DWORD in the target Windows environment;
the general header does not require windows.h. Its Windows translator is
declared on POSIX but only linked on Windows, an API discoverability concern,
not a demonstrated current caller defect. This does not justify exporting
internal helpers across the application DLL boundary.

Added 28 pure mapping assertions to io_error/tests/io_error_test.c under the
Windows compilation branch, covering success, encoding, missing paths,
permissions, existing paths, directory errors, memory/disk exhaustion,
size/range, contention, read-only/cross-device, cancellation, timeout,
unsupported operations and unknown-code fallback. Existing native test passes
after rebuilding. The expanded Windows test compiles and links with MinGW;
it was not executed. These checks validate selected translation contracts,
not which native errors a real failing operation will produce, exhaustive
Windows error coverage, or last-error preservation at every call site.

### A091 — Required Windows DLL/launcher and libretro application packaging is absent

Severity: medium (confirmed architectural requirement gap, not a reproduced
Windows runtime failure). AGENTS.md specifies a DLL containing SyncTERM and
implementing the libretro ABI, with thin console .com and GUI .exe launchers.
The current app/CMakeLists.txt instead unconditionally builds syncterm_app
from syncterm.c and st_terminfo.c, linking the static component targets. It
defines neither that shared application library nor either thin launcher.
Root CMake adds the component/application directories without another
packaging path. Repository source/build searches for RETRO_API, WinMain,
DllMain and shared-library targets found no application ABI implementation.
console/console_libretro.c provides a pull-based bitmap backend, not the
libretro core entry points or the application run loop required by this
architecture. Backend frame-copy tests do not establish core integration.

Recommendation: track the DLL boundary, narrow launcher exports, libretro
entry points, and separate launcher artifacts as explicit outstanding
implementation work. Do not infer that internal component APIs need exports.
This audit does not implement that architecture, and does not claim the
existing standalone executable fails to run on Windows.

Verification in this pass: reread root/app build definitions, application
entry point and command-line parser; native configured CTests syncterm_help,
syncterm_version, syncterm_terminfo and app_startup pass (4/4). Those tests
exercise early-exit/parser/startup configuration paths, not interactive GUI
startup, Windows execution, or DLL unload/reload. Existing A088 separately
records the absent connected-session loop and misleading manual scope.

Application parser legacy comparison: read the option switch in legacy
syncterm.c:2336–2535 against app/st_command_line.c. The backend letters,
window/fullscreen suffixes, eight supported row values, address-family,
status/popup, safe-mode, default-protocol and script options remain represented.
ANSI U/C suffixes are the explicitly requested extension, not a regression.
Modern parsing rejects trailing garbage on flag/backend/row options that
legacy often ignored; this is stricter invalid-input handling, not evidence
of losing a documented valid invocation. Modern -b/-n correctly consume the
separate value (the legacy second pass did not increment past it). Both retain
the last positional URL. Legacy's Darwin-only ignored -psn_ argument is not
accepted now; whether any supported launch environment still supplies it is
unverified, so this is a compatibility question rather than a confirmed
current macOS startup defect. This comparison does not establish connected
protocol behavior or actual backend availability from parser acceptance.

Read all 1489 lines of Manual.txt, the 33-line root LICENSE and all 179 lines
of historical REVIEW.md. AGENTS.md and WREN_GUIDANCE.md were read during the
audit; Console.adoc was covered with the console component. `cmp cterm.adoc
/synchronet/src/sbbs/src/conio/cterm.adoc` succeeds: the deliberately frozen
CTerm chapter is byte-identical to the reference, not a promise that its
emulator has already been ported. REVIEW.md's remediation rule remains
historical and does not authorize changes in this audit.

Build/documentation agreement checked against root/app CMake and crypto
selection: CMake 3.22, C17, Botan >=3.13/OpenSSL >=3.0 selection, executable
location, and optional Unifont inclusion agree with the current build.
The root license and manual both explicitly disclose the GPL consequence of
including modified Unifont raster data. This records consistency of project
notices, not an independent legal opinion on all distribution combinations.

### A088 — User manual does not clearly distinguish implemented menu from future connected runtime

Severity: medium (confirmed documentation/implementation mismatch).
Manual.txt's Directory and Connected State sections describe selecting a BBS
as establishing a connection and offer terminal, transfer and SSH/MQTT
operations as current behavior. app/syncterm.c:220 instead applies selected
profile overrides, sets success, then disposes the profile and tears down the
host/consoles; there is no connected-session loop. The dependency section
describes current optional-library behavior but gives no comparable boundary
for these larger retained sections. This is not authorization to port those
features, nor a claim that the consciously deferred CTerm port is itself a new
regression. Recommend a clear current-implementation scope notice while
preserving the legacy CTerm specification unchanged.

Additional documentation gaps: Manual.txt references Themes.adoc, which is
absent from this repository. Its opening still claims this project lives in
the Synchronet git repository. The connected GHost section gives a
ghost:// URL while the earlier supported-scheme list omits it. These are
source-checked documentation gaps;
no GUI, network connection, personal settings or distribution was exercised.

### A090 — Wren filesystem handle and error contract is unresolved

Severity: low (contradictory public documentation and an unsettled handle
model, not a demonstrated filesystem defect).

`File` and `Directory` objects retain pathnames, but the manual also describes
them as filesystem snapshots and claims that existence checks prevent an
operation from acting on the wrong object. A pathname lookup cannot establish
that identity: an entry can be replaced at the same name.

The current methods also report failed lookups inconsistently. A closed
`File` whose pathname cannot be inspected is marked dead and returns
`FileErr.vanished`; a `Directory` whose pathname no longer names a directory
is marked dead and aborts the fiber. Generic filename-policy prose additionally
fails to distinguish the strict Cache leaf-name policy from the relaxed safe
leaf names accepted below Download.

Before changing either code or documentation, decide whether these handles
represent path capabilities or particular opened filesystem objects, then
define per-operation error results, invalidation, replacement behavior, and
the separately scoped Cache and Download name policies. The existing
existence checks and `FileErr.vanished` name are evidence, not the desired
contract.

## Findings

### A007 — Typed-list representation needs a portability decision

Severity: unresolved, potentially medium. Classification: design/portability
concern, not a reproduced miscompilation or an approved redesign.

`array_list/array_list.h` ARRAY_LIST_DEFINE and SORTED_ARRAY_LIST_DEFINE cast
the address of a typed handle to array_list_t *; the underlying functions then
read/write it as void ** storage. Array slots are also maintained as void * and
exposed for direct typed-pointer indexing. str_list uses the same handle-address
casts. This is more than converting an individual object pointer to/from void *:
the effective access types and pointer representations deserve an explicit
supported-platform contract. The user's requested cast-based typed interface is
intentional; this concern does not imply the interface must be discarded.
An optimized GCC 13 -O3 -flto -Wstrict-aliasing=2 run and Clang -O3 run of the
array tests pass without diagnostics. No optimizer failure has been demonstrated,
and passing tests do not settle the C-language portability question.
Recommendation: separately establish the supported representation/aliasing
contract before deciding whether any implementation adjustment is needed.

### A011 — Non-native rwlock fast paths need performance validation

Severity: unresolved performance concern. Classification: source-confirmed work
on uncontended paths, not a measured throughput regression.

`threads/st_rwlock_windows.c` allocates a reader record on first acquisition,
frees it at final release, resets zero_readers for each acquisition, and sets
the event when the last reader leaves. Write acquisition/release also perform
event operations without first requiring contention. Reader lookup is linear
in participating thread records. The pthread rwlock fallback shares the reader
allocation/linear lookup, though it uses condition variables rather than events.
These costs deserve checking against the stated efficiency objective, especially
for short-lived locks in renderer paths. Windows native SRW replacement is not
automatically equivalent because the API explicitly supports recursive readers.
Recommendation: benchmark uncontended and contended paths against actual console
usage before selecting any remedy. No synchronization redesign performed.

### A023 — Real-time audio callbacks take locks and free objects

Severity: medium performance/reliability risk. Classification: confirmed
callback-path operations conflicting with provider guidance; no measured
deadline miss or audible glitch reproduced.

audio_backend_portaudio.c's callback synchronously invokes audio.c backend_pull.
That calls mixer pull (mixer and stream mutexes), acknowledgment (locks and
buffer deallocation), then reap_one_shots (engine lock, stream destruction,
condition/mutex destruction and free). Public clear can also free an arbitrarily
long queue under the same stream mutex, blocking the callback behind it.

PortAudio's callback guidance specifically identifies mutex operations and
allocation/deallocation as operations to avoid due to unbounded execution and
priority inversion. Source: [Writing a Callback Function](https://portaudio.com/docs/v19-doxydocs/writing_a_callback.html).
PipeWire explicitly selects RT_PROCESS and reaches the same shared path;
other callback providers share it too, but their exact timing guarantees were
not established by this finding.

Recommendation: establish a bounded callback execution/ownership strategy and
defer reclamation outside the callback, then measure worst-case callback time
under producer/clear contention. This is a recommendation for separately
authorized work, not permission to redesign the mixer during the audit.

## Component notes

### Main menu — full script source coverage

Read all 1189 lines of auto/menu/main_menu.wren. Reviewed widget/focus routing,
comment editing, command gating, list refresh, editor navigation, add/delete/
copy/paste, quick connect, settings reload, password preparation and exit.
Consulted the accepted-edit persistence and modal ownership manual contract.
Command handlers ordinarily require an empty modal stack; console access is
deliberately bound separately and can inspect the current menu context.
Directory copy uses a detached BbsEditor draft, and paste copies the palette
list before applying. Current selection is normally restored by entry name
after sorting/reload, not retained row position alone.

Cross-component risks: settingsAction_ calls MainMenu.prepare after changes.
Edit navigation can reach system entries without returning through edit_'s copy prompt;
native write policy remains the enforcement boundary. Comment save failure
alerts but leaves the edited working value and moves focus; this is not a
claim of rollback. Several add/paste cancellation/error branches ignore the
result of deleting the newly created entry. Fault-path integration remains
outstanding and no personal-data mutation was attempted.

Layout uses byte counts for directory labels/title widths while drawing uses
glyph display widths; Unicode labels can overestimate space. Tiny-screen
stacked panes impose minimum heights/widths that can exceed the screen; actual
clipping/interaction needs tests. Exit-confirmation guard is reset on normal
return only; an aborting confirmation could strand it. These are source
concerns, not new GUI reproductions. Wren guidance used; only tests/report
changed, no commits. Remaining script source reading is syncterm.wren and
wrentest.wren remainder; full repository completion is still unproven.

### Common Wren module — full source coverage

Completed all 2159 lines of scripts/syncterm.wren, including declaration
groups, error classes, screen preservation helpers, input claim/wake contract,
REPL classification, Surface sequence views, constants, capability APIs,
hook dispatch, and WON serialization. This completes the current scripts/
source inventory; older chronological lists of outstanding scripts below are
superseded. Source coverage is not equivalent to connected-host integration
or runtime verification of every foreign declaration.

Source-level contract concern: Range keys are hashable and documented as
supported Map keys, but writeMap_ feeds them to write_, which converts Ranges
to Lists. Serialized Map output then contains List keys, rejected by the
documented parser contract. Legacy shares this conversion; do not label it a
new modernization regression.

Other design limits: SurfaceRows/Cols manufacture lazy views without bounds
validation at outer indexing (cell access returns null later). Preservation
helpers explicitly do not restore on abort, so callers must own that cleanup;
this is documented behavior, not an undisclosed guarantee. REPL statement
classification skips ASCII whitespace but not leading comments; comment-led
declarations need a focused evaluation test. Many connected APIs remain
declared but unavailable in the menu host by design; declaration presence
alone cannot establish implementation coverage. Stale ciolib/process-global
comments should be reconciled in a separately authorized documentation pass.

Used Wren guidance and the relevant manual contracts. Only audit tests/report
changed; no production changes or commits. Root documentation and remaining
cross-component follow-ups still prevent repository-wide completion.

### Wren self-test driver — full source coverage

Completed the remaining lines 901–1890 of scripts/wrentest.wren after the
earlier 1–900 review. Covered live CTerm/BBS properties, console/REPL,
Cell/Surface views, hooks, filesystem/hash tests, WON cases, sentinel-driven
async phases and final cleanup. Consulted Wren.adoc's hook dispatch and WON
contracts and the Wren guidance. This completes source reading, not execution
of the connected-session suite.

The driver imports scrollback_view, which is absent from the current scripts
inventory, and references absent runtests entry scripts. It is not registered
in wren_host/CMakeLists.txt. Its async phases send shell commands to Conn;
filesystem cases delete fixed cache names before creating fixtures. Do not
run this wholesale in the menu host or against personal cache/connection
state. Individual UI suites already exercised by the isolated audit runner
are recorded separately; those results do not verify connected dispatch.

Test-design concerns confirmed by source:

- Hooks are registered before unguarded nested suite calls. An abort there
  bypasses report_ and cleanup_, leaving the registered hooks behind. The
  watchdog returns immediately while __pending is null, so it cannot recover
  this early abort. cleanup_ also assumes claim B was already popped by T09;
  it is not a general abort-safe cleanup routine.
- The three-second watchdog is periodic, rather than reset at each phase.
  A new phase can therefore receive less than three seconds before timeout.
  Result delivery assertions also depend on wall-clock shell sleeps rather
  than explicit completion acknowledgments. No timing failure reproduced.
- testWONDeserializeErrors_ assigns `deep` from a bare backslash-u input but
  never checks it. This neither verifies a quoted truncated Unicode escape
  nor tests the documented parser nesting limit. testFileErrConstants_ checks
  enum availability, not actual recoverable I/O error handling.
- The normal return path does remove the registered hooks and pops claim A
  and the fall-through claim. It should not be described as having no cleanup;
  the uncovered behavior is cleanup on abort or incomplete async phases.

No production changes, new runtime claims or commits in this pass. Remaining
script source reading is the rest of syncterm.wren (lines 1–210 reread here);
repository-wide documentation and final coverage consolidation remain open.

### BBS editor — full script source coverage

Read all 1409 lines of menu_bbs_editor.wren and all original 182 lines of its
tests. Reviewed draft construction, immediate application, close-time save,
navigation, protocol-dependent fields, palette/log subeditors, and script
ordering. Legacy comparison shows this script is identical. The local manual
explicitly says owning-editor Escape does not roll back accepted edits;
immediate application is therefore not itself a cancellation defect.

Source close-path concern: update_ validates paired TLS fields before applying
any draft changes; callers retain the draft after false. pane.onClose checks
bbs.dirty and save_(bbs) but never update_/validate the draft. Thus an invalid
half-entered certificate/key or PSK pair can remain displayed but not applied,
and Escape can close successfully when the native entry is clean. Even when
dirty, saving writes the previously applied entry, not the rejected draft.
This needs a focused modal test and an explicit UX decision about preserving
or discarding pending invalid edits; it is not evidence of native file loss.

Other observations: script reordering mutates the draft list in place but
only calls onChange on commit; cancel restores the original slot. Palette
editing materializes minimum defaults and normalizes an unchanged default
palette back to empty on exit. Save failure keeps the editor open and clears
pending next/previous navigation. Sequential native property setters are not
transactional if a later setter aborts; test boundary-invalid values. GHost
labels edit password while ghostProgram is separately copied/applied; trace
the eventual connection consumer before asserting the desired mapping.
Hard-coded protocol/mode/font catalog indices couple the script to native
tables; no mechanical enum replacement is authorized by this audit.

Wren guidance used; only tests and report changed, no commits. This completes
BBS editor source coverage. Remaining script sources are main_menu.wren,
syncterm.wren remainder and wrentest.wren remainder, plus cross-script and
runtime gaps already recorded. Existing auxiliary pixel/demo/Unicode/theme
failures remain, and are not new BBS-editor findings.

### Theme editor — full script source coverage

Read all 1576 lines of menu_theme_editor.wren and all 250 lines of its test
module; reviewed legacy diff around reusable cell views and Unicode glyph
selection. Consulted ThemeDocument ownership/save/error contracts. Native
strict Clang auxiliary-scripts runner now executes the unmodified theme
suite: 12 passes, 1 failure (combined Unicode/ASCII glyph-display check).
That assertion includes inspector column-zero checks, while onPaint_ draws
its text at x=1. This is a confirmed test-oracle mismatch within the combined
failure, not proof the entire display is correct or broken. Assertions were
not weakened; isolating its atlas and inspector subchecks remains useful.

Draft ownership is clear: the native document is authoritative and refresh
creates a separate Theme.fromData snapshot. Preview applies the draft to
inner widgets, retaining installed styling for its outer controls. The
reusable Cell is bound to surface storage by cellAt(_,_,_), so mutating it
does write the grid; no missing write-back finding. Model mode setters
validate values and preserve selected names across refreshes. Codepoint
parsing checks scalar upper bound and surrogates before acceptance.

Save/close behavior: conflicts and existing destinations require confirmation
before force=true; other save errors stay visible and return false. The close
prompt only accepts Save if saving succeeds. Discard closes the editor but
does not roll back a caller-retained native draft; caller ownership/lifetime
must be considered when interpreting that action. state.saved records explicit
Save/SaveAs actions, not saving through confirmClose_; current callers should
not assume that flag exhaustively reports writes. Runtime save-fault and
nested-modal coverage remain outstanding; no files were written in this pass.

Source concerns: filename_ slices the .ini suffix using codepoint counts as
byte offsets, so non-ASCII basenames can receive a duplicate extension.
ThemeEditorContent chooses inspector width with min(rect.w - 20), which can
be negative on narrow consoles; test tiny geometry before treating this as
supported. Repeated refresh reconstructs theme
snapshots and can refresh content twice via model.onChange plus the explicit
call; no performance measurement is claimed.

Theme editor and its test are removed from the remaining source-review
backlog; broader runtime gaps remain.

### Wren console — source coverage

Read the complete wren_console.wren and its full legacy diff. Reviewed
command registration, history, REPL error isolation, quoting, clipboard input,
prompt positioning, log reconstruction and screen/mouse restoration. Local
changes add default colours and backend-appropriate Ctrl+Q/Ctrl+S exits;
other reviewed paths are inherited. Consulted the String indexing contract
and console editing documentation. Wren guidance used.

Remaining source questions: renderAllRows_ splits explicit newlines but does
not wrap to screen width despite claiming display-row units; long lines can
make scrollback paging and single-row moveRect disagree. drawPrompt_ has no
horizontal viewport or long-input bounds policy. The main runBody_ is guarded
by Fiber.try, but setup and restoration failures lie outside that guard.
User-defined toString failures occur after the eval fiber returns. These need
targeted event/failure tests before stronger runtime claims.

Updated remaining script coverage: wren_console.wren is now source-reviewed;
main_menu, full BBS/theme editors and their tests, syncterm.wren remainder,
and wrentest.wren remainder are still outstanding. Auxiliary run continues
to fail for the separately documented pixel binding and demo dependency gaps.

### Auxiliary UI scripts — coverage and integration gaps

Read complete auto/menu/menu_host_ui.wren (80 lines), menu_host_ui_test.wren
(33), ui.wren (30), and ui_demo.wren (627). Consulted the host-controller
ABI/status restoration contract. MenuHostUI delegates interactive dialogs to
independently pumped synchronous Apps; its progress pane deliberately draws
without running an input loop, leaving restoration to its C owner. Its four
existing width checks pass in the native headless auxiliary-scripts audit mode.

ui_demo.wren:30 imports transfer_app, absent from both scripts/ and the
embedded script list. Loading the demo source directly with the production
loader in an empty temporary script root fails on that import. The demo is
not embedded itself, so an initial bare import was insufficient evidence;
the regression harness now reads the actual shipped source. This is a low
demo integration gap, not proof that ordinary menu startup fails. The demo
also uses Screen.modalRun and Transfer operations, whose connected-context
requirements remain relevant after resolving the import. Recommendation:
make the shipped gallery's dependency/context requirements explicit and
ensure its supported invocation is tested when those integrations exist.

The auxiliary-scripts audit test reports status-width 4/4 pass and the demo
import failure. It does not invoke the interactive gallery or start a transfer.
Strict native Clang test compilation succeeds. ui.wren is a re-export module,
with no new state; it does not currently aggregate LogView/Progress classes
despite its broad "every public class" comment (documentation/layout concern,
not a runtime failure). Wren guidance used; only test harness and audit report
changed. These integration gaps remain distinct from numbered rendering
findings.

Picker-script coverage: read all ui_picker.wren (58 lines) and
auto/picker/file_picker.wren (650 lines), both identical to the legacy
scripts. Consulted Host picker and FilePickerOptions contracts and traced
accept/acceptAll/save disposition through wren_picker.c. ListPicker owns a
fresh App and chooses synchronous versus cooperative pumping explicitly.
PickerPane owns its selected-path list; review receives copies, and selection
changes mutate only the owner until completion. Directory refresh commits the
new listing only after a successful host list call. Mask rollback exists for
applyMask_; applyPath_ changes the mask before a potentially failing refresh,
so failed typed navigation may leave the displayed mask out of step with the
old listing. This last case is an untested UI consistency concern.

Extended the existing st_wren_picker_test with five headless event sequences
against the shipped picker: Ctrl+A/Ctrl+J multi-file acceptance, selection
followed by Escape cancellation, directory acceptance, explicit overwrite,
and explicit append. Checks assert completion, path count, returned entry
kind and save disposition. These tests return choices only; no selected file
is opened for writing. Existing build-tree fixtures and separate logical
consoles are used; no personal paths or physical window input. Native CMake
build succeeds and wren_host plus wren_picker CTest pass 2/2. An initial test
fixture incorrectly sent printable O as KEY_CODE, which has codepoint zero;
it timed out in the save-choice popup. Using the actual TEXT event contract
resolved the fixture error; it is not reported as a product defect.

Remaining picker checks: typed path and mask errors, multi-directory review,
tiny-window layout, and nested-dialog cancellation require more coverage.
File existence can change after listing; explicit save disposition conveys
the selected operation, not a filesystem reservation. Source review is not
proof of downstream file-open race handling. No new confirmed finding in
this pass. Wren guidance used; only tests and this report changed, no commits.

Log/scrollback script coverage included ui_logview.wren, its test suite and
menu_scrollback.wren. The offline viewer copies source rectangles, bounds its
source coordinate lookup, and resets hyperlink hover after normal modal
completion. Exception cleanup of screen/mouse state remains a source-level
question; completed-session integration was not established by this audit
pass. LogView capacity validation also remains an unclassified source concern.

Sort-profile script pass: read full menu_sort_profiles.wren (243 lines),
compared with legacy (identical), consulted the owning-editor persistence
contract, and traced delete/save through menu bindings to the settings model.
This module is now included in script coverage. Added read-only helper tests
for all catalog field labels (including default descending direction), signed
order comparison and empty profile-list add-row construction. Native isolated
menu-suite passes 7/7; no profile mutations or configuration writes performed.

Edit ownership agrees with the manual: fields edit a copied order, then update
the working profile on leaving that editor; Escape is not rollback of already
accepted field edits. Clipboard copies the order list, avoiding aliasing with
later profile edits. ASCII case folding checks name uniqueness; name-byte and
field validity ultimately belong to the binding/model. Save failure displays
an alert and exits with working state retained, rather than silently reverting.

Source usability concern: F6 paste is registered with allowOnBlankRow=false,
so the blank append row refuses paste even with a clipboard. This prevents
direct append via paste; evaluate desired insertion behavior and the model's
minimum-profile rule before broadening the claim to empty-profile recovery.
Cut stores the clipboard before deletion, so a failed deletion behaves as copy
plus an error, not data loss. No separate runtime defect asserted here.
Negative field signs are preserved by sameOrder_.
No production changes or commits.

Settings-menu source coverage completed: read remaining lines 1..651 and
936..937 of menu_settings_ui.wren. All 1274 lines now reviewed, including
local/cloud theme catalogs, preview/selection, deletion confirmation, safe-mode
menu guards, and descriptive help. Script remains identical to legacy.
Executed menu_ui_test via new menu-suite runner mode: existing retained-view
and browser-height tests plus new theme identity/name-disambiguation, installed
versus online filtering, and ASCII-only namesEqual checks pass 5/5. Fixtures
are in-memory lists; no download, theme selection/deletion, personal file or
GUI operation performed. menu_ui_test source/runtime coverage is now recorded.

Theme preview routing: row movement previews a valid installed entry, cancels
preview on unavailable/update-needed entries, and replaces app.theme with
Theme.current. Accept calls the appropriate select binding and retains the
browser on an error string. Cancel/direct commands restore preview before
return. Script-level behavior agrees with the previously tested MenuUi browser
callbacks, but host rollback-failure paths and real rendering remain untested.
Online list deliberately contains only not-yet-cached entries, so its preview
branch normally cancels rather than fetching; downloads happen on acceptance
or copy. Catalog helper tests validate identities independently of display
names. Local file/package deletion requires confirmation and is excluded in
safe mode; cloud-refresh/cache permissions are host policy, not established
by those menu guards.

Remaining integration questions: synchronous cloud actions bracket popStatus
without exception cleanup, and callbacks restore app.theme only on normal
return. Safe-mode theme-document editing still reaches the editor while host
bindings must prohibit persistence. Output-mode help is stale (describes ANSI
as CP437 and omits newer backends); keep that distinction from actual backend
behavior. Web-list names use ASCII comparison, while the host validates their
cache components. Screen-mode geometry concerns and broad
settings-save/reload semantics remain those recorded in the binding audit,
not resolved by reading the menu. No new production changes or commits.

Settings-menu partial pass: read menu_settings_ui.wren lines 652..935 and
938..1274 (font help/management/details, program/classic-color/custom-mode
settings, audio, file-location/build displays, web lists and encryption).
Full script comparison with legacy has no differences. Earlier sections
1..651 and the small 936..937 boundary still need complete reading; this is
not full module coverage. Also read complete menu_ui_test.wren (51 lines),
whose two retained-view/height assertions have not yet been run independently.

Custom-font workflow has no live font preview to restore: edits target a
per-host draft and save happens on leaving Font Management. The script
re-fetches handles after structural edits; host generation checks reject stale
handles. Safe mode suppresses entry and the font mutation bindings
independently reject writes. Names are validated before mutation, and picker
paths are stored without prematurely validating bitmap size; a failed path
assignment now receives menu feedback.

font_ releases focus before Host.pickFile and restores only after normal
return; exceptional picker restoration remains an integration question.
Font save failure displays an alert then returns from management with the
in-memory dirty state retained; no transactional-discard claim is made.
Program settings call applySettings_ after edits; broader cancellation/save
behavior needs isolated model tests. No font files, personal configuration or
production code modified; report-only this pass. Font model API manual and
related host functions were rechecked.

MenuUi pass: read complete scripts/menu_ui.wren (696 lines) and full legacy
diff. This module is now included in script coverage. Modern changes are
selective Unicode width measurement; browser label widths still use .count.
Main responsibilities are mapped choice values, callbacks before dismissal,
browser preview/cancel hooks, standalone synchronous dialog ownership, and
retained ListView state across repeated choices.

Extended ui_popup_test with browser acceptance, rejection, Escape and direct-
command cases using the existing fake App. All four assertions pass: mapped
row values are returned, rejection keeps the pane open, acceptance omits the
cancel hook, Escape calls it once, and a direct command calls cancellation
before returning its command/value. These are direct synchronous event tests,
not a real App loop or disk-backed menu session. Existing nested-choice
callback test also remains passing.

Unresolved failure contracts: browser cancellation sets its closed flag before
calling external onCancel; an abort there leaves a resident pane whose later
cancel callback becomes a no-op unless App error cleanup discards it. Whether
that pane was opened before or during the failed dispatch matters and needs
an App-level test. commandChoice's finish checks whether its pane remains
anywhere in the stack then pops the top, assuming callbacks have completed
their nested modal work before returning. Clarify asynchronous child ownership
before classifying this as a defect. Browser choices expect paired rows while
retaining branches for unpaired rows; caller usage needs validation. Standalone
wrappers deliberately use runSync; their shutdown/error paths remain part of
host integration coverage. ASCII-only namesEqual uses explicit byte folding,
not locale. No production changes or commits.

Menu palette picker pass: read all 240 implementation lines and 225 original
test lines, full legacy comparison (identical), ColorPicker manual's palette
integration description, menu_bbs_editor's editColor_/editPalette_ callers,
and App.modal/dispatchSync_ failure cleanup. These two palette-picker modules
are now covered; only the cited sections of menu_bbs_editor are covered by
this pass, not the complete editor.

The separate runAudit() test entry catches each original check's abort as a
failure without altering run() or assertions. Navigation, focus propagation,
compact bounds, repeated short palettes, sample selection and pane commands
are covered without a real modal loop, desktop, palette installation or
persistent settings access.

Ownership/failure trace: PaletteColorPicker keeps candidate RGB in its own
ColorPicker and only returns it after accepted commitEdits. ContrastPreview
borrows colors but reads it, substituting the candidate at the selected entry;
it does not modify the caller's list. editPalette_ writes colors[picked] only
after choose returns non-null. Therefore a draw abort before return does not
apply that candidate through this path. App's synchronous dispatch catches
the enclosing handler failure and discards newly pushed modal frames. These
are source-level conclusions, not an end-to-end failed-dialog test. The outer
editor does eagerly pad a short palette to its minimum before opening its
choice UI, which is separate from candidate acceptance and warrants inspection
with the editor's draft/persistence flow. No claim that every failure in that
flow is transactional. Invalid/empty palette entry arguments and dimensions
below preferred size remain caller-contract questions. No production edits
or commits.

ColorPicker pass: read all 552 production lines, all 263 existing test lines,
complete manual section, full legacy diff (script identical), and related C
Color bindings plus their legacy implementation. Both script modules are
included in source coverage. The color-suite isolated audit runner provides
an independent entry point for these checks.

Source observations, not additional confirmed findings: RGB is retained as
canonical state and HSV rounding is intentionally lossy; no arbitrary RGB/HSV
exact-roundtrip assertion is appropriate. Greys/black retain undefined HSV
components explicitly. ColorHexInput filters keyboard characters but inherits
bulk paste, so malformed pasted text must be rejected at commit (parseHex
does enforce six ASCII hexadecimal bytes). syncChildren_ assigns the whole
hex value on each valid edit, resetting its cursor; middle-of-field editing
needs a focused test. Layout assumes the preferred dimensions, and per-column
ramp painting creates a Style and calls host color conversion for each cell;
performance has not been measured. No production changes or commits.

Markdown pass: read all 575 lines of ui_markdown.wren, complete legacy diff,
and the documented Markdown subset. No dedicated ui_markdown_test.wren exists;
extended ui_help_test.wren exercises the parser via the isolated headless host.
This module is now included in script coverage.

Further source concerns: a single word longer than the viewport is emitted
unsplit and Help clips its suffix with no horizontal scrolling. Definition
terms are measured against a cap but their prefix is not clipped/wrapped;
at narrow widths the continuation indent can consume the complete viewport,
leaving descriptions invisible. Need explicit overflow-policy tests before
classifying these separately. Parsing/coalescing repeatedly concatenates strings
and closing-marker searches rescan suffixes; worst-case cost needs measurement,
not an unsupported performance claim. CR handling is explicitly LF-only in
source; normalization responsibility needs documentation review.

Help pass: read full ui_help.wren (350 lines), original ui_help_test.wren
(297 lines), matching manual section and Markdown-subset introduction, and
complete legacy diff. Both modules are now included in script coverage.
Tests exercise keyboard dismissal/scrolling, preformatted hard lines,
individual wheel/click/drag events and scrollbar drawing. No actual modal
pumping or screen text selection is established by directly injected events.
The documentation says any other key dismisses, but process-close Key.quit
deliberately falls through and its test passes.

DatePicker pass: read complete ui_date_picker.wren (299 lines), original
test module (211 lines), and full matching manual section. Implementation is
byte-identical to legacy. These two modules are now included in script
coverage. Added date-suite to strict native Clang isolated host runner and
tests spanning all 146097 days of 1600..1999 against sequential civil dates,
both navigation limits, and negative UTC timestamps. Those tests pass.
The expanded suite covers title, weekdays, neighbor and selected styles in
addition to the arithmetic checks; no separate calendar arithmetic defect was
established by the former legacy-attribute mismatch.
Default-font/default-theme headless only; no desktop or locale dependence.

Contract questions: setDate/moveDays/moveMonths accept fractional and non-finite
numbers without explicit rejection, despite a calendar's integral domain.
No malformed-input application failure asserted. Six-row grids at years 1
and 9999 can display neighboring dates outside the selectable range, which
setDate clamps; clarify intended edge presentation. Host mouse translation
currently produces press and click but no double-click classification, so the
script's directly injected double-click test cannot establish real host
double-click acceptance. This integration gap requires broader input review.
No changes to calendar implementation or documentation.

MenuBar pass: read complete implementation/test module, corresponding manual
section, full legacy diff, and relevant host/App mouse dispatch. Both script
modules now included in coverage. Existing assertions exercise keyboard
wrap/Home/End, ASCII hotkeys, individual clicks/gaps, focus callbacks and basic
drawing.
Remaining source concerns: mutable items can bypass layout-cache invalidation;
layout always measures font zero while painting uses role-specific fonts;
focus setter accepts fractional indices. These require contract/custom-font
tests before separate findings. No desktop interaction is claimed.

Form pass: read full ui_form.wren and ui_form_test.wren, the matching manual
section, and the complete legacy diff. These two modules are now included in
script coverage. Layout measures labels using font
zero but painting uses the selected style font; that shared width-consistency
question remains untested for custom fonts. fields exposes the mutable backing
list despite a source comment calling it read-only. rowH/rowGap assignment
does not itself re-layout/dirty; the source instructs callers to assign bounds
to recompute, whereas the manual is less explicit. Very small bounds can leave
zero-width fields or buttons below/outside the form; no viewport/scrolling
guarantee is documented, so this is a caller-layout concern pending usage
review, not a separately confirmed defect. No production changes or commits.

TextInput pass: read complete ui_input.wren (469 lines), original test module
(554 lines), TextInput manual section, and full legacy diff. Both modules now
included in script coverage. The diff adds font-dependent widths to cursor,
paint, mouse positioning and visibility calculations. Existing array-of-
codepoint editing, shared insert mode and selection semantics are retained.
The width-aware tests require an explicit wide-font fixture; without that
fixture, a failed width precondition is not evidence of a product defect.

The suite does not touch the desktop clipboard: right-click uses PasteProbe,
and paste-content checks call insertText_ directly. Native clipboard transfer
and callback-abort repaint behavior remain untested. Array cursor setters
accept fractions, and value=/maxLen= do not enforce an existing-value cap;
these need domain/contract clarification rather than automatic remediation.
The manual's cursorAttr description says focused style although
the code deliberately uses fillStyle_. Full grapheme editing is not claimed
or treated as required here. Display-width methods repeatedly resolve style
and cross the host boundary per character; no performance regression is
asserted without comparative measurement.

Popup pass: read all 731 implementation lines, all 558 original test lines,
the popup manual through its layout/standalone introduction, and compared the
complete implementation with legacy (identical). Script coverage includes
ui_popup.wren and ui_popup_test.wren; imports ui_input/ui_help/menu_ui are not
thereby marked reviewed. Tests cover fake-parent dismissal, prompt editing,
layout and MenuUi choice interactions. Strict native Clang headless execution
only.

Source questions requiring followup: preformatted= does not mark the cached
surface dirty or re-layout a popup changed after its first draw. Find sizing
uses byte count rather than font-dependent column widths. Very small consoles
can force button rows/input fields beyond their content bounds; tall ordinary
Popup messages are not explicitly clipped to the content rectangle before
drawing. These are not yet separate reproduced findings. Confirm's close-button path returns
null rather than the false used by Esc; the manual specifies Yes/No/Esc but
does not define that path, so clarify the return contract before calling it a
defect. Standalone asynchronous driver cleanup remains part of App integration
review, not established by FakeApp tests.

Progress/StatusBar pass: read complete ui_progress.wren (157 lines),
ui_statusbar.wren (118 lines), both original test modules (224/114 lines),
the StatusBar manual contract, and complete production diffs against legacy.
The production differences are Unicode display-width measurement, not changes
to progress arithmetic or segment priority. ProgressBar/ProgressText have no
dedicated widget section in the current manual (the progress-overlay host
contract is separate). These four modules are now included in script coverage.

Extended the isolated headless host runner with progress-suite/statusbar-suite.
Original suites pass 16/16 and 9/9 on native Clang. Added tests for decreasing
multi-row progress, clearing after total becomes null, replacing and recentering
shorter progress text, switching segments to shorter text, null-text clearing,
and competing segments at narrow width. Expanded suites pass 19/19 and 12/12.
No personal settings, desktop interaction, production changes or commits.

Remaining questions (not confirmed defects): segment and progress-lines lists
are retained by reference, so caller mutation without reassigning/markDirty
can bypass the widget cache; clarify whether that is permitted by the API.
Progress arithmetic multiplies before dividing and does not reject non-finite
inputs; realistic transfer sizes do not approach floating overflow, and no
application failure is asserted. Retained-list mutation and non-finite
arithmetic remain outside the ordinary finite-value layout fixtures.

RadioGroup/SpinBox pass: read complete implementations (199/161 lines), test
modules (197/206 original lines) and matching manual sections. Both production
scripts were byte-identical to legacy at audit time. No GUI or external input.
The radio suite covers cursor-versus-selection,
wrapping, edge fallthrough and callbacks; the spinbox suite covers valid range
clamping, arithmetic steps, keys, individual mouse events and basic drawing.

Open numeric-contract questions: SpinBox permits min>max, negative/zero steps,
NaN/non-finite values and non-integral values without explicit validation.
RadioGroup similarly does not constrain assigned indices to integers. These
are not demonstrated application failures without identifying callers and
their expected domains. SpinBox handles both button press and completed click,
which may increment twice for one physical gesture when both are delivered;
existing tests inject only a single event. Verify host/backend event sequences
before promoting that concern. RadioGroup's corresponding double event is
idempotent for an unchanged selection. Both callbacks run before markDirty,
sharing the earlier callback-abort cache concern. Scripts coverage now includes
these four modules.

Button/Checkbox pass: read both complete implementations (128/85 lines),
both complete test modules (141/147 lines), corresponding manual sections,
and full legacy diffs. Native isolated headless runs of the unchanged suites
report button 11 passed / 0 failed and checkbox 14 passed / 0 failed. The
strict C17 host-test runner has explicit button-suite and checkbox-suite
modes and reads only the corresponding repository test. No production or
build changes, desktop interaction or personal data access.

Compared with legacy, implementation changes concern Unicode width measurement
and Button centering/hotkey placement. Activation and callbacks are unchanged.
Neither control defines a separate disabled property: focus/visibility routing
is supplied by the parent, and direct handle calls need not check focused.
Their tests intentionally activate otherwise-unfocused objects, so that is
not a demonstrated defect. Button hotkeys explicitly fold ASCII only; the
visual hotkey index uses Wren String indexing, requiring a future non-ASCII
prefix test. Both intrinsicWidth getters measure font selector 0 rather than
the effective style font, so font-dependent sizing remains an untested concern.
Checkbox value= invokes its callback before markDirty; a callback abort may
leave changed state with a clean cache. No abort test performed here.
The button style test merely checks unequal attributes; it does not validate
exact attributes or RGB/legacy independence.
Scripts coverage includes these four modules; Unicode/custom-font layout,
hotkey routing and callback failure cases remain beyond their passing suites.

Read all 341 lines of ui_style_test.wren and ran it unchanged through the
isolated `style-suite` host mode: 35 passed, 0 failed. Covers Style composition,
equality, complete/null fields, dotted role inheritance, Theme.fromData,
default role completeness and glyph presence. It does not paint cached widget
trees or verify generation refresh after mutation. The native host suite runner was rebuilt
with strict C17 and warnings-as-errors. The scripts coverage inventory also
includes complete ui_style.wren and ui_style_test.wren.

Style pass: read all 223 lines of ui_style.wren and manual Theme/Style/Glyphs
and Cell sections. Style is an immutable four-field value with left-biased
composition; Theme walks dotted parent roles and a distinct inactive chain.
Theme.current caches by host generation; Theme.default is per-VM memoized.
Glyph caches retain both rich and ASCII resolutions. Legacy diff is limited
to removal of CP437 probing in favor of host-resolved Unicode fallback,
consistent with the approved direction; the manual still describes the old
CP437 restriction and probe. Theme's advertised immutability relies on callers:
roles and glyph maps are retained by reference, and roles is exposed directly.
External map edits can invalidate cached glyph resolutions. No mutation test
performed; treat this as an ownership-contract concern rather than a defect.
The separate ui_style_test module remains unread/unexecuted at this point.

ListView pass: read all 688 lines of ui_list.wren, all 590 lines of
ui_list_test.wren and the manual's ListView navigation/layout/search section.
The implementation is byte-for-byte identical to the legacy reference, so
concerns found here are not established extraction regressions. Navigation
clamps/wraps as documented, row click activation differs deliberately from
press/drag handling, and search folds only ASCII A–Z without locale calls.

Extended the isolated host test runner with `list-suite`, loading the unchanged
test module directly from the repository root. Native strict Clang build
succeeds; the suite returns 53 passes and 3 failures, all in expected legacy
style attributes: selected-row drawing, unfocused selection style, and focused
selection style. The fixture does not load settings. Whether these failures
are host initialization/theme assumptions or implementation defects remains
unresolved; they are not promoted to findings. The runner now prints failing
assertion labels from its VM log. No GUI, network or personal-list access.

ListView preferredWidth formats every item and measures bytes, not display
columns, and omits the tag marker from its returned width. Actual row painting
uses Painter's glyph-column widths. This needs explicit Unicode/tag-mode
layout testing. Each dirty draw fills the viewport then repaints all visible
rows, not just the changed selection; natural-width measurement scans the
whole collection, but ordinary navigation does not itself call that getter.
Do not infer the earlier large-list pauses from these costs without tracing
the concrete layout caller and measuring. Exposed items are a mutable list;
mutating it without items= can desynchronize tag storage and selection. The
API's mutation ownership expectation needs clarification from consumers.
The existing suite does not test search/tag modes or preferred-width Unicode
behavior. Scripts inventory coverage now also includes ui_list and its test.

App pass: read all 808 lines of ui_app.wren and its manual sections. The
synchronous dispatcher catches each key/mouse handler in a child Fiber,
removes newly pushed modal frames after an abort, and preserves already
dismissed frames. Quit requests forcibly unwind one ordinary modal even if
its Escape handler aborts, while atExit modals keep ownership. This does not
protect drawAll_, layout callbacks, setup/teardown, or an aborting onError
callback; those paths can still bypass mouse/cursor restoration. Their wider
failure handling remains a source-level concern, not a reproduced finding.

The audit-only runAfterCursorAudit entry point in ui_widget_test.wren invokes
the 20 checks after the cursor cases with fresh counters. The original run and
assertions are unchanged. The isolated `widget-tail` host-test mode covers key
routing, modal quit/error cleanup, Backspace/Escape aliases and mouse
ownership. No GUI or connected-session execution is required.

App draws cached widget surfaces but rebuilds its complete screen-sized
composition from the captured backdrop and submits Screen.putRect each loop.
The root's children are composited directly, avoiding its own stale-background
cache; nested plain Containers retain the earlier concern. Potential allocation
and full-frame comparison costs need measured evidence before assigning a
performance finding. Async run uses one claim and at most one pending tick;
runChild catches errors but returns its default null without reporting them,
and pending work is not explicitly canceled on quit. These async paths depend
on the not-yet-integrated connected host and were not executed here. The sync
menu path uses blocking input and deliberately does not support timer ticks.

Completed reading all 1047 lines of ui_widget_test.wren. The isolated runner
loads this repository test directly with wrenInterpret; it does not modify
the embedded-script manifest or require connected-session APIs. Build uses
the existing host-test link dependencies with strict C17 and Werror. Run
from the repository root with a fresh temporary directory and `widget-suite`.
The runner performs no GUI interaction or personal-file access.

Widget/Pane pass: read all 619 lines of ui_widget.wren, all 389 lines of
ui_pane.wren, ui_widget_test.wren through line 355, and the corresponding
Widget/Container/Pane manual sections. A complete legacy comparison shows
ui_widget.wren is unchanged. Pane differs only in using Screen.textWidth for
title measurement and replacing byte/string slicing with Painter's cell-width
cap. Thus potential widget issues below are inherited concerns, not established
modernization regressions. No script suite was executed in this pass.

Ownership is a parent reference plus mutable child list; add/remove maintain
ordinary parent/focus state, but there is no cycle, duplicate-child or
reparenting guard and the children getter exposes the list directly. Whether
callers violate the tree invariant remains untraced. Rect is immutable but
does not validate integer or nonnegative dimensions; tiny Pane fit limits can
produce negative inner bounds passed onward to children. Surface creation's
validation is not equivalent to a layout-level small-screen policy.

Redraws bubble upward while active-layer changes are detected during draw.
Widget.theme= does not invalidate descendants, unlike the separately tested
App theme change. Container composition retains its old background, so hiding,
moving or removing a child may leave old pixels unless its caller rebuilds
that background. Both need focused runtime/caller tests. Focus ring traversal
filters focusable but not visible, whereas spatial traversal filters both;
direct focusedIndex assignments accept out-of-range values. The spatial
algorithm uses only the primary-axis distance, contrary to the manual's
Manhattan-distance description. These are contract/design follow-ups, not
new confirmed findings. The existing test Probe overrides draw and bypasses
Widget's actual surface/cache path, so its dirty-state checks alone cannot
prove correct compositing or descendant repainting.

Read complete ui_draw.wren (287 lines) and ui_draw_test.wren (417 lines), plus
Wren.adoc's Painter contract. Primitives mutate retargeted Cell views and bulk
fill through the host instead of allocating one Cell per glyph. They perform
no Screen writes. Existing tests cover ordinary text, wide glyph placement,
character-only truncation, frames/titles, single-cell scrollbar inverse and
Pane layout behavior; this pass read but did not run that script suite. The
top-level wrentest module also imports connected-only APIs and cannot be
treated as a ready-to-run menu-host suite. Only its initial imports/setup were
inspected, not the full suite.

Remaining drawing questions: negative initial coordinates break line/text
loops rather than skip clipped prefixes; frame/title helpers assume border
glyphs occupy one cell; title width uses font 0 when the style inherits a font,
which may not match destination cells; scrollbar drawing does not clamp its
input scrollTop. These require caller-contract/reachability checks before
being promoted to findings. Pane implementation itself remains unread by this
pass.

Wren VM integration boundary: read CMakeLists, README, MIT license, common
configuration, optional Meta implementation, and selected constructor/compiler
and host REPL calls. Full recursive comparison with the read-only legacy
wren/ tree shows only explicit void parameter lists, void-pointer printf
casts, removed extra macro semicolons, and the new CMake file (before adding
the audit-only tests directory). No substantive interpreter fork was found.
Public version remains 0.4.0. The vendored core and optional generated Wren
includes match legacy; this is not an independent upstream provenance check.

The VM uses strict C17 with computed goto disabled, both optional modules
enabled by default, and NaN tagging enabled without a representation probe.
The host's REPL deliberately includes private VM headers and directly accesses
apiStack, modules and fiber frames. Its compile return placement matches the
vendored Meta implementation and does not allocate between receiving the
closure and rooting it in the API slot. This remains version-coupled code,
so upstream updates need explicit integration review. Existing host tests
cover REPL compilation and caught traces, not all possible private-layout or
representation configurations. No alternate NaN-tagging configuration tested.

Allocation failure is a dependency limitation: wrenNewVM immediately memsets
its allocation without checking for NULL, despite the host checking its return
value. Default allocations use realloc, and later VM allocations also assume
success. Host null checks therefore do not establish graceful VM OOM handling.
No OOM was induced and no production allocator change is proposed by this
audit; this inherited vendor behavior should be stated when describing host
failure guarantees. Vendor implementation internals beyond these integration
points have not been exhaustively reviewed.

Completed reading all 862 lines of st_wren_host_test.c and all 365 lines of
scripts/syncterm_menu.wren. An independent read-only signature comparison
expanded C BIND_GETSET entries and normalized Wren parameter lists: all 286
foreign declarations have matching class/static/signature bindings, with no
extra C entries. This establishes binding presence, not correct behavior.
Several were intentionally stubbed (offline scrollback and cache); their
advertised semantics still need consumer/legacy comparison.

Native CMake build of st_wren_host_test succeeds and CTest wren_host passes
(1/1, 1.12 seconds). It uses build-tree fixture files, headless output and an
ANSI discard callback; no desktop or actual terminal interaction. It covers
menu selection, settings setter/apply happy paths, theme/font/sort collections,
encryption transitions, REPL error recovery and style restoration, log ring
eviction, Cell retarget lifetime and control-key routing. It does not exercise
physical backend behavior. Its web refresh case deliberately
uses an invalid URI and verifies restoration of one cell, two palette entries
and cursor, not a successful live download or full-screen restoration.

Settings/BBS binding pass completes source reading of wren_bind_menu.c
(including the full binding table). Binding-to-script declaration parity and
the complete host test remain pending. The Settings manual was checked before
this pass. Setters mark dirty even when assigning the existing value, and
applying scaling ignores native setter errors.

Menu theme/font pass extends wren_bind_menu.c coverage through :2528, plus
the previously read :3360-3480. Read Wren.adoc Menu Themes and Custom Fonts
sections. ThemeDocument foreign objects own their documents and release them
on VM finalization; field/type validation delegates final domain checks to the
theme library. Draft edits stay in memory, and save explicitly returns the
read-only code in safe mode. Selected-theme save ignores active-theme reload
errors, so persistence success does not prove the displayed theme refreshed.
Theme deletion first persists Classic selection; a later deletion failure
rolls back only in-memory settings/theme, not the already saved INI selection.
This failure-order concern is source-only and still needs injection tests.

Cloud refresh/cache/select can perform cache writes without a safe-mode gate;
selection only gates settings_save. Whether cache updates are an intentional
safe-mode exception needs an explicit policy/legacy comparison. No browser,
external command, or cache update was launched during this audit.

Menu binding read coverage extends through :1525, in addition to :3360-3480.
Build-options data distinguishes compiled video/audio backends from unavailable
features and reports the Unifont-derived binary license option. Audio choices
use runtime availability; settings UI/backend-policy completeness remains to
be assessed with scripts.
Theme selection snapshots settings, applies selection, skips settings_save in
safe mode and attempts rollback on failure; rollback errors are ignored. Read
preview/catalog helpers as well, but full theme-edit integration and mutation
failure paths remain pending. No actual audio device or theme change was made.

URL parsing uses counted strings, rejects embedded NUL, handles bracketed hosts,
bounded user/password/address spans and explicit numeric ports. It does not
percent-decode credentials or generally reject unrecognized schemes; legacy
parity remains pending. Explicit CLI connection types override defaults before
scheme parsing, and profile replacement occurs only on success. Menu's default
port binding returns 23 for MQTT while quick-connect's helper returns 8883;
consumer and legacy expectations need reconciliation.

Core binding source read is complete (remaining :1740-2340 covered this pass).
Reviewed Cell and CustomCursor manual contracts plus Host.print and Format
descriptions. Cell foreground/background RGB tags are separate from flags.
Font assignment reruns Unicode lookup and drops
explicit raw-glyph state, which needs a raw-cell/font-order test before claiming
preserved semantics. Explicit full-cell font mutations remain distinct from
the screen write edge's legacy selector resolution.

Clipboard getter frees text with the owning library's callback; setter uses
C-string input and ignores write errors. Host.print intentionally writes and
flushes stdout, as documented, and is distinct from System.print's VM-local
log.

Theme serialization places lists/strings in Wren slots before later allocating
objects. UTF-8 encoding assumes validated theme scalar values; producer validation
was reviewed with theme but malformed in-memory inputs were not injected here.

The source-complete core pass is not complete cross-component verification:
screen/Cell constructors, GC pressure, full-cell/wide glyph mutation, host error
unwinding and script use still need integration coverage. No missing bindings
were implemented or contracts changed to match existing code.

Core screen/surface pass extends read coverage through :1740 (now :1-1740
and :2340-2540 read). Coordinate clipping uses int64 intermediates on bounded
int32 inputs, accounts for negative source/destination positions, and clips
source/destination extents together. Surface Cell retargeting retains new
storage before releasing old storage and changes only its index when already
sharing storage. Full-cell screen blits deliberately bypass glyph lookup.
Window mutators often discard console errors, unlike snapshot and rectangle
methods; consistency should be addressed only after documenting the intended
script failure contracts.

Source-only follow-ups: Surface.fill directly overwrites cells without the pair
repair used by other mutations. ScreenFonts setters only change context slot
numbers, not console selection; how scripts consume those values must be
traced before assigning a defect. Palette binding remains limited to 0..255
despite the native console's 16-bit palette; its intended script boundary
still needs reconciliation. Cell.ch
decodes the first codepoint but does not reject trailing codepoints despite
its error wording; inspect documented Cell semantics before classifying.

Core binding first pass: wren_bind_core.c:1-810, :1430-1520 and :2340-2540
read, with Wren.adoc Screen, Surface constructor/access/iteration and Input
contract sections. This is partial coverage, not the complete core binding.
Surface storage uses explicit reference counts shared by Cell views; lifetime
review of mutation/retarget/blit methods is still pending. Surface.new fills
cells from current style with spaces, whereas the manual describes zero/NUL
initialization; legacy comparison and consumer expectations remain unresolved.
Mouse event construction derives pixel positions from cell positions, does
not preserve source modifiers, and drops simultaneous secondary button changes;
these require event-contract/legacy tests. Timed input computes a fresh deadline
after each ignored event, so continuous ignored events can extend the timeout;
no timing reproduction performed yet.

Numeric regression build: native Clang C17, include the same component headers
as st_wren_host plus name_value_list/str_list/array_list, link the existing
st_wren_picker_test dependency archive sequence and instrument the included
core source with UBSan/float-cast-overflow. Both numeric test modes terminate
with the precise sanitizer failures above. Only the included core source is
instrumented; this is not a claim of a fully sanitized dependency build.

Picker completion/lifetime pass: remaining wren_picker.c sections now read,
completing all 1215 lines. Also read scripts/syncterm_picker.wren and reserved
picker_bootstrap.wren completely, plus the File method/write-consent manual
sections. The request stores only a generation, not a retained pointer to the
stack call; access checks the host's active call and generation. Completion
copies NUL-checked paths into call-owned absolute strings, rejects duplicate
accepts and mismatched completion modes, and disposal frees all selected paths.
Path handling is lexical; the trusted picker UI is responsible for confirmation
and existence policy. Case folding/sorting and wildcard matching operate on
ASCII bytes without locale dependence; '?' therefore matches a byte, not a
Unicode character. Directory masks currently filter files, not directory rows,
despite the option documentation mentioning both. Compatibility significance
of those matching differences remains to be tested against the legacy UI.

The return File object owns a copied path and disposition. token returns null,
isOpen returns false, and open/read/write are not bound in this host. The menu's
theme-install path consumes st_wren_file_read_path instead. This is a limited
path/consent adapter, not the complete File API described in Wren.adoc. Do not
claim implemented one-shot open/write enforcement from storing a disposition;
those methods are absent. The intended menu-vs-connected API subset requires
documentation reconciliation, not speculative implementation during the audit.
Multiple-file conversion skips any push_file allocation failure rather than
reporting failure for the whole selection; allocation-failure testing remains
outstanding. Caller-supplied picker initial/mask/title strings use C-string
accessors, unlike the request methods' explicit NUL rejection.

Potential reentrancy concern to verify: st_wren_picker_bind_method is consulted
for both VMs and exports Host.pickFile/pickFiles/pickSavePath without checking
the calling VM. A picker script invoking one could reenter the already-running
picker VM and replace host->picker_call; no active-call guard is visible in
st_wren_picker_host_run. This has not been executed and is not yet promoted to
a confirmed finding. The picker overrides are trusted local code; no remote
script reachability is claimed.

The complete st_wren_picker_test.c is now read. Native Clang target rebuild
and CTest wren_picker pass (1/1, 0.25 seconds). Coverage includes separate VM
font/mouse state and cells, successful file selection, Escape cancellation,
preserved menu cells, and injected menu-restoration failure reaching the caller.
It does not establish all multi-select/save-consent paths, stale request
rejection, recursive entry, malformed strings, GC/allocation failures or
Windows runtime behavior.

Picker filesystem pass: wren_picker_fs_posix.c and wren_picker_fs_windows.c
read completely. Both append into caller-owned vectors with checked geometric
growth; names become vector-owned only on successful append. POSIX uses strict
UTF-8 validation to skip unrepresentable names, follows filesystem metadata and
filters to files/directories. Windows uses extended wide paths and UTF-16
conversion, checks FindNextFile termination errors, and frees its search/path
resources. FILETIME conversion handles pre-epoch dates with negative floor
seconds; maximum input remains within int64 seconds. Windows execution was not
performed in this pass.

Windows hidden-file policy concern: the adapter discards FILE_ATTRIBUTE_HIDDEN,
and shared fn_request_list classifies hidden entries solely by a leading dot.
Thus a Windows hidden file without a dot is not filtered by the default path.
The documented showHidden option implies broader semantics, but Windows runtime
and legacy hidden-file policy comparison remain outstanding. Windows reparse
points also differ from POSIX stat-followed classification and need tests.

Picker shared-source coverage added in this pass: wren_picker.c:1-145
(request/string/path primitives), :250-360 (vector ownership/list serialization
and request setup) and :448-552 (listing consumer). This is explicitly partial,
not completion of the 1215-line file. Read Wren.adoc FilePickerOptions alongside
the earlier picker trust-boundary section.

Wren host entry/lifecycle pass: read st_wren_host.h, wren_host_internal.h,
wren_host.c, CMakeLists.txt and GenerateEmbeddedWren.cmake completely. Read
Wren.adoc introduction, script directory/module naming, override/load order,
and VM trust/lifetime sections before assessing their implementation. The
public run_menu contract explicitly returns a profile to a **future**
connection layer; this establishes that app's current selection-and-exit path
is a known integration boundary, not independently proven accidental loss.
Documentation nevertheless describes connected APIs/lifetimes not implemented
by this host; full documentation-scope reconciliation remains pending.

Host construction borrows the copied config's pointer members, registers fonts
in the supplied consoles, initializes separate menu/picker contexts and VMs,
and publishes the host only after initialization succeeds. Destruction releases
call/class/log handles before freeing each VM, then list/theme/profile state.
The public comment explicitly covers console/paths borrowing but not every
pointer field or owner-thread/reentrancy requirement; caller contract review
remains open. Font registration side effects survive failed host creation in
borrowed consoles; failure atomicity is not promised. Repeated custom-font
reloads register fresh font copies and preserve historical identifiers; LRU
replacement is intentional, but selector reverse-map behavior still needs
binding-level review.

Module names are restricted to ASCII letters/digits/underscore; loaded source
is capped at 4 MiB, NUL-terminated and freed using the VM load-complete hook.
Read errors fall through to embedded content rather than being distinguished
from absence. Embedded NUL handling needs further tests. The embedded
generator encodes bytes as bounded adjacent hex string literals, writes only
the build-tree output supplied by CMake and attaches explicit script/generator
dependencies. No generator edits or regeneration were performed this pass.

The Wren skill and its referenced notes plus WREN_GUIDANCE.md were read for
this pass; repository manual/guidance takes precedence over conflicting range
notation in the external reminder. Only the opening 160 lines of host tests
and selected picker test/setup/run sections were inspected here; no claim of
full test or binding coverage follows from reading the host entry point.

REPL binding pass: wren_repl.c read completely with Wren.adoc's built-in
REPL and Console/LogSource/REPL contract sections. Log entries own bounded
8-KiB copies and optional VM handles; eviction and clear release both. Clear
preserves monotonic totals as documented. The 1024-entry capture ring is
separate per context and commit copies entries into that VM's ordinary log.
Unread/error counters are marked separately from clearing contents. Numeric
sequence validation rejects non-numbers, NaN/infinity, negatives, fractions
and values at/above the rounded uint64 limit before conversion. Eventual
loss of exact double sequence precision above 2^53 is a theoretical API
limit, not a practical reproduced fault. Cached triples are mutable Wren
lists returned by identity, so scripts can alter subsequent observations of
that cached view without altering the underlying text; compatibility/contract
significance remains unresolved rather than called a confirmed defect.

REPL compilation deliberately uses VM internals to return a closure without
reentering the interpreter, and caught-fiber trace output goes to the local log
without setting the host fatal-error state. Full REPL behavior also depends on
scripts/syncterm.wren and wren_console.wren, not yet covered by this C-file pass.
GC stress, allocation failures, mutable cached triples and malformed input
tests remain pending; existing integration REPL assertions are not yet fully
reviewed. The Console documentation's sample filters e[2] against a LogSource
value even though its documented and implemented source field is e[1]; this
is a documentation example discrepancy, not a change to the tuple layout.

Application startup/entry-point pass: st_console_startup.h/c, syncterm.c,
st_terminfo.h/c and the complete st_app_startup_test.c read. Startup translates
stored output-mode numbers into backend/presentation/fullscreen settings,
then applies command-line backend and row overrides. Mode geometry retains
explicit pixel dimensions (including non-text scanlines) and cursor ranges;
custom mode uses configured dimensions. The startup tests cover representative
SDL, curses, ANSI, VGA and Prestel configurations, not every table entry or
actual native window geometry. All-mode legacy-table parity remains pending.

The entry point's help/version/terminfo actions return before settings/path
initialization. Normal startup owns the display, menu/picker consoles and host;
cleanup disposes the selected profile, host, both consoles, display, settings
and paths in that order. Font-zero registration is explicitly checked for both
consoles. It calls setlocale only for LC_CTYPE at application startup; this is
not evidence of locale-sensitive parsing in lower-level libraries. The host
receives borrowed paths/title/console pointers whose enclosing lifetimes cover
host destruction. Host internal shutdown behavior still needs separate review.

Important integration limit: successful menu/URL selection only applies
profile overrides, sets result=0 and proceeds to cleanup. No connection or
terminal loop is invoked by app/syncterm.c. Record this as the current
application integration gap, not a newly proven regression: the connected
terminal/CTerm migration scope and host behavior need reconciliation. Likewise,
launch-script and safe-mode enforcement must be checked inside the host, not
inferred from populating its config. Ignored title/refresh results and settings
shutdown persistence need consumer-policy review.

Native Clang build targets st_app_startup_test and syncterm_app complete.
CTest app_startup, syncterm_help, syncterm_version and syncterm_terminfo all
pass (4/4). `build/app/syncterm -T | tic -c -x /dev/stdin` exits successfully;
tic warns that older versions may interpret the description as an alias.
This check does not install terminal entries, open a console or load personal
settings. The emitted base/bitmap capabilities match the inspected legacy
syncterm.c terminfo block; legacy description typos (80x23 for 42/43 rows)
are retained. Syntax acceptance does not prove emulator capability parity.
The writer checks fputs failures but does not own/close its stream; final
stdout flush error handling is not tested. No Windows/macOS application
runtime verification is claimed.

Application command-line pass: st_command_line.h/c read completely. Parsing
uses a temporary result (output unchanged on failure), borrows argv strings,
uses ASCII-only case normalization, permits attached/separate paths for
-B/-N/-W, and restricts row selections to the legacy mode set. Backend options
are attached and include ANSI UTF-8/CP437 distinctions. Single-argument -T/-v
are special actions; -T with further arguments selects telnet. Repeated
positional arguments overwrite the URL slot. Full legacy/manual comparison
and application consumption of parsed options remain pending. Registered
parser tests cover valid forms, an invalid suffix and exact-size bare -i
rejection.

Root build pass: CMakeLists.txt, syncterm_config.h.in, syncterm_types.h and
.gitignore read completely. Root declares C and uses a function-scoped strict
C17 compile/link probe for ssize_t and the configured public type header;
the Windows fallback uses SSIZE_T, while POSIX absence is fatal. Configuration
is generated in the binary directory. Root itself neither downloads data nor
runs target executables; component-specific probes remain covered by their
own audits. Optional Unifont controls both its subdirectory
and app linkage/definition. There are no root directory-wide compiler flags.
The build*/ ignore pattern is not anchored and also ignores such directories
below subdirectories, consistent with broadly excluding build trees.

Fresh native Clang configuration at /tmp/st-root-audit-20260918 with
BUILD_TESTING=OFF and SYNCTERM_ENABLE_UNIFONT=OFF completes successfully.
It selects Botan and probes POSIX capabilities; this is configure-only
evidence, not a new full build or cross-compilation run. Prior Unifont-disabled
application build/symbol checks are recorded separately. Application CMake
and Wren VM CMake were read as integration entry points but their broader
inventory rows remain pending. In particular, Windows DLL/launcher packaging
must be reconciled with app's executable target during application review.

Preparation for Wren review: read the Wren skill, its referenced SyncTERM
notes and repository WREN_GUIDANCE.md completely. The external skill notes
reverse the range endpoint notation compared with repository guidance;
repository Wren.adoc and actual VM behavior must resolve such conflicts.
No script or binding has been modified, and no Wren coverage is claimed from
this preparation. Audit-only authority prohibits editing the external skill
notes or production guidance as part of this goal.

Complete st_console_test.c and st_console_benchmark.c now read. The main native
Clang console test reaches and passes the former audit suffix: font rendering,
raster margins and scrolling, existing SAA5050 case, concurrent writers,
deferred presentation, wide cells, ANSI colour/CP437 output, font
fallback/copy/deferred registration, 65,536-slot LRU replacement, concurrent
activation/metadata, failed detachment and retained ANSI options. Isolated PTY
tests supply the previously documented curses coverage.

Wide-cell tests cover four-column wrapping and
single-cell overwrite, not one-column windows or partial-window scrolling
across wide pairs. Invalid scalar put_codepoint is explicitly expected to
succeed with UINT32_MAX in a no-font headless console; treat that as an API
contract consistency question rather than silently replacing its test oracle.
Snapshot tests require draining even an open nested batch to output and
propagate output failure; this narrows the earlier uncertainty about intended
snapshot behavior. LRU testing explicitly expects replacement without a
generation scheme, consistent with the user's stated requirement.

Benchmark workload: 200 full 80x25 writes within one batch measure cell-update
cost; 200 unbatched writes measure updates plus headless rasterization, not
isolated glyph cost. A further 200 wide-glyph frames and 2,000 scroll/last-line
writes use synthetic fonts and a 640x406 raster with six margin rows. Final
checksum is printed, not compared to an oracle. It neither measures a display
backend/scaler nor proves margin preservation (the separate correctness test
does that). There is no legacy implementation in this benchmark, so timings
cannot establish a comparative speedup/regression. Build optimization and
workload must accompany any cited timings. The existing native Clang debug
build (-g, no optimization flag in st_console flags.make) completed this pass:
cell update 1.446 ms/frame, update+raster 3.576 ms/frame, wide update+raster
7.093 ms/frame, scroll 252.524 us/line; checksum 14401940803765488640. These are
single-run diagnostic measurements, not release-performance conclusions.

Quartz implementation and st_console_quartz_test.c now read completely.
No macOS SDK/compiler/runtime validation is available in this environment.
The existing test checks explicit/automatic creation, two consoles, title,
pointer visibility, basic pixel presentation and logical resize only. It does
not establish text composition, physical input, logical-console switching,
fullscreen sequencing, monitor/backing-scale transitions or painted contents.

AppKit work is dispatched synchronously to the main thread, borrowing caller
command storage until performSelectorOnMainThread returns. With SDL built,
the SDL runner pumps Quartz; without SDL, a joined application worker is
paired with a main-thread event pump using release/acquire completion state.
Object retain/release is explicit. Views/delegates have their state pointers
cleared before closing and releasing windows. The runner does not destroy
remaining Quartz states before returning; destroying surviving consoles after
runner shutdown would skip the rejected destroy command and free C state
while AppKit objects still reference it. Whether the documented host lifecycle
allows that sequence remains an integration question, not a reproduced crash.

Rendering copies dirty pixel rectangles into owned storage, creates a
CGDataProvider/CGImage for drawRect, and relies on AppKit invalidation clipping.
Allocation checks for provider/colour-space creation are absent before their
use. Byte count is protected by upload allocation bounds. Backing-scale changes
adjust point sizes to retain requested pixel sizes. Snap and center use the
window's screen/visibleFrame, falling back to mainScreen for centering only.
No retina/multi-monitor or interactive-resize measurements were performed.

Input posts event.characters directly from keyDown rather than using a
NSTextInputClient implementation or composition callbacks. The view has no
marked-text storage. This requires real input-method tests before claiming
the same committed-text behavior as SDL/XIM. Modifier events toggle a local
boolean and focus loss clears it, so release-after-focus-change needs testing.
Physical mapping omits several keypad/international keys. Mouse conversion
uses signed floating-point coordinates; scrolling collapses every nonzero
delta to one step, with no accumulation.

Fullscreen commands return before animation completion; keyboard shortcuts,
command state and delegate state use different points for recording fullscreen.
Rapid enable/disable and rejected transitions are not covered. Source floating
to uint32_t geometry conversions lack explicit range checks for host sizes;
practical host bounds and error consequences remain unverified. These are
source concerns, not additional confirmed runtime findings. Remaining console
coverage is the complete core test/benchmark pass and targeted follow-ups,
plus application/Wren integration accounted for in their inventory rows.

GDI backend and st_console_gdi_test.c now read completely. The existing
MinGW build successfully builds/links st_console_gdi_test; no Windows or Wine
execution performed. Its test cases cover initial aspect-correct dimensions,
fractional initial scaling, two displays, Escape event deduplication, resize
and fullscreen roundtrip. They do not test logical-console activation,
multi-monitor placement, failed message delivery, DLL unload/reload, surrogate
input across focus changes or rendered pixel contents.

Each display has its own Win32 message thread. SendMessageW synchronously
borrows command storage; timer draws use presenter snapshots, and WM_PAINT
reuses retained DIB pixels. Source damage is transformed into invalidation
coordinates, with BeginPaint providing clipping. WM_SIZE still invokes
render immediately; no interactive-resize throughput measurements performed.
Fullscreen uses the nearest monitor's rcMonitor; center/snap use rcWork.
Client dimensions are obtained from GetClientRect rather than truncated
WM_SIZE fields. High-DPI integration remains pending with application setup.

Source-level lifecycle concerns (not new reproduced findings): send_command
ignores SendMessageW delivery failure and returns a zero-initialized command
result; worker GetMessage failure is not published to callers. If destruction
fails, gdi_destroy nevertheless joins. register_class uses the executable's
GetModuleHandleW(NULL), treats any same-name class as success, and never
unregisters it. Because the project intends DLL-contained implementation,
callback lifetime across DLL unload/reload needs explicit integration checking.
Do not infer a reproduced unload failure from this source observation.

Clipboard reads bound the UTF-16 terminator search by GlobalSize and transfer
allocation ownership only after successful SetClipboardData. Conversion
failures are sometimes reported as allocation or invalid-input errors without
preserving the underlying cause. Text input handles surrogate pairs and
suppresses duplicated control text, but pending high surrogates are not reset
on focus loss or logical-console activation. Keyboard repeat counts are
reduced to a boolean rather than replayed; scan-code portability (particularly
Pause/extended keys) needs actual event tests. Mouse capture is retained until
all buttons are released; wheel deltas smaller than WHEEL_DELTA are discarded
instead of accumulated. These are unverified behavior gaps, not tested fixes.

SDL source and st_console_sdl_test.c now read completely. Native Clang and
GCC st_console_sdl CTests pass with the registered dummy video driver and
empty DISPLAY/WAYLAND_DISPLAY. Coverage includes two displays, concurrent
pixel writers, quit distribution, geometry rejection, logical-console
activation/destruction and the real-main dispatcher wrapper. No accelerated
renderer, physical display, real input method or multi-monitor runtime tested.

The process-wide manager runs SDL calls on the dispatcher thread, while the
application runs in a joined worker. Per-display commands borrow caller stack
storage until signaled completion; the console core is responsible for one
in-flight command per display. SDL library and video/text-input lifetime
span the runner rather than each console. Events route by window ID, with
SDL_QUIT delivered to every managed display. Returning from application_main
with other threads still submitting console commands needs lifecycle-contract
verification at the host integration; the passing tests destroy consoles first.

Window/pixel units are separated for high-DPI sizing; monitor bounds come
from SDL_GetWindowDisplayIndex and usable bounds (ordinary bounds fallback),
with frame borders subtracted for snap limits. Mouse mapping widens before
subtracting destination offsets. Internal scaling uploads regions into a
streaming texture and redraws the retained texture on expose.
External scaling quality is selected when a texture is created; changing
scaling mode without changing texture dimensions does not recreate it.
SDL renderer reset events have no handling in dispatch_event/state_for_event;
device-loss and texture-reinitialization behavior remains untested.

Each resize/expose event calls draw_state immediately, unlike X11's coalesced
repaint flag. A burst can therefore perform repeated render/present calls
(potentially vsync waits), although current output size is queried rather
than replaying dimensions from each queued event. No comparative performance
measurement was made, so this is a risk to test, not a measured regression.
The loop handles one queued command before draining SDL events and iterating
every display; fairness under continuous input or many vsynced windows is
also not established by the dummy-driver tests.

Input uses SDL_TEXTINPUT for committed UTF-8 and physical scancodes separately.
translated_key does not include SDLK_KP_ENTER, although evdev_key reports it
and legacy conio/sdl_con.c:181 supplies Enter's translated value. Whether the
host's physical-key path compensates must be checked before claiming a broken
shortcut. Focus loss posts an event but does not clear the cached modifiers
attached to subsequent text; input-method/focus transition tests remain open.

Wayland backend/loader and st_console_wayland_test.c now read in full.
The existing integration test covers two displays, logical-console switching,
basic pixel submission/resize, fullscreen request success and pointer-visibility
requests without pointer entry. It does not assert rendered output, input,
clipboard exchange or protocol-version fallback. The compositor-independent
internal test covers mapped-buffer cleanup, busy-buffer presentation policy,
closed and backpressured clipboard recipients, connection-loss paths and
pending-command failure. No live compositor was available in this pass.

Worker lifecycle owns protocol objects, presents cached frames periodically,
and serializes caller commands with a mutex/condition and wake pipe. Caller
stack commands remain borrowed until completion; normal destruction submits
stop, joins, then destroys presenter and synchronization objects. The
documented startup contract waits for initial xdg configure without a client
deadline. Normal presentation does not block for buffer release: when both
shared-memory buffers are owned by the compositor, that attempt is skipped and
a later change presents the current retained state. Established connection
loss posts one close event and fails pending and later backend operations.

Protocol capability concerns: registry binding caps versions but does not
require compositor v4 before invoking damage_buffer (a v4 request). Multiple
seat globals overwrite one slot, removals are ignored, and seat_capabilities
only adds keyboard/pointer objects rather than handling removal. Input-repeat
logic does not query per-key repeat capability. These remain source-level
portability/lifecycle concerns, not compositor-tested findings. Fullscreen
tracks requested and configured state separately; a compositor changing state
independently may leave fullscreen_requested stale. The documented Wayland
positioning limitation is distinct from these implementation questions.

Input uses xkb modifier names and key-to-UTF32, not an input-method or compose
state. Repeat scheduling starts for every pressed key without querying the
keymap's repeat flag; modifier/lock-key behavior remains to be tested.
pointer_enter discards its coordinates, leaving a subsequent button event
dependent on the last motion position. No compositor or desktop input was
exercised.

X11 backend source and st_console_x11_test.c have now been read completely.
The event loop drains pending events before one draw_state call, and Expose
sets a repaint flag so the retained XImage can be re-emitted. A bounded burst
of synthetic ConfigureNotify events is tested for input responsiveness, but
the existing test does not measure rescale counts or sustained event floods.
The queue-drain loop has no budget; command starvation under a continuously
nonempty event stream remains an unmeasured concern, not a performance result.

Monitor selection uses the window center with XRandR monitors, Xinerama
fallback and root-screen fallback. Available geometry intersects that result
with the desktop workarea; centering/snap account for frame extents. Existing
multi-monitor checks are conditional on the test server actually exposing
multiple monitors. No multi-monitor run was performed in this pass.

The X11 worker owns Display/window/image and the console pointer. Synchronous
commands borrow caller storage until completion under a mutex/condition and
a wake pipe; shutdown submits a stop command and joins before freeing state.
Timer draws use cached presenter data rather than acquiring the caller's
logical-state lock. Startup failures join a started worker and release its
resources. XInitThreads is cached process-wide; interaction with other users
of Xlib before initialization or after unloading is not established here.

Clipboard review raises additional unverified integration concerns: the
nested selection-wait loop bypasses XFilterEvent, supports neither INCR nor
retrying XA_STRING after a refused UTF8_STRING request, and waits up to two
seconds on the backend worker. A monotonic clock failure returning repeated
zero timestamps would defeat that timeout. Tests cover small UTF-8 transfers
between two instances, not those cases. Default visual colour masks are used
without an indexed-visual path; legacy visual support comparison remains open.
No GUI test or clipboard operation was run on the user's display.

X11 input pass: console_x11_keys.c and its header read completely, plus the
backend's key_event/build_keycode_map, event dispatch and event-loop sections.
Physical keys prefer XKB key names, then an evdev-keycode heuristic, then a
keysym fallback. The fallback conflates dedicated navigation with keypad
positions and KP Enter with Return; impact is limited to missing preferred
mapping paths and is not yet runtime-tested. Modifier translation assumes
Mod1=Alt, Mod2=NumLock and Mod4=GUI; alternate server maps remain unverified.
Text uses Xutf8LookupString with an input context and suppresses duplicate
text for normalized special keys. Without a context it deliberately drops
non-ASCII bytes rather than assuming an encoding. XFilterEvent precedes
dispatch. Repeat release/press pairs are collapsed; focus loss clears the
backend repeat table. Existing key tests cover only a small mapping sample.
No X server, input method or user desktop was exercised in this pass.
The remaining X11 implementation and full integration test remain pending.

### console — core/text/raster review in progress

Read console.c, console_internal.h, console_text.c and console_pixels.c in
full, in addition to the public API/README/capability-gap documents. Remaining
presenter/backend implementations, complete tests/build and host integrations
are not yet reviewed. Core lifecycle uses lifecycle_mutex before state locks
and backend_mutex, drains input selection under input_mutex and presentation
references under presentation_mutex, and retains failed-detachment consoles.
These mechanisms still require backend-by-backend verification, not a blanket
concurrency approval. Deferred presentation coalesces requests for 10 ms;
explicit presentation forces open batches. Snapshot creation calls forced
presentation, not merely rasterization; compatibility with the user's stated
drain-versus-flush distinction needs further contract/test review.

Font registrations are sorted by 16-bit ID; mapping search is binary, slot
exhaustion selects least-recently-used registration and logs replacement.
Lookups update atomic use counters even under read locks; eviction occurs
under the state write lock. No generation mechanism is introduced. Per-glyph
lookup/counter costs and cache effectiveness need measurement before calling
them a performance defect.

Text operations retain immediate wrap/scroll, window-relative tab stops and
destructive backspace. UTF-8 writes validate before mutation. INT32_MIN scroll
negation widens first. Full-width vertical raster scrolling uses a circular
text-area offset, with right/bottom pixel margins addressed independently;
partial scrolling copies rows overlap-safely. Raster normalization allocates
both full text planes before changing the circular offset.

Further targeted checks remain: one-column windows receiving wide glyphs,
scrolling through wide-pair boundaries, wide fill codepoints, deferred font
registration changing a cell from narrow to wide, and invalid-scalar behavior
of direct codepoint writes. These are source concerns, not yet reproduced
findings. Generic move_cells allocates a cell rectangle and reuses semantic
cell setters; whether pixel overlays and explicit glyph identity survive all
move cases remains to be tested. No performance conclusions from source alone.

Shared bitmap presenter, libretro backend, text-terminal character helper and
main entry wrapper now also read in full, together with console_bitmap_test.c
and st_console_libretro_test.c. Existing bitmap/libretro CTests pass native
Clang and GCC. Those tests exercise aspect fitting, undersized-host clipping,
scale preservation, cursor scanlines, external-scaling fallback, circular
raster copying, dirty/blink uploads, and frame-copy destination bounds.

Presenter fitting preserves at least the source raster dimensions. Regional
upload callbacks map source damage through the scaler;
failed uploads generally request a subsequent full upload. Backend-specific
failure handling is still pending. Animation uses unsigned elapsed-time
differences from a uint32_t epoch; long-running wrap/phase discontinuities
have not been tested. Blink-cell bookkeeping uses each cell's own BLINK flag,
which needs a dedicated test for Prestel lower rows borrowing upper flags.

Libretro serializes retained-pixel access with backend_mutex then a private
mutex. Size queries are advisory and frame copies recheck both dimensions and
stride multiplication before writing. Size/copy only return the retained
frame, with no timer-only presenter tick and no presentation worker configured
for this backend. Whether the integrating run loop explicitly forces frames
often enough for idle blink/cursor animation remains unresolved; do not claim
integration coverage from these library tests.

The main wrapper selects Quartz's application runner when built, otherwise
SDL's runner; runner lifecycle and no-SDL fallback remain assigned to their
backend source reviews. No production files, build configuration or existing
documentation modified.

ANSI source pass: console_ansi.c, console_ansi_system.h and both POSIX/Windows
system implementations now read completely. Full ANSI tests/build/protocol
comparison remain pending. Output batches into 8 KiB writes, tracks drawn
cells, clips to reported host dimensions, and detects one-row scrolling.
Custom output callbacks present synchronously; stdio mode uses the deferred
worker. Shutdown joins input before emitting cursor/style/scroll-region
restoration and restoring host settings. POSIX uses saved termios; Windows
saves modes, code pages, cursor info and CRT translation modes. No real user's
terminal touched in this pass.

Further source concerns needing targeted checks: drawn-cell cache is updated
before output success; two independent stdio ANSI displays have no exclusive
ownership guard. Terminal colour capability probing calls setupterm without an
observed del_curterm cleanup, and shares the process-global terminfo context
with curses; coexistence review remains pending. ASCII comparisons are local
byte operations, not locale calls.

ANSI PTY test now read completely and passed native Clang: Return, Alt arrow,
Alt text, UTF-8, the implemented DoorWay byte forms, initial host sizing,
resize notifications/idle repaint, and emitted shutdown restoration sequence.
It checks bytes, not a terminal emulator's resulting visual state; DoorWay
protocol validity still requires independent comparison. It uses its own
forkpty child and does not touch the user's terminal.

console_ncurses.c now read completely. It enforces one curses physical display
with an atomic ownership flag; logical peers share that display. Curses calls
from input and rendering serialize on backend_mutex, including nonblocking
wgetch/wget_wch (escape-sequence processing may still wait internally; latency
not measured). Shutdown joins input before restoring changed palette entries,
cursor/endwin and deleting screen/library state. Attach drops bitmap storage
and resets cell dimensions for this text backend. API ownership impact remains
part of final contract reconciliation.

Native RGB bit-depth detection handles boolean/numeric/string RGB terminfo;
COLORTERM truecolor/24bit selects raw SGR only when native RGB is unavailable.
Extended pair allocation uses int pair values through wattr_set's opts pointer;
fallback preallocates a reduced square pair set. Logical colours are now
resolved before native direct-colour encoding. Remaining source concerns include
extended key/modifier handling (only a fixed key set mapped), wide/control
character output, raw-SGR coexistence with curses input refresh, and error-path
drawn-cache consistency. Full ncurses tests, build probes, legacy comparison
and Windows/runtime portability remain pending; source read is not validation.

Ncurses PTY test and complete console/CMakeLists.txt/config template now read.
Existing ANSI, ncurses and ncurses_direct CTests pass native Clang and GCC.
Both curses cases use TERM=xterm-256color; the latter adds COLORTERM=truecolor,
testing raw SGR, not native RGB terminfo. Tests check Return, bright/RGB/bold
output sequences, logical dimensions versus host resize, peer bitmap removal,
idle repaint and cursor visibility on exit. They do not model the terminal
screen or cover extended modifier keys, native RGB, load failures, or palette
restoration failures. No user's terminal/display used.

Build review: platform libraries are target-scoped, generated configuration
and Wayland protocol files reside in the binary tree, runtime-loaded GUI
libraries generally need headers at build time rather than runtime linkage.
Wayland scanner is a host executable; cross-root discovery/tool selection
needs actual cross-build verification. Curses header checks do not establish
the complete implementation
API/ABI. GCC suppresses pedantic warnings for whole backend source files, not
only symbol conversions. These are review concerns, not reproduced additional
portability failures.

GUI CTests for X11/Wayland/Quartz/GDI inherit the ambient display rather than
provisioning isolation; do not run indiscriminately on the user's desktop.
SDL's registered test does select the dummy video driver. Full GUI source/test
review remains pending. The source inventory's remaining console_test.c and
benchmark coverage also remains partial; current green PTY checks are not
whole-console completion.

Win32 text backend and st_console_win32_test.c now read completely. Existing
MinGW build-win64 target st_console_win32_test compiles/links; not executed.
The backend owns a separate screen buffer and stop event, restores the original
input mode/active buffer/display mode, and joins its input worker before freeing
state. It enforces one physical console through an atomic flag. Rendering uses
bounded 256-cell WriteConsoleOutputW runs and rejects dimensions above SHRT_MAX.
Clipboard reads bound the UTF-16 terminator search by GlobalSize; writes transfer
the allocation only after SetClipboardData succeeds. No clipboard operation or
URL launch was executed in this audit.

Input review concerns, not yet runtime findings: post_key uses wRepeatCount only
to set the physical repeat boolean and posts translated/text events once per
record; Ctrl-generated codepoints below 0x20 are discarded unless the virtual
key has its own translated-key entry. Need to verify physical-event consumers
before claiming user shortcuts are broken. Pending high surrogate state is
backend-wide and not explicitly cleared on focus loss or console activation.
The physical-key mapping omits scan codes beyond 0x58 except its enhanced cases.
Input failure exits the worker without a close/error notification. Public
event guarantees and legacy behavior need targeted fixtures for these cases.

Other source concerns: RGB approximation uses fixed thresholds rather than
the current mutable 16-colour palette; indexed values are masked to four bits.
Bold/blink/conceal/reveal handling is absent from cell_attributes, unlike the
other text backends; capability/documentation and legacy comparison are still
needed. Non-BMP narrow glyph output emits a high surrogate without a following
continuation cell. Host resize events report buffer dimensions but do not
directly repaint/update stored draw bounds. Mixed-axis resize operations and
partial host mutations on failure have not been tested. Title changes are not
restored at destruction; whether restoration is promised remains to reconcile.

The existing Windows integration test checks singleton behavior, text-terminal
capabilities, title/write/resize calls and the original buffer's cursor state
after destruction. It does not exercise input, actual output cell contents,
font/Unicode widths, palette restoration or activation. Do not treat successful
cross-compilation as Windows behavior verification.

### hex

All four component files read: header, implementation, CMake and test. No
allocation, mutable global state, system locale calls or OS-specific branches.
Encoding checks overflow before multiplication. Decoding validates the entire
source before writing, honoring failure atomicity and non-overlap precondition.
Two decode passes deliberately support unchanged destination on invalid input;
no performance defect established. Public CMake includes are scoped to the
component and C17 requirements are target-local.
Consumer sites identified in INI, list_crypto, BBS profile codec and theme cloud;
full caller error/format handling remains assigned to those component reviews.

### utf8

All four component files read. Scalar validity rejects surrogates and values
above U+10FFFF; decoding checks leading ranges, continuation bytes, length,
overlong forms and scalar validity before publishing outputs. Encoding stages
bytes locally and leaves an undersized destination untouched while returning
required size, as documented. No allocation, locale state or OS-specific path.
CMake depends on io_error; no dependency cycle within this component.
Console/theme/Wren caller behavior
remains assigned to their respective reviews. Round trips alone are insufficient
proof of correctness; tests must retain independent known encodings and invalid
sequence cases as well.

### string_util

All four files read (header, source, CMake, tests). Operations use explicit
US-ASCII comparisons with unsigned-byte conversion; no locale calls,
allocation, mutable global state or external dependencies. Comparisons and
trimming are linear and bounded by the supplied span or terminating NUL.
NULL-handling varies between APIs and deserves explicit documentation;
compare requires non-NULL strings while starts_with returns false on NULL.
trim_span takes optional output pointers and returns half-open offsets.
No implementation defect established. Expanded tests exercise every byte's
classification/lowercase/trim behavior, including embedded NUL and high bytes,
plus empty spans, prefix/length mismatches and unsigned comparison ordering.
No system locale is selected or consulted by those tests.

### time

All six component files read, including both platform implementations and build
probes. Monotonic deadlines handle infinity, expiration, addition saturation and
int64 output clamping. UTC arithmetic normalizes nanosecond carry and checks
time_t representability before publishing output. The POSIX nanosecond path
checks multiply/add overflow. Windows milliseconds use GetTickCount64 while
nanoseconds use QueryPerformanceCounter/Frequency: do not assume a shared epoch
between these APIs. No current mixed-epoch consumer was found in the call-site
search; nanoseconds are used by benchmarks. Neither helper owns resources or
uses mutable global state. Windows arithmetic was
source-reviewed but not fault-injected or runtime-tested in this pass.

### io_error

All five files read. Stable negative constants, errno translation, and Win32
translation are centralized without state or allocation. Unknown errors map to
IO_ERROR_IO; operation-specific and Winsock interpretations remain outside this
general translator. Error codes are passed as arguments, avoiding delayed reads
of errno/GetLastError. Public io_error_from_windows is declared on all platforms
but defined only by Windows builds; portable callers must not assume it links on
POSIX. Existing tests cover only a subset of errno and never call the Windows
translator; expanded mapping coverage remains an audit task. No implementation
defect established from this component's source review.

### event and semaphore

Read all four tracked files in each component, plus the underlying condition
timed-wait paths. The opaque types each own a mutex, condition and predicate;
creation publishes only after initialization and unwinds prior resources on
failure. Destruction explicitly requires callers to quiesce operations/waiters;
neither API promises close/cancellation semantics. State transitions are under
the mutex and waits use predicate loops. Auto-reset coalesces repeated sets and
consumes a single stored signal; manual-reset broadcasts and remains set until
reset. The counting semaphore signals one waiter per post, checks its bound
before incrementing (including SIZE_MAX), and consumes only after a successful
wait. No fairness guarantee or full POSIX semaphore semantics are promised.
Operations use constant-sized state; no per-wait allocation or full-waiter scan.
No production consumers found by the repository call-site search at this point.
Both builds expose only their own includes and a public threads dependency;
language and warning settings are target-local and standalone builds are wired.

Existing tests cover auto/manual behavior, coalescing, five waiters, handshake,
bounded resource pooling, invalid arguments and ordinary expiration. Added tests
cover expired-but-ready success, actual expiration when empty, NULL deadlines
not consuming state, subsequent reuse, and semaphore SIZE_MAX saturation.
No implementation defect established. Signal/unlock failures can report failure
after changing state; no blanket failure atomicity is promised for operations.
Allocation/init/notification failure paths have been source-reviewed, not
fault-injected. Test readiness counters precede actual wait entry, so multiwaiter
tests are evidence of progress but not proof every waiter was blocked when set.
Test failure cleanup can leave worker threads alive until process exit; CTest's
20-second timeout bounds a regression hang. Windows runtime and scheduler-race
exhaustiveness are not established by the native repeated tests.

### dynamic_library

All eight tracked files read. The handle is opaque and uniquely consumed by
close, including close failure; open leaves the result unchanged on all failures.
Filename buffer arithmetic is bounded before allocation, formatting suffixes fit
the reserved space, all aliases are validated before loading, and a successfully
loaded native handle is closed if wrapper allocation fails. There is no wrapper
global state or locale dependency. Function/data lookup deliberately uses one
platform symbol API; the caller supplies the final declared type. Concurrent
close/lookup safety requires external lifetime control, not provided here.
Each alias's fallback search completes before the next alias; runtime work is
linear in preferred_major and alias count, so arbitrary untrusted huge major
values would be expensive. Current callers use small constants. Names and
runtime symbol use/teardown in audio and console remain those callers' audits.

Build checks establish loader link availability in the target's strict C17
language mode. POSIX ${CMAKE_DL_LIBS} is target-local; Apple
naming is selected in CMake. GCC pedantic suppression accommodates native
function-pointer conversions; no alternative declaration hack is introduced.
Fixtures exercise functions and data, invalid input, missing names/symbols,
aliases, older versions, bare names, eager unresolved-symbol failure, and
local/global provider visibility. Allocation/close failures and competing-name
precedence are not fault-injected. Windows/macOS loader runtime remains untested.
Windows name validation rejects slashes but accepts colon; whether drive-relative
names should be rejected to enforce the stated "not paths" restriction remains
an unresolved platform question, not a demonstrated current caller vulnerability.

### file_lock

All seven original component files read, covering public/internal headers,
coordination, both backends, CMake and test. Persistent sidecars avoid changing
lock identity when the target file is replaced. POSIX uses flock, Windows uses
blocking LockFileEx; local readers/writer bookkeeping and waiters are protected
by a once-initialized global mutex. Waiters retain the list entry until after
their predicate loop; failed native acquisition releases its local claim.
Native release precedes waking local waiters. Sidecars are intentionally not
deleted by release. The path-keyed local list is linear in simultaneous paths;
no scaling problem established for current settings/BBS consumers. Their
transaction boundaries and path ownership are still assigned to those audits.

The API is blocking and has no cancellation, upgrade or recursive-acquisition
promise. Destruction is by consuming release, with NULL accepted. A valid-output
argument is cleared early, unlike several other components' unchanged-on-error
policy; the header does not promise otherwise. Readers may starve writers, since
waiting writers do not exclude new readers. Mutex/condition errors are ignored
in coordination; no injected synchronization-failure evidence yet. String path
keys do not canonicalize aliases; native locks still provide the actual file
exclusion. The POSIX descriptor is opened without close-on-exec: inheritance into
spawned programs needs checking with the connection/process-launch audit before
assigning user-visible impact. flock errors are retried only on EINTR.

Existing tests exercise shared/shared and both shared/exclusive directions
within one process, but not exclusive/exclusive or multiple processes. The
50-ms scheduling assumption can make them flaky under extreme load. Native
tests pass; interprocess, alias, Windows runtime, allocation and synchronization
failure tests remain verification limitations, not claims of proven correctness.

### file_map

All five files read. Read-only whole-file mappings share stream-based setup in
each backend. Stream position is unchanged and ownership remains with caller;
the native mapping survives stream closure. POSIX checks descriptor access mode,
file kind, negative size and size_t range before mmap. Windows checks native
handle type, file information and size, retains the mapping handle and unwinds
both resources on view failure. Empty maps use a byte in the owned wrapper.
Results are published only after success; close consumes wrappers and attempts
all native cleanup even after an earlier failure. Filename opens precheck kind,
then validate the opened handle again. The stated no-concurrent-modification
precondition is essential: these are not stable copies or a locking API.

No defect established within the documented contract. Windows empty write-only
streams can bypass the access check that POSIX performs before its empty case;
the header does not currently promise rejection of write-only empty streams.
Mapped size is captured at open and data has no NUL-termination promise. INI's
borrowed-stream use is identified but parser ownership/error behavior remains
part of the INI audit. CMake is target-scoped and its platform capability
checks use the target's strict C17 language mode.
Tests cover binary/Unicode paths, empty files, multiple maps, stream position,
non-file streams, invalid arguments and unchanged failure outputs. Added a
direct lifetime check after closing the borrowed stream. Native tests pass;
Windows is compile-only. Mapping exhaustion, cleanup failure, huge files and
32-bit address-space limits have not been fault-injected.

### array_list, str_list, name_value_list

Read every tracked file in all three components: headers, implementations,
CMake targets, normal tests and all string-list failure-injection helpers.
The generic core owns pointer storage only, uses a count/capacity header and a
NULL sentinel, grows geometrically and guards allocation arithmetic. The earlier
realloc-lifetime fix is present (new_list is captured before realloc); it is not
a newly fixed issue in this audit. Insert/take shift exactly the tail including
sentinel. Search uses safe midpoint arithmetic, lower-bound lookup and upper-
bound stable insertion. Mutability of sorted arrays remains a caller invariant
explicitly documented; the generated unsorted operations are not a promise to
enforce sorting. A007 records the remaining typed-representation concern.

String lists copy before growing, append-list stages all copies and reserves
before publishing them, and clone/split replace only once construction succeeds.
This supports self-append, self-clone and input strings borrowed from the old
destination. Join checks cumulative sizes and allocates an exact result. Removal
frees an item directly; take and join expose the matching free_item operation.
Counts are O(1), append amortized O(1) apart from string copy, arbitrary removal
linear, and teardown linear. No locale calls or synchronization; external
lifetime/concurrency control is required. Retained empty allocation and handle
invalidation are documented. Array and string-copy allocation failures are both
covered by the previously committed injection tests, including growth failure.

Name/value items own copied strings; replacement duplicates before freeing,
including aliases of the current value/name. Owning lists and borrowed indexes
have distinct free operations and explicit lifetime requirements. Comparisons
use the ASCII-only helper, not locale. Unique insertion frees its new candidate
on duplicate/error without changing the existing item. It allocates even when
the key already exists; this can fail before reporting a duplicate under memory
pressure, which is not prohibited by its current contract. Teardown's earlier
linear-time change is present. Constructor and insertion failure cleanup is
source-reviewed, but this component lacks injected allocation-failure tests.

All three CMake files are target-scoped with directional dependencies: generic
array has none; strings depend privately on array/string compatibility; name/
value exposes array types publicly and keeps string helpers private. Tests cover
growth, sorted duplicate stability, missing-key insertion positions, sentinels,
invalid arguments, borrowed indexes, aliases and large teardown. Added self-clone
and split-from-owned-string cases to string tests. No new functional defect
established. Platform caller usage, including stale copied handles and index
lifetime, remains part of each consuming component's review.

### string_compat

All five files read. Capability probes take function
addresses with complete signatures, preventing undeclared-call false positives,
and link actual calls. Linux _GNU_SOURCE and Windows CRT warning selection
are applied consistently to probes and propagated to consumers through the
target. Native facilities are used when available; only missing ones receive
namespaced fallback declarations/macros, not invented libc declarations.
Generated configuration lives in the build tree. No production locale calls.

Fallback duplication bounds reads by the supplied maximum and allocates space
for the terminator. strlcpy/strlcat follow required-length/truncation semantics,
including size zero and unterminated destination cases. Inputs must meet normal
string/buffer and non-overlap requirements; this is not a NULL-tolerant wrapper.
No implementation defect established. Allocation-failure injection is absent,
though failure return/cleanup is simple and source-reviewed. The fallback test
option defaults OFF; forced unused implementations are not part of ordinary
platform testing. Exercised it deliberately in a separate temporary build,
without altering the user's normal configuration or production build logic.

### windows_utf

All four files read. Conversion uses strict native UTF-8/UTF-16 APIs with a
sizing pass, includes the terminating NUL, rejects malformed sequences, and
publishes allocated output only after successful conversion. The 16-bit wchar_t
assertion and allocation-size check encode platform assumptions explicitly.
Second-pass errors are saved before freeing buffers. No locale conversion or
mutable global state. Inputs are NUL-terminated and caller-owned; simultaneous
mutation during the two passes is not supported. The small header documents
allocation/failure ownership but omits explicit zero-success/error values and
NUL-terminated input wording. No implementation defect established.
Build rejects non-Windows targets and exposes the shared error dependency.
Tests now check the independent UTF-16 surrogate-pair representation rather
than roundtrip alone, lone low/high surrogate rejection, empty strings and
invalid arguments. MinGW compiles these tests; no runtime execution claimed.

### path

All eight tracked files read. Public output functions report required sizes,
leave too-small buffers untouched and support exact in-place output; allocated
joins publish after construction. Native separators are distinct from the
foreign-separator helpers. POSIX normalization is lexical, removes dot/dot-dot
components without walking above root, and does not resolve symlinks; tilde
expansion reads HOME and leaves named-user syntax literal. Windows handles
drive-relative/current-drive/UNC roots, converts strictly through windows_utf,
and constructs the extended namespace internally while rejecting incoming
device namespace syntax.

The component validator promises separator/dot rejection only. It accepts colon,
Windows device basenames and trailing dots/spaces, so it must not be treated as
a complete Windows safe-download validator. Consumers in theme/BBS/webget are
identified for their own security/filename-policy review. Joining does not
replace a base when name is rooted; it concatenates, as currently documented.
No allocations escape failure cleanup in source review. Windows wide joins have
less complete multiplication guards than other helpers; practical input/native
length limits and 32-bit behavior are not exercised here. Tests now apply exact-
capacity canaries and in-place checks to their output vectors, plus repeated
separators/root/dot traversal on POSIX. Native tests pass; normal and controlled
buffer-growth Windows tests pass under Wine. No macOS/Haiku runtime or
allocation-error injection performed.

### app_paths

All nine files read. Common code owns allocated fields, constructs a temporary
result before publication, copies overrides, and derives system-cache paths
after cache overrides. Dispose frees all fields and zeros the structure. Init
does not free a previously initialized output; header should state initialization
and disposal preconditions rather than imply replacement. Directory creation is
lazy and confined to prepare calls, whose underlying permission/error behavior
belongs to filesystem. Platform defaults are computed before overrides, so full
overrides do not bypass native-directory discovery failures. That is a design
limitation, not an established violation of a promised contract.

POSIX selects XDG/HOME defaults; macOS uses Library support/cache paths;
Haiku uses find_directory categories, not guessed POSIX paths; Windows uses
known-folder APIs and strict UTF conversion. Native discovery errors are often
collapsed to NOT_FOUND or OUT_OF_MEMORY, reducing diagnostic precision. No
mutable registry, system locale or hidden worker threads. Defaults depend on
process environment; concurrent environment mutation is not coordinated.
CMake isolates OS-specific policy and common ownership but publicly propagates
filesystem/path dependencies that the public struct header does not require;
this is an exposure concern, not a build failure. Normal tests cover overrides,
derived cache naming, lazy prepare and cleanup but not every platform default,
all override fields or allocation rollback. Windows compile verified; macOS and
Haiku discovery code source-reviewed only. Cross-checking defaults against
manuals/legacy migration expectations remains in root-documentation review.

### filesystem

All six original files read (header, common composition, both backends, CMake,
tests). Public size/time fields encode stable widths and output structs publish
only on success. POSIX stat/lstat and Windows handle/reparse metadata distinguish
follow/no-follow operations. Windows path conversion consistently uses the exact
extended namespace through the path helper. Filetime conversion guards range
and handles pre-epoch rounding without
negating INT64_MIN. Stream size does not seek; truncation flushes, validates
representability and leaves position unchanged. Sync flushes stream buffers and
requests file durability, not parent-directory durability.

POSIX copy checks source type and same-inode destinations, then streams through
a fixed 64-KiB stack buffer, applies ordinary 0777 permission bits and second-
resolution mtime. Windows uses file identities before CopyFileW. These are not
atomic transactions: destination truncation/partial writes can survive failure,
and pre-open identity checks do not protect against concurrent path replacement.
No such guarantees are documented, but consumers must not infer them. Directory
destination/symlink behavior and "permission mode" (special bits versus ordinary
permissions/Windows attributes) need sharper cross-platform documentation.
POSIX copy does not reject a FIFO destination before blocking fopen; caller
trust/nonregular-output policy remains a concern, not a demonstrated current
consumer hang. Rename uses native same-volume semantics; no copy fallback.

Recursive mkdir accepts existing directory components; POSIX creation mode is
0777 filtered by umask, not inherently private. Applications requiring private
settings/key directories must impose that policy separately (caller reviews
pending). Neither backend provides hostile-directory race resistance. Windows
directory-link deletion behavior differs from POSIX lstat-based removal and is
not explicitly documented.
CMake requires 64-bit off_t with a _FILE_OFFSET_BITS retry, target-local
definitions and strict-C17 probes. Public
path linkage exceeds the types needed by filesystem.h; no cycle established.

Existing tests cover metadata, exclusive creation, multi-GiB sparse truncation,
position retention, copy permissions/mtime, replacement rename, nested creation,
symlinks/dangling links and cleanup; Windows additionally has long/exact names
and optional symlink tests. Added hard-link self-copy preservation and oversized
truncate rejection. Native Clang/GCC pass; MinGW builds and root behavior runs
under Wine. Real short writes, disk exhaustion, native cleanup failures,
cross-volume rename, ACL preservation and concurrent path replacement are not
runtime-tested. No performance benchmark or claim of copy throughput parity.

### threads

All eight tracked files read: public/config headers, CMake, pthread/Windows
thread implementations, both rwlock implementations, and complete tests.
The namespaced API is intentional, not an unauthorized standard-name shim.
CMake probes in strict C17 and prefers the complete pthread contract even on
Windows; otherwise it selects the Win32 implementation. Optional pthread
timed-mutex/rwlock facilities are separately probed. Configuration only compiles
and links; no target execution required. PUBLIC platform definitions/includes
keep the concrete public type layout consistent with consumers. NULL/invalid-
handle tolerance is not consistent across native and fallback paths and should
not be assumed.

Pthread startup owns and frees its argument packet, join/exit transfer int
results through intptr_t, and native TLS keys own only associations, not caller
data. Native return codes map to the narrow result enum. Timed-mutex fallback
polls trylock with one-ms sleeps only while contended. Sleep directly returns
nanosleep's status; precise C11-versus-native error semantics are not documented.
Cancellation and fork-recovery are not provided. Native rwlocks delegate policy
to pthread; fallback uses predicate loops, waiter counters and per-thread read
counts so existing readers may recurse while a writer waits. New readers give
waiting writers priority. Counter limits are checked; destruction requires
external exclusion and no holders/waiters. Failed destruction may still consume
a fallback handle, unlike native behavior; callers need a clear error contract.

Windows plain mutexes use SRWLOCK, recursive mutexes critical sections,
conditions native condition variables, once INIT_ONCE, and threads _beginthreadex.
Timed mutex waits poll on contention; condition waits convert UTC deadlines to
bounded millisecond chunks. Clock-read failure is treated as timeout rather than
ST_THREAD_ERROR, unlike the pthread timed-mutex fallback. No injected-clock
Windows test here. The once wrapper asserts representation size/alignment but
casts a project struct to INIT_ONCE, another platform ABI assumption worth
documenting. Join/detach consume native handles; current-thread handles contain
identity only, so current cannot be treated as a joinable ownership handle.

Windows TLS uses TlsAlloc plus a per-thread linked list, active flag and atomic
reference counts. Delete does not invoke destructors; deferred records retain
the storage until cleanup. Cleanup clears each value before callbacks and allows
bounded reinstallation iterations. It runs only through this library's thread
entry return or st_thread_exit, not automatically when an externally created
thread exits. Thus foreign-thread non-NULL storage can leak records/destructors
unless explicitly cleared/deleted first. No production TLS consumers were found
in the call-site search; this is a missing public restriction, not demonstrated
current application leakage. Concurrent delete versus access still requires
external key-lifetime control; atomics do not make stale key handles safe.
Win32 rwlock counters/events/read records were traced; A011 captures performance
uncertainty. No new functional failure demonstrated in tested paths.

Tests cover normal/explicit thread exit, negative results, thread-local isolation,
recursive mutexes, try/timed contention, conditions, once, destructor rearming,
delete with another thread holding a value, detach and mutex/rwlock stress.
They do not force every waiter interleaving, inject synchronization/allocation
failures, test foreign-thread TLS cleanup or measure latency/fairness. Ordinary
and forced pthread fallback tests pass repeatedly. Win32 backend compilation
verified from generated configuration; no Windows runtime claim. All owning
subsystems still require separate worker shutdown/lock-order audits.

### socket and tcp_client

Read all seven socket files and all ten TCP-client files, including both native
resolvers, private shared contracts, build logic and fake/real tests. Handles
remain native; validity checks compare only the invalid sentinel, not kernel
resource state. Runtime startup/cleanup is caller-owned. Send/receive allow
partial counts and limit native lengths; wait interprets readable EOF through
peek. Windows peeks without MSG_DONTWAIT, so shared blocking-socket readers can
race and block after readiness; external receive serialization is an unstated
requirement. These semantics are stream-oriented despite the generic socket
name: zero-length datagrams are not distinguished from EOF. Closing or reusing
handles concurrently with operations is not protected by this layer.

POSIX wait uses poll except when urgent data requires select; that fallback
rejects descriptors at/above FD_SETSIZE. The advertised internal capacity is
unbounded for poll, whereas Windows select limits handle count. POSIX negative
invalid handles may be ignored by poll, producing timeout rather than invalid-
argument; indefinite waits can therefore hang on an invalid sentinel. The
header should clarify valid-handle preconditions and these backend restrictions.
Readiness may be consumed by another reader, yielding no events after a wake;
Windows always monitors exceptions and may likewise return no requested events.
The public wait contract does not explain spurious/no-event success. Monotonic
deadline recomputation handles native INT_MAX wait chunks; interruptions return
to the public caller, while TCP retries them. Poll allocates an array even for
single-socket waits; hot-path cost has not been benchmarked.

SIGPIPE suppression is probed: MSG_NOSIGNAL is per send, SO_NOSIGPIPE is applied
only to sockets created by the internal connector. Thus externally supplied
handles on SO_NOSIGPIPE-only platforms need their own setup, an undocumented
constraint. Error translation is platform-specific, with some unavoidable
resource-error distinctions. TCP uses the socket private header as an explicit
cross-library mechanism boundary; it is not an independent public wait-many API.
Root include directories are exposed for generated config/types. Capability
probes use strict C17 in both standalone and aggregate builds. Windows link
dependencies and platform sources are target-local.

TCP resolves synchronously before deadline/cancellation, as explicitly stated
in its header. It copies IPv4/IPv6 addresses into aligned bounded storage,
interleaves families, staggers attempts by 250 ms and polls cancellation at
50 ms while racing. Immediate failures advance without the stagger; output
publication waits until blocking mode is restored and losers are closed.
Per-attempt storage cleanup is explicit.
Interleaving repeatedly scans from the beginning (quadratic in address count)
and each wait rebuilds active arrays; no real-world large-resolution benchmark
performed. next_attempt = now + 250 lacks deadline-style saturation at INT64_MAX;
this is a theoretical extreme-clock overflow, not an ordinary uptime failure.
Numeric service strings avoid locale formatting ambiguity; DNS text/IDN policy
is delegated to native getaddrinfo and is not a Unicode-hostname contract.

Tests cover IPv4 loopback I/O, urgent data, EOF, cancellation, zero timeout,
family ordering/staggering, recorded wait errors, error priority and loser
cleanup. IPv6 live connections, high descriptor counts, interrupt/clock/
allocator faults, native Windows errors and concurrent readiness consumers
remain runtime limitations. No external network services were contacted by tests.

### serial

All six component files read. The opaque handle owns one native device handle;
open publishes only after setup, close consumes it (including reported failure),
and the documented concurrency allowance is one reader plus one writer with
configuration/close externally serialized. POSIX opens nonblocking/no-controlling-
terminal and sets close-on-exec, validates termios, and defers raw configuration
to configure. Configuration stages a complete termios object, rejects unsupported
rate/flow selections and preserves old speeds for baud zero. It uses one
tcsetattr, but driver acceptance/rounding and partial native failures are not
verified by readback. Poll timeouts are chunked for large durations; interruptions
are reported, not silently restarted. Reads/writes may be short. Close flushes
away pending output rather than guaranteeing delivery, which merits explicit
documentation. Modem controls and high rates are capability-selected in CMake.

Windows uses strict UTF conversion, overlapped CreateFile and native DCB state.
Open changes COM timeout/event-mask settings despite the header's broad "device
configuration is not changed" wording; line parameters are left unchanged.
Configure preserves unrelated DCB flags (e.g. inherited character filtering),
so the claim of complete configuration needs sharper raw-byte semantics and
tests on previously configured devices. DTR/RTS are disabled when handshake is
not requested; caller modem setup must account for this. Reads use immediate
ReadFile then WaitCommEvent and retry; writes issue overlapped WriteFile. Timeout
cancellation waits for completion before releasing stack OVERLAPPED/event state.
Clock-helper errors at serial_windows.c:209/219 return without that cleanup,
but current Windows millisecond time uses GetTickCount64 and cannot fail for
these valid inputs: recorded as latent defensive inconsistency, not reachable
memory corruption. Real cancellation can exceed the requested timeout while
waiting for a driver; no hard realtime guarantee is stated.

Windows size capping is MAXDWORD rather than signed ssize_t maximum, so 32-bit
multi-GiB transfers need review/testing; no such transfer attempted. Event objects
are allocated per operation, unlike POSIX readiness polling. No throughput or
driver-latency benchmark. Baud enumeration reports native expressible/supported
rates, not a guarantee of physical hardware acceptance. Modem status outputs
publish only on success. There is no locale dependency or global mutable state.

CMake selects one platform source with narrow helpers and strict-C17
per-capability probes. Root includes are exposed for
common generated types. Native PTY tests cover transfers, timeouts, invalid
config, enumeration, zero-length operations and close on this FreeBSD host. No
physical serial device, modem-line transitions, flow-control stalls, Windows
COM runtime, unplugging, allocation failure or cancellation fault injection was
exercised. Those limits are distinct from the passing native PTY test and the
Windows argument coverage.

### connection

- Read all connection sources, headers, generated-header template, CMake and
  test sources, including POSIX/Windows/stub shell backends and serial fake.
  Compared relevant legacy RLogin controls, modem replies, PTY initial flags,
  environment construction and teardown. Remaining runtime/stress coverage
  gaps are explicit below and are not claims of verified behavior.
- Queue state is mutex-protected; workers own transport input/output, and
  close sets stopping, wakes queues/state, interrupts, joins, then destroys.
  The public contract explicitly prohibits concurrent close/public operations.
  Callback reentrancy/allowed operations and simultaneous public operations
  need clearer treatment in the remaining integration review.
- Other core behavior to assess against callers: queued()
  excludes the output worker's in-flight buffer, close discards pending data,
  read errors are logged but subsequent receive returns zero after draining,
  and remote input EOF stops both directions. These are not yet labeled
  defects without establishing the required caller contract.
- Core clock helpers ignore failures (zero timestamp or expired deadline),
  unlike returning the clock error. The deadline is initialized. Mutex,
  condition and join failures are
  commonly ignored; injected synchronization failures are not covered yet.
- Network transport serializes geometry and atomically queues RLogin window
  packets; stop wakes blocked queue writers before joining. Geometry updates
  can wait indefinitely for output capacity. RLogin urgent control handling,
  initial handshake and pause semantics have narrow existing tests. It starts
  in binary mode as the legacy BBS implementation does, not RFC cooked mode.
  The initial server acknowledgment byte is not separately consumed or
  validated; the fake server omits it. Compatibility with rejection messages
  and ordinary servers remains an integration test gap.
- Connection's forkpty capability probes use strict C17 in both aggregate and
  standalone builds.
- Serial setup opens/configures before publishing the connection, snapshots
  the 7-bit policy, and closes/DTR-drops on setup failure. Its profile/global
  settings inputs must remain stable during setup; mutable global modem
  settings couple this library to settings. Runtime DCD/DSR checks only happen
  after a read timeout, so continuous input delays carrier-loss detection.
  Modem close sleeps 500ms then permits up to ten seconds for DCD to drop.
  Clock failure can defeat those deadlines because the helper returns zero.
  Initial input draining has no cancellation check until draining finishes.
  Response substring matching preserves the legacy broad OK/CONNECT policy;
  tests cover only ATZ/ATDT555 success and initialization cancellation, not
  timeout, malformed responses, echo-off variants or physical modem behavior.
- POSIX shell environment is assembled before fork and frees the parent's
  copies afterward. TERM/COLUMNS/LINES are supplied, but legacy TERMCAP and
  MM_CHARSET construction is absent; integration expectations need follow-up.
  Default SHELL is now passed through `/bin/sh -c`, unlike the old direct exec;
  paths containing shell syntax/spaces therefore merit compatibility testing.
  PTY setup uses complete system TTYDEF defaults when available and otherwise
  retains the platform-created state, then disables software flow control and
  selects the supported erase keys and 115200 baud. Registered tests cover
  newline processing, echo, canonical Return, Ctrl+C and Ctrl+S behavior.
- POSIX PTY master stays blocking. poll before write does not establish that
  the entire buffer fits. Shutdown sends SIGHUP, then joins workers, and only
  afterwards reaches destroy's ten-second wait/SIGKILL escalation. A blocked
  writer plus a child ignoring HUP is therefore an unresolved shutdown-hang
  concern requiring a controlled saturation test. Legacy escalation happened
  before waiting for its I/O threads. Allocation-failure cleanup also uses
  unbounded waitpid after HUP. No hang reproduced yet.
- Windows ConPTY source inspected and cross-compiled, not run. Conversion and
  process failures still collapse into IO_ERROR_IO, and native_error fields
  are never filled by these providers. No Win32 behavior is inferred from
  native PTY tests.
- Current search finds no production st_connection_* call sites outside the
  library (profile header only documents the API); no app or Wren integration
  can be claimed from these standalone tests. Existing network tests assume
  some TCP reads yield the requested length and have early-failure cleanup
  gaps; their current success does not establish exhaustive stream behavior.

### profile

- Read every local file: public model, implementation, CMake and existing test.
  The component is cohesive and depends only on string duplication; connection
  and persistence consumers interpret the model, not vice versa.
- Six path fields are owned; other strings are fixed arrays. Copy builds a
  temporary with all owned pointers cleared, duplicates each path, disposes
  partial state on failure, then replaces the initialized destination. The
  self-copy fast path is safe. Reinitializing an already owning profile without
  dispose would leak; init/dispose lifecycle requirements could be clearer.
  Disposed state is zeroed, not reset to normal profile defaults.
- No internal synchronization: callers must serialize mutation and not shallow
  copy owning profiles. Header documents ownership and initialized destination,
  but not source string termination/enum validity or failure preservation.
  Unknown fields are copied as data; consumer validation belongs to settings,
  BBS-list and connection integration, not an inferred model invariant.
- Existing native copy test passes Clang/GCC, covering two owned paths and
  a NULL path. All six allocation-failure positions, populated destination
  replacement, and exhaustive field defaults are not tested. MinGW compiles;
  no Windows execution performed. Source review finds transactional copy
  cleanup sound; no claim of injected-allocation verification.

### ini

- Read the full ini.c, public header, CMake and both complete test sources.
  Consumer behavior and value_convert internals remain separate pending work
  assigned to their inventory rows, not implied by helper coverage.
- Records own raw text; sections own name/value entries and borrowed record
  references plus sorted indexes. Document destruction frees records then
  sections without dereferencing dead record references. Parsing constructs a
  temporary document and replaces the destination only on successful parsing.
  Index/storage capacity is reserved before mutation to avoid partial append.
- ASCII trimming/folding/classification is delegated to string_util, with no
  locale-dependent ctype calls. Arbitrary non-NUL value bytes are preserved;
  NUL anywhere rejects the document. Malformed lines are retained as trivia,
  and malformed section starts suppress subsequent keys until a valid section.
  This permissive parse policy needs explicit public documentation.
- Serialization and collection result functions free the previous destination.
  The header's replacement contract does not clearly state initialized-or-NULL
  output requirements or serialized-buffer deallocation.
- Style prefix/separator contents can change serialized grammar. Removing a
  key using a borrowed matching name may invalidate that name during the scan.
  These are follow-up candidates, not yet independently reproduced findings.
- Sorted index insertion and record-array shifting can produce quadratic
  construction/removal for large documents. Each parsed key allocates both raw
  text and duplicated name/value storage (including temporary copies). This
  is a performance observation, not a measured regression; benchmarks pending.
- Existing tests cover duplicate keys/sections, last-match lookup, non-ASCII
  name suppression and opaque value bytes, literals, malformed lines, read
  position preservation, replacement on parse errors, typed conversions,
  comments, rename/remove, clone independence and invalid arguments. They do
  not inject each allocation failure or short stream writes. The public
  view-invalidated-on-mutation wording is broader than the
  stable section pointers reused by existing tests and likely consumers;
  clarify that contract rather than infer undocumented stability guarantees.
- Added zero-bitfield test passes with a nonzero default: the empty encoded
  value is correctly interpreted as zero. This investigated concern was ruled
  out, not recorded as a defect. No system locale dependency introduced by
  the new tests or present in INI's direct character handling.

### value_convert

- Read value_convert.c/.h, decimal_double.c/.h, both native time adapters/
  internal header, CMake and complete test source.
- Public header lists signatures only: accepted bases, whitespace, booleans,
  duration units, datetime formats/local timezone, finite-number policy,
  output preservation and required buffer sizes are unspecified. Integer
  parsing uses checked unsigned magnitude with safe INT64_MIN handling.
  Negative integer formatting writes the minus sign before discovering an
  insufficient buffer; other formatters generally stage before copying.
  No unchanged-on-failure guarantee should be inferred without documentation.
- Datetimes deliberately use local timezone through mktime/native localtime,
  while month recognition/output is fixed ASCII, not system-locale text.
  Zero formats as Never rather than an epoch date. mktime's -1 value is always
  rejected even where it also represents a real pre-epoch second; DST folds,
  signed/unsigned time_t narrowing and pre-epoch boundaries need tests.
- Duration parsing checks isfinite after multiplying, rather than establishing
  range before multiplication; floating exception/trapping and non-IEC behavior
  remain portability questions for the decimal arithmetic review. No integer
  replacement or locale API introduced by this audit.
- string_util is linked PUBLIC though its API is absent from the public
  conversion header; this is an unnecessarily exposed implementation
  dependency, not a cycle.
- Existing numeric tests cover a small set of extrema and a locale-independence
  check that silently skips unavailable non-C locales. No claim those locales
  were installed/exercised. New date test uses two forms in the same timezone
  and does not modify process timezone or require any installed locale.
- Decimal conversion uses least-significant-first decimal digits, exact powers
  of two, checked uint64 significand construction and ties-to-even rounding;
  it does not reinterpret a double's storage or call locale-sensitive strtod/
  printf. Radix other than two and significands over 63 bits are explicitly
  rejected at compilation. Public formatting separately limits precision to
  19 decimal digits. These restrictions are not documented in the public API
  or established by a configuration capability check.
- The 5,000 exponent/fractional-digit limit and 12,564-digit storage capacity
  reject some mathematically representable long textual values. Explicit
  exponent overflow on an all-zero mantissa is accepted as signed zero, but
  capacity overflow is rejected first. Underflow rounding to zero returns
  false, unlike an explicit zero; nonfinite strings and hex floats are rejected.
  These are observable policy choices to document, not inferred libc parity.
- The parser stages the result until success; formatter stages nonzero output
  and checks capacity before copying. For tested binary53 values, significand
  conversion is exact before ldexp. Directed floating-rounding modes and
  targets without gradual underflow have not been verified; the code uses
  nearest-even decimal rounding independently of the caller's rounding mode.
- Each decimal_integer occupies roughly 12.6KB; parsing plus the exponent
  search holds three such objects simultaneously. Repeated per-digit multiply
  and zero-trimming memmove loops can be costly for extreme exponents/long
  inputs. No performance benchmark or small-thread-stack test was run, so no
  measured performance regression or stack failure is asserted.
- New tests use mathematical midpoint strings and integer values around 2^53,
  guarded by radix/mantissa checks rather than assuming object representations.
  Halfway-even, just-above-halfway, malformed-input preservation, signed zero,
  insufficient double-format buffer checks and date precedence pass on native
  Clang/GCC. These selected cases are not exhaustive proof of rounding
  correctness across all inputs or floating-point formats.

### audio_mixer

- Read the full mixer implementation, header, CMake and complete test source.
  Device/backend integration and legacy comparison remain assigned to audio's
  inventory row. This is a fixed-format internal mixer, not a general audio
  conversion API; header should explicitly state stereo interleaving, frame
  units, take-on-success ownership and destruction concurrency restrictions.
- Mixer mutex protects stream membership/pull/ack; stream mutex protects
  queue/ramp/state. Nested paths acquire mixer before stream; append releases
  stream before signaling under mixer. Pending buffers retire with a pull
  ticket and are freed at acknowledgment; drain snapshots sequence numbers.
  Tests establish that idle means no queued source, not no unacknowledged
  device data. Clear discards both source and retired buffers; it cannot undo
  samples already copied into a backend. Consumer semantics need audio review.
- Destroy broadcasts then immediately destroys synchronization objects and
  frees state; it is not safe concurrently with an active drain/wait operation.
  The internal header does not document that exclusion. No concurrent-destroy
  reproduction attempted; outer audio API lifetime coordination still pending.
- Buffer frame counts are capped before byte-size multiplication; clamped
  fades keep combined fade arithmetic within bounds. Per-buffer/stream gains
  are finite and bounded, but the accumulator has no explicit stream-count
  saturation guard. Enormous numbers of high-gain streams could overflow;
  no realistic workload or runtime reproduction established.
- Ramps advance once for the whole chunk before any sample is mixed, making
  their envelope depend on pull size; a chunk as long as the ramp applies its
  final volume immediately. Low-gain interpolation also substitutes -60dB
  below a 1e-6 threshold despite permitting volumes down to -192dB. These
  source concerns need targeted output tests/legacy comparison.
- Hot path holds the mixer mutex, computes powf per buffer, tanh per channel
  sample and frees acknowledged buffers under locks. Unbounded stream/buffer
  lists and drain scans affect scaling. No audio latency or contention metric
  gathered, so no measured performance regression claimed. Ticket/sequence
  wrap after 2^64 operations is not handled consistently but not reproduced.
- Thread/io_error dependencies are PUBLIC despite neither being exposed by the
  mixer header. Existing tests
  cover basic mixing, loop termination, clear/drain and delayed acknowledgment,
  but their sleeps are scheduling assumptions, not explicit waiter barriers.
  Allocation/synchronization failure injection and concurrent mutation stress
  remain test gaps.

### audio

- Read every local source/header, all eight backends, synthesis/beep paths,
  selection table, CMake/configuration template and both test sources.
  Source coverage is complete; actual audio device verification is not.
- The public API explicitly excludes concurrent destruction versus operations
  using the same object. This resolves the internal mixer's undocumented
  destruction exclusion for these callers; no externally supported concurrent
  destroy/drain behavior should be inferred. Stream mutation uses mixer locks;
  engine membership and one-shot retirement use the engine mutex.
- One engine is enforced with a process-static atomic, released on all examined
  creation failure paths and destruction. One-shots are marked only after
  append succeeds so an empty newly created stream cannot be prematurely reaped.
  A very fast blocking backend may finish before marking, leaving the idle
  one-shot until a later pull/reap or engine destruction; memory-retention
  timing deserves a targeted test, not a use-after-free claim.
- Callback pulls acknowledge immediately; blocking worker acknowledges after
  write accepts the full fixed 512-frame block. Short final content is padded
  with silence. Backend latency is sampled separately by the drain caller.
  Clear cancellation is checked during mixer drain, but after that returns the
  outer drain samples latency/sleeps and checks only engine failure. A clear
  during that phase cannot return CANCELLED as the broad public wording says;
  controlled late-clear reproduction pending.
- Shutdown stops the mixer then joins the blocking worker before backend close.
  The backend contract has no interrupt/cancel operation. OSS write, Pulse
  simple_write and ALSA write/recovery loops can remain in the backend after
  mixer stop; stalled-device shutdown needs testing. No physical device hang
  observed. Latency methods run concurrently with writes and one another;
  verify each provider's allowed threading contract before asserting safety.
- Runtime-loaded ALSA/Pulse handles are owned until close, and missing-symbol
  loaders close their temporary library without publishing it. OSS opens the
  default device and verifies exact format/channels/rate; no device selection
  interface is present. Native library details, syscall request-width behavior
  and actual device operation have not been exercised by the fake-backend test.
- Core u8 mono conversion checks allocation multiplication, expands each input
  into two stereo frames with midpoint interpolation, and retains ownership on
  failed append_take. Frequency/duration arithmetic is bounded before tone
  allocation; synthesis implementation review remains separate pending work.
- Existing test verifies selection fallthrough, singleton/result preservation,
  basic append/drain, one-shots and initial failure notification using fakes.
  It explicitly permits engine creation succeeding after an immediate failure
  callback, with later stream creation returning IO_ERROR_IO; this is existing
  tested policy, not an assumed failure of the test. Fake write failure flag
  is never enabled, so write-error/shutdown coverage is incomplete.
- Callback review: CoreAudio allocates/primes three buffers, starts the queue,
  and stops/disposes before freeing state. PortAudio initializes, opens/starts,
  closes/terminates before unloading. SDL opts into audio subsystem ownership
  only if not already initialized, closes its device before releasing subsystem
  state. Concurrent external SDL subsystem shutdown is not addressed by that
  conditional ownership policy; host integration review belongs to app/console.
- PipeWire stops its thread loop before disconnect/destroy; initial connect
  success is asynchronous and open does not wait for a usable streaming state.
  Runtime errors mark the engine failed rather than trying another provider.
  PortAudio/SDL do not report ongoing device failure through failed callbacks.
  Default-device removal, async startup failure, and provider quiescence on
  error-returning close APIs have not been runtime-tested.
- WASAPI owns COM initialization/interfaces entirely in its worker, publishes
  initialization through a mutex/condition, and joins before releasing events.
  Buffer size is written before publication and immutable afterward. Its
  stop event gives callback-style shutdown an explicit wakeup, unlike blocking
  engine providers. Windows implementation compiles with MinGW but no Windows
  audio device test was run; macOS CoreAudio is source-only in this audit.
- Latency handling is provider-dependent: PipeWire returns a constant 24ms,
  CoreAudio/WASAPI use buffer capacity, SDL uses one obtained buffer, and
  PortAudio uses reported stream latency. These are not verified playback
  completion timestamps. Actual drain completion versus device consumption
  remains an integration test gap; latency conversions can truncate fractions.
- Synthesis uses bounded frequency, backward in-place mono-to-stereo expansion,
  and explicit integer wrapping for legacy noisy shapes. Checked xpbeep.c's
  waveform reference: SINE_HARM's overwritten intermediate assignments are
  explicitly intentional artifacts, not a newly introduced harmonic bug.
  Exact waveform equivalence, edge-cleaning at very short durations and
  physical speaker fallback are not tested by the current stereo-equality
  smoke assertions. No audible smoke test was executed.
- CMake separates policy AUTO/ON/OFF from detection and supports runtime-loaded
  optional libraries. Package probes compile only top-level headers, not all
  used API declarations/struct fields (notably PipeWire requested buffer field);
  version compatibility can fail later at build time. SDL is off in normal
  builds, so its source was
  inspected but not compiled here. Native configured providers: ALSA, OSS,
  PipeWire, PortAudio, PulseAudio. MinGW configured provider: WASAPI.
- Dependency direction is audio -> mixer/loader/threads; mixer is needlessly
  PUBLIC even though audio.h does not expose it. No dependency cycle observed.
  Real-time work/reclamation concern is A023, not a demonstrated timing result.

### theme/

Read all local files: st_theme.c, st_theme_document.c, st_theme_cloud.c,
theme_internal.h, both directory implementations, public header, local CMake
and the complete test. Expanded public-API tests exercise history branching,
reset persistence and directory errors. Native tests do not exercise actual
network downloads; Windows runtime and injected allocation/I/O failures remain
verification gaps. Host consumers are tracked under their own inventory rows.

- Theme loading clones built-in defaults before applying package overrides.
  Glyph fields resolve at load time; ASCII is mandatory for new glyphs,
  omitted CP437 falls back to ASCII, and omitted Codepoint is mapped from
  CP437. Existing built-in glyph overrides retain their other explicit built-in
  fields. Current tests explicitly expect overriding arrow.up ASCII alone
  to retain its built-in Unicode arrow. Unicode scalar validation rejects
  surrogates and out-of-range values. No locale-dependent classification in
  this loader's number/name parsing.
- Preview/select/apply load a replacement before freeing the prior theme.
  Cancel preview frees only the preview and increments generation. Returned
  theme/catalog pointers are borrowed and invalidated by corresponding
  replacement/free operations; the API's lifetime/threading documentation
  deserves clarification. No internal manager locking is present; caller
  serialization must be assessed in the host audit.
- Catalog refresh now stages a complete replacement and retains the prior
  catalog on failure. Each file is parsed into a full default clone for
  metadata, and duplicate-section detection is quadratic; no performance
  measurement yet.
- Parser allocation failures are sometimes reported as INVALID_FORMAT rather
  than OUT_OF_MEMORY.
- Directory backend discrepancies: POSIX skips non-UTF-8 names and stat errors;
  Windows stops on conversion errors and includes non-directory entries.
  Symlink/reparse handling and cloud downloaded-name validation require
  caller-specific review.
- Local build is strict C17 and target-scoped. It exposes the full settings
  dependency publicly though the theme header already uses forward declarations;
  this is a coupling observation, not authorization to redesign it. Core themes,
  document editing and online catalog/cache are distinct implementation files
  within one library. Network/crypto link requirements are pulled in for local
  theme consumers as well; evaluate only against actual consumer requirements,
  not an assumption that every implementation file needs its own library.
- Editor states deeply own metadata/style/glyph strings; previews are separate
  allocations. Documents borrow their manager and must not outlive it, as the
  header now states. Every mutation clones the entire state and
  builds a preview; history is unbounded, including redundant same-value edits.
  Indexed enumeration scans past removed entries, so enumerating all entries
  via *_at is quadratic. No UI workload benchmark performed.
- commit_state discards redo history before realloc succeeds, so failed edits
  can still remove redo. No allocation fault harness covers that path. Public
  errors also collapse preview allocation failures
  into invalid-format/incomplete-default-style messages.
- Document open reads the file three times (original bytes, parsed source,
  resolved theme); concurrent edits can mix versions. Save compares original
  bytes before opening the destination directly. The comparison is not locked
  through the write, so concurrent saves can evade conflict detection. No race
  test exists. Direct writes deliberately preserve symbolic- and hard-link
  relationships; case-insensitive POSIX filesystems still complicate filename
  identity.
- Cloud manifest parsing checks required repository/name/URI/hash and duplicate
  known fields/packages; unknown sections/keys remain allowed. Package IDs are
  ASCII lowercase/digit/dot/underscore/hyphen and appended with .ini, so bare
  dot IDs do not become parent-directory components. Windows reserved basenames
  are not excluded locally; webget/path behavior requires integration review.
- Download validation hashes the candidate and parses it before replacement.
  Cache-refresh also checks hashes, but startup/apply's load_package directly
  loads the named cached file without a manifest/hash check. This permits old
  cached versions offline; whether validation parity is desired is unresolved,
  not assumed malicious-content execution. Hashing then reopening is not a
  single immutable snapshot. Whole-file buffers have no local size limit for
  pre-existing cache files; network requests specify webget's size limit.
- Default manifest URL is plain HTTP and accepted entry URLs may be HTTP.
  SHA256 supplied by that same manifest establishes content agreement, not
  repository authentication against a network attacker. This is a trust-policy
  concern, not proof of code execution or permission to change network policy.
- Cloud refresh retains the old catalog on parse failure; fetch failure can
  return success with a warning when cached manifest parsing succeeds. Entries
  distinguish cached-on-disk from valid parsed theme; hash mismatch marks an
  update available, not necessarily corruption. Allocation failures when adding
  a missing selected-package placeholder are ignored. Public cloud preview and
  delete pass package to strcmp without validating NULL, unlike cache/select;
  API nullability/error contracts should be made explicit. No NULL crash test.
- Existing tests cover local preview/cancel, scalar validation, Unicode fallback,
  basic history, save/reopen/conflict and a valid cached manifest/hash. They do
  not cover live fetch, invalid hashes/manifest duplicate branches, most failed
  allocation paths, concurrent writers, all reset combinations or platform
  filesystem semantics. Those limits remain despite this source coverage.

### settings/

Read all local source/header/CMake/tests. Model and persistence are separated
into two implementation files, with a public process-global store and owned
snapshot struct. Instance copy is transactional and handles self-copy; snapshot
init zeroes rather than installing application defaults. Disposal nulls/frees
owned paths, fonts and name/value lists. A destination must be initialized.
No internal mutex protects the global store or font/sort/web mutation APIs;
external caller serialization and borrowed handle lifetimes are requirements
that the header does not fully spell out. File locks do not protect these
in-process memory accesses. Host consumers remain their own audit area.

- Save canonicalizes base/local/remote settings to avoid textual/default-only
  conflicts; scalar fields merge independently. Web lists/sort profiles and
  all Font sections are grouped conflict units, so disjoint edits within those
  groups can conflict. Documentation's 'same field' wording should distinguish
  collection granularity. Unknown unrelated fields/comments are retained;
  modifying Font sections reconstructs them and drops unknown keys/comments
  inside those sections. No preservation test there yet.
- Saved baseline_settings reflects local globals, while baseline_document
  contains merged remote changes. Globals do not absorb external changes on
  save. Subsequent saves continue to preserve externally changed fields if the
  local value stays unchanged; changing one later may conflict. This is an API
  semantics/documentation observation, not automatic authorization to reload.
- Same-process concurrent stores are not supported; load/reload/save depend on
  static state. Settings global is intentional application scope, not an audit
  mandate to replace it with a new abstraction. Dependency direction is toward
  INI/filesystem/locks/paths; app_paths and io_error are PUBLIC despite the
  header exposing only forward-declared paths and int statuses.
- Range handling remains inconsistent: dimensions/timeouts and Classic theme
  colours are bounded, while output, cursor and screen enum consumers require
  their own checks. ScalingFactor only excludes negatives; finite-but-huge
  input must be bounded by console sizing
  before narrowing. KDF is an opaque bounded string here; crypto consumer owns
  cost validation. No locale calls introduced.
- Custom font names are nonempty, INI-safe printable US-ASCII and unique under
  locale-independent ASCII case comparison. Duplicate sort/web-list names can
  still collapse under case-insensitive lookup. Sort update replaces the record
  before updating active name/order, so allocation failure can leave partially
  applied changes. Removing the last profile recreates defaults but can leave a
  partial list on allocation failure. Not injected.
- Legacy settings reference preserves VideoMode alias, theme-package precedence,
  KDF string migration, sort profile defaults/custom-order migration and the
  per-key Classic/UIFC fallback. This is not a full app-level legacy behavior
  claim.
- Existing tests cover owned copies, mixed independent/conflicting external
  settings edits, path redirection, sort and custom-font draft persistence,
  stale font snapshots, Classic/UIFC migration, colour bounds, unknown root
  content, basic empty sort values and host font-handle invalidation. Targeted
  injection covers initial-load and reload snapshot-copy failure plus both
  save snapshot copies. Actual simultaneous writers, most mutation allocation
  failures, disk-full writes and all field boundaries are not tested.

### bbs_list/

Read all local source/header/build/test files, including profile codec,
web-refresh worker logic and its full loopback test. Encryption, HTTP/TLS
implementation and application/Wren consumers remain separate inventory areas.

- Public list objects own profiles/diagnostics, paths and a copied password;
  close clears password bytes through crypto before freeing. Public getters
  document borrowed lifetimes, permit their values as mutation inputs and
  require caller-serialized access.
  Multiple objects can share disk paths, but also consult process-global
  settings for selected personal path and named web sources. Their behavior
  is not independent of those mutable globals.
- Precedence is personal, shared, system cache, configured named caches. Later
  duplicates replace earlier ones only within a source. ASCII case-folded name
  search is linear per appended section: full reconstruction is quadratic up
  to the advertised 100000-entry cap. No large-list rebuild benchmark yet.
  Sorting uses thread-local comparator context, not a process-global comparator;
  independent threads don't share fields, but sorting one list concurrently
  remains unsafe. Stable tie handling falls back to name, not original order.
- Store performs sidecar-locked reread/field merge/write. V1 ordinary writes
  explicitly return NOT_SUPPORTED; explicit rewrite handles migration. Writes
  prepare crypto output in tmpfile first, so crypto preparation failures occur
  before the destination is opened, then write the completed bytes through the
  destination while preserving its filesystem identity.
- Normal store writes use the object's retained write_options, not current
  remote encryption options. Same-password external rekey/parameter changes
  could be overwritten by a stale object's next edit; review with list_crypto
  before assigning a separate finding. Rewrite updates metadata from caller
  options without rereading normalized defaults, unlike install_document.
- Update/remove merge profile content and remove older duplicate sections.
  Case-only rename is treated as equal and not applied. Cache status is a
  secondary result after persistence; callers passing NULL cannot inspect it.
- Cache roots use validated portable raw keys or a bracketed hexadecimal key
  for display names which are not portable path components. Destructive
  boundaries reject other keys and sweep leaves unrecognized names untouched.
  Removal handles observed symlinks/reparse points without recursive following,
  but path-based check/open sequences can race with replacements; no race test.
  POSIX readdir errors are not distinguished from EOF and recursive depth is
  unbounded. Legacy raw keys still lack a dedicated ownership marker.
- Existing native list tests pass: precedence/duplicates, typed round trips,
  independent-field merge, conflicting edits/removal, cache rename/remove/sweep,
  confined unsafe-name keys, borrowed mutation inputs and encryption
  rewrite/reopen. Disposable failure tests also cover allocation rejection
  before persistence and failed-reload view preservation. They do not cover
  simultaneous threads/processes.
- Profile codec initializes a fresh profile, bounds copied fixed strings,
  connection kind/port/data bits/stop bits/music and palette length, and uses
  shared hex decoding for SSH fingerprints. It assumes its internal name
  argument was validated before strcpy; current append/update call paths do
  validate names. It is not a replacement-into-owned-profile API: init would
  discard pre-existing owned paths. Current callers dispose/init appropriately.
- Most other enum fields accept arbitrary numeric values from INI and narrow
  size_t to int; sort order and call count also narrow without bounds. Consumer
  range assumptions still need checking. No new
  array-indexing defect established within this codec: connection kind is
  validated before indexing its port table. Palette allows full uint32_t
  values, not just 24-bit RGB; renderer interpretation belongs to console.
  Malformed numeric palette parsing can fail the whole rebuild, not just emit
  a per-entry diagnostic. No malformed-palette regression yet.
- System sources suppress usernames/passwords, Wren scripts, calls/times,
  local file paths and PSK settings, while retaining general terminal options,
  SSH fingerprint and some SSH/telnet policy booleans. Connection-policy use of
  these downloaded fields remains a consumer trust-boundary review item, not
  an assumed prohibition on remote terminal configuration.
- Encoding defaults deliberately omits address, comment, Wren scripts, times,
  sort order and SSH fingerprint, while preserving many connection options.
  Root/default editor consumers must be checked for promises covering those
  omitted fields. Profile equality serializes two complete INI documents and
  compares bytes; allocation failure becomes inequality/conflict. It has
  avoidable work per removal but no measured timing regression here.
- Web batch allocates one worker/thread per configured list, with no concurrency
  cap. Public mutable progress fields and completion count are mutex-protected;
  callbacks run on the calling thread after copying fields and releasing the
  lock. Names/URLs are borrowed from global settings, not owned by the batch.
  Callback-driven or other-thread mutation of settings can invalidate them;
  required non-reentrancy/settings lifetime restrictions are undocumented.
- Normal worker shutdown joins before freeing batch/mutex/condition. Join
  failure is reported but cleanup still frees worker state; safe reclamation
  after a failed join is not established. Lock/unlock results are mostly ignored.
  No fault injection or thread sanitizer run. Progress waits poll at 200ms;
  failure to obtain UTC deadline falls back to completion-signalled indefinite
  wait, avoiding uninitialized deadline but losing intermediate updates.
- Batch progress callback is void and cannot cancel. It returns first failure
  in configuration order, not temporal failure order. Sequential list refresh
  stops at first failure and does not rebuild its view even if earlier caches
  were updated; the batch cache-only API independently attempts every item.
  These distinctions should be in the public contract.
- Web validation checks INI readability and section count, not that content is
  semantically a BBS list. Permissive INI parsing can accept an HTML error body
  as an empty document and permit replacement of cached content. Desired empty
  list semantics must be established before imposing a nonempty constraint.
  Duplicate named list names, including 'System List', can share output/cache
  keys. Exact URL metadata prevents cross-resource freshness and validator
  reuse, but duplicate names still share an output path and lock and can
  replace each other's cached body.
- Full web tests prove two downloads overlap via a server barrier, progress
  reports active/completed states, one failure does not prevent the other batch
  download, and indexed refresh avoids an unrelated unreachable URL. Servers
  are loopback only. CTest has a 15-second timeout. Missing coverage includes
  system-list branch, callback reentrancy, cancellation, thread-create/join
  failures, invalid cache names, malformed downloaded profiles and cache-key
  collisions. Windows test targets compile, but were not executed.

### webget/

Read all local implementation/header/CMake/test files. Synchronous request API
borrows inputs/callback contexts for the call; result is published on success.
The public header documents exact URL cache identity and preservation of the
old output until successful replacement, but gives little contract detail for
timeout defaults/infinity, progress cancellation, validation callback statuses
or partial success. Request includes TLS options and therefore exposes TLS
dependency; other dependencies are target-private except io_error. No platform
branches in the downloader itself. TLS/socket/lock platform behavior belongs
to those rows.

- URL parser rejects userinfo, fragments and literal control/space bytes, handles
  bracketed hosts, decimal ports and query-only targets. Allocation failures and
  parse failures are cleaned up by the caller. No redirect, proxy, compression
  negotiation, IDNA conversion or informational-response support is present;
  whether required by existing SyncTERM consumers needs integration comparison.
- TCP and TLS operations use per-operation timeouts, not one overall body
  deadline. Slow streaming can extend the whole call indefinitely. Cancellation
  is checked at phase transitions/body progress and via TCP callback, but not
  during every stalled read, lock acquisition or TLS handshake. All caller
  strings must remain stable even while invoking user callbacks.
- HTTP headers are bounded to 64KiB total/8192-byte lines. reader_line removes
  every CR, accepts bare LF and does not reject NUL. Header names/values are
  permissive; duplicate Content-Length overwrites prior values, and chunked
  takes precedence if both framing headers appear. These are protocol-hardening
  concerns pending dedicated malformed-response tests, not a smuggling exploit
  claim for this single-request Connection:close client.
- Fixed/chunked/close-delimited bodies enforce the configured maximum while
  writing. Chunked's header_bytes budget accumulates over every chunk line and
  trailer, so many small legitimate chunks can hit the 64KiB limit despite a
  small body. Chunk/trailer timeout errors sometimes collapse to PROTOCOL.
  Only fixed-size and 304 paths are in existing integration tests.
- HTTP dates use ASCII Gregorian arithmetic, not system locale. Only the
  preferred 29-byte date shape is parsed; weekday text is ignored. Cache-Control
  parser handles a small subset and ignores no-store/quoted max-age. A complete
  HTTP-cache conformance claim is not established. Signed subtraction involving
  extreme on-disk Date/Expires values can overflow; no UBSan metadata fixture.
- Cache keys/output names are component-validated, but callers can choose
  colliding output/.new/.cacheinfo names. Lock is keyed by metadata name, not
  output identity. Fixed temporary names are opened wb, not exclusively;
  symlink/cross-key alias behavior requires a disposable-file test. Cleanup may
  remove fixed temporary paths even on failure before acquiring their lock.
- Cache metadata records the exact requested URL. Missing legacy identity or a
  changed URL discards freshness and validators in memory, makes an
  unconditional request, and preserves the existing body and on-disk metadata
  if replacement fails. An unsolicited 304 without ETag or Last-Modified
  conditions is rejected. Tests cover changed URLs, legacy metadata, old-body
  preservation, validator suppression and successful rebinding.
- Fresh-cache and 304 paths skip validate, maximum-size enforcement and most
  progress callbacks. Existing tests explicitly expect no repeated validation.
  This conflicts with theme's desire to redownload after an updated manifest
  hash: theme rejects old cached bytes, but webget may still return the fresh
  old file. No end-to-end theme update regression yet; API needs an explicit
  policy for callers whose validation requirements change.
- Download installation renames the completed temporary before allocating/writing
  new metadata. Later failure can return error after output changed and leave
  old validators paired with new bytes. Body and metadata are not one atomic
  transaction; no crash/write-failure injection. Neither temporary is synced
  here. A cached 304 requires an existing regular output and at least one sent
  validator; missing cached output can still send stored validators and fail on
  304 rather than retrying unconditionally.
- Normal cleanup frees URL/cache/header buffers and runtime references. A
  link-time fake transport test verifies that TLS cancellation and failed open
  close the raw socket exactly once, while successful TLS ownership closes only
  through the TLS session. Existing integration tests cover fixed download,
  ETag conditional response, fresh hit, default one-day caching and the cache
  identity cases above. No real TLS handshake, chunked/EOF body, callback
  rejection, malformed headers, network interruption, disk-full or concurrent
  cache-key collision coverage yet. Focused tests pass under native Clang, GCC
  and ASan; the MinGW targets build and execute under Wine.

### list_crypto/

Read public/internal headers, full format reader/writer, IDEA/RC2 legacy
decoders, CMake and complete tests. This is compatibility/implementation
review, not independent cryptographic certification. Provider implementations
and cost limits remain under xptls_crypto; BBS persistence is covered separately.
No new confirmed functional defect assigned in this pass.

- Format boundary is explicit: ASCII first-line metadata, followed by binary
  two-byte big-endian IV length, IV and ciphertext (RC4 omits IV fields).
  v2 salt is hex, v1 salt printable text. Cipher/key sizes, nonempty salt,
  header size, IV size, block alignment and decimal overflow are checked.
  Exact encryption prefixes identify format; a damaged prefix may instead be
  treated as plain INI. This is not an authenticated format and the documented
  DecryptionCheck marker is not represented here as an integrity guarantee.
  The user's stored-password/legacy-compatibility scope is preserved; no
  speculative replacement format or mandatory authentication redesign proposed.
- Inspection maps the complete file even though it uses only the first line;
  first-newline scan can traverse the whole mapping. Read has no caller-selected
  file-size/resource ceiling and allocates a full ciphertext/plaintext copy
  before KDF work. Header KDF values are checked by provider parameter validation,
  with PBKDF2 up to INT_MAX and scrypt memory guard delegated to xp_kdf. This
  does not establish an acceptable interactive CPU budget for every accepted
  parameter. No expensive adversarial-cost execution performed.
- Read publishes document/info only after parse and marker checks succeed,
  preserving initialized prior output on failure. V1 iteration override is
  capped at INT_MAX, defaults to 50000; inspect cannot know that caller value.
  The stream contract relies on file_map_open_stream. Existing tests verify
  unchanged file position; EOF/error indicators and unsupported stream kinds
  are not independently exercised here.
- Write accepts only AES-128/256 and ChaCha20, using v2 scrypt; key_bits zero
  means 256. Other KDF fields are required, not implicitly filled with defaults.
  Caller supplies an already positioned/truncated stream and owns flush/close
  and replacement atomicity. Header comment says NONE ignores password fields,
  but NULL password with nonzero count is rejected before the NONE branch: a
  minor documented-contract inconsistency, not an observed consumer failure.
- Key/IV/salt buffers and decrypted/ciphertext working buffers are explicitly
  cleared on cleanup. Not all transient plaintext copies are: document_for_write
  frees its serialized source without clearing, and temporary INI documents use
  ordinary disposal. Returned decrypted documents necessarily retain secrets.
  Do not claim complete heap erasure or locked-memory handling from the working
  buffer wipes. Legacy IDEA leaves rotated/intermediate scalar key material
  uncleared even though its final key schedules are cleared. No memory-forensics
  or compiler-erasure verification performed.
- Legacy decoders are decrypt-only, bounded by fixed block/key sizes, use
  explicit endian loads/stores and preserve prior ciphertext for CBC chaining.
  Direct known vectors and full legacy fixtures pass in the available builds;
  independent randomized differential verification against a reference cipher
  has not been done. Small-integer promotion assumes the supported normal C17
  toolchain integer widths; no exotic 16-bit-int target claim.
- Header scrypt parser accepts repeated r/p tags with last value winning;
  inspection's encryption_available checks provider name, not an individual
  cipher/KDF policy probe. Crypto failures are deliberately coarsened to
  DECRYPTION_FAILED/IO except disabled/unsupported. Operational provider failure
  may consequently look like a bad password. Document clone/serialize/parsing
  duplicates whole-file allocations; no performance benchmark in this pass.
- Existing tests cover plain preservation, modern round trips, wrong passwords,
  output preservation, malformed/truncated headers, key restrictions, v1/v2
  fixtures and direct IDEA/RC2 vectors. Added counted {'a', NUL, 'b'} password
  test rejects the one-byte prefix and accepts the full count. Clang/GCC pass;
  MinGW compile/link only. Tests skip provider-unsupported operations, so green
  totals do not prove every legacy cipher is enabled. No provider-disabled
  configuration, fault injection, fuzzing or Windows runtime run here.

### xptls/

Read the public header, OpenSSL/Botan/unavailable implementations and CMake
completely. The component is cohesive client-only TLS; sockets and generic
errors are its public dependencies, provider details private. CMake uses the
crypto provider selection and imported OpenSSL/Botan targets, C17 or C++20
without extensions, and permits standalone configuration. When a production
TLS provider and an OpenSSL test fixture are available, CMake registers a
portable loopback test covering TLS 1.2 and TLS 1.3 PSK success, wrong-secret
rejection and certificate-fallback rejection. Native OpenSSL and Botan runs
pass; no Windows runtime verification was available.

- Ownership of a successful session/socket and peer DER is documented.
  Failure leaves the socket open but both providers force it to blocking after
  a failed nonblocking handshake, regardless of its original mode. The header
  does not promise restoration; this needs a clear contract before reuse.
  Other missing contracts include timeout units/default/infinite behavior,
  zero-sized I/O, accepted-versus-delivered send counts, retry rules after a
  timeout, thread safety, diagnostic callback lifetime/reentrancy, and whether
  CERTIFICATE means a trust anchor or an exact peer pin. Implementations use
  trust stores, not an explicit leaf-fingerprint comparison.
- Provider I/O semantics differ: Botan queues the whole send and reports
  success if its subsequent flush times out; OpenSSL returns the timeout with
  a zero count while an SSL write may still need continuation. Botan receive
  pumps input before flushing previously queued output. A caller expecting
  send-success to mean delivered bytes can therefore wait for a response to
  bytes still queued locally. This is a contract/design concern pending a
  forced-backpressure fixture, not a reproduced ordinary webget failure.
- Botan's read/write mutexes do not establish one common invariant for its
  ciphertext deque: send/flush and receive can both flush it, and received_data
  can append through callbacks without taking provider_mutex. terminate takes
  provider_mutex but pump does not. OpenSSL has no corresponding session
  locking, and its terminating flag is plain bool. Concurrent calls cannot be
  presumed safe from Botan's partial locks. The visible webget caller uses
  each session sequentially; no current concurrent caller defect is claimed.
  Define the intended contract before exposing these objects to duplex workers.
- Botan flush constructs a vector of the entire pending deque on each partial
  send, then pops bytes individually; record callbacks similarly enqueue bytes.
  This can amplify copying under backpressure. No throughput regression or
  comparative metric has been established. Allocation failure in flush_pending
  is outside the try blocks in open/send/receive/flush, allowing a C++ exception
  to escape an extern-C entry point instead of returning the API's error code.
  Error reporting itself assigns std::string and can allocate. No OOM injection
  performed; treat this as a conditional failure-path concern.
- Both providers validate PSK presence/version combinations but do not reject
  all invalid enum values or reversed min/max version ranges explicitly.
  OpenSSL's unrecognized server_auth falls through to VERIFY_NONE, whereas
  Botan enables verification for any non-NONE value. Invalid caller input is
  not a valid-input authentication failure.
- OpenSSL's TLS 1.3 PSK callback always supplies a SHA-256 session and ignores
  the requested digest; primary documentation requires compatibility when the
  digest is supplied. Botan filters non-SHA-256 PSK requests. No second-callback
  interoperability failure reproduced. OpenSSL also does not explicitly clear
  its error queue before SSL operations; stale diagnostic/error classification
  and post-timeout write retry behavior need provider-specific tests.
- Botan Deadline adds arbitrary nonnegative int64 milliseconds to a steady
  clock time point without a representability check; large finite timeouts
  need boundary tests. Normal values were used in the handshake regression.
  Peer certificate copies are session-owned; Botan can replace them on another
  verify callback, which would conflict with the unconditional until-close
  borrowed-data promise if renegotiation is allowed. Not reproduced.
- Missing verification includes real trusted/untrusted chains, IP-literal
  identity checking, mutual TLS, version bounds, record fragmentation,
  close_notify versus abrupt EOF,
  backpressure/retry, cancellation, allocation failures, and Windows runtime.
  The unavailable provider is source-reviewed only in this pass. Existing HTTP
  loopback tests do not exercise TLS.

### xptls_crypto/

Read every local file, including the built-in SHA-256 implementation, all six
clear implementations, both test files and provider-selection CMake. Narrow
whole-buffer cipher/digest/KDF interfaces match the list-encryption consumer;
provider ownership stays private. Error values are a separate crypto namespace,
translated at consumers, rather than provider-specific error codes leaking
through the API. Headers have C linkage guards and standard type includes.

- Cipher documentation explicitly permits exact in-place operation and defines
  IV/counter layout and no-padding CBC. Added in-place AES encrypt/decrypt
  checks pass with both providers. Existing known-answer AES/ChaCha/PBKDF2 and
  scrypt checks pass. Unsupported legacy encryption is reported by
  xp_cipher_supported(), but xp_cipher_crypt() does not enforce that policy in
  either provider. Clarify whether supported() describes recommended policy or
  executable capability; no current list writer uses a legacy cipher.
- Buffer argument validation checks NULL/count pairs and output capacity.
  OpenSSL chunks inputs to its int-sized API and aligns chunks to block size;
  Botan CBC copies the entire input through secure_vector, adding a whole-file
  allocation to list encryption. Partial overlap (other than exact in-place),
  failure-output contents, zero-length output behavior and valid key lengths
  are not fully specified. CBC partial-block errors can leave partial output
  in OpenSSL, unlike Botan's copy-on-success path. No caller atomic-output
  promise was found, so this is a contract concern rather than a new finding.
- KDF counted-byte inputs preserve embedded NULs; list_crypto's added password
  test covers that consumer with both providers. The common scrypt memory
  predicate uses division/subtraction to avoid overflow when checking
  128*r*(N+p+2), rejects non-power-of-two N and zero parameters, and caps the
  accounted memory at 1025 MiB. This is not a CPU-work limit. OpenSSL limits
  PBKDF2 iterations and lengths to INT_MAX; Botan accepts size_t iterations.
  The list reader independently limits iterations to INT_MAX. Hostile-cost CPU
  exhaustion and near-limit actual allocation were not exercised.
- Whole-buffer ChaCha20 exposes a 32-bit counter with a 12-byte IETF nonce,
  but neither wrapper checks whether input length crosses the remaining
  32-bit block-counter space. Provider behavior at exhaustion is not tested;
  interoperability and rejection need explicit bounds before claiming support
  for such inputs. No large-file test was attempted.
- Botan generally catches exceptions, but cipher_name string construction in
  supported()/crypt() and static provider-version initialization happen outside
  catches. Allocation failure could escape the C ABI instead of producing the
  declared error/boolean result. OpenSSL's legacy-provider load uses once and
  intentionally retains the provider; it operates on the default library
  context, so loading it changes process-wide provider availability. Provider
  absence/loading failure and concurrent initialization were not injected.
- The none provider retains SHA-256 for package integrity while disabling
  RNG/ciphers/KDFs. It exposes digest sizes for SHA-384/512 without implementing
  them; size is not a capability query. Reviewed padding, endianness, unsigned
  arithmetic and NULL/zero handling. New known-answer empty and 56-byte SHA-256
  checks cover the second padding block, complementing existing abc. They pass
  on all three providers, as do invalid NULL/nonzero and short-output checks.
  This is limited regression coverage, not cryptographic certification.
- Clear selection prefers explicitly non-elidable system/provider facilities;
  fallback writes through volatile bytes and is explicitly best-effort. All
  paths accept NULL with zero size by avoiding access. Tests establish visible
  zeroing, not erasure of registers, other copies, swap, or allocator history.
  Existing clear fallback testing is opt-in, consistent with project policy.
- CMake's C probes select C17/no extensions and Linux environment definitions
  matching the target. Botan probing sets C++20 but does not explicitly match
  the target's no-extensions setting; probes exercise provider linkage, not all
  required algorithms. Explicit provider absence fails configuration; auto can
  select none. Cross-build auto selection intentionally avoids the native
  Botan probe. Cached usability values after dependency/toolchain changes and
  all alternate system-clear choices were not exhaustively configured.

Verification: normal build OpenSSL and build-gcc Botan xp_crypto CTests pass
after rebuilding. Fresh standalone configuration at
/tmp/st-crypto-audit.ckTcuL with CRYPTO_BACKEND=none and
XPTLS_CRYPTO_TEST_CLEAR_FALLBACK=ON builds and passes both CTests. MinGW builds
the expanded test but was not executed. No implementation/CMake changes or
commits. Remaining gaps include allocation/provider-failure injection, legacy
cipher key-size coverage, SHA-384/512 vectors, long multi-block hash vectors,
counter exhaustion, large-file performance and non-native runtime verification.

### codepage/

Read all four files. This is an allocation-free, locale-independent CP437
mapping component, not a general encoding conversion framework. Forward lookup
is constant-time; reverse lookup scans at most 159 non-ASCII display mappings
after its ASCII fast path. Constant tables need no initialization, synchronization
or ownership protocol. Exhaustive display-mode roundtrip checks all 256 bytes;
expanded tests check all low control identities, DEL/display distinction,
invalid scalars and unchanged output on failure. Clang/GCC pass and MinGW
compiles/links; no Windows execution.

The reverse operation deliberately accepts U+0000..U+001F in addition to display
symbols, so it is not a bijection, and U+007F does not reverse-map despite its
forward non-graphic representation. Header wording could make that asymmetry
and unchanged-on-failure behavior explicit. No replacement character is silently
chosen. Tables and code contain no locale-dependent functions. Public C++ linkage
guards are absent; present C callers work, and no C++ caller requirement is
established. The standalone CMake branch does not include CTest, unlike nearby
components; top-level test registration works. Consumer control-versus-graphic
choices in console/Wren remain assigned to those inventory rows. No source-table
comparison against an external CP437 mapping was performed; roundtrip alone
does not independently certify the semantic identity of every entry.

### unicode_width/

Read header, generator, upstream notes, CMake and test; inspected generated
runtime/header plus mechanically checked all emitted intervals for ordering,
nonoverlap and scalar-range bounds. Fresh generator run using pinned SHA-256
checked Unicode 17.0.0 inputs wrote only under
/tmp/st-unicode-audit.lfl3AA; cmp reports byte-for-byte identity with the committed
generated source. No production regeneration occurred. The generated tables
therefore match the reviewed generator and pinned inputs; this does not claim
review of every Unicode assignment by eye.

The API separates EAW and emoji properties from the fixed application policy:
W/F or Emoji_Presentation means two columns; other scalars mean one, invalid
scalars zero. In particular controls and combining scalars are not wcwidth-style
zero-width characters here. That is explicitly documented, not a locale regression
or a defect to fix by introducing wcwidth. Sequence/grapheme presentation must
be handled elsewhere. All lookups binary-search immutable tables with bounded
midpoint arithmetic, are allocation-free and have no mutable global state.

The generator verifies both source hashes before output, merges the width union
and emoji ranges, and emits named EAW ranges in input order. It relies on pinned
input ordering; a future version update should validate sorted/nonoverlapping
named ranges rather than silently assuming it. It also ignores comment-based
missing-value declarations and defaults unmatched EAW to neutral; current
regeneration equality plus sampled CJK defaults are verified, not an independent
proof against every normative default rule. Network downloads have no explicit
timeout/size limit, acceptable only as an offline maintenance tool concern,
not an application runtime issue. Generator exceptions during writing can leave
partial output; normal builds never run it. Provenance/version/hashes and Unicode
license attribution are recorded; full license packaging review is still pending.

Expanded tests exhaust every value through U+10FFFF, checking all surrogate
rejections and the agreement of the separately stored EAW/emoji/column tables,
plus UINT32_MAX. Clang/GCC CTests pass; MinGW compiles/links, not run. This
consistency test cannot independently catch a wrong property shared by all
tables. External conformance/property-file comparison and font/grapheme consumer
behavior remain verification limits. Only test/report changes, no commits.

### unicode_normalization/ and unicode_grapheme/

Read all non-generated files in both directories, including full generators
and their emitted runtime functions. Regenerated each under
/tmp/st-unicode-audit.lfl3AA from downloaded, hash-verified pinned Unicode
17.0.0 sources; both generated C files compare byte-for-byte identical with
the repository. No production outputs were rewritten. Generated bulk data was
checked mechanically, not reviewed assignment by assignment: normalization
contains 403 ordered CCC ranges, 2081 ordered decompositions with bounded
offsets/counts (maximum four scalars), and 961 lexicographically ordered
composition pairs. Grapheme data has 1429 nonoverlapping ordered break ranges,
505 Indic ranges and 451 pictographic ranges. All 5699 trie nodes and 5698
edges have bounded offsets/counts, sorted children and acyclic forward links.

- Responsibilities are appropriately limited: canonical decomposition/CCC/
  pair composition and scalar grapheme properties/RGI longest-prefix matching.
  Neither API claims complete NFC/NFD or full grapheme-boundary state. Ordering,
  composition blocking, regional-indicator parity, Indic context and sequence
  rendering belong to callers; those are not certified by this component pass.
  APIs take counted scalars, allocate nothing, use immutable tables and have no
  locale dependency or shared mutable state. Binary-search midpoint arithmetic
  is bounded and trie matching stops at the first failed edge.
- Decomposition returns required length without modifying an undersized output;
  NULL/zero is a size query, NULL/nonzero is rejected. It handles Hangul
  algorithmically and recursively expanded canonical table mappings; tagged
  compatibility decompositions are deliberately excluded. Full composition
  exclusions are respected by table generation. CCC zero also represents
  invalid/unassigned input; grapheme properties similarly lack a separate
  invalid-scalar result. The public contract could explicitly describe these
  defaults, failed-compose output preservation and NULL emoji input semantics.
- Generator expansion detects cycles and sorts composition/decomposition keys.
  UnicodeData First/Last ranges are not expanded, so a future input version
  with relevant nonzero CCC or decomposition fields on such ranges would need
  handling. Current pinned output fits its uint8 decomposition count and uint32
  offset fields; generator-side size assertions would make future updates
  fail clearly rather than relying on compiler diagnostics.
- Grapheme generation sorts property ranges, builds a deterministic sorted-child
  trie, and uses uint16 child counts/uint32 indexes. Current bounds verified;
  it does not explicitly validate all emitted integer-width limits. The emoji
  parser accepts every non-comment sequence row from the two pinned sequence
  files without filtering property names. Correct RGI scope consequently relies
  on those source files' selected categories; independent source-conformance
  checking remains a gap, not evidence of an observed false match.
- Both generators verify source hashes before emitting files, record version and
  license provenance, and support local offline sources. They are maintenance
  tools, not normal-build network requirements. Like the width generator,
  downloads lack explicit timeouts and output writes are not transactional;
  exceptions can leave a partial generated file if run against the real tree.
  No such rewrite was performed during the audit.
- Tests expanded for query/invalid/short-output contracts, all 11,172 Hangul
  syllables decomposing and recomposing (including aliased composition output),
  and truncated emoji sequences retaining only the longest complete prefix.
  Both native Clang/GCC CTests pass; MinGW builds both tests but was not run.
  Existing tests exercise an excluded composition and representative Indic,
  Hangul, ZWJ, flag and keycap cases. No complete NormalizationTest or
  GraphemeBreakTest conformance run is claimed, nor would testing these oracles
  alone establish correctness of the caller's whole algorithms. Full RGI
  sequence corpus matching and malformed generator-fixture tests remain gaps.
  Only tests/report changed; no commits or implementation changes.

### fonts/ and unifont/ — initial pass

Read all small public/private headers, catalog implementation, both CMake files,
both tests, upstream notes and the complete shared update_fonts.py generator.
The catalog returns borrowed, process-lifetime immutable bitmap/mapping storage
for exact dimensions only; rejected selector/dimension/NULL requests do not
write the output. No runtime allocation, locale use or synchronization is needed
in this library. The console owns dynamic registration/lifetime/lookup policy;
that much larger implementation remains pending in the console inventory row.

Mechanically extracted legacy allfonts.c string-literal bitmap bytes and
compared them to every local font_<selector>_<size> array for selectors 0–45:
all 98 available bitmap variants match byte-for-byte and absent variants remain
absent. This checks actual preserved data, not just the test's availability
matrix. Legacy Unicode mapping equivalence and complete catalog metadata review
remain pending. The legacy file contains further empty/dynamic slots; no claim
is made that all 257 legacy records should be built-in modern fonts.

The generator pins and SHA-256 verifies UNSCII and Unifont inputs, rejects
duplicate codepoints/invalid scalar ranges and malformed bitmap lengths, sorts
glyphs into narrow/wide arrays and overlays UNSCII on Unifont by codepoint.
Generated mappings index narrow then wide storage. Fallback selection prefers
a narrow U+FFFD, then '?'; absent wide replacement is synthesized by centering
the narrow bitmap in a 16-pixel row. It does not create a new Unicode mapping
for that synthetic fallback. This is constant asset construction, not runtime
copying. Negative codepoints are not explicitly rejected by parse_hex, but
inputs are pinned/hashes checked; malformed-fixture validation is not yet tested.

Fresh generator execution wrote only below /tmp/st-unicode-audit.lfl3AA;
all three outputs (UNSCII 8/16 and the Unifont composite) match the repository
byte-for-byte. Native Clang fonts/unifont CTests pass using the existing headless
console backend; no user's display/window was used. Tests cover every available
catalog dimension through registration, representative selected-font versus
fallback behavior, wide replacement cells, and one composite overlay match.
They do not yet establish every Unicode mapping or every composite glyph.

Unifont is isolated in its own library and top-level option, with upstream notes
explicitly choosing GPLv2-or-later for derived data and explaining the executable
licensing consequence. This is an observation of project documentation, not a
new legal determination. Full license text/packaging/build-options display,
optional-disabled build verification, remaining metadata and mapping coverage
are pending. No production changes or new tests/commits in this initial pass.

### Font mapping and optional-library follow-up

Mechanically checked every codepage_N_mapping array: Unicode keys strictly
increase, are scalars, and every mapped glyph is below 256. Compared mappings
against legacy forward tables using the legacy control-graphics and broken-bar
rules. Most agree with the first glyph for each codepoint; differences require
reverse-selection semantics rather than assuming forward maps are bijective:
CP1251 and ARMSCII select their printable section-sign slots, ATASCII selects
several high-half glyphs (also visible in its legacy reverse table), Prestel
omits 32 raw control mappings, and Atari ST selects one of multiple replacement
glyph slots. These are not classified as new defects. Complete reverse-map/
rendering equivalence remains unresolved, particularly duplicated or inverse
glyphs; bounds checking alone is not a semantic proof.

Read the bundled GPLv2 text in full. Top-level CMake conditionally adds unifont;
app CMake conditionally links it and defines ST_HAVE_UNIFONT. Startup chooses
UNSCII for 8-row cells and, for other heights, the Unifont composite only under
that definition, otherwise UNSCII 16. Search located the build-options disclosure
and its test in Wren bindings, but their behavioral review is deferred until
the required Wren guidance is read in the Wren pass. No new legal conclusion
or licensing-policy change is proposed. Full installation/packaging and actual
option-disabled build verification remain pending.

GCC fonts/unifont CTests pass as well as the earlier Clang run. MinGW builds
both test executables; Windows runtime remains unverified. Only this audit
report changed in the follow-up, with no commits.

### Font-library completion evidence

All 46 catalog names, codepage associations and broken-vertical flags now
mechanically compare equal to the legacy metadata, in addition to the 98 bitmap
comparisons already recorded. Mapping bounds and source comparisons are complete
to the extent described above; duplicated/inverse glyph rendering semantics
remain a verification limit, not a claim of complete visual equivalence.

Expanded unifont test iterates every UNSCII-16 mapping, looks up that codepoint
in the composite through the real console API and verifies exact bitmap bytes
and narrow/wide column count. Clang/GCC pass; MinGW compiles/links, not executed.
Fresh top-level Clang configuration at /tmp/st-no-unifont-audit.lJ1LmH with
SYNCTERM_ENABLE_UNIFONT=OFF and CRYPTO_BACKEND=OpenSSL builds syncterm_app and
st_fonts_test successfully; fonts CTest passes. Neither the application's link
line nor nm symbol listing contains st_unifont. The application itself was not
launched, so this does not claim menu display or GUI startup verification.

Font and optional-data component source coverage is now marked Reviewed with
these explicit limits. Full install/license packaging remains in the root
documentation/build inventory, and the menu's build-options disclosure remains
in the Wren-host inventory. The libraries publicly depend on st_console to
share its font structure; this ties static data packaging to a larger console
target and prevents a truly independent fonts-only configuration unless that
target exists. This is a dependency/layout concern, not authorization to move
types or redesign the build. No production changes or commits; only the overlay
test and audit report changed this pass.

### pixels/ — initial pass

Read st_pixels.h, both private headers, README, all st_pixels.c and st_scaler.c,
and st_pixels_test.c. Filter implementations, scaler tests/benchmark and CMake
remain pending. Buffer ownership is explicit, creates/clones publish only on
success, alternate allocation copies the primary, and self-blits clone before
writing. Public structs permit direct access, so callers must preserve size/
allocation invariants; two distinct structs sharing planes are not covered by
the source==destination self-blit check. Masks pack rows without byte padding;
the README is clearer about this than the short header comment.

Allocation count helpers bound width*height and byte multiplication; blits use
64-bit scaled bounds, validate source/mask extents by subtraction and clip
destination coordinates before indexing. Self-blits copy the whole plane even
for small rectangles. No performance measurement yet. Compatibility scaling
allocates intermediate planes and persistent scaling retains stage buffers.
Persistent full-scale calls rebuild maps even when dimensions are unchanged;
the README's 'prepared once per plan' wording is not sufficient evidence of a
cached no-rebuild fast path. Regional updates avoid those allocations/maps.

Borrowed scaler output lifetime and region prerequisites are partially
documented; concurrency, output/source aliasing, invalidation after a failed
scale, and empty output damage need explicit contracts. A failed plan can
invalidate the scaler and reallocate storage previously borrowed by the caller,
consistent with 'until next scale call' but not obviously with the header's
broader 'results unchanged on failure' phrase. No allocation-failure test yet.
The same typed-pointer-to-void** helper pattern noted in A007 appears in
ensure_array calls; no separate redesign or duplicate finding is introduced.

### pixels/ — filter and verification completion

Read both filters, CMake, the full scaler test and standalone benchmark. Pointy
uses local equality-based corner rules for 3/5/odd factors; xBR uses packed RGB
with YCoCg distance, never palette-index lookup. Boundary sampling clamps to
edge pixels. Regional execution expands dependency radii by one for pointy and
two for xBR, then propagates through each retained intermediate output. Scale
policy is blocky or pointy; xBR is a stage, not a third user policy. The xBR
source retains its LGPL-2.1-or-later notice; packaging implications remain under
root license review, not a new relicensing determination here.

New small-edge test iterates source dimensions 1..4 on both axes, factors 2..7
with a noninteger vertical finish, both policies, and every source pixel.
After each mutation it verifies bounded output damage and full equality between
regional and independently recomputed output. All pass native Clang/GCC and
Clang ASan+UBSan; existing pointy hashes and larger multistage region cases also
pass. MinGW builds normal and expanded test targets, not executed.

Large-dimension arithmetic remains a concern: public pointy planning checks
target dimensions against INT_MAX, but xBR computes int nl1=2*output_width and
nl2=3*output_width, requiring tighter representability than that bound. This
conditional source concern has not been exercised with huge allocations.
Internal filter functions assume validated dimensions/regions, and are not
independently safe arbitrary-input APIs. Allocation-failure injection, caller
aliasing, exhaustive downscale damage and original-implementation output
equivalence remain verification gaps.

The benchmark is appropriately separate from CTest and measures the common
720x400 -> 1440x1080 case (integer horizontal, interpolated vertical), plus
one-cell/cursor damage. Its old-block/old-point cases are a reconstructed
pipeline using benchmark copies of multiplication/interpolation and the same
current st_xbr_filter, not compiled original legacy source. Its short checksums
sample pixels rather than prove complete frame equality. Thus its labels must
not be reported as independent proof of original-code performance/correctness.
Damage benchmarks mutate only one pixel inside their advertised rectangle,
which also limits representativeness for dense text changes/scrolling.

Fresh standalone Release build /tmp/st-pixels-audit.xvPU3K passes both pixel
CTests. One local five-frame benchmark run measured blocky 5.147 ms/frame,
reconstructed old-block 8.007, allocating blocky 9.857, pointy 81.837 and
reconstructed old-point 81.046. Blocky cell/cursor updates measured 4.266/0.795
microseconds and pointy cell update 68.850 microseconds. These are observations
on this host/run, not stable thresholds, multi-platform comparisons or verified
original-code metrics. No benchmark rewrite or performance remediation done.
Only tests/report changed; no commits.

## Verification

Available builds are Clang (build), GCC (build-gcc), and MinGW (build-win64);
availability is not a tested-platform claim.
No implementation is modified to obtain passing tests. Record real failures,
unavailable runtimes and remaining uncertainty rather than treating them as
successful verification.

- List crypto pass: rebuilt st_list_crypto_test for Clang/GCC/MinGW; both native
  CTests pass with the new embedded-NUL password boundary test. MinGW was not
  executed. Only list_crypto/tests/st_list_crypto_test.c and this report changed
  in this pass. No production changes or commits.

- BBS codec/web completion pass: both bbs_list and bbs_web pass with native
  Clang and GCC after CMake target builds. Both MinGW targets compile/link;
  no Windows runtime claim. This pass changes only the coverage/report notes.
  Existing audit tests remain unchanged, and no production changes or commits
  were made.

- Hex and UTF-8: both targets rebuilt through CMake and their CTests passed
  under Clang and GCC. Expanded UTF-8 tests cover all scalar values through
  U+110000 (surrogates and U+110000 rejected), independent known encodings at
  width/surrogate boundaries, every truncated prefix of those vectors,
  unchanged undersized outputs, invalid continuation/overlong sequences,
  isolated non-ASCII bytes and NULL arguments. Hex adds every invalid byte at
  each position of a four-byte input and checks the entire destination remains
  unchanged. Existing valid vectors and all-byte binary round trips retained.
- Both expanded test targets built successfully for MinGW; no Windows runtime
  execution claimed for these audit additions. macOS, Haiku and MSVC were not
  tested. Sources contain no platform branches, but that is not runtime evidence.
- Existing string_util CTest passes under both native compilers. No new test
  or implementation change in string_util during this pass.
- Only hex/tests/st_hex_test.c, utf8/tests/st_utf8_test.c and this audit report
  changed during this pass. No production/header/build/documentation edits,
  commits, pushes or reverts performed as part of this pass.
- Event/semaphore pass: both targets rebuilt with Clang and GCC, both CTests
  passed ten consecutive runs per compiler. MinGW targets compiled and linked;
  no Windows runtime claim. Only their test sources and this report changed.
  No production changes or additional commits made during this pass.
- Dynamic-library pass: native Clang/GCC fixture CTests pass and MinGW target
  builds. No test or production source changes in that component.
- File-map/file-lock pass: native Clang/GCC CTests pass and MinGW targets build.
  The first build of the new stream-lifetime test caught an undeclared boolean
  literal in the test addition; corrected the test before the passing runs.
  No implementation changes or commits in this pass.
- Collection pass: array_list, str_list, str_list_failures and name_value_list
  CTests pass under Clang/GCC; MinGW targets build. Standalone array tests also
  pass with Clang -O3 and GCC -O3 -flto -Wstrict-aliasing=2, without diagnostics.
  These are not evidence of Windows runtime behavior or general alias safety.
  Only str_list's test and this report changed in this pass; no commits.
- String-helper pass: normal string_compat and expanded string_util tests pass
  under native Clang/GCC. A fresh standalone Clang CMake build at
  /tmp/st-string-audit.LRX7tn with STRING_COMPAT_TEST_FALLBACKS=ON passes both
  native and forced-fallback tests. All expanded helper tests, including
  windows_utf, compile/link with MinGW. No Windows runtime claim. New test
  cases include bounded non-NUL strndup input and size-zero/one copy boundaries.
  No production edits, normal-build option changes or commits.
- Threads pass: native Clang/GCC st_threads CTest passes five consecutive runs
  each. A separate /tmp/st-threads-audit.G680vO build forces both optional pthread
  rwlocks and mutex_timedlock absent; its full test passes five runs. Normal
  builds remain unchanged. MinGW compiles the Win32 (not pthread) backend.
  No code or test changes in this component; report only, no commits.
- Socket/TCP pass: Clang/GCC socket and TCP loopback tests pass, including
  bound-but-not-listening refusal. MinGW builds all three targets; no Windows
  runtime claim was made in that pass.
- Serial pass: normal Clang serial CTest passes but its flags confirm PTY=0.
  Existing tests compiled directly with -DST_SERIAL_TEST_HAVE_PTY=1 and linked
  against build/build-gcc serial and io_error libraries both pass. MinGW serial
  test target builds; Windows COM I/O unexecuted. No code/test changes made in
  this component; report only, no commits.
- Connection initial pass: connection_serial passes in both native builds.
  MinGW expanded st_connection_test compiles/links; not executed. Remaining
  component review explicitly pending above.
- Connection backend pass: POSIX shell line-discipline coverage now verifies
  newline processing, echo, canonical Return, Ctrl+C and disabled software
  flow control under Clang and GCC. MinGW compiles/links, without executing
  ConPTY or Windows tests.
- Connection/profile boundary pass: Existing profile CTests pass both. MinGW
  builds connection_serial and profile tests, not run.
  Connection and profile source coverage is complete with documented
  stress/error-injection/platform gaps.
- Audio engine initial pass: existing native st_audio CTest passes using fake
  engine backends (availability checks load libraries, no audible playback).
  No new tests or production changes this pass; report only. Remaining audio
  sources/platform runtime verification are explicitly pending above.
- Audio backend completion pass: all remaining source/build/test files read;
  native GCC st_audio fake-provider CTest passes, MinGW st_audio_test builds
  with WASAPI. Native Clang test passed in the preceding pass. No physical
  device, audible smoke, SDL, macOS or Windows runtime verification claimed.
  PortAudio primary guidance checked for A023; legacy harmonic artifacts
  confirmed as intentional. Report only this pass, no commits.
