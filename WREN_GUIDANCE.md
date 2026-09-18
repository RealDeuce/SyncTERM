# SyncTERM Wren Development Guidance

`Wren.adoc` is the authoritative SyncTERM Wren API manual. This file is a
compact reminder for editing and debugging; consult the manual whenever API
details matter.

## Core syntax

- Newlines terminate statements. There is no `;` token.
- Two statements on one line are a compile error.
- Ternary expressions `cond ? a : b` must stay on one line.
- Use braces when an `else` follows; write `} else {` together.
- Comments are `//` and nested `/* ... */`.
- String interpolation is `"value %(expr)"`.
- Escape a literal percent in a string as `\%`.
- Declare variables with `var`; there are no implicit declarations.
- Inner blocks may shadow outer variables.
- Type checks use `obj is Class`.

## Operators and common precedence traps

- `&&` and `||` short-circuit and return operands rather than coerced Boolean
  values.
- Bitwise operators work on truncated 32-bit integers.
- `..` is a half-open range and `...` is a closed range.
- Ranges bind tighter than method calls; parenthesize a range before chaining.
- Strings do not define ordering operators. Supply an explicit comparator.
- Modulo follows the dividend sign. Use `((x % n) + n) % n` when a positive
  result is required.
- Iterating a `Map` directly yields internal entries. Iterate `map.keys` when
  key values are required.

## Functions, closures, and returns

- There is no top-level `function` keyword. Function values use `Fn.new`.
- Closure parameters use bars: `Fn.new {|a, b| a + b }`.
- A block following a method call supplies its final argument.
- Single-line bodies implicitly return their expression.
- Multi-line bodies return `null` unless they execute an explicit `return`.
- A single-line body containing `return` is a compile error.

## Classes

- Constructors use `construct new(args) { ... }`.
- Inheritance uses `class Child is Parent` and parent construction uses
  `super(args)`.
- Getters are bare methods; setters use `name=(value)`.
- Instance fields begin with `_`; static fields begin with `__`.
- Fields are private to their declaring class. A subclass field with the same
  spelling is a different field, so cross class boundaries through methods.
- A trailing underscore is the project convention for class-private helpers.
- Use `foreign class` only when the host allocates instance data. Namespace
  bindings can be ordinary classes with foreign static methods.

## Fibers and asynchronous work

- Wren is single-threaded and cooperative. It has no `async`, `await`,
  Promise, thread scheduler, or background Wren execution.
- `Fiber.new { ... }` creates a coroutine, `Fiber.yield(value)` returns to the
  immediate caller, and `.try()` catches `Fiber.abort`.
- Host asynchronous APIs use fibers as resume tokens. The compact form
  `SFTP.realpath(Fiber.current, ".") || Fiber.yield()` is safe only when that
  fiber has no other resume source.
- In UI handlers, use `app.runChild(Fn.new { ... })` rather than yielding the
  app run-fiber directly.
- Hooks with return contracts must run synchronously and must not yield to the
  dispatcher.

## Modules, hooks, and persistent state

- Import API symbols explicitly, for example
  `import "syncterm" for Screen, Input, Hook, Key`.
- User library scripts are imported by basename. Auto-loaded entry scripts
  live below `scripts/auto/<event>/`.
- Top-level module code runs once for the relevant VM lifetime. Persistent
  script state belongs in static class fields.
- Hook callbacks run in registration order. Only an explicit `true` consumes
  the event; `false`, other values, and errors pass it through.
- Each connection receives a fresh connected-session VM. Menu and picker VMs
  have their own longer-lived state and their own logical consoles.

## Mistakes to avoid

- Do not add semicolons or JavaScript-style `function` declarations.
- Do not split ternaries across lines.
- Do not assume a multi-line body returns its last expression.
- Do not write a single-line body as `{ return x }`.
- Do not put `else` on a new line after an unbraced one-line `if`.
- Do not put an unescaped literal `%` in a string.
- Do not access inherited fields directly.
- Do not confuse byte indexing with codepoint counts.
- Do not sort strings without an explicit comparator.
- Do not assume negative modulo produces a positive result.
- Do not use `for (key in map)` when actual key values are needed.
- Do not use single-element lists as mutable closure cells; closures can
  reassign captured locals directly.
- Do not yield directly from multi-source UI app handler fibers.
- Do not open `Alert`, `Confirm`, or `Prompt` on an unpumped fresh `App`.
- Do not dismiss a repeating choice owner before an action opens nested UI;
  keep it on the modal stack behind its child.
- Do not use a standalone `App.runSync()` from a streaming or timer hook when
  the UI must outlive that dispatch call.
- C-owned direct `wrenCall` popup bridges use `app.runSync()`, because the
  surrounding C terminal loop cannot dispatch keys during the call.
- Do not call `Screen.modalRun()` in the persistent menu VM because `CTerm` is
  intentionally unavailable there. Menu-owned synchronous child UIs use
  `Screen.save()`, the child call, and `Screen.restore()`.

## Validation

Build and run the relevant CTest targets. When the legacy Wren harness is
applicable, `/synchronet/src/sbbs/src/syncterm/run_wren.sh <script.wren>` can
also drive a headless SyncTERM session and report Wren host failures.
