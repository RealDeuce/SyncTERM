/* Check RIP codepage/UTF-8 consistency within the modern terminal. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_term.h"
#include "st_pixels.h"

#include <stdio.h>
#include <string.h>

static int
transport_size(FILE *stream, size_t *result)
{
	uint8_t sauce[128];
	long end;
	size_t size, cut = 0;

	if (fseek(stream, 0, SEEK_END) != 0 || (end = ftell(stream)) < 0)
		return 1;
	size = (size_t)end;
	if (size >= sizeof(sauce)) {
		if (fseek(stream, end - (long)sizeof(sauce), SEEK_SET) != 0
		    || fread(sauce, 1, sizeof(sauce), stream) != sizeof(sauce))
			return 1;
		if (memcmp(sauce, "SAUCE00", 7) == 0) {
			cut = sizeof(sauce);
			if (sauce[104] != 0) {
				size_t comment_size = (size_t)sauce[104] * 64 + 5;
				char header[5];

				if (size >= cut + comment_size
				    && fseek(stream, end - (long)(cut + comment_size),
				    SEEK_SET) == 0
				    && fread(header, 1, sizeof(header), stream)
				    == sizeof(header)
				    && memcmp(header, "COMNT", 5) == 0)
					cut += comment_size;
			}
			size -= cut;
			if (size != 0) {
				uint8_t marker;

				if (fseek(stream, (long)(size - 1), SEEK_SET) != 0
				    || fread(&marker, 1, 1, stream) != 1)
					return 1;
				if (marker == 0x1a)
					size--;
			}
		}
	}
	if (fseek(stream, 0, SEEK_SET) != 0)
		return 1;
	*result = size;
	return 0;
}

static int
feed(st_cterm_t *terminal, const void *bytes, size_t size)
{
	size_t consumed;
	int status = st_cterm_write(terminal, bytes, size, &consumed);

	return status == 0 && consumed == size ? 0 : 1;
}

static int
compare_file(const char *path)
{
	static const uint8_t fixture[] =
	    "!|1K|*|W00|S010F|=00000001|c00\r\n"
	    "!|L00000101|@0202\x82\r\n"
	    "!|w0000000000\r\n"
	    "\x1b\x86";
	const char *label = path == NULL ? "built-in RIP stream" : path;
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config screen = {
		.columns = 80, .rows = 43,
		.cell_width = 8, .cell_height = 8,
		.pixel_width = 640, .pixel_height = 350,
	};
	const struct st_console_rect full = {0, 0, 640, 350};
	static const uint8_t trailer[] = "\n!|#\r\n";
	st_console_t *console[2] = {NULL, NULL};
	st_font_catalog_t *fonts[2] = {NULL, NULL};
	st_cterm_t *terminal[2] = {NULL, NULL};
	struct st_pixel_buffer *pixels[2] = {NULL, NULL};
	uint32_t palette[2][256];
	uint8_t wire[4096];
	char unicode[4096 * 4];
	FILE *stream = NULL;
	size_t fixture_offset = 0;
	size_t converted;
	size_t remaining;
	size_t differences = 0, first = 0;
	size_t min_x = 640, min_y = 350, max_x = 0, max_y = 0;
	uint32_t first_left = 0, first_right = 0;
	int result = 2;

	for (size_t i = 0; i < 2; i++) {
		if (st_console_create(&console[i], &display, &screen) != 0
		    || st_font_catalog_create(&fonts[i], console[i]) != 0)
			goto done;
		const struct st_cterm_config config = {
			.console = console[i],
			.fonts = fonts[i],
			.emulation = ST_CTERM_ANSI_BBS,
			.encoding = i == 0 ? ST_CTERM_ENCODING_CODEPAGE
			    : ST_CTERM_ENCODING_UTF8,
			.rip_version = ST_CTERM_RIP_3,
			.codepage = ST_CODEPAGE_CP437,
		};
		if (st_cterm_create(&terminal[i], &config) != 0)
			goto done;
	}
	if (path != NULL) {
		stream = fopen(path, "rb");
		if (stream == NULL) {
			perror(path);
			goto done;
		}
		/* Match rip_harness.py's transmitted bytes on the codepage side.
		 * The UTF-8 side intentionally tests the production wire conversion;
		 * this is not a historical-renderer comparison. */
		if (transport_size(stream, &remaining) != 0)
			goto done;
	}
	else
		remaining = sizeof(fixture) - 1;
	while (remaining != 0) {
		size_t chunk = remaining < sizeof(wire)
		    ? remaining : sizeof(wire);
		size_t size;

		if (stream != NULL)
			size = fread(wire, 1, chunk, stream);
		else {
			memcpy(wire, fixture + fixture_offset, chunk);
			fixture_offset += chunk;
			size = chunk;
		}

		if (size != chunk)
			goto done;
		if (feed(terminal[0], wire, size) != 0
		    || st_term_rip_wire_to_utf8(unicode, sizeof(unicode),
		    &converted, wire, size, ST_CODEPAGE_CP437) != 0
		    || feed(terminal[1], unicode, converted) != 0) {
			fprintf(stderr, "%s: feed failed\n", label);
			goto done;
		}
		remaining -= size;
	}
	if (feed(terminal[0], trailer, sizeof(trailer) - 1) != 0
	    || st_term_rip_wire_to_utf8(unicode, sizeof(unicode),
	    &converted, trailer, sizeof(trailer) - 1, ST_CODEPAGE_CP437) != 0
	    || feed(terminal[1], unicode, converted) != 0
	    || st_cterm_finish(terminal[0]) != 0
	    || st_cterm_finish(terminal[1]) != 0)
		goto done;
	for (size_t i = 0; i < 2; i++) {
		if (st_console_get_pixels(&pixels[i], console[i], &full,
		    false) != 0
		    || st_console_get_palette(console[i], 0, palette[i], 256) != 0)
			goto done;
	}
	for (size_t i = 0; i < 640u * 350u; i++) {
		uint32_t left = palette[0][pixels[0]->primary[i]];
		uint32_t right = palette[1][pixels[1]->primary[i]];

		if (left != right) {
			if (differences++ == 0) {
				first = i;
				first_left = left;
				first_right = right;
			}
			if (i % 640 < min_x)
				min_x = i % 640;
			if (i % 640 > max_x)
				max_x = i % 640;
			if (i / 640 < min_y)
				min_y = i / 640;
			if (i / 640 > max_y)
				max_y = i / 640;
		}
	}
	if (differences != 0)
		printf("%s: %zu pixels differ, first (%zu,%zu) %#x/%#x, box (%zu,%zu)-(%zu,%zu)\n",
		    label, differences, first % 640, first / 640,
		    first_left, first_right, min_x, min_y, max_x, max_y);
	result = differences == 0 ? 0 : 1;
done:
	if (stream != NULL)
		fclose(stream);
	for (size_t i = 0; i < 2; i++) {
		st_pixel_buffer_destroy(pixels[i]);
		st_cterm_destroy(&terminal[i]);
		if (fonts[i] != NULL)
			st_font_catalog_destroy(fonts[i]);
		st_console_destroy(console[i]);
	}
	return result;
}

int
main(int argc, char **argv)
{
	size_t mismatches = 0, errors = 0;
	int files = argc < 2 ? 1 : argc - 1;

	for (int i = 0; i < files; i++) {
		int status = compare_file(argc < 2 ? NULL : argv[i + 1]);

		mismatches += status == 1;
		errors += status == 2;
	}
	fprintf(stderr, "%d files: %zu pixel mismatches, %zu errors\n",
	    files, mismatches, errors);
	return mismatches == 0 && errors == 0 ? 0 : 1;
}
