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

The repository-wide audit is complete. This is not an implementation approval,
release certification or claim that findings have been fixed. The consolidated
API/design assessment, priority groups and all 94 outstanding finding summaries are in
AUDIT_INDEX.md; full evidence, recommendations and limitations remain here.

| Objective requirement | Authoritative evidence and disposition |
| --- | --- |
| Entire repository, not only recent changes | git ls-files identifies 47 top-level source directories and 12 root files; every directory has an inventory row, no omissions. Per-component reviews cover local implementations, headers, tests/build and associated data/docs. |
| API design, architecture and library layout | AUDIT_INDEX boundary/coupling/ownership assessment plus component public-contract and CMake dependency reviews; A001–A007, A047/A066, A088/A091 and host-contract findings. Cohesive decomposition is recognized; no redesign authorized. |
| Correctness, ownership/lifetime and concurrency | Component lifecycle/error reviews, normal and injected tests, explicit GUI/host ownership closure; source and reproduced failures are qualified individually. A016, A023, A027/A035/A036 and A108/A109 illustrate distinct lifetime/error risks. |
| Portability and all platform backends | POSIX/Windows sources and every audio/console backend reviewed; native, MinGW compile-only and unavailable runtime evidence kept separate. Capability probes/standalone targets reviewed. No unsupported platform pass inferred. |
| Performance | Scaler/renderer benchmark source and measured cases reviewed, including common mixed-axis scaling; A011/A023 and backend pacing risks distinguished from measured regressions. Reconstructed legacy benchmark limitations explicitly recorded. |
| Required legacy behavior | Read-only comparisons cover fonts/catalog/maps, modes, palette capacity, Prestel versus Mode 7, command line, console/input and settings behavior; A015/A018/A032 plus explicit parity limits. Frozen CTerm chapter cmp succeeds. |
| Wren scripts and bindings | Complete local source/signature/manual reviews; isolated shipped-script suites and host fixtures; host closure A100–A114. Connected/SFTP production integration missing from this application is reported, not invented or silently counted as working. |
| Build logic, generators and documentation | Root/component CMake and generators reviewed; standalone/probe failures recorded; pinned Unicode/font regeneration and table comparisons documented; root manuals/license/development guidance covered. Wren VM vendored internals limited to integration boundaries as requested. |
| Inventory and consolidated findings | 94 outstanding detailed headings are matched in the index; every finding has severity/qualification and recommended direction. 13 high, 52 medium, 27 low, 2 unresolved concerns; not all runtime reproductions. |
| Relevant tests where practical | Fresh complete native build succeeds. Final selected CTest run: 67 executed, 58 pass, 9 fail, 5.71 seconds; failure matrix in index. Isolated regressions, sanitizers/provider/fallback and cross-compile evidence retained in detailed entries. |
| Only tests/reports changed; no implementation remediation | Current status: 30 modified tracked tests, 23 untracked tests, 2 untracked reports; no staged files. Filtered worktree inspection finds no other changes. Baseline-to-current non-test difference is pre-existing str_list/CMakeLists.txt, unchanged since fdeab0d. No final-consolidation commits/push/reverts. |
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

Latest native verification: the complete configured build succeeds with
`cmake --build build -j4`. CTest excluding the real-display X11/Wayland tests
executes 67 tests in 5.71 seconds: 58 pass and 9 fail, all at recorded
regressions. No selected test times out or skips. AUDIT_INDEX.md contains the
failure-to-finding matrix and command. SDL runs with its configured dummy
driver; this is not real-window verification. Remaining standalone audit cases
are not registered in CMake and therefore are not counted among these 67 tests.

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
| Root manuals, license and development guidance | Reviewed | Manual.txt, Wren.adoc, LICENSE, REVIEW.md and development guidance read; cterm.adoc intentionally frozen and identical to legacy by cmp; Console.adoc covered with console; contract mismatches A088–A090 and licensing/distribution notes below |
| app/ | Reviewed | All local implementation/header/build/test read; A053, A088, A091; parser and 28 fixed-mode legacy geometry checked; startup/host ownership boundary dispositioned below; native startup tests pass, GUI/platform and future connected-loop limits explicit |
| app_paths/ | Reviewed | A009; native normal tests plus failing XDG reproduction; other platforms source-only except MinGW compile |
| array_list/ | Reviewed | A007 unresolved portability concern; native and optimized tests pass |
| audio/ | Reviewed | All local backends/synthesis/build/tests read; A023 callback risk; native fake tests pass, MinGW WASAPI compile only; actual device/platform gaps below |
| audio_mixer/ | Reviewed | All local sources/tests/build read; A022 crossfade gap reproduced; device integration assigned to audio; ramp/performance/lifetime gaps below |
| bbs_list/ | Reviewed | All local sources/header/tests/build read; A033 sanitizer reproduction, A034/A035 source findings; native list/web tests pass, MinGW compile only; integration/fault gaps below |
| codepage/ | Reviewed | All local source/header/build/test read; exhaustive graphic roundtrip and expanded control/invalid-input tests pass native Clang/GCC; MinGW compile only |
| connection/ | Reviewed | A014/A015/A018 reproduced; A016 Windows source race; A017 RFC mismatch; shutdown stress and platform runtime gaps documented |
| console/ | Reviewed | All local implementation/backends, tests, benchmark, build and docs read; local integration dispositions A047–A051, A108/A109 and GUI closure notes; native/isolated tests and platform-only risks distinguished; remaining Wren consumer questions belong to wren_host |
| dynamic_library/ | Reviewed | A005; native fixtures pass; Windows compiled, not executed |
| event/ | Reviewed | A004; native repeated tests, MinGW compile; failure-injection limitations below |
| file_lock/ | Reviewed | A006; native tests plus failing controlled error-path reproduction; Windows source/compile only |
| file_map/ | Reviewed | Native stream-lifetime test passes; Windows source/compile only |
| filesystem/ | Reviewed | A010; native expanded tests pass; Windows compile-only regression |
| fonts/ | Reviewed | API/generator/build/tests/docs reviewed; 98 legacy bitmaps and all 46 catalog metadata entries match; mapping bounds/legacy comparison and semantic limits below; native tests pass |
| hex/ | Reviewed | A001; native tests and MinGW build; caller integration tracked separately |
| ini/ | Reviewed | All local sources/tests/build read; A019/A020 reproduced; consumer integration tracked with owning components; failure injection/performance gaps below |
| io_error/ | Reviewed | Sources/header/build and caller adapters reviewed; native test passes; 28 Win32 mapping cases added and MinGW compile/link verified, Windows execution unavailable |
| list_crypto/ | Reviewed | All local code/tests/build read; expanded counted-password tests pass natively; MinGW compile only; provider/resource/erasure limits below |
| name_value_list/ | Reviewed | Ownership/sorted contracts checked; allocation injection remains a limitation |
| path/ | Reviewed | A008 conditional Windows defect; native boundary tests pass, MinGW compile |
| pixels/ | Reviewed | All local API/implementation/filter/build/docs/tests/benchmark read; expanded upscale edge tests pass native/ASan/UBSan, MinGW compile; measurement limits below |
| profile/ | Reviewed | Ownership/copy source and native tests; A018 consumer enum mismatch; allocation-failure injection remains a test gap |
| scripts/ | Reviewed | All current script implementations/tests read, including main menu, editors, REPL, common bindings module and self-test driver; isolated suite results and runtime/cross-script gaps documented below; older remaining-source lists superseded |
| semaphore/ | Reviewed | A004; native repeated tests, MinGW compile; failure-injection limitations below |
| serial/ | Reviewed | A013 disables normal PTY coverage; manually enabled native PTY tests pass; Windows compile only |
| settings/ | Reviewed | All local sources/header/tests/build read; A029/A030/A032 reproduced, A031 source-confirmed write risk; global caller discipline and failure injection gaps below |
| socket/ | Reviewed | A012 integration defect; loopback native tests pass; Windows compile only |
| str_list/ | Reviewed | Native failure injection and alias-input cases pass; MinGW compile |
| string_compat/ | Reviewed | Native and opt-in fallback tests pass; MinGW compile |
| string_util/ | Reviewed | Exhaustive byte classification/trim tests pass natively; MinGW compile |
| tcp_client/ | Reviewed | A012 reproduced with consuming-error fake; real loopback refusal passes on FreeBSD |
| theme/ | Reviewed | All local sources/header/tests/build read; A024/A025/A026 reproduced; A027/A028 source-confirmed error paths; host/network integration and failure-injection gaps below |
| threads/ | Reviewed | A004/A011; native and forced pthread fallback tests pass; Win32 backend compiled |
| time/ | Reviewed | A002; controlled-clock and existing native tests; Windows source-only review |
| unicode_grapheme/ | Reviewed | Public contracts/generator/runtime/build/docs/tests reviewed; pinned regeneration identical, generated trie bounds/order checked; expanded native tests pass, MinGW compile only |
| unicode_normalization/ | Reviewed | Public contracts/generator/runtime/build/docs/tests reviewed; pinned regeneration identical, table bounds/order checked; exhaustive Hangul roundtrip passes natively, MinGW compile only |
| unicode_width/ | Reviewed | Header/generator/runtime/build/docs/tests reviewed; pinned regeneration identical, exhaustive scalar consistency passes; font/grapheme integration tracked separately |
| unifont/ | Reviewed | API/build/test/docs/license/shared generator reviewed; identical regeneration and exhaustive UNSCII overlay test; enabled/disabled native builds verified; packaging/Wren display tracked with owners |
| utf8/ | Reviewed | A001; expanded native tests and MinGW build; caller integration tracked separately |
| value_convert/ | Reviewed | A021 date precedence conflict; all local sources/tests/build read; native exact rounding checks pass; exotic formats and numeric performance unverified |
| webget/ | Reviewed | All local source/header/test/build read; A036 source double-close, A037 URL/cache mismatch reproduced; protocol/TLS/fault gaps below |
| windows_utf/ | Reviewed | Source and MinGW compile; Windows runtime not verified |
| wren_host/ | Reviewed | All local implementation, public/internal headers, CMake/generator and integration tests read; isolated shipped-script and host-contract cases run; mutation/error/input/font/ownership integration dispositioned below, including A112–A114; runtime/fault limits explicit |
| wren_vm/ | Reviewed at integration boundary | Full legacy diff, build/configuration, license, REPL private API coupling and minimal consumer link checked; upstream internals not exhaustively audited |
| xptls/ | Reviewed | All five local source/header/build files read; A038 PSK authentication bypass reproduced against OpenSSL, rejected by Botan; broader TLS/platform coverage gaps below |
| xptls_crypto/ | Reviewed | All local headers/providers/clear implementations/build/tests read; expanded tests pass OpenSSL, Botan and none; MinGW compile only; contract/resource gaps below |

## Root documentation review and consolidated follow-ups

### A112 — Wren input drops physical-key edges

Severity: medium API/input defect. Wren.adoc:2790 specifies PhysicalKeyEvent
press and release objects from Input.next/poll. In
`wren_host/wren_bind_core.c:648`, releases are discarded and presses are
converted only when modified_physical_key supplies a legacy key code; other
presses are discarded. No PhysicalKeyEvent is constructed on this input path.
The object's constructor/getters existing does not establish delivery.

Native `st_wren_loader_audit_test physical-input` posts an evdev-30 press,
polls through the real host, then posts/releases and polls separately. Both
cases fail because the returned value is not a PhysicalKeyEvent. Events are
posted to a private headless console, not synthesized on the user's desktop.
Recommendation: retain physical edges as their declared event type while
preserving separately committed text/normalized key semantics; test ordinary,
modified, repeat and release paths for both delivery and duplicate suppression.
A104 separately covers the timeout extension caused by filtered events.

### A113 — Menu default ports disagree with its URL parser

Severity: low user-visible consistency defect, source-confirmed.
`wren_host/wren_bind_menu.c:357` connection_default_port returns 8883 for
MQTT and zero for shell; `fn_menu_default_port:1100` defaults both to 23.
Wren.adoc:4406 calls this the built-in default for the connection type.
These two policies in the same host disagree without a documented reason.
No network operation or connection test performed. Recommendation: establish
one existing connection-type policy for URL parsing and editor defaults and
verify every enumerated type, not merely SSH/RLogin/TelnetTLS.

### A114 — Failed active-theme deletion can leave saved and live settings split

Severity: medium persistence/failure-atomicity defect, source-confirmed.
`wren_host/wren_bind_menu.c:1523` switches away from an active theme, clears
theme_file and saves settings before attempting st_theme_delete. If that
deletion fails, rollback restores previous settings/theme in memory but does
not restore the settings file already saved. The caller sees an error and the
old active theme, but restart can use the newly saved selection. No deletion,
personal settings write or injected filesystem failure was executed.
Recommendation: define and enforce the transaction boundary across selection,
settings persistence and file deletion, testing deletion failure after save.
Memory/theme rollback calls also discard their own failure results; general
OOM rollback reliability remains unverified, not an asserted successful path.

### Wren-host integration coverage closure

All previously identified host follow-ups now have findings or explicit
dispositions. The Wren skill and local manual guided these comparisons; the
modern isolated host harness, not the legacy executable, supplied runtime
evidence. All new fixtures use fresh temporary paths and a headless console.

- Input deadlines are A104; physical delivery is A112. Window mutation
  bindings discard native failures, including
  invalid window bounds and write/scroll failures. This is a source-only error-
  reporting concern: setter return conventions and recoverable write failures
  need an explicit contract; not every void-style setter is thereby broken.
- MouseEvent fields expose modifiers, but the console mouse event has no
  modifier member and make_mouse_event leaves that field zero. Wheel conversion
  collapses magnitude and interprets nonpositive vertical movement as down,
  including horizontal-only input. Full-resolution mouse/drag semantics remain
  incomplete host integration risks; no real pointer gesture was tested.
- Font slots are context storage with no rendering consumer found; the manual
  still promises active slot loading. This is an incomplete integration
  concern, distinct from working explicit Cell.font/catalog lookup. Legacy
  Cell field setters did not automatically resolve selector bits either, so
  that absence alone is not classified as a regression. Any future selector
  policy must retain the full-cell explicit-glyph bypass and registry lifetime.
- Theme selection/deletion rollback is rechecked; A114 records the persisted
  versus live split. General OOM/failure rollback remains untested. BBS and
  Settings persistence/stale-handle/safe-mode boundaries are already covered
  in A061–A065 and A100–A105, not assumed safe from their normal-path tests.
- Host/VM lifetimes, borrowed config and synchronous dispatcher use follow the
  application ownership review. Multiple logical consoles do not make Wren
  thread-safe. Actual connected-session event scheduling/SFTP capabilities
  remain absent or unavailable as A088 documents; this audit did not invent
  that consumer or count menu execution as proof of it.

Remaining work is final repository-wide inventory/evidence reconciliation,
not unseen host source or a requirement to implement these findings. Review
completion does not imply that every documented API currently works.

### A108 — Wayland buffer failure loses the mapped length

Severity: medium resource/error-path defect, source-confirmed; not fault-
injected against a compositor. `console/console_wayland.c:297` maps `size`
bytes before creating the pool and protocol buffer. The stored buffer->size
is assigned only after both creations succeed (:318). Either failure calls
buffer_destroy, which passes the still-zero size to munmap and then discards
the pointer. get_buffer_from destroys/reset the previous buffer before this
creation path, so an old successful size does not repair the failed cleanup.
Recommendation: retain mapped-region ownership/length as soon as acquired;
inject each subsequent creation failure and verify release before discarding
state. This is separate from buffer exhaustion or compositor shutdown.

Also source-visible: width/stride and size_t multiplication are bounded, but
total size is narrowed to int32_t for wl_shm_create_pool without a matching
INT32_MAX total-size check. Large allocation/protocol behavior was not tested;
the pool's signed protocol-size boundary needs validation before allocation.

### A109 — Wayland worker waits do not consistently handle disconnection

Severity: medium shutdown/liveness defect, source-confirmed paths; no live
compositor failure injection. In `console/console_wayland.c:1651` worker,
dispatch_pending, periodic draw and flush results are discarded. The poll
result handles POLLIN but not POLLHUP/POLLERR/POLLNVAL. A terminal-ready fd
without POLLIN can repeatedly wake the worker without setting failed or
notifying the consumer. Unlike this path, a dispatch failure under POLLIN
does break and publishes failure to waiting commands: do not claim all
disconnects hang.

`get_buffer_from:345` also waits in wl_display_dispatch when both buffers are
busy, without servicing the command pipe. A connected but nonresponsive
compositor withholding buffer releases can therefore prevent a queued stop
command from completing. Startup's initial-configure wait has no deadline.
Recommendation: make compositor errors and shutdown requests observable in
every blocking wait, preserve buffered protocol work, and test connection
loss, withheld configure/release events and destruction with a pending command.
No claim that the existing basic Wayland integration test exercises these
cases; it was not run on the user's display.

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
- X11 retains/coalesces exposure output; A048/A049 are distinct signature/
  rendering defects. Default-visual mask conversion has no indexed-visual
  allocation path; legacy x_events.c contains XAllocColor use. Indexed-visual
  parity remains unverified rather than declared supported or intentionally
  removed. Clipboard INCR/fallback, alternate modifier maps and unusual visual
  classes remain source-level compatibility risks documented in its detailed
  review; no physical clipboard/input was exercised.
- Wayland protocol-version/seat-removal/repeat/compose limitations remain
  source-level compatibility risks; A050/A051 are isolated reproductions and
  A108/A109 are source findings. Compositor positioning restrictions are not
  substitutes for implementing supported behavior or lifecycle handling.
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

### A105 — Custom-font bindings do not preserve their declared editing boundaries

Severity: medium (source-confirmed API/transaction contract mismatch; no new
font-file mutation or allocation-failure reproduction). Wren.adoc:4632–4674
describes a separate custom-font model, unrestricted native-editor names,
transactional reread, and no extra setFile size restriction. Current
wren_bind_menu.c:2463 accepts only 2048/3584/4096/10240-byte files, while name
validation imposes additional character rules. saveFonts calls the complete
st_settings_save rather than a font-sections-only write; reloadFonts calls
complete st_settings_reload before attempting font registration. It can thus
reload non-font edits and, on a later registration failure, leave newly loaded
settings with unchanged font-generation bookkeeping. Successful persistence
followed by registration failure also cannot be interpreted as no mutation.

Recommendation: reconcile the native/editor contract and explicitly define
the separate persistence and live-slot update boundaries, then test unrelated
settings preservation and failures between those stages. This is not a
recommendation to weaken font bitmap validation in the renderer. A029 remains
the separately reproduced empty-name persistence defect. These details close
the earlier custom-font source questions with explicit source-only evidence;
fault injection, filesystem replacement races and real font rendering remain
verification limits, not successful tests.

Safe-mode follow-up to A062: Settings.save (:2992), Menu.saveWebLists (:2754)
and Menu.setEncryption (:2604) also have no host safe_mode check. They call
settings persistence or BBS encryption rewrite directly; settings_store.c's
save checks its initialized baseline/path but has no host policy parameter.
This is source-confirmed missing enforcement beyond the already reproduced
BBS.save case. No additional rewrite/encryption/save was run in this pass.
Cache refresh and external URL launch remain separate policy questions;
their omission of a gate must not be silently treated as an approved exception
or as evidence of an executed external action.

Immediate-settings follow-up: settings_apply applies a theme before copying
global settings, ignores both console scaling-setter results, then clears the
snapshot dirty flag. A064 already demonstrates apply-before-save-failure;
allocation or backend failure in these additional stages is not injected.
Audio/list-path/KDF changes cannot be declared operational merely because a
settings field changed; the currently absent connected runtime is covered by
A088. These are explicit failure-boundary and consumer-verification limits.

### A104 — Timed Wren input restarts its timeout after ignored events

Severity: medium (source-confirmed deadline contract defect; timing not
reproduced). Wren.adoc:2671 specifies waiting up to ms milliseconds.
next_input_event in wren_bind_core.c:600 instead constructs a fresh UTC
deadline inside its event-filtering loop. Physical releases, unrecognized
physical presses and default/ignored event types continue that loop. An
ignored event arriving near the deadline therefore starts another full wait;
a continuing stream can extend the call indefinitely. Each underlying console
wait can honor its deadline while the public Wren call violates its bound.

Recommendation: compute one deadline for the operation and reuse it across
filtered events, testing ignored events near expiry and continuous ignored
input. No real keyboard or timing-sensitive test was run in this pass, and
no measured latency is claimed. The evidence is the placement of deadline
construction plus explicit continue branches, not an assumption of GC or
mutex contention. This is distinct from documented indefinite Input.next().

Font-selector contract reconciliation: legacy wren_bind_screen.c's Cell.font
and legacyAttr/bright setters modify separate stored fields, so the absence
of automatic font reassignment on a Cell bright/blink edit is not by itself a
new regression. The new explicit font ID/full-cell contract must not be
replaced by unconditional selector interpretation. Screen.font is a different
API: legacy calls getfont/setfont, while current core bindings only read/write
context.fonts.slots, and the manual still says it loads an active font slot.
No current rendering consumer of those slot writes was found; retain this as
a source-confirmed incomplete integration concern requiring a declared host
slot policy, not proof that all explicit Cell.font operations fail. Connected
terminal selector handling is not implemented by this menu host. Wren guidance
and the local input/font API documentation informed this comparison.

Font-selector tracing in this pass: Cell.font resolves the selected catalog
identifier and re-runs Unicode lookup; legacyAttr/bright/blink mutations only
change color/flag fields, and core bindings do not call
st_console_font_for_flags. This confirms the implementation paths but does not
alone settle when menu cells should interpret legacy selector bits. Keep that
as a caller/legacy-contract question rather than recommending that every
bright/blink change automatically chooses a different font. Full-cell writes
intentionally bypass lookup, a distinct contract that must be retained.

### A102 — BBS admission and transient-operation guards do not match the menu contract

Severity: medium (confirmed host API contract defect, persistence consequences
partly source-only). Wren.adoc requires quick-connect transient rename/save/
delete to return false and Menu.create to reject invalid/duplicate names.
wren_bind_menu.c's quick-connect and create paths both construct PERSONAL,
is_new foreign objects. rename checks string capacity only, with no transient
or read-only guard. create copies defaults and truncates the supplied name
without checking validity or uniqueness. The shared save path treats is_new
as permission to add a durable personal entry; there is no transient marker.

