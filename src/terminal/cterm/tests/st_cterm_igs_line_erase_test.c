/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "igs_console_test_model.h"

static void
no_reply(const char *bytes, size_t size, void *context)
{
	(void)bytes;
	(void)context;
	CHECK(size == 0);
}

static void
prepare(struct fixture *f)
{
	feed(f, "\033q\033wG#c>1,15:c 0,0:\033E"
	    "G#c>1,4:c 0,3:");
}

static void
erase_case(struct fixture *f, unsigned resolution, unsigned x, unsigned y,
    uint16_t depth, bool reverse, int selector)
{
	struct cterminal *t = f->terminal;
	struct cterm_igs *igs = t->igs_engine;
	prepare(f);
	if (reverse)
		feed(f, "\033p");
	char position[32];
	(void)snprintf(position, sizeof(position), "G#p>%u,%u:X 10,19,17:", x, y);
	feed(f, position);
	feed(f, depth == 0 ? "\033e" : "\033f");
	t->vt52_cursor_hide_depth = depth;

	const struct st_console_rect full = {0, 0, igs->width, igs->height};
	seed(f, &full, 768, 769);
	struct cterm_igs_attributes attributes;
	uint32_t palette[16];
	memcpy(&attributes, &igs->attributes, sizeof(attributes));
	memcpy(palette, igs->palette, sizeof(palette));
	igs->math[0] = (int16_t)selector;
	feed(f, "G#l 0]:");

	unsigned last = (unsigned)t->width - 1;
	unsigned new_x = x;
	unsigned left = 0, right = last;
	if (selector == 1)
		right = x;
	else if (selector == 2)
		left = x;
	else
		new_x = depth < last ? depth : last;
	CHECK(t->xpos == (int)new_x + 1 && t->ypos == (int)y + 1);
	CHECK(t->vt52_cursor_hide_depth == depth && (t->cursor != CTERM_CURSOR_HIDDEN) == (depth == 0));
	CHECK(t->negative == reverse && t->accumulator == NULL);
	CHECK(t->vt52_foreground == (uint16_t)(4 - 32));
	CHECK(t->vt52_background == (uint16_t)(3 - 32));
	CHECK(memcmp(&attributes, &igs->attributes, sizeof(attributes)) == 0);
	CHECK(memcmp(palette, igs->palette, sizeof(palette)) == 0);
	CHECK(igs->draw_x == 19 && igs->draw_y == 17 && igs->wait_deadline == 0);

	const unsigned ch = resolution == 2 ? 16 : 8;
	const unsigned blank = resolution == 2 ? 769 : 771;
	struct st_pixel_buffer *pixels = NULL;
	CHECK(st_console_get_pixels(&pixels, f->console, &full, false) == 0);
	for (unsigned py = 0; py < igs->height; ++py)
		for (unsigned px = 0; px < igs->width; ++px) {
			bool erased = py / ch == y && px >= left * 8 && px < (right + 1) * 8;
			size_t i = (size_t)py * igs->width + px;
			CHECK(pixels->primary[i] == (erased ? blank : 768));
			CHECK(pixels->alternate[i] == (erased ? blank : 769));
		}
	st_pixel_buffer_destroy(pixels);
	const struct st_console_rect row = {0, y, (unsigned)t->width, 1};
	struct st_console_cell cells[80];
	CHECK(st_console_read_cells(f->console, &row, cells, row.width) == 0);
	for (unsigned col = left; col <= right; ++col)
		CHECK(cells[col].glyph == ' ' && cells[col].background == blank
		    && (cells[col].flags & ST_CONSOLE_CELL_PIXEL_GRAPHICS) == 0);
}

static void
pending_case(struct fixture *f, unsigned resolution, unsigned state, int selector)
{
	static const char *prefixes[] = {"", "\033", "\033Y", "\033Y!", "\033b", "\033c"};
	struct cterminal *t = f->terminal;
	prepare(f);
	feed(f, "\033f\033pG#p 3,2:");
	feed(f, prefixes[state]);
	struct model m = {
		.state = state, .x = 3, .y = 2, .row = 1,
		.columns = (unsigned)t->width, .rows = (unsigned)t->height,
		.fg = (uint16_t)(4 - 32), .bg = (uint16_t)(3 - 32), .reverse = true,
	};
	model_byte(&m, 27);
	if (selector >= 0) {
		static const uint8_t letters[] = {'l', 'o', 'K'};
		model_byte(&m, letters[selector > 2 ? 0 : selector]);
	}
	t->igs_engine->math[0] = (int16_t)selector;
	feed(f, "G#l 0]:");
	model_check_console(t, &m);
	model_byte(&m, 'A');
	feed(f, "A");
	model_check_console(t, &m);
	model_pixels(f, resolution, &m);
}

