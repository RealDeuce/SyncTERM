/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "private/transfer_internal.h"
#include <stdarg.h>

void st_transfer_log_message(st_transfer_t *t, int level, const char *format, ...)
{
	struct st_transfer_log entry = {.level = level};
	va_list arguments;
	va_start(arguments, format);
	vsnprintf(entry.text, sizeof(entry.text), format, arguments);
	va_end(arguments);
	(void)st_mutex_lock(&t->mutex);
	if (t->log_size < ST_TRANSFER_LOG_CAPACITY) {
		t->logs[(t->log_head + t->log_size++) % ST_TRANSFER_LOG_CAPACITY] = entry;
	} else ++t->log_dropped;
	(void)st_mutex_unlock(&t->mutex);
}
int st_transfer_protocol_log(void *context, int level, const char *text)
{
	st_transfer_log_message(context, level, "%s", text);
	return 0;
}
void st_transfer_progress(st_transfer_t *t, int64_t offset, int64_t total)
{
	uint64_t now = 0;
	(void)st_time_monotonic_ms(&now);
	uint64_t elapsed = now > t->started_ms ? now - t->started_ms : 1;
	(void)st_mutex_lock(&t->mutex);
	strlcpy(t->snapshot.line1, t->current_name, sizeof(t->snapshot.line1));
	if (t->file_count != 0)
		snprintf(t->snapshot.line2, sizeof(t->snapshot.line2),
		    "File %zu/%zu: %" PRId64 " bytes", t->file_number, t->file_count, offset);
	else
		snprintf(t->snapshot.line2, sizeof(t->snapshot.line2),
		    "File %zu: %" PRId64 " bytes", t->file_number, offset);
	snprintf(t->snapshot.line3, sizeof(t->snapshot.line3),
	    "Elapsed: %" PRIu64 "s   %" PRIu64 " bytes/s", elapsed / 1000,
	    offset > 0 ? (uint64_t)((long double)offset * 1000 / elapsed) : 0);
	t->snapshot.bytes_current = offset;
	t->snapshot.bytes_total = total > 0 ? total : 0;
	t->dirty = true;
	(void)st_mutex_unlock(&t->mutex);
}
bool st_transfer_cancelled(void *context)
{
	st_transfer_t *t = context;
	(void)st_mutex_lock(&t->mutex);
	bool result = t->abort;
	(void)st_mutex_unlock(&t->mutex);
	return result;
}
bool st_transfer_connected(void *context)
{
	st_transfer_t *t = context;
	(void)st_mutex_lock(&t->mutex);
	bool result = !t->abort && (!t->eof || t->input_size != 0);
	(void)st_mutex_unlock(&t->mutex);
	return result && t->io_error == 0;
}
int st_transfer_send_bytes(st_transfer_t *t, const void *bytes, size_t size, unsigned timeout)
{
	struct st_deadline deadline;
	int status = st_deadline_start(&deadline, timeout);
	size_t sent = 0;
	while (status == 0 && sent < size) {
		int64_t remaining;
		if (st_transfer_cancelled(t)) return IO_ERROR_CANCELLED;
		status = st_deadline_remaining_ms(&deadline, &remaining);
		if (status != 0) break;
		if (remaining == 0 && sent != 0) return IO_ERROR_TIMED_OUT;
		ssize_t count = t->config.send(t->config.send_context,
		    (const uint8_t *)bytes + sent, size - sent, remaining > 100 ? 100 : remaining);
		if (count == IO_ERROR_TIMED_OUT || count == IO_ERROR_WOULD_BLOCK) {
			if (remaining == 0) return IO_ERROR_TIMED_OUT;
			continue;
		}
		if (count <= 0) return count < 0 ? (int)count : IO_ERROR_NOT_CONNECTED;
		if ((size_t)count > size - sent) return IO_ERROR_IO;
		sent += (size_t)count;
	}
	return status;
}
void st_transfer_flush(void *context)
{
	st_transfer_t *t = context;
	if (t->output_size == 0 || t->io_error != 0) return;
	t->io_error = st_transfer_send_bytes(t, t->output, t->output_size, 10000);
	t->output_size = 0;
}
int st_transfer_send_byte(void *context, uint8_t byte, unsigned timeout)
{
	st_transfer_t *t = context;
	(void)timeout;
	if (t->io_error != 0 || st_transfer_cancelled(t)) return -1;
	t->output[t->output_size++] = byte;
	if (t->output_size == sizeof(t->output)) st_transfer_flush(t);
	return t->io_error == 0 ? 0 : -1;
}
int st_transfer_recv_ms(void *context, unsigned timeout)
{
	st_transfer_t *t = context;
	struct timespec deadline;
	st_transfer_flush(t);
	if (st_time_utc_deadline_after_ms(&deadline, timeout) != 0) return -1;
	(void)st_mutex_lock(&t->mutex);
	while (t->input_size == 0 && !t->eof && !t->abort && t->io_error == 0) {
		if (timeout == 0 || st_condition_timedwait(&t->changed, &t->mutex, &deadline) != ST_THREAD_SUCCESS) break;
	}
	int byte = -1;
	if (!t->abort && t->input_size != 0) {
		byte = t->input[t->input_head];
		t->input_head = (t->input_head + 1) % sizeof(t->input);
		--t->input_size;
	}
	(void)st_mutex_unlock(&t->mutex);
	return byte;
}
int st_transfer_recv(void *context, unsigned timeout)
{
	uint64_t remaining = (uint64_t)timeout * 1000;
	do {
		unsigned wait = remaining > 100 ? 100 : (unsigned)remaining;
		int byte = st_transfer_recv_ms(context, wait);
		if (byte >= 0 || !st_transfer_connected(context)) return byte;
		remaining -= wait;
	} while (remaining != 0);
	return -1;
}
bool st_transfer_waiting(void *context, unsigned timeout)
{
	st_transfer_t *t = context;
	struct timespec deadline;
	st_transfer_flush(t);
	if (st_time_utc_deadline_after_ms(&deadline, (int64_t)timeout * 1000) != 0) return false;
	(void)st_mutex_lock(&t->mutex);
	while (t->input_size == 0 && !t->abort && !t->eof) {
		if (timeout == 0 || st_condition_timedwait(&t->changed, &t->mutex, &deadline) != ST_THREAD_SUCCESS) break;
	}
	bool waiting = t->input_size != 0 && !t->abort;
	(void)st_mutex_unlock(&t->mutex);
	return waiting;
}
size_t st_transfer_recv_span(void *context, uint8_t *bytes, size_t size, const uint8_t *plain)
{
	st_transfer_t *t = context;
	size_t count = 0;
	(void)st_mutex_lock(&t->mutex);
	while (count < size && t->input_size != 0 && !t->abort) {
		uint8_t byte = t->input[t->input_head];
		if (!plain[byte]) break;
		bytes[count++] = byte;
		t->input_head = (t->input_head + 1) % sizeof(t->input);
		--t->input_size;
	}
	(void)st_mutex_unlock(&t->mutex);
	return count;
}
int st_transfer_request_dialog(st_transfer_t *t, enum st_transfer_dialog kind,
	const char *name, char *new_name, size_t size)
{
	(void)st_mutex_lock(&t->mutex);
	t->dialog = kind;
	t->response_ready = false;
	strlcpy(t->dialog_name, name, sizeof(t->dialog_name));
	while (!t->response_ready && !t->abort && !t->eof)
		(void)st_condition_wait(&t->changed, &t->mutex);
	int response = t->response_ready && !t->abort && !t->eof ? t->response : ST_TRANSFER_SKIP;
	if (new_name != NULL && size != 0) strlcpy(new_name, t->response_name, size);
	t->dialog = ST_TRANSFER_DIALOG_NONE;
	(void)st_mutex_unlock(&t->mutex);
	return response;
}
int st_transfer_destination(st_transfer_t *t, const char *name, char **path, bool allow_existing)
{
	const char *base = t->config.destination == NULL ? path_untrusted_basename(name) : name;
	char renamed[1024];
	if (!path_untrusted_component_valid(base)) return IO_ERROR_INVALID_ARGUMENT;
	int status = t->config.destination == NULL ? path_join_alloc(path, t->directory, base)
	    : t->config.destination(t->config.destination_context, t->directory, base, path);
	while (status == 0) {
		struct fs_info info;
		status = fs_get_link_info(&info, *path);
		if (status == IO_ERROR_NOT_FOUND) return 0;
		if (status != 0) break;
		if (info.kind != FS_ENTRY_FILE) { status = IO_ERROR_INVALID_ARGUMENT; break; }
		if (allow_existing || t->config.overwrite || t->config.discard) return 0;
		int response = st_transfer_request_dialog(t, ST_TRANSFER_DIALOG_DUPLICATE, base, renamed, sizeof(renamed));
		if (st_transfer_cancelled(t)) { status = IO_ERROR_CANCELLED; break; }
		if (response == ST_TRANSFER_SKIP) { status = 1; break; }
		if (response == ST_TRANSFER_OVERWRITE) return 0;
		if (!path_untrusted_component_valid(renamed)) { status = IO_ERROR_INVALID_ARGUMENT; break; }
		free(*path); *path = NULL;
		status = t->config.destination == NULL ? path_join_alloc(path, t->directory, renamed)
		    : t->config.destination(t->config.destination_context, t->directory, renamed, path);
		base = renamed;
	}
	free(*path); *path = NULL;
	return status;
}
int st_transfer_received(st_transfer_t *t, enum st_transfer_receive_kind kind,
	const uint8_t *bytes, size_t size, bool success)
{
	if (!t->config.receive_events) return 0;
	do {
		(void)st_mutex_lock(&t->mutex);
		while (t->received_size == 16 && !t->abort && !t->eof)
			(void)st_condition_wait(&t->changed, &t->mutex);
		if (t->abort || t->eof) {
			(void)st_mutex_unlock(&t->mutex);
			return IO_ERROR_CANCELLED;
		}
		struct st_transfer_receive_event *event = &t->received[(t->received_head + t->received_size++) % 16];
		event->kind = kind;
		strlcpy(event->filename, t->current_name, sizeof(event->filename));
		event->size = size > sizeof(event->bytes) ? sizeof(event->bytes) : size;
		event->success = success;
		if (event->size != 0) memcpy(event->bytes, bytes, event->size);
		size_t count = event->size;
		(void)st_mutex_unlock(&t->mutex);
		if (size == 0) break;
		bytes += count; size -= count;
	} while (size != 0);
	return 0;
}
bool st_transfer_take_received(st_transfer_t *t, struct st_transfer_receive_event *event)
{
	if (t == NULL || event == NULL) return false;
	(void)st_mutex_lock(&t->mutex);
	bool available = t->received_size != 0;
	if (available) {
		*event = t->received[t->received_head];
		t->received_head = (t->received_head + 1) % 16; --t->received_size;
		(void)st_condition_broadcast(&t->changed);
	}
	(void)st_mutex_unlock(&t->mutex);
	return available;
}
static int worker(void *context)
{
	st_transfer_t *t = context;
	int result;
	(void)st_time_monotonic_ms(&t->started_ms);
	st_transfer_log_message(t, LOG_INFO, "%s started", t->config.upload ? "Upload" : "Download");
	switch (t->config.protocol) {
	case ST_TRANSFER_ZMODEM: result = st_transfer_run_zmodem(t); break;
	case ST_TRANSFER_CET: result = st_transfer_run_cet(t); break;
	case ST_TRANSFER_ASCII: case ST_TRANSFER_RAW: result = st_transfer_run_upload(t); break;
	case ST_TRANSFER_FAKE:
		result = 0;
		for (int i = 0; i <= 100; ++i) {
			if (st_transfer_cancelled(t)) { result = IO_ERROR_CANCELLED; break; }
			st_transfer_progress(t, i * 1024, 100 * 1024);
			st_transfer_sleep(10);
		}
		break;
	default: result = st_transfer_run_xymodem(t); break;
	}
	st_transfer_flush(t);
	if (result == 0) result = t->io_error;
	if (st_transfer_cancelled(t)) result = IO_ERROR_CANCELLED;
	if (result != 0 && st_transfer_needs_input(t) && t->config.send != NULL) {
		static const uint8_t cancel[] = {24,24,24,24,24,24,24,24,8,8,8,8,8,8,8,8};
		(void)t->config.send(t->config.send_context, cancel, sizeof(cancel), 100);
	}
	st_transfer_log_message(t, result == 0 ? LOG_INFO : LOG_ERR,
	    result == 0 ? "Transfer complete" : result == IO_ERROR_CANCELLED ? "Transfer cancelled" : "Transfer failed (%d)", result);
	(void)st_mutex_lock(&t->mutex);
	t->result = result; t->done = true; t->dirty = true;
	(void)st_condition_broadcast(&t->changed);
	(void)st_mutex_unlock(&t->mutex);
	return 0;
}
int st_transfer_create(st_transfer_t **result, const struct st_transfer_config *config)
{
	if (result == NULL || config == NULL || config->protocol < ST_TRANSFER_ZMODEM
	    || config->protocol > ST_TRANSFER_XMODEM_G || (config->protocol != ST_TRANSFER_FAKE && config->send == NULL)
	    || (config->upload && (config->path_count == 0 || config->paths == NULL))
	    || (!config->upload && config->protocol != ST_TRANSFER_FAKE && (config->directory == NULL || !*config->directory))) return IO_ERROR_INVALID_ARGUMENT;
	if (config->path_count > 65536
	    || (config->upload && (config->discard || config->receive_events))
	    || (config->receive_events && config->protocol == ST_TRANSFER_ZMODEM && !config->overwrite && !config->discard)
	    || config->last_ch < 0 || config->last_ch > 255
	    || (config->upload && config->path_count != 1 && config->protocol != ST_TRANSFER_ZMODEM
	    && config->protocol != ST_TRANSFER_YMODEM && config->protocol != ST_TRANSFER_YMODEM_G)
	    || (config->upload && config->protocol == ST_TRANSFER_CET)
	    || (!config->upload && (config->protocol == ST_TRANSFER_ASCII || config->protocol == ST_TRANSFER_RAW
	    || config->protocol == ST_TRANSFER_XMODEM_1K || config->protocol == ST_TRANSFER_XMODEM_128))
	    || (config->upload && (config->protocol == ST_TRANSFER_XMODEM_CRC || config->protocol == ST_TRANSFER_XMODEM_CHECKSUM)))
		return IO_ERROR_INVALID_ARGUMENT;
	for (size_t i = 0; i < config->path_count; ++i)
		if (config->paths == NULL || config->paths[i] == NULL || !*config->paths[i]) return IO_ERROR_INVALID_ARGUMENT;
	st_transfer_t *t = calloc(1, sizeof(*t));
	if (t == NULL) return IO_ERROR_OUT_OF_MEMORY;
	if (st_mutex_init(&t->mutex, ST_MUTEX_PLAIN) != ST_THREAD_SUCCESS) { free(t); return IO_ERROR_OUT_OF_MEMORY; }
	if (st_condition_init(&t->changed) != ST_THREAD_SUCCESS) { st_mutex_destroy(&t->mutex); free(t); return IO_ERROR_OUT_OF_MEMORY; }
	t->config = *config;
	t->result = IO_ERROR_IN_PROGRESS;
	t->directory = strdup(config->directory == NULL ? "" : config->directory);
	t->filename = strdup(config->filename == NULL ? "" : config->filename);
	t->paths = calloc(config->path_count + 1, sizeof(*t->paths));
	if (t->directory == NULL || t->filename == NULL || t->paths == NULL) goto no_memory;
	for (size_t i = 0; i < config->path_count; ++i) {
		if (config->paths[i] == NULL) goto no_memory;
		t->paths[i] = strdup(config->paths[i]);
		if (t->paths[i] == NULL) goto no_memory;
	}
	t->config.paths = (const char *const *)t->paths;
	t->config.directory = t->directory; t->config.filename = t->filename;
	t->file_count = config->path_count; t->file_number = 1;
	if (config->upload && config->protocol != ST_TRANSFER_ZMODEM
	    && (config->last_ch == 'G' || config->last_ch == 'C' || config->last_ch == NAK)) {
		t->input[0] = (uint8_t)config->last_ch;
		t->input_size = 1;
	}
	*result = t;
	return 0;
no_memory:
	st_transfer_destroy(&t);
	return IO_ERROR_OUT_OF_MEMORY;
}
int st_transfer_launch(st_transfer_t *t)
{
	if (t == NULL || t->thread_started || t->done || t->abort) return IO_ERROR_INVALID_ARGUMENT;
	if (st_thread_create(&t->thread, worker, t) != ST_THREAD_SUCCESS) return IO_ERROR_OUT_OF_MEMORY;
	t->thread_started = true;
	return 0;
}
int st_transfer_start(st_transfer_t **result, const struct st_transfer_config *config)
{
	int status = st_transfer_create(result, config);
	if (status != 0) return status;
	status = st_transfer_launch(*result);
	if (status != 0) st_transfer_destroy(result);
	return status;
}
bool st_transfer_request_abort(st_transfer_t *t)
{
	if (t == NULL) return false;
	(void)st_mutex_lock(&t->mutex);
	bool changed = !t->abort && !t->done;
	if (!t->done) t->abort = true;
	(void)st_condition_broadcast(&t->changed);
	(void)st_mutex_unlock(&t->mutex);
	return changed;
}
int st_transfer_join(st_transfer_t *t)
{
	if (t == NULL) return IO_ERROR_INVALID_ARGUMENT;
	(void)st_transfer_request_abort(t);
	if (t->thread_started) {
		(void)st_thread_join(t->thread, NULL);
		t->thread_started = false;
	}
	if (!t->done) { t->done = true; t->result = IO_ERROR_CANCELLED; }
	return st_transfer_result(t);
}
void st_transfer_destroy(st_transfer_t **pointer)
{
	if (pointer == NULL || *pointer == NULL) return;
	st_transfer_t *t = *pointer;
	(void)st_transfer_join(t);
	for (size_t i = 0; i < t->config.path_count; ++i) free(t->paths == NULL ? NULL : t->paths[i]);
	free(t->paths); free(t->directory); free(t->filename); free(t->replay);
	st_condition_destroy(&t->changed); st_mutex_destroy(&t->mutex); free(t); *pointer = NULL;
}
bool st_transfer_done(st_transfer_t *t)
{
	if (t == NULL) return true;
	(void)st_mutex_lock(&t->mutex); bool value = t->done; (void)st_mutex_unlock(&t->mutex); return value;
}
int st_transfer_result(st_transfer_t *t)
{
	if (t == NULL) return IO_ERROR_INVALID_ARGUMENT;
	(void)st_mutex_lock(&t->mutex); int value = t->result; (void)st_mutex_unlock(&t->mutex); return value;
}
bool st_transfer_aborted(st_transfer_t *t) { return t != NULL && st_transfer_cancelled(t); }
bool st_transfer_needs_input(const st_transfer_t *t)
{
	return t != NULL && t->config.protocol != ST_TRANSFER_RAW && t->config.protocol != ST_TRANSFER_ASCII && t->config.protocol != ST_TRANSFER_FAKE;
}
size_t st_transfer_input_space(st_transfer_t *t)
{
	(void)st_mutex_lock(&t->mutex); size_t size = sizeof(t->input) - t->input_size; (void)st_mutex_unlock(&t->mutex); return size;
}
size_t st_transfer_feed(st_transfer_t *t, const void *bytes, size_t size)
{
	(void)st_mutex_lock(&t->mutex);
	if (size > sizeof(t->input) - t->input_size) size = sizeof(t->input) - t->input_size;
	for (size_t i = 0; i < size; ++i) t->input[(t->input_head + t->input_size + i) % sizeof(t->input)] = ((const uint8_t *)bytes)[i];
	t->input_size += size;
	(void)st_condition_signal(&t->changed);
	(void)st_mutex_unlock(&t->mutex);
	return size;
}
void st_transfer_eof(st_transfer_t *t)
{
	(void)st_mutex_lock(&t->mutex); t->eof = true; (void)st_condition_broadcast(&t->changed); (void)st_mutex_unlock(&t->mutex);
}
size_t st_transfer_take_input(st_transfer_t *t, void *bytes, size_t size)
{
	(void)st_mutex_lock(&t->mutex);
	if (!t->done) size = 0;
	if (size > t->input_size) size = t->input_size;
	for (size_t i = 0; i < size; ++i) ((uint8_t *)bytes)[i] = t->input[(t->input_head + i) % sizeof(t->input)];
	t->input_head = (t->input_head + size) % sizeof(t->input); t->input_size -= size;
	(void)st_mutex_unlock(&t->mutex);
	return size;
}
bool st_transfer_tick_dirty(st_transfer_t *t)
{
	(void)st_mutex_lock(&t->mutex); bool dirty = t->dirty; (void)st_mutex_unlock(&t->mutex); return dirty;
}
void st_transfer_snapshot(st_transfer_t *t, struct st_transfer_snapshot *snapshot)
{
	(void)st_mutex_lock(&t->mutex); *snapshot = t->snapshot; t->dirty = false; (void)st_mutex_unlock(&t->mutex);
}
size_t st_transfer_drain_log(st_transfer_t *t, struct st_transfer_log *logs, size_t count)
{
	size_t used = 0;
	(void)st_mutex_lock(&t->mutex);
	while (used < count && t->log_size != 0) {
		logs[used++] = t->logs[t->log_head]; t->log_head = (t->log_head + 1) % ST_TRANSFER_LOG_CAPACITY; --t->log_size;
	}
	if (used < count && t->log_dropped != 0) {
		logs[used].level = LOG_NOTICE;
		snprintf(logs[used++].text, sizeof(logs[0].text), "[%zu log lines suppressed]", t->log_dropped); t->log_dropped = 0;
	}
	(void)st_mutex_unlock(&t->mutex);
	return used;
}
enum st_transfer_dialog st_transfer_dialog(st_transfer_t *t, char *filename, size_t size)
{
	(void)st_mutex_lock(&t->mutex);
	enum st_transfer_dialog kind = t->dialog;
	if (filename != NULL && size != 0) strlcpy(filename, t->dialog_name, size);
	(void)st_mutex_unlock(&t->mutex);
	return kind;
}
int st_transfer_respond(st_transfer_t *t, int response, const char *name)
{
	if (response < 0 || response > ST_TRANSFER_RENAME || (name != NULL && strlen(name) >= sizeof(t->response_name))) return IO_ERROR_INVALID_ARGUMENT;
	(void)st_mutex_lock(&t->mutex);
	if (t->dialog == ST_TRANSFER_DIALOG_NONE || t->response_ready) { (void)st_mutex_unlock(&t->mutex); return IO_ERROR_INVALID_ARGUMENT; }
	t->response = response; t->response_ready = true;
	strlcpy(t->response_name, name == NULL ? "" : name, sizeof(t->response_name));
	(void)st_condition_broadcast(&t->changed); (void)st_mutex_unlock(&t->mutex);
	return 0;
}
const uint8_t *st_transfer_replay(st_transfer_t *t, size_t *size)
{
	if (!st_transfer_done(t)) { *size = 0; return NULL; }
	*size = t->replay_size; return t->replay;
}
