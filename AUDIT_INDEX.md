# Audit findings index

This is the consolidated summary of the completed repository-wide audit in
[AUDIT.md](AUDIT.md). The detailed entries contain
classification, affected locations, evidence, reproductions, limitations and
recommended remedies. Severity does not imply exploitability; source-confirmed
risks and reproduced failures must be distinguished using those entries.
No remediation is authorized by this report.

The audit is complete, not the implementation: **26 outstanding findings**
comprise 9 medium, 15 low and 2 unresolved design/performance
concerns.
This is not a count of exploitable vulnerabilities or reproduced failures.
The audited inventory covered 47 tracked source directories and 12 root files.
Vendored internals were reviewed only at the requested integration boundaries.

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
| Script declarations → bound implementation | A098 shows an unavailable demo dependency | Keep capability scope explicit and test the shipped scripts against the actual host |
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
for its consumers.

Concurrency/performance assessment: lock/condition ownership and shutdown are
reviewed per component in AUDIT.md. Callback locking/allocation is an explicit
audio risk (A023). The unresolved rwlock performance concern A011 is not a
measured regression. Scaler/renderer correctness tests and existing benchmarks
do not establish comparative speed across every platform, mode and update pattern.
No new blanket GC, contention or scaling-performance diagnosis is made from
source shape. The source and test inventory must retain those measurement
limits in the final report.

The current TODO documents contain 26 outstanding numbered findings. A007 and
A011 are unresolved design/performance concerns, not confirmed runtime defects.
Missing features and documentation errors are also explicitly distinguished
from implementation failures in the detailed ledger.

| Finding | Severity / qualification | Subject |
| --- | --- | --- |
| A007 | unresolved | Typed-list representation needs a portability decision |
| A011 | unresolved performance concern | Non-native rwlock fast paths need performance validation |
| A023 | medium performance/reliability risk | Real-time audio callbacks take locks and free objects |
| A053 | low | Bare -i option reads past the argument terminator |
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
| A085 | medium | Wren console helper paths corrupt multibyte text |
| A086 | low | New-entry address parsing aborts on a trailing colon |
| A088 | medium | User manual does not clearly distinguish implemented menu from future connected runtime |
| A089 | low | Wren language reference gives incorrect standard-library contracts |
| A090 | low | Wren examples and cell/input descriptions retain incompatible legacy assumptions |
| A091 | medium | Required Windows DLL/launcher and libretro application packaging is absent |
| A095 | low | Distribution notice assembly is not implemented |
| A098 | low | Shipped UI demo imports an absent module |
| A113 | low source-confirmed | Menu default ports disagree with URL parsing |

## Verification and limits

### Latest configured native verification

Native Clang and GCC builds have succeeded. The current configured native tree
registers 98 tests; the latest disposition is **97 pass and 1 Wayland test
skipped**. The
X11 tests pass on an isolated display; one GCC full-suite run encountered an
intermittent BadWindow failure and its isolated rerun passed. SDL uses its
CMake-configured dummy video driver.
This is native verification, not Windows/macOS execution or comprehensive
backend coverage.

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

Native Clang and GCC execution provide the current runtime evidence;
component-specific sanitizer, optional-provider and forced-fallback results
are recorded separately. MinGW builds succeed, and focused path/filesystem
tests run under Wine; this does not establish blanket physical-Windows
behavior. No blanket macOS, Haiku, Linux/musl, MSVC, physical GUI,
audio-device or remote-server validation is claimed. Fault injection and
performance measurements cover only their documented scenarios. The scaler
benchmark's reconstructed old pipeline is not an independent legacy binary.

Registered tests intentionally retain failures for outstanding findings.
Standalone regressions that remain relevant are not registered in CMake.
Git history records completed burn-down work; these documents describe the
current outstanding set.
