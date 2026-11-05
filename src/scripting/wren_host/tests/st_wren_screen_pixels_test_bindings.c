/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_wren_screen_test_calls.h"
#include "st_pixels.h"

static int
test_pixel_buffer_blit(struct st_pixel_buffer *destination,
	const struct st_pixel_buffer *source, const struct st_pixel_mask *mask,
	const struct st_pixel_blit *blit)
{
	struct st_wren_screen_fault *fault = &st_wren_test_screen_fault;
	if (fault->name != NULL && strcmp(fault->name, "st_pixel_buffer_blit") == 0) {
		++fault->calls;
		return fault->status;
	}
	return st_pixel_buffer_blit(destination, source, mask, blit);
}

#define st_pixel_buffer_blit test_pixel_buffer_blit
#include "../wren_bind_pixels.c"
