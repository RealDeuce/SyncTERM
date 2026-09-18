/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_pixels.h"

#include "io_error.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", \
		    __FILE__, __LINE__, #expression); \
		exit(EXIT_FAILURE); \
	} \
} while (false)

static size_t
position(const struct st_pixel_buffer *buffer, uint32_t x, uint32_t y)
{
	return (size_t)y * buffer->width + x;
}

int
main(void)
{
	struct st_pixel_buffer *source = NULL;
	struct st_pixel_buffer *destination = NULL;
	struct st_pixel_buffer *copy = NULL;
	struct st_pixel_buffer *scaled = NULL;
	struct st_pixel_mask *mask = NULL;
	struct st_pixel_blit blit = {
		.source_x = 0,
		.source_y = 0,
		.width = 2,
		.height = 2,
		.destination_x = -1,
		.destination_y = 1,
		.scale_x = 2,
		.scale_y = 2,
		.mask_x = 0,
		.mask_y = 0,
		.flags = ST_PIXEL_BLIT_FLIP_X,
	};
	bool bit;

	CHECK(st_pixel_buffer_create(&source, 2, 2, true) == 0);
	CHECK(st_pixel_buffer_create(&destination, 4, 4, false) == 0);
	CHECK(st_pixel_mask_create(&mask, 2, 2) == 0);
	for (size_t i = 0; i < 4; ++i) {
		source->primary[i] = (uint32_t)i + 1;
		source->alternate[i] = (uint32_t)i + 11;
	}
	CHECK(st_pixel_buffer_fill(destination, 9, 19) == 0);
	CHECK(st_pixel_mask_set(mask, 0, 0, true) == 0);
	CHECK(st_pixel_mask_set(mask, 1, 1, true) == 0);
	CHECK(st_pixel_mask_get(mask, 1, 1, &bit) == 0 && bit);
	CHECK(st_pixel_mask_get(mask, 1, 0, &bit) == 0 && !bit);
	CHECK(st_pixel_buffer_blit(destination, source, mask, &blit) == 0);
	CHECK(destination->alternate != NULL);
	/* The clipped first row addresses flipped source (1,0), masked out. */
	CHECK(destination->primary[position(destination, 0, 1)] == 9);
	CHECK(destination->alternate[position(destination, 0, 1)] == 9);
	/* The second source row's flipped x=1 lands in the clipped left pixel. */
	CHECK(destination->primary[position(destination, 0, 3)] == 4);
	CHECK(destination->alternate[position(destination, 0, 3)] == 14);
	CHECK(destination->primary[position(destination, 1, 3)] == 9);

	CHECK(st_pixel_buffer_clone(&copy, source) == 0);
	CHECK(copy->primary != source->primary);
	CHECK(copy->alternate != source->alternate);
	CHECK(copy->primary[3] == 4 && copy->alternate[3] == 14);
	blit = (struct st_pixel_blit){
		.source_x = 0,
		.source_y = 0,
		.width = 2,
		.height = 2,
		.destination_x = 0,
		.destination_y = 0,
		.scale_x = 1,
		.scale_y = 1,
		.flags = ST_PIXEL_BLIT_FLIP_X | ST_PIXEL_BLIT_FLIP_Y,
	};
	CHECK(st_pixel_buffer_blit(copy, copy, NULL, &blit) == 0);
	CHECK(copy->primary[0] == 4 && copy->primary[3] == 1);
	st_pixel_buffer_remove_alternate(copy);
	CHECK(copy->alternate == NULL);
	CHECK(st_pixel_buffer_ensure_alternate(copy) == 0);
	CHECK(copy->alternate[0] == copy->primary[0]);
	CHECK(st_pixel_buffer_scale(&scaled, source, 4, 6) == 0);
	CHECK(scaled->width == 4 && scaled->height == 6);
	CHECK(scaled->primary[position(scaled, 0, 0)] == 1);
	CHECK(scaled->primary[position(scaled, 1, 0)] == 1);
	CHECK(scaled->primary[position(scaled, 2, 0)] == 2);
	CHECK(scaled->primary[position(scaled, 0, 5)] == 3);
	CHECK(scaled->primary[position(scaled, 3, 5)] == 4);
	CHECK(scaled->alternate[position(scaled, 3, 5)] == 14);
	blit.width = 3;
	CHECK(st_pixel_buffer_blit(destination, source, NULL, &blit)
	    == IO_ERROR_OUT_OF_RANGE);

	st_pixel_mask_destroy(mask);
	st_pixel_buffer_destroy(scaled);
	st_pixel_buffer_destroy(copy);
	st_pixel_buffer_destroy(destination);
	st_pixel_buffer_destroy(source);
	return 0;
}
