/* Independent sixel pixels, numeric bounds and partial-image ownership. */
/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include "cterm.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); exit(1); } } while (0)
#define WIDTH 160
#define HEIGHT 160
#define SENTINEL ST_PIXEL_COLOR_RGB_VALUE(18, 52, 86)

struct fixture {
	st_console_t *console;
	st_font_catalog_t *fonts;
	st_cterm_t *terminal;
};

static void
feed(struct fixture *f, const char *bytes, size_t size)
{
	size_t consumed;
	int status = st_cterm_write(f->terminal, bytes, size, &consumed);
	if (status != 0 || consumed != size) {
		fprintf(stderr, "write status %d, consumed %zu/%zu:", status, consumed, size);
		for (size_t i = 0; i < size; ++i)
			fprintf(stderr, " %02x", (unsigned char)bytes[i]);
		fputc('\n', stderr);
		exit(1);
	}
}

static void
text(struct fixture *f, const char *bytes)
{
	feed(f, bytes, strlen(bytes));
}

static void
reset(struct fixture *f)
{
	struct st_pixel_buffer *background;
	const struct st_pixel_blit blit = {.width = WIDTH, .height = HEIGHT, .scale_x = 1, .scale_y = 1};
	CHECK(st_cterm_reset_terminal(f->terminal) == 0);
	text(f, "\x1b[?80h\x1b[?25l\x1b[0;40m");
	CHECK(st_pixel_buffer_create(&background, WIDTH, HEIGHT, false) == 0);
	CHECK(st_pixel_buffer_fill(background, SENTINEL, SENTINEL) == 0);
	CHECK(st_console_blit_pixels(f->console, background, NULL, &blit) == 0);
	st_pixel_buffer_destroy(background);
}

static void
pixels(struct fixture *f, unsigned red_width, unsigned red_height,
	unsigned background_width, unsigned background_height)
{
	const struct st_console_rect area = {0, 0, WIDTH, HEIGHT};
	struct st_pixel_buffer *frame;
	uint32_t palette[UINT16_MAX + 1U];
	CHECK(st_console_get_palette(f->console, 0, palette, UINT16_MAX + 1U) == 0);
	CHECK(st_console_get_pixels(&frame, f->console, &area, false) == 0);
	for (unsigned y = 0; y < HEIGHT; ++y) {
		for (unsigned x = 0; x < WIDTH; ++x) {
			uint32_t expected = x < red_width && y < red_height ? 0xff0000
			    : x < background_width && y < background_height ? 0 : 0x123456;
			size_t pos = (size_t)y * WIDTH + x;
			uint32_t value = frame->primary[pos];
			CHECK(ST_PIXEL_COLOR_IS_RGB(value) || value <= UINT16_MAX);
			value = ST_PIXEL_COLOR_IS_RGB(value) ? value & 0xffffff : palette[value];
			if (value != expected) {
				fprintf(stderr, "pixel %u,%u: %06x != %06x\n", x, y, (unsigned)value, (unsigned)expected);
				exit(1);
			}
			if (frame->alternate != NULL) {
				value = frame->alternate[pos];
				CHECK(ST_PIXEL_COLOR_IS_RGB(value) || value <= UINT16_MAX);
				value = ST_PIXEL_COLOR_IS_RGB(value) ? value & 0xffffff : palette[value];
				CHECK(value == expected);
			}
		}
	}
	st_pixel_buffer_destroy(frame);
	CHECK(f->terminal->sixel == SIXEL_INACTIVE);
	CHECK(f->terminal->sx_pixels == NULL && f->terminal->sx_mask == NULL);
}