Two additional native `bbs-setters` cases reproduce accepted rename of a
quick-connect object and non-null creation of the existing fixture name Audit.
Both assertions fail after a clean strict-warning compile. The fixture URL
is parsed only, never connected; neither case calls save. The first compile
hit Clang's adjacent-string-array warning in the test, corrected with explicit
parentheses before the successful rebuild and five-case run. Output from the
older executable after that failed compile is not evidence for the new cases.

Lower-layer inspection limits the claim: st_bbs_list_add validates the name
and rejects a duplicate disk section; it resets added/connected/calls when
inserting. update/remove require baseline_personal and only write the personal
document. Thus an editable system-copy or duplicate draft is not evidence
that system/web source files can be overwritten. It still contradicts the
host's in-memory read-only/admission promises, and transient saves can reach
the ordinary insertion path if other checks succeed. Actual transient disk
insertion was not executed. Existing A062 covers the separate safe-mode save
violation. Recommend enforcing object-category/name rules at the documented
API boundary and testing them without weakening lower storage validation.

### A101 — BBS handles remain usable after a successful model reload

Severity: medium (confirmed stale-state contract mismatch, not a demonstrated
memory-lifetime error). Wren.adoc:4282 promises that a successful full load,
create or delete invalidates older BBS handles and that using them aborts.
The foreign_bbs structure in wren_bind_menu.c:43 contains an owned profile
copy and original name but no model-generation token. bbs_value checks the
foreign type only. Reload therefore cannot distinguish an old object from one
acquired from the current model.

Extended the isolated `bbs-setters` test with a third case: obtain a BBS from
Menu.entries, successfully reload the synthetic list, then read the old
entry's addr in a caught fiber. No abort occurs, so the stale-handle assertion
fails. Strict-warning native compilation succeeds; the two earlier setter
cases still fail independently. No save/delete or personal-list operation is
performed. Source inspection shows save uses original_name against the current
host list, so stale copies can be submitted to a different model generation;
actual overwrite behavior is not reproduced here and is not asserted as a
new disk-data-loss finding.

Recommendation: reconcile the implemented handle semantics with the declared
generation contract and test load/create/delete invalidation plus sort/rename
preservation. Deep copies avoid a dangling borrowed profile but do not by
themselves supply stale-object detection. This finding does not authorize
introducing a new generation design; it reports the documented behavior that
the implementation currently does not provide. Wren guidance was used for
the caught-fiber assertion.

### A100 — Rejected BBS palette/script assignments partially change the object

Severity: medium (confirmed in-memory mutation on validation failure).
wren_host/wren_bind_menu.c:3254 writes each accepted palette element directly
into the profile before validating later elements; palette_size and dirty are
only updated after the loop. The wrenScripts setter at :3320 likewise copies
each name into the existing byte buffer before checking the next, and only
terminates the new string/sets dirty on complete success. Rejected input can
therefore leave changed colors or a mixture of new-prefix and old-suffix
script names, without the normal success bookkeeping.

The new `bbs-setters` mode in the isolated host audit test loads a synthetic
personal list from a fresh /tmp root, sets palette [1,2], catches the expected
abort assigning [99,"bad"], then finds the first color is no longer 1. A
second case sets scripts ["original"], catches the abort from ["x",false],
and finds the old name changed. Both retention assertions fail natively after
strict-warning compilation. The test calls no save and accesses no personal
data; it demonstrates object mutation, not disk corruption. Dirty-state
behavior follows from source ordering but is not asserted by these cases,
which first make valid edits.

Recommend validating/staging the complete proposed collection before updating
the profile, with invalid later-element and capacity cases retained as tests.
This does not prescribe a new transaction API or authorize an implementation
change. Wren guidance was used to contain expected setter aborts with Fiber.try
and inspect the same live object afterward.

### A099 — Menu screen-mode changes lose mode identity and use incomplete geometry

Severity: medium (confirmed integration/legacy behavior mismatch).
wren_host/wren_bind_menu.c:2177 duplicates a dimensions-only mode table;
Prestel and Atari XEP80 are listed as 24 rows instead of the legacy/startup
25. setScreenMode at :2248 only calls st_console_resize. It does not apply
cell/raster geometry, aspect, blink mode or the configured default cursor
promised by Wren.adoc:4450. currentScreenMode at :2223 guesses the first enum
with matching grid dimensions, collapsing LCD/EGA/VGA and other 80x25 modes.

Added `screen-mode` to the isolated host audit harness. After a successful
Menu.setScreenMode(PRESTEL), the public console size is 40x24 rather than
40x25. A separate script successfully selects LCD_80X25 then fails the check
that currentScreenMode retains that identity. Native strict-warning compile
and fresh temporary-root execution reproduce both cases without a GUI.
Other geometry/cursor omissions are source-confirmed, not visual assertions
from this headless fixture. The all-mode startup test remains passing: it
exercises a different path and does not contradict this finding.

Recommendation: make the menu mode-change contract use authoritative mode
configuration/identity rather than a separate grid-size approximation, and
test round-trip identity plus full geometry and cursor effects. That is a
remedy recommendation only; no shared API or implementation was redesigned.
Wren guidance and the mode API manual contract were used for the test.

### A096 — BBS editor script discovery is an unconditional empty stub

Severity: medium (confirmed missing shipped-menu functionality). At
wren_host/wren_bind_menu.c:3484, Menu.scriptModules binds fn_menu_empty_list,
which only returns a new empty List. Wren.adoc:4123 promises available root
modules with reserved menu/picker names excluded. ScriptListPane in
scripts/menu_bbs_editor.wren:99 uses that result for additions and labels
configured names missing when absent from it. This is not the deferred
execution of connected scripts: their editor catalog is already consumed by
the shipped menu. The isolated auxiliary-scripts run again reports the
module-discovery assertion failing (the other four editor helper cases pass).
Recommend implementing the declared catalog/filtering contract and retaining
tests for discovery, reserved names, duplicates and byte-length limits.

### A097 — Declared pixel binding family is unavailable in the host

Severity: medium (confirmed binding/API availability gap). The current core
binding table has no PixelColor, PixelBuffer, PixelMask, PixelBlit, or pixel
Screen methods; the generic binder in wren_host.c returns st_wren_unsupported
for unresolved declarations. The shipped scripts/pixel_vm_test.wren aborts at
line 18 on PixelColor.rgb(0x123456), before buffer allocation or any of its six
checks. This was reproduced again in the headless auxiliary runner.
Colour-word construction is not a physical-display capability test. The
console and `st_pixel_buffer` now retain the same indexed/direct colour words,
so the storage prerequisite is complete; the declared Wren binding family and
its per-host admission rules remain missing. Recommend implementing and
testing that surface without classifying the abort as a headless/hardware
limitation. No later pixel test coverage is implied by loading the module.

### A098 — Shipped UI demo imports an absent module

Severity: low (confirmed demo integration gap, not a menu-startup failure).
scripts/ui_demo.wren:30 imports transfer_app, which is absent from the script
tree and embedded module list. Interpreting the actual shipped demo source
through the production loader again fails on that import in a fresh isolated
script root. The demo itself is not embedded, so a failed bare import alone
would not establish this finding. Recommend supplying/documenting a supported
invocation and its dependencies; connected-only modal/transfer calls remain
additional context requirements and are not tested past the missing import.

These three entries consolidate previously recorded unnumbered gaps, not
three newly discovered regressions. Reused the existing isolated native
auxiliary-scripts audit executable in a fresh /tmp directory; it performs no
personal-list access or real transfers. Other results repeat A085/A086 and a
known theme-test column-oracle mismatch, not new findings. Wren guidance and
the relevant module-catalog manual contract informed this review. The
unavailable WON.deserialize helper remains another explicitly recorded API
scope question; declaration alone is not evidence every connected-only
capability should be exposed in the trusted menu host.

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
through destroy. A055 remains the separate startup/recovery policy defect;
allocation failure at every individual VM operation has not been injected.
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
such failures fatal is not prescribed by the audit. Settings persistence at
application exit remains an implementation/documentation gap under A088.
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
current macOS startup defect. Bare -i remains the independently reproduced
A053 memory-safety failure. This comparison does not establish connected
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
host/consoles; there is no connected-session loop. The manual explicitly
labels JPEG XL reconnection incomplete but gives no comparable boundary for
these larger retained sections. This is not authorization to port those
features, nor a claim that the consciously deferred CTerm port is itself a
new regression. Recommend a clear current-implementation scope notice while
preserving the legacy CTerm specification unchanged.

Additional documentation gaps: Manual.txt references Themes.adoc, which is
absent from this repository. Its opening still claims this project lives in
the Synchronet git repository. The persistent-state section describes saving
window scaling at exit; the current app teardown does not implement that
workflow. The connected GHost section gives a ghost:// URL while the earlier
supported-scheme list omits it. These are source-checked documentation gaps;
no GUI, network connection, personal settings or distribution was exercised.
Full Wren.adoc coverage and cross-reference checks remain outstanding.

### A089 — Wren language reference gives incorrect standard-library contracts

Severity: low (confirmed documentation errors, not VM regressions).
Read Wren.adoc lines 1–1720 continuously in this pass and compared the
language/stdlib claims with the vendored VM and an isolated native menu VM.
The added `core-helpers` cases deliberately assert the manual's claims:

- Map iteration is documented as yielding keys in three places; the shipped
  Map.iteratorValue constructs MapEntry objects. Use map.keys for keys.
- List.remove is documented as returning true when removed; it returns the
  removed value instead (the check using integer 9 fails).
- The reverse-range example says `5..0` produces five values ending at 1;
  the inclusive range has six values and ends at 0.
- Object.supertype is documented as Object; the VM returns null.

All four documentation-claim checks fail in fresh headless host execution.
They are intentionally not proposed VM requirements: remedy the manual, not
the implementation. Native strict-C17 compilation of the audit harness
succeeds. The pre-existing serializer control still passes and the parser
availability and A087 checks still fail separately. No production/manual edit.
Source additionally shows System.clock delegates to C clock()/CLOCKS_PER_SEC,
not the wall-clock-since-start contract stated here; it should not be used as
the documented elapsed-time clock on the strength of that statement.

The script-loading discussion at Wren.adoc:400 lists embedded sources before
user files, contradicting both its later Load Order section and the actual
load_module implementation (root user file, event user file, embedded fallback).
The Headless Tests section describes run_wren.sh, absent from this repo.
The embedded-module inventory lists numerous absent connected scripts; that
is part of A088's current-versus-future documentation boundary, not evidence
they are implemented or an authorization to import them. The claimed unknown
foreign-declaration load error also conflicts with bind_method's catch-all
unsupported method: unknown declarations can load and fail only when called.
Picker recovery and additional-auto-load promises have concrete failures
already recorded in A055 and A054 respectively.

Subsequently read Wren.adoc lines 1721–4110 continuously: hook handles, modal
input, REPL, screen/cursor/video APIs, pixel APIs, input/event constants,
codepages, cells/surfaces/fonts, log access, connected APIs, and the beginning
of the menu model contract. Remaining continuous reading starts at line 4111.
Previously consulted later API sections are useful evidence, not full manual
coverage.

### A090 — Wren examples and cell/input descriptions retain incompatible legacy assumptions

Severity: low (source-confirmed documentation defects; recommended remedy is
documentation correction, not restoring obsolete transport restrictions).

- Wren.adoc's second modal-input example posts to Fiber.current inside the
  claim callback. That is the dispatch callback fiber, not the parked worker
  created outside it. Capture the worker before constructing the callback,
  as the preceding example already does. The documented Hook.dispatch_
  implementation executes the callback in a new child fiber, establishing
  the identity mismatch without requiring a live connection reproduction.
- The Console.entries example tests e[2] against LogSource.runtimeError,
  although its own documented tuple and wren_repl.c:137–139 place source at
  index 1 and text at index 2. It silently selects no ordinary error entries.
- Cell.ch is described as round-tripped through CP437 and cells as legacy
  vmem_cell storage with control bits hidden in fg/bg. The current binding
  stores a Unicode codepoint directly in st_console_cell; flags are separate.
  chByte retains an explicit CP437 adapter. The old representation description
  must not be used to justify moving flags back into colors.
- KeyEvent.new is documented as rejecting non-byte-stuffable 16-bit values
  and normalizing 0x01e0 to Escape. allocate_key_event currently accepts every
  integer in 0..UINT16_MAX and performs neither check nor normalization.
  Single-byte synthetic keys use CP437, not the documented selected codepage.
  Modern event transport does not require the old byte-stuffing restriction;
  whether normalization is still required is a separate compatibility question.

Other claims in this span map to already-recorded findings: the pixel API
promises availability in all three VMs despite missing current bindings;
static cursor methods conflict with A068; Color helpers conflict with A079;
transactional Menu.load conflicts
with A061. Do not count these repeated documentation promises as independent
runtime defects. Font-slot terminology also needs reconciliation with the
new registry/selector distinction. No connected runtime, GUI, network or
personal-data action was performed; this pass changes audit notes only.

### Wren manual completion pass

Read the remainder of Wren.adoc, lines 4111–7739: menu record/settings/theme/
encryption/web/font contracts; host and picker capabilities; filesystem
rights/errors/tokens; transfer and SFTP contracts; WON; complete UI library
reference and worked examples. Combined with the preceding continuous passes,
the whole manual now has source coverage. No live transfer, consent-token
operation, external command, device I/O or personal-data access was attempted.

Additional documentation reconciliation under A088/A090:

- ThemeDocument.glyphs is described as five fields and addGlyph as three
  arguments. Current rows have three metadata fields plus all three glyph
  fields (including Codepoint), and the binding is addGlyph(_,_). The final
  Glyphs chapter still requires CP437 and mentions Codepage.encodes_, absent
  from the current module. This contradicts the implemented Unicode fallback
  direction; do not remove Unicode support to satisfy this old description.
- Directory staleness says external disappearance aborts, while File's later
  contract says it returns FileErr.vanished. Filename policy also alternates
  between strict cache names and relaxed download names without consistently
  qualifying the generic Directory statements. These must be clarified before
  consumers can rely on the error/filename contract. The assertion that an
  existence check ensures identity is too strong: existence alone cannot
  distinguish replacement at the same pathname. No new runtime exploit or
  file-operation defect is claimed from those prose contradictions.
- File rights/hash/token paragraphs describe the legacy complete filesystem
  surface. Current wren_picker.c exposes File.path/toString/token/isOpen,
  with token unavailable; the broader methods are not established merely by
  their shared-module declarations. Keep this integration gap distinct from
  intentionally absent connected protocols.
- The UI introduction promises no foreign calls per cell. Painter.text and
  line routines call Surface.cellAt, field setters and Screen.textWidth while
  iterating. Reusing mutable Cell views avoids allocation but does not remove
  those foreign calls. This is a misleading performance claim, not a measured
  performance regression.
- TextInput prose says multibyte/emoji codepoints each take one cell, despite
  the current display-width-aware implementation. Some examples also omit
  needed imports (TimerElapsed in the multi-fire example) or include invalid
  Wren semicolon syntax (Status callable chaining). These are example defects,
  not reasons to change the language or make glyph widths uniformly one.

The documented short-read SFTP contract supports A083: a successful read is
explicitly up-to-count, not an exact chunk fill. The settings failure/stale
snapshot claims correspond to A063–A065, ClassicTheme fallback to A032, and
UI empty selection/tab order/Markdown semantics to A071/A074/A077/A078.
Repeated promises were not assigned duplicate defect numbers. Full-source
documentation coverage does not settle remaining app/console/host integration
follow-ups or constitute final consolidated audit completion.

## Findings

### A001 — Empty-input and decode-result contracts are underspecified

Severity: low. Classification: confirmed API documentation gap, not a proven
implementation failure.

- hex/st_hex.h: encode/decode comments do not say that both buffers must be
  non-NULL even for empty input. hex/st_hex.c rejects NULL unconditionally.
- utf8/st_utf8.h: decode documents unchanged outputs but not its success value,
  exact error codes, mandatory output pointers, one-scalar prefix consumption,
  or treatment of an empty/truncated input. The implementation returns zero on
  success, INVALID_ARGUMENT for invalid pointers, INVALID_ENCODING for an empty
  or incomplete sequence, and does not require consuming the entire buffer.

Recommendation: document the existing contracts explicitly after approval;
there is no need to redesign either API to resolve this finding. No header or
implementation changes made by this audit.

### A002 — Monotonic capability probe does not match target language mode

Severity: medium portability risk. Classification: confirmed configuration
context mismatch; no failing target platform reproduced in this audit.

`time/CMakeLists.txt:22` and the librt retry use check_c_source_compiles without
selecting C17/no extensions for the probe. The actual st_time target selects
both. `build/CMakeFiles/CMakeConfigureLog.yaml:3681` confirms that the successful
probe command has no `-std=c17`, whereas the target uses strict C17. A declaration
visible only in a compiler's default programming environment can therefore pass
the probe without establishing the target's required declaration/link context.
Linux's explicit _GNU_SOURCE reduces that particular risk there but does not
make the language-mode mismatch disappear. Recommendation: align probe context
with the consuming target; no CMake changes made by this audit.

### A003 — SDL uses an uninitialized deadline after clock failure

Severity: medium, rare error path. Classification: confirmed source defect;
the helper failure is tested, full SDL failure injection is not reproduced.

`console/console_sdl.c:1238` declares an uninitialized timespec in next_command;
line 1240 discards the return from st_time_utc_deadline_after_ms. If that helper
fails (e.g. timespec_get returns zero), it leaves the output untouched. With an
empty command queue and no shutdown, the subsequent condition timed wait receives
the uninitialized structure. This violates its input contract and may produce
undefined behavior or an unintended wait/busy loop.

`time/tests/st_time_fault_test.c` directly exercises the helper's clock-failure
path and verifies the output remains untouched. Recommendation: handle the
failure before passing a deadline to timedwait, using an explicit error/shutdown
policy. Merely initializing a zero deadline could turn failure into a busy loop.
The broader SDL lifecycle audit is still pending. No implementation changed.

### A004 — Event and semaphore deadline contracts omit the clock

Severity: low. Classification: confirmed public API documentation gap.

`event/st_event.h:32` and `semaphore/st_semaphore.h:33` accept a timespec
deadline without specifying absolute versus relative time or the clock.
Their implementations pass the value to st_condition_timedwait; the pthread
backend uses default condition attributes (realtime clock), and the Windows
backend computes a remaining duration from UTC. The public thread header also
does not state that clock contract. Callers therefore need implementation
knowledge to construct a correct deadline. Headers additionally omit explicit
BUSY/timeout outcomes and immediate availability behavior for expired deadlines.
Recommendation: document absolute UTC deadlines and the operation outcomes;
do not change clocks or timeout behavior as an incidental documentation fix.
No public headers changed in this audit.

### A005 — Dynamic-library search policy is only partially documented

Severity: low. Classification: confirmed API contract gap, not a request to
change the previously approved loading policy.

`dynamic_library/dynamic_library.h:23` describes preferred and older major
versions but omits unversioned precedence. The POSIX implementation tries
libNAME.so.MAJOR, libNAME.so, NAME.so, then descending older .so versions;
on Apple it subsequently tries the dylib sequence. Thus an unversioned library
can win over an available older version, and .so names win over dylib names.
Windows ignores preferred_major and visibility, trying NAME.dll per alias.
The header does not spell out these platform distinctions, symbol lifetime
(bounded by the loaded library), or that loader failures other than absence
also return NOT_FOUND (the unresolved-dependency fixture exercises that case).
Recommendation: document the existing policy and limitations rather than
silently changing lookup order or splitting the symbol API.

### A006 — POSIX file-lock cleanup can overwrite the acquisition error

Severity: low (misreported failure). Classification: confirmed defect with a
controlled failing test, not a reproduced real filesystem failure.

`file_lock/st_file_lock_posix.c:48` closes the descriptor after a non-EINTR
flock failure, then translates errno. close can overwrite errno, so the caller
receives the cleanup error instead of the acquisition error. The Windows path
saves its error before cleanup and does not have this problem.
`file_lock/tests/st_file_lock_error_test.c` includes the unchanged POSIX source
with controlled open/flock/close calls. With flock reporting EACCES and close
reporting EIO, the API returns -19 instead of -4. The test intentionally fails
under both native compilers. Recommendation: save the acquisition errno before
cleanup; do not modify implementation as part of this audit.

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

### A008 — Windows current-directory buffer growth is mistaken for success

Severity: medium under concurrent directory changes. Classification: confirmed
source/API mismatch, not runtime-reproduced on Windows; current application
reachability remains unproven.

`path/path_windows.c:98` tests GetCurrentDirectoryW only for zero after a prior
sizing query. A larger required buffer is also returned as a nonzero value;
that case is not successful string retrieval. The helper then returns the
allocated buffer for string processing without establishing it contains a
terminated path. See Microsoft's [GetCurrentDirectory contract](https://learn.microsoft.com/pl-pl/windows/win32/api/winbase/nf-winbase-getcurrentdirectory).
The drive-relative helper at line 117 and the second path_make_absolute call
in path_windows_extended similarly do not verify that the result fits.
Trigger requires path context to grow between calls; this audit has not found
an ordinary parent-process chdir caller establishing that trigger in SyncTERM.
Recommendation: check returned sizes and retry/fail safely, with an explicit
concurrency policy for relative resolution. Do not change implementation during
the audit.

### A009 — POSIX app paths accept relative XDG base directories

Severity: low. Classification: confirmed specification mismatch, reproduced.

