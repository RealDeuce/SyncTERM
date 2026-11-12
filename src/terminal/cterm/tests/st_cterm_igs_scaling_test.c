/* g state and the axis-aware numeric reader. Reference 15 and 11;
 * IG221.TXT describes the intended normalized 10000x10000 canvas. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm.h"
#include "cterm_igs.h"
#include "cterm_igs_loop.h"
#include "st_cterm.h"
#include "st_graphics_parser.h"
#include "io_error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); \
} } while (0)

struct fixture { st_console_t *console; st_font_catalog_t *fonts; st_cterm_t *terminal; };
static uint64_t local_ms;
static int now(uint64_t *value) { *value = local_ms; return 0; }

static void
create(struct fixture *f, unsigned resolution)
{
	memset(f, 0, sizeof(*f));
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry = {
		.columns = resolution == 0 ? 40 : 80, .rows = 25, .cell_width = 8,
		.cell_height = resolution == 2 ? 16 : 8,
	};
	CHECK(st_console_create(&f->console, &display, &geometry) == 0);
	CHECK(st_font_catalog_create(&f->fonts, f->console) == 0);
	const struct st_cterm_config config = {
		.console = f->console, .fonts = f->fonts, .font_selector = 44,
		.emulation = ST_CTERM_ATARI_ST_VT52, .graphics_mode = ST_CTERM_GRAPHICS_IGS_221,
		.encoding = ST_CTERM_ENCODING_CODEPAGE, .codepage = ST_CODEPAGE_ATARI_ST,
		.quiet = true,
	};
	CHECK(st_cterm_create(&f->terminal, &config) == 0);
	f->terminal->igs_engine->now_ms = now;
	local_ms = 1000;
}

static void
destroy(struct fixture *f)
{
	st_cterm_destroy(&f->terminal);
	st_font_catalog_destroy(f->fonts);
	st_console_destroy(f->console);
}

static void
feed(struct fixture *f, const char *wire)
{
	size_t used;
	CHECK(st_cterm_write(f->terminal, wire, strlen(wire), &used) == 0 && used == strlen(wire));
}

static int
value(struct cterm_igs *igs, unsigned number, enum st_graphics_axis axis,
    enum st_graphics_operand_source source)
{
	const struct st_graphics_argument a = {.kind = ST_GRAPHICS_NUMBER, .axis = axis,
	    .operand = {.source = source, .value = number}};
	int result = -123;
	CHECK(cterm_igs_argument_value(igs, &a, &result) == 0);
	return result;
}

static void
vectors_and_capture(void)
{
	/* Half of 9999 maps to the midpoint, correcting the integer quotient. */
	const unsigned input[] = {0,1,1000,2500,5000,7500,9998,9999};
	const int expected[3][2][8] = {
		{{0,0,32,80,160,239,319,319},{0,0,20,50,100,149,199,199}},
		{{0,0,64,160,320,479,639,639},{0,0,20,50,100,149,199,199}},
		{{0,0,64,160,320,479,639,639},{0,0,40,100,200,299,399,399}},
	};
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f;
		create(&f, resolution);
		struct cterm_igs *igs = f.terminal->igs_engine;
		igs->draw_x = 17; igs->draw_y = 23;
		igs->attributes.line_width = 5;
		igs->clipping = false;
		uint32_t palette[16]; memcpy(palette, igs->palette, sizeof(palette));
		for (unsigned mode = 0; mode < 3; ++mode) {
			feed(&f, mode == 0 ? "G#g 0:" : mode == 1 ? "G#g 1:" : "G#g 2:");
			CHECK(igs->scaling_mode == (int)mode && igs->scaling_resolution == (int)resolution);
			CHECK(igs->attributes.line_width == 5);
			for (unsigned axis = ST_GRAPHICS_UNSCALED; axis <= ST_GRAPHICS_Y; ++axis)
				for (size_t n = 0; n < sizeof(input)/sizeof(*input); ++n) {
					int wanted = mode == 1 && axis != ST_GRAPHICS_UNSCALED ? expected[resolution][axis - 1][n]
					    : mode == 2 && axis == ST_GRAPHICS_Y && resolution == 2 ? (int)input[n] * 2 : (int)input[n];
					CHECK(value(igs, input[n], (enum st_graphics_axis)axis, ST_GRAPHICS_CONSTANT) == wanted);
				}
		}
		feed(&f, "G#g 1:G#I 0:");
		CHECK(igs->scaling_mode == 1 && !igs->clipping);
		CHECK(igs->draw_x == 17 && igs->draw_y == 23);
		CHECK(memcmp(palette, igs->palette, sizeof(palette)) == 0);
		if (resolution != 2) {
			feed(&f, resolution == 0 ? "G#R 1,0:" : "G#R 0,0:");
			local_ms += 17;
			CHECK(st_cterm_poll(f.terminal) == 0);
			CHECK(igs->resolution == 1 - resolution && igs->scaling_resolution == (int)resolution);
			CHECK(value(igs, 5000, ST_GRAPHICS_X, ST_GRAPHICS_CONSTANT) == expected[resolution][0][4]);
			feed(&f, "G#g 0:");
			CHECK(igs->scaling_mode == 0 && igs->scaling_resolution == (int)(1 - resolution));
		}
		for (unsigned invalid = 0; invalid < 2; ++invalid) {
			igs->math[0] = invalid == 0 ? -1 : 3;
			feed(&f, "G#g 0]:");
			CHECK(igs->scaling_mode == 0 && igs->scaling_resolution == (int)igs->resolution);
		}
		feed(&f, "G#g 9999:");
		CHECK(igs->scaling_mode == 0);
		CHECK(st_cterm_reset_terminal(f.terminal) == 0);
		CHECK(igs->scaling_mode == 0 && igs->scaling_resolution == (int)igs->resolution);
		destroy(&f);
	}
}

