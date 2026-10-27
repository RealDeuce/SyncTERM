/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_CONSOLE_TEST_INTERNAL_H
#define ST_CONSOLE_TEST_INTERNAL_H
#include "console_internal.h"
#include "console_config.h"
#if ST_CONSOLE_TEST
#define ST_CONSOLE_TEST_MAX_DIMENSION 4096U
static inline bool st_console_test_dimensions(uint64_t width, uint64_t height)
{
	return width != 0 && height != 0 && width <= ST_CONSOLE_TEST_MAX_DIMENSION
	    && height <= ST_CONSOLE_TEST_MAX_DIMENSION && width * height <= 4194304U;
}
struct test_control;
struct st_console_test_state {
	struct st_console_bitmap_presenter *presenter;
	struct st_console_bitmap_frame frame;
	struct st_console_bitmap_geometry geometry;
	uint32_t width, height;
	uint64_t presentation, timestamp, next_console_id, capture_id, url_serial, events_dequeued;
	bool fullscreen, clock_override;
	uint32_t clock_ms;
	char *clipboard, *url, *title;
	st_console_test_application_reader reader;
	void *reader_context;
	struct test_control *control;
};
struct st_console_test_state *st_console_test_state(st_console_display_t *display);
void st_console_test_control_destroy(st_console_display_t *display);
int st_console_test_control_start(st_console_display_t *display);
int st_console_test_host_resize(st_console_display_t *display, uint32_t width, uint32_t height);
#endif
#endif
