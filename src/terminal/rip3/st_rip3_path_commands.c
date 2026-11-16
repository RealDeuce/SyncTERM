/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_command_internal.h"
#include "st_rip3_environments.h"
#include "st_rip3_styles.h"
#include "st_rip3_ports.h"
#include "st_rip3_draw_internal.h"
#include "st_rip3_polygon_internal.h"
#include "io_error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int
st_rip3_path_commands_execute(const struct st_rip3_command_host *host,
    const uint8_t *command, size_t length, bool *operation)
{
	struct st_rip3_environment environment;
	struct st_rip3_style style;
	struct st_rip3_frame frame;
	struct st_rip3_port_info port;
	struct st_rip3_stroke_mask mask = {0};
	struct st_rip3_vector *points;
	uint64_t value;
	unsigned environment_number, current;
	const uint8_t *arguments = command+1;
	size_t count = length-1, vertices, width, fields, total;
	bool filled = command[0] == 'p', borders = false;
	uint8_t foreground = 0, background = 0, drawing = 0;
	int status;

	if (memchr(arguments, ' ', count) != NULL)
		return IO_ERROR_NOT_SUPPORTED; /* General numeric comments: #7. */
	if (count < 2)
		return IO_ERROR_INVALID_FORMAT;
	status = st_rip3_command_environment(host, &environment_number, &environment);
	if (status != 0)
		return status;
	status = st_rip3_command_field(&value, arguments, 2, environment.base_math);
	if (status != 0)
		return status;
	/* Only the documented two-digit direct count is decoded here. Zero may
	 * have a special large-count meaning (#13); undersized paths' wire
	 * policy is also unresolved, rather than a chosen degeneracy default. */
	if (value < (command[0] == 'l' ? 2u : 3u))
		return IO_ERROR_NOT_SUPPORTED;
	vertices = (size_t)value; /* At most 4095 in a two-digit current-base field. */
	width = environment.coordinate_size;
	fields = vertices*2;
	total = 2+fields*width;
	if (count <= total-width || count > total)
		return IO_ERROR_INVALID_FORMAT;
	/* Reject a malformed late field before allocating or looking up inks.
	 * Inactive viewports still consume and validate the entire command. */
	for (size_t i = 0; i < fields; i++) {
		status = st_rip3_command_field(&value, arguments+2+i*width,
		    i == fields-1 ? count-2-i*width : width, environment.base_math);
		if (status != 0)
			return status;
	}
	if (host->ports == NULL || host->styles == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	status = st_rip3_command_frame(host, environment_number, &frame);
	if (status == 0)
		status = st_rip3_ports_current(host->ports, &current);
	if (status == 0)
		status = st_rip3_ports_info(host->ports, current, &port);
	if (status != 0 || !port.viewport_active)
		return status;
	status = st_rip3_styles_current(host->styles, &current);
	if (status == 0)
		status = st_rip3_styles_get(host->styles, current, &style);
	if (status != 0)
		return status;
	*operation = true;
	if (filled) {
		bool fill_foreground = false, fill_background = false;
		if (host->borders == NULL)
			return IO_ERROR_NOT_SUPPORTED;
		borders = *host->borders;
		for (size_t i = 0; i < 8; i++) {
			fill_foreground |= style.fill.rows[i] != 0;
			fill_background |= style.fill.rows[i] != 255;
		}
		/* Polygon borders retain their pattern but use COPY, even with a NOT
		 * interior. Resolve every used operand before any geometry can paint. */
		if (borders)
			status = st_rip3_command_ink(host,environment.rgb_bits == 8,style.drawing_color,&drawing);
		if (status == 0 && style.write_mode != ST_RIP3_NOT && fill_foreground)
			status = st_rip3_command_ink(host,environment.rgb_bits == 8,style.fill_color,&foreground);
		if (status == 0 && ((style.write_mode != ST_RIP3_NOT && fill_background)
		    || (borders && style.pen.draw_background && style.pen.pattern != 0xffff)))
			status = st_rip3_command_ink(host,environment.rgb_bits == 8,style.background_color,&background);
		if (status != 0)
			return status;
	}
	else if (style.write_mode != ST_RIP3_NOT) {
		status = st_rip3_command_ink(host, environment.rgb_bits == 8, style.drawing_color, &foreground);
		if (status == 0 && style.pen.draw_background && style.pen.pattern != 0xffff)
			status = st_rip3_command_ink(host, environment.rgb_bits == 8, style.background_color, &background);
		if (status != 0)
			return status;
	}
	points = malloc(vertices*sizeof(*points));
	if (points == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	for (size_t i = 0; i < fields; i++) {
		status = st_rip3_command_field(&value, arguments+2+i*width,
		    i == fields-1 ? count-2-i*width : width, environment.base_math);
		if (status != 0)
			goto done;
		/* Products fit uint64_t and projections are exact integers in double.
		 * Keep far endpoints intact: clipping/saturation before stroke coverage
		 * would alter both the edge slope and its continuing mask phase. */
		if (i%2 == 0)
			points[i/2].x = (double)(value*frame.device_width/frame.world_width);
		else
			points[i/2].y = (double)(value*frame.device_height/frame.world_height);
	}
	if (filled) {
		status = st_rip3_polygon_fill(host->ports,points,vertices,&style.fill,
		    foreground,background,borders ? &style.pen : NULL,drawing,style.write_mode);
		goto done;
	}
	status = st_rip3_stroke_path_vectors(host->ports, points, vertices,
	    command[0] == 'P', &style.pen, &mask);
	if (status == 0)
		status = st_rip3_stroke_apply(host->ports, &mask, foreground, background, style.write_mode);
done:
	st_rip3_stroke_free(&mask);
	free(points);
	return status;
}
