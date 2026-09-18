# SyncTERM development instructions

This directory contains the modernized SyncTERM project. The legacy source at
`/synchronet/src/sbbs/src/syncterm` and its sibling libraries are reference
material; do not modify that tree unless explicitly requested. New and
extracted code belongs here.

## Project direction

SyncTERM is being modernized as a C17 application with CMake as its only
supported build system. Prefer small, reviewable changes that leave the tree
buildable and preserve existing behavior unless a behavior change is part of
the task.

The goal is not to rewrite all of `xpdev`, `conio`, or `ciolib`. Before moving
or replacing shared code, identify what SyncTERM actually calls and the
transitive functionality those calls require. Implement, extract, or retain
only that subset. Do not port unused APIs for completeness, build a new
general-purpose portability library, or expand a change to other Synchronet
programs unless explicitly requested.

Keep modernization changes focused. Avoid unrelated cleanup, formatting
churn, and speculative abstractions.

## Build system

- CMake is the sole supported build system. Do not add features to, repair, or
  validate legacy makefiles or IDE project files as part of this work.
- Keep the top-level SyncTERM CMake project independently configurable; do not
  require configuring the whole Synchronet source tree.
- Use out-of-source builds. Generated sources, configuration headers, and
  probe artifacts belong in the build tree.
- Require C17 on project targets with `C_STANDARD 17`,
  `C_STANDARD_REQUIRED YES`, and `C_EXTENSIONS NO`. Do not set language flags
  manually when a CMake language feature or target property expresses the
  requirement.
- Use target-based CMake. Attach sources, include directories, compile
  definitions, options, and link dependencies to the narrowest relevant
  target. Avoid directory-wide flags and global include/link state.
- Use imported targets from `find_package()` where available. Do not pass raw
  dependency flags around globally.
- Options express user policy; probes establish platform capabilities. Keep
  detected values internal unless users genuinely need to select behavior.
- Configuration must also work when cross-compiling. Prefer compile or link
  tests over `try_run()` and do not assume target programs can execute on the
  build host.

## Platform detection

SyncTERM has two platform families:

- **Windows**, built with either MSVC or MinGW. Both toolchains target the
  Windows implementation; compiler identity is not a platform boundary.
- **POSIX**, including FreeBSD, Linux, macOS, Haiku, and other supported
  POSIX-like systems.

Use this two-family split for high-level implementation boundaries. Do not
create independent platform layers for each POSIX operating system. When one
member needs genuinely different code, isolate the smallest OS-specific part
and keep the shared POSIX path intact. Likewise, do not treat MinGW as POSIX
merely because it exposes some POSIX-compatible APIs.

Detect capabilities instead of inferring them from an operating-system or
compiler name. Use CMake checks such as header, symbol, type-size, compile, and
link probes. A probe should test the complete declaration and link context in
which SyncTERM will use the capability.

Put probe results needed by C into a generated configuration header. Do not
scatter new compiler, architecture, or operating-system preprocessor tests
through otherwise portable source files. Platform checks are appropriate
when selecting an inherently platform-specific implementation, but keep that
selection in CMake and the implementation behind a narrow interface.

Do not assume that Unix-like systems have identical APIs, that all Windows
toolchains expose the same C library, or that compiler identity proves the
availability of a header or function.

## C types and interfaces

- Include the standard header that declares each type or facility used.
- Keep C17 as the required language level, but where practical prefer
  established library facilities standardized by C23 over new project-local
  replacements. For example, use `strdup()` or `strndup()` as the first choice
  for an allocated string copy. Verify the required declaration and link
  availability with a CMake capability probe, and provide only a narrow
  fallback for targets that lack the facility.
- Use `bool` for boolean values.
- Use `size_t` for object sizes and counts, `ptrdiff_t` for pointer
  differences, and `uintptr_t`/`intptr_t` only when an integer must safely
  carry a pointer representation.
- Use `uint8_t`, `int32_t`, and other exact-width types when width is part of a
  file format, network protocol, ABI, hardware interface, bit layout, or
  arithmetic requirement. Do not use exact-width types merely because an old
  typedef happened to have that width on one platform.
- Retain `ssize_t` where an interface needs a signed byte count with a negative
  error result. POSIX supplies it; Windows uses a narrow compatibility typedef
  backed by `SSIZE_T` when the target C library does not declare it.
