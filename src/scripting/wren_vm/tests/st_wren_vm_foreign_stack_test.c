/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */

#include "wren.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

union allocation_header {
	max_align_t alignment;
	size_t size;
};

/* Moving every allocation makes stale VM stack pointers deterministic. */
static void *
moving_reallocate(void *memory, size_t size, void *context)
{
	union allocation_header *old = memory == NULL ? NULL
	    : (union allocation_header *)memory - 1;
	union allocation_header *next = NULL;
	(void)context;

	if (size != 0) {
		next = malloc(sizeof(*next) + size);
		if (next == NULL)
			abort();
		next->size = size;
		if (old != NULL)
			memcpy(next + 1, memory, size < old->size ? size : old->size);
	}
	free(old);
	return next == NULL ? NULL : next + 1;
}

static void
grow_stack(WrenVM *vm)
{
	wrenEnsureSlots(vm, 4096);
	wrenSetSlotNull(vm, 4095);
	wrenSetSlotDouble(vm, 0, 1234);
}

static void
allocate_growing(WrenVM *vm)
{
	wrenEnsureSlots(vm, 4096);
	wrenSetSlotNull(vm, 4095);
	int *value = wrenSetSlotNewForeign(vm, 0, 0, sizeof(*value));
	*value = 42;
}

static WrenForeignClassMethods
bind_class(WrenVM *vm, const char *module, const char *class_name)
{
	(void)vm;
	WrenForeignClassMethods methods = {NULL, NULL};
	if (strcmp(module, "foreign_constructor") == 0
	    && strcmp(class_name, "Growing") == 0)
		methods.allocate = allocate_growing;
	return methods;
}

static WrenForeignMethodFn
bind_method(WrenVM *vm, const char *module, const char *class_name,
	bool is_static, const char *signature)
{
	(void)vm;
	return strcmp(module, "foreign_stack") == 0
	    && strcmp(class_name, "Host") == 0 && is_static
	    && strcmp(signature, "grow()") == 0 ? grow_stack : NULL;
}

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
	config.reallocateFn = moving_reallocate;
	config.errorFn = report_error;
	config.bindForeignMethodFn = bind_method;
	config.bindForeignClassFn = bind_class;
	WrenVM *vm = wrenNewVM(&config);
	if (vm == NULL)
		return EXIT_FAILURE;
	WrenInterpretResult result = wrenInterpret(vm, "foreign_stack",
	    "class Host { foreign static grow() }\n"
	    "class Probe {\n"
	    "  static run() {\n"
	    "    var value = 7\n"
	    "    var captured = Fn.new { value }\n"
	    "    for (i in 0...3) {\n"
	    "      if (Host.grow() != 1234) Fiber.abort(\"foreign result\")\n"
	    "      value = value + 1\n"
	    "      if (captured.call() != value) Fiber.abort(\"open upvalue\")\n"
	    "    }\n"
	    "    return value\n"
	    "  }\n"
	    "}\n"
	    "if (Probe.run() != 10) Fiber.abort(\"relocated frame\")\n");
	WrenInterpretResult constructor = wrenInterpret(vm, "foreign_constructor",
	    "foreign class Growing { construct new() {} }\n"
	    "class Probe {\n"
	    "  static run() {\n"
	    "    var before = 7\n"
	    "    var object = Growing.new()\n"
	    "    if (before != 7 || object == null) Fiber.abort(\"constructor frame\")\n"
	    "    return before\n"
	    "  }\n"
	    "}\n"
	    "if (Probe.run() != 7) Fiber.abort(\"constructor result\")\n");
	wrenFreeVM(vm);
	return result == WREN_RESULT_SUCCESS && constructor == WREN_RESULT_SUCCESS ? EXIT_SUCCESS : EXIT_FAILURE;
}
