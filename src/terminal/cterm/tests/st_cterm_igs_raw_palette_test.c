/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "igs_test_fixture.h"

static void
no_reply(const char *bytes, size_t size, void *context)
{
	(void)bytes;
	(void)context;
	CHECK(size == 0);
}

/* Literal original-ST DAC levels, independent of production conversion. */
static uint32_t
rgb(unsigned word)
{
	static const unsigned levels[] = {0, 36, 73, 109, 146, 182, 219, 255};
	return levels[(word >> 8) & 7] * 65536 + levels[(word >> 4) & 7] * 256 + levels[word & 7];
}

static void
raw_case(struct fixture *f, int index, int color)
{
	struct cterm_igs *igs = f->terminal->igs_engine;
	uint16_t raw[16];
	uint32_t realized[16], saved[16], published[16];
	int requested[16][3];
	memcpy(raw, igs->raw_palette, sizeof(raw));
	memcpy(realized, igs->palette, sizeof(realized));
	memcpy(saved, igs->saved_palette, sizeof(saved));
	memcpy(requested, igs->requested_palette, sizeof(requested));
	if (color >= 0) {
		unsigned physical = (unsigned)(index > 15 ? 15 : index) & 15;
		raw[physical] = (uint16_t)color;
		if (igs->resolution != 2)
			realized[physical] = rgb((unsigned)color);
		else if (physical == 0) {
			realized[0] = (color & 1) ? 0xffffff : 0;
			realized[1] = realized[0] ^ UINT32_C(0xffffff);
		}
	}
	igs->math[0] = (int16_t)index;
	igs->math[1] = (int16_t)color;
	uint32_t final = literal_words(igs, (int[]){index, color}, 2);
	feed(f, "G#X 1,0],1]:");
	CHECK(igs->random_seed == final);
	CHECK(memcmp(raw, igs->raw_palette, sizeof(raw)) == 0);
	CHECK(memcmp(realized, igs->palette, sizeof(realized)) == 0);
	CHECK(memcmp(saved, igs->saved_palette, sizeof(saved)) == 0);
	CHECK(memcmp(requested, igs->requested_palette, sizeof(requested)) == 0);
	CHECK(st_console_get_palette(f->console, 768, published, 16) == 0);
	CHECK(memcmp(realized, published, sizeof(realized)) == 0);
}

static void
words_and_state(void)
{
	static const int indices[] = {INT16_MIN, -17, -16, -1, 0, 1, 3, 15, 16, 9999, INT16_MAX};
	static const int colors[] = {INT16_MIN, -1, 0, 1, 0x777, 0x7888, INT16_MAX};
	for (unsigned resolution = 0; resolution < 3; ++resolution)
		for (unsigned reserved = 0; reserved < 2; ++reserved) {
			struct fixture f;
			create(&f, resolution, reserved != 0);
			struct cterminal *t = f.terminal;
			struct cterm_igs *igs = t->igs_engine;
			t->response_cb = no_reply;
			feed(&f, "\033fG#p>3,2:X 10,19,17:\033b");
			struct cterm_igs_attributes attributes;
			memcpy(&attributes, &igs->attributes, sizeof(attributes));
			const struct st_console_rect full = {0, 0, igs->width, igs->height};
			seed(&f, &full, 769, 768);
			for (unsigned scaling = 0; scaling < 3; ++scaling) {
				char setup[16];
				(void)snprintf(setup, sizeof(setup), "G#g %u:", scaling);
				feed(&f, setup);
				for (size_t i = 0; i < sizeof(indices) / sizeof(*indices); ++i)
					for (size_t c = 0; c < sizeof(colors) / sizeof(*colors); ++c)
						raw_case(&f, indices[i], colors[c]);
				CHECK(igs->scaling_mode == (int)scaling && igs->scaling_resolution == (int)resolution);
			}
			if (!reserved && resolution == 0)
				for (int index = INT16_MIN; index <= INT16_MAX; ++index)
					raw_case(&f, index, 0x715);
			if (!reserved && resolution != 1)
				for (int color = INT16_MIN; color <= INT16_MAX; ++color)
					raw_case(&f, resolution == 0 ? -1 : 0, color);
			CHECK(memcmp(&attributes, &igs->attributes, sizeof(attributes)) == 0);
			CHECK(t->xpos == 4 && t->ypos == 3 && t->accumulator != NULL);
			CHECK(t->seq_trailing_remain == 1 && t->vt52_foreground == 15 && t->vt52_background == 0);
			CHECK(igs->draw_x == 19 && igs->draw_y == 17 && igs->wait_deadline == 0);
			struct st_pixel_buffer *pixels = NULL;
			CHECK(st_console_get_pixels(&pixels, f.console, &full, false) == 0);
			for (size_t i = 0; i < (size_t)igs->width * igs->height; ++i)
				CHECK(pixels->primary[i] == 769 && pixels->alternate[i] == 768);
			st_pixel_buffer_destroy(pixels);
			feed(&f, "\001");
			destroy(&f);
		}
}

