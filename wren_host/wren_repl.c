/* Per-VM console log and immediate-mode evaluation bindings. */
/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "wren_host_internal.h"
#include "st_time.h"
#include "string_compat.h"
/* Like Wren's meta module, compilation returns a closure without re-entering
 * the interpreter. The Wren caller executes it in its own (catchable) fiber. */
#include "wren_vm.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
release_entry(WrenVM *vm, struct st_wren_log_entry *entry)
{
	if (entry->cached != NULL)
		wrenReleaseHandle(vm, entry->cached);
	free(entry->text);
	*entry = (struct st_wren_log_entry){0};
}

static void
clear_log(WrenVM *vm, struct st_wren_log *log)
{
	for (size_t i = 0; i < ST_WREN_LOG_CAPACITY; ++i)
		release_entry(vm, &log->entries[i]);
	log->count = 0;
}

void
st_wren_log_dispose(WrenVM *vm)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);

	clear_log(vm, &context->log);
	clear_log(vm, &context->capture);
	context->capturing = false;
}

void
st_wren_log_append(WrenVM *vm, int source, const char *text)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);
	struct st_wren_log *log = context->capturing
	    ? &context->capture : &context->log;
	uint64_t milliseconds = 0;
	char *copy;
	struct st_wren_log_entry *entry;

	copy = strndup(text, 8192);
	if (copy == NULL)
		return;
	entry = &log->entries[log->total % ST_WREN_LOG_CAPACITY];
	release_entry(vm, entry);
	(void)st_time_monotonic_ms(&milliseconds);
	entry->text = copy;
	entry->timestamp = (double)milliseconds / 1000.0;
	entry->source = source;
	++log->total;
	if (source != 0)
		++log->errors;
	if (log->count < ST_WREN_LOG_CAPACITY)
		++log->count;
}

static void
console_count(WrenVM *vm)
{
	wrenSetSlotDouble(vm, 0, (double)st_wren_context_from_vm(vm)->log.count);
}

void
st_wren_log_error(WrenVM *vm, WrenErrorType type, const char *module,
	int line, const char *message)
{
	char entry[1024];

	if (type == WREN_ERROR_RUNTIME)
		(void)snprintf(entry, sizeof(entry), "%s",
		    message == NULL ? "" : message);
	else
		(void)snprintf(entry, sizeof(entry), "%s:%d%s%s",
		    module == NULL ? "?" : module, line,
		    type == WREN_ERROR_STACK_TRACE ? " in " : ": ",
		    message == NULL ? "" : message);
	st_wren_log_append(vm, type == WREN_ERROR_COMPILE ? 1 :
	    type == WREN_ERROR_RUNTIME ? 2 : 3, entry);
}

static void
console_total(WrenVM *vm)
{
	wrenSetSlotDouble(vm, 0, (double)st_wren_context_from_vm(vm)->log.total);
}

/* Validate before converting: callers can supply NaN, infinity, or fractions. */
static bool
sequence(WrenVM *vm, uint64_t *result)
{
	double value;

	if (wrenGetSlotType(vm, 1) != WREN_TYPE_NUM)
		return false;
	value = wrenGetSlotDouble(vm, 1);
	if (!(value >= 0 && value < (double)UINT64_MAX) || floor(value) != value)
		return false;
	*result = (uint64_t)value;
	return true;
}

static void
console_get(WrenVM *vm)
{
	struct st_wren_log *log = &st_wren_context_from_vm(vm)->log;
	struct st_wren_log_entry *entry;
	uint64_t seq;

	if (!sequence(vm, &seq) || seq < log->total - log->count
	    || seq >= log->total) {
		wrenSetSlotNull(vm, 0);
		return;
	}
	entry = &log->entries[seq % ST_WREN_LOG_CAPACITY];
	if (entry->cached != NULL) {
		wrenSetSlotHandle(vm, 0, entry->cached);
		return;
	}
	wrenEnsureSlots(vm, 2);
	wrenSetSlotNewList(vm, 0);
	wrenSetSlotDouble(vm, 1, entry->timestamp);
	wrenInsertInList(vm, 0, -1, 1);
	wrenSetSlotDouble(vm, 1, entry->source);
	wrenInsertInList(vm, 0, -1, 1);
	wrenSetSlotString(vm, 1, entry->text);
	wrenInsertInList(vm, 0, -1, 1);
	entry->cached = wrenGetSlotHandle(vm, 0);
}

