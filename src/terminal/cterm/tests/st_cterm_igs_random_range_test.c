/* X2: ordered range updates and independent arity/boundary expectations. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "igs_test_fixture.h"

static void
range_case(struct fixture *f, int first, int second, int third)
{
	struct cterm_igs *igs = f->terminal->igs_engine;
	igs->random_range[0][0] = 10; igs->random_range[0][1] = 20;
	igs->random_range[1][0] = 15; igs->random_range[1][1] = 14;
	igs->math[0] = (int16_t)first; igs->math[1] = (int16_t)second; igs->math[2] = (int16_t)third;
	igs->random_seed = 1;
	uint32_t final = 1;
	/* Exhaustive first-word inputs include both random encodings. Make
	 * R's interval valid for that case; the first accepted byte in either
	 * [10,20] or [15,20] is 20 on call 25. */
	int retained_upper = first == -32764 ? 20 : 14;
	igs->random_range[1][1] = retained_upper;
	if (first == -32765 || first == -32764) { first = 20; final = UINT32_C(0x3adc14f6); }
	int lower = first < 0 ? 0 : first;
	bool extra = lower == second;
	int expected[2][2] = {{lower, second == first ? 20 : second}, {extra ? lower : 15, extra ? third : retained_upper}};
	for (unsigned i = 0; i < 2; ++i)
		if (expected[i][1] < expected[i][0])
			expected[i][1] = expected[i][0] == INT16_MAX ? INT16_MAX : expected[i][0] + 1;
	feed(f, extra ? "G#X 2,0],1],2]:" : "G#X 2,0],1]:");
	CHECK(memcmp(expected, igs->random_range, sizeof(expected)) == 0);
	CHECK(igs->random_seed == final && igs->text_deadline == 0 && igs->wait_deadline == 0);
}

static void
matrix(void)
{
	static const int first[] = {INT16_MIN, -1, 0, 1, 2, 100, 255, 256, 9999, 32766, INT16_MAX};
	static const int rest[] = {INT16_MIN, -1, 0, 1, 2, 100, INT16_MAX};
	for (unsigned resolution = 0; resolution < 3; ++resolution)
		for (unsigned status = 0; status < 2; ++status) {
			struct fixture f; create(&f, resolution, status != 0);
			struct cterm_igs *igs = f.terminal->igs_engine;
			feed(&f, "\033fG#p>3,2:X 10,19,17:\033b");
			struct cterm_igs_attributes attributes; memcpy(&attributes, &igs->attributes, sizeof(attributes));
			uint32_t palette[16]; memcpy(palette, igs->palette, sizeof(palette));
			const struct st_console_rect full = {0, 0, igs->width, igs->height};
			seed(&f, &full, 769, 768);
			for (unsigned scaling = 0; scaling < 3; ++scaling) {
				char setup[16]; (void)snprintf(setup, sizeof(setup), "G#g %u:", scaling); feed(&f, setup);
				for (size_t a = 0; a < sizeof(first) / sizeof(*first); ++a)
					for (size_t b = 0; b < sizeof(rest) / sizeof(*rest); ++b)
						for (size_t c = 0; c < sizeof(rest) / sizeof(*rest); ++c)
							range_case(&f, first[a], rest[b], rest[c]);
				CHECK(igs->scaling_mode == (int)scaling && igs->scaling_resolution == (int)resolution);
			}
			if (resolution == 0 && !status)
				for (int word = INT16_MIN; word <= INT16_MAX; ++word) range_case(&f, word, 0, 37);
			CHECK(memcmp(&attributes, &igs->attributes, sizeof(attributes)) == 0);
			CHECK(memcmp(palette, igs->palette, sizeof(palette)) == 0);
			CHECK(igs->draw_x == 19 && igs->draw_y == 17 && f.terminal->seq_trailing_remain == 1);
			CHECK(f.terminal->xpos == 4 && f.terminal->ypos == 3);
			struct st_pixel_buffer *pixels = NULL;
			CHECK(st_console_get_pixels(&pixels, f.console, &full, false) == 0);
			for (size_t i = 0; i < (size_t)igs->width * igs->height; ++i)
				CHECK(pixels->primary[i] == 769 && pixels->alternate[i] == 768);
			st_pixel_buffer_destroy(pixels); feed(&f, "\001"); destroy(&f);
		}
}

static void
random_operands(void)
{
	struct fixture f; create(&f, 0, false);
	struct cterm_igs *igs = f.terminal->igs_engine;
	feed(&f, "G#X 2,100,r:");
	CHECK(igs->random_range[0][0] == 100 && igs->random_range[0][1] == 129);
	CHECK(igs->random_seed == UINT32_C(0x9b958193));
	CHECK(st_cterm_reset_terminal(f.terminal) == 0);
	feed(&f, "G#X 2,100,100,R:");
	CHECK(igs->random_range[0][0] == 100 && igs->random_range[0][1] == 199);
	CHECK(igs->random_range[1][0] == 100 && igs->random_range[1][1] == 129);
	CHECK(igs->random_seed == UINT32_C(0x9b958193));
	igs->math[0] = INT16_MAX;
	feed(&f, "G#X 2,0],1:");
	CHECK(igs->random_range[0][0] == INT16_MAX && igs->random_range[0][1] == INT16_MAX);
	igs->random_seed = 4;
	feed(&f, "G#X 10,r,0:");
	/* The first acceptable candidate appears on call 89478 from seed 4. */
	CHECK(igs->draw_x == INT16_MAX && igs->random_seed == UINT32_C(0x367fff7e));
	destroy(&f);
}

