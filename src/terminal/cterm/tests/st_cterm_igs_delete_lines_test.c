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
	feed(f, "\033q\033w\033fG#c>1,15:c 0,0:\033E"
	    "G#c>1,4:c 0,3:");
}

static unsigned
pattern(unsigned x, unsigned y, unsigned ch, unsigned mask)
{
	return (x / 5 + y / ch + y % ch) & mask;
}

static void
scroll_case(struct fixture *f, unsigned resolution, unsigned top, bool reverse, int count)
{
	struct cterminal *t = f->terminal;
	struct cterm_igs *igs = t->igs_engine;
	const unsigned ch = resolution == 2 ? 16 : 8;
	const unsigned mask = resolution == 0 ? 15 : resolution == 1 ? 3 : 1;
	const unsigned rows = (unsigned)t->height;
	prepare(f);
	for (unsigned row = 0; row < rows; ++row) {
		char wire[24];
		(void)snprintf(wire, sizeof(wire), "G#p 3,%u:%c", row, 'A' + (int)row);
		feed(f, wire);
	}
	if (reverse)
		feed(f, "\033p");
	char position[32];
	(void)snprintf(position, sizeof(position), "G#p>%u,%u:X 10,19,17:", 4u, top);
	feed(f, position);
	const uint16_t depth = t->vt52_cursor_hide_depth;

	/* Keep the cells as landmarks but overwrite their raster in both
	 * phases with different scanline data, including the status row. */
	struct st_pixel_buffer *source = NULL;
	CHECK(st_pixel_buffer_create(&source, igs->width, igs->height, true) == 0);
	for (unsigned y = 0; y < igs->height; ++y)
		for (unsigned x = 0; x < igs->width; ++x) {
			unsigned pen = pattern(x, y, ch, mask);
			size_t i = (size_t)y * igs->width + x;
			source->primary[i] = 768 + pen;
			source->alternate[i] = 768 + (pen ^ mask);
		}
	const struct st_pixel_blit blit = {.width = igs->width, .height = igs->height, .scale_x = 1, .scale_y = 1};
	CHECK(st_console_blit_pixels(f->console, source, NULL, &blit) == 0);
	st_pixel_buffer_destroy(source);

	struct cterm_igs_attributes attributes;
	uint32_t palette[16];
	memcpy(&attributes, &igs->attributes, sizeof(attributes));
	memcpy(palette, igs->palette, sizeof(palette));
	igs->math[0] = (int16_t)count;
	feed(f, "G#d 0]:");
	CHECK(t->xpos == 1 && t->ypos == (int)top + 1 && t->negative == reverse);
	CHECK(t->vt52_foreground == (uint16_t)(4 - 32) && t->vt52_background == (uint16_t)(3 - 32));
	CHECK(t->vt52_cursor_hide_depth == depth && t->accumulator == NULL);
	CHECK(memcmp(&attributes, &igs->attributes, sizeof(attributes)) == 0);
	CHECK(memcmp(palette, igs->palette, sizeof(palette)) == 0);
	CHECK(igs->draw_x == 19 && igs->draw_y == 17 && igs->wait_deadline == 0);

	const unsigned amount = count > 0 ? (unsigned)count : 1;
	const unsigned blank = 768 + (3 & mask);
	const struct st_console_rect full = {0, 0, igs->width, igs->height};
	struct st_pixel_buffer *actual = NULL;
	CHECK(st_console_get_pixels(&actual, f->console, &full, false) == 0);
	for (unsigned y = 0; y < igs->height; ++y) {
		unsigned source_y = y / ch >= top && y / ch < rows ? y + amount * ch : y;
		bool blanked = y / ch < rows && source_y / ch >= rows;
		for (unsigned x = 0; x < igs->width; ++x) {
			unsigned pen = pattern(x, source_y, ch, mask);
			size_t i = (size_t)y * igs->width + x;
			CHECK(actual->primary[i] == (blanked ? blank : 768 + pen));
			CHECK(actual->alternate[i] == (blanked ? blank : 768 + (pen ^ mask)));
		}
	}
	st_pixel_buffer_destroy(actual);
	const struct st_console_rect column = {3, 0, 1, rows};
	struct st_console_cell cells[25];
	CHECK(st_console_read_cells(f->console, &column, cells, 1) == 0);
	for (unsigned row = 0; row < rows; ++row) {
		unsigned source_row = row < top ? row : row + amount;
		CHECK(cells[row].glyph == (source_row < rows ? 'A' + source_row : ' '));
		if (source_row >= rows)
			CHECK(cells[row].background == blank && (cells[row].flags & ST_CONSOLE_CELL_PIXEL_GRAPHICS) == 0);
	}
}

