/* Snapshot ownership, renderer consistency, and console lifetime coverage. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console_test_internal.h"
#include "io_error.h"
#include "st_time.h"
#include "st_console_test.h"
#define CHECK(e)                                                                \
	do {                                                                    \
		if (!(e)) {                                                     \
			fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); \
			exit(1);                                                \
		}                                                               \
	} while (0)

static void
application(void *context, struct st_console_test_application *state)
{
	st_console_test_number(state, "owner_value", *(int *)context);
	st_console_test_text(state, "owner_text", "copied");
}

/* Inspect the driver's last actual frame directly so a forced capture cannot
 * satisfy the idle timeout test by publishing a frame itself. */
static void
synchronized_output(void)
{
	const struct st_console_display_config config = {.backend = ST_CONSOLE_BACKEND_TEST};
	const struct st_console_config geometry = {.columns = 3, .rows = 1, .cell_width = 1, .cell_height = 1};
	st_console_t *console;
	CHECK(st_console_create(&console, &config, &geometry) == 0);
	st_console_display_t *display = console->display;
	struct st_console_test_state *state = st_console_test_state(display);
	(void)st_mutex_lock(&display->backend_mutex);
	state->clock_override = true;
	state->clock_ms = 1000;
	(void)st_mutex_unlock(&display->backend_mutex);
	struct st_pixel_buffer *art;
	CHECK(st_pixel_buffer_create(&art, 2, 1, true) == 0);
	CHECK(st_pixel_buffer_ensure_fast_blink(art) == 0);
	const uint32_t red = 0xff0000, green = 0x00ff00;
	CHECK(st_console_set_palette(console, 1, &red, 1) == 0);
	art->primary[0] = art->primary[1] = 1;
	art->alternate[0] = art->alternate[1] = ST_PIXEL_COLOR_RGB_TAG;
	st_pixel_set_bit(art->fast_blink, 1, true);
	const struct st_pixel_blit blit = {.width = 2, .height = 1, .scale_x = 1, .scale_y = 1};
	CHECK(st_console_begin_update(console) == 0);
	CHECK(st_console_blit_pixels(console, art, NULL, &blit) == 0);
	CHECK(st_console_set_position(console, 2, 0) == 0);
	CHECK(st_console_set_cursor(console, true, ST_CONSOLE_CURSOR_BLOCK) == 0);
	CHECK(st_console_set_synchronized_output(console, true) == 0);
	CHECK(st_console_set_palette(console, 1, &green, 1) == 0);
	CHECK(st_console_set_cursor(console, false, ST_CONSOLE_CURSOR_BLOCK) == 0);
	CHECK(st_console_end_update(console) == 0);
	unsigned seen = 0;
	for (unsigned tick = 0; tick < 24; ++tick) {
		(void)st_mutex_lock(&display->backend_mutex);
		state->clock_ms = 1000 + (tick * 133 + 2) / 3;
		(void)st_mutex_unlock(&display->backend_mutex);
		struct st_console_test_capture *frame;
		CHECK(st_console_test_capture(display, &frame) == 0);
		bool slow = tick / 6 % 2 == 0, fast = tick / 4 % 2 == 0, cursor = tick / 3 % 2 == 0;
		CHECK(frame->palette[1] == green && !frame->cursor_visible);
		CHECK(frame->rgb->pixels[0] == (slow ? red : 0));
		CHECK(frame->rgb->pixels[1] == (fast ? red : 0));
		CHECK((frame->rgb->pixels[2] != 0) == cursor);
		seen |= 1U << (slow + 2 * fast + 4 * cursor);
		st_console_test_capture_destroy(frame);
	}
	CHECK(seen == 255);
	/* Atari has no animation wakeups. Switch while held, then let a new
	 * synchronized update expire with no further input or capture requests. */
	CHECK(st_console_set_blink_mode(console, ST_CONSOLE_BLINK_ATARI) == 0);
	CHECK(st_console_set_synchronized_output(console, false) == 0);
	CHECK(st_console_set_synchronized_output(console, true) == 0);
	CHECK(st_console_set_palette(console, 1, &red, 1) == 0);
	(void)st_mutex_lock(&display->backend_mutex);
	CHECK(state->frame.pixels->pixels[0] == green);
	(void)st_mutex_unlock(&display->backend_mutex);
	uint64_t start, now;
	CHECK(st_time_monotonic_ms(&start) == 0);
	for (;;) {
		(void)st_mutex_lock(&display->backend_mutex);
		bool published = state->frame.pixels->pixels[0] == red;
		(void)st_mutex_unlock(&display->backend_mutex);
		CHECK(st_time_monotonic_ms(&now) == 0);
		if (published)
			break;
		CHECK(now - start < 3000);
		const struct timespec pause = {.tv_nsec = 10000000};
		(void)st_thread_sleep(&pause, NULL);
	}
	CHECK(st_console_synchronized_output_mode(console) == 2);
	st_pixel_buffer_destroy(art);
	st_console_destroy(console);
}

