/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "st_codepage.h"
#include "st_rip3_bitmap_error_ui.h"
#include "st_rip3_bitmap_stamp.h"
#include "st_rip3_bitmap_text.h"
#include "st_rip3_classic_text.h"
#include "st_rip3_palettes.h"
#include "st_rip3_ports.h"
#include "st_utf8.h"

static bool
filename_text(uint8_t text[255], size_t *length, const char *name)
{
	size_t remaining = strlen(name), count = 0;
	while (remaining != 0) {
		uint32_t scalar;
		size_t   used;
		if (st_utf8_decode_one(&scalar, &used, name, remaining) != 0)
			return false;
		uint8_t code = '?';
		if (scalar >= 32 && scalar != 127)
			(void)st_codepage_from_unicode(&code, ST_CODEPAGE_CP437, scalar);
		if (count < 255)
			text[count++] = code;
		name += used;
		remaining -= used;
	}
	*length = count;
	return true;
}

static int
native_font(struct st_rip3_classic_font *font, const struct st_rip3_ports *ports)
{
	struct st_rip3_port_info screen;
	int                      status = st_rip3_ports_info(ports, 0, &screen);
	if (status != 0)
		return status;
	enum st_rip3_canvas canvas = screen.bounds.x1 == 639   ? ST_RIP3_CANVAS_640_480
	                             : screen.bounds.x1 == 799 ? ST_RIP3_CANVAS_800_600
	                                                       : ST_RIP3_CANVAS_1024_768;
	return st_rip3_classic_font_builtin(font, canvas, 0);
}

static uint8_t
contrasting(const struct st_rip3_palette *palette, uint8_t background)
{
	uint8_t  result = background;
	uint32_t best = 0, base = palette->rgb[background];
	for (unsigned i = 0; i < 256; i++) {
		int32_t  r        = (int32_t)((base >> 16) & 255) - (int32_t)((palette->rgb[i] >> 16) & 255);
		int32_t  g        = (int32_t)((base >> 8) & 255) - (int32_t)((palette->rgb[i] >> 8) & 255);
		int32_t  b        = (int32_t)(base & 255) - (int32_t)(palette->rgb[i] & 255);
		uint32_t distance = (uint32_t)(r * r + g * g + b * b);
		if (distance > best) {
			best   = distance;
			result = (uint8_t)i;
		}
	}
	return result;
}

static bool
visible_area(const struct st_rip3_bitmap_destination *area, const struct st_rip3_rect *clip,
    struct st_rip3_rect *visible)
{
	int64_t x0 = area->x0 > clip->x0 ? area->x0 : clip->x0;
	int64_t y0 = area->y0 > clip->y0 ? area->y0 : clip->y0;
	int64_t x1 = area->x1 < clip->x1 ? area->x1 : clip->x1;
	int64_t y1 = area->y1 < clip->y1 ? area->y1 : clip->y1;
	if (x0 > x1 || y0 > y1)
		return false;
	*visible = (struct st_rip3_rect) {(int32_t)x0, (int32_t)y0, (int32_t)x1, (int32_t)y1};
	return true;
}

