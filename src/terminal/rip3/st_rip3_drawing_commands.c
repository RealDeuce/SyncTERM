/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_command_internal.h"
#include "st_rip3_environments.h"
#include "st_rip3_styles.h"
#include "st_rip3_ports.h"
#include "st_rip3_palettes.h"
#include "st_rip3_draw_internal.h"
#include "io_error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static int
color_field(const struct st_rip3_environment *environment,
    const uint8_t *arguments, size_t count, uint32_t *result)
{
	uint64_t value;
	int status;

	if (count == 0 || count > 4)
		return IO_ERROR_INVALID_FORMAT;
	status = st_rip3_command_field(&value, arguments, count,
	    environment->rgb_bits == 8 ? 64 : environment->base_math);
	if (status != 0)
		return status;
	if (value > (environment->rgb_bits == 8 ? UINT64_C(0xffffff) : 255))
		return IO_ERROR_OUT_OF_RANGE;
	*result = (uint32_t)value;
	return 0;
}

static int
fill_style(struct st_rip3_style *style, const struct st_rip3_environment *environment,
    const uint8_t *arguments, size_t count, bool custom)
{
	size_t prefix = custom ? 16 : 2;
	uint32_t color;
	uint64_t value;
	int status;

	if (count <= prefix || count > prefix+4)
		return IO_ERROR_INVALID_FORMAT;
	status = color_field(environment, arguments+prefix, count-prefix, &color);
	if (status != 0)
		return status;
	if (custom) {
		unsigned rows[8];

		for (size_t i = 0; i < 8; i++) {
			status = st_rip3_command_field(&value, arguments+2*i, 2, environment->base_math);
			if (status != 0)
				return status;
			if (value > 255)
				return IO_ERROR_OUT_OF_RANGE;
			rows[i] = (unsigned)value;
		}
		status = st_rip3_fill_custom(&style->fill, rows, &style->fill.origin);
	}
	else {
		/* Background/solid fills and patterned masks whose table columns
		 * agree. Conflicting masks (02/04/05/07/0B) remain under #12. */
		static const uint8_t masks[12][8] = {
		    [0] = {0}, [1] = {255,255,255,255,255,255,255,255},
		    [3] = {1,2,4,8,16,32,64,128},
		    [6] = {0xa5,0xd2,0x69,0xb4,0x5a,0x2d,0x96,0x4b},
		    [8] = {0x81,0x42,0x24,0x18,0x18,0x24,0x42,0x81},
		    [9] = {0xcc,0x33,0xcc,0x33,0xcc,0x33,0xcc,0x33},
		    [10] = {0x80,0,8,0,0x80,0,8,0},
		};

		status = st_rip3_command_field(&value, arguments, 2, environment->base_math);
		if (status != 0)
			return status;
		if (value > 11)
			return IO_ERROR_OUT_OF_RANGE;
		if (value == 2 || value == 4 || value == 5 || value == 7 || value == 11)
			return IO_ERROR_NOT_SUPPORTED; /* Stock masks awaiting #12. */
		/* Only the mask/ink are specified here. Retain the caller's explicit
		 * resolved device phase until wire anchoring is clarified under #12. */
		memcpy(style->fill.rows, masks[value], sizeof(style->fill.rows));
	}
	if (status == 0)
		style->fill_color = color;
	return status;
}

static int
font_style(struct st_rip3_style *style, const uint8_t *arguments,
    size_t count, unsigned radix)
{
	uint64_t values[4];
	int status;

	if (count < 7 || count > 8)
		return IO_ERROR_INVALID_FORMAT;
	for (size_t i = 0; i < 4; i++) {
		status = st_rip3_command_field(values+i, arguments+2*i,
		    i == 3 ? count-6 : 2, radix);
		if (status != 0)
			return status;
	}
	return st_rip3_classic_text_style_set(&style->font, (unsigned)values[0],
	    (unsigned)values[1], (unsigned)values[2], (unsigned)values[3]);
}

