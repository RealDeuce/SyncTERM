/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */
#include "console_x11_pixels.h"

#include <X11/Xutil.h>
#include <stdint.h>
#include <string.h>

static unsigned long
component_value(uint32_t component, unsigned long mask)
{
	unsigned int shift = 0;
	unsigned long maximum;

	if (mask == 0)
		return 0;
	while (((mask >> shift) & 1UL) == 0)
		++shift;
	maximum = mask >> shift;
	return (((unsigned long)component * maximum + 127UL) / 255UL << shift) & mask;
}

void
st_x11_pixel_writer_init(struct st_x11_pixel_writer *writer, const XImage *image)
{
	const uint32_t marker = 1;
	unsigned char first;

	memcpy(&first, &marker, 1);
	writer->little = image->byte_order == LSBFirst;
	writer->bytes = image->format == ZPixmap && image->xoffset == 0
	    && (image->bits_per_pixel == 8 || image->bits_per_pixel == 16
	    || image->bits_per_pixel == 24 || image->bits_per_pixel == 32)
	    ? image->bits_per_pixel / 8 : 0;
	writer->copy = writer->bytes == 4 && writer->little == (first == 1)
	    && image->red_mask == 0xff0000 && image->green_mask == 0xff00
	    && image->blue_mask == 0xff;
	for (unsigned i = 0; i < 256; ++i) {
		writer->red[i] = component_value(i, image->red_mask);
		writer->green[i] = component_value(i, image->green_mask);
		writer->blue[i] = component_value(i, image->blue_mask);
	}
}

void
st_x11_pixel_writer_upload(const struct st_x11_pixel_writer *writer,
	XImage *image, const struct st_rgb_buffer *pixels,
	const struct st_console_rect *rectangle)
{
	for (uint32_t y = rectangle->y; y < rectangle->y + rectangle->height; ++y) {
		const uint32_t *source = pixels->pixels + (size_t)y * pixels->width
		    + rectangle->x;
		unsigned char *destination = (unsigned char *)image->data
		    + (size_t)y * (size_t)image->bytes_per_line
		    + (size_t)rectangle->x * (unsigned)writer->bytes;

		if (writer->copy) {
			memcpy(destination, source, (size_t)rectangle->width * sizeof(*source));
			continue;
		}
		/* Dispatch once per row; pixel addresses, format tests and the
		 * byte-writing loop are absent from the conversion inner loops. */
#define PACK_ROW(...) do { \
	for (uint32_t x = 0; x < rectangle->width; ++x) { \
		uint32_t rgb = source[x]; \
		unsigned long value = writer->red[(rgb >> 16) & 255] \
		    | writer->green[(rgb >> 8) & 255] | writer->blue[rgb & 255]; \
		__VA_ARGS__; \
		destination += writer->bytes; \
	} \
} while (false)
		if (writer->little) {
			switch (writer->bytes) {
			case 1: PACK_ROW(destination[0] = (unsigned char)value); break;
			case 2: PACK_ROW(destination[0] = (unsigned char)value;
			    destination[1] = (unsigned char)(value >> 8)); break;
			case 3: PACK_ROW(destination[0] = (unsigned char)value;
			    destination[1] = (unsigned char)(value >> 8);
			    destination[2] = (unsigned char)(value >> 16)); break;
			case 4: PACK_ROW(destination[0] = (unsigned char)value;
			    destination[1] = (unsigned char)(value >> 8);
			    destination[2] = (unsigned char)(value >> 16);
			    destination[3] = (unsigned char)(value >> 24)); break;
			default: PACK_ROW(XPutPixel(image, (int)(rectangle->x + x),
			    (int)y, value)); break;
			}
		} else {
			switch (writer->bytes) {
			case 1: PACK_ROW(destination[0] = (unsigned char)value); break;
			case 2: PACK_ROW(destination[0] = (unsigned char)(value >> 8);
			    destination[1] = (unsigned char)value); break;
			case 3: PACK_ROW(destination[0] = (unsigned char)(value >> 16);
			    destination[1] = (unsigned char)(value >> 8);
			    destination[2] = (unsigned char)value); break;
			case 4: PACK_ROW(destination[0] = (unsigned char)(value >> 24);
			    destination[1] = (unsigned char)(value >> 16);
			    destination[2] = (unsigned char)(value >> 8);
			    destination[3] = (unsigned char)value); break;
			default: PACK_ROW(XPutPixel(image, (int)(rectangle->x + x),
			    (int)y, value)); break;
			}
		}
#undef PACK_ROW
	}
}
