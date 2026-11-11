/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "igs_console_test_model.h"

static const char *const prefixes[] = {"", "\033", "\033Y", "\033Y!", "\033b", "\033c"};
static const int selectors[] = {INT16_MIN, 0, 1, 2, 3, 4, INT16_MAX};

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
	feed(f, "\033q\033w\033fG#c>1,15:c 0,0:\033E"
	    "G#c>1,4:c>0,3:S 0,0,0,0:");
}

static void
erase_case(struct fixture *f, unsigned resolution, unsigned x, unsigned y, bool reverse, int selector)
{
	struct cterminal *t = f->terminal;
	struct cterm_igs *igs = t->igs_engine;
	prepare(f);
	if (reverse)
		feed(f, "\033p");
	char position[32];
	(void)snprintf(position, sizeof(position), "G#p>%u,%u:X 10,19,17:", x, y);
	feed(f, position);
	igs->clip_x1 = igs->clip_y1 = 40;
	igs->clip_x2 = igs->clip_y2 = 50;
	const uint16_t depth = t->vt52_cursor_hide_depth;
	const struct st_console_rect full = {0, 0, igs->width, igs->height};
	seed(f, &full, 768, 769);
	struct cterm_igs_attributes attributes;
	uint32_t palette[16];
	memcpy(&attributes, &igs->attributes, sizeof(attributes));
	memcpy(palette, igs->palette, sizeof(palette));
	CHECK(palette[0] == 0);
	igs->math[0] = (int16_t)selector;
	feed(f, "G#s 0]:");

	int operation = selector > 5 ? 0 : selector;
	bool home = operation == 0 || operation == 4;
	CHECK(t->xpos == (home ? 1 : (int)x + 1) && t->ypos == (home ? 1 : (int)y + 1));
	CHECK(t->negative == reverse && t->vt52_cursor_hide_depth == depth);
	CHECK(t->vt52_foreground == (uint16_t)(4 - 32));
	CHECK(t->vt52_background == (uint16_t)(3 - 32));
	CHECK((t->accumulator != NULL) == (operation < 0));
	CHECK((t->sequence != 0) == (operation < 0));
	CHECK(memcmp(&attributes, &igs->attributes, sizeof(attributes)) == 0);
	CHECK(memcmp(palette, igs->palette, sizeof(palette)) == 0);
	CHECK(igs->clip_x1 == 40 && igs->clip_y1 == 40 && igs->clip_x2 == 50 && igs->clip_y2 == 50);
	CHECK(igs->draw_x == 19 && igs->draw_y == 17 && igs->wait_deadline == 0);

	const unsigned ch = resolution == 2 ? 16 : 8;
	const unsigned blank = operation == 3 || operation == 4 ? 768 : resolution == 2 ? 769 : 771;
	struct st_pixel_buffer *pixels = NULL;
	CHECK(st_console_get_pixels(&pixels, f->console, &full, false) == 0);
	for (unsigned py = 0; py < igs->height; ++py)
		for (unsigned px = 0; px < igs->width; ++px) {
			unsigned row = py / ch, col = px / 8;
			bool erased = row < (unsigned)t->height && (home || operation == 3
			    || (operation == 1 && (row < y || (row == y && col <= x)))
			    || (operation == 2 && (row > y || (row == y && col >= x))));
			size_t i = (size_t)py * igs->width + px;
			CHECK(pixels->primary[i] == (erased ? blank : 768));
			CHECK(pixels->alternate[i] == (erased ? blank : 769));
		}
	st_pixel_buffer_destroy(pixels);
	if (operation < 0)
		feed(f, "\001");
}

static void
pending_case(struct fixture *f, unsigned resolution, unsigned state, int selector)
{
	static const uint8_t letters[] = {'E', 'd', 'J', 0, 'E'};
	struct cterminal *t = f->terminal;
	prepare(f);
	feed(f, "\033pG#p 3,2:");
	feed(f, prefixes[state]);
	struct model m = {
		.state = state, .x = 3, .y = 2, .row = 1,
		.columns = (unsigned)t->width, .rows = (unsigned)t->height,
		.fg = (uint16_t)(4 - 32), .bg = (uint16_t)(3 - 32), .reverse = true,
	};
	if (selector != 3) {
		model_byte(&m, 27);
		if (selector >= 0)
			model_byte(&m, letters[selector > 5 ? 0 : selector]);
	}
	if (selector == 4)
		m.count = 0;
	t->igs_engine->math[0] = (int16_t)selector;
	feed(f, "G#s 0]:");
	model_check_console(t, &m);
	model_pixels(f, resolution, &m);
	model_byte(&m, 'A');
	feed(f, "A");
	model_check_console(t, &m);
	model_pixels(f, resolution, &m);
	feed(f, "\001\001");
}

