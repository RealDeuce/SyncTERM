/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "file_map.h"

#include "filesystem.h"
#include "io_error.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
	#include <fcntl.h>
	#include <io.h>
	#include <process.h>
#else
	#include <unistd.h>
#endif

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		    __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static void
make_path(char *result, size_t size, const char *directory, const char *name)
{
	int written = snprintf(result, size, "%s/%s", directory, name);
	CHECK(written >= 0 && (size_t)written < size);
}

static void
write_bytes(const char *path, const void *data, size_t size)
{
	FILE *stream = NULL;

	CHECK(fs_open(&stream, path, "wb") == 0);
	if (stream == NULL)
		return;
	CHECK(fwrite(data, 1, size, stream) == size);
	CHECK(fclose(stream) == 0);
}

static void
test_stream_mapping(const char *path, const unsigned char *contents,
    size_t size)
{
	FILE *stream = NULL;
	file_map_t *map = NULL;
	long position;

	CHECK(fs_open(&stream, path, "r+b") == 0);
	if (stream == NULL)
		return;
	CHECK(fseek(stream, 6, SEEK_SET) == 0);
	position = ftell(stream);
	CHECK(position == 6);
	CHECK(file_map_open_stream(&map, stream) == 0);
	CHECK(map != NULL);
	CHECK(file_map_size(map) == size);
	CHECK(memcmp(file_map_data(map), contents, size) == 0);
	CHECK(ftell(stream) == position);
	file_map_close(map);
	CHECK(fgetc(stream) == 'S');
	CHECK(fclose(stream) == 0);

	/* The mapping must own its lifetime independently of the borrowed stream. */
	map = NULL;
	CHECK(fs_open(&stream, path, "rb") == 0);
	if (stream == NULL)
		return;
	CHECK(file_map_open_stream(&map, stream) == 0);
	CHECK(fclose(stream) == 0);
	CHECK(file_map_size(map) == size);
	CHECK(file_map_data(map) != NULL);
	if (file_map_data(map) != NULL)
		CHECK(memcmp(file_map_data(map), contents, size) == 0);
	file_map_close(map);
}

static void
test_non_file_stream(void)
{
	int descriptors[2];
	int created;
	FILE *stream;
	file_map_t *unchanged = (file_map_t *)(uintptr_t)1;

#ifdef _WIN32
	created = _pipe(descriptors, 256, _O_BINARY);
#else
	created = pipe(descriptors);
#endif
	CHECK(created == 0);
	if (created != 0)
		return;
#ifdef _WIN32
	stream = _fdopen(descriptors[0], "rb");
	CHECK(_close(descriptors[1]) == 0);
#else
	stream = fdopen(descriptors[0], "rb");
	CHECK(close(descriptors[1]) == 0);
#endif
	CHECK(stream != NULL);
	if (stream == NULL)
		return;
	CHECK(file_map_open_stream(&unchanged, stream) ==
	    IO_ERROR_NOT_SUPPORTED);
	CHECK(unchanged == (file_map_t *)(uintptr_t)1);
	CHECK(fclose(stream) == 0);
}

int
main(void)
{
	static const unsigned char contents[] = {
		0x00, 0x01, 0x7f, 0x80, 0xfe, 0xff, 'S', 'T'
	};
	char directory[256];
	char binary_path[512];
	char empty_path[512];
	char unicode_path[512];
#ifdef _WIN32
	long process_id = (long)_getpid();
#else
	long process_id = (long)getpid();
#endif
	int written = snprintf(directory, sizeof(directory),
	    "file-map-test-%ld", process_id);
	file_map_t *map = NULL;
	file_map_t *second = NULL;
	file_map_t *unchanged = (file_map_t *)(uintptr_t)1;

	CHECK(written >= 0 && (size_t)written < sizeof(directory));
	if (written < 0 || (size_t)written >= sizeof(directory))
		return EXIT_FAILURE;
	(void)fs_remove_directory(directory);
	CHECK(fs_create_directory(directory) == 0);
	make_path(binary_path, sizeof(binary_path), directory, "binary.dat");
	make_path(empty_path, sizeof(empty_path), directory, "empty.dat");
	make_path(unicode_path, sizeof(unicode_path), directory,
	    "unicode-\xf0\x9f\x98\x80.dat");

	write_bytes(binary_path, contents, sizeof(contents));
	write_bytes(empty_path, "", 0);
	write_bytes(unicode_path, contents, sizeof(contents));
	test_stream_mapping(binary_path, contents, sizeof(contents));
	test_non_file_stream();

	CHECK(file_map_open(&map, binary_path) == 0);
	CHECK(map != NULL);
	CHECK(file_map_size(map) == sizeof(contents));
	CHECK(file_map_data(map) != NULL);
	CHECK(memcmp(file_map_data(map), contents, sizeof(contents)) == 0);
	CHECK(file_map_open(&second, binary_path) == 0);
	CHECK(second != NULL);
	CHECK(file_map_size(second) == sizeof(contents));
	CHECK(memcmp(file_map_data(second), contents, sizeof(contents)) == 0);
	file_map_close(second);
	file_map_close(map);

	map = NULL;
	CHECK(file_map_open(&map, empty_path) == 0);
	CHECK(map != NULL);
	CHECK(file_map_size(map) == 0);
	CHECK(file_map_data(map) != NULL);
	file_map_close(map);

	map = NULL;
	CHECK(file_map_open(&map, unicode_path) == 0);
	CHECK(map != NULL);
	CHECK(file_map_size(map) == sizeof(contents));
	CHECK(memcmp(file_map_data(map), contents, sizeof(contents)) == 0);
	file_map_close(map);

	CHECK(file_map_open(NULL, binary_path) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(file_map_open(&unchanged, NULL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(unchanged == (file_map_t *)(uintptr_t)1);
	CHECK(file_map_open(&unchanged, "") == IO_ERROR_INVALID_ARGUMENT);
	CHECK(unchanged == (file_map_t *)(uintptr_t)1);
	CHECK(file_map_open(&unchanged, "file-map-does-not-exist") ==
	    IO_ERROR_NOT_FOUND);
	CHECK(unchanged == (file_map_t *)(uintptr_t)1);
	CHECK(file_map_open(&unchanged, directory) == IO_ERROR_IS_DIRECTORY);
	CHECK(unchanged == (file_map_t *)(uintptr_t)1);
	CHECK(file_map_open_stream(NULL, stdin) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(file_map_open_stream(&unchanged, NULL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(unchanged == (file_map_t *)(uintptr_t)1);
	CHECK(file_map_data(NULL) == NULL);
	CHECK(file_map_size(NULL) == 0);
	file_map_close(NULL);

	CHECK(fs_remove_file(unicode_path) == 0);
	CHECK(fs_remove_file(empty_path) == 0);
	CHECK(fs_remove_file(binary_path) == 0);
	CHECK(fs_remove_directory(directory) == 0);
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
