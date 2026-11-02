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

- [x] Expose Unicode text sends, framed binary payload sends and deliberate
  protocol-wire sends as distinct operations. Binary payloads must preserve
  arbitrary bytes and apply Telnet/MQTT transport framing.
- [x] Require explicit ownership for destructive raw input and wire access.
  Respect terminal read-ahead, transfer reservations and pending operations;
  release ownership on explicit close, failure, GC and VM/session shutdown.
- [x] Migrate callers and remove ambiguous/unowned legacy entry points.
- [x] Verify IAC bytes, malformed UTF-8, NUL, queue pressure, ordered reads,
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

- [x] Replace undifferentiated `SFTPHandle` values with remote-file and
  remote-directory objects recording owner, kind, access rights and close state.
- [x] Move handle operations onto their owning objects while retaining SFTP
  namespace methods for path operations. Make close and scoped cleanup safe
  across success, failure, cancellation and disconnect.
- [x] Migrate browser, transfer queue, key installer, scripts and tests.
- [x] Test wrong-kind/wrong-owner use, rights, duplicate/concurrent close,
  failed close, abandoned handles, pending operations and shutdown.

Starting points: `foreign_sftp_handle`, `pending_request` and response
construction in `wren_sftp.c`, plus the SFTP and async manual sections.

## 5 Make filesystem outcomes consistent

- [x] Define and apply consistent success, recoverable-error and EOF results
  for local and remote filesystem operations. Preserve domain-specific error
  details; report programmer mistakes before native work starts.
- [x] Replace ambiguous null/Bool failures that discard native error details.
  Reconcile setters, open/close, zero-byte requests and EOF semantics.
- [x] Migrate callers and remove stale manual claims and workaround branches.
- [x] Test missing paths, existing targets, invalid arguments, read/write/close
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

- [x] Establish a documented lossless representation/range for native file
  sizes, offsets and other exposed 64-bit quantities. Never silently round
  native results or accept a Number outside the chosen exact range.
- [x] Apply shared, checked conversion rules to inputs and results, including
  signedness and host-size limits, and migrate persistence/callers as needed.
- [x] Cover zero, fractions, negative values, NaN/infinity, 32-bit host bounds,
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

Item 7: native integer inputs/results use a shared exact range of ±(2^53−1),
with unsigned lower bounds and host/protocol checks. SFTP v3 counts/times are
additionally bounded by UINT32_MAX. File metadata reports FileErr.outOfRange;
remote stat/list responses fail atomically with a non-transient range error.
Profile timestamps, picker metadata, transfer snapshots, theme generations and
console sequence numbers also reject overflow. Queue persistence validates
all numeric metadata and reserves an exhausted next-ID sentinel.

Boundary tests cover zero, fractions, negatives, NaN/infinity, the 2^53 and
signed/unsigned 64-bit limits, unchanged outputs on rejected conversions,
file side effects, provider submission values and invalid stat/list results,
and preservation of malformed/out-of-range saved queues. Five native tests
(including live SFTP), three ASan/UBSan tests and six headless application/UI
tests passed. Native Clang/GCC and MinGW i686/x86_64 libraries built without
warnings; Windows runtime and 32-bit runtime tests were not run.

Item 4: SFTP.open/opendir now construct VM-owned RemoteFile/RemoteDirectory
objects from retained request metadata. Methods enforce kind, read/write and
append rights before submission. Pending I/O retains the receiver until its
reply drains, including cancellation. Concurrent close shares one operation;
success is idempotent and failure retains ownership for retry. Busy close and
concurrent cursor reads return explicit errors. GC and abandoned-open cleanup
track close replies as auxiliary work. Provider result kinds are validated.

The browser/key installer now use asynchronous resource scopes; the queue
awaits parallel workers before closing. Scoped cleanup can await close during
normal execution, preserves original callback errors, and remains synchronous
during teardown. Shutdown invalidates resources and releases pending roots
without resuming parked bodies. The old SFTPHandle and static handle-operation
entry points are gone.

