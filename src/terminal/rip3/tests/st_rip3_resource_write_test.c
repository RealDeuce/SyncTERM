/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "filesystem.h"
#include "io_error.h"
#include "path.h"
#include "st_rip3_resources.h"

extern bool st_rip3_resource_test_fail_replace;

#define CHECK(expression)                                                                        \
	do {                                                                                     \
		if (!(expression)) {                                                             \
			fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
			return 1;                                                                \
		}                                                                                \
	} while (0)

static int
expect(const char *directory, const char *name, const void *expected, size_t size)
{
	char   *path   = NULL;
	FILE   *stream = NULL;
	uint8_t bytes[64];
	CHECK(size <= sizeof(bytes));
	CHECK(path_join_alloc(&path, directory, name) == 0);
	CHECK(fs_open(&stream, path, "rb") == 0);
	CHECK(fread(bytes, 1, sizeof(bytes), stream) == size && !ferror(stream));
	CHECK(size == 0 || memcmp(bytes, expected, size) == 0);
	CHECK(fclose(stream) == 0);
	free(path);
	return 0;
}

static int
put(const char *directory, const char *name, const void *bytes, size_t size)
{
	char *path   = NULL;
	int   status = path_join_alloc(&path, directory, name);
	if (status == 0)
		status = fs_replace_file(path, bytes, size);
	free(path);
	return status;
}

static int
remove_file(const char *directory, const char *name)
{
	char *path   = NULL;
	int   status = path_join_alloc(&path, directory, name);
	if (status == 0)
		status = fs_remove_file(path);
	free(path);
	return status;
}

