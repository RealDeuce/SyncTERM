/* Normalized input must reach every emulation's reference key tables. */
/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "cterm.h"
#include "io_error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
		exit(EXIT_FAILURE); \
	} \
} while (false)

struct output { uint8_t bytes[64]; size_t size; };

static void
capture(void *context, enum st_cterm_output_origin origin,
	const uint8_t *bytes, size_t size)
{
	struct output *output = context;

	CHECK(origin == ST_CTERM_OUTPUT_USER);
	CHECK(size <= sizeof(output->bytes) - output->size);
	memcpy(output->bytes + output->size, bytes, size);
	output->size += size;
}

static void
feed(st_cterm_t *terminal, const char *bytes)
{
	size_t consumed;

	CHECK(st_cterm_write(terminal, bytes, strlen(bytes), &consumed) == 0);
	CHECK(consumed == strlen(bytes));
}

static void
compare_key(st_cterm_t *terminal, struct output *output,
	uint16_t key, uint32_t modifiers, int reference)
{
	struct output actual;

	*output = (struct output){0};
	(void)st_cterm_handle_key(terminal, key, modifiers);
	actual = *output;
	*output = (struct output){0};
	/* Numeric reference keys are from v1.10a's input layer. The unchanged
	 * private encoder is the oracle, not the normalized-key adapter. */
	(void)cterm_encode_key(terminal, reference);
	CHECK(actual.size == output->size);
	CHECK(memcmp(actual.bytes, output->bytes, actual.size) == 0);
}

