/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_bitmap_file.h"
#include "st_rip3_resources.h"
#include "st_rip3_bitmap_stamp.h"
#include "st_rip3_ports.h"
#include "st_pixels.h"
#include "filesystem.h"
#include "path.h"
#include "io_error.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern bool st_rip3_bitmap_file_test_fail_allocate;
extern bool st_rip3_bitmap_file_test_fail_decode;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr,"check failed at line %d: %s\n",__LINE__,#expression); \
		return 1; \
	} \
} while (0)

static int
put(const char *directory, const char *name, const void *bytes, size_t size)
{
	char *path = NULL;
	int status = path_join_alloc(&path,directory,name);
	if (status == 0) status = fs_replace_file(path,bytes,size);
	free(path); return status;
}

static int
expect(const struct st_rip3_resource_paths *paths, const char *name, uint32_t color)
{
	struct st_rip3_bitmap *bitmap = NULL;
	CHECK(st_rip3_bitmap_load(&bitmap,paths,name) == 0);
	CHECK(bitmap->bit_depth == 24 && bitmap->palette_size == 0 && bitmap->pixels->width == 1 && bitmap->pixels->height == 1);
	CHECK(bitmap->pixels->primary[0] == (ST_PIXEL_COLOR_RGB_TAG | color));
	st_rip3_bitmap_destroy(bitmap);
	return 0;
}

static int
failure(const struct st_rip3_resource_paths *paths, const char *name, int error)
{
	struct st_rip3_bitmap marker = {0}, *bitmap = &marker;
	CHECK(st_rip3_bitmap_load(&bitmap,paths,name) == error && bitmap == &marker);
	return 0;
}

