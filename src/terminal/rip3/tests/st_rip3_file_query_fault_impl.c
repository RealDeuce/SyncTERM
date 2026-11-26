/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>

#include "filesystem.h"

unsigned file_query_open_calls, file_query_metadata_calls;
int      file_query_open_error;
bool     file_query_metadata_error, file_query_bad_time;
int64_t  file_query_length_override = INT64_MIN;

static int
test_open(FILE **result, const char *path, const char *mode)
{
	file_query_open_calls++;
	if (file_query_open_error != 0) {
		int error             = file_query_open_error;
		file_query_open_error = 0;
		return error;
	}
	return fs_open(result, path, mode);
}

static int
test_metadata(struct fs_info *result, FILE *stream)
{
	file_query_metadata_calls++;
	if (file_query_metadata_error)
		return IO_ERROR_IO;
	int status = fs_get_stream_info(result, stream);
	if (status == 0 && file_query_bad_time)
		result->modified_time = INT64_MAX;
	return status;
}

static int64_t
test_length(FILE *stream)
{
	return file_query_length_override == INT64_MIN ? fs_stream_size(stream) : file_query_length_override;
}

#define fs_open test_open
#include "../st_rip3_resources.c"
#undef fs_open
#define fs_get_stream_info test_metadata
#define fs_stream_size     test_length
#include "../st_rip3_file_query.c"
