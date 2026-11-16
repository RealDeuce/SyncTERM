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
	uint8_t header[14], *bytes = NULL;
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
	status = read_exact(resource.stream,header,sizeof(header));
	if (status != 0) goto done;
	uint32_t declared = (uint32_t)header[2] | (uint32_t)header[3]<<8
	    | (uint32_t)header[4]<<16 | (uint32_t)header[5]<<24;
	if (header[0] != 'B' || header[1] != 'M' || declared < 54 || declared > (uint64_t)physical) {
		status = IO_ERROR_INVALID_FORMAT; goto done;
	}
	size_t amount = (size_t)declared;
	if ((uint64_t)amount != declared) { status = IO_ERROR_OUT_OF_RANGE; goto done; }
	bytes = malloc(amount);
	if (bytes == NULL) { status = IO_ERROR_OUT_OF_MEMORY; goto done; }
	memcpy(bytes,header,sizeof(header));
	status = read_exact(resource.stream,bytes+sizeof(header),amount-sizeof(header));
	if (status == 0) status = st_rip3_bitmap_decode(&bitmap,bytes,amount);
done:
	free(bytes);
	if (fclose(resource.stream) != 0 && status == 0) status = IO_ERROR_IO;
	resource.stream = NULL; st_rip3_resource_dispose(&resource);
	if (status == 0) *result = bitmap;
	else st_rip3_bitmap_destroy(bitmap);
	return status;
}
