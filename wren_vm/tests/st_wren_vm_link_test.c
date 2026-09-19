/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "wren.h"

#include <stdlib.h>

/* A consumer of just the VM, without unrelated libraries supplying libm. */
int
main(void)
{
	WrenConfiguration configuration;
	WrenVM *vm;
	WrenInterpretResult result;

	wrenInitConfiguration(&configuration);
	vm = wrenNewVM(&configuration);
	if (vm == NULL)
		return EXIT_FAILURE;
	result = wrenInterpret(vm, "link_test",
	    "if (9.sqrt != 3) Fiber.abort(\"sqrt failed\")\n");
	wrenFreeVM(vm);
	return result == WREN_RESULT_SUCCESS ? EXIT_SUCCESS : EXIT_FAILURE;
}
