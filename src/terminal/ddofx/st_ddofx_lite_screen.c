/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>

#include "st_ddofx_graphics.h"
#include "st_ddofx_resource.h"
#include "st_ddofx_samples.h"
#include "st_time.h"

/* A RAW member is the VGA character/attribute memory image emitted by
 * ANSI2RAW. Even control characters are glyph indices in that image. */
void
st_ddofx_display_raw(const struct st_ddofx *ddo, const struct st_ddofx_sink *sink, const char *member)
{
	enum {
		COLUMNS = 80,
		ROWS    = 25
	};

	uint32_t columns, rows;
	if (!*member || !sink->open_asset || !sink->text_config
	    || st_cterm_size(sink->lite_terminal, &columns, &rows) || rows <= 1)
		return;
	char container[sizeof(ddo->resource_prefix) + 1];
	snprintf(container, sizeof(container), "%sM", ddo->resource_prefix);
	FILE *file = sink->open_asset(sink->context, "BBSTHEME", NULL, container);
	if (!file)
		return;
	uint32_t length;
	uint8_t  raw[ROWS * COLUMNS * 2];
	bool     valid = st_ddofx_resource_find(file, member, -1, &length, NULL) >= 0 && length == sizeof(raw)
	                 && fread(raw, 1, sizeof(raw), file) == sizeof(raw);
	fclose(file);
	if (!valid)
		return;

	st_console_t           *console = sink->text_config->console;
	struct st_console_style style;
	if (st_console_get_style(console, &style))
		return;
	struct st_console_rect  area  = {0, 1, columns < COLUMNS ? columns : COLUMNS,
	    rows - 1 < ROWS ? rows - 1 : ROWS};
	struct st_console_cell *cells = malloc(area.width * area.height * sizeof(*cells));
	if (!cells)
		return;
	static const uint8_t dos_colours[] = {0, 4, 2, 6, 1, 5, 3, 7};
	for (unsigned y = 0; y < area.height; ++y) {
		for (unsigned x = 0; x < area.width; ++x) {
			unsigned at    = 2 * (y * COLUMNS + x);
			uint8_t  attr  = raw[at + 1];
			uint32_t flags = ST_CONSOLE_CELL_LEGACY_FOREGROUND | ST_CONSOLE_CELL_LEGACY_BACKGROUND;
			if (attr & 0x08)
				flags |= ST_CONSOLE_CELL_BRIGHT;
			if (attr & 0x80)
				flags |= ST_CONSOLE_CELL_BLINK;
			cells[y * area.width + x] = (struct st_console_cell) {.font = style.font,
			    .glyph_font                                             = style.font,
			    .glyph                                                  = raw[at],
			    .foreground                                             = dos_colours[attr & 7],
			    .background                                             = dos_colours[(attr >> 4) & 7],
			    .flags                                                  = flags};
		}
	}
	(void)st_console_write_complete_cells(console, &area, cells, area.width);
	free(cells);
}

struct st_ddofx_lite_fade {
	st_console_t *display;
	uint32_t      original[256];
	uint64_t      started;
	unsigned      duration;
	bool          fade_in;
};

void
st_ddofx_restore_lite_palette(struct st_ddofx *ddo)
{
	struct st_ddofx_lite_fade *f = ddo->lite_fade;
	if (f) {
		(void)st_console_set_palette(f->display, 0, f->original, 256);
		free(f);
		ddo->lite_fade = NULL;
	}
}

bool
st_ddofx_lite_fade_step(struct st_ddofx *ddo, uint64_t now)
{
	struct st_ddofx_lite_fade *f       = ddo->lite_fade;
	uint64_t                   elapsed = now - f->started;
	bool                       done    = elapsed >= f->duration;
	if (done && f->fade_in) {
		st_ddofx_restore_lite_palette(ddo);
		ddo->fade_due = 0;
		return false;
	}

	/* DLY is the duration in milliseconds. Sample the ramp at display
	 * intervals so a long transition leaves the session responsive. */
	unsigned level = done ? 0 : (unsigned)(255 * elapsed / f->duration);
	if (!done && !f->fade_in)
		level = 255 - level;
	uint32_t palette[256];
	for (size_t i = 0; i < 256; ++i) {
		uint32_t c     = f->original[i];
		unsigned red   = (c >> 16 & 255) * level / 255;
		unsigned green = (c >> 8 & 255) * level / 255;
		unsigned blue  = (c & 255) * level / 255;
		palette[i]     = (red << 16) | (green << 8) | blue;
	}
	(void)st_console_set_palette(f->display, 0, palette, 256);
	uint64_t end  = f->started + f->duration;
	ddo->fade_due = done ? 0 : end - now < 16 ? end : now + 16;
	return !done;
}

