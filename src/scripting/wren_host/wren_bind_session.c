/* Connected-session state exposed to Wren UI scripts. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "wren_host_internal.h"

#include "io_error.h"
#include "st_connection.h"
#include "st_ooii.h"
#include "st_term.h"
#include "st_time.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct foreign_conn_error {
	int code;
	bool partial;
	char message[96];
};

enum cterm_error_operation {
	CTERM_ERROR_WRITE,
	CTERM_ERROR_KEY,
	CTERM_ERROR_RESET,
};

struct foreign_cterm_error {
	int code;
	enum cterm_error_operation operation;
	bool handled;
	size_t consumed;
	char message[160];
};

struct st_wren_wire {
	struct st_wren_context *context;
};

static void
finalize_wire(void *data)
{
	struct st_wren_wire *wire = data;
	if (wire->context != NULL) {
		(void)st_term_session_release_wire(wire->context->session, wire);
		wire->context->wire = NULL;
		wire->context = NULL;
	}
}
static void
allocate_wire(WrenVM *vm)
{
	struct st_wren_wire *wire = wrenSetSlotNewForeign(vm, 0, 0, sizeof(*wire));
	*wire = (struct st_wren_wire){0};
}

static void
allocate_conn_error(WrenVM *vm)
{
	struct foreign_conn_error *error = wrenSetSlotNewForeign(vm, 0, 0,
	    sizeof(*error));
	*error = (struct foreign_conn_error){0};
}

static void
allocate_cterm_state(WrenVM *vm)
{
	struct st_cterm_state *state = wrenSetSlotNewForeign(vm, 0, 0,
	    sizeof(*state));
	*state = (struct st_cterm_state){0};
}

static void
allocate_cterm_error(WrenVM *vm)
{
	struct foreign_cterm_error *error = wrenSetSlotNewForeign(vm, 0, 0,
	    sizeof(*error));
	*error = (struct foreign_cterm_error){0};
}

WrenForeignClassMethods
st_wren_session_bind_class(const char *class_name)
{
	WrenForeignClassMethods methods = {NULL, NULL};

	if (strcmp(class_name, "WireLease") == 0)
		return (WrenForeignClassMethods){allocate_wire, finalize_wire};
	if (strcmp(class_name, "ConnError") == 0)
		methods.allocate = allocate_conn_error;
	else if (strcmp(class_name, "CTermError") == 0)
		methods.allocate = allocate_cterm_error;
	else if (strcmp(class_name, "ExtAttr") == 0
	    || strcmp(class_name, "LastColumnFlag") == 0)
		methods.allocate = allocate_cterm_state;
	return methods;
}

enum { CONN_DOWN = 1, CONN_IO, CONN_FULL, CONN_TEXT, CONN_BUSY };

static void
push_conn_error(WrenVM *vm, int code, bool partial, const char *message)
{
	struct foreign_conn_error *error;

	/* Slot zero may hold the returned Operation. */
	wrenEnsureSlots(vm, 3);
	wrenGetVariable(vm, "syncterm", "ConnError", 2);
	error = wrenSetSlotNewForeign(vm, 1, 2, sizeof(*error));
	*error = (struct foreign_conn_error){.code = code, .partial = partial};
	(void)snprintf(error->message, sizeof(error->message), "%s", message);
}

static struct foreign_conn_error *
conn_error(WrenVM *vm)
{
	return wrenGetSlotForeign(vm, 0);
}

static void fn_conn_error_code(WrenVM *vm)
{ wrenSetSlotDouble(vm, 0, conn_error(vm)->code); }
static void fn_conn_error_partial(WrenVM *vm)
{ wrenSetSlotBool(vm, 0, conn_error(vm)->partial); }
static void fn_conn_error_message(WrenVM *vm)
{ wrenSetSlotString(vm, 0, conn_error(vm)->message); }
static void fn_conn_error_string(WrenVM *vm)
{
	struct foreign_conn_error *error = conn_error(vm);
	static const char *const names[] = {
		"UNKNOWN", "NOT_CONNECTED", "IO", "QUEUE_FULL", "INVALID_TEXT", "BUSY",
	};
	char formatted[160];

	(void)snprintf(formatted, sizeof(formatted), "ConnError: %s: %s",
	    error->code >= CONN_DOWN && error->code <= CONN_BUSY
	    ? names[error->code] : names[0], error->message);
	wrenSetSlotString(vm, 0, formatted);
}

static void
push_cterm_error(WrenVM *vm, int code, enum cterm_error_operation operation,
	size_t consumed, bool handled)
{
	static const char *const reasons[] = {
		"Invalid argument", "Invalid encoding", "Not found", "Permission denied",
		"Already exists", "Not a directory", "Is a directory", "Directory not empty",
		"Out of memory", "No space available", "Name too long", "Out of range",
		"Busy", "Read-only", "Cross-device operation", "Too many links",
		"Interrupted", "Not supported", "I/O failure", "Timed out", "Cancelled",
		"Would block", "In progress", "Name resolution failed", "Connection refused",
		"Connection reset", "Connection aborted", "Not connected", "Network unreachable",
		"Host unreachable", "Address in use", "Address not available", "Message too large",
		"Protocol failure", "Password required", "Decryption failed", "Invalid format", "Conflict",
	};
	wrenEnsureSlots(vm, 2);
	wrenGetVariable(vm, "syncterm", "CTermError", 1);
	struct foreign_cterm_error *error = wrenSetSlotNewForeign(vm, 0, 1,
	    sizeof(*error));
	*error = (struct foreign_cterm_error){.code = code, .operation = operation,
	    .consumed = consumed, .handled = handled};
	(void)snprintf(error->message, sizeof(error->message), "CTerm.%s failed: %s.",
	    operation == CTERM_ERROR_WRITE ? "write"
	    : operation == CTERM_ERROR_KEY ? "sendKey" : "reset",
	    code <= -1 && code >= -38
	    ? reasons[-code - 1] : "Native terminal failure");
}

static struct foreign_cterm_error *
cterm_error(WrenVM *vm)
{
	return st_wren_foreign_require(vm, 0, "syncterm", "CTermError");
}
static void fn_cterm_error_code(WrenVM *vm)
{
	struct foreign_cterm_error *error = cterm_error(vm);
	if (error != NULL) wrenSetSlotDouble(vm, 0, error->code);
}
static void fn_cterm_error_message(WrenVM *vm)
{
	struct foreign_cterm_error *error = cterm_error(vm);
	if (error != NULL) wrenSetSlotString(vm, 0, error->message);
}
static void fn_cterm_error_consumed(WrenVM *vm)
{
	struct foreign_cterm_error *error = cterm_error(vm);
	if (error == NULL) return;
	if (error->operation == CTERM_ERROR_WRITE) st_wren_return_u64(vm, error->consumed);
	else wrenSetSlotNull(vm, 0);
}
static void fn_cterm_error_handled(WrenVM *vm)
{
	struct foreign_cterm_error *error = cterm_error(vm);
	if (error == NULL) return;
	if (error->operation == CTERM_ERROR_KEY) wrenSetSlotBool(vm, 0, error->handled);
	else wrenSetSlotNull(vm, 0);
}
static void fn_cterm_error_string(WrenVM *vm)
{
	struct foreign_cterm_error *error = cterm_error(vm);
	char text[208];
	if (error == NULL) return;
	(void)snprintf(text, sizeof(text), "CTermError(%d): %s", error->code,
	    error->message);
	wrenSetSlotString(vm, 0, text);
}

