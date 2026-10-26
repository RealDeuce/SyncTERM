/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_TRANSFER_INTERNAL_H
#define ST_TRANSFER_INTERNAL_H
#include "st_transfer.h"
#include "transfer_protocol.h"
#include "st_time.h"
#include "io_error.h"
#define ST_TRANSFER_INPUT_CAPACITY 65536
#define ST_TRANSFER_LOG_CAPACITY 256
struct st_transfer {
	struct st_transfer_config config;
	char **paths;
	char *directory, *filename;
	st_thread_t thread;
	bool thread_started;
	/* Mutex owns queue, EOF/abort/completion, snapshots, logs and dialog.
	 * changed means input arrived, EOF/abort occurred, or a dialog was answered.
	 * The owner feeds input and drains mailboxes; the worker waits in loops. */
	st_mutex_t mutex;
	st_condition_t changed;
	uint8_t input[ST_TRANSFER_INPUT_CAPACITY];
	size_t input_head, input_size;
	bool eof, abort, done, dirty;
	int result;
	struct st_transfer_snapshot snapshot;
	struct st_transfer_log logs[ST_TRANSFER_LOG_CAPACITY];
	size_t log_head, log_size, log_dropped;
	enum st_transfer_dialog dialog;
	char dialog_name[1024], response_name[1024];
	int response;
	bool response_ready;
	/* Worker-owned, read by the owner only after done synchronizes. */
	uint8_t output[8192];
	size_t output_size;
	int io_error;
	char current_name[1024];
	int64_t current_size;
	uint64_t started_ms;
	size_t file_number, file_count;
	uint8_t *replay;
	size_t replay_size, replay_capacity;
};
void st_transfer_log_message(st_transfer_t *t, int level, const char *format, ...);
int st_transfer_protocol_log(void *context, int level, const char *text);
void st_transfer_progress(st_transfer_t *t, int64_t offset, int64_t total);
int st_transfer_recv_ms(void *context, unsigned timeout);
int st_transfer_recv(void *context, unsigned timeout);
int st_transfer_send_byte(void *context, uint8_t byte, unsigned timeout);
void st_transfer_flush(void *context);
bool st_transfer_connected(void *context);
bool st_transfer_cancelled(void *context);
bool st_transfer_waiting(void *context, unsigned timeout);
size_t st_transfer_recv_span(void *context, uint8_t *bytes, size_t size, const uint8_t *plain);
int st_transfer_send_bytes(st_transfer_t *t, const void *bytes, size_t size, unsigned timeout);
int st_transfer_request_dialog(st_transfer_t *t, enum st_transfer_dialog kind, const char *name, char *new_name, size_t size);
/* Returns 0 with a destination, 1 on skip, or an IO_ERROR. */
int st_transfer_destination(st_transfer_t *t, const char *name, char **path, bool allow_existing);
int st_transfer_run_zmodem(st_transfer_t *t);
int st_transfer_run_xymodem(st_transfer_t *t);
int st_transfer_run_cet(st_transfer_t *t);
int st_transfer_run_upload(st_transfer_t *t);
#endif
