/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_pixel_image.h"
#include "filesystem.h"
#include "io_error.h"
#include <stdbool.h>
#include <stdlib.h>

bool st_rip3_jpeg_file_test_fail_allocate;
bool st_rip3_jpeg_file_test_fail_decode;
bool st_rip3_jpeg_file_test_fail_size;
bool st_rip3_jpeg_file_test_fail_close;
bool st_rip3_jpeg_file_test_unavailable;
unsigned st_rip3_jpeg_file_test_read_fault;

static void *
test_malloc(size_t size)
{
	if (st_rip3_jpeg_file_test_fail_allocate) {
		st_rip3_jpeg_file_test_fail_allocate = false;
		return NULL;
	}
	return malloc(size);
}

static bool
test_supported(void)
{
	return !st_rip3_jpeg_file_test_unavailable && st_pixel_image_jpeg_supported();
}

static int
test_decode(struct st_pixel_buffer **result, const void *bytes, size_t size)
{
	if (st_rip3_jpeg_file_test_fail_decode) {
		st_rip3_jpeg_file_test_fail_decode = false;
		return IO_ERROR_OUT_OF_MEMORY;
	}
	return st_pixel_image_decode_jpeg(result,bytes,size);
}

static int64_t
test_size(FILE *stream)
{
	if (st_rip3_jpeg_file_test_fail_size) {
		st_rip3_jpeg_file_test_fail_size = false;
		return IO_ERROR_IO;
	}
	return fs_stream_size(stream);
}

static size_t
test_read(void *bytes, size_t size, size_t count, FILE *stream)
{
	if (st_rip3_jpeg_file_test_read_fault != 0) return 0;
	return fread(bytes,size,count,stream);
}

static int
test_error(FILE *stream)
{
	if (st_rip3_jpeg_file_test_read_fault != 0) {
		bool error = st_rip3_jpeg_file_test_read_fault == 2;
		st_rip3_jpeg_file_test_read_fault = 0;
		return error;
	}
	return ferror(stream);
}

static int
test_close(FILE *stream)
{
	int status = fclose(stream);
	if (st_rip3_jpeg_file_test_fail_close) {
		st_rip3_jpeg_file_test_fail_close = false;
		return EOF;
	}
	return status;
}

#define malloc test_malloc
#define st_pixel_image_jpeg_supported test_supported
#define st_pixel_image_decode_jpeg test_decode
#define fs_stream_size test_size
#define fread test_read
#undef ferror
#define ferror test_error
#define fclose test_close
#include "../st_rip3_jpeg_file.c"