- Do not introduce new aliases for standard integer or boolean types. The
  `st_threads.h` types are the explicitly approved threading and
  synchronization namespace; do not add a second set of wrappers or aliases.
- Do not mechanically replace types across the tree. Check signedness,
  conversions, format specifiers, sentinel values, overflow behavior, and
  structure layout at each interface.
- Treat serialized structures, protocol fields, plugin boundaries, and public
  data structures as compatibility boundaries. Assert required sizes and
  offsets where useful; encode and decode explicitly when an in-memory layout
  should not be the external format.

## Threads and synchronization

Use the complete C11-equivalent API from `st_threads.h`: `st_thread_t`,
`st_mutex_t`, `st_condition_t`, `st_thread_storage_t`, `st_once_flag_t`, and
their associated functions. This project-owned, namespaced implementation is
intentional: it prevents collisions with a C library's `<threads.h>` symbols
and guarantees which native threading environment creates SyncTERM's threads.
Do not define or alias the standard `thrd_*`, `mtx_*`, `cnd_*`, or `tss_*`
names. Use `<stdatomic.h>` when atomic access, rather than mutual exclusion,
is the actual requirement.

CMake must select the implementation by testing the complete declaration and
link context. Prefer a pthread implementation whenever the target provides
the required pthread contract, including a MinGW environment backed by
winpthreads; otherwise use the Win32 implementation on Windows. If neither is
available, configuration should fail with a clear explanation. Semaphores,
events, and reader/writer locks belong in separate interfaces; those may use
native facilities selected by capability and build on `st_threads.h` only
where a native facility is unavailable.

For every thread, make ownership and shutdown explicit. Define who starts it,
how it is asked to stop, which state is protected by each mutex, what predicate
each condition variable represents, and who joins it. Wait for condition
variables in a predicate loop. Do not use `volatile` as synchronization and
do not replace a mutex-protected invariant with atomics without demonstrating
that the resulting memory ordering is correct.

## Decomposition

- Prefer cohesive libraries with small public headers over `xpdev`-style
  umbrella libraries.
- Give each library split out of `xpdev` its own directory and its own
  `CMakeLists.txt`. The split remains responsibility-driven; do not divide
  related functionality merely to keep source-file counts small.
- A component should own one recognizable responsibility and expose only what
  its consumers need.
- Keep dependencies explicit and directional. Avoid cyclic libraries and
  headers that expose unrelated subsystems.
- Keep platform-neutral policy separate from platform-specific mechanisms.
- Add a compatibility adapter only as a temporary migration boundary with a
  known consumer and removal path, not as a permanent second API.
- Remove an old source from the SyncTERM build once its used functionality has
  migrated and verification covers the replacement. Unused portions need not
  be copied or rewritten.

## Windows DLL boundary

On Windows, SyncTERM is contained in a DLL that implements libretro and backs
the thin console `.com` and Win32 `.exe` launchers. Only declarations that
cross from the DLL into those launchers, plus the libretro ABI declared with
`RETRO_API`, receive import/export decoration. Internal component APIs are not
DLL exports.

## Verification

For each change, configure and build through the SyncTERM CMake project and
run the relevant tests with CTest or the test target defined by CMake. Add
focused tests before changing behavior that is subtle, platform-dependent, or
concurrent. Test both successful operations and shutdown/error paths for
threaded code.

When changing detection logic, verify both the capability-present and
capability-absent paths where practical. Never claim a platform or toolchain
was tested when it was not; state the configurations actually exercised.

Warnings introduced by the change are defects. Keep Windows builds clean with
MSVC and MinGW, and POSIX builds clean with the supported GCC and Clang
toolchains, while avoiding compiler-specific language extensions in portable
C.

## Version control

When requested work is complete and its relevant verification has passed,
commit the complete change without waiting for a separate request. Do not
commit incomplete work merely as a checkpoint. Never push commits or otherwise
publish changes unless explicitly requested.

## Source conventions

When extracting legacy code, preserve its dominant style unless the same
change deliberately modernizes it. Use tabs for C indentation, avoid unrelated
whitespace changes, use bounded string handling, and do not create identifiers
reserved to the C implementation. Comments should explain non-obvious
invariants or constraints, not narrate edit history.
