/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "igs_console_scroll_test.h"

static void
scroll_case(struct fixture *f, unsigned resolution, unsigned row, bool reverse,
    int selector, int count)
{
	struct cterminal *t = f->terminal;
	struct cterm_igs *igs = t->igs_engine;
	prepare_scroll(f);
	seed_scroll_scene(f, resolution);
	if (reverse)
		feed(f, "\033p");
	char position[32];
	(void)snprintf(position, sizeof(position), "G#p>4,%u:X 10,19,17:", row);
	feed(f, position);
	const uint16_t depth = t->vt52_cursor_hide_depth;
	struct cterm_igs_attributes attributes;
	uint32_t palette[16];
	memcpy(&attributes, &igs->attributes, sizeof(attributes));
	memcpy(palette, igs->palette, sizeof(palette));

	igs->math[0] = (int16_t)selector;
	igs->math[1] = (int16_t)count;
	feed(f, "G#i 0],1]:");

	const int times = count > 0 ? count : 1;
	const bool insert = selector == 1;
	const int next_y = insert ? (int)row : times > (int)row ? 0 : (int)row - times;
	const int shift = insert ? times : times > (int)row ? times - (int)row : 0;
	CHECK(t->xpos == (insert ? 1 : 5) && t->ypos == next_y + 1 && t->negative == reverse);
	CHECK(t->vt52_foreground == (uint16_t)(4 - 32) && t->vt52_background == (uint16_t)(3 - 32));
	CHECK(t->vt52_cursor_hide_depth == depth && t->accumulator == NULL);
	CHECK(memcmp(&attributes, &igs->attributes, sizeof(attributes)) == 0);
	CHECK(memcmp(palette, igs->palette, sizeof(palette)) == 0);
	CHECK(igs->draw_x == 19 && igs->draw_y == 17 && igs->wait_deadline == 0);
	check_scroll_scene(f, resolution, insert ? row : 0, shift);
}

static void
composition(struct fixture *f, unsigned resolution, unsigned state, int selector, int count)
{
	static const char *prefixes[] = {"", "\033", "\033Y", "\033Y!", "\033b", "\033c"};
	struct cterminal *t = f->terminal;
	prepare_scroll(f);
	feed(f, "G#p 3,2:AG#p 3,3:AG#p 3,2:\033p");
	if (state == 6) {
		const char bottom[] = {27, 'Y', (char)(31 + t->height), 0};
		feed(f, "\033v");
		feed(f, bottom);
	} else {
		feed(f, prefixes[state]);
	}
	struct model m = {
		.state = state == 6 ? 3 : state, .x = 3, .y = 2,
		.row = state == 6 ? (unsigned)t->height - 1 : 1,
		.columns = (unsigned)t->width, .rows = (unsigned)t->height,
		.fg = (uint16_t)(4 - 32), .bg = (uint16_t)(3 - 32),
		.reverse = true, .wrap = state == 6,
		.count = 2, .cells = {{3, 2, 'A', 4, 3}, {3, 3, 'A', 4, 3}},
	};
	for (int i = 0; i < (count > 0 ? count : 1); ++i) {
		model_byte(&m, 27);
		if (selector >= 0)
			model_byte(&m, selector == 1 ? 'L' : 'I');
	}
	t->igs_engine->math[0] = (int16_t)selector;
	t->igs_engine->math[1] = (int16_t)count;
	feed(f, "G#i 0],1]:");
	model_check_console(t, &m);
	model_pixels(f, resolution, &m);
	feed(f, "\001\001");
}