static void
check_mappings(void)
{
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry = {
		.columns = 40, .rows = 25, .cell_width = 8, .cell_height = 16,
	};
	const enum st_codepage codepages[] = {ST_CODEPAGE_CP437,
	    ST_CODEPAGE_PETSCII_UPPER, ST_CODEPAGE_ATASCII, ST_CODEPAGE_PRESTEL,
	    ST_CODEPAGE_PRESTEL, ST_CODEPAGE_ATARI_ST};
	const uint32_t modifiers[] = {0, ST_CONSOLE_MODIFIER_SHIFT,
	    ST_CONSOLE_MODIFIER_CONTROL, ST_CONSOLE_MODIFIER_ALT};
	/* Plain, Shift, Ctrl, Alt, in the normalized navigation enum order. */
	const int navigation[][4] = {
		{0x4800,0x91e0,0x8d00,0x9800}, {0x5000,0x96e0,0x9100,0xa000},
		{0x4b00,0x93e0,0x7300,0xffff}, {0x4d00,0x94e0,0x7400,0xffff},
		{0x5200,0x0500,0x0400,0xa200}, {0x5300,0x0700,0x0600,0xa300},
		{0x4700,0x3700,0x7700,0xffff}, {0x4f00,0x95e0,0x7500,0xffff},
		{0x4900,0x3900,0x8400,0xffff}, {0x5100,0x3300,0x7600,0xffff},
	};
	st_console_t *console = NULL;
	st_font_catalog_t *fonts = NULL;
	struct output output = {0};

	CHECK(st_console_create(&console, &display, &geometry) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	for (unsigned mode = ST_CTERM_ANSI_BBS; mode <= ST_CTERM_ATARI_ST_VT52; ++mode) {
		struct st_cterm_config config = {
			.console = console, .fonts = fonts, .emulation = (enum st_cterm_emulation)mode,
			.encoding = ST_CTERM_ENCODING_CODEPAGE, .codepage = codepages[mode],
			.output = capture, .output_context = &output, .quiet = true,
		};
		st_cterm_t *terminal = NULL;

		CHECK(st_cterm_create(&terminal, &config) == 0);
		for (unsigned state = 0; state < 2; ++state) {
			if (mode == ST_CTERM_PRESTEL) feed(terminal, state ? "\x11" : "\x14");
			if (mode == ST_CTERM_ANSI_BBS || mode == ST_CTERM_ATARI_ST_VT52)
				feed(terminal, state ? "\x1b[?67h" : "\x1b[?67l");
			for (unsigned modifier = 0; modifier < 4; ++modifier) {
				for (unsigned number = 1; number <= 12; ++number) {
					int scan = number <= 10 ? 0x3a + (int)number : 0x7a + (int)number;
					if (modifier != 0)
						scan += number <= 10 ? (modifier == 1 ? 0x19 : modifier == 2 ? 0x23 : 0x2d)
						    : (int)modifier * 2;
					compare_key(terminal, &output, (uint16_t)(ST_CONSOLE_KEY_F1 + number - 1),
					    modifiers[modifier], scan << 8);
				}
				for (unsigned key = 0; key < 10; ++key)
					compare_key(terminal, &output, (uint16_t)(ST_CONSOLE_KEY_UP + key),
					    modifiers[modifier], navigation[key][modifier]);
			}
			compare_key(terminal, &output, '\r', ST_CONSOLE_MODIFIER_CONTROL, 10);
			compare_key(terminal, &output, '\b', ST_CONSOLE_MODIFIER_CONTROL, 127);
			compare_key(terminal, &output, '\t', ST_CONSOLE_MODIFIER_SHIFT, 0x0f00);
			compare_key(terminal, &output, '\t', ST_CONSOLE_MODIFIER_CONTROL, 0xffff);
			for (unsigned byte = 0; byte <= 255; ++byte) {
				uint8_t encoded;
				int reference = (int)byte;

				if (byte < 32 && st_codepage_from_unicode(&encoded, codepages[mode], byte))
					reference = encoded;
				compare_key(terminal, &output, (uint16_t)byte, 0, reference);
			}
		}
		if (mode == ST_CTERM_ANSI_BBS) {
			feed(terminal, "\x1b[=255h");
			compare_key(terminal, &output, ST_CONSOLE_KEY_F1, ST_CONSOLE_MODIFIER_SHIFT, 0x5400);
		}
		for (unsigned byte = 32; byte < 256; ++byte) {
			uint32_t scalar = st_codepage_to_unicode(codepages[mode], (uint8_t)byte, false);
			uint8_t encoded;
			struct output actual;

			if (!st_codepage_from_unicode(&encoded, codepages[mode], scalar))
				continue;
			output = (struct output){0};
			CHECK(st_cterm_handle_text_key(terminal, scalar));
			actual = output;
			output = (struct output){0};
			(void)cterm_encode_key(terminal, encoded);
			CHECK(actual.size == output.size);
			CHECK(memcmp(actual.bytes, output.bytes, actual.size) == 0);
		}
		if (mode == ST_CTERM_ATASCII) {
			output = (struct output){0};
			CHECK(st_cterm_handle_key(terminal, '\r', 0));
			CHECK(st_cterm_handle_key(terminal, '\b', 0));
			CHECK(st_cterm_handle_key(terminal, '\t', 0));
			CHECK(output.size == 3 && memcmp(output.bytes, "\x9b\x7e\x7f", 3) == 0);
		}
		st_cterm_destroy(&terminal);
	}
	st_font_catalog_destroy(fonts);
	st_console_destroy(console);
}

static void
check_reveal_pixels(void)
{
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry = {
		.columns = 40, .rows = 25, .cell_width = 12, .cell_height = 20,
	};
	st_console_t *console = NULL;
	st_font_catalog_t *fonts = NULL;
	st_cterm_t *terminal = NULL;
	struct st_pixel_buffer *hidden = NULL, *shown = NULL, *again = NULL;
	const struct st_console_rect row = {0, 0, 480, 20};
	struct output output = {0};

	CHECK(st_console_create(&console, &display, &geometry) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	const struct st_cterm_config config = {
		.console = console, .fonts = fonts, .emulation = ST_CTERM_PRESTEL,
		.encoding = ST_CTERM_ENCODING_CODEPAGE, .codepage = ST_CODEPAGE_PRESTEL,
		.font_selector = 43, .quiet = true, .output = capture, .output_context = &output,
	};
	CHECK(st_cterm_create(&terminal, &config) == 0);
	feed(terminal, "\x1bXA");
	CHECK(st_console_get_pixels(&hidden, console, &row, false) == 0);
	CHECK(st_cterm_handle_key(terminal, '\t', 0));
	CHECK(output.size == 1 && output.bytes[0] == '\t');
	output = (struct output){0};
	CHECK(st_cterm_handle_key(terminal, ST_CONSOLE_KEY_F3, 0));
	CHECK(output.size == 0);
	CHECK(st_console_get_pixels(&shown, console, &row, false) == 0);
	CHECK(memcmp(hidden->primary, shown->primary, 480 * 20 * sizeof(uint32_t)) != 0);
	CHECK(st_cterm_handle_key(terminal, ST_CONSOLE_KEY_PAGE_DOWN, 0));
	CHECK(output.size == 0);
	CHECK(st_console_get_pixels(&again, console, &row, false) == 0);
	CHECK(memcmp(hidden->primary, again->primary, 480 * 20 * sizeof(uint32_t)) == 0);
	CHECK(st_cterm_handle_key(terminal, '\r', ST_CONSOLE_MODIFIER_CONTROL));
	CHECK(output.size == 1 && output.bytes[0] == '\r');
	output = (struct output){0};
	CHECK(st_cterm_handle_text_key(terminal, '#'));
	CHECK(st_cterm_handle_text_key(terminal, '_'));
	CHECK(st_cterm_handle_text_key(terminal, '`'));
	CHECK(output.size == 3 && memcmp(output.bytes, "_`#", 3) == 0);
	st_pixel_buffer_destroy(hidden);
	st_pixel_buffer_destroy(shown);
	st_pixel_buffer_destroy(again);
	st_cterm_destroy(&terminal);
	st_font_catalog_destroy(fonts);
	st_console_destroy(console);
}

int
st_cterm_keyboard_test_main(void)
{
	check_mappings();
	check_reveal_pixels();
	return EXIT_SUCCESS;
}
