/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_command_internal.h"
#include "st_rip3_environments.h"
#include "st_rip3_styles.h"
#include "st_rip3_ports.h"
#include "st_rip3_ellipse.h"
#include "st_rip3_arc.h"
#include "st_rip3_draw_internal.h"
#include "io_error.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

int
st_rip3_curve_commands_execute(const struct st_rip3_command_host *host,
    const uint8_t *command, size_t length, bool *operation)
{
	struct st_rip3_environment environment;
	struct st_rip3_style style;
	struct st_rip3_frame frame;
	struct st_rip3_port_info port;
	struct st_rip3_ellipse ellipse;
	struct st_rip3_fill fill;
	struct st_rip3_arc interval = {.kind=ST_RIP3_ARC_CURVE,.start=0,.sweep=360};
	uint64_t values[6];
	unsigned environment_number, number;
	bool arc = command[0] == 'A';
	bool pie = command[0] == 'I' || command[0] == 'i';
	bool chord = command[0] == '_';
	bool angles = arc || pie || chord || command[0] == 'O' || command[0] == 'V';
	bool circle = arc || command[0] == 'I' || command[0] == 'C' || command[0] == 'G';
	bool filled = pie || chord || command[0] == 'G' || command[0] == 'o';
	bool borders = false;
	size_t count = length-1, fields = angles ? (circle ? 5 : 6) : circle ? 3 : 4;
	size_t width, total, position = 0;
	uint8_t drawing = 0, foreground = 0, background = 0;
	double x, y, rx, ry;
	int status;

	if (memchr(command+1, ' ', count) != NULL)
		return IO_ERROR_NOT_SUPPORTED; /* General numeric comments: #7. */
	status = st_rip3_command_environment(host, &environment_number, &environment);
	if (status != 0)
		return status;
	width = environment.coordinate_size;
	total = angles ? (circle ? 3 : 4)*width+4 : fields*width;
	if (count <= total-width || count > total)
		return IO_ERROR_INVALID_FORMAT;
	for (size_t i = 0; i < fields; i++) {
		size_t field_width = angles && (i == 2 || i == 3) ? 2 : width;

		status = st_rip3_command_field(values+i, command+1+position,
		    i == fields-1 ? count-position : field_width, environment.base_math);
		if (status != 0)
			return status;
		position += field_width;
	}
	if (host->styles == NULL || host->ports == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	status = st_rip3_styles_current(host->styles, &number);
	if (status == 0)
		status = st_rip3_styles_get(host->styles, number, &style);
	if (status == 0)
		status = st_rip3_command_frame(host, environment_number, &frame);
	if (status != 0)
		return status;
	x = (double)(values[0] * frame.device_width / frame.world_width);
	y = (double)(values[1] * frame.device_height / frame.world_height);
	rx = (double)(values[angles ? 4 : 2] * frame.device_width / frame.world_width);
	/* The circle radius is horizontal for aspect correction. All supported
	 * 4:3 device canvases have square pixels; both device radii are equal. */
	ry = circle ? rx : (double)(values[angles ? 5 : 3] * frame.device_height / frame.world_height);
	status = st_rip3_ellipse_axes(&ellipse, x,y,rx,ry,0);
	if (status == 0)
		status = st_rip3_ports_current(host->ports, &number);
	if (status == 0)
		status = st_rip3_ports_info(host->ports, number, &port);
	if (status != 0 || !port.viewport_active)
		return status;
	if (angles && command[0] != 'O') {
		status = st_rip3_arc_axis(&interval,(uint64_t)rx,(uint64_t)ry,
		    (unsigned)values[2],(unsigned)values[3]);
		if (status != 0 || interval.kind == ST_RIP3_ARC_EMPTY)
			return status;
	}
	if (chord && interval.kind == ST_RIP3_ARC_POINT)
		return 0;
	/* #75 tracks the conflict between solid-curve prose and the original
	 * logical-pen/pass boundary. Do not guess patterned I/i/_ behavior. */
	if ((pie || chord) && style.pen.pattern != 0xffff)
		return IO_ERROR_NOT_SUPPORTED;
	*operation = true;
	if (interval.kind == ST_RIP3_ARC_POINT) {
		const struct st_rip3_vector points[] = {{x,y},
		    {x+(double)interval.first.x,y+(double)interval.first.y}};
		const struct st_rip3_pen pen = {.pattern=0xffff,.thickness=style.pen.thickness};
		struct st_rip3_stroke_mask mask = {0};

		/* A/V use a point, I/i a center-to-endpoint line even without borders.
		 * The raw endpoint can lie outside a noncircular ellipse. Prepare clipped
		 * coverage first so an invisible operation needs neither ink nor scratch. */
		status = st_rip3_stroke_path_vectors(host->ports,pie ? points : points+1,
		    pie ? 2 : 1,false,&pen,&mask);
		if (status == 0 && mask.coverage != NULL && style.write_mode != ST_RIP3_NOT)
			status = st_rip3_command_ink(host,environment.rgb_bits == 8,style.drawing_color,&drawing);
		if (status == 0)
			status = st_rip3_stroke_apply(host->ports,&mask,drawing,0,style.write_mode);
		st_rip3_stroke_free(&mask);
		return status;
	}
	if (filled) {
		if (host->borders == NULL)
			return IO_ERROR_NOT_SUPPORTED;
		borders = *host->borders;
		if (!borders && (rx == 0 || ry == 0))
			return 0;
		/* Existing G/o behavior is retained. Newly exposed sector borders
		 * accept the unambiguous solid COPY case while #75 is unresolved. */
		if ((pie || chord) && borders && style.write_mode != ST_RIP3_COPY)
			return IO_ERROR_NOT_SUPPORTED;
	}
	if (borders || (!filled && style.write_mode != ST_RIP3_NOT)) {
		status = st_rip3_command_ink(host, environment.rgb_bits == 8, style.drawing_color, &drawing);
		if (status != 0)
			return status;
	}
	if (filled && rx != 0 && ry != 0 && style.write_mode != ST_RIP3_NOT) {
		bool uses_foreground = false, uses_background = false;

		for (size_t i = 0; i < 8; i++) {
			uses_foreground |= style.fill.rows[i] != 0;
			uses_background |= style.fill.rows[i] != 255;
		}
		if (uses_foreground)
			status = st_rip3_command_ink(host, environment.rgb_bits == 8, style.fill_color, &foreground);
		if (status == 0 && uses_background)
			status = st_rip3_command_ink(host, environment.rgb_bits == 8, style.background_color, &background);
		if (status != 0)
			return status;
	}
	if (!filled)
		return st_rip3_draw_ellipse_outline(host->ports, &ellipse, interval.start,interval.sweep,
		    ST_RIP3_ELLIPSE_ARC, style.pen.thickness, drawing, style.write_mode);
	struct st_rip3_stroke_mask radial = {0};
	if (pie && borders && interval.sweep == 360 && rx != 0 && ry != 0) {
		double scale = hypot((double)interval.first.x/rx,(double)interval.first.y/ry);
		const struct st_rip3_vector points[] = {{x,y},
		    {x+(double)interval.first.x/scale,y+(double)interval.first.y/scale}};
		const struct st_rip3_pen pen = {.pattern=0xffff,.thickness=style.pen.thickness};

		/* Different raw endpoints on one ray reach a full Pie, whose border
		 * retains an internal radius. Prepare it before any fill can change pixels.
		 * Repeated coverage is idempotent in the supported COPY border case. */
		status = st_rip3_stroke_path_vectors(host->ports,points,2,false,&pen,&radial);
		if (status != 0)
			return status;
	}
	st_rip3_command_fill(&port,&style.fill,&fill);
	status = st_rip3_draw_filled_ellipse(host->ports, &ellipse, interval.start,interval.sweep,
	    chord ? ST_RIP3_ELLIPSE_CHORD : ST_RIP3_ELLIPSE_PIE, &fill, foreground, background,
	    borders ? style.pen.thickness : 0, drawing, style.write_mode);
	if (status == 0)
		status = st_rip3_stroke_apply(host->ports,&radial,drawing,0,ST_RIP3_COPY);
	st_rip3_stroke_free(&radial);
	return status;
}
