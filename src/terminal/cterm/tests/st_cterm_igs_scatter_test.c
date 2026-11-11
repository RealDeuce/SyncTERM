/* X0: literal reference-20/124 sampling and reference-43/44/52 marker masks. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "igs_test_fixture.h"

struct scatter_case { int ex, ey, count; unsigned points[4][2]; uint32_t seed; };
static const struct scatter_case cases[] = {
	{255,255,4,{{230,202},{48,58},{69,129},{216,45}},UINT32_C(0x37d52df9)},
	{63,63,4,{{48,58},{6,45},{54,60},{60,21}},UINT32_C(0x03cf1503)},
	{1,255,3,{{0,0},{0,0},{0,0}},UINT32_C(0x79e83a8e)},
	{-1,-1,3,{{0,0},{0,0},{0,0}},UINT32_C(0xc3e51b65)},
	{-1,255,2,{{0,0},{0,0}},UINT32_C(0x03cf1503)},
	{256,9999,4,{{230,202},{48,58},{69,129},{216,45}},UINT32_C(0x37d52df9)},
	{8,0,3,{{0,0},{0,0},{0,0}},UINT32_C(0xfe5cc4c9)},
	{0,0,3,{{0,0},{0,0},{0,0}},UINT32_C(0xc3e51b65)},
};
static const unsigned shapes[6][7] = {
	{0,0,0,0x10,0,0,0}, {0x10,0x10,0x10,0x1ff,0x10,0x10,0x10},
	{0x10,0x92,0x7c,0x10,0x7c,0x92,0x10}, {0x1ff,0x101,0x101,0x101,0x101,0x101,0x1ff},
	{0x101,0xc2,0x2c,0x10,0x2c,0xc2,0x101}, {0x10,0x2c,0xc2,0x101,0xc2,0x2c,0x10},
};
static const unsigned xor_shapes[6][7] = {
	{0,0,0,0x10,0,0,0}, {0x10,0x10,0x10,0x1ef,0x10,0x10,0x10},
	{0x10,0x92,0x7c,0x10,0x7c,0x92,0x10}, {0x1fe,0x101,0x101,0x101,0x101,0x101,0x1ff},
	{0x101,0xc2,0x2c,0,0x2c,0xc2,0x101}, {0x10,0x2c,0xc2,0,0xc2,0x2c,0},
};

static void
pixels(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution)
		for (unsigned status = 0; status < 2; ++status) {
			struct fixture f; create(&f, resolution, status != 0);
			struct cterm_igs *igs = f.terminal->igs_engine;
			const struct st_console_rect full = {0,0,igs->width,igs->height};
			struct st_pixel_buffer *expected = NULL;
			CHECK(st_pixel_buffer_create(&expected, igs->width, igs->height, true) == 0);
			for (size_t c = 0; c < sizeof(cases) / sizeof(*cases); ++c)
				for (unsigned shape = 0; shape < 6; ++shape)
					for (unsigned mode = 1; mode <= 4; ++mode) {
						CHECK(st_cterm_reset_terminal(f.terminal) == 0);
						seed(&f, &full, 769, 768);
						CHECK(st_pixel_buffer_fill(expected, 769, 768) == 0);
						igs->math[0] = igs->math[1] = 40;
						igs->math[2] = (int16_t)cases[c].ex; igs->math[3] = (int16_t)cases[c].ey;
						igs->math[4] = (int16_t)cases[c].count;
						igs->draw_x = 19; igs->draw_y = 17; igs->draw_start_x = 13; igs->draw_start_y = 14;
						igs->last_line = false;
						char wire[96];
						(void)snprintf(wire, sizeof(wire), "\033fG#T>1,%u,1:M>%u:X 0,0],1],2],3],4]:\033b", shape + 1, mode);
						feed(&f, wire);
						bool setup = cases[c].ex == 0 && cases[c].ey == 0 && resolution != 2;
						CHECK(igs->random_seed == (setup ? 1 : cases[c].seed));
						CHECK(igs->marker_cycle == (setup ? 40 : 0) && igs->marker_cycle_restart == (setup ? 40 : 0));
						CHECK(igs->draw_x == 19 && igs->draw_y == 17 && igs->draw_start_x == 13 && igs->draw_start_y == 14);
						CHECK(igs->last_line == !setup && igs->attributes.marker_pen == 1 && igs->marker_pen_cache == 1);
						CHECK(f.terminal->xpos == 1 && f.terminal->ypos == 1 && f.terminal->seq_trailing_remain == 1);
						CHECK(igs->wait_deadline == 0 && igs->text_deadline == 0);
						if (!setup && mode != 4)
							for (int n = 0; n < cases[c].count; ++n)
								for (int y = 0; y < 7; ++y)
									for (int x = 0; x < 9; ++x) {
										if (!((mode == 3 ? xor_shapes : shapes)[shape][y] & (1u << x))) continue;
										unsigned px = 40 + cases[c].points[n][0] + (unsigned)x - 4;
										unsigned py = 40 + cases[c].points[n][1] + (unsigned)y - 3;
										if (px >= igs->width || py >= (status ? 24u : 25u) * (resolution == 2 ? 16u : 8u)) continue;
										size_t offset = (size_t)py * igs->width + px;
										unsigned mask = (unsigned[]){15,3,1}[resolution];
										if (mode == 3) { expected->primary[offset] = 768 + ((expected->primary[offset]-768)^mask); expected->alternate[offset] = 768 + ((expected->alternate[offset]-768)^mask); }
										else expected->primary[offset] = expected->alternate[offset] = 768 + mask;
									}
						struct st_pixel_buffer *actual = NULL;
						CHECK(st_console_get_pixels(&actual, f.console, &full, false) == 0);
						size_t bytes = (size_t)igs->width * igs->height * sizeof(*actual->primary);
						CHECK(memcmp(expected->primary, actual->primary, bytes) == 0);
						CHECK(memcmp(expected->alternate, actual->alternate, bytes) == 0);
						st_pixel_buffer_destroy(actual);
					}
			st_pixel_buffer_destroy(expected); destroy(&f);
		}
}

static void
cycles_and_limits(void)
{
	static const unsigned map[] = {0,15,1,2,4,6,3,5,7,8,9,10,12,14,11,13};
	static const int pens[3][4] = {{2,3,4,5},{2,3,1,2},{1,1,1,1}};
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f, resolution, false);
		struct cterm_igs *igs = f.terminal->igs_engine;
		const struct st_console_rect full = {0,0,igs->width,igs->height};
		for (unsigned cache = 0; cache < 2; ++cache) {
			CHECK(st_cterm_reset_terminal(f.terminal) == 0);
			seed(&f, &full, 768, 768);
			feed(&f, cache ? "G#C 0,3:" : "G#C 0,0:");
			igs->marker_cycle = igs->marker_cycle_restart = 1;
			feed(&f, "G#X 0,40,40,63,63,4:");
			CHECK(igs->marker_cycle == (resolution == 0 ? 5 : resolution == 1 ? 2 : 1));
			CHECK(igs->marker_cycle_restart == 1 && igs->marker_pen_cache == (cache ? 3 : 0));
			CHECK(igs->attributes.marker_pen == (cache ? (resolution == 2 ? 1 : 3) : pens[resolution][3]));
			struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b, f.console, &full, false) == 0);
			for (unsigned n = 0; n < 4; ++n) {
				size_t offset = (size_t)(40 + cases[1].points[n][1]) * igs->width + 40 + cases[1].points[n][0];
				CHECK(b->primary[offset] == 768 + (map[pens[resolution][n]] & (unsigned[]){15,3,1}[resolution]));
			}
			st_pixel_buffer_destroy(b);
		}
		/* Serial color setup bypasses restore; monochrome takes the normal
		 * zero-count path and restores a nonzero cached pen. */
		igs->attributes.marker_pen = 0; igs->marker_pen_cache = 1;
		feed(&f, "G#X 0,3,99,0,0,0:");
		CHECK(igs->attributes.marker_pen == (resolution == 2 ? 1 : 0));
		CHECK(igs->marker_cycle == (resolution == 2 ? 1 : 3));
		for (unsigned operation = 0; operation < 6; ++operation) {
			char wire[24]; (void)snprintf(wire, sizeof(wire), "G#I %u:", operation); feed(&f, wire);
			if (st_cterm_input_paused(f.terminal)) { local_ms = igs->wait_deadline; CHECK(st_cterm_poll(f.terminal) == 0); }
			CHECK(igs->marker_cycle == (resolution == 2 ? 1 : 3));
		}
		feed(&f, resolution == 0 ? "G#R 1,0:" : "G#R 0,0:");
		if (st_cterm_input_paused(f.terminal)) { local_ms = igs->wait_deadline; CHECK(st_cterm_poll(f.terminal) == 0); }
		CHECK(igs->marker_cycle == (resolution == 2 ? 1 : 3));
		feed(&f, "G#I 5:");
		if (st_cterm_input_paused(f.terminal)) { local_ms = igs->wait_deadline; CHECK(st_cterm_poll(f.terminal) == 0); }
		CHECK(igs->resolution == resolution && igs->marker_cycle == (resolution == 2 ? 1 : 3));
		CHECK(st_cterm_reset_terminal(f.terminal) == 0);
		CHECK(igs->marker_cycle == 0 && igs->marker_cycle_restart == 0);
		feed(&f, "G#g>1:X 0,5000,5000,1000,1000,1:");
		CHECK(igs->random_seed == (resolution == 0 ? UINT32_C(0xc97b72f1) : UINT32_C(0x1bf50664)));
		CHECK(st_cterm_reset_terminal(f.terminal) == 0);
		seed(&f, &full, 768, 768);
		feed(&f, "G#g>2:X 0,40,40,63,63,1:");
		CHECK(igs->random_seed == UINT32_C(0x58543a1d));
		struct st_pixel_buffer *dot = NULL;
		const struct st_console_rect point = {88, resolution == 2 ? 138 : 98, 1, 1};
		CHECK(st_console_get_pixels(&dot, f.console, &point, false) == 0);
		CHECK(dot->primary[0] == 768u + ((unsigned[]){15,3,1}[resolution])); st_pixel_buffer_destroy(dot);
		CHECK(st_cterm_reset_terminal(f.terminal) == 0);
		igs->math[4] = INT16_MIN; igs->marker_cycle = 2; igs->marker_pen_cache = -1;
		igs->attributes.marker_pen = 0;
		feed(&f, "G#X 0,40,40,255,255,4]:");
		CHECK(igs->random_seed == 1 && igs->marker_cycle == 2 && igs->attributes.marker_pen == 1);
		CHECK(st_cterm_reset_terminal(f.terminal) == 0);
		igs->math[4] = INT16_MAX;
		feed(&f, "G#M>4:X 0,40,40,255,255,4]:");
		CHECK(igs->random_seed == UINT32_C(0xd5f8a65b));
		CHECK(st_cterm_reset_terminal(f.terminal) == 0);
		igs->marker_cycle = 1; igs->marker_cycle_restart = 1;
		igs->marker_pen_cache = 3; igs->attributes.marker_type = 0;
		const char bad_marker[] = "G#X 0,40,40,255,255,1:"; size_t used;
		CHECK(st_cterm_write(f.terminal, bad_marker, sizeof(bad_marker)-1, &used) == IO_ERROR_INVALID_ARGUMENT);
		CHECK(igs->random_seed == UINT32_C(0x5eb5ca17));
		CHECK(igs->attributes.marker_pen == (resolution == 2 ? 1 : 3));
		CHECK(igs->marker_cycle == (resolution == 2 ? 1 : 2) && igs->draw_x == 0 && igs->draw_y == 0);
		destroy(&f);
	}
	struct fixture f; create(&f, 0, false);
	struct cterm_igs *igs = f.terminal->igs_engine;
	igs->attributes.marker_pen = 3;
	for (int word = INT16_MIN; word <= INT16_MAX; ++word) {
		igs->math[0] = (int16_t)word;
		feed(&f, "G#X 0,0],0,0,0,0:");
		CHECK(igs->marker_cycle == word && igs->marker_cycle_restart == word);
		CHECK(igs->attributes.marker_pen == 3 && igs->random_seed == 1);
	}
	feed(&f, "G#X 0,40,40,255,255,2:");
	CHECK(igs->marker_cycle == -32767 && igs->marker_cycle_restart == INT16_MAX);
	destroy(&f);
}