#define SEND_COUNT_MAX 64U
#define SEND_STORAGE_MAX (1024U * 1024U)

struct st_wren_send {
	struct st_wren_send *next;
	struct st_wren_operation *operation;
	struct st_term_encoded_text text;
	size_t position;
	enum st_connection_send_kind kind;
	struct st_wren_wire *wire;
	WrenHandle *owner;
};

static void
send_remove(struct st_wren_context *context, struct st_wren_send *send,
	struct st_wren_send *previous)
{
	if (previous == NULL)
		context->send_head = send->next;
	else
		previous->next = send->next;
	if (context->send_tail == send)
		context->send_tail = previous;
	--context->send_count;
	context->send_storage -= send->text.storage_size;
	if (send->owner != NULL) wrenReleaseHandle(context->vm, send->owner);
	free(send->text.bytes);
	free(send);
}

static void
send_cancel(void *data, struct st_wren_operation *operation)
{
	struct st_wren_context *context = data;
	struct st_wren_send *previous = NULL;

	for (struct st_wren_send *send = context->send_head; send != NULL;
	    previous = send, send = send->next) {
		if (send->operation == operation) {
			send_remove(context, send, previous);
			return;
		}
	}
}

void
st_wren_send_dispose(struct st_wren_context *context)
{
	while (context->send_head != NULL)
		send_remove(context, context->send_head, NULL);
}

static bool
release_wire(struct st_wren_wire *wire)
{
	struct st_wren_context *context = wire->context;
	if (context == NULL) return false;
	(void)st_term_session_release_wire(context->session, wire);
	context->wire = NULL;
	wire->context = NULL;
	/* Cancelling removes each node. Its accepted prefix remains queued. */
	for (;;) {
		struct st_wren_send *send = context->send_head;
		while (send != NULL && send->wire != wire) send = send->next;
		if (send == NULL) break;
		if (st_wren_operation_cancel(context->vm, send->operation) != 0) break;
	}
	return true;
}

void
st_wren_wire_dispose(struct st_wren_context *context)
{
	/* Teardown separately cancels all Operations without resuming bodies. */
	if (context->wire != NULL) finalize_wire(context->wire);
}

static bool
send_busy(struct st_wren_context *context)
{
	struct st_transfer_inspection state;
	bool reserved;

	return st_term_session_inspect_transfer(context->session, &state, &reserved)
	    || reserved;
}

int
st_wren_send_pump(struct st_wren_context *context)
{
	WrenVM *vm = context->vm;

	/* Bound work per terminal iteration. Each transport submission accepts
	 * complete encoded scalars, and never a positive partial unit. */
	for (unsigned batch = 0; batch < 64 && context->send_head != NULL; ++batch) {
		struct st_wren_send *send = context->send_head;
		struct st_wren_operation *operation = send->operation;
		int code = 0;
		const char *message = NULL;

		if (!st_connection_is_connected(context->connection)) {
			code = CONN_DOWN;
			message = "connection is down";
		}
		else if (send_busy(context)) {
			code = CONN_BUSY;
			message = "connection input/output is owned";
		}
		else if (send->position < send->text.size) {
			size_t amount = send->text.size - send->position;
			if (amount > ST_CONNECTION_ATOMIC_SEND_MAX) {
				amount = ST_CONNECTION_ATOMIC_SEND_MAX;
				if (send->text.utf8) {
					while ((send->text.bytes[send->position + amount] & 0xc0) == 0x80)
						--amount;
				}
			}
			ssize_t written = st_connection_try_send_atomic(context->connection,
			    send->text.bytes + send->position, amount,
			    send->kind);
			if (written == IO_ERROR_WOULD_BLOCK)
				return 0;
			if (written == (ssize_t)amount) {
				send->position += amount;
				if (send->position < send->text.size)
					continue;
			}
			else {
				code = written == IO_ERROR_NOT_CONNECTED ? CONN_DOWN : CONN_IO;
				message = "connection could not accept the remaining output";
			}
		}
		wrenEnsureSlots(vm, 2);
		if (code != 0)
			push_conn_error(vm, code, send->position != 0, message);
		else
			wrenSetSlotNull(vm, 1);
		/* Resolution queues wakes; no Wren callback runs inside this pump. */
		bool partial = send->position != 0;
		WrenHandle *owner = send->owner;
		struct st_wren_wire *wire = send->wire;
		send->owner = NULL;
		send_remove(context, send, NULL);
		if (code != 0 && wire != NULL) {
			(void)release_wire(wire);
			push_conn_error(vm, code, partial, message);
		}
		int status = st_wren_operation_resolve(vm, operation, 1);
		if (owner != NULL) wrenReleaseHandle(vm, owner);
		if (status != 0)
			return status;
	}
	return 0;
}

static void
send_reject(WrenVM *vm, struct st_wren_wire *wire, int code, const char *message)
{
	if (wire != NULL) (void)release_wire(wire);
	struct st_wren_operation *operation = st_wren_operation_new(vm, 0);

	push_conn_error(vm, code, false, message);
	if (st_wren_operation_resolve(vm, operation, 1) != 0)
		st_wren_abort(vm, "out of memory completing send");
}

static void
conn_send(WrenVM *vm, enum st_connection_send_kind kind, struct st_wren_wire *wire)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);
	struct st_term_encoded_text text = {0};
	struct st_wren_send *send;
	int length;
	const char *value;

	if (wrenGetSlotType(vm, 1) != WREN_TYPE_STRING) {
		st_wren_abort(vm, "send expects a String");
		return;
	}
	value = wrenGetSlotBytes(vm, 1, &length);
	if (context->connection == NULL || context->session == NULL
	    || !st_connection_is_connected(context->connection)) {
		send_reject(vm, wire, CONN_DOWN, "connection is down");
		return;
	}
	if ((wire != NULL && wire->context != context) || (context->wire != NULL && context->wire != wire) || send_busy(context)) {
		send_reject(vm, wire, CONN_BUSY, "connection input/output is owned");
		return;
	}
	if (context->send_count >= SEND_COUNT_MAX || (size_t)length >= SEND_STORAGE_MAX) {
		send_reject(vm, wire, CONN_FULL, "script send queue limit reached");
		return;
	}
	if (kind != ST_CONNECTION_SEND_TEXT) {
		text.bytes = malloc((size_t)length + 1);
		if (text.bytes != NULL)
			memcpy(text.bytes, value, (size_t)length);
		text.size = (size_t)length;
		text.storage_size = (size_t)length + 1;
	}
	else {
		int status = st_term_session_encode_text(context->session, value,
		    (size_t)length, &text);
		if (status == IO_ERROR_INVALID_ENCODING) {
			send_reject(vm, wire, CONN_TEXT, "malformed or unmappable Unicode text");
			return;
		}
		if (status != 0) {
			st_wren_abort(vm, "could not encode script text");
			return;
		}
	}
	if (text.bytes == NULL) {
		st_wren_abort(vm, "out of memory preparing send");
		return;
	}
	if (text.storage_size > SEND_STORAGE_MAX - context->send_storage) {
		free(text.bytes);
		send_reject(vm, wire, CONN_FULL, "script send storage limit reached");
		return;
	}
	send = calloc(1, sizeof(*send));
	if (send == NULL) {
		free(text.bytes);
		st_wren_abort(vm, "out of memory queueing send");
		return;
	}
	/* Copy/encode before creating the Operation: slot one holds the caller's
	 * String and operation allocation reuses it and can collect that String. */
	send->text = text;
	send->kind = kind;
	send->wire = wire;
	if (wire != NULL) send->owner = wrenGetSlotHandle(vm, 0);
	send->operation = st_wren_operation_new(vm, 0);
	st_wren_operation_on_cancel(send->operation, send_cancel, context);
	if (context->send_tail == NULL)
		context->send_head = send;
	else
		context->send_tail->next = send;
	context->send_tail = send;
	++context->send_count;
	context->send_storage += text.storage_size;
	if (st_wren_send_pump(context) != 0)
		st_wren_abort(vm, "out of memory completing send");
}

