/* Deterministic parser-to-raster streaming benchmark; no connection or GUI. */
/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */

#include "st_cterm.h"
#include "st_time.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", \
		    __FILE__, __LINE__, #expression); \
		exit(EXIT_FAILURE); \
	} \
} while (false)

static uint64_t
run(enum st_cterm_encoding encoding, bool status, size_t chunk, size_t lines)
{
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config geometry = {
		.columns = 80, .rows = 25, .cell_width = 8, .cell_height = 16,
	};
	const struct st_console_rect full = {0, 0, 80, 25};
	st_console_t *console = NULL;
	st_font_catalog_t *fonts = NULL;
	st_cterm_t *terminal = NULL;
	struct st_pixel_buffer *pixels = NULL;
	struct st_console_cell cells[80 * 25];
	uint64_t start, end, checksum = 0;
	/* Non-ASCII bytes exercise the selected decoder without changing width. */
	const char *line = encoding == ST_CTERM_ENCODING_UTF8
	    ? "\033[32m0123456789 abcdefghijklmnopqrstuvwxyz \xc3\xa9 \033[0m\r\n"
	    : "\033[32m0123456789 abcdefghijklmnopqrstuvwxyz \x82 \033[0m\r\n";
	size_t line_size = strlen(line);
	size_t size = lines * line_size;
	char *input = malloc(size);

	CHECK(input != NULL);
	for (size_t i = 0; i < lines; ++i)
		memcpy(input + i * line_size, line, line_size);
	CHECK(st_console_create(&console, &display, &geometry) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	const struct st_cterm_config config = {
		.console = console, .fonts = fonts,
		.emulation = ST_CTERM_ANSI_BBS, .encoding = encoding,
		.codepage = ST_CODEPAGE_CP437, .status_line = status,
		.scrollback_lines = 1000, .quiet = true,
	};
	CHECK(st_cterm_create(&terminal, &config) == 0);
	CHECK(st_time_process_cpu_ns(&start) == 0);
	for (size_t offset = 0; offset < size;) {
		size_t amount = size - offset < chunk ? size - offset : chunk;
		size_t consumed;
		CHECK(st_cterm_write(terminal, input + offset, amount, &consumed) == 0);
		CHECK(consumed == amount);
		offset += consumed;
	}
	CHECK(st_console_present(console) == 0);
	CHECK(st_time_process_cpu_ns(&end) == 0);
	CHECK(st_console_read_cells(console, &full, cells, 80) == 0);
	for (size_t i = 0; i < sizeof(cells) / sizeof(cells[0]); ++i) {
		const unsigned char *bytes = (const unsigned char *)&cells[i];
		for (size_t j = 0; j < sizeof(cells[i]); ++j)
			checksum = checksum * 33 + bytes[j];
	}
	const struct st_console_rect raster = {0, 0, 640, 400};
	CHECK(st_console_get_pixels(&pixels, console, &raster, false) == 0);
	for (size_t i = 0; i < (size_t)640 * 400; ++i) {
		checksum = checksum * 33 + pixels->primary[i];
		checksum = checksum * 33 + pixels->alternate[i];
	}
	printf("%s status=%d chunk=%zu: %.3f CPU ms, %.0f lines/CPU-s, checksum=%" PRIu64 "\n",
	    encoding == ST_CTERM_ENCODING_UTF8 ? "UTF-8" : "CP437", status,
	    chunk, (double)(end - start) / 1000000.0,
	    end > start ? (double)lines * 1000000000.0 / (double)(end - start)
	    : 0.0, checksum);
	st_pixel_buffer_destroy(pixels);
	st_cterm_destroy(&terminal);
	st_font_catalog_destroy(fonts);
	st_console_destroy(console);
	free(input);
	return checksum;
}

int
st_cterm_benchmark_main(int argc, char **argv)
{
	size_t lines = 2000;
	if (argc == 2) {
		char *end;
		unsigned long count = strtoul(argv[1], &end, 10);
		CHECK(*argv[1] != '\0' && *end == '\0' && count > 0 && count <= 1000000);
		lines = (size_t)count;
	}
	CHECK(argc <= 2);
	for (int encoding = ST_CTERM_ENCODING_UTF8;
	    encoding <= ST_CTERM_ENCODING_CODEPAGE; ++encoding) {
		for (int status = 0; status <= 1; ++status) {
			uint64_t batched = run((enum st_cterm_encoding)encoding,
			    status != 0, 4096, lines);
			CHECK(run((enum st_cterm_encoding)encoding, status != 0,
			    1, lines) == batched);
		}
	}
	return EXIT_SUCCESS;
}
