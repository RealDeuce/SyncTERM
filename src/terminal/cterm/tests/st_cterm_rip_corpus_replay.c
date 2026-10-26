/* Replay RIP files through the live CTerm path for pixel comparisons. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_term.h"
#include "st_pixels.h"

#include <stdbool.h>
#include <inttypes.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
print_fragment(const uint8_t *bytes, size_t size)
{
	for (size_t i = 0; i < size && i < 64; i++) {
		uint8_t ch = bytes[i];

		if (ch >= 32 && ch <= 126 && ch != '\\')
			putchar(ch);
		else
			printf("\\x%02x", ch);
	}
}

static int
replay(const char *path, const char *capture_path, bool harness_trailer,
    uint64_t snapshot_at, bool legacy_blue_initial, size_t chunk_size,
    bool rip3)
{
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config console_config = {
		.columns = 80, .rows = 25,
		.cell_width = 8, .cell_height = 14,
		.pixel_width = 640, .pixel_height = 350,
	};
	st_console_t *console = NULL;
	st_font_catalog_t *fonts = NULL;
	st_cterm_t *terminal = NULL;
	struct st_pixel_buffer *pixels = NULL;
	FILE *stream = NULL;
	FILE *capture = NULL;
	uint8_t bytes[4096];
	char unicode[4096 * 4];
	uint64_t offset = 0;
	int status = 1;

	if (st_console_create(&console, &display, &console_config) != 0
	    || st_font_catalog_create(&fonts, console) != 0)
		goto done;
	const struct st_cterm_config config = {
		.console = console,
		.fonts = fonts,
		.emulation = ST_CTERM_ANSI_BBS,
		.encoding = ST_CTERM_ENCODING_UTF8,
		.rip_version = rip3 ? ST_CTERM_RIP_3 : ST_CTERM_RIP_1,
		.codepage = ST_CODEPAGE_CP437,
		.rip_directory = getenv("ST_RIP_TEST_DIRECTORY"),
	};
	if (st_cterm_create(&terminal, &config) != 0)
		goto done;
	if (legacy_blue_initial) {
		/* Match the legacy dialer's initial pixel canvas for streams
		 * that do not begin with RIP_RESET_WINDOWS. */
		struct st_pixel_buffer *initial = NULL;
		const struct st_pixel_blit blit = {
			.width = 640, .height = 350,
			.scale_x = 1, .scale_y = 1,
		};

		if (st_pixel_buffer_create(&initial, 640, 350, false) != 0)
			goto done;
		if (st_pixel_buffer_fill(initial, 4, 0) != 0
		    || st_console_blit_pixels(console, initial, NULL, &blit) != 0) {
			st_pixel_buffer_destroy(initial);
			goto done;
		}
		st_pixel_buffer_destroy(initial);
	}
	stream = fopen(path, "rb");
	if (stream == NULL) {
		perror(path);
		goto done;
	}
	for (;;) {
		size_t size = fread(bytes, 1, chunk_size, stream);
		size_t converted = 0;
		size_t consumed = 0;

		if (size == 0)
			break;
		if (snapshot_at != 0 && size > snapshot_at - offset)
			size = (size_t)(snapshot_at - offset);
		status = st_term_rip_wire_to_utf8(unicode, sizeof(unicode),
		    &converted, bytes, size, ST_CODEPAGE_CP437);
		if (status != 0) {
			printf("%s: wire conversion status=%d at offset=%" PRIu64 "\n",
			    path, status, offset);
			status = 1;
			goto done;
		}
		status = st_cterm_write(terminal, unicode, converted, &consumed);
		if (status != 0 || consumed != converted) {
			printf("%s: status=%d wire offset=%" PRIu64
			    " consumed=%zu/%zu UTF-8 bytes: ",
			    path, status, offset, consumed, converted);
			print_fragment(bytes, size);
			putchar('\n');
			status = 1;
			goto done;
		}
		offset += size;
		if (snapshot_at != 0 && offset == snapshot_at)
			break;
	}
	if (snapshot_at != 0 && offset != snapshot_at) {
		fprintf(stderr, "%s: snapshot offset exceeds file size\n", path);
		status = 1;
		goto done;
	}
	if (ferror(stream)) {
		perror(path);
		status = 1;
		goto done;
	}
	if (harness_trailer) {
		/* rip_harness.py's send_rip_file sends these bytes after the
		 * unmodified file; they dispatch its final pending command. */
		static const uint8_t trailer[] = "\n!|#\r\n";
		size_t consumed = 0;

		status = st_cterm_write(terminal, trailer, sizeof(trailer) - 1,
		    &consumed);
		if (status != 0 || consumed != sizeof(trailer) - 1)
			goto done;
	}
	status = snapshot_at == 0 ? st_cterm_finish(terminal) : 0;
	if (status == 0 && capture_path != NULL) {
		const struct st_console_rect rect = {0, 0, 640, 350};
		uint32_t palette[256];

		status = st_console_get_pixels(&pixels, console, &rect, false);
		if (status != 0)
			goto done;
		status = st_console_get_palette(console, 0, palette, 256);
		if (status != 0)
			goto done;
		capture = fopen(capture_path, "wb");
		if (capture == NULL) {
			perror(capture_path);
			status = 1;
			goto done;
		}
		for (size_t i = 0; i < 640u * 350u; i++) {
			uint32_t pixel = pixels->primary[i];
			uint32_t colour;

			if (ST_PIXEL_COLOR_IS_RGB(pixel))
				colour = pixel & UINT32_C(0x00ffffff);
			else if (pixel < 256)
				colour = palette[pixel];
			else {
				fprintf(stderr, "%s: invalid pixel value %#" PRIx32
				    " at (%zu,%zu)\n", path, pixel,
				    i % 640u, i / 640u);
				status = 1;
				goto done;
			}
			uint8_t rgb[3] = {
				(uint8_t)(colour >> 16),
				(uint8_t)(colour >> 8),
				(uint8_t)colour,
			};

			if (fwrite(rgb, 1, sizeof(rgb), capture) != sizeof(rgb)) {
				perror(capture_path);
				status = 1;
				goto done;
			}
		}
		if (fclose(capture) != 0) {
			capture = NULL;
			perror(capture_path);
			status = 1;
			goto done;
		}
		capture = NULL;
	}
	if (status == 0)
		printf("%s: OK (%" PRIu64 " file bytes%s%s)\n", path, offset,
		    harness_trailer ? ", plus harness trailer" : "",
		    snapshot_at != 0 ? ", intermediate snapshot" : "");
	else
		printf("%s: finish status=%d (%" PRIu64 " bytes)\n",
		    path, status, offset);
