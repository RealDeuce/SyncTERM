/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "st_ddofx_graphics_internal.h"
#include "st_ddofx_resource.h"
#include "st_ddofx_script.h"

struct glyph {
	uint8_t  width, height;
	uint32_t offset;
};

struct st_ddofx_bmf {
	struct glyph glyphs[256];
	unsigned     descent;
	uint8_t      rows[];
};

static struct st_ddofx_bmf *
read_font(FILE *file, uint32_t length)
{
	uint8_t header[2];
	if (fread(header, 1, sizeof(header), file) != sizeof(header))
		return NULL;
	unsigned banks     = header[0] & 128 ? 4 : 1;
	unsigned slots     = banks == 4 ? 64 : 96;
	unsigned columns   = header[0] & 127;
	unsigned stride    = columns * header[1];
	unsigned bank_size = slots * (stride + 2);
	if (!stride || length != 2 + banks * bank_size)
		return NULL;
	struct st_ddofx_bmf *font = malloc(sizeof(*font) + length - 2);
	if (!font)
		return NULL;
	if (fread(font->rows, 1, length - 2, file) != length - 2) {
		free(font);
		return NULL;
	}
	memset(font->glyphs, 0, sizeof(font->glyphs));
	unsigned used = 0;
	for (unsigned b = 0; b < banks; ++b) {
		uint8_t *bank = font->rows + b * bank_size;
		unsigned base = banks == 1 ? 32 : b * slots;
		/* Copy the metrics before packing the active rows over slot padding. */
		for (unsigned i = 0; i < slots; ++i) {
			struct glyph *g = &font->glyphs[base + i];
			g->width        = bank[i];
			g->height       = bank[slots + i];
			if ((g->width + 7u) / 8 > columns || g->height > header[1]) {
				free(font);
				return NULL;
			}
		}
		for (unsigned i = 0; i < slots; ++i) {
			struct glyph *g    = &font->glyphs[base + i];
			unsigned      size = ((g->width + 7u) / 8) * g->height;
			g->offset          = used;
			memmove(font->rows + used, bank + slots * 2 + i * stride, size);
			used += size;
		}
	}
	const struct glyph *a         = &font->glyphs['A'];
	unsigned            row_bytes = (a->width + 7u) / 8;
	font->descent                 = 0;
	while (row_bytes && font->descent < a->height) {
		const uint8_t *row = font->rows + a->offset + (a->height - font->descent - 1) * row_bytes;
		unsigned       i   = 0;
		while (i < row_bytes && row[i] == 0)
			++i;
		if (i != row_bytes)
			break;
		++font->descent;
	}
	struct st_ddofx_bmf *packed = realloc(font, sizeof(*font) + used);
	return packed ? packed : font;
}

static void
load_font(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, char *args)
{
	const char *name = st_ddofx_field(&args);
	int32_t     slot;
	if (!name || !args || !st_ddofx_number(ddo, args, &slot) || slot < 1 || slot > 32)
		return;
	struct st_ddofx_graphics *g = ddo->graphics;
	free(g->bitmap_fonts[slot - 1]);
	g->bitmap_fonts[slot - 1] = NULL;
	if (!ddo->resource_prefix[0] || !sink->open_asset)
		return;
	char container[sizeof(ddo->resource_prefix) + 1];
	snprintf(container, sizeof(container), "%sM", ddo->resource_prefix);
	FILE *file = sink->open_asset(sink->context, "BBSTHEME", NULL, container);
	if (!file)
		return;
	uint32_t length;
	if (st_ddofx_resource_find(file, name, -1, &length, NULL) >= 0 && length >= 2) {
		g->bitmap_fonts[slot - 1] = read_font(file, length);
		if (g->bitmap_fonts[slot - 1])
			g->bitmap_slot = (unsigned)(slot - 1);
	}
	fclose(file);
}

enum {
	MAX_TEXT = 100
};

struct bitmap_run {
	const struct glyph *glyph;
	unsigned            colour, underline;
};