struct collected { struct cterm_igs *igs; int numbers[2]; unsigned commands; };
static int no_text(void *c, const uint8_t *p, size_t n) { (void)c; (void)p; return n == 0 ? 0 : IO_ERROR_PROTOCOL; }
static int collect(void *context, const struct st_graphics_command_view *view)
{
	struct collected *c = context;
	CHECK(view->count == 2);
	for (unsigned i = 0; i < 2; ++i)
		CHECK(cterm_igs_argument_value(c->igs, &view->arguments[i], &c->numbers[i]) == 0);
	++c->commands;
	return 0;
}

static void
typed_reader(void)
{
	const uint8_t attention[] = {'G','#'};
	const struct st_graphics_dialect dialect = {.attention = attention, .attention_size = 2,
	    .chain_indicator = true, .chain = '>', .number_skip_crlf = true, .number_digits = 4};
	const struct st_graphics_parameter xy[] = {
		{.kind = ST_GRAPHICS_NUMBER, .axis = ST_GRAPHICS_X},
		{.kind = ST_GRAPHICS_NUMBER, .axis = ST_GRAPHICS_Y},
	};
	/* Synthetic reader schema only; no fake drawing command is installed. */
	const struct st_graphics_command commands[] = {{'~', xy, 2, NULL}};
	const struct st_graphics_parser_config config = {.dialect = &dialect, .commands = commands,
	    .command_count = 1, .max_arguments = 2, .max_payload = 0};
	const char wire[] = "G#~ 10_XX00],5\r\n000:";
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f;
		create(&f, resolution);
		struct cterm_igs *igs = f.terminal->igs_engine;
		feed(&f, "G#g 1:");
		for (unsigned i = 0; i < 128; ++i) igs->math[i] = (int16_t)(1000 + i);
		for (size_t split = 0; split <= sizeof(wire); ++split) {
			st_graphics_parser_t *parser = NULL;
			struct collected c = {.igs = igs};
			const struct st_graphics_parser_sink sink = {.context = &c, .text = no_text, .command = collect};
			CHECK(st_graphics_parser_create(&parser, &config) == 0);
			size_t used;
			if (split == sizeof(wire)) {
				for (size_t i = 0; i < sizeof(wire) - 1; ++i)
					CHECK(st_graphics_parser_feed(parser, &sink, (const uint8_t *)wire + i, 1, &used) == 0 && used == 1);
			} else {
				CHECK(st_graphics_parser_feed(parser, &sink, (const uint8_t *)wire, split, &used) == 0 && used == split);
				CHECK(st_graphics_parser_feed(parser, &sink, (const uint8_t *)wire + split, sizeof(wire) - 1 - split, &used) == 0
				    && used == sizeof(wire) - 1 - split);
			}
			CHECK(c.commands == 1 && c.numbers[0] == (resolution == 0 ? 1032 : 1064));
			CHECK(c.numbers[1] == (resolution == 2 ? 200 : 100));
			st_graphics_parser_destroy(parser);
		}
		CHECK(value(igs, 9999, ST_GRAPHICS_X, ST_GRAPHICS_ARRAY) == 1127);
		igs->random_range[0][0] = igs->random_range[0][1] = 7;
		igs->random_range[1][0] = igs->random_range[1][1] = 9;
		CHECK(value(igs, 0, ST_GRAPHICS_X, ST_GRAPHICS_RANDOM_R) == 7);
		CHECK(value(igs, 0, ST_GRAPHICS_Y, ST_GRAPHICS_RANDOM_R2) == 9);
		feed(&f, "G#g 2:");
		CHECK(value(igs, 5, ST_GRAPHICS_Y, ST_GRAPHICS_ARRAY) == (resolution == 2 ? 1010 : 1005));
		igs->math[0] = -2;
		CHECK(value(igs, 0, ST_GRAPHICS_Y, ST_GRAPHICS_ARRAY) == -2);
		struct st_graphics_argument invalid = {.kind = ST_GRAPHICS_NUMBER,
		    .axis = ST_GRAPHICS_X, .operand = {.value = 10000}};
		int unchanged = 777;
		CHECK(cterm_igs_argument_value(igs, &invalid, &unchanged) == IO_ERROR_OUT_OF_RANGE && unchanged == 777);
		invalid.operand.value = 5000;
		igs->scaling_resolution = 3;
		CHECK(cterm_igs_argument_value(igs, &invalid, &unchanged) == IO_ERROR_INVALID_ARGUMENT && unchanged == 777);
		igs->scaling_resolution = (int)resolution;
		invalid.axis = (enum st_graphics_axis)99;
		CHECK(cterm_igs_argument_value(igs, &invalid, &unchanged) == IO_ERROR_INVALID_ARGUMENT && unchanged == 777);
		destroy(&f);
	}
}

