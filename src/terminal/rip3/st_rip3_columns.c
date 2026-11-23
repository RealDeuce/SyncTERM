/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_columns.h"

#include "st_rip3_classic_text.h"
#include "st_rip3_command_internal.h"
#include "st_rip3_environments.h"
#include "st_rip3_graphical_bytes.h"
#include "st_rip3_outline_cache.h"
#include "st_rip3_outline_font.h"
#include "st_rip3_outline_layout.h"
#include "st_rip3_ports.h"
#include "st_rip3_styles.h"
#include "st_rip3_text_commands.h"
#include "st_codepage.h"
#include "st_unicode_compare.h"
#include "st_utf8.h"
#include "io_error.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

int
st_rip3_columns_decode(struct st_rip3_column_definition *definition, const struct st_rip3_command_host *host,
    const uint8_t *command, size_t length, size_t *tail)
{
	struct st_rip3_environment env;
	unsigned current;
	int status = st_rip3_command_environment(host, &current, &env);
	if (status != 0)
		return status;
	size_t width = env.coordinate_size;
	bool extended = command[1] == 'e';
	size_t fixed = 4 + 4 * width;
	if (extended ? length < fixed + 14 : length != fixed)
		return IO_ERROR_INVALID_FORMAT;
	uint64_t field[6];
	for (size_t i = 0; i < 6; ++i) {
		size_t offset = i < 4 ? i * width : 4 * width + i - 4;
		status = st_rip3_command_field(&field[i], command + 2 + offset, i < 4 ? width : 1, env.base_math);
		if (status != 0)
			return status;
	}
	if (field[4] >= ST_RIP3_COLUMN_COUNT || field[5] >= ST_RIP3_COLUMN_COUNT)
		return IO_ERROR_OUT_OF_RANGE;
	uint64_t flags = 0, color = 0;
	if (extended) {
		status = st_rip3_command_field(&flags, command + fixed, 4, env.base_math);
		if (status != 0)
			return status;
		if (flags > 3 || field[5] == 0)
			return IO_ERROR_OUT_OF_RANGE;
		uint64_t reserved;
		status = st_rip3_command_field(&color, command + fixed + 4, 2, env.base_math);
		if (status == 0)
			status = st_rip3_command_field(&reserved, command + fixed + 6, 4, env.base_math);
		if (status == 0)
			status = st_rip3_command_field(&reserved, command + fixed + 10, 4, env.base_math);
		if (status != 0)
			return status;
		if ((flags & 2) != 0 && color > UINT8_MAX)
			return IO_ERROR_OUT_OF_RANGE;
		fixed += 14;
	}
	struct st_rip3_frame frame;
	struct st_rip3_point first, last;
	status = st_rip3_command_frame(host, current, &frame);
	if (status == 0)
		status = st_rip3_world_to_device(&frame,
		    &(struct st_rip3_point){(int32_t)field[0], (int32_t)field[1]}, &first);
	if (status == 0)
		status = st_rip3_world_to_device(&frame,
		    &(struct st_rip3_point){(int32_t)field[2], (int32_t)field[3]}, &last);
	if (status != 0)
		return status;
	struct st_rip3_port_info port;
	status = st_rip3_ports_current(host->ports, &current);
	if (status == 0)
		status = st_rip3_ports_info(host->ports, current, &port);
	if (status != 0)
		return status;
	*definition = (struct st_rip3_column_definition){
		.area = {first.x < last.x ? first.x : last.x, first.y < last.y ? first.y : last.y,
		    first.x > last.x ? first.x : last.x, first.y > last.y ? first.y : last.y},
		.slot = (unsigned)field[4], .article = (unsigned)field[5], .flags = (unsigned)flags,
		.color = (uint8_t)color, .extended = extended, .suppressed = port.protected,
	};
	*tail = fixed;
	return 0;
}

struct column_line {
	const struct st_rip3_command_host *host;
	struct st_rip3_style style;
	struct st_rip3_classic_font classic;
	struct st_rip3_outline_font outline;
	struct st_rip3_outline_layout layout;
	const uint8_t *glyphs;
	size_t length;
	unsigned rgb_bits;
	uint8_t foreground, background;
	const struct st_rip3_column_search *search;
	int64_t height;
	int32_t x, y;
	double width;
	bool justify;
};

