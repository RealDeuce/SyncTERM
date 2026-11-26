/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "filesystem.h"
#include "path.h"
#include "st_rip3_file_query.h"
#include "st_rip3_resources.h"

#define CHECK(expression)                                                                        \
	do {                                                                                     \
		if (!(expression)) {                                                             \
			fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
			return 1;                                                                \
		}                                                                                \
	} while (0)

extern unsigned file_query_open_calls, file_query_metadata_calls;
extern int      file_query_open_error;
extern bool     file_query_metadata_error, file_query_bad_time;
extern int64_t  file_query_length_override;

static int
expect(const struct st_rip3_resource_paths *paths, unsigned mode, const char *name, const char *expected)
{
	char  *reply;
	size_t size;
	int    status = st_rip3_file_query(paths, mode, (const uint8_t *)name, strlen(name), &reply, &size);
	bool   equal  = expected == NULL
	                    ? reply == NULL && size == 0
	                    : reply != NULL && size == strlen(expected) && memcmp(reply, expected, size + 1) == 0;
	free(reply);
	CHECK(status == 0 && equal);
	return 0;
}

int
main(int argc, char **argv)
{
	char   *host = NULL, *shared = NULL, *bitmap = NULL, *fallback = NULL;
	uint8_t bmp[70] = {'B', 'M', [14] = 40, [18] = 9, [22] = 2};

	CHECK(argc == 2);
	CHECK(path_join_alloc(&host, argv[1], "host") == 0);
	CHECK(path_join_alloc(&shared, argv[1], "shared") == 0);
	CHECK(fs_create_directories(host) == 0 && fs_create_directories(shared) == 0);
	CHECK(path_join_alloc(&bitmap, host, "Pixel.bmp") == 0);
	CHECK(path_join_alloc(&fallback, shared, "PIXEL.BMP") == 0);
	CHECK(fs_replace_file(bitmap, bmp, sizeof(bmp)) == 0);
	CHECK(fs_replace_file(fallback, "fallback", 8) == 0);
	const struct st_rip3_resource_paths paths = {.host = host, .shared = shared};

	bool present;
	file_query_open_calls = file_query_metadata_calls = 0;
	CHECK(st_rip3_file_exists(&paths, "c:\\outside\\pixel.icn", &present) == 0 && present);
	CHECK(st_rip3_file_exists(&paths, "junk;pixel.icn.old", &present) == 0 && present);
	CHECK(st_rip3_file_exists(&paths, "Pixel.msk.icn", &present) == 0 && !present);
	CHECK(st_rip3_file_exists(&paths, "Pixel", &present) == 0 && !present);
	CHECK(st_rip3_file_exists(&paths, "Pixel*", &present) == IO_ERROR_INVALID_FORMAT);
	CHECK(file_query_open_calls == 0 && file_query_metadata_calls == 0);

	char *mask = NULL;
	CHECK(path_join_alloc(&mask, host, "PIXEL.BMM") == 0);
	CHECK(fs_replace_file(mask, "", 0) == 0);
	CHECK(st_rip3_file_exists(&paths, "Pixel.msk.icn", &present) == 0 && present);
	CHECK(st_rip3_file_exists(&paths, "Pixel.icn.msk", &present) == 0 && present);
	CHECK(file_query_open_calls == 0 && file_query_metadata_calls == 0);
	CHECK(expect(&paths, 0, "Pixel.msk.icn", "0") == 0);
	CHECK(fs_remove_file(mask) == 0);
	free(mask);

	for (unsigned mode = 0; mode <= 4; mode++) {
		file_query_open_calls = file_query_metadata_calls = 0;
		file_query_open_error                             = IO_ERROR_NOT_FOUND;
		CHECK(expect(&paths, mode, "Pixel.icn", NULL) == 0);
		CHECK(file_query_open_calls == 1 && file_query_metadata_calls == 0);
	}
	file_query_open_calls = file_query_metadata_calls = 0;
	file_query_metadata_error                         = true;
	CHECK(expect(&paths, 0, "Pixel.icn", "1") == 0);
	CHECK(expect(&paths, 1, "Pixel.icn", "1\r") == 0);
	CHECK(file_query_metadata_calls == 0);
	CHECK(expect(&paths, 2, "Pixel.icn", "1.0\r") == 0);
	CHECK(expect(&paths, 3, "Pixel.icn", "1.0.00/00/00.00:00:00\r") == 0);
	CHECK(expect(&paths, 4, "Pixel.icn", "1.PIXEL.ICN:0.00/00/00.00:00:00\r") == 0);
	CHECK(file_query_metadata_calls == 3 && file_query_open_calls == 5);
	file_query_metadata_error = false;
	file_query_bad_time       = true;
	CHECK(expect(&paths, 2, "Pixel.icn", "1.22\r") == 0);
	CHECK(expect(&paths, 3, "Pixel.icn", "1.0.00/00/00.00:00:00\r") == 0);
	CHECK(expect(&paths, 4, "Pixel.icn", "1.PIXEL.ICN:0.00/00/00.00:00:00\r") == 0);
	file_query_bad_time = false;

	file_query_length_override = IO_ERROR_IO;
	CHECK(expect(&paths, 2, "Pixel.bmp", "1.-1\r") == 0);
	CHECK(expect(&paths, 2, "Pixel.icn", "1.22\r") == 0);
	file_query_length_override = (int64_t)INT32_MAX + 1;
	CHECK(expect(&paths, 2, "Pixel.bmp", "1.-1\r") == 0);
	file_query_length_override = INT64_MIN;
	bmp[19]                    = 128;
	CHECK(fs_replace_file(bitmap, bmp, sizeof(bmp)) == 0);
	CHECK(expect(&paths, 2, "Pixel.icn", "1.70\r") == 0);
	bmp[19] = 0;
	bmp[14] = 12;
	CHECK(fs_replace_file(bitmap, bmp, sizeof(bmp)) == 0);
	CHECK(expect(&paths, 2, "Pixel.icn", "1.70\r") == 0);

	CHECK(fs_remove_file(bitmap) == 0);
	CHECK(expect(&paths, 2, "Pixel.bmp", "1.8\r") == 0);
	CHECK(fs_remove_file(fallback) == 0);
	CHECK(expect(&paths, 2, "Pixel.bmp", "0\r") == 0);
	CHECK(fs_remove_directory(host) == 0 && fs_remove_directory(shared) == 0);
	CHECK(fs_remove_directory(argv[1]) == 0);
	free(fallback);
	free(bitmap);
	free(shared);
	free(host);
	return 0;
}
