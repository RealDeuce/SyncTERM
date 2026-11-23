/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_columns.h"

#include "st_rip3_classic_text.h"
#include "st_rip3_command_internal.h"
#include "st_rip3_environments.h"
#include "st_rip3_outline_cache.h"
#include "st_rip3_outline_font.h"
#include "st_rip3_outline_layout.h"
#include "st_rip3_ports.h"
#include "st_rip3_styles.h"
#include "st_rip3_text_commands.h"
#include "io_error.h"

#include <math.h>
#include <stdlib.h>

int
st_rip3_columns_define(struct st_rip3_columns *columns, const struct st_rip3_command_host *host,
    const uint8_t *command, size_t length)
{
	struct st_rip3_environment env;
	unsigned current;
	int status = st_rip3_command_environment(host, &current, &env);
	if (status != 0)
		return status;
	size_t width = env.coordinate_size;
	if (length != 4 + 4 * width)
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
	if (status != 0 || port.protected)
		return status;
	columns->slots[field[4]] = (struct st_rip3_column){
		.area = {first.x < last.x ? first.x : last.x, first.y < last.y ? first.y : last.y,
		    first.x > last.x ? first.x : last.x, first.y > last.y ? first.y : last.y},
		.article = (unsigned)field[5], .defined = true,
	};
	columns->article = (unsigned)field[5];
	columns->current = 0;
	columns->offset = 0;
	columns->active = true;
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
	int64_t height;
	int32_t x, y;
	double width;
	bool justify;
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
measure_word(const struct column_line *line, size_t start, size_t end, double *width)
{
	if (line->style.font_mode == ST_RIP3_FONT_OUTLINE) {
		struct st_rip3_outline_width measure;
		int status = st_rip3_outline_cache_measure(&measure, line->host->text->outline_cache,
		    line->glyphs + start, end - start);
		if (status == 0)
			*width = (double)measure.advance;
		return status;
	}
	struct st_rip3_classic_text_plan plan;
	int status = st_rip3_classic_text_plan(&line->classic, &line->style.font, line->glyphs + start, end - start,
	    &(struct st_rip3_text_position){0, 0}, &plan);
	if (status == 0)
		*width = plan.advance;
	return status;
}

static int
draw_run(const struct column_line *line, size_t start, size_t end, struct st_rip3_text_position at)
{
	if (start == end)
		return 0;
	if (line->style.font_mode == ST_RIP3_FONT_OUTLINE) {
		at.y -= (double)line->outline.bounds.ymin + line->layout.baseline.y;
		at.x -= line->layout.baseline.x;
		return st_rip3_command_draw_outline(line->host, &line->style, &line->layout, &line->outline,
		    line->rgb_bits, &at, line->glyphs + start, end - start);
	}
	return st_rip3_draw_classic_text(line->host->ports, &line->classic, &line->style.font,
	    line->glyphs + start, end - start, &at, line->foreground, line->background, line->style.write_mode);
}

static bool
next_word(const struct column_line *line, size_t *start, size_t *end)
{
	*start = *end;
	while (*start < line->length && line->glyphs[*start] == ' ')
		++*start;
	*end = *start;
	while (*end < line->length && line->glyphs[*end] != ' ')
		++*end;
	return *end != *start;
}

static int
draw_line(void *context, struct st_rip3_point origin)
{
	const struct column_line *line = context;
	struct st_rip3_text_position at = {(double)line->x - origin.x, (double)line->y - origin.y};
	if (!line->justify)
		return draw_run(line, 0, line->length, at);
	size_t start, end = 0, words = 0;
	double total = 0;
	while (next_word(line, &start, &end)) {
		double width;
		int status = measure_word(line, start, end, &width);
		if (status != 0)
			return status;
		total += width;
		++words;
	}
	if (words < 2 || total >= line->width)
		return draw_run(line, 0, line->length, at);
	double gap = (line->width - total) / (double)(words - 1);
	end = 0;
	while (next_word(line, &start, &end)) {
		double width;
		int status = measure_word(line, start, end, &width);
		if (status == 0)
			status = draw_run(line, start, end, at);
		if (status != 0)
			return status;
		at.x += width + gap;
	}
	return 0;
}

int
st_rip3_columns_line(struct st_rip3_columns *columns, const struct st_rip3_command_host *host,
    const uint8_t *text, size_t length, bool justify)
{
	if (!columns->active || columns->current == ST_RIP3_COLUMN_COUNT)
		return 0;
	struct column_line line = {.host = host, .justify = justify};
	uint8_t *owned = NULL;
	int status = st_rip3_command_expanded_glyphs(host->text, text, length, &line.glyphs, &line.length, &owned);
	if (status == 0)
		status = prepare_line(&line);
	if (status == 0) {
		for (; columns->current < ST_RIP3_COLUMN_COUNT; ++columns->current) {
			const struct st_rip3_column *slot = &columns->slots[columns->current];
			int64_t height = (int64_t)slot->area.y1 - slot->area.y0 + 1;
			if (slot->defined && slot->article == columns->article && line.height <= height - columns->offset) {
				line.x = slot->area.x0;
				line.y = (int32_t)(slot->area.y0 + columns->offset);
				line.width = (double)slot->area.x1 - slot->area.x0 + 1;
				status = st_rip3_ports_draw_clipped(host->ports, &slot->area, draw_line, &line);
				if (status == 0)
					columns->offset += line.height;
				break;
			}
			columns->offset = 0;
		}
	}
	free(owned);
	return status;
}