struct article_buffer {
	uint8_t *bytes;
	size_t length, capacity;
};

struct st_rip3_article_flow {
	struct column_line pen;
	struct article_buffer word, line, pending;
	size_t word_start;
	double width;
	bool prepared, newline, previous_cr, dropping_word, retention_failed;
};

static int
prepare_line(struct column_line *line)
{
	const struct st_rip3_command_host *host = line->host;
	struct st_rip3_environment env;
	struct st_rip3_frame frame;
	unsigned current;
	int status = st_rip3_styles_current(host->styles, &current);
	if (status == 0)
		status = st_rip3_styles_get(host->styles, current, &line->style);
	if (status == 0)
		status = st_rip3_command_environment(host, &current, &env);
	if (status == 0)
		status = st_rip3_command_frame(host, current, &frame);
	if (status != 0)
		return status;
	line->rgb_bits = env.rgb_bits;
	if (line->style.font_mode == ST_RIP3_FONT_OUTLINE) {
		line->style.outline.string_rotation = 0;
		status = st_rip3_outline_layout_prepare(&line->layout, &line->style.outline, &frame);
		if (status == 0)
			status = st_rip3_command_outline_font(&line->outline, host->text, &line->style.outline,
			    &frame, line->layout.font_orientation);
		if (status != 0)
			return status;
		line->layout.justification = 0;
		line->height = line->outline.bounds.ymax - line->outline.bounds.ymin;
		if (line->height < 1)
			line->height = 1;
		return st_rip3_outline_cache_prepare(host->text->outline_cache, &line->outline, line->layout.spacing);
	}
	line->style.font.direction = 0;
	line->style.font.flags &= 32;
	if (host->text->font != NULL && host->text->font->number == line->style.font.font)
		line->classic = *host->text->font;
	else
		status = st_rip3_classic_font_builtin_device(&line->classic, frame.device_width, frame.device_height,
		    line->style.font.font);
	struct st_rip3_classic_text_plan plan;
	if (status == 0)
		status = st_rip3_classic_text_plan(&line->classic, &line->style.font, line->glyphs, line->length,
		    &(struct st_rip3_text_position){0, 0}, &plan);
	if (status == 0)
		line->height = (int64_t)ceil(plan.ascent + plan.descent) + 1;
	if (status == 0 && line->style.write_mode != ST_RIP3_NOT) {
		status = st_rip3_command_ink(host, env.rgb_bits, line->style.drawing_color, &line->foreground);
		if (status == 0 && (line->style.font.flags & 32))
			status = st_rip3_command_ink(host, env.rgb_bits, line->style.background_color, &line->background);
	}
	return status;
}

static int
measure_span(const struct column_line *line, const uint8_t *bytes, size_t length, double *width)
{
	if (line->style.font_mode == ST_RIP3_FONT_OUTLINE) {
		struct st_rip3_outline_width measure;
		int status = st_rip3_outline_cache_measure(&measure, line->host->text->outline_cache,
		    bytes, length);
		if (status == 0)
			*width = (double)measure.advance;
		return status;
	}
	struct st_rip3_classic_text_plan plan;
	int status = st_rip3_classic_text_plan(&line->classic, &line->style.font, bytes, length,
	    &(struct st_rip3_text_position){0, 0}, &plan);
	if (status == 0)
		*width = plan.advance;
	return status;
}

static int
draw_run(const struct column_line *line, size_t start, size_t end, struct st_rip3_text_position at,
    const uint8_t *foreground)
{
	if (start == end)
		return 0;
	if (line->style.font_mode == ST_RIP3_FONT_OUTLINE) {
		at.y -= (double)line->outline.bounds.ymin + line->layout.baseline.y;
		at.x -= line->layout.baseline.x;
		return st_rip3_command_draw_outline(line->host, &line->style, &line->layout, &line->outline,
		    line->rgb_bits, &at, line->glyphs + start, end - start, foreground);
	}
	return st_rip3_draw_classic_text(line->host->ports, &line->classic, &line->style.font,
	    line->glyphs + start, end - start, &at, foreground != NULL ? *foreground : line->foreground,
	    line->background, line->style.write_mode);
}

static bool
space(uint8_t byte)
{
	return byte == ' ' || (byte >= '\t' && byte <= '\r');
}

