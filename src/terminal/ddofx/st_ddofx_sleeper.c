/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdio.h>
#include <string.h>

#include "st_builtin_fonts.h"
#include "st_ddofx_graphics_internal.h"
#include "st_time.h"

enum {
	WARNING_MS = 180000,
	COLUMNS    = 40,
	ROWS       = 3
};

void
st_ddofx_sleep_arm(struct st_ddofx *ddo)
{
	uint64_t now;
	if (!ddo->graphics || st_time_monotonic_ms(&now) != 0)
		return;
	uint64_t duration     = (uint64_t)(ddo->sleeper.configured ? ddo->sleeper.minutes : 10) * 60000;
	ddo->sleeper.deadline = now + duration;
	/* For a short interval, allow a second of inactivity before opening
	 * the countdown. A dismissing key can then return to the door. */
	if (duration > WARNING_MS)
		ddo->sleeper.next_update = ddo->sleeper.deadline - WARNING_MS;
	else
		ddo->sleeper.next_update = now + (duration ? 1000 : 0);
}

void
st_ddofx_sleep_close(struct st_ddofx *ddo)
{
	struct st_ddofx_graphics *g = ddo->graphics;
	if (!g || !g->sleep_view)
		return;
	if (ddo->sleeper.warning)
		(void)st_console_raster_view_restore_transient(g->sleep_view, g->sleep_saved);
	st_console_destroy(g->sleep_view);
	st_pixel_buffer_destroy(g->sleep_saved);
	g->sleep_view        = NULL;
	g->sleep_saved       = NULL;
	ddo->sleeper.warning = false;
	st_ddofx_present_origin(ddo);
}

static bool
open_warning(struct st_ddofx *ddo)
{
	struct st_ddofx_graphics    *g    = ddo->graphics;
	const struct st_console_rect area = {(WIDTH - COLUMNS * 8) / 2, (HEIGHT - ROWS * 16) / 2, COLUMNS * 8,
	    ROWS * 16};
	struct st_console_font       font;
	if (!st_builtin_font_get(0, 8, 16, &font)
	    || st_console_create_raster_view(&g->sleep_view, g->display, COLUMNS, ROWS, 8, 16, area.x, area.y)
	    || st_console_register_static_font(g->sleep_view, &font, &g->sleep_font)
	    || st_console_get_pixels(&g->sleep_saved, g->display, &area, false)
	    || st_console_raster_view_adopt_transient(g->sleep_view))
		return false;
	ddo->sleeper.warning = true;
	st_ddofx_present_origin(ddo);
	return true;
}

static bool
draw_warning(struct st_ddofx_graphics *g, unsigned seconds)
{
	char countdown[COLUMNS];
	snprintf(countdown, sizeof(countdown), "Disconnecting in %u:%02u", seconds / 60, seconds % 60);
	const char            *lines[] = {"DDO F/X inactivity", countdown, "Press any key to continue"};
	struct st_console_cell cells[ROWS][COLUMNS];
	for (unsigned y = 0; y < ROWS; ++y) {
		size_t length = strlen(lines[y]), left = (COLUMNS - length) / 2;
		for (unsigned x = 0; x < COLUMNS; ++x) {
			struct st_console_cell *c = &cells[y][x];
			*c                        = (struct st_console_cell) {.font = g->sleep_font,
			    .foreground                      = ST_CONSOLE_COLOR_RGB_VALUE(255, 255, 255),
			    .background                      = y ? ST_CONSOLE_COLOR_RGB_VALUE(35, 42, 58)
			                                         : ST_CONSOLE_COLOR_RGB_VALUE(44, 75, 129)};
			uint8_t       width;
			unsigned char ch = x >= left && x < left + length ? lines[y][x - left] : ' ';
			if (st_console_font_lookup(g->sleep_view, c->font, ch, &c->glyph_font, &c->glyph, &width))
				return false;
		}
	}
	const struct st_console_rect grid = {0, 0, COLUMNS, ROWS};
	return st_console_write_complete_cells(g->sleep_view, &grid, &cells[0][0], COLUMNS) == 0;
}

uint32_t
st_ddofx_sleep_wait_ms(const struct st_ddofx *ddo)
{
	uint64_t now;
	if (!ddo->graphics || !ddo->sleeper.deadline || ddo->sleeper.suspended || ddo->background_download
	    || st_time_monotonic_ms(&now) != 0)
		return UINT32_MAX;
	if (now >= ddo->sleeper.next_update)
		return 0;
	uint64_t delay = ddo->sleeper.next_update - now;
	return delay < UINT32_MAX ? (uint32_t)delay : UINT32_MAX - 1;
}

bool
st_ddofx_sleep_poll(struct st_ddofx *ddo, const struct st_ddofx_sink *sink)
{
	if (st_ddofx_sleep_wait_ms(ddo) != 0)
		return ddo->sleeper.warning;
	uint64_t now;
	if (st_time_monotonic_ms(&now) != 0)
		return ddo->sleeper.warning;
	if (now >= ddo->sleeper.deadline) {
		st_ddofx_sleep_close(ddo);
		ddo->sleeper.deadline = 0;
		if (sink->disconnect)
			sink->disconnect(sink->context);
		return true;
	}
	uint64_t remaining = ddo->sleeper.deadline - now;
	unsigned seconds   = (unsigned)((remaining + 999) / 1000);
	if ((!ddo->sleeper.warning && !open_warning(ddo)) || !draw_warning(ddo->graphics, seconds)) {
		st_ddofx_sleep_close(ddo);
		/* Keep the session usable if the countdown cannot be displayed. */
		ddo->sleeper.deadline = 0;
		return false;
	}
	ddo->sleeper.next_update = ddo->sleeper.deadline - (seconds - 1) * UINT64_C(1000);
	return true;
}

bool
st_ddofx_sleep_key(struct st_ddofx *ddo, const struct st_console_event *event)
{
	if (!ddo->graphics)
		return false;
	bool warning = ddo->sleeper.warning;
	if (event->type == ST_CONSOLE_EVENT_PHYSICAL_KEY) {
		if (!event->data.physical_key.pressed) {
			if (ddo->sleeper.dismissal_key == event->data.physical_key.evdev) {
				ddo->sleeper.dismissal_key = 0;
				return true;
			}
			return warning;
		}
		ddo->sleeper.dismissal_key = warning ? event->data.physical_key.evdev : 0;
	}
	else if (ddo->sleeper.dismissal_key) {
		/* Consume the translated event paired with the dismissing press. */
		ddo->sleeper.dismissal_key = 0;
		return true;
	}
	if (warning)
		st_ddofx_sleep_close(ddo);
	st_ddofx_sleep_arm(ddo);
	return warning;
}
