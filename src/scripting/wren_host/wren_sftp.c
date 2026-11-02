/* Provider-neutral connected Wren and asynchronous SFTP bindings. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "wren_host_internal.h"

#include "io_error.h"
#include "st_profile.h"
#include "st_connection.h"
#include "st_time.h"
#include "st_unicode_text.h"
#include "st_utf8.h"
#include "string_compat.h"
#include "regexp.h"

#include <errno.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct pending_request {
	uint64_t id;
	struct st_wren_operation *operation;
	struct pending_request *next;
};

struct pending_timer {
	uint64_t deadline;
	struct st_wren_operation *operation;
	struct pending_timer *next;
};

struct connected_hook {
	size_t references;
	WrenHandle *function;
	int kind;
	int filter;
	uint32_t modifiers;
	uint64_t interval_ms;
	uint64_t next_deadline_ms;
	uint64_t call_count;
	double total_runtime;
	double min_runtime;
	double max_runtime;
	Prog *regex_prog;
	PikeVM *regex_vm;
	char *regex_buf;
	size_t regex_buf_len;
	size_t regex_pos;
	int regex_groups;
	bool regex_clean;
	struct connected_hook *next;
};

struct foreign_hook_handle {
	struct connected_hook *hook;
};

struct st_wren_connected {
	struct st_wren_context context;
	struct st_wren_host *host;
	WrenVM *vm;
	struct st_profile profile;
	struct st_sftp_provider provider;
	struct pending_request *requests;
	struct pending_timer *timers;
	struct connected_hook *hooks;
	size_t hook_dispatch_depth;
	FILE *capture_stream;
	bool capture_raw;
	bool capture_paused;
	char last_error[512];
	bool shutting_down;
};

#define ST_SFTP_HANDLE_MAGIC UINT32_C(0x53544648)
#define ST_WREN_REGEX_BUFFER_SIZE 4096
#define ST_WREN_HOOK_MATCH 9

static jmp_buf re1_jump;
static char re1_error[256];

static void
trap_re1_fatal(const char *message)
{
	(void)snprintf(re1_error, sizeof(re1_error), "%s", message);
	longjmp(re1_jump, 1);
}

static void
free_regexp(Regexp *regexp)
{
	if (regexp == NULL)
		return;
	free_regexp(regexp->left);
	free_regexp(regexp->right);
	free(regexp);
}

static void
free_regex_hook(struct connected_hook *hook)
{
	pikevm_free(hook->regex_vm);
	free(hook->regex_prog);
	free(hook->regex_buf);
	hook->regex_vm = NULL;
	hook->regex_prog = NULL;
	hook->regex_buf = NULL;
}

/* The dispatch list and the foreign handle own independent references. */
static void
release_hook(struct connected_hook *hook)
{
	if (hook != NULL && --hook->references == 0) {
		free_regex_hook(hook);
		free(hook);
	}
}

static void
collect_removed_hooks(struct st_wren_connected *connected)
{
	struct connected_hook **cursor = &connected->hooks;

	/* A callback can register/remove hooks while a dispatcher or matcher
	 * still holds pointers into this list. Reclaim only outside that call. */
	if (connected->hook_dispatch_depth != 0)
		return;
	while (*cursor != NULL) {
		struct connected_hook *hook = *cursor;

		if (hook->function != NULL) {
			cursor = &hook->next;
			continue;
		}
		*cursor = hook->next;
		hook->next = NULL;
		free_regex_hook(hook);
		release_hook(hook);
	}
}

static WrenInterpretResult
connected_call(struct st_wren_connected *connected, WrenHandle *call)
{
	WrenInterpretResult result;

	++connected->hook_dispatch_depth;
	result = wrenCall(connected->vm, call);
	--connected->hook_dispatch_depth;
	return result;
}

static void
finalize_hook_handle(void *data)
{
	struct foreign_hook_handle *handle = data;

	release_hook(handle->hook);
}

struct foreign_sftp_handle {
	uint32_t magic;
	struct st_wren_connected *connected;
	uint64_t value;
	bool active;
};
struct foreign_sftp_stat { struct st_sftp_stat value; };
struct foreign_sftp_entry {
	char *name;
	char *long_name;
	char *hash;
	size_t hash_size;
	struct st_sftp_stat stat;
	bool directory;
	bool has_long_description;
};
struct foreign_sftp_error {
	int code;
	uint32_t server_status;
	char *message;
	bool transient;
};

struct sftp_binding {
	const char *class_name;
	bool is_static;
	const char *signature;
	WrenForeignMethodFn method;
};

static struct st_wren_connected *
connected_from_vm(WrenVM *vm)
{
	return st_wren_context_from_vm(vm)->connected;
}

static void
allocate_hook_handle(WrenVM *vm)
{
	struct foreign_hook_handle *value = wrenSetSlotNewForeign(vm, 0, 0,
	    sizeof(*value));
	*value = (struct foreign_hook_handle){0};
}

static struct connected_hook *
hook_from_handle(WrenVM *vm)
{
	struct foreign_hook_handle *value = wrenGetSlotForeign(vm, 0);
	return value == NULL ? NULL : value->hook;
}

static void
fn_hook_remove(WrenVM *vm)
{
	struct connected_hook *hook = hook_from_handle(vm);

	bool removed = hook != NULL && hook->function != NULL;

	if (removed) {
		wrenReleaseHandle(vm, hook->function);
		hook->function = NULL;
	}
	wrenSetSlotBool(vm, 0, removed);
}

static void fn_hook_call_count(WrenVM *vm)
{
	struct connected_hook *hook = hook_from_handle(vm);
	wrenSetSlotDouble(vm, 0, hook == NULL
	    ? 0 : (double)hook->call_count);
}
static void fn_hook_total_runtime(WrenVM *vm)
{
	struct connected_hook *hook = hook_from_handle(vm);
	wrenSetSlotDouble(vm, 0, hook == NULL
	    ? 0 : hook->total_runtime);
}
static void fn_hook_min_runtime(WrenVM *vm)
{
	struct connected_hook *hook = hook_from_handle(vm);
	wrenSetSlotDouble(vm, 0, hook == NULL
	    ? 0 : hook->min_runtime);
}
static void fn_hook_max_runtime(WrenVM *vm)
{
	struct connected_hook *hook = hook_from_handle(vm);
	wrenSetSlotDouble(vm, 0, hook == NULL
	    ? 0 : hook->max_runtime);
}
static void fn_hook_to_string(WrenVM *vm)
{
	struct connected_hook *hook = hook_from_handle(vm);
	wrenSetSlotString(vm, 0, hook == NULL || hook->function == NULL
	    ? "HookHandle(removed)" : "HookHandle(active)");
}

static void
record_hook_runtime(struct connected_hook *hook, uint64_t before)
{
	uint64_t after;
	double elapsed = 0;

	if (before != 0 && st_time_monotonic_ms(&after) == 0 && after >= before)
		elapsed = (double)(after - before) / 1000.0;
	if (hook->call_count == 0 || elapsed < hook->min_runtime)
		hook->min_runtime = elapsed;
	if (elapsed > hook->max_runtime)
		hook->max_runtime = elapsed;
	hook->total_runtime += elapsed;
	hook->call_count++;
}

static void
warn_observe_return(WrenVM *vm, const char *name)
{
	WrenType type = wrenGetSlotType(vm, 0);
	char message[160];

	if (type != WREN_TYPE_BOOL && type != WREN_TYPE_STRING)
		return;
	(void)snprintf(message, sizeof(message),
	    "Hook.%s returned %s; observe-only return is ignored",
	    name, type == WREN_TYPE_BOOL ? "Bool" : "String");
	st_wren_log_append(vm, 2, message);
}

static void
allocate_handle(WrenVM *vm)
{
	struct foreign_sftp_handle *value = wrenSetSlotNewForeign(vm, 0, 0,
	    sizeof(*value));
	*value = (struct foreign_sftp_handle){
		.magic = ST_SFTP_HANDLE_MAGIC,
	};
}

static void
finalize_handle(void *data)
{
	struct foreign_sftp_handle *handle = data;
	struct st_sftp_request request;
	uint64_t request_id;

	if (handle->magic == ST_SFTP_HANDLE_MAGIC && handle->active
	    && handle->connected != NULL && !handle->connected->shutting_down
	    && handle->connected->provider.submit != NULL) {
		request = (struct st_sftp_request){
			.operation = ST_SFTP_CLOSE,
			.handle = handle->value,
		};
		(void)handle->connected->provider.submit(
		    handle->connected->provider.context, &request, &request_id);
	}
	handle->active = false;
	handle->connected = NULL;
	handle->magic = 0;
}

static void
allocate_stat(WrenVM *vm)
{
	struct foreign_sftp_stat *value = wrenSetSlotNewForeign(vm, 0, 0,
	    sizeof(*value));
	*value = (struct foreign_sftp_stat){0};
}

static void
allocate_entry(WrenVM *vm)
{
	struct foreign_sftp_entry *value = wrenSetSlotNewForeign(vm, 0, 0,
	    sizeof(*value));
	*value = (struct foreign_sftp_entry){0};
}

static void
finalize_entry(void *data)
{
	struct foreign_sftp_entry *entry = data;
	free(entry->name);
	free(entry->long_name);
	free(entry->hash);
}

static void
allocate_error(WrenVM *vm)
{
	struct foreign_sftp_error *value = wrenSetSlotNewForeign(vm, 0, 0,
	    sizeof(*value));
	*value = (struct foreign_sftp_error){0};
}

static void
finalize_error(void *data)
{
	struct foreign_sftp_error *error = data;
	free(error->message);
}

static bool
slot_path(WrenVM *vm, int slot, const char **result)
{
	int size;
	const char *value;

	if (!st_wren_text_slot(vm, slot, &value, &size))
		return false;
	if (size < 0 || memchr(value, '\0', (size_t)size) != NULL)
		return false;
	*result = value;
	return true;
}