static void fn_conn_send(WrenVM *vm) { conn_send(vm, ST_CONNECTION_SEND_TEXT, NULL); }
static void fn_conn_send_bytes(WrenVM *vm) { conn_send(vm, ST_CONNECTION_SEND_BYTES, NULL); }

static void
fn_conn_connected(WrenVM *vm)
{
	struct st_connection *connection = st_wren_context_from_vm(vm)->connection;
	wrenSetSlotBool(vm, 0, connection != NULL
	    && st_connection_is_connected(connection));
}

static void
fn_conn_type(WrenVM *vm)
{
	wrenSetSlotDouble(vm, 0,
	    st_wren_context_from_vm(vm)->profile->connection_type);
}

static void
fn_conn_close(WrenVM *vm)
{
	st_term_session_request_end(st_wren_context_from_vm(vm)->session,
	    false);
	wrenSetSlotNull(vm, 0);
}

static struct st_wren_wire *
wire_value(WrenVM *vm)
{
	struct st_wren_wire *wire = st_wren_foreign_require(vm, 0, "syncterm", "WireLease");
	if (wire != NULL && wire->context != NULL
	    && wire->context != st_wren_context_from_vm(vm)) {
		st_wren_abort(vm, "WireLease: invalid owner");
		return NULL;
	}
	return wire;
}
static void fn_wire_release(WrenVM *vm)
{
	struct st_wren_wire *wire = wire_value(vm);
	if (wire != NULL) wrenSetSlotBool(vm, 0, release_wire(wire));
}
static void fn_wire_released(WrenVM *vm)
{
	struct st_wren_wire *wire = wire_value(vm);
	if (wire != NULL) wrenSetSlotBool(vm, 0, wire->context == NULL);
}
static void fn_wire_send_bytes(WrenVM *vm)
{
	struct st_wren_wire *wire = wire_value(vm);
	if (wire != NULL) conn_send(vm, ST_CONNECTION_SEND_BYTES, wire);
}
static void fn_wire_send(WrenVM *vm)
{
	struct st_wren_wire *wire = wire_value(vm);
	if (wire != NULL) conn_send(vm, ST_CONNECTION_SEND_WIRE, wire);
}
static void
fn_claim_wire(WrenVM *vm)
{
	struct st_wren_context *context = st_wren_session_context(vm);
	if (context == NULL) return;
	if (context->cleanup_closing || !st_connection_is_connected(context->connection)) {
		push_conn_error(vm, CONN_DOWN, false, "session is closing");
		WrenHandle *result = wrenGetSlotHandle(vm, 1);
		wrenSetSlotHandle(vm, 0, result);
		wrenReleaseHandle(vm, result);
		return;
	}
	if (context->wire != NULL || context->send_head != NULL) {
		push_conn_error(vm, CONN_BUSY, false, "connection already has pending work or an owner");
		WrenHandle *result = wrenGetSlotHandle(vm, 1);
		wrenSetSlotHandle(vm, 0, result);
		wrenReleaseHandle(vm, result);
		return;
	}
	wrenEnsureSlots(vm, 2);
	wrenGetVariable(vm, "syncterm", "WireLease", 1);
	struct st_wren_wire *wire = wrenSetSlotNewForeign(vm, 0, 1, sizeof(*wire));
	*wire = (struct st_wren_wire){0};
	int status = st_term_session_claim_wire(context->session, wire);
	if (status != 0) {
		push_conn_error(vm, CONN_BUSY, false, "terminal input is owned or being dispatched");
		WrenHandle *result = wrenGetSlotHandle(vm, 1);
		wrenSetSlotHandle(vm, 0, result);
		wrenReleaseHandle(vm, result);
		return;
	}
	wire->context = context;
	context->wire = wire;
}
static void
wire_inspect(WrenVM *vm, bool consume, bool count_only)
{
	struct st_wren_wire *wire = wire_value(vm);
	uint64_t requested = 0;
	if (wire == NULL) return;
	if (!count_only && (!st_wren_number_u64(vm, 1, &requested) || requested > 65536)) {
		st_wren_abort(vm, "WireLease read count must be an integer from 0 through 65536");
		return;
	}
	if (wire->context == NULL) {
		push_conn_error(vm, CONN_DOWN, false, "wire lease is released");
		WrenHandle *result = wrenGetSlotHandle(vm, 1);
		wrenSetSlotHandle(vm, 0, result);
		wrenReleaseHandle(vm, result);
		return;
	}
	struct st_wren_context *context = wire->context;
	size_t available;
	(void)st_term_session_wire_available(context->session, wire, &available);
	if (count_only) { st_wren_return_u64(vm, available); return; }
	uint8_t buffer[65536];
	ssize_t count = st_term_session_read_wire(context->session, wire, buffer, (size_t)requested, consume);
	if (count == IO_ERROR_WOULD_BLOCK || requested == 0) wrenSetSlotBytes(vm, 0, "", 0);
	else if (count == 0) { (void)release_wire(wire); wrenSetSlotNull(vm, 0); }
	else if (count > 0) wrenSetSlotBytes(vm, 0, (const char *)buffer, (size_t)count);
	else {
		(void)release_wire(wire);
		push_conn_error(vm, CONN_IO, false, "wire input failed");
		WrenHandle *result = wrenGetSlotHandle(vm, 1);
		wrenSetSlotHandle(vm, 0, result);
		wrenReleaseHandle(vm, result);
	}
}
static void fn_wire_read(WrenVM *vm) { wire_inspect(vm, true, false); }
static void fn_wire_peek(WrenVM *vm) { wire_inspect(vm, false, false); }
static void fn_wire_available(WrenVM *vm) { wire_inspect(vm, false, true); }

static void
fn_conn_end_session(WrenVM *vm)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);

	if (wrenGetSlotType(vm, 1) != WREN_TYPE_BOOL) {
		st_wren_abort(vm, "Conn.endSession expects a Bool");
		return;
	}
	st_term_session_request_end(context->session, wrenGetSlotBool(vm, 1));
	wrenSetSlotNull(vm, 0);
}

static void
fn_conn_scrollback(WrenVM *vm)
{
	st_wren_context_from_vm(vm)->scrollback_requested = true;
	wrenSetSlotNull(vm, 0);
}

static void
fn_conn_paste(WrenVM *vm)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);
	char *clipboard = NULL;
	char *prepared = NULL;
	size_t prepared_size;
	int status;

	wrenSetSlotNull(vm, 0);
	if (context->session == NULL || context->connection == NULL
	    || !st_connection_is_connected(context->connection)
	    || st_console_get_clipboard_text(context->console, &clipboard) != 0
	    || clipboard == NULL)
		return;
	if (clipboard[0] == '\0') {
		free(clipboard);
		return;
	}
	status = st_term_prepare_paste(&prepared, &prepared_size, clipboard,
	    strlen(clipboard), st_cterm_bracketed_paste(context->cterm));
	free(clipboard);
	if (status == 0)
		{
		wrenEnsureSlots(vm, 2);
		wrenSetSlotBytes(vm, 1, prepared, prepared_size);
		conn_send(vm, ST_CONNECTION_SEND_TEXT, NULL);
	}
	free(prepared);
	if (status != 0)
		st_wren_abort(vm, "Conn.paste failed to send clipboard text");
}

