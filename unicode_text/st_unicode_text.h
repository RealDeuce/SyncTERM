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
typedef struct st_unicode_presentation st_unicode_presentation_t;

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
	/* Grapheme presentation hint: 0 suppresses drawing; isolated visible
	 * marks use 1 even when their scalar advance is zero. Normalize mode
	 * does not compute widths and always reports 0. */
	uint8_t columns;
};

enum st_unicode_presentation_event_kind {
	ST_UNICODE_PRESENTATION_BEGIN,
	ST_UNICODE_PRESENTATION_REVISE,
	ST_UNICODE_PRESENTATION_COMMIT,
	ST_UNICODE_PRESENTATION_DECODE_ERROR,
};

/*
 * BEGIN publishes the first scalar immediately. REVISE replaces that same
 * provisional grapheme after a combining/ZWJ extension. COMMIT makes the
 * most recently published bytes final. Event byte storage is borrowed only
 * for the callback. A decode error contains the offending original bytes.
 */
struct st_unicode_presentation_event {
	enum st_unicode_presentation_event_kind kind;
	const uint8_t *bytes;
	size_t size;
	bool rgi_emoji;
	/* 0, 1 or 2. Zero retains the bytes and provisional event lifecycle,
	 * but must not look up a glyph, move the cursor or trigger wrapping. */
	uint8_t columns;
};

typedef int (*st_unicode_presentation_callback_t)(void *context,
	const struct st_unicode_presentation_event *event);
/* Invoked for each decoded scalar before grapheme presentation.  A consumed
 * scalar is not added to the current grapheme. */
typedef int (*st_unicode_presentation_scalar_filter_t)(void *context,
	uint32_t scalar, bool *consumed);

int st_unicode_presentation_create(st_unicode_presentation_t **result,
	st_unicode_presentation_callback_t callback, void *context);
void st_unicode_presentation_set_scalar_filter(
	st_unicode_presentation_t *presentation,
	st_unicode_presentation_scalar_filter_t filter, void *context);
void st_unicode_presentation_destroy(st_unicode_presentation_t **presentation);
void st_unicode_presentation_reset(st_unicode_presentation_t *presentation);
int st_unicode_presentation_feed(st_unicode_presentation_t *presentation,
	const void *bytes, size_t size, size_t *consumed);
/*
 * Flushes an incomplete UTF-8 sequence as one decode error and commits the
 * provisional grapheme, then remains ready for more input. Use at a control,
 * reset, encoding change, or disconnect boundary, never at an ordinary read.
 */
int st_unicode_presentation_boundary(st_unicode_presentation_t *presentation);
/* Commit the provisional grapheme for a layout change, retaining incomplete
 * UTF-8 input for the next feed. This does not end the encoded stream. */
int st_unicode_presentation_commit(st_unicode_presentation_t *presentation);

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
 * NUL-terminated NFC and result_size excludes the terminator.  changed may be
 * NULL. Outputs are unchanged when the input is malformed or allocation fails.
 */
int st_unicode_text_normalize(char **result, size_t *result_size,
	const void *bytes, size_t size, bool *changed);

#endif
