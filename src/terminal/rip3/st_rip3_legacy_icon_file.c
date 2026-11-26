/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "filesystem.h"
#include "io_error.h"
#include "st_pixels.h"
#include "st_rip3_bitmap_file.h"
#include "st_rip3_resources.h"
#include "string_util.h"

const char *
st_rip3_bitmap_legacy_extension(const char *extension)
{
	static const char legacy[][5] = {".ICN", ".HIC", ".MSK"};
	static const char bitmap[][5] = {".BMP", ".BMH", ".BMM"};
	for (size_t i = 0; extension != NULL && i < sizeof(legacy) / sizeof(legacy[0]); ++i)
		if (ascii_case_compare(extension, legacy[i]) == 0)
			return bitmap[i];
	return NULL;
}

static int
read_exact(FILE *stream, void *bytes, size_t size)
{
	if (fread(bytes, 1, size, stream) == size)
		return 0;
	return ferror(stream) ? IO_ERROR_IO : IO_ERROR_INVALID_FORMAT;
}

static int
write_converted_bitmap(const struct st_rip3_resource_paths *paths, const char *name, const uint8_t *bytes,
    size_t length)
{
	char *path     = NULL;
	FILE *previous = NULL;
	int   status   = st_rip3_resource_transfer_path(&path, paths, name, false);
	if (status != 0)
		return status;
	status = fs_open(&previous, path, "rb");
	if (status == IO_ERROR_NOT_FOUND)
		status = 0;
	int64_t old_length = previous == NULL ? 0 : fs_stream_size(previous);
	if (old_length < 0)
		status = (int)old_length;
	if (status == 0 && (uint64_t)old_length > length) {
		/* Keep the old tail observable, as with the original nontruncating
		 * write. Stage the replacement so read/write errors preserve it. */
		struct st_rip3_resource             scratch = {0};
		const struct st_rip3_resource_paths sibling = {.host = paths->host};
		status                                      = fs_stream_seek(previous, (int64_t)length);
		if (status == 0)
			status = st_rip3_resource_scratch(&scratch, &sibling);
		if (status == 0 && fwrite(bytes, 1, length, scratch.stream) != length)
			status = IO_ERROR_IO;
		uint8_t tail[4096];
		for (uint64_t left = (uint64_t)old_length - length; status == 0 && left != 0;) {
			size_t chunk = left < sizeof(tail) ? (size_t)left : sizeof(tail);
			status       = read_exact(previous, tail, chunk);
			if (status == 0 && fwrite(tail, 1, chunk, scratch.stream) != chunk)
				status = IO_ERROR_IO;
			left -= chunk;
		}
		(void)fclose(previous);
		previous = NULL;
		if (scratch.stream != NULL) {
			if (fclose(scratch.stream) != 0 && status == 0)
				status = IO_ERROR_IO;
			scratch.stream = NULL;
		}
		if (status == 0)
			status = fs_rename(scratch.path, path);
		if (status == 0)
			st_rip3_resource_dispose(&scratch);
		else
			st_rip3_resource_remove_scratch(&scratch);
	}
	else {
		if (previous != NULL)
			(void)fclose(previous);
		if (status == 0)
			status = fs_replace_file(path, bytes, length);
	}
	free(path);
	return status;
}

int
st_rip3_bitmap_convert_download(const struct st_rip3_resource_paths *paths, const char *name)
{
	static const uint32_t ega[16] = {
	    0x000000,
	    0x0000aa,
	    0x00aa00,
	    0x00aaaa,
	    0xaa0000,
	    0xaa00aa,
	    0xaa5500,
	    0xaaaaaa,
	    0x555555,
	    0x5555ff,
	    0x55ff55,
	    0x55ffff,
	    0xff5555,
	    0xff55ff,
	    0xffff55,
	    0xffffff,
	};
	if (paths == NULL || !st_rip3_resource_name_valid(name))
		return IO_ERROR_INVALID_ARGUMENT;
	const char *extension = strrchr(name, '.');
	if (st_rip3_bitmap_legacy_extension(extension) == NULL)
		return IO_ERROR_INVALID_ARGUMENT;

	char *source = NULL;
	FILE *input  = NULL;
	int   status = st_rip3_resource_transfer_path(&source, paths, name, true);
	if (status == 0)
		status = fs_open(&input, source, "rb");
	free(source);
	if (status != 0)
		return status;
	int64_t size = fs_stream_size(input);
	if (size < 0)
		status = (int)size;
	else if (size <= 4 || size > 65532)
		status = IO_ERROR_INVALID_FORMAT;
	uint8_t *legacy = status == 0 ? malloc((size_t)size) : NULL;
	if (status == 0)
		status = legacy == NULL ? IO_ERROR_OUT_OF_MEMORY : read_exact(input, legacy, (size_t)size);
	if (fclose(input) != 0 && status == 0)
		status = IO_ERROR_IO;
	struct st_rip3_bitmap *bitmap = NULL;
	if (status == 0)
		status = st_rip3_bitmap_decode_legacy(&bitmap, legacy, (size_t)size, ega);
	free(legacy);
	if (status == 0 && bitmap->pixels->width > 640)
		status = IO_ERROR_OUT_OF_RANGE;
	uint8_t *bytes = NULL;
	size_t   length;
	if (status == 0)
		status = st_rip3_bitmap_encode(&bytes, &length, bitmap);
	st_rip3_bitmap_destroy(bitmap);
	if (status == 0) {
		/* The legacy converter gives biClrUsed an explicit value. */
		bytes[46]          = 16;
		size_t stem        = (size_t)(extension - name);
		char  *destination = malloc(stem + sizeof(".BMP"));
		if (destination == NULL)
			status = IO_ERROR_OUT_OF_MEMORY;
		else {
			for (size_t i = 0; i < stem; ++i)
				destination[i] =
				    name[i] >= 'a' && name[i] <= 'z' ? (char)(name[i] - 'a' + 'A') : name[i];
			memcpy(destination + stem, ".BMP", sizeof(".BMP"));
			status = write_converted_bitmap(paths, destination, bytes, length);
			free(destination);
		}
	}
	free(bytes);
	if (status == 0)
		(void)st_rip3_resource_remove(paths, name);
	return status;
}
