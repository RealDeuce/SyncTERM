/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>
#include <string.h>

#include "filesystem.h"
#include "st_ddofx_graphics_internal.h"
#include "st_ddofx_resource.h"

struct pcx_image {
	unsigned width, height;
	uint32_t palette[256];
	uint8_t  pixels[];
};

static unsigned
read_word(const uint8_t *p)
{
	return p[0] | (unsigned)p[1] << 8;
}

static void
write_word(uint8_t *p, unsigned value)
{
	p[0] = (uint8_t)value;
	p[1] = (uint8_t)(value >> 8);
}

static bool
read_row(FILE *file, uint32_t *remaining, uint8_t *row, unsigned width, unsigned stride)
{
	for (unsigned x = 0; x < stride;) {
		if (!*remaining)
			return false;
		--*remaining;
		int      index = fgetc(file);
		unsigned count = 1;
		if (index >= 0xc0) {
			count = (unsigned)index & 63;
			if (!count || count > stride - x || !*remaining)
				return false;
			--*remaining;
			index = fgetc(file);
		}
		if (index == EOF)
			return false;
		if (x < width)
			memset(row + x, index, count > width - x ? width - x : count);
		x += count;
	}
	return true;
}

static struct pcx_image *
read_image(FILE *file, uint32_t length, unsigned max_width, unsigned max_height)
{
	uint8_t header[128], tail[769];
	int64_t start = fs_stream_position(file);
	if (length < sizeof(header) + sizeof(tail) || start < 0 || start > INT64_MAX - length
	    || fread(header, 1, sizeof(header), file) != sizeof(header)
	    || memcmp(header, "\x0a\x05\x01\x08", 4) != 0 || header[65] != 1)
		return NULL;
	unsigned x = read_word(header + 4), y = read_word(header + 6);
	unsigned xmax = read_word(header + 8), ymax = read_word(header + 10);
	unsigned stride = read_word(header + 66);
	if (xmax < x || ymax < y)
		return NULL;
	unsigned width = xmax - x + 1, height = ymax - y + 1;
	if (width > max_width || height > max_height || stride < width)
		return NULL;
	struct pcx_image *image = malloc(sizeof(*image) + width * height);
	if (!image)
		return NULL;
	image->width       = width;
	image->height      = height;
	uint32_t remaining = length - sizeof(header) - sizeof(tail);
	for (unsigned row = 0; row < height; ++row) {
		if (!read_row(file, &remaining, image->pixels + row * width, width, stride)) {
			free(image);
			return NULL;
		}
	}
	if (fs_stream_seek(file, start + length - (int64_t)sizeof(tail)) != 0
	    || fread(tail, 1, sizeof(tail), file) != sizeof(tail) || tail[0] != 12) {
		free(image);
		return NULL;
	}
	for (unsigned i = 0; i < 256; ++i) {
		uint32_t rgb = 0;
		for (unsigned c = 0; c < 3; ++c) {
			unsigned v = tail[1 + i * 3 + c] >> 2;
			rgb        = (rgb << 8) | (v << 2) | (v >> 4);
		}
		image->palette[i] = rgb;
	}
	return image;
}

struct visual_load {
	struct st_ddofx_graphics *graphics;
	const struct pcx_image   *image;
};

static int
load_visual(void *context, const struct st_pixel_buffer *pixels, const uint32_t *palette,
    struct st_console_rect *changed)
{
	const struct visual_load *load  = context;
	const struct pcx_image   *image = load->image;
	if (pixels->width != WIDTH || pixels->height != HEIGHT)
		return -1;
	for (unsigned y = 0; y < HEIGHT; ++y) {
		for (unsigned x = 0; x < WIDTH; ++x) {
			size_t  pos = y * WIDTH + x;
			uint8_t index;
			if (y < image->height && x < image->width) {
				index                = image->pixels[y * image->width + x];
				pixels->primary[pos] = pixels->alternate[pos] = index;
			}
			else
				index = st_console_palette_index256(palette, pixels->primary[pos]);
			load->graphics->page[HIDDEN][y][x] = index;
		}
	}
	*changed = (struct st_console_rect) {0, 0, image->width < WIDTH ? image->width : WIDTH,
	    image->height < HEIGHT ? image->height : HEIGHT};
	return 0;
}

static void
load_page(struct st_ddofx *ddo, enum page dest, FILE *file, uint32_t length, int x, int y, bool snapshot)
{
	struct st_ddofx_graphics *g = ddo->graphics;
	struct st_ddofx_graphic_window *w = &g->graphic_windows[g->alert_graphic_window];
	bool window = dest == VISUAL && w->visual;
	unsigned width = window ? (unsigned)w->width : WIDTH, height = window ? (unsigned)w->height : HEIGHT;
	if ((unsigned)x >= width || (unsigned)y >= height)
		return;
	struct pcx_image *image = read_image(file, length, window || snapshot ? 10000 : WIDTH,
	    window || snapshot ? 10000 : HEIGHT);
	if (!image)
		return;

	bool batch = dest == VISUAL && st_console_begin_update(g->display) == 0;
	if (dest == VISUAL && !window) {
		struct visual_load load = {g, image};
		if (st_console_modify_pixels_with_palette(g->display, load_visual, &load) != 0)
			goto done;
	}
	else {
		uint8_t *pixels = window ? w->visual : (uint8_t *)g->page[dest];
		unsigned columns = image->width < width - x ? image->width : width - x;
		unsigned rows = image->height < height - y ? image->height : height - y;
		for (unsigned row = 0; row < rows; ++row)
			memcpy(pixels + (y + row) * width + x, image->pixels + row * image->width, columns);
		if (window)
			memcpy(w->hidden_page, w->visual, (size_t)width * height);
	}
	memcpy(g->palette, image->palette, sizeof(g->palette));
	st_ddofx_set_palette(g, 0, 256);
	g->cx = g->cy = 0;
	if (window)
		st_ddofx_refresh_graphic_interior(ddo);
	if (dest == VISUAL) {
		st_ddofx_repaint_text_windows(ddo, window ? g->alert_graphic_window : 0);
		if (!window)
			st_ddofx_redraw_graphic_windows(ddo);
	}

done:
	if (batch)
		(void)st_console_end_update(g->display);
	free(image);
}

