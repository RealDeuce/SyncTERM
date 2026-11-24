/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_command_internal.h"
#include "st_rip3_clipboard.h"
#include "st_rip3_environments.h"
#include "st_rip3_ports.h"
#include "io_error.h"

#include <stddef.h>

int
st_rip3_get_image_commands_execute(const struct st_rip3_command_host *host,
    const uint8_t *command, size_t length, bool *operation)
{
	struct st_rip3_environment environment;
	struct st_rip3_frame frame;
	struct st_rip3_port_info port;
	struct st_rip3_clipboard_area area;
	uint64_t positions[4];
	unsigned number, current;
	int status = st_rip3_command_environment(host,&number,&environment);

	if (status != 0) return status;
	struct st_rip3_fields fields = {command + 2, length - 2};
	for (size_t i = 0; i < 4; i++) {
		status = st_rip3_command_next_field(&fields, &positions[i], environment.coordinate_size,
		    environment.base_math, false);
		if (status != 0) return status;
		if (positions[i] > INT32_MAX) return IO_ERROR_OUT_OF_RANGE;
	}
	/* The descriptor requires a fifth field. The handler only uses the rectangle. */
	status = st_rip3_command_next_field(&fields, NULL, 1, environment.base_math, true);
	if (status != 0) return status;
	if (host->ports == NULL) return IO_ERROR_NOT_SUPPORTED;
	status = st_rip3_ports_current(host->ports,&current);
	if (status == 0) status = st_rip3_ports_info(host->ports,current,&port);
	if (status != 0 || !port.viewport_active) return status;
	if (host->clipboard == NULL || host->offscreen_pixel_budget == NULL) return IO_ERROR_NOT_SUPPORTED;
	status = st_rip3_command_frame(host,number,&frame);
	if (status != 0) return status;
	if (positions[0] > positions[2]) { uint64_t swap = positions[0]; positions[0] = positions[2]; positions[2] = swap; }
	if (positions[1] > positions[3]) { uint64_t swap = positions[1]; positions[1] = positions[3]; positions[3] = swap; }
	area.left = (int64_t)(positions[0]*frame.device_width/frame.world_width);
	area.top = (int64_t)(positions[1]*frame.device_height/frame.world_height);
	area.right = (int64_t)(positions[2]*frame.device_width/frame.world_width)+1;
	area.bottom = (int64_t)(positions[3]*frame.device_height/frame.world_height)+1;
	*operation = true;
	return st_rip3_clipboard_capture(host->ports,host->clipboard,&area,*host->offscreen_pixel_budget);
}
