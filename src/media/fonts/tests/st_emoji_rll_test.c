/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */

#include "st_emoji_fonts.h"
#include "st_time.h"
#include "color_rll_fixture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%s:%d: %s\n", \
    __FILE__, __LINE__, #e); exit(EXIT_FAILURE); } } while (false)

#define COLUMNS 80U
#define ROWS 25U
#define PAIRS (COLUMNS * ROWS / 2)
#define SAMPLES 7U
#define PASSES 40U

struct packed_bank {
	struct st_console_font font;
	struct st_console_color_glyph *colors;
	uint8_t *data;
	size_t raw_bytes;
	size_t packed_bytes;
};

static struct packed_bank
pack_bank(const struct st_console_font *source)
{
	struct packed_bank bank = {.font = *source};
	size_t capacity = 0;
	for (size_t i = 0; i < source->color_glyph_count; ++i)
		capacity += source->color_glyphs[i].size + 2;
	bank.colors = calloc(source->color_glyph_count, sizeof(*bank.colors));
	bank.data = malloc(capacity);
	CHECK(bank.colors != NULL && bank.data != NULL);
	bank.font.color_glyphs = bank.colors;
	for (size_t i = 0; i < source->color_glyph_count; ++i) {
		const struct st_console_color_glyph *raw = &source->color_glyphs[i];
		struct st_console_color_glyph *packed = &bank.colors[i];
		*packed = *raw;
		packed->row_stride = 0;
		size_t j = 0;
		while (j < i && source->color_glyphs[j].rgba != raw->rgba)
			++j;
		if (j < i) {
			packed->rgba = bank.colors[j].rgba;
			packed->size = bank.colors[j].size;
			continue;
		}
		size_t width = raw->width != 0 ? raw->width : source->width
		    * (raw->glyph < source->glyph_count ? 1U : 2U);
		packed->rgba = bank.data + bank.packed_bytes;
		packed->size = encode_color_rll(raw->rgba, raw->row_stride,
		    width, source->height, bank.data + bank.packed_bytes);
		CHECK(packed->size != 0);
		bank.raw_bytes += raw->size;
		bank.packed_bytes += packed->size;
	}
	return bank;
}

static st_console_t *
make_console(uint32_t height)
{
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config config = {
		.columns = COLUMNS, .rows = ROWS, .cell_width = 8, .cell_height = height,
		.color_emoji = true,
	};
	st_console_t *console = NULL;
	CHECK(st_console_create(&console, &display, &config) == 0);
	return console;
}

/* mode 0 redraws both halves; modes 1 and 2 update only left/right cells. */
static void
frame(st_console_t *console, const struct st_console_font *font, uint16_t id,
	size_t pass, unsigned mode, bool decorations)
{
	struct st_console_cell cells[COLUMNS * ROWS];
	for (size_t i = 0; i < COLUMNS * ROWS; i += 2) {
		size_t artwork_pass = mode == 0 ? pass : PASSES + 1;
		size_t index = (i / 2 + artwork_pass * PAIRS) % font->color_glyph_count;
		cells[i] = (struct st_console_cell){
			.glyph = font->color_glyphs[index].glyph,
			.font = id, .glyph_font = id,
			.foreground = ST_CONSOLE_COLOR_RGB_VALUE(201, 117, 23),
			.background = ST_CONSOLE_COLOR_RGB_VALUE(17U + (uint32_t)(artwork_pass % 23), 49, 73),
		};
		if (decorations) {
			if (i % 3 == 0)
				cells[i].flags |= ST_CONSOLE_CELL_UNDERLINE;
			if (i % 5 == 0)
				cells[i].flags |= ST_CONSOLE_CELL_BLINK;
			if (i % 7 == 0)
				cells[i].flags |= ST_CONSOLE_CELL_REVERSE | ST_CONSOLE_CELL_BOLD;
			cells[i].hyperlink = i % 11 == 0 ? 1 : 0;
		}
		cells[i + 1] = cells[i];
		cells[i + 1].flags |= ST_CONSOLE_CELL_CONTINUATION;
		if (mode != 0)
			cells[i + mode - 1].background
			    = ST_CONSOLE_COLOR_RGB_VALUE(17U + (uint32_t)(pass % 23), 49, 73);
	}
	CHECK(st_console_begin_update(console) == 0);
	const struct st_console_rect rect = {0, 0, COLUMNS, ROWS};
	/* Complete cells preserve independently changed attributes on one half
	 * while keeping each wide glyph pair valid. Unchanged halves stay clean. */
	CHECK(st_console_write_complete_cells(console, &rect, cells, COLUMNS) == 0);
	CHECK(st_console_end_update(console) == 0);
}

static struct st_pixel_buffer *
capture(st_console_t *console, uint32_t height)
{
	const struct st_console_rect rect = {0, 0, COLUMNS * 8, ROWS * height};
	struct st_pixel_buffer *pixels = NULL;
	CHECK(st_console_get_pixels(&pixels, console, &rect, false) == 0);
	return pixels;
}

