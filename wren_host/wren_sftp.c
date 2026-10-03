/* Provider-neutral connected Wren and asynchronous SFTP bindings. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "wren_host_internal.h"

#include "io_error.h"
#include "st_profile.h"
#include "st_time.h"
#include "string_compat.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct pending_request {
	uint64_t id;
	WrenHandle *fiber;
	struct pending_request *next;
};

struct pending_timer {
	uint64_t deadline;
	WrenHandle *fiber;
	struct pending_timer *next;
};

struct connected_hook {
	WrenHandle *function;
	int kind;
	struct connected_hook *next;
};

struct st_wren_connected {
	struct st_wren_context context;
	struct st_wren_host *host;
	WrenVM *vm;
	struct st_profile profile;
	struct st_wren_sftp_provider provider;
	struct pending_request *requests;
	struct pending_timer *timers;
	struct connected_hook *hooks;
	char last_error[512];
	bool shutting_down;
};

#define ST_WREN_SFTP_HANDLE_MAGIC UINT32_C(0x53544648)

struct foreign_sftp_handle {
	uint32_t magic;
	struct st_wren_connected *connected;
	uint64_t value;
	bool active;
};
struct foreign_sftp_stat { struct st_wren_sftp_stat value; };
struct foreign_sftp_entry {
	char *name;
	char *long_name;
	char *hash;
	size_t hash_size;
	struct st_wren_sftp_stat stat;
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
allocate_handle(WrenVM *vm)
{
	struct foreign_sftp_handle *value = wrenSetSlotNewForeign(vm, 0, 0,
	    sizeof(*value));
	*value = (struct foreign_sftp_handle){
		.magic = ST_WREN_SFTP_HANDLE_MAGIC,
	};
}

static void
finalize_handle(void *data)
{
	struct foreign_sftp_handle *handle = data;
	struct st_wren_sftp_request request;
	uint64_t request_id;

	if (handle->magic == ST_WREN_SFTP_HANDLE_MAGIC && handle->active
	    && handle->connected != NULL && !handle->connected->shutting_down
	    && handle->connected->provider.submit != NULL) {
		request = (struct st_wren_sftp_request){
			.operation = ST_WREN_SFTP_CLOSE,
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
slot_u64(WrenVM *vm, int slot, uint64_t *result)
{
	double value;

	if (wrenGetSlotType(vm, slot) != WREN_TYPE_NUM)
		return false;
	value = wrenGetSlotDouble(vm, slot);
	if (!isfinite(value) || value < 0 || trunc(value) != value
	    || value > (double)INT64_MAX)
		return false;
	*result = (uint64_t)value;
	return true;
}

static bool
slot_handle(WrenVM *vm, int slot, uint64_t *result)
{
	struct foreign_sftp_handle *handle;

	if (wrenGetSlotType(vm, slot) != WREN_TYPE_FOREIGN)
		return false;
	handle = wrenGetSlotForeign(vm, slot);
	if (handle == NULL || handle->magic != ST_WREN_SFTP_HANDLE_MAGIC
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

static bool
submit_request(WrenVM *vm, const struct st_wren_sftp_request *request)
{
	struct st_wren_connected *connected = connected_from_vm(vm);
	struct pending_request *pending;
	uint64_t id;
	int status;

	if (connected == NULL || connected->provider.submit == NULL) {
		push_error(vm, 0, IO_ERROR_NOT_SUPPORTED, 0,
		    "SFTP is unavailable", true);
		return false;
	}
	pending = calloc(1, sizeof(*pending));
	if (pending == NULL) {
		push_error(vm, 0, IO_ERROR_OUT_OF_MEMORY, 0, "Out of memory", true);
		return false;
	}
	status = connected->provider.submit(connected->provider.context,
	    request, &id);
	if (status != 0) {
		wrenReleaseHandle(vm, pending->fiber);
		free(pending);
		push_error(vm, 0, status, 0, "SFTP request was not queued", true);
		return false;
	}
	for (struct pending_request *other = connected->requests; other != NULL;
	    other = other->next) {
		if (other->id == id) {
			wrenReleaseHandle(vm, pending->fiber);
			free(pending);
			push_error(vm, 0, IO_ERROR_CONFLICT, 0,
			    "SFTP provider reused a pending request ID", true);
			return false;
		}
	}
	pending->fiber = wrenGetSlotHandle(vm, 1);
	if (pending->fiber == NULL) {
		free(pending);
		push_error(vm, 0, IO_ERROR_OUT_OF_MEMORY, 0, "Out of memory", true);
		return false;
	}
	pending->id = id;
	pending->next = connected->requests;
	connected->requests = pending;
	wrenSetSlotNull(vm, 0);
	return true;
}

static void
fn_sftp_available(WrenVM *vm)
{
	struct st_wren_connected *connected = connected_from_vm(vm);
	bool available = connected != NULL && connected->provider.available != NULL
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
	provider_string(vm, c == NULL ? NULL : c->provider.long_name_extension);
}

static bool
begin_path_request(WrenVM *vm, enum st_wren_sftp_operation operation,
	int path_slot)
{
	struct st_wren_sftp_request request = {.operation = operation};

	if (!slot_path(vm, path_slot, &request.path)) {
		st_wren_abort(vm, "SFTP: expected a path String without NUL");
		return false;
	}
	return submit_request(vm, &request);
}

static void fn_sftp_realpath(WrenVM *vm)
{ (void)begin_path_request(vm, ST_WREN_SFTP_REALPATH, 2); }
static void fn_sftp_stat(WrenVM *vm)
{ (void)begin_path_request(vm, ST_WREN_SFTP_STAT, 2); }
static void fn_sftp_opendir(WrenVM *vm)
{ (void)begin_path_request(vm, ST_WREN_SFTP_OPENDIR, 2); }
static void fn_sftp_mkdir(WrenVM *vm)
{ (void)begin_path_request(vm, ST_WREN_SFTP_MKDIR, 2); }
static void fn_sftp_rmdir(WrenVM *vm)
{ (void)begin_path_request(vm, ST_WREN_SFTP_RMDIR, 2); }
static void fn_sftp_remove(WrenVM *vm)
{ (void)begin_path_request(vm, ST_WREN_SFTP_REMOVE, 2); }
static void fn_sftp_descs(WrenVM *vm)
{ (void)begin_path_request(vm, ST_WREN_SFTP_DESCRIPTIONS, 2); }

static void
fn_sftp_open(WrenVM *vm)
{
	struct st_wren_sftp_request request = {.operation = ST_WREN_SFTP_OPEN};
	uint64_t flags;

	if (!slot_path(vm, 2, &request.path) || !slot_u64(vm, 3, &flags)
	    || flags > UINT32_MAX) {
		st_wren_abort(vm, "SFTP.open: invalid path or flags");
		return;
	}
	request.flags = (uint32_t)flags;
	(void)submit_request(vm, &request);
}

static void
fn_sftp_handle_only(WrenVM *vm, enum st_wren_sftp_operation operation)
{
	struct st_wren_sftp_request request = {.operation = operation};
	struct foreign_sftp_handle *handle;

	if (!slot_handle(vm, 2, &request.handle)) {
		st_wren_abort(vm, "SFTP: invalid handle");
		return;
	}
	handle = wrenGetSlotForeign(vm, 2);
	if (submit_request(vm, &request) && operation == ST_WREN_SFTP_CLOSE)
		handle->active = false;
}
static void fn_sftp_readdir(WrenVM *vm)
{ fn_sftp_handle_only(vm, ST_WREN_SFTP_READDIR); }
static void fn_sftp_close(WrenVM *vm)
{ fn_sftp_handle_only(vm, ST_WREN_SFTP_CLOSE); }

static void
fn_sftp_read(WrenVM *vm)
{
	struct st_wren_sftp_request request = {.operation = ST_WREN_SFTP_READ};
	uint64_t count;

	if (!slot_handle(vm, 2, &request.handle)
	    || !slot_u64(vm, 3, &count) || count > SIZE_MAX
	    || !slot_u64(vm, 4, &request.offset)) {
		st_wren_abort(vm, "SFTP.read: invalid handle, count, or offset");
		return;
	}
	request.size = (size_t)count;
	(void)submit_request(vm, &request);
}

static void
fn_sftp_write(WrenVM *vm)
{
	struct st_wren_sftp_request request = {.operation = ST_WREN_SFTP_WRITE};
	int size;

	if (!slot_handle(vm, 2, &request.handle)
	    || !slot_u64(vm, 3, &request.offset)
	    || wrenGetSlotType(vm, 4) != WREN_TYPE_STRING) {
		st_wren_abort(vm, "SFTP.write: invalid handle, offset, or bytes");
		return;
	}
	request.data = wrenGetSlotBytes(vm, 4, &size);
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
	struct st_wren_sftp_request request = {.operation = ST_WREN_SFTP_RENAME};

	if (!slot_path(vm, 2, &request.path)
	    || !slot_path(vm, 3, &request.second_path)) {
		st_wren_abort(vm, "SFTP.rename: invalid path");
		return;
	}
	(void)submit_request(vm, &request);
}

static void
fn_sftp_set_mtime(WrenVM *vm)
{
	struct st_wren_sftp_request request = {
		.operation = ST_WREN_SFTP_SET_MTIME,
	};

	if (!slot_path(vm, 2, &request.path)
	    || !slot_u64(vm, 3, &request.modified_time)) {
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
{ wrenSetSlotDouble(vm, 0, (double)stat_value(vm)->value.size); }
static void fn_stat_mtime(WrenVM *vm)
{ wrenSetSlotDouble(vm, 0, (double)stat_value(vm)->value.modified_time); }
static void fn_stat_atime(WrenVM *vm)
{ wrenSetSlotDouble(vm, 0, (double)stat_value(vm)->value.accessed_time); }
static void fn_stat_mode(WrenVM *vm)
{ wrenSetSlotDouble(vm, 0, stat_value(vm)->value.mode); }
static void fn_stat_uid(WrenVM *vm)
{ wrenSetSlotDouble(vm, 0, stat_value(vm)->value.uid); }
static void fn_stat_gid(WrenVM *vm)
{ wrenSetSlotDouble(vm, 0, stat_value(vm)->value.gid); }
static void fn_stat_string(WrenVM *vm)
{
	char value[96];
	(void)snprintf(value, sizeof(value), "SFTPStat(size=%llu)",
	    (unsigned long long)stat_value(vm)->value.size);
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
{ wrenSetSlotDouble(vm, 0, (double)entry_value(vm)->stat.size); }
static void fn_entry_mtime(WrenVM *vm)
{ wrenSetSlotDouble(vm, 0, (double)entry_value(vm)->stat.modified_time); }
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
{ wrenSetSlotString(vm, 0, entry_value(vm)->name); }

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
	(void)snprintf(value, sizeof(value), "SFTPError %d/%u: %s",
	    error->code, error->server_status,
	    error->message == NULL ? "operation failed" : error->message);
	wrenSetSlotString(vm, 0, value);
}

static void
fn_timer_trigger(WrenVM *vm)
{
	struct st_wren_connected *connected = connected_from_vm(vm);
	struct pending_timer *timer;
	uint64_t milliseconds;
	uint64_t now;

	if (connected == NULL || !slot_u64(vm, 2, &milliseconds)
	    || st_time_monotonic_ms(&now) != 0) {
		st_wren_abort(vm, "Timer.trigger: invalid fiber or delay");
		return;
	}
	timer = calloc(1, sizeof(*timer));
	if (timer == NULL) {
		st_wren_abort(vm, "Timer.trigger: out of memory");
		return;
	}
	timer->fiber = wrenGetSlotHandle(vm, 1);
	if (timer->fiber == NULL) {
		free(timer);
		st_wren_abort(vm, "Timer.trigger: out of memory");
		return;
	}
	timer->deadline = UINT64_MAX - now < milliseconds ? UINT64_MAX
	    : now + milliseconds;
	timer->next = connected->timers;
	connected->timers = timer;
	wrenSetSlotNull(vm, 0);
}

static void fn_cterm_refresh(WrenVM *vm) { wrenSetSlotNull(vm, 0); }
static void fn_cterm_sftp_active(WrenVM *vm)
{ wrenSetSlotBool(vm, 0, st_wren_context_from_vm(vm)->cterm_sftp_active); }
static void fn_cterm_sftp_active_set(WrenVM *vm)
{
	if (wrenGetSlotType(vm, 1) != WREN_TYPE_BOOL) {
		st_wren_abort(vm, "CTerm.sftpActive expects a Bool");
		return;
	}
	st_wren_context_from_vm(vm)->cterm_sftp_active = wrenGetSlotBool(vm, 1);
	wrenSetSlotBool(vm, 0, st_wren_context_from_vm(vm)->cterm_sftp_active);
}

static void
register_hook(WrenVM *vm, int function_slot, int kind)
{
	struct st_wren_connected *connected = connected_from_vm(vm);
	struct connected_hook *hook;

	if (connected == NULL) {
		wrenSetSlotNull(vm, 0);
		return;
	}
	hook = calloc(1, sizeof(*hook));
	if (hook == NULL) {
		st_wren_abort(vm, "Hook registration failed");
		return;
	}
	hook->function = wrenGetSlotHandle(vm, function_slot);
	if (hook->function == NULL) {
		free(hook);
		st_wren_abort(vm, "Hook registration failed");
		return;
	}
	hook->kind = kind;
	hook->next = connected->hooks;
	connected->hooks = hook;
	wrenSetSlotNull(vm, 0);
}
static void fn_hook_on_key(WrenVM *vm) { register_hook(vm, 1, 1); }
static void fn_hook_on_key_value(WrenVM *vm) { register_hook(vm, 2, 1); }
static void fn_hook_shell(WrenVM *vm) { register_hook(vm, 1, 2); }
static void fn_hook_disconnect(WrenVM *vm) { register_hook(vm, 1, 3); }

static const struct sftp_binding bindings[] = {
	{"SFTP", true, "available", fn_sftp_available},
	{"SFTP", true, "pubdir", fn_sftp_pubdir},
	{"SFTP", true, "lname", fn_sftp_lname},
	{"SFTP", true, "realpath(_,_)", fn_sftp_realpath},
	{"SFTP", true, "stat(_,_)", fn_sftp_stat},
	{"SFTP", true, "opendir(_,_)", fn_sftp_opendir},
	{"SFTP", true, "readdir(_,_)", fn_sftp_readdir},
	{"SFTP", true, "close(_,_)", fn_sftp_close},
	{"SFTP", true, "open(_,_,_)", fn_sftp_open},
	{"SFTP", true, "read(_,_,_,_)", fn_sftp_read},
	{"SFTP", true, "write(_,_,_,_)", fn_sftp_write},
	{"SFTP", true, "mkdir(_,_)", fn_sftp_mkdir},
	{"SFTP", true, "rmdir(_,_)", fn_sftp_rmdir},
	{"SFTP", true, "remove(_,_)", fn_sftp_remove},
	{"SFTP", true, "rename(_,_,_)", fn_sftp_rename},
	{"SFTP", true, "setMtime(_,_,_)", fn_sftp_set_mtime},
	{"SFTP", true, "descs(_,_)", fn_sftp_descs},
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
	{"Timer", true, "trigger(_,_)", fn_timer_trigger},
	{"CTerm", true, "refreshStatus()", fn_cterm_refresh},
	{"CTerm", true, "sftpActive", fn_cterm_sftp_active},
	{"CTerm", true, "sftpActive=(_)", fn_cterm_sftp_active_set},
	{"Hook", true, "onKey(_)", fn_hook_on_key},
	{"Hook", true, "onKey(_,_)", fn_hook_on_key_value},
	{"Hook", true, "onShellClose(_)", fn_hook_shell},
	{"Hook", true, "onDisconnect(_)", fn_hook_disconnect},
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
push_response(WrenVM *vm, int slot,
	const struct st_wren_sftp_response *response)
{
	switch (response->kind) {
	case ST_WREN_SFTP_RESULT_OK:
		wrenSetSlotNull(vm, slot);
		return true;
	case ST_WREN_SFTP_RESULT_EOF:
		wrenSetSlotNull(vm, slot);
		return true;
	case ST_WREN_SFTP_RESULT_STRING:
	case ST_WREN_SFTP_RESULT_BYTES:
		wrenSetSlotBytes(vm, slot, response->data, response->size);
		return true;
	case ST_WREN_SFTP_RESULT_HANDLE: {
		struct foreign_sftp_handle *handle;
		wrenEnsureSlots(vm, slot + 2);
		wrenGetVariable(vm, "syncterm", "SFTPHandle", slot + 1);
		handle = wrenSetSlotNewForeign(vm, slot, slot + 1,
		    sizeof(*handle));
		*handle = (struct foreign_sftp_handle){
			.magic = ST_WREN_SFTP_HANDLE_MAGIC,
			.connected = connected_from_vm(vm),
			.value = response->handle,
			.active = true,
		};
		return true;
	}
	case ST_WREN_SFTP_RESULT_STAT: {
		struct foreign_sftp_stat *stat;
		wrenEnsureSlots(vm, slot + 2);
		wrenGetVariable(vm, "syncterm", "SFTPStat", slot + 1);
		stat = wrenSetSlotNewForeign(vm, slot, slot + 1, sizeof(*stat));
		stat->value = response->stat;
		return true;
	}
	case ST_WREN_SFTP_RESULT_ENTRIES:
		wrenEnsureSlots(vm, slot + 3);
		wrenSetSlotNewList(vm, slot);
		for (size_t i = 0; i < response->entry_count; ++i) {
			const struct st_wren_sftp_entry *source = &response->entries[i];
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
	case ST_WREN_SFTP_RESULT_ERROR:
		push_error(vm, slot, response->error_code, response->server_status,
		    response->message, response->transient);
		return true;
	}
	return false;
}

static int
deliver_response(struct st_wren_connected *connected,
	const struct st_wren_sftp_response *response)
{
	struct pending_request *pending = take_request(connected,
	    response->request_id);
	WrenVM *vm = connected->vm;
	int status;

	if (pending == NULL)
		return 0;
	wrenEnsureSlots(vm, 3);
	wrenSetSlotHandle(vm, 1, pending->fiber);
	if (!push_response(vm, 2, response)) {
		wrenReleaseHandle(vm, pending->fiber);
		free(pending);
		return IO_ERROR_INVALID_FORMAT;
	}
	status = st_wren_wake_enqueue(vm, 1, 2);
	wrenReleaseHandle(vm, pending->fiber);
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
		wrenEnsureSlots(connected->vm, 4);
		wrenSetSlotHandle(connected->vm, 1, timer->fiber);
		wrenGetVariable(connected->vm, "syncterm", "TimerElapsed", 3);
		(void)wrenSetSlotNewForeign(connected->vm, 2, 3, 1);
		if (st_wren_wake_enqueue(connected->vm, 1, 2) != 0)
			status = IO_ERROR_OUT_OF_MEMORY;
		wrenReleaseHandle(connected->vm, timer->fiber);
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

int
st_wren_connected_create(st_wren_connected_t **result,
	const struct st_wren_connected_config *config)
{
	struct st_wren_connected *connected;
	WrenConfiguration wren_config;
	int status;

	if (result == NULL || config == NULL || config->host == NULL
	    || config->console == NULL || config->profile == NULL
	    || config->sftp == NULL || config->sftp->submit == NULL
	    || config->sftp->poll == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	connected = calloc(1, sizeof(*connected));
	if (connected == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	connected->host = config->host;
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
	connected->context.event = "connected";
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
	WrenHandle *call = wrenMakeCallHandle(connected->vm, "call()");
	int status = 0;

	if (call == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	for (struct connected_hook *hook = connected->hooks; hook != NULL;
	    hook = hook->next) {
		if (hook->kind != kind)
			continue;
		wrenEnsureSlots(connected->vm, 1);
		wrenSetSlotHandle(connected->vm, 0, hook->function);
		if (wrenCall(connected->vm, call) != WREN_RESULT_SUCCESS)
			status = IO_ERROR_INVALID_FORMAT;
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

void
st_wren_connected_destroy(st_wren_connected_t *connected)
{
	if (connected == NULL)
		return;
	if (connected->vm != NULL)
		(void)run_hooks(connected, 3);
	connected->shutting_down = true;
	if (connected->provider.cancel_all != NULL)
		connected->provider.cancel_all(connected->provider.context);
	while (connected->requests != NULL) {
		struct pending_request *next = connected->requests->next;
		if (connected->vm != NULL)
			wrenReleaseHandle(connected->vm, connected->requests->fiber);
		free(connected->requests);
		connected->requests = next;
	}
	while (connected->timers != NULL) {
		struct pending_timer *next = connected->timers->next;
		if (connected->vm != NULL)
			wrenReleaseHandle(connected->vm, connected->timers->fiber);
		free(connected->timers);
		connected->timers = next;
	}
	while (connected->hooks != NULL) {
		struct connected_hook *next = connected->hooks->next;
		if (connected->vm != NULL)
			wrenReleaseHandle(connected->vm, connected->hooks->function);
		free(connected->hooks);
		connected->hooks = next;
	}
	if (connected->vm != NULL) {
		st_wren_input_context_dispose(connected->vm);
		st_wren_log_dispose(connected->vm);
		wrenFreeVM(connected->vm);
	}
	st_profile_dispose(&connected->profile);
	free(connected);
}

int
st_wren_connected_pump(st_wren_connected_t *connected, uint32_t timeout_ms)
{
	struct st_wren_sftp_response response;
	int status;
	bool first = true;
	uint64_t now;

	if (connected == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (connected->timers != NULL && st_time_monotonic_ms(&now) == 0) {
		for (struct pending_timer *timer = connected->timers; timer != NULL;
		    timer = timer->next) {
			uint64_t remaining = timer->deadline > now
			    ? timer->deadline - now : 0;

			if (remaining < timeout_ms)
				timeout_ms = (uint32_t)remaining;
		}
	}
	do {
		status = connected->provider.poll(connected->provider.context,
		    &response, first ? timeout_ms : 0);
		first = false;
		if (status < 0)
			return status;
		if (status > 0) {
			status = deliver_response(connected, &response);
			if (status != 0)
				return status;
		}
	} while (status > 0);
	status = deliver_timers(connected);
	if (status == 0)
		status = st_wren_wake_drain(connected->vm);
	return status;
}

int
st_wren_connected_interpret(st_wren_connected_t *connected,
	const char *module, const char *source)
{
	if (connected == NULL || module == NULL || source == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	connected->last_error[0] = '\0';
	return wrenInterpret(connected->vm, module, source) == WREN_RESULT_SUCCESS
	    ? 0 : IO_ERROR_INVALID_FORMAT;
}

const char *
st_wren_connected_last_error(const st_wren_connected_t *connected)
{
	return connected == NULL || connected->last_error[0] == '\0' ? NULL
	    : connected->last_error;
}
