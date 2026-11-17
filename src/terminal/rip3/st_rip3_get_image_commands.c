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
	uint64_t fields[4], reserved = 0;
	unsigned number, current;
	int status = st_rip3_command_environment(host,&number,&environment);

	if (status != 0) return status;
	size_t width = environment.coordinate_size, prefix = 2+4*width;
	if (length <= 2+3*width || length > prefix+1) return IO_ERROR_INVALID_FORMAT;
	for (size_t i = 0; i < 4; i++) {
		size_t count = i == 3 && length < prefix ? length-2-3*width : width;
		status = st_rip3_command_field(fields+i,command+2+i*width,count,environment.base_math);
		if (status != 0) return status;
		if (fields[i] > INT32_MAX) return IO_ERROR_OUT_OF_RANGE;
	}
	if (length > prefix) status = st_rip3_command_field(&reserved,command+prefix,1,environment.base_math);
	if (status != 0) return status;
	if (reserved != 0) return IO_ERROR_NOT_SUPPORTED;
	if (host->ports == NULL) return IO_ERROR_NOT_SUPPORTED;
	status = st_rip3_ports_current(host->ports,&current);
	if (status == 0) status = st_rip3_ports_info(host->ports,current,&port);
	if (status != 0 || !port.viewport_active) return status;
	if (host->clipboard == NULL || host->offscreen_pixel_budget == NULL) return IO_ERROR_NOT_SUPPORTED;
	status = st_rip3_command_frame(host,number,&frame);
	if (status != 0) return status;
	if (fields[0] > fields[2]) { uint64_t swap = fields[0]; fields[0] = fields[2]; fields[2] = swap; }
	if (fields[1] > fields[3]) { uint64_t swap = fields[1]; fields[1] = fields[3]; fields[3] = swap; }
	area.left = (int64_t)(fields[0]*frame.device_width/frame.world_width);
	area.top = (int64_t)(fields[1]*frame.device_height/frame.world_height);
	area.right = (int64_t)(fields[2]*frame.device_width/frame.world_width)+1;
	area.bottom = (int64_t)(fields[3]*frame.device_height/frame.world_height)+1;
	*operation = true;
	return st_rip3_clipboard_capture(host->ports,host->clipboard,&area,*host->offscreen_pixel_budget);
}
