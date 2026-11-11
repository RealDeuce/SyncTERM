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
state_only(struct fixture *f)
{
	struct cterminal *t = f->terminal;
	struct cterm_igs *igs = t->igs_engine;
	feed(f, "G#c>1,3:c>0,2:p>3,2:X 10,19,17:");
	const struct st_console_rect full = {0, 0, igs->width, igs->height};
	seed(f, &full, 769, 768);
	struct cterm_igs_attributes attributes;
	uint32_t palette[16];
	memcpy(&attributes, &igs->attributes, sizeof(attributes));
	memcpy(palette, igs->palette, sizeof(palette));

	feed(f, "G#v>1:v 1:");
	CHECK(t->negative);
	feed(f, "\033q");
	CHECK(!t->negative);
	feed(f, "\033pG#v>9999:v 0:");
	CHECK(!t->negative);
	feed(f, "G#v 1:");
	CHECK(t->negative);
	CHECK(t->xpos == 4 && t->ypos == 3);
	CHECK(t->vt52_foreground == (uint16_t)(3 - 32));
	CHECK(t->vt52_background == (uint16_t)(2 - 32));
	CHECK(memcmp(&attributes, &igs->attributes, sizeof(attributes)) == 0);
	CHECK(memcmp(palette, igs->palette, sizeof(palette)) == 0);
	CHECK(igs->draw_x == 19 && igs->draw_y == 17 && igs->wait_deadline == 0);
	struct st_pixel_buffer *pixels = NULL;
	CHECK(st_console_get_pixels(&pixels, f->console, &full, false) == 0);
	for (size_t i = 0; i < (size_t)igs->width * igs->height; ++i)
		CHECK(pixels->primary[i] == 769 && pixels->alternate[i] == 768);
	st_pixel_buffer_destroy(pixels);
}

static void
composition(struct fixture *f, unsigned resolution, unsigned state, bool reverse, int selector)
{
	static const char *prefixes[] = {"", "\033", "\033Y", "\033Y!", "\033b", "\033c"};
	struct cterminal *t = f->terminal;
	feed(f, "\033q\033f\033wG#c>1,15:c 0,0:\033E"
	    "G#c>1,3:c>0,2:p 3,2:");
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
		.fg = (uint16_t)(3 - 32), .bg = (uint16_t)(2 - 32),
		.reverse = reverse, .wrap = state == 6,
	};
	model_byte(&m, 27);
	if (selector >= 0)
		model_byte(&m, selector == 1 ? 'p' : 'q');
	t->igs_engine->math[0] = (int16_t)selector;
	feed(f, "G#v 0]:");
	model_check_console(t, &m);

	/* Probe the resulting raster colors with ordinary output as well. */
	model_byte(&m, 'A');
	feed(f, "A");
	model_check_console(t, &m);
	model_pixels(f, resolution, &m);
	feed(f, "\001\001");
}

static void
compositions(void)
{
	static const int selectors[] = {INT16_MIN, 0, 1, INT16_MAX};
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		for (unsigned reserved = 0; reserved < 2; ++reserved) {
			struct fixture f;
			create(&f, resolution, reserved != 0);
			f.terminal->response_cb = no_reply;
			if (reserved) {
				unsigned cell_height = resolution == 2 ? 16 : 8;
				const struct st_console_rect status = {0, 24 * cell_height,
				    f.terminal->igs_engine->width, cell_height};
				seed(&f, &status, 769, 768);
			} else {
				state_only(&f);
			}
			for (unsigned state = 0; state < 7; ++state)
				for (unsigned reverse = 0; reverse < 2; ++reverse)
					for (size_t i = 0; i < sizeof(selectors) / sizeof(*selectors); ++i)
						composition(&f, resolution, state, reverse != 0, selectors[i]);
			destroy(&f);
		}
	}
}

static void
partitions(void)
{
	const char wire[] = "G#g>2:v>1:v>r:M 2:A";
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
		const struct st_console_rect area = {0, 0, 1, 1};
		struct st_console_cell cell;
		CHECK(st_console_read_cells(f.console, &area, &cell, 1) == 0 && cell.glyph == 'A');
		CHECK(!f.terminal->negative && cell.foreground == 783 && cell.background == 768);
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
		feed(&f, "G#v 1:G#v r");
		CHECK(t->negative && st_cterm_finish(t) == 0 && t->accumulator == NULL);
		feed(&f, "G#v r");
		local_ms += 5001;
		CHECK(st_cterm_poll(t) == 0 && t->negative && igs->random_seed == 1);
		const char bad[] = "G#v 10000:";
		size_t used;
		CHECK(st_cterm_write(t, bad, sizeof(bad) - 1, &used) == 0);
		CHECK(t->negative && t->accumulator == NULL);
		feed(&f, "G#I>0:I>1:I>2:I>3:I 4:");
		char same[32];
		(void)snprintf(same, sizeof(same), "G#R %u,0:", resolution);
		feed(&f, same);
		CHECK(t->negative);
		feed(&f, resolution == 0 ? "G#R 1,0:" : "G#R 0,0:");
		if (cterm_igs_paused(igs)) {
			local_ms += 17;
			CHECK(st_cterm_poll(t) == 0);
		}
		CHECK(t->negative == (resolution == 2));
		feed(&f, "G#v 1:G#I 5:");
		if (cterm_igs_paused(igs)) {
			local_ms += 17;
			CHECK(st_cterm_poll(t) == 0);
		}
		CHECK(t->negative == (resolution == 2));

		feed(&f, "\033q");
		t->screen.error = IO_ERROR_IO;
		const char on[] = "G#v 1:";
		CHECK(st_cterm_write(t, on, sizeof(on) - 1, &used) == 0);
		CHECK(!t->negative && t->accumulator != NULL && t->sequence != 0);
		feed(&f, "p");
		CHECK(t->negative);
		CHECK(st_cterm_reset_terminal(t) == 0 && !t->negative);
		destroy(&f);
	}
}

int
st_cterm_igs_reverse_test_main(void)
{
	compositions();
	partitions();
	lifetime();
	return 0;
}
