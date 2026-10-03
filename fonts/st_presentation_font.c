/* Add the console's drawable missing-text glyphs to its fallback font. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_presentation_font.h"

#include "io_error.h"

#include <stdlib.h>
#include <string.h>

static void
set_pixel(uint8_t *glyph, size_t stride, uint32_t x, uint32_t y)
{
	glyph[(size_t)y * stride + x / 8] |= (uint8_t)(0x80U >> (x % 8));
}

static void
draw_box(uint8_t *glyph, size_t stride, uint32_t width, uint32_t height,
	bool cross)
{
	if (width == 0 || height == 0)
		return;
	for (uint32_t x = 0; x < width; ++x) {
		set_pixel(glyph, stride, x, 0);
		set_pixel(glyph, stride, x, height - 1);
	}
	for (uint32_t y = 0; y < height; ++y) {
		set_pixel(glyph, stride, 0, y);
		set_pixel(glyph, stride, width - 1, y);
	}
	if (width < 3 || height < 3)
		return;
	for (uint32_t y = 1; y + 1 < height; ++y) {
		uint32_t x = 1 + (uint32_t)(((uint64_t)(width - 3) * (y - 1))
		    / (height - 2));

		set_pixel(glyph, stride, x, y);
		if (cross)
			set_pixel(glyph, stride, width - 1 - x, y);
	}
}

static bool
bank_size(uint32_t count, size_t stride, size_t *size)
{
	if (count != 0 && stride > SIZE_MAX / count)
		return false;
	*size = (size_t)count * stride;
	return true;
}

void
st_presentation_font_destroy(struct st_presentation_font *font)
{
	if (font == NULL)
		return;
	free((void *)font->font.bitmap);
	free((void *)font->font.wide_bitmap);
	free((void *)font->font.mapping);
	free((void *)font->font.cluster_mapping);
	memset(font, 0, sizeof(*font));
}

int
st_presentation_font_create(struct st_presentation_font *result,
	const struct st_console_font *base)
{
	struct st_presentation_font created = {0};
	struct st_console_font_mapping *mapping = NULL;
	struct st_console_font_cluster_mapping *clusters = NULL;
	uint8_t *bitmap = NULL;
	uint8_t *wide_bitmap = NULL;
	uint32_t narrow_count;
	uint32_t wide_count;
	size_t bitmap_size;
	size_t wide_bitmap_size;
	size_t wide_row_stride;
	size_t wide_glyph_stride;

	if (result == NULL || base == NULL || base->bitmap == NULL
	    || base->glyph_count > UINT32_MAX - 2
	    || base->wide_glyph_count == UINT32_MAX)
		return IO_ERROR_INVALID_ARGUMENT;
	narrow_count = base->glyph_count + 2;
	wide_count = base->wide_glyph_count + 1;
	wide_row_stride = base->wide_glyph_count == 0
	    ? ((size_t)base->width * 2 + 7) / 8 : base->wide_row_stride;
	wide_glyph_stride = base->wide_glyph_count == 0
	    ? wide_row_stride * base->height : base->wide_glyph_stride;
	if (!bank_size(narrow_count, base->glyph_stride, &bitmap_size)
	    || !bank_size(wide_count, wide_glyph_stride, &wide_bitmap_size))
		return IO_ERROR_OUT_OF_MEMORY;
	bitmap = calloc(1, bitmap_size);
	wide_bitmap = calloc(1, wide_bitmap_size);
	if (bitmap == NULL || wide_bitmap == NULL)
		goto out_of_memory;
	for (uint32_t i = 0; i < base->glyph_count; ++i)
		memcpy(bitmap + (size_t)i * base->glyph_stride,
		    base->bitmap + (size_t)i * base->glyph_stride,
		    base->glyph_stride);
	for (uint32_t i = 0; i < base->wide_glyph_count; ++i)
		memcpy(wide_bitmap + (size_t)i * wide_glyph_stride,
		    base->wide_bitmap + (size_t)i * base->wide_glyph_stride,
		    base->wide_glyph_stride);
	draw_box(bitmap + (size_t)base->glyph_count * base->glyph_stride,
	    base->row_stride, base->width, base->height, false);
	draw_box(bitmap + (size_t)(base->glyph_count + 1) * base->glyph_stride,
	    base->row_stride, base->width, base->height, true);
	draw_box(wide_bitmap + (size_t)base->wide_glyph_count
	    * wide_glyph_stride, wide_row_stride, base->width * 2,
	    base->height, true);
	if (base->mapping_count != 0) {
		if (base->mapping_count > SIZE_MAX / sizeof(mapping[0]))
			goto out_of_memory;
		mapping = malloc(base->mapping_count * sizeof(mapping[0]));
		if (mapping == NULL)
			goto out_of_memory;
		memcpy(mapping, base->mapping,
		    base->mapping_count * sizeof(mapping[0]));
		for (size_t i = 0; i < base->mapping_count; ++i)
			if (mapping[i].glyph >= base->glyph_count)
				mapping[i].glyph += 2;
	}
	if (base->cluster_mapping_count != 0) {
		if (base->cluster_mapping_count > SIZE_MAX / sizeof(clusters[0]))
			goto out_of_memory;
		clusters = malloc(base->cluster_mapping_count * sizeof(clusters[0]));
		if (clusters == NULL)
			goto out_of_memory;
		memcpy(clusters, base->cluster_mapping,
		    base->cluster_mapping_count * sizeof(clusters[0]));
		for (size_t i = 0; i < base->cluster_mapping_count; ++i)
			if (clusters[i].glyph >= base->glyph_count)
				clusters[i].glyph += 2;
	}
	created.font = *base;
	created.font.bitmap = bitmap;
	created.font.bitmap_size = bitmap_size;
	created.font.glyph_count = narrow_count;
	created.font.wide_bitmap = wide_bitmap;
	created.font.wide_bitmap_size = wide_bitmap_size;
	created.font.wide_glyph_count = wide_count;
	created.font.wide_row_stride = wide_row_stride;
	created.font.wide_glyph_stride = wide_glyph_stride;
	created.font.mapping = mapping;
	created.font.cluster_mapping = clusters;
	created.font.fallback_glyph = base->glyph_count;
	created.font.decode_error_glyph = base->glyph_count + 1;
	created.font.wide_fallback_glyph = narrow_count
	    + base->wide_glyph_count;
	*result = created;
	return 0;
out_of_memory:
	free(bitmap);
	free(wide_bitmap);
	free(mapping);
	free(clusters);
	return IO_ERROR_OUT_OF_MEMORY;
}
