# Audit findings index

This is the consolidated summary of the completed repository-wide audit in
[AUDIT.md](AUDIT.md). The detailed entries contain
classification, affected locations, evidence, reproductions, limitations and
recommended remedies. Severity does not imply exploitability; source-confirmed
risks and reproduced failures must be distinguished using those entries.
No remediation is authorized by this report.

The audit is complete, not the implementation: **71 outstanding findings**
comprise 3 high, 41 medium, 25 low and 2 unresolved design/performance
concerns.
This is not a count of exploitable vulnerabilities or reproduced failures.
All 47 tracked source directories and 12 root files are covered by the
inventory and component/integration dispositions. Vendored internals were
reviewed only at the requested integration boundaries.

Most urgent follow-up groups, subject to separate authorization:

- Memory/resource safety: A036.
- Authentication and write-policy correctness: A038 and A062.
- Data preservation and required compatibility: A016 and the
  mode-specific rendering/persistence findings.

Start with the detailed evidence and regression tests for each finding;
do not infer that every issue needs a redesign or that changing the public
contract is an approved remedy.

## API, architecture and layout assessment

The responsibility-based library split is substantially present: small utility
directories own headers, implementations and CMake targets, with explicit
dependencies. The console owns character generation, logical sessions and
backend presentation; the application/host owns menu policy. The audit does
not recommend recreating an xpdev umbrella or splitting cohesive code solely
to meet a source-file count. The strongest recurring weaknesses are at
consumer boundaries, not evidence that the entire decomposition is wrong.

| Boundary | Evidence and assessment | Recommended direction, not authorized work |
| --- | --- | --- |
| Native I/O → portable result | A006 loses errno during cleanup | Preserve error ownership across cleanup |
| Mutable model → persistence | A031, A061–A065 and A100–A102 show failure-atomicity and handle-contract mismatches | Define when state changes become visible and when handles expire; test failure paths at the public boundary |
| Script declarations → bound implementation | A054–A055, A096 and A098–A099 show unavailable or approximated contracts | Keep capability scope explicit and test the shipped scripts against the actual host |
| Public target → independent consumer | A002, A013, A047, A066 expose probe/dependency gaps | Probe the real declaration/link environment and test minimal consumers |
| Source libraries → application distribution | A088, A091, A095 distinguish a functioning menu build from connected runtime, DLL/core packaging and notice assembly | Document and verify actual artifacts without representing planned integration as complete |

Concrete coupling concerns, not automatic refactoring requests:

- `st_fonts` publicly links `st_console` to share its font structure; static
  font data therefore carries the console target dependency. This is a
  cohesion/reuse tradeoff, not a proven cycle or runtime failure.
- `st_bbs_list` includes cache-refresh/network policy and privately depends on
  global settings. `st_wren_host` coordinates console, lists, themes, settings,
  audio and crypto. Those are recognizable application responsibilities, but
  borrowed global state and callback reentrancy need explicit contracts.
- Settings exposes a process-global object; multiple logical consoles do not
  imply independent application settings or independently thread-safe VMs.
- Connection publicly exposes root/config include paths in addition to its
  domain headers. The generated type-header dependency explains part of this,
  but broad usage requirements should not be mistaken for deliberate API
  exposure of every neighboring component.
- A single internal API need not acquire DLL export decoration. A091 concerns
  the absent application/launcher boundary, not a mandate to export libraries.

Ownership assessment: opaque handles and matching destruction functions are
common and appropriate; the public pixel buffer deliberately exposes planes
for its consumers. Failures cluster where multiple layers believe they own a
descriptor (A036), or a handle outlives a native resource operation (A016).
Deep copying a BBS object avoids
a dangling pointer but does not implement stale-model semantics (A101).