static void
numeric_bounds(struct fixture *f)
{
	reset(f);
	text(f, "\x1bPq\"4294967297;4294967297;4294967297;4294967297$");
	CHECK(f->terminal->sx_iv == HEIGHT && f->terminal->sx_ih == WIDTH);
	CHECK(f->terminal->sx_width == INT_MAX && f->terminal->sx_height == INT_MAX);
	text(f, "\x1b\\");
	const char *large[] = {"4294967296", "18446744073709551616"};
	for (size_t i = 0; i < sizeof(large) / sizeof(large[0]); ++i) {
		char sequence[128];
		reset(f);
		CHECK(snprintf(sequence, sizeof(sequence), "\x1bP7;1q!%s$", large[i]) > 0);
		text(f, sequence);
		CHECK(f->terminal->sx_repeat == 32767);
		text(f, "\x1b\\");
	}
	reset(f);
	text(f, "\x1bP4294967303;4294967296q");
	CHECK(f->terminal->sx_iv == 2 && f->terminal->sx_trans != 0);
	uint32_t foreground = f->terminal->sx_fg;
	text(f, "#4294967296;2;100;0;0$");
	CHECK(f->terminal->sx_fg == foreground);
	text(f, "\x1b\\");

	/* Width * horizontal scale exceeds INT_MAX, but the visible row and
	 * final background must stay bounded to this 160-by-160 viewport. */
	reset(f);
	text(f, "\x1bP7;0q\"1;2;2147483647;2147483647#0;2;100;0;0~~-?\x1b\\");
	pixels(f, 4, 6, WIDTH, HEIGHT);
	reset(f);
	text(f, "\x1bP7;1q\"4294967296;1#0;2;100;0;0~\x1b\\");
	pixels(f, 1, HEIGHT, 0, 0);
	reset(f);
	text(f, "\x1bP7;1q\"1;4294967296#0;2;100;0;0~\x1b\\");
	pixels(f, WIDTH, 6, 0, 0);
	reset(f);
	text(f, "\x1bP7;1q\"0;0#0;2;100;0;0!0~\x1b\\");
	pixels(f, 1, 6, 0, 0);

	/* The last native palette entry is usable; one past it is ignored. */
	reset(f);
	unsigned base = 282 + ((f->terminal->screen.capabilities & ST_CONSOLE_CAP_EXTENDED_PALETTE) ? 16 : 0);
	char sequence[128];
	CHECK(snprintf(sequence, sizeof(sequence), "\x1bP7;1q#%u;2;100;0;0#%u;2;0;0;100@\x1b\\",
	    UINT16_MAX - base, UINT16_MAX - base + 1) > 0);
	text(f, sequence);
	pixels(f, 1, 1, 0, 0);

	/* A tall sixel row may start above the screen after scrolling. Its
	 * visible tail and final cursor remain bounded to the viewport. */
	reset(f);
	text(f, "\x1b[?80l\x1bP7;1q\"4294967296;1#0;2;100;0;0~-~\x1b\\");
	pixels(f, 1, HEIGHT, WIDTH, HEIGHT);
	struct st_cterm_state state;
	CHECK(st_cterm_state(f->terminal, &state) == 0);
	CHECK(state.y == 10);
	reset(f);
	text(f, "\x1b[?80l\x1bP2q\x1b\\");
	CHECK(st_cterm_state(f->terminal, &state) == 0);
	CHECK(state.x == 1 && state.y == 2);
}

static void
rendering_and_splits(struct fixture *f)
{
	const char sequence[] = "\x1bP7;0q\"2;3;4;9#0;2;100;0;0!2~\x1b\\";
	for (size_t split = 0; split < sizeof(sequence); ++split) {
		reset(f);
		feed(f, sequence, split);
		feed(f, sequence + split, sizeof(sequence) - 1 - split);
		pixels(f, 6, 12, 12, 18);
	}
	reset(f);
	for (size_t i = 0; i < sizeof(sequence) - 1; ++i)
		feed(f, sequence + i, 1);
	pixels(f, 6, 12, 12, 18);
	reset(f);
	text(f, "\x1bP7;1q#0;2;100;0;0@\x1b\\");
	pixels(f, 1, 1, 0, 0);
	reset(f);
	text(f, "\x1bP7;1q\"2;3;4;9#0;2;100;0;0!2~\x1b\\");
	pixels(f, 6, 12, 0, 0);
	reset(f);
	text(f, "\x1bP7;0q\"2;3;4;9\x1b\\");
	pixels(f, 0, 0, 12, 18);
}