static void
fn_conn_queued(WrenVM *vm)
{
	wrenSetSlotDouble(vm, 0, (double)st_connection_queued(
	    st_wren_context_from_vm(vm)->connection));
}

static void
fn_conn_session_log(WrenVM *vm)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);
	char *text = NULL;
	if (context->session_log == NULL) {
		wrenSetSlotNull(vm, 0);
		return;
	}
	int status = context->session_log(context->session_log_context, &text);
	if (status != 0 || text == NULL)
		st_wren_abort(vm, "Unable to copy session log");
	else
		wrenSetSlotString(vm, 0, text);
	free(text);
}

static void
fn_conn_elapsed_seconds(WrenVM *vm)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);
	uint64_t now;

	if (context->connection != NULL) {
		wrenSetSlotDouble(vm, 0,
		    (double)(st_connection_elapsed_ms(context->connection) / 1000));
		return;
	}
	if (context->connected_at_ms == 0 || st_time_monotonic_ms(&now) != 0
	    || now < context->connected_at_ms)
		wrenSetSlotDouble(vm, 0, 0);
	else
		wrenSetSlotDouble(vm, 0,
		    (double)((now - context->connected_at_ms) / 1000));
}

static void
fn_scrollback_width(WrenVM *vm)
{
	uint32_t columns = 0;
	size_t rows;

	(void)st_cterm_scrollback_size(st_wren_context_from_vm(vm)->cterm,
	    &columns, &rows);
	wrenSetSlotDouble(vm, 0, columns);
}

static void
fn_scrollback_height(WrenVM *vm)
{
	uint32_t columns = 0;
	size_t rows = 0;

	(void)st_cterm_scrollback_size(st_wren_context_from_vm(vm)->cterm,
	    &columns, &rows);
	wrenSetSlotDouble(vm, 0, (double)rows);
}

static bool
slot_signed_row(WrenVM *vm, int slot, int64_t *value)
{
	double number;

	if (wrenGetSlotType(vm, slot) != WREN_TYPE_NUM
	    || !isfinite(number = wrenGetSlotDouble(vm, slot))
	    || number != floor(number)
	    || number < INT32_MIN || number > INT32_MAX)
		return false;
	*value = (int64_t)number;
	return true;
}

static void
fn_scrollback_copy_rows(WrenVM *vm)
{
	struct st_console_cell *cells;
	uint32_t width;
	uint32_t height;
	int64_t source;
	int64_t count;
	int64_t destination;
	int copied;

	if (!st_wren_surface_view(vm, 1, &cells, &width, &height)
	    || !slot_signed_row(vm, 2, &source)
	    || !slot_signed_row(vm, 3, &count)
	    || !slot_signed_row(vm, 4, &destination)) {
		st_wren_abort(vm, "Scrollback.copyRowsTo expects a Surface and integer rows");
		return;
	}
	if (source < 0) {
		count += source;
		destination -= source;
		source = 0;
	}
	if (destination < 0) {
		count += destination;
		source -= destination;
		destination = 0;
	}
	if (count <= 0) {
		wrenSetSlotDouble(vm, 0, 0);
		return;
	}
	if (st_wren_context_from_vm(vm)->cterm == NULL) {
		wrenSetSlotDouble(vm, 0, 0);
		return;
	}
	copied = st_cterm_copy_scrollback_rows(
	    st_wren_context_from_vm(vm)->cterm, cells, width, height,
	    (size_t)source, (size_t)count, (size_t)destination);
	if (copied < 0) {
		st_wren_screen_result(vm, copied, "Scrollback.copyRowsTo");
		return;
	}
	uint32_t history_width = 0;
	size_t history_rows;
	(void)st_cterm_scrollback_size(st_wren_context_from_vm(vm)->cterm,
		&history_width, &history_rows);
	for (int row = 0; row < copied; ++row) {
		struct st_pixel_buffer *pixels = NULL;
		int status = st_cterm_copy_history_pixels(st_wren_context_from_vm(vm)->cterm,
			(size_t)source + (size_t)row, &pixels);
		if (status == 0)
			status = st_wren_surface_take_row_pixels(vm, 1,
				(uint32_t)destination + (uint32_t)row, history_width, pixels);
		if (status != 0) {
			st_wren_screen_result(vm, status, "Scrollback.copyRowsTo pixels");
			return;
		}
	}
	wrenSetSlotDouble(vm, 0, copied);
}

static void
fn_capture_active(WrenVM *vm)
{
	wrenSetSlotBool(vm, 0, st_wren_connected_capture_active(
	    st_wren_context_from_vm(vm)->connected));
}

static void
fn_capture_paused(WrenVM *vm)
{
	wrenSetSlotBool(vm, 0, st_wren_connected_capture_paused(
	    st_wren_context_from_vm(vm)->connected));
}

static void
fn_capture_start(WrenVM *vm)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);
	FILE *stream = NULL;
	bool raw;

	if (context->connected == NULL || context->cterm == NULL) {
		wrenSetSlotNull(vm, 0);
		return;
	}
	if (st_wren_connected_capture_active(context->connected)) {
		st_wren_abort(vm, "Capture.start: already capturing");
		return;
	}
	if (wrenGetSlotType(vm, 2) != WREN_TYPE_BOOL) {
		st_wren_abort(vm, "Capture.start expects a Bool for raw");
		return;
	}
	raw = wrenGetSlotBool(vm, 2);
	if (!st_wren_file_claim_write(vm, 1, "Capture.start", &stream))
		return;
	if (st_wren_connected_capture_start(context->connected, stream, raw) != 0) {
		(void)fclose(stream);
		st_wren_abort(vm, "Capture.start failed");
		return;
	}
	wrenSetSlotNull(vm, 0);
}

static void
fn_capture_stop(WrenVM *vm)
{
	st_wren_connected_capture_stop(st_wren_context_from_vm(vm)->connected);
	wrenSetSlotNull(vm, 0);
}

static void
fn_capture_pause(WrenVM *vm)
{
	st_wren_connected_capture_pause(st_wren_context_from_vm(vm)->connected);
	wrenSetSlotNull(vm, 0);
}

static void
fn_capture_resume(WrenVM *vm)
{
	st_wren_connected_capture_resume(st_wren_context_from_vm(vm)->connected);
	wrenSetSlotNull(vm, 0);
}

static void
fn_cterm_status_display(WrenVM *vm)
{
	wrenSetSlotDouble(vm, 0, st_cterm_status_display(
	    st_wren_context_from_vm(vm)->cterm));
}

static void
cterm_geometry(WrenVM *vm, bool origin, bool horizontal)
{
	uint32_t x = 0;
	uint32_t y = 0;
	st_cterm_t *cterm = st_wren_context_from_vm(vm)->cterm;

	if (cterm != NULL) {
		if (origin)
			(void)st_cterm_origin(cterm, &x, &y);
		else
			(void)st_cterm_size(cterm, &x, &y);
	}
	wrenSetSlotDouble(vm, 0, horizontal ? x : y);
}

