/* SkyPix configuration for a connected display. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_SKYPIX_H_
#define ST_SKYPIX_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "st_amiga_font.h"

/* Called at the second bang, with following wire bytes unread. Return 0 for
 * a completed cache hit, IO_ERROR_IN_PROGRESS for an owned transfer, or an error. */
typedef int (*st_skypix_transfer_callback_t)(void *context, unsigned mode, unsigned width, unsigned height,
    const uint8_t *filename, size_t size);

struct st_skypix_config {
	bool kick13;
	/* Zero uses the terminal grid extent, excluding the status row. */
	uint32_t    width, height;
	const char *font_directory;
	/* The default strike is borrowed through destruction. NULL selects Pearl 8. */
	const st_amiga_font_t        *font;
	st_skypix_transfer_callback_t transfer;
	void                         *transfer_context;
};

#endif