static void
partitions_and_recovery(void)
{
	const char wire[] = "AG#g>1:g 2:B";
	for (unsigned resolution = 0; resolution < 3; ++resolution)
		for (size_t split = 0; split <= sizeof(wire); ++split) {
			struct fixture f;
			create(&f, resolution);
			size_t used;
			if (split == sizeof(wire)) {
				for (size_t i = 0; i < sizeof(wire) - 1; ++i)
					CHECK(st_cterm_write(f.terminal, wire + i, 1, &used) == 0 && used == 1);
			} else {
				CHECK(st_cterm_write(f.terminal, wire, split, &used) == 0 && used == split);
				CHECK(st_cterm_write(f.terminal, wire + split, sizeof(wire) - 1 - split, &used) == 0 && used == sizeof(wire) - 1 - split);
			}
			CHECK(st_cterm_finish(f.terminal) == 0);
			struct cterm_igs *igs = f.terminal->igs_engine;
			CHECK(igs->scaling_mode == 2 && igs->scaling_resolution == (int)resolution);
			struct st_console_cell cells[2];
			const struct st_console_rect rect = {0,0,2,1};
			CHECK(st_console_read_cells(f.console, &rect, cells, 2) == 0 && cells[0].glyph == 'A' && cells[1].glyph == 'B');
			feed(&f, "G#g 1");
			CHECK(igs->scaling_mode == 2);
			CHECK(st_cterm_finish(f.terminal) == 0 && igs->scaling_mode == 2);
			feed(&f, "G#g 1"); local_ms += 5000;
			CHECK(st_cterm_poll(f.terminal) == 0 && igs->scaling_mode == 2);
			CHECK(st_cterm_write(f.terminal, "G#g 99999:", 10, &used) == 0);
			feed(&f, "G#g 0:");
			CHECK(igs->scaling_mode == 0);
			destroy(&f);
		}
}

