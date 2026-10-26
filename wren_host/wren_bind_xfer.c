/* Per-VM transfer ownership; workers never touch Wren or CTerm. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "wren_host_internal.h"
#include "st_transfer.h"
#include "st_transfer_detect.h"
#include "st_term.h"
#include "st_connection.h"
#include "filesystem.h"
#include "io_error.h"
#include "string_compat.h"
#include "st_app_paths.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

struct st_wren_transfer {
	st_transfer_t *worker;
	struct st_transfer_detector detector;
	uint8_t prefix[33];
	size_t prefix_size;
	int automatic;
	bool upload, inline_upload;
};
static struct st_wren_transfer *state(struct st_wren_context *context)
{
	if (context->transfer == NULL) context->transfer = calloc(1, sizeof(*context->transfer));
	return context->transfer;
}
static const char *download_root(struct st_wren_context *context)
{
	const char *path = context->profile == NULL ? NULL : context->profile->download_directory;
	if ((path == NULL || !*path) && context->host->config.paths != NULL) path = context->host->config.paths->download_directory;
	return st_wren_download_directory_allowed(path) ? path : NULL;
}
static ssize_t send_bytes(void *context, const void *data, size_t size, int64_t timeout)
{
	return st_connection_send(context, data, size, timeout);
}
static void log_entry(struct st_wren_context *context, const struct st_transfer_log *entry)
{
	if (context->transfer_log != NULL && entry->level <= context->transfer_log_level)
		context->transfer_log(context->transfer_log_context, entry->level, entry->text);
}
static int start(struct st_wren_context *context, struct st_transfer_config *config)
{
	struct st_wren_transfer *s = state(context);
	if (s == NULL) return IO_ERROR_OUT_OF_MEMORY;
	if (s->worker != NULL) return IO_ERROR_BUSY;
	if (config->protocol != ST_TRANSFER_FAKE) {
		if (context->host->config.safe_mode || context->connection == NULL || context->session == NULL
		    || context->profile == NULL || context->profile->connection_type == ST_CONNECTION_MQTT)
			return IO_ERROR_PERMISSION_DENIED;
		config->send = send_bytes; config->send_context = context->connection;
		config->directory = download_root(context);
		if (!config->upload && config->directory == NULL) return IO_ERROR_PERMISSION_DENIED;
	}
	int status = st_transfer_create(&s->worker, config);
	if (status != 0) return status;
	if (context->session != NULL) {
		status = st_term_session_begin_transfer(context->session, s->worker);
		if (status != 0) { st_transfer_destroy(&s->worker); return status; }
	}
	status = st_transfer_launch(s->worker);
	if (status != 0) {
		(void)st_term_session_end_transfer(context->session);
		st_transfer_destroy(&s->worker);
		return status;
	}
	s->upload = config->upload;
	s->inline_upload = config->protocol == ST_TRANSFER_ASCII || config->protocol == ST_TRANSFER_RAW;
	if (s->prefix_size != 0) {
		(void)st_transfer_feed(s->worker, s->prefix, s->prefix_size); s->prefix_size = 0;
	}
	if (config->protocol == ST_TRANSFER_CET && context->session != NULL) {
		size_t size;
		const uint8_t *frame = st_term_session_prestel_frame(context->session, &size);
		(void)st_transfer_feed(s->worker, frame, size);
	}
	return 0;
}
static void finish(struct st_wren_context *context)
{
	struct st_wren_transfer *s = context->transfer;
	if (s == NULL) return;
	if (s->worker != NULL) {
		(void)st_transfer_join(s->worker);
		(void)st_term_session_end_transfer(context->session);
		size_t replay_size;
		const uint8_t *replay = st_transfer_replay(s->worker, &replay_size);
		if (context->cterm != NULL && replay_size != 0) {
			size_t consumed;
			(void)st_cterm_write(context->cterm, replay, replay_size, &consumed);
		}
		struct st_transfer_log logs[32]; size_t count;
		do {
			count = st_transfer_drain_log(s->worker, logs, 32);
			for (size_t i = 0; i < count; ++i) log_entry(context, &logs[i]);
		} while (count != 0);
		st_transfer_destroy(&s->worker);
	}
	st_term_session_reserve_transfer(context->session, false);
	s->automatic = 0; s->prefix_size = 0; s->detector.size = 0;
}
void st_wren_transfer_dispose(struct st_wren_context *context)
{
	finish(context); free(context->transfer); context->transfer = NULL;
}
bool st_wren_transfer_detect_enabled(const struct st_wren_context *context)
{
	return context != NULL && !context->host->config.safe_mode && context->connection != NULL
	    && context->session != NULL && context->profile != NULL
	    && context->profile->connection_type != ST_CONNECTION_MQTT;
}
int st_wren_transfer_filter(struct st_wren_context *context, uint8_t byte,
	bool *consumed, uint8_t *replacement, size_t capacity, size_t *size)
{
	if (!st_wren_transfer_detect_enabled(context)) return 0;
	struct st_wren_transfer *s = state(context);
	if (s == NULL) return IO_ERROR_OUT_OF_MEMORY;
	if (s->worker != NULL || s->automatic != 0) return 0;
	uint8_t output[33]; size_t count;
	int event = st_transfer_detect(&s->detector, byte, output, &count);
	if (event != 0) {
		if (download_root(context) == NULL && event == 1) {
			/* Keep literal bytes when downloads cannot be offered. */
			event = 0;
		} else {
			memcpy(s->prefix, output, count); s->prefix_size = count; s->automatic = event;
			if (event == 1) {
				struct st_transfer_config config = {.protocol = ST_TRANSFER_ZMODEM};
				int status = start(context, &config);
				if (status != 0) { s->automatic = 0; s->prefix_size = 0; return status; }
			} else st_term_session_reserve_transfer(context->session, true);
			*consumed = true; *size = 0; return 0;
		}
	}
	if (count == 1 && output[0] == byte && s->detector.size == 0) return 0;
	if (count > capacity) return IO_ERROR_OUT_OF_RANGE;
	memcpy(replacement, output, count); *size = count; *consumed = true;
	return 0;
}
static st_transfer_t *get_worker(WrenVM *vm)
{
	struct st_wren_transfer *s = st_wren_context_from_vm(vm)->transfer;
	return s == NULL ? NULL : s->worker;
}
static void active(WrenVM *vm) { wrenSetSlotBool(vm, 0, get_worker(vm) != NULL); }
static void uploading(WrenVM *vm)
{
	struct st_wren_transfer *s = st_wren_context_from_vm(vm)->transfer;
	wrenSetSlotBool(vm, 0, s != NULL && s->worker != NULL && s->upload);
}
static void inline_upload(WrenVM *vm)
{
	struct st_wren_transfer *s = st_wren_context_from_vm(vm)->transfer;
	wrenSetSlotBool(vm, 0, s != NULL && s->inline_upload);
}
static void begin(WrenVM *vm)
{
	const char *kind; int length;
	if (!st_wren_text_slot(vm, 1, &kind, &length) || strcmp(kind, "fake") != 0) { st_wren_abort(vm, "Transfer.beginSession expects fake"); return; }
	struct st_transfer_config config = {.protocol = ST_TRANSFER_FAKE};
	if (start(st_wren_context_from_vm(vm), &config) != 0) st_wren_abort(vm, "Transfer already active or could not start");
	else wrenSetSlotBool(vm, 0, true);
}
static void end(WrenVM *vm) { finish(st_wren_context_from_vm(vm)); wrenSetSlotNull(vm, 0); }
static void done(WrenVM *vm) { wrenSetSlotBool(vm, 0, st_transfer_done(get_worker(vm))); }
static void success(WrenVM *vm) { wrenSetSlotBool(vm, 0, st_transfer_result(get_worker(vm)) == 0); }
static void aborted(WrenVM *vm) { wrenSetSlotBool(vm, 0, st_transfer_aborted(get_worker(vm))); }
static void abort_transfer(WrenVM *vm) { wrenSetSlotBool(vm, 0, st_transfer_request_abort(get_worker(vm))); }
static void tick_dirty(WrenVM *vm)
{
	st_transfer_t *t = get_worker(vm); wrenSetSlotBool(vm, 0, t != NULL && st_transfer_tick_dirty(t));
}
static void snapshot(WrenVM *vm)
{
	struct st_transfer_snapshot value = {0}; st_transfer_t *t = get_worker(vm);
	if (t != NULL) st_transfer_snapshot(t, &value);
	wrenEnsureSlots(vm, 2); wrenSetSlotNewList(vm, 0);
	const char *lines[] = {value.line1, value.line2, value.line3, value.line4};
	for (unsigned i = 0; i < 4; ++i) { wrenSetSlotString(vm, 1, lines[i]); wrenInsertInList(vm, 0, -1, 1); }
	wrenSetSlotDouble(vm, 1, (double)value.bytes_current); wrenInsertInList(vm, 0, -1, 1);
	wrenSetSlotDouble(vm, 1, (double)value.bytes_total); wrenInsertInList(vm, 0, -1, 1);
}
static void drain_log(WrenVM *vm)
{
	struct st_transfer_log logs[256]; st_transfer_t *t = get_worker(vm);
	size_t count = t == NULL ? 0 : st_transfer_drain_log(t, logs, 256);
	wrenEnsureSlots(vm, 4); wrenSetSlotNewList(vm, 0);
	struct st_wren_context *context = st_wren_context_from_vm(vm);
	for (size_t i = 0; i < count; ++i) {
		log_entry(context, &logs[i]);
		wrenSetSlotNewList(vm, 1); wrenSetSlotDouble(vm, 2, logs[i].level); wrenInsertInList(vm, 1, -1, 2);
		wrenSetSlotString(vm, 2, logs[i].text); wrenInsertInList(vm, 1, -1, 2); wrenInsertInList(vm, 0, -1, 1);
	}
}
static void dialog_pending(WrenVM *vm)
{
	st_transfer_t *t = get_worker(vm);
	wrenSetSlotDouble(vm, 0, t == NULL ? 0 : st_transfer_dialog(t, NULL, 0));
}
static void dialog_filename(WrenVM *vm)
{
	char name[1024] = ""; st_transfer_t *t = get_worker(vm);
	if (t != NULL) (void)st_transfer_dialog(t, name, sizeof(name));
	wrenSetSlotString(vm, 0, name);
}
static void dialog_respond(WrenVM *vm)
{
	st_transfer_t *t = get_worker(vm); const char *name = NULL; int length = 0;
	if (t == NULL || wrenGetSlotType(vm, 1) != WREN_TYPE_NUM) { st_wren_abort(vm, "No transfer dialog"); return; }
	double response = wrenGetSlotDouble(vm, 1);
	if (!isfinite(response) || response < 0 || response > 2 || floor(response) != response) { st_wren_abort(vm, "Invalid transfer dialog response"); return; }
	if (wrenGetSlotType(vm, 2) != WREN_TYPE_NULL && !st_wren_text_slot(vm, 2, &name, &length)) return;
	if (st_transfer_respond(t, (int)response, name) != 0) st_wren_abort(vm, "No pending transfer dialog or invalid filename");
	else wrenSetSlotNull(vm, 0);
}
static bool parse_protocol(const char *name, bool upload, enum st_transfer_protocol *protocol)
{
	static const struct { const char *name; enum st_transfer_protocol protocol; } kinds[] = {
		{"zmodem", ST_TRANSFER_ZMODEM}, {"ymodem", ST_TRANSFER_YMODEM},
		{"ymodem-g", ST_TRANSFER_YMODEM_G}, {"xmodem-1k", ST_TRANSFER_XMODEM_1K},
		{"xmodem-128", ST_TRANSFER_XMODEM_128}, {"xmodem-crc", ST_TRANSFER_XMODEM_CRC},
		{"xmodem-chksum", ST_TRANSFER_XMODEM_CHECKSUM}, {"ascii", ST_TRANSFER_ASCII},
		{"raw", ST_TRANSFER_RAW}, {"cet", ST_TRANSFER_CET}
	};
	for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); ++i) if (strcmp(name, kinds[i].name) == 0) {
		*protocol = kinds[i].protocol;
		if (upload) return *protocol != ST_TRANSFER_CET && *protocol != ST_TRANSFER_XMODEM_CRC && *protocol != ST_TRANSFER_XMODEM_CHECKSUM && *protocol != ST_TRANSFER_YMODEM_G;
		return *protocol != ST_TRANSFER_RAW && *protocol != ST_TRANSFER_ASCII && *protocol != ST_TRANSFER_XMODEM_1K && *protocol != ST_TRANSFER_XMODEM_128;
	}
	return false;
}
static void dispatch(WrenVM *vm, bool upload, bool batch)
{
	const char *kind; int length;
	struct st_transfer_config config = {.upload = upload};
	struct st_wren_context *context = st_wren_context_from_vm(vm);
	if (!st_wren_text_slot(vm, 1, &kind, &length)) return;
	if (!parse_protocol(kind, upload, &config.protocol)
	    || (batch && config.protocol != ST_TRANSFER_ZMODEM && config.protocol != ST_TRANSFER_YMODEM)) { st_wren_abort(vm, "Unsupported transfer protocol"); return; }
	struct st_cterm_state terminal_state = {0};
	if (context->cterm != NULL) (void)st_cterm_state(context->cterm, &terminal_state);
	if (config.protocol == ST_TRANSFER_CET && (context->cterm == NULL || terminal_state.emulation != ST_CTERM_PRESTEL)) { st_wren_abort(vm, "CET requires Prestel"); return; }
	char **paths = NULL;
	if (upload) {
		if (wrenGetSlotType(vm, 3) != WREN_TYPE_NUM) { st_wren_abort(vm, "Expected last wire byte"); return; }
		double last = wrenGetSlotDouble(vm, 3);
		if (!isfinite(last) || last < 0 || last > 255 || floor(last) != last) { st_wren_abort(vm, "Invalid last wire byte"); return; }
		config.last_ch = (int)last;
		if (batch && wrenGetSlotType(vm, 2) != WREN_TYPE_LIST) { st_wren_abort(vm, "Expected upload paths"); return; }
		int count = batch ? wrenGetListCount(vm, 2) : 1;
		if (count <= 0 || count > 65536) { st_wren_abort(vm, "Invalid upload file count"); return; }
		paths = calloc((size_t)count, sizeof(*paths));
		if (paths == NULL) { st_wren_abort(vm, "Out of memory"); return; }
		config.path_count = (size_t)count; config.paths = (const char *const *)paths;
		wrenEnsureSlots(vm, 5);
		for (int i = 0; i < count; ++i) {
			int slot = batch ? 4 : 2; const char *path;
			if (batch) wrenGetListElement(vm, 2, i, slot);
			if (!st_wren_text_slot(vm, slot, &path, &length)) goto finished;
			paths[i] = strdup(path);
			if (paths[i] == NULL) { st_wren_abort(vm, "Out of memory"); goto finished; }
		}
	} else if (config.protocol == ST_TRANSFER_XMODEM_CRC || config.protocol == ST_TRANSFER_XMODEM_CHECKSUM) {
		if (!st_wren_text_slot(vm, 2, &config.filename, &length)) return;
	}
	if (start(context, &config) != 0) st_wren_abort(vm, "Transfer could not start (active, unsafe, or invalid configuration)");
	else wrenSetSlotBool(vm, 0, true);
finished:
	for (size_t i = 0; i < config.path_count; ++i) free(paths[i]);
	free(paths);
}
static void upload(WrenVM *vm) { dispatch(vm, true, false); }
static void upload_batch(WrenVM *vm) { dispatch(vm, true, true); }
static void download(WrenVM *vm) { dispatch(vm, false, false); }
static void automatic(WrenVM *vm)
{
	struct st_wren_transfer *s = st_wren_context_from_vm(vm)->transfer;
	int event = s == NULL ? 0 : s->automatic;
	if (s != NULL) s->automatic = 0;
	if (event == 0) wrenSetSlotNull(vm, 0); else wrenSetSlotString(vm, 0, event == 1 ? "download" : "upload");
}
static void cancel_automatic(WrenVM *vm)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);
	struct st_wren_transfer *s = context->transfer;
	if (s != NULL && s->worker == NULL && (s->automatic != 0 || s->prefix_size != 0)) {
		s->automatic = 0; s->prefix_size = 0;
		st_term_session_reserve_transfer(context->session, false);
		static const uint8_t cancel[] = {24,24,24,24,24,24,24,24};
		if (context->connection != NULL) (void)st_connection_send(context->connection, cancel, sizeof(cancel), 100);
	}
	wrenSetSlotNull(vm, 0);
}
WrenForeignMethodFn st_wren_transfer_bind_method(const char *class_name, bool is_static, const char *signature)
{
	if (!is_static || strcmp(class_name, "Transfer") != 0) return NULL;
	static const struct { const char *signature; WrenForeignMethodFn method; } methods[] = {
		{"active", active}, {"uploading", uploading}, {"inlineUpload", inline_upload},
		{"beginSession(_)", begin}, {"endSession()", end}, {"done", done}, {"success", success},
		{"aborted", aborted}, {"requestAbort()", abort_transfer}, {"tickDirty", tick_dirty},
		{"snapshot()", snapshot}, {"drainLog()", drain_log}, {"dialogPending", dialog_pending},
		{"dialogFilename", dialog_filename}, {"dialogRespond(_,_)", dialog_respond},
		{"upload(_,_,_)", upload}, {"uploadBatch(_,_,_)", upload_batch}, {"download(_,_)", download},
		{"autoPending_", automatic}, {"cancelAuto_()", cancel_automatic}
	};
	for (size_t i = 0; i < sizeof(methods) / sizeof(methods[0]); ++i) if (strcmp(signature, methods[i].signature) == 0) return methods[i].method;
	return NULL;
}
