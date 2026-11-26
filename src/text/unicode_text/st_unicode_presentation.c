/* Immediate provisional presentation of an incremental UTF-8 stream. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdlib.h>

#include "cluster_columns.h"
#include "io_error.h"
#include "st_unicode_grapheme.h"
#include "st_unicode_width.h"
#include "st_utf8.h"
#include "unicode_text_internal.h"

struct st_unicode_presentation {
	struct st_utf8_decoder                  decoder;
	uint32_t                               *cluster;
	size_t                                  count;
	size_t                                  capacity;
	st_unicode_text_t                      *normalizer;
	struct st_unicode_presentation_event    event;
	st_unicode_presentation_callback_t      callback;
	void                                   *context;
	st_unicode_presentation_scalar_filter_t scalar_filter;
	void                                   *scalar_context;
};

static bool
append_scalar(st_unicode_presentation_t *presentation, uint32_t scalar)
{
	uint32_t *fresh;
	size_t    capacity;

	if (presentation->count < presentation->capacity) {
		presentation->cluster[presentation->count++] = scalar;
		return true;
	}
	capacity = presentation->capacity == 0 ? 8 : presentation->capacity * 2;
	if (capacity < presentation->capacity || capacity > SIZE_MAX / sizeof(*fresh))
		return false;
	fresh = realloc(presentation->cluster, capacity * sizeof(*fresh));
	if (fresh == NULL)
		return false;
	presentation->cluster                        = fresh;
	presentation->capacity                       = capacity;
	presentation->cluster[presentation->count++] = scalar;
	return true;
}

static bool
control_property(enum st_unicode_grapheme_break value)
{
	return value == ST_UNICODE_GRAPHEME_CR || value == ST_UNICODE_GRAPHEME_LF
	       || value == ST_UNICODE_GRAPHEME_CONTROL;
}

static bool
indic_no_break(const uint32_t *cluster, size_t count, uint32_t next)
{
	if (st_unicode_indic_conjunct_break(next) != ST_UNICODE_INCB_CONSONANT)
		return false;
	for (size_t i = count; i > 0; --i) {
		enum st_unicode_indic_conjunct_break property = st_unicode_indic_conjunct_break(cluster[i - 1]);

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
	    || st_unicode_grapheme_break(cluster[count - 1]) != ST_UNICODE_GRAPHEME_ZWJ)
		return false;
	index = count - 1;
	while (index > 0 && st_unicode_grapheme_break(cluster[index - 1]) == ST_UNICODE_GRAPHEME_EXTEND)
		--index;
	return index > 0 && st_unicode_extended_pictographic(cluster[index - 1]);
}

static bool
break_before(const uint32_t *cluster, size_t count, uint32_t next)
{
	enum st_unicode_grapheme_break left;
	enum st_unicode_grapheme_break right    = st_unicode_grapheme_break(next);
	size_t                         regional = 0;

	if (count == 0)
		return false;
	left = st_unicode_grapheme_break(cluster[count - 1]);
	if (left == ST_UNICODE_GRAPHEME_CR && right == ST_UNICODE_GRAPHEME_LF)
		return false;
	if (control_property(left) || control_property(right))
		return true;
	if (left == ST_UNICODE_GRAPHEME_L
	    && (right == ST_UNICODE_GRAPHEME_L || right == ST_UNICODE_GRAPHEME_V || right == ST_UNICODE_GRAPHEME_LV
	        || right == ST_UNICODE_GRAPHEME_LVT))
		return false;
	if ((left == ST_UNICODE_GRAPHEME_LV || left == ST_UNICODE_GRAPHEME_V)
	    && (right == ST_UNICODE_GRAPHEME_V || right == ST_UNICODE_GRAPHEME_T))
		return false;
	if ((left == ST_UNICODE_GRAPHEME_LVT || left == ST_UNICODE_GRAPHEME_T) && right == ST_UNICODE_GRAPHEME_T)
		return false;
	if (right == ST_UNICODE_GRAPHEME_EXTEND || right == ST_UNICODE_GRAPHEME_ZWJ
	    || right == ST_UNICODE_GRAPHEME_SPACING_MARK || left == ST_UNICODE_GRAPHEME_PREPEND)
		return false;
	if (indic_no_break(cluster, count, next) || emoji_zwj_no_break(cluster, count, next))
		return false;
	if (right == ST_UNICODE_GRAPHEME_REGIONAL_INDICATOR) {
		for (size_t i = count; i > 0; --i) {
			if (st_unicode_grapheme_break(cluster[i - 1]) != ST_UNICODE_GRAPHEME_REGIONAL_INDICATOR)
				break;
			++regional;
		}
		if ((regional & 1U) != 0)
			return false;
	}
	return true;
}

static int
publish(st_unicode_presentation_t *presentation, enum st_unicode_presentation_event_kind kind)
{
	if (presentation->event.bytes == NULL) {
		st_unicode_text_t          *normalizer = presentation->normalizer;
		struct st_unicode_text_item item;
		size_t                      consumed;
		st_unicode_text_rewind(normalizer);
		int status = st_unicode_text_feed_scalars(normalizer, presentation->cluster, presentation->count,
		    &consumed);
		if (status == 0)
			status = st_unicode_text_finish(normalizer);
		if (status == 0)
			status = st_unicode_text_peek(normalizer, &item);
		if (status != 0)
			return status;

		bool rgi =
		    st_unicode_rgi_emoji_prefix(presentation->cluster, presentation->count) == presentation->count;
		presentation->event = (struct st_unicode_presentation_event) {
		    .bytes     = item.bytes,
		    .size      = item.size,
		    .rgi_emoji = rgi,
		    .columns   = rgi ? 2 : cluster_columns(item.bytes, item.size),
		};
	}
	/* COMMIT can borrow the same NFC output as the last BEGIN or REVISE. */
	struct st_unicode_presentation_event event = presentation->event;
	event.kind                                 = kind;
	return presentation->callback(presentation->context, &event);
}

