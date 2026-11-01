/* Scoped Wren cleanup retained until normal exit or VM shutdown. */

/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */

#include "wren_host_internal.h"
#include "wren_value.h"
#include "wren_vm.h"

#include <string.h>

struct st_wren_cleanup {
	struct st_wren_context *context;
	struct st_wren_cleanup *next;
	WrenHandle *function;
	WrenHandle *owner;
};

static void
cleanup_detach(struct st_wren_cleanup *cleanup)
{
	struct st_wren_context *context = cleanup->context;

	if (context == NULL)
		return;
	for (struct st_wren_cleanup **cursor = &context->cleanups;
	    *cursor != NULL; cursor = &(*cursor)->next) {
		if (*cursor == cleanup) {
			*cursor = cleanup->next;
			break;
		}
	}
	wrenReleaseHandle(context->vm, cleanup->function);
	wrenReleaseHandle(context->vm, cleanup->owner);
	cleanup->context = NULL;
	cleanup->function = NULL;
	cleanup->owner = NULL;
}

static void
cleanup_allocate(WrenVM *vm)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);
	WrenHandle *function = wrenGetSlotHandle(vm, 1);

	if (context->cleanup_closing || !IS_CLOSURE(function->value)) {
		wrenReleaseHandle(vm, function);
		st_wren_abort(vm, "cleanup requires a function and a live VM");
		return;
	}
	struct st_wren_cleanup *cleanup = wrenSetSlotNewForeign(vm, 0, 0,
	    sizeof(*cleanup));
	*cleanup = (struct st_wren_cleanup){
		.context = context, .next = context->cleanups, .function = function,
	};
	/* Retaining the owner keeps both the guard on its stack and the pending
	 * scope alive, even if the script drops every handle while it is parked. */
	cleanup->owner = wrenMakeHandle(vm, OBJ_VAL(vm->fiber));
	context->cleanups = cleanup;
}

static void
cleanup_finalize(void *data)
{
	cleanup_detach(data);
}

static void
cleanup_take(WrenVM *vm)
{
	struct st_wren_cleanup *cleanup = wrenGetSlotForeign(vm, 0);

	if (cleanup->context == NULL) {
		wrenSetSlotNull(vm, 0);
		return;
	}
	wrenSetSlotHandle(vm, 0, cleanup->function);
	cleanup_detach(cleanup);
}

void
st_wren_cleanup_shutdown(WrenVM *vm)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);
	WrenHandle *call;

	if (context->cleanup_closing)
		return;
	context->cleanup_closing = true;
	if (context->cleanups == NULL)
		return;
	call = wrenMakeCallHandle(vm, "runCleanup_(_)");
	/* Do not resume the parked body or its awaiters during teardown. Only
	 * retained cleanup functions run, once each, in reverse entry order. */
	while (context->cleanups != NULL) {
		struct st_wren_cleanup *cleanup = context->cleanups;

		wrenEnsureSlots(vm, 2);
		wrenGetVariable(vm, "syncterm", "Cleanup_", 0);
		wrenSetSlotHandle(vm, 1, cleanup->function);
		cleanup_detach(cleanup);
		/* The trusted wrapper catches an abort and rejects a yield. A failed
		 * cleanup is logged by the VM; older scopes must still restore. */
		(void)wrenCall(vm, call);
	}
	wrenReleaseHandle(vm, call);
}

WrenForeignMethodFn
st_wren_cleanup_bind_method(const char *class_name, bool is_static,
	const char *signature)
{
	return !is_static && strcmp(class_name, "Cleanup_") == 0
	    && strcmp(signature, "take_()") == 0 ? cleanup_take : NULL;
}

WrenForeignClassMethods
st_wren_cleanup_bind_class(const char *class_name)
{
	WrenForeignClassMethods methods = {NULL, NULL};

	if (strcmp(class_name, "Cleanup_") == 0) {
		methods.allocate = cleanup_allocate;
		methods.finalize = cleanup_finalize;
	}
	return methods;
}
