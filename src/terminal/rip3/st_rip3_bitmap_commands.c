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

int
st_rip3_bitmap_commands_execute(const struct st_rip3_command_host *host,
    const uint8_t *command, size_t length, bool *operation)
{
	struct st_rip3_environment environment;
	struct st_rip3_palette palette;
	struct st_rip3_bitmap *bitmap = NULL;
	uint8_t *bytes = NULL;
	char *filename;
	size_t size;
	uint64_t reserved;
	unsigned number;
	int status;

	if (length < 4) return IO_ERROR_INVALID_FORMAT;
	status = st_rip3_command_environment(host,&number,&environment);
	if (status == 0) status = st_rip3_command_field(&reserved,command+2,1,environment.base_math);
	if (status != 0) return status;
	if (reserved != 0 || host->clipboard == NULL) return IO_ERROR_NOT_SUPPORTED;
	*operation = true;
	if (!host->clipboard->set) return 0;
	if (host->clipboard->port == 0 || host->clipboard->port > 35) return IO_ERROR_INVALID_ARGUMENT;
	if (host->ports == NULL || host->palettes == NULL || host->resources == NULL
	    || host->resources->host == NULL || host->resources->host[0] == 0)
		return IO_ERROR_NOT_SUPPORTED;
	filename = strndup((const char *)command+3,length-3);
	if (filename == NULL) return IO_ERROR_OUT_OF_MEMORY;
	status = st_rip3_palettes_current(host->palettes,&number);
	if (status == 0) status = st_rip3_palettes_get(host->palettes,number,&palette);
	if (status == 0) status = st_rip3_bitmap_capture(&bitmap,host->ports,host->clipboard->port,NULL,&palette);
	/* Clipboard pointer deletion/restore and inactive-source rules remain
	 * under #42. Do not clear/reassign a dangling selection as a side effect. */
	if (status == IO_ERROR_NOT_FOUND) status = IO_ERROR_NOT_SUPPORTED;
	if (status == 0) status = st_rip3_bitmap_encode(&bytes,&size,bitmap);
	if (status == 0) status = st_rip3_resource_write(host->resources,filename,bytes,size);
	free(bytes); st_rip3_bitmap_destroy(bitmap); free(filename);
	return status;
}