Concurrency/performance assessment: lock/condition ownership and shutdown are
reviewed per component in AUDIT.md. Atomics are not accepted as proof of
lifetime safety (A016), and callback locking/allocation is an explicit audio
risk (A023). The unresolved rwlock performance concern A011 is not a measured
regression. Scaler/renderer correctness tests and existing benchmarks do not
establish comparative speed across every platform, mode and update pattern.
No new blanket GC, contention or scaling-performance diagnosis is made from
source shape. The source and test inventory must retain those measurement
limits in the final report.

The current TODO documents contain 71 outstanding numbered findings. A007 and
A011 are unresolved design/performance concerns, not confirmed runtime defects.
Missing features and documentation errors are also explicitly distinguished
from implementation failures in the detailed ledger.

| Finding | Severity / qualification | Subject |
| --- | --- | --- |
| A001 | low | Empty-input and decode-result contracts are underspecified |
| A002 | medium portability risk | Monotonic capability probe does not match target language mode |
| A003 | medium | SDL uses an uninitialized deadline after clock failure |
| A004 | low | Event and semaphore deadline contracts omit the clock |
| A005 | low | Dynamic-library search policy is only partially documented |
| A006 | low | POSIX file-lock cleanup can overwrite the acquisition error |
| A007 | unresolved | Typed-list representation needs a portability decision |
| A008 | medium under concurrent directory changes | Windows current-directory buffer growth is mistaken for success |
| A009 | low | POSIX app paths accept relative XDG base directories |
| A010 | low | Windows recursive mkdir reports success for nonexistent roots |
| A011 | unresolved performance concern | Non-native rwlock fast paths need performance validation |
| A013 | medium verification gap | Serial PTY capability test is pre-disabled |
| A016 | medium | ConPTY atomic handles do not protect active users from close |
| A021 | medium | Compact date-only branch is shadowed by epoch parsing |
| A022 | medium | Crossfade completion inserts pull-size-dependent silence |
| A023 | medium performance/reliability risk | Real-time audio callbacks take locks and free objects |
| A031 | medium | Settings saves truncate the only copy before writing |
| A033 | medium | BBS sort-field normalization overflows on INT_MIN |
| A036 | high | Downloader closes the socket twice after TLS setup failure |
| A037 | medium | Changing a download URL can reuse another resource's cache |
| A038 | high | OpenSSL TLS 1.3 PSK mode accepts an unauthenticated certificate server |
| A047 | low | Standalone console build omits its string_compat dependency |
| A048 | low | X11 runtime loader gives XkbFreeKeyboard the wrong function type |
| A050 | medium | Wayland upper/left margin mouse coordinates wrap to the opposite edge |
| A051 | medium | Wayland clipboard send can terminate the process on receiver closure |
| A053 | low | Bare -i option reads past the argument terminator |
| A054 | medium | Additional user event scripts are never auto-loaded |
| A055 | medium | Picker load failure prevents application startup instead of recovery |
| A056 | low | POSIX picker reports directory read errors as successful listings |
| A058 | medium | Wren cannot round-trip every 64-bit mouse-event mask |
| A061 | medium | Failed Menu.load discards the existing model |
| A062 | high | BBS.save writes the personal list in safe mode |
| A063 | medium | Settings snapshots never become stale after another snapshot applies |
| A064 | medium | Failed Settings.save still applies the previously unapplied snapshot |
| A065 | medium | Settings.reload reads disk rather than current running settings |
| A066 | low | Wren VM target does not declare its math-library dependency |
| A067 | medium | Painter truncation changes the style of an undrawn destination cell |
| A070 | medium | Changing a Container theme does not repaint inheriting children |
| A071 | low | A newly constructed empty RadioGroup reports cursor zero |
| A072 | medium | Popup wrapping counts UTF-8 bytes as screen columns |
| A073 | medium | Changing TextInput's display mask leaves its viewport stale |
| A074 | low | Form Tab order depends on callback/field creation order |
| A075 | medium | MenuBar invokes resident commands twice per mouse click |
| A076 | low | Help scrollbar-separator changes do not invalidate wrapped layout |
| A077 | low | Markdown parses bold delimiters inside inline code |
| A078 | low | Markdown bullet hard breaks lose continuation indentation |
| A080 | low | Valid hex edits reset the color-picker cursor to the end |
| A081 | low | LogView scrollbar counts entries instead of wrapped rows |
| A082 | low | LogView capacity reduction loses its surviving scroll anchor |
| A083 | medium | SFTP queue loses unfilled portions of short-read chunks |
| A084 | medium | Failed SFTP navigation retains rows under a different directory |
| A085 | medium | Wren console helper paths corrupt multibyte text |
| A086 | low | New-entry address parsing aborts on a trailing colon |
| A087 | medium | Hook error reporting can throw while handling a non-string abort |
| A088 | medium | User manual does not clearly distinguish implemented menu from future connected runtime |
| A089 | low | Wren language reference gives incorrect standard-library contracts |
| A090 | low | Wren examples and cell/input descriptions retain incompatible legacy assumptions |
| A091 | medium | Required Windows DLL/launcher and libretro application packaging is absent |
| A095 | low | Distribution notice assembly is not implemented |
| A096 | medium | BBS editor script discovery is an unconditional empty stub |
| A098 | low | Shipped UI demo imports an absent module |
| A099 | medium | Menu screen-mode changes lose mode identity and use incomplete geometry |
| A100 | medium | Rejected BBS palette/script assignments partially change the object |
| A101 | medium | BBS handles remain usable after a successful model reload |
| A102 | medium | BBS admission and transient-operation guards do not match the menu contract |
| A104 | medium | Timed Wren input restarts its timeout after ignored events |
| A105 | medium | Custom-font bindings do not preserve their declared editing boundaries |
| A108 | medium source-confirmed | Wayland buffer failure loses the mapped length |
| A109 | medium source-confirmed | Wayland worker waits incompletely handle disconnection and shutdown |
| A112 | medium reproduced | Wren input drops physical-key press/release edges |
| A113 | low source-confirmed | Menu default ports disagree with URL parsing |

