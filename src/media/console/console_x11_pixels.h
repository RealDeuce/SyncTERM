/* Private XImage packing; no display connection or Xlib calls are required. */
/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_CONSOLE_X11_PIXELS_H_
#define ST_CONSOLE_X11_PIXELS_H_

#include "st_console.h"
#include "st_pixels.h"
#include <X11/Xlib.h>

struct st_x11_pixel_writer {
	unsigned long red[256], green[256], blue[256];
	int bytes;
	bool little;
	bool copy;
};

void st_x11_pixel_writer_init(struct st_x11_pixel_writer *writer,
	const XImage *image);
void st_x11_pixel_writer_upload(const struct st_x11_pixel_writer *writer,
	XImage *image, const struct st_rgb_buffer *pixels,
	const struct st_console_rect *rectangle);

#endif