static bool
slot_handle(WrenVM *vm, int slot, uint64_t *result)
{
	struct foreign_sftp_handle *handle;

	if (!st_wren_foreign_is(vm, slot, "syncterm", "SFTPHandle"))
		return false;
	handle = wrenGetSlotForeign(vm, slot);
	if (handle == NULL || handle->magic != ST_SFTP_HANDLE_MAGIC
	    || handle->connected != connected_from_vm(vm) || !handle->active)
		return false;
	*result = handle->value;
	return true;
}

static void
push_error(WrenVM *vm, int slot, int code, uint32_t server_status,
	const char *message, bool transient)
{
	struct foreign_sftp_error *error;

	wrenEnsureSlots(vm, slot + 2);
	wrenGetVariable(vm, "syncterm", "SFTPError", slot + 1);
	error = wrenSetSlotNewForeign(vm, slot, slot + 1, sizeof(*error));
	*error = (struct foreign_sftp_error){
		.code = code,
		.server_status = server_status,
		.message = message == NULL ? NULL : strdup(message),
		.transient = transient,
	};
}

static void
cancel_native_operation(void *context, struct st_wren_operation *operation)
{
	struct st_wren_connected *connected = context;

	for (struct pending_request *request = connected->requests;
	    request != NULL; request = request->next) {
		if (request->operation == operation) {
			/* The provider cannot revoke one submitted request. Keep its
			 * ID until the reply arrives, but release script ownership. */
			request->operation = NULL;
			return;
		}
	}
	for (struct pending_timer **cursor = &connected->timers;
	    *cursor != NULL; cursor = &(*cursor)->next) {
		if ((*cursor)->operation == operation) {
			struct pending_timer *timer = *cursor;

			*cursor = timer->next;
			free(timer);
			return;
		}
	}
}

static bool
request_error(WrenVM *vm, int code, const char *message)
{
	struct st_wren_operation *operation = st_wren_operation_new(vm, 0);

	push_error(vm, 1, code, 0, message, true);
	(void)st_wren_operation_resolve(vm, operation, 1);
	return false;
}

static bool
submit_request(WrenVM *vm, const struct st_sftp_request *request)
{
	struct st_wren_connected *connected = connected_from_vm(vm);
	struct pending_request *pending;
	uint64_t id;
	int status;
	size_t count = 0;

	if (connected == NULL || connected->shutting_down
	    || connected->provider.submit == NULL)
		return request_error(vm, IO_ERROR_NOT_SUPPORTED, "SFTP is unavailable");
	for (struct pending_request *other = connected->requests; other != NULL;
	    other = other->next)
		++count;
	if (count >= 256)
		return request_error(vm, IO_ERROR_WOULD_BLOCK, "too many pending SFTP requests");
	pending = calloc(1, sizeof(*pending));
	if (pending == NULL)
		return request_error(vm, IO_ERROR_OUT_OF_MEMORY, "Out of memory");
	/* Submit copies all borrowed arguments before creating the result object
	 * reuses argument slots or triggers garbage collection. */
	status = connected->provider.submit(connected->provider.context, request, &id);
	if (status != 0) {
		free(pending);
		return request_error(vm, status, "SFTP request was not queued");
	}
	for (struct pending_request *other = connected->requests; other != NULL;
	    other = other->next) {
		if (other->id == id) {
			free(pending);
			return request_error(vm, IO_ERROR_CONFLICT,
			    "SFTP provider reused a pending request ID");
		}
	}
	pending->id = id;
	pending->operation = st_wren_operation_new(vm, 0);
	st_wren_operation_on_cancel(pending->operation, cancel_native_operation, connected);
	pending->next = connected->requests;
	connected->requests = pending;
	return true;
}

static void
fn_sftp_available(WrenVM *vm)
{
	struct st_wren_connected *connected = connected_from_vm(vm);
	bool available = connected != NULL && !connected->shutting_down
	    && connected->provider.available != NULL
	    && connected->provider.available(connected->provider.context);
	wrenSetSlotBool(vm, 0, available);
}

static void
provider_string(WrenVM *vm, const char *(*getter)(void *))
{
	struct st_wren_connected *connected = connected_from_vm(vm);
	const char *value = connected == NULL || getter == NULL ? NULL
	    : getter(connected->provider.context);
	if (value == NULL)
		wrenSetSlotNull(vm, 0);
	else
		wrenSetSlotString(vm, 0, value);
}

static void fn_sftp_pubdir(WrenVM *vm)
{
	struct st_wren_connected *c = connected_from_vm(vm);
	provider_string(vm, c == NULL ? NULL : c->provider.public_directory);
}
static void fn_sftp_lname(WrenVM *vm)
{
	struct st_wren_connected *c = connected_from_vm(vm);
	wrenSetSlotBool(vm, 0, c != NULL && c->provider.long_name_extension != NULL
	    && c->provider.long_name_extension(c->provider.context));
}

static void fn_sftp_start(WrenVM *vm)
{
	const struct st_sftp_request request = {.operation = ST_SFTP_START};
	(void)submit_request(vm, &request);
}

static void fn_host_ssh_public_key(WrenVM *vm)
{
	char *key = st_connection_ssh_public_key();
	char *blob = key == NULL ? NULL : strchr(key, ' ');
	if (blob == NULL) { free(key); wrenSetSlotNull(vm, 0); return; }
	*blob++ = '\0';
	char *end = strpbrk(blob, " \r\n");
	if (end != NULL) *end = '\0';
	wrenEnsureSlots(vm, 3);
	wrenSetSlotNewMap(vm, 0);
	wrenSetSlotString(vm, 1, "algo");
	wrenSetSlotString(vm, 2, key);
	wrenSetMapValue(vm, 0, 1, 2);
	wrenSetSlotString(vm, 1, "blob");
	wrenSetSlotString(vm, 2, blob);
	wrenSetMapValue(vm, 0, 1, 2);
	free(key);
}

static bool
begin_path_request(WrenVM *vm, enum st_sftp_operation operation,
	int path_slot)
{
	struct st_sftp_request request = {.operation = operation};

	if (!slot_path(vm, path_slot, &request.path)) {
		st_wren_abort(vm, "SFTP: expected a path String without NUL");
		return false;
	}
	return submit_request(vm, &request);
}

static void fn_sftp_realpath(WrenVM *vm)
{ (void)begin_path_request(vm, ST_SFTP_REALPATH, 1); }
static void fn_sftp_stat(WrenVM *vm)
{ (void)begin_path_request(vm, ST_SFTP_STAT, 1); }
static void fn_sftp_opendir(WrenVM *vm)
{ (void)begin_path_request(vm, ST_SFTP_OPENDIR, 1); }
static void fn_sftp_mkdir(WrenVM *vm)
{ (void)begin_path_request(vm, ST_SFTP_MKDIR, 1); }
static void fn_sftp_rmdir(WrenVM *vm)
{ (void)begin_path_request(vm, ST_SFTP_RMDIR, 1); }
static void fn_sftp_remove(WrenVM *vm)
{ (void)begin_path_request(vm, ST_SFTP_REMOVE, 1); }
static void fn_sftp_descs(WrenVM *vm)
{ (void)begin_path_request(vm, ST_SFTP_DESCRIPTIONS, 1); }

static void
fn_sftp_open(WrenVM *vm)
{
	struct st_sftp_request request = {.operation = ST_SFTP_OPEN};
	uint64_t flags;

	if (!slot_path(vm, 1, &request.path) || !st_wren_number_u64(vm, 2, &flags)
	    || flags > UINT32_MAX) {
		st_wren_abort(vm, "SFTP.open: invalid path or flags");
		return;
	}
	request.flags = (uint32_t)flags;
	(void)submit_request(vm, &request);
}

static void
fn_sftp_handle_only(WrenVM *vm, enum st_sftp_operation operation)
{
	struct st_sftp_request request = {.operation = operation};
	struct foreign_sftp_handle *handle;
	WrenHandle *keep_alive;

	if (!slot_handle(vm, 1, &request.handle)) {
		st_wren_abort(vm, "SFTP: invalid handle");
		return;
	}
	handle = wrenGetSlotForeign(vm, 1);
	keep_alive = wrenGetSlotHandle(vm, 1);
	if (submit_request(vm, &request) && operation == ST_SFTP_CLOSE)
		handle->active = false;
	wrenReleaseHandle(vm, keep_alive);
}
static void fn_sftp_readdir(WrenVM *vm)
{ fn_sftp_handle_only(vm, ST_SFTP_READDIR); }
static void fn_sftp_close(WrenVM *vm)
{ fn_sftp_handle_only(vm, ST_SFTP_CLOSE); }

static void
fn_sftp_read(WrenVM *vm)
{
	struct st_sftp_request request = {.operation = ST_SFTP_READ};
	uint64_t count;

	if (!slot_handle(vm, 1, &request.handle)
	    || !st_wren_number_u64(vm, 2, &count) || count > SIZE_MAX
	    || count > UINT32_MAX
	    || !st_wren_number_u64(vm, 3, &request.offset)) {
		st_wren_abort(vm, "SFTP.read: invalid handle, count, or offset");
		return;
	}
	request.size = (size_t)count;
	(void)submit_request(vm, &request);
}

static void
fn_sftp_write(WrenVM *vm)
{
	struct st_sftp_request request = {.operation = ST_SFTP_WRITE};
	int size;

	if (!slot_handle(vm, 1, &request.handle)
	    || !st_wren_number_u64(vm, 2, &request.offset)
	    || wrenGetSlotType(vm, 3) != WREN_TYPE_STRING) {
		st_wren_abort(vm, "SFTP.write: invalid handle, offset, or bytes");
		return;
	}
	request.data = wrenGetSlotBytes(vm, 3, &size);
	if (size < 0) {
		st_wren_abort(vm, "SFTP.write: invalid bytes");
		return;
	}
	request.size = (size_t)size;
	(void)submit_request(vm, &request);
}