static FILE *
open_pcx(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, const char *member, uint32_t *length)
{
	if (!ddo->resource_prefix[0] || !sink->open_asset)
		return NULL;
	char container[sizeof(ddo->resource_prefix) + 1];
	snprintf(container, sizeof(container), "%sS", ddo->resource_prefix);
	FILE *file = sink->open_asset(sink->context, "BBSTHEME", NULL, container);
	if (!file)
		return NULL;
	uint16_t type;
	if (st_ddofx_resource_find(file, member, -1, length, &type) >= 0 && (type & 255) == 1)
		return file;
	fclose(file);
	return NULL;
}

void
st_ddofx_resource_pcx(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, enum page dest, const char *member,
    int x, int y)
{
	uint32_t length;
	FILE *file = open_pcx(ddo, sink, member, &length);
	if (file) {
		load_page(ddo, dest, file, length, x, y, false);
		fclose(file);
	}
}

static bool
write_page(FILE *file, const uint8_t *data, const uint32_t *palette, unsigned width, unsigned height)
{
	uint8_t header[128] = {10, 5, 1, 8};
	write_word(header + 8, width - 1);
	write_word(header + 10, height - 1);
	write_word(header + 12, 128);
	write_word(header + 14, 96);
	header[65] = 1;
	unsigned stride = (width + 1) & ~1u;
	write_word(header + 66, stride);
	header[68] = 1;
	write_word(header + 70, width);
	write_word(header + 72, height);
	if (fwrite(header, 1, sizeof(header), file) != sizeof(header))
		return false;
	uint8_t *row = malloc(2 * stride);
	if (!row)
		return false;
	for (unsigned y = 0; y < height; ++y, data += width) {
		size_t  used = 0;
		for (unsigned x = 0; x < width;) {
			uint8_t  index = data[x];
			unsigned count = 1;
			while (count < 63 && x + count < width && data[x + count] == index)
				++count;
			if (count > 1 || index >= 0xc0)
				row[used++] = (uint8_t)(0xc0 | count);
			row[used++] = index;
			x += count;
		}
		if (width < stride)
			row[used++] = 0;
		if (fwrite(row, 1, used, file) != used) {
			free(row);
			return false;
		}
	}
	free(row);
	uint8_t tail[769] = {12};
	for (unsigned i = 0; i < 256; ++i) {
		for (unsigned c = 0; c < 3; ++c) {
			unsigned dac        = (palette[i] >> (18 - c * 8)) & 63;
			tail[1 + 3 * i + c] = (uint8_t)(dac * 255 / 63);
		}
	}
	return fwrite(tail, 1, sizeof(tail), file) == sizeof(tail) && fflush(file) == 0;
}

static int
capture_visual(void *context, const struct st_pixel_buffer *pixels, const uint32_t *palette,
    struct st_console_rect *changed)
{
	uint8_t *data = context;
	(void)changed;
	if (pixels->width != WIDTH || pixels->height != HEIGHT)
		return -1;
	for (unsigned i = 0; i < WIDTH * HEIGHT; ++i)
		data[i] = st_console_palette_index256(palette, pixels->primary[i]);
	return 0;
}

void
st_ddofx_page_file(struct st_ddofx *ddo, enum page file_page, enum page target, bool save)
{
	struct st_ddofx_graphics *g    = ddo->graphics;
	FILE                    **slot = &ddo->page_files[file_page == VISUAL ? 2 : file_page];
	if (!save) {
		if (*slot && fs_stream_seek(*slot, 0) == 0) {
			int64_t length = fs_stream_size(*slot);
			if (length > 0 && length <= UINT32_MAX)
				load_page(ddo, target, *slot, (uint32_t)length, 0, 0, true);
		}
		return;
	}

	uint8_t       *visible = NULL;
	const uint8_t *data;
	struct st_ddofx_graphic_window *w = &g->graphic_windows[g->alert_graphic_window];
	unsigned width = WIDTH, height = HEIGHT;
	if (target == VISUAL && w->visual) {
		data = w->visual;
		width = w->width;
		height = w->height;
	}
	else if (target == VISUAL) {
		visible = malloc(WIDTH * HEIGHT);
		if (!visible || st_console_modify_pixels_with_palette(g->display, capture_visual, visible) != 0) {
			free(visible);
			return;
		}
		data = visible;
	}
	else
		data = (const uint8_t *)&g->page[target];

	FILE *file = tmpfile();
	if (file && write_page(file, data, g->palette, width, height)) {
		if (*slot)
			fclose(*slot);
		*slot = file;
	}
	else if (file)
		fclose(file);
	free(visible);
}
