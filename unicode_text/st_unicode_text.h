/* Incremental UTF-8 validation, NFC normalization, and grapheme segmentation. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_UNICODE_TEXT_H_
#define ST_UNICODE_TEXT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct st_unicode_text st_unicode_text_t;

enum st_unicode_text_mode {
	ST_UNICODE_TEXT_NORMALIZE,
	ST_UNICODE_TEXT_GRAPHEMES,
};

enum st_unicode_text_item_kind {
	ST_UNICODE_TEXT_ITEM_TEXT,
	ST_UNICODE_TEXT_ITEM_DECODE_ERROR,
};

enum st_unicode_text_state {
	ST_UNICODE_TEXT_READY = UINT32_C(1) << 0,
	ST_UNICODE_TEXT_INCOMPLETE = UINT32_C(1) << 1,
	ST_UNICODE_TEXT_FINISHED = UINT32_C(1) << 2,
	ST_UNICODE_TEXT_SAW_DECODE_ERROR = UINT32_C(1) << 3,
	ST_UNICODE_TEXT_CHANGED = UINT32_C(1) << 4,
};

struct st_unicode_text_item {
	enum st_unicode_text_item_kind kind;
	const uint8_t *bytes;
	size_t size;
	bool rgi_emoji;
	uint8_t columns;
};

int st_unicode_text_create(st_unicode_text_t **result,
	enum st_unicode_text_mode mode);
void st_unicode_text_destroy(st_unicode_text_t **text);
void st_unicode_text_reset(st_unicode_text_t *text);

int st_unicode_text_feed_byte(st_unicode_text_t *text, uint8_t byte);
int st_unicode_text_feed(st_unicode_text_t *text, const void *bytes,
	size_t size, size_t *consumed);
int st_unicode_text_feed_scalar(st_unicode_text_t *text, uint32_t scalar);
int st_unicode_text_feed_scalars(st_unicode_text_t *text,
	const uint32_t *scalars, size_t count, size_t *consumed);
int st_unicode_text_finish(st_unicode_text_t *text);

uint32_t st_unicode_text_state(const st_unicode_text_t *text);
int st_unicode_text_peek(const st_unicode_text_t *text,
	struct st_unicode_text_item *item);
int st_unicode_text_consume(st_unicode_text_t *text);

/*
 * Strict finite-text convenience operation.  On success result is allocated,
 * NUL-terminated NFC and result_size excludes the terminator.  Outputs are
 * unchanged when the input is malformed or an allocation fails.
 */
int st_unicode_text_normalize(char **result, size_t *result_size,
	const void *bytes, size_t size, bool *changed);

#endif
