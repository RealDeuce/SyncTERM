/* r: native cursor save and one-shot restore through VT52. */
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
position(const struct fixture *f, int x, int y)
{
	CHECK(f->terminal->xpos == x && f->terminal->ypos == y);
}

static void
normal(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f;
		create(&f, resolution, false);
		struct cterminal *t = f.terminal;
		struct cterm_igs *igs = t->igs_engine;
		t->response_cb = no_reply;
		feed(&f, "\033fG#p>3,2:r 1:");
		position(&f, 1, 1);

		const struct st_console_rect full = {0, 0, igs->width, igs->height};
		seed(&f, &full, 769, 768);
		feed(&f, "G#X 10,19,17:");
		struct cterm_igs_attributes attributes;
		uint32_t palette[16];
		memcpy(&attributes, &igs->attributes, sizeof(attributes));
		memcpy(palette, igs->palette, sizeof(palette));

		feed(&f, "G#g>1:p>3,2:r 0:");
		position(&f, 4, 3);
		feed(&f, "G#p 7,5:\033k");
		position(&f, 4, 3);
		CHECK(t->save_xpos == 4 && t->save_ypos == 3);
		feed(&f, "G#r 1:");
		position(&f, 1, 1);

		igs->math[0] = INT16_MAX;
		feed(&f, "G#g>2:p>3,2:r>0:p>7,5:r>0]:p>8,6:r 1:");
		position(&f, 8, 6);
		feed(&f, "G#p 9,8:\033jG#p>3,2:r 1:");
		position(&f, 10, 9);
		feed(&f, "\033k");
		position(&f, 1, 1);
		CHECK(memcmp(&attributes, &igs->attributes, sizeof(attributes)) == 0);
		CHECK(memcmp(palette, igs->palette, sizeof(palette)) == 0);
		CHECK(igs->draw_x == 19 && igs->draw_y == 17 && igs->random_seed == 1);
		CHECK(igs->wait_deadline == 0);
		struct st_pixel_buffer *pixels = NULL;
		CHECK(st_console_get_pixels(&pixels, f.console, &full, false) == 0);
		for (size_t i = 0; i < (size_t)igs->width * igs->height; ++i)
			CHECK(pixels->primary[i] == 769 && pixels->alternate[i] == 768);
		st_pixel_buffer_destroy(pixels);
		destroy(&f);
	}
}

static void
composition_case(struct fixture *f, unsigned resolution, unsigned state, bool reverse,
    bool saved, int selector)
{
	static const char *prefixes[] = {"", "\033", "\033Y", "\033Y!", "\033b", "\033c"};
	struct cterminal *t = f->terminal;
	struct cterm_igs *igs = t->igs_engine;
	feed(f, "\033q\033f\033wG#c>1,15:c 0,0:\033E\033k"
	    "G#c>0,2:p 7,5:");
	if (saved)
		feed(f, "\033j");
	feed(f, "G#p 3,2:");
	if (reverse)
		feed(f, "\033p");
	if (state == 6) {
		char bottom[] = {27, 'Y', (char)(31 + t->height), 0};
		feed(f, "\033v");
		feed(f, bottom);
	} else {
		feed(f, prefixes[state]);
	}
	struct model m = {
		.state = state == 6 ? 3 : state,
		.x = 3, .y = 2, .row = state == 6 ? (unsigned)t->height - 1 : 1,
		.columns = (unsigned)t->width, .rows = (unsigned)t->height,
		.fg = (uint16_t)(15 - 32), .bg = (uint16_t)(2 - 32),
		.reverse = reverse, .wrap = state == 6,
		.saved = saved, .saved_x = 7, .saved_y = 5,
	};
	model_byte(&m, 27);
	if (selector >= 0)
		model_byte(&m, selector == 1 ? 'k' : 'j');
	igs->math[0] = (int16_t)selector;
	feed(f, "G#r 0]:");
	position(f, (int)m.x + 1, (int)m.y + 1);
	CHECK(t->vt52_foreground == m.fg && t->vt52_background == m.bg);
	CHECK(t->negative == m.reverse);
	CHECK((t->accumulator != NULL) == (m.state != 0));
	CHECK((t->sequence != 0) == (m.state == 1));
	if (m.state >= 2)
		CHECK(t->seq_trailing_remain == (m.state == 2 ? 2 : 1));
	model_pixels(f, resolution, &m);

	/* Complete any pending bytes before probing the save slot. */
	feed(f, "\001\001G#p>9,8:r 1:");
	position(f, m.saved ? (int)m.saved_x + 1 : 1,
	    m.saved ? (int)m.saved_y + 1 : 1);
	feed(f, "\033k");
	position(f, 1, 1);
}

