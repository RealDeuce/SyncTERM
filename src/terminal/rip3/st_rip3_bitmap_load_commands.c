/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_command_internal.h"
#include "st_rip3_bitmap_file.h"
#include "st_rip3_bitmap_stamp.h"
#include "st_rip3_environments.h"
#include "st_rip3_palettes.h"
#include "st_rip3_ports.h"
#include "st_pixels.h"
#include "string_compat.h"
#include "io_error.h"

#include <stdlib.h>

int
st_rip3_bitmap_load_commands_execute(const struct st_rip3_command_host *host,
    const uint8_t *command, size_t length, bool *operation)
{
	struct st_rip3_environment environment;
	struct st_rip3_port_info info;
	struct st_rip3_frame frame;
	struct st_rip3_bitmap_destination area;
	struct st_rip3_bitmap *bitmap = NULL;
	struct st_rip3_palette palette;
	uint64_t fields[4], duplicate, mode, flags, transparent, reserved;
	unsigned number, port;
	int status;

	status = st_rip3_command_environment(host,&number,&environment);
	if (status != 0) return status;
	size_t width = environment.coordinate_size, suffix = 2+4*width, prefix = suffix+10;
	if (length <= prefix) return IO_ERROR_INVALID_FORMAT;
	for (size_t i = 0; i < 4; i++) {
		status = st_rip3_command_field(fields+i,command+2+i*width,width,environment.base_math);
		if (status != 0) return status;
		if (fields[i] > INT32_MAX) return IO_ERROR_OUT_OF_RANGE;
	}
	status = st_rip3_command_field(&duplicate,command+suffix,1,environment.base_math);
	if (status == 0) status = st_rip3_command_field(&mode,command+suffix+1,1,environment.base_math);
	if (status == 0) status = st_rip3_command_field(&flags,command+suffix+2,2,environment.base_math);
	if (status == 0) status = st_rip3_command_field(&transparent,command+suffix+4,2,environment.base_math);
	if (status == 0) status = st_rip3_command_field(&reserved,command+suffix+6,4,environment.base_math);
	if (status != 0) return status;
	if (mode > 4 || ((flags&8) != 0 && transparent > 255)
	    || ((flags&1) != 0 && duplicate > 35)) return IO_ERROR_OUT_OF_RANGE;
	bool verbatim = fields[2] == 0 && fields[3] == 0;
	if (!verbatim && (fields[0] > fields[2] || fields[1] > fields[3])) return IO_ERROR_INVALID_FORMAT;
	if (reserved != 0 || (flags&~UINT64_C(15)) != 0) return IO_ERROR_NOT_SUPPORTED;
	if (host->ports == NULL) return IO_ERROR_NOT_SUPPORTED;
	status = st_rip3_ports_current(host->ports,&port);
	if (status == 0) status = st_rip3_ports_info(host->ports,port,&info);
	if (status != 0) return status;
	/* Drawing in a deactivated viewport is parsed but completely ignored. */
	if (!info.viewport_active) return 0;
	if ((flags&4) != 0) return IO_ERROR_NOT_SUPPORTED;
	/* Zero is the clipboard selector, never a literal same-port shortcut. */
	if ((flags&1) != 0 && (duplicate == 0 || duplicate != port)) return IO_ERROR_NOT_SUPPORTED;
	if (host->resources == NULL || host->palettes == NULL
	    || host->bitmap_errors == NULL || host->bitmap_errors->show == NULL) return IO_ERROR_NOT_SUPPORTED;
	status = st_rip3_command_frame(host,number,&frame);
	if (status != 0) return status;
	/* Keep the complete projected rectangle for sampling and error-box sizing.
	 * Wire coordinates fit int32_t and device dimensions are at most 1024. */
	area.x0 = (int64_t)(fields[0]*frame.device_width/frame.world_width);
	area.y0 = (int64_t)(fields[1]*frame.device_height/frame.world_height);
	area.x1 = (int64_t)(fields[2]*frame.device_width/frame.world_width);
	area.y1 = (int64_t)(fields[3]*frame.device_height/frame.world_height);
	char *name = strndup((const char *)command+prefix,length-prefix);
	if (name == NULL) return IO_ERROR_OUT_OF_MEMORY;
	*operation = true;
	status = st_rip3_bitmap_load(&bitmap,host->resources,name);
	if (status != 0) {
		/* Invalid names have not passed the UTF-8 namespace contract. Memory
		 * failures use the resource diagnostic, not a disk-error UI request. */
		if (status != IO_ERROR_INVALID_ARGUMENT && status != IO_ERROR_INVALID_ENCODING
		    && status != IO_ERROR_OUT_OF_MEMORY)
			host->bitmap_errors->show(host->bitmap_errors->context,name,port,verbatim ? NULL : &area);
		goto done;
	}
	if ((flags&2) != 0 && bitmap->palette_size != 0) {
		status = IO_ERROR_NOT_SUPPORTED; goto done;
	}
	if (verbatim) {
		area.x1 = area.x0+bitmap->pixels->width-1;
		area.y1 = area.y0+bitmap->pixels->height-1;
	}
	status = st_rip3_palettes_current(host->palettes,&number);
	if (status == 0) status = st_rip3_palettes_get(host->palettes,number,&palette);
	uint8_t transparent_color = (uint8_t)transparent;
	if (status == 0) status = st_rip3_draw_bitmap(host->ports,bitmap,&area,&palette,
	    (enum st_rip3_transfer_mode)mode,(flags&8) != 0 ? &transparent_color : NULL);
done:
	st_rip3_bitmap_destroy(bitmap); free(name);
	return status;
}
