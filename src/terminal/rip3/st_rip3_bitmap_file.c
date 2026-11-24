/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_bitmap_file.h"
#include "st_rip3_resources.h"
#include "filesystem.h"
#include "io_error.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int
read_exact(FILE *stream, void *bytes, size_t size)
{
	if (fread(bytes,1,size,stream) == size) return 0;
	return ferror(stream) ? IO_ERROR_IO : IO_ERROR_INVALID_FORMAT;
}

int
st_rip3_bitmap_load(struct st_rip3_bitmap **result,
    const struct st_rip3_resource_paths *paths, const char *name)
{
	struct st_rip3_resource resource = {0};
	struct st_rip3_bitmap *bitmap = NULL;
	uint8_t signature[2], *bytes = NULL;
	char *extended = NULL;
	int status;

	if (result == NULL || paths == NULL || name == NULL || name[0] == 0) return IO_ERROR_INVALID_ARGUMENT;
	if (strchr(name,'.') == NULL) {
		size_t length = strlen(name);
		if (length > SIZE_MAX-5) return IO_ERROR_OUT_OF_RANGE;
		extended = malloc(length+5);
		if (extended == NULL) return IO_ERROR_OUT_OF_MEMORY;
		memcpy(extended,name,length); memcpy(extended+length,".BMP",5);
	}
	status = st_rip3_resource_open(&resource,paths,ST_RIP3_RESOURCE_MEDIA,extended == NULL ? name : extended);
	free(extended);
	if (status != 0) return status;
	int64_t physical = fs_stream_size(resource.stream);
	if (physical < 0) { status = (int)physical; goto done; }
	if (physical < 54) { status = IO_ERROR_INVALID_FORMAT; goto done; }
	size_t amount = (size_t)physical;
	if ((uint64_t)amount != (uint64_t)physical) { status = IO_ERROR_OUT_OF_RANGE; goto done; }
	status = read_exact(resource.stream,signature,sizeof(signature));
	if (status != 0) goto done;
	if (signature[0] != 'B' || signature[1] != 'M') {
		status = IO_ERROR_INVALID_FORMAT; goto done;
	}
	bytes = malloc(amount);
	if (bytes == NULL) { status = IO_ERROR_OUT_OF_MEMORY; goto done; }
	memcpy(bytes,signature,sizeof(signature));
	status = read_exact(resource.stream,bytes+sizeof(signature),amount-sizeof(signature));
	if (status == 0) status = st_rip3_bitmap_decode(&bitmap,bytes,amount);
done:
	free(bytes);
	if (fclose(resource.stream) != 0 && status == 0) status = IO_ERROR_IO;
	resource.stream = NULL; st_rip3_resource_dispose(&resource);
	if (status == 0) *result = bitmap;
	else st_rip3_bitmap_destroy(bitmap);
	return status;
}
