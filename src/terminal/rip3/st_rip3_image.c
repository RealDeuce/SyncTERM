/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_image.h"
#include "st_rip3_ports.h"
#include "st_pixels.h"
#include "io_error.h"

#include <stdlib.h>

static int32_t
clamp_coordinate(int32_t value, int32_t maximum)
{
	return value < 0 ? 0 : value > maximum ? maximum : value;
}

static int
validate_image(const struct st_pixel_buffer *image,
    const uint32_t *source_palette, size_t source_palette_size,
    const struct st_rip3_palette *palette)
{
	if (image == NULL || palette == NULL || image->primary == NULL
	    || image->width == 0 || image->height == 0
	    || (source_palette_size != 0 && source_palette == NULL))
		return IO_ERROR_INVALID_ARGUMENT;
	if (source_palette_size > 256 || (uint64_t)image->width * image->height
	    > SIZE_MAX / sizeof(*image->primary))
		return IO_ERROR_OUT_OF_RANGE;
	for (size_t i = 0; i < 256; i++) {
		if (palette->rgb[i] > UINT32_C(0xffffff))
			return IO_ERROR_OUT_OF_RANGE;
	}
	for (size_t i = 0; i < source_palette_size; i++) {
		if (source_palette[i] > UINT32_C(0xffffff))
			return IO_ERROR_OUT_OF_RANGE;
	}
	for (size_t i = 0; i < (size_t)image->width * image->height; i++) {
		uint32_t pixel = image->primary[i];

		if (ST_PIXEL_COLOR_IS_RGB(pixel)) {
			if ((pixel & ~ST_PIXEL_COLOR_RGB_TAG) > UINT32_C(0xffffff))
				return IO_ERROR_INVALID_FORMAT;
		}
		else if (pixel >= source_palette_size)
			return IO_ERROR_INVALID_FORMAT;
	}
	return 0;
}

int
st_rip3_draw_image(struct st_rip3_ports *ports,
    const struct st_pixel_buffer *image, const uint32_t *source_palette,
    size_t source_palette_size, const struct st_rip3_palette *palette)
{
	struct st_rip3_port_info info;
	struct st_rip3_rect area, fitted;
	unsigned current;
	uint32_t width, height, available_width, available_height;
	uint8_t *pixels = NULL, mapped[256], black;
	int status;

	if (ports == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_rip3_ports_current(ports, &current);
	if (status == 0)
		status = st_rip3_ports_info(ports, current, &info);
	if (status != 0 || !info.viewport_active || !info.image.enabled)
		return status;
	if ((info.image.flags & 16) != 0)
		return IO_ERROR_NOT_SUPPORTED;
	status = validate_image(image, source_palette, source_palette_size, palette);
	if (status != 0)
		return status;
	if ((info.image.flags & 8) != 0 && source_palette_size != 0)
		return IO_ERROR_NOT_SUPPORTED;
	area = (struct st_rip3_rect){0,0,info.viewport.x1 - info.viewport.x0,
	    info.viewport.y1 - info.viewport.y0};
	if (!info.image.automatic) {
		struct st_rip3_port_info screen;
		status = st_rip3_ports_info(ports,0,&screen);
		if (status != 0) return status;
		/* Common presentation and the JPEG wrapper each consume a far-edge
		 * unit. Nonpositive far coordinates use physical device dimensions;
		 * notably wire all-zero style stores (0,0,1,1), not inhibit state. */
		int64_t right = (int64_t)info.image.area.x1-2, bottom = (int64_t)info.image.area.y1-2;
		if (right <= 0) right = screen.bounds.x1;
		if (bottom <= 0) bottom = screen.bounds.y1;
		area = (struct st_rip3_rect){
		    clamp_coordinate(info.image.area.x0,area.x1), clamp_coordinate(info.image.area.y0,area.y1),
		    clamp_coordinate((int32_t)right,area.x1), clamp_coordinate((int32_t)bottom,area.y1)};
	}

	if (area.x0 > area.x1 || area.y0 > area.y1) return 0;
	available_width = (uint32_t)(area.x1 - area.x0) + 1;
	available_height = (uint32_t)(area.y1 - area.y0) + 1;
	width = available_width;
	height = available_height;
	if ((info.image.flags & 1) != 0) {
		if ((uint64_t)width * image->height > (uint64_t)height * image->width) {
			width = (uint32_t)((uint64_t)height * image->width / image->height);
			if (width == 0)
				width = 1;
		}
		else {
			height = (uint32_t)((uint64_t)width * image->height / image->width);
			if (height == 0)
				height = 1;
		}
	}
	fitted.x0 = area.x0 + (int32_t)((available_width - width) / 2);
	fitted.y0 = area.y0 + (int32_t)((available_height - height) / 2);
	fitted.x1 = fitted.x0 + (int32_t)width - 1;
	fitted.y1 = fitted.y0 + (int32_t)height - 1;
	if ((uint64_t)width * height > SIZE_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	pixels = malloc((size_t)width * height);
	if (pixels == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	/* Validation above makes color mapping infallible. Prepare the complete
	 * fitted bitmap before clearing or painting any destination pixels. */
	(void)st_rip3_palette_color(palette, true, 0, &black);
	for (size_t i = 0; i < source_palette_size; i++)
		(void)st_rip3_palette_color(palette, true, source_palette[i], &mapped[i]);
	for (uint32_t y = 0; y < height; y++) {
		uint32_t sy = (uint32_t)(((uint64_t)y * 2 + 1) * image->height
		    / (2 * (uint64_t)height));

		for (uint32_t x = 0; x < width; x++) {
			uint32_t sx = (uint32_t)(((uint64_t)x * 2 + 1) * image->width
			    / (2 * (uint64_t)width));
			uint32_t sample = image->primary[(size_t)sy * image->width + sx];
			uint8_t *pixel = &pixels[(size_t)y * width + x];

			if (ST_PIXEL_COLOR_IS_RGB(sample))
				(void)st_rip3_palette_color(palette, true,
				    sample & ~ST_PIXEL_COLOR_RGB_TAG, pixel);
			else
				*pixel = mapped[sample];
		}
	}
	if ((info.image.flags & 4) == 0)
		(void)st_rip3_ports_fill(ports, &fitted, black, ST_RIP3_COPY);
	/* The original JPEG consumer uses SRCCOPY independently of the vector
	 * ROP. The clear also covers only the fitted image, retaining margins. */
	for (int32_t y = fitted.y0; y <= fitted.y1; y++) {
		for (int32_t x = fitted.x0; x <= fitted.x1; x++) {
			(void)st_rip3_ports_plot(ports, x, y,
			    pixels[(size_t)(y - fitted.y0) * width + (uint32_t)(x - fitted.x0)], ST_RIP3_COPY);
		}
	}
	free(pixels);
	return 0;
}
