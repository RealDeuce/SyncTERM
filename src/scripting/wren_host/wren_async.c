/* Owned asynchronous results and operation-aware fiber delivery. */

/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */

#include "wren_host_internal.h"
#include "io_error.h"
#include "wren_vm.h"
#include "wren_value.h"

#include <stdlib.h>
#include <string.h>

struct operation_waiter {
	WrenHandle *fiber;
	struct operation_waiter *next;
};

struct st_wren_operation {
	struct st_wren_context *context;
	/* Pending native work owns this root; completion releases it after
	 * queuing identifiable notifications for all registered waiters. */
	WrenHandle *self;
	WrenHandle *result;
	struct operation_waiter *waiters;
	void (*cancel)(void *, struct st_wren_operation *);
	void *cancel_context;
	bool done;
	bool cancelled;
	bool awaited;
	struct st_wren_operation *next;
};

struct operation_error {
	int code;
	const char *message;
};

static bool
fiber_done(WrenHandle *handle)
{
	ObjFiber *fiber = AS_FIBER(handle->value);

	return fiber->numFrames == 0 || !IS_NULL(fiber->error);
}

static void
release_waiters(WrenVM *vm, struct operation_waiter *waiter)
{
	while (waiter != NULL) {
		struct operation_waiter *next = waiter->next;

		wrenReleaseHandle(vm, waiter->fiber);
		free(waiter);
		waiter = next;
	}
}

static void
finalize_operation(void *data)
{
	struct st_wren_operation *operation = data;
	struct st_wren_context *context = operation->context;

	if (context == NULL)
		return;
	for (struct st_wren_operation **cursor = &context->operations;
	    *cursor != NULL; cursor = &(*cursor)->next) {
		if (*cursor == operation) {
			*cursor = operation->next;
			break;
		}
	}
	if (operation->result != NULL)
		wrenReleaseHandle(context->vm, operation->result);
	release_waiters(context->vm, operation->waiters);
}

struct st_wren_operation *
st_wren_operation_new(WrenVM *vm, int slot)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);
	struct st_wren_operation *operation;

	wrenEnsureSlots(vm, slot + 2);
	wrenGetVariable(vm, "syncterm", "Operation", slot + 1);
	operation = wrenSetSlotNewForeign(vm, slot, slot + 1, sizeof(*operation));
	*operation = (struct st_wren_operation){.context = context};
	operation->self = wrenGetSlotHandle(vm, slot);
	operation->next = context->operations;
	context->operations = operation;
	return operation;
}

void
st_wren_operation_on_cancel(struct st_wren_operation *operation,
	void (*cancel)(void *, struct st_wren_operation *), void *context)
{
	operation->cancel = cancel;
	operation->cancel_context = context;
}

int
st_wren_operation_resolve(WrenVM *vm, struct st_wren_operation *operation,
	int result_slot)
{
	int scratch = result_slot + 1;
	int status = 0;

	if (operation->done)
		return IO_ERROR_CONFLICT;
	operation->result = wrenGetSlotHandle(vm, result_slot);
	operation->done = true;
	operation->cancel = NULL;
	operation->cancel_context = NULL;
	wrenEnsureSlots(vm, scratch + 2);
	wrenSetSlotHandle(vm, scratch, operation->self);
	for (struct operation_waiter *waiter = operation->waiters;
	    waiter != NULL; waiter = waiter->next) {
		if (fiber_done(waiter->fiber))
			continue;
		wrenSetSlotHandle(vm, scratch + 1, waiter->fiber);
		if (st_wren_wake_enqueue(vm, scratch + 1, scratch, operation) != 0)
			status = IO_ERROR_OUT_OF_MEMORY;
	}
	if (status != 0) {
		release_waiters(vm, operation->waiters);
		operation->waiters = NULL;
	}
	wrenReleaseHandle(vm, operation->self);
	operation->self = NULL;
	return status;
}

static void
push_operation_error(WrenVM *vm, int slot, int code, const char *message)
{
	struct operation_error *error;

	wrenEnsureSlots(vm, slot + 2);
	wrenGetVariable(vm, "syncterm", "OperationError", slot + 1);
	error = wrenSetSlotNewForeign(vm, slot, slot + 1, sizeof(*error));
	*error = (struct operation_error){code, message};
}