Five native tests (including live SFTP), three ASan/UBSan tests and six headless
application/add-on tests passed. Regressions cover rights, independent resource
state, directory cursor serialization, close races/retry/cancellation, pending
I/O, GC fallback, async scope results/errors and shutdown. Native Clang/GCC and
MinGW i686/x86_64 libraries built without warnings; Windows runtime tests were
not run. Concurrent unrelated crypto work may affect later builds; preserve
that work and distinguish its failures from Wren regressions.

Item 5: local mutations now return null/Error and create methods return the
resource/Error. FileError.code uses portable native IO_ERROR_* reasons;
operation buckets and null/Bool failure aliases are removed. Positive-count
byte reads use nonempty bytes/null EOF; zero reads return an immediate empty
String and zero writes are no-ops. Local close is idempotent and consumes the
stream even on failure. Setters retain assigned-value success and native
error results. C update-stream direction/buffering stays inside the binding.
Signed local timestamps are verified after storage; unsupported or rounded
values report outOfRange. The queue avoids unsupported SFTP v3 timestamps.

Paths use strict UTF-8 without NFC conversion, including picker paths; byte
payloads preserve malformed UTF-8 and NUL. The new malformed-name regression
also exposed an uninitialized free in the shared NFC helper, confirmed by
GDB and ASan and now fixed. Directory inventory keeps its receiver rooted,
reports failures instead of returning partial data, and excludes links when
minting grants. Picker navigation retains its separate explicit-consent path.
Queue/browser callers now inspect native failures and remove old EOF/Bool
workarounds. Remote results validate payload sizes/pointers and preserve
server status details; immediate provider failures have accurate retry flags.

The pre-fix /dev/full test reproduced loss of disk-full detail at fclose.
Regressions now cover buffered/direct write and close failures, creation and
removal reasons, missing roots, empty/short/EOF reads, zero requests, invalid
arguments without side effects, update-stream direction, signed/unrepresentable
timestamps, exact Unicode paths, link confinement, malformed provider payloads,
and protocol error details. Native host/menu/live-SFTP/core/picker checks passed;
ASan/UBSan host/core/picker checks passed. Six headless application/UI checks
passed, including the picker in an NFD directory. Clang/GCC and MinGW i686/
x86_64 builds were warning-free. Windows runtime tests were not run.

Item 2: Conn.sendText validates/NFC-normalizes and encodes Unicode; sendBytes
preserves arbitrary bytes while applying Telnet/MQTT framing. Telnet binary
payloads use NVT CR escaping without text CR/LF folding; rlogin byte sends and
raw reads bypass local text flow control. File-transfer output uses framed
bytes. Unframed output and destructive input now require Session.claimWire's
WireLease. It excludes native transfers/reservations, other owners, pending
script sends and reentrant input dispatch. Input includes returned transfer
bytes and terminal read-ahead before the connection ring. Release preserves
unread input, captures consumed bytes once, cancels pending owned sends and
retains the independent terminal suspension state. Owned sends retain their
lease; failure, GC and teardown release it. Old Conn send/sendRaw/recv/peek/
pending methods are removed; bundled scripts and manuals use the new contract.
Clipboard sends share the owned output queue.

Connection framing/flow-control, terminal handoff and Wren host/menu/core/picker
checks passed. Regressions exercise malformed UTF-8, NUL, IAC, CR/LF, MQTT binary
payloads, byte flow-control, read-ahead and returned-input ordering, peeks,
competing owners, capture, native reservations, queue limits, cancellation,
GC, failure and shutdown. Four ASan/UBSan checks and six headless application/UI
checks passed; the final send-rejection change also passed host/ASan host and
protocol checks. Clang/GCC and MinGW i686/x86_64 builds were warning-free.
Windows runtime tests were not run.
