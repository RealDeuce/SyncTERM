/* Add the console's drawable missing-text glyphs to its fallback font. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_presentation_font.h"
#include "st_emoji_fonts.h"

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
	free((void *)font->font.color_glyphs);
	memset(font, 0, sizeof(*font));
}

static const struct st_console_font *
emoji_font(const struct st_console_font *base)
{
	if (base->width != 8)
		return NULL;
	switch (base->height) {
		case 8: return &st_emoji_8_font;
		case 14: return &st_emoji_14_font;
		case 16: return &st_emoji_16_font;
		default: return NULL;
	}
}

static uint32_t
emoji_glyph(const struct st_console_font *emoji,
	const struct st_console_font *base, uint32_t narrow_count, uint32_t glyph)
{
	return glyph < emoji->glyph_count ? base->glyph_count + 2 + glyph
	    : narrow_count + base->wide_glyph_count + 1 + (glyph - emoji->glyph_count);
}

static int
compare_color(const void *left, const void *right)
{
	uint32_t a = ((const struct st_console_color_glyph *)left)->glyph;
	uint32_t b = ((const struct st_console_color_glyph *)right)->glyph;

	return (a > b) - (a < b);
}

static int
create_presentation(struct st_presentation_font *result,
	const struct st_console_font *base, bool prefer_emoji)
{
	struct st_presentation_font created = {0};
	struct st_console_font_mapping *mapping = NULL;
	struct st_console_font_cluster_mapping *clusters = NULL;
	struct st_console_color_glyph *colors = NULL;
	uint8_t *bitmap = NULL;
	uint8_t *wide_bitmap = NULL;
	uint32_t narrow_count;
	uint32_t wide_count;
	size_t bitmap_size;
	size_t wide_bitmap_size;
	size_t wide_row_stride;
	size_t wide_glyph_stride;
	const struct st_console_font *emoji;
	size_t mapping_count = 0;
	size_t cluster_count;
	uint32_t narrow_added;

	if (result == NULL || base == NULL || base->bitmap == NULL
	    || base->glyph_count > UINT32_MAX - 2
	    || base->wide_glyph_count == UINT32_MAX)
		return IO_ERROR_INVALID_ARGUMENT;
	emoji = prefer_emoji ? emoji_font(base) : NULL;
	uint64_t total_narrow = (uint64_t)base->glyph_count + 2
	    + (emoji == NULL ? 0 : emoji->glyph_count);
	uint64_t total_wide = (uint64_t)base->wide_glyph_count + 1
	    + (emoji == NULL ? 0 : emoji->wide_glyph_count);
	if (total_narrow + total_wide > UINT32_MAX)
		return IO_ERROR_INVALID_ARGUMENT;
	narrow_count = (uint32_t)total_narrow;
	wide_count = (uint32_t)total_wide;
	narrow_added = narrow_count - base->glyph_count;
	size_t emoji_mappings = emoji == NULL ? 0 : emoji->mapping_count;
	size_t emoji_clusters = emoji == NULL ? 0 : emoji->cluster_mapping_count;
	if (base->mapping_count > SIZE_MAX - emoji_mappings
	    || base->cluster_mapping_count > SIZE_MAX - emoji_clusters)
		return IO_ERROR_OUT_OF_MEMORY;
	cluster_count = base->cluster_mapping_count + emoji_clusters;
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
	if (emoji != NULL) {
		for (uint32_t i = 0; i < emoji->glyph_count; ++i)
			for (uint32_t y = 0; y < base->height; ++y)
				bitmap[(size_t)(base->glyph_count + 2 + i) * base->glyph_stride
				    + (size_t)y * base->row_stride]
				    = emoji->bitmap[(size_t)i * emoji->glyph_stride + y];
		for (uint32_t i = 0; i < emoji->wide_glyph_count; ++i)
			for (uint32_t y = 0; y < base->height; ++y)
				memcpy(wide_bitmap + (size_t)(base->wide_glyph_count + 1 + i)
				    * wide_glyph_stride + (size_t)y * wide_row_stride,
				    emoji->wide_bitmap + (size_t)i * emoji->wide_glyph_stride
				    + (size_t)y * emoji->wide_row_stride, 2);
	}
	size_t mapping_capacity = base->mapping_count + emoji_mappings;
	if (mapping_capacity != 0) {
		if (mapping_capacity > SIZE_MAX / sizeof(mapping[0]))
			goto out_of_memory;
		mapping = malloc(mapping_capacity * sizeof(mapping[0]));
		if (mapping == NULL)
			goto out_of_memory;
		/* Merge sorted maps. A selected BBS font keeps its native glyphs;
		 * the Unicode font of last resort prefers the Noto emoji artwork. */
		size_t i = 0, j = 0;
		while (i < base->mapping_count || j < emoji_mappings) {
			bool from_base = j == emoji_mappings || (i < base->mapping_count
			    && base->mapping[i].codepoint < emoji->mapping[j].codepoint);
			bool equal = i < base->mapping_count && j < emoji_mappings
			    && base->mapping[i].codepoint == emoji->mapping[j].codepoint;
			if (equal) from_base = !prefer_emoji;
			struct st_console_font_mapping entry = from_base
			    ? base->mapping[i] : emoji->mapping[j];
			if (from_base) {
				if (entry.glyph >= base->glyph_count)
					entry.glyph += narrow_added;
			}
			else
				entry.glyph = emoji_glyph(emoji, base, narrow_count, entry.glyph);
			mapping[mapping_count++] = entry;
			if (from_base || equal) ++i;
			if (!from_base || equal) ++j;
		}
	}
	if (cluster_count != 0) {
		if (cluster_count > SIZE_MAX / sizeof(clusters[0]))
			goto out_of_memory;
		clusters = malloc(cluster_count * sizeof(clusters[0]));
		if (clusters == NULL)
			goto out_of_memory;
		size_t used = 0;
		/* Exact cluster lookup uses the first match. Match scalar-map
		 * precedence while borrowing the base's existing text strings. */
		for (size_t pass = 0; pass < 2; ++pass) {
			bool from_emoji = (pass == 0) == prefer_emoji;
			const struct st_console_font *source = from_emoji ? emoji : base;
			if (source == NULL) continue;
			for (size_t i = 0; i < source->cluster_mapping_count; ++i) {
				struct st_console_font_cluster_mapping entry = source->cluster_mapping[i];
				if (from_emoji)
					entry.glyph = emoji_glyph(emoji, base, narrow_count, entry.glyph);
				else if (entry.glyph >= base->glyph_count)
					entry.glyph += narrow_added;
				clusters[used++] = entry;
			}
		}
	}
	size_t emoji_colors = emoji == NULL ? 0 : emoji->color_glyph_count;
	if (base->color_glyph_count > SIZE_MAX - emoji_colors)
		goto out_of_memory;
	size_t color_count = base->color_glyph_count + emoji_colors;
	if (color_count != 0) {
		if (color_count > SIZE_MAX / sizeof(colors[0]))
			goto out_of_memory;
		colors = malloc(color_count * sizeof(colors[0]));
		if (colors == NULL)
			goto out_of_memory;
		for (size_t i = 0; i < base->color_glyph_count; ++i) {
			colors[i] = base->color_glyphs[i];
			if (colors[i].glyph >= base->glyph_count)
				colors[i].glyph += narrow_added;
		}
		for (size_t i = 0; i < emoji_colors; ++i) {
			struct st_console_color_glyph entry = emoji->color_glyphs[i];
			entry.glyph = emoji_glyph(emoji, base, narrow_count, entry.glyph);
			colors[base->color_glyph_count + i] = entry;
		}
		qsort(colors, color_count, sizeof(colors[0]), compare_color);
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
	created.font.mapping_count = mapping_count;
	created.font.color_glyphs = colors;
	created.font.color_glyph_count = color_count;
	created.font.cluster_mapping = clusters;
	created.font.cluster_mapping_count = cluster_count;
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
	free(colors);
	return IO_ERROR_OUT_OF_MEMORY;
}

int
st_presentation_font_create(struct st_presentation_font *result,
	const struct st_console_font *base)
{
	return create_presentation(result, base, false);
}

int
st_presentation_font_create_fallback(struct st_presentation_font *result,
	const struct st_console_font *base)
{
	return create_presentation(result, base, true);
}