int
st_rip3_command_ink(const struct st_rip3_command_host *host, bool direct, uint32_t color, uint8_t *result)
{
	if (!direct) {
		if (color > 255)
			return IO_ERROR_OUT_OF_RANGE;
		*result = (uint8_t)color;
		return 0;
	}
	if (host->palettes == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	return st_rip3_palettes_color(host->palettes, true, color, result);
}

static int
shape(const struct st_rip3_command_host *host,
    unsigned environment_number, const struct st_rip3_environment *environment,
    const struct st_rip3_style *style, const uint8_t *arguments,
    size_t count, uint8_t command, bool *operation)
{
	uint64_t values[4];
	struct st_rip3_vector points[4];
	struct st_rip3_frame frame;
	struct st_rip3_port_info port;
	unsigned current;
	struct st_rip3_stroke_mask mask = {0};
	size_t width = environment->coordinate_size, vertices = 2;
	uint8_t foreground = 0, background = 0;
	int status;

	if (count <= 3*width || count > 4*width)
		return IO_ERROR_INVALID_FORMAT;
	for (size_t i = 0; i < 4; i++) {
		status = st_rip3_command_field(values+i, arguments+i*width,
		    i == 3 ? count-3*width : width, environment->base_math);
		if (status != 0)
			return status;
	}
	if (host->ports == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	status = st_rip3_command_frame(host, environment_number, &frame);
	if (status != 0)
		return status;
	/* Valid five-digit wire values project beyond int32_t at small worlds.
	 * Divide in wide integer arithmetic, then preserve the exact integral
	 * projection as doubles until raster clipping. Axis saturation would
	 * change the slope of a line whose far endpoint lies outside the canvas. */
	for (size_t i = 0; i < 2; i++) {
		points[i].x = (double)(values[i*2] * frame.device_width / frame.world_width);
		points[i].y = (double)(values[i*2+1] * frame.device_height / frame.world_height);
	}
	if (command == 'R' || command == 'K') {
		double x0 = points[0].x < points[1].x ? points[0].x : points[1].x;
		double x1 = points[0].x > points[1].x ? points[0].x : points[1].x;
		double y0 = points[0].y < points[1].y ? points[0].y : points[1].y;
		double y1 = points[0].y > points[1].y ? points[0].y : points[1].y;

		points[0] = (struct st_rip3_vector){x0,y0};
		points[1] = (struct st_rip3_vector){x1,y1};
		if (x0 != x1 && y0 != y1) {
			points[1] = (struct st_rip3_vector){x1,y0};
			points[2] = (struct st_rip3_vector){x1,y1};
			points[3] = (struct st_rip3_vector){x0,y1};
			vertices = 4;
		}
	}
	status = st_rip3_ports_current(host->ports, &current);
	if (status == 0)
		status = st_rip3_ports_info(host->ports, current, &port);
	if (status != 0 || !port.viewport_active)
		return status;
	*operation = true;
	if (command == 'B' || command == 'K') {
		bool uses_foreground = false, uses_background = false;
		bool borders = false, interior = true;
		uint8_t border_color = 0;
		struct st_rip3_rect area;

		if (command == 'K') {
			if (host->borders == NULL)
				return IO_ERROR_NOT_SUPPORTED;
			borders = *host->borders;
			interior = vertices == 4;
			if (!interior && !borders)
				return 0;
		}
		if (borders) {
			/* Border COPY needs drawing ink even when the fill mode is NOT.
			 * Resolve it before the fill can change any pixels. */
			status = st_rip3_command_ink(host, environment->rgb_bits == 8, style->drawing_color, &border_color);
			if (status != 0)
				return status;
		}
		if (interior) {
			for (size_t i = 0; i < 8; i++) {
				uses_foreground |= style->fill.rows[i] != 0;
				uses_background |= style->fill.rows[i] != 255;
			}
		}
		if (style->write_mode != ST_RIP3_NOT) {
			if (uses_foreground)
				status = st_rip3_command_ink(host, environment->rgb_bits == 8, style->fill_color, &foreground);
			if (status == 0 && uses_background)
				status = st_rip3_command_ink(host, environment->rgb_bits == 8, style->background_color, &background);
			if (status != 0)
				return status;
		}
		/* Axis-aligned filled bounds may saturate outside the device range:
		 * unlike a line slope, their intersection with the viewport is exact. */
		size_t opposite = vertices == 4 ? 2 : 1;
		area = (struct st_rip3_rect){
		    points[0].x > INT32_MAX ? INT32_MAX : (int32_t)points[0].x,
		    points[0].y > INT32_MAX ? INT32_MAX : (int32_t)points[0].y,
		    points[opposite].x > INT32_MAX ? INT32_MAX : (int32_t)points[opposite].x,
		    points[opposite].y > INT32_MAX ? INT32_MAX : (int32_t)points[opposite].y};
		if (command == 'K') {
			if (borders) {
				const struct st_rip3_pen pen = {.pattern=0xffff,.thickness=style->pen.thickness};

				/* Preserve wide border endpoints and prepare all scratch storage
				 * before filling. The specific N rule is solid COPY (#33). */
				status = st_rip3_stroke_path_vectors(host->ports, points, vertices,
				    vertices == 4, &pen, &mask);
				if (status != 0)
					return status;
			}
			status = st_rip3_draw_filled_rectangle(host->ports, &area, &style->fill,
			    foreground, background, NULL, 0, style->write_mode);
			if (status == 0 && borders)
				status = st_rip3_stroke_apply(host->ports, &mask, border_color, 0, ST_RIP3_COPY);
			st_rip3_stroke_free(&mask);
			return status;
		}
		return st_rip3_draw_bar(host->ports, &area, &style->fill,
		    foreground, background, style->write_mode);
	}
	/* Destination NOT ignores both colour operands. Background lookup is
	 * only needed for a pen whose pattern-off pixels are painted. */
	if (style->write_mode != ST_RIP3_NOT) {
		status = st_rip3_command_ink(host, environment->rgb_bits == 8, style->drawing_color, &foreground);
		if (status == 0 && style->pen.draw_background)
			status = st_rip3_command_ink(host, environment->rgb_bits == 8, style->background_color, &background);
		if (status != 0)
			return status;
	}
	status = st_rip3_stroke_path_vectors(host->ports, points, vertices,
	    vertices == 4, &style->pen, &mask);
	if (status == 0)
		status = st_rip3_stroke_apply(host->ports, &mask, foreground, background, style->write_mode);
	st_rip3_stroke_free(&mask);
	return status;
}

int
st_rip3_drawing_commands_execute(const struct st_rip3_command_host *host,
    const uint8_t *command, size_t length, bool *operation)
{
	struct st_rip3_environment environment;
	struct st_rip3_style style;
	unsigned environment_number, number;
	uint64_t value;
	const uint8_t *arguments = command+1;
	size_t count = length-1;
	int status;

	if (memchr(arguments, ' ', count) != NULL)
		return IO_ERROR_NOT_SUPPORTED; /* General numeric comments: #7. */
	if (command[0] == 'W' && memchr(arguments, '.', count) != NULL)
		return IO_ERROR_NOT_SUPPORTED; /* Termination/default value is unresolved. */
	if (host->styles == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	status = st_rip3_command_environment(host, &environment_number, &environment);
	if (status == 0)
		status = st_rip3_styles_current(host->styles, &number);
	if (status == 0)
		status = st_rip3_styles_get(host->styles, number, &style);
	if (status != 0)
		return status;
	if (command[0] == 'L' || command[0] == 'R' || command[0] == 'B' || command[0] == 'K')
		return shape(host, environment_number, &environment, &style, arguments,
		    count, command[0], operation);
	if (command[0] == 'S' || command[0] == 's') {
		status = fill_style(&style, &environment, arguments, count, command[0] == 's');
		if (status != 0)
			return status;
		*operation = true;
		return st_rip3_styles_set(host->styles, number, &style);
	}
	if (command[0] == 'Y') {
		status = font_style(&style, arguments, count, environment.base_math);
		if (status != 0)
			return status;
		*operation = true;
		return st_rip3_styles_set(host->styles, number, &style);
	}
	if (command[0] == 'W') {
		if (count == 0 || count > 2)
			return IO_ERROR_INVALID_FORMAT;
		status = st_rip3_command_field(&value, arguments, count, environment.base_math);
		if (status != 0)
			return status;
		if (value > 4)
			return IO_ERROR_OUT_OF_RANGE;
		style.write_mode = (enum st_rip3_write_mode)value;
	}
	else {
		uint32_t color;

		status = color_field(&environment, arguments, count, &color);
		if (status != 0)
			return status;
		if (command[0] == 'c') style.drawing_color = color;
		else style.background_color = color;
	}
	*operation = true;
	return st_rip3_styles_set(host->styles, number, &style);
}