static void
console_iterate(WrenVM *vm)
{
	struct st_wren_log *log = &st_wren_context_from_vm(vm)->log;
	uint64_t seq;

	if (wrenGetSlotType(vm, 1) == WREN_TYPE_NULL)
		seq = log->total - log->count;
	else if (sequence(vm, &seq))
		++seq;
	else {
		wrenSetSlotBool(vm, 0, false);
		return;
	}
	if (seq < log->total - log->count)
		seq = log->total - log->count;
	if (seq < log->total)
		wrenSetSlotDouble(vm, 0, (double)seq);
	else
		wrenSetSlotBool(vm, 0, false);
}

static void
console_clear(WrenVM *vm)
{
	clear_log(vm, &st_wren_context_from_vm(vm)->log);
	wrenSetSlotNull(vm, 0);
}

static void
console_seen(WrenVM *vm)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);

	context->seen_total = context->log.total;
	context->seen_errors = context->log.errors;
	wrenSetSlotNull(vm, 0);
}

static void
log_unread(WrenVM *vm)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);

	wrenSetSlotBool(vm, 0, context->log.total > context->seen_total);
}

static void
log_unread_error(WrenVM *vm)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);

	wrenSetSlotBool(vm, 0, context->log.errors > context->seen_errors);
}

static void
capture_start(WrenVM *vm)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);

	clear_log(vm, &context->capture);
	context->capturing = true;
	wrenSetSlotNull(vm, 0);
}

static void
capture_clear(WrenVM *vm)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);

	clear_log(vm, &context->capture);
	context->capturing = false;
	wrenSetSlotNull(vm, 0);
}

static void
capture_contains(WrenVM *vm)
{
	struct st_wren_log *log = &st_wren_context_from_vm(vm)->capture;
	const char *needle;
	bool found = false;

	if (wrenGetSlotType(vm, 1) != WREN_TYPE_STRING) {
		st_wren_abort(vm, "REPL.captureContains_ expects a String");
		return;
	}
	if (!st_wren_text_slot(vm, 1, &needle, NULL))
		return;
	for (uint64_t i = log->total - log->count; i < log->total; ++i) {
		if (strstr(log->entries[i % ST_WREN_LOG_CAPACITY].text,
		    needle) != NULL)
			found = true;
	}
	wrenSetSlotBool(vm, 0, found);
}

static void
capture_commit(WrenVM *vm)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);
	struct st_wren_log *log = &context->capture;

	context->capturing = false;
	for (uint64_t i = log->total - log->count; i < log->total; ++i) {
		struct st_wren_log_entry *entry =
		    &log->entries[i % ST_WREN_LOG_CAPACITY];
		st_wren_log_append(vm, entry->source, entry->text);
	}
	clear_log(vm, log);
	wrenSetSlotNull(vm, 0);
}

static void
repl_compile(WrenVM *vm)
{
	ObjClosure *closure;
	const char *module;
	const char *source;

	if (wrenGetSlotType(vm, 1) != WREN_TYPE_STRING
	    || wrenGetSlotType(vm, 2) != WREN_TYPE_STRING
	    || wrenGetSlotType(vm, 3) != WREN_TYPE_BOOL
	    || wrenGetSlotType(vm, 4) != WREN_TYPE_BOOL) {
		st_wren_abort(vm, "REPL.compile_ expects two Strings and two Bools");
		return;
	}
	if (!st_wren_text_slot(vm, 1, &module, NULL)
	    || !st_wren_text_slot(vm, 2, &source, NULL))
		return;
	module = wrenGetSlotString(vm, 1);
	closure = wrenCompileSource(vm, module, source, wrenGetSlotBool(vm, 3),
	    wrenGetSlotBool(vm, 4));
	vm->apiStack[0] = closure == NULL ? NULL_VAL : OBJ_VAL(closure);
}

