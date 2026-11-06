/* Optional bitmap driver inspection/control. No process addresses cross the wire. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_CONSOLE_TEST_H
#define ST_CONSOLE_TEST_H

#include "st_console.h"

struct st_console_test_field {
	char name[64];
	int64_t number;
	char *text; /* NULL selects number. */
};

struct st_console_test_application {
	struct st_console_test_field fields[128];
	size_t count;
	int error;
};

struct st_console_test_glyph {
	uint16_t font;
	uint32_t glyph;
	char *text;
	size_t text_size;
};

/* All buffers are owned by this immutable capture, independently of the display. */
struct st_console_test_capture {
	uint64_t id, console_id, presentation, generation, timestamp, events_dequeued;
	uint32_t columns, rows, cell_width, cell_height, pixel_top;
	uint32_t cursor_x, cursor_y, cursor_shape, blink_mode, video_flags;
	struct st_console_custom_cursor cursor;
	struct st_console_pixel_cursor pixel_cursor;
	bool cursor_visible, cursor_custom, fullscreen, pointer_visible;
	uint32_t pointer_shape, output_width, output_height;
	struct st_console_rect destination;
	double scaling;
	char *title, *clipboard, *url;
	uint64_t url_serial;
	struct st_console_cell *cells;
	struct st_pixel_buffer *planes;
	struct st_rgb_buffer *rgb; /* Complete drawable, including letterboxing. */
	uint32_t *palette;
	struct st_console_test_glyph *glyphs;
	size_t glyph_count;
	struct st_console_test_application application;
};

/* The owner callback runs inside capture's console/backend lock transaction.
 * It must only copy application-owned state; never call console APIs or Wren.
 * Worker state may be copied under its own mutex, last in the lock order. */
typedef void (*st_console_test_application_reader)(void *context,
	struct st_console_test_application *application);
void st_console_test_number(struct st_console_test_application *application,
	const char *name, int64_t value);
void st_console_test_text(struct st_console_test_application *application,
	const char *name, const char *value);
void st_console_test_set_application_reader(st_console_display_t *display,
	st_console_test_application_reader reader, void *context);
int st_console_test_capture(st_console_display_t *display,
	struct st_console_test_capture **result);
void st_console_test_capture_destroy(struct st_console_test_capture *capture);

int st_console_test_start_control(st_console_display_t *display);
void st_console_test_stop_control(st_console_display_t *display);

/* Called on the application/input owner, outside all console locks. */
void st_console_test_service(st_console_display_t *display);
/* Pure argument detection, for dispatchers which initialize before main. */
bool st_console_test_requested(int argc, char *const argv[]);

#endif
