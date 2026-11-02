# Wren C API and object-model corrections

SyncTERM 2.0 may break script compatibility to make native object identity,
resource ownership, byte/text boundaries, error results, and integer limits
explicit. Preserve separate menu, picker and connected VMs and the existing
filesystem consent model. Migrate bundled scripts, add-ons, tests and the
public manual with each change; do not retain obsolete compatibility aliases.

Delete this temporary checklist only after all seven findings and their
acceptance checks are complete. Commit coherent, verified changes as they
are completed. The earlier screen-snapshot regression is already fixed in
`936a9a7`: `Screen.save()` returns an owned `ScreenSnapshot` whose native
resources are finalized exactly once.

## 1 Validate native object identity

- [x] Check the actual Wren class before reading a foreign payload. Replace
  tag-only and magic-only argument checks across core, pixel, filesystem,
  menu, SFTP and other binding consumers with a consistent mechanism.
- [x] Preserve VM/context ownership checks and explicit rejection of stale,
  closed, or unavailable objects after type validation.
- [x] Add cross-type regressions, including errors whose leading code or
  size collides with another payload's tag, normal inputs, and all VM modes.
  Incorrect arguments must produce a Wren error or the documented false/null
  result without entering an unrelated native subsystem or reading its data.

Starting points: `foreign_kind` in `wren_bind_core.c`, `slot_pixel_*`,
filesystem grants, menu handles and `slot_handle` in `wren_sftp.c`.

## 2 Separate payloads from owned wire access

- [ ] Expose Unicode text sends, framed binary payload sends and deliberate
  protocol-wire sends as distinct operations. Binary payloads must preserve
  arbitrary bytes and apply Telnet/MQTT transport framing.
- [ ] Require explicit ownership for destructive raw input and wire access.
  Respect terminal read-ahead, transfer reservations and pending operations;
  release ownership on explicit close, failure, GC and VM/session shutdown.
- [ ] Migrate callers and remove ambiguous/unowned legacy entry points.
- [ ] Verify IAC bytes, malformed UTF-8, NUL, queue pressure, ordered reads,
  handoff/replay, competing owners, native transfers and disconnect paths.

Starting points: `wren_bind_session.c`, `st_term.c`, connection framing and
transfer ownership, and the Conn section of `docs/Wren.adoc`.

## 3 Own session lifetime holds

- [x] Replace the shared `CTerm.sftpActive` Bool with per-owner session
  lifetime leases; derive native auxiliary activity from live leases and work.
- [x] Support idempotent release, multiple independent owners, structured
  cleanup across cooperative yields/errors, GC fallback and VM teardown.
- [x] Migrate SFTP queue and other callers; remove the terminal-emulation flag.
- [x] Test that one owner cannot release another's hold, failed callbacks
  release their own hold, and shell EOF closes only after all work/holds end.

Starting points: `wren_sftp.c`, `scripts/sftp_queue.wren`, auxiliary activity
in `src/syncterm.c` and `st_term_session_set_auxiliary_active`.

## 4 Model remote resources as owned objects

- [ ] Replace undifferentiated `SFTPHandle` values with remote-file and
  remote-directory objects recording owner, kind, access rights and close state.
- [ ] Move handle operations onto their owning objects while retaining SFTP
  namespace methods for path operations. Make close and scoped cleanup safe
  across success, failure, cancellation and disconnect.
- [ ] Migrate browser, transfer queue, key installer, scripts and tests.
- [ ] Test wrong-kind/wrong-owner use, rights, duplicate/concurrent close,
  failed close, abandoned handles, pending operations and shutdown.

Starting points: `foreign_sftp_handle`, `pending_request` and response
construction in `wren_sftp.c`, plus the SFTP and async manual sections.

## 5 Make filesystem outcomes consistent

- [ ] Define and apply consistent success, recoverable-error and EOF results
  for local and remote filesystem operations. Preserve domain-specific error
  details; report programmer mistakes before native work starts.
