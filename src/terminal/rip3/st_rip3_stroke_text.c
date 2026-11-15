/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_stroke_text.h"

#include "st_rip3_draw_internal.h"
#include "st_rip3_ports.h"
#include "io_error.h"

#include <math.h>
#include <stdlib.h>

int
st_rip3_chr_text_measure(const struct st_rip3_chr *font,
    const uint8_t *text, size_t length, struct st_rip3_chr_text_metrics *result)
{
	struct st_rip3_chr_text_metrics metrics = {0};
	struct st_rip3_chr_glyph glyph;
	int status;

	if (font == NULL || result == NULL || (text == NULL && length != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_rip3_chr_glyph(&glyph, font, (uint8_t)font->first_character);
	if (status != 0)
		return status;
	if (font->capital < -128 || font->capital > 127
	    || font->baseline < -128 || font->baseline > 127
	    || font->descender < -128 || font->descender > 127
	    || font->capital < font->baseline || font->baseline < font->descender)
		return IO_ERROR_INVALID_FORMAT;
	metrics.height = (unsigned)(font->capital - font->descender);
	for (size_t i = 0; i < length; i++) {
		status = st_rip3_chr_glyph(&glyph, font, text[i]);
		if (status == IO_ERROR_NOT_FOUND)
			continue;
		if (status != 0)
			return status;
		if (glyph.advance > INT32_MAX - metrics.advance)
			return IO_ERROR_OUT_OF_RANGE;
		metrics.advance += glyph.advance;
	}
	*result = metrics;
	return 0;
}

static int
stroke_count(const struct st_rip3_chr *font, const uint8_t *text,
    size_t length, size_t *result)
{
	size_t strokes = 0;

	for (size_t i = 0; i < length; i++) {
		struct st_rip3_chr_glyph glyph;
		struct st_rip3_chr_record record;
		size_t cursor = 0;
		int status = st_rip3_chr_glyph(&glyph, font, text[i]);

		if (status == IO_ERROR_NOT_FOUND)
			continue;
		if (status != 0)
			return status;
		if (glyph.has_scan)
			return IO_ERROR_NOT_SUPPORTED;
		while ((status = st_rip3_chr_next(&record, &glyph, &cursor)) == 0) {
			if (record.operation == ST_RIP3_CHR_DRAW) {
				if (strokes == SIZE_MAX)
					return IO_ERROR_OUT_OF_RANGE;
				strokes++;
			}
		}
		if (status != IO_ERROR_NOT_FOUND)
			return status;
	}
	*result = strokes;
	return 0;
}

static struct st_rip3_vector
transform(const struct st_rip3_point *origin,
    const struct st_rip3_chr_text_matrix *matrix, size_t advance, int x, int y)
{
	double u = (double)advance + x;

	return (struct st_rip3_vector){origin->x + matrix->xx * u + matrix->xy * y,
	    origin->y + matrix->yx * u + matrix->yy * y};
}

static int
stroke_build(const struct st_rip3_chr *font, const uint8_t *text,
    size_t length, const struct st_rip3_point *origin,
    const struct st_rip3_chr_text_matrix *matrix,
    struct st_rip3_vector *vertices, struct st_rip3_vector_run *runs, size_t capacity)
{
	size_t advance = 0, next = 0;

	for (size_t i = 0; i < length; i++) {
		struct st_rip3_chr_glyph glyph;
		struct st_rip3_chr_record record;
		int last_x = 0, last_y = 0;
		size_t cursor = 0;
		int status = st_rip3_chr_glyph(&glyph, font, text[i]);

		if (status == IO_ERROR_NOT_FOUND)
			continue;
		if (status != 0)
			return status;
		while ((status = st_rip3_chr_next(&record, &glyph, &cursor)) == 0) {
			if (record.operation == ST_RIP3_CHR_SCAN)
				return IO_ERROR_NOT_SUPPORTED;
			if (record.operation == ST_RIP3_CHR_DRAW) {
				if (next >= capacity)
					return IO_ERROR_INVALID_FORMAT;
				vertices[2 * next] = transform(origin, matrix, advance, last_x, last_y);
				vertices[2 * next + 1] = transform(origin, matrix, advance, record.x, record.y);
				runs[next] = (struct st_rip3_vector_run){2 * next, 2};
				next++;
			}
			last_x = record.x;
			last_y = record.y;
		}
		if (status != IO_ERROR_NOT_FOUND)
			return status;
		/* Measurement already bounded this same immutable span. */
		advance += glyph.advance;
	}
	return next == capacity ? 0 : IO_ERROR_INVALID_FORMAT;
}

static bool
mask_at(const struct st_rip3_stroke_mask *mask, int32_t x, int32_t y)
{
	if (mask->coverage == NULL || x < mask->bounds.x0 || x > mask->bounds.x1
	    || y < mask->bounds.y0 || y > mask->bounds.y1)
		return false;
	return mask->coverage[(size_t)(y - mask->bounds.y0) * mask->width
	    + (size_t)(x - mask->bounds.x0)] != 0;
}

static int
apply_shadow(struct st_rip3_ports *ports, const struct st_rip3_stroke_mask *ink,
    const struct st_rip3_stroke_mask *shadow, uint8_t foreground,
    uint8_t background, enum st_rip3_write_mode mode)
{
	struct st_rip3_rect bounds;

	if (ink->coverage == NULL)
		return st_rip3_stroke_apply(ports, shadow, background, background, mode);
	if (shadow->coverage == NULL)
		return st_rip3_stroke_apply(ports, ink, foreground, foreground, mode);
	bounds = ink->bounds;
	if (shadow->bounds.x0 < bounds.x0)
		bounds.x0 = shadow->bounds.x0;
	if (shadow->bounds.y0 < bounds.y0)
		bounds.y0 = shadow->bounds.y0;
	if (shadow->bounds.x1 > bounds.x1)
		bounds.x1 = shadow->bounds.x1;
	if (shadow->bounds.y1 > bounds.y1)
		bounds.y1 = shadow->bounds.y1;
	for (int32_t y = bounds.y0; y <= bounds.y1; y++) {
		for (int32_t x = bounds.x0; x <= bounds.x1; x++) {
			bool foreground_pixel = mask_at(ink, x, y);
			int status;

			if (!foreground_pixel && !mask_at(shadow, x, y))
				continue;
			status = st_rip3_ports_plot(ports, x, y,
			    foreground_pixel ? foreground : background, mode);
			if (status != 0)
				return status;
		}
	}
	return 0;
}

int
st_rip3_draw_chr_text(struct st_rip3_ports *ports,
    const struct st_rip3_chr *font, const uint8_t *text, size_t length,
    const struct st_rip3_point *origin, const struct st_rip3_chr_text_matrix *matrix,
    bool shadow, uint8_t foreground, uint8_t background,
    enum st_rip3_write_mode mode)
{
	struct st_rip3_chr_text_metrics metrics;
	struct st_rip3_port_info info;
	struct st_rip3_vector *vertices = NULL;
	struct st_rip3_vector_run *runs = NULL;
	struct st_rip3_stroke_mask ink = {0}, shade = {0};
	size_t count;
	unsigned current;
	int status;

	if (ports == NULL || origin == NULL || matrix == NULL
	    || mode < ST_RIP3_COPY || mode > ST_RIP3_NOT)
		return IO_ERROR_INVALID_ARGUMENT;
	if (!isfinite(matrix->xx) || !isfinite(matrix->xy)
	    || !isfinite(matrix->yx) || !isfinite(matrix->yy)
	    || fabs(matrix->xx) > INT32_MAX || fabs(matrix->xy) > INT32_MAX
	    || fabs(matrix->yx) > INT32_MAX || fabs(matrix->yy) > INT32_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	status = st_rip3_chr_text_measure(font, text, length, &metrics);
	if (status != 0 || length == 0)
		return status;
	status = st_rip3_ports_current(ports, &current);
	if (status != 0)
		return status;
	status = st_rip3_ports_info(ports, current, &info);
	if (status != 0 || !info.viewport_active)
		return status;
	status = stroke_count(font, text, length, &count);
	if (status != 0 || count == 0)
		return status;
	if (count > SIZE_MAX / (2 * sizeof(*vertices)) || count > SIZE_MAX / sizeof(*runs))
		return IO_ERROR_OUT_OF_RANGE;
	vertices = malloc(2 * count * sizeof(*vertices));
	runs = malloc(count * sizeof(*runs));
	if (vertices == NULL || runs == NULL) {
		status = IO_ERROR_OUT_OF_MEMORY;
		goto done;
	}
	status = stroke_build(font, text, length, origin, matrix, vertices, runs, count);
	if (status != 0)
		goto done;
	status = st_rip3_stroke_vectors(ports, vertices, 2 * count, runs, count, 1, &ink);
	if (status != 0)
		goto done;
	if (shadow) {
		for (size_t i = 0; i < 2 * count; i++) {
			vertices[i].x++;
			vertices[i].y++;
		}
		status = st_rip3_stroke_vectors(ports, vertices, 2 * count, runs, count, 1, &shade);
		if (status == 0)
			status = apply_shadow(ports, &ink, &shade, foreground, background, mode);
	}
	else
		status = st_rip3_stroke_apply(ports, &ink, foreground, background, mode);
done:
	st_rip3_stroke_free(&ink);
	st_rip3_stroke_free(&shade);
	free(vertices);
	free(runs);
	return status;
}
