/* Host-owned callback cleanup, using ordinary Wren fibers. */

/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */

#include "wren_host_internal.h"
#include "wren_value.h"
#include "wren_vm.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

struct st_wren_cleanup {
	struct st_wren_context *context;
	struct st_wren_cleanup *next;
	WrenHandle *function;
	WrenHandle *owner;
	WrenHandle *body;
};

/* Only callbacks explicitly registered by the host library participate.
 * Calling, yielding or transferring an ordinary Fiber is never redirected. */
static const WrenHandle *
cleanup_target(struct st_wren_context *context, const WrenHandle *fiber)
{
	for (;;) {
		const WrenHandle *parent = NULL;

		for (struct st_wren_cleanup *scope = context->cleanups;
		    scope != NULL; scope = scope->next) {
			if (wrenValuesSame(scope->body->value, fiber->value)) {
				parent = scope->owner;
				break;
			}
		}
		if (parent == NULL)
			return fiber;
		fiber = parent;
	}
}

WrenHandle *
st_wren_cleanup_wake_target(WrenVM *vm, WrenHandle *fiber)
{
	return (WrenHandle *)cleanup_target(st_wren_context_from_vm(vm), fiber);
}

bool
st_wren_fibers_same_scope(WrenVM *vm,
	const WrenHandle *left, const WrenHandle *right)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);

	return IS_FIBER(left->value) && IS_FIBER(right->value)
	    && wrenValuesSame(cleanup_target(context, left)->value,
	    cleanup_target(context, right)->value);
}

static bool
cleanup_belongs_to(struct st_wren_context *context,
	const struct st_wren_cleanup *scope, const WrenHandle *fiber)
{
	const WrenHandle *owner = scope->owner;

	for (;;) {
		const WrenHandle *parent = NULL;

		if (wrenValuesSame(owner->value, fiber->value))
			return true;
		for (struct st_wren_cleanup *next = context->cleanups;
		    next != NULL; next = next->next) {
			if (wrenValuesSame(next->body->value, owner->value)) {
				parent = next->owner;
				break;
			}
		}
		if (parent == NULL)
			return false;
		owner = parent;
	}
}

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
	wrenReleaseHandle(context->vm, cleanup->body);
	cleanup->context = NULL;
	cleanup->function = NULL;
	cleanup->owner = NULL;
	cleanup->body = NULL;
}

static void
cleanup_allocate(WrenVM *vm)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);
	WrenHandle *function = wrenGetSlotHandle(vm, 1);
	WrenHandle *body = wrenGetSlotHandle(vm, 2);
	WrenHandle *owner = wrenMakeHandle(vm, OBJ_VAL(vm->fiber));

	if (context->cleanup_closing || !IS_CLOSURE(function->value)
	    || !IS_FIBER(body->value)
	    || wrenValuesSame(cleanup_target(context, body)->value,
	    cleanup_target(context, owner)->value)) {
		wrenReleaseHandle(vm, function);
		wrenReleaseHandle(vm, body);
		wrenReleaseHandle(vm, owner);
		st_wren_abort(vm, "cleanup requires a function and an independent callback fiber");
		return;
	}
	/* A callback has exactly one host driver while it is registered. */
	for (struct st_wren_cleanup *scope = context->cleanups;
	    scope != NULL; scope = scope->next) {
		if (wrenValuesSame(scope->body->value, body->value)) {
			wrenReleaseHandle(vm, function);
			wrenReleaseHandle(vm, body);
			wrenReleaseHandle(vm, owner);
			st_wren_abort(vm, "callback already has a host driver");
			return;
		}
	}
	struct st_wren_cleanup *cleanup = wrenSetSlotNewForeign(vm, 0, 0,
	    sizeof(*cleanup));
	*cleanup = (struct st_wren_cleanup){
		.context = context, .next = context->cleanups, .function = function,
		.owner = owner, .body = body,
	};
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

/* Return child cleanups in reverse entry order, and detach the host's waits.
 * The parked callback stays parked; its Fiber state is not modified. */
static void
cleanup_take_children(WrenVM *vm)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);
	WrenHandle *fiber = wrenGetSlotHandle(vm, 1);
	struct st_wren_cleanup **children;
	size_t capacity = 0;
	size_t count = 0;

	if (!IS_FIBER(fiber->value) || AS_FIBER(fiber->value) == vm->fiber) {
		wrenReleaseHandle(vm, fiber);
		st_wren_abort(vm, "cleanup cancellation requires a parked callback");
		return;
	}
	for (struct st_wren_cleanup *scope = context->cleanups;
	    scope != NULL; scope = scope->next)
		capacity++;
	children = capacity == 0 ? NULL : malloc(capacity * sizeof(*children));
	if (capacity != 0 && children == NULL) {
		wrenReleaseHandle(vm, fiber);
		st_wren_abort(vm, "out of memory cancelling callback");
		return;
	}
	/* Collect before detaching: parent links are needed for membership. */
	for (struct st_wren_cleanup *scope = context->cleanups;
	    scope != NULL; scope = scope->next) {
		if (cleanup_belongs_to(context, scope, fiber))
			children[count++] = scope;
	}
	wrenEnsureSlots(vm, 3);
	wrenSetSlotNewList(vm, 0);
	st_wren_wake_forget_fiber(vm, fiber);
	st_wren_operation_forget_fiber(vm, fiber);
	for (size_t i = 0; i < count; ++i) {
		struct st_wren_cleanup *scope = children[i];

		st_wren_wake_forget_fiber(vm, scope->body);
		st_wren_operation_forget_fiber(vm, scope->body);
		wrenSetSlotHandle(vm, 2, scope->function);
		wrenInsertInList(vm, 0, -1, 2);
		cleanup_detach(scope);
	}
	free(children);
	wrenReleaseHandle(vm, fiber);
}

static void
cleanup_is_current_scope(WrenVM *vm)
{
	WrenHandle *fiber = wrenGetSlotHandle(vm, 1);
	WrenHandle *current = wrenMakeHandle(vm, OBJ_VAL(vm->fiber));
	bool same = st_wren_fibers_same_scope(vm, fiber, current);

	wrenReleaseHandle(vm, current);
	wrenReleaseHandle(vm, fiber);
	wrenSetSlotBool(vm, 0, same);
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
	while (context->cleanups != NULL) {
		struct st_wren_cleanup *cleanup = context->cleanups;

		wrenEnsureSlots(vm, 2);
		wrenGetVariable(vm, "syncterm", "Cleanup_", 0);
		wrenSetSlotHandle(vm, 1, cleanup->function);
		cleanup_detach(cleanup);
		(void)wrenCall(vm, call);
	}
	wrenReleaseHandle(vm, call);
}

WrenForeignMethodFn
st_wren_cleanup_bind_method(const char *class_name, bool is_static,
	const char *signature)
{
	if (strcmp(class_name, "Cleanup_") != 0)
		return NULL;
	if (is_static && strcmp(signature, "isCurrentScope_(_)") == 0)
		return cleanup_is_current_scope;
	if (is_static && strcmp(signature, "takeChildren_(_)") == 0)
		return cleanup_take_children;
	return !is_static && strcmp(signature, "take_()") == 0 ? cleanup_take : NULL;
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
