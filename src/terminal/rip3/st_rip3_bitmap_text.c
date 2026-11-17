/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_bitmap_text.h"

#include "st_rip3_ports.h"
#include "io_error.h"

#include <stdint.h>
#include <math.h>

int
st_rip3_bitmap_text_measure(const struct st_rip3_maf_font *font,
    const struct st_rip3_bitmap_text_style *style, size_t length,
    struct st_rip3_bitmap_text_metrics *result)
{
	struct st_rip3_bitmap_text_metrics metrics = {0};
	uint32_t cell_width, cell_height, span;

	if (font == NULL || style == NULL || result == NULL || font->glyphs == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (font->width == 0 || font->width > UINT16_MAX
	    || font->height == 0 || font->height > UINT16_MAX
	    || font->row_bytes != (font->width + 7U) / 8U
	    || font->glyph_size == 0 || font->glyph_size > UINT16_MAX
	    || font->row_bytes * font->height != font->glyph_size)
		return IO_ERROR_INVALID_FORMAT;
	if (style->magnification == 0 || style->magnification > 10)
		return IO_ERROR_OUT_OF_RANGE;
	cell_width = font->width * style->magnification;
	cell_height = font->height * style->magnification;
	if (length > INT32_MAX / cell_width)
		return IO_ERROR_OUT_OF_RANGE;
	span = (uint32_t)length * cell_width;
	if (length != 0) {
		metrics.width = style->vertical ? cell_height : span;
		metrics.height = style->vertical ? span : cell_height;
		metrics.advance = style->vertical ? (struct st_rip3_point){0,-(int32_t)span}
		    : (struct st_rip3_point){(int32_t)span,0};
	}
	*result = metrics;
	return 0;
}

/* -1: outside cell span, 0: blank cell pixel, 1: foreground glyph pixel. */
static int
glyph_pixel(const struct st_rip3_maf_font *font,
    const struct st_rip3_bitmap_text_style *style, const uint8_t *text,
    int64_t span, const struct st_rip3_text_position *origin, int64_t x, int64_t y)
{
	int64_t dx = x - (int64_t)origin->x;
	int64_t dy = y - (int64_t)origin->y;
	int64_t u = style->vertical ? -dy : dx;
	int64_t v = style->vertical ? dx : dy;
	size_t column, row;
	const uint8_t *glyph;

	if (u < 0 || u >= span || v < 0
	    || v >= (int64_t)font->height * style->magnification)
		return -1;
	column = (size_t)(u / style->magnification);
	row = (size_t)(v / style->magnification);
	glyph = st_rip3_maf_glyph(font, text[column / font->width]);
	if (glyph == NULL)
		return 0;
	column %= font->width;
	return (glyph[row * font->row_bytes + column / 8]
	    & (UINT8_C(0x80) >> (column & 7))) != 0;
}

int
st_rip3_draw_bitmap_text(struct st_rip3_ports *ports,
    const struct st_rip3_maf_font *font,
    const struct st_rip3_bitmap_text_style *style,
    const uint8_t *text, size_t length, const struct st_rip3_text_position *origin,
    uint8_t foreground, uint8_t background, enum st_rip3_write_mode mode)
{
	struct st_rip3_bitmap_text_metrics metrics;
	struct st_rip3_port_info info;
	int64_t left, top, right, bottom, span;
	unsigned current;
	int status;
	struct st_rip3_text_position snapped;
	const double limit = 1024.0*INT32_MAX;

	if (ports == NULL || origin == NULL || (text == NULL && length != 0)
	    || mode < ST_RIP3_COPY || mode > ST_RIP3_NOT)
		return IO_ERROR_INVALID_ARGUMENT;
	if (!isfinite(origin->x) || !isfinite(origin->y)
	    || fabs(origin->x) > limit || fabs(origin->y) > limit) return IO_ERROR_OUT_OF_RANGE;
	/* Keep the caller's fractional pen position intact. Snap the final bitmap
	 * address once, with the same half-positive rule as classic layout. */
	snapped = (struct st_rip3_text_position){floor(origin->x+0.5),floor(origin->y+0.5)};
	origin = &snapped;
	status = st_rip3_bitmap_text_measure(font, style, length, &metrics);
	if (status != 0 || length == 0)
		return status;
	status = st_rip3_ports_current(ports, &current);
	if (status != 0)
		return status;
	status = st_rip3_ports_info(ports, current, &info);
	if (status != 0 || !info.viewport_active)
		return status;
	span = style->vertical ? metrics.height : metrics.width;
	left = (int64_t)origin->x;
	right = left + metrics.width - 1;
	top = style->vertical ? (int64_t)origin->y - metrics.height + 1 : (int64_t)origin->y;
	bottom = top + metrics.height - 1;
	if (style->shadow) {
		right++;
		bottom++;
	}
	if (left < 0)
		left = 0;
	if (top < 0)
		top = 0;
	if (right > info.viewport.x1 - info.viewport.x0)
		right = info.viewport.x1 - info.viewport.x0;
	if (bottom > info.viewport.y1 - info.viewport.y0)
		bottom = info.viewport.y1 - info.viewport.y0;
	for (int64_t y = top; y <= bottom; y++) {
		for (int64_t x = left; x <= right; x++) {
			int pixel = glyph_pixel(font, style, text, span, origin, x, y);
			uint8_t color;

			if (pixel == 1)
				color = foreground;
			else if ((pixel == 0 && style->opaque)
			    || (style->shadow && glyph_pixel(font, style, text,
			    span, origin, x - 1, y - 1) == 1))
				color = background;
			else
				continue;
			status = st_rip3_ports_plot(ports, (int32_t)x, (int32_t)y, color, mode);
			if (status != 0)
				return status;
		}
	}
	return 0;
}
