/* Live RIP icon capture, file write, and file load. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_cterm.h"
#include "cterm_rip_icon_file.h"

#include "filesystem.h"
#include "io_error.h"
#include "path.h"
#include "st_pixels.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef RIP_ASSET_TEST_DIRECTORY
#error RIP_ASSET_TEST_DIRECTORY is required
#endif

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
		return 1; \
	} \
} while (0)

struct response_capture {
	char bytes[512];
	size_t size;
	int count;
};

static void
capture_response(void *context, enum st_cterm_output_origin origin,
    const uint8_t *bytes, size_t size)
{
	struct response_capture *capture = context;

	if (origin != ST_CTERM_OUTPUT_RESPONSE)
		return;
	capture->count++;
	if (size > sizeof(capture->bytes) - capture->size)
		return;
	memcpy(capture->bytes + capture->size, bytes, size);
	capture->size += size;
}

static bool
date_valid(const char *date)
{
	for (size_t i = 0; i < 17; i++) {
		if (i == 2 || i == 5) {
			if (date[i] != '/') return false;
		}
		else if (i == 8) {
			if (date[i] != '.') return false;
		}
		else if (i == 11 || i == 14) {
			if (date[i] != ':') return false;
		}
		else if (date[i] < '0' || date[i] > '9')
			return false;
	}
	return true;
}

static int
feed(st_cterm_t *terminal, const char *wire)
{
	size_t consumed;
	size_t size = strlen(wire);
	int status = st_cterm_write(terminal, wire, size, &consumed);

	return status == 0 && consumed == size ? 0 : 1;
}

static int
pixel(st_console_t *console, uint32_t x, uint32_t y,
    uint32_t *colour)
{
	const struct st_console_rect rect = {x, y, 1, 1};
	struct st_pixel_buffer *pixels = NULL;
	int status = st_console_get_pixels(&pixels, console, &rect, false);

	if (status == 0) {
		*colour = pixels->primary[0];
		st_pixel_buffer_destroy(pixels);
	}
	return status;
}

int
main(void)
{
	struct response_capture response = {0};
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config console_config = {
		.columns = 80, .rows = 43,
		.cell_width = 8, .cell_height = 8,
		.pixel_width = 640, .pixel_height = 350,
	};
	const struct st_cterm_config config = {
		.emulation = ST_CTERM_ANSI_BBS,
		.encoding = ST_CTERM_ENCODING_CODEPAGE,
		.graphics_mode = ST_CTERM_GRAPHICS_RIP_3,
		.codepage = ST_CODEPAGE_CP437,
		.rip_directory = RIP_ASSET_TEST_DIRECTORY,
		.output = capture_response,
		.output_context = &response,
	};
	st_console_t *console = NULL;
	st_font_catalog_t *fonts = NULL;
	st_cterm_t *terminal = NULL;
	struct st_cterm_config terminal_config = config;
	struct fs_info info;
	char *saved = NULL, *escaped = NULL, *subdir = NULL;
	char *scene = NULL, *loop = NULL, *escaped_scene = NULL;
	char *error_icon = NULL;
	uint32_t colour;
	uint8_t bytes[16];
	FILE *stream;
	int status;

	CHECK(path_join_alloc(&saved, RIP_ASSET_TEST_DIRECTORY,
	    "SAVED.ICN") == 0);
	CHECK(path_join_alloc(&subdir, RIP_ASSET_TEST_DIRECTORY,
	    "SUBDIR") == 0);
	CHECK(path_join_alloc(&escaped, RIP_ASSET_TEST_DIRECTORY,
	    "../ESCAPE.ICN") == 0);
	CHECK(path_join_alloc(&scene, RIP_ASSET_TEST_DIRECTORY,
	    "CHILD.RIP") == 0);
	CHECK(path_join_alloc(&loop, RIP_ASSET_TEST_DIRECTORY,
	    "LOOP.RIP") == 0);
	CHECK(path_join_alloc(&escaped_scene, RIP_ASSET_TEST_DIRECTORY,
	    "../ESCAPE.RIP") == 0);
	CHECK(path_join_alloc(&error_icon, RIP_ASSET_TEST_DIRECTORY,
	    "ERROR.ICN") == 0);
	status = fs_remove_file(saved);
	CHECK(status == 0 || status == IO_ERROR_NOT_FOUND);
	CHECK(st_console_create(&console, &display, &console_config) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	terminal_config.console = console;
	terminal_config.fonts = fonts;
	CHECK(st_cterm_create(&terminal, &terminal_config) == 0);
	CHECK(feed(terminal, "!|c0F|X0A0A\r") == 0);
	CHECK(feed(terminal, "!|1C0A0A0A0A00\r") == 0);
	CHECK(feed(terminal, "!|1W0SAVED.ICN\r") == 0);
	CHECK(fs_get_link_info(&info, saved) == 0
	    && info.kind == FS_ENTRY_FILE && info.size == 9);
	CHECK(fs_open(&stream, saved, "rb") == 0);
	CHECK(fread(bytes, 1, 9, stream) == 9 && fclose(stream) == 0);
	CHECK(memcmp(bytes, "\0\0\0\0\200\200\200\200" "F", 9) == 0);
	/* !|1F uses the same real, case-insensitive asset directory. */
	response.size = 0; response.count = 0;
	CHECK(feed(terminal, "!|1F000000saved.icn\r") == 0);
	CHECK(response.count == 1 && response.size == 1
	    && memcmp(response.bytes, "1", 1) == 0);
	response.size = 0; response.count = 0;
	CHECK(feed(terminal, "!|1F010000MISSING.ICN\r") == 0);
	CHECK(response.count == 1 && response.size == 3
	    && memcmp(response.bytes, "0\r\n", 3) == 0);
	response.size = 0; response.count = 0;
	CHECK(feed(terminal, "!|1F010000SAVED.ICN\r") == 0);
	CHECK(response.count == 1 && response.size == 3
	    && memcmp(response.bytes, "1\r\n", 3) == 0);
	response.size = 0; response.count = 0;
	CHECK(feed(terminal, "!|1F000000MISSING.ICN\r") == 0);
	CHECK(response.count == 1 && response.size == 1
	    && memcmp(response.bytes, "0", 1) == 0);
	response.size = 0; response.count = 0;
	CHECK(feed(terminal, "!|1F020000saved.icn\r") == 0);
	CHECK(response.count == 1 && response.size == 5
	    && memcmp(response.bytes, "1.9\r\n", 5) == 0);
	response.size = 0; response.count = 0;
	CHECK(feed(terminal, "!|1F020000MISSING.ICN\r") == 0);
	CHECK(response.count == 1 && response.size == 3
	    && memcmp(response.bytes, "0\r\n", 3) == 0);
	response.size = 0; response.count = 0;
	CHECK(feed(terminal, "!|1F030000saved.icn\r") == 0);
	CHECK(response.count == 1 && response.size == 23
	    && memcmp(response.bytes, "1.9.", 4) == 0
	    && date_valid(response.bytes + 4)
	    && memcmp(response.bytes + 21, "\r\n", 2) == 0);
	response.size = 0; response.count = 0;
	CHECK(feed(terminal, "!|1F040000saved.icn\r") == 0);
	CHECK(response.count == 1 && response.size == 33
	    && memcmp(response.bytes, "1.saved.icn.9.", 14) == 0
	    && date_valid(response.bytes + 14)
	    && memcmp(response.bytes + 31, "\r\n", 2) == 0);
	response.size = 0; response.count = 0;
	CHECK(feed(terminal, "!|1F000000../ESCAPE.ICN\r") == 0);
	CHECK(response.count == 0 && response.size == 0);
	CHECK(fs_create_directories(subdir) == 0);
	CHECK(fs_get_link_info(&info, subdir) == 0
	    && info.kind == FS_ENTRY_DIRECTORY);
	response.size = 0; response.count = 0;
	CHECK(feed(terminal, "!|1F000000SUBDIR\r") == 0);
	CHECK(response.count == 1 && response.size == 1
	    && memcmp(response.bytes, "1", 1) == 0);
	response.size = 0; response.count = 0;
	CHECK(feed(terminal, "!|1F010000SUBDIR\r") == 0);
	CHECK(response.count == 1 && response.size == 3
	    && memcmp(response.bytes, "1\r\n", 3) == 0);
	{
		char expected[64];
		int length = snprintf(expected, sizeof(expected),
		    "1.%" PRIu64 "\r\n", info.size);

		CHECK(length > 0 && (size_t)length < sizeof(expected));
		response.size = 0; response.count = 0;
		CHECK(feed(terminal, "!|1F020000SUBDIR\r") == 0);
		CHECK(response.count == 1 && response.size == (size_t)length
		    && memcmp(response.bytes, expected, (size_t)length) == 0);
		length = snprintf(expected, sizeof(expected),
		    "1.%" PRIu64 ".", info.size);
		CHECK(length > 0 && (size_t)length < sizeof(expected));
		response.size = 0; response.count = 0;
		CHECK(feed(terminal, "!|1F030000SUBDIR\r") == 0);
		CHECK(response.count == 1
		    && response.size == (size_t)length + 19
		    && memcmp(response.bytes, expected, (size_t)length) == 0
		    && date_valid(response.bytes + length)
		    && memcmp(response.bytes + length + 17, "\r\n", 2) == 0);
		length = snprintf(expected, sizeof(expected),
		    "1.SUBDIR.%" PRIu64 ".", info.size);
		CHECK(length > 0 && (size_t)length < sizeof(expected));
		response.size = 0; response.count = 0;
		CHECK(feed(terminal, "!|1F040000SUBDIR\r") == 0);
		CHECK(response.count == 1
		    && response.size == (size_t)length + 19
		    && memcmp(response.bytes, expected, (size_t)length) == 0
		    && date_valid(response.bytes + length)
		    && memcmp(response.bytes + length + 17, "\r\n", 2) == 0);
	}
	CHECK(feed(terminal, "!|*\r") == 0);
	CHECK(pixel(console, 40, 40, &colour) == 0 && colour == 0x10);
	CHECK(feed(terminal, "!|1I141400010SAVED.ICN\r") == 0);
	CHECK(pixel(console, 40, 40, &colour) == 0 && colour == 0x1f);
	CHECK(feed(terminal, "!|1W0../ESCAPE.ICN\r") == 0);
	CHECK(fs_get_link_info(&info, escaped) == IO_ERROR_NOT_FOUND);
	CHECK(st_cterm_reset_terminal(terminal) == 0);
	CHECK(feed(terminal, "!|1I141400010SAVED.ICN\r") == 0);
	CHECK(pixel(console, 40, 40, &colour) == 0 && colour == 0x1f);
	/* The scene's final command has no terminator; its graphics state
	 * remains in effect after the outer parser resumes. */
	{
		static const char child[] = "!|c03|X0B0B";
		static const char recursive[] =
		    "!|1R00000000LOOP.RIP\r!|c04|X0D0D\r";
		static const char outside[] = "!|c0F|X0E0E\r";

		CHECK(fs_replace_file(scene, child, sizeof(child) - 1) == 0);
		CHECK(fs_replace_file(loop, recursive,
		    sizeof(recursive) - 1) == 0);
		CHECK(fs_replace_file(escaped_scene, outside,
		    sizeof(outside) - 1) == 0);
	}
	CHECK(feed(terminal, "!|1R00000000child.rip\r") == 0);
	CHECK(pixel(console, 11, 11, &colour) == 0 && colour == 0x16);
	CHECK(feed(terminal, "!|X0C0C\r") == 0);
	CHECK(pixel(console, 12, 12, &colour) == 0 && colour == 0x16);
	CHECK(feed(terminal, "!|1R00000000LOOP.RIP\r") == 0);
	CHECK(pixel(console, 13, 13, &colour) == 0 && colour == 0x11);
	CHECK(feed(terminal, "!|1R00000000../ESCAPE.RIP\r") == 0);
	CHECK(pixel(console, 14, 14, &colour) == 0 && colour == 0x10);
	{
		static const char dollar_scene[] = "!|c05|X0G0G";

		CHECK(fs_replace_file(scene, dollar_scene,
		    sizeof(dollar_scene) - 1) == 0);
		CHECK(feed(terminal, "!|$>child.rip$\r") == 0);
		CHECK(pixel(console, 16, 16, &colour) == 0 && colour == 0x15);
		CHECK(feed(terminal, "!|$>../ESCAPE.RIP$\r") == 0);
		CHECK(pixel(console, 14, 14, &colour) == 0 && colour == 0x10);
	}
	CHECK(feed(terminal, "!|c04\r") == 0);
	{
		const struct st_console_rect rect = {80, 80, 300, 38};
		struct st_pixel_buffer *popup = NULL;
		size_t red = 0;

		CHECK(feed(terminal, "!|1I282800110MISSING\r") == 0);
		CHECK(st_console_get_pixels(&popup, console, &rect, false) == 0);
		for (size_t i = 0; i < (size_t)rect.width * rect.height; i++)
			red += popup->primary[i] == 0x19;
		CHECK(red > 20);
		st_pixel_buffer_destroy(popup);
		CHECK(feed(terminal, "!|1W0ERROR.ICN\r") == 0);
		CHECK(fs_get_link_info(&info, error_icon) == 0
		    && info.kind == FS_ENTRY_FILE && info.size > 9);
		CHECK(feed(terminal, "!|X0F0F\r") == 0);
		CHECK(pixel(console, 15, 15, &colour) == 0 && colour == 0x11);
	}