`app_paths/st_app_paths_posix.c:20` accepts every nonempty environment value.
For XDG_CONFIG_HOME, XDG_CACHE_HOME and XDG_DATA_HOME, relative values should
instead be ignored according to the [XDG Base Directory specification](https://specifications.freedesktop.org/basedir/latest/).
With all three set to `relative`, the standalone
`app_paths/tests/st_app_paths_xdg_test.c` fails: settings, cache and SSH-key paths
are rooted in the process working directory rather than the HOME defaults.
The test performs no filesystem mutations. Recommendation: validate absolute
XDG bases separately from explicit user path overrides; preserve any deliberate
relative override support. No implementation changed.

### A010 — Windows recursive mkdir reports success for nonexistent roots

Severity: low. Classification: confirmed control-flow defect; Windows runtime
reproduction not executed in this audit.

`filesystem/filesystem_windows.c:479` only validates/creates the final path when
its length exceeds the parsed root. For a drive root or UNC share root, the
component loop is empty and the final check is skipped. Lexical path conversion
can succeed for an absent drive/share, so fs_create_directories returns zero
without checking that any directory exists. fs_create_parent_directories can
inherit this false success for a file immediately below such a root.
Recommendation: verify the root exists and is a directory when it is the entire
requested path. The standalone Windows test
`filesystem/tests/filesystem_missing_root_test.c` picks an absent drive and
expects failure; compiled/linked with MinGW, not run. No implementation fix.

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

### A012 — TCP racing ignores errors already consumed by socket wait

Severity: high correctness impact. Classification: confirmed integration defect
with a deterministic failing algorithm test; not reproduced by the native
FreeBSD loopback refusal case.

`socket/socket_posix.c` socket_poll_events reads SO_ERROR on POLLERR and stores
the translated error in socket_wait_item.error. Windows wait similarly reads
the connection error on its exception path. `tcp_client/tcp_client.c` ignores
that field and unconditionally calls socket_internal_connect_result again for
every reported event. SO_ERROR reads consume pending errors, so zero from the
second query does not establish connection success. See the
[POSIX socket-option contract](https://pubs.opengroup.org/onlinepubs/9799919799/functions/V2_chap02.html).
The connector can then publish a failed socket as success and abandon other
addresses instead of trying them.

The original fake preserved errors indefinitely. Updated only the fake to set
SOCKET_WAIT_ERROR/item.error and make the subsequent connection-error query
return zero, matching the consuming wait path. Existing
test_specific_error_and_unchanged_outputs now fails under both Clang and GCC.
A separate real bound-but-not-listening loopback test passes on this FreeBSD
host; this does not exercise every asynchronous error ordering. Recommendation:
honor the recorded wait error before querying SO_ERROR again and test that a
failed candidate does not suppress a later successful one. No production fix.

### A013 — Serial PTY capability test is pre-disabled

Severity: medium verification gap. Classification: confirmed build/test defect.

`serial/CMakeLists.txt` sets ST_SERIAL_TEST_HAVE_PTY to 0 immediately before
calling check_c_source_compiles with that variable. The check skips already
defined results. The resulting native test target has
`-DST_SERIAL_TEST_HAVE_PTY=0` in its flags.make and runs only argument tests,
not serial configuration, timed I/O, byte transfer or baud enumeration.
The normal CTest passes without exposing this omission.

Compiled the unchanged test manually with ST_SERIAL_TEST_HAVE_PTY=1 against
each native build's serial/error libraries. Both Clang and GCC PTY tests pass
on this host, establishing that the omitted coverage can run here.
Recommendation: leave the probe result undefined on POSIX until the check, and
explicitly report test skips. No CMake fix made under audit-only authority.

### A014 — Connection input worker silently drops short queue writes

Severity: high. Classification: confirmed data-loss defect, reproduced on
native Clang and GCC against unchanged production code.

`connection/connection.c:291` calls queue_write once per transport read and
checks only for a negative return. queue_write deliberately returns a positive
short count once it has made progress and encounters a full queue, even with
an infinite timeout (line 91). The worker discards the unqueued suffix and
reads the next transport chunk into the same buffer. This affects every
transport using the common worker when the consumer falls behind and a read
is larger than the remaining queue capacity. It can corrupt terminal output
and file-transfer byte streams without a reported error.

Reproduction: `test_partial_input_queue` in
connection/tests/st_connection_test.c uses the internal transport bind hook,
before workers start, to preload 16,383 bytes into the 16,384-byte input queue.
The fake transport supplies four more bytes; the test waits until the queue
is full, drains it, then waits for the remaining bytes. Both native builds
report "input queue retained 16384 of 16387 bytes" and fail. Queue observations
use the locked public pending/receive operations, with a bounded polling wait
rather than assuming a fixed sleep establishes the state. That wait permits a corrected
worker to block retaining the suffix until the test drains the queue.

Recommendation: have the worker retain and enqueue each received chunk's
entire suffix before reading again, with shutdown/error handling at each
iteration. Preserve the public send API's documented short-write behavior;
changing that contract is not necessary. No implementation change made.

### A015 — POSIX shell startup forces raw terminal settings

Severity: high behavioral regression. Classification: confirmed source and
native reproduction; interactive-shell-specific recovery behavior not tested.

`connection/connection_shell_posix.c:291-299` disables OPOST, ECHO, ICANON,
ISIG, ICRNL and related terminal processing before executing the shell.
The legacy conn_pty.c:437-443 instead initializes TTYDEF input/output/local
flags, disabling only software flow control in the input flags. Programs
are then free to select raw mode themselves when needed. The new code makes
ordinary command output lose LF-to-CRLF processing; programs that rely on
canonical input, echo or terminal-generated signals also inherit those
facilities disabled, unless they explicitly restore them.

Reproduction: test_shell_line_discipline in connection/tests/st_connection_test.c
opens a shell connection running `printf 'a\nb\n'`. Both Clang and GCC native
builds return four bytes rather than the expected six-byte `a\r\nb\r\n`.
The test reads through the public connection API, closes/reaps the child, and
fails after cleanup. The existing shell test's output contains no newline,
so it did not detect this difference. This is separate from A014: only four
bytes enter an otherwise empty input queue.

Recommendation: initialize the slave terminal with the legacy interactive
line-discipline policy and let child applications request raw mode. Verify
echo, Return, signal keys and ordinary newline output; do not compensate by
rewriting bytes in the transport. No implementation changed.

### A016 — ConPTY atomic handles do not protect active users from close

Severity: medium. Classification: confirmed source-level lifetime race;
Windows runtime reproduction and actual handle reuse not demonstrated.

`connection/connection_shell_windows.c:38,57` loads a pipe handle atomically,
then uses it in ReadFile/WriteFile at lines 44/63. Concurrent shutdown calls
shell_interrupt:83-84, atomically clears and immediately closes both handles.
A worker can be descheduled after loading a non-NULL handle and resume its
I/O call after that handle is closed. Atomic exchange prevents double-close
by the owners but does not retain ownership for a reader of the value.
The public no-concurrent-close rule does not prevent this race: these are
the connection's own workers, joined only after interrupt runs.

At minimum the worker can issue I/O on an invalid handle; if the numeric
handle is reused in the interval it could address a different object.
No claim is made here that closing necessarily cancels already-pending
synchronous I/O, or that the present code was observed hanging on Windows.
Those shutdown semantics also need Windows execution tests.

Recommendation: coordinate cancellation/unblocking and worker completion
without invalidating handles still usable by workers; close the owned handles
only when their users are finished. A mutex held over indefinitely blocking
I/O is not by itself a sufficient cancellation strategy. No code changed.

### A017 — RLogin urgent flush clears the wrong direction

Severity: medium. Classification: confirmed source/protocol mismatch;
network-level urgent-flush reproduction not added.

RFC 1282's From Server to Client section specifies that urgent 0x02 discards
received server data awaiting display. connection/connection_network.c:92-94
instead invokes st_connection_clear_output, which removes queued outbound
user input and leaves the input queue untouched. The same mistake exists in
the inspected legacy rlogin.c:116-123, so this is retained incorrect behavior,
not a newly introduced regression. Existing tests do not send 0x02.

The same section requires a window response to urgent 0x80. The new handler
only responds when window_enabled was previously false, so repeated requests
are ignored. The legacy code has the same limitation. Current tests send one
request and do not exercise repetition.

Recommendation: handle flush against pending server-output data, accounting
for data before the urgent mark and any consumer-owned buffering, without
discarding outbound user input; answer each window request. Preserve the
explicit BBS username/password conventions separately from protocol fixes.
Source: [RFC 1282](https://www.rfc-editor.org/rfc/rfc1282.html). No code changed.

### A018 — Profile flow-control choices are cast to unrelated serial flags

Severity: high. Classification: confirmed interface integration defect,
reproduced using the serial test provider on native Clang and GCC.

`connection/connection_serial.c:230` casts profile->flow_control directly to
the serial_config bitmask. The two declarations have different meanings:

| Profile choice/value | Required serial flags | Actual serial flags |
| --- | --- | --- |
| RTS/CTS = 0 | RTS/CTS = 1 | None = 0 |
| XON/XOFF = 1 | XON/XOFF = 4 | RTS/CTS = 1 |
| Both = 2 | RTS/CTS + XON/XOFF = 5 | DTR/DSR = 2 |
| None = 3 | None = 0 | RTS/CTS + DTR/DSR = 3 |

This applies to serial and modem setup; three-wire additionally masks the
wrongly translated result. The default profile choice (RTS/CTS) therefore
silently disables flow control. Real driver acceptance of unwanted DTR/DSR
depends on platform capability, but the incorrect flags are deterministic.

Reproduction: new test_profile_flow_mapping in
connection/tests/st_connection_serial_test.c sets all four ST_PROFILE_FLOW_*
choices, inspects the serial configuration delivered to the fake, then closes
each connection. Both native toolchains fail all four mappings. Existing
tests assigned SERIAL_FLOW_CONTROL_* values directly to the profile field,
bypassing the public model and masking this bug.

Recommendation: explicitly translate the profile enum to the serial bitmask
and reject unknown choices; apply the three-wire restriction after translation.
Test all choices through profile declarations, including modem and three-wire
paths. No production or public interface change made.

### A019 — INI insertion after an unterminated line corrupts serialization

Severity: high. Classification: confirmed round-trip/data-preservation defect,
reproduced on native Clang and GCC.

`ini_section_set_string` inserts a new raw record without ensuring the preceding
record ends in a newline. The parser legitimately retains a last line without
one. Serializing therefore concatenates the old record with the new key.
ini_document_add_section has the same missing-boundary check when appending
with the default empty section separator (source inspection, not separately
tested yet).

Reproduction: new test_append_after_unterminated_line in
ini/tests/ini_interface_test.c parses `First=one`, adds Second=two in the root,
serializes and reparses. Output is `First=oneSecond=two\n`: the original value
changes and the new key disappears. Starting with `[Section]` instead produces
`[Section]Second=two\n`, losing the section as well. Both cases fail on Clang
and GCC; MinGW compiles the test but has not executed it. Existing parse tests
cover unterminated input without this subsequent insertion.

Recommendation: maintain physical record boundaries when inserting/appending,
while retaining the missing final newline on unmodified round trips. Cover
keys, section headers, section additions, LF/CRLF and trailing trivia in tests.
No production changes made.

### A020 — INI setter accepts keys that its parser cannot recognize

Severity: medium. Classification: confirmed successful-mutation round-trip
failure on native Clang and GCC.

ini_section_set_string validates printable names excluding '=' and ':' and
rejects a leading ';', but permits leading '['. parse_line treats any line
starting with '[' as a section header or malformed section, never a key.
Setting `[Key]` to `value` succeeds in memory, serializes as `[Key]=value\n`,
and then disappears on reparse. Because clone uses serialize/parse, cloning
also cannot preserve such an accepted entry (source consequence, not separately
tested). A malformed section also suppresses later keys until a valid header.

New test_accepted_key_round_trip demonstrates the loss on both native builds.
It allows either explicit INVALID_ARGUMENT rejection or successful preservation,
without prescribing a new escaping syntax. Recommendation: make setter name
validation and documented grammar agree with the parser, and test all reserved
leading syntax. Review style prefixes/separators for the same mismatch; those
remain source concerns, not independently reproduced findings. No code fixed.

### A021 — Compact date-only branch is shadowed by epoch parsing

Severity: medium. Classification: confirmed unreachable intended parsing path;
the desired precedence is an unresolved API contract decision.

value_parse_datetime first accepts any int64 text representable by time_t as
an epoch value, then attempts length-eight YYYYMMDD date parsing. Ordinary
eight-digit dates are already accepted by the integer branch, so the later
date-only branch cannot interpret them as calendar dates on tested targets.
The compact date-with-time form containing 'T' does reach the calendar parser.

New test in value_convert/tests/value_convert_test.c compares `20200102` with
`Jan  2 2020` in the same process/timezone. Both Clang and GCC fail the equality
check: the former is interpreted as the integer epoch count. This test encodes
the calendar meaning implied by the existing length-eight branch, not a claim
that the currently undocumented public API resolves the ambiguity that way.

Recommendation: explicitly decide/document numeric-epoch versus compact-date
precedence, preserve required consumer/legacy behavior, and make all supported
date syntaxes reachable. Do not simply reorder parsing without considering
existing epoch inputs. Implementation unchanged; failing test retained.

### A022 — Crossfade completion inserts pull-size-dependent silence

Severity: medium. Classification: confirmed deterministic audio discontinuity,
reproduced on native Clang and GCC.

pull_chunk_locked in audio_mixer/audio_mixer.c unconditionally breaks its
stream-buffer loop after mixing an overlapping pair. Even if both buffers
finish before the requested chunk ends, the next queued buffer is deferred
until another pull/chunk. The remaining accumulator samples stay zero.

New test_crossfade_pull_partition queues three two-frame buffers, with the
second crossfading over one frame of the first. It compares a four-frame pull
with four single-frame pulls of identically initialized mixers. Bulk output's
last two left samples are 0,0; partitioned output's are 2990,2990. Both native
toolchains fail; cleanup completes before assertion. This checks stream
continuity rather than prescribing exact crossfade gain curves.

Recommendation: continue through subsequent queued buffers at the correct
output offset after an overlap finishes, retaining both participants' consumed
counts and ticket lifetimes. Test overlap shorter than a chunk, successor
buffers, loops and multiple adjacent crossfades. No mixer code changed.

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

### A024 — POSIX theme catalog hides directory-open errors

Severity: low. Classification: confirmed error-reporting defect, reproduced.

- `theme/theme_directory_posix.c:76` returns `IO_ERROR_NOT_FOUND` for every
  failed `opendir()`, regardless of errno. This includes a non-directory path,
  permission denial, descriptor exhaustion and actual absence.
- `theme/st_theme.c:1048` intentionally treats NOT_FOUND as an empty optional
  themes directory, returning success with only the Classic entry. Thus real
  configuration/access failures are silently presented as an empty catalog.
- New `test_invalid_catalog_directory` in `theme/tests/st_theme_test.c` uses
  an existing regular theme file as the directory. Manager creation succeeds;
  catalog refresh incorrectly returns zero. Reproduced with native Clang and
  GCC. MinGW compiled/linked, not executed; Windows enumeration does preserve
  FindFirstFileW errors, so this is not a claim of the same Windows defect.
- Related source-only gap: the POSIX readdir loop does not distinguish an
  enumeration error from end-of-directory. No injected readdir failure tested.

Recommended remedy: preserve/map directory-open errors, distinguish readdir
failure from EOF, and reserve the optional-directory fallback for actual
absence. Do not eliminate the intended missing-directory fallback.

### A025 — Theme editor loses dirty state after branching from undo

Severity: medium. Classification: confirmed correctness defect, reproduced.

`theme/st_theme_document.c:380` discards redo states and reuses their numeric
positions without invalidating saved_position. Dirty checking at line 1046
only compares positions; save records the current position at line 1443.
Open a file, edit Author to `saved`, save, undo, edit Author to `different`:
the new state reuses the saved index and dirty() returns false, although disk
still contains `Author=saved`. A caller relying on dirty() can discard the
unsaved edit without warning. `test_branch_dirty` reproduces this under native
Clang and GCC; MinGW compile/link only.

Recommended remedy: invalidate the saved-state marker when its redo branch
is discarded, and test save/undo/branch combinations. No fix made.

### A026 — Resetting the last theme override is not persisted

Severity: medium. Classification: confirmed persistence defect, reproduced.

`theme/st_theme_document.c:1213` skips a style with no explicitly present fields
instead of removing its old keys from the cloned source. Resetting the sole
Foreground override on frame shows the correct built-in value in preview, but
save retains the old source override. Reopening restores 0x123456 rather than
the preview's 0xffff55. `test_reset_last_override` reproduces this under native
Clang and GCC; MinGW compile/link only. The same skip exists for glyphs at
line 1249; resetting the last override on a built-in glyph is source-affected
but not separately reproduced here.

Recommended remedy: reconcile zero-presence fields with retained source keys,
without dropping unrelated comments/unknown fields. Test reset/save/reopen
for both styles and glyphs. Validation of syntax alone does not catch this.

### A027 — Failed theme document construction can free inputs twice

Severity: high, conditional on construction failure. Classification:
source-confirmed ownership defect; allocation failure not injected yet.

`theme/st_theme_document.c:448` assigns source/original to the new document
and moves the initial state. If preview construction then fails (including
allocation failure), line 455 frees the document, including source/original.
The callers treat nonzero status as retaining input ownership: create at
line 496 frees source again, open at line 537 frees source and original again,
and copy_path at line 576 frees source again. Nulling the callee's document
fields does not null these separate caller-local pointers. This can become
use-after-free/double-free while attempting to report the original failure.

Recommended remedy: make constructor ownership transfer consistent on all
returns, and test each failing allocation stage. This report does not claim
a normal successful-construction crash or an executed OOM reproduction.

### A028 — Short theme-file reads skip fclose

Severity: low. Classification: source-confirmed resource leak; short-read
injection/concurrent truncation not reproduced.

Both `theme/st_theme_cloud.c:193` (hash input) and
`theme/st_theme_document.c:420` (original/conflict snapshot) use
`fread(...) != expected || fclose(...) != 0`. A short/error read satisfies the
first operand, so fclose is never executed; only the buffer is freed.
Repeated failures leak descriptors/stream allocations. Files shrinking between
size query and read are one possible trigger; ordinary complete reads close.

Recommended remedy: close the stream on every read outcome while preserving
the primary failure. Add controlled short-read/error-path coverage.

### A029 — An accepted empty font rename disappears on settings reload

Severity: medium. Classification: confirmed persistence defect, reproduced.

`settings/st_settings.c:361` explicitly permits an empty replacement font name.
Saving encodes it as [Font:], but `settings/st_settings_store.c:421` ignores
that section on load. The font record/path is lost from the loaded model even
though rename and save returned success. The next font rewrite can remove its
remaining on-disk section. `test_empty_font_name` adds a named font with a path,
renames it empty, saves and reloads: count becomes zero instead of one.
Reproduced with native Clang/GCC; MinGW compile/link only.

Wren.adoc's MenuFont.name contract explicitly permits empty replacement names,
so silently tightening the public behavior is not an approved remedy. Preserve
the supported record across save/load, or obtain explicit authorization for a
contract change. Test also allows an explicit rejection rather than data loss;
that allowance does not by itself establish Wren compatibility.

### A030 — Unbounded numeric theme colors reach RGB array indexing

Severity: high. Classification: confirmed validation defect; invalid input
propagation reproduced, subsequent out-of-bounds access established by source.

`settings/st_settings_store.c:344` and adjacent color loads use
ini_section_get_enum_names, whose implementation (`ini/ini.c:787`) intentionally
also returns arbitrary parseable numeric values without bounding to name_count.
The unsigned settings fields therefore accept FrameColour=1000000. In
`theme/st_theme.c:406`, classic_colour only handles the exact default sentinel;
set_complete_style then indexes its 16-entry rgb array at lines 383-384 with
the supplied foreground/background. Theme manager creation always builds a
Classic theme, even when a package is selected, so this path is not restricted
to explicitly choosing the Classic theme.

`test_classic_color_bounds` reproduces the unbounded settings value under
Clang/GCC. It deliberately does not invoke unsafe rendering/theme construction.
MinGW compiled/linked only. Other color keys have the same source pattern.
Recommended remedy: validate each consumer's enum range/default sentinel before
array indexing, including public struct inputs; do not globally restrict INI's
numeric-enum API without reviewing its other consumers.

### A031 — Settings saves truncate the only copy before writing

Severity: medium. Classification: source-confirmed data-loss risk on write
failure; no disk-full/process-kill reproduction performed.

`settings/st_settings_store.c:635` opens the existing settings file with wb.
Once open succeeds the previous contents are gone; a short write/close failure
returns an error but cannot restore them. The sidecar lock protects cooperating
writers, not failure atomicity or readers which ignore the lock. The public
description calls this a read/merge/write transaction without documenting that
failure can leave truncated settings. No temporary replacement, backup or
fs_stream_sync is used here (unlike theme document saves).

Recommended remedy: retain the old file until a complete replacement is ready,
with explicit replacement/durability semantics and fault-injection tests.
This is not a claim that every successful fclose must imply crash durability.

### A032 — Classic color migration lost per-key UIFC fallback

Severity: medium. Classification: confirmed legacy behavior regression,
reproduced.

`settings/st_settings_store.c:246` selects UIFC only when ClassicTheme is wholly
absent. Legacy `/synchronet/src/sbbs/src/syncterm/syncterm.c:1876` selects the
section independently for each key using iniReadExistingString. With
[ClassicTheme] FrameColour=Blue and [UIFC] TextColour=Red, the new loader returns
default TextColour 16, not Red 4. The new test_classic_per_key_fallback confirms
this under native Clang/GCC. MinGW compile/link only; legacy helper inspected
read-only, not executed. Recommended remedy: preserve per-key migration fallback
and test mixed-section files without eliminating the newer-section precedence.

### A033 — BBS sort-field normalization overflows on INT_MIN

Severity: medium. Classification: confirmed undefined arithmetic, reproduced.

`bbs_list/st_bbs_sort.c:26` computes -field before validating its magnitude.
On the tested targets INT_MIN cannot be negated as int. The scalar public
normalizer, field-name lookup and list sorting all reach this expression.
The standalone test `bbs_list/tests/st_bbs_sort_boundary_test.c` includes the
unchanged production source, supplies only a count stub for unused list sorting,
and tests valid aliases and invalid boundaries. Clang and GCC UBSan both stop
at line 26 with negation-of--2147483648 overflow. No application-input
reachability claim yet; host sort-order parsing remains to review.

Recommended remedy: validate without negating INT_MIN and cover the complete
int boundary contract. Also source-only: field_count*sizeof(int) in sort's
allocation lacks an overflow check; callers' count bounds remain to review.

### A034 — BBS cache maintenance does not confine entry names to cache paths

Severity: high. Classification: source-confirmed destructive-path defect;
no destructive reproduction executed.

`bbs_list/st_bbs_list.c:34` permits printable ASCII names except brackets and
one reserved literal. It accepts '.', '..', and '../outside'. Add/update/remove
use this validator; personal sections loaded from disk use it too. Both cache
backends concatenate those names with cache_path using path_join_alloc, which
does not sanitize components. POSIX `st_bbs_cache_remove` (line 94) passes that
path directly to recursive remove_tree. Thus deleting a personal entry named
'../outside' can delete the sibling path rather than that entry's cache.
Rename also accepts unconstrained source/destination components. Windows uses
the same join before path conversion and recursive removal; exact Windows
normalization/runtime impact not tested. This is not a claim that opening a
remote list by itself triggers deletion.

Recommended remedy: separate BBS display names from safe cache components and
enforce confinement at every destructive cache boundary. A display-name ban
alone may break existing lists and is not assumed authorized. A future
reproduction must intercept filesystem operations or use only disposable
fixtures, never real user cache/data. Dot-component paths can target a broad
parent, so do not execute them against existing directories during the audit.

### A035 — BBS mutation can use borrowed input after rebuilding frees it

Severity: high, conditional on borrowed input. Classification: source-confirmed
lifetime defect; no sanitizer reproduction yet.

`st_bbs_list_remove(list, st_bbs_list_at(list, i)->profile->name, ...)` supplies
a name valid at call entry. At `bbs_list/st_bbs_store.c:374`, install_document
rebuilds the view and frees that profile. Line 376 then uses name again for
cache removal. Similarly update uses old_name and profile->name at lines
314-315 after installing/rebuilding; both may alias existing entries. No public
contract prohibits using returned profile/name pointers as mutation inputs.
Existing tests use string literals and independent profile copies, so they
do not cover these aliases. The consequence includes using freed bytes as
filesystem path input, not just an incorrect UI value.

Recommended remedy: retain required input values until all post-rebuild work
finishes, and test getter-derived input arguments. Document borrowed view
invalidation separately; it does not justify invalidating arguments while the
callee still needs them.

### A036 — Downloader closes the socket twice after TLS setup failure

Severity: high, conditional on descriptor reuse. Classification: confirmed
source-level double-close; concurrent descriptor reuse not reproduced.

`webget/st_webget.c:826` closes transport.socket on cancellation at TLS progress;
line 831 does the same when TLS open fails. Neither invalidates the field.
The caller then reaches cleanup at line 1093, and close_transport at line 844
closes the same still-valid-looking handle again. st_tls.h explicitly retains
caller socket ownership on failed open, so that contract does not remove the
second close. Between closes, another thread can acquire the recycled socket
descriptor and have it closed by this cleanup. BBS batch downloads provide
actual concurrent socket activity, though the reuse race is not tested here.

Recommended remedy: use one cleanup owner or invalidate immediately upon close;
test cancellation and failed TLS setup with a controlled socket provider.

### A037 — Changing a download URL can reuse another resource's cache

Severity: medium. Classification: confirmed cache-identity defect, reproduced.

`webget/st_webget.c:37` cache_info and its on-disk metadata contain freshness
and validators, but no source URL. Fresh-cache lookup at line 938 uses only
cache key/output path. BBS callers derive these from the configured list name,
not its URL. Editing that list's URL retains the old resource as fresh (up to
the one-day default or supplied max-age); stale metadata can also send old
validators to the new endpoint. Theme packages have a similar stable ID/URI
separation. No caller-side metadata invalidation was found in the BBS/settings
APIs reviewed so far; application UI behavior remains a separate audit area.

New webget test downloads /default, then requests /different using the same
key/output and a callback cancelling before any network action. It returns
success using the old fresh file instead of reaching cancellation for the new
resource. Reproduced with native Clang/GCC; MinGW compile/link only. Existing
304 test also intentionally changes ephemeral server port, illustrating the
current cross-URL validator behavior rather than establishing correctness.

Recommended remedy: bind cache metadata/validators to resource identity and
invalidate them when the requested URL changes. Update the conditional-response
fixture to use one resource endpoint when implementing that behavior.

### A038 — OpenSSL TLS 1.3 PSK mode accepts an unauthenticated certificate server

Severity: high. Confirmed by a local regression; not an inferred cipher-policy
concern. `xptls/st_tls_openssl.c:253` installs the TLS 1.3 PSK callback but
`:257` disables certificate verification. After `SSL_connect()` succeeds,
`:330` captures certificates and publishes the session without verifying that
the handshake actually used the PSK. A peer can instead complete an ordinary
certificate handshake without knowing the configured secret. The Botan
implementation explicitly rejects `psk && !session->psk_used` at
`xptls/st_tls_botan.cpp:469`.

`xptls/tests/st_tls_psk_required_test.c` constructs a local certificate-only
TLS 1.3 server with a freshly generated self-signed certificate, no PSK
callback and no configured secret. It connects through the unchanged public
client API with a nonempty PSK and requires rejection. Native OpenSSL returns
0 (regression exits 1); the same test linked against the Botan provider
returns -34 and passes. The fixture uses a POSIX socket pair and a child
process, no external network service or trust-store changes. No Windows
runtime verification. The normal repository caller currently found is
webget using WEB_PKI, not PSK; this finding concerns the exposed PSK API and
must not be described as a demonstrated web-download authentication bypass.

Recommendation: enforce successful PSK authentication before publishing a
PSK session, independently of the TLS 1.3 negotiated cipher suite. OpenSSL's
[PSK callback documentation](https://docs.openssl.org/3.0/man3/SSL_CTX_set_psk_client_callback/)
states that TLS 1.3 PSK establishment is observable through
`SSL_session_reused()`. Add provider-parity tests for a correct PSK, incorrect
PSK, and certificate-only fallback. No implementation change made.

### A047 — Standalone console build omits its string_compat dependency

Severity: low (component build correctness). console/CMakeLists.txt declares a
standalone STConsole project and adds absent component targets individually,
but never adds string_compat before linking syncterm_string_compat. A top-level
build supplies that target and masks the omission. A fresh native Clang
configure with `cmake -S console -B /tmp/st-console-config-audit.fv42uV
-DBUILD_TESTING=OFF -DST_CONSOLE_X11=OFF -DST_CONSOLE_WAYLAND=OFF
-DST_CONSOLE_SDL=OFF` succeeds; building then fails at console.c:13 because
string_compat.h is not found. No build files were edited.

Recommendation: establish the dependency target in the standalone path just
like the other required libraries, and retain a minimal standalone build check.
This does not imply the top-level application build fails.

### A048 — X11 runtime loader gives XkbFreeKeyboard the wrong function type

Severity: low (confirmed declaration mismatch; no runtime failure reproduced).
console/console_x11.c:121 and :222 declare the dynamically loaded
XkbFreeKeyboard as returning Status. The installed X11/XKBlib.h:645 declares
void XkbFreeKeyboard(XkbDescPtr, unsigned int, Bool). build_keycode_map calls
through the incompatible pointer at :1518; discarding the result does not
make the function types compatible.

The compile-only console/tests/st_console_x11_signature_test.c includes the
unchanged backend and uses C17 _Generic assertions against the header,
loader typedef and loader member. Native Clang accepts the official-header
assertion and fails both loader assertions. Run with -fsyntax-only and the
st_console target's include directories from flags.make; no linking, display
access or CMake edits are needed. This demonstrates the type mismatch, not a
crash or a particular ABI consequence.

Recommendation: match the installed public declaration exactly and check
other manually declared dynamic entry points against their public headers.

### A049 — X11 external-scaling pixmap is not initialized on first scaled resize

Severity: medium (source-confirmed missing upload; visible result not exercised
against an X server). In console_x11.c:draw_state, external mode at exactly
the source dimensions uses XPutImage directly on the window. The XRender
pixmap is created only when destination dimensions subsequently differ.
ensure_render_resources creates that pixmap but does not populate it;
draw_state uploads only when damage_valid is true, then composites it.

The presenter external branch in console_bitmap.c does not upload pixels for
a host-size-only change: it sets frame.changed through layout_changed, while
preserving the native raster. Consequently an unchanged, cursor-hidden image
starting at 1:1 and then resized larger reaches a newly created pixmap with
no XPutImage at all. Cursor-only damage can populate just a small portion.
Likewise an existing pixmap can become stale while rendering goes directly
to the window at 1:1, then be reused on returning to scaled dimensions.

Existing st_console_x11_test.c checks external-mode creation and activation
success, not displayed pixels across the 1:1-to-scaled transition. This is
not a claim that a real-display reproduction was run. Recommendation: track
the pixmap's content validity separately from the XImage's damage and upload
the complete image when establishing or re-establishing that validity.
Legacy comparison: conio/x_events.c:1937 uploads the full dw-by-dh XImage to
xrender_pm immediately before XRenderComposite; the new damage-only upload
removed that initialization guarantee. Legacy source was read, not executed.

### A050 — Wayland upper/left margin mouse coordinates wrap to the opposite edge

Severity: medium (confirmed input-coordinate error). In
console/console_wayland.c:mouse_position, wl_fixed_to_int(x) is signed int
but frame.destination.x is uint32_t. Subtraction therefore wraps in unsigned
arithmetic before assignment to int64_t. Negative differences bypass the
negative clamp and hit the maximum-coordinate clamp instead. Y is identical.

The standalone console/tests/st_console_wayland_mouse_test.c includes the
unchanged implementation and calls the conversion with source 80x40,
destination {10,10,160,80}, and pointer (5,5). Native Clang returns (79,39)
instead of (0,0). Compile with the st_console target's include paths (including
build/console/wayland-protocols) and st_console_test's library dependencies.
The executable needs no compositor and creates no window. X11 widens the
coordinate before subtracting and does not have this particular expression.

Recommendation: perform subtraction in a signed type that represents both
operands before clamping; test each margin and the interior independently.

### A051 — Wayland clipboard send can terminate the process on receiver closure

Severity: medium (confirmed process termination under default SIGPIPE policy).
console/console_wayland.c:source_send writes directly to the recipient's pipe
without suppressing SIGPIPE. If the recipient closes its read end, the write
can terminate the process before the callback reaches its error handling.

console/tests/st_console_wayland_clipboard_pipe_test.c invokes the unchanged
callback in a child process with SIGPIPE explicitly set to its default and a
local pipe whose reader has been closed. Native Clang execution reports
termination by signal 13 instead of normal callback return. No compositor,
system clipboard, external recipient or user window is involved. Compile and
link using the same paths/dependencies as the A050 standalone test.

This establishes library behavior with default signal handling, not the
complete application's signal disposition. No SIGPIPE handling was found in
the initial app/console/connection search, but application-wide startup and
dependency effects remain part of the pending integration review.
Recommendation: make peer closure an ordinary transfer failure without
requiring an undocumented process-global signal policy. Also test transfer
cancellation and shutdown while a recipient stops reading; blocking writes
on the worker are a separate unresolved responsiveness concern.

### A053 — Bare -i option reads past the argument terminator

Severity: low (confirmed one-byte out-of-bounds read on malformed local CLI
input). app/st_command_line.c:38 reads value[1] in parse_backend before
checking whether value[0] is NUL. st_command_line_parse passes argument+2
for -i, so a precisely sized "-i" argument makes that access one byte past
the allocated string. Subsequent validation rejects the option, too late to
prevent the read.

app/tests/st_command_line_empty_backend_test.c supplies a three-byte heap
string containing "-i". Native Clang AddressSanitizer reports the read at
parse_backend:38 via st_command_line_parse:199. Reproduce with:
`cc -std=c17 -g -fsanitize=address -Iapp -Iconsole -Ipixels -Iprofile
-Iio_error app/tests/st_command_line_empty_backend_test.c
app/st_command_line.c -o /tmp/st_command_line_empty_backend_test`, then run
that executable. No application startup, settings access or display involved.

Recommendation: validate the first character before reading the second;
retain empty, one-character, valid two-character and excess-character option
tests. No broad exploitability claim is made for this local argument error.

### A054 — Additional user event scripts are never auto-loaded

Severity: medium (documented extension behavior missing). Source-confirmed.
Wren.adoc:918 specifies discovering scripts/auto/<event>/ and executing user
scripts in addition to embedded event modules. wren_host/wren_host.c:425
instead iterates only st_wren_embedded_scripts and loads those names. Its
menu and picker callers do not enumerate the user event directory. A new
auto/menu/my_startup.wren or auto/picker/my_startup.wren therefore never runs
unless another module explicitly imports it. Existing-name overrides can load;
this is not a claim that all overrides are broken.

The CMake embedding list contains only menu_host_ui/main_menu for menu and
file_picker for picker. The generator preserves this list with event tags;
it cannot discover additional user files at runtime. Recommended regression:
place a distinct marker module in each temporary event directory, create the
host, and verify the module was interpreted exactly once without an import.
The new st_wren_loader_audit_test.c autoload case now confirms that neither
marker module is loaded despite successful headless host creation. It tests
presence, not an exactly-once counter. Remedy: implement the documented
event discovery/order contract and test unique scripts, overrides and imports
together, rather than treating the embedded-name list as the complete event.

Legacy comparison: wren_menu_host.c:269 and wren_picker_host.c:292 in the
read-only Synchronet tree glob their respective auto directories, suppress
overridden embedded modules and interpret additional user files. This supports
classifying the missing discovery as lost behavior, not just aspirational docs.

### A055 — Picker load failure prevents application startup instead of recovery

Severity: medium (local script error terminates the application). Source-confirmed.
Wren.adoc:1287 promises a recovery surface for picker implementation compile
or runtime failures, reserving complete disablement for VM/bootstrap failure.
In wren_host/wren_host.c:732, an error interpreting the picker event returns
immediately, before recovery handles are acquired. st_wren_host_create at
:879 propagates that error after destroying the entire host. app/syncterm.c
then reports menu initialization failure and exits. A syntax error in a
file_picker override thus cannot reach the promised picker REPL/cancel screen.

The separate recovery call in wren_picker.c belongs to invocation of an
already initialized picker and does not cover this initialization failure.
Recommended regression: use an isolated invalid file_picker override, assert
that the menu host still initializes, then exercise picker recovery/cancel.
The new st_wren_loader_audit_test.c recovery case now confirms host creation
fails with IO_ERROR_INVALID_FORMAT (-37) for a syntactically invalid picker
override. It does not reach recovery-screen interaction. Remedy: preserve a
usable bootstrap/recovery path when the optional picker implementation fails,
without disguising VM/bootstrap initialization failures.

Legacy comparison: wren_picker_host.c:370 acquires bootstrap/recovery handles
before load_event_scripts(), and does not make a missing FilePicker class
fatal to picker initialization. The modern host moved implementation loading
ahead of recovery-handle setup and added the fatal return/class requirement.

### A056 — POSIX picker reports directory read errors as successful listings

Severity: low (misleading empty/partial directory display). Confirmed with a
controlled native failure. wren_host/wren_picker_fs_posix.c:83 terminates its
readdir loop on NULL without checking errno to distinguish EOF from failure.
With no prior entry error and successful closedir, it returns success. The
request-list consumer at wren_picker.c:494 consequently treats the incomplete
result as a valid listing instead of returning its failure result.

st_wren_picker_readdir_error_test.c includes the production POSIX adapter with
only readdir replaced by a function returning NULL/EIO. Real opendir/closedir
operate on /tmp; no directory contents are modified. Native Clang C17 with
-Wall -Wextra -Wpedantic -Werror compiles the fixture; running it prints
`directory read error reported: FAIL (status 0)` and exits 1. Link dependencies
are filesystem, path, utf8, string_compat and io_error; include directories
also require wren_host, profile and wren_vm/include. No actual disk fault or
partial-results test is claimed.

Recommendation: clear/check errno immediately around each readdir call, retain
the primary read error through close, and test EOF separately from read failure.
Metadata failures need a consistent skip-or-fail policy too: current fs_get_info
errors continue but leave status set, so whether they ultimately fail the whole
listing depends on whether a later entry overwrites status. That ordering
problem is source-observed, not exercised by this read-error fixture.

### A058 — Wren cannot round-trip every 64-bit mouse-event mask

Severity: medium API representation limitation. The native mask is uint64_t,
and the single-event helpers correctly support event numbers 0 through 63.
`Input.mouseEvents`, however, crosses the Wren boundary as a Num, whose exact
integer range ends at 2^53 - 1. Masks combining high and low bits can therefore
lose low bits when read and restored, and the whole-mask setter cannot express
bit 63. Define a lossless script representation or constrain and document the
whole-mask contract without narrowing the single-event helpers.

### A061 — Failed Menu.load discards the existing model

Severity: medium (loss of in-memory directory state on reload failure).
Confirmed through the native headless Wren host. wren_bind_menu.c:314 closes
host->bbs_list and clears it before attempting st_bbs_list_open. The documented
transactional load contract (Wren.adoc:4040) requires failure to leave the old
model and generation intact. The lower-level open API constructs a separate
list, so the destructive ordering is in this binding, not a necessary property
of opening a list.

The `reload` case in st_wren_loader_audit_test.c writes one temporary personal
entry, verifies Menu.load(null) succeeds with exactly one entry, then points the
borrowed test paths at a directory instead of a file. A second Menu.load fails
as expected, but Menu.entries becomes empty; the test exits 1 with
`failed reload lost old entries`. No personal file is touched. Remedy: open
and validate the replacement before swapping model ownership, with tests for
password/decryption, I/O and allocation failure as well as successful replacement.
Only the I/O-path failure was reproduced, not unsaved-edit loss or generation.

### A062 — BBS.save writes the personal list in safe mode

Severity: high (violates explicitly selected no-write mode). Confirmed using
only an isolated temporary personal list. app's -s help promises to prevent
writing local files and passes safe_mode to the host. In
wren_bind_menu.c:3419, fn_bbs_save calls the persistent add/update/defaults
operations without checking host->config.safe_mode. st_bbs_store.c writes
the personal file and has no separate safe-mode policy. This is distinct from
sort-profile/theme bindings which explicitly suppress persistence in safe mode.

The `safe-save` case in st_wren_loader_audit_test.c creates a one-entry fixture,
constructs a host with safe_mode=true, asserts Host.safeMode and successful
Menu.load, edits that entry's comment and calls save(). Reading the fixture
afterward shows changed contents; the unchanged-file assertion fails. The
native Clang strict C17 test exits 1 with `safe-mode save leaves file unchanged:
FAIL`. It does not touch personal settings or any non-fixture file. The save
return value is not constrained: refusing or keeping edits in memory would
both satisfy this no-disk-write test.

Recommendation: enforce no-write policy at every host persistence boundary,
including add/update/default/delete and cache side effects, with isolated
safe-mode tests for each. Delete has the same missing guard in source but was
not executed; do not claim its runtime verification from this save test.

### A063 — Settings snapshots never become stale after another snapshot applies

Severity: medium (documented lifetime/conflicting-update contract). Wren.adoc's
Menu Program Settings section requires successful apply/save to invalidate
other outstanding Settings objects. wren_bind_menu.c:53 stores only the
snapshot and dirty flag; settings_value at :143 checks the foreign kind but
has no generation check. settings_apply at :2965 copies the entire snapshot
to running settings without invalidating other snapshots. Consequently an
old editor can remain usable and potentially overwrite newer accepted values.

Confirmed with the `settings-stale` case in
wren_host/tests/st_wren_loader_audit_test.c: acquire two snapshots, toggle
confirmClose on the second, successfully apply it, and read the first inside
a catching Fiber. The getter returns normally instead of aborting. Native
Clang strict C17 build succeeds; the test exits 1 with `settings apply
invalidates other snapshots: FAIL`. This uses a headless temporary host and
does not initialize or save personal settings. Overwriting newer values was
not executed; it follows from the unchecked whole-snapshot apply path.

Recommendation: implement the documented snapshot validity contract, with
tests for apply/save/reload and multiple editor handles. Do not infer a
memory-use-after-free here: snapshots own their settings data.

### A064 — Failed Settings.save still applies the previously unapplied snapshot

Severity: medium (transaction/visible-state contract). Wren.adoc Menu Program
Settings explicitly promises that an unapplied snapshot is not applied when
save fails. wren_bind_menu.c:2995 instead calls settings_apply before
st_settings_save, so failure of the latter leaves running settings changed
and the snapshot marked clean. The API returns false after those effects.

Confirmed by `settings-save-failure` in st_wren_loader_audit_test.c. The host
has no loaded settings store, guaranteeing that st_settings_save returns
IO_ERROR_INVALID_ARGUMENT before file I/O. The script toggles confirmClose,
checks save returns false, and reads a fresh Menu.settings snapshot. It sees
the changed value; the native strict Clang test exits 1 with `failed settings
save preserves running values: FAIL`. This is a persistence initialization
failure, not an injected disk-full or permission error; those paths were not
executed. No personal files were loaded or written.

Recommendation: ensure persistence failure preserves the documented running
state and dirty state, including effects on theme/scaling. Add failures after
store initialization as well as this early error; do not silently weaken the
documented transaction guarantee.

### A065 — Settings.reload reads disk rather than current running settings

Severity: medium (snapshot/reload semantics). Wren.adoc defines reload as
discarding this snapshot's unapplied changes and copying current running
settings. wren_bind_menu.c:3008 instead calls st_settings_reload, which loads
from the configured file under a file lock, before copying the global state.
This unnecessarily requires a disk store and can replace accepted in-memory
settings with persisted values. The latter data-loss scenario remains a
source-level consequence, not a runtime-tested case.

The `settings-reload` standalone test creates a valid snapshot without loading
a settings file, edits confirmClose, then calls reload to restore the running
value. It aborts with `settings reload failed` instead of succeeding; native
strict Clang test exits 1. No disk-store or GUI access. Recommendation: keep
snapshot refresh distinct from disk reload, following the existing public
contract, and test applied-but-unsaved running values plus external file edits.

### A066 — Wren VM target does not declare its math-library dependency

Severity: low (component build dependency). wren_vm/CMakeLists.txt builds
st_wren_vm without linking any math library. The VM references trunc, acos,
sin, pow and other math functions. The complete host currently links through
other libraries which supply -lm, concealing the missing dependency.

Confirmed on native FreeBSD/Clang with
wren_vm/tests/st_wren_vm_link_test.c: compile strict C17 with the public
include directory and build/wren_vm/libst_wren_vm.a alone. Link fails with
undefined math symbols. The identical command with -lm links and executes
successfully, creating a VM, interpreting a sqrt assertion and freeing it.
No CMake changes or new CTest registrations were made. This is not a failure
of the current full application link. Platforms with math in the default C
runtime were not tested by this isolated consumer.

Recommendation: express the target's required math dependency using the
project's platform capability policy, and cover a minimal consuming target
without unrelated libraries accidentally satisfying it.

### A067 — Painter truncation changes the style of an undrawn destination cell

Severity: medium (visible drawing corruption). scripts/ui_draw.wren:74 applies
the style to the destination Cell view before checking whether a wide glyph
fits maxW or the Surface boundary. A rejected glyph leaves the old character
with the new style. The uncapped text form similarly styles before its right
edge check, and hline before its remaining-length check; those variants have
not yet been reproduced independently.

Confirmed with `painter-truncate` in st_wren_loader_audit_test.c. The test
registers a tiny static font with a known two-column U+4E00 mapping before
creating the headless host, sets the destination to Z with attribute 7, then
calls Painter.text with a contrasting style and maxW=1. It first asserts the
glyph is actually two columns. The character is not drawn, but the destination
style changes. Native strict Clang test exits 1 with `clipped glyph changed
destination style`. The initial test attempt without the fixture font stopped
at its width precondition; that attempt did not demonstrate the defect. The
final fixture does not depend on optional Unifont being enabled.

Recommendation: determine fit before mutating destination state, and test full
cell preservation on rejected glyphs, including font, colours and pair flags.
This is a rendering-contract issue, not an out-of-bounds write demonstration.

### A068 — Documented live CustomCursor properties are not bound

Severity: medium (public API omission; runtime fiber abort). The syncterm
module declares static getters/setters for startLine, endLine, range, blink
and visible. Wren.adoc's CustomCursor section explicitly documents both live
static access and staged instance access. wren_bind_core.c's binding table
provides the instance properties and static presetLegacy_ only; the ten static
property signatures have no implementation and reach the unavailable-capability
fallback. This is unrelated to backend capability detection.

Confirmed by running the unchanged UiWidgetTest suite in a native headless
menu host through the new `widget-suite` mode of st_wren_loader_audit_test.c.
The suite aborts at ui_widget_test.wren:772, reading CustomCursor.visible in
testAppCursorStateInvalidation_. The test executable exits 1 with `this
capability is not available in the menu host`. Static visible was executed;
the other nine omissions are established by binding-table inspection, not
independent executions. Existing host tests use staged snapshots and thus do
not cover this live-property path.

Recommendation: cover and implement the already documented live cursor API
with the required atomic per-field behavior, retaining staged multi-field
updates. No production change made during the audit.

### A069 — Cell legacy attributes are reconstructed from RGB colour bits

Severity: medium (legacy API regression and theme-state corruption).
wren_bind_core.c:1793 derives legacyAttr from foreground & 15 and background
& 7, regardless of whether those fields contain indexed or RGB colours.
cell_rgb_set replaces the colour with tagged RGB, so it also silently changes
the apparent legacy attribute. Legacy wren_bind_screen.c:2117 reads an
independent vmem_cell.legacy_attr; its RGB setters modify only fg/bg.

Confirmed by `cell-colours` in st_wren_loader_audit_test.c: set legacyAttr to
0x70, fgRgb to zero, bgRgb to 0xaaaaaa; legacyAttr reads back as 0x20. Native
strict Clang headless test fails with the observed decimal value 32. This
does not require theme settings or Unifont. Painter applies legacyAttr and
then RGB fields, so its four-field Style cannot round-trip through Cell as
legacy did. This explains the three list-suite attribute failures: the default
selected style's 0x70 becomes 0x20, and the default 0x1f becomes 0x0f after
its RGB fields are assigned. The theme's default role values themselves match
the test expectations. Do not characterize these as fixture initialization
failures or infer actual backend pixels solely from attribute readback.

Recommendation: reconcile the Cell/Style compatibility contract with the new
cell representation without treating packed RGB components as palette indices.
Test simultaneous legacy/RGB state, bright/blink and font selectors, and
limited-colour backend fallback. Choosing an API/storage remedy is remediation
and is outside this audit.

### A070 — Changing a Container theme does not repaint inheriting children

Severity: medium (stale visual state; inherited defect). Widget.theme= in
scripts/ui_widget.wren marks only itself and ancestors dirty. Its clean child
still inherits the newly assigned Theme through effectiveTheme, but draw()
reuses that child's old surface unless its activity state changes. Container
composition therefore redraws the parent using stale child colours. App.theme=
has separate subtree invalidation and does not establish correctness for this
per-widget theme setter. The widget source matches legacy byte-for-byte.

Confirmed with `inherited-theme` in st_wren_loader_audit_test.c: create a
Container and a painted child, paint with inherited RGB 0x112233, replace the
parent Theme with RGB 0x445566, and draw the parent again. The child retains
the first colour. Native strict Clang headless test exits 1 after confirming
the initial paint. It reads RGB directly, so A069's legacyAttr issue does not
explain the result. No App event loop, disk writes or GUI needed.

Recommendation: invalidate inherited-style consumers when a widget's theme
changes, with tests for overridden descendants and parent reattachment as
well as ordinary inheritance. No implementation change made.

### A071 — A newly constructed empty RadioGroup reports cursor zero

Severity: low (public state contract; inherited). ui_radio.wren initializes
_items to an empty list and _selected to -1, but _cursor to 0. Its cursor
getter therefore returns 0 rather than null. The manual states both pointers
are null for an empty group; assigning items=[] does set _cursor to -1, so
empty state depends on whether the setter was called. Legacy source is
identical.

Extended the existing defaults assertion in ui_radio_test.wren to check cursor
as well as selection. Native headless `radio-suite` returns 16 passes and one
failure at that assertion. Other assertions were unchanged. Recommendation:
make constructor and empty-items assignment obey the same documented state,
and test cursorPos/first navigation from empty and explicitly cleared states.
No production fix made.

### A072 — Popup wrapping counts UTF-8 bytes as screen columns

Severity: medium (inherited visible layout defect). scripts/ui_popup.wren:265
wrapInto_ compares byte offsets with a display-cell width and advances a hard
break by that many bytes. This is not safe even for Unicode characters that
have CP437 equivalents. Native headless popup-suite demonstrates that wrapping
two U+00E9 characters at width one produces four rows [é, empty, é, empty],
instead of two; two U+2500 box-drawing characters at width two produce three
rows [─, ─, empty], instead of one. Wren range slicing decodes codepoints at
selected byte positions and skips continuation positions; the reproduced
effect is spurious wrapping/empty rows, not a claim of invalid UTF-8 output.

This routine controls Popup sizing, msgRows and message painting, and is also
used by ProgressText. The complete popup script is byte-identical to legacy:
this is an inherited defect exposed by the audit, not a new-port regression.
Recommendation: wrap using display widths under the same font-resolution
rules as drawing, preserving UTF-8 boundaries and hard-break/space semantics.
Test CP437-representable multibyte characters as well as wide glyphs and font
overrides. No production fix made.

Reproduction: strict-C17 standalone st_wren_loader_audit_test, fresh /tmp
fixture root, popup-suite mode. Expanded ui_popup_test.wren returns 44 passes
and the two failures above. Existing fake App initially aborted because it
lacked modalTop and returned a null effectiveTheme. Updated that test fixture
to provide its actual stack top and Theme.default; no assertions removed or
weakened. The remaining 43 existing assertions and the added ASCII CR/LF/CRLF
wrapping check pass. Tests do not exercise a real modal event loop or desktop.

### A073 — Changing TextInput's display mask leaves its viewport stale

Severity: medium (cursor/display correctness). scripts/ui_input.wren:120
mask= changes the displayed glyph and marks the widget dirty, but does not
call ensureVisible_. Width-dependent scrolling introduced in the modernized
widget therefore continues using the old mask/value geometry. cursorPos
clamps an out-of-view insertion column to the final field cell, hiding the
inconsistency rather than displaying the insertion point.

Reproduction: a four-cell field containing AAA initially fits with its cursor
after the text. Assign U+4E00 as a two-column mask. The unchanged scrollOff=0
paints two wide masks and clamps the cursor to column four, inside the second
mask; expected scrollOff=2 shows the last mask and an insertion cursor after
it at column three. Added testMaskWidthUpdatesViewport_ to ui_input_test.wren;
it fails under native Clang in input-suite mode. The runner explicitly
registers a tiny font containing a two-column U+4E00 glyph, so this result
does not depend on optional Unifont availability. The original wide-character
scrolling test passes with the same fixture.

Recommendation: recalculate the viewport whenever displayed widths change,
including mask changes; verify theme/font/focus changes separately. Only the
mask case is reproduced. Legacy used one column per character, so its same
mask setter did not encounter this width transition. No implementation change.

### A074 — Form Tab order depends on callback/field creation order

Severity: low (confirmed API-contract mismatch; inherited). Wren.adoc's Form
section promises fields in declaration order, then OK, then Cancel.
scripts/ui_form.wren addFieldH and ensureButtons_ append to Container.children
in call order; layout_ changes geometry but does not change traversal order.
Consequently assigning onCancel before onSubmit gives field/Cancel/OK, and
adding a second field after onSubmit gives first-field/OK/second-field despite
the second field being laid out above the buttons. The relevant legacy code
is identical; only Unicode label-width calculations changed in this port.

Added two tests to ui_form_test.wren that send actual Key.tab events through
Form.handle and inspect focusedChild identity. Both fail in the strict native
Clang headless form-suite. All twelve original checks and a new callback-setter
idempotence/removal check pass (13 passes, 2 failures total). No real input,
clipboard, desktop or modal loop is involved.

Recommendation: make traversal match the declared logical field/button order
regardless of callback assignment order or subsequent field additions, or
explicitly reconsider the API contract. Test button removal/recreation and
clearFields/repopulation as well as initial construction. No production edits.

### A075 — MenuBar invokes resident commands twice per mouse click

Severity: medium (duplicate command execution). ui_menubar.wren handleMouse_
activates on both button1Press and button1Click. wren_bind_core.c:583
button_event translates left-button down to 1 (Press) and up to 3 (Click),
as defined in syncterm.wren. App.dispatchMouse_ forwards both to the hit
widget without coalescing. A command that leaves the bar resident therefore
runs twice for one ordinary down/up gesture. A command opening a modal may
route the second event elsewhere instead; this test does not claim every
command executes twice.

Added testPressAndClickActivateOnce_ to ui_menubar_test.wren, delivering the
host's two-event sequence and counting invocations. Native Clang isolated
headless menubar-suite reports 17 passes and this one failure. Host translation
and App routing were source-traced; no physical mouse/backend run claimed.
Both-event activation is inherited from the legacy MenuBar script; its only
modern differences are Unicode measurement and removal of byte truncation.
Recommendation: define a single activation edge per gesture and verify modal
opening, release-outside, and drag cancellation behavior across the host and
widgets. No implementation change.

SpinBox followup to A075: host button_event's press/click pair also triggers
both branches of SpinBox.handleMouse_. Added testMouseGestureSingleStep_:
value 5 becomes 7, with two onChange calls, instead of one step to 6 and one
notification. Native headless spinbox-suite returns 21 passes and one failure.
This confirms the previously open paired-event question; its implementation
is byte-identical to legacy. Include SpinBox in A075's affected widgets and
single-gesture regression coverage. No physical input or implementation edits.

### A076 — Help scrollbar-separator changes do not invalidate wrapped layout

Severity: low (inherited stale layout). scripts/ui_help.wren's
scrollbarSeparator= setter marks drawing dirty but leaves _layoutW valid.
ensureLayout_ returns early when bounds width/height are unchanged, even
though separator visibility changes available text width by one column.
Painting uses the new viewportCols_ with the old wrapped lines and scroll
range. The cache key lacks a layout-affecting option.

Added testSeparatorReflows_ compares an already laid-out Help whose separator
is disabled with a freshly constructed Help having the same final properties.
At 14x6, body "aaaa bbbbb aaaa bbbbb aaaa bbbbb", End produces different
scrollTop values. Strict native Clang headless help-suite reports all 23
existing checks passing and this regression failing. No GUI or production
edits. The relevant implementation is unchanged from legacy; only run-text
painting width changed in the modernized version.

Recommendation: include separator state in the layout cache key or invalidate
it when changing that option, then clamp scroll position against recomputed
lines. Test both directions and at top/end; custom-font layout consistency
remains a separate question.

### A077 — Markdown parses bold delimiters inside inline code

Severity: low (inherited help-text corruption). ui_markdown.wren parseInline_
handles ** before checking whether the active role is MdRole.code. Parsing
`**literal**` enclosed in backticks loses the literal asterisks and changes
the run's role to bold. This violates the advertised inline-code semantics
of the documented Markdown subset. The implementation uses one role variable
rather than preserving code-span literal content independently of other markup.

Added testCodeSpanKeepsLiteralMarkup_ in ui_help_test.wren expects one code
run containing **literal**. It fails in the native headless help-suite.
Recommendation: keep code-span contents literal and separately test escaping,
unmatched delimiters, and bold surrounding code; do not infer support for all
CommonMark constructs beyond the documented subset. Relevant parsing code is
unchanged from legacy. No implementation edits.

### A078 — Markdown bullet hard breaks lose continuation indentation

Severity: low (inherited documented-format mismatch). Wren.adoc explicitly
supports trailing-double-space hard breaks inside bullets with hanging-indent
continuations. ui_markdown.wren parse consumes each bullet as a single source
line; only the paragraph branch joins following lines and converts trailing
spaces to a hard-break token. Consequently "- first  \nsecond" is parsed as
a bullet followed by an independent paragraph, and second is drawn without
the required two-cell indent.

Added testBulletHardBreakKeepsIndent_: bullet continuation assertion fails;
the same hard break in an ordinary paragraph passes. Strict native Clang
headless help-suite now reports 24 passes / 3 failures (A076, A077, A078).
Recommendation: preserve bullet continuation ownership during block parsing
and test both forced and soft wraps with inline roles. Relevant code is
unchanged from legacy. No desktop interaction or production edits.

### A079 — Color bindings substitute palette indexes for their RGB contract

Severity: high (confirmed color-picker rendering abort; API regression).
wren_host/wren_bind_core.c:2032 fn_color_to_attr validates both arguments as
0..15 and packs them as nibbles. Wren.adoc:2417 instead specifies nearest
legacy-attribute conversion from 24-bit RGB. ColorRamp/ColorSwatch and
menu_palette_picker pass RGB, so ordinary rendering fails with "integer is
out of range". Legacy wren_bind_screen.c:1813 sets the RGB flag and calls
ciolib_rgb_to_legacyattr; this conversion was not preserved.

Native strict-Clang headless color-suite, loading the unchanged
ui_color_picker_test.wren, aborts at testRampStyleRoles_ during ColorRamp.draw.
The stack reaches ui_color_picker onPaint_; source tracing identifies the
RGB Color.toLegacyAttr call. The earlier pure-color/keyboard/mouse tests run
before this abort but the suite never returns aggregate counts, and later
tests must not be described as passed. No physical display, clipboard or
personal settings involved; the failure is retained.

The reverse binding fn_color_from_attr at :2017 returns the foreground nibble
and three-bit background index, not the documented pair of 24-bit RGB values
(Wren.adoc:2414). This is source-confirmed and differs from legacy's
ciolib_attr2palette mapping; palette-dependent behavior needs dedicated tests.
Recommendation: implement the documented RGB/index distinction and test both
directions independently against known colors and active palette semantics.
A round trip alone could conceal mutually wrong implementations. This is
separate from A069's Cell legacyAttr storage/readback defect. No fix made.

### A080 — Valid hex edits reset the color-picker cursor to the end

Severity: low (inherited editing regression relative to TextInput behavior).
ui_color_picker.wren hexChanged_ calls setRgb_ on every complete valid hex
edit. syncChildren_ then assigns _hex.value, which resets TextInput's cursor
to the end even when its text already matches the edited value. Subsequent
overwrite keystrokes no longer edit the adjacent digit as intended.

Added testHexOverwriteCursor_: start with 102030, focus the hex field, select
overwrite mode, Home, then type A. The canonical color correctly becomes
A02030, but cursor becomes 6 instead of 1. Native Clang isolated color-audit
reproduces this without any drawing, so A079 does not cause it. Test restores
the shared insert mode. The script is identical to legacy. Recommendation:
avoid resetting the active editor when propagating its own accepted edit;
preserve caret/selection while still updating dependent controls. No fix made.

ColorPicker verification followup: added runAudit() to the test module. It
invokes each of the original 19 test methods in its own Fiber.try, counts an
abort as a failure, and continues; original run() remains unchanged. Added
independent RGB-to-attribute and attribute-to-RGB contract tests plus the
overwrite regression. Strict native Clang color-audit returns 14 passes and
8 failures: five RGB-conversion aborts (four original drawing tests and one
direct A079 assertion), original hex-style legacyAttr mismatch (A069), direct
fromAttr assertion (A079), and caret reset (A080). All original tests have now
been attempted; none were omitted or weakened. The fromAttr failure promotes
that part of A079 from source-only to runtime-confirmed under the fixture's
default palette. No physical backend or palette-animation verification claimed.

### A081 — LogView scrollbar counts entries instead of wrapped rows

Severity: low (inherited navigation defect). scripts/ui_logview.wren:291
clamps mouse scrolling against _ring.count, and :337 passes that same count
to Painter.scrollbarClick. Rendering and keyboard scrolling instead use
visualRowCount_. A single long entry can therefore display a scrollbar which
cannot be operated with its arrow/track because the mouse path believes the
entire buffer fits in the viewport.

Added testWrappedScrollbarClick_ to scripts/ui_logview_test.wren: a 7x3
viewport holds one 20-character ASCII entry, wrapping into four rows in the
five-column content area. Home anchors at zero. Pressing the down arrow
should advance to the natural tail (scrollTop null), but leaves zero.
Confirmed with the native headless logview-suite runner. The production
module is identical to legacy, so this is not a modernization regression.
Recommendation: use the same visual-row coordinate system for scrollbar
geometry, hit testing and clamping. No implementation changes made.

### A082 — LogView capacity reduction loses its surviving scroll anchor

Severity: low (inherited reading-position defect). scripts/ui_logview.wren:70
removes old entries without subtracting their wrapped row counts from an
active anchor. It then compares that visual-row anchor with a logical entry
count. The append-overflow path already attempts the necessary subtraction;
the capacity setter does not.

Added testCapacityShrinkAnchor_: ten single-row entries in a two-row viewport,
anchor at entry four, then reduce capacity to eight. Entry four survives and
should become offset two; actual offset remains four, displaying entry six.
Confirmed in native headless logview-suite. Recommendation: retain the
surviving visual anchor when evicting entries and clamp using visual rows.
Wrapped-entry and viewport-resize interactions require additional tests.

## Component notes

### A086 — New-entry address parsing aborts on a trailing colon

Severity: low (confirmed user-input error handling defect, inherited).
scripts/auto/menu/main_menu.wren:1005 searches for a second colon starting
at colon+1 before checking that this offset is inside the string. A trailing
colon makes indexOf(_, start) abort with "Start out of bounds." rather than
returning the unsplit address/null port as the subsequent guard intends.
Legacy main_menu.wren contains the same ordering.

Added scripts/main_menu_test.wren with nine address-helper cases against
MainMenuApp in an empty isolated headless host. Native strict Clang result:
8 pass, 1 fail, exclusively bbs.example:. Tests cover port limits, empty port,
IPv6 preservation and a multibyte hostname. Each helper call runs in Fiber.try
so the failure is counted and later cases still run. Recommendation: check
the suffix boundary before searching it, and test new-entry recovery. The
App catches handler errors; this evidence does not establish a program crash.
add_ creates a working entry before parsing, so cleanup of that draft after
this abort needs a separate integration test. No lists were loaded or saved.

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

Cross-component risks: settingsAction_ calls MainMenu.prepare after changes;
a failed Menu.load can invalidate previously retained entry handles (A061),
so leaving the old visual list is not a complete recovery. Edit navigation
can reach system entries without returning through edit_'s copy prompt;
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

### A087 — Hook error reporting can throw while handling a non-string abort

Severity: medium (confirmed error-containment defect; inherited from legacy).
scripts/syncterm.wren:1482 concatenates `f.error` directly onto a String.
Wren permits non-string abort values. `Hook.dispatch_(Fn.new {
Fiber.abort(42) })` therefore throws `Right operand must be a string` from
finishDispatch_, rather than returning null as the documented hook contract
requires. The legacy script has the same expression at line 1489.

Reproduced in a fresh isolated native headless menu VM with the `core-helpers`
mode of wren_host/tests/st_wren_loader_audit_test.c. The helper was invoked
directly, not through a live connected dispatcher: this proves the wrapper
failure, not a whole-program crash. Recommendation: safely format arbitrary
error values without allowing diagnostic formatting to defeat containment;
test numeric, null, foreign and throwing-toString error objects, as well as
ordinary string errors. No production change made.

### Common Wren module — full source coverage

Completed all 2159 lines of scripts/syncterm.wren, including declaration
groups, error classes, screen preservation helpers, input claim/wake contract,
REPL classification, Surface sequence views, constants, capability APIs,
hook dispatch, and WON serialization. This completes the current scripts/
source inventory; older chronological lists of outstanding scripts below are
superseded. Source coverage is not equivalent to connected-host integration
or runtime verification of every foreign declaration.

Fresh `core-helpers` execution compiled with native C17 strict warnings:
serialization of a list containing Num/Bool/null/UTF-8 passes; calling
WON.deserialize on `[1,true,null]` aborts with `this capability is not
available in the menu host`; non-string hook abort containment fails A087.
No WON deserializer binding exists in current wren_host C sources, so the
manual's general-purpose serialization/parser pairing is incomplete here.
This is an API integration gap, not a missing network or display capability.

Source-level contract concern: Range keys are hashable and documented as
supported Map keys, but writeMap_ feeds them to write_, which converts Ranges
to Lists. Serialized Map output then contains List keys, rejected by the
documented parser contract. A runtime parser round trip cannot be established
in the current host because its deserializer is unavailable. Legacy shares
this conversion; do not label it a new modernization regression.

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

Added runHelpers() to the test module, retaining run() and every assertion.
The isolated auxiliary-scripts runner uses only the five non-persistence
checks: native strict Clang result 4 pass, 1 fail. Movement commit, movement
cancel, deletion and EditorPane clear pass. Discovery fails because
wren_host/wren_bind_menu.c:3484 binds Menu.scriptModules to fn_menu_empty_list.
This is a confirmed medium incomplete-integration gap: the shipped script
picker cannot offer modules and labels configured modules missing. It is not
merely caused by the empty test directory; the binding always returns empty.
Recommendation: implement the documented filtered module catalog and test
disk/embedded sources and reserved VM exclusions before claiming discovery.
No BBS list paths were configured or accessed. Original defaults/persistence
tests were read but not executed by this helper-only mode; their coverage
must not be inferred from the four passing checks.

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
supported. Existing MenuBar double-activation and ColorPicker failures
(A075/A079) propagate into this editor. Repeated refresh reconstructs theme
snapshots and can refresh content twice via model.onChange plus the explicit
call; no performance measurement is claimed.

Documentation mismatch: Wren.adoc:4542 still describes five-field glyph rows
and addGlyph(name, cp437, ascii), while the current editor consumes codepoint
as a sixth field and calls addGlyph(name, ascii). This needs reconciliation
with the reviewed binding, not removal of Unicode functionality. This audit
does not alter the existing manual. Wren guidance used; only audit harness
and report changed, no commits. Theme editor and its test are now removed
from the remaining source-review backlog; broader runtime gaps remain.

### A085 — Wren console helper paths corrupt multibyte text

Severity: medium (confirmed interactive text correctness defects, inherited).
scripts/wren_console.wren mixes codepoint counts with byte-based String slice
indices in killWord_ and runCommand_. quoteString_ iterates UTF-8 bytes but
re-encodes each byte using String.fromCodePoint, producing mojibake instead
of retaining the original text. These paths are unchanged from legacy.

Added scripts/wren_console_test.wren and included it in the isolated
auxiliary-scripts runner. Native strict Clang run: two ASCII controls pass;
three Unicode assertions fail. quoteString_("é") should return quoted é;
killWord_("é x") should retain "é "; a registered /audit-args command invoked
with "éxy" should receive the entire argument. All three fail through the
production embedded Wren module. No interactive input, arbitrary REPL eval,
user file, or network operation is used. The test unregisters its command.

Recommendation: consistently distinguish byte offsets, codepoints and display
columns; preserve UTF-8 sequences when quoting and editing; use byte lengths
for byte slices. Add actual cursor/backspace/delete event tests as well:
the loop currently moves/deletes one byte even though it accepts Unicode
text events, and drawPrompt_ uses that byte offset as a screen column. Those
additional paths are source concerns, not exercised by these helper tests.

### Wren console — source coverage

Read all 788 lines of wren_console.wren and its full legacy diff. Reviewed
command registration, history, REPL error isolation, quoting, clipboard input,
prompt positioning, log reconstruction and screen/mouse restoration. Local
changes add default colours and backend-appropriate Ctrl+Q/Ctrl+S exits;
other reviewed paths are inherited. Consulted the String indexing contract
and console editing documentation. Wren guidance used.

Further source questions: clipboard insertion accepts ASCII only although
keyboard text accepts Unicode; /in uses the same count/slice mismatch as
registered commands. renderAllRows_ splits explicit newlines but does not
wrap to screen width despite claiming display-row units; long lines can make
scrollback paging and single-row moveRect disagree. drawPrompt_ has no
horizontal viewport or long-input bounds policy. The main runBody_ is guarded
by Fiber.try, but setup and restoration failures lie outside that guard.
User-defined toString failures occur after the eval fiber returns. These need
targeted event/failure tests before stronger runtime claims.

Updated remaining script coverage: wren_console.wren is now source-reviewed;
main_menu, full BBS/theme editors and their tests, syncterm.wren remainder,
and wrentest.wren remainder are still outstanding. Auxiliary run continues
to fail for the separately documented pixel binding and demo dependency
gaps; those failures are not attributed to A085. Only tests/report changed.

### A084 — Failed SFTP navigation retains rows under a different directory

Severity: medium (source-confirmed wrong-path behavior in currently
unintegrated scripts; no live remote operation reproduced).
scripts/sftp_app.wren:598/600 changes _bCwd before refreshBrowser_. If
loadDir returns SFTPError, :627 onward updates the title to the new cwd,
shows an alert, and leaves a nonempty _bList unchanged. Subsequent file
selection at :613 and description lookup at :672 concatenate the new cwd
with the old row name. BrowserRow.remote still describes the original row,
so chip/status and action can even disagree about which remote file it is.

Concrete source trace: /old lists directory blocked and file report.txt;
opening blocked sets cwd=/old/blocked, but a listing failure retains the
/old rows. Selecting report.txt now queues /old/blocked/report.txt rather
than /old/report.txt. No malicious input is required; an ordinary permission
or transient listing error suffices. The script is identical to legacy Wren.
Recommendation: commit cwd and rows together after successful navigation,
or disable/clear stale rows on failure; bind actions to the displayed row's
identity. Add a fake-directory-error test before implementing a remedy.

### SFTP UI — source coverage and remaining script inventory

Read all 815 lines of sftp_app.wren and compared with legacy (identical).
Reviewed path/date/size helpers, browser row snapshots, mode caching, queue
selection preservation, description loading, picker handoff, modal lifetime,
shell-close behavior and tick-driven updates. Consulted the documented
runChild single-resume-source contract and its implementation. SFTP requests
are correctly issued from child fibers rather than directly awaiting results
on the input/timer-driven App fiber. That alone does not ensure navigation
ordering: runChild pumps further events while loading, so nested navigation
and late-result application remain unresolved without controlled scheduling
tests. Constructor-time browser loading also precedes App.run registration
and needs an integration test in the eventual connected host.

Performance concern: every queue generation change refreshes every browser
row; statusChip can enumerate Download.list separately for each local match
and compute a full-file hash. Queue generation advances per completed chunk,
with UI polling at 100ms. This is potentially repeated directory enumeration
and file hashing on the foreground thread, not merely repainting status
glyphs. No timing benchmark or claim about observed UI stalls is made.

Ownership: QueueRow holds detached JobSnap values. Selection/cancellation use
(direction, remote) identity, inheriting duplicate-job ambiguity noted in the
queue review. Picker focus restoration and __current restoration occur on
normal return only; abort cleanup requires integration verification. The
comment claiming resume from done is stale: claimNext_ resets done to zero.
Shell suspension is a request, not proof all in-flight workers have stopped.
No connected VM, network, transfer, production edit or commit was used.

Explicit remaining script source coverage after this pass:
- auto/menu/main_menu.wren — full pass outstanding.
- menu_bbs_editor.wren and menu_bbs_editor_test.wren — earlier selected paths
  reviewed, full pass outstanding.
- menu_theme_editor.wren and menu_theme_editor_test.wren — full pass outstanding.
- syncterm.wren — bindings/classes reviewed selectively, remainder outstanding.
- wren_console.wren — full script pass outstanding despite native REPL review.
- wrentest.wren — selected tests read, remainder outstanding.

All other files currently listed by rg --files scripts have source coverage
recorded in component notes (this is not a claim all tests pass or all GUI/
connected behavior has been exercised). Wren guidance used for this pass;
only the audit report changed. Broader inventory rows remain active.

### A083 — SFTP queue loses unfilled portions of short-read chunks

Severity: medium (source-confirmed transfer correctness defect in currently
unintegrated scripts; no live transfer reproduced). scripts/sftp_queue.wren
JobCtx.claimChunk reserves the whole requested range by advancing _nextOff.
downloadChunk_ at :709 accepts any nonempty SFTP.read result, writes it once
at the reserved offset and claims another range; it never requests the
unfilled suffix. Wren.adoc:5965 explicitly permits reading *up to* count
bytes. With reserved ranges [0,30720) and [30720,61440), a first result of
100 bytes leaves [100,30720) unwritten even if later reads succeed. No error
is recorded. runDownload_ then sets total=done and status=DONE (:692), hiding
the size discrepancy rather than checking full byte coverage.

The upload loop (:819) makes the equivalent assumption about local readBytes,
also documented as an up-to read. EOF/empty results before all reserved bytes
arrive merely break the worker loop; the dispatcher can still mark DONE.
The script is identical to the legacy Wren queue, so this is inherited, not
a proven regression from the old C queue. Recommendation: complete each
reserved interval or explicitly fail an unexpected short/EOF result; verify
full coverage before success. Test short reads at interior offsets, premature
EOF and out-of-order completion using deterministic fake endpoints. Those
worker-level reproduction tests remain outstanding; no network used here.

### SFTP queue — source coverage

Read all 950 lines of scripts/sftp_queue.wren, compared against legacy Wren
(identical), and consulted local File/SFTP read/write/close contracts. Reviewed
Job/JobSnap ownership, JobCtx reservations/wakeup, adaptive sizing, enqueue/
cancel, both transfer loops, persistence and reload. This does not cover the
815-line sftp_app.wren or establish a working connected host: queue module and
required SFTP bindings are absent from the current embedded host integration.
All findings here describe supplied script behavior when that integration is
provided, not an observed current menu transfer failure.

Further source-confirmed error-handling gaps: runDownload_ ignores an initial
SFTP.stat error when total<=0, allowing zero claimed bytes followed by DONE;
positive enqueue sizes are trusted despite the enqueue comment promising a
restat, so remote growth since selection is not fetched. Both dispatchers
discard SFTP.close errors (:680/:791), although the documented result permits
SFTPError; an upload close failure can therefore still be reported as DONE.
Treat these as medium completion-contract concerns requiring fake-endpoint
tests, not as demonstrated server failures.

Ownership/concurrency questions: cooperative non-yielding claimChunk makes
offset reservation serial, and bumpAlive precedes every worker launch.
However, workerExit is only on normal loop exit; an aborting local-file API
can strand the dispatcher's alive count and skip endpoint cleanup. stop()
clears __running but claimChunk checks cancel/suspended, not running; safety
therefore depends on disconnect retiring the VM and its pending callbacks.
Same-VM stop/start with pending workers is not shown safe. Snapshot copies
scalar/string job state rather than exposing live Jobs; enqueue intentionally
returns a mutable Job. Duplicate direction/remote pairs make status/cancel
select the first match, which may be terminal; intended identity semantics
need clarification.

Persistence deletes the old file before creating/writing the replacement,
has no null check after Cache.create, and lacks exception cleanup; an I/O
failure can lose the previous saved queue and abort a transition. load_ guards
WON parse errors but not file I/O, accepts unknown status strings and arbitrary
numeric totals; unknown statuses become jobs no worker claims. These are
source concerns for followup, not newly executed filesystem failures. No
production edits, transfers, personal files or commits; Wren guidance used.

### Auxiliary UI scripts — coverage and integration gaps

Read complete auto/menu/menu_host_ui.wren (80 lines), menu_host_ui_test.wren
(33), ui.wren (30), pixel_vm_test.wren (36), and ui_demo.wren (627).
Consulted the host-controller ABI/status restoration contract and pixel API
documentation. MenuHostUI delegates interactive dialogs to independently
pumped synchronous Apps; its progress pane deliberately draws without running
an input loop, leaving restoration to its C owner. Its four existing width
checks pass in the native headless auxiliary-scripts audit mode.

Confirmed incomplete integration, not a physical-backend limitation:
pixel_vm_test aborts at line 18, PixelColor.rgb(0x123456), before allocating a
buffer or running any of its six checks. wren_host.c:378 routes unbound methods
to st_wren_unsupported; wren_bind_core.c has no PixelColor registrations.
The API is declared in syncterm.wren and documented in Wren.adoc:2434 onward.
This is a medium API availability gap: ordinary colour-word construction
should not depend on a graphical backend. Recommendation: explicitly scope
and complete the promised pixel bindings, with menu/picker parity tests;
do not misrepresent an unimplemented binding as host hardware incapability.
No buffer/raster correctness claim follows from this early abort.

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

The auxiliary-scripts audit test reports status-width 4/4 pass, pixel suite
runtime failure and demo import failure, returning failure overall. It does
not invoke the interactive gallery or start a transfer. Strict native Clang
test compilation succeeds. ui.wren is a re-export module, with no new state;
it does not currently aggregate LogView/Progress classes despite its broad
"every public class" comment (documentation/layout concern, not a runtime
failure). Wren guidance used; only test harness and audit report changed.
These integration gaps remain distinct from numbered rendering findings.

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

Log/scrollback script coverage: read all ui_logview.wren (379 lines), its
original 275-line test suite, and menu_scrollback.wren (214 lines). Compared
both implementations with legacy: LogView is unchanged; offline scrollback
only adds reusable Cell storage and checked cellAt output calls. Consulted
Wren.adoc's detached offline Surface and screen-save/restore contracts.
The viewer copies source rectangles, bounds its source coordinate lookup,
and resets hyperlink hover after normal modal completion. Exception cleanup
of screen/mouse state remains a source-level question; completed-session
integration is not established by this review or the currently stubbed host.

Extended the isolated audit runner with logview-suite and added two focused
tests. Strict native Clang build succeeds; suite returns 18 passes, 3 failures:
the original severity-attribute check (consistent with A069), plus A081 and
A082. Existing assertions remain unchanged. Existing wren_host CTest passes
1/1; git diff --check passes. No production changes, personal data access,
GUI actions, commits, or legacy execution. Wren guidance used throughout.
LogView's codepoint-count wrapping also differs from display-cell width for
wide glyphs, and capacity validation is absent; these remain source concerns
rather than additional runtime-confirmed findings in this pass.

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
Negative field signs are preserved by sameOrder_; integer overflow concerns
in native sorting remain A033, not resolved by these Wren helper tests.
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
behavior. Web-list names use ASCII comparison, but path-component validity is
the host's responsibility (see A034). Screen-mode geometry concerns and broad
settings-save/reload semantics remain those recorded in the binding audit,
not resolved by reading the menu. No new production changes or commits.

Settings-menu partial pass: read menu_settings_ui.wren lines 652..935 and
938..1274 (font help/management/details, program/classic-color/custom-mode
settings, audio, file-location/build displays, web lists and encryption).
Full script comparison with legacy has no differences. Earlier sections
1..651 and the small 936..937 boundary still need complete reading; this is
not full module coverage. Also read complete menu_ui_test.wren (51 lines),
whose two retained-view/height assertions have not yet been run independently.

Custom-font workflow has no live font preview to restore: edits target the
settings font model and save/reload happens on leaving Font Management.
The script re-fetches handles each iteration after structural edits; host
generation checks reject stale handles. Safe mode suppresses entry and the
font mutation bindings independently reject writes. Existing binding-review
concern is now traced through its caller: fn_font_set_file enforces exact
2048/3584/4096/10240-byte sizes, contradicting Wren.adoc's explicit no-size-
restriction contract, and font_ ignores its Bool result. Thus validation/open
failure has no menu error feedback. This is source-confirmed, not a new
runtime-tested finding or a reason to perform unsafe settings writes.

font_ releases focus before Host.pickFile and restores only after normal
return; exceptional picker restoration remains an integration question.
Font save failure displays an alert then returns from management with the
in-memory dirty state retained; no transactional-discard claim is made.
Program settings call applySettings_ after edits; failed apply calls reload,
so the previously recorded Settings.reload contract issue (A065) is relevant
to this caller. Broader cancellation/save behavior needs isolated model tests.
No font files, personal configuration or production code modified; report-only
this pass. Font model API manual and related host functions were rechecked.

MenuUi pass: read complete scripts/menu_ui.wren (696 lines) and full legacy
diff. This module is now included in script coverage. Modern changes are
selective Unicode width measurement; browser label widths still use .count.
ChoiceMessage delegates wrapping to Popup.wrap_, so A072 also affects its
message viewport. Main responsibilities are mapped choice values, callbacks
before dismissal, browser preview/cancel hooks, standalone synchronous dialog
ownership, and retained ListView state across repeated choices.

Extended ui_popup_test with browser acceptance, rejection, Escape and direct-
command cases using the existing fake App. All four assertions pass: mapped
row values are returned, rejection keeps the pane open, acceptance omits the
cancel hook, Escape calls it once, and a direct command calls cancellation
before returning its command/value. Entire popup-suite now 48 passes / 2
retained A072 failures. These are direct synchronous event tests, not a real
App loop or disk-backed menu session. Existing nested-choice callback test
also remains passing.

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

Added a separate runAudit() test entry which catches each original check's
abort as a failure without altering run() or assertions. Native strict-Clang
headless palette-suite: 9 pass, 2 abort with "integer is out of range" in
contrast/legacy-attribute drawing. This extends A079's reproduced impact to
PaletteContrastPreview. Navigation, focus propagation, compact bounds,
repeated short palettes, sample selection and pane commands pass. No real
modal loop, desktop, palette installation or persistent settings access.

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
Color bindings plus their legacy implementation. Both script modules now
included in source coverage. Added color-suite to isolated audit runner;
runtime verification is incomplete because the original suite aborts at
the first ramp-style draw (A079). Remaining tests still need an independent
entry point if they are to be exercised without changing production code.

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
This module is now included in script coverage. Modern differences add font-zero
display-width queries; parsing and wrap policy are inherited. Confirmed new
findings are A077/A078, not Unicode byte-splitting (inline parsing correctly
materializes codepoints before scanning).

Further source concerns: a single word longer than the viewport is emitted
unsplit and Help clips its suffix with no horizontal scrolling. Definition
terms are measured against a cap but their prefix is not clipped/wrapped;
at narrow widths the continuation indent can consume the complete viewport,
leaving descriptions invisible. Need explicit overflow-policy tests before
classifying these separately. Layout has no theme/font parameter and measures
font zero whereas Help paints per-role fonts. Parsing/coalescing repeatedly
concatenates strings and closing-marker searches rescan suffixes; worst-case
cost needs measurement, not an unsupported performance claim. CR handling is
explicitly LF-only in source; normalization responsibility needs documentation
review. No production/build/documentation changes or commits.

Help pass: read full ui_help.wren (350 lines), original ui_help_test.wren
(297 lines), matching manual section and Markdown-subset introduction, and
complete legacy diff. Both modules are now included in script coverage.
Existing 23 checks pass in isolated native headless host; new cache-invalidation
failure is A076. Tests exercise keyboard dismissal/scrolling, preformatted
hard lines, individual wheel/click/drag events and scrollbar drawing. FakeApp
is sufficient for these tests because painting occurs outside its parentage;
its null theme/missing modalTop would not support a general attached draw.
Markdown implementation and its own tests remain pending, not implicitly
covered by the Help wrapper review. No actual modal pumping or screen text
selection is established by directly injected events. The documentation says
any other key dismisses, but process-close Key.quit deliberately falls through
and its test passes. No existing documentation changed.

DatePicker pass: read complete ui_date_picker.wren (299 lines), original
test module (211 lines), and full matching manual section. Implementation is
byte-identical to legacy. These two modules are now included in script
coverage. Added date-suite to strict native Clang isolated host runner and
tests spanning all 146097 days of 1600..1999 against sequential civil dates,
both navigation limits, and negative UTC timestamps. Those tests pass.
The expanded suite reports 17 passes and one existing combined drawing
failure; diagnostics show title and weekdays pass, while neighbor and selected
legacy-attribute comparisons fail. Selected attribute reads 32 instead of 112,
the same RGB/legacy-field mismatch reproduced independently in A069; no new
calendar arithmetic defect is inferred from this drawing failure.
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
modules now included in coverage. Original suite 17/17 passes; added paired-
event failure is A075. Existing assertions exercise keyboard wrap/Home/End,
ASCII hotkeys, individual clicks/gaps, focus callbacks and basic drawing.
Remaining source concerns: mutable items can bypass layout-cache invalidation;
layout always measures font zero while painting uses role-specific fonts;
focus setter accepts fractional indices. These require contract/custom-font
tests before separate findings. SpinBox's previously unresolved paired-event
concern now has a confirmed host sequence to test against; it is not marked
runtime-confirmed solely from the MenuBar reproduction. No desktop interaction,
production changes or commits.

Form pass: read full ui_form.wren and ui_form_test.wren, the matching manual
section, and the complete legacy diff. These two modules are now included in
script coverage. Added form-suite to the isolated headless audit runner;
results and new failing Tab regressions are A074. Width calculations are the
only production differences from legacy. Layout measures labels using font
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
Strict native Clang headless suite with the explicit wide-font fixture returns
46 passes and two failures: selected-text legacyAttr (A069) and new wide-mask
viewport (A073). Before registering that font, the original wide-glyph test
also failed due to fixture capability; that failure is not a product finding.

The suite does not touch the desktop clipboard: right-click uses PasteProbe,
and paste-content checks call insertText_ directly. Native clipboard transfer
and callback-abort repaint behavior remain untested. Array cursor setters
accept fractions, and value=/maxLen= do not enforce an existing-value cap;
these need domain/contract clarification rather than automatic remediation.
The manual's claim of one cell per codepoint is stale relative to modern
width handling; its cursorAttr description also says focused style although
the code deliberately uses fillStyle_. Full grapheme editing is not claimed
or treated as required here. Display-width methods repeatedly resolve style
and cross the host boundary per character; no performance regression is
asserted without comparative measurement.

Popup pass: read all 731 implementation lines, all 558 original test lines,
the popup manual through its layout/standalone introduction, and compared the
complete implementation with legacy (identical). Script coverage includes
ui_popup.wren and ui_popup_test.wren; imports ui_input/ui_help/menu_ui are not
thereby marked reviewed. Tests cover fake-parent dismissal, prompt editing,
layout and MenuUi choice interactions; the two added Unicode wrap failures
are A072. Strict native Clang headless execution only.

Source questions requiring followup: preformatted= does not mark the cached
surface dirty or re-layout a popup changed after its first draw. PopStatus and
popup centering/truncation still use codepoint counts (Find uses byte count)
rather than font-dependent column widths. Very small consoles can force button
rows/input fields beyond their content bounds; tall ordinary Popup messages
are not explicitly clipped to the content rectangle before drawing. These
are not yet separate reproduced findings. Confirm's close-button path returns
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
application failure is asserted. Width measurement with a null style font
uses font zero, sharing the inherited-font question noted for other widgets.
Tests use normal finite values and the default font, not wide-glyph/custom-font
layouts. ProgressText wrapping delegates to Popup.wrap_, which still requires
the popup component review. Passing these tests does not establish that helper's
Unicode or narrow-width correctness.

RadioGroup/SpinBox pass: read complete implementations (199/161 lines), test
modules (197/206 original lines) and matching manual sections. Both production
scripts are byte-identical to legacy. The isolated strict-Clang runner reports
radio 16 passed / 1 failed (new A071 assertion), spinbox 21 passed / 0 failed.
No GUI or external input. The radio suite covers cursor-versus-selection,
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
The button style test merely checks unequal attributes, which still passes
despite A069; it does not validate exact attributes or RGB/legacy independence.
Scripts coverage includes these four modules; Unicode/custom-font layout,
hotkey routing and callback failure cases remain beyond their passing suites.

Read all 341 lines of ui_style_test.wren and ran it unchanged through the
isolated `style-suite` host mode: 35 passed, 0 failed. Covers Style composition,
equality, complete/null fields, dotted role inheritance, Theme.fromData,
default role completeness and glyph presence. It does not paint cached widget
trees or verify generation refresh after mutation; A070 supplies a separate
focused test for one of those gaps. The native host suite runner was rebuilt
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

Added an audit-only runAfterCursorAudit entry point to ui_widget_test.wren,
invoking exactly the original 20 checks after the failing cursor case with
fresh counters. The original run and assertions are unchanged. The isolated
`widget-tail` host-test mode reports 20 passed, 0 failed, covering key routing,
modal quit/error cleanup, Backspace/Escape aliases and mouse ownership. This
does not turn the full suite green: A068 remains and the tail intentionally
does not test cursor properties. No GUI or connected-session execution.

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
The suite aborts at A068 before returning its pass/fail counts; do not report
it as completed or passing. Tests after the cursor case (including modal error
recovery and mouse dispatch) were read but not executed in this invocation.
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
being promoted to findings. A067 is independently reproduced, not inferred
from those questions. Pane implementation itself remains unread by this pass.

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
Several are intentionally stubbed (offline scrollback, hyperlinks, cache,
scriptModules), and serialRates/flowControlsNoRts share generic handlers;
their advertised semantics still need consumer/legacy comparison.

Native CMake build of st_wren_host_test succeeds and CTest wren_host passes
(1/1, 1.12 seconds). It uses build-tree fixture files, headless output and an
ANSI discard callback; no desktop or actual terminal interaction. It covers
menu selection, settings setter/apply happy paths, theme/font/sort collections,
encryption transitions, REPL error recovery and style restoration, log ring
eviction, Cell retarget lifetime and control-key routing. It does not exercise
settings stale-handle, failed-save or reload contracts (A063–A065), safe-mode
persistence, or physical backend behavior. Its web refresh case deliberately
uses an invalid URI and verifies restoration of one cell, two palette entries
and cursor, not a successful live download or full-screen restoration.

Settings/BBS binding pass completes source reading of wren_bind_menu.c
(including the full binding table). Binding-to-script declaration parity and
the complete host test remain pending. The Settings manual was checked before
this pass. Beyond A063, source-level contract discrepancies require targeted
tests: reload calls st_settings_reload (disk reload), not a copy of current
running settings; save applies first and can then fail persistence, although
the manual promises unapplied changes remain unapplied on failure; setters
mark dirty even when assigning the existing value. Apply explicitly updates
theme and scaling but ignores scaling errors; other promised immediate
effects and KDF/list-path integration require tracing consumers. BBS palette
and script-list setters mutate elements before completing validation, so a
later invalid element may leave partial changes without setting dirty.
These are not yet runtime-confirmed findings. Settings.save, saveWebLists and
setEncryption also lack host safe-mode checks; persistence behavior for those
paths has not been reproduced and is not covered by A062's BBS-only test.

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
safe-mode exception needs explicit policy/legacy comparison before extending
A062. Menu.openUrl likewise lacks a safe-mode gate despite Host.safeMode's
documented restriction on script-driven external commands; no browser or
external command was launched during this audit.

Screen-mode binding discrepancy: screen_mode_size duplicates only text-grid
dimensions, currentScreenMode returns the first matching grid (so multiple
80x25 modes collapse), and setScreenMode calls only st_console_resize without
updating font geometry, raster margins, cursor defaults or aspect. Its Prestel
and Atari XEP80 rows are 24 versus startup's 25. This is a source-observed
integration concern needing focused mode tests and legacy comparison, not
evidence that all console resize operations are wrong.

Custom-font bindings use generation checks and prohibit mutation in safe mode.
Reload calls the whole settings reload before font registration and increments
font_generation only if both succeed; a partial registration failure can leave
new settings paired with old generation/slots. Save preserves handles, but its
full settings_save persists more than just font sections, unlike the stated
separate-model contract. setFile requires exact bitmap byte sizes although the
manual explicitly says it adds no size restriction. Name validation also adds
ASCII/bracket/whitespace restrictions beyond that contract. These are recorded
as source/documentation discrepancies pending consumer/legacy tests, without
changing either side to force agreement. No network, theme deletion, font-file
write or external URL action was performed in this pass.
Menu binding read coverage extends through :1525, in addition to :3360-3480.
Build-options data distinguishes compiled video/audio backends from unavailable
features and reports the Unifont-derived binary license option. Audio choices
use runtime availability, whereas outputModes currently offers only Automatic;
settings UI/backend-policy completeness remains to be assessed with scripts.
Theme selection snapshots settings, applies selection, skips settings_save in
safe mode and attempts rollback on failure; rollback errors are ignored. Read
preview/catalog helpers as well, but full theme-edit integration and mutation
failure paths remain pending. No actual audio device or theme change was made.

Menu binding initial pass: wren_bind_menu.c:1-1180 and :3360-3480 read with
Wren.adoc Menu BBS Model and identity/persistence/generation contracts. BBS
foreign objects contain owned deep profile copies rather than borrowed record
pointers, so list replacement does not inherently dangle those copies. However,
no BBS generation check exists in bbs_value, unlike font_value's explicit
generation guard. The documentation promises stale-handle rejection after
load/create/delete. This is a behavior discrepancy pending targeted tests,
not proof of a memory lifetime bug. Profiles release owned data on finalization.

Additional source follow-ups: quick-connect objects use is_new, the same flag
as new durable entries, and save routes such objects to st_bbs_list_add, despite
the documented prohibition on transient saves. Menu.create/copy truncate names
via snprintf and do not perform documented uniqueness validation or statistics
reset. nameAvailable checks combined-list existence only, not validity/reserved
names/personal-only collisions. Read-only source restrictions are missing from
rename and some setters; save's lower-level source checks must be traced before
claiming any on-disk effect. The absence of safe-mode checks in BBS save/delete
also requires lower-level verification. No writes were tested in this pass.

URL parsing uses counted strings, rejects embedded NUL, handles bracketed hosts,
bounded user/password/address spans and explicit numeric ports. It does not
percent-decode credentials or generally reject unrecognized schemes; legacy
parity remains pending. Explicit CLI connection types override defaults before
scheme parsing, and profile replacement occurs only on success. Menu's default
port binding returns 23 for MQTT while quick-connect's helper returns 8883;
consumer and legacy expectations need reconciliation. Sort parsing reuses the
INT_MIN-sensitive normalizer already covered by A033; this is another consumer
of that existing finding rather than a duplicate defect.

Core binding source read is complete (remaining :1740-2340 covered this pass).
Reviewed Cell and CustomCursor manual contracts plus Host.print and Format
descriptions. Cell foreground/background RGB tags are now separate from flags;
manual wording about preserving packed high bits and CP437-only characters
still describes legacy layout. Font assignment reruns Unicode lookup and drops
explicit raw-glyph state, which needs a raw-cell/font-order test before claiming
preserved semantics. Attribute/blink/bright mutations do not rerun font/width
resolution; alternate-font selector integration remains an open behavior check.

CustomCursor uses staged instances and applies through the console API; its
setters accept zero range/inverted endpoints and apply discards validation
failure, so invalid edits can appear to succeed. Several boolean setters simply
ignore wrong types rather than abort, unlike numeric validation. Clipboard
getter frees text with the owning library's callback; setter uses C-string
input and ignores write errors. Host.print intentionally writes/flushed stdout,
as documented, and is distinct from System.print's VM-local log.

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
Windows runtime behavior. Existing headless success does not contradict the
separate A055 load-error and A056 directory-error regressions.

Picker filesystem pass: wren_picker_fs_posix.c and wren_picker_fs_windows.c
read completely. Both append into caller-owned vectors with checked geometric
growth; names become vector-owned only on successful append. POSIX uses strict
UTF-8 validation to skip unrepresentable names, follows filesystem metadata and
filters to files/directories. Windows uses extended wide paths and UTF-16
conversion, checks FindNextFile termination errors, and frees its search/path
resources. FILETIME conversion handles pre-epoch dates with negative floor
seconds; maximum input remains within int64 seconds. FindClose failure is
ignored. Windows execution was not performed in this pass.

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
the earlier picker trust-boundary section. New directory-read regression is
standalone; no production or CMake changes were made.

Loader regression verification: wren_host/tests/st_wren_loader_audit_test.c
compiled with native Clang C17 and -Wall -Wextra -Wpedantic -Werror, linked
against the existing build/wren_host and dependency archives (the same archive
order as st_wren_picker_test's link.txt). Run the executable with two arguments:
a fresh mktemp -d directory and either autoload or recovery. Each invocation
uses only a headless console and its temporary script/theme/cache directories;
no personal settings, GUI or network operations are used. Both modes exit 1
with the failures recorded in A054/A055. The autoload host successfully
initializes before checking the two absent modules. Tests remain standalone,
not added to CMake because build configuration is outside audit edit authority.

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
from absence. Embedded NUL, Unicode Windows paths (plain fopen), role-specific
override precedence and reserved-module rules need further tests. The embedded
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
and application consumption of parsed options remain pending. Existing parser
tests inspected through their command-line section cover valid forms and an
invalid suffix but not bare -i; A053 adds the missing boundary case.

Root build pass: CMakeLists.txt, syncterm_config.h.in, syncterm_types.h and
.gitignore read completely. Root declares C and uses a function-scoped strict
C17 compile/link probe for ssize_t and the configured public type header;
the Windows fallback uses SSIZE_T, while POSIX absence is fatal. Configuration
is generated in the binary directory. Root itself neither downloads data nor
runs target executables; component-specific probes remain covered by their
own audits, including A002. Optional Unifont controls both its subdirectory
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
uses signed floating-point coordinates and thus avoids A050's unsigned wrap;
scrolling collapses every nonzero delta to one step, with no accumulation.

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
SDL_QUIT delivered to every managed display. A003 still covers the unchecked
deadline-construction failure in next_command. Returning from application_main
with other threads still submitting console commands needs lifecycle-contract
verification at the host integration; the passing tests destroy consoles first.

Window/pixel units are separated for high-DPI sizing; monitor bounds come
from SDL_GetWindowDisplayIndex and usable bounds (ordinary bounds fallback),
with frame borders subtracted for snap limits. Mouse mapping widens before
subtracting destination offsets, unlike A050. Internal scaling uploads
regions into a streaming texture and redraws the retained texture on expose.
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
clipboard exchange, failed compositor connections after startup, protocol
version fallback, buffer exhaustion or shutdown during an unfinished transfer.
It was not run on a live compositor in this pass. A050 and A051 are isolated
native regression tests and both fail as documented.

Worker lifecycle owns protocol objects, presents cached frames periodically,
and serializes caller commands with a mutex/condition and wake pipe. Caller
stack commands remain borrowed until completion; normal destruction submits
stop, joins, then destroys presenter and synchronization objects. Startup
waits for initial xdg configure without a deadline, and get_buffer_from can
block on protocol dispatch without polling the command pipe. Worker ignores
dispatch_pending/draw/flush errors in its periodic path and only tests POLLIN
on the display descriptor; terminal connection-error handling needs injection.

Protocol capability concerns: registry binding caps versions but does not
require compositor v4 before invoking damage_buffer (a v4 request). Multiple
seat globals overwrite one slot, removals are ignored, and seat_capabilities
only adds keyboard/pointer objects rather than handling removal. Input-repeat
logic does not query per-key repeat capability. These remain source-level
portability/lifecycle concerns, not compositor-tested findings. Fullscreen
tracks requested and configured state separately; a compositor changing state
independently may leave fullscreen_requested stale. The documented Wayland
positioning limitation is distinct from these implementation questions.

Wayland review in progress: loader header/source read completely, and backend
through get_clipboard's receive loop (line 1220). Loader is once-initialized
and retains its library process-wide. Protocol bindings dispatch through the
loaded proxy entry points; xkbcommon is separately loaded per backend.
Shared-memory buffers are double-buffered, with pending damage retained for
busy buffers and copied on release. get_buffer_from waits through blocking
wl_display_dispatch if both are busy; shutdown responsiveness when releases
stop needs a dedicated check. buffer_create records mapped size only after
pool/buffer creation succeeds, although failure cleanup uses buffer->size
for munmap; this error-path leak is a source concern pending fault injection.

Input uses xkb modifier names and key-to-UTF32, not an input-method or compose
state. Repeat scheduling starts for every pressed key without querying the
keymap's repeat flag; modifier/lock-key behavior remains to be tested.
pointer_enter discards its coordinates, leaving a subsequent button event
dependent on the last motion position. A050 independently reproduces the
signed/unsigned margin conversion error. Clipboard source_send writes a
potentially blocking pipe on the backend thread; cancellation, peer closure
and shutdown behavior require follow-up. No compositor or desktop accessed.

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
The remaining X11 implementation and full integration test remain pending;
A048 is a compile-time finding, not evidence that runtime input was validated.

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

The ASCII/alternate-character helper tries Unicode box-drawing mappings and
then a CP437 interpretation of the low byte of glyph before ordinary ASCII.
Because glyph is a font index, not necessarily CP437, that fallback needs
selected-font/custom-map tests. No new confirmed finding assigned yet.
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
ownership guard. CP437 output uses glyph rather than a Unicode-to-
CP437 conversion; custom font maps therefore need coverage. Terminal colour
capability probing calls setupterm without an observed del_curterm cleanup,
and shares the process-global terminfo context with curses; coexistence review
remains pending. ASCII comparisons are local byte operations, not locale calls.

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
needs actual cross-build verification. Several probes do not explicitly select
the target's strict C17 programming environment (same class of concern as
A002), and curses header checks do not establish the complete implementation
API/ABI. GCC suppresses pedantic warnings for whole backend source files, not
only symbol conversions. These are review concerns, not reproduced additional
portability failures. Minimal standalone native build concretely fails A047.

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
component and C17 requirements are target-local. A001 documents the API gap.
Consumer sites identified in INI, list_crypto, BBS profile codec and theme cloud;
full caller error/format handling remains assigned to those component reviews.

### utf8

All four component files read. Scalar validity rejects surrogates and values
above U+10FFFF; decoding checks leading ranges, continuation bytes, length,
overlong forms and scalar validity before publishing outputs. Encoding stages
bytes locally and leaves an undersized destination untouched while returning
required size, as documented. No allocation, locale state or OS-specific path.
CMake depends on io_error; no dependency cycle within this component.
A001 records missing decode contract details. Console/theme/Wren caller behavior
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
uses mutable global state. A002 covers the capability-probe mismatch; A003 is
a caller error found while tracing the helper contract. Windows arithmetic was
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

Build checks establish loader link availability but, like A002, do not select
the target's C17 language mode. POSIX ${CMAKE_DL_LIBS} is target-local; Apple
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
assigning user-visible impact. flock errors are retried only on EINTR; A006
captures cleanup error loss. The flock probe shares A002's language-mode issue.

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
part of the INI audit. CMake is target-scoped and has platform capability checks;
the language-mode caveat in A002 also applies to those probes.
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

All five files read. Unlike the A002 probes, the local capability helper sets
C17, requires it, and disables language extensions. Probes take function
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
device namespace syntax. A008 captures unsafe sizing under changing path context.

The component validator promises separator/dot rejection only. It accepts colon,
Windows device basenames and trailing dots/spaces, so it must not be treated as
a complete Windows safe-download validator. Consumers in theme/BBS/webget are
identified for their own security/filename-policy review. Joining does not
replace a base when name is rooted; it concatenates, as currently documented.
No allocations escape failure cleanup in source review. Windows wide joins have
less complete multiplication guards than other helpers; practical input/native
length limits and 32-bit behavior are not exercised here. Tests now apply exact-
capacity canaries and in-place checks to their output vectors, plus repeated
separators/root/dot traversal on POSIX. Native tests pass, Windows compiles only.
No macOS/Haiku runtime or allocation-error injection performed.

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

POSIX selects XDG/HOME defaults (A009); macOS uses Library support/cache paths;
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
extended namespace through the path helper; A008 remains relevant to relative
calls. Filetime conversion guards range and handles pre-epoch rounding without
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
not explicitly documented. A010 records a concrete root-only failure.
CMake requires 64-bit off_t with a _FILE_OFFSET_BITS retry and target-local
definitions, but its probes share A002's default-language-mode mismatch. Public
path linkage exceeds the types needed by filesystem.h; no cycle established.

Existing tests cover metadata, exclusive creation, multi-GiB sparse truncation,
position retention, copy permissions/mtime, replacement rename, nested creation,
symlinks/dangling links and cleanup; Windows additionally has long/exact names
and optional symlink tests. Added hard-link self-copy preservation and oversized
truncate rejection. Native Clang/GCC pass; MinGW builds. Windows missing-root
test builds only. Real short writes, disk exhaustion, native cleanup failures,
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
keep the concrete public type layout consistent with consumers. Most operation
preconditions are left implicit by the terse C11-style header; A004 includes
the missing deadline clock. NULL/invalid-handle tolerance is not consistent
across native and fallback paths and should not be assumed.

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
Root include directories are exposed for generated config/types. Standalone
builds set C17 for probes, but aggregate builds may share A002's probe-context
issue. Windows link dependencies and platform sources are target-local.

TCP resolves synchronously before deadline/cancellation, as explicitly stated
in its header. It copies IPv4/IPv6 addresses into aligned bounded storage,
interleaves families, staggers attempts by 250 ms and polls cancellation at
50 ms while racing. Immediate failures advance without the stagger; output
publication waits until blocking mode is restored and losers are closed.
Per-attempt storage cleanup is explicit. A012 violates error-path correctness.
Interleaving repeatedly scans from the beginning (quadratic in address count)
and each wait rebuilds active arrays; no real-world large-resolution benchmark
performed. next_attempt = now + 250 lacks deadline-style saturation at INT64_MAX;
this is a theoretical extreme-clock overflow, not an ordinary uptime failure.
Numeric service strings avoid locale formatting ambiguity; DNS text/IDN policy
is delegated to native getaddrinfo and is not a Unicode-hostname contract.

Tests cover IPv4 loopback I/O, urgent data, EOF, cancellation, zero timeout,
family ordering/staggering, error priority and loser cleanup. New loopback
refusal test passes natively; the corrected consuming-error fake fails as
documented in A012. IPv6 live connections, high descriptor counts, interrupt/
clock/allocator faults, native Windows errors and concurrent readiness consumers
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

CMake selects one platform source with narrow helpers and per-capability probes;
aggregate probe language-mode caveat A002 applies. Root includes are exposed for
common generated types. A013 explains why ordinary serial CTest lacked real I/O.
Manually enabled PTY tests cover transfers, timeouts, invalid config, enumeration,
zero-length operations and close on this FreeBSD host. No physical serial device,
modem-line transitions, flow-control stalls, Windows COM runtime, unplugging,
allocation failure or cancellation fault injection was exercised. Those limits
are distinct from the passing argument-only normal CTest.

### connection

- Read all connection sources, headers, generated-header template, CMake and
  test sources, including POSIX/Windows/stub shell backends and serial fake.
  Compared relevant legacy RLogin controls, modem replies, PTY initial flags,
  environment construction and teardown. Checked RFC 1282 directly; A017
  records retained protocol mistakes. Remaining runtime/stress coverage gaps
  are explicit below and are not claims of verified behavior.
- Queue state is mutex-protected; workers own transport input/output, and
  close sets stopping, wakes queues/state, interrupts, joins, then destroys.
  The public contract explicitly prohibits concurrent close/public operations.
  Callback reentrancy/allowed operations and simultaneous public operations
  need clearer treatment in the remaining integration review.
- A014 is confirmed. Other core behavior to assess against callers: queued()
  excludes the output worker's in-flight buffer, close discards pending data,
  read errors are logged but subsequent receive returns zero after draining,
  and remote input EOF stops both directions. These are not yet labeled
  defects without establishing the required caller contract.
- Core clock helpers ignore failures (zero timestamp or expired deadline),
  unlike returning the clock error. The deadline is initialized, so this is
  not the uninitialized-data issue in A003. Mutex/condition/join failures are
  commonly ignored; injected synchronization failures are not covered yet.
- Network transport serializes geometry and atomically queues RLogin window
  packets; stop wakes blocked queue writers before joining. Geometry updates
  can wait indefinitely for output capacity. RLogin urgent control handling,
  initial handshake and pause semantics have narrow existing tests. It starts
  in binary mode as the legacy BBS implementation does, not RFC cooked mode.
  The initial server acknowledgment byte is not separately consumed or
  validated; the fake server omits it. Compatibility with rejection messages
  and ordinary servers remains an integration test gap. A017 covers urgent
  direction/repeated-response discrepancies.
- Connection's forkpty capability probes have the same target/probe language
  context issue as A002 in aggregate builds; standalone configuration sets
  the language standard. Unlike serial A013, their actual probe result names
  are distinct from the preinitialized selection variables.
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
  A015 covers the proven raw-terminal regression.
- POSIX PTY master stays blocking. poll before write does not establish that
  the entire buffer fits. Shutdown sends SIGHUP, then joins workers, and only
  afterwards reaches destroy's ten-second wait/SIGKILL escalation. A blocked
  writer plus a child ignoring HUP is therefore an unresolved shutdown-hang
  concern requiring a controlled saturation test. Legacy escalation happened
  before waiting for its I/O threads. Allocation-failure cleanup also uses
  unbounded waitpid after HUP. No hang reproduced yet.
- Windows ConPTY source inspected and cross-compiled, not run. A016 records
  the lifetime race. Read/write ignore timeout_ms and use synchronous pipes;
  verify close cancellation, process descendants, and close_console behavior
  with a full output pipe on Windows. Failure paths collapse conversion/OOM/
  process errors into IO_ERROR_IO, and native_error fields are never filled
  by these providers. No Win32 behavior is inferred from native PTY tests.
- Current search finds no production st_connection_* call sites outside the
  library (profile header only documents the API); no app or Wren integration
  can be claimed from these standalone tests. Existing network tests assume
  some TCP reads yield the requested length and have early-failure cleanup
  gaps; their current success does not establish exhaustive stream behavior.

### profile

- Read every local file: public model, implementation, CMake and existing test.
  The component is cohesive and depends only on string duplication; connection
  and persistence consumers interpret the model, not vice versa. Shared enum
  values must not be assumed identical to lower-level APIs (A018).
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
  output requirements or serialized-buffer deallocation. document_read can
  publish a parsed result then return a mapping-close error, unlike the broad
  success-only replacement promise; cleanup-failure injection not done.
- A019 and A020 confirm physical/logical representation mismatches. Further
  boundary checks needed: style prefix/separator contents can change grammar;
  removing a key
  using a borrowed matching name may invalidate that name during the scan.
  These are follow-up candidates, not yet independently reproduced findings.
- Sorted index insertion and record-array shifting can produce quadratic
  construction/removal for large documents. Each parsed key allocates both raw
  text and duplicated name/value storage (including temporary copies). This
  is a performance observation, not a measured regression; benchmarks pending.
- Existing tests cover duplicate keys/sections, last-match lookup, non-ASCII
  name suppression and opaque value bytes, literals, malformed lines, read
  position preservation, replacement on parse errors, typed conversions,
  comments, rename/remove, clone independence and invalid arguments. They do
  not inject each allocation failure, mapping-close failure or short stream
  writes. The public view-invalidated-on-mutation wording is broader than the
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
  A021 demonstrates the distinct compact-date precedence conflict.
- Duration parsing checks isfinite after multiplying, rather than establishing
  range before multiplication; floating exception/trapping and non-IEC behavior
  remain portability questions for the decimal arithmetic review. No integer
  replacement or locale API introduced by this audit.
- Native-time probes share A002's language-context mismatch. string_util is
  linked PUBLIC though its API is absent from the public conversion header;
  this is an unnecessarily exposed implementation dependency, not a cycle.
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
  and insufficient double-format buffer checks pass on native Clang/GCC.
  The suite still fails only at A021. These selected cases are not exhaustive
  proof of rounding correctness across all inputs or floating-point formats.

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
  source concerns need targeted output tests/legacy comparison; A022 is the
  separately reproduced crossfade issue.
- Hot path holds the mixer mutex, computes powf per buffer, tanh per channel
  sample and frees acknowledged buffers under locks. Unbounded stream/buffer
  lists and drain scans affect scaling. No audio latency or contention metric
  gathered, so no measured performance regression claimed. Ticket/sequence
  wrap after 2^64 operations is not handled consistently but not reproduced.
- CMake math probes share A002's context mismatch; thread/io_error dependencies
  are PUBLIC despite neither being exposed by the mixer header. Existing tests
  cover basic mixing, loop termination, clear/drain and delayed acknowledgment,
  but their sleeps are scheduling assumptions, not explicit waiter barriers.
  Allocation/synchronization failure injection and concurrent mutation stress
  remain test gaps. All existing checks precede the new A022 failure.

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
  version compatibility can fail later at build time. Probes also share A002's
  strict-language-context gap. SDL is off in normal builds, so its source was
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
- Catalog refresh destroys the old catalog before enumeration and can leave
  partial results on failure. Error behavior is not transactional. Each file
  is parsed into a full default clone for metadata, and duplicate-section
  detection is quadratic; no performance measurement yet.
- Source-only cleanup concern: load_path returns directly when read/close
  reports failure, without freeing a possibly published INI document. A
  successful parse followed by fclose failure would leak it. No close-failure
  injection performed. Parser allocation failures are sometimes reported as
  INVALID_FORMAT rather than OUT_OF_MEMORY.
- Classic color construction's unchecked RGB indexing was followed through
  settings ingestion in the next pass: A030 confirms unbounded numeric input.
- Directory backend discrepancies: POSIX skips non-UTF-8 names and stat errors;
  Windows stops on conversion errors and includes non-directory entries.
  POSIX open/enumeration error handling is A024. Symlink/reparse handling and
  cloud downloaded-name validation require caller-specific review.
- Local build is strict C17 and target-scoped. It exposes the full settings
  dependency publicly though the theme header already uses forward declarations;
  this is a coupling observation, not authorization to redesign it. Core themes,
  document editing and online catalog/cache are distinct implementation files
  within one library. Network/crypto link requirements are pulled in for local
  theme consumers as well; evaluate only against actual consumer requirements,
  not an assumption that every implementation file needs its own library.
- Editor states deeply own metadata/style/glyph strings; previews are separate
  allocations. Documents borrow their manager and must not outlive it, which
  the header does not explain. Every mutation clones the entire state and
  builds a preview; history is unbounded, including redundant same-value edits.
  Indexed enumeration scans past removed entries, so enumerating all entries
  via *_at is quadratic. No UI workload benchmark performed.
- commit_state discards redo history before realloc succeeds, so failed edits
  can still remove redo. Constructor failure ownership is A027. Saved-index
  reuse and zero-presence source reconciliation are A025/A026. No allocation
  fault harness yet. Public errors also collapse preview allocation failures
  into invalid-format/incomplete-default-style messages.
- Document open reads the file three times (original bytes, parsed source,
  resolved theme); concurrent edits can mix versions. Save compares original
  bytes then writes/validates a same-directory exclusive temporary and renames.
  Comparison is not locked through replacement: concurrent saves or destination
  creation between check/rename can evade conflict/exists checks. No race test.
  Symlinks and case-insensitive POSIX filesystems complicate filename identity.
  Save does sync the temporary, but not the containing directory. No claim of
  crash-durable rename. Short-read descriptor leaks are A028.
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

- Load binds copied paths and shuts down the previous store before opening the
  replacement. Reload decodes a temporary first, but installs global settings
  before copying the new baseline; that allocation failure can leave changed
  globals and a disposed baseline snapshot with the old baseline document.
  No OOM injection. Save writes disk before allocating its replacement baseline
  snapshot, so a returned OOM can occur after persistence succeeded.
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
- Range handling is inconsistent: dimensions/timeouts are bounded, enum numeric
  fallbacks are not. A030 makes the Classic color consequence concrete; output,
  cursor and screen enum consumers require their own checks. ScalingFactor only
  excludes negatives; finite-but-huge input must be bounded by console sizing
  before narrowing. KDF is an opaque bounded string here; crypto consumer owns
  cost validation. No locale calls introduced.
- Name validation differs: custom font insertion disallows empty but rename
  allows it (A029); sort names reject =/: but accept '[' prefixes (A020's INI
  round-trip problem applies); web list names are only checked non-NULL and
  can cause save rejection or case-insensitive duplicate collapse. Duplicate
  font/sort names remain accepted; full behavior should match the documented
  menu contract rather than silently imposing uniqueness. Sort update replaces
  the record before updating active name/order, so allocation failure can leave
  partially applied changes. Removing the last profile recreates defaults but
  can leave a partial list on allocation failure. Not injected.
- Legacy settings reference preserves VideoMode alias, theme-package precedence,
  KDF string migration, and sort profile defaults/custom-order migration.
  Compared relevant read-only syncterm.c and bbslist.c helpers; per-key Classic
  fallback differs (A032). This is not a full app-level legacy behavior claim.
- Existing tests cover one owned-copy case and mixed independent/conflicting
  external settings edits, path redirection, sort and custom-font persistence,
  unknown root content and basic empty sort values. New tests expose A029,
  A030 and A032. Actual simultaneous writers, allocation failures, disk-full
  writes, all field boundaries and host handle invalidation are not tested.

### bbs_list/

Read all local source/header/build/test files, including profile codec,
web-refresh worker logic and its full loopback test. Encryption, HTTP/TLS
implementation and application/Wren consumers remain separate inventory areas.

- Public list objects own profiles/diagnostics, paths and a copied password;
  close clears password bytes through crypto before freeing. Public getters
  borrow entries/profiles/defaults/diagnostics without clear invalidation or
  thread-safety documentation. A035 affects getter-derived mutation inputs.
  Multiple objects can share disk paths, but also consult process-global
  settings for selected personal path and named web sources. Their behavior
  is not independent of those mutable globals.
- Rebuild clears the view and defaults before decoding replacements. Failure
  can leave a partial view with freed prior entries but unchanged generation;
  reload/install have already replaced the personal document. Caller use of
  generation on failed operations needs follow-up. Optional source failures
  usually become diagnostics and continue, but diagnostic-allocation failures
  are ignored in several branches. shared_path strdup failure is not checked
  in open and can silently omit that source.
- Precedence is personal, shared, system cache, configured named caches. Later
  duplicates replace earlier ones only within a source. ASCII case-folded name
  search is linear per appended section: full reconstruction is quadratic up
  to the advertised 100000-entry cap. No large-list rebuild benchmark yet.
  Sorting uses thread-local comparator context, not a process-global comparator;
  independent threads don't share fields, but sorting one list concurrently
  remains unsafe. Stable tie handling falls back to name, not original order.
- Store performs sidecar-locked reread/field merge/write. V1 ordinary writes
  explicitly return NOT_SUPPORTED; explicit rewrite handles migration. Writes
  prepare crypto output in tmpfile first, but then truncate the actual file and
  copy: A031's failure-atomicity concern also applies here. No sync/replacement
  protects the existing list from partial destination writes. This preparation
  prevents crypto preparation failures from truncating the list, not write
  failures after output is opened.
- Normal store writes use the object's retained write_options, not current
  remote encryption options. Same-password external rekey/parameter changes
  could be overwritten by a stale object's next edit; review with list_crypto
  before assigning a separate finding. Rewrite updates metadata from caller
  options without rereading normalized defaults, unlike install_document.
- Update/remove merge profile content and remove older duplicate sections.
  Case-only rename is treated as equal and not applied. Cache status is a
  secondary result after persistence; callers passing NULL cannot inspect it.
  Failed readback/rebuild after a successful write returns failure despite disk
  mutation. Contracts should distinguish persistence and refreshed-view errors.
- Cache removal handles observed symlinks/reparse points without recursive
  following, but path-based check/open sequences can race with replacements;
  no race test. POSIX readdir errors are not distinguished from EOF. Recursive
  depth is unbounded. Sweep protects a configured system-cache basename and
  current entry names, not a dedicated ownership marker. A034 concerns direct
  lexical escape even without any race or symlink.
- Existing native list test passes: precedence/duplicates, typed round trips,
  independent-field merge, conflicting edits/removal, cache rename/remove/sweep,
  encryption rewrite/reopen. It uses disposable build-tree fixtures. It does
  not cover borrowed inputs, failure atomicity, malformed names at cache
  boundaries, allocation failures or simultaneous threads/processes.
- Profile codec initializes a fresh profile, bounds copied fixed strings,
  connection kind/port/data bits/stop bits/music and palette length, and uses
  shared hex decoding for SSH fingerprints. It assumes its internal name
  argument was validated before strcpy; current append/update call paths do
  validate names. It is not a replacement-into-owned-profile API: init would
  discard pre-existing owned paths. Current callers dispose/init appropriately.
- Most other enum fields accept arbitrary numeric values from INI and narrow
  size_t to int; sort order and call count also narrow without bounds. A030
  already demonstrates why consumer range assumptions need checking. No new
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
  keys; webget locking and identity behavior remains to review.
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
Public header gives little contract detail for timeout defaults/infinity,
progress cancellation, validation callback statuses, cache ownership or partial
success. Request includes TLS options and therefore exposes TLS dependency;
other dependencies are target-private except io_error. No platform branches in
the downloader itself. TLS/socket/lock platform behavior belongs to those rows.

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
  here. Cached response status 304 requires an existing regular output but does
  not prove a validator was sent; missing cached output can still send stored
  validators and fail on 304 rather than retrying unconditionally.
- Normal cleanup frees URL/cache/header buffers and runtime references; TLS
  failure double-close is A036. Existing native tests cover fixed download,
  ETag conditional response, fresh hit and default one-day caching. A037 adds
  changed-URL behavior. No TLS handshake, chunked/EOF body, callback rejection,
  malformed headers, network interruption, disk-full or concurrent cache-key
  collision coverage yet. Native Clang/GCC tests fail only the new assertion;
  MinGW compile/link only.

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
- Read publishes document/info only after parse, marker check and map close
  succeed, preserving initialized prior output on failure. V1 iteration override
  is capped at INT_MAX, defaults to 50000; inspect cannot know that caller value.
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
without extensions, and permits standalone configuration. There are no TLS
tests registered by this component's CMake. Builds of st_tls in build
(OpenSSL) and build-gcc (Botan) pass; the new standalone A038 test exercises
both native provider implementations, not merely availability probes.

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
  not the valid-input PSK bypass in A038. Botan credentials also process
  certificate fields in PSK mode while OpenSSL ignores them; validation for
  paired certificate/key fields is skipped in PSK mode in both implementations.
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
  identity checking, mutual TLS, TLS 1.2/1.3 PSK success and wrong-secret cases,
  version bounds, record fragmentation, close_notify versus abrupt EOF,
  backpressure/retry, cancellation, allocation failures, and Windows runtime.
  The unavailable provider is source-reviewed only in this pass. Existing HTTP
  loopback tests do not exercise TLS. No production or CMake changes, and the
  standalone regression is intentionally not registered by a build-file edit.

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

- Webget pass: CMake builds succeed for Clang/GCC/MinGW; native webget CTest
  fails the added changed-URL cache regression (status 0 versus CANCELLED -21)
  after original assertions pass. No external endpoints contacted: test uses
  loopback and cancellation. Only webget/tests/st_webget_test.c and this report
  changed in this pass; no production modifications or commits.

- BBS codec/web completion pass: both bbs_list and bbs_web pass with native
  Clang and GCC after CMake target builds. Both MinGW targets compile/link;
  no Windows runtime claim. This pass changes only the coverage/report notes.
  Existing audit tests remain unchanged, and no production changes or commits
  were made.

- BBS initial pass: native Clang st_bbs_list_test rebuilt through CMake and
  CTest passes. Standalone sort boundary test fails as intended under Clang
  and GCC undefined-behavior sanitizers. Reproduction (from repository root):
  obtain include flags from the corresponding build's
  bbs_list/CMakeFiles/st_bbs_list_test.dir/flags.make C_INCLUDES line; compile
  with cc or gcc13, -std=c17 -Wall -Wextra -Wpedantic -Werror
  -fsanitize=undefined -fno-sanitize-recover=undefined, those include flags,
  -Istring_util, bbs_list/tests/st_bbs_sort_boundary_test.c and the matching
  build/string_util/libsyncterm_string_util.a; run the resulting executable.
  Both report st_bbs_sort.c:26 signed negation overflow. Test is intentionally
  not registered with CMake because build modifications are out of scope.
  No destructive path reproduction, Windows runtime or new commits.

- Settings pass: both native builds pass settings; settings_store reaches and
  fails three new cases (A029/A030/A032) after its original checks pass. Clang,
  GCC and MinGW targets compile/link; no Windows runtime execution. Only
  settings/tests/st_settings_store_test.c and this report changed in this pass.
  Existing theme audit test changes remain untouched. No commits or fixes.

- Theme partial pass: rebuilt st_theme_test with native Clang, native GCC and
  MinGW. Both native CTests reach and fail the new regular-file-as-directory
  regression; all preceding assertions pass. Windows compile/link only.
  No implementation fixes or commits made for this pass.
- Theme editor/cloud pass: rebuilt the same target in Clang/GCC/MinGW.
  Native CTest reproduces A025 (unsaved branch reported clean), A026 (reset
  override restored on reopen) and A024. Existing preceding checks pass.
  MinGW compile/link only. Changed only theme/tests/st_theme_test.c and this
  report; no production changes or new commits.

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
- Time/error pass: existing time and io_error CTests pass in both native builds.
  Added audit-only time/tests/st_time_fault_test.c; compiled and ran successfully
  with cc (Clang) and gcc13 using the documented standalone command. It directly
  includes unchanged production st_time.c with controlled clocks, testing UTC
  failure/invalid nanoseconds, carry normalization, invalid duration, monotonic
  failure output preservation, infinity, UINT64 saturation, INT64 clamping and
  expiration. No CMake registration was added because build edits are prohibited.
  This is helper-level evidence, not evidence that SDL handles failures safely.
- Event/semaphore pass: both targets rebuilt with Clang and GCC, both CTests
  passed ten consecutive runs per compiler. MinGW targets compiled and linked;
  no Windows runtime claim. Only their test sources and this report changed.
  No production changes or additional commits made during this pass.
- Dynamic-library pass: native Clang/GCC fixture CTests pass and MinGW target
  builds. No test or production source changes in that component.
- File-map/file-lock pass: native Clang/GCC CTests pass and MinGW targets build.
  The new standalone POSIX lock-error reproduction compiles with both native
  compilers and intentionally exits 1: expected -4, got -19, one cleanup close.
  No build registration added; its source includes the reproduction command.
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
- Path/app-path pass: native Clang/GCC CTests pass; MinGW builds both targets.
  New standalone XDG test links against unchanged native libraries and exits 1
  with relative XDG_CONFIG_HOME/XDG_CACHE_HOME/XDG_DATA_HOME; outputs are
  relative/syncterm/syncterm.ini, relative/syncterm, and
  relative/syncterm/syncterm.ssh. HOME is inherited, not changed. No files created
  by that reproduction; ordinary app-path tests use their build-tree fixture.
  Windows sizing finding is source-only. No production edits or commits.
- Filesystem pass: expanded native CTests pass under Clang and GCC; MinGW
  normal target and standalone missing-root regression compile/link. The latter
  has not been run and is expected to expose A010. No implementation or build
  configuration changed; no commits.
- Threads pass: native Clang/GCC st_threads CTest passes five consecutive runs
  each. A separate /tmp/st-threads-audit.G680vO build forces both optional pthread
  rwlocks and mutex_timedlock absent; its full test passes five runs. Normal
  builds remain unchanged. MinGW compiles the Win32 (not pthread) backend.
  No code or test changes in this component; report only, no commits.
- Socket/TCP pass: Clang/GCC socket and TCP loopback tests pass, including
  bound-but-not-listening refusal. Both algorithm CTests fail at the existing
  expected HOST_UNREACHABLE assertion after fake wait models consumed errors.
  MinGW builds all three targets; no Windows runtime claim. Test failures are
  retained; no production edits, build changes or commits.
- Serial pass: normal Clang serial CTest passes but its flags confirm PTY=0.
  Existing tests compiled directly with -DST_SERIAL_TEST_HAVE_PTY=1 and linked
  against build/build-gcc serial and io_error libraries both pass. MinGW serial
  test target builds; Windows COM I/O unexecuted. No code/test changes made in
  this component; report only, no commits.
- Connection initial pass: native Clang/GCC connection tests pass existing
  raw/RLogin/shell cases then fail the new partial-input regression with
  16,384/16,387 bytes. connection_serial passes in both builds. MinGW expanded
  st_connection_test compiles/links; not executed. No production changes or
  new commits. Remaining component review explicitly pending above.
- Connection backend pass: added POSIX shell newline regression; Clang and
  GCC connection CTest now reports both A015 (4 instead of 6 newline bytes)
  and A014 (16,384 instead of 16,387 queue bytes). MinGW compiles/links again,
  without executing ConPTY or Windows tests. No production changes or commits.
- Connection/profile boundary pass: RFC checked for A017; new flow mapping
  regression fails all four choices in native Clang/GCC. Existing profile
  CTests pass both. MinGW builds connection_serial and profile tests, not run.
  Only test/report changes, no commits. Connection and profile source coverage
  complete with documented stress/error-injection/platform gaps.
- INI initial pass: existing ini CTest passes Clang/GCC; expanded ini_interface
  fails both unterminated-line insertion regressions after earlier tests pass.
  MinGW compiles/links expanded test, not run. Source implementation unchanged;
  test/report only, no commits. INI remains partially reviewed.
- INI completion pass: full local test-source review complete. Clang/GCC
  expanded ini_interface reproduces A019 and A020; zero-bitfield regression
  passes. MinGW builds the expanded executable, not run. No implementation,
  build configuration or existing documentation edits; no commits.
- Value-conversion initial pass: expanded value_convert test fails A021 on
  native Clang/GCC after existing checks pass. MinGW test compiles/links, not
  executed. Only test/report changes; decimal implementation review pending.
- Value-conversion decimal pass: complete source review and additional exact
  numerical/invalid-input tests pass before the retained A021 failure on both
  native toolchains. MinGW compiles/links again; no runtime claim. No numerical
  production changes or new findings beyond documented contract/coverage gaps.
- Audio mixer pass: new pull-partition crossfade test fails identically on
  native Clang/GCC (silent bulk tail versus queued nonzero single-frame tail).
  MinGW compiles/links test; not executed. Only test/report edits, no commits.
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
