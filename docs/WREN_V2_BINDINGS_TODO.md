# Wren v2 binding corrections

SyncTERM 2.0 may break script compatibility to remove obsolete bindings and
make resource ownership, input events, asynchronous completion, and errors
explicit. Preserve the separate menu, picker, and connected VMs and the
file-consent capability model. Update bundled scripts, tests, and the public
manual with each API change; do not retain obsolete aliases for compatibility.

Delete this temporary checklist after all seven items and their acceptance
checks are complete. Commit coherent, verified changes as they are completed.

## 1 Restore UI state after errors

- [ ] Make `Screen.modalRun`, `CustomCursor.preserve`, and
  `VideoFlags.preserve` restore state after callback aborts and propagate the
  original error. Modal callbacks must continue supporting cooperative yields.
- [ ] Ensure App shutdown restores cursor, mouse subscriptions, claims, and
  running state after failures. Propagate errors from `App.runChild`.
- [ ] Cover normal return, abort, nested use, yield/resume, and shutdown in
  focused Wren host and UI tests.

Starting points: `scripts/syncterm.wren`, `scripts/ui_app.wren`, and the
snapshot and input ownership bindings in `wren_bind_core.c`.

## 2 Own asynchronous operations

- [ ] Replace public fiber-argument SFTP and timer calls with owned operations
  whose completion identity, result, cancellation, and lifetime are explicit.
  Supply an ergonomic Wren await helper without the single-source restriction.
- [ ] Validate targets and arguments before scheduling; enforce a bounded
  pending-timer policy and release cancelled/dead-owner resources.
- [ ] Migrate SFTP queue/browser, public-key installer, App ticks and mouse
  repeat, and all remaining bundled callers and tests.
- [ ] Test immediate and deferred success/error, overlapping completions,
  cancellation, limits, abandoned operations, and disconnect/shutdown.

Starting points: `wren_sftp.c`, wake delivery in `wren_bind_core.c`, and
`docs/Wren.adoc` sections Timer, SFTP, and Async Ops.

## 3 Reclaim removed hooks

- [ ] Make removal idempotent with an explicit Bool result; retain readable
  metrics on a removed handle until that handle is collected.
- [ ] Reclaim dispatch entries and regex resources once no dispatch uses them.
  Removed hooks must not consume the live registration limit.
- [ ] Test repeated register/remove beyond the limit, live-limit exhaustion,
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

- [ ] Delete `Input.setupMouseEvents` and `CTerm.refreshStatus`, their bindings,
  and all bundled call sites and obsolete explanatory comments.
- [ ] Verify automatic mouse subscription and status rendering across menus,
  nested dialogs, suspended terminals, restoration, and disconnect.

Starting points: `wren_bind_session.c`, `wren_sftp.c`, and connected scripts.

## 6 Resolve foreign bindings strictly

- [ ] Fail module loading for unknown foreign method declarations. Model
  intentionally unavailable capabilities explicitly so shared UI modules
  continue loading in each intended VM.
- [ ] Resolve the declared/documented but unimplemented `Conn.protocolLog`
  contract and reconcile stale declarations and manual entries.
- [ ] Test unknown methods and classes, all built-in declarations, forbidden
  menu/picker/session access, and supported shared modules in all three VMs.

Starting points: `st_wren_bind_method`, `st_wren_bind_class`, module loading
in `wren_host.c`, and the `wren_core-helpers` contract test.

## 7 Make text sends safely retryable

- [ ] Replace output-byte progress as an input-string retry offset with an
  owned queued send or progress in a precisely defined original-input unit.
  Define text normalization, codepage conversion, queue pressure, and failure
  behavior. Keep raw byte sends distinct.
- [ ] Migrate `ConnError.bytesSent` callers and documentation as necessary.
- [ ] Test partial queueing for UTF-8 multibyte and normalized text, codepage
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