static bool
next_word(const struct column_line *line, size_t *start, size_t *end)
{
	*start = *end;
	while (*start < line->length && space(line->glyphs[*start]))
		++*start;
	*end = *start;
	while (*end < line->length && !space(line->glyphs[*end]))
		++*end;
	return *end != *start;
}

static size_t
search_utf8(char output[4 * ST_RIP3_COLUMN_SEARCH_LENGTH], const uint8_t *text, size_t length)
{
	size_t used = 0;
	for (size_t i = 0; i < length; ++i) {
		uint32_t cp = st_codepage_to_unicode(ST_CODEPAGE_CP437, text[i], false);
		ptrdiff_t size = st_utf8_encode_one(output + used, 4 * ST_RIP3_COLUMN_SEARCH_LENGTH - used, cp);
		if (size < 0)
			return 0;
		used += (size_t)size;
	}
	return used;
}

static int
search_color(const struct st_rip3_column_search *search, const uint8_t *word, size_t length,
    const uint8_t **ink)
{
	*ink = NULL;
	if (search->count == 0 || length > ST_RIP3_COLUMN_SEARCH_LENGTH)
		return 0;
	char left[4 * ST_RIP3_COLUMN_SEARCH_LENGTH], right[4 * ST_RIP3_COLUMN_SEARCH_LENGTH];
	size_t size = search_utf8(left, word, length);
	for (size_t i = 0; i < search->count; ++i) {
		bool equal;
		size_t other = search_utf8(right, search->words[i], search->lengths[i]);
		int status = st_unicode_caseless_equal(&equal, left, size, right, other);
		if (status != 0)
			return status;
		if (equal) {
			*ink = &search->color;
			break;
		}
	}
	return 0;
}

static int
draw_line(void *context, struct st_rip3_point origin)
{
	const struct column_line *line = context;
	struct st_rip3_text_position at = {(double)line->x - origin.x, (double)line->y - origin.y};
	if (!line->justify && line->search->count == 0)
		return draw_run(line, 0, line->length, at, NULL);
	size_t start, end = 0, words = 0;
	double total = 0, gap = 0;
	if (line->justify) {
		while (next_word(line, &start, &end)) {
			double width;
			int status = measure_span(line, line->glyphs + start, end - start, &width);
			if (status != 0)
				return status;
			total += width;
			++words;
		}
		if (words > 1 && total < line->width)
			gap = (line->width - total) / (double)(words - 1);
	}
	if (gap == 0 && line->search->count == 0)
		return draw_run(line, 0, line->length, at, NULL);
	size_t previous = 0;
	end = 0;
	while (next_word(line, &start, &end)) {
		double width, space_width = 0;
		const uint8_t *ink;
		int status = search_color(line->search, line->glyphs + start, end - start, &ink);
		if (status == 0 && gap == 0 && previous < start)
			status = measure_span(line, line->glyphs + previous, start - previous, &space_width);
		if (status == 0)
			status = measure_span(line, line->glyphs + start, end - start, &width);
		at.x += space_width;
		if (status == 0)
			status = draw_run(line, start, end, at, ink);
		if (status != 0)
			return status;
		at.x += width + gap;
		previous = end;
	}
	return 0;
}

static const struct st_rip3_column *
available_column(struct st_rip3_columns *columns, int64_t line_height)
{
	struct st_rip3_column_cursor *cursor = &columns->cursors[columns->article];
	while (cursor->slot < ST_RIP3_COLUMN_COUNT) {
		const struct st_rip3_column *slot = &columns->slots[cursor->slot];
		int64_t height = (int64_t)slot->area.y1 - slot->area.y0 + 1;
		if (slot->defined && slot->article == columns->article && line_height <= height - cursor->offset)
			return slot;
		++cursor->slot;
		cursor->offset = 0;
	}
	return NULL;
}

static int
paint_line(struct st_rip3_columns *columns, struct column_line *line, const struct st_rip3_column *slot)
{
	struct st_rip3_column_cursor *cursor = &columns->cursors[columns->article];
	line->x = slot->area.x0;
	line->y = (int32_t)(slot->area.y0 + cursor->offset);
	line->width = (double)slot->area.x1 - slot->area.x0 + 1;
	line->search = &slot->search;
	int status = 0;
	if (line->length != 0)
		status = st_rip3_ports_draw_clipped(line->host->ports, &slot->area, draw_line, line);
	if (status == 0)
		cursor->offset += line->height;
	return status;
}

