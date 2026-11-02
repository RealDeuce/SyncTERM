/* Standard Wren Fiber behavior must not depend on SyncTERM host wrappers. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */

#include "wren.h"

#include <stdio.h>

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
	WrenVM *vm;
	int failed = 0;

	wrenInitConfiguration(&config);
	config.errorFn = report_error;
	vm = wrenNewVM(&config);
	if (wrenInterpret(vm, "fiber_contract",
	    "var failure = [\"original error\"]\n"
	    "var child = Fiber.new {\n"
	    "  var received = Fiber.yield(7)\n"
	    "  if (received != 9) Fiber.abort(\"resume value\")\n"
	    "  Fiber.abort(failure)\n"
	    "}\n"
	    "if (child.try() != 7 || child.isDone || child.error != null) Fiber.abort(\"yield result\")\n"
	    "var resumer = Fiber.new { child.call(9) }\n"
	    "if (resumer.try() != failure || child.error != failure || resumer.error != failure) Fiber.abort(\"ordinary try after yield\")\n"
	    "var missing = Fiber.new { Fiber.new {}.tryScoped() }\n"
	    "if (!(missing.try() is String)) Fiber.abort(\"unexpected tryScoped extension\")\n"
	    "missing = Fiber.new { Fiber.new {}.abortScoped(failure) }\n"
	    "if (!(missing.try() is String)) Fiber.abort(\"unexpected abortScoped extension\")\n"
	    "var caller = Fiber.current\n"
	    "var transfer = Fiber.new {\n"
	    "  var value = caller.transfer(11)\n"
	    "  return value\n"
	    "}\n"
	    "if (transfer.transfer() != 11 || transfer.call(17) != 17 || !transfer.isDone) Fiber.abort(\"ordinary transfer\")\n"
	    "var retry = Fiber.new {\n"
	    "  Fiber.yield()\n"
	    "  Fiber.abort(failure)\n"
	    "}\n"
	    "retry.try()\n"
	    "if (retry.try() != failure || retry.error != failure) Fiber.abort(\"try on each resume\")\n"
	    "class Suspension {\n"
	    "  static value { __value }\n"
	    "  static value=(value) { __value = value }\n"
	    "}\n"
	    "var sleeper = Fiber.new {\n"
	    "  Suspension.value = 1\n"
	    "  Fiber.suspend()\n"
	    "  Suspension.value = 2\n"
	    "}\n"
	    "sleeper.call()\n"
	    "Suspension.value = 3\n") != WREN_RESULT_SUCCESS)
		failed = 1;
	if (!failed && wrenInterpret(vm, "check_suspended",
	    "import \"fiber_contract\" for Suspension\n"
	    "if (Suspension.value != 1) Fiber.abort(\"suspend must return to host\")\n")
	    != WREN_RESULT_SUCCESS)
		failed = 1;
	if (!failed && wrenInterpret(vm, "fiber_contract", "sleeper.transfer()\n")
	    != WREN_RESULT_SUCCESS)
		failed = 1;
	if (!failed && wrenInterpret(vm, "check_resumed",
	    "import \"fiber_contract\" for Suspension\n"
	    "if (Suspension.value != 3) Fiber.abort(\"suspend retains ordinary caller\")\n")
	    != WREN_RESULT_SUCCESS)
		failed = 1;
	wrenFreeVM(vm);
	return failed;
}
