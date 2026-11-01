# Wren v2 binding corrections

SyncTERM 2.0 may break script compatibility to remove obsolete bindings and
make resource ownership, input events, asynchronous completion, and errors
explicit. Preserve the separate menu, picker, and connected VMs and the
file-consent capability model. Update bundled scripts, tests, and the public
manual with each API change; do not retain obsolete aliases for compatibility.

Delete this temporary checklist after all seven items and their acceptance
checks are complete. Commit coherent, verified changes as they are completed.

## 1 Restore UI state after errors

- [x] Make `Screen.modalRun`, `CustomCursor.preserve`, and
  `VideoFlags.preserve` restore state after callback aborts and propagate the
  original error. Modal callbacks must continue supporting cooperative yields.
- [ ] Ensure App shutdown restores cursor, mouse subscriptions, claims, and
  running state after failures. Propagate errors from `App.runChild`.
- [ ] Cover normal return, abort, nested use, yield/resume, and shutdown in
  focused Wren host and UI tests.

Starting points: `scripts/syncterm.wren`, `scripts/ui_app.wren`, and the
snapshot and input ownership bindings in `wren_bind_core.c`.

## 2 Own asynchronous operations

- [x] Replace public fiber-argument SFTP and timer calls with owned operations
  whose completion identity, result, cancellation, and lifetime are explicit.
  Supply an ergonomic Wren await helper without the single-source restriction.
- [x] Validate targets and arguments before scheduling; enforce a bounded
  pending-timer policy and release cancelled/dead-owner resources.
- [x] Migrate SFTP queue/browser, public-key installer, App ticks and mouse
  repeat, and all remaining bundled callers and tests.
- [x] Test immediate and deferred success/error, overlapping completions,
  cancellation, limits, abandoned operations, and disconnect/shutdown.

Starting points: `wren_sftp.c`, wake delivery in `wren_bind_core.c`, and
`docs/Wren.adoc` sections Timer, SFTP, and Async Ops.

## 3 Reclaim removed hooks

- [x] Make removal idempotent with an explicit Bool result; retain readable
  metrics on a removed handle until that handle is collected.
- [x] Reclaim dispatch entries and regex resources once no dispatch uses them.
  Removed hooks must not consume the live registration limit.
- [x] Test repeated register/remove beyond the limit, live-limit exhaustion,
  self-removal, removal of another hook, regex removal, and VM teardown.

Starting points: `connected_hook`, `foreign_hook_handle`, `register_hook`,
and all dispatch loops in `wren_sftp.c`.

## 4 Preserve Unicode and key modifiers

- [ ] Expose text, logical key, modifiers, and physical key information without
  encoding combinations as DOS compatibility codes or selecting text meaning
  through a display font. Keep remote terminal encoding inside the terminal.
- [ ] Migrate hooks, UI shortcuts, synthetic events, terminal key forwarding,
  scripts, and tests; remove obsolete compatibility helpers and constants.
- [ ] Test Unicode text independent of display font, combined modifiers,
  physical edges, unget round trips, unsupported combinations, CP437 and UTF-8
  sessions, and menu/picker/session input isolation.

Starting points: input records in `wren_host_internal.h`, input bindings in
`wren_bind_core.c`, `Hook.onKey`, `KeyEvent`, and `Key`.

## 5 Remove obsolete mouse and status calls

- [x] Delete `Input.setupMouseEvents` and `CTerm.refreshStatus`, their bindings,
  and all bundled call sites and obsolete explanatory comments.
- [x] Verify automatic mouse subscription and status rendering across menus,
  nested dialogs, suspended terminals, restoration, and disconnect.

Starting points: `wren_bind_session.c`, `wren_sftp.c`, and connected scripts.

## 6 Resolve foreign bindings strictly

- [x] Fail module loading for unknown foreign method declarations. Model
  intentionally unavailable capabilities explicitly so shared UI modules
  continue loading in each intended VM.
- [x] Resolve the declared/documented but unimplemented `Conn.protocolLog`
  contract and reconcile stale declarations and manual entries.
- [x] Test unknown methods and classes, all built-in declarations, forbidden
  menu/picker/session access, and supported shared modules in all three VMs.

Starting points: `st_wren_bind_method`, `st_wren_bind_class`, module loading
in `wren_host.c`, and the `wren_core-helpers` contract test.