static void
fn_operation_done(WrenVM *vm)
{
	wrenSetSlotBool(vm, 0, ((struct st_wren_operation *)wrenGetSlotForeign(vm, 0))->done);
}

static void
fn_operation_cancelled(WrenVM *vm)
{
	wrenSetSlotBool(vm, 0, ((struct st_wren_operation *)wrenGetSlotForeign(vm, 0))->cancelled);
}

static void
fn_operation_result(WrenVM *vm)
{
	struct st_wren_operation *operation = wrenGetSlotForeign(vm, 0);

	if (!operation->done) {
		st_wren_abort(vm, "Operation.result requires completion");
		return;
	}
	wrenSetSlotHandle(vm, 0, operation->result);
}

int
st_wren_operation_cancel(WrenVM *vm, struct st_wren_operation *operation)
{
	push_operation_error(vm, 1, IO_ERROR_CANCELLED, "operation cancelled");
	operation->cancelled = true;
	if (operation->cancel != NULL)
		operation->cancel(operation->cancel_context, operation);
	return st_wren_operation_resolve(vm, operation, 1);
}

static void
fn_operation_cancel(WrenVM *vm)
{
	struct st_wren_operation *operation = wrenGetSlotForeign(vm, 0);

	if (operation->done) {
		wrenSetSlotBool(vm, 0, false);
		return;
	}
	if (st_wren_operation_cancel(vm, operation) != 0) {
		st_wren_abort(vm, "out of memory delivering cancellation");
		return;
	}
	wrenSetSlotBool(vm, 0, true);
}

static void
fn_operation_wait(WrenVM *vm)
{
	struct st_wren_operation *operation = wrenGetSlotForeign(vm, 0);
	struct operation_waiter *waiter;
	WrenHandle *fiber = wrenGetSlotHandle(vm, 1);

	if (!IS_FIBER(fiber->value) || AS_FIBER(fiber->value) != vm->fiber) {
		wrenReleaseHandle(vm, fiber);
		st_wren_abort(vm, "Operation.await_ requires the calling fiber");
		return;
	}
	if (operation->done) {
		wrenReleaseHandle(vm, fiber);
		return;
	}
	for (struct st_wren_operation *other = operation->context->operations;
	    other != NULL; other = other->next) {
		if (other == operation)
			continue;
		for (waiter = other->waiters; waiter != NULL; waiter = waiter->next) {
			if (wrenValuesSame(waiter->fiber->value, fiber->value)) {
				wrenReleaseHandle(vm, fiber);
				st_wren_abort(vm, "fiber already awaits another operation");
				return;
			}
		}
	}
	for (waiter = operation->waiters; waiter != NULL; waiter = waiter->next) {
		if (wrenValuesSame(waiter->fiber->value, fiber->value)) {
			wrenReleaseHandle(vm, fiber);
			return;
		}
	}
	waiter = calloc(1, sizeof(*waiter));
	if (waiter == NULL) {
		wrenReleaseHandle(vm, fiber);
		st_wren_abort(vm, "out of memory awaiting operation");
		return;
	}
	waiter->fiber = fiber;
	waiter->next = operation->waiters;
	operation->waiters = waiter;
	operation->awaited = true;
}

/* While await is parked, arbitrary wake values stay in the VM queue. Only
 * this operation's identity may resume that fiber until its wait is removed. */
enum st_wren_wake_action
st_wren_operation_wake_action(WrenVM *vm, WrenHandle *fiber,
	struct st_wren_operation *source)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);

	for (struct st_wren_operation *operation = context->operations;
	    operation != NULL; operation = operation->next) {
		for (struct operation_waiter **cursor = &operation->waiters;
		    *cursor != NULL; cursor = &(*cursor)->next) {
			struct operation_waiter *waiter = *cursor;

			if (!st_wren_fibers_same_scope(vm, waiter->fiber, fiber))
				continue;
			if (source == NULL)
				return ST_WREN_WAKE_DEFER;
			if (source != operation)
				continue;
			*cursor = waiter->next;
			wrenReleaseHandle(vm, waiter->fiber);
			free(waiter);
			return ST_WREN_WAKE_DELIVER;
		}
	}
	return source == NULL ? ST_WREN_WAKE_DELIVER : ST_WREN_WAKE_DISCARD;
}

