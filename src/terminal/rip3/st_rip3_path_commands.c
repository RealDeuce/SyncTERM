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

static int
read_vertices(struct st_rip3_fields *fields, const struct st_rip3_environment *environment,
    const struct st_rip3_frame *frame, struct st_rip3_vector *points, size_t count, bool last)
{
	for (size_t i = 0; i < count; i++) {
		uint64_t x, y;
		int status = st_rip3_command_next_field(fields, &x, environment->coordinate_size,
		    environment->base_math, false);
		if (status == 0)
			status = st_rip3_command_next_field(fields, &y, environment->coordinate_size,
			    environment->base_math, last && i+1 == count);
		if (status != 0)
			return status;
		/* Keep the exact projected integers through stroke clipping. */
		if (points != NULL)
			points[i] = (struct st_rip3_vector){
			    (double)(x * frame->device_width / frame->world_width),
			    (double)(y * frame->device_height / frame->world_height),
			};
	}
	return 0;
}

static int
multiple_contours(const uint8_t *bytes, size_t length,
    const struct st_rip3_environment *environment, const struct st_rip3_frame *frame,
    struct st_rip3_vector_contour *contours, struct st_rip3_vector *points,
    size_t *contour_count, size_t *vertices)
{
	uint64_t value;
	struct st_rip3_fields fields = {bytes, length};
	size_t numbers = 1;
	int status;
	status = st_rip3_command_next_field(&fields, &value, 2, environment->base_math, false);
	if (status != 0) return status;
	if (value == 0 || value > 8192/5) return IO_ERROR_OUT_OF_RANGE;
	*contour_count = (size_t)value; *vertices = 0;
	for (size_t c = 0; c < *contour_count; c++) {
		size_t width = c == 0 ? 2 : environment->coordinate_size;
		status = st_rip3_command_next_field(&fields, &value, width, environment->base_math, false);
		if (status != 0) return status;
		numbers++;
		if (value < 2) return IO_ERROR_INVALID_FORMAT;
		if (numbers > 8193 || value > (8193-numbers)/2) return IO_ERROR_OUT_OF_RANGE;
		size_t count = (size_t)value;
		numbers += 2*count;
		if (contours != NULL) contours[c] = (struct st_rip3_vector_contour){points+*vertices,count};
		status = read_vertices(&fields, environment, frame,
		    points == NULL ? NULL : points+*vertices, count, c+1 == *contour_count);
		if (status != 0)
			return status;
		*vertices += count;
	}
	return fields.remaining == 0 ? 0 : IO_ERROR_INVALID_FORMAT;
}

int
st_rip3_path_commands_execute(const struct st_rip3_command_host *host,
    const uint8_t *command, size_t length, bool *operation)
{
	struct st_rip3_environment environment;
	struct st_rip3_style style;
	struct st_rip3_frame frame;
	struct st_rip3_port_info port;
	struct st_rip3_vector *points;
	struct st_rip3_vector_contour *contours = NULL;
	uint64_t value;
	unsigned environment_number, current;
	const uint8_t *arguments = command+1;
	size_t count = length-1, vertices = 0, contour_count = 1;
	struct st_rip3_fields fields = {arguments, count};
	bool multiple = command[0] == '<';
	bool filled = command[0] == 'p' || multiple, borders = false, interior;
	uint32_t foreground = 0, background = 0, drawing = 0;
	int status;

	status = st_rip3_command_environment(host, &environment_number, &environment);
	if (status != 0)
		return status;
	if (multiple) {
		status = multiple_contours(arguments,count,&environment,NULL,NULL,NULL,&contour_count,&vertices);
		if (status != 0) return status;
	}
	else {
		status = st_rip3_command_next_field(&fields, &value, 2, environment.base_math, false);
		if (status != 0) return status;
		if (value == 0) return IO_ERROR_INVALID_FORMAT; /* Diagnosed zero, never 4096. */
		vertices = (size_t)value;
		/* Validate the variable list before allocating or resolving ink. */
		struct st_rip3_fields validate = fields;
		status = read_vertices(&validate, &environment, NULL, NULL, vertices, true);
		if (status != 0 || validate.remaining != 0)
			return IO_ERROR_INVALID_FORMAT;
		if (vertices == 1) return 0;
	}
	interior = multiple ? vertices > 2*contour_count : vertices >= 3;
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
		borders = style.borders;
		if (!interior && !borders) return 0;
		for (size_t i = 0; interior && i < 8; i++) {
			fill_foreground |= style.fill.rows[i] != 0;
			fill_background |= style.fill.rows[i] != 255;
		}
		/* Polygon borders retain their pattern but use COPY, even with a NOT
		 * interior. Resolve every used operand before any geometry can paint. */
		if (borders)
			status = st_rip3_command_ink(host,environment.rgb_bits,style.drawing_color,&drawing);
		if (status == 0 && style.write_mode != ST_RIP3_NOT && fill_foreground)
			status = st_rip3_command_ink(host,environment.rgb_bits,style.fill_color,&foreground);
		if (status == 0 && ((style.write_mode != ST_RIP3_NOT && fill_background)
		    || (borders && style.pen.draw_background && style.pen.pattern != 0xffff)))
			status = st_rip3_command_ink(host,environment.rgb_bits,style.background_color,&background);
		if (status != 0)
			return status;
	}
	else if (style.write_mode != ST_RIP3_NOT) {
		status = st_rip3_command_ink(host,environment.rgb_bits, style.drawing_color, &foreground);
		if (status == 0 && style.pen.draw_background && style.pen.pattern != 0xffff)
			status = st_rip3_command_ink(host,environment.rgb_bits, style.background_color, &background);
		if (status != 0)
			return status;
	}
	points = malloc(vertices*sizeof(*points));
	if (points == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	if (multiple) {
		contours = malloc(contour_count*sizeof(*contours));
		if (contours == NULL) {status = IO_ERROR_OUT_OF_MEMORY;goto done;}
		status = multiple_contours(arguments,count,&environment,&frame,contours,points,&contour_count,&vertices);
		if (status != 0) goto done;
	}
	else {
		status = read_vertices(&fields, &environment, &frame, points, vertices, true);
		if (status != 0)
			goto done;
	}
	if (filled) {
		struct st_rip3_fill fill;
		st_rip3_command_fill(&port,&style.fill,&fill);
		if (multiple) status = st_rip3_polygon_fill_contours(host->ports,contours,contour_count,&fill,
		    foreground,background,borders ? &style.pen : NULL,drawing,style.write_mode);
		else status = st_rip3_polygon_fill(host->ports,points,vertices,&fill,
		    foreground,background,borders ? &style.pen : NULL,drawing,style.write_mode);
		goto done;
	}
	status = st_rip3_stroke_visits(host->ports,points,vertices,command[0]=='P',
	    &style.pen,foreground,background,style.write_mode);
done:
	free(contours); free(points);
	return status;
}
