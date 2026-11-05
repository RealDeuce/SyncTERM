/* SkyPix uses its own raster policy and native Amiga bitmap metrics. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_skypix.h"
#include "cterm.h"
#include "cterm_input.h"
#include "cterm_internal.h"
#include "cterm_skypix_walkers.h"
#include "cterm_text.h"
#include "io_error.h"
#include "st_amiga_fonts.h"
#include "st_time.h"
#include "st_utf8.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int palette(struct cterm_skypix *s, const unsigned *values)
{
	static const unsigned defaults[16] = {0,   287, 3549, 3840, 241, 943,  4082, 3086,
										  182, 221, 175,  123,	15,	 1807, 2829, 2824};
	uint32_t rgb[16];
	if (values == NULL)
		values = defaults;
	for (unsigned i = 0; i < 16; ++i)
		rgb[i] = ((values[i] >> 8 & 15) * 17 << 16) | ((values[i] >> 4 & 15) * 17 << 8) |
				 (values[i] & 15) * 17;
	return st_console_set_palette(s->terminal->screen.console, CTERM_SKYPIX_PALETTE_FIRST, rgb,
								  s->colours);
}
static int pixels(struct cterm_skypix *s, struct st_pixel_buffer **buffer)
{
	const struct st_console_rect rect = {0, 0, 640, 200};
	return st_console_get_pixels(buffer, s->terminal->screen.console, &rect, false);
}
static int publish(struct cterm_skypix *s, const struct st_pixel_buffer *buffer, int x, int y)
{
	const struct st_pixel_blit blit = {
		.width = buffer->width,
		.height = buffer->height,
		.destination_x = x,
		.destination_y = y,
		.scale_x = 1,
		.scale_y = 1,
	};
	/* SkyPix pixels do not blink. A missing alternate source tells the
	 * shared blitter to copy the primary image into both destination planes. */
	struct st_pixel_buffer image = *buffer;
	image.alternate = NULL;
	return st_console_blit_pixels(s->terminal->screen.console, &image, NULL, &blit);
}
static void point(struct st_pixel_buffer *b, int x, int y, unsigned colour)
{
	if (x >= 0 && x < (int)b->width && y >= 0 && y < (int)b->height)
		b->primary[(size_t)y * b->width + (unsigned)x] = CTERM_SKYPIX_PALETTE_FIRST + colour;
}
static void span(struct st_pixel_buffer *b, int x1, int x2, int y, unsigned c)
{
	if (y < 0 || y >= (int)b->height)
		return;
	if (x1 < 0)
		x1 = 0;
	if (x2 >= (int)b->width)
		x2 = (int)b->width - 1;
	for (int x = x1; x <= x2; ++x)
		point(b, x, y, c);
}
struct drawing {
	struct st_pixel_buffer *pixels;
	unsigned colour;
};
static void plot(void *context, int x, int y)
{
	struct drawing *drawing = context;
	point(drawing->pixels, x, y, drawing->colour);
}
static int flood(struct st_pixel_buffer *b, unsigned mode, int x, int y, unsigned fg, unsigned bg)
{
	if (x < 0 || x >= 640 || y < 0 || y >= 200 || mode > 1)
		return 0;
	fg += CTERM_SKYPIX_PALETTE_FIRST;
	bg += CTERM_SKYPIX_PALETTE_FIRST;
	uint32_t original = b->primary[y * 640 + x];
	if ((mode == 1 && original == fg) || (mode == 0 && original == bg))
		return 0;
	/* A visited plane is essential in boundary mode when the new colour
	 * already occurs inside the area. Each canvas point is enqueued once. */
	uint8_t *visited = calloc(640 * 200, 1);
	unsigned *queue = malloc(640 * 200 * sizeof(*queue));
	if (visited == NULL || queue == NULL) {
		free(visited);
		free(queue);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	size_t head = 0, tail = 0;
	queue[tail++] = (unsigned)(y * 640 + x);
	visited[y * 640 + x] = 1;
	while (head < tail) {
		unsigned p = queue[head++];
		b->primary[p] = fg;
		unsigned adjacent[4] = {p % 640 ? p - 1 : p, p % 640 < 639 ? p + 1 : p,
								p >= 640 ? p - 640 : p, p < 640 * 199 ? p + 640 : p};
		for (unsigned i = 0; i < 4; ++i) {
			unsigned q = adjacent[i];
			if (!visited[q] && (mode == 1 ? b->primary[q] == original : b->primary[q] != bg)) {
				visited[q] = 1;
				queue[tail++] = q;
			}
		}
	}
	free(queue);
	free(visited);
	return 0;
}
static void sync_cursor(struct cterm_skypix *s)
{
	if (!s->custom && !s->positioned) {
		s->x = (s->terminal->xpos - 1) * 8;
		s->y = (s->terminal->ypos - 1) * 8;
	}
}
int cterm_skypix_reset_font(struct cterm_skypix *s)
{
	sync_cursor(s);
	st_amiga_font_release(s->font);
	s->font = st_amiga_font_builtin("pearl.font", 8);
	s->custom = false;
	if ((s->x & 7) == 0 && (s->y & 7) == 0 && s->x >= 0 && s->x < 640 && s->y >= 0 && s->y < 200) {
		s->positioned = false;
		s->terminal->xpos = s->x / 8 + 1;
		s->terminal->ypos = s->y / 8 + 1;
	}
	s->terminal->cursor = CTERM_CURSOR_UNDERLINE;
	cterm_render_set_cursor(s->terminal, s->terminal->cursor);
	return 0;
}
int cterm_skypix_reset_style(struct cterm_skypix *s)
{
	int status = cterm_skypix_reset_font(s);
	return status != 0 ? status : palette(s, NULL);
}
static void warn_font(struct cterm_skypix *s, const char *name, unsigned height, int error)
{
	char key[280];
	int n = snprintf(key, sizeof(key), "%s/%u;", name, height);
	if (n < 0 || (size_t)n >= sizeof(key) || strstr(s->warned, key) != NULL)
		return;
	size_t used = strlen(s->warned);
	if ((size_t)n >= sizeof(s->warned) - used)
		return;
	cterm_log(s->terminal, ST_CTERM_LOG_WARNING,
			  "SkyPix font %s/%u unavailable (%d); using compiled fallback", name, height, error);
	memcpy(s->warned + used, key, (size_t)n + 1);
}
static int set_font(struct cterm_skypix *s, unsigned height, const uint8_t *bytes, size_t size)
{
	if (height == 0)
		return cterm_skypix_reset_font(s);
	char name[256];
	/* Wire font names are Latin-1. Paths are not accepted as family names. */
	size_t used = 0;
	for (size_t i = 0; i < size; ++i) {
		if (bytes[i] < 0x20 || bytes[i] == '/' || bytes[i] == '\\' || bytes[i] == ':' ||
			bytes[i] == 0x7f)
			return 0;
		char encoded[4];
		ptrdiff_t n = st_utf8_encode_one(encoded, sizeof(encoded), bytes[i]);
		if (n < 0 || (size_t)n >= sizeof(name) - used)
			return 0;
		memcpy(name + used, encoded, (size_t)n);
		used += (size_t)n;
	}
	name[used] = 0;
	const st_amiga_font_t *font = NULL;
	int status = st_amiga_fonts_lookup(&font, s->font_directory, name, height);
	if (font == NULL)
		return status;
	if (status != 0)
		warn_font(s, name, height, status);
	sync_cursor(s);
	st_amiga_font_release(s->font);
	s->font = font;
	s->custom = true;
	s->positioned = true;
	s->terminal->cursor = s->custom ? CTERM_CURSOR_HIDDEN : CTERM_CURSOR_UNDERLINE;
	cterm_render_set_cursor(s->terminal, s->terminal->cursor);
	return 0;
}
static void text_boundary(struct cterm_skypix *s)
{
	free(s->pending);
	s->pending = NULL;
	free(s->undo_pending);
	s->undo_pending = NULL;
	s->pending_rect = (struct st_console_rect){0};
}
static int command(void *context, const unsigned *p, size_t count, const uint8_t *text, size_t size)
{
	struct cterm_skypix *s = context;
	struct cterminal *t = s->terminal;
	int status = cterm_text_boundary(t);
	struct st_pixel_buffer *b = NULL;
	if (status != 0)
		return status;
	text_boundary(s);
	unsigned fg = (t->attr & 15) & (s->colours - 1), bg = (t->attr >> 4 & 15) & (s->colours - 1);
	unsigned op = p[0];
	int v[17];
	for (size_t i = 0; i < count; ++i)
		v[i] = cterm_skypix_word(p[i]);
	if (op == 0 || op == 9 || op == 21 || op == 22)
		return 0;
	if (op == 10 && count == 2)
		return set_font(s, p[1], text, size);
	if (op == 11 && count == 17) {
		for (unsigned i = 1; i < 17; ++i)
			if (p[i] > 4095)
				return 0;
		return palette(s, p + 1);
	}
	if ((op == 12 && count == 1) || (op == 17 && count == 2)) {
		cterm_set_fgattr(t, 2);
		cterm_set_bgattr(t, 0);
		return palette(s, NULL);
	}
	if (op == 14 && count == 2) {
		if (p[1] <= 2)
			return 0;
		uint64_t now;
		status = st_time_monotonic_ms(&now);
		if (status == 0)
			s->delay_until = now + (uint64_t)p[1] * 20;
		return status;
	}
	if (op == 15 && count == 2) {
		cterm_set_fgattr(t, (unsigned char)p[1]);
		return 0;
	}
	if (op == 18 && count == 2) {
		cterm_set_bgattr(t, (unsigned char)p[1]);
		return 0;
	}
	if (op == 19 && count == 3) {
		s->x = v[1];
		s->y = v[2];
		s->positioned = true;
		return 0;
	}
	if (op == 8 && count == 3) {
		s->pen_x = v[1];
		s->pen_y = v[2];
		return 0;
	}
	if (op == 99 && count == 1)
		return 0;
	if (op == 16 && count == 4) {
		if (t->skypix_transfer == NULL) {
			static const uint8_t cancel[] = {0x18, 0x18};
			if (t->api_output != NULL)
				t->api_output(t->api_output_context, ST_CTERM_OUTPUT_RESPONSE, cancel,
							  sizeof(cancel));
			return 0;
		}
		status = t->skypix_transfer(t->skypix_transfer_context, p[1], p[2], p[3], text, size);
		if (status == IO_ERROR_IN_PROGRESS)
			s->receiving = true;
		else if (status != 0) {
			static const uint8_t cancel[] = {0x18, 0x18};
			if (t->api_output != NULL)
				t->api_output(t->api_output_context, ST_CTERM_OUTPUT_RESPONSE, cancel,
							  sizeof(cancel));
			cterm_log(t, ST_CTERM_LOG_WARNING, "SkyPix transfer declined (%d)", status);
		}
		return 0;
	}
	if (!((op == 1 || op == 2) && count == 3) && !(op == 3 && count == 4) &&
		!((op == 4 || op == 5 || op == 6 || op == 13) && count == 5) && !(op == 7 && count == 9))
		return 0;
	status = pixels(s, &b);
	if (status != 0)
		return status;
	switch (op) {
	case 1:
		point(b, v[1], v[2], fg);
		break;
	case 2: {
		struct drawing drawing = {b, fg};
		cterm_skypix_line(s->pen_x, s->pen_y, v[1], v[2], plot, &drawing);
		s->pen_x = v[1];
		s->pen_y = v[2];
		break;
	}
	case 3:
		status = flood(b, 1, v[2], v[3], fg, bg);
		break;
	case 4:
		if (v[1] <= v[3] && v[2] <= v[4])
			for (int y = v[2] < 0 ? 0 : v[2]; y <= v[4] && y < 200; ++y)
				span(b, v[1], v[3], y, fg);
		break;
	case 5:
	case 13: {
		struct drawing drawing = {b, fg};
		cterm_skypix_ellipse(v[1], v[2], v[3], v[4], s->kick13, plot, &drawing);
		if (op == 13)
			status = flood(b, 0, v[1], v[2], fg, fg);
		break;
	}
	case 6: {
		if (p[1] >= 640 || p[2] >= 199 || p[3] == 0 || p[4] == 0 || p[3] > 640 - p[1] ||
			p[4] > 199 - p[2])
			break;
		struct st_pixel_buffer *brush = NULL;
		status = st_pixel_buffer_create(&brush, p[3], p[4], false);
		if (status == 0) {
			for (unsigned y = 0; y < p[4]; ++y)
				memcpy(brush->primary + (size_t)y * p[3],
					   b->primary + (size_t)(y + p[2] + 1) * 640 + p[1], p[3] * sizeof(uint32_t));
			st_pixel_buffer_destroy(s->brush);
			s->brush = brush;
			s->brush_depth = 3;
		}
		break;
	}
	case 7:
		if (s->brush == NULL || p[7] > 255 || p[8] > 255)
			break;
		for (unsigned y = 0; y < p[6] && y + p[4] < 200 && y + p[2] < s->brush->height; ++y)
			for (unsigned x = 0; x < p[5] && x + p[3] < 640 && x + p[1] < s->brush->width; ++x) {
				uint32_t src = s->brush->primary[(size_t)(y + p[2]) * s->brush->width + x + p[1]];
				uint32_t *dst = &b->primary[(size_t)(y + p[4]) * 640 + x + p[3]], value = *dst;
				for (unsigned plane = 0;
					 plane < s->brush_depth && plane < (s->colours == 8 ? 3u : 4u); ++plane) {
					unsigned mask = 1u << plane,
							 index = 4u + ((src & mask) ? 2u : 0u) + ((*dst & mask) ? 1u : 0u);
					if (p[8] & mask)
						value = (value & ~mask) | ((p[7] & (1u << index)) ? mask : 0);
				}
				*dst = CTERM_SKYPIX_PALETTE_FIRST + (value & 15);
			}
		break;
	}
	if (status == 0 && op != 6)
		status = publish(s, b, 0, 0);
	st_pixel_buffer_destroy(b);
	return status;
}
static int text(void *context, const uint8_t *bytes, size_t size)
{
	struct cterm_skypix *s = context;
	size_t used;
	int x = s->terminal->xpos, y = s->terminal->ypos;
	if (size != 0 && (bytes[0] < 32 || bytes[0] == 127)) {
		int boundary = cterm_text_boundary(s->terminal);
		if (boundary != 0)
			return boundary;
		text_boundary(s);
	}
	s->presented = false;
	int status = cterm_input_write(s->terminal, bytes, size, &used, CTERM_INPUT_WIRE);
	if (!s->presented && (s->custom || s->positioned) &&
		(x != s->terminal->xpos || y != s->terminal->ypos)) {
		s->x = (s->terminal->xpos - 1) * 8;
		s->y = (s->terminal->ypos - 1) * 8;
	}
	return status;
}
bool cterm_skypix_paused(void *context)
{
	struct cterm_skypix *s = context;
	if (s->receiving)
		return true;
	if (s->delay_until == 0)
		return false;
	uint64_t now;
	if (st_time_monotonic_ms(&now) != 0)
		return false;
	if (now >= s->delay_until) {
		s->delay_until = 0;
		return false;
	}
	return true;
}
static struct cterm_skypix_sink sink(struct cterm_skypix *s)
{
	return (struct cterm_skypix_sink){s, text, command, cterm_skypix_paused};
}
int cterm_skypix_feed(struct cterm_skypix *s, const uint8_t *bytes, size_t size, size_t *used)
{
	struct cterm_skypix_sink callbacks = sink(s);
	return cterm_skypix_parser_feed(&s->parser, &callbacks, bytes, size, used);
}
int cterm_skypix_finish(struct cterm_skypix *s)
{
	s->delay_until = 0;
	struct cterm_skypix_sink callbacks = sink(s);
	return cterm_skypix_parser_finish(&s->parser, &callbacks);
}
int cterm_skypix_reset(struct cterm_skypix *s)
{
	memset(&s->parser, 0, sizeof(s->parser));
	s->delay_until = 0;
	s->receiving = false;
	s->colours = 8;
	text_boundary(s);
	s->x = s->y = s->pen_x = s->pen_y = 0;
	s->positioned = false;
	st_pixel_buffer_destroy(s->brush);
	s->brush = NULL;
	st_pixel_buffer_destroy(s->undo);
	s->undo = NULL;
	st_amiga_font_release(s->font);
	s->font = st_amiga_font_builtin("pearl.font", 8);
	s->custom = false;
	s->terminal->attr = 2;
	s->terminal->screen.default_attribute = 2;
	cterm_render_attribute_colours(s->terminal, 2, &s->terminal->fg_color, &s->terminal->bg_color);
	return palette(s, NULL);
}
int cterm_skypix_create(struct cterminal *t, const char *fonts)
{
	uint32_t width, height;
	int status = st_console_pixel_size(t->screen.console, &width, &height);
	if (status != 0)
		return status;
	if (width != 640 || height < 200 || t->width != 80 || t->height != 25)
		return IO_ERROR_INVALID_ARGUMENT;
	struct cterm_skypix *s = calloc(1, sizeof(*s));
	if (s == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	s->terminal = t;
	s->font_directory = fonts;
	uint8_t bitmap[256 * 8];
	struct st_console_font_mapping mapping[256];
	const st_amiga_font_t *font = st_amiga_font_builtin("pearl.font", 8);
	memset(bitmap, 0, sizeof(bitmap));
	for (unsigned c = 0; c < 256; ++c) {
		const struct st_amiga_glyph glyph = *st_amiga_font_glyph(font, (uint8_t)c);
		for (unsigned y = 0; y < glyph.height; ++y)
			for (unsigned x = 0; x < 8 && x < glyph.width; ++x) {
				unsigned bit = x + glyph.bit_offset;
				int row = (int)y + glyph.top;
				if (row >= 0 && row < 8 &&
					(glyph.bits[(size_t)y * glyph.stride + bit / 8] & (0x80u >> (bit % 8))))
					bitmap[c * 8 + (unsigned)row] |= (uint8_t)(0x80u >> x);
			}
		mapping[c] = (struct st_console_font_mapping){c, c};
	}
	const struct st_console_font console_font = {.bitmap = bitmap,
												 .bitmap_size = sizeof(bitmap),
												 .width = 8,
												 .height = 8,
												 .glyph_count = 256,
												 .row_stride = 1,
												 .glyph_stride = 8,
												 .mapping = mapping,
												 .mapping_count = 256,
												 .fallback_glyph = '?'};
	status = st_console_register_font_copy(t->screen.console, &console_font, &s->default_font);
	if (status == 0) {
		t->screen.font_identifiers[0] = s->default_font;
		t->screen.font_valid[0] = true;
		t->screen.font_codepages[0] = ST_CODEPAGE_ISO_8859_1;
		for (unsigned i = 0; i < 4; ++i)
			t->altfont[i] = 0;
		for (unsigned i = 0; i < 5; ++i)
			t->screen.selectors[i] = 0;
		s->kick13 = t->graphics_mode == ST_CTERM_GRAPHICS_SKYPIX_13;
		t->skypix = 1;
		t->skypix_engine = s;
		status = cterm_skypix_reset(s);
		if (status == 0) {
			cterm_start(t);
			status = cterm_render_take_error(t);
		}
	}
	if (status != 0) {
		t->skypix_engine = NULL;
		cterm_skypix_destroy(s);
	}
	return status;
}
void cterm_skypix_destroy(struct cterm_skypix *s)
{
	if (s == NULL)
		return;
	st_amiga_font_release(s->font);
	st_pixel_buffer_destroy(s->brush);
	st_pixel_buffer_destroy(s->undo);
	text_boundary(s);
	free(s);
}
static void cursor_bookkeeping(struct cterm_skypix *s)
{
	s->terminal->xpos = s->x < 0 ? 1 : s->x >= 640 ? 80 : s->x / 8 + 1;
	s->terminal->ypos = s->y < 0 ? 1 : s->y >= 200 ? 25 : s->y / 8 + 1;
}
bool cterm_skypix_control(struct cterm_skypix *s, uint8_t byte)
{
	if (!s->custom && !s->positioned)
		return false;
	if (byte == '\r') {
		s->x = 0;
		if (s->custom)
			s->y = cterm_skypix_word((unsigned)(s->y + (int)st_amiga_font_height(s->font)));
	} else if (byte == '\n') {
		if (!s->custom) {
			s->y = cterm_skypix_word((unsigned)(s->y + 8));
			if (s->y >= 200 && s->y < 208) {
				cterm_scrollup(s->terminal);
				s->y -= 8;
			}
		}
	} else if (byte == '\b' && !s->custom)
		s->x = s->x >= 8 ? s->x - 8 : 0;
	else
		return false;
	cursor_bookkeeping(s);
	s->presented = true;
	return true;
}
int cterm_skypix_present(struct cterm_skypix *s, const struct st_unicode_presentation_event *event,
						 bool *handled)
{
	*handled = s->custom || s->positioned;
	if (!*handled)
		return 0;
	if (event->kind == ST_UNICODE_PRESENTATION_COMMIT) {
		st_pixel_buffer_destroy(s->undo);
		s->undo = NULL;
		free(s->undo_pending);
		s->undo_pending = NULL;
		return 0;
	}
	if (event->kind == ST_UNICODE_PRESENTATION_REVISE && s->undo != NULL) {
		int status = publish(s, s->undo, (int)s->undo_rect.x, (int)s->undo_rect.y);
		if (status != 0)
			return status;
		s->x = s->undo_x;
		s->y = s->undo_y;
		if (s->undo_pending != NULL && s->pending != NULL) {
			for (unsigned y = 0; y < s->undo_rect.height; ++y)
				memcpy(s->pending + (size_t)(s->undo_rect.y + y) * 640 + s->undo_rect.x,
					   s->undo_pending + (size_t)y * s->undo_rect.width, s->undo_rect.width);
			s->pending_rect = s->undo_pending_rect;
		}
	}
	st_pixel_buffer_destroy(s->undo);
	s->undo = NULL;
	free(s->undo_pending);
	s->undo_pending = NULL;
	if (!s->custom && s->x == 640 && s->y >= 0 && s->y < 200) {
		s->x = 0;
		s->y += 8;
		if (s->y >= 200) {
			cterm_scrollup(s->terminal);
			s->y -= 8;
		}
	}
	uint32_t codepoint;
	size_t consumed;
	uint8_t character = '?';
	if (st_utf8_decode_one(&codepoint, &consumed, event->bytes, event->size) == 0 &&
		consumed == event->size && codepoint <= 255)
		character = (uint8_t)codepoint;
	const struct st_amiga_glyph glyph = *st_amiga_font_glyph(s->font, character);
	int next = cterm_skypix_word((unsigned)(s->x + glyph.advance));
	bool legacy = s->kick13 && s->custom;
	int left = s->x + glyph.bearing, top = s->y + glyph.top;
	int right = left + (int)glyph.width, bottom = top + (int)glyph.height;
	if (!s->custom) {
		if (left > s->x)
			left = s->x;
		if (right < s->x + glyph.advance)
			right = s->x + glyph.advance;
	}
	if (legacy && (s->pending != NULL || right > next)) {
		if (s->pending == NULL) {
			s->pending = calloc(640 * 200, 1);
			if (s->pending == NULL)
				return IO_ERROR_OUT_OF_MEMORY;
		}
		if (s->pending_rect.width != 0) {
			if (left > (int)s->pending_rect.x)
				left = (int)s->pending_rect.x;
			if (top > (int)s->pending_rect.y)
				top = (int)s->pending_rect.y;
			if (right < (int)(s->pending_rect.x + s->pending_rect.width))
				right = (int)(s->pending_rect.x + s->pending_rect.width);
			if (bottom < (int)(s->pending_rect.y + s->pending_rect.height))
				bottom = (int)(s->pending_rect.y + s->pending_rect.height);
		}
	}
	if (left < 0)
		left = 0;
	if (top < 0)
		top = 0;
	if (right > 640)
		right = 640;
	if (bottom > 200)
		bottom = 200;
	s->undo_x = s->x;
	s->undo_y = s->y;
	if (right > left && bottom > top) {
		struct st_console_rect area = {(uint32_t)left, (uint32_t)top, (uint32_t)(right - left),
									   (uint32_t)(bottom - top)};
		int status = st_console_get_pixels(&s->undo, s->terminal->screen.console, &area, false);
		if (status != 0)
			return status;
		s->undo_rect = area;
		if (legacy && s->pending != NULL) {
			s->undo_pending = malloc((size_t)area.width * area.height);
			if (s->undo_pending == NULL)
				return IO_ERROR_OUT_OF_MEMORY;
			for (unsigned y = 0; y < area.height; ++y)
				memcpy(s->undo_pending + (size_t)y * area.width,
					   s->pending + (size_t)(area.y + y) * 640 + area.x, area.width);
			s->undo_pending_rect = s->pending_rect;
		}
		struct st_pixel_buffer *image = NULL;
		status = st_pixel_buffer_clone(&image, s->undo);
		if (status != 0)
			return status;
		unsigned fg = (s->terminal->attr & 15) & (s->colours - 1),
				 bg = (s->terminal->attr >> 4 & 15) & (s->colours - 1);
		if (!s->custom)
			st_pixel_buffer_fill(image, CTERM_SKYPIX_PALETTE_FIRST + bg,
								 CTERM_SKYPIX_PALETTE_FIRST + bg);
		if (legacy && s->pending != NULL)
			for (int py = top; py < bottom; ++py)
				for (int px = left; px < right && px < next; ++px) {
					size_t index = (size_t)py * 640 + (unsigned)px;
					if (s->pending[index]) {
						point(image, px - left, py - top, fg);
						s->pending[index] = 0;
					}
				}
		for (int py = top; py < bottom; ++py)
			for (int px = left; px < right; ++px) {
				int gx = px - s->x - glyph.bearing, gy = py - s->y - glyph.top;
				if (gx < 0 || gy < 0 || (unsigned)gx >= glyph.width || (unsigned)gy >= glyph.height)
					continue;
				unsigned x = (unsigned)gx, y = (unsigned)gy;
				unsigned bit = x + glyph.bit_offset;
				if (glyph.bits[(size_t)y * glyph.stride + bit / 8] & (0x80u >> (bit % 8))) {
					if (legacy && px >= next)
						s->pending[(size_t)py * 640 + (unsigned)px] = 1;
					else
						point(image, px - left, py - top, fg);
				}
			}
		if (legacy && s->pending != NULL) {
			int min_x = 640, min_y = 200, max_x = -1, max_y = -1;
			for (int py = top; py < bottom; ++py)
				for (int px = left; px < right; ++px)
					if (s->pending[(size_t)py * 640 + (unsigned)px]) {
						if (px < min_x)
							min_x = px;
						if (px > max_x)
							max_x = px;
						if (py < min_y)
							min_y = py;
						if (py > max_y)
							max_y = py;
					}
			s->pending_rect = max_x < 0 ? (struct st_console_rect){0}
										: (struct st_console_rect){(uint32_t)min_x, (uint32_t)min_y,
																   (uint32_t)(max_x - min_x + 1),
																   (uint32_t)(max_y - min_y + 1)};
		}
		status = publish(s, image, left, top);
		st_pixel_buffer_destroy(image);
		if (status != 0)
			return status;
	}
	s->x = next;
	s->presented = true;
	s->pen_x = s->x;
	s->pen_y = cterm_skypix_word((unsigned)(s->y + (int)st_amiga_font_height(s->font) - 1));
	cursor_bookkeeping(s);
	return 0;
}
