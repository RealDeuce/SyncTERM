/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdio.h>
#include <stdlib.h>

#include "console_internal.h"

#define CHECK(expression)                                                                                \
	do {                                                                                             \
		if (!(expression)) {                                                                     \
			fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, #expression); \
			exit(EXIT_FAILURE);                                                              \
		}                                                                                        \
	} while (false)

struct phase {
	uint32_t elapsed;
	bool     alternate;
	bool     cursor;
};

static void
check_mode(enum st_console_blink_mode mode, const struct phase *phases, size_t count, bool steady)
{
	const struct st_console_display_config display = {
	    .backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config config = {
	    .columns      = 2,
	    .rows         = 1,
	    .cell_width   = 1,
	    .cell_height  = 1,
	    .scaling_mode = ST_CONSOLE_SCALING_EXTERNAL,
	};
	struct st_console_bitmap_presenter *presenter = NULL;
	struct st_console_bitmap_frame      frame;
	st_console_t                       *console   = NULL;
	const uint32_t                      primary   = UINT32_C(0x110000);
	const uint32_t                      alternate = UINT32_C(0x000022);
	const uint32_t cursor     = mode == ST_CONSOLE_BLINK_PRESTEL ? UINT32_C(0xffffff) : UINT32_C(0x003300);
	const uint32_t epoch      = 1000;
	uint64_t       generation = 0;

	CHECK(st_console_create(&console, &display, &config) == 0);
	for (size_t i = 0; i < 2; ++i) {
		console->pixels->primary[i]   = ST_PIXEL_COLOR_RGB_TAG | primary;
		console->pixels->alternate[i] = ST_PIXEL_COLOR_RGB_TAG | alternate;
	}
	console->style.foreground =
	    ST_CONSOLE_COLOR_RGB_VALUE((cursor >> 16) & 0xff, (cursor >> 8) & 0xff, cursor & 0xff);
	console->cursor_x       = 1;
	console->cursor_y       = 0;
	console->cursor_visible = true;
	console->blink_mode     = mode;
	if (steady) {
		console->cursor_custom       = true;
		console->custom_cursor.blink = false;
	}
	CHECK(st_console_bitmap_presenter_create(&presenter, console) == 0);
	for (size_t i = 0; i < count; ++i) {
		const st_console_t *source       = i == 0 ? console : NULL;
		uint32_t            text         = phases[i].alternate ? alternate : primary;
		uint32_t            cursor_pixel = (steady || phases[i].cursor) ? cursor : text;
		CHECK(st_console_bitmap_presenter_frame(presenter, source, 2, 1, epoch + phases[i].elapsed, NULL,
			  NULL, &frame)
		      == 0);
		if (i == 0)
			generation = frame.generation;
		CHECK(frame.pixels->pixels[0] == text);
		CHECK(frame.pixels->pixels[1] == cursor_pixel);
		CHECK(frame.generation == generation);
	}
	st_console_bitmap_presenter_destroy(presenter);
	st_console_destroy(console);
}

static void
check_warm_scaled_cache(void)
{
	const struct st_console_display_config display = {
	    .backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config config = {
	    .columns      = 1,
	    .rows         = 1,
	    .cell_width   = 1,
	    .cell_height  = 1,
	    .blink_mode   = ST_CONSOLE_BLINK_PC,
	    .scaling_mode = ST_CONSOLE_SCALING_INTERNAL,
	};
	struct st_console_bitmap_presenter *presenter = NULL;
	struct st_console_bitmap_frame      frame;
	const struct st_rgb_buffer         *primary;
	const struct st_rgb_buffer         *alternate;
	st_console_t                       *console = NULL;
	uint64_t                            generation;

	CHECK(st_console_create(&console, &display, &config) == 0);
	console->pixels->primary[0]   = ST_PIXEL_COLOR_RGB_VALUE(1, 2, 3);
	console->pixels->alternate[0] = ST_PIXEL_COLOR_RGB_VALUE(4, 5, 6);
	console->cursor_visible       = false;
	CHECK(st_console_bitmap_presenter_create(&presenter, console) == 0);
	CHECK(st_console_bitmap_presenter_frame(presenter, console, 4, 4, 1000, NULL, NULL, &frame) == 0);
	primary    = frame.pixels;
	generation = frame.generation;
	CHECK(st_console_bitmap_presenter_frame(presenter, NULL, 4, 4, 1266, NULL, NULL, &frame) == 0);
	alternate = frame.pixels;
	CHECK(alternate != primary && frame.generation == generation);
	CHECK(st_console_bitmap_presenter_frame(presenter, NULL, 4, 4, 1532, NULL, NULL, &frame) == 0);
	CHECK(frame.pixels == primary && frame.generation == generation);
	console->pixels->primary[0]   = ST_PIXEL_COLOR_RGB_VALUE(7, 8, 9);
	console->pixels->alternate[0] = ST_PIXEL_COLOR_RGB_VALUE(10, 11, 12);
	st_console_cell_at(console, 0)->flags |= ST_CONSOLE_CELL_DIRTY;
	CHECK(st_console_bitmap_presenter_frame(presenter, console, 4, 4, 1532, NULL, NULL, &frame) == 0);
	CHECK(frame.generation == generation + 1);
	CHECK(st_console_bitmap_presenter_frame(presenter, NULL, 4, 4, 1798, NULL, NULL, &frame) == 0);
	CHECK(frame.pixels == alternate);
	CHECK(frame.pixels->pixels[0] == UINT32_C(0x0a0b0c));
	CHECK(frame.generation == generation + 1);
	st_console_bitmap_presenter_destroy(presenter);
	st_console_destroy(console);
}

static void
check_deferred_images(void)
{
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config config = {
	    .columns      = 3,
	    .rows         = 1,
	    .cell_width   = 1,
	    .cell_height  = 1,
	    .blink_mode   = ST_CONSOLE_BLINK_PC,
	    .scaling_mode = ST_CONSOLE_SCALING_EXTERNAL,
	};
	st_console_t                       *console   = NULL;
	struct st_console_bitmap_presenter *presenter = NULL;
	struct st_console_bitmap_frame      frame;
	const struct st_rgb_buffer         *cached[4];
	const uint32_t                      phases[] = {0, 133, 266, 399};

	CHECK(st_console_create(&console, &display, &config) == 0);
	console->cursor_x       = 2;
	console->cursor_visible = true;
	for (unsigned x = 0; x < 3; ++x) {
		console->pixels->primary[x]   = ST_PIXEL_COLOR_RGB_TAG | 1;
		console->pixels->alternate[x] = ST_PIXEL_COLOR_RGB_TAG | 2;
	}
	CHECK(st_console_bitmap_presenter_create(&presenter, console) == 0);
	for (unsigned i = 0; i < 4; ++i) {
		CHECK(st_console_bitmap_presenter_frame(presenter, i ? NULL : console, 3, 1, 1000 + phases[i],
			  NULL, NULL, &frame)
		      == 0);
		cached[i] = frame.pixels;
		CHECK(cached[i]->pixels[0] == (i < 2 ? 1U : 2U));
	}
	console->dirty_row_end       = 0;
	console->pixel_surface_dirty = false;
	for (unsigned x = 0; x < 2; ++x) {
		console->pixels->primary[x]   = ST_PIXEL_COLOR_RGB_TAG | (3 + x);
		console->pixels->alternate[x] = ST_PIXEL_COLOR_RGB_TAG | (5 + x);
		console->pixel_damage         = (struct st_console_rect) {x, 0, 1, 1};
		console->pixel_damage_valid   = true;
		CHECK(st_console_bitmap_prepare_frame(presenter, console, 1532) == 0);
	}
	/* Later console edits are outside the prepared frame. */
	console->pixels->alternate[1] = ST_PIXEL_COLOR_RGB_TAG | 7;
	for (unsigned i = 0; i < 4; ++i) {
		CHECK(cached[i]->pixels[0] == (i < 2 ? 1U : 2U));
		CHECK(
		    st_console_bitmap_presenter_frame(presenter, NULL, 3, 1, 1532 + phases[i], NULL, NULL, &frame)
		    == 0);
		CHECK(frame.pixels == cached[i]);
		CHECK(frame.pixels->pixels[0] == (i < 2 ? 3U : 5U));
		CHECK(frame.pixels->pixels[1] == (i < 2 ? 4U : 6U));
	}
	st_console_bitmap_presenter_destroy(presenter);
	st_console_destroy(console);
}

static void
check_identical_planes_do_not_animate(void)
{
	const struct st_console_display_config display = {
	    .backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config config = {
	    .columns      = 1,
	    .rows         = 1,
	    .cell_width   = 1,
	    .cell_height  = 1,
	    .blink_mode   = ST_CONSOLE_BLINK_PC,
	    .scaling_mode = ST_CONSOLE_SCALING_EXTERNAL,
	};
	struct st_console_bitmap_presenter *presenter = NULL;
	struct st_console_bitmap_frame      frame;
	st_console_t                       *console = NULL;
	uint64_t                            generation;
	const void                         *identity;

	CHECK(st_console_create(&console, &display, &config) == 0);
	console->pixels->primary[0]   = ST_PIXEL_COLOR_RGB_VALUE(1, 2, 3);
	console->pixels->alternate[0] = console->pixels->primary[0];
	console->cursor_visible       = false;
	CHECK(st_console_bitmap_presenter_create(&presenter, console) == 0);
	CHECK(st_console_bitmap_presenter_frame(presenter, console, 1, 1, 1000, NULL, NULL, &frame) == 0);
	generation = frame.generation;
	identity   = frame.image_identity;
	CHECK(st_console_bitmap_presenter_frame(presenter, NULL, 1, 1, 1266, NULL, NULL, &frame) == 0);
	CHECK(!frame.changed);
	CHECK(frame.generation == generation);
	CHECK(frame.image_identity == identity);
	/* Two indices with the same colour also share a phase until recoloured. */
	console->palette[50] = console->palette[51] = UINT32_C(0x010203);
	console->palette_dirty                      = true;
	console->pixels->primary[0]                 = 50;
	console->pixels->alternate[0]               = 51;
	console->pixel_damage                       = (struct st_console_rect) {0, 0, 1, 1};
	console->pixel_damage_valid                 = true;
	CHECK(st_console_bitmap_presenter_frame(presenter, console, 1, 1, 1532, NULL, NULL, &frame) == 0);
	generation           = frame.generation;
	console->palette[52] = 0x789abc;
	CHECK(st_console_bitmap_presenter_frame(presenter, console, 1, 1, 1798, NULL, NULL, &frame) == 0);
	CHECK(!frame.changed && frame.generation == generation && frame.image_identity == identity);
	console->palette[51] = 0x112233;
	CHECK(st_console_bitmap_presenter_frame(presenter, console, 1, 1, 1798, NULL, NULL, &frame) == 0);
	CHECK(frame.changed && frame.pixels->pixels[0] == 0x112233);
	/* An indexed cursor and an overlapping pointer both transform the index.
	 * Select the hidden phases after the live masks have been replaced. */
	console->cursor_custom = true;
	console->custom_cursor = (struct st_console_custom_cursor){
	    .visible = true, .blink = true, .range = 1, .index_base = 48, .index_xor = 3,
	};
	console->pointer_rect                   = (struct st_console_rect) {0, 0, 1, 1};
	console->indexed_pointer.and_mask[0][0] = 255;
	console->indexed_pointer.xor_mask[0][0] = 1;
	console->palette[48]                    = 0x222222;
	console->palette[49]                    = 0x333333;
	CHECK(st_console_bitmap_presenter_frame(presenter, console, 1, 1, 2000, NULL, NULL, &frame) == 0);
	CHECK(frame.pixels->pixels[0] == 0x222222); /* (50 ^ 3) ^ 1 */
	console->indexed_pointer.xor_mask[0][0] = 0;
	CHECK(st_console_bitmap_presenter_frame(presenter, NULL, 1, 1, 2133, NULL, NULL, &frame) == 0);
	CHECK(frame.pixels->pixels[0] == 0x112233); /* 50 ^ 1 */
	CHECK(st_console_bitmap_presenter_frame(presenter, NULL, 1, 1, 2266, NULL, NULL, &frame) == 0);
	CHECK(frame.pixels->pixels[0] == 0x333333); /* (51 ^ 3) ^ 1 */
	st_console_bitmap_presenter_destroy(presenter);
	st_console_destroy(console);
}

int
main(void)
{
	static const struct phase pc[] = {
	    {0,   false, true },
	    {132, false, true },
	    {133, false, false},
	    {265, false, false},
	    {266, true,  true },
	    {399, true,  false},
	};
	static const struct phase prestel[] = {
	    {0,    false, false},
	    {313,  false, false},
	    {314,  true,  true },
	    {628,  true,  false},
	    {942,  true,  true },
	    {1256, false, false},
	};
	static const struct phase commodore[] = {
	    {0,   true,  true },
	    {332, true,  true },
	    {333, false, false},
	    {666, true,  true },
	};
	static const struct phase atari[] = {
	    {0,    false, true},
	    {5000, false, true},
	};
	static const struct phase atari_st[] = {
	    {0,    false, true },
	    {499,  false, true },
	    {500,  false, false},
	    {1000, false, true },
	};

	check_mode(ST_CONSOLE_BLINK_PC, pc, sizeof(pc) / sizeof(pc[0]), false);
	check_mode(ST_CONSOLE_BLINK_PC, pc, sizeof(pc) / sizeof(pc[0]), true);
	check_mode(ST_CONSOLE_BLINK_PRESTEL, prestel, sizeof(prestel) / sizeof(prestel[0]), false);
	check_mode(ST_CONSOLE_BLINK_COMMODORE, commodore, sizeof(commodore) / sizeof(commodore[0]), false);
	check_mode(ST_CONSOLE_BLINK_ATARI, atari, sizeof(atari) / sizeof(atari[0]), false);
	check_mode(ST_CONSOLE_BLINK_ATARI_ST, atari_st, sizeof(atari_st) / sizeof(atari_st[0]), false);
	check_warm_scaled_cache();
	check_deferred_images();
	check_identical_planes_do_not_animate();
	return EXIT_SUCCESS;
}
