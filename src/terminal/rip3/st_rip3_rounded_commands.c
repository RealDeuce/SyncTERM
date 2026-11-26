/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "io_error.h"
#include "st_rip3_command_internal.h"
#include "st_rip3_environments.h"
#include "st_rip3_ports.h"
#include "st_rip3_rounded_rectangle_internal.h"
#include "st_rip3_styles.h"

int
st_rip3_rounded_commands_execute(const struct st_rip3_command_host *host, const uint8_t *command, size_t length,
    bool *operation)
{
	struct st_rip3_environment       environment;
	struct st_rip3_frame             frame;
	struct st_rip3_style             style;
	struct st_rip3_port_info         port;
	struct st_rip3_rounded_rectangle rectangle;
	struct st_rip3_fill              fill;
	struct st_rip3_pen               pen;
	uint64_t                         values[5], radius;
	unsigned                         number, current;
	bool                             filled = command[0] == 'u', borders = !filled;
	bool                             foreground_used = false, background_used = false;
	uint32_t                         foreground = 0, background = 0, drawing = 0;
	struct st_rip3_fields            arguments = {command + 1, length - 1};
	int                              status;
	status = st_rip3_command_environment(host, &number, &environment);
	if (status != 0)
		return status;
	size_t width = environment.coordinate_size;
	for (size_t i = 0; i < 5; i++) {
		status = st_rip3_command_next_field(&arguments, values + i, width, environment.base_math, i == 4);
		if (status != 0)
			return status;
	}
	if (host->ports == NULL || host->styles == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	status = st_rip3_command_frame(host, number, &frame);
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
	if (filled) {
		borders = style.borders;
		for (size_t i = 0; i < 8; i++) {
			foreground_used |= style.fill.rows[i] != 0;
			background_used |= style.fill.rows[i] != 255;
		}
	}
	pen = style.pen;
	if (filled) {
		pen.pattern         = 0xffff;
		pen.draw_background = false;
	}
	background_used |= borders && pen.draw_background && pen.pattern != 0xffff;
	*operation = true;
	if (style.write_mode != ST_RIP3_NOT) {
		if (foreground_used)
			status = st_rip3_command_ink(host, environment.rgb_bits, style.fill_color, &foreground);
		if (status == 0 && background_used)
			status =
			    st_rip3_command_ink(host, environment.rgb_bits, style.background_color, &background);
	}
	if (status == 0 && borders && (filled || style.write_mode != ST_RIP3_NOT))
		status = st_rip3_command_ink(host, environment.rgb_bits, style.drawing_color, &drawing);
	if (status != 0)
		return status;
	rectangle.x0 =
	    (double)((values[0] < values[2] ? values[0] : values[2]) * frame.device_width / frame.world_width);
	rectangle.x1 =
	    (double)((values[0] > values[2] ? values[0] : values[2]) * frame.device_width / frame.world_width);
	rectangle.y0 =
	    (double)((values[1] < values[3] ? values[1] : values[3]) * frame.device_height / frame.world_height);
	rectangle.y1 =
	    (double)((values[1] > values[3] ? values[1] : values[3]) * frame.device_height / frame.world_height);
	radius = values[4] * frame.device_width / frame.world_width;
	/* X uses the full converted radius; the shipped Y argument sign-extends
	 * its low WORD before doubling. Negative diameters fit by magnitude. */
	unsigned low      = (unsigned)(radius & 65535);
	double   radius_y = low >= 32768 ? 65536 - low : low;
	rectangle.radius  = fmin((double)radius, (rectangle.x1 - rectangle.x0) / 2);
	radius_y          = fmin(radius_y, (rectangle.y1 - rectangle.y0) / 2);
	st_rip3_command_fill(&port, &style.fill, &fill);
	return st_rip3_rounded_rectangle_render(host->ports, &rectangle, radius_y, borders ? &pen : NULL,
	    filled ? &fill : NULL, foreground, background, drawing, style.write_mode);
}