#ifdef RIP_ICON_PATH
	{
		static const uint32_t ega_indexes[16] = {
			0x10, 0x14, 0x12, 0x16, 0x11, 0x15, 0x13, 0x17,
			0x18, 0x1c, 0x1a, 0x1e, 0x19, 0x1d, 0x1b, 0x1f,
		};
		const struct st_console_rect rect = {0, 0, 51, 40};
		struct st_pixel_buffer *screen = NULL;
		uint8_t source[4096], *icon = NULL;
		char *icon_path = NULL;
		uint32_t width, height;
		size_t source_size;

		CHECK(path_join_alloc(&icon_path, RIP_ASSET_TEST_DIRECTORY,
		    "RLIB.ICN") == 0);
		CHECK(fs_copy_file(RIP_ICON_PATH, icon_path) == 0);
		stream = fopen(RIP_ICON_PATH, "rb");
		CHECK(stream != NULL);
		source_size = fread(source, 1, sizeof(source), stream);
		CHECK(ferror(stream) == 0 && feof(stream) != 0);
		CHECK(fclose(stream) == 0);
		CHECK(cterm_rip_icon_decode(&icon, &width, &height,
		    source, source_size) == 0);
		CHECK(width == rect.width && height == rect.height);
		/* DOS RIP names are case-insensitive even on POSIX hosts. */
		CHECK(feed(terminal, "!|1I000000010rlib\r") == 0);
		CHECK(st_console_get_pixels(&screen, console, &rect, false) == 0);
		for (size_t i = 0; i < (size_t)width * height; i++)
			CHECK(screen->primary[i] == ega_indexes[icon[i]]);
		st_pixel_buffer_destroy(screen);
		free(icon);
		free(icon_path);
	}