static void
partitions_and_recovery(void)
{
	static const char *wires[] = {"AG#X>2,10,20:M 2:B", "AG#X>2,30,30,40:M 2:B",
	    "AG#X>2,0],0,37:M 2:B", "AG#X>2,0],0]:M 2:B", "AG#X>r,100,r:M 2:B"};
	static const int expected[][2][2] = {{{10,20},{0,199}}, {{30,199},{30,40}},
	    {{0,0},{0,37}}, {{0,199},{0,199}}, {{100,250},{0,255}}};
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f, resolution, false);
		struct cterm_igs *igs = f.terminal->igs_engine;
		for (size_t which = 0; which < sizeof(wires) / sizeof(*wires); ++which) {
			size_t size = strlen(wires[which]);
			for (size_t split = 0; split <= size + 1; ++split) {
				CHECK(st_cterm_reset_terminal(f.terminal) == 0);
				igs->math[0] = -1;
				if (which == 4) {
					igs->random_seed = 218;
					igs->random_range[0][1] = igs->random_range[1][1] = 255;
				}
				size_t used;
				if (split > size) {
					for (size_t i = 0; i < size; ++i)
						CHECK(st_cterm_write(f.terminal, wires[which] + i, 1, &used) == 0 && used == 1);
				} else {
					CHECK(st_cterm_write(f.terminal, wires[which], split, &used) == 0 && used == split);
					CHECK(st_cterm_write(f.terminal, wires[which] + split, size - split, &used) == 0 && used == size - split);
				}
				CHECK(memcmp(expected[which], igs->random_range, sizeof(igs->random_range)) == 0);
				CHECK(igs->random_seed == (which == 4 ? UINT32_C(0xeccafa98) : 1));
				CHECK(igs->attributes.writing_mode == 2 && f.terminal->xpos == 3);
				struct st_console_cell cells[2]; const struct st_console_rect area = {0, 0, 2, 1};
				CHECK(st_console_read_cells(f.console, &area, cells, 2) == 0);
				CHECK(cells[0].glyph == 'A' && cells[1].glyph == 'B');
			}
		}
		const char *partial[] = {"G#X 2,", "G#X 2,10,", "G#X 2,10,10,", "G#X r,100,"};
		for (size_t i = 0; i < sizeof(partial) / sizeof(*partial); ++i) {
			CHECK(st_cterm_reset_terminal(f.terminal) == 0);
			igs->random_seed = 218;
			igs->random_range[0][1] = igs->random_range[1][1] = 255;
			int saved[2][2]; memcpy(saved, igs->random_range, sizeof(saved));
			feed(&f, partial[i]); CHECK(st_cterm_finish(f.terminal) == 0);
			CHECK(memcmp(saved, igs->random_range, sizeof(saved)) == 0 && igs->random_seed == 218);
			feed(&f, partial[i]); local_ms += 5001; CHECK(st_cterm_poll(f.terminal) == 0);
			CHECK(memcmp(saved, igs->random_range, sizeof(saved)) == 0 && igs->random_seed == 218);
		}
		const char bad[] = "G#X 2,10,10,10000:"; size_t used;
		CHECK(st_cterm_write(f.terminal, bad, sizeof(bad) - 1, &used) == 0);
		CHECK(igs->random_range[0][0] == 0 && igs->random_range[0][1] == 255);
		igs->random_range[0][1] = 50;
		const char impossible[] = "G#X 2,100,r:";
		CHECK(st_cterm_write(f.terminal, impossible, sizeof(impossible) - 1, &used) == 0);
		CHECK(igs->random_range[0][0] == 0 && igs->random_range[0][1] == 50 && igs->random_seed == 218);
		feed(&f, "G#X 2,2,3:");
		for (unsigned operation = 0; operation < 6; ++operation) {
			char wire[24]; (void)snprintf(wire, sizeof(wire), "G#I %u:", operation); feed(&f, wire);
			if (st_cterm_input_paused(f.terminal)) { local_ms = igs->wait_deadline; CHECK(st_cterm_poll(f.terminal) == 0); }
			CHECK(igs->random_range[0][0] == 2 && igs->random_range[0][1] == 3);
		}
		feed(&f, igs->resolution == 0 ? "G#R 1,0:" : "G#R 0,0:");
		if (st_cterm_input_paused(f.terminal)) { local_ms = igs->wait_deadline; CHECK(st_cterm_poll(f.terminal) == 0); }
		CHECK(igs->random_range[0][0] == 2 && igs->random_range[0][1] == 3);
		CHECK(igs->random_range[1][0] == 0 && igs->random_range[1][1] == 255);
		feed(&f, "G#X 2,10,10,"); CHECK(st_cterm_reset_terminal(f.terminal) == 0);
		CHECK(igs->random_range[0][0] == 0 && igs->random_range[0][1] == 199);
		destroy(&f);
	}
}

int
st_cterm_igs_random_range_test_main(void)
{
	matrix(); random_operands(); partitions_and_recovery();
	return 0;
}
