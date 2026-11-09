/* Native pre-emulation input integration, not IGS command conformance. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm.h"
#include "cterm_igs.h"
#include "st_cterm.h"
#include "io_error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); \
} } while (0)

struct fixture {
	st_console_t *console;
	st_font_catalog_t *fonts;
	st_cterm_t *terminal;
	bool reset_on_z;
};

static uint64_t local_ms;
static int
now_ms(uint64_t *result)
{
	*result = local_ms;
	return 0;
}

static int
filter(void *context, uint32_t scalar, bool *consumed, char *replacement,
    size_t capacity, size_t *replacement_size)
{
	struct fixture *f = context;
	(void)replacement;
	(void)capacity;
	*consumed = false;
	*replacement_size = 0;
	if (f->reset_on_z && scalar == 'Z') {
		f->reset_on_z = false;
		*consumed = true;
		CHECK(st_cterm_reset_terminal(f->terminal) == 0);
	}
	return 0;
}

static void
create(struct fixture *f, unsigned mode, bool utf8, bool status)
{
	memset(f, 0, sizeof(*f));
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry = {
		.columns = mode == 0 ? 40 : 80, .rows = 25,
		.cell_width = 8, .cell_height = mode == 2 ? 16 : 8,
		.pixel_width = mode == 0 ? 320 : 640, .pixel_height = mode == 2 ? 400 : 200,
	};
	CHECK(st_console_create(&f->console, &display, &geometry) == 0);
	CHECK(st_font_catalog_create(&f->fonts, f->console) == 0);
	const struct st_cterm_config config = {
		.console = f->console, .fonts = f->fonts, .font_selector = 44,
		.emulation = ST_CTERM_ATARI_ST_VT52, .graphics_mode = ST_CTERM_GRAPHICS_IGS_221,
		.encoding = utf8 ? ST_CTERM_ENCODING_UTF8 : ST_CTERM_ENCODING_CODEPAGE,
		.codepage = ST_CODEPAGE_ATARI_ST, .status_line = status, .quiet = true,
		.scrollback_lines = 2, .input_filter = filter, .input_filter_context = f,
	};
	CHECK(st_cterm_create(&f->terminal, &config) == 0);
	CHECK(f->terminal->igs_engine != NULL && f->terminal->skypix_engine == NULL
	    && f->terminal->rip_engine == NULL);
	CHECK(f->terminal->igs_engine->resolution == mode);
	f->terminal->igs_engine->now_ms = now_ms;
}

static void
destroy(struct fixture *f)
{
	st_cterm_destroy(&f->terminal);
	CHECK(f->terminal == NULL);
	st_font_catalog_destroy(f->fonts);
	st_console_destroy(f->console);
}

static void
feed(struct fixture *f, const char *bytes, size_t size)
{
	size_t used = 99;
	CHECK(st_cterm_write(f->terminal, bytes, size, &used) == 0 && used == size);
}

static void
read_cells(struct fixture *f, struct st_console_cell cells[12])
{
	const struct st_console_rect rect = {0,0,6,2};
	CHECK(st_console_read_cells(f->console, &rect, cells, 6) == 0);
}

static void
partitions(void)
{
	/* ESC Y row+32/column+32 is ordinary Atari ST input. Unknown ~ consumes
	 * its chain byte, then C is ordinary. The final G is held until EOF. */
	const char wire[] = "A\x1bY!\"BG#~ CG";
	for (unsigned mode = 0; mode < 3; ++mode)
		for (unsigned utf8 = 0; utf8 < 2; ++utf8)
			for (unsigned status = 0; status < 2; ++status)
				for (size_t split = 0; split <= sizeof(wire); ++split) {
					struct fixture f;
					create(&f, mode, utf8 != 0, status != 0);
					local_ms = 1000;
					if (split == sizeof(wire)) {
						for (size_t i = 0; i < sizeof(wire)-1; ++i)
							feed(&f, wire+i, 1);
					}
					else {
						feed(&f, wire, split);
						feed(&f, wire+split, sizeof(wire)-1-split);
					}
					CHECK(st_cterm_finish(f.terminal) == 0);
					struct st_console_cell cells[12];
					read_cells(&f, cells);
					CHECK(cells[0].glyph == 'A');
					CHECK(cells[8].glyph == 'B' && cells[9].glyph == 'C' && cells[10].glyph == 'G');
					struct st_cterm_state state;
					CHECK(st_cterm_state(f.terminal, &state) == 0);
					CHECK(state.x == 6 && state.y == 2 && !state.skypix);
					CHECK(state.graphics_mode == ST_CTERM_GRAPHICS_IGS_221
					    && state.emulation == ST_CTERM_ATARI_ST_VT52);
					destroy(&f);
				}
}

