/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_command_internal.h"
#include "st_rip3_environments.h"
#include "st_rip3_image.h"
#include "st_rip3_jpeg_file.h"
#include "st_rip3_palettes.h"
#include "st_rip3_ports.h"
#include "st_rip3_styles.h"
#include "st_pixels.h"
#include "io_error.h"

#include <stdlib.h>
#include <string.h>

int
st_rip3_image_commands_execute(const struct st_rip3_command_host *host,
    const uint8_t *command, size_t length, bool *operation)
{
	struct st_rip3_environment environment;
	struct st_rip3_port_info port;
	struct st_rip3_style style;
	struct st_rip3_palette palette;
	struct st_pixel_buffer *image = NULL;
	uint64_t reserved;
	unsigned number;
	uint8_t background = 0;
	int status;

	if (length <= 6) return IO_ERROR_INVALID_FORMAT;
	status = st_rip3_command_environment(host,&number,&environment);
	if (status == 0) status = st_rip3_command_field(&reserved,command+2,4,environment.base_math);
	if (status != 0) return status;
	if (reserved != 0) return IO_ERROR_NOT_SUPPORTED;
	/* Unexpanded variables, conditionals, controls and quoting cannot become
	 * literal disk filenames. The enclosing expansion stage is still pending. */
	for (size_t i = 6; i < length; i++) {
		if (strchr("$[\\^`",command[i]) != NULL
		    || (i+1 < length && ((command[i] == '<' && command[i+1] == '<')
		    || (command[i] == '(' && command[i+1] == '(')))) return IO_ERROR_NOT_SUPPORTED;
	}
	if (host->ports == NULL) return IO_ERROR_NOT_SUPPORTED;
	status = st_rip3_ports_current(host->ports,&number);
	if (status == 0) status = st_rip3_ports_info(host->ports,number,&port);
	if (status != 0 || !port.viewport_active) return status;
	/* Disabled-view deletion bits conflict in the reference (#21). Active
	 * images with flag 2 also need resolved delivery/deletion ownership. */
	if ((!port.image.enabled && (port.image.flags&14) != 0) || (port.image.flags&2) != 0)
		return IO_ERROR_NOT_SUPPORTED;
	if (!port.image.enabled) return 0;
	if (host->resources == NULL || host->styles == NULL || host->palettes == NULL) return IO_ERROR_NOT_SUPPORTED;
	status = st_rip3_styles_current(host->styles,&number);
	if (status == 0) status = st_rip3_styles_get(host->styles,number,&style);
	if (status != 0) return status;
	/* #21 leaves the clear/margin write mode unresolved. COPY agrees under
	 * either interpretation. Other modes are unambiguous only when there
	 * is neither a clear nor an aspect-preserving margin operation. */
	if (style.write_mode != ST_RIP3_COPY && ((port.image.flags&4) == 0 || (port.image.flags&1) != 0))
		return IO_ERROR_NOT_SUPPORTED;
	if (status == 0) status = st_rip3_palettes_current(host->palettes,&number);
	if (status == 0) status = st_rip3_palettes_get(host->palettes,number,&palette);
	if (status == 0 && (port.image.flags&4) == 0)
		status = st_rip3_command_ink(host,environment.rgb_bits == 8,style.background_color,&background);
	if (status != 0) return status;
	size_t size = length-6;
	char *name = malloc(size+1);
	if (name == NULL) return IO_ERROR_OUT_OF_MEMORY;
	memcpy(name,command+6,size); name[size] = 0;
	*operation = true;
	status = st_rip3_jpeg_load(&image,host->resources,name);
	if (status == 0) status = st_rip3_draw_image(host->ports,image,NULL,0,&palette,background,style.write_mode);
	st_pixel_buffer_destroy(image); free(name);
	/* Filename validity is syntax, even though storage detects it. All file,
	 * decoder and allocation failures remain nonfatal inside command processing. */
	if (status == IO_ERROR_INVALID_ARGUMENT || status == IO_ERROR_INVALID_ENCODING) *operation = false;
	return status;
}