static void
cancel_and_destroy(struct fixture *f)
{
	reset(f);
	text(f, "\x1b[?25h");
	int cursor = f->terminal->cursor;
	text(f, "\x1bP7;1q~~");
	CHECK(f->terminal->sx_pixels != NULL && f->terminal->sx_mask != NULL);
	text(f, "\x18");
	CHECK(f->terminal->sixel == SIXEL_INACTIVE);
	CHECK(f->terminal->sx_pixels == NULL && f->terminal->sx_mask == NULL);
	CHECK(f->terminal->cursor == cursor);
	/* Starting another string must discard the partial image as well. */
	text(f, "\x1bP7;1q~~\x1b]ignored\x1b\\");
	CHECK(f->terminal->sx_pixels == NULL && f->terminal->sx_mask == NULL);
	CHECK(f->terminal->cursor == cursor);
	text(f, "\x1bP7;1q~~");
	CHECK(st_console_resize(f->console, 21, 10) == 0);
	text(f, "~\x1b\\");
	CHECK(f->terminal->sx_pixels == NULL && f->terminal->sx_mask == NULL);
	CHECK(f->terminal->cursor == cursor);
	CHECK(st_console_resize(f->console, 20, 10) == 0);
	text(f, "\x1bP7;1q~~");
	CHECK(st_cterm_resize(f->terminal, 20, 10) == 0);
	CHECK(f->terminal->sixel == SIXEL_STARTED && f->terminal->sx_pixels != NULL);
	CHECK(st_cterm_resize(f->terminal, 0, 5) != 0);
	CHECK(f->terminal->sixel == SIXEL_STARTED && f->terminal->sx_pixels != NULL);
	CHECK(st_cterm_resize(f->terminal, 20, 5) == 0);
	CHECK(f->terminal->sixel == SIXEL_INACTIVE);
	CHECK(f->terminal->sx_pixels == NULL && f->terminal->sx_mask == NULL);
	text(f, "~\x1b\\");
	CHECK(f->terminal->sx_pixels == NULL && f->terminal->sx_mask == NULL);
	CHECK(f->terminal->cursor == cursor);
	CHECK(st_cterm_resize(f->terminal, 20, 10) == 0);
	CHECK(st_cterm_reset_terminal(f->terminal) == 0);
	text(f, "\x1bP7;1q~~");
	CHECK(st_cterm_reset_terminal(f->terminal) == 0);
	CHECK(f->terminal->sx_pixels == NULL && f->terminal->sx_mask == NULL);
	text(f, "\x1bP7;1q~~");
	st_cterm_destroy(&f->terminal);
}

int
main(void)
{
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry = {.columns = 20, .rows = 10, .cell_width = 8, .cell_height = 16};
	struct fixture f = {0};
	CHECK(st_console_create(&f.console, &display, &geometry) == 0);
	CHECK(st_font_catalog_create(&f.fonts, f.console) == 0);
	for (unsigned utf8 = 0; utf8 < 2; ++utf8) {
		const struct st_cterm_config config = {
			.console = f.console, .fonts = f.fonts, .emulation = ST_CTERM_ANSI_BBS,
			.encoding = utf8 ? ST_CTERM_ENCODING_UTF8 : ST_CTERM_ENCODING_CODEPAGE,
			.codepage = ST_CODEPAGE_CP437, .quiet = true,
		};
		CHECK(st_cterm_create(&f.terminal, &config) == 0);
		numeric_bounds(&f);
		rendering_and_splits(&f);
		cancel_and_destroy(&f);
	}
	st_font_catalog_destroy(f.fonts);
	st_console_destroy(f.console);
	return 0;
}