static void
lifecycle(void)
{
	struct fixture f;
	create(&f, 0, false, false);
	/* A first ordinary byte must not initialize/clear a screen on which
	 * another bitmap operation has already drawn. Header bytes resembling
	 * a VT52 clear are also owned by graphics dispatch, not base emulation. */
	const uint32_t sentinel = ST_PIXEL_COLOR_RGB_TAG | UINT32_C(0x123456);
	CHECK(st_console_set_pixel(f.console, 100, 100, sentinel) == 0);
	feed(&f, "G#\x1b" "EA", 5);
	CHECK(st_cterm_finish(f.terminal) == 0);
	const struct st_console_rect point = {100,100,1,1};
	struct st_pixel_buffer *pixels = NULL;
	CHECK(st_console_get_pixels(&pixels, f.console, &point, false) == 0);
	CHECK(pixels->primary[0] == sentinel && pixels->alternate[0] == sentinel);
	st_pixel_buffer_destroy(pixels);
	CHECK(st_cterm_reset_terminal(f.terminal) == 0);
	feed(&f, "\x1bH", 2);
	local_ms = 1000;
	feed(&f, "G#", 2);
	CHECK(st_graphics_parser_in_command(f.terminal->igs_engine->parser));
	local_ms = 5999;
	CHECK(st_cterm_poll(f.terminal) == 0);
	CHECK(st_graphics_parser_in_command(f.terminal->igs_engine->parser));
	local_ms = 6000;
	CHECK(st_cterm_poll(f.terminal) == 0);
	CHECK(!st_graphics_parser_in_command(f.terminal->igs_engine->parser));
	feed(&f, "A", 1);
	CHECK(st_cterm_finish(f.terminal) == 0);
	struct st_console_cell cells[12];
	read_cells(&f, cells);
	CHECK(cells[0].glyph == 'A');
	feed(&f, "G#~", 3);
	CHECK(st_cterm_reset_terminal(f.terminal) == 0);
	CHECK(!st_graphics_parser_in_command(f.terminal->igs_engine->parser));
	feed(&f, "\x1bHB", 3);
	CHECK(st_cterm_finish(f.terminal) == 0);
	read_cells(&f, cells);
	CHECK(cells[0].glyph == 'B');
	feed(&f, "G#", 2);
	CHECK(st_cterm_prepare_disconnect(f.terminal) == 0);
	CHECK(!st_graphics_parser_in_command(f.terminal->igs_engine->parser));
	destroy(&f);
	create(&f, 0, false, false);
	/* Semantic replacements deliberately bypass graphics recognition. */
	size_t used;
	CHECK(st_cterm_write_utf8(f.terminal, "G#~ X", 5, &used) == 0 && used == 5);
	CHECK(st_cterm_finish(f.terminal) == 0);
	read_cells(&f, cells);
	CHECK(cells[0].glyph == 'G' && cells[1].glyph == '#' && cells[4].glyph == 'X');
	CHECK(st_cterm_reset_terminal(f.terminal) == 0);
	f.reset_on_z = true;
	feed(&f, "\x1bHZG#~ C", 8);
	CHECK(!f.reset_on_z);
	CHECK(st_cterm_finish(f.terminal) == 0);
	read_cells(&f, cells);
	CHECK(cells[0].glyph == 'C');
	destroy(&f);
	create(&f, 0, true, false);
	feed(&f, "G#\xc3\xa9X", 5);
	CHECK(st_cterm_finish(f.terminal) == 0);
	read_cells(&f, cells);
	CHECK(cells[0].glyph == 'X');
	destroy(&f);
}

static void
invalid_configuration(void)
{
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry = {
		.columns = 90, .rows = 25, .cell_width = 8, .cell_height = 16,
	};
	st_console_t *console = NULL;
	st_font_catalog_t *fonts = NULL;
	st_cterm_t *terminal = NULL;
	CHECK(st_console_create(&console, &display, &geometry) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	struct st_cterm_config config = {
		.console = console, .fonts = fonts, .font_selector = 44,
		.emulation = ST_CTERM_ANSI_BBS, .graphics_mode = ST_CTERM_GRAPHICS_IGS_221,
		.encoding = ST_CTERM_ENCODING_CODEPAGE, .codepage = ST_CODEPAGE_ATARI_ST, .quiet = true,
	};
	CHECK(st_cterm_create(&terminal, &config) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(terminal == NULL);
	config.emulation = ST_CTERM_ATARI_ST_VT52;
	CHECK(st_cterm_create(&terminal, &config) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(terminal == NULL);
	st_font_catalog_destroy(fonts);
	st_console_destroy(console);
}

int
st_cterm_igs_input_test_main(void)
{
	partitions();
	lifecycle();
	invalid_configuration();
	return 0;
}
