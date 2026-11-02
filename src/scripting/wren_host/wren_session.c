/* Connected-session views and per-owner lifetime leases. */

/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */

#include "wren_host_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

struct session_value {
	struct st_wren_context *context;
};

struct st_wren_session_lease {
	struct st_wren_context *context;
	struct st_wren_session_lease *previous;
	struct st_wren_session_lease *next;
};

static bool
release_lease(struct st_wren_session_lease *lease)
{
	if (lease->context == NULL)
		return false;
	if (lease->previous == NULL)
		lease->context->session_leases = lease->next;
	else
		lease->previous->next = lease->next;
	if (lease->next != NULL)
		lease->next->previous = lease->previous;
	lease->context = NULL;
	lease->previous = NULL;
	lease->next = NULL;
	return true;
}

static void
finalize_lease(void *data)
{
	(void)release_lease(data);
}

static void
allocate_session(WrenVM *vm)
{
	struct session_value *session = wrenSetSlotNewForeign(vm, 0, 0,
	    sizeof(*session));
	*session = (struct session_value){.context = st_wren_context_from_vm(vm)};
}

static void
allocate_lease(WrenVM *vm)
{
	struct st_wren_session_lease *lease = wrenSetSlotNewForeign(vm, 0, 0,
	    sizeof(*lease));
	*lease = (struct st_wren_session_lease){0};
}

struct st_wren_context *
st_wren_session_context(WrenVM *vm)
{
	struct session_value *session = st_wren_foreign_require(vm, 0,
	    "syncterm", "Session");
	struct st_wren_context *context = st_wren_context_from_vm(vm);

	if (session == NULL)
		return NULL;
	if (session->context != context || context->connected == NULL) {
		st_wren_abort(vm, "Session: invalid owner");
		return NULL;
	}
	return context;
}

static void
fn_session_current(WrenVM *vm)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);

	if (context->connected == NULL || context->cleanup_closing) {
		wrenSetSlotNull(vm, 0);
		return;
	}
	if (context->session_value == NULL) {
		allocate_session(vm);
		context->session_value = wrenGetSlotHandle(vm, 0);
	}
	else
		wrenSetSlotHandle(vm, 0, context->session_value);
}

static void
fn_session_hold(WrenVM *vm)
{
	struct st_wren_context *context = st_wren_session_context(vm);
	struct st_wren_session_lease *lease;

	if (context == NULL)
		return;
	if (context->cleanup_closing) {
		st_wren_abort(vm, "Session: session is closing");
		return;
	}
	wrenEnsureSlots(vm, 2);
	wrenGetVariable(vm, "syncterm", "SessionLease", 1);
	lease = wrenSetSlotNewForeign(vm, 0, 1, sizeof(*lease));
	*lease = (struct st_wren_session_lease){
		.context = context, .next = context->session_leases,
	};
	if (lease->next != NULL)
		lease->next->previous = lease;
	context->session_leases = lease;
}

static void
fn_session_hold_count(WrenVM *vm)
{
	struct st_wren_context *context = st_wren_session_context(vm);
	size_t count = 0;

	if (context == NULL)
		return;
	for (struct st_wren_session_lease *lease = context->session_leases;
	    lease != NULL; lease = lease->next)
		++count;
	wrenSetSlotDouble(vm, 0, (double)count);
}

static struct st_wren_session_lease *
lease_value(WrenVM *vm)
{
	struct st_wren_session_lease *lease = st_wren_foreign_require(vm, 0,
	    "syncterm", "SessionLease");

	if (lease != NULL && lease->context != NULL
	    && lease->context != st_wren_context_from_vm(vm)) {
		st_wren_abort(vm, "SessionLease: invalid owner");
		return NULL;
	}
	return lease;
}

static void
fn_lease_release(WrenVM *vm)
{
	struct st_wren_session_lease *lease = lease_value(vm);

	if (lease != NULL)
		wrenSetSlotBool(vm, 0, release_lease(lease));
}

static void
fn_lease_released(WrenVM *vm)
{
	struct st_wren_session_lease *lease = lease_value(vm);

	if (lease != NULL)
		wrenSetSlotBool(vm, 0, lease->context == NULL);
}

void
st_wren_session_release_holds(WrenVM *vm)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);

	while (context->session_leases != NULL)
		(void)release_lease(context->session_leases);
}

void
st_wren_session_object_dispose(WrenVM *vm)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);

	st_wren_session_release_holds(vm);
	if (context->session_value != NULL) {
		wrenReleaseHandle(vm, context->session_value);
		context->session_value = NULL;
	}
}

WrenForeignClassMethods
st_wren_session_object_bind_class(const char *class_name)
{
	if (strcmp(class_name, "Session") == 0)
		return (WrenForeignClassMethods){allocate_session, NULL};
	if (strcmp(class_name, "SessionLease") == 0)
		return (WrenForeignClassMethods){allocate_lease, finalize_lease};
	return (WrenForeignClassMethods){NULL, NULL};
}

WrenForeignMethodFn
st_wren_session_object_bind_method(const char *class_name, bool is_static,
	const char *signature)
{
	if (strcmp(class_name, "Session") == 0) {
		if (is_static && strcmp(signature, "current") == 0)
			return fn_session_current;
		if (!is_static && strcmp(signature, "hold()") == 0)
			return fn_session_hold;
		if (!is_static && strcmp(signature, "holdCount") == 0)
			return fn_session_hold_count;
	}
	else if (!is_static && strcmp(class_name, "SessionLease") == 0) {
		if (strcmp(signature, "release()") == 0)
			return fn_lease_release;
		if (strcmp(signature, "released") == 0)
			return fn_lease_released;
	}
	return NULL;
}
