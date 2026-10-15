/* Immediate provisional presentation of an incremental UTF-8 stream. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_unicode_text.h"

#include "io_error.h"
#include "st_unicode_grapheme.h"
#include "st_unicode_width.h"
#include "st_utf8.h"

#include <stdlib.h>
#include <string.h>

struct st_unicode_presentation {
	struct st_utf8_decoder decoder;
	uint32_t *cluster;
	size_t count;
	size_t capacity;
	uint8_t *normalized;
	size_t normalized_size;
	st_unicode_presentation_callback_t callback;
	void *context;
	st_unicode_presentation_scalar_filter_t scalar_filter;
	void *scalar_context;
};

static bool
append_scalar(st_unicode_presentation_t *presentation, uint32_t scalar)
{
	uint32_t *fresh;
	size_t capacity;

	if (presentation->count < presentation->capacity) {
		presentation->cluster[presentation->count++] = scalar;
		return true;
	}
	capacity = presentation->capacity == 0 ? 8 : presentation->capacity * 2;
	if (capacity < presentation->capacity
	    || capacity > SIZE_MAX / sizeof(*fresh))
		return false;
	fresh = realloc(presentation->cluster, capacity * sizeof(*fresh));
	if (fresh == NULL)
		return false;
	presentation->cluster = fresh;
	presentation->capacity = capacity;
	presentation->cluster[presentation->count++] = scalar;
	return true;
}

static bool
control_property(enum st_unicode_grapheme_break value)
{
	return value == ST_UNICODE_GRAPHEME_CR
	    || value == ST_UNICODE_GRAPHEME_LF
	    || value == ST_UNICODE_GRAPHEME_CONTROL;
}

static bool
indic_no_break(const uint32_t *cluster, size_t count, uint32_t next)
{
	if (st_unicode_indic_conjunct_break(next) != ST_UNICODE_INCB_CONSONANT)
		return false;
	for (size_t i = count; i > 0; --i) {
		enum st_unicode_indic_conjunct_break property
		    = st_unicode_indic_conjunct_break(cluster[i - 1]);

		if (property == ST_UNICODE_INCB_LINKER)
			return true;
		if (property != ST_UNICODE_INCB_EXTEND)
			return false;
	}
	return false;
}

static bool
emoji_zwj_no_break(const uint32_t *cluster, size_t count, uint32_t next)
{
	size_t index;

	if (!st_unicode_extended_pictographic(next) || count == 0
	    || st_unicode_grapheme_break(cluster[count - 1])
	    != ST_UNICODE_GRAPHEME_ZWJ)
		return false;
	index = count - 1;
	while (index > 0 && st_unicode_grapheme_break(cluster[index - 1])
	    == ST_UNICODE_GRAPHEME_EXTEND)
		--index;
	return index > 0 && st_unicode_extended_pictographic(cluster[index - 1]);
}

static bool
break_before(const uint32_t *cluster, size_t count, uint32_t next)
{
	enum st_unicode_grapheme_break left;
	enum st_unicode_grapheme_break right = st_unicode_grapheme_break(next);
	size_t regional = 0;

	if (count == 0)
		return false;
	left = st_unicode_grapheme_break(cluster[count - 1]);
	if (left == ST_UNICODE_GRAPHEME_CR && right == ST_UNICODE_GRAPHEME_LF)
		return false;
	if (control_property(left) || control_property(right))
		return true;
	if (left == ST_UNICODE_GRAPHEME_L
	    && (right == ST_UNICODE_GRAPHEME_L
	    || right == ST_UNICODE_GRAPHEME_V
	    || right == ST_UNICODE_GRAPHEME_LV
	    || right == ST_UNICODE_GRAPHEME_LVT))
		return false;
	if ((left == ST_UNICODE_GRAPHEME_LV || left == ST_UNICODE_GRAPHEME_V)
	    && (right == ST_UNICODE_GRAPHEME_V
	    || right == ST_UNICODE_GRAPHEME_T))
		return false;
	if ((left == ST_UNICODE_GRAPHEME_LVT || left == ST_UNICODE_GRAPHEME_T)
	    && right == ST_UNICODE_GRAPHEME_T)
		return false;
	if (right == ST_UNICODE_GRAPHEME_EXTEND
	    || right == ST_UNICODE_GRAPHEME_ZWJ
	    || right == ST_UNICODE_GRAPHEME_SPACING_MARK
	    || left == ST_UNICODE_GRAPHEME_PREPEND)
		return false;
	if (indic_no_break(cluster, count, next)
	    || emoji_zwj_no_break(cluster, count, next))
		return false;
	if (right == ST_UNICODE_GRAPHEME_REGIONAL_INDICATOR) {
		for (size_t i = count; i > 0; --i) {
			if (st_unicode_grapheme_break(cluster[i - 1])
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
publish(st_unicode_presentation_t *presentation,
	enum st_unicode_presentation_event_kind kind)
{
	struct st_unicode_presentation_event event = {.kind = kind};
	char *source;
	size_t source_size = 0;
	size_t offset = 0;
	bool changed;
	uint8_t columns = 1;
	bool rgi;
	int status;

	if (presentation->count == 0)
		return 0;
	if (presentation->count > SIZE_MAX / 4)
		return IO_ERROR_OUT_OF_MEMORY;
	source = malloc(presentation->count * 4);
	if (source == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	for (size_t i = 0; i < presentation->count; ++i) {
		ptrdiff_t encoded = st_utf8_encode_one(source + source_size,
		    presentation->count * 4 - source_size,
		    presentation->cluster[i]);
		if (encoded < 0) {
			free(source);
			return IO_ERROR_INVALID_ENCODING;
		}
		source_size += (size_t)encoded;
	}
	free(presentation->normalized);
	presentation->normalized = NULL;
	status = st_unicode_text_normalize((char **)&presentation->normalized,
	    &presentation->normalized_size, source, source_size, &changed);
	free(source);
	if (status != 0)
		return status;
	rgi = st_unicode_rgi_emoji_prefix(presentation->cluster,
	    presentation->count) == presentation->count;
	if (rgi)
		columns = 2;
	else {
		while (offset < presentation->normalized_size) {
			uint32_t scalar;
			size_t consumed;
			enum st_unicode_grapheme_break property;

			if (st_utf8_decode_one(&scalar, &consumed,
			    presentation->normalized + offset,
			    presentation->normalized_size - offset) != 0)
				break;
			offset += consumed;
			property = st_unicode_grapheme_break(scalar);
			if (property != ST_UNICODE_GRAPHEME_EXTEND
			    && property != ST_UNICODE_GRAPHEME_ZWJ) {
				columns = st_unicode_columns(scalar);
				if (columns == 0)
					columns = 1;
				break;
			}
		}
	}
	event.bytes = presentation->normalized;
	event.size = presentation->normalized_size;
	event.rgi_emoji = rgi;
	event.columns = columns;
	return presentation->callback(presentation->context, &event);
}

static int
commit(st_unicode_presentation_t *presentation)
{
	int status;

	if (presentation->count == 0)
		return 0;
	status = publish(presentation, ST_UNICODE_PRESENTATION_COMMIT);
	if (status == 0)
		presentation->count = 0;
	return status;
}

static int
accept(st_unicode_presentation_t *presentation, uint32_t scalar)
{
	bool revise = presentation->count != 0;
	int status;

	if (break_before(presentation->cluster, presentation->count, scalar)) {
		status = commit(presentation);
		if (status != 0)
			return status;
		revise = false;
	}
	if (!append_scalar(presentation, scalar))
		return IO_ERROR_OUT_OF_MEMORY;
	return publish(presentation, revise ? ST_UNICODE_PRESENTATION_REVISE
	    : ST_UNICODE_PRESENTATION_BEGIN);
}

static int
decode_error(st_unicode_presentation_t *presentation,
	const struct st_utf8_decode_event *decode)
{
	struct st_unicode_presentation_event event = {
		.kind = ST_UNICODE_PRESENTATION_DECODE_ERROR,
		.bytes = decode->data.malformed.bytes,
		.size = decode->data.malformed.size,
		.columns = 1,
	};
	int status = commit(presentation);

	return status == 0 ? presentation->callback(presentation->context, &event)
	    : status;
}

int
st_unicode_presentation_create(st_unicode_presentation_t **result,
	st_unicode_presentation_callback_t callback, void *context)
{
	st_unicode_presentation_t *presentation;

	if (result == NULL || callback == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	presentation = calloc(1, sizeof(*presentation));
	if (presentation == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	presentation->callback = callback;
	presentation->context = context;
	*result = presentation;
	return 0;
}

void
st_unicode_presentation_set_scalar_filter(
	st_unicode_presentation_t *presentation,
	st_unicode_presentation_scalar_filter_t filter, void *context)
{
	if (presentation == NULL)
		return;
	presentation->scalar_filter = filter;
	presentation->scalar_context = context;
}

void
st_unicode_presentation_destroy(st_unicode_presentation_t **presentation)
{
	if (presentation == NULL || *presentation == NULL)
		return;
	free((*presentation)->cluster);
	free((*presentation)->normalized);
	free(*presentation);
	*presentation = NULL;
}

void
st_unicode_presentation_reset(st_unicode_presentation_t *presentation)
{
	if (presentation == NULL)
		return;
	st_utf8_decoder_reset(&presentation->decoder);
	presentation->count = 0;
}

int
st_unicode_presentation_feed(st_unicode_presentation_t *presentation,
	const void *bytes, size_t size, size_t *consumed)
{
	const uint8_t *input = bytes;
	size_t offset = 0;
	int status = 0;

	if (presentation == NULL || consumed == NULL
	    || (bytes == NULL && size != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	while (offset < size) {
		struct st_utf8_decode_event event;
		size_t used;

		status = st_utf8_decoder_feed(&presentation->decoder,
		    input + offset, size - offset, &used, &event);
		offset += used;
		if (status != 0)
			break;
		if (event.kind == ST_UTF8_DECODE_EVENT_SCALAR) {
			bool filtered = false;

			if (presentation->scalar_filter != NULL)
				status = presentation->scalar_filter(
				    presentation->scalar_context, event.data.scalar,
				    &filtered);
			if (status == 0 && !filtered)
				status = accept(presentation, event.data.scalar);
		}
		else if (event.kind == ST_UTF8_DECODE_EVENT_MALFORMED)
			status = decode_error(presentation, &event);
		if (status != 0)
			break;
	}
	*consumed = offset;
	return status;
}

int
st_unicode_presentation_boundary(st_unicode_presentation_t *presentation)
{
	struct st_utf8_decode_event event;
	int status;

	if (presentation == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_utf8_decoder_flush(&presentation->decoder, &event);
	if (status != 0)
		return status;
	if (event.kind == ST_UTF8_DECODE_EVENT_MALFORMED) {
		status = decode_error(presentation, &event);
		if (status != 0)
			return status;
	}
	return commit(presentation);
}
