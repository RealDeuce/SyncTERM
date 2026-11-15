/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_fill.h"

#include "st_rip3_ports.h"
#include "io_error.h"

#include <stddef.h>
#include <stdint.h>

int
st_rip3_fill_custom(struct st_rip3_fill *fill, const unsigned rows[8],
    const struct st_rip3_point *origin)
{
	struct st_rip3_fill value;

	if (fill == NULL || rows == NULL || origin == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	for (size_t i = 0; i < 8; i++) {
		if (rows[i] > 255)
			return IO_ERROR_OUT_OF_RANGE;
		value.rows[i] = (uint8_t)rows[i];
	}
	value.origin = *origin;
	*fill = value;
	return 0;
}

int
st_rip3_fill_solid(struct st_rip3_fill *fill, bool foreground)
{
	if (fill == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	for (size_t i = 0; i < 8; i++)
		fill->rows[i] = foreground ? 255 : 0;
	fill->origin = (struct st_rip3_point){0, 0};
	return 0;
}

static struct st_rip3_rect
normalize(struct st_rip3_rect area)
{
	int32_t swap;

	if (area.x0 > area.x1) {
		swap = area.x0;
		area.x0 = area.x1;
		area.x1 = swap;
	}
	if (area.y0 > area.y1) {
		swap = area.y0;
		area.y0 = area.y1;
		area.y1 = swap;
	}
	return area;
}

static int
fill_area(struct st_rip3_ports *ports, struct st_rip3_rect area,
    const struct st_rip3_fill *fill, uint8_t foreground, uint8_t background,
    enum st_rip3_write_mode mode)
{
	struct st_rip3_port_info info;
	unsigned current;
	int status;

	status = st_rip3_ports_current(ports, &current);
	if (status != 0)
		return status;
	status = st_rip3_ports_info(ports, current, &info);
	if (status != 0)
		return status;
	if (!info.viewport_active)
		return 0;
	if (area.x0 < 0)
		area.x0 = 0;
	if (area.y0 < 0)
		area.y0 = 0;
	if (area.x1 > info.viewport.x1 - info.viewport.x0)
		area.x1 = info.viewport.x1 - info.viewport.x0;
	if (area.y1 > info.viewport.y1 - info.viewport.y0)
		area.y1 = info.viewport.y1 - info.viewport.y0;
	for (int32_t y = area.y0; y <= area.y1; y++) {
		/* Unsigned modular differences give Euclidean phase modulo eight,
		 * even for negative or extreme origins, without signed overflow. */
		unsigned row = ((uint32_t)y - (uint32_t)fill->origin.y) & 7;

		for (int32_t x = area.x0; x <= area.x1; x++) {
			unsigned col = ((uint32_t)x - (uint32_t)fill->origin.x) & 7;
			bool on = (fill->rows[row] & (UINT8_C(0x80) >> col)) != 0;

			status = st_rip3_ports_plot(ports, x, y,
			    on ? foreground : background, mode);
			if (status != 0)
				return status;
		}
	}
	return 0;
}

int
st_rip3_draw_bar(struct st_rip3_ports *ports, const struct st_rip3_rect *area,
    const struct st_rip3_fill *fill, uint8_t foreground, uint8_t background,
    enum st_rip3_write_mode mode)
{
	if (ports == NULL || area == NULL || fill == NULL
	    || mode < ST_RIP3_COPY || mode > ST_RIP3_NOT)
		return IO_ERROR_INVALID_ARGUMENT;
	return fill_area(ports, normalize(*area), fill, foreground, background, mode);
}

int
st_rip3_draw_filled_rectangle(struct st_rip3_ports *ports,
    const struct st_rip3_rect *area, const struct st_rip3_fill *fill,
    uint8_t foreground, uint8_t background, const struct st_rip3_pen *border,
    uint8_t border_color, enum st_rip3_write_mode mode)
{
	struct st_rip3_rect rect;
	struct st_rip3_pen outline;
	struct st_rip3_point corners[4];
	int status;

	if (ports == NULL || area == NULL || fill == NULL
	    || mode < ST_RIP3_COPY || mode > ST_RIP3_NOT)
		return IO_ERROR_INVALID_ARGUMENT;
	/* Validate before the fill. Border patterns are deliberately ignored. */
	if (border != NULL) {
		status = st_rip3_pen_set(&outline, false, 0, 0, border->thickness);
		if (status != 0)
			return status;
	}
	rect = normalize(*area);
	if (rect.x0 != rect.x1 && rect.y0 != rect.y1) {
		struct st_rip3_rect interior = rect;

		interior.x1--;
		interior.y1--;
		status = fill_area(ports, interior, fill, foreground, background, mode);
		if (status != 0)
			return status;
	}
	if (border == NULL)
		return 0;
	corners[0] = (struct st_rip3_point){rect.x0, rect.y0};
	corners[1] = (struct st_rip3_point){rect.x1, rect.y0};
	corners[2] = (struct st_rip3_point){rect.x1, rect.y1};
	corners[3] = (struct st_rip3_point){rect.x0, rect.y1};
	if (rect.x0 == rect.x1 || rect.y0 == rect.y1)
		return st_rip3_draw_line(ports, &corners[0], &corners[2], &outline,
		    border_color, background, ST_RIP3_COPY);
	for (unsigned i = 0; i < 4; i++) {
		status = st_rip3_draw_line(ports, &corners[i], &corners[(i + 1) % 4],
		    &outline, border_color, background, ST_RIP3_COPY);
		if (status != 0)
			return status;
	}
	return 0;
}