void
st_rip3_bitmap_error_show(void *context, const char *name, unsigned port,
    const struct st_rip3_bitmap_destination *requested, bool natural_size)
{
	static const uint8_t                   title[] = "Cannot load bitmap";
	struct st_rip3_bitmap_error_ui        *ui      = context;
	struct st_rip3_port_info               info;
	struct st_rip3_palette                 palette;
	struct st_rip3_classic_font            builtin;
	struct st_rip3_bitmap_text_metrics     metrics;
	const struct st_rip3_bitmap_text_style style = {.magnification = 1};
	const struct st_rip3_maf_font         *font;
	struct st_rip3_bitmap_destination      area;
	uint8_t                                filename[255], background;
	size_t                                 length;
	unsigned                               current;
	const int64_t                          limit = INT64_C(1024) * INT32_MAX;

	if (ui == NULL || ui->ports == NULL)
		return;
	if (st_rip3_ports_current(ui->ports, &current) != 0 || current != port
	    || st_rip3_ports_info(ui->ports, port, &info) != 0 || !info.viewport_active)
		return;
	if (ui->palettes == NULL || name == NULL || !filename_text(filename, &length, name))
		return;
	font = ui->font;
	if (font == NULL) {
		if (native_font(&builtin, ui->ports) != 0)
			return;
		font = &builtin.data.bitmap;
	}
	if (st_rip3_bitmap_text_measure(font, &style, 1, &metrics) != 0)
		return;
	area        = requested != NULL ? *requested : (struct st_rip3_bitmap_destination) {0};
	bool framed = requested != NULL && !natural_size;
	if (area.x0 < -limit || area.x0 > limit || area.y0 < -limit || area.y0 > limit)
		return;
	if (framed) {
		if (area.x1 < area.x0 || area.y1 < area.y0 || area.x1 > limit || area.y1 > limit)
			return;
		area.x0++;
		area.y0++;
	}
	size_t columns              = length < 18 ? 18 : length > 32 ? 32 : length;
	area.x1                     = area.x0 + (int64_t)columns * font->width + 5;
	area.y1                     = area.y0 + (int64_t)font->height * 2 + 6;
	struct st_rip3_rect visible = {0, 0, -1, -1};
	(void)visible_area(&area, &info.clip, &visible);
	int32_t x0 = visible.x0, y0 = visible.y0;
	size_t  width = (size_t)((int64_t)visible.x1 - x0 + 1), height = (size_t)((int64_t)visible.y1 - y0 + 1);
	if (width != 0 && height > SIZE_MAX / width)
		return;
	if (st_rip3_palettes_current_palette(ui->palettes, &palette) != 0
	    || st_rip3_palette_color(&palette, true, 0, &background) != 0)
		return;
	uint8_t  foreground = contrasting(&palette, background);
	uint8_t *pixels     = width == 0 ? NULL : malloc(width * height);
	if (width != 0 && pixels == NULL)
		return;
	int64_t box_width = area.x1 - area.x0 + 1, box_height = area.y1 - area.y0 + 1;
	for (size_t y = 0; y < height; y++)
		for (size_t x = 0; x < width; x++) {
			int64_t dx = (int64_t)x0 + (int64_t)x - area.x0, dy = (int64_t)y0 + (int64_t)y - area.y0;
			bool    ink = dx == 0 || dy == 0 || dx == box_width - 1 || dy == box_height - 1;
			if (!ink && dx >= 3 && dx < box_width - 3 && dy >= 3 && dy < box_height - 3) {
				const uint8_t *text  = NULL;
				size_t         count = 0, row = 0;
				if (dy < (int64_t)font->height + 3) {
					text  = title;
					count = sizeof(title) - 1;
					row   = (size_t)(dy - 3);
				}
				else if (dy >= (int64_t)font->height + 4 && dy < (int64_t)font->height * 2 + 4) {
					text  = filename;
					count = length;
					row   = (size_t)(dy - font->height - 4);
				}
				uint64_t column = (uint64_t)(dx - 3) / font->width;
				if (column < count) {
					const uint8_t *glyph = st_rip3_maf_glyph(font, text[column]);
					unsigned       bit   = (unsigned)((uint64_t)(dx - 3) % font->width);
					ink = glyph != NULL
					      && (glyph[row * font->row_bytes + bit / 8] & (0x80u >> (bit & 7)))
					             != 0;
				}
			}
			pixels[y * width + x] = ink ? foreground : background;
		}
	if (framed && visible_area(requested, &info.clip, &visible)) {
		(void)st_rip3_ports_fill(ui->ports, &visible, foreground, ST_RIP3_COPY);
		struct st_rip3_bitmap_destination interior = {requested->x0 + 1, requested->y0 + 1,
		    requested->x1 - 1, requested->y1 - 1};
		if (visible_area(&interior, &info.clip, &visible))
			(void)st_rip3_ports_fill(ui->ports, &visible, background, ST_RIP3_COPY);
	}
	for (size_t y = 0; y < height; y++)
		for (size_t x = 0; x < width; x++)
			(void)st_rip3_ports_plot(ui->ports, x0 + (int32_t)x, y0 + (int32_t)y,
			    pixels[y * width + x], ST_RIP3_COPY);
	free(pixels);
}