static void
reset_string(unsigned resolution, bool reserved, unsigned state)
{
	struct fixture f;
	create_with_history(&f, resolution, reserved, 64);
	struct cterminal *t = f.terminal;
	prepare(&f);
	feed(&f, "\033p\033vG#p 255,255:");
	feed(&f, prefixes[state]);
	if (reserved) {
		unsigned ch = resolution == 2 ? 16 : 8;
		const struct st_console_rect status = {0, 24 * ch, t->igs_engine->width, ch};
		seed(&f, &status, 769, 768);
	}
	uint32_t columns;
	size_t before, after;
	CHECK(st_cterm_scrollback_size(t, &columns, &before) == 0);
	feed(&f, "G#s 5:");
	const bool scrolled = state == 0 || state == 2;
	CHECK(st_cterm_scrollback_size(t, &columns, &after) == 0);
	CHECK(after == before + (size_t)t->height + scrolled);
	if (scrolled) {
		struct st_console_cell row[80];
		CHECK(st_cterm_copy_scrollback_rows(t, row, columns, 1, after - 2, 1, 0) == 1);
		const struct st_console_cell q = row[columns - 1];
		CHECK(q.glyph == 'q');
		CHECK(q.foreground == (resolution == 2 ? 769u : 771u));
		CHECK(q.background == (resolution == 0 ? 772u : 768u));
	}
	struct model m = {
		.columns = (unsigned)t->width, .rows = (unsigned)t->height,
		.fg = resolution == 0 ? 31 : 19, .bg = 16, .reverse = scrolled, .wrap = true,
	};
	model_check_console(t, &m);
	model_pixels(&f, resolution, &m);
	destroy(&f);
}

static void
scenes(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution)
		for (unsigned reserved = 0; reserved < 2; ++reserved) {
			struct fixture f;
			create(&f, resolution, reserved != 0);
			struct cterminal *t = f.terminal;
			t->response_cb = no_reply;
			const unsigned x[] = {0, 3, (unsigned)t->width - 1};
			const unsigned y[] = {0, 6, (unsigned)t->height - 1};
			for (size_t point = 0; point < sizeof(x) / sizeof(*x); ++point)
				for (unsigned reverse = 0; reverse < 2; ++reverse)
					for (size_t i = 0; i < sizeof(selectors) / sizeof(*selectors); ++i)
						erase_case(&f, resolution, x[point], y[point], reverse != 0, selectors[i]);
			if (reserved) {
				unsigned ch = resolution == 2 ? 16 : 8;
				const struct st_console_rect status = {0, 24 * ch, t->igs_engine->width, ch};
				seed(&f, &status, 769, 768);
			}
			for (unsigned state = 1; state < 6; ++state)
				for (size_t i = 0; i < sizeof(selectors) / sizeof(*selectors); ++i)
					pending_case(&f, resolution, state, selectors[i]);
			destroy(&f);
			for (unsigned state = 0; state < 6; ++state)
				reset_string(resolution, reserved != 0, state);
		}
}

static void
workstation_history(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f;
		create_with_history(&f, resolution, true, 64);
		struct cterminal *t = f.terminal;
		prepare(&f);
		uint32_t columns;
		size_t before, after;
		CHECK(st_cterm_scrollback_size(t, &columns, &before) == 0);
		feed(&f, "G#p 3,2:AG#s 3:");
		CHECK(st_cterm_scrollback_size(t, &columns, &after) == 0 && before == after);
		feed(&f, "G#p 3,2:AG#s 4:");
		CHECK(st_cterm_scrollback_size(t, &columns, &after) == 0);
		CHECK(after == before + (size_t)t->height);
		struct st_console_cell row[80];
		CHECK(st_cterm_copy_scrollback_rows(t, row, columns, 1, before + 2, 1, 0) == 1);
		CHECK(row[3].glyph == 'A');
		destroy(&f);
	}
}

static void
partitions_and_recovery(void)
{
	const char wire[] = "G#g>2:s>r:M 2:";
	const size_t size = sizeof(wire) - 1;
	struct fixture f;
	create(&f, 0, false);
	struct cterminal *t = f.terminal;
	for (size_t split = 0; split <= size + 1; ++split) {
		CHECK(st_cterm_reset_terminal(t) == 0);
		feed(&f, "G#p 3,2:");
		t->igs_engine->random_range[0][1] = 255;
		size_t used;
		if (split > size) {
			for (size_t i = 0; i < size; ++i)
				CHECK(st_cterm_write(t, wire + i, 1, &used) == 0 && used == 1);
		} else {
			CHECK(st_cterm_write(t, wire, split, &used) == 0 && used == split);
			CHECK(st_cterm_write(t, wire + split, size - split, &used) == 0 && used == size - split);
		}
		CHECK(t->xpos == 1 && t->ypos == 1 && t->accumulator == NULL);
		CHECK(t->igs_engine->random_seed == UINT32_C(0xbb40e62e));
		CHECK(t->igs_engine->attributes.writing_mode == 2 && t->igs_engine->input_deadline == 0);
	}
	feed(&f, "G#p 3,2:G#s r");
	CHECK(st_cterm_finish(t) == 0);
	feed(&f, "G#s r");
	local_ms += 5001;
	CHECK(st_cterm_poll(t) == 0 && t->igs_engine->random_seed == UINT32_C(0xbb40e62e));
	const char bad[] = "G#s 10000:";
	size_t used;
	CHECK(st_cterm_write(t, bad, sizeof(bad) - 1, &used) == IO_ERROR_OUT_OF_RANGE);
	CHECK(t->xpos == 4 && t->ypos == 3 && t->accumulator == NULL);
	t->screen.error = IO_ERROR_IO;
	const char clear[] = "G#s 4:";
	CHECK(st_cterm_write(t, clear, sizeof(clear) - 1, &used) == IO_ERROR_IO);
	CHECK(t->xpos == 4 && t->ypos == 3 && t->sequence != 0);
	feed(&f, "E");
	CHECK(t->xpos == 1 && t->ypos == 1 && t->accumulator == NULL);
	destroy(&f);
}

int
st_cterm_igs_screen_erase_test_main(void)
{
	scenes();
	workstation_history();
	partitions_and_recovery();
	return 0;
}