static void
compare(st_console_t *raw, st_console_t *packed, uint32_t height)
{
	struct st_pixel_buffer *a = capture(raw, height), *b = capture(packed, height);
	size_t bytes = (size_t)a->width * a->height * sizeof(*a->primary);
	CHECK(a->width == b->width && a->height == b->height);
	CHECK(memcmp(a->primary, b->primary, bytes) == 0);
	CHECK(memcmp(a->alternate, b->alternate, bytes) == 0);
	st_pixel_buffer_destroy(a);
	st_pixel_buffer_destroy(b);
}

static void
verify(const struct st_console_font *raw, const struct st_console_font *packed)
{
	st_console_t *a = make_console(raw->height), *b = make_console(raw->height);
	uint16_t raw_id, packed_id;
	CHECK(st_console_register_static_font(a, raw, &raw_id) == 0);
	CHECK(st_console_register_static_font(b, packed, &packed_id) == 0);
	size_t frames = (raw->color_glyph_count + PAIRS - 1) / PAIRS;
	for (unsigned mode = 0; mode < 3; ++mode)
		for (size_t pass = 0; pass < frames; ++pass) {
			frame(a, raw, raw_id, pass, mode, true);
			frame(b, packed, packed_id, pass, mode, true);
			compare(a, b, raw->height);
		}
	st_console_destroy(a);
	st_console_destroy(b);
}

static uint64_t
cpu_ns(void)
{
	uint64_t result;
	CHECK(st_time_process_cpu_ns(&result) == 0);
	return result;
}

static double
measure(st_console_t *console, const struct st_console_font *font, uint16_t id,
	unsigned mode)
{
	/* Warm both caches and the cell state outside the timed region. */
	frame(console, font, id, PASSES + 1, 0, false);
	uint64_t start = cpu_ns();
	for (size_t pass = 0; pass < PASSES; ++pass)
		frame(console, font, id, pass, mode, false);
	return (double)(cpu_ns() - start) / 1000000.0 / PASSES;
}

static int
compare_double(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;
	return (x > y) - (x < y);
}

static void
benchmark(const struct st_console_font *raw, const struct st_console_font *packed)
{
	st_console_t *console = make_console(raw->height);
	uint16_t ids[2];
	const struct st_console_font *fonts[] = {raw, packed};
	CHECK(st_console_register_static_font(console, raw, &ids[0]) == 0);
	CHECK(st_console_register_static_font(console, packed, &ids[1]) == 0);
	const char *names[] = {"full", "left", "right"};
	for (unsigned mode = 0; mode < 3; ++mode) {
		double samples[2][SAMPLES];
		for (unsigned sample = 0; sample < SAMPLES; ++sample)
			for (unsigned order = 0; order < 2; ++order) {
				unsigned format = (sample + order) % 2;
				samples[format][sample] = measure(console, fonts[format], ids[format], mode);
			}
		for (unsigned format = 0; format < 2; ++format)
			qsort(samples[format], SAMPLES, sizeof(double), compare_double);
		printf("height=%u mode=%s CPU ms/frame: RGBA %.3f [%.3f,%.3f], "
		    "RLL %.3f [%.3f,%.3f], ratio %.3f\n", raw->height, names[mode],
		    samples[0][SAMPLES / 2], samples[0][0], samples[0][SAMPLES - 1],
		    samples[1][SAMPLES / 2], samples[1][0], samples[1][SAMPLES - 1],
		    samples[1][SAMPLES / 2] / samples[0][SAMPLES / 2]);
		fflush(stdout);
	}
	st_console_destroy(console);
}

int
main(int argc, char **argv)
{
	bool profile = argc == 2 && strcmp(argv[1], "--benchmark") == 0;
	CHECK(argc == 1 || profile);
	const struct st_console_font *fonts[] = {
		&st_emoji_8_font, &st_emoji_14_font, &st_emoji_16_font,
	};
	if (profile)
		printf("Headless 80x25; %u samples x %u frames; median [min,max]; "
		    "alternating format order; total process CPU.\n", SAMPLES, PASSES);
	for (size_t i = 0; i < sizeof(fonts) / sizeof(fonts[0]); ++i) {
		struct packed_bank bank = pack_bank(fonts[i]);
		verify(fonts[i], &bank.font);
		printf("height=%u: %zu colour IDs verified; unique bytes RGBA=%zu RLL=%zu "
		    "(%.2f%% saved, includes half offsets)\n", fonts[i]->height,
		    fonts[i]->color_glyph_count, bank.raw_bytes, bank.packed_bytes,
		    100.0 * (1.0 - (double)bank.packed_bytes / (double)bank.raw_bytes));
		fflush(stdout);
		if (profile)
			benchmark(fonts[i], &bank.font);
		free(bank.data);
		free(bank.colors);
	}
	return EXIT_SUCCESS;
}
