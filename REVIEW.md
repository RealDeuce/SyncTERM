# Repository-wide API, design, and implementation review

## Scope and completion rule

Baseline: `11374c6`. This review covers the entire modernized repository,
not a commit-sized diff. The inventory below includes every tracked top-level
directory at that baseline. Each row includes its public and internal APIs,
implementation, platform variants, CMake, tests, generated-data provenance,
scripts, and local documentation where present. New files inherit the scope
of their directory. Build directories are verification artifacts, not sources.

The previous shared-display work (`7608b9e`) and REPL fixes (`66755e0`,
`11374c6`) provide specific regression evidence; they do not establish full
coverage of console or Wren, let alone this repository.

For each area, inspect contracts/usability, dependency direction, ownership,
failure behavior, concurrency, portability/probes, performance-sensitive paths,
required legacy behavior, documentation, and test adequacy. Record evidence
and findings rather than equating a passing suite with a completed review.
Completion requires every area reviewed and every actionable finding fixed
and verified or explicitly deferred/accepted by the user. Material API or
product-policy choices require user direction. Commit completed fixes locally;
do not push. The legacy tree is read-only reference.

## Coverage inventory

`Pending` means no repository-wide review disposition yet. `In progress`
does not imply that a whole directory, every platform, or every contract has
been verified. A completed row must link findings and verification evidence.

| Area | Status | Focus / evidence |
| --- | --- | --- |
| Root CMake, config/type headers, .gitignore | In progress | Top-level target order and platform/type probes read; dependency/probe audit pending. |
| AGENTS.md, LICENSE, Manual.txt, Console.adoc, Wren.adoc, WREN_GUIDANCE.md, cterm.adoc | Pending | Documentation/implementation agreement and licensing boundaries; preserve the intentionally unmodified CTerm specification. |
| app | Pending | Startup, CLI, menu/connection lifecycle. |
| app_paths | Pending | Platform paths and override ownership. |
| array_list | In progress | Header, implementation, CMake, existing test read; R001, R002. |
| str_list | Pending | Owning strings, transactional mutation, allocation failures. |
| name_value_list | Pending | Owning collections versus borrowed sorted indexes. |
| hex | Pending | Encoding bounds and malformed input. |
| utf8 | Pending | Invalid sequences, bounds, caller contracts. |
| unicode_width | Pending | Width policy, generated tables and updater. |
| unicode_normalization | Pending | Normalization contracts, generated tables and updater. |
| unicode_grapheme | Pending | Segmentation/oracles, generated tables and updater. |
| codepage | Pending | Legacy mappings and conversion contracts. |
| windows_utf | Pending | Windows conversion and allocation boundaries. |
| string_compat | Pending | Native declaration probes and optional fallback tests. |
| string_util | Pending | Locale independence, trimming/search contracts. |
| value_convert | Pending | Parsing/formatting ranges and numeric portability. |
| io_error | Pending | Error mapping, preservation and API consistency. |
| time | Pending | Clocks, overflow and deadline contracts. |
| path | Pending | Path semantics and ownership. |
| filesystem | Pending | Streams, metadata, enumeration and failure behavior. |
| file_map | Pending | File/stream mappings and platform contract. |
| file_lock | Pending | Lock scope, cleanup and platform behavior. |
| dynamic_library | Pending | Loading, symbols and lifetime/error contracts. |
| threads | Pending | C11-equivalent native implementations and lifecycle. |
| event | Pending | Reset semantics, waiting and shutdown. |
| semaphore | Pending | Counting semantics, wakeups and shutdown. |
| socket | Pending | Socket ownership, readiness and cross-platform errors. |
| tcp_client | Pending | Address iteration, connect/cancel and deadlines. |
| serial | Pending | Serial configuration, I/O and cancellation. |
| connection | Pending | Raw/rlogin/serial/modem/shell contracts and lifecycle. |
| xptls | Pending | TLS integration, verification and failure paths. |
| xptls_crypto | Pending | Provider integration, crypto API contracts and build policy. |
| list_crypto | Pending | Existing stored-password threat model and format compatibility. |
| webget | Pending | HTTP/TLS, redirects, bounds and progress/cancellation. |
| profile | Pending | Profile lifetime, defaults and protocol fields. |
| ini | Pending | Parsing, rewrite retention, indexing and ownership. |
| settings | Pending | Full configuration import/round trip and lifetime. |
| theme | Pending | Theme formats, fallback rules and editing contracts. |
| bbs_list | Pending | List/cache ownership, refresh policy and progress. |
| audio_mixer | Pending | Mixing bounds, formats and performance. |
| audio | Pending | Backends, runtime/link-time dependencies and shutdown. |
| pixels | Pending | Pixel operations, scalers, performance and reference behavior. |
| fonts | Pending | Registry integration, complete built-ins/maps and generators. |
| unifont | Pending | Optional font integration, updater and license consequences. |
| console | Pending | All APIs and all backends; shared-display review is partial prior evidence only. |
| scripts | Pending | All menu/picker/UI scripts, embedded interfaces and script tests. |
| wren_host | Pending | All host bindings, VM contexts, unsupported capabilities and tests. |
| wren_vm | Pending | Vendored VM integration boundary, compile options, ownership and host use of internals. |