static void
scenes(void)
{
	static const int scroll_counts[] = {INT16_MIN, 0, 2, 25};
	static const int selectors[] = {INT16_MIN, 0, 1, INT16_MAX};
	static const int counts[] = {0, 2, 3};
	for (unsigned resolution = 0; resolution < 3; ++resolution)
		for (unsigned reserved = 0; reserved < 2; ++reserved) {
			struct fixture f;
			create(&f, resolution, reserved != 0);
			struct cterminal *t = f.terminal;
			t->response_cb = no_reply;
			const unsigned positions[] = {0, 2, (unsigned)t->height - 1};
			for (size_t row = 0; row < sizeof(positions) / sizeof(*positions); ++row)
				for (unsigned reverse = 0; reverse < 2; ++reverse)
					for (int selector = 0; selector <= 2; ++selector)
						for (size_t c = 0; c < sizeof(scroll_counts) / sizeof(*scroll_counts); ++c)
							scroll_case(&f, resolution, positions[row], reverse != 0,
							    selector, scroll_counts[c]);
			if (resolution == 0 && !reserved) {
				scroll_case(&f, resolution, positions[2], true, 1, INT16_MAX);
				composition(&f, resolution, 0, -1, INT16_MAX);
			}
			if (reserved) {
				unsigned ch = resolution == 2 ? 16 : 8;
				const struct st_console_rect status = {0, 24 * ch, t->igs_engine->width, ch};
				seed(&f, &status, 769, 768);
			}
			for (unsigned state = 0; state < 7; ++state)
				for (size_t s = 0; s < sizeof(selectors) / sizeof(*selectors); ++s)
					for (size_t c = 0; c < sizeof(counts) / sizeof(*counts); ++c)
						composition(&f, resolution, state, selectors[s], counts[c]);
			destroy(&f);
		}
}

static void
partitions_and_recovery(void)
{
	const char wire[] = "G#g>2:p>3,2:i>r,3:M 2:A";
	const size_t size = sizeof(wire) - 1;
	struct fixture f;
	create_with_history(&f, 0, false, 64);
	struct cterminal *t = f.terminal;
	for (size_t split = 0; split <= size + 1; ++split) {
		CHECK(st_cterm_reset_terminal(t) == 0);
		t->igs_engine->random_range[0][1] = 255;
		feed(&f, "G#p 3,0:A");
		size_t used;
		if (split > size) {
			for (size_t i = 0; i < size; ++i)
				CHECK(st_cterm_write(t, wire + i, 1, &used) == 0 && used == 1);
		} else {
			CHECK(st_cterm_write(t, wire, split, &used) == 0 && used == split);
			CHECK(st_cterm_write(t, wire + split, size - split, &used) == 0 && used == size - split);
		}
		check_cell(&f, 3, 0, 'A');
		check_cell(&f, 3, 1, 'A');
		CHECK(t->xpos == 5 && t->ypos == 1 && t->accumulator == NULL);
		CHECK(t->igs_engine->random_seed == UINT32_C(0xbb40e62e));
		CHECK(t->igs_engine->attributes.writing_mode == 2 && t->igs_engine->input_deadline == 0);
	}
	prepare_scroll(&f);
	feed(&f, "G#p 3,0:AG#p 3,0:G#i r,");
	CHECK(st_cterm_finish(t) == 0);
	feed(&f, "G#i r,");
	local_ms += 5001;
	CHECK(st_cterm_poll(t) == 0 && t->igs_engine->random_seed == UINT32_C(0xbb40e62e));
	const char bad[] = "G#i r,10000:";
	size_t used;
	CHECK(st_cterm_write(t, bad, sizeof(bad) - 1, &used) == IO_ERROR_OUT_OF_RANGE);
	check_cell(&f, 3, 0, 'A');
	CHECK(t->xpos == 4 && t->ypos == 1 && t->accumulator == NULL);

	uint32_t columns;
	size_t before, after;
	CHECK(st_cterm_scrollback_size(t, &columns, &before) == 0);
	t->screen.error = IO_ERROR_IO;
	const char command[] = "G#i 0,2:";
	CHECK(st_cterm_write(t, command, sizeof(command) - 1, &used) == IO_ERROR_IO);
	CHECK(t->xpos == 4 && t->ypos == 1 && t->sequence != 0);
	check_cell(&f, 3, 0, 'A');
	feed(&f, "I");
	CHECK(t->xpos == 4 && t->ypos == 1 && t->accumulator == NULL);
	check_cell(&f, 3, 0, ' ');
	check_cell(&f, 3, 1, 'A');
	feed(&f, "\033LG#p 3,0:");
	CHECK(t->xpos == 4 && t->ypos == 1);
	check_cell(&f, 3, 2, 'A');
	CHECK(st_cterm_scrollback_size(t, &columns, &after) == 0 && before == after);
	destroy(&f);
}

int
st_cterm_igs_insert_lines_test_main(void)
{
	scenes();
	partitions_and_recovery();
	return 0;
}