int
main(int argc, char **argv)
{
	char          *host = NULL, *shared = NULL, *fonts = NULL, *outside = NULL;
	char          *collision = NULL, *misconfigured = NULL, *missing_bbs = NULL;
	struct fs_info info;
	CHECK(argc == 3);
	CHECK(path_join_alloc(&host, argv[1], "RIP") == 0);
	CHECK(path_join_alloc(&shared, argv[1], "shared") == 0);
	CHECK(path_join_alloc(&fonts, argv[1], "fonts") == 0);
	CHECK(path_join_alloc(&outside, argv[1], "outside") == 0);
	struct st_rip3_resource_paths paths = {.host = host, .shared = shared, .fonts = fonts};
	/* Literal 1x1 24-bit BMP, including its padded BGR pixel, is written
	 * byte-for-byte. The expected bytes do not come from our BMP encoder. */
	const uint8_t bitmap[] = {
	    'B',
	    'M',
	    58,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    54,
	    0,
	    0,
	    0,
	    40,
	    0,
	    0,
	    0,
	    1,
	    0,
	    0,
	    0,
	    1,
	    0,
	    0,
	    0,
	    1,
	    0,
	    24,
	    0,
	    0,
	    0,
	    0,
	    0,
	    4,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0x33,
	    0x22,
	    0x11,
	    0,
	};
	CHECK(sizeof(bitmap) == 58);
	CHECK(st_rip3_resource_write(&paths, "../outside/Keep.BMP", bitmap, sizeof(bitmap))
	      == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_resource_write(&paths, "\xff.bmp", bitmap, sizeof(bitmap)) == IO_ERROR_INVALID_ENCODING);
	CHECK(fs_get_info(&info, host) == IO_ERROR_NOT_FOUND); /* Invalid input cannot create the hierarchy. */
	CHECK(put(shared, "Tile.BMP", "shared", 6) == 0);
	CHECK(put(fonts, "Tile.BMP", "fonts", 5) == 0);
	CHECK(st_rip3_resource_write(&paths, "Tile.BMP", bitmap, sizeof(bitmap)) == 0);
	CHECK(expect(host, "Tile.BMP", bitmap, sizeof(bitmap)) == 0);
	CHECK(expect(shared, "Tile.BMP", "shared", 6) == 0);
	CHECK(expect(fonts, "Tile.BMP", "fonts", 5) == 0);
	CHECK(st_rip3_resource_write(&paths, "tILE.bmp", "replaced", 8) == 0);
	CHECK(expect(host, "Tile.BMP", "replaced", 8) == 0);
	CHECK(remove_file(host, "Tile.BMP") == 0);
	CHECK(
	    st_rip3_resource_write(&paths, "Empty.DAT", NULL, 0) == 0 && expect(host, "Empty.DAT", NULL, 0) == 0);
	CHECK(remove_file(host, "Empty.DAT") == 0);
	CHECK(st_rip3_resource_write(&paths, "\xce\xa9mega.BMP", bitmap, sizeof(bitmap)) == 0);
	CHECK(st_rip3_resource_write(&paths, "\xcf\x89MEGA.bmp", "unicode", 7) == 0);
	CHECK(expect(host, "\xce\xa9mega.BMP", "unicode", 7) == 0 && remove_file(host, "\xce\xa9mega.BMP") == 0);
	/* Case-sensitive volumes can contain ambiguous caseless names. Detect
	 * the actual volume behavior rather than infer it from the platform. */
	CHECK(put(host, "Twin.BMP", "one", 3) == 0 && put(host, "twin.bmp", "two", 3) == 0);
	char *twin   = NULL;
	FILE *stream = NULL;
	char  content[3];
	CHECK(path_join_alloc(&twin, host, "Twin.BMP") == 0 && fs_open(&stream, twin, "rb") == 0);
	CHECK(fread(content, 1, 3, stream) == 3 && fclose(stream) == 0);
	free(twin);
	if (memcmp(content, "one", 3) == 0) {
		CHECK(st_rip3_resource_write(&paths, "Twin.BMP", "bad", 3) == IO_ERROR_CONFLICT);
		CHECK(st_rip3_resource_write(&paths, "TWIN.BMP", "bad", 3) == IO_ERROR_CONFLICT);
		CHECK(expect(host, "Twin.BMP", "one", 3) == 0 && expect(host, "twin.bmp", "two", 3) == 0);
		CHECK(remove_file(host, "twin.bmp") == 0);
	}
	else
		CHECK(memcmp(content, "two", 3) == 0);
	CHECK(remove_file(host, "Twin.BMP") == 0);
	CHECK(path_join_alloc(&collision, host, "Directory.BMP") == 0 && fs_create_directory(collision) == 0);
	CHECK(st_rip3_resource_write(&paths, "directory.bmp", "bad", 3) == IO_ERROR_CONFLICT);
	CHECK(fs_get_info(&info, collision) == 0 && info.kind == FS_ENTRY_DIRECTORY);
	CHECK(fs_remove_directory(collision) == 0);
	free(collision);
	/* A filesystem replacement failure retains the old file and never
	 * falls back to another resource directory. */
	CHECK(st_rip3_resource_write(&paths, "Tile.BMP", "old", 3) == 0);
	st_rip3_resource_test_fail_replace = true;
	CHECK(st_rip3_resource_write(&paths, "tile.bmp", bitmap, sizeof(bitmap)) == IO_ERROR_IO);
	CHECK(!st_rip3_resource_test_fail_replace && expect(host, "Tile.BMP", "old", 3) == 0);
	CHECK(expect(shared, "Tile.BMP", "shared", 6) == 0);
	CHECK(st_rip3_resource_write(&paths, "tile.bmp", bitmap, sizeof(bitmap)) == 0);
	CHECK(expect(host, "Tile.BMP", bitmap, sizeof(bitmap)) == 0 && remove_file(host, "Tile.BMP") == 0);
	const char *const invalid[] = {NULL, "", ".", "..", "../outside/Keep.BMP", "a/b", "a\\b", "/absolute",
	    "C:thing", "name:stream", "*.BMP", "?.BMP", "a|b", "a<b", "a>b", "a\"b", "a\nb", "tail.", "tail "};
	for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
		CHECK(st_rip3_resource_write(&paths, invalid[i], bitmap, sizeof(bitmap))
		      == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_resource_write(&paths, "\xff.bmp", bitmap, sizeof(bitmap)) == IO_ERROR_INVALID_ENCODING);
	CHECK(st_rip3_resource_write(&paths, "tile.bmp", NULL, 1) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_resource_write(NULL, "tile.bmp", bitmap, sizeof(bitmap)) == IO_ERROR_INVALID_ARGUMENT);
	paths.host = NULL;
	CHECK(st_rip3_resource_write(&paths, "Tile.BMP", bitmap, sizeof(bitmap)) == IO_ERROR_NOT_SUPPORTED);
	paths.host = "";
	CHECK(st_rip3_resource_write(&paths, "Tile.BMP", bitmap, sizeof(bitmap)) == IO_ERROR_NOT_SUPPORTED);
	CHECK(expect(shared, "Tile.BMP", "shared", 6) == 0 && expect(fonts, "Tile.BMP", "fonts", 5) == 0);
	CHECK(path_join_alloc(&missing_bbs, argv[1], "missing-bbs") == 0);
	CHECK(path_join_alloc(&misconfigured, missing_bbs, "RIP") == 0);
	paths.host = misconfigured;
	CHECK(st_rip3_resource_write(&paths, "Tile.BMP", bitmap, sizeof(bitmap)) == IO_ERROR_NOT_FOUND);
	CHECK(fs_get_info(&info, missing_bbs) == IO_ERROR_NOT_FOUND);
	free(misconfigured);
	free(missing_bbs);
	CHECK(path_join_alloc(&misconfigured, argv[1], "NotDirectory") == 0);
	CHECK(fs_replace_file(misconfigured, "file", 4) == 0);
	paths.host = misconfigured;
	CHECK(st_rip3_resource_write(&paths, "Tile.BMP", bitmap, sizeof(bitmap)) != 0);
	CHECK(expect(shared, "Tile.BMP", "shared", 6) == 0 && fs_remove_file(misconfigured) == 0);
	free(misconfigured);
	paths.host = host;
	/* The fixture creates links only where the filesystem supports them. */
	if (strcmp(argv[2], "links") == 0) {
		paths.host = argv[1];
		CHECK(st_rip3_resource_write(&paths, "Link.BMP", bitmap, sizeof(bitmap)) == IO_ERROR_CONFLICT);
		CHECK(st_rip3_resource_write(&paths, "Dangling.BMP", bitmap, sizeof(bitmap)) == IO_ERROR_CONFLICT);
		CHECK(expect(outside, "Keep.BMP", "outside", 7) == 0);
	}
	CHECK(expect(outside, "Keep.BMP", "outside", 7) == 0);
	free(host);
	free(shared);
	free(fonts);
	free(outside);
	return 0;
}
