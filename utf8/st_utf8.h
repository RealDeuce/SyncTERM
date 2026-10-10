/* Strict, locale-independent UTF-8 encoding and decoding. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_UTF8_H_
#define ST_UTF8_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum st_utf8_decode_event_kind {
	ST_UTF8_DECODE_EVENT_NONE,
	ST_UTF8_DECODE_EVENT_SCALAR,
	ST_UTF8_DECODE_EVENT_MALFORMED,
};

struct st_utf8_decode_event {
	enum st_utf8_decode_event_kind kind;
	union {
		uint32_t scalar;
		struct {
			uint8_t bytes[4];
			size_t size;
		} malformed;
	} data;
};

/* Caller-owned incremental decoder state.  An all-zero value is initialized. */
struct st_utf8_decoder {
	uint8_t pending[4];
	size_t pending_size;
	size_t expected_size;
};

bool st_unicode_scalar_valid(uint32_t value);

/* Outputs are unchanged on failure. */
int st_utf8_decode_one(uint32_t *result, size_t *consumed,
	const void *data, size_t size);

/*
 * Returns the required byte count or a negative IO_ERROR_* value.  A NULL
 * result is permitted only when result_size is zero.  An undersized buffer is
 * unchanged.
 */
ptrdiff_t st_utf8_encode_one(char *result, size_t result_size,
	uint32_t value);

/* init/reset accept NULL; reset silently discards an incomplete prefix. */
void st_utf8_decoder_init(struct st_utf8_decoder *decoder);
void st_utf8_decoder_reset(struct st_utf8_decoder *decoder);
/* Returns false for NULL. */
bool st_utf8_decoder_incomplete(const struct st_utf8_decoder *decoder);

/*
 * Consumes input through at most one scalar or malformed maximal subpart.
 * When a byte terminates a malformed prefix it is not consumed, so the caller
 * must process the event and feed that byte again.  A NONE event means all of
 * the supplied input was consumed into an incomplete prefix (or size was
 * zero).  Outputs and decoder state are unchanged on argument failure.
 */
int st_utf8_decoder_feed(struct st_utf8_decoder *decoder,
	const void *data, size_t size, size_t *consumed,
	struct st_utf8_decode_event *event);

/* Emits an incomplete prefix as a malformed event and leaves decoder empty. */
int st_utf8_decoder_flush(struct st_utf8_decoder *decoder,
	struct st_utf8_decode_event *event);

#endif