done:
	if (capture != NULL)
		fclose(capture);
	st_pixel_buffer_destroy(pixels);
	if (stream != NULL)
		fclose(stream);
	st_cterm_destroy(&terminal);
	if (fonts != NULL)
		st_font_catalog_destroy(fonts);
	st_console_destroy(console);
	return status;
}

int
main(int argc, char **argv)
{
	int failures = 0;
	const char *capture_path = NULL;
	bool harness_trailer = false;
	bool legacy_blue_initial = false;
	uint64_t snapshot_at = 0;
	size_t chunk_size = 4096;
	const char *rip_test_version = getenv("ST_RIP_TEST_VERSION");
	bool rip3 = false;
	int first = 1;

	if (rip_test_version != NULL) {
		if (strcmp(rip_test_version, "3") == 0)
			rip3 = true;
		else if (strcmp(rip_test_version, "1") != 0) {
			fprintf(stderr, "invalid ST_RIP_TEST_VERSION: %s\n",
			    rip_test_version);
			return 2;
		}
	}

	if (argc == 5 && (strcmp(argv[1], "--chunk-rgb24") == 0
	    || strcmp(argv[1], "--chunk-legacy-initial-rgb24") == 0)) {
		char *end;
		unsigned long long value;

		errno = 0;
		value = strtoull(argv[2], &end, 10);
		if (errno != 0 || end == argv[2] || *end != '\0'
		    || value == 0 || value > 4096) {
			fprintf(stderr, "invalid chunk size: %s\n", argv[2]);
			return 2;
		}
		chunk_size = (size_t)value;
		capture_path = argv[3];
		legacy_blue_initial = strcmp(argv[1],
		    "--chunk-legacy-initial-rgb24") == 0;
		first = 4;
	}
	else if (argc == 4 && (strcmp(argv[1], "--rgb24") == 0
	    || strcmp(argv[1], "--harness-rgb24") == 0
	    || strcmp(argv[1], "--legacy-initial-rgb24") == 0)) {
		capture_path = argv[2];
		harness_trailer = strcmp(argv[1], "--harness-rgb24") == 0;
		legacy_blue_initial = strcmp(argv[1],
		    "--legacy-initial-rgb24") == 0;
		first = 3;
	}
	else if (argc == 5 && (strcmp(argv[1], "--rgb24-at") == 0
	    || strcmp(argv[1], "--legacy-initial-rgb24-at") == 0)) {
		char *end;
		unsigned long long value;

		errno = 0;
		value = strtoull(argv[3], &end, 10);
		if (errno != 0 || end == argv[3] || *end != '\0'
		    || value == 0 || value > UINT64_MAX) {
			fprintf(stderr, "invalid snapshot offset: %s\n", argv[3]);
			return 2;
		}
		capture_path = argv[2];
		snapshot_at = (uint64_t)value;
		legacy_blue_initial = strcmp(argv[1],
		    "--legacy-initial-rgb24-at") == 0;
		first = 4;
	}
	else if (argc >= 3 && strcmp(argv[1], "--legacy-initial") == 0) {
		legacy_blue_initial = true;
		first = 2;
	}

	if (argc <= first || (capture_path != NULL && argc != first + 1)
	    || (argv[1][0] == '-' && capture_path == NULL
	        && !legacy_blue_initial)) {
		fprintf(stderr, "usage: %s [--legacy-initial | --rgb24 output.rgb24 | --harness-rgb24 output.rgb24 | --legacy-initial-rgb24 output.rgb24 | --chunk-rgb24 size output.rgb24 | --chunk-legacy-initial-rgb24 size output.rgb24 | --rgb24-at output.rgb24 offset | --legacy-initial-rgb24-at output.rgb24 offset] file.rip [file.rip ...]\n", argv[0]);
		return 2;
	}
	for (int i = first; i < argc; i++) {
		if (replay(argv[i], capture_path, harness_trailer,
		    snapshot_at, legacy_blue_initial, chunk_size, rip3) != 0)
			failures++;
	}
	return failures == 0 ? 0 : 1;
}