static void
discard_cluster(st_unicode_presentation_t *presentation)
{
	presentation->count       = 0;
	presentation->event.bytes = NULL;
	presentation->cluster =
	    trim_empty_buffer(presentation->cluster, &presentation->capacity, sizeof(*presentation->cluster));
	st_unicode_text_reset(presentation->normalizer);
}

static int
commit(st_unicode_presentation_t *presentation)
{
	int status;

	if (presentation->count == 0)
		return 0;
	status = publish(presentation, ST_UNICODE_PRESENTATION_COMMIT);
	if (status == 0)
		discard_cluster(presentation);
	return status;
}

static int
accept(st_unicode_presentation_t *presentation, uint32_t scalar)
{
	bool revise = presentation->count != 0;
	int  status;

	if (break_before(presentation->cluster, presentation->count, scalar)) {
		status = commit(presentation);
		if (status != 0)
			return status;
		revise = false;
	}
	if (!append_scalar(presentation, scalar))
		return IO_ERROR_OUT_OF_MEMORY;
	presentation->event.bytes = NULL;
	return publish(presentation, revise ? ST_UNICODE_PRESENTATION_REVISE : ST_UNICODE_PRESENTATION_BEGIN);
}

static int
decode_error(st_unicode_presentation_t *presentation, const struct st_utf8_decode_event *decode)
{
	struct st_unicode_presentation_event event = {
	    .kind    = ST_UNICODE_PRESENTATION_DECODE_ERROR,
	    .bytes   = decode->data.malformed.bytes,
	    .size    = decode->data.malformed.size,
	    .columns = 1,
	};
	int status = commit(presentation);

	return status == 0 ? presentation->callback(presentation->context, &event) : status;
}

int
st_unicode_presentation_create(st_unicode_presentation_t **result, st_unicode_presentation_callback_t callback,
    void *context)
{
	st_unicode_presentation_t *presentation;

	if (result == NULL || callback == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	presentation = calloc(1, sizeof(*presentation));
	if (presentation == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	int status = st_unicode_text_create(&presentation->normalizer, ST_UNICODE_TEXT_NORMALIZE);
	if (status != 0) {
		free(presentation);
		return status;
	}
	presentation->callback = callback;
	presentation->context  = context;
	*result                = presentation;
	return 0;
}

void
st_unicode_presentation_set_scalar_filter(st_unicode_presentation_t *presentation,
    st_unicode_presentation_scalar_filter_t filter, void *context)
{
	if (presentation == NULL)
		return;
	presentation->scalar_filter  = filter;
	presentation->scalar_context = context;
}

void
st_unicode_presentation_destroy(st_unicode_presentation_t **presentation)
{
	if (presentation == NULL || *presentation == NULL)
		return;
	free((*presentation)->cluster);
	st_unicode_text_destroy(&(*presentation)->normalizer);
	free(*presentation);
	*presentation = NULL;
}

void
st_unicode_presentation_reset(st_unicode_presentation_t *presentation)
{
	if (presentation == NULL)
		return;
	st_utf8_decoder_reset(&presentation->decoder);
	discard_cluster(presentation);
}

static int
deliver(st_unicode_presentation_t *presentation, const struct st_utf8_decode_event *event)
{
	if (presentation == NULL || event == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (event->kind == ST_UTF8_DECODE_EVENT_MALFORMED)
		return decode_error(presentation, event);
	if (event->kind != ST_UTF8_DECODE_EVENT_SCALAR)
		return 0;
	bool filtered = false;
	int  status   = 0;
	if (presentation->scalar_filter != NULL)
		status = presentation->scalar_filter(presentation->scalar_context, event->data.scalar, &filtered);
	return status != 0 || filtered ? status : accept(presentation, event->data.scalar);
}

int
st_unicode_presentation_decoded(st_unicode_presentation_t *presentation, const struct st_utf8_decode_event *event)
{
	if (presentation == NULL || event == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	/* A local byte writer may have left a partial scalar before this source
	 * resumed. Preserve its error bytes without breaking a complete cluster. */
	if (st_utf8_decoder_incomplete(&presentation->decoder)) {
		struct st_utf8_decode_event partial;
		st_utf8_decoder_flush(&presentation->decoder, &partial);
		int status = deliver(presentation, &partial);
		if (status != 0)
			return status;
	}
	return deliver(presentation, event);
}

int
st_unicode_presentation_feed(st_unicode_presentation_t *presentation, const void *bytes, size_t size,
    size_t *consumed)
{
	const uint8_t *input  = bytes;
	size_t         offset = 0;
	int            status = 0;

	if (presentation == NULL || consumed == NULL || (bytes == NULL && size != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	while (offset < size) {
		struct st_utf8_decode_event event;
		size_t                      used;

		status =
		    st_utf8_decoder_feed(&presentation->decoder, input + offset, size - offset, &used, &event);
		offset += used;
		if (status != 0)
			break;
		status = deliver(presentation, &event);
		if (status != 0)
			break;
	}
	*consumed = offset;
	return status;
}

int
st_unicode_presentation_commit(st_unicode_presentation_t *presentation)
{
	return presentation == NULL ? IO_ERROR_INVALID_ARGUMENT : commit(presentation);
}

int
st_unicode_presentation_boundary(st_unicode_presentation_t *presentation)
{
	struct st_utf8_decode_event event;
	int                         status;

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