Bundled and external dependencies are reviewed at their integration boundaries;
this is not a request to rewrite or exhaustively audit third-party internals.
Each area's actual dependency list must be checked, including optional ones.

## Findings

### R001 — Array-list growth reads an expired allocation pointer

Status: fixed and verified in the commit introducing this ledger.

`array_list_reserve()` calls `realloc(array_list_storage(*list), ...)`, then
reads `*list` to decide whether to initialize a fresh allocation. On successful
reallocation that pointer refers to the old allocation whose lifetime has
ended. Save the initial-allocation decision before reallocating. Preserve
failure atomicity and the existing public API.

The original array-list test only inserts four elements, exactly the initial
capacity, so it does not exercise growth. Add explicit repeated growth,
element/sentinel preservation, reserve failure, and allocated-empty reuse
coverage. Runtime tests exercise those paths but do not themselves prove the
absence of a stale-pointer read; that is established by source inspection.

### R002 — Array-list public contract and typed adapters need further review

Status: open review item; not yet a completed defect disposition.

The public header does not currently spell out the allocation provenance
requirement, NULL-item rejection, terminating sentinel, mutation invalidation,
sorted-input requirement, duplicate placement, or output/failure behavior
implemented by the source. Typed adapters also cast the caller's handle address
to `array_list_t *`; audit aliasing and representation assumptions against the
approved typed-list design and all direct users before changing that boundary.
Do not silently replace the user-approved macro-based interface.

## Verification ledger

Available existing build trees: `build` (Clang), `build-gcc` (GCC), and
`build-win64` (MinGW cross-build). Each fix must record its actual checks below;
availability is not evidence that a platform has been tested for this review.
GUI checks must use an isolated virtual display, never the user's desktop.

- Inventory established from `git ls-files` and all tracked CMakeLists.txt;
  root has 46 component CMake subdirectories, plus scripts and root artifacts.
- R001: full `cmake --build` succeeded for `build`, `build-gcc`, and
  `build-win64`. Clang and GCC each passed all 12 selected tests:
  `array_list`, `str_list`, `str_list_failures`, `name_value_list`, `ini`,
  `ini_interface`, `settings`, `settings_store`, `bbs_list`, `st_console`,
  `wren_host`, and `wren_picker`. The MinGW `array_list_test.exe` also passed
  under Wine. New `test_growth()` exercises repeated growth to 257 items,
  explicit reservation to 1024 slots, rejected SIZE_MAX reservation without
  mutation, and growth/reuse of allocated-empty storage to 2048 slots.
- No area is marked complete yet. Native and cross-platform verification for
  the repository-wide review remains ongoing. These tests do not establish
  Windows native/MSVC, macOS, Haiku, or all other POSIX-platform coverage.