static void
random_tail(void)
{
	/* Reference 11: two ordered tests after lookup and relative encoding.
	 * Seeds invert Random's odd multiplier; the next states are 0x800300
	 * and 0x800400. The following byte candidates are 135 and 180. */
	static const struct {
		int encoded; uint32_t seed; int lower, upper, expected; uint32_t final;
	} cases[] = {
		{-32765, UINT32_C(0x62683a5b), INT16_MIN, INT16_MAX, 180, UINT32_C(0x1a18b401)},
		{-32765, UINT32_C(0xe40e955b), INT16_MIN, INT16_MAX, -32765, UINT32_C(0x00800300)},
		{-32764, UINT32_C(0xe40e955b), INT16_MIN, INT16_MAX, -32765, UINT32_C(0x00800300)},
		{-32764, UINT32_C(0x62683a5b), INT16_MIN, INT16_MAX, -32764, UINT32_C(0x00800400)},
	};
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f, resolution);
		struct cterm_igs *igs = f.terminal->igs_engine;
		feed(&f, "G#g 2:");
		for (size_t i = 0; i < sizeof(cases)/sizeof(*cases); ++i)
			for (unsigned source = 0; source < 4; ++source) {
				igs->math[0] = (int16_t)cases[i].encoded;
				igs->random_seed = cases[i].seed;
				unsigned which = cases[i].encoded == -32765 ? 0 : 1;
				igs->random_range[0][0] = igs->random_range[1][0] = 0;
				igs->random_range[0][1] = igs->random_range[1][1] = 255;
				igs->random_range[which][0] = cases[i].lower;
				igs->random_range[which][1] = cases[i].upper;
				if (source < 2) {
					CHECK(value(igs, 0, ST_GRAPHICS_Y, source == 0 ? ST_GRAPHICS_ARRAY
					    : which == 0 ? ST_GRAPHICS_RANDOM_R : ST_GRAPHICS_RANDOM_R2) == cases[i].expected);
				} else {
					igs->loop = (struct cterm_igs_loop){.count = 1, .to = 1, .step = 1,
					    .operands = {(int16_t)cases[i].encoded}, .no_parameters = true};
					if (source == 3) {
						igs->loop.current = igs->loop.to = cases[i].encoded;
						igs->loop.operands[0] = -32767;
						igs->loop.no_parameters = false;
					}
					igs->loop_mode = 1;
					int result = 777;
					CHECK(cterm_igs_loop_value(igs, &result) == 0 && result == cases[i].expected);
					igs->loop_mode = 0;
				}
				CHECK(igs->random_seed == cases[i].final);
			}
		igs->loop.initializing = igs->loop.loading = true;
		igs->random_seed = 1;
		for (unsigned r = 0; r < 2; ++r) {
			igs->math[0] = (int16_t)(-32765 + (int)r);
			CHECK(value(igs, 0, ST_GRAPHICS_Y, ST_GRAPHICS_ARRAY) == igs->math[0]);
			CHECK(value(igs, 0, ST_GRAPHICS_Y, r == 0 ? ST_GRAPHICS_RANDOM_R : ST_GRAPHICS_RANDOM_R2) == igs->math[0]);
		}
		igs->loop.loading = false;
		struct st_graphics_argument a = {.kind = ST_GRAPHICS_NUMBER, .axis = ST_GRAPHICS_Y,
		    .operand = {.source = ST_GRAPHICS_RANDOM_R, .operation = ST_GRAPHICS_STEP_PLUS}};
		int result = 777;
		CHECK(cterm_igs_argument_value(igs, &a, &result) == 0 && result == 32765 && igs->random_seed == 1);
		/* Encoding an array value can itself create the random sentinel. */
		a.operand.source = ST_GRAPHICS_ARRAY;
		igs->random_range[0][0] = igs->random_range[1][0] = 0;
		igs->random_range[0][1] = igs->random_range[1][1] = 255;
		const enum st_graphics_operand_operation operations[] = {
			ST_GRAPHICS_STEP_PLUS, ST_GRAPHICS_STEP_MINUS, ST_GRAPHICS_MINUS_STEP};
		for (unsigned op = 0; op < 3; ++op) {
			igs->math[0] = (int16_t)(op == 0 ? 32765 : op == 1 ? 22764 : 12765);
			a.operand.operation = operations[op]; igs->random_seed = 1;
			CHECK(cterm_igs_argument_value(igs, &a, &result) == 0 && result == 230 && igs->random_seed == UINT32_C(0xbb40e62e));
		}
		igs->loop.initializing = false;
		a.has_value = true; a.value = -32765;
		CHECK(cterm_igs_argument_value(igs, &a, &result) == 0 && result == -32765 && igs->random_seed == UINT32_C(0xbb40e62e));
		a.has_value = false; a.operand.operation = ST_GRAPHICS_VALUE;
		igs->math[0] = -32765;
		igs->random_range[0][0] = 2; igs->random_range[0][1] = 1;
		result = 777;
		CHECK(cterm_igs_argument_value(igs, &a, &result) == IO_ERROR_OUT_OF_RANGE && result == 777);
		CHECK(igs->random_seed == UINT32_C(0xbb40e62e));
		igs->random_seed = UINT32_C(0x62683a5b);
		igs->random_range[0][0] = INT16_MIN; igs->random_range[0][1] = INT16_MAX;
		igs->random_range[1][0] = 2; igs->random_range[1][1] = 1;
		CHECK(cterm_igs_argument_value(igs, &a, &result) == IO_ERROR_OUT_OF_RANGE && result == 777);
		CHECK(igs->random_seed == UINT32_C(0x00800400));
		destroy(&f);
	}
}