void
st_ddofx_lite_fade(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, bool fade_in, int32_t duration)
{
	if (duration < 0 || !sink->text_config)
		return;
	uint64_t now;
	if (st_time_monotonic_ms(&now))
		return;
	struct st_ddofx_lite_fade *f = ddo->lite_fade;
	if (fade_in && !f)
		return;
	if (!fade_in && f)
		return;
	if (!f) {
		f = malloc(sizeof(*f));
		if (!f)
			return;
		f->display = sink->text_config->console;
		if (st_console_get_palette(f->display, 0, f->original, 256)) {
			free(f);
			return;
		}
		ddo->lite_fade = f;
	}
	f->fade_in  = fade_in;
	f->started  = now;
	f->duration = (unsigned)duration;
	(void)st_ddofx_lite_fade_step(ddo, now);
}

/* Animate the display origin in units of the SDK's 400-scanline text mode.
 * The terminal keeps writing to its unshifted logical page throughout. */
static bool
show_offset(struct st_ddofx *ddo)
{
	uint32_t pixels = (uint64_t)ddo->slide.height * ddo->slide.offset / 400;
	return st_console_set_vertical_origin(ddo->slide.display, ddo->slide.rows,
		   ddo->slide.suspended ? 0 : pixels)
	       == 0;
}

void
st_ddofx_slide_restore(struct st_ddofx *ddo)
{
	if (ddo->slide.display)
		(void)st_console_set_vertical_origin(ddo->slide.display, 0, 0);
	ddo->slide.display = NULL;
	ddo->slide.due     = 0;
}

void
st_ddofx_slide_suspend(struct st_ddofx *ddo, bool suspended)
{
	ddo->slide.suspended = suspended;
	if (ddo->slide.display && !show_offset(ddo))
		st_ddofx_slide_restore(ddo);
}

void
st_ddofx_slide(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, bool up, bool bounce)
{
	uint32_t columns, rows, width, height;
	uint64_t now;
	if (!sink->text_config || st_cterm_size(sink->lite_terminal, &columns, &rows)
	    || st_console_cell_pixel_size(sink->text_config->console, &width, &height) || !height || !rows
	    || rows > UINT32_MAX / height || st_time_monotonic_ms(&now))
		return;
	ddo->slide.display  = sink->text_config->console;
	ddo->slide.up       = up;
	ddo->slide.bounce   = bounce;
	ddo->slide.rows     = rows;
	ddo->slide.height   = rows * height;
	ddo->slide.offset   = up ? 0 : 400;
	ddo->slide.velocity = -3;
	ddo->slide.steps = ddo->slide.rebounds = 0;
	ddo->slide.due                         = now + 14;
	if (!show_offset(ddo)) {
		st_ddofx_slide_restore(ddo);
		return;
	}
	if (!bounce)
		st_ddofx_term_sound(ddo, sink, up ? 20 : 19);
}

bool
st_ddofx_slide_step(struct st_ddofx *ddo, const struct st_ddofx_sink *sink)
{
	uint64_t now;
	if (st_time_monotonic_ms(&now)) {
		st_ddofx_slide_restore(ddo);
		return false;
	}
	if (now < ddo->slide.due)
		return true;
	bool impact = false;
	do {
		ddo->slide.due += 14;
		if (ddo->slide.bounce) {
			ddo->slide.offset += ddo->slide.velocity;
			if (++ddo->slide.steps % 4 == 0)
				--ddo->slide.velocity;
			if (ddo->slide.offset <= 0) {
				ddo->slide.offset   = 0;
				ddo->slide.velocity = abs(-ddo->slide.velocity - 2);
				impact              = true;
				if (++ddo->slide.rebounds == 10)
					ddo->slide.due = 0;
			}
		}
		else {
			ddo->slide.offset += ddo->slide.up ? 10 : -10;
			if (ddo->slide.offset == 0 || ddo->slide.offset == 400)
				ddo->slide.due = 0;
		}
	} while (ddo->slide.due && now >= ddo->slide.due);
	if (impact)
		st_ddofx_term_sound(ddo, sink, 15);
	if (!show_offset(ddo) || (!ddo->slide.due && !ddo->slide.up))
		st_ddofx_slide_restore(ddo);
	return ddo->slide.due != 0;
}