static void
composition(struct fixture *f, unsigned resolution, unsigned state, bool reverse, int count)
{
	static const char *prefixes[] = {"", "\033", "\033Y", "\033Y!", "\033b", "\033c"};
	struct cterminal *t = f->terminal;
	prepare(f);
	feed(f, "G#p 3,2:AG#p 3,3:AG#p 3,2:");
	if (reverse)
		feed(f, "\033p");
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
		.reverse = reverse, .wrap = state == 6,
		.count = 2, .cells = {{3, 2, 'A', 4, 3}, {3, 3, 'A', 4, 3}},
	};
	unsigned times = count > 0 ? (unsigned)count : 1;
	for (unsigned i = 0; i < times; ++i) {
		model_byte(&m, 27);
		model_byte(&m, 'M');
	}
	t->igs_engine->math[0] = (int16_t)count;
	feed(f, "G#d 0]:");
	model_check_console(t, &m);
	model_pixels(f, resolution, &m);
}

static void
scenes(void)
{
	static const int scroll_counts[] = {INT16_MIN, 0, 1, 2, 25};
	static const int composition_counts[] = {0, 2, 3};
	for (unsigned resolution = 0; resolution < 3; ++resolution)
		for (unsigned reserved = 0; reserved < 2; ++reserved) {
			struct fixture f;
			create(&f, resolution, reserved != 0);
			struct cterminal *t = f.terminal;
			t->response_cb = no_reply;
			const unsigned positions[] = {0, 2, (unsigned)t->height - 1};
			for (size_t top = 0; top < sizeof(positions) / sizeof(*positions); ++top)
				for (unsigned reverse = 0; reverse < 2; ++reverse)
					for (size_t i = 0; i < sizeof(scroll_counts) / sizeof(*scroll_counts); ++i)
						scroll_case(&f, resolution, positions[top], reverse != 0, scroll_counts[i]);
			if (resolution == 0 && !reserved)
				scroll_case(&f, resolution, positions[2], true, INT16_MAX);
			if (reserved) {
				unsigned ch = resolution == 2 ? 16 : 8;
				const struct st_console_rect status = {0, 24 * ch, t->igs_engine->width, ch};
				seed(&f, &status, 769, 768);
			}
			for (unsigned state = 0; state < 7; ++state)
				for (unsigned reverse = 0; reverse < 2; ++reverse)
					for (size_t i = 0; i < sizeof(composition_counts) / sizeof(*composition_counts); ++i)
						composition(&f, resolution, state, reverse != 0, composition_counts[i]);
			destroy(&f);
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
partitions_and_recovery(void)
{
	const char wire[] = "G#g>2:p>3,2:d>r:M 2:A";
	const size_t size = sizeof(wire) - 1;
	struct fixture f;
	create_with_history(&f, 0, false, 64);
	struct cterminal *t = f.terminal;
	for (size_t split = 0; split <= size + 1; ++split) {
		CHECK(st_cterm_reset_terminal(t) == 0);
		/* Chapter 124's next state for seed 218 yields count two. */
		t->igs_engine->random_seed = 218;
		t->igs_engine->random_range[0][1] = 255;
		feed(&f, "G#p 3,4:A");
		size_t used;
		if (split > size) {
			for (size_t i = 0; i < size; ++i)
				CHECK(st_cterm_write(t, wire + i, 1, &used) == 0 && used == 1);
		} else {
			CHECK(st_cterm_write(t, wire, split, &used) == 0 && used == split);
			CHECK(st_cterm_write(t, wire + split, size - split, &used) == 0 && used == size - split);
		}
		check_cell(&f, 0, 2, 'A');
		check_cell(&f, 3, 2, 'A');
		check_cell(&f, 3, 4, ' ');
		CHECK(t->xpos == 2 && t->ypos == 3 && t->accumulator == NULL);
		CHECK(t->igs_engine->random_seed == UINT32_C(0x75440253));
		CHECK(t->igs_engine->attributes.writing_mode == 2 && t->igs_engine->input_deadline == 0);
	}
	prepare(&f);
	feed(&f, "G#p 3,1:AG#p 3,0:G#d r");
	CHECK(st_cterm_finish(t) == 0);
	feed(&f, "G#d r");
	local_ms += 5001;
	CHECK(st_cterm_poll(t) == 0 && t->igs_engine->random_seed == UINT32_C(0x75440253));
	const char bad[] = "G#d 10000:";
	size_t used;
	CHECK(st_cterm_write(t, bad, sizeof(bad) - 1, &used) == IO_ERROR_OUT_OF_RANGE);
	check_cell(&f, 3, 1, 'A');
	CHECK(t->xpos == 4 && t->ypos == 1 && t->accumulator == NULL);

	uint32_t columns;
	size_t before, after;
	CHECK(st_cterm_scrollback_size(t, &columns, &before) == 0);
	t->screen.error = IO_ERROR_IO;
	const char command[] = "G#d 2:";
	CHECK(st_cterm_write(t, command, sizeof(command) - 1, &used) == IO_ERROR_IO);
	CHECK(t->xpos == 4 && t->ypos == 1 && t->sequence != 0);
	check_cell(&f, 3, 1, 'A');
	feed(&f, "M");
	CHECK(t->xpos == 1 && t->ypos == 1 && t->accumulator == NULL);
	check_cell(&f, 3, 0, 'A');
	CHECK(st_cterm_scrollback_size(t, &columns, &after) == 0 && before == after);
	destroy(&f);
}

int
st_cterm_igs_delete_lines_test_main(void)
{
	scenes();
	partitions_and_recovery();
	return 0;
}