static void
random_tail_partitions(void)
{
	const char *wires[] = {"AG#g>2:X>3],0],1]:M 2:B", "AG#&>0,0,1,0,X,3,10,0],1]:M 2:B"};
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f, resolution);
		struct cterm_igs *igs = f.terminal->igs_engine;
		for (unsigned which = 0; which < 2; ++which) {
			size_t size = strlen(wires[which]);
			for (size_t split = 0; split <= size + 1; ++split) {
				CHECK(st_cterm_reset_terminal(f.terminal) == 0);
				igs->math[0] = igs->math[3] = -32765;
				igs->math[1] = igs->math[2] = -32764;
				igs->random_seed = 69;
				igs->random_range[0][1] = igs->random_range[1][1] = 255;
				size_t used;
				if (split > size) {
					for (size_t i = 0; i < size; ++i)
						CHECK(st_cterm_write(f.terminal, wires[which] + i, 1, &used) == 0 && used == 1);
				} else {
					CHECK(st_cterm_write(f.terminal, wires[which], split, &used) == 0 && used == split);
					CHECK(st_cterm_write(f.terminal, wires[which] + split, size - split, &used) == 0 && used == size - split);
				}
				/* Seed 69 yields byte candidates 10,83,69. Dynamic X10
				 * consumes all three once; the literal loop selector only two. */
				CHECK(igs->draw_x == (which == 0 ? 83 : 10) && igs->draw_y == (which == 0 ? 69 : 83));
				CHECK(igs->random_seed == (which == 0 ? UINT32_C(0x665c4520) : UINT32_C(0xc9c253fb)));
				CHECK(igs->attributes.writing_mode == 2 && !igs->loop.run && igs->loop_mode == 0);
				struct st_console_cell cells[2]; const struct st_console_rect area = {0,0,2,1};
				CHECK(st_console_read_cells(f.console, &area, cells, 2) == 0 && cells[0].glyph == 'A' && cells[1].glyph == 'B');
			}
		}
		destroy(&f);
	}
}

int
st_cterm_igs_scaling_test_main(void)
{
	vectors_and_capture();
	typed_reader();
	random_tail();
	random_tail_partitions();
	partitions_and_recovery();
	return 0;
}
