/* k: native cursor visibility and receive-only destructive backspace. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "igs_test_fixture.h"

static void
expect_cell(struct fixture *f, unsigned x, unsigned y, uint32_t glyph)
{
	const struct st_console_rect area = {x, y, 1, 1};
	struct st_console_cell cell;
	CHECK(st_console_read_cells(f->console, &area, &cell, 1) == 0);
	CHECK(cell.glyph == glyph);
}

static void
expect_cursor(struct fixture *f, bool visible)
{
	bool actual;
	enum st_console_cursor_shape shape;
	CHECK(st_console_get_cursor(f->console, &actual, &shape) == 0);
	CHECK(actual == visible);
	if (visible)
		CHECK(shape == ST_CONSOLE_CURSOR_BLOCK);
}

static void
backspace(struct fixture *f)
{
	struct cterminal *t = f->terminal;
	feed(f, "ABC");
	const struct st_console_rect area = {16, 0, 8, t->igs_engine->resolution == 2 ? 16 : 8};
	seed(f, &area, 769, 768);
	feed(f, "G#k 2:\b");
	CHECK(t->xpos == 3 && t->ypos == 1);
	expect_cell(f, 1, 0, 'B');
	expect_cell(f, 2, 0, ' ');
	struct st_pixel_buffer *pixels = NULL;
	CHECK(st_console_get_pixels(&pixels, f->console, &area, false) == 0);
	for (size_t i = 0; i < (size_t)area.width * area.height; ++i)
		CHECK(pixels->primary[i] == 768 && pixels->alternate[i] == 768);
	st_pixel_buffer_destroy(pixels);
	feed(f, "CG#k 3:\b");
	expect_cell(f, 2, 0, 'C');

	feed(f, "G#k 2:");
	size_t used;
	CHECK(st_cterm_write_utf8(t, "D\b", 2, &used) == 0 && used == 2);
	expect_cell(f, 2, 0, 'D');
	feed(f, "G#W 100,60,A\bB@");
	CHECK(t->xpos == 3 && t->ypos == 1);
	CHECK(t->igs_engine->shared_text_size == 3 && t->igs_engine->shared_text[1] == 8);

	/* The original BS becomes a color parameter. The extra space and BS
	 * still use the resulting state and leave the cursor on that space. */
	feed(f, "\033bG#k 2:\b");
	CHECK(t->vt52_foreground == (uint16_t)(8 - 32));
	CHECK(t->xpos == 3 && t->ypos == 1);
	expect_cell(f, 2, 0, ' ');
	feed(f, "G#p 0,0:\b");
	CHECK(t->xpos == 1);
	expect_cell(f, 0, 0, ' ');
}

static void
visibility(struct fixture *f)
{
	struct cterminal *t = f->terminal;
	expect_cursor(f, false);
	CHECK(t->vt52_cursor_hide_depth == 1);
	feed(f, "G#k>0:k 0:");
	CHECK(t->vt52_cursor_hide_depth == 3);
	feed(f, "G#k 1:");
	expect_cursor(f, true);
	CHECK(t->vt52_cursor_hide_depth == 0);
	feed(f, "G#k 4:");
	expect_cursor(f, false);
	t->igs_engine->math[0] = INT16_MIN;
	feed(f, "G#k 0]:");
	CHECK(t->accumulator != NULL);
	feed(f, "e");
	expect_cursor(f, true);
	feed(f, "\033f\033bG#k 1:");
	expect_cursor(f, false);
	CHECK(t->vt52_foreground == (uint16_t)(27 - 32));
	expect_cell(f, 0, 0, 'e');
	feed(f, "\033e");
	expect_cursor(f, true);
	t->vt52_cursor_hide_depth = UINT16_MAX;
	feed(f, "\033f");
	CHECK(t->vt52_cursor_hide_depth == UINT16_MAX);
	expect_cursor(f, false);
	feed(f, "\033e");
	expect_cursor(f, true);

	feed(f, "G#k>2:I>0:I 2:");
	CHECK(t->igs_engine->destructive_backspace);
	feed(f, t->igs_engine->resolution == 0 ? "G#R 1,0:" : "G#R 0,0:");
	if (cterm_igs_paused(t->igs_engine)) {
		local_ms += 17;
		CHECK(st_cterm_poll(t) == 0);
		expect_cursor(f, false);
		CHECK(t->vt52_cursor_hide_depth == 1);
	}
	CHECK(t->igs_engine->destructive_backspace);
	CHECK(st_cterm_reset_terminal(t) == 0);
	expect_cursor(f, false);
	feed(f, "AB\b");
	expect_cell(f, 1, 0, 'B');
}

static void
partitions(void)
{
	const char wire[] = "ABG#k 2:\bG#k>0:k 1:";
	const size_t size = sizeof(wire) - 1;
	struct fixture f;
	create(&f, 0, false);
	for (size_t split = 0; split <= size + 1; ++split) {
		CHECK(st_cterm_reset_terminal(f.terminal) == 0);
		size_t used;
		if (split > size) {
			for (size_t i = 0; i < size; ++i)
				CHECK(st_cterm_write(f.terminal, wire + i, 1, &used) == 0 && used == 1);
		} else {
			CHECK(st_cterm_write(f.terminal, wire, split, &used) == 0 && used == split);
			CHECK(st_cterm_write(f.terminal, wire + split, size - split, &used) == 0 && used == size - split);
		}
		expect_cell(&f, 0, 0, 'A');
		expect_cell(&f, 1, 0, ' ');
		expect_cursor(&f, true);
	}
	destroy(&f);
}

static void
recovery(void)
{
	struct fixture f;
	create(&f, 0, false);
	struct cterminal *t = f.terminal;
	feed(&f, "G#k 2");
	CHECK(st_cterm_finish(t) == 0 && !t->igs_engine->destructive_backspace);
	feed(&f, "G#k 2");
	local_ms += 5001;
	CHECK(st_cterm_poll(t) == 0 && !t->igs_engine->destructive_backspace);
	size_t used;
	const char invalid[] = "G#k 10000:";
	CHECK(st_cterm_write(t, invalid, sizeof(invalid) - 1, &used) == 0);
	expect_cursor(&f, false);
	t->screen.error = IO_ERROR_IO;
	const char enable[] = "G#k 1:";
	CHECK(st_cterm_write(t, enable, sizeof(enable) - 1, &used) == 0);
	CHECK(t->accumulator != NULL && t->vt52_cursor_hide_depth == 1);
	feed(&f, "e");
	expect_cursor(&f, true);
	destroy(&f);
}

int
st_cterm_igs_cursor_test_main(void)
{
	partitions();
	recovery();
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f;
		create(&f, resolution, resolution == 1);
		backspace(&f);
		visibility(&f);
		destroy(&f);
	}
	return 0;
}