static void
compositions(void)
{
	static const int selectors[] = {INT16_MIN, 0, 1, 2};
	for (unsigned resolution = 0; resolution < 3; ++resolution)
		for (unsigned reserved = 0; reserved < 2; ++reserved) {
			struct fixture f;
			create(&f, resolution, reserved != 0);
			unsigned cell_height = resolution == 2 ? 16 : 8;
			if (reserved) {
				const struct st_console_rect status = {0, 24 * cell_height,
				    f.terminal->igs_engine->width, cell_height};
				seed(&f, &status, 769, 768);
			}
			for (unsigned state = 0; state < 7; ++state)
				for (unsigned reverse = 0; reverse < 2; ++reverse)
					for (unsigned saved = 0; saved < 2; ++saved)
						for (size_t i = 0; i < sizeof(selectors) / sizeof(*selectors); ++i)
							composition_case(&f, resolution, state, reverse != 0,
							    saved != 0, selectors[i]);
			destroy(&f);
		}
}

static void
partitions(void)
{
	const char wire[] = "G#p>3,2:r>r:p>7,5:r>1:M 2:A";
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
		position(&f, 5, 3);
		const struct st_console_rect area = {3, 2, 1, 1};
		struct st_console_cell cell;
		CHECK(st_console_read_cells(f.console, &area, &cell, 1) == 0 && cell.glyph == 'A');
		CHECK(igs->random_seed == UINT32_C(0xbb40e62e) && igs->attributes.writing_mode == 2);
		CHECK(f.terminal->accumulator == NULL && igs->input_deadline == 0);
	}
	destroy(&f);
}

static void
lifetime(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f;
		create(&f, resolution, false);
		struct cterminal *t = f.terminal;
		struct cterm_igs *igs = t->igs_engine;
		t->response_cb = no_reply;
		feed(&f, "G#p>3,2:r>0:p 7,5:");
		feed(&f, "G#r 0");
		CHECK(st_cterm_finish(t) == 0 && t->accumulator == NULL);
		feed(&f, "G#r 0");
		local_ms += 5001;
		CHECK(st_cterm_poll(t) == 0);
		const char bad[] = "G#r 10000:";
		size_t used;
		CHECK(st_cterm_write(t, bad, sizeof(bad) - 1, &used) == 0);
		position(&f, 8, 6);
		feed(&f, "G#I>0:I>1:I>2:I>3:I 4:");
		char same[32];
		(void)snprintf(same, sizeof(same), "G#R %u,0:", resolution);
		feed(&f, same);
		feed(&f, "\033EG#r 1:");
		position(&f, 4, 3);

		feed(&f, "\033jG#p 7,5:");
		feed(&f, resolution == 0 ? "G#R 1,0:" : "G#R 0,0:");
		if (cterm_igs_paused(igs)) {
			local_ms += 17;
			CHECK(st_cterm_poll(t) == 0);
			CHECK(t->save_xpos == 4 && t->save_ypos == 3);
		}
		feed(&f, "G#p>7,5:r 1:");
		position(&f, resolution == 2 ? 4 : 1, resolution == 2 ? 3 : 1);

		/* I5 still reinitializes an already selected color resolution. */
		feed(&f, "G#I 5:");
		if (cterm_igs_paused(igs)) {
			local_ms += 17;
			CHECK(st_cterm_poll(t) == 0);
		}
		feed(&f, "G#p>3,2:r 0:G#I 5:");
		if (cterm_igs_paused(igs)) {
			local_ms += 17;
			CHECK(st_cterm_poll(t) == 0);
		}
		feed(&f, "G#p>7,5:r 1:");
		position(&f, resolution == 2 ? 4 : 1, resolution == 2 ? 3 : 1);

		feed(&f, "G#p>3,2:r>0:p 7,5:");
		t->screen.error = IO_ERROR_IO;
		const char restore[] = "G#r 1:";
		CHECK(st_cterm_write(t, restore, sizeof(restore) - 1, &used) == 0);
		position(&f, 8, 6);
		CHECK(t->accumulator != NULL && t->sequence != 0);
		feed(&f, "k");
		position(&f, 4, 3);
		feed(&f, "\033j");
		CHECK(st_cterm_reset_terminal(t) == 0);
		feed(&f, "G#p>7,5:r 1:");
		position(&f, 1, 1);
		destroy(&f);
	}
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
	feed(&f, "\033Y\042\043\033k");
	position(&f, 4, 3);
	feed(&f, "\033j\033Y\045\047\033k");
	position(&f, 4, 3);
	feed(&f, "\033Y\046\050\033k");
	position(&f, 4, 3);
	destroy(&f);
}

int
st_cterm_igs_cursor_save_test_main(void)
{
	normal();
	compositions();
	partitions();
	lifetime();
	standalone_vt52();
	return 0;
}