static void
repl_has_module(WrenVM *vm)
{
	const char *module;

	if (wrenGetSlotType(vm, 1) != WREN_TYPE_STRING) {
		st_wren_abort(vm, "REPL.hasModule expects a String");
		return;
	}
	if (!st_wren_text_slot(vm, 1, &module, NULL))
		return;
	wrenSetSlotBool(vm, 0, wrenHasModule(vm, module));
}

static void
repl_modules(WrenVM *vm)
{
	wrenEnsureSlots(vm, 2);
	wrenSetSlotNewList(vm, 0);
	for (uint32_t i = 0; i < vm->modules->capacity; ++i) {
		Value key = vm->modules->entries[i].key;

		if (!IS_STRING(key))
			continue;
		wrenSetSlotString(vm, 1, AS_CSTRING(key));
		wrenInsertInList(vm, 0, -1, 1);
	}
}

static void
repl_trace(WrenVM *vm)
{
	ObjFiber *fiber;

	if (!IS_FIBER(vm->apiStack[1])) {
		st_wren_abort(vm, "REPL.printTrace_ expects a Fiber");
		return;
	}
	fiber = AS_FIBER(vm->apiStack[1]);
	/* A caught error belongs in this VM's log, not the host's fatal-error
	 * state. In particular it must not poison the menu/picker run result. */
	if (!IS_NULL(fiber->error)) {
		st_wren_log_error(vm, WREN_ERROR_RUNTIME, NULL, -1,
		    IS_STRING(fiber->error) ? AS_CSTRING(fiber->error) : "[error object]");
		for (int i = fiber->numFrames - 1; i >= 0; --i) {
			CallFrame *frame = &fiber->frames[i];
			ObjFn *fn = frame->closure->fn;
			if (fn->module == NULL || fn->module->name == NULL)
				continue;
			st_wren_log_error(vm, WREN_ERROR_STACK_TRACE,
			    fn->module->name->value,
			    fn->debug->sourceLines.data[frame->ip - fn->code.data - 1],
			    fn->debug->name);
		}
	}
	wrenSetSlotNull(vm, 0);
}

WrenForeignMethodFn
st_wren_repl_bind_method(const char *class_name, bool is_static,
	const char *signature)
{
	static const struct {
		const char *class_name;
		const char *signature;
		WrenForeignMethodFn method;
	} methods[] = {
		{"Console", "count", console_count},
		{"Console", "total", console_total},
		{"Console", "[_]", console_get},
		{"Console", "iteratorValue(_)", console_get},
		{"Console", "iterate(_)", console_iterate},
		{"Console", "clear()", console_clear},
		{"Console", "markSeen()", console_seen},
		{"Host", "logUnread", log_unread},
		{"Host", "logUnreadError", log_unread_error},
		{"REPL", "compile_(_,_,_,_)", repl_compile},
		{"REPL", "hasModule(_)", repl_has_module},
		{"REPL", "modules", repl_modules},
		{"REPL", "printTrace_(_)", repl_trace},
		{"REPL", "captureStart_()", capture_start},
		{"REPL", "captureClear_()", capture_clear},
		{"REPL", "captureCommit_()", capture_commit},
		{"REPL", "captureContains_(_)", capture_contains},
	};
	if (is_static) {
		for (size_t i = 0; i < sizeof(methods) / sizeof(methods[0]); ++i) {
			if (strcmp(class_name, methods[i].class_name) == 0
			    && strcmp(signature, methods[i].signature) == 0)
				return methods[i].method;
		}
	}
	return NULL;
}