## 7 Make text sends safely retryable

- [x] Replace output-byte progress as an input-string retry offset with an
  owned queued send or progress in a precisely defined original-input unit.
  Define text normalization, codepage conversion, queue pressure, and failure
  behavior. Keep raw byte sends distinct.
- [x] Migrate `ConnError.bytesSent` callers and documentation as necessary.
- [x] Test partial queueing for UTF-8 multibyte and normalized text, codepage
  conversion, raw bytes, queue-full and disconnect paths; retries must neither
  duplicate nor skip input or corrupt an encoded character.

Starting points: `fn_conn_send` in `wren_bind_session.c`, `send_text` and
`send_all_count` in `src/terminal/term/st_term.c`, and the Conn manual section.

## Verification record

Baseline on 2026-11-01: configured the top-level CMake project in `build`,
built `st_wren_host_test`, `st_wren_loader_audit_test`, and
`st_wren_picker_test`, and passed `wren_host`, `wren_core-helpers`, and
`wren_picker_contract`. Headless tests ran with desktop display variables
cleared. Record change-specific checks and actual platforms below as work
completes.

Item 5: removed both no-op APIs and all bundled calls, and corrected the
mouse/status manual. CMake rebuilt the host, loader, and picker test targets;
`wren_host`, `wren_core-helpers`, `wren_picker_contract`, and the input, popup,
and menu synthetic suites passed (6 tests, POSIX host). Existing host tests
cover remote mouse gates, restoration, status, and disconnect.

Item 3: dispatch-list and foreign-handle references now own hooks separately.
Removed entries release regex storage outside dispatch and no longer consume
the live limit; retained handles preserve metrics and removal returns Bool.
The new regression failed before the fix and `wren_host` passed afterwards.
It covers 600 register/remove cycles, 256 live regex hooks, overflow, removal
of self/another hook, registration and GC inside a callback, and teardown.
The FreeBSD Clang ASan/UBSan host test passed with leak detection disabled
(unsupported by this platform). The Windows i686 and x86_64 MinGW host-library
cross-builds passed without warnings; no Windows runtime tests were run.

Item 6: unknown methods now fail module loading, while known unavailable
capabilities use an explicit VM error. Unknown foreign constructors reject
allocation safely instead of receiving a generic native payload. Namespace
classes are ordinary Wren classes. Removed unused `Conn.protocolLog` and
`Palette.mode` declarations, migrated `Screen.writeRect`/`moveRect` callers
to surfaces, corrected numeric FlowControl constants, and implemented the
useful declared `Host.launchScript` and `Screen.hyperlinkId` getters/setters.
The manual and bundled tests match. All 13 host/menu/picker/settings and
synthetic UI tests passed. ASan/UBSan core-helper and console tests passed
(leak detection unsupported on FreeBSD); MinGW i686/x86_64 library builds
passed without warnings. No Windows runtime execution was performed.

Item 2 foundation: the new async cancellation test exposed stale interpreter
stack pointers after foreign API slot growth. The VM now reloads frame
pointers after foreign methods/constructors and discards allocator scratch
slots. A standalone test with an allocator that always moves memory failed
before the fix and passed afterwards, including ASan/UBSan. MinGW i686 and
x86_64 VM-library builds passed; the native host regression also passed.

Item 2: SFTP and `Timer.after` now return owned Operation objects with
completion identity, retained results, multiple awaiters, `.then`, and
idempotent cancellation. Native completion markers are distinct from ordinary
wakes; deferred messages retain FIFO order. Timers are bounded at 32 and
provider requests at 256. Dead await owners release their pending operations.
Cancelled SFTP replies are drained and late open handles closed; collected
unclaimed successful results also close their handles. Shutdown cancels
operations before disconnect hooks and never resumes async callbacks during
teardown. App ticks/mouse repeat hold cancellable timers, and quit explicitly
wakes a parked App. Bundled scripts and the manual use the new API throughout.