static void
fn_sftp_rename(WrenVM *vm)
{
	struct st_sftp_request request = {.operation = ST_SFTP_RENAME};

	if (!slot_path(vm, 1, &request.path)
	    || !slot_path(vm, 2, &request.second_path)) {
		st_wren_abort(vm, "SFTP.rename: invalid path");
		return;
	}
	(void)submit_request(vm, &request);
}

static void
fn_sftp_set_mtime(WrenVM *vm)
{
	struct st_sftp_request request = {
		.operation = ST_SFTP_SET_MTIME,
	};

	if (!slot_path(vm, 1, &request.path)
	    || !st_wren_number_u64(vm, 2, &request.modified_time)
	    || request.modified_time > UINT32_MAX) {
		st_wren_abort(vm, "SFTP.setMtime: invalid path or time");
		return;
	}
	(void)submit_request(vm, &request);
}

static struct foreign_sftp_stat *
stat_value(WrenVM *vm)
{
	return wrenGetSlotForeign(vm, 0);
}
static void fn_stat_size(WrenVM *vm)
{ st_wren_return_u64(vm, stat_value(vm)->value.size); }
static void fn_stat_mtime(WrenVM *vm)
{ st_wren_return_i64(vm, stat_value(vm)->value.modified_time); }
static void fn_stat_atime(WrenVM *vm)
{ st_wren_return_i64(vm, stat_value(vm)->value.accessed_time); }
static void fn_stat_mode(WrenVM *vm)
{ wrenSetSlotDouble(vm, 0, stat_value(vm)->value.mode); }
static void fn_stat_uid(WrenVM *vm)
{ wrenSetSlotDouble(vm, 0, stat_value(vm)->value.uid); }
static void fn_stat_gid(WrenVM *vm)
{ wrenSetSlotDouble(vm, 0, stat_value(vm)->value.gid); }
static void fn_stat_string(WrenVM *vm)
{
	char value[96];
	const struct st_sftp_stat *s = &stat_value(vm)->value;
	(void)snprintf(value, sizeof(value), "SFTPStat(size=%llu, mode=0%o, mtime=%lld)",
	    (unsigned long long)s->size, (unsigned)s->mode, (long long)s->modified_time);
	wrenSetSlotString(vm, 0, value);
}

static struct foreign_sftp_entry *
entry_value(WrenVM *vm) { return wrenGetSlotForeign(vm, 0); }
static void fn_entry_name(WrenVM *vm)
{ wrenSetSlotString(vm, 0, entry_value(vm)->name); }
static void fn_entry_long_name(WrenVM *vm)
{
	if (entry_value(vm)->long_name == NULL) wrenSetSlotNull(vm, 0);
	else wrenSetSlotString(vm, 0, entry_value(vm)->long_name);
}
static void fn_entry_size(WrenVM *vm)
{ st_wren_return_u64(vm, entry_value(vm)->stat.size); }
static void fn_entry_mtime(WrenVM *vm)
{ st_wren_return_i64(vm, entry_value(vm)->stat.modified_time); }
static void fn_entry_directory(WrenVM *vm)
{ wrenSetSlotBool(vm, 0, entry_value(vm)->directory); }
static void fn_entry_long_desc(WrenVM *vm)
{ wrenSetSlotBool(vm, 0, entry_value(vm)->has_long_description); }
static void fn_entry_hash(WrenVM *vm)
{
	struct foreign_sftp_entry *entry = entry_value(vm);
	if (entry->hash == NULL) wrenSetSlotNull(vm, 0);
	else wrenSetSlotBytes(vm, 0, entry->hash, entry->hash_size);
}
static void fn_entry_string(WrenVM *vm)
{
	struct foreign_sftp_entry *e = entry_value(vm);
	wrenSetSlotString(vm, 0, e->long_name != NULL && e->long_name[0] != '\0'
	    ? e->long_name : e->name != NULL && e->name[0] != '\0' ? e->name : "SFTPEntry");
}

static struct foreign_sftp_error *
error_value(WrenVM *vm) { return wrenGetSlotForeign(vm, 0); }
static void fn_error_code(WrenVM *vm)
{ wrenSetSlotDouble(vm, 0, error_value(vm)->code); }
static void fn_error_status(WrenVM *vm)
{ wrenSetSlotDouble(vm, 0, error_value(vm)->server_status); }
static void fn_error_message(WrenVM *vm)
{
	if (error_value(vm)->message == NULL) wrenSetSlotNull(vm, 0);
	else wrenSetSlotString(vm, 0, error_value(vm)->message);
}
static void fn_error_transient(WrenVM *vm)
{ wrenSetSlotBool(vm, 0, error_value(vm)->transient); }
static void fn_error_string(WrenVM *vm)
{
	struct foreign_sftp_error *error = error_value(vm);
	char value[256];
	const char *name = st_sftp_error_name(error->code, error->server_status);
	(void)snprintf(value, sizeof(value), "SFTPError: %s%s%s", name,
	    error->message == NULL || error->message[0] == '\0' ? "" : ": ",
	    error->message == NULL ? "" : error->message);
	wrenSetSlotString(vm, 0, value);
}

static void
fn_timer_after(WrenVM *vm)
{
	struct st_wren_connected *connected = connected_from_vm(vm);
	struct pending_timer *timer;
	uint64_t milliseconds, now;
	size_t count = 0;

	if (connected == NULL || connected->shutting_down
	    || !st_wren_number_u64(vm, 1, &milliseconds) || milliseconds > UINT32_MAX
	    || st_time_monotonic_ms(&now) != 0) {
		st_wren_abort(vm, "Timer.after requires a live session and integer delay in 0..4294967295");
		return;
	}
	for (timer = connected->timers; timer != NULL; timer = timer->next)
		++count;
	if (count >= 32) {
		st_wren_abort(vm, "Timer.after: 32 timers already pending");
		return;
	}
	timer = calloc(1, sizeof(*timer));
	if (timer == NULL) {
		st_wren_abort(vm, "Timer.after: out of memory");
		return;
	}
	timer->operation = st_wren_operation_new(vm, 0);
	st_wren_operation_on_cancel(timer->operation, cancel_native_operation, connected);
	timer->deadline = UINT64_MAX - now < milliseconds ? UINT64_MAX : now + milliseconds;
	timer->next = connected->timers;
	connected->timers = timer;
}
static struct connected_hook *
register_hook(WrenVM *vm, int function_slot, int kind, int filter)
{
	struct st_wren_connected *connected = connected_from_vm(vm);
	struct connected_hook *hook;
	size_t count = 0;

	if (connected == NULL) {
		wrenSetSlotNull(vm, 0);
		return NULL;
	}
	collect_removed_hooks(connected);
	for (struct connected_hook *existing = connected->hooks;
	    existing != NULL; existing = existing->next) {
		if (existing->function != NULL && (existing->kind == kind || ((kind == 8
		    || kind == ST_WREN_HOOK_MATCH)
		    && (existing->kind == 8
		    || existing->kind == ST_WREN_HOOK_MATCH))))
			++count;
	}
	if (count >= 256) {
		wrenSetSlotNull(vm, 0);
		return NULL;
	}
	hook = calloc(1, sizeof(*hook));
	if (hook == NULL) {
		st_wren_abort(vm, "Hook registration failed");
		return NULL;
	}
	hook->function = wrenGetSlotHandle(vm, function_slot);
	if (hook->function == NULL) {
		free(hook);
		st_wren_abort(vm, "Hook registration failed");
		return NULL;
	}
	hook->references = 2;
	hook->kind = kind;
	hook->filter = filter;
	if (kind == 4) {
		if (filter < 0)
			connected->context.mouse_events |=
			    (UINT64_C(1) << 50) - 1;
		else
			connected->context.mouse_events |= UINT64_C(1) << filter;
	}
	struct connected_hook **tail = &connected->hooks;
	while (*tail != NULL)
		tail = &(*tail)->next;
	*tail = hook;
	wrenEnsureSlots(vm, function_slot + 2);
	wrenGetVariable(vm, "syncterm", "HookHandle", function_slot + 1);
	struct foreign_hook_handle *handle = wrenSetSlotNewForeign(vm, 0,
	    function_slot + 1, sizeof(*handle));
	*handle = (struct foreign_hook_handle){.hook = hook};
	return hook;
}
static void fn_hook_on_key(WrenVM *vm) { register_hook(vm, 1, 1, -1); }
static void fn_hook_on_key_value(WrenVM *vm)
{
	double key;
	double modifiers;

	if (wrenGetSlotType(vm, 1) != WREN_TYPE_NUM
	    || !isfinite(key = wrenGetSlotDouble(vm, 1))
	    || key < 0 || key > ST_WREN_QUIT_KEY || key != floor(key)
	    || (key >= 0xd800 && key <= 0xdfff)
	    || wrenGetSlotType(vm, 2) != WREN_TYPE_NUM
	    || !isfinite(modifiers = wrenGetSlotDouble(vm, 2))
	    || modifiers < 0 || modifiers > UINT32_MAX || modifiers != floor(modifiers)) {
		st_wren_abort(vm, "Hook.onKey expects a key and modifier mask");
		return;
	}
	struct connected_hook *hook = register_hook(vm, 3, 1, (int)key);
	if (hook != NULL)
		hook->modifiers = (uint32_t)modifiers
		    & ~(ST_CONSOLE_MODIFIER_CAPS_LOCK | ST_CONSOLE_MODIFIER_NUM_LOCK);
}
static void fn_hook_shell(WrenVM *vm) { register_hook(vm, 1, 2, -1); }
static void fn_hook_disconnect(WrenVM *vm) { register_hook(vm, 1, 3, -1); }
static void fn_hook_on_wire(WrenVM *vm) { register_hook(vm, 1, 7, -1); }
static void fn_hook_on_wire_value(WrenVM *vm)
{
	double byte;

	if (wrenGetSlotType(vm, 1) != WREN_TYPE_NUM
	    || !isfinite(byte = wrenGetSlotDouble(vm, 1))
	    || byte < 0 || byte > UINT8_MAX || byte != floor(byte)) {
		st_wren_abort(vm, "Hook.onWire expects a byte value");
		return;
	}
	register_hook(vm, 2, 7, (int)byte);
}
static void fn_hook_on_input(WrenVM *vm) { register_hook(vm, 1, 8, -1); }
static void fn_hook_on_input_value(WrenVM *vm)
{
	double scalar;

	if (wrenGetSlotType(vm, 1) != WREN_TYPE_NUM
	    || !isfinite(scalar = wrenGetSlotDouble(vm, 1))
	    || scalar < 0 || scalar > 0x10ffff
	    || (scalar >= 0xd800 && scalar <= 0xdfff)
	    || scalar != floor(scalar)) {
		st_wren_abort(vm, "Hook.onInput expects a Unicode scalar");
		return;
	}
	register_hook(vm, 2, 8, (int)scalar);
}