static void fn_cterm_x_origin(WrenVM *vm)
{ cterm_geometry(vm, true, true); }
static void fn_cterm_y_origin(WrenVM *vm)
{ cterm_geometry(vm, true, false); }
static void fn_cterm_width(WrenVM *vm)
{ cterm_geometry(vm, false, true); }
static void fn_cterm_height(WrenVM *vm)
{ cterm_geometry(vm, false, false); }

static struct st_cterm_state
cterm_state(WrenVM *vm)
{
	struct st_cterm_state state = {0};
	(void)st_cterm_state(st_wren_context_from_vm(vm)->cterm, &state);
	return state;
}

#define CTERM_NUMBER(name, member) \
	static void fn_cterm_##name(WrenVM *vm) \
	{ wrenSetSlotDouble(vm, 0, (double)cterm_state(vm).member); }
#define CTERM_BOOL(name, member) \
	static void fn_cterm_##name(WrenVM *vm) \
	{ wrenSetSlotBool(vm, 0, cterm_state(vm).member); }
CTERM_NUMBER(x, x)
CTERM_NUMBER(y, y)
CTERM_NUMBER(top_margin, top_margin)
CTERM_NUMBER(bottom_margin, bottom_margin)
CTERM_NUMBER(left_margin, left_margin)
CTERM_NUMBER(right_margin, right_margin)
CTERM_NUMBER(attr, attr)
CTERM_NUMBER(fg_color, fg_color)
CTERM_NUMBER(bg_color, bg_color)
CTERM_NUMBER(font_slot, font_slot)
CTERM_NUMBER(scrollback_lines, scrollback_lines)
CTERM_NUMBER(scrollback_width, scrollback_width)
CTERM_NUMBER(scrollback_pos, scrollback_pos)
CTERM_NUMBER(scrollback_start, scrollback_start)
CTERM_NUMBER(emulation, emulation)
CTERM_BOOL(has_palette_override, has_palette_override)
CTERM_BOOL(started, started)
CTERM_BOOL(skypix, skypix)
CTERM_NUMBER(graphics_mode, graphics_mode)
#undef CTERM_NUMBER
#undef CTERM_BOOL

static void
cterm_number_list(WrenVM *vm, const double *values, size_t count)
{
	wrenEnsureSlots(vm, 2);
	wrenSetSlotNewList(vm, 0);
	for (size_t i = 0; i < count; ++i) {
		wrenSetSlotDouble(vm, 1, values[i]);
		wrenInsertInList(vm, 0, -1, 1);
	}
}

static bool
font_integer(WrenVM *vm, int slot, int64_t maximum, int64_t *value)
{
	if (!st_wren_number_i64(vm, slot, value) || *value < 0 || *value > maximum) {
		st_wren_abort(vm, "expected an exact font integer in range");
		return false;
	}
	return true;
}

static void
set_terminal_fonts(WrenVM *vm, uint8_t selector, unsigned mask)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);
	uint32_t capabilities = 0;
	int status = st_console_capabilities(context->console, &capabilities);
	if (context->cterm == NULL || status != 0
	    || (capabilities & ST_CONSOLE_CAP_LOADABLE_FONTS) == 0) {
		st_wren_push_font_error(vm, ST_WREN_FONT_UNAVAILABLE, status, "Font selection is unavailable");
		return;
	}
	status = st_cterm_set_fonts(context->cterm, selector, mask);
	if (status != 0)
		st_wren_push_font_error(vm, status == IO_ERROR_NOT_FOUND
		    ? ST_WREN_FONT_NOT_LOADED : ST_WREN_FONT_NATIVE_FAILURE,
		    status, "Terminal font assignment failed");
	else wrenSetSlotNull(vm, 0);
}
static void fn_cterm_font_get(WrenVM *vm)
{
	int64_t slot;
	if (!font_integer(vm, 1, 3, &slot)) return;
	struct st_cterm_state state = cterm_state(vm);
	wrenSetSlotDouble(vm, 0, state.alt_fonts[slot]);
}
static void fn_cterm_font_set(WrenVM *vm)
{
	int64_t slot, selector;
	if (!font_integer(vm, 1, 3, &slot) || !font_integer(vm, 2, UINT8_MAX, &selector)) return;
	set_terminal_fonts(vm, (uint8_t)selector, 1U << slot);
	if (wrenGetSlotType(vm, 0) != WREN_TYPE_NULL) wrenAbortFiber(vm, 0);
	else wrenSetSlotDouble(vm, 0, (double)selector);
}
static void fn_cterm_set_font(WrenVM *vm)
{
	int64_t selector, mask;
	if (!font_integer(vm, 1, UINT8_MAX, &selector) || !font_integer(vm, 2, 15, &mask)) return;
	if (mask == 0) {
		st_wren_abort(vm, "font assignment mask must not be zero");
		return;
	}
	set_terminal_fonts(vm, (uint8_t)selector, (unsigned)mask);
}

static void
fn_cterm_palette_override(WrenVM *vm)
{
	struct st_cterm_state state = cterm_state(vm);
	double values[16];

	if (!state.has_palette_override) {
		cterm_number_list(vm, NULL, 0);
		return;
	}
	for (size_t i = 0; i < 16; ++i)
		values[i] = state.palette_override[i];
	cterm_number_list(vm, values, 16);
}

static void
cterm_snapshot_object(WrenVM *vm, const char *class_name)
{
	struct st_cterm_state state = cterm_state(vm);
	struct st_cterm_state *object;

	wrenEnsureSlots(vm, 2);
	wrenGetVariable(vm, "syncterm", class_name, 1);
	object = wrenSetSlotNewForeign(vm, 0, 1, sizeof(*object));
	*object = state;
}

static void fn_cterm_ext_attr(WrenVM *vm)
{ cterm_snapshot_object(vm, "ExtAttr"); }
static void fn_cterm_last_column_flag(WrenVM *vm)
{ cterm_snapshot_object(vm, "LastColumnFlag"); }

#define EXT_ATTR_BOOL(name, member) \
	static void fn_ext_attr_##name(WrenVM *vm) \
	{ wrenSetSlotBool(vm, 0, \
	    ((struct st_cterm_state *)wrenGetSlotForeign(vm, 0))->ext_attr.member); }
EXT_ATTR_BOOL(auto_wrap, auto_wrap)
EXT_ATTR_BOOL(origin_mode, origin_mode)
EXT_ATTR_BOOL(sx_scroll, sx_scroll)
EXT_ATTR_BOOL(dec_lrmm, dec_lrmm)
EXT_ATTR_BOOL(bracket_paste, bracket_paste)
EXT_ATTR_BOOL(dec_bkm, dec_bkm)
EXT_ATTR_BOOL(prestel_mosaic, prestel_mosaic)
EXT_ATTR_BOOL(prestel_double_height, prestel_double_height)
EXT_ATTR_BOOL(prestel_conceal, prestel_conceal)
EXT_ATTR_BOOL(prestel_separated, prestel_separated)
EXT_ATTR_BOOL(prestel_hold, prestel_hold)
EXT_ATTR_BOOL(alternate_keypad, alternate_keypad)
#undef EXT_ATTR_BOOL

#define LAST_COLUMN_BOOL(name, member) \
	static void fn_last_column_##name(WrenVM *vm) \
	{ wrenSetSlotBool(vm, 0, \
	    ((struct st_cterm_state *)wrenGetSlotForeign(vm, 0))->last_column_flag.member); }