- [ ] Replace ambiguous null/Bool failures that discard native error details.
  Reconcile setters, open/close, zero-byte requests and EOF semantics.
- [ ] Migrate callers and remove stale manual claims and workaround branches.
- [ ] Test missing paths, existing targets, invalid arguments, read/write/close
  failures, empty data, EOF and cancelled operations without weakening consent.

Starting points: `wren_bind_fs.c`, SFTP results, queue/browser callers and
FileError/SFTPError documentation.

## 6 Separate profiles, entries and sessions

- [ ] Replace the overloaded BBS models with distinct profile values/views,
  editable directory entries and connected-session state. Share profile field
  meanings and expose configured encoding separately from live terminal state.
- [ ] Keep entry identity stable across unrelated insertion/deletion; reject
  deleted/replaced entries and stale snapshots without aliasing new records.
- [ ] Implement the advertised personal-entry cache capability with existing
  cache-path policy, readonly/system restrictions and consent boundaries.
- [ ] Migrate menus, connected defaults, add-ons, scripts, tests and manual;
  remove obsolete namespaces and reacquisition workarounds.
- [ ] Test create/copy/rename/delete/reload, unsaved and system entries,
  profile/session isolation, cache ownership and persistent VM lifetimes.

Starting points: the two BBS classes, `wren_bbs_model.c`, `wren_bind_menu.c`,
`wren_bind_session.c` and the native profile/list interfaces.

## 7 Define exact integer boundaries

- [ ] Establish a documented lossless representation/range for native file
  sizes, offsets and other exposed 64-bit quantities. Never silently round
  native results or accept a Number outside the chosen exact range.
- [ ] Apply shared, checked conversion rules to inputs and results, including
  signedness and host-size limits, and migrate persistence/callers as needed.
- [ ] Cover zero, fractions, negative values, NaN/infinity, 32-bit host bounds,
  the 2^53 boundary, signed limits and provider values that cannot be represented.

Starting points: `number_u64` in `wren_bind_fs.c`, `slot_u64` and stat/entry
getters in `wren_sftp.c`, and existing exact-integer validation in pixel bindings.

## Verification record

Baseline: tracked worktree clean at `936a9a7`; user files `docs/HACKING.pdf`
and `vert.log` remain untracked and outside this work. Existing CMake build
trees include native Clang, GCC, ASan/UBSan, control-bitmap application and
MinGW i686/x86_64 configurations. Run tests headlessly with desktop variables
cleared. Never run Windows tests under Wine; report cross-builds separately
from native runtime tests. Add change-specific checks and actual results here.

Item 1: native argument validation now checks the actual class in its loaded
VM module before inspecting any payload. The shared helper allocates nothing
and preserves argument slots/arity. Core, pixels, filesystem grants, menu
handles, SFTP handles and picker requests retain their separate resource-state
and ownership checks. Receiver-only methods also rely on VM method dispatch.

The new cross-type regression reproduced a bus error before the fix. It now
covers WON errors, SFTP stat values colliding with header/magic fields, a
same-named foreign class from another module, soft-result contracts, unchanged
slots, and menu/picker/connected modes. Seven native host/UI tests passed;
the expanded host/menu tests also passed afterwards. Four ASan/UBSan tests
passed with unsupported FreeBSD leak detection disabled. Six control-bitmap
application/add-on tests passed. FreeBSD Clang/GCC and MinGW i686/x86_64 host
libraries built without warnings; Windows runtime tests were not run.

Item 3: `Session.current` now issues independent `SessionLease` objects.
Explicit release is idempotent, GC releases abandoned leases, scoped holds
preserve callback results/errors across yields, and teardown releases all
holds before disconnect hooks. Native pending requests retain their own
auxiliary activity. The queue and public-key installer now own their holds;
the old terminal flag and aggregation module have been removed.

Seven native host/UI tests and the live SFTP test passed. Three ASan/UBSan
tests and six control-bitmap application/add-on tests passed. Native Clang/GCC
and MinGW i686/x86_64 host libraries built without warnings. Windows runtime
tests were not run.