static int
load_and_stamp(const struct st_rip3_resource_paths *paths)
{
	struct st_rip3_bitmap *bitmap = NULL;
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_palette palette = {0};
	const struct st_rip3_rect area = {0,0,3,3};
	const struct st_rip3_bitmap_destination destination = {1,1,2,2};
	palette.rgb[7] = 0x445566;
	CHECK(st_rip3_bitmap_load(&bitmap,paths,"Scene") == 0);
	CHECK(st_rip3_ports_create(&ports,ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_ports_define(ports,1,&area,true) == 0 && st_rip3_ports_switch(ports,1) == 0);
	CHECK(st_rip3_draw_bitmap(ports,bitmap,&destination,&palette,ST_RIP3_BITMAP_MATCH,ST_RIP3_TRANSFER_COPY,NULL) == 0);
	st_rip3_bitmap_destroy(bitmap);
	for (int32_t y = 0; y < 4; y++) for (int32_t x = 0; x < 4; x++) {
		uint8_t actual, expected = x >= 1 && x <= 2 && y >= 1 && y <= 2 ? 7 : 0;
		CHECK(st_rip3_ports_read(ports,1,x,y,&actual) == 0 && actual == expected);
	}
	st_rip3_ports_destroy(ports);
	return 0;
}

int
main(int argc, char **argv)
{
	CHECK(argc == 2);
	char *host = NULL, *shared = NULL;
	CHECK(path_join_alloc(&host,argv[1],"host") == 0 && fs_create_directories(host) == 0);
	CHECK(path_join_alloc(&shared,argv[1],"shared") == 0 && fs_create_directories(shared) == 0);
	const struct st_rip3_resource_paths paths = {.host=host,.shared=shared};
	const uint8_t literal[] = {
		'B','M',58,0,0,0,0,0,0,0,54,0,0,0,40,0,0,0,1,0,0,0,1,0,0,0,
		1,0,24,0,0,0,0,0,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0x33,0x22,0x11,0,
	};
	uint8_t bytes[70]; memcpy(bytes,literal,sizeof(literal));
	CHECK(put(shared,"Scene.BmP",literal,sizeof(literal)) == 0 && expect(&paths,"scene",0x112233) == 0);
	bytes[54] = 0x66; bytes[55] = 0x55; bytes[56] = 0x44;
	CHECK(put(host,"sCENE.bmp",bytes,sizeof(literal)) == 0 && expect(&paths,"SCENE.BMP",0x445566) == 0);
	CHECK(put(shared,"\xce\xa9mega.BMP",literal,sizeof(literal)) == 0 && expect(&paths,"\xcf\x89MEGA",0x112233) == 0);
	/* Explicit extensions are verbatim, including a BMP with a legacy name.
	 * Loading never converts/deletes or rewrites the source file. */
	CHECK(put(host,"Picture.ICN",literal,sizeof(literal)) == 0 && expect(&paths,"picture.icn",0x112233) == 0);
	char *path = NULL;
	struct fs_info before, after;
	CHECK(path_join_alloc(&path,host,"Picture.ICN") == 0);
	CHECK(fs_set_modified_time(path,INT64_C(1234567890)) == 0 && fs_get_info(&before,path) == 0);
	CHECK(before.modified_time == INT64_C(1234567890));
	CHECK(expect(&paths,"Picture.ICN",0x112233) == 0 && fs_get_info(&after,path) == 0);
	CHECK(before.size == after.size && before.modified_time == after.modified_time); free(path);
	memcpy(bytes,literal,sizeof(literal)); memset(bytes+58,0xa5,sizeof(bytes)-58);
	CHECK(put(host,"Trailing.BMP",bytes,sizeof(bytes)) == 0 && expect(&paths,"Trailing.BMP",0x112233) == 0);
	CHECK(put(host,"Broken.BMP",NULL,0) == 0 && failure(&paths,"Broken.BMP",IO_ERROR_INVALID_FORMAT) == 0);
	CHECK(put(host,"Broken.BMP",bytes,13) == 0 && failure(&paths,"Broken.BMP",IO_ERROR_INVALID_FORMAT) == 0);
	memcpy(bytes,literal,sizeof(literal)); bytes[0] = 'X';
	CHECK(put(host,"Broken.BMP",bytes,58) == 0 && failure(&paths,"Broken.BMP",IO_ERROR_INVALID_FORMAT) == 0);
	memcpy(bytes,literal,sizeof(literal)); bytes[2] = 0;
	CHECK(put(host,"Size.BMP",bytes,58) == 0 && expect(&paths,"Size.BMP",0x112233) == 0);
	bytes[2] = 55;
	CHECK(put(host,"Size.BMP",bytes,58) == 0 && expect(&paths,"Size.BMP",0x112233) == 0);
	memset(bytes+2,255,4);
	CHECK(put(host,"Size.BMP",bytes,58) == 0 && expect(&paths,"Size.BMP",0x112233) == 0);
	CHECK(put(host,"Broken.BMP",bytes,57) == 0 && failure(&paths,"Broken.BMP",IO_ERROR_INVALID_FORMAT) == 0);
	memcpy(bytes,literal,sizeof(literal)); bytes[30] = 1;
	CHECK(put(host,"Broken.BMP",bytes,58) == 0 && failure(&paths,"Broken.BMP",IO_ERROR_NOT_SUPPORTED) == 0);
	memcpy(bytes,literal,sizeof(literal)); bytes[14] = 12;
	CHECK(put(host,"Broken.BMP",bytes,58) == 0 && failure(&paths,"Broken.BMP",IO_ERROR_NOT_SUPPORTED) == 0);
	memcpy(bytes,literal,sizeof(literal)); bytes[26] = 0;
	CHECK(put(host,"Broken.BMP",bytes,58) == 0 && failure(&paths,"Broken.BMP",IO_ERROR_INVALID_FORMAT) == 0);
	st_rip3_bitmap_file_test_fail_decode = true;
	CHECK(failure(&paths,"Scene.BMP",IO_ERROR_OUT_OF_MEMORY) == 0 && !st_rip3_bitmap_file_test_fail_decode);
	st_rip3_bitmap_file_test_fail_allocate = true;
	CHECK(failure(&paths,"Scene.BMP",IO_ERROR_OUT_OF_MEMORY) == 0 && !st_rip3_bitmap_file_test_fail_allocate);
	st_rip3_bitmap_file_test_fail_allocate = true;
	CHECK(failure(&paths,"Scene",IO_ERROR_OUT_OF_MEMORY) == 0 && !st_rip3_bitmap_file_test_fail_allocate);
	CHECK(expect(&paths,"Scene",0x445566) == 0);
	CHECK(failure(&paths,"missing",IO_ERROR_NOT_FOUND) == 0);
	CHECK(failure(&paths,"../Scene.BMP",IO_ERROR_INVALID_ARGUMENT) == 0);
	CHECK(failure(&paths,"*.BMP",IO_ERROR_INVALID_ARGUMENT) == 0);
	CHECK(failure(&paths,"\xff.bmp",IO_ERROR_INVALID_ENCODING) == 0);
	CHECK(failure(&paths,NULL,IO_ERROR_INVALID_ARGUMENT) == 0);
	CHECK(st_rip3_bitmap_load(NULL,&paths,"Scene") == IO_ERROR_INVALID_ARGUMENT);
	CHECK(failure(NULL,"Scene",IO_ERROR_INVALID_ARGUMENT) == 0);
	CHECK(load_and_stamp(&paths) == 0);
	free(host); free(shared);
	return 0;
}