LAST_COLUMN_BOOL(set, set)
LAST_COLUMN_BOOL(enabled, enabled)
LAST_COLUMN_BOOL(forced, forced)
#undef LAST_COLUMN_BOOL

static void
fn_cterm_doorway_mode(WrenVM *vm)
{
	wrenSetSlotBool(vm, 0, st_cterm_doorway_mode(
	    st_wren_context_from_vm(vm)->cterm));
}

static void
fn_cterm_doorway_mode_set(WrenVM *vm)
{
	if (wrenGetSlotType(vm, 1) != WREN_TYPE_BOOL) {
		st_wren_abort(vm, "CTerm.doorwayMode expects a Bool");
		return;
	}
	bool enabled = wrenGetSlotBool(vm, 1);
	st_cterm_set_doorway_mode(st_wren_context_from_vm(vm)->cterm, enabled);
	wrenSetSlotBool(vm, 0, enabled);
}

static void
fn_cterm_music(WrenVM *vm)
{
	wrenSetSlotDouble(vm, 0, st_cterm_music_mode(
	    st_wren_context_from_vm(vm)->cterm));
}

static void
fn_cterm_music_set(WrenVM *vm)
{
	double mode;

	if (wrenGetSlotType(vm, 1) != WREN_TYPE_NUM
	    || !isfinite(mode = wrenGetSlotDouble(vm, 1))
	    || mode != floor(mode) || mode < INT_MIN || mode > INT_MAX) {
		st_wren_abort(vm, "CTerm.music expects an integer mode");
		return;
	}
	st_cterm_set_music_mode(st_wren_context_from_vm(vm)->cterm,
	    (int)mode);
	wrenSetSlotDouble(vm, 0, st_cterm_music_mode(
	    st_wren_context_from_vm(vm)->cterm));
}

static void
fn_cterm_atascii_inverse(WrenVM *vm)
{
	wrenSetSlotBool(vm, 0, st_cterm_atascii_inverse(
	    st_wren_context_from_vm(vm)->cterm));
}

static void
fn_cterm_mouse_mode(WrenVM *vm)
{
	wrenSetSlotDouble(vm, 0, st_cterm_mouse_mode(
	    st_wren_context_from_vm(vm)->cterm));
}

static void
fn_cterm_mouse_sgr_pixels(WrenVM *vm)
{
	wrenSetSlotBool(vm, 0, st_cterm_mouse_sgr_pixels(
	    st_wren_context_from_vm(vm)->cterm));
}

static void
fn_cterm_send_key(WrenVM *vm)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);
	struct st_console_event event;
	bool handled = false;

	if (!st_wren_input_native(vm, 1, &event))
		return;
	if (context->session == NULL) {
		st_wren_abort(vm, "CTerm.sendKey requires an active terminal");
		return;
	}
	int status = st_term_session_send_key(context->session, &event, &handled);
	if (status != 0) {
		push_cterm_error(vm, status, CTERM_ERROR_KEY, 0, handled);
		return;
	}
	wrenSetSlotBool(vm, 0, handled);
}

static void
fn_cterm_write(WrenVM *vm)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);
	const char *bytes;
	int length;
	size_t consumed = 0;
	int status;

	if (context->cterm == NULL) {
		st_wren_abort(vm, "CTerm.write requires an active terminal");
		return;
	}
	if (!st_wren_text_slot(vm, 1, &bytes, &length))
		return;
	status = st_cterm_write_utf8(context->cterm, bytes, (size_t)length,
	    &consumed);
	/* Counts describe the normalized C input, not the original Wren String. */
	if (consumed > (size_t)length || (status == 0 && consumed != (size_t)length)) {
		st_wren_abort(vm, "CTerm.write returned an invalid native consumed count");
		return;
	}
	if (status != 0) {
		push_cterm_error(vm, status, CTERM_ERROR_WRITE, consumed, false);
		return;
	}
	wrenSetSlotNull(vm, 0);
}

static void
fn_cterm_reset(WrenVM *vm)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);
	if (context->cterm == NULL) {
		st_wren_abort(vm, "CTerm.reset requires an active terminal");
		return;
	}
	int status = st_cterm_reset_terminal(context->cterm);
	if (status != 0)
		push_cterm_error(vm, status, CTERM_ERROR_RESET, 0, false);
	else
		wrenSetSlotNull(vm, 0);
}

static void
fn_cterm_save_screenshot(WrenVM *vm)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);
	FILE *stream = NULL;
	bool with_sauce;
	int native_error = 0;
	int status;

	if (context->cterm == NULL || wrenGetSlotType(vm, 2) != WREN_TYPE_BOOL) {
		st_wren_abort(vm, "CTerm.saveScreenshot requires a terminal and Bool");
		return;
	}
	with_sauce = wrenGetSlotBool(vm, 2);
	if (!st_wren_file_claim_write(vm, 1, "CTerm.saveScreenshot",
	    &stream))
		return;
	status = st_cterm_save_binary(context->cterm, stream, with_sauce,
	    context->profile == NULL ? NULL : context->profile->name,
	    context->profile == NULL ? NULL : context->profile->username,
	    &native_error);
	if (fclose(stream) != 0 && status == 0) {
		status = IO_ERROR_IO;
		native_error = errno == 0 ? EIO : errno;
	}
	if (status != 0) {
		st_wren_file_write_error(vm, native_error,
		    "screen save failed");
		return;
	}
	wrenSetSlotNull(vm, 0);
}

static void
fn_cterm_mouse_disabled(WrenVM *vm)
{
	wrenSetSlotBool(vm, 0,
	    st_wren_context_from_vm(vm)->cterm_mouse_disabled);
}

static void
fn_cterm_mouse_disabled_set(WrenVM *vm)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);

	if (wrenGetSlotType(vm, 1) != WREN_TYPE_BOOL) {
		st_wren_abort(vm, "CTerm.mouseDisabled expects a Bool");
		return;
	}
	context->cterm_mouse_disabled = wrenGetSlotBool(vm, 1);
	st_term_session_set_mouse_disabled(context->session,
	    context->cterm_mouse_disabled);
	wrenSetSlotBool(vm, 0, context->cterm_mouse_disabled);
}

static void
fn_cterm_suspended(WrenVM *vm)
{
	wrenSetSlotBool(vm, 0, st_wren_context_from_vm(vm)->cterm_suspended);
}

static void
fn_cterm_suspended_set(WrenVM *vm)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);

	if (wrenGetSlotType(vm, 1) != WREN_TYPE_BOOL) {
		st_wren_abort(vm, "CTerm.suspended expects a Bool");
		return;
	}
	context->cterm_suspended = wrenGetSlotBool(vm, 1);
	st_term_session_set_suspended(context->session, context->cterm_suspended);
	wrenSetSlotBool(vm, 0, context->cterm_suspended);
}


static void
fn_cterm_ooii_mode(WrenVM *vm)
{
	wrenSetSlotDouble(vm, 0, st_term_session_ooii_mode(
	    st_wren_context_from_vm(vm)->session));
}

static void
fn_cterm_ooii_mode_set(WrenVM *vm)
{
	struct st_term_session *session = st_wren_context_from_vm(vm)->session;
	double mode;
	if (wrenGetSlotType(vm, 1) != WREN_TYPE_NUM
	    || !isfinite(mode = wrenGetSlotDouble(vm, 1)) || mode != floor(mode)) {
		st_wren_abort(vm, "CTerm.ooiiMode expects an integer");
		return;
	}
	st_term_session_set_ooii_mode(session, mode < 0 ? 0
	    : mode > ST_OOII_MAX_MODE ? ST_OOII_MAX_MODE : (int)mode);
	wrenSetSlotDouble(vm, 0, st_term_session_ooii_mode(session));
}