static const char *
regex_anchor_error(const Regexp *regexp)
{
	const char *error;

	if (regexp == NULL)
		return "empty pattern";
	switch (regexp->type) {
	case Lit:
	case Dot:
		return NULL;
	case Cat:
	case Paren:
		return regex_anchor_error(regexp->left);
	case Alt:
		error = regex_anchor_error(regexp->left);
		return error == NULL ? regex_anchor_error(regexp->right) : error;
	case Star:
	case Plus:
	case Quest:
		return "pattern must start with a fixed-character anchor";
	default:
		return "unknown regex node type";
	}
}

static int
regex_group_count(const Regexp *regexp)
{
	int left, right, own;

	if (regexp == NULL)
		return 0;
	left = regex_group_count(regexp->left);
	right = regex_group_count(regexp->right);
	own = regexp->type == Paren ? regexp->n : 0;
	if (left > own) own = left;
	if (right > own) own = right;
	return own;
}

static void
register_match_hook(WrenVM *vm, bool clean)
{
	const char *pattern;
	const char *error;
	char *copy = NULL;
	Regexp *regexp = NULL;
	Prog *program = NULL;
	PikeVM *pike = NULL;
	char *buffer = NULL;
	struct connected_hook *hook;
	int length;
	int groups;
	char message[320];

	if (connected_from_vm(vm) == NULL) {
		wrenSetSlotNull(vm, 0);
		return;
	}
	if (!st_wren_text_slot(vm, 1, &pattern, &length))
		return;
	if (length == 0 || length >= ST_WREN_REGEX_BUFFER_SIZE
	    || memchr(pattern, '\0', (size_t)length) != NULL) {
		st_wren_abort(vm, "Hook.onMatch requires a nonempty pattern under 4096 bytes without NUL");
		return;
	}
	copy = malloc((size_t)length + 1);
	if (copy == NULL) {
		st_wren_abort(vm, "Hook.onMatch: out of memory");
		return;
	}
	memcpy(copy, pattern, (size_t)length);
	copy[length] = '\0';
	re1_error[0] = '\0';
	re1_fatal_handler = trap_re1_fatal;
	if (setjmp(re1_jump) != 0) {
		re1_fatal_handler = NULL;
		(void)snprintf(message, sizeof(message), "Hook.onMatch: %s",
		    re1_error);
		free(copy);
		st_wren_abort(vm, message);
		return;
	}
	regexp = parse_unanchored(copy);
	free(copy);
	copy = NULL;
	error = regex_anchor_error(regexp);
	groups = regex_group_count(regexp);
	if (error == NULL && groups > (MAXSUB / 2) - 1)
		error = "pattern has more than nine capture groups";
	if (error != NULL) {
		re1_fatal_handler = NULL;
		free_regexp(regexp);
		st_wren_abort(vm, error);
		return;
	}
	program = compile(regexp);
	re1_fatal_handler = NULL;
	free_regexp(regexp);
	buffer = malloc(ST_WREN_REGEX_BUFFER_SIZE);
	if (buffer != NULL)
		pike = pikevm_new(program, MAXSUB);
	if (buffer == NULL || pike == NULL) {
		free(buffer);
		free(program);
		st_wren_abort(vm, "Hook.onMatch: out of memory");
		return;
	}
	buffer[0] = '\0';
	pikevm_start(pike, buffer);
	hook = register_hook(vm, 2, ST_WREN_HOOK_MATCH, -1);
	if (hook == NULL) {
		pikevm_free(pike);
		free(program);
		free(buffer);
		return;
	}
	hook->regex_prog = program;
	hook->regex_vm = pike;
	hook->regex_buf = buffer;
	hook->regex_groups = groups;
	hook->regex_clean = clean;
}

static void fn_hook_on_match(WrenVM *vm)
{ register_match_hook(vm, false); }
static void fn_hook_on_match_clean(WrenVM *vm)
{ register_match_hook(vm, true); }
static void fn_hook_on_mouse(WrenVM *vm) { register_hook(vm, 1, 4, -1); }
static void fn_hook_on_mouse_value(WrenVM *vm)
{
	double event;

	if (wrenGetSlotType(vm, 1) != WREN_TYPE_NUM
	    || !isfinite(event = wrenGetSlotDouble(vm, 1))
	    || event < 0 || event >= 50 || event != floor(event)) {
		st_wren_abort(vm, "Hook.onMouse expects a mouse event code");
		return;
	}
	register_hook(vm, 2, 4, (int)event);
}
static void fn_hook_on_physical_key(WrenVM *vm)
{ register_hook(vm, 1, 5, -1); }
static void fn_hook_on_physical_key_value(WrenVM *vm)
{
	double evdev;

	if (wrenGetSlotType(vm, 1) != WREN_TYPE_NUM
	    || !isfinite(evdev = wrenGetSlotDouble(vm, 1))
	    || evdev < 0 || evdev > UINT16_MAX || evdev != floor(evdev)) {
		st_wren_abort(vm, "Hook.onPhysicalKey expects an evdev code");
		return;
	}
	register_hook(vm, 2, 5, (int)evdev);
}
static void
fn_hook_every(WrenVM *vm)
{
	double interval;
	uint64_t now;
	struct connected_hook *hook;

	if (wrenGetSlotType(vm, 1) != WREN_TYPE_NUM
	    || !isfinite(interval = wrenGetSlotDouble(vm, 1))
	    || interval < 1 || interval > UINT32_MAX
	    || interval != floor(interval)) {
		st_wren_abort(vm, "Hook.every expects a positive millisecond interval");
		return;
	}
	if (st_time_monotonic_ms(&now) != 0) {
		st_wren_abort(vm, "Hook.every: monotonic clock unavailable");
		return;
	}
	register_hook(vm, 2, 6, -1);
	if (wrenGetSlotType(vm, 0) != WREN_TYPE_FOREIGN)
		return;
	hook = hook_from_handle(vm);
	if (hook == NULL)
		return;
	hook->interval_ms = (uint64_t)interval;
	hook->next_deadline_ms = now + hook->interval_ms;
}

static const struct sftp_binding bindings[] = {
	{"SFTP", true, "available", fn_sftp_available},
	{"SFTP", true, "start()", fn_sftp_start},
	{"Host", true, "sshPublicKey", fn_host_ssh_public_key},
	{"SFTP", true, "pubdir", fn_sftp_pubdir},
	{"SFTP", true, "lname", fn_sftp_lname},
	{"SFTP", true, "realpath(_)", fn_sftp_realpath},
	{"SFTP", true, "stat(_)", fn_sftp_stat},
	{"SFTP", true, "opendir(_)", fn_sftp_opendir},
	{"SFTP", true, "readdir(_)", fn_sftp_readdir},
	{"SFTP", true, "close(_)", fn_sftp_close},
	{"SFTP", true, "open(_,_)", fn_sftp_open},
	{"SFTP", true, "read(_,_,_)", fn_sftp_read},
	{"SFTP", true, "write(_,_,_)", fn_sftp_write},
	{"SFTP", true, "mkdir(_)", fn_sftp_mkdir},
	{"SFTP", true, "rmdir(_)", fn_sftp_rmdir},
	{"SFTP", true, "remove(_)", fn_sftp_remove},
	{"SFTP", true, "rename(_,_)", fn_sftp_rename},
	{"SFTP", true, "setMtime(_,_)", fn_sftp_set_mtime},
	{"SFTP", true, "descs(_)", fn_sftp_descs},
	{"SFTPStat", false, "size", fn_stat_size},
	{"SFTPStat", false, "mtime", fn_stat_mtime},
	{"SFTPStat", false, "atime", fn_stat_atime},
	{"SFTPStat", false, "mode", fn_stat_mode},
	{"SFTPStat", false, "uid", fn_stat_uid},
	{"SFTPStat", false, "gid", fn_stat_gid},
	{"SFTPStat", false, "toString", fn_stat_string},
	{"SFTPEntry", false, "name", fn_entry_name},
	{"SFTPEntry", false, "longname", fn_entry_long_name},
	{"SFTPEntry", false, "size", fn_entry_size},
	{"SFTPEntry", false, "mtime", fn_entry_mtime},
	{"SFTPEntry", false, "isDir", fn_entry_directory},
	{"SFTPEntry", false, "hasLongDesc", fn_entry_long_desc},
	{"SFTPEntry", false, "hash", fn_entry_hash},
	{"SFTPEntry", false, "toString", fn_entry_string},
	{"SFTPError", false, "code", fn_error_code},
	{"SFTPError", false, "serverStatus", fn_error_status},
	{"SFTPError", false, "message", fn_error_message},
	{"SFTPError", false, "isTransient", fn_error_transient},
	{"SFTPError", false, "toString", fn_error_string},
	{"Timer", true, "after(_)", fn_timer_after},
	{"Hook", true, "onKey(_)", fn_hook_on_key},
	{"Hook", true, "onKey(_,_,_)", fn_hook_on_key_value},
	{"Hook", true, "onShellClose(_)", fn_hook_shell},
	{"Hook", true, "onDisconnect(_)", fn_hook_disconnect},
	{"Hook", true, "onWire(_)", fn_hook_on_wire},
	{"Hook", true, "onWire(_,_)", fn_hook_on_wire_value},
	{"Hook", true, "onInput(_)", fn_hook_on_input},
	{"Hook", true, "onInput(_,_)", fn_hook_on_input_value},
	{"Hook", true, "onMatch(_,_)", fn_hook_on_match},
	{"Hook", true, "onMatchClean(_,_)", fn_hook_on_match_clean},
	{"Hook", true, "onMouse(_)", fn_hook_on_mouse},
	{"Hook", true, "onMouse(_,_)", fn_hook_on_mouse_value},
	{"Hook", true, "onPhysicalKey(_)", fn_hook_on_physical_key},
	{"Hook", true, "onPhysicalKey(_,_)", fn_hook_on_physical_key_value},
	{"Hook", true, "every(_,_)", fn_hook_every},
	{"HookHandle", false, "remove()", fn_hook_remove},
	{"HookHandle", false, "callCount", fn_hook_call_count},
	{"HookHandle", false, "totalRuntime", fn_hook_total_runtime},
	{"HookHandle", false, "minRuntime", fn_hook_min_runtime},
	{"HookHandle", false, "maxRuntime", fn_hook_max_runtime},
	{"HookHandle", false, "toString", fn_hook_to_string},
};

