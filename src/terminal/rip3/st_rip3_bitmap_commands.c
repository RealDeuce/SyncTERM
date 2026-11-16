/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_command_internal.h"
#include "st_rip3_bitmap.h"
#include "st_rip3_environments.h"
#include "st_rip3_palettes.h"
#include "st_rip3_ports.h"
#include "st_rip3_resources.h"
#include "string_compat.h"
#include "io_error.h"

#include <stdlib.h>

static int
write_bitmap(const struct st_rip3_command_host *host, unsigned source,
    const struct st_rip3_rect *area, const uint8_t *name, size_t name_length)
{
	struct st_rip3_palette palette;
	struct st_rip3_bitmap *bitmap = NULL;
	uint8_t *bytes = NULL;
	char *filename;
	size_t size;
	unsigned number;
	int status;

	if (host->ports == NULL || host->palettes == NULL || host->resources == NULL
	    || host->resources->host == NULL || host->resources->host[0] == 0)
		return IO_ERROR_NOT_SUPPORTED;
	filename = strndup((const char *)name,name_length);
	if (filename == NULL) return IO_ERROR_OUT_OF_MEMORY;
	status = st_rip3_palettes_current(host->palettes,&number);
	if (status == 0) status = st_rip3_palettes_get(host->palettes,number,&palette);
	if (status == 0) status = st_rip3_bitmap_capture(&bitmap,host->ports,source,area,&palette);
	if (status == IO_ERROR_NOT_FOUND) status = IO_ERROR_NOT_SUPPORTED;
	if (status == 0) status = st_rip3_bitmap_encode(&bytes,&size,bitmap);
	if (status == 0) status = st_rip3_resource_write(host->resources,filename,bytes,size);
	free(bytes); st_rip3_bitmap_destroy(bitmap); free(filename);
	return status;
}

static int32_t
project(uint64_t value, uint32_t device, uint32_t world)
{
	uint64_t coordinate = value*device/world;
	/* Wire values fit int32_t. Saturate only an already-external projection,
	 * retaining it until the documented upper-left outside no-op check. */
	return coordinate > INT32_MAX ? INT32_MAX : (int32_t)coordinate;
}

static int
port_write(const struct st_rip3_command_host *host, unsigned environment_number,
    const struct st_rip3_environment *environment, const uint8_t *command,
    size_t length, bool *operation)
{
	struct st_rip3_port_info info;
	struct st_rip3_frame frame;
	struct st_rip3_rect area;
	uint64_t fields[4], source, reserved;
	size_t width = environment->coordinate_size, prefix = 3+4*width+4;
	int status;

	if (length <= prefix) return IO_ERROR_INVALID_FORMAT;
	status = st_rip3_command_field(&source,command+2,1,environment->base_math);
	for (size_t i = 0; status == 0 && i < 4; i++) {
		status = st_rip3_command_field(fields+i,command+3+i*width,width,environment->base_math);
		if (status == 0 && fields[i] > INT32_MAX) status = IO_ERROR_OUT_OF_RANGE;
	}
	if (status == 0) status = st_rip3_command_field(&reserved,command+3+4*width,4,environment->base_math);
	if (status != 0) return status;
	if (source > 35) return IO_ERROR_OUT_OF_RANGE;
	if (reserved != 0) return IO_ERROR_NOT_SUPPORTED;
	bool remainder = fields[2] == 0 && fields[3] == 0;
	if (!remainder && (fields[0] > fields[2] || fields[1] > fields[3])) return IO_ERROR_INVALID_FORMAT;
	if (host->ports == NULL) return IO_ERROR_NOT_SUPPORTED;
	*operation = true;
	status = st_rip3_ports_info(host->ports,(unsigned)source,&info);
	if (status != 0 || !info.defined) return status;
	if (!info.viewport_active) return IO_ERROR_NOT_SUPPORTED;
	if (remainder && fields[0] == 0 && fields[1] == 0)
		return write_bitmap(host,(unsigned)source,NULL,command+prefix,length-prefix);
	status = st_rip3_command_frame(host,environment_number,&frame);
	if (status != 0) return status;
	area.x0 = project(fields[0],frame.device_width,frame.world_width);
	area.y0 = project(fields[1],frame.device_height,frame.world_height);
	int32_t max_x = info.viewport.x1-info.viewport.x0, max_y = info.viewport.y1-info.viewport.y0;
	if (area.x0 > max_x || area.y0 > max_y) return 0;
	area.x1 = remainder ? max_x : project(fields[2],frame.device_width,frame.world_width);
	area.y1 = remainder ? max_y : project(fields[3],frame.device_height,frame.world_height);
	/* The outside lower-right rule's reset-both versus per-axis clipping
	 * interpretation remains under #10. Do not select it via the generic
	 * device capture kernel's intersection policy. */
	if (area.x1 > max_x || area.y1 > max_y) return IO_ERROR_NOT_SUPPORTED;
	return write_bitmap(host,(unsigned)source,&area,command+prefix,length-prefix);
}

int
st_rip3_bitmap_commands_execute(const struct st_rip3_command_host *host,
    const uint8_t *command, size_t length, bool *operation)
{
	struct st_rip3_environment environment;
	uint64_t reserved;
	unsigned number;
	int status;

	if (length < 4) return IO_ERROR_INVALID_FORMAT;
	status = st_rip3_command_environment(host,&number,&environment);
	if (status != 0) return status;
	if (command[0] == '2') return port_write(host,number,&environment,command,length,operation);
	status = st_rip3_command_field(&reserved,command+2,1,environment.base_math);
	if (status != 0) return status;
	if (reserved != 0 || host->clipboard == NULL) return IO_ERROR_NOT_SUPPORTED;
	*operation = true;
	if (!host->clipboard->set) return 0;
	if (host->clipboard->port == 0 || host->clipboard->port > 35) return IO_ERROR_INVALID_ARGUMENT;
	status = write_bitmap(host,host->clipboard->port,NULL,command+3,length-3);
	/* Clipboard pointer deletion/restore and inactive-source rules remain
	 * under #42. Do not clear/reassign a dangling selection as a side effect. */
	return status;
}