static const uint32_t output_rates[] = {
	300, 600, 1200, 2400, 4800, 9600, 19200, 38400,
	57600, 76800, 115200,
};

static void
fn_cterm_throttle_speed(WrenVM *vm)
{
	struct st_term_session *session = st_wren_context_from_vm(vm)->session;

	wrenSetSlotDouble(vm, 0, st_term_session_throttle_speed(session));
}

static void
fn_cterm_throttle_speed_set(WrenVM *vm)
{
	struct st_term_session *session = st_wren_context_from_vm(vm)->session;
	double bps;

	if (wrenGetSlotType(vm, 1) != WREN_TYPE_NUM
	    || !isfinite(bps = wrenGetSlotDouble(vm, 1))
	    || bps < 0 || bps > UINT32_MAX || bps != floor(bps)) {
		st_wren_abort(vm, "CTerm.throttleSpeed expects a nonnegative BPS value");
		return;
	}
	st_term_session_set_throttle_speed(session, (uint32_t)bps);
	wrenSetSlotDouble(vm, 0, st_term_session_throttle_speed(session));
}

static void
fn_cterm_throttle_step(WrenVM *vm, bool up)
{
	struct st_term_session *session = st_wren_context_from_vm(vm)->session;
	uint32_t current = st_term_session_throttle_speed(session);
	uint32_t selected = 0;

	if (up) {
		for (size_t i = 0; i < sizeof(output_rates)
		    / sizeof(output_rates[0]); ++i) {
			if (output_rates[i] > current) {
				selected = output_rates[i];
				break;
			}
		}
		if (selected == 0)
			selected = current;
	}
	else {
		for (size_t i = 0; i < sizeof(output_rates)
		    / sizeof(output_rates[0]); ++i) {
			if (output_rates[i] >= current)
				break;
			selected = output_rates[i];
		}
	}
	st_term_session_set_throttle_speed(session, selected);
	wrenSetSlotDouble(vm, 0, st_term_session_throttle_speed(session));
}

static void fn_cterm_throttle_up(WrenVM *vm)
{ fn_cterm_throttle_step(vm, true); }
static void fn_cterm_throttle_down(WrenVM *vm)
{ fn_cterm_throttle_step(vm, false); }

static void
fn_host_edit_bbs_list(WrenVM *vm)
{
	if (st_wren_host_run_connected_menu(
	    st_wren_context_from_vm(vm)->host) != 0)
		st_wren_abort(vm, "Host.editBBSList failed");
}

static void
fn_host_output_rates(WrenVM *vm)
{
	wrenEnsureSlots(vm, 2);
	wrenSetSlotNewList(vm, 0);
	for (size_t i = 0; i < sizeof(output_rates)
	    / sizeof(output_rates[0]); ++i) {
		wrenSetSlotDouble(vm, 1, output_rates[i]);
		wrenInsertInList(vm, 0, -1, 1);
	}
	wrenSetSlotDouble(vm, 1, 0);
	wrenInsertInList(vm, 0, -1, 1);
}

static void
fn_host_output_rate_names(WrenVM *vm)
{
	static const char *const names[] = {
		"300", "600", "1200", "2400", "4800", "9600", "19200",
		"38400", "57600", "76800", "115200", "Current",
	};

	wrenEnsureSlots(vm, 2);
	wrenSetSlotNewList(vm, 0);
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
		wrenSetSlotString(vm, 1, names[i]);
		wrenInsertInList(vm, 0, -1, 1);
	}
}


static void
fn_log_unread(WrenVM *vm)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);
	wrenSetSlotBool(vm, 0, context->log.total > context->seen_total);
}

static void
fn_log_unread_error(WrenVM *vm)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);
	wrenSetSlotBool(vm, 0, context->log.errors > context->seen_errors);
}

static void
fn_music_names(WrenVM *vm)
{
	static const char *const names[] = {
		"ESC [ | only", "BANSI Style", "All ANSI Music enabled",
	};

	wrenEnsureSlots(vm, 2);
	wrenSetSlotNewList(vm, 0);
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
		wrenSetSlotString(vm, 1, names[i]);
		wrenInsertInList(vm, 0, -1, 1);
	}
}

static void
fn_music_help(WrenVM *vm)
{
	wrenSetSlotString(vm, 0,
	    "# ANSI Music Setup\n\n"
	    "ESC [ | only\n:  Only `CSI |` (SyncTERM) ANSI music is supported. "
	    "Enables Delete Line.\n"
	    "BANSI Style\n:  Also enables BANSI-Style (`CSI N`). "
	    "Enables Delete Line.\n"
	    "All ANSI Music Enabled\n:  Enables both `CSI M` and `CSI N` "
	    "ANSI music. Delete Line is disabled.\n\n"
	    "So-called ANSI music reused terminal control sequences, notably "
	    "Delete Line. Choose the narrowest mode supported by the BBS.");
}

struct session_binding {
	const char *class_name;
	const char *signature;
	WrenForeignMethodFn method;
};

static void fn_cterm_encoding(WrenVM *vm)
{ wrenSetSlotDouble(vm, 0, st_cterm_encoding(st_wren_context_from_vm(vm)->cterm)); }
static void fn_cterm_codepage(WrenVM *vm)
{ wrenSetSlotDouble(vm, 0, st_cterm_codepage(st_wren_context_from_vm(vm)->cterm)); }

