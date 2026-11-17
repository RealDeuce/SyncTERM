/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_jpeg_file.h"
#include "st_rip3_resources.h"
#include "st_pixel_image.h"
#include "st_pixels.h"
#include "filesystem.h"
#include "io_error.h"

#include <stdint.h>
#include <stdlib.h>

int
st_rip3_jpeg_load(struct st_pixel_buffer **result,
    const struct st_rip3_resource_paths *paths, const char *name)
{
	struct st_rip3_resource resource = {0};
	struct st_pixel_buffer *image = NULL;
	uint8_t *bytes = NULL;
	int status;

	if (result == NULL || paths == NULL || name == NULL || name[0] == 0) return IO_ERROR_INVALID_ARGUMENT;
	if (!st_pixel_image_jpeg_supported()) return IO_ERROR_NOT_SUPPORTED;
	status = st_rip3_resource_open(&resource,paths,ST_RIP3_RESOURCE_MEDIA,name);
	if (status != 0) return status;
	int64_t physical = fs_stream_size(resource.stream);
	if (physical < 0) { status = (int)physical; goto done; }
	if (physical == 0) { status = IO_ERROR_INVALID_FORMAT; goto done; }
	if ((uint64_t)physical > SIZE_MAX) { status = IO_ERROR_OUT_OF_RANGE; goto done; }
	size_t amount = (size_t)physical;
	bytes = malloc(amount);
	if (bytes == NULL) { status = IO_ERROR_OUT_OF_MEMORY; goto done; }
	if (fread(bytes,1,amount,resource.stream) != amount) {
		status = ferror(resource.stream) ? IO_ERROR_IO : IO_ERROR_INVALID_FORMAT;
		goto done;
	}
	status = st_pixel_image_decode_jpeg(&image,bytes,amount);
done:
	free(bytes);
	if (fclose(resource.stream) != 0 && status == 0) status = IO_ERROR_IO;
	resource.stream = NULL; st_rip3_resource_dispose(&resource);
	if (status == 0) *result = image;
	else st_pixel_buffer_destroy(image);
	return status;
}
