/* RIP_DEFINE variable persistence and query expansion. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "filesystem.h"
#include "ini.h"
#include "io_error.h"
#include "path.h"
#include "st_cterm.h"
#include "st_pixels.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef RIP_VARIABLES_TEST_DIRECTORY
#error RIP_VARIABLES_TEST_DIRECTORY must name a test-owned build directory
#endif

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
		return 1; \
	} \
} while (0)

struct capture {
	char bytes[128];
	size_t size;
	int prompts;
	bool unicode_prompt;
	int prompt_status;
};

static void
output(void *context, enum st_cterm_output_origin origin,
    const uint8_t *bytes, size_t size)
{
	struct capture *capture = context;

	if (origin == ST_CTERM_OUTPUT_USER
	    && size <= sizeof(capture->bytes) - capture->size) {
		memcpy(capture->bytes + capture->size, bytes, size);
		capture->size += size;
	}
}

static int
prompt(void *context, const char *question, const char *initial,
    size_t width, bool reject_blank, char **result)
{
	struct capture *capture = context;
	const char *answer = capture->unicode_prompt ? "e\xcc\x81" : "Typed";

	capture->prompts++;
	if (capture->prompt_status != 0) return capture->prompt_status;
	if (strcmp(question, capture->unicode_prompt ? "Accent" : "Question") != 0
	    || strcmp(initial, capture->unicode_prompt ? "A" : "Start") != 0
	    || width != (capture->unicode_prompt ? 1U : 5U)
	    || reject_blank)
		return 1;
	*result = malloc(strlen(answer) + 1);
	if (*result == NULL)
		return 1;
	memcpy(*result, answer, strlen(answer) + 1);
	return 0;
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
query(st_cterm_t *terminal, struct capture *capture, const char *name,
    const char *expected)
{
	char wire[64];
	int size = snprintf(wire, sizeof(wire), "!|1\x1b" "0000$%s$^M\r\n",
	    name);

	if (size < 0 || (size_t)size >= sizeof(wire))
		return 1;
	capture->size = 0;
	if (feed(terminal, wire) != 0)
		return 1;
	return capture->size == strlen(expected)
	    && memcmp(capture->bytes, expected, capture->size) == 0 ? 0 : 1;
}

static int
pixel_colour(st_console_t *console, uint32_t x, uint32_t y,
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

static int
read_values(const char *path, const char *name, const char *expected)
{
	ini_document_t *document = NULL;
	FILE *stream = NULL;
	const char *value;
	int status;

	status = fs_open(&stream, path, "rb");
	if (status != 0)
		return 1;
	status = ini_document_read(&document, stream);
	fclose(stream);
	if (status != 0)
		return 1;
	value = ini_section_find(ini_document_root(document), name);
	status = value != NULL && strcmp(value, expected) == 0 ? 0 : 1;
	ini_document_free(&document);
	return status;
}

int
main(void)
{
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config screen = {
		.columns = 80, .rows = 43,
		.cell_width = 8, .cell_height = 8,
		.pixel_width = 640, .pixel_height = 350,
	};
	static const char seed[] = "NAME = One\nKEEP = Value\n";
	st_console_t *console = NULL;
	st_font_catalog_t *fonts = NULL;
	st_cterm_t *terminal = NULL;
	struct capture capture = {0};
	char *path = NULL;

	CHECK(fs_create_directories(RIP_VARIABLES_TEST_DIRECTORY) == 0);
	CHECK(path_join_alloc(&path, RIP_VARIABLES_TEST_DIRECTORY,
	    "variables.ini") == 0);
	CHECK(fs_replace_file(path, seed, sizeof(seed) - 1) == 0);
	CHECK(st_console_create(&console, &display, &screen) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	const struct st_cterm_config config = {
		.console = console,
		.fonts = fonts,
		.emulation = ST_CTERM_ANSI_BBS,
		.encoding = ST_CTERM_ENCODING_UTF8,
		.rip_version = ST_CTERM_RIP_1,
		.rip_directory = RIP_VARIABLES_TEST_DIRECTORY,
		.codepage = ST_CODEPAGE_CP437,
		.output = output,
		.output_context = &capture,
		.rip_prompt = prompt,
		.rip_prompt_context = &capture,
	};
	CHECK(st_cterm_create(&terminal, &config) == 0);
	CHECK(query(terminal, &capture, "TWIN", "YES\r") == 0);
	CHECK(query(terminal, &capture, "TWFONT", "1\r") == 0);
	CHECK(query(terminal, &capture, "TWW", "80\r") == 0);
	CHECK(query(terminal, &capture, "TWH", "43\r") == 0);
	CHECK(query(terminal, &capture, "TWX0", "0\r") == 0);
	CHECK(query(terminal, &capture, "TWX1", "79\r") == 0);
	CHECK(query(terminal, &capture, "TWY0", "0\r") == 0);
	CHECK(query(terminal, &capture, "TWY1", "42\r") == 0);
	CHECK(query(terminal, &capture, "CURX", "0\r") == 0);
	CHECK(query(terminal, &capture, "CURY", "0\r") == 0);
	CHECK(query(terminal, &capture, "STATBAR", "NO\r") == 0);
	capture.size = 0;
	CHECK(feed(terminal, "!|1\x1b" "0000$DATE$^M\r\n") == 0);
	CHECK(capture.size == 9 && capture.bytes[2] == '/'
	    && capture.bytes[5] == '/' && capture.bytes[8] == '\r');
	capture.size = 0;
	CHECK(feed(terminal, "!|1\x1b" "0000$TIME$^M\r\n") == 0);
	CHECK(capture.size == 9 && capture.bytes[2] == ':'
	    && capture.bytes[5] == ':' && capture.bytes[8] == '\r');
	capture.size = 0;
	CHECK(feed(terminal, "!|1\x1b" "0000$DATETIME$^M\r\n") == 0);
	CHECK(capture.size == 26 && capture.bytes[24] == '\n'
	    && capture.bytes[25] == '\r');
	capture.size = 0;
	CHECK(feed(terminal, "!|1\x1b" "0000$DATETIME$Z\r\n") == 0);
	CHECK(capture.size == 26 && capture.bytes[24] == '\n'
	    && capture.bytes[25] == 'Z');
	capture.size = 0;
	CHECK(feed(terminal, "!|1\x1b" "0000$DATETIME$^^\r\n") == 0);
	CHECK(capture.size == 26 && capture.bytes[24] == '\n'
	    && capture.bytes[25] == '^');
	capture.size = 0;
	CHECK(feed(terminal, "!|1\x1b" "0000$ADOW$^M\r\n") == 0);
	CHECK(capture.size == 4 && capture.bytes[3] == '\r');
	capture.size = 0;
	CHECK(feed(terminal, "!|$ALARM$\r!|$BEEP$\r!|$BLIP$\r") == 0);
	CHECK(feed(terminal,
	    "!|$MUSIC$\r!|$PHASER$\r!|$REVPHASER$\r") == 0);
	CHECK(capture.size == 0);
	CHECK(feed(terminal,
	    "!|$APP0$\r!|$APP9$\r!|$VT102ON$\r!|$VT102OFF$\r") == 0);
	CHECK(capture.size == 0);
	{
		struct st_console_style saved, styled;
		struct st_console_cell status_cell;
		const struct st_console_rect status_row = {0, 42, 1, 1};

		CHECK(st_console_get_style(console, &saved) == 0);
		styled = saved;
		styled.foreground = ST_CONSOLE_COLOR_RGB_VALUE(0, 255, 0);
		styled.background = ST_CONSOLE_COLOR_RGB_VALUE(255, 0, 0);
		CHECK(st_console_set_style(console, &styled) == 0);
		CHECK(feed(terminal, "!|$SBARON$\r") == 0);
		CHECK(st_console_read_cells(console, &status_row,
		    &status_cell, 1) == 0);
		CHECK(status_cell.foreground ==
		    ST_CONSOLE_COLOR_RGB_VALUE(170, 170, 170));
		CHECK(status_cell.background ==
		    ST_CONSOLE_COLOR_RGB_VALUE(0, 0, 0));
		CHECK(st_console_set_style(console, &saved) == 0);
	}
	CHECK(query(terminal, &capture, "STATBAR", "YES\r") == 0);
	CHECK(feed(terminal, "!|$SBAROFF$\r") == 0);
	CHECK(query(terminal, &capture, "STATBAR", "NO\r") == 0);
	CHECK(feed(terminal, "!|$COFF$\r") == 0);
	CHECK(query(terminal, &capture, "CURSOR", "NO\r") == 0);
	CHECK(feed(terminal, "!|$CON$\r") == 0);
	CHECK(query(terminal, &capture, "CURSOR", "YES\r") == 0);
	CHECK(feed(terminal, "!|$UNKNOWN$\r") == 0);
	CHECK(query(terminal, &capture, "name", "One\r") == 0);
	CHECK(feed(terminal, "!|1D00500NAME,12:Two\r\n") == 0);
	CHECK(capture.prompts == 0);
	CHECK(query(terminal, &capture, "NAME", "Two\r") == 0);
	CHECK(read_values(path, "NAME", "Two") == 0);
	CHECK(read_values(path, "KEEP", "Value") == 0);
	/* The direct reset flushes this pending command through its native prompt,
	 * then reconstructs the engine and reloads the saved NAME variable. */
	CHECK(feed(terminal, "!|1D00100PENDING,5:?Question?Start") == 0);
	CHECK(capture.prompts == 0);
	CHECK(st_cterm_reset_terminal(terminal) == 0);
	CHECK(capture.prompts == 1);
	CHECK(query(terminal, &capture, "PENDING", "Typed\r") == 0);
	CHECK(query(terminal, &capture, "NAME", "Two\r") == 0);
	CHECK(feed(terminal, "!|1D00100FRESH,5:?Question?Start\r\n") == 0);
	CHECK(capture.prompts == 2);
	CHECK(read_values(path, "FRESH", "Typed") == 0);
	capture.unicode_prompt = true;
	CHECK(feed(terminal, "!|1D00100ACCENT,1:?Accent?A\r\n") == 0);
	CHECK(capture.prompts == 3);
	CHECK(query(terminal, &capture, "ACCENT", "\xc3\xa9\r") == 0);
	CHECK(read_values(path, "ACCENT", "\xc3\xa9") == 0);
	st_cterm_destroy(&terminal);
	CHECK(st_cterm_create(&terminal, &config) == 0);
	CHECK(query(terminal, &capture, "fresh", "Typed\r") == 0);
	CHECK(query(terminal, &capture, "accent", "\xc3\xa9\r") == 0);
	{
		const struct st_console_rect point = {630, 349, 1, 1};
		struct st_pixel_buffer *pixel = NULL;

		CHECK(feed(terminal, "!|f8W5K\r") == 0);
		CHECK(feed(terminal, "!|1\x1b" "0000$COMPAT$\r") == 0);
		CHECK(feed(terminal, "!|c04|XHI9P\r") == 0);
		CHECK(st_console_get_pixels(&pixel, console, &point, false) == 0);
		CHECK(pixel->primary[0] == 0x11);
		st_pixel_buffer_destroy(pixel);
		CHECK(feed(terminal, "!|f8W5K\r") == 0);
		CHECK(feed(terminal, "!|$compat$\r") == 0);
		CHECK(feed(terminal, "!|XHH9P\r") == 0);
		const struct st_console_rect adjacent = {629, 349, 1, 1};

		CHECK(st_console_get_pixels(&pixel, console, &adjacent, false) == 0);
		CHECK(pixel->primary[0] == 0x11);
		st_pixel_buffer_destroy(pixel);
	}
	{
		uint32_t colour;

		CHECK(feed(terminal, "!|c04|X0K0K\r") == 0);
		CHECK(pixel_colour(console, 20, 20, &colour) == 0
		    && colour == 0x11);
		CHECK(feed(terminal, "!|$SAVE1$\r") == 0);
		CHECK(feed(terminal, "!|c03|X0K0K\r") == 0);
		CHECK(pixel_colour(console, 20, 20, &colour) == 0
		    && colour == 0x16);
		CHECK(feed(terminal, "!|$RESTORE1$\r") == 0);
		CHECK(pixel_colour(console, 20, 20, &colour) == 0
		    && colour == 0x11);
		CHECK(feed(terminal, "!|c03|X0K0K\r") == 0);
		CHECK(feed(terminal, "!|$RESTORE1$\r") == 0);
		CHECK(pixel_colour(console, 20, 20, &colour) == 0
		    && colour == 0x16);
		CHECK(feed(terminal, "!|v14142828\r") == 0);
		CHECK(feed(terminal, "!|1\x1b" "0000$SAVE2$\r") == 0);
		CHECK(feed(terminal, "!|v0000HR9P\r") == 0);
		CHECK(feed(terminal, "!|1\x1b" "0000$RESTORE2$\r") == 0);
		CHECK(feed(terminal, "!|c04|X0000\r") == 0);
		CHECK(pixel_colour(console, 40, 40, &colour) == 0
		    && colour == 0x11);
	}
	{
		uint32_t colour;
		uint32_t columns, rows, cell_width, cell_height;

		CHECK(feed(terminal, "!|v0000HR9P|c04|X0K0K\r") == 0);
		CHECK(feed(terminal, "!|1C0K0K0K0K00\r") == 0);
		CHECK(feed(terminal, "!|$SAVEALL$\r") == 0);
		CHECK(feed(terminal, "!|c03|X0K0K|1C0K0K0K0K00\r") == 0);
		CHECK(feed(terminal, "!|w0000130O14\r") == 0);
		CHECK(st_cterm_size(terminal, &columns, &rows) == 0
		    && columns == 40 && rows == 25);
		CHECK(query(terminal, &capture, "TWFONT", "4\r") == 0);
		CHECK(query(terminal, &capture, "TWW", "40\r") == 0);
		CHECK(feed(terminal, "!|$RESTOREALL$\r") == 0);
		CHECK(st_cterm_size(terminal, &columns, &rows) == 0
		    && columns == 80 && rows == 43);
		CHECK(st_console_cell_pixel_size(console, &cell_width,
		    &cell_height) == 0 && cell_width == 8 && cell_height == 8);
		CHECK(query(terminal, &capture, "TWFONT", "1\r") == 0);
		CHECK(pixel_colour(console, 20, 20, &colour) == 0
		    && colour == 0x11);
		CHECK(feed(terminal, "!|1P1E1E000\r") == 0);
		CHECK(pixel_colour(console, 50, 50, &colour) == 0
		    && colour == 0x11);
		CHECK(feed(terminal, "!|$SCB$\r") == 0);
		CHECK(feed(terminal, "!|1C1E1E1E1E00\r") == 0);
		CHECK(feed(terminal, "!|$RCB$\r") == 0);
		CHECK(feed(terminal, "!|1P1O1O000\r") == 0);
		CHECK(pixel_colour(console, 60, 60, &colour) == 0
		    && colour == 0x11);
		CHECK(feed(terminal, "!|c03|X0K0K\r") == 0);
		CHECK(feed(terminal, "!|$PCB$\r") == 0);
		CHECK(pixel_colour(console, 20, 20, &colour) == 0
		    && colour == 0x11);
		CHECK(feed(terminal, "!|$STW$\r") == 0);
		CHECK(feed(terminal, "!|w0000130O14\r") == 0);
		CHECK(feed(terminal, "!|$RTW$\r") == 0);
		CHECK(st_cterm_size(terminal, &columns, &rows) == 0
		    && columns == 80 && rows == 43);
		CHECK(feed(terminal, "!|$SAVE$\r") == 0);
		CHECK(feed(terminal, "!|c03|X1O1O\r") == 0);
		CHECK(feed(terminal, "!|$RESTORE$\r") == 0);
		CHECK(pixel_colour(console, 60, 60, &colour) == 0
		    && colour == 0x11);
		CHECK(feed(terminal, "!|$EGW$\r") == 0);
		CHECK(pixel_colour(console, 60, 60, &colour) == 0
		    && colour == 0x10);
		CHECK(feed(terminal, "!|$ETW$\r") == 0);
		CHECK(feed(terminal, "!|$DTW$\r") == 0);
		CHECK(query(terminal, &capture, "TWIN", "NO\r") == 0);
		CHECK(query(terminal, &capture, "TWW", "0\r") == 0);
		CHECK(feed(terminal, "!|$RESET$\r") == 0);
		CHECK(query(terminal, &capture, "TWIN", "YES\r") == 0);
	}
	/* Failure while flushing a pending RIP callback propagates unchanged;
	 * destroying the terminal after that failed reset remains safe. */
	capture.prompt_status = IO_ERROR_IO;
	CHECK(feed(terminal, "!|1D00000FAILED,5:?Question?Start") == 0);
	CHECK(st_cterm_reset_terminal(terminal) == IO_ERROR_IO);
	st_cterm_destroy(&terminal);
	st_font_catalog_destroy(fonts);
	st_console_destroy(console);
	free(path);
	return 0;
}
