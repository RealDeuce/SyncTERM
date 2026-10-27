/* Session-owned in-band transfers. All paths are UTF-8. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_TRANSFER_H
#define ST_TRANSFER_H
#include "syncterm_types.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct st_transfer st_transfer_t;
enum st_transfer_protocol {
	ST_TRANSFER_ZMODEM, ST_TRANSFER_YMODEM, ST_TRANSFER_YMODEM_G,
	ST_TRANSFER_XMODEM_1K, ST_TRANSFER_XMODEM_128,
	ST_TRANSFER_XMODEM_CRC, ST_TRANSFER_XMODEM_CHECKSUM,
	ST_TRANSFER_ASCII, ST_TRANSFER_RAW, ST_TRANSFER_CET, ST_TRANSFER_FAKE,
	ST_TRANSFER_XMODEM_G
};
struct st_transfer_config {
	enum st_transfer_protocol protocol;
	bool upload;
	const char *const *paths;
	size_t path_count;
	const char *directory;
	const char *filename;
	int last_ch;
	/* Optional receive policy. The callback runs on the worker and returns
	 * an allocated destination or an IO_ERROR. Borrowed until join. */
	int (*destination)(void *context, const char *directory, const char *name, char **path);
	void *destination_context;
	bool overwrite; /* Explicit asset-refresh policy; disables ZMODEM resume. */
	bool discard; /* Receive into a temporary stream instead of the destination. */
	/* Owner must drain the bounded event mailbox. ZMODEM event consumers
	 * select overwrite or discard, so playback always starts at offset zero. */
	bool receive_events;
	/* Borrowed until destroy joins the worker. Must honor the timeout and
	 * short counts. No receive callback: the terminal owner feeds input. */
	ssize_t (*send)(void *context, const void *bytes, size_t size, int64_t timeout_ms);
	void *send_context;
};
struct st_transfer_snapshot {
	char line1[1024], line2[160], line3[160], line4[160];
	int64_t bytes_current, bytes_total;
};
struct st_transfer_log { int level; char text[480]; };
enum st_transfer_receive_kind { ST_TRANSFER_RECEIVE_BEGIN, ST_TRANSFER_RECEIVE_DATA, ST_TRANSFER_RECEIVE_END };
struct st_transfer_receive_event {
	enum st_transfer_receive_kind kind;
	char filename[1024];
	uint8_t bytes[4096];
	size_t size;
	bool success; /* END only; failed files must not flush an incomplete scene. */
};
/* Nonblocking owner receive; wakes a producer waiting for mailbox capacity. */
bool st_transfer_take_received(st_transfer_t *transfer, struct st_transfer_receive_event *event);
enum st_transfer_dialog { ST_TRANSFER_DIALOG_NONE, ST_TRANSFER_DIALOG_DUPLICATE, ST_TRANSFER_DIALOG_FILENAME };
enum st_transfer_response { ST_TRANSFER_OVERWRITE, ST_TRANSFER_SKIP, ST_TRANSFER_RENAME };

/* Create copies configuration without performing I/O. The terminal owner can
 * then claim the stream and enable binary mode before launching the worker.
 * One owner serializes launch, feed, snapshots, dialogs and destruction. */
int st_transfer_create(st_transfer_t **result, const struct st_transfer_config *config);
int st_transfer_launch(st_transfer_t *transfer);
/* Convenience for owners which have already established stream ownership. */
int st_transfer_start(st_transfer_t **result, const struct st_transfer_config *config);
/* Abort wakes both input and dialog predicates. Destroy joins before freeing. */
bool st_transfer_request_abort(st_transfer_t *transfer);
int st_transfer_join(st_transfer_t *transfer);
void st_transfer_destroy(st_transfer_t **transfer);
bool st_transfer_done(st_transfer_t *transfer);
int st_transfer_result(st_transfer_t *transfer);
bool st_transfer_aborted(st_transfer_t *transfer);
bool st_transfer_needs_input(const st_transfer_t *transfer);
/* Nonblocking, bounded input. Feed only the returned capacity, preserve the
 * rest in the terminal's existing buffer. EOF follows the last received byte. */
size_t st_transfer_input_space(st_transfer_t *transfer);
size_t st_transfer_feed(st_transfer_t *transfer, const void *bytes, size_t size);
void st_transfer_eof(st_transfer_t *transfer);
/* After done, return queued trailing terminal output before destroy. */
size_t st_transfer_take_input(st_transfer_t *transfer, void *bytes, size_t size);
/* Snapshot clears dirty. Logs are drained on the owner, including suppression. */
bool st_transfer_tick_dirty(st_transfer_t *transfer);
void st_transfer_snapshot(st_transfer_t *transfer, struct st_transfer_snapshot *snapshot);
size_t st_transfer_drain_log(st_transfer_t *transfer, struct st_transfer_log *logs, size_t count);
enum st_transfer_dialog st_transfer_dialog(st_transfer_t *transfer, char *filename, size_t capacity);
int st_transfer_respond(st_transfer_t *transfer, int response, const char *new_name);
/* Non-destructive, coherent progress/completion read; never clears UI dirty. */
struct st_transfer_inspection {
	struct st_transfer_snapshot progress;
	enum st_transfer_protocol protocol;
	enum st_transfer_dialog dialog;
	bool upload, done, aborted;
	int result;
};
void st_transfer_inspect(st_transfer_t *transfer, struct st_transfer_inspection *state);
/* CET's final raw frame is borrowed after done and before destroy. */
const uint8_t *st_transfer_replay(st_transfer_t *transfer, size_t *size);
#endif