static void
partitions_and_errors(void)
{
	const char *wires[] = {"AG#g>2:X>1,1,1792:M 2:B", "AG#g>2:X>r,r,R:M 2:B"};
	for (unsigned resolution = 0; resolution < 3; ++resolution)
		for (size_t which = 0; which < sizeof(wires) / sizeof(*wires); ++which) {
			struct fixture f;
			create(&f, resolution, false);
			struct cterminal *t = f.terminal;
			struct cterm_igs *igs = t->igs_engine;
			t->response_cb = no_reply;
			const size_t size = strlen(wires[which]);
			for (size_t split = 0; split <= size + 1; ++split) {
				CHECK(st_cterm_reset_terminal(t) == 0);
				/* Chapter 124's recurrence yields selector 1, index 240,
				 * then color 51 from this seed. */
				igs->random_seed = 109;
				igs->random_range[0][1] = igs->random_range[1][1] = 255;
				size_t used;
				if (split > size) {
					for (size_t i = 0; i < size; ++i)
						CHECK(st_cterm_write(t, wires[which] + i, 1, &used) == 0 && used == 1);
				} else {
					CHECK(st_cterm_write(t, wires[which], split, &used) == 0 && used == split);
					CHECK(st_cterm_write(t, wires[which] + split, size - split, &used) == 0 && used == size - split);
				}
				CHECK(igs->attributes.writing_mode == 2 && t->xpos == 3 && t->ypos == 1);
				const struct st_console_rect area = {0, 0, 2, 1};
				struct st_console_cell cells[2];
				CHECK(st_console_read_cells(f.console, &area, cells, 2) == 0);
				CHECK(cells[0].glyph == 'A' && cells[1].glyph == 'B');
				if (which == 0)
					CHECK(igs->raw_palette[1] == 0x700 && igs->random_seed == 109);
				else
					CHECK(igs->raw_palette[15] == 51 && igs->random_seed == UINT32_C(0x84c43368));
			}
			uint16_t raw[16];
			memcpy(raw, igs->raw_palette, sizeof(raw));
			const char *partial[] = {"G#X 1", "G#X 1,0", "G#X 1,0,1792"};
			for (size_t i = 0; i < sizeof(partial) / sizeof(*partial); ++i) {
				feed(&f, partial[i]);
				CHECK(st_cterm_finish(t) == 0 && memcmp(raw, igs->raw_palette, sizeof(raw)) == 0);
				feed(&f, partial[i]);
				local_ms += 5001;
				CHECK(st_cterm_poll(t) == 0 && memcmp(raw, igs->raw_palette, sizeof(raw)) == 0);
			}
			const char bad[] = "G#X 1,0,10000:";
			size_t used;
			CHECK(st_cterm_write(t, bad, sizeof(bad) - 1, &used) == 0);
			CHECK(memcmp(raw, igs->raw_palette, sizeof(raw)) == 0);
			destroy(&f);
		}
}

static void
lifetime(void)
{
	/* A register outside medium's visible four colors survives the mode
	 * change and becomes available through the same indexed palette bank. */
	struct fixture medium;
	create(&medium, 1, false);
	feed(&medium, "G#X>1,15,1792:R 0,0:");
	struct cterm_igs *retained = medium.terminal->igs_engine;
	local_ms = retained->wait_deadline;
	CHECK(st_cterm_poll(medium.terminal) == 0);
	CHECK(retained->resolution == 0 && retained->raw_palette[15] == 0x700);
	CHECK(retained->palette[15] == 0xff0000);
	destroy(&medium);
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f;
		create(&f, resolution, false);
		struct cterm_igs *igs = f.terminal->igs_engine;
		feed(&f, "G#S>0,7,7,7:X 1,0,0:");
		CHECK(igs->requested_palette[0][0] == 1000 && igs->palette[0] == 0);
		feed(&f, "G#I 2:");
		CHECK(igs->raw_palette[0] == 0 && igs->requested_palette[0][0] == 1000);
		feed(&f, resolution == 0 ? "G#R 1,0:" : "G#R 0,0:");
		if (st_cterm_input_paused(f.terminal)) {
			local_ms = igs->wait_deadline;
			CHECK(st_cterm_poll(f.terminal) == 0);
		}
		CHECK(igs->raw_palette[0] == 0 && igs->palette[0] == 0);
		CHECK(igs->requested_palette[0][0] == 1000);
		feed(&f, "G#I 5:");
		if (st_cterm_input_paused(f.terminal)) {
			local_ms = igs->wait_deadline;
			CHECK(st_cterm_poll(f.terminal) == 0);
		}
		CHECK(igs->raw_palette[0] == 0 && igs->palette[0] == 0);
		CHECK(st_cterm_reset_terminal(f.terminal) == 0 && igs->palette[0] == 0xffffff);
		destroy(&f);
	}
}

int
st_cterm_igs_raw_palette_test_main(void)
{
	words_and_state();
	partitions_and_errors();
	lifetime();
	return 0;
}