## Verification and limits

### Latest configured native verification

`cmake --build build -j4` succeeds. The subsequent command
`ctest --test-dir build --output-on-failure --timeout 30` registers 73 tests:
the stable disposition is **69 pass, 3 fail and 1 Wayland test skipped**. The
X11 tests pass on the available test display; SDL uses its CMake-configured
dummy video driver.
This is native verification, not Windows/macOS execution or comprehensive
backend coverage.

| Failed registered test | Existing findings reproduced |
| --- | --- |
| value_convert | A021 compact-date parsing |
| webget | A037 cache identity after URL change |
| st_audio_mixer | A022 partition-dependent crossfade |

These results do not include the standalone audit regression executables or
the extra isolated Wren script cases documented in AUDIT.md. Some registered
tests terminate at their first failing assertion; passing their earlier
assertions does not establish coverage of the remainder. Existing separate
suffix tests and standalone reproductions remain relevant evidence.

### Coverage reconciliation

The final requirement-to-evidence table is at the start of AUDIT.md. Historical
progress notes are retained as history, not a current unfinished-work list.
Application, console and Wren-host closure sections explicitly disposition
their former follow-ups. Other components have source/test/build/generator
reviews and limitations in their own sections; script and root-document
reviews distinguish implemented behavior from future connected-session work.

Native Clang execution is the strongest runtime evidence in this audit;
component-specific GCC, sanitizer, optional-provider and forced-fallback
results are recorded separately. MinGW compile/link results do not establish
Windows behavior. No blanket macOS, Haiku, Linux/musl, MSVC, physical GUI,
audio-device or remote-server validation is claimed. Fault injection and
performance measurements cover only their documented scenarios. The scaler
benchmark's reconstructed old pipeline is not an independent legacy binary.

Registered tests intentionally retain failures for outstanding findings.
Standalone regressions that remain relevant are not registered in CMake.
Git history records completed burn-down work; these documents describe the
current outstanding set.