static void
scenes(void)
{
	static const int selectors[] = {0, 1, 2, INT16_MAX};
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		for (unsigned reserved = 0; reserved < 2; ++reserved) {
			struct fixture f;
			create(&f, resolution, reserved != 0);
			struct cterminal *t = f.terminal;
			t->response_cb = no_reply;
			const unsigned right = (unsigned)t->width - 1, bottom = (unsigned)t->height - 1;
			const unsigned x[] = {0, 1, 4, 5, right - 1, right, right};
			const unsigned y[] = {0, 1, 6, 7, bottom, bottom, 0};
			const uint16_t depths[] = {0, 1, 2, (uint16_t)right, (uint16_t)(right + 1), 0x8000, UINT16_MAX};
			for (size_t i = 0; i < sizeof(depths) / sizeof(*depths); ++i)
				for (unsigned reverse = 0; reverse < 2; ++reverse)
					for (size_t j = 0; j < sizeof(selectors) / sizeof(*selectors); ++j)
						erase_case(&f, resolution, x[i], y[i], depths[i], reverse != 0, selectors[j]);
			/* The shared pixel oracle uses distinct sentinels for the status row. */
			if (reserved) {
				unsigned ch = resolution == 2 ? 16 : 8;
				const struct st_console_rect status = {0, 24 * ch, t->igs_engine->width, ch};
				seed(&f, &status, 769, 768);
			}
			for (unsigned state = 0; state < 6; ++state) {
				pending_case(&f, resolution, state, INT16_MIN);
				if (state != 0)
					for (size_t i = 0; i < sizeof(selectors) / sizeof(*selectors); ++i)
						pending_case(&f, resolution, state, selectors[i]);
			}
			destroy(&f);
		}
	}
}

static void
check_cell(struct fixture *f, unsigned x, unsigned y, unsigned glyph)
{
	const struct st_console_rect area = {x, y, 1, 1};
	struct st_console_cell cell;
	CHECK(st_console_read_cells(f->console, &area, &cell, 1) == 0 && cell.glyph == glyph);
}

static void
partitions(void)
{
	const char wire[] = "G#g>2:p 4,2:AG#p>4,2:l>r:M 2:";
	const size_t size = sizeof(wire) - 1;
	struct fixture f;
	create(&f, 0, false);
	for (size_t split = 0; split <= size + 1; ++split) {
		CHECK(st_cterm_reset_terminal(f.terminal) == 0);
		struct cterm_igs *igs = f.terminal->igs_engine;
		igs->random_range[0][1] = 255;
		size_t used;
		if (split > size) {
			for (size_t i = 0; i < size; ++i)
				CHECK(st_cterm_write(f.terminal, wire + i, 1, &used) == 0 && used == 1);
		} else {
			CHECK(st_cterm_write(f.terminal, wire, split, &used) == 0 && used == split);
			CHECK(st_cterm_write(f.terminal, wire + split, size - split, &used) == 0 && used == size - split);
		}
		check_cell(&f, 4, 2, ' ');
		CHECK(f.terminal->xpos == 2 && f.terminal->ypos == 3);
		CHECK(igs->random_seed == UINT32_C(0xbb40e62e) && igs->attributes.writing_mode == 2);
		CHECK(f.terminal->accumulator == NULL && igs->input_deadline == 0);
	}
	destroy(&f);
}

static void
lifetime(void)
{
	struct fixture f;
	create(&f, 0, false);
	struct cterminal *t = f.terminal;
	struct cterm_igs *igs = t->igs_engine;
	t->response_cb = no_reply;
	feed(&f, "G#p 4,2:AG#p 4,2:G#l r");
	CHECK(st_cterm_finish(t) == 0 && t->accumulator == NULL);
	feed(&f, "G#l r");
	local_ms += 5001;
	CHECK(st_cterm_poll(t) == 0 && igs->random_seed == 1);
	const char bad[] = "G#l 10000:";
	size_t used;
	CHECK(st_cterm_write(t, bad, sizeof(bad) - 1, &used) == 0);
	check_cell(&f, 4, 2, 'A');
	CHECK(t->xpos == 5 && t->ypos == 3);

	t->screen.error = IO_ERROR_IO;
	const char whole[] = "G#l 0:";
	CHECK(st_cterm_write(t, whole, sizeof(whole) - 1, &used) == 0);
	CHECK(t->accumulator != NULL && t->sequence != 0);
	check_cell(&f, 4, 2, 'A');
	feed(&f, "l");
	check_cell(&f, 4, 2, ' ');
	CHECK(t->xpos == 2 && t->ypos == 3 && t->vt52_cursor_hide_depth == 1);

	feed(&f, "\033e\033f\033fG#p 4,2:");
	CHECK(st_cterm_reset_terminal(t) == 0);
	feed(&f, "G#p>4,2:l 0:");
	CHECK(t->xpos == 2 && t->ypos == 3 && t->vt52_cursor_hide_depth == 1);
	destroy(&f);
}

static void
standalone_vt52(void)
{
	struct fixture f;
	create(&f, 0, false);
	st_cterm_destroy(&f.terminal);
	const struct st_cterm_config config = {
		.console = f.console, .fonts = f.fonts, .font_selector = 44,
		.terminal_rows = 25, .emulation = ST_CTERM_ATARI_ST_VT52,
		.graphics_mode = ST_CTERM_GRAPHICS_OFF, .encoding = ST_CTERM_ENCODING_CODEPAGE,
		.codepage = ST_CODEPAGE_ATARI_ST, .quiet = true,
	};
	CHECK(st_cterm_create(&f.terminal, &config) == 0);
	feed(&f, "\033Y\042\043A\033Y\042\043\033l");
	check_cell(&f, 3, 2, ' ');
	CHECK(f.terminal->xpos == 4 && f.terminal->ypos == 3);
	destroy(&f);
}

int
st_cterm_igs_line_erase_test_main(void)
{
	scenes();
	partitions();
	lifetime();
	standalone_vt52();
	return 0;
}
