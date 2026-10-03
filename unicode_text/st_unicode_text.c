/* Incremental UTF-8 validation, NFC normalization, and grapheme segmentation. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_unicode_text.h"

#include "io_error.h"
#include "st_unicode_grapheme.h"
#include "st_unicode_normalization.h"
#include "st_unicode_width.h"
#include "st_utf8.h"

#include <stdlib.h>
#include <string.h>

struct scalar_buffer {
	uint32_t *values;
	size_t count;
	size_t capacity;
};

struct byte_buffer {
	uint8_t *values;
	size_t count;
	size_t capacity;
};

struct queued_item {
	enum st_unicode_text_item_kind kind;
	size_t offset;
	size_t size;
	bool rgi_emoji;
	uint8_t columns;
};

struct item_buffer {
	struct queued_item *values;
	size_t count;
	size_t capacity;
};

struct st_unicode_text {
	enum st_unicode_text_mode mode;
	uint32_t state;
	uint8_t utf8[4];
	size_t utf8_count;
	size_t utf8_expected;
	struct scalar_buffer segment;
	struct scalar_buffer original;
	struct scalar_buffer cluster;
	struct byte_buffer output;
	struct item_buffer items;
	size_t item_head;
};

static bool
grow(void **values, size_t *capacity, size_t required, size_t element)
{
	size_t fresh;
	void *allocation;

	if (required <= *capacity)
		return true;
	fresh = *capacity == 0 ? 16 : *capacity;
	while (fresh < required) {
		if (fresh > SIZE_MAX / 2) {
			fresh = required;
			break;
		}
		fresh *= 2;
	}
	if (fresh > SIZE_MAX / element)
		return false;
	allocation = realloc(*values, fresh * element);
	if (allocation == NULL)
		return false;
	*values = allocation;
	*capacity = fresh;
	return true;
}

static bool
scalar_append(struct scalar_buffer *buffer, uint32_t value)
{
	if (!grow((void **)&buffer->values, &buffer->capacity,
	    buffer->count + 1, sizeof(buffer->values[0])))
		return false;
	buffer->values[buffer->count++] = value;
	return true;
}

static bool
byte_append(struct byte_buffer *buffer, const void *bytes, size_t size,
	size_t *offset)
{
	if (size > SIZE_MAX - buffer->count
	    || !grow((void **)&buffer->values, &buffer->capacity,
	    buffer->count + size, sizeof(buffer->values[0])))
		return false;
	*offset = buffer->count;
	memcpy(buffer->values + buffer->count, bytes, size);
	buffer->count += size;
	return true;
}

static bool
item_append(struct item_buffer *buffer, const struct queued_item *item)
{
	if (!grow((void **)&buffer->values, &buffer->capacity,
	    buffer->count + 1, sizeof(buffer->values[0])))
		return false;
	buffer->values[buffer->count++] = *item;
	return true;
}

static int
queue_bytes(st_unicode_text_t *text, enum st_unicode_text_item_kind kind,
	const void *bytes, size_t size, bool rgi, uint8_t columns)
{
	struct queued_item item = {
		.kind = kind,
		.rgi_emoji = rgi,
		.columns = columns,
	};

	if (kind == ST_UNICODE_TEXT_ITEM_TEXT
	    && text->mode == ST_UNICODE_TEXT_NORMALIZE
	    && text->items.count > text->item_head
	    && text->items.values[text->items.count - 1].kind
	    == ST_UNICODE_TEXT_ITEM_TEXT) {
		if (!byte_append(&text->output, bytes, size, &item.offset))
			return IO_ERROR_OUT_OF_MEMORY;
		text->items.values[text->items.count - 1].size += size;
		text->state |= ST_UNICODE_TEXT_READY;
		return 0;
	}
	if (!byte_append(&text->output, bytes, size, &item.offset))
		return IO_ERROR_OUT_OF_MEMORY;
	item.size = size;
	if (!item_append(&text->items, &item)) {
		text->output.count = item.offset;
		return IO_ERROR_OUT_OF_MEMORY;
	}
	text->state |= ST_UNICODE_TEXT_READY;
	return 0;
}

static bool
grapheme_control(enum st_unicode_grapheme_break value)
{
	return value == ST_UNICODE_GRAPHEME_CR
	    || value == ST_UNICODE_GRAPHEME_LF
	    || value == ST_UNICODE_GRAPHEME_CONTROL;
}

static bool
indic_no_break(const struct scalar_buffer *cluster, uint32_t next)
{
	if (st_unicode_indic_conjunct_break(next) != ST_UNICODE_INCB_CONSONANT)
		return false;
	for (size_t i = cluster->count; i > 0; --i) {
		enum st_unicode_indic_conjunct_break property
		    = st_unicode_indic_conjunct_break(cluster->values[i - 1]);

		if (property == ST_UNICODE_INCB_LINKER)
			return true;
		if (property == ST_UNICODE_INCB_EXTEND)
			continue;
		return false;
	}
	return false;
}

static bool
emoji_zwj_no_break(const struct scalar_buffer *cluster, uint32_t next)
{
	size_t index;

	if (!st_unicode_extended_pictographic(next) || cluster->count == 0
	    || st_unicode_grapheme_break(cluster->values[cluster->count - 1])
	    != ST_UNICODE_GRAPHEME_ZWJ)
		return false;
	index = cluster->count - 1;
	while (index > 0
	    && st_unicode_grapheme_break(cluster->values[index - 1])
	    == ST_UNICODE_GRAPHEME_EXTEND)
		--index;
	return index > 0
	    && st_unicode_extended_pictographic(cluster->values[index - 1]);
}

static bool
grapheme_break_before(const struct scalar_buffer *cluster, uint32_t next)
{
	enum st_unicode_grapheme_break left;
	enum st_unicode_grapheme_break right = st_unicode_grapheme_break(next);
	size_t regional = 0;

	if (cluster->count == 0)
		return false;
	left = st_unicode_grapheme_break(cluster->values[cluster->count - 1]);
	if (left == ST_UNICODE_GRAPHEME_CR && right == ST_UNICODE_GRAPHEME_LF)
		return false;
	if (grapheme_control(left) || grapheme_control(right))
		return true;
	if (left == ST_UNICODE_GRAPHEME_L
	    && (right == ST_UNICODE_GRAPHEME_L
	    || right == ST_UNICODE_GRAPHEME_V
	    || right == ST_UNICODE_GRAPHEME_LV
	    || right == ST_UNICODE_GRAPHEME_LVT))
		return false;
	if ((left == ST_UNICODE_GRAPHEME_LV
	    || left == ST_UNICODE_GRAPHEME_V)
	    && (right == ST_UNICODE_GRAPHEME_V
	    || right == ST_UNICODE_GRAPHEME_T))
		return false;
	if ((left == ST_UNICODE_GRAPHEME_LVT
	    || left == ST_UNICODE_GRAPHEME_T)
	    && right == ST_UNICODE_GRAPHEME_T)
		return false;
	if (right == ST_UNICODE_GRAPHEME_EXTEND
	    || right == ST_UNICODE_GRAPHEME_ZWJ
	    || right == ST_UNICODE_GRAPHEME_SPACING_MARK)
		return false;
	if (left == ST_UNICODE_GRAPHEME_PREPEND)
		return false;
	if (indic_no_break(cluster, next) || emoji_zwj_no_break(cluster, next))
		return false;
	if (right == ST_UNICODE_GRAPHEME_REGIONAL_INDICATOR) {
		for (size_t i = cluster->count; i > 0; --i) {
			if (st_unicode_grapheme_break(cluster->values[i - 1])
			    != ST_UNICODE_GRAPHEME_REGIONAL_INDICATOR)
				break;
			++regional;
		}
		if ((regional & 1U) != 0)
			return false;
	}
	return true;
}

static int
emit_cluster(st_unicode_text_t *text)
{
	uint8_t encoded[4];
	struct byte_buffer bytes = {0};
	bool rgi;
	uint8_t columns = 1;
	int status = 0;

	if (text->cluster.count == 0)
		return 0;
	for (size_t i = 0; i < text->cluster.count; ++i) {
		ptrdiff_t count = st_utf8_encode_one((char *)encoded,
		    sizeof(encoded), text->cluster.values[i]);
		size_t ignored;

		if (count < 0 || !byte_append(&bytes, encoded, (size_t)count,
		    &ignored)) {
			status = IO_ERROR_OUT_OF_MEMORY;
			goto finished;
		}
	}
	rgi = st_unicode_rgi_emoji_prefix(text->cluster.values,
	    text->cluster.count) == text->cluster.count;
	if (rgi)
		columns = 2;
	else {
		for (size_t i = 0; i < text->cluster.count; ++i) {
			enum st_unicode_grapheme_break property
			    = st_unicode_grapheme_break(text->cluster.values[i]);

			if (property != ST_UNICODE_GRAPHEME_EXTEND
			    && property != ST_UNICODE_GRAPHEME_ZWJ) {
				columns = st_unicode_columns(text->cluster.values[i]);
				if (columns == 0)
					columns = 1;
				break;
			}
		}
	}
	status = queue_bytes(text, ST_UNICODE_TEXT_ITEM_TEXT, bytes.values,
	    bytes.count, rgi, columns);
	if (status == 0)
		text->cluster.count = 0;
finished:
	free(bytes.values);
	return status;
}

static int
accept_normalized(st_unicode_text_t *text, uint32_t scalar)
{
	uint8_t encoded[4];
	ptrdiff_t size;

	if (text->mode == ST_UNICODE_TEXT_NORMALIZE) {
		size_t encoded_size;

		size = st_utf8_encode_one((char *)encoded, sizeof(encoded), scalar);
		if (size < 0)
			return IO_ERROR_INVALID_ENCODING;
		encoded_size = (size_t)size;
		return queue_bytes(text, ST_UNICODE_TEXT_ITEM_TEXT, encoded,
		    encoded_size, false, 0);
	}
	if (grapheme_break_before(&text->cluster, scalar)) {
		int status = emit_cluster(text);

		if (status != 0)
			return status;
	}
	return scalar_append(&text->cluster, scalar)
	    ? 0 : IO_ERROR_OUT_OF_MEMORY;
}

static int
flush_segment(st_unicode_text_t *text)
{
	struct scalar_buffer normalized = {0};
	size_t starter = SIZE_MAX;
	uint8_t last_class = 0;
	int status = 0;

	if (text->segment.count == 0)
		return 0;
	for (size_t i = 0; i < text->segment.count; ++i) {
		uint32_t scalar = text->segment.values[i];
		uint8_t combining = st_unicode_canonical_combining_class(scalar);
		uint32_t composition;

		if (starter != SIZE_MAX
		    && st_unicode_canonical_compose(normalized.values[starter],
		    scalar, &composition)
		    && (last_class == 0 || last_class < combining)) {
			normalized.values[starter] = composition;
			continue;
		}
		if (!scalar_append(&normalized, scalar)) {
			status = IO_ERROR_OUT_OF_MEMORY;
			goto finished;
		}
		if (combining == 0) {
			starter = normalized.count - 1;
			last_class = 0;
		}
		else
			last_class = combining;
	}
	if (normalized.count != text->original.count
	    || memcmp(normalized.values, text->original.values,
	    normalized.count * sizeof(normalized.values[0])) != 0)
		text->state |= ST_UNICODE_TEXT_CHANGED;
	for (size_t i = 0; i < normalized.count; ++i) {
		status = accept_normalized(text, normalized.values[i]);
		if (status != 0)
			goto finished;
	}
	text->segment.count = 0;
	text->original.count = 0;
finished:
	free(normalized.values);
	return status;
}

static bool
segment_can_compose(const struct scalar_buffer *segment, uint32_t scalar)
{
	uint32_t starter;
	uint32_t composition;

	if (segment->count == 0
	    || st_unicode_canonical_combining_class(segment->values[0]) != 0)
		return false;
	starter = segment->values[0];
	for (size_t i = 1; i < segment->count; ++i) {
		if (st_unicode_canonical_combining_class(segment->values[i]) != 0
		    || !st_unicode_canonical_compose(starter, segment->values[i],
		    &composition))
			return false;
		starter = composition;
	}
	return st_unicode_canonical_compose(starter, scalar, &composition);
}

static int
segment_decomposed(st_unicode_text_t *text, uint32_t scalar)
{
	uint8_t combining = st_unicode_canonical_combining_class(scalar);
	size_t insertion;

	if (combining == 0 && text->segment.count != 0
	    && !segment_can_compose(&text->segment, scalar)) {
		int status = flush_segment(text);

		if (status != 0)
			return status;
	}
	if (!scalar_append(&text->segment, scalar))
		return IO_ERROR_OUT_OF_MEMORY;
	if (combining == 0)
		return 0;
	insertion = text->segment.count - 1;
	while (insertion > 0) {
		uint8_t previous = st_unicode_canonical_combining_class(
		    text->segment.values[insertion - 1]);

		if (previous == 0 || previous <= combining)
			break;
		text->segment.values[insertion] = text->segment.values[insertion - 1];
		--insertion;
	}
	text->segment.values[insertion] = scalar;
	return 0;
}

static int
accept_scalar(st_unicode_text_t *text, uint32_t scalar)
{
	uint32_t local[4];
	uint32_t *decomposition = local;
	size_t count;
	int status;

	if (!st_unicode_scalar_valid(scalar))
		return IO_ERROR_INVALID_ARGUMENT;
	count = st_unicode_canonical_decompose(scalar, NULL, 0);
	if (count > sizeof(local) / sizeof(local[0])) {
		if (count > SIZE_MAX / sizeof(*decomposition))
			return IO_ERROR_OUT_OF_MEMORY;
		decomposition = malloc(count * sizeof(*decomposition));
		if (decomposition == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
	}
	(void)st_unicode_canonical_decompose(scalar, decomposition, count);
	if (count != 0
	    && st_unicode_canonical_combining_class(decomposition[0]) == 0
	    && text->segment.count != 0
	    && !segment_can_compose(&text->segment, decomposition[0])) {
		status = flush_segment(text);
		if (status != 0)
			goto finished;
	}
	if (!scalar_append(&text->original, scalar)) {
		status = IO_ERROR_OUT_OF_MEMORY;
		goto finished;
	}
	status = 0;
	for (size_t i = 0; i < count; ++i) {
		status = segment_decomposed(text, decomposition[i]);
		if (status != 0)
			break;
	}
finished:
	if (decomposition != local)
		free(decomposition);
	return status;
}

static int
flush_unicode(st_unicode_text_t *text)
{
	int status = flush_segment(text);

	if (status == 0 && text->mode == ST_UNICODE_TEXT_GRAPHEMES)
		status = emit_cluster(text);
	return status;
}

static int
emit_decode_error(st_unicode_text_t *text, const uint8_t *bytes, size_t size)
{
	int status = flush_unicode(text);

	if (status != 0)
		return status;
	status = queue_bytes(text, ST_UNICODE_TEXT_ITEM_DECODE_ERROR, bytes,
	    size, false, 0);
	if (status == 0)
		text->state |= ST_UNICODE_TEXT_SAW_DECODE_ERROR;
	return status;
}

static size_t
utf8_expected(uint8_t byte)
{
	if (byte >= 0xc2 && byte <= 0xdf)
		return 2;
	if (byte >= 0xe0 && byte <= 0xef)
		return 3;
	if (byte >= 0xf0 && byte <= 0xf4)
		return 4;
	return 0;
}

static bool
continuation_valid(const st_unicode_text_t *text, uint8_t byte)
{
	if (byte < 0x80 || byte > 0xbf)
		return false;
	if (text->utf8_count != 1)
		return true;
	if (text->utf8[0] == 0xe0)
		return byte >= 0xa0;
	if (text->utf8[0] == 0xed)
		return byte <= 0x9f;
	if (text->utf8[0] == 0xf0)
		return byte >= 0x90;
	if (text->utf8[0] == 0xf4)
		return byte <= 0x8f;
	return true;
}

int
st_unicode_text_create(st_unicode_text_t **result,
	enum st_unicode_text_mode mode)
{
	st_unicode_text_t *text;

	if (result == NULL || (mode != ST_UNICODE_TEXT_NORMALIZE
	    && mode != ST_UNICODE_TEXT_GRAPHEMES))
		return IO_ERROR_INVALID_ARGUMENT;
	text = calloc(1, sizeof(*text));
	if (text == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	text->mode = mode;
	*result = text;
	return 0;
}

void
st_unicode_text_destroy(st_unicode_text_t **text)
{
	if (text == NULL || *text == NULL)
		return;
	free((*text)->segment.values);
	free((*text)->original.values);
	free((*text)->cluster.values);
	free((*text)->output.values);
	free((*text)->items.values);
	free(*text);
	*text = NULL;
}

void
st_unicode_text_reset(st_unicode_text_t *text)
{
	if (text == NULL)
		return;
	text->state = 0;
	text->utf8_count = 0;
	text->utf8_expected = 0;
	text->segment.count = 0;
	text->original.count = 0;
	text->cluster.count = 0;
	text->output.count = 0;
	text->items.count = 0;
	text->item_head = 0;
}

int
st_unicode_text_feed_byte(st_unicode_text_t *text, uint8_t byte)
{
	uint32_t scalar;
	size_t consumed;
	int status;

	if (text == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if ((text->state & ST_UNICODE_TEXT_FINISHED) != 0)
		return IO_ERROR_CONFLICT;
again:
	if (text->utf8_count == 0) {
		if (byte < 0x80)
			return accept_scalar(text, byte);
		text->utf8_expected = utf8_expected(byte);
		if (text->utf8_expected == 0)
			return emit_decode_error(text, &byte, 1);
		text->utf8[0] = byte;
		text->utf8_count = 1;
		return 0;
	}
	if (!continuation_valid(text, byte)) {
		status = emit_decode_error(text, text->utf8, text->utf8_count);
		if (status != 0)
			return status;
		text->utf8_count = 0;
		text->utf8_expected = 0;
		goto again;
	}
	text->utf8[text->utf8_count++] = byte;
	if (text->utf8_count != text->utf8_expected)
		return 0;
	status = st_utf8_decode_one(&scalar, &consumed, text->utf8,
	    text->utf8_count);
	if (status == 0)
		status = accept_scalar(text, scalar);
	else
		status = emit_decode_error(text, text->utf8, text->utf8_count);
	text->utf8_count = 0;
	text->utf8_expected = 0;
	return status;
}

int
st_unicode_text_feed(st_unicode_text_t *text, const void *bytes,
	size_t size, size_t *consumed)
{
	const uint8_t *input = bytes;
	size_t offset = 0;
	int status = 0;

	if (text == NULL || consumed == NULL || (bytes == NULL && size != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	if ((text->state & ST_UNICODE_TEXT_FINISHED) != 0)
		return IO_ERROR_CONFLICT;
	while (offset < size) {
		status = st_unicode_text_feed_byte(text, input[offset]);
		if (status != 0)
			break;
		++offset;
	}
	*consumed = offset;
	return status;
}

int
st_unicode_text_feed_scalar(st_unicode_text_t *text, uint32_t scalar)
{
	if (text == NULL || !st_unicode_scalar_valid(scalar))
		return IO_ERROR_INVALID_ARGUMENT;
	if ((text->state & ST_UNICODE_TEXT_FINISHED) != 0)
		return IO_ERROR_CONFLICT;
	if (text->utf8_count != 0)
		return IO_ERROR_CONFLICT;
	return accept_scalar(text, scalar);
}

int
st_unicode_text_feed_scalars(st_unicode_text_t *text,
	const uint32_t *scalars, size_t count, size_t *consumed)
{
	size_t offset = 0;
	int status = 0;

	if (text == NULL || consumed == NULL || (scalars == NULL && count != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	while (offset < count) {
		status = st_unicode_text_feed_scalar(text, scalars[offset]);
		if (status != 0)
			break;
		++offset;
	}
	*consumed = offset;
	return status;
}

int
st_unicode_text_finish(st_unicode_text_t *text)
{
	int status;

	if (text == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if ((text->state & ST_UNICODE_TEXT_FINISHED) != 0)
		return 0;
	if (text->utf8_count != 0) {
		status = emit_decode_error(text, text->utf8, text->utf8_count);
		if (status != 0)
			return status;
		text->utf8_count = 0;
		text->utf8_expected = 0;
	}
	status = flush_unicode(text);
	if (status == 0)
		text->state |= ST_UNICODE_TEXT_FINISHED;
	return status;
}

uint32_t
st_unicode_text_state(const st_unicode_text_t *text)
{
	uint32_t state;

	if (text == NULL)
		return 0;
	state = text->state;
	if (text->utf8_count != 0 || text->segment.count != 0
	    || text->cluster.count != 0)
		state |= ST_UNICODE_TEXT_INCOMPLETE;
	else
		state &= ~ST_UNICODE_TEXT_INCOMPLETE;
	if (text->item_head < text->items.count)
		state |= ST_UNICODE_TEXT_READY;
	else
		state &= ~ST_UNICODE_TEXT_READY;
	return state;
}

int
st_unicode_text_peek(const st_unicode_text_t *text,
	struct st_unicode_text_item *item)
{
	const struct queued_item *queued;

	if (text == NULL || item == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (text->item_head >= text->items.count)
		return IO_ERROR_NOT_FOUND;
	queued = &text->items.values[text->item_head];
	*item = (struct st_unicode_text_item){
		.kind = queued->kind,
		.bytes = text->output.values + queued->offset,
		.size = queued->size,
		.rgi_emoji = queued->rgi_emoji,
		.columns = queued->columns,
	};
	return 0;
}

int
st_unicode_text_consume(st_unicode_text_t *text)
{
	if (text == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (text->item_head >= text->items.count)
		return IO_ERROR_NOT_FOUND;
	++text->item_head;
	if (text->item_head == text->items.count) {
		text->item_head = 0;
		text->items.count = 0;
		text->output.count = 0;
		text->state &= ~ST_UNICODE_TEXT_READY;
	}
	return 0;
}

int
st_unicode_text_normalize(char **result, size_t *result_size,
	const void *bytes, size_t size, bool *changed)
{
	st_unicode_text_t *text = NULL;
	struct st_unicode_text_item item;
	char *normalized = NULL;
	size_t consumed;
	int status;

	if (result == NULL || result_size == NULL || changed == NULL
	    || (bytes == NULL && size != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_unicode_text_create(&text, ST_UNICODE_TEXT_NORMALIZE);
	if (status == 0)
		status = st_unicode_text_feed(text, bytes, size, &consumed);
	if (status == 0 && consumed != size)
		status = IO_ERROR_INVALID_ENCODING;
	if (status == 0)
		status = st_unicode_text_finish(text);
	if (status == 0
	    && (st_unicode_text_state(text) & ST_UNICODE_TEXT_SAW_DECODE_ERROR) != 0)
		status = IO_ERROR_INVALID_ENCODING;
	if (status == 0 && text->output.count == SIZE_MAX)
		status = IO_ERROR_OUT_OF_MEMORY;
	if (status == 0) {
		normalized = malloc(text->output.count + 1);
		if (normalized == NULL)
			status = IO_ERROR_OUT_OF_MEMORY;
	}
	if (status == 0) {
		size_t offset = 0;

		while (st_unicode_text_peek(text, &item) == 0) {
			memcpy(normalized + offset, item.bytes, item.size);
			offset += item.size;
			(void)st_unicode_text_consume(text);
		}
		normalized[offset] = '\0';
		*result = normalized;
		*result_size = offset;
		*changed = (text->state & ST_UNICODE_TEXT_CHANGED) != 0;
	}
	st_unicode_text_destroy(&text);
	return status;
}
