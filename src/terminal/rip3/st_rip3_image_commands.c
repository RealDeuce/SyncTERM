/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_command_internal.h"
#include "st_rip3_bitmap_file.h"
#include "st_rip3_bitmap_stamp.h"
#include "st_rip3_environments.h"
#include "st_rip3_image.h"
#include "st_rip3_image_style.h"
#include "st_rip3_jpeg_file.h"
#include "st_rip3_palettes.h"
#include "st_rip3_ports.h"
#include "st_pixels.h"
#include "io_error.h"

#include <stdlib.h>
#include <string.h>

static int
image_style(const struct st_rip3_command_host *host, const uint8_t *command,
    size_t length, bool *operation)
{
	struct st_rip3_environment env; struct st_rip3_frame frame;
	unsigned number, current; uint64_t values[4], flags, reserved;
	int status = st_rip3_command_environment(host,&number,&env);
	if (status != 0) return status;
	size_t prefix = 2+4*env.coordinate_size;
	if (length <= prefix || length > prefix+16) return IO_ERROR_INVALID_FORMAT;
	for (size_t i = 0; i < 4; i++) {
		status = st_rip3_command_field(values+i,command+2+i*env.coordinate_size,env.coordinate_size,env.base_math);
		if (status != 0) return status;
		if (values[i] > INT32_MAX) return IO_ERROR_OUT_OF_RANGE;
	}
	size_t flags_size = length-prefix > 4 ? 4 : length-prefix;
	status = st_rip3_command_field(&flags,command+prefix,flags_size,env.base_math);
	if (status != 0) return status;
	if (flags > 63) return IO_ERROR_OUT_OF_RANGE;
	for (size_t i = prefix+4; i < length; i++) {
		/* Reserved is a zero block, not one potentially >64-bit number. */
		status = st_rip3_command_field(&reserved,command+i,1,env.base_math);
		if (status != 0) return status;
		if (reserved != 0) return IO_ERROR_NOT_SUPPORTED;
	}
	if (host->ports == NULL) return IO_ERROR_NOT_SUPPORTED;
	status = st_rip3_ports_current(host->ports,&current);
	if (status == 0) status = st_rip3_command_frame(host,number,&frame);
	if (status != 0) return status;
	const struct st_rip3_rect world = {(int32_t)values[0],(int32_t)values[1],(int32_t)values[2],(int32_t)values[3]};
	*operation = true;
	return st_rip3_image_style_define(host->ports,current,&frame,&world,(unsigned)flags);
}

static int
show_jpeg(const struct st_rip3_command_host *host,
    const uint8_t *filename, size_t length, bool received, bool *operation)
{
	struct st_rip3_port_info port;
	struct st_rip3_palette palette;
	struct st_pixel_buffer *image = NULL;
	unsigned number;
	int status;

	if (host->ports == NULL) return IO_ERROR_NOT_SUPPORTED;
	status = st_rip3_ports_current(host->ports,&number);
	if (status == 0) status = st_rip3_ports_info(host->ports,number,&port);
	if (status != 0 || !port.viewport_active) return status;
	/* This explicit internal inhibit state is not produced by wire 1i's
	 * all-zero coordinates. No image-style bit deletes a skipped JPEG. */
	if (!port.image.enabled && !received) return 0;
	if (host->resources == NULL || host->palettes == NULL) return IO_ERROR_NOT_SUPPORTED;
	status = st_rip3_palettes_current_palette(host->palettes,&palette);
	if (status != 0) return status;
	char *name = malloc(length + 1);
	if (name == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	memcpy(name, filename, length);
	name[length] = '\0';
	*operation = true;
	status = st_rip3_jpeg_load(&image,host->resources,name);
	/* Invalid resource names are syntax; a later presentation/wallpaper
	 * rectangle failure belongs to the operation, after any initial paint. */
	if (status == IO_ERROR_INVALID_ARGUMENT || status == IO_ERROR_INVALID_ENCODING) *operation = false;
	if (status == 0)
		status = received ? st_rip3_draw_received_jpeg(host->ports, image, &palette)
		    : st_rip3_draw_image(host->ports, image, NULL, 0, &palette);
	st_pixel_buffer_destroy(image);
	free(name);
	/* Every service failure remains inside the public void parser boundary. */
	return status;
}

int
st_rip3_command_jpeg(const struct st_rip3_command_host *host,
    const uint8_t *filename, size_t length, bool *operation)
{
	return show_jpeg(host, filename, length, false, operation);
}

int
st_rip3_command_received_jpeg(const struct st_rip3_command_host *host, const char *filename)
{
	bool operation = false;
	return show_jpeg(host, (const uint8_t *)filename, strlen(filename), true, &operation);
}

int
st_rip3_command_bitmap_playback(const struct st_rip3_command_host *host, const char *filename)
{
	struct st_rip3_port_info port;
	struct st_rip3_palette palette;
	struct st_rip3_bitmap *bitmap = NULL;
	unsigned current;
	int status = st_rip3_ports_current(host->ports,&current);
	if (status == 0) status = st_rip3_ports_info(host->ports,current,&port);
	if (status != 0 || !port.viewport_active || !port.image.enabled) return status;
	if (!port.image.automatic && (port.image.area.x1 == 0 || port.image.area.y1 == 0)) return 0;
	if (host->resources == NULL) return IO_ERROR_NOT_SUPPORTED;
	status = st_rip3_palettes_current_palette(host->palettes,&palette);
	if (status == 0) status = st_rip3_bitmap_load(&bitmap,host->resources,filename);
	if (status == 0) status = st_rip3_draw_bitmap_playback(host->ports,&port,bitmap,&palette);
	st_rip3_bitmap_destroy(bitmap);
	return status;
}

int
st_rip3_image_commands_execute(const struct st_rip3_command_host *host,
    const uint8_t *command, size_t length, bool *operation)
{
	struct st_rip3_environment environment;
	uint64_t reserved;
	unsigned number;
	int status;

	if (command[1] == 'i') return image_style(host,command,length,operation);
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
	return st_rip3_command_jpeg(host,command+6,length-6,operation);
}