static void
partitions_and_recovery(void)
{
	const char *wires[] = {"AG#X>0,40,40,63,63,4:M 2:B", "AG#g>1:X>r,r,R,r,R,0:M 2:B"};
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f, resolution, false);
		struct cterm_igs *igs = f.terminal->igs_engine;
		for (unsigned which = 0; which < 2; ++which) {
			size_t size = strlen(wires[which]);
			for (size_t split = 0; split <= size + 1; ++split) {
				CHECK(st_cterm_reset_terminal(f.terminal) == 0);
				igs->random_seed = which ? 228 : 1;
				igs->random_range[0][1] = igs->random_range[1][1] = 255;
				size_t used;
				if (split > size) {
					for (size_t i = 0; i < size; ++i) CHECK(st_cterm_write(f.terminal, wires[which]+i, 1, &used) == 0 && used == 1);
				} else {
					CHECK(st_cterm_write(f.terminal, wires[which], split, &used) == 0 && used == split);
					CHECK(st_cterm_write(f.terminal, wires[which]+split, size-split, &used) == 0 && used == size-split);
				}
				CHECK(igs->random_seed == (which ? UINT32_C(0x6e1e0371) : UINT32_C(0x03cf1503)));
				CHECK(igs->attributes.writing_mode == 2 && igs->draw_x == 0 && igs->draw_y == 0);
				CHECK(f.terminal->xpos == 3 && f.terminal->ypos == 1);
			}
		}
		const char *partial[] = {"G#X 0,", "G#X 0,40,40,63,", "G#X 0,40,40,63,63,4", "G#X r,r,"};
		for (size_t i = 0; i < sizeof(partial)/sizeof(*partial); ++i) {
			igs->random_seed = 228;
			feed(&f, partial[i]); CHECK(st_cterm_finish(f.terminal) == 0 && igs->random_seed == 228);
			feed(&f, partial[i]); local_ms += 5001; CHECK(st_cterm_poll(f.terminal) == 0 && igs->random_seed == 228);
		}
		const char bad[] = "G#X 0,40,40,63,63,10000:"; size_t used;
		CHECK(st_cterm_write(f.terminal, bad, sizeof(bad)-1, &used) == IO_ERROR_OUT_OF_RANGE);
		CHECK(igs->random_seed == 228);
		feed(&f, "G#X 0,1,0,0,0,0:");
		CHECK(st_cterm_reset_terminal(f.terminal) == 0 && igs->marker_cycle == 0 && igs->marker_cycle_restart == 0);
		destroy(&f);
	}
}

int
st_cterm_igs_scatter_test_main(void)
{
	pixels();
	cycles_and_limits();
	partitions_and_recovery();
	return 0;
}
