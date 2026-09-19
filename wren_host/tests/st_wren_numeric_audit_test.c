/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

/* Exercise the actual numeric helpers with sanitizer instrumentation. */
#include "../wren_bind_core.c"

int
main(int argc, char **argv)
{
	struct st_wren_context context = {0};
	WrenConfiguration config;
	WrenVM *vm;
	int result = EXIT_SUCCESS;

	if (argc != 2)
		return EXIT_FAILURE;
	wrenInitConfiguration(&config);
	config.userData = &context;
	vm = wrenNewVM(&config);
	if (vm == NULL)
		return EXIT_FAILURE;
	wrenEnsureSlots(vm, 2);
	if (strcmp(argv[1], "mouse") == 0) {
		/* 37 is the wheel-down event produced by next_input_event. */
		wrenSetSlotDouble(vm, 1, 37);
		fn_input_mouse_event_enable(vm);
	}
	else if (strcmp(argv[1], "integer") == 0) {
		int64_t value = 0;

		wrenSetSlotDouble(vm, 1, 0x1p63);
		if (slot_integer(vm, 1, 0, INT64_MAX - 1, &value)) {
			fprintf(stderr, "FAIL: accepted 2^63 for an int64_t result\n");
			result = EXIT_FAILURE;
		}
	}
	else
		result = EXIT_FAILURE;
	wrenFreeVM(vm);
	return result;
}