struct text_image {
	struct st_ddofx_graphics *graphics;
	enum page                 page;
	int                       x, y, width, height;
	double                    cosine, sine;
	struct bounds             bounds;
	/* Zero represents transparency. Palette indices are stored plus one. */
	uint16_t colours[];
};

static bool
scale_run(struct text_image *image, const struct st_ddofx_bmf *font, const uint8_t *text, unsigned colour)
{
	struct bitmap_run runs[MAX_TEXT];
	unsigned          count = 0, width = 0, height = 0;
	int               underline_start = -1;
	for (; *text; ++text) {
		if (*text == 127) {
			if (!text[1])
				break;
			if (*++text != '_')
				colour = *text;
			else if (underline_start < 0)
				underline_start = (int)count;
			else {
				for (unsigned i = (unsigned)underline_start; i < count; ++i)
					runs[i].underline = colour + 1;
				underline_start = -1;
			}
			continue;
		}
		const struct glyph *g = &font->glyphs[*text];
		if (!g->width)
			continue;
		runs[count++] = (struct bitmap_run) {.glyph = g, .colour = colour};
		width += g->width;
		if (height < g->height)
			height = g->height;
	}
	if (!width || !height)
		return false;
	/* BMF rows are top-first and tightly packed within each glyph. The
	 * natural line has a leading column, a bottom row and shared descent. */
	unsigned natural_height = height + font->descent + 1;
	unsigned index = 0, left = 1;
	for (int x = 0; x < image->width; ++x) {
		unsigned sx = (unsigned)x * (width + 1) / (unsigned)image->width;
		if (!sx)
			continue;
		while (sx >= left + runs[index].glyph->width) {
			left += runs[index].glyph->width;
			++index;
		}
		const struct glyph *g   = runs[index].glyph;
		unsigned            top = height + font->descent - g->height;
		for (int y = 0; y < image->height; ++y) {
			unsigned sy = (unsigned)y * natural_height / (unsigned)image->height;
			if (runs[index].underline && sy == height + 1) {
				image->colours[y * image->width + x] = (uint16_t)runs[index].underline;
				continue;
			}
			if (sy < top || sy >= top + g->height)
				continue;
			const uint8_t *row = font->rows + g->offset + (sy - top) * ((g->width + 7u) / 8);
			if (row[(sx - left) / 8] & (128u >> ((sx - left) % 8)))
				image->colours[y * image->width + x] = (uint16_t)(runs[index].colour + 1);
		}
	}
	return true;
}

static void
outline(struct text_image *image, unsigned colour)
{
	/* Mark neighboring background first so a new outline pixel cannot seed
	 * another one. Outlining occurs in destination pixels before rotation. */
	for (int y = 0; y < image->height; ++y) {
		for (int x = 0; x < image->width; ++x) {
			if (image->colours[y * image->width + x])
				continue;
			for (int dy = -1; dy <= 1; ++dy) {
				for (int dx = -1; dx <= 1; ++dx) {
					int sx = x + dx, sy = y + dy;
					if (sx < 0 || sy < 0 || sx >= image->width || sy >= image->height)
						continue;
					uint16_t adjacent = image->colours[sy * image->width + sx];
					if (adjacent && adjacent != UINT16_MAX)
						image->colours[y * image->width + x] = UINT16_MAX;
				}
			}
		}
	}
	for (int i = 0; i < image->width * image->height; ++i)
		if (image->colours[i] == UINT16_MAX)
			image->colours[i] = (uint16_t)(colour + 1);
}