WrenForeignMethodFn
st_wren_sftp_bind_method(const char *class_name, bool is_static,
	const char *signature)
{
	for (size_t i = 0; i < sizeof(bindings) / sizeof(bindings[0]); ++i) {
		if (bindings[i].is_static == is_static
		    && strcmp(bindings[i].class_name, class_name) == 0
		    && strcmp(bindings[i].signature, signature) == 0)
			return bindings[i].method;
	}
	return NULL;
}

WrenForeignClassMethods
st_wren_sftp_bind_class(const char *class_name)
{
	WrenForeignClassMethods methods = {NULL, NULL};

	if (strcmp(class_name, "SFTPHandle") == 0) {
		methods.allocate = allocate_handle;
		methods.finalize = finalize_handle;
	}
	else if (strcmp(class_name, "SFTPStat") == 0)
		methods.allocate = allocate_stat;
	else if (strcmp(class_name, "SFTPEntry") == 0) {
		methods.allocate = allocate_entry;
		methods.finalize = finalize_entry;
	}
	else if (strcmp(class_name, "SFTPError") == 0) {
		methods.allocate = allocate_error;
		methods.finalize = finalize_error;
	}
	else if (strcmp(class_name, "HookHandle") == 0) {
		methods.allocate = allocate_hook_handle;
		methods.finalize = finalize_hook_handle;
	}
	return methods;
}

static struct pending_request *
take_request(struct st_wren_connected *connected, uint64_t id)
{
	struct pending_request **cursor = &connected->requests;

	while (*cursor != NULL) {
		if ((*cursor)->id == id) {
			struct pending_request *result = *cursor;
			*cursor = result->next;
			return result;
		}
		cursor = &(*cursor)->next;
	}
	return NULL;
}

static bool
stat_exact(const struct st_sftp_stat *value)
{
	return st_wren_u64_exact(value->size)
	    && st_wren_i64_exact(value->modified_time)
	    && st_wren_i64_exact(value->accessed_time);
}

static bool
push_response(WrenVM *vm, int slot,
	const struct st_sftp_response *response)
{
	bool exact = response->kind != ST_SFTP_RESULT_STAT
	    || stat_exact(&response->stat);

	if (response->kind == ST_SFTP_RESULT_ENTRIES) {
		for (size_t i = 0; i < response->entry_count && exact; ++i)
			exact = stat_exact(&response->entries[i].stat);
	}
	if (!exact) {
		push_error(vm, slot, IO_ERROR_OUT_OF_RANGE, 0,
		    "SFTP metadata exceeds Wren's exact Number range", false);
		return true;
	}
	switch (response->kind) {
	case ST_SFTP_RESULT_STARTED:
		wrenSetSlotBool(vm, slot, true);
		return true;
	case ST_SFTP_RESULT_OK:
		wrenSetSlotNull(vm, slot);
		return true;
	case ST_SFTP_RESULT_EOF:
		wrenSetSlotNull(vm, slot);
		return true;
	case ST_SFTP_RESULT_STRING:
	case ST_SFTP_RESULT_BYTES:
		wrenSetSlotBytes(vm, slot, response->data, response->size);
		return true;
	case ST_SFTP_RESULT_HANDLE: {
		struct foreign_sftp_handle *handle;
		wrenEnsureSlots(vm, slot + 2);
		wrenGetVariable(vm, "syncterm", "SFTPHandle", slot + 1);
		handle = wrenSetSlotNewForeign(vm, slot, slot + 1,
		    sizeof(*handle));
		*handle = (struct foreign_sftp_handle){
			.magic = ST_SFTP_HANDLE_MAGIC,
			.connected = connected_from_vm(vm),
			.value = response->handle,
			.active = true,
		};
		return true;
	}
	case ST_SFTP_RESULT_STAT: {
		struct foreign_sftp_stat *stat;
		wrenEnsureSlots(vm, slot + 2);
		wrenGetVariable(vm, "syncterm", "SFTPStat", slot + 1);
		stat = wrenSetSlotNewForeign(vm, slot, slot + 1, sizeof(*stat));
		stat->value = response->stat;
		return true;
	}
	case ST_SFTP_RESULT_ENTRIES:
		wrenEnsureSlots(vm, slot + 3);
		wrenSetSlotNewList(vm, slot);
		for (size_t i = 0; i < response->entry_count; ++i) {
			const struct st_sftp_entry *source = &response->entries[i];
			struct foreign_sftp_entry *entry;
			wrenGetVariable(vm, "syncterm", "SFTPEntry", slot + 2);
			entry = wrenSetSlotNewForeign(vm, slot + 1, slot + 2,
			    sizeof(*entry));
			*entry = (struct foreign_sftp_entry){
				.name = source->name == NULL ? strdup("")
				    : strdup(source->name),
				.long_name = source->long_name == NULL ? NULL
				    : strdup(source->long_name),
				.stat = source->stat,
				.directory = source->directory,
				.has_long_description = source->has_long_description,
			};
			if (source->hash != NULL && source->hash_size != 0) {
				entry->hash = malloc(source->hash_size);
				if (entry->hash != NULL) {
					memcpy(entry->hash, source->hash, source->hash_size);
					entry->hash_size = source->hash_size;
				}
			}
			if (entry->name == NULL
			    || (source->long_name != NULL && entry->long_name == NULL)
			    || (source->hash != NULL && source->hash_size != 0
			    && entry->hash == NULL))
				return false;
			wrenInsertInList(vm, slot, -1, slot + 1);
		}
		return true;
	case ST_SFTP_RESULT_ERROR:
		push_error(vm, slot, response->error_code, response->server_status,
		    response->message, response->transient);
		return true;
	}
	return false;
}

static void
close_discarded_handle(struct st_wren_connected *connected,
	const struct st_sftp_response *response)
{
	if (response->kind == ST_SFTP_RESULT_HANDLE && connected->provider.submit != NULL) {
		const struct st_sftp_request close = {
			.operation = ST_SFTP_CLOSE, .handle = response->handle,
		};
		uint64_t id;

		(void)connected->provider.submit(connected->provider.context, &close, &id);
	}
}

static int
deliver_response(struct st_wren_connected *connected,
	const struct st_sftp_response *response)
{
	struct pending_request *pending = take_request(connected, response->request_id);
	WrenVM *vm = connected->vm;
	int status;

	if (pending == NULL || pending->operation == NULL) {
		close_discarded_handle(connected, response);
		free(pending);
		return 0;
	}
	wrenEnsureSlots(vm, 3);
	if (!push_response(vm, 1, response))
		push_error(vm, 1, IO_ERROR_OUT_OF_MEMORY, 0, "Out of memory decoding SFTP result", true);
	status = st_wren_operation_resolve(vm, pending->operation, 1);
	free(pending);
	return status;
}

static int
deliver_timers(struct st_wren_connected *connected)
{
	struct pending_timer **cursor = &connected->timers;
	uint64_t now;
	int status = 0;

	if (st_time_monotonic_ms(&now) != 0)
		return IO_ERROR_IO;
	while (*cursor != NULL) {
		struct pending_timer *timer = *cursor;

		if (timer->deadline > now) {
			cursor = &timer->next;
			continue;
		}
		*cursor = timer->next;
		wrenEnsureSlots(connected->vm, 2);
		wrenSetSlotNull(connected->vm, 1);
		if (st_wren_operation_resolve(connected->vm, timer->operation, 1) != 0)
			status = IO_ERROR_OUT_OF_MEMORY;
		free(timer);
	}
	return status;
}