int
main(void)
{
	synchronized_output();
	const struct st_console_display_config config = {.backend = ST_CONSOLE_BACKEND_TEST};
	const struct st_console_config geometry = {.columns = 2, .rows = 2, .cell_width = 2, .cell_height = 2};
	st_console_display_t          *display  = NULL;
	st_console_t                  *console = NULL, *peer = NULL;
	CHECK(st_console_display_create(&display, &config) == 0);
	CHECK(st_console_create_on_display(&console, display, &geometry) == 0);
	bool mouse = false;
	CHECK(st_console_set_pointer_visible(console, false) == 0);
	CHECK(st_console_mouse_present(console, &mouse) == 0 && mouse);
	CHECK(st_console_set_cursor(console, false, ST_CONSOLE_CURSOR_BLOCK) == 0);
	uint8_t                              bitmap[]  = {0xc0, 0x40};
	const struct st_console_font_mapping mapping[] = {
	    {0x1f579, 0}
        };
	const struct st_console_font font = {.bitmap = bitmap,
	    .bitmap_size                             = 2,
	    .width                                   = 2,
	    .height                                  = 2,
	    .glyph_count                             = 1,
	    .row_stride                              = 1,
	    .glyph_stride                            = 2,
	    .mapping                                 = mapping,
	    .mapping_count                           = 1};
	uint16_t                     identifier;
	CHECK(st_console_register_font_copy(console, &font, &identifier) == 0);
	const struct st_console_cell cell      = {.glyph = 0,
	    .glyph_font                             = identifier,
	    .font                                   = identifier,
	    .foreground                             = 3,
	    .background                             = ST_CONSOLE_COLOR_RGB_VALUE(1, 2, 3),
	    .hyperlink                              = 42};
	const struct st_console_rect rectangle = {0, 0, 1, 1};
	CHECK(st_console_write_cells(console, &rectangle, &cell, 1) == 0);
	uint32_t colour = 0x123456;
	CHECK(st_console_set_palette(console, 3, &colour, 1) == 0);
	int owner = 7;
	st_console_test_set_application_reader(display, application, &owner);
	struct st_console_test_capture *first = NULL, *second = NULL;
	CHECK(st_console_test_capture(display, &first) == 0);
	CHECK(first->columns == 2 && first->rows == 2 && first->cells[0].hyperlink == 42);
	CHECK(first->planes->primary[0] == 3 && first->palette[3] == 0x123456);
	CHECK(first->rgb->pixels[0] == 0x123456
	      && first->cells[0].background == ST_CONSOLE_COLOR_RGB_VALUE(1, 2, 3));
	CHECK(first->application.fields[0].number == 7);
	CHECK(strcmp(first->application.fields[1].text, "copied") == 0);
	CHECK(first->glyph_count != 0 && strcmp(first->glyphs[0].text, "\xf0\x9f\x95\xb9") == 0);
	owner  = 9;
	colour = 0xabcdef;
	CHECK(st_console_set_palette(console, 3, &colour, 1) == 0);
	CHECK(st_console_test_capture(display, &second) == 0);
	CHECK(second->id > first->id && second->presentation > first->presentation);
	CHECK(second->rgb->pixels[0] == 0xabcdef && second->application.fields[0].number == 9);
	CHECK(first->rgb->pixels[0] == 0x123456 && first->application.fields[0].number == 7);
	st_console_test_capture_destroy(second);
	second                      = NULL;
	struct st_pixel_buffer *art = NULL;
	CHECK(st_pixel_buffer_create(&art, 1, 1, true) == 0);
	art->primary[0]   = ST_CONSOLE_COLOR_RGB_VALUE(0x12, 0x34, 0x56);
	art->alternate[0] = ST_CONSOLE_COLOR_RGB_VALUE(0xab, 0xcd, 0xef);
	const struct st_pixel_blit blit =
	    {.width = 1, .height = 1, .destination_x = 3, .destination_y = 3, .scale_x = 1, .scale_y = 1};
	CHECK(st_console_blit_pixels(console, art, NULL, &blit) == 0);
	CHECK(st_console_test_capture(display, &second) == 0);
	CHECK(second->planes->primary[15] == art->primary[0]);
	CHECK(second->planes->alternate[15] == art->alternate[0]);
	st_pixel_buffer_destroy(art);
	st_console_test_capture_destroy(second);
	second = NULL;
	CHECK(st_console_create_on_display(&peer, display, &geometry) == 0);
	CHECK(st_console_set_synchronized_output(console, true) == 0);
	CHECK(st_console_display_activate(display, peer) == 0);
	CHECK(st_console_synchronized_output_mode(peer) == 2);
	CHECK(st_console_test_capture(display, &second) == 0);
	CHECK(first->console_id != second->console_id);
	st_console_test_capture_destroy(second);
	second = NULL;
	CHECK(st_console_resize(peer, 3, 4) == 0);
	CHECK(st_console_test_capture(display, &second) == 0);
	CHECK(second->columns == 3 && second->rows == 4 && second->planes->height == 8);
	st_console_test_capture_destroy(second);
	st_console_test_set_application_reader(display, NULL, NULL);
	st_console_destroy(peer);
	st_console_destroy(console);
	st_console_display_destroy(display);
	CHECK(first->rgb->pixels[0] == 0x123456 && first->cells[0].hyperlink == 42);
	st_console_test_capture_destroy(first);
	const struct st_console_display_config headless = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	CHECK(st_console_create(&console, &headless, &geometry) == 0);
	CHECK(st_console_mouse_present(console, &mouse) == 0 && !mouse);
	CHECK(st_console_set_synchronized_output(console, true) == 0);
	CHECK(st_console_synchronized_output_mode(console) == 0);
	st_console_destroy(console);
	return 0;
}
