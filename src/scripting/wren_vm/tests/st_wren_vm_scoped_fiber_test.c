/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */

#include "wren.h"

#include <stdio.h>
#include <stdlib.h>

static void
report_error(WrenVM *vm, WrenErrorType type, const char *module,
	int line, const char *message)
{
	(void)vm;
	(void)type;
	fprintf(stderr, "%s:%d: %s\n", module == NULL ? "?" : module, line, message);
}

int
main(void)
{
	WrenConfiguration config;
	wrenInitConfiguration(&config);
	config.errorFn = report_error;
	config.initialHeapSize = 1024;
	config.minHeapSize = 1024;
	WrenVM *vm = wrenNewVM(&config);
	if (vm == NULL)
		return EXIT_FAILURE;
	WrenInterpretResult result = wrenInterpret(vm, "scoped_fiber",
	    "class Scope {\n"
	    "  static run(fn, cleanup) {\n"
	    "    var body = Fiber.new { fn.call() }\n"
	    "    var result = body.tryScoped()\n"
	    "    cleanup.call()\n"
	    "    if (body.error != null) Fiber.abort(body.error)\n"
	    "    return result\n"
	    "  }\n"
	    "}\n"
	    "var order = []\n"
	    "if (Scope.run(Fn.new { 42 }, Fn.new { order.add(\"return\") }) != 42) Fiber.abort(\"return value\")\n"
	    "var error = [\"identity\"]\n"
	    "var caught = Fiber.new {\n"
	    "  Scope.run(Fn.new { Fiber.abort(error) }, Fn.new { order.add(\"abort\") })\n"
	    "}\n"
	    "caught.try()\n"
	    "if (caught.error != error || order.join(\",\") != \"return,abort\") Fiber.abort(\"immediate cleanup/error identity\")\n"
	    "var leaf = null\n"
	    "var entered = false\n"
	    "var outer = Fiber.new {\n"
	    "  Scope.run(Fn.new {\n"
	    "    Scope.run(Fn.new {\n"
	    "      leaf = Fiber.current\n"
	    "      entered = true\n"
	    "      var first = Fiber.yield(\"first\")\n"
	    "      if (first != \"resume outer\") Fiber.abort(\"outer resume value\")\n"
	    "      System.gc()\n"
	    "      var second = Fiber.yield(\"second\")\n"
	    "      if (second != \"resume leaf\") Fiber.abort(\"leaf resume value\")\n"
	    "      Fiber.abort(error)\n"
	    "    }, Fn.new { order.add(\"inner\") })\n"
	    "  }, Fn.new { order.add(\"outer\") })\n"
	    "}\n"
	    "if (outer.try() != \"first\" || !entered || outer.isDone || leaf.isDone) Fiber.abort(\"yield through scopes\")\n"
	    "if (outer.try(\"resume outer\") != \"second\" || outer.isDone) Fiber.abort(\"resume outer through scopes\")\n"
	    "if (leaf.try(\"resume leaf\") != error || outer.error != error || !outer.isDone || !leaf.isDone) Fiber.abort(\"resume leaf through scopes\")\n"
	    "if (order.join(\",\") != \"return,abort,inner,outer\") Fiber.abort(\"nested LIFO cleanup\")\n"
	    "var done = false\n"
	    "var paused = Fiber.new {\n"
	    "  var answer = Scope.run(Fn.new {\n"
	    "    leaf = Fiber.current\n"
	    "    var value = Fiber.yield()\n"
	    "    if (value != 17) Fiber.abort(\"null yield resume\")\n"
	    "    return 99\n"
	    "  }, Fn.new { done = true })\n"
	    "  if (answer != 99) Fiber.abort(\"deferred result\")\n"
	    "}\n"
	    "if (paused.call() != null || done) Fiber.abort(\"no-value yield\")\n"
	    "leaf.call(17)\n"
	    "if (!done || !paused.isDone) Fiber.abort(\"deferred cleanup\")\n"
	    "// An ordinary child Fiber still yields back to its direct caller.\n"
	    "Scope.run(Fn.new {\n"
	    "  var ordinary = Fiber.new { Fiber.yield(7) }\n"
	    "  if (ordinary.call() != 7) Fiber.abort(\"ordinary child yield\")\n"
	    "}, Fn.new {})\n"
	    "var cancelled = Fiber.new {\n"
	    "  Scope.run(Fn.new { Fiber.yield() }, Fn.new { order.add(\"cancel\") })\n"
	    "}\n"
	    "cancelled.call()\n"
	    "if (cancelled.abortScoped(error) != error || cancelled.error != error || order[-1] != \"cancel\") Fiber.abort(\"cancel scoped cleanup\")\n"
	    "var recovered = Fiber.new {\n"
	    "  var child = Fiber.new { Fiber.yield() }\n"
	    "  child.tryScoped()\n"
	    "  if (child.error != error) Fiber.abort(\"cancel handler error\")\n"
	    "  return 19\n"
	    "}\n"
	    "recovered.call()\n"
	    "if (recovered.abortScoped(error) != 19 || recovered.error != null || !recovered.isDone) Fiber.abort(\"cancel handler result\")\n"
	    "var ordinary = Fiber.new { Fiber.yield() }\n"
	    "ordinary.call()\n"
	    "if (ordinary.abortScoped(error) != error || ordinary.error != error) Fiber.abort(\"cancel ordinary fiber\")\n"
	    "// Returning without yielding detaches links and does not poison reuse.\n"
	    "for (i in 0...100) Scope.run(Fn.new { System.gc() }, Fn.new {})\n"
	    "var bad = Fiber.new {\n"
	    "  Scope.run(Fn.new { Fiber.suspend() }, Fn.new { order.add(\"suspend\") })\n"
	    "}\n"
	    "bad.try()\n"
	    "if (bad.error == null || order[-1] != \"suspend\") Fiber.abort(\"scoped suspend guard\")\n"
	    "bad = Fiber.new {\n"
	    "  Scope.run(Fn.new { Fiber.new {}.transfer() }, Fn.new { order.add(\"transfer\") })\n"
	    "}\n"
	    "bad.try()\n"
	    "if (bad.error == null || order[-1] != \"transfer\") Fiber.abort(\"scoped transfer guard\")\n");
	if (result != WREN_RESULT_SUCCESS) {
		wrenFreeVM(vm);
		return EXIT_FAILURE;
	}
	/* Model a host wake of the innermost body, with only that body rooted.
	 * Its enclosing cleanup and continuation must survive GC and run once. */
	WrenInterpretResult prepared = wrenInterpret(vm, "host_resume",
	    "import \"scoped_fiber\" for Scope\n"
	    "var leaf = null\n"
	    "var finished = false\n"
	    "Fiber.new {\n"
	    "  var value = Scope.run(Fn.new {\n"
	    "    leaf = Fiber.current\n"
	    "    if (Fiber.yield(\"park\") != 23) Fiber.abort(\"host wake value\")\n"
	    "    return 41\n"
	    "  }, Fn.new { finished = true })\n"
	    "  if (value != 41) Fiber.abort(\"host continuation\")\n"
	    "}.call()\n"
	    "System.gc()\n");
	if (prepared != WREN_RESULT_SUCCESS) {
		wrenFreeVM(vm);
		return EXIT_FAILURE;
	}
	wrenEnsureSlots(vm, 2);
	wrenGetVariable(vm, "host_resume", "leaf", 0);
	WrenHandle *leaf = wrenGetSlotHandle(vm, 0);
	WrenHandle *call = wrenMakeCallHandle(vm, "call(_)");
	wrenCollectGarbage(vm);
	wrenSetSlotHandle(vm, 0, leaf);
	wrenSetSlotDouble(vm, 1, 23);
	WrenInterpretResult resumed = wrenCall(vm, call);
	wrenReleaseHandle(vm, call);
	wrenReleaseHandle(vm, leaf);
	WrenInterpretResult checked = wrenInterpret(vm, "host_resume",
	    "if (!finished || !leaf.isDone) Fiber.abort(\"host cleanup\")\n");
	WrenInterpretResult root_prepared = wrenInterpret(vm, "root_scope",
	    "import \"scoped_fiber\" for Scope\n"
	    "var body = null\n"
	    "var cleaned = false\n"
	    "var result = Scope.run(Fn.new {\n"
	    "  body = Fiber.current\n"
	    "  return Fiber.yield(\"to host\")\n"
	    "}, Fn.new { cleaned = true })\n"
	    "if (result != 29 || !cleaned) Fiber.abort(\"root scope resume\")\n");
	wrenEnsureSlots(vm, 2);
	wrenGetVariable(vm, "root_scope", "body", 0);
	call = wrenMakeCallHandle(vm, "call(_)");
	wrenSetSlotDouble(vm, 1, 29);
	WrenInterpretResult root_resumed = wrenCall(vm, call);
	wrenReleaseHandle(vm, call);
	WrenInterpretResult ordinary_prepared = wrenInterpret(vm, "root_yield",
	    "var body = Fiber.current\n"
	    "if (Fiber.yield(\"direct to host\") != 31) Fiber.abort(\"ordinary root resume\")\n");
	wrenEnsureSlots(vm, 2);
	wrenGetVariable(vm, "root_yield", "body", 0);
	call = wrenMakeCallHandle(vm, "call(_)");
	wrenSetSlotDouble(vm, 1, 31);
	WrenInterpretResult ordinary_resumed = wrenCall(vm, call);
	wrenReleaseHandle(vm, call);
	wrenFreeVM(vm);
	return result == WREN_RESULT_SUCCESS && prepared == WREN_RESULT_SUCCESS
	    && resumed == WREN_RESULT_SUCCESS && checked == WREN_RESULT_SUCCESS
	    && root_prepared == WREN_RESULT_SUCCESS && root_resumed == WREN_RESULT_SUCCESS
	    && ordinary_prepared == WREN_RESULT_SUCCESS && ordinary_resumed == WREN_RESULT_SUCCESS
	    ? EXIT_SUCCESS : EXIT_FAILURE;
}