void
st_wren_operation_forget_fiber(WrenVM *vm, const WrenHandle *fiber)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);

	for (struct st_wren_operation *operation = context->operations;
	    operation != NULL; operation = operation->next) {
		for (struct operation_waiter **cursor = &operation->waiters;
		    *cursor != NULL;) {
			struct operation_waiter *waiter = *cursor;

			if (!wrenValuesSame(waiter->fiber->value, fiber->value)) {
				cursor = &waiter->next;
				continue;
			}
			*cursor = waiter->next;
			wrenReleaseHandle(vm, waiter->fiber);
			free(waiter);
		}
	}
	/* The next normal collect cancels abandoned awaited operations, without
	 * overwriting the caller's API slots during cleanup-list construction. */
}

int
st_wren_operation_collect(WrenVM *vm)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);

	for (struct st_wren_operation *operation = context->operations;
	    operation != NULL; operation = operation->next) {
		for (struct operation_waiter **cursor = &operation->waiters;
		    *cursor != NULL;) {
			struct operation_waiter *waiter = *cursor;

			if (!fiber_done(waiter->fiber)) {
				cursor = &waiter->next;
				continue;
			}
			*cursor = waiter->next;
			wrenReleaseHandle(vm, waiter->fiber);
			free(waiter);
		}
		if (operation->awaited && operation->waiters == NULL && !operation->done) {
			wrenEnsureSlots(vm, 1);
			wrenSetSlotHandle(vm, 0, operation->self);
			if (st_wren_operation_cancel(vm, operation) != 0)
				return IO_ERROR_OUT_OF_MEMORY;
		}
	}
	return 0;
}

int
st_wren_operation_cancel_all(WrenVM *vm)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);

	for (struct st_wren_operation *operation = context->operations;
	    operation != NULL; operation = operation->next) {
		if (operation->done)
			continue;
		wrenEnsureSlots(vm, 1);
		wrenSetSlotHandle(vm, 0, operation->self);
		if (st_wren_operation_cancel(vm, operation) != 0)
			return IO_ERROR_OUT_OF_MEMORY;
	}
	return 0;
}

void
st_wren_operation_dispose(WrenVM *vm)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);

	while (context->operations != NULL) {
		struct st_wren_operation *operation = context->operations;

		context->operations = operation->next;
		if (operation->self != NULL)
			wrenReleaseHandle(vm, operation->self);
		if (operation->result != NULL)
			wrenReleaseHandle(vm, operation->result);
		release_waiters(vm, operation->waiters);
		operation->self = NULL;
		operation->result = NULL;
		operation->waiters = NULL;
		operation->cancel = NULL;
		operation->cancel_context = NULL;
		operation->context = NULL;
	}
}

static void fn_error_code(WrenVM *vm)
{ wrenSetSlotDouble(vm, 0, ((struct operation_error *)wrenGetSlotForeign(vm, 0))->code); }
static void fn_error_message(WrenVM *vm)
{ wrenSetSlotString(vm, 0, ((struct operation_error *)wrenGetSlotForeign(vm, 0))->message); }

WrenForeignMethodFn
st_wren_async_bind_method(const char *class_name, bool is_static,
	const char *signature)
{
	if (is_static)
		return NULL;
	if (strcmp(class_name, "Operation") == 0) {
		if (strcmp(signature, "isDone") == 0) return fn_operation_done;
		if (strcmp(signature, "cancelled") == 0) return fn_operation_cancelled;
		if (strcmp(signature, "result") == 0) return fn_operation_result;
		if (strcmp(signature, "cancel()") == 0) return fn_operation_cancel;
		if (strcmp(signature, "await_(_)") == 0) return fn_operation_wait;
	}
	if (strcmp(class_name, "OperationError") == 0) {
		if (strcmp(signature, "code") == 0) return fn_error_code;
		if (strcmp(signature, "message") == 0
		    || strcmp(signature, "toString") == 0) return fn_error_message;
	}
	return NULL;
}

WrenForeignClassMethods
st_wren_async_bind_class(const char *class_name)
{
	WrenForeignClassMethods methods = {NULL, NULL};

	if (strcmp(class_name, "Operation") == 0) {
		methods.allocate = st_wren_unsupported;
		methods.finalize = finalize_operation;
	}
	else if (strcmp(class_name, "OperationError") == 0)
		methods.allocate = st_wren_unsupported;
	return methods;
}
