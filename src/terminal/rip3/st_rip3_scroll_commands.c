/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <string.h>

#include "io_error.h"
#include "st_pixels.h"
#include "st_rip3_command_internal.h"
#include "st_rip3_environments.h"
#include "st_rip3_palettes.h"
#include "st_rip3_ports.h"
#include "st_rip3_styles.h"

static int
blit_fill(const struct st_rip3_command_host *host, unsigned bits, unsigned mode,
    struct st_rip3_surface_blit_fill *fill)
{
	*fill = (struct st_rip3_surface_blit_fill) {0};
	if (mode == 0)
		return 0;
	if (mode == 5 || mode == 6) {
		fill->mode = mode == 5 ? ST_RIP3_BLIT_SOLID : ST_RIP3_BLIT_SCROLL_CORNER;
		fill->ink  = ST_PIXEL_COLOR_RGB_TAG;
		return 0;
	}
	struct st_rip3_style style;
	unsigned             current;
	int                  status = st_rip3_styles_current(host->styles, &current);
	if (status == 0)
		status = st_rip3_styles_get(host->styles, current, &style);
	if (status != 0)
		return status;
	uint8_t  ink, background = 0;
	uint32_t color = mode == 1 ? style.drawing_color : mode == 2 ? style.background_color : style.fill_color;
	status         = st_rip3_command_index(host, bits, color, &ink);
	if (status == 0 && mode == 4)
		status = st_rip3_command_index(host, bits, style.background_color, &background);
	if (status != 0)
		return status;
	fill->mode       = mode == 4 ? ST_RIP3_BLIT_PATTERN : ST_RIP3_BLIT_SOLID;
	fill->ink        = ink;
	fill->background = background;
	memcpy(fill->pattern, style.fill.rows, sizeof(fill->pattern));
	return 0;
}

int
st_rip3_scroll_blit_command(const struct st_rip3_command_host *host, const uint8_t *command, size_t length,
    bool *operation)
{
	struct st_rip3_environment environment;
	unsigned                   number, current;
	int                        status = st_rip3_command_environment(host, &number, &environment);
	if (status != 0)
		return status;
	size_t                width       = environment.coordinate_size;
	bool                  scroll      = command[1] == 'G';
	size_t                coordinates = scroll ? 4 : 6;
	struct st_rip3_fields fields      = {command + 2, length - 2};
	uint64_t              positions[6], mode, reserved = 0;
	for (size_t i = 0; status == 0 && i < coordinates; i++)
		status = st_rip3_command_next_field(&fields, &positions[i], width, environment.base_math, false);
	if (status == 0)
		status = st_rip3_command_next_field(&fields, &mode, 1, environment.base_math, false);
	if (status == 0 && (scroll || fields.remaining != 0))
		status = st_rip3_command_next_field(&fields, &reserved, 1, environment.base_math, !scroll);
	if (status == 0 && scroll)
		status = st_rip3_command_next_field(&fields, &positions[5], width, environment.base_math, true);
	if (status != 0)
		return status;
	if (mode > (scroll ? 6u : 5u))
		return IO_ERROR_OUT_OF_RANGE;
	if ((scroll && reserved != 0) || host->ports == NULL || host->palettes == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	struct st_rip3_port_info port;
	status = st_rip3_ports_current(host->ports, &current);
	if (status == 0)
		status = st_rip3_ports_info(host->ports, current, &port);
	if (status != 0 || !port.viewport_active)
		return status;

	struct st_rip3_frame frame;
	status = st_rip3_command_frame(host, number, &frame);
	if (status != 0)
		return status;
	unsigned left = positions[0] <= positions[2] ? 0 : 2;
	unsigned top  = positions[1] <= positions[3] ? 1 : 3;
	if (scroll)
		positions[4] = positions[left];
	const struct st_rip3_surface_blit_area area = {(int64_t)(positions[left] * frame.device_width
	                                                         / frame.world_width),
	    (int64_t)(positions[top] * frame.device_height / frame.world_height),
	    (int64_t)(positions[2 - left] * frame.device_width / frame.world_width),
	    (int64_t)(positions[4 - top] * frame.device_height / frame.world_height),
	    (int64_t)(positions[4] * frame.device_width / frame.world_width),
	    (int64_t)(positions[5] * frame.device_height / frame.world_height)};

	if (area.x0 == area.destination_x && area.y0 == area.destination_y)
		return scroll ? IO_ERROR_INVALID_FORMAT : 0;
	struct st_rip3_surface_blit_fill fill;
	struct st_rip3_palette           palette;
	status = blit_fill(host, environment.rgb_bits, (unsigned)mode, &fill);
	if (status == 0)
		status = st_rip3_palettes_current_palette(host->palettes, &palette);
	if (status != 0)
		return status;
	*operation = true;
	return st_rip3_ports_blit_surface(host->ports, &area, &fill, &palette);
}
