/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_command_internal.h"
#include "st_rip3_bitmap.h"
#include "st_rip3_environments.h"
#include "st_rip3_palettes.h"
#include "st_rip3_ports.h"
#include "st_rip3_resources.h"
#include "string_compat.h"
#include "io_error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

enum bitmap_source { BITMAP_CLIPBOARD, BITMAP_PORT, BITMAP_VIEWPORT };

static int
write_bitmap(const struct st_rip3_command_host *host, unsigned source,
    const struct st_rip3_rect *area, enum bitmap_source kind,
    const uint8_t *name, size_t name_length)
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
	if (status == 0) {
		if (kind == BITMAP_PORT)
			status = st_rip3_bitmap_capture_port(&bitmap,host->ports,source,area,&palette);
		else if (kind == BITMAP_VIEWPORT)
			status = st_rip3_bitmap_capture_stored_viewport(&bitmap,host->ports,source,area,&palette);
		else
			status = st_rip3_bitmap_capture(&bitmap,host->ports,source,area,&palette);
	}
	if (status == IO_ERROR_NOT_FOUND) status = IO_ERROR_NOT_SUPPORTED;
	if (status == 0) status = st_rip3_bitmap_encode(&bytes,&size,bitmap);
	if (status == 0) status = st_rip3_resource_write(host->resources,filename,bytes,size);
	free(bytes); st_rip3_bitmap_destroy(bitmap); free(filename);
	return status;
}

static int
port_write(const struct st_rip3_command_host *host,
    const struct st_rip3_environment *environment, const uint8_t *command,
    size_t length, bool *operation)
{
	struct st_rip3_port_info info;
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
	if (host->ports == NULL) return IO_ERROR_NOT_SUPPORTED;
	*operation = true;
	status = st_rip3_ports_info(host->ports,(unsigned)source,&info);
	if (status != 0) return status;
	if (!info.defined) return IO_ERROR_NOT_FOUND;
	if (!remainder && (fields[0] >= fields[2] || fields[1] >= fields[3]))
		return IO_ERROR_INVALID_ARGUMENT;
	/* The shipped writer ignores the computed crop for offscreen sources.
	 * Inactive and protected defined sources remain readable in both paths. */
	if (info.offscreen)
		return write_bitmap(host,(unsigned)source,NULL,BITMAP_PORT,command+prefix,length-prefix);
	uint64_t view_width = (uint64_t)(info.viewport.x1-info.viewport.x0)+1;
	uint64_t view_height = (uint64_t)(info.viewport.y1-info.viewport.y0)+1;
	uint64_t right = remainder || fields[2] > view_width ? view_width : fields[2];
	uint64_t bottom = remainder || fields[3] > view_height ? view_height : fields[3];
	if (fields[0] >= right || fields[1] >= bottom) return IO_ERROR_INVALID_ARGUMENT;
	/* 2W fields are raw DEVICE offsets. Convert its half-open far boundaries
	 * to the capture kernel's inclusive endpoints only after independent
	 * clipping; the capture reads relative to the stored viewport. */
	area = (struct st_rip3_rect){
	    (int32_t)fields[0], (int32_t)fields[1],
	    (int32_t)(right-1), (int32_t)(bottom-1)};
	return write_bitmap(host,(unsigned)source,&area,BITMAP_VIEWPORT,command+prefix,length-prefix);
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
	if (command[0] == '2') return port_write(host,&environment,command,length,operation);
	status = st_rip3_command_field(&reserved,command+2,1,environment.base_math);
	if (status != 0) return status;
	if (reserved != 0 || host->clipboard == NULL) return IO_ERROR_NOT_SUPPORTED;
	*operation = true;
	if (!host->clipboard->set) return 0;
	if (host->clipboard->port == 0 || host->clipboard->port > 35) return IO_ERROR_INVALID_ARGUMENT;
	status = write_bitmap(host,host->clipboard->port,NULL,BITMAP_CLIPBOARD,command+3,length-3);
	/* Clipboard lifecycle/export reconciliation is separate. Do not clear or
	 * reassign a dangling selection as an export side effect. */
	return status;
}