static int
draw_image(void *context, const struct st_pixel_buffer *pixels, struct st_console_rect *changed)
{
	const struct text_image *image = context;
	if (pixels && (pixels->width != WIDTH || pixels->height != HEIGHT))
		return -1;
	double cx = image->x + (image->width - 1) * 0.5, cy = image->y + (image->height - 1) * 0.5;
	for (int y = image->bounds.top; y <= image->bounds.bottom; ++y) {
		for (int x = image->bounds.left; x <= image->bounds.right; ++x) {
			double dx = x - cx, dy = y - cy;
			int    sx = (int)floor(dx * image->cosine + dy * image->sine + image->width * 0.5);
			int    sy = (int)floor(dy * image->cosine - dx * image->sine + image->height * 0.5);
			if (sx < 0 || sy < 0 || sx >= image->width || sy >= image->height)
				continue;
			uint16_t ink = image->colours[sy * image->width + sx];
			if (!ink)
				continue;
			if (pixels)
				pixels->alternate[y * WIDTH + x] = pixels->primary[y * WIDTH + x] = ink - 1u;
			else
				image->graphics->page[image->page][y][x] = (uint8_t)(ink - 1);
		}
	}
	if (changed)
		*changed = (struct st_console_rect) {image->bounds.left, image->bounds.top,
		    image->bounds.right - image->bounds.left + 1, image->bounds.bottom - image->bounds.top + 1};
	return 0;
}

static void
print_text(struct st_ddofx *ddo, char *args)
{
	struct st_ddofx_graphics  *g    = ddo->graphics;
	const struct st_ddofx_bmf *font = g->bitmap_fonts[g->bitmap_slot];
	if (!font)
		return;
	int32_t n[7];
	for (unsigned i = 0; i < 7; ++i)
		if (!st_ddofx_field_number(ddo, &args, &n[i]))
			return;
	const char *name = st_ddofx_field(&args);
	enum page   page;
	if (!name || !args)
		return;
	name                = st_ddofx_value(ddo, name);
	const uint8_t *text = (const uint8_t *)st_ddofx_value(ddo, args);
	if (strlen(name) != 2 || !st_ddofx_page_name(name, &page) || !*text
	    || strlen((const char *)text) > MAX_TEXT || n[0] < 0 || n[1] <= n[0] || n[1] >= WIDTH || n[2] < 0
	    || n[3] <= n[2] || n[3] >= HEIGHT || (unsigned)n[4] > 254 || (unsigned)n[5] > 255
	    || (unsigned)n[6] > 359)
		return;
	int w = n[1] - n[0] + 1, h = n[3] - n[2] + 1;
	if (h + font->descent > 79)
		return;
	struct text_image *image = calloc(1, sizeof(*image) + (size_t)w * h * sizeof(*image->colours));
	if (!image)
		return;
	image->graphics = g;
	image->page     = page;
	image->x        = n[0];
	image->y        = n[2];
	image->width    = w;
	image->height   = h;
	if (scale_run(image, font, text, (unsigned)n[4])) {
		if (n[5])
			outline(image, (unsigned)n[5]);
		st_ddofx_rotation(n[6], &image->cosine, &image->sine);
		double cx = n[0] + (w - 1) * 0.5, cy = n[2] + (h - 1) * 0.5;
		double rx     = (fabs(image->cosine) * w + fabs(image->sine) * h) * 0.5;
		double ry     = (fabs(image->sine) * w + fabs(image->cosine) * h) * 0.5;
		image->bounds = (struct bounds) {(int)fmax(0, ceil(cx - rx)), (int)fmin(WIDTH - 1, floor(cx + rx)),
		    (int)fmax(0, ceil(cy - ry)), (int)fmin(HEIGHT - 1, floor(cy + ry))};
		if (page == VISUAL)
			(void)st_console_modify_pixels(g->display, draw_image, image);
		else
			(void)draw_image(image, NULL, NULL);
	}
	free(image);
}

void
st_ddofx_bitmap_text(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, const char *verb, char *args)
{
	if (!strcmp(verb, "2F"))
		load_font(ddo, sink, args);
	else if (!strcmp(verb, "2X"))
		print_text(ddo, args);
	else {
		int32_t slot;
		if (args && st_ddofx_number(ddo, args, &slot) && slot >= 1 && slot <= 32
		    && ddo->graphics->bitmap_fonts[slot - 1])
			ddo->graphics->bitmap_slot = (unsigned)(slot - 1);
	}
}