#endif
	/* A saved RIP scene is a second CP437 input boundary.  Loading it in
	 * a UTF-8 session must render the same non-ASCII text as raw CP437. */
	{
		static const char cp437_scene[] = "!|@0A0A\x82\r";
		const struct st_console_rect rect = {0, 0, 640, 350};
		struct st_pixel_buffer *codepage_pixels = NULL;
		struct st_pixel_buffer *unicode_pixels = NULL;
		size_t painted = 0;

		CHECK(fs_replace_file(scene, cp437_scene,
		    sizeof(cp437_scene) - 1) == 0);
		CHECK(st_cterm_reset_terminal(terminal) == 0);
		CHECK(feed(terminal, "!|1R00000000CHILD.RIP\r") == 0);
		CHECK(st_console_get_pixels(&codepage_pixels, console,
		    &rect, false) == 0);
		for (size_t i = 0; i < (size_t)rect.width * rect.height; i++)
			painted += codepage_pixels->primary[i]
			    != codepage_pixels->primary[0];
		CHECK(painted != 0);
		CHECK(st_cterm_set_encoding(terminal,
		    ST_CTERM_ENCODING_UTF8, ST_CODEPAGE_CP437) == 0);
		CHECK(st_cterm_reset_terminal(terminal) == 0);
		CHECK(feed(terminal, "!|1R00000000CHILD.RIP\r") == 0);
		CHECK(st_console_get_pixels(&unicode_pixels, console,
		    &rect, false) == 0);
		CHECK(memcmp(codepage_pixels->primary, unicode_pixels->primary,
		    (size_t)rect.width * rect.height
		    * sizeof(codepage_pixels->primary[0])) == 0);
		st_pixel_buffer_destroy(unicode_pixels);
		st_pixel_buffer_destroy(codepage_pixels);
	}
	st_cterm_destroy(&terminal);
	st_font_catalog_destroy(fonts);
	st_console_destroy(console);
	free(escaped);
	free(scene);
	free(loop);
	free(escaped_scene);
	free(error_icon);
	free(saved);
	free(subdir);
	return 0;
}