static const struct session_binding bindings[] = {
	{"Scrollback", "width", fn_scrollback_width},
	{"Scrollback", "height", fn_scrollback_height},
	{"Scrollback", "copyRowsTo(_,_,_,_)", fn_scrollback_copy_rows},
	{"Conn", "sendText(_)", fn_conn_send},
	{"Conn", "sendBytes(_)", fn_conn_send_bytes},
	{"Conn", "close()", fn_conn_close},
	{"Conn", "endSession(_)", fn_conn_end_session},
	{"Conn", "scrollback()", fn_conn_scrollback},
	{"Conn", "paste()", fn_conn_paste},
	{"Conn", "connected", fn_conn_connected},
	{"Conn", "type", fn_conn_type},
	{"Conn", "queued", fn_conn_queued},
	{"Conn", "elapsedSeconds", fn_conn_elapsed_seconds},
	{"Conn", "sessionLog", fn_conn_session_log},
	{"Capture", "active", fn_capture_active},
	{"Capture", "paused", fn_capture_paused},
	{"Capture", "start(_,_)", fn_capture_start},
	{"Capture", "stop()", fn_capture_stop},
	{"Capture", "pause()", fn_capture_pause},
	{"Capture", "resume()", fn_capture_resume},
	{"CTerm", "statusDisplay", fn_cterm_status_display},
	{"CTerm", "originX", fn_cterm_x_origin},
	{"CTerm", "originY", fn_cterm_y_origin},
	{"CTerm", "width", fn_cterm_width},
	{"CTerm", "height", fn_cterm_height},
	{"CTerm", "encoding", fn_cterm_encoding},
	{"CTerm", "codepage", fn_cterm_codepage},
	{"CTerm", "x", fn_cterm_x},
	{"CTerm", "y", fn_cterm_y},
	{"CTerm", "topMargin", fn_cterm_top_margin},
	{"CTerm", "bottomMargin", fn_cterm_bottom_margin},
	{"CTerm", "leftMargin", fn_cterm_left_margin},
	{"CTerm", "rightMargin", fn_cterm_right_margin},
	{"CTerm", "attr", fn_cterm_attr},
	{"CTerm", "fgColor", fn_cterm_fg_color},
	{"CTerm", "bgColor", fn_cterm_bg_color},
	{"CTerm", "hasPaletteOverride", fn_cterm_has_palette_override},
	{"CTerm", "paletteOverride", fn_cterm_palette_override},
	{"CTerm", "fontSlot", fn_cterm_font_slot},
	{"CTermFonts", "[_]", fn_cterm_font_get},
	{"CTermFonts", "[_]=(_)", fn_cterm_font_set},
	{"CTerm", "setFont(_,_)", fn_cterm_set_font},
	{"CTerm", "scrollbackLines", fn_cterm_scrollback_lines},
	{"CTerm", "scrollbackWidth", fn_cterm_scrollback_width},
	{"CTerm", "scrollbackPos", fn_cterm_scrollback_pos},
	{"CTerm", "scrollbackStart", fn_cterm_scrollback_start},
	{"CTerm", "emulation", fn_cterm_emulation},
	{"CTerm", "started", fn_cterm_started},
	{"CTerm", "skypix", fn_cterm_skypix},
	{"CTerm", "graphicsMode", fn_cterm_graphics_mode},
	{"CTerm", "extAttr", fn_cterm_ext_attr},
	{"CTerm", "lastColumnFlag", fn_cterm_last_column_flag},
	{"CTerm", "doorwayMode", fn_cterm_doorway_mode},
	{"CTerm", "doorwayMode=(_)", fn_cterm_doorway_mode_set},
	{"CTerm", "music", fn_cterm_music},
	{"CTerm", "music=(_)", fn_cterm_music_set},
	{"CTerm", "atasciiInverse", fn_cterm_atascii_inverse},
	{"CTerm", "mouseMode", fn_cterm_mouse_mode},
	{"CTerm", "mouseSgrPixels", fn_cterm_mouse_sgr_pixels},
	{"CTerm", "sendKey(_)", fn_cterm_send_key},
	{"CTerm", "write(_)", fn_cterm_write},
	{"CTerm", "reset()", fn_cterm_reset},
	{"CTerm", "saveScreenshot(_,_)", fn_cterm_save_screenshot},
	{"CTerm", "mouseDisabled", fn_cterm_mouse_disabled},
	{"CTerm", "mouseDisabled=(_)", fn_cterm_mouse_disabled_set},
	{"CTerm", "suspended", fn_cterm_suspended},
	{"CTerm", "suspended=(_)", fn_cterm_suspended_set},
	{"CTerm", "ooiiMode", fn_cterm_ooii_mode},
	{"CTerm", "ooiiMode=(_)", fn_cterm_ooii_mode_set},
	{"CTerm", "throttleSpeed", fn_cterm_throttle_speed},
	{"CTerm", "throttleSpeed=(_)", fn_cterm_throttle_speed_set},
	{"CTerm", "throttleSpeedUp()", fn_cterm_throttle_up},
	{"CTerm", "throttleSpeedDown()", fn_cterm_throttle_down},
	{"Host", "logUnread", fn_log_unread},
	{"Host", "logUnreadError", fn_log_unread_error},
	{"Host", "musicNames", fn_music_names},
	{"Host", "musicHelp", fn_music_help},
	{"Host", "outputRates", fn_host_output_rates},
	{"Host", "outputRateNames", fn_host_output_rate_names},
	{"Host", "editBBSList_()", fn_host_edit_bbs_list},
};

WrenForeignMethodFn
st_wren_session_bind_method(const char *class_name, bool is_static,
	const char *signature)
{
	if (!is_static && strcmp(class_name, "Session") == 0
	    && strcmp(signature, "claimWire()") == 0) return fn_claim_wire;
	if (!is_static && strcmp(class_name, "WireLease") == 0) {
		if (strcmp(signature, "release()") == 0) return fn_wire_release;
		if (strcmp(signature, "released") == 0) return fn_wire_released;
		if (strcmp(signature, "readBytes(_)") == 0) return fn_wire_read;
		if (strcmp(signature, "peekBytes(_)") == 0) return fn_wire_peek;
		if (strcmp(signature, "available") == 0) return fn_wire_available;
		if (strcmp(signature, "sendBytes(_)") == 0) return fn_wire_send_bytes;
		if (strcmp(signature, "sendWire(_)") == 0) return fn_wire_send;
	}
	if (!is_static && strcmp(class_name, "ConnError") == 0) {
		if (strcmp(signature, "code") == 0)
			return fn_conn_error_code;
		if (strcmp(signature, "partial") == 0)
			return fn_conn_error_partial;
		if (strcmp(signature, "message") == 0)
			return fn_conn_error_message;
		if (strcmp(signature, "toString") == 0)
			return fn_conn_error_string;
	}
	if (!is_static && strcmp(class_name, "CTermError") == 0) {
		if (strcmp(signature, "code") == 0) return fn_cterm_error_code;
		if (strcmp(signature, "message") == 0) return fn_cterm_error_message;
		if (strcmp(signature, "consumedBytes") == 0) return fn_cterm_error_consumed;
		if (strcmp(signature, "handled") == 0) return fn_cterm_error_handled;
		if (strcmp(signature, "toString") == 0) return fn_cterm_error_string;
	}
	if (!is_static && strcmp(class_name, "ExtAttr") == 0) {
		static const struct {
			const char *signature;
			WrenForeignMethodFn method;
		} attrs[] = {
			{"autoWrap", fn_ext_attr_auto_wrap},
			{"originMode", fn_ext_attr_origin_mode},
			{"sxScroll", fn_ext_attr_sx_scroll},
			{"decLrmm", fn_ext_attr_dec_lrmm},
			{"bracketPaste", fn_ext_attr_bracket_paste},
			{"decBkm", fn_ext_attr_dec_bkm},
			{"prestelMosaic", fn_ext_attr_prestel_mosaic},
			{"prestelDoubleHeight", fn_ext_attr_prestel_double_height},
			{"prestelConceal", fn_ext_attr_prestel_conceal},
			{"prestelSeparated", fn_ext_attr_prestel_separated},
			{"prestelHold", fn_ext_attr_prestel_hold},
			{"alternateKeypad", fn_ext_attr_alternate_keypad},
		};
		for (size_t i = 0; i < sizeof(attrs) / sizeof(attrs[0]); ++i) {
			if (strcmp(signature, attrs[i].signature) == 0)
				return attrs[i].method;
		}
	}
	if (!is_static && strcmp(class_name, "LastColumnFlag") == 0) {
		if (strcmp(signature, "set") == 0)
			return fn_last_column_set;
		if (strcmp(signature, "enabled") == 0)
			return fn_last_column_enabled;
		if (strcmp(signature, "forced") == 0)
			return fn_last_column_forced;
	}
	if (!is_static)
		return NULL;
	for (size_t i = 0; i < sizeof(bindings) / sizeof(bindings[0]); ++i) {
		if (strcmp(bindings[i].class_name, class_name) == 0
		    && strcmp(bindings[i].signature, signature) == 0)
			return bindings[i].method;
	}
	return NULL;
}