Verification for item 2: all 15 selected VM, host, menu, picker, settings,
synthetic UI, and live SSH/SFTP tests passed. Focused regressions cover
immediate/deferred errors and success, reversed provider replies, multiple
waiters, cancellation, both limits, dead owners, forged completion values,
message ordering, unclaimed handles, and shutdown. The ordering regression
failed before the drain correction. The updated host suite passed ASan/UBSan
with FreeBSD leak detection disabled. The native application linked; FreeBSD
Clang/GCC host libraries and MinGW i686/x86_64 host libraries built without
warnings. Windows runtime testing was not performed.

Item 7 foundation: added an indivisible nonblocking transport write for
complete encoded units, including Telnet/MQTT framing and RLogin cooked
flow-control commits. Queue-pressure and ring-wrap regressions plus existing
connection/RLogin suites passed on FreeBSD Clang (4 tests); the three focused
atomic/RLogin tests also passed ASan/UBSan with leak detection disabled.
GCC and MinGW i686/x86_64 connection-library builds passed without warnings.
Windows runtime tests were not run. The owned Wren queue remains to implement.

Item 7: `Conn.send` and `sendRaw` now return owned, cancellable Operations.
The host retains encoded output and handles queue pressure internally, with
64 pending requests and 1 MiB of owned storage as explicit bounds. Strict
text conversion validates/NFC-normalizes the whole request, rejects malformed
or unmappable text before output, and keeps raw bytes distinct. Transport
polls never split encoded scalars. Removed `bytesSent`, `shortSend`, and the
obsolete terminal poll/count interface; failures expose a diagnostic `partial`
Bool instead of an unsafe retry offset. Login and emoji callers await sends;
the public manual and Unicode boundary document describe the new contract.

Verification for item 7: all 16 selected terminal, Wren host/menu/picker,
settings, synthetic UI, and live SSH/SFTP tests passed. Deterministic output
capacity tests cover normalization, UTF-8/CP437, multibyte chunk boundaries,
FIFO completion, embedded NUL/raw bytes, malformed/unmappable input, both
queue limits, cancellation after a prefix, dead awaiters, GC with dropped
handles, transfer reservation, disconnect after a prefix, and shutdown with
parked waiters. Terminal and host suites passed FreeBSD Clang ASan/UBSan
(leak detection unsupported, disabled). The native application linked; GCC
and MinGW i686/x86_64 host-library builds passed without warnings. Windows
runtime tests were not run.

Item 1 foundation: added `Fiber.tryScoped()` to retain cleanup/error
continuations across cooperative yields and host wakes. Both scope links
participate in GC and detach on return/abort. Transfers and VM suspension
cannot bypass a live scope. Corrected the value-yield stack shape when
returning directly to the host. The standalone regression failed before
implementation, then passed with nested LIFO cleanup, exact error identity,
outer/inner resumes, GC, ordinary children, and direct host/root resumes.
All three VM tests and the existing host suite passed on FreeBSD Clang;
scoped-fiber and moving-allocator regressions passed ASan/UBSan (leak
detection disabled). GCC and MinGW i686/x86_64 VM-library builds passed
without warnings. UI helpers and shutdown cleanup remain to migrate.

Item 1 state helpers: `Screen.modalRun`, `CustomCursor.preserve`, and
`VideoFlags.preserve` now retain their cleanup across errors, yields, async
awaits, GC, and VM shutdown. Helpers return callback values and propagate the
original error object even if cleanup itself fails. Native cleanup ownership
runs retained functions in reverse entry order before disconnect hooks without
resuming callback bodies. `abortScoped` unwinds a parked scope; `Screen.update`
uses it when rejecting a yielding callback. Wake deferral and FIFO ordering
recognize both a scoped caller and its body as the same suspended execution.

All 16 selected VM, host/menu/picker, settings, synthetic UI, and live SFTP
tests passed. Focused regressions cover successful returns, exact error
identity, nested state/font/suspension restoration, manual yields, async
errors, caller/body wake ordering, rejected-yield cleanup, and shutdown after
GC with parked waiters. A further shutdown-failure regression confirms an
aborting inner cleanup still permits older scopes and disconnect hooks to
run. Native and ASan/UBSan host/scoped-fiber checks passed, including that
shutdown failure; FreeBSD leak detection was disabled as unsupported. The
application linked, and GCC plus MinGW i686/x86_64 host libraries built without
warnings. Windows runtime tests were not run. App run/runSync cleanup,
runChild error propagation/cancellation, and their focused tests remain.