static int
append_bytes(struct article_buffer *buffer, const uint8_t *bytes, size_t length)
{
	if (length == 0)
		return 0;
	if (length > SIZE_MAX - buffer->length)
		return IO_ERROR_OUT_OF_RANGE;
	size_t required = buffer->length + length;
	if (required > buffer->capacity) {
		size_t capacity = buffer->capacity <= SIZE_MAX / 2 ? buffer->capacity * 2 : required;
		if (capacity < 64)
			capacity = 64;
		if (capacity < required)
			capacity = required;
		uint8_t *data = realloc(buffer->bytes, capacity);
		if (data == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		buffer->bytes = data;
		buffer->capacity = capacity;
	}
	memcpy(buffer->bytes + buffer->length, bytes, length);
	buffer->length = required;
	return 0;
}

static int
prepare_flow(struct st_rip3_article_flow *flow, const struct st_rip3_command_host *host)
{
	if (flow->prepared)
		return 0;
	flow->pen = (struct column_line){.host = host};
	int status = prepare_line(&flow->pen);
	if (status == 0)
		status = measure_span(&flow->pen, flow->line.bytes, flow->line.length, &flow->width);
	flow->prepared = status == 0;
	return status;
}

static int
retain_from(struct st_rip3_columns *columns, const struct st_rip3_command_host *host, size_t start)
{
	struct st_rip3_article_flow *flow = columns->flow;
	if (flow->retention_failed || flow->pending.length <= start)
		return 0;
	int status = st_rip3_overflow_remainder(&columns->overflow, host->resources, columns->article,
	    flow->pending.bytes + start, flow->pending.length - start);
	flow->retention_failed = status != 0;
	flow->pending.length = 0;
	flow->word_start = 0;
	return status;
}

static int
finish_line(struct st_rip3_columns *columns, const struct st_rip3_command_host *host, bool blank, bool *changed)
{
	struct st_rip3_article_flow *flow = columns->flow;
	if (flow->line.length == 0 && !blank)
		return 0;
	int status = prepare_flow(flow, host);
	if (status == 0) {
		const struct st_rip3_column *slot = available_column(columns, flow->pen.height);
		if (slot != NULL) {
			flow->pen.glyphs = flow->line.bytes;
			flow->pen.length = flow->line.length;
			*changed |= flow->line.length != 0;
			status = paint_line(columns, &flow->pen, slot);
		}
		else if (flow->line.length != 0)
			status = retain_from(columns, host, 0);
	}
	flow->line.length = 0;
	flow->width = 0;
	return status;
}

static int
finish_word(struct st_rip3_columns *columns, const struct st_rip3_command_host *host, bool *changed)
{
	struct st_rip3_article_flow *flow = columns->flow;
	flow->dropping_word = false;
	if (flow->word.length == 0)
		return 0;
	const uint8_t *glyphs;
	size_t length;
	uint8_t *owned = NULL;
	int status = st_rip3_command_expanded_glyphs(host->text, flow->word.bytes, flow->word.length,
	    &glyphs, &length, &owned);
	flow->word.length = 0;
	if (status == 0)
		status = prepare_flow(flow, host);
	const struct st_rip3_column *slot = status == 0 ? available_column(columns, flow->pen.height) : NULL;
	if (status == 0 && slot == NULL) {
		size_t start = flow->line.length != 0 ? 0 : flow->word_start;
		status = retain_from(columns, host, start);
		flow->line.length = 0;
		flow->width = 0;
	}
	if (slot != NULL) {
		const uint8_t separator = ' ';
		double word_width, separator_width = 0;
		status = measure_span(&flow->pen, glyphs, length, &word_width);
		if (status == 0 && flow->line.length != 0)
			status = measure_span(&flow->pen, &separator, 1, &separator_width);
		double width = (double)slot->area.x1 - slot->area.x0 + 1;
		if (status == 0 && flow->line.length != 0 && flow->width + separator_width + word_width > width) {
			status = finish_line(columns, host, false, changed);
			slot = available_column(columns, flow->pen.height);
			if (status == 0 && slot == NULL)
				status = retain_from(columns, host, flow->word_start);
			separator_width = 0;
		}
		if (status == 0 && slot != NULL) {
			size_t previous = flow->line.length;
			if (previous != 0)
				status = append_bytes(&flow->line, &separator, 1);
			if (status == 0)
				status = append_bytes(&flow->line, glyphs, length);
			if (status == 0) {
				flow->width += separator_width + word_width;
				if (previous == 0 && flow->word_start < flow->pending.length) {
					/* Retain raw bytes starting at the first word of the
					 * next unpainted line, including its trailing delimiter. */
					flow->pending.length -= flow->word_start;
					memmove(flow->pending.bytes, flow->pending.bytes + flow->word_start, flow->pending.length);
					flow->word_start = 0;
				}
			}
			else
				flow->line.length = previous;
		}
	}
	free(owned);
	return status;
}

static int
flush_pending(struct st_rip3_columns *columns, const struct st_rip3_command_host *host)
{
	if (columns->flow == NULL)
		return 0;
	bool changed = false;
	int status = finish_word(columns, host, &changed);
	int line_status = finish_line(columns, host, false, &changed);
	return status != 0 ? status : line_status;
}

bool
st_rip3_columns_raw_active(const struct st_rip3_columns *columns)
{
	return columns->active && columns->article != 0;
}

int
st_rip3_columns_feed(struct st_rip3_columns *columns, const struct st_rip3_command_host *host,
    const uint8_t *bytes, size_t length, bool *changed)
{
	bool retain = columns->overflow.articles[columns->article].enabled;
	const struct st_rip3_column_cursor *cursor = &columns->cursors[columns->article];
	if (!retain && cursor->slot == ST_RIP3_COLUMN_COUNT)
		return 0;
	if (columns->flow == NULL) {
		columns->flow = calloc(1, sizeof(*columns->flow));
		if (columns->flow == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
	}
	struct st_rip3_article_flow *flow = columns->flow;
	int storage_status = 0;
	for (size_t i = 0; i < length; ++i) {
		uint8_t byte = bytes[i];
		if (retain && !flow->retention_failed) {
			storage_status = st_rip3_overflow_source(&columns->overflow, host->resources, columns->article, byte);
			if (storage_status == 0 && st_rip3_overflow_writing_remainder(&columns->overflow, columns->article)) {
				storage_status = st_rip3_overflow_remainder(&columns->overflow, host->resources,
				    columns->article, &byte, 1);
				if (storage_status == 0)
					continue;
			}
			if (storage_status == 0 && (flow->pending.length != 0 || (!space(byte) && byte >= ' ')))
				storage_status = append_bytes(&flow->pending, &byte, 1);
			flow->retention_failed = storage_status != 0;
		}
		if (cursor->slot == ST_RIP3_COLUMN_COUNT && (!retain || flow->retention_failed))
			continue;
		bool lf_after_cr = flow->previous_cr && byte == '\n';
		flow->previous_cr = byte == '\r';
		if (lf_after_cr || (!space(byte) && byte < ' '))
			continue;
		if (!space(byte)) {
			flow->newline = false;
			if (!flow->dropping_word) {
				if (flow->word.length == 0 && retain && !flow->retention_failed)
					flow->word_start = flow->pending.length - 1;
				int status = append_bytes(&flow->word, &byte, 1);
				if (status != 0) {
					flow->word.length = 0;
					flow->dropping_word = true;
					return status;
				}
			}
			continue;
		}
		int status = finish_word(columns, host, changed);
		if (status != 0)
			return status;
		if (byte == '\r' || byte == '\n') {
			if (flow->newline) {
				status = finish_line(columns, host, false, changed);
				if (status == 0)
					status = finish_line(columns, host, true, changed);
				if (status != 0)
					return status;
				flow->pending.length = 0;
				flow->word_start = 0;
			}
			flow->newline = true;
		}
	}
	return storage_status;
}

static void
clear_flow(struct st_rip3_columns *columns)
{
	if (columns->flow != NULL) {
		free(columns->flow->word.bytes);
		free(columns->flow->line.bytes);
		free(columns->flow->pending.bytes);
		free(columns->flow);
		columns->flow = NULL;
	}
}

int
st_rip3_columns_end(struct st_rip3_columns *columns, const struct st_rip3_command_host *host)
{
	if (columns->article == 0)
		return 0;
	int status = flush_pending(columns, host);
	int output_status = st_rip3_overflow_close(&columns->overflow, columns->article);
	clear_flow(columns);
	columns->active = false;
	return status == 0 ? output_status : status;
}

void
st_rip3_columns_invalidate(struct st_rip3_columns *columns)
{
	if (columns->flow != NULL)
		columns->flow->prepared = false;
}

int
st_rip3_columns_reset(struct st_rip3_columns *columns, bool hard)
{
	clear_flow(columns);
	memset(columns->slots, 0, sizeof(columns->slots));
	memset(columns->cursors, 0, sizeof(columns->cursors));
	columns->active = false;
	columns->article = 0;
	return st_rip3_overflow_reset(&columns->overflow, hard);
}

void
st_rip3_columns_dispose(struct st_rip3_columns *columns)
{
	clear_flow(columns);
	st_rip3_overflow_dispose(&columns->overflow);
	*columns = (struct st_rip3_columns){0};
}

int
st_rip3_columns_select(struct st_rip3_columns *columns, const struct st_rip3_command_host *host,
    unsigned article)
{
	int status = columns->article != article ? flush_pending(columns, host) : 0;
	clear_flow(columns);
	columns->article = article;
	columns->active = false;
	for (size_t i = 0; i < ST_RIP3_COLUMN_COUNT; ++i) {
		if (columns->slots[i].defined && columns->slots[i].article == article) {
			columns->active = true;
			break;
		}
	}
	return status;
}

int
st_rip3_columns_define(struct st_rip3_columns *columns, const struct st_rip3_command_host *host,
    const struct st_rip3_column_definition *definition, const uint8_t *words, size_t length, bool converted)
{
	struct st_rip3_column_search search = {.color = definition->color};
	bool truncated = false;
	if ((definition->flags & 2) != 0) {
		size_t at = 0;
		while (at < length) {
			while (at < length && space(words[at]))
				++at;
			if (at == length)
				break;
			if (search.count == ST_RIP3_COLUMN_SEARCH_WORDS) {
				truncated = true;
				break;
			}
			size_t start = at;
			while (at < length && !space(words[at]))
				++at;
			if (st_rip3_graphical_units(words + start, at - start, converted) > ST_RIP3_COLUMN_SEARCH_LENGTH)
				return IO_ERROR_OUT_OF_RANGE;
			const uint8_t *glyphs;
			size_t size;
			uint8_t *owned = NULL;
			int status = st_rip3_command_expanded_glyphs(host->text, words + start, at - start,
			    &glyphs, &size, &owned);
			if (status != 0)
				return status;
			memcpy(search.words[search.count], glyphs, size);
			search.lengths[search.count++] = size;
			free(owned);
		}
		if (search.count == 0)
			return IO_ERROR_INVALID_FORMAT;
	}
	unsigned article = definition->article;
	int status = st_rip3_overflow_close(&columns->overflow, article);
	columns->cursors[article] = (struct st_rip3_column_cursor){0};
	struct st_rip3_column *slot = &columns->slots[definition->slot];
	slot->area = definition->area;
	slot->article = definition->article;
	slot->defined = true;
	if (definition->extended) {
		slot->search = search;
		columns->overflow.articles[definition->article].enabled = (definition->flags & 1) != 0;
	}
	int selection_status = st_rip3_columns_select(columns, host, article);
	if (status == 0)
		status = selection_status;
	/* Original reports excess words after installing the first eight. */
	return status == 0 && truncated ? IO_ERROR_OUT_OF_RANGE : status;
}

int
st_rip3_columns_line(struct st_rip3_columns *columns, const struct st_rip3_command_host *host,
    const uint8_t *text, size_t length, bool justify)
{
	if (!columns->active || columns->cursors[columns->article].slot == ST_RIP3_COLUMN_COUNT)
		return 0;
	int status = flush_pending(columns, host);
	if (status != 0)
		return status;
	struct column_line line = {.host = host, .justify = justify};
	uint8_t *owned = NULL;
	status = st_rip3_command_expanded_glyphs(host->text, text, length, &line.glyphs, &line.length, &owned);
	if (status == 0)
		status = prepare_line(&line);
	if (status == 0) {
		const struct st_rip3_column *slot = available_column(columns, line.height);
		if (slot != NULL)
			status = paint_line(columns, &line, slot);
	}
	free(owned);
	return status;
}
