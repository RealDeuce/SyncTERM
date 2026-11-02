/* Runtime identity checks for native Wren objects. */

/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */

#include "wren_host_internal.h"
#include "wren_vm.h"
#include "wren_value.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

bool
st_wren_foreign_is(WrenVM *vm, int slot, const char *module_name,
	const char *class_name)
{
	ObjClass *actual;
	size_t module_length = strlen(module_name);
	size_t class_length = strlen(class_name);

	if (wrenGetSlotType(vm, slot) != WREN_TYPE_FOREIGN)
		return false;
	actual = wrenGetClass(vm, vm->apiStack[slot]);
	if (actual->name->length != class_length
	    || memcmp(actual->name->value, class_name, class_length) != 0)
		return false;
	/* Inspect the already-loaded module without allocating a temporary
	 * String or growing API slots. Validators must preserve constructor
	 * arity and must not invalidate a caller's stack/borrowed values. */
	for (uint32_t i = 0; i < vm->modules->capacity; ++i) {
		MapEntry *entry = &vm->modules->entries[i];
		ObjModule *module;
		int index;

		if (!IS_STRING(entry->key)
		    || AS_STRING(entry->key)->length != module_length
		    || memcmp(AS_CSTRING(entry->key), module_name, module_length) != 0)
			continue;
		module = AS_MODULE(entry->value);
		index = wrenSymbolTableFind(&module->variableNames, class_name,
		    class_length);
		return index >= 0 && IS_CLASS(module->variables.data[index])
		    && actual == AS_CLASS(module->variables.data[index]);
	}
	return false;
}

void *
st_wren_foreign_require(WrenVM *vm, int slot, const char *module_name,
	const char *class_name)
{
	char message[96];

	if (st_wren_foreign_is(vm, slot, module_name, class_name))
		return wrenGetSlotForeign(vm, slot);
	(void)snprintf(message, sizeof(message), "expected a %s", class_name);
	st_wren_abort(vm, message);
	return NULL;
}
