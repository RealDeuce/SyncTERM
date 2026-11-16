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
ink(const struct st_rip3_command_host *host, bool direct, uint32_t color, uint8_t *result)
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
    size_t count, bool rectangle, bool *operation)
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
	if (rectangle) {
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
	/* Destination NOT ignores both colour operands. Background lookup is
	 * only needed for a pen whose pattern-off pixels are painted. */
	if (style->write_mode != ST_RIP3_NOT) {
		status = ink(host, environment->rgb_bits == 8, style->drawing_color, &foreground);
		if (status == 0 && style->pen.draw_background)
			status = ink(host, environment->rgb_bits == 8, style->background_color, &background);
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
	if (command[0] == 'L' || command[0] == 'R')
		return shape(host, environment_number, &environment, &style, arguments,
		    count, command[0] == 'R', operation);
	if (count == 0 || count > (command[0] == 'W' ? 2u : 4u))
		return IO_ERROR_INVALID_FORMAT;
	status = st_rip3_command_field(&value, arguments, count,
	    command[0] != 'W' && environment.rgb_bits == 8 ? 64 : environment.base_math);
	if (status != 0)
		return status;
	if (command[0] == 'W') {
		if (value > 4)
			return IO_ERROR_OUT_OF_RANGE;
		style.write_mode = (enum st_rip3_write_mode)value;
	}
	else {
		if (value > (environment.rgb_bits == 8 ? UINT64_C(0xffffff) : 255))
			return IO_ERROR_OUT_OF_RANGE;
		if (command[0] == 'c') style.drawing_color = (uint32_t)value;
		else style.background_color = (uint32_t)value;
	}
	*operation = true;
	return st_rip3_styles_set(host->styles, number, &style);
}