static void
connected_report_error(WrenVM *vm, WrenErrorType type, const char *module,
	int line, const char *message)
{
	struct st_wren_connected *connected = connected_from_vm(vm);
	const char *kind = type == WREN_ERROR_COMPILE ? "compile"
	    : type == WREN_ERROR_RUNTIME ? "runtime" : "stack";

	st_wren_log_error(vm, type, module, line, message);
	if (type != WREN_ERROR_STACK_TRACE)
		(void)snprintf(connected->last_error, sizeof(connected->last_error),
		    "%s %s:%d: %s", kind, module == NULL ? "?" : module, line,
		    message == NULL ? "" : message);
	fprintf(stderr, "[wren-connected] %s %s:%d: %s\n", kind,
	    module == NULL ? "?" : module, line, message == NULL ? "" : message);
}

void
st_wren_connected_log_transfer(st_wren_connected_t *connected,
	int level, const char *message)
{
	if (connected != NULL && message != NULL && level >= 0
	    && level <= connected->context.transfer_log_level
	    && connected->context.transfer_log != NULL)
		connected->context.transfer_log(connected->context.transfer_log_context,
		    level, message);
}

int
st_wren_connected_create(st_wren_connected_t **result,
	const struct st_wren_connected_config *config)
{
	struct st_wren_connected *connected;
	WrenConfiguration wren_config;
	int status;

	if (result == NULL || config == NULL || config->host == NULL
	    || config->console == NULL || config->profile == NULL
	    || (config->sftp != NULL && (config->sftp->submit == NULL
	    || config->sftp->poll == NULL)))
		return IO_ERROR_INVALID_ARGUMENT;
	connected = calloc(1, sizeof(*connected));
	if (connected == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	connected->host = config->host;
	if (config->sftp != NULL)
		connected->provider = *config->sftp;
	st_profile_init(&connected->profile);
	if (!st_profile_copy(&connected->profile, config->profile)) {
		free(connected);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	connected->context.host = config->host;
	connected->context.console = config->console;
	connected->context.connected = connected;
	connected->context.profile = &connected->profile;
	connected->context.transfer_log_level = config->profile->transfer_log_level;
	connected->context.transfer_log = config->transfer_log;
	connected->context.transfer_log_context = config->transfer_log_context;
	connected->context.session_log = config->session_log;
	connected->context.session_log_context = config->session_log_context;
	connected->context.cterm = config->cterm;
	connected->context.connection = config->connection;
	connected->context.session = config->session;
	(void)st_time_monotonic_ms(&connected->context.connected_at_ms);
	connected->context.event = "connected";
	connected->context.status_enabled = config->status_enabled;
	/* Local link gestures and hover remain available without remote tracking. */
	connected->context.mouse_events |= ST_WREN_LINK_MOUSE_EVENTS;
	if (config->console == config->host->menu.console)
		connected->context.fonts = config->host->menu.fonts;
	else {
		status = st_wren_context_load_fonts(&connected->context);
		if (status != 0) {
			st_wren_connected_destroy(connected);
			return status;
		}
	}
	wrenInitConfiguration(&wren_config);
	wren_config.userData = &connected->context;
	wren_config.writeFn = st_wren_write_output;
	wren_config.errorFn = connected_report_error;
	wren_config.loadModuleFn = st_wren_load_module;
	wren_config.bindForeignMethodFn = st_wren_bind_method;
	wren_config.bindForeignClassFn = st_wren_bind_class;
	connected->vm = wrenNewVM(&wren_config);
	if (connected->vm == NULL) {
		st_wren_connected_destroy(connected);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	connected->context.vm = connected->vm;
	status = wrenInterpret(connected->vm, "connected_bootstrap",
	    "import \"syncterm\" for Host\n") == WREN_RESULT_SUCCESS
	    ? 0 : IO_ERROR_INVALID_FORMAT;
	if (status == 0)
		status = st_wren_input_context_init(connected->vm);
	if (status == 0)
		status = st_wren_interpret_event_scripts(connected->vm, "connected");
	if (status == 0)
		status = st_wren_interpret_profile_scripts(connected->vm,
		    &connected->profile);
	if (status != 0) {
		st_wren_connected_destroy(connected);
		return status;
	}
	*result = connected;
	return 0;
}

static int
run_hooks(struct st_wren_connected *connected, int kind)
{
	WrenHandle *call = wrenMakeCallHandle(connected->vm, "dispatch_(_)");
	int status = 0;

	if (call == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	for (struct connected_hook *hook = connected->hooks; hook != NULL;
	    hook = hook->next) {
		uint64_t before = 0;

		if (hook->kind != kind || hook->function == NULL)
			continue;
		wrenEnsureSlots(connected->vm, 2);
		wrenGetVariable(connected->vm, "syncterm", "Hook", 0);
		wrenSetSlotHandle(connected->vm, 1, hook->function);
		(void)st_time_monotonic_ms(&before);
		if (connected_call(connected, call) != WREN_RESULT_SUCCESS)
			status = IO_ERROR_INVALID_FORMAT;
		else
			warn_observe_return(connected->vm,
			    kind == 2 ? "onShellClose" : "onDisconnect");
		record_hook_runtime(hook, before);
	}
	wrenReleaseHandle(connected->vm, call);
	return status;
}

int
st_wren_connected_shell_closed(st_wren_connected_t *connected)
{
	return connected == NULL ? IO_ERROR_INVALID_ARGUMENT
	    : run_hooks(connected, 2);
}

int
st_wren_connected_handle_key_record(st_wren_connected_t *connected,
	const struct st_wren_input_record *record, bool *consumed)
{
	uint32_t key = 0;
	uint32_t modifiers;
	bool has_key;

	if (connected == NULL || record == NULL || consumed == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	*consumed = false;
	has_key = st_wren_input_key(&record->data.key.native, &key, &modifiers);
	modifiers &= ~(ST_CONSOLE_MODIFIER_CAPS_LOCK | ST_CONSOLE_MODIFIER_NUM_LOCK);
	for (struct connected_hook *hook = connected->hooks; hook != NULL;
	    hook = hook->next) {
		uint64_t before = 0;

		if (hook->kind != 1 || (hook->filter >= 0
		    && (!has_key || (uint32_t)hook->filter != key
		    || hook->modifiers != modifiers)) || hook->function == NULL)
			continue;
		wrenEnsureSlots(connected->vm, 4);
		wrenSetSlotHandle(connected->vm, 0,
		    connected->context.hook_class);
		wrenSetSlotHandle(connected->vm, 1, hook->function);
		st_wren_input_push_event(connected->vm, 2, record);
		(void)st_time_monotonic_ms(&before);
		if (connected_call(connected,
		    connected->context.hook_dispatch) != WREN_RESULT_SUCCESS) {
			record_hook_runtime(hook, before);
			continue;
		}
		record_hook_runtime(hook, before);
		if (wrenGetSlotType(connected->vm, 0) == WREN_TYPE_BOOL
		    && wrenGetSlotBool(connected->vm, 0)) {
			*consumed = true;
			break;
		}
	}
	return 0;
}

int
st_wren_connected_rip_transfer(st_wren_connected_t *connected,
	const struct st_cterm_rip_transfer_request *request)
{
	return connected == NULL ? IO_ERROR_NOT_CONNECTED
	    : st_wren_transfer_rip_request(&connected->context, request);
}

bool
st_wren_connected_has_wire_hooks(const st_wren_connected_t *connected)
{
	if (connected == NULL)
		return false;
	if (st_wren_transfer_detect_enabled(&connected->context)) return true;
	for (const struct connected_hook *hook = connected->hooks; hook != NULL;
	    hook = hook->next) {
		if (hook->kind == 7 && hook->function != NULL)
			return true;
	}
	return false;
}

int
st_wren_connected_filter_wire_byte(st_wren_connected_t *connected,
	uint8_t byte, bool *consumed, uint8_t *replacement, size_t capacity,
	size_t *replacement_size)
{
	if (connected == NULL || consumed == NULL || replacement_size == NULL
	    || (replacement == NULL && capacity != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	*consumed = false;
	*replacement_size = 0;
	int transfer_status = st_wren_transfer_filter(&connected->context, byte,
	    consumed, replacement, capacity, replacement_size);
	if (transfer_status != 0 || *consumed) return transfer_status;
	for (struct connected_hook *hook = connected->hooks; hook != NULL;
	    hook = hook->next) {
		uint64_t before = 0;
		int size;
		const char *bytes;

		if (hook->kind != 7 || hook->function == NULL
		    || (hook->filter >= 0 && hook->filter != byte))
			continue;
		wrenEnsureSlots(connected->vm, 3);
		wrenSetSlotHandle(connected->vm, 0,
		    connected->context.hook_class);
		wrenSetSlotHandle(connected->vm, 1, hook->function);
		wrenSetSlotDouble(connected->vm, 2, byte);
		(void)st_time_monotonic_ms(&before);
		if (connected_call(connected,
		    connected->context.hook_dispatch) != WREN_RESULT_SUCCESS) {
			record_hook_runtime(hook, before);
			continue;
		}
		record_hook_runtime(hook, before);
		if (wrenGetSlotType(connected->vm, 0) == WREN_TYPE_BOOL
		    && wrenGetSlotBool(connected->vm, 0)) {
			*consumed = true;
			break;
		}
		if (wrenGetSlotType(connected->vm, 0) != WREN_TYPE_STRING)
			continue;
		bytes = wrenGetSlotBytes(connected->vm, 0, &size);
		if (size < 0 || (size_t)size > capacity) {
			st_wren_log_append(connected->vm, 2,
			    "Hook.onWire replacement exceeds wire buffer capacity");
			continue;
		}
		if (size != 0)
			memcpy(replacement, bytes, (size_t)size);
		*replacement_size = (size_t)size;
		*consumed = true;
		break;
	}
	return 0;
}

bool
st_wren_connected_has_input_hooks(const st_wren_connected_t *connected)
{
	if (connected == NULL)
		return false;
	for (const struct connected_hook *hook = connected->hooks; hook != NULL;
	    hook = hook->next) {
		if ((hook->kind == 8 || hook->kind == ST_WREN_HOOK_MATCH)
		    && hook->function != NULL)
			return true;
	}
	return false;
}

static void
drain_match_hook(st_wren_connected_t *connected, struct connected_hook *hook)
{
	char *matches[MAXSUB];

	while (hook->function != NULL && hook->regex_pos < hook->regex_buf_len) {
		int result = pikevm_step(hook->regex_vm,
		    hook->regex_buf + hook->regex_pos);

		if (result < 0) {
			hook->regex_pos++;
			continue;
		}
		if (result == 1) {
			uint64_t before = 0;
			size_t end;

			pikevm_match(hook->regex_vm, matches);
			wrenEnsureSlots(connected->vm, 4);
			wrenSetSlotHandle(connected->vm, 0,
			    connected->context.hook_class);
			wrenSetSlotHandle(connected->vm, 1, hook->function);
			wrenSetSlotNewList(connected->vm, 2);
			for (int group = 0; group <= hook->regex_groups; ++group) {
				char *start = matches[2 * group];
				char *finish = matches[2 * group + 1];

				if (start == NULL || finish == NULL)
					wrenSetSlotNull(connected->vm, 3);
				else
					wrenSetSlotBytes(connected->vm, 3, start,
					    (size_t)(finish - start));
				wrenInsertInList(connected->vm, 2, -1, 3);
			}
			(void)st_time_monotonic_ms(&before);
			if (connected_call(connected,
			    connected->context.hook_dispatch) == WREN_RESULT_SUCCESS)
				warn_observe_return(connected->vm,
				    hook->regex_clean ? "onMatchClean" : "onMatch");
			record_hook_runtime(hook, before);
			end = matches[1] == NULL ? hook->regex_buf_len
			    : (size_t)(matches[1] - hook->regex_buf);
			if (end > hook->regex_buf_len)
				end = hook->regex_buf_len;
			memmove(hook->regex_buf, hook->regex_buf + end,
			    hook->regex_buf_len - end);
			hook->regex_buf_len -= end;
		}
		else {
			memmove(hook->regex_buf, hook->regex_buf + 1,
			    hook->regex_buf_len - 1);
			hook->regex_buf_len--;
		}
		hook->regex_buf[hook->regex_buf_len] = '\0';
		hook->regex_pos = 0;
		pikevm_start(hook->regex_vm, hook->regex_buf);
	}
}

static void
feed_match_hook(st_wren_connected_t *connected, struct connected_hook *hook,
	uint32_t scalar)
{
	char utf8[4];
	ptrdiff_t count = st_utf8_encode_one(utf8, sizeof(utf8), scalar);

	/* CTerm owns sequence bodies. ESC is only a boundary marker here,
	 * so running a second ANSI parser would eat the next text scalar. */
	if (hook->regex_clean && scalar == 0x1b)
		return;
	if (count <= 0)
		return;
	for (ptrdiff_t i = 0; i < count; ++i) {
		uint8_t byte = (uint8_t)utf8[i];

		if (hook->regex_buf_len + 1 >= ST_WREN_REGEX_BUFFER_SIZE) {
			size_t drop = ST_WREN_REGEX_BUFFER_SIZE / 2 + 1;

			memmove(hook->regex_buf, hook->regex_buf + drop,
			    hook->regex_buf_len - drop);
			hook->regex_buf_len -= drop;
			hook->regex_pos = 0;
			pikevm_start(hook->regex_vm, hook->regex_buf);
		}
		hook->regex_buf[hook->regex_buf_len++] = (char)byte;
		hook->regex_buf[hook->regex_buf_len] = '\0';
		drain_match_hook(connected, hook);
	}
}

int
st_wren_connected_filter_input_scalar(st_wren_connected_t *connected,
	uint32_t scalar, bool *consumed, char *replacement, size_t capacity,
	size_t *replacement_size)
{
	if (connected == NULL || consumed == NULL || replacement_size == NULL
	    || (replacement == NULL && capacity != 0)
	    || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff))
		return IO_ERROR_INVALID_ARGUMENT;
	*consumed = false;
	*replacement_size = 0;
	for (struct connected_hook *hook = connected->hooks; hook != NULL;
	    hook = hook->next) {
		uint64_t before = 0;
		int size;
		const char *bytes;
		char *normalized = NULL;
		size_t normalized_size = 0;
		int status;

		if (hook->kind == ST_WREN_HOOK_MATCH
		    && hook->function != NULL) {
			feed_match_hook(connected, hook, scalar);
			continue;
		}
		if (hook->kind != 8 || hook->function == NULL
		    || (hook->filter >= 0 && (uint32_t)hook->filter != scalar))
			continue;
		wrenEnsureSlots(connected->vm, 3);
		wrenSetSlotHandle(connected->vm, 0,
		    connected->context.hook_class);
		wrenSetSlotHandle(connected->vm, 1, hook->function);
		wrenSetSlotDouble(connected->vm, 2, (double)scalar);
		(void)st_time_monotonic_ms(&before);
		if (connected_call(connected,
		    connected->context.hook_dispatch) != WREN_RESULT_SUCCESS) {
			record_hook_runtime(hook, before);
			continue;
		}
		record_hook_runtime(hook, before);
		if (wrenGetSlotType(connected->vm, 0) == WREN_TYPE_BOOL
		    && wrenGetSlotBool(connected->vm, 0)) {
			*consumed = true;
			break;
		}
		if (wrenGetSlotType(connected->vm, 0) != WREN_TYPE_STRING)
			continue;
		bytes = wrenGetSlotBytes(connected->vm, 0, &size);
		status = size < 0 ? IO_ERROR_INVALID_ENCODING
		    : st_unicode_text_normalize(&normalized, &normalized_size,
		    bytes, (size_t)size, NULL);
		if (status == IO_ERROR_OUT_OF_MEMORY)
			return status;
		if (status != 0 || normalized_size > capacity) {
			st_wren_log_append(connected->vm, 2, status != 0
			    ? "Hook.onInput replacement is not valid UTF-8"
			    : "Hook.onInput replacement exceeds input buffer capacity");
			free(normalized);
			continue;
		}
		if (normalized_size != 0)
			memcpy(replacement, normalized, normalized_size);
		*replacement_size = normalized_size;
		*consumed = true;
		free(normalized);
		break;
	}
	return 0;
}

int
st_wren_connected_handle_event(st_wren_connected_t *connected,
	const struct st_console_event *event, bool *consumed)
{
	int status;

	if (connected == NULL || event == NULL || consumed == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_wren_input_dispatch_console_event(connected->vm, event,
	    consumed);
	return status;
}

int
st_wren_connected_handle_mouse_record(struct st_wren_connected *connected,
	const struct st_wren_input_record *record, bool *consumed)
{
	if (connected == NULL || record == NULL || consumed == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	*consumed = false;
	if (connected->context.link_gesture
	    && st_wren_link_mouse(&connected->context, record)) {
		*consumed = true;
		return 0;
	}
	for (struct connected_hook *hook = connected->hooks; hook != NULL;
	    hook = hook->next) {
		uint64_t before = 0;

		if (hook->kind != 4 || hook->function == NULL
		    || (hook->filter >= 0
		    && hook->filter != record->data.mouse.event))
			continue;
		wrenEnsureSlots(connected->vm, 4);
		wrenSetSlotHandle(connected->vm, 0,
		    connected->context.hook_class);
		wrenSetSlotHandle(connected->vm, 1, hook->function);
		st_wren_input_push_event(connected->vm, 2, record);
		(void)st_time_monotonic_ms(&before);
		if (connected_call(connected,
		    connected->context.hook_dispatch) != WREN_RESULT_SUCCESS) {
			record_hook_runtime(hook, before);
			continue;
		}
		record_hook_runtime(hook, before);
		if (wrenGetSlotType(connected->vm, 0) == WREN_TYPE_BOOL
		    && wrenGetSlotBool(connected->vm, 0)) {
			*consumed = true;
			break;
		}
	}
	if (!*consumed)
		*consumed = st_wren_link_mouse(&connected->context, record);
	return 0;
}

int
st_wren_connected_handle_physical_record(
	struct st_wren_connected *connected,
	const struct st_wren_input_record *record, bool *consumed)
{
	if (connected == NULL || record == NULL || consumed == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	*consumed = false;
	for (struct connected_hook *hook = connected->hooks; hook != NULL;
	    hook = hook->next) {
		uint64_t before = 0;

		if (hook->kind != 5 || hook->function == NULL
		    || (hook->filter >= 0
		    && hook->filter != record->data.physical_key.evdev))
			continue;
		wrenEnsureSlots(connected->vm, 4);
		wrenSetSlotHandle(connected->vm, 0,
		    connected->context.hook_class);
		wrenSetSlotHandle(connected->vm, 1, hook->function);
		st_wren_input_push_event(connected->vm, 2, record);
		(void)st_time_monotonic_ms(&before);
		if (connected_call(connected,
		    connected->context.hook_dispatch) != WREN_RESULT_SUCCESS) {
			record_hook_runtime(hook, before);
			continue;
		}
		record_hook_runtime(hook, before);
		if (wrenGetSlotType(connected->vm, 0) == WREN_TYPE_BOOL
		    && wrenGetSlotBool(connected->vm, 0)) {
			*consumed = true;
			break;
		}
	}
	return 0;
}

void
st_wren_connected_destroy(st_wren_connected_t *connected)
{
	if (connected == NULL)
		return;
	connected->shutting_down = true;
	(void)st_console_set_pointer_shape(connected->context.console,
	    ST_CONSOLE_POINTER_TEXT);
	st_wren_connected_capture_stop(connected);
	if (connected->provider.cancel_all != NULL)
		connected->provider.cancel_all(connected->provider.context);
	if (connected->vm != NULL)
		(void)st_wren_operation_cancel_all(connected->vm);
	if (connected->vm != NULL)
		st_wren_cleanup_shutdown(connected->vm);
	if (connected->vm != NULL)
		st_wren_session_release_holds(connected->vm);
	while (connected->requests != NULL) {
		struct pending_request *next = connected->requests->next;
		free(connected->requests);
		connected->requests = next;
	}
	while (connected->timers != NULL) {
		struct pending_timer *next = connected->timers->next;
		free(connected->timers);
		connected->timers = next;
	}
	if (connected->vm != NULL)
		(void)run_hooks(connected, 3);
	while (connected->hooks != NULL) {
		struct connected_hook *next = connected->hooks->next;
		free_regex_hook(connected->hooks);
		if (connected->vm != NULL
		    && connected->hooks->function != NULL)
			wrenReleaseHandle(connected->vm, connected->hooks->function);
		connected->hooks->function = NULL;
		release_hook(connected->hooks);
		connected->hooks = next;
	}
	if (connected->vm != NULL) {
		st_wren_status_dispose(connected->vm);
		st_wren_input_context_dispose(connected->vm);
		st_wren_log_dispose(connected->vm);
		st_wren_session_object_dispose(connected->vm);
		wrenFreeVM(connected->vm);
	}
	st_profile_dispose(&connected->profile);
	free(connected);
}

bool
st_wren_connected_auxiliary_active(const st_wren_connected_t *connected)
{
	return connected != NULL && !connected->shutting_down
	    && (connected->context.session_leases != NULL || connected->requests != NULL);
}

bool
st_wren_connected_capture_active(const st_wren_connected_t *connected)
{
	return connected != NULL && connected->capture_stream != NULL;
}

bool
st_wren_connected_capture_paused(const st_wren_connected_t *connected)
{
	return st_wren_connected_capture_active(connected)
	    && connected->capture_paused;
}

int
st_wren_connected_capture_start(st_wren_connected_t *connected,
	FILE *stream, bool raw)
{
	if (connected == NULL || stream == NULL || connected->capture_stream != NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	connected->capture_stream = stream;
	connected->capture_raw = raw;
	connected->capture_paused = false;
	return 0;
}

void
st_wren_connected_capture_stop(st_wren_connected_t *connected)
{
	if (connected == NULL || connected->capture_stream == NULL)
		return;
	if (fclose(connected->capture_stream) != 0 && connected->vm != NULL)
		st_wren_log_append(connected->vm, 2, "Capture close failed");
	connected->capture_stream = NULL;
	connected->capture_paused = false;
}

void
st_wren_connected_capture_pause(st_wren_connected_t *connected)
{
	if (st_wren_connected_capture_active(connected))
		connected->capture_paused = true;
}

void
st_wren_connected_capture_resume(st_wren_connected_t *connected)
{
	if (st_wren_connected_capture_active(connected))
		connected->capture_paused = false;
}

static int
capture_write(st_wren_connected_t *connected, const void *bytes, size_t size)
{
	if (fwrite(bytes, 1, size, connected->capture_stream) != size) {
		if (connected->vm != NULL)
			st_wren_log_append(connected->vm, 2, "Capture write failed");
		st_wren_connected_capture_stop(connected);
	}
	return 0;
}

int
st_wren_connected_capture_wire(st_wren_connected_t *connected,
	const uint8_t *bytes, size_t size)
{
	if (!st_wren_connected_capture_active(connected)
	    || !connected->capture_raw || connected->capture_paused || size == 0)
		return 0;
	return capture_write(connected, bytes, size);
}

int
st_wren_connected_capture_input(st_wren_connected_t *connected,
	const char *bytes, size_t size, bool protocol)
{
	if (!st_wren_connected_capture_active(connected)
	    || connected->capture_raw || connected->capture_paused || size == 0
	    || protocol || (size == 1 && (uint8_t)bytes[0] == 0x1b))
		return 0;
	return capture_write(connected, bytes, size);
}

static int
fire_interval_hooks(st_wren_connected_t *connected, uint64_t now)
{
	WrenHandle *call = NULL;
	int status = 0;

	for (struct connected_hook *hook = connected->hooks; hook != NULL;
	    hook = hook->next) {
		uint64_t before = 0;

		if (hook->kind != 6 || hook->function == NULL
		    || now < hook->next_deadline_ms)
			continue;
		if (call == NULL) {
			call = wrenMakeCallHandle(connected->vm, "dispatch_(_)");
			if (call == NULL)
				return IO_ERROR_OUT_OF_MEMORY;
		}
		/* Do not replay missed ticks after a stalled event loop. */
		hook->next_deadline_ms = now > UINT64_MAX - hook->interval_ms
		    ? UINT64_MAX : now + hook->interval_ms;
		wrenEnsureSlots(connected->vm, 2);
		wrenGetVariable(connected->vm, "syncterm", "Hook", 0);
		wrenSetSlotHandle(connected->vm, 1, hook->function);
		(void)st_time_monotonic_ms(&before);
		if (connected_call(connected, call) != WREN_RESULT_SUCCESS)
			status = IO_ERROR_INVALID_FORMAT;
		else
			warn_observe_return(connected->vm, "every");
		record_hook_runtime(hook, before);
	}
	if (call != NULL)
		wrenReleaseHandle(connected->vm, call);
	return status;
}

int
st_wren_connected_pump(st_wren_connected_t *connected, uint32_t timeout_ms)
{
	struct st_sftp_response response;
	int status;
	bool first = true;
	uint64_t now;

	if (connected == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	collect_removed_hooks(connected);
	status = st_wren_operation_collect(connected->vm);
	if (status != 0)
		return status;
	st_wren_transfer_pump(&connected->context);
	status = st_wren_send_pump(&connected->context);
	if (status != 0)
		return status;
	if (connected->timers != NULL && st_time_monotonic_ms(&now) == 0) {
		for (struct pending_timer *timer = connected->timers; timer != NULL;
		    timer = timer->next) {
			uint64_t remaining = timer->deadline > now
			    ? timer->deadline - now : 0;

			if (remaining < timeout_ms)
				timeout_ms = (uint32_t)remaining;
		}
	}
	if (st_time_monotonic_ms(&now) == 0) {
		for (struct connected_hook *hook = connected->hooks; hook != NULL;
		    hook = hook->next) {
			uint64_t remaining;

			if (hook->kind != 6 || hook->function == NULL)
				continue;
			remaining = hook->next_deadline_ms > now
			    ? hook->next_deadline_ms - now : 0;
			if (remaining < timeout_ms)
				timeout_ms = (uint32_t)remaining;
		}
	}
	if (connected->provider.poll != NULL) {
		/* Drain a bounded batch without replacing poll's readiness result
		 * with deliver_response's zero-on-success status. */
		for (unsigned batch = 0; batch < 256; ++batch) {
			int polled = connected->provider.poll(connected->provider.context,
			    &response, first ? timeout_ms : 0);
			first = false;
			if (polled < 0) return polled;
			if (polled == 0) break;
			status = deliver_response(connected, &response);
			if (status != 0) return status;
		}
	}
	status = deliver_timers(connected);
	if (status == 0)
		status = st_wren_wake_drain(connected->vm);
	if (status == 0 && st_time_monotonic_ms(&now) == 0)
		status = fire_interval_hooks(connected, now);
	if (status == 0 && connected->context.scrollback_requested) {
		WrenHandle *run;

		connected->context.scrollback_requested = false;
		if (!wrenHasModule(connected->vm, "scrollback_view")
		    || !wrenHasVariable(connected->vm, "scrollback_view",
		    "ScrollbackView"))
			return IO_ERROR_INVALID_FORMAT;
		run = wrenMakeCallHandle(connected->vm, "run()");
		if (run == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		wrenEnsureSlots(connected->vm, 1);
		wrenGetVariable(connected->vm, "scrollback_view",
		    "ScrollbackView", 0);
		status = connected_call(connected, run) == WREN_RESULT_SUCCESS
		    ? 0 : IO_ERROR_INVALID_FORMAT;
		wrenReleaseHandle(connected->vm, run);
	}
	return status;
}

int
st_wren_connected_render_status(st_wren_connected_t *connected, bool enabled)
{
	return connected == NULL ? IO_ERROR_INVALID_ARGUMENT
	    : st_wren_status_render(connected->vm, enabled);
}

char *
st_wren_connected_status_details(st_wren_connected_t *connected)
{
	if (connected == NULL || connected->context.status_details == NULL) return NULL;
	WrenVM *vm = connected->vm;
	WrenHandle *call = wrenMakeCallHandle(vm, "call()");
	if (call == NULL) return NULL;
	wrenEnsureSlots(vm, 1);
	wrenSetSlotHandle(vm, 0, connected->context.status_details);
	char *result = NULL;
	if (wrenCall(vm, call) == WREN_RESULT_SUCCESS
	    && wrenGetSlotType(vm, 0) == WREN_TYPE_STRING)
		result = strdup(wrenGetSlotString(vm, 0));
	wrenReleaseHandle(vm, call);
	return result;
}

int
st_wren_connected_interpret(st_wren_connected_t *connected,
	const char *module, const char *source)
{
	if (connected == NULL || module == NULL || source == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	connected->last_error[0] = '\0';
	WrenInterpretResult result = wrenInterpret(connected->vm, module, source);

	collect_removed_hooks(connected);
	return result == WREN_RESULT_SUCCESS ? 0 : IO_ERROR_INVALID_FORMAT;
}

const char *
st_wren_connected_last_error(const st_wren_connected_t *connected)
{
	return connected == NULL || connected->last_error[0] == '\0' ? NULL
	    : connected->last_error;
}
