/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_command_internal.h"
#include "st_rip3_bitmap.h"
#include "st_rip3_bitmap_file.h"
#include "st_rip3_environments.h"
#include "st_rip3_palettes.h"
#include "st_rip3_ports.h"
#include "st_rip3_resources.h"
#include "st_pixels.h"
#include "string_compat.h"
#include "io_error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int
scale_clipboard(struct st_rip3_bitmap *bitmap, const struct st_rip3_environments *environments)
{
	struct st_rip3_frame device;
	int status = st_rip3_environments_device(environments, &device);
	if (status != 0)
		return status;
	struct st_pixel_buffer *source = bitmap->pixels;
	uint64_t width = (uint64_t)source->width * 640 / device.device_width;
	uint64_t height = (uint64_t)source->height * 350 / device.device_height;
	if (width == 0 || height == 0 || width > INT32_MAX || height > INT32_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	if (width == source->width && height == source->height)
		return 0;
	struct st_pixel_buffer *pixels;
	status = st_pixel_buffer_create(&pixels, (uint32_t)width, (uint32_t)height, false);
	if (status != 0)
		return status;
	for (uint32_t y = 0; y < pixels->height; ++y) {
		size_t row = ((2 * (uint64_t)y + 1) * source->height / (2 * height)) * source->width;
		for (uint32_t x = 0; x < pixels->width; ++x) {
			size_t column = (2 * (uint64_t)x + 1) * source->width / (2 * width);
			pixels->primary[(size_t)y * pixels->width + x] = source->primary[row + column];
		}
	}
	bitmap->pixels = pixels;
	st_pixel_buffer_destroy(source);
	return 0;
}

static int
write_bitmap(const struct st_rip3_command_host *host, unsigned source,
    const struct st_rip3_rect *area, bool backing_bitmap, bool scale, const char *filename,
    bool temporary)
{
	struct st_rip3_palette palette;
	struct st_rip3_bitmap *bitmap = NULL;
	uint8_t *bytes = NULL;
	size_t size;
	int status;

	if (host->ports == NULL || host->palettes == NULL || host->resources == NULL
	    || host->resources->host == NULL || host->resources->host[0] == 0)
		return IO_ERROR_NOT_SUPPORTED;
	status = st_rip3_palettes_current_palette(host->palettes,&palette);
	if (status == 0) {
		if (backing_bitmap)
			status = st_rip3_bitmap_capture_port(&bitmap,host->ports,source,area,&palette);
		else
			status = st_rip3_bitmap_capture_stored_viewport(&bitmap,host->ports,source,area,&palette);
	}
	if (status == IO_ERROR_NOT_FOUND) status = IO_ERROR_NOT_SUPPORTED;
	if (status == 0 && scale) status = scale_clipboard(bitmap, host->environments);
	if (status == 0) status = st_rip3_bitmap_encode(&bytes,&size,bitmap);
	if (status == 0) {
		if (temporary)
			status = st_rip3_resource_write_temporary(host->resources, filename, bytes, size,
			    host->temporary_resources);
		else
			status = st_rip3_resource_write(host->resources, filename, bytes, size);
	}
	free(bytes); st_rip3_bitmap_destroy(bitmap);
	return status;
}

static int
port_write(const struct st_rip3_command_host *host,
    const struct st_rip3_environment *environment, const uint8_t *command,
    size_t length, bool *operation)
{
	struct st_rip3_port_info info;
	struct st_rip3_rect area;
	uint64_t positions[4], source, flags;
	struct st_rip3_fields fields = {command + 2, length - 2};
	size_t width = environment->coordinate_size;
	unsigned radix = environment->base_math;
	int status = st_rip3_command_next_field(&fields, &source, 1, radix, false);

	for (size_t i = 0; status == 0 && i < 4; i++) {
		status = st_rip3_command_next_field(&fields, &positions[i], width, radix, false);
		if (status == 0 && positions[i] > INT32_MAX) status = IO_ERROR_OUT_OF_RANGE;
	}
	if (status == 0) status = st_rip3_command_next_field(&fields, &flags, 2, radix, false);
	if (status == 0) status = st_rip3_command_next_field(&fields, NULL, 2, radix, false);
	if (status != 0) return status;
	if (fields.remaining == 0) return IO_ERROR_INVALID_FORMAT;
	if (source > 35) return IO_ERROR_OUT_OF_RANGE;
	bool remainder = positions[2] == 0 && positions[3] == 0;
	if (host->ports == NULL) return IO_ERROR_NOT_SUPPORTED;
	*operation = true;
	status = st_rip3_ports_info(host->ports,(unsigned)source,&info);
	if (status != 0) return status;
	if (!info.defined) return IO_ERROR_NOT_FOUND;
	if (!remainder && (positions[0] >= positions[2] || positions[1] >= positions[3]))
		return IO_ERROR_INVALID_ARGUMENT;
	/* The shipped writer ignores the computed crop for offscreen sources.
	 * Inactive and protected defined sources remain readable in both paths. */
	if (!info.offscreen) {
		uint64_t view_width = (uint64_t)(info.viewport.x1-info.viewport.x0)+1;
		uint64_t view_height = (uint64_t)(info.viewport.y1-info.viewport.y0)+1;
		uint64_t right = remainder || positions[2] > view_width ? view_width : positions[2];
		uint64_t bottom = remainder || positions[3] > view_height ? view_height : positions[3];
		if (positions[0] >= right || positions[1] >= bottom) return IO_ERROR_INVALID_ARGUMENT;
		/* 2W uses raw device offsets with half-open far boundaries. */
		area = (struct st_rip3_rect){
		    (int32_t)positions[0], (int32_t)positions[1],
		    (int32_t)(right-1), (int32_t)(bottom-1)};
	}
	char *filename = strndup((const char *)fields.bytes, fields.remaining);
	if (filename == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	status = write_bitmap(host, (unsigned)source, info.offscreen ? NULL : &area,
	    info.offscreen, false, filename, (flags & 1) != 0);
	free(filename);
	return status;
}

int
st_rip3_bitmap_commands_execute(const struct st_rip3_command_host *host,
    const uint8_t *command, size_t length, bool *operation)
{
	struct st_rip3_environment environment;
	uint64_t flags;
	unsigned number;
	int status;

	if (length < 3) return IO_ERROR_INVALID_FORMAT;
	status = st_rip3_command_environment(host,&number,&environment);
	if (status != 0) return status;
	if (command[0] == '2') return port_write(host,&environment,command,length,operation);
	struct st_rip3_fields fields = {command + 2, length - 2};
	status = st_rip3_command_next_field(&fields, &flags, 1, environment.base_math, false);
	if (status != 0) return status;
	const uint8_t *end = memchr(fields.bytes, 0, fields.remaining);
	size_t name_length = end == NULL ? fields.remaining : (size_t)(end - fields.bytes);
	if (name_length == 0) return 0;
	if (host->clipboard == NULL || host->ports == NULL) return IO_ERROR_NOT_SUPPORTED;
	*operation = true;
	if (!host->clipboard->set) return 0;
	if (host->clipboard->port == 0 || host->clipboard->port > 35) return IO_ERROR_INVALID_ARGUMENT;
	struct st_rip3_port_info info;
	status = st_rip3_ports_info(host->ports, host->clipboard->port, &info);
	if (status != 0 || !info.defined) return status;

	char *filename = malloc(name_length + sizeof(".BMP"));
	if (filename == NULL) return IO_ERROR_OUT_OF_MEMORY;
	memcpy(filename, fields.bytes, name_length);
	filename[name_length] = '\0';
	char *extension = strrchr(filename, '.');
	const char *alias = st_rip3_bitmap_legacy_extension(extension);
	if (alias != NULL || extension == NULL)
		memcpy(extension == NULL ? filename + name_length : extension,
		    alias == NULL ? ".BMP" : alias, sizeof(".BMP"));
	bool scale = info.offscreen && (flags & 2) == 0;
	status = write_bitmap(host, host->clipboard->port, NULL,
	    info.offscreen && !scale, scale, filename, (flags & 1) != 0);
	free(filename);
	return status;
}
