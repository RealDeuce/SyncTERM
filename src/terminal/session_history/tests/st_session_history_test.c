/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_session_history.h"
#include "io_error.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, \
		    #expression); \
		return EXIT_FAILURE; \
	} \
} while (false)

int
main(void)
{
	st_session_history_t *history = NULL;
	struct st_console_cell source[8] = {0};
	struct st_console_cell *copy = NULL;
	uint32_t width;
	size_t lines;
	enum st_screen_mode mode;
	bool status;
	struct st_pixel_buffer **pixels = calloc(4, sizeof(*pixels));
	struct st_pixel_buffer *image = NULL;
	CHECK(pixels != NULL);
	CHECK(st_pixel_buffer_create(&pixels[1], 16, 8, true) == 0);
	CHECK(st_pixel_buffer_fill(pixels[1], ST_PIXEL_COLOR_RGB_VALUE(255, 0, 0),
	    ST_PIXEL_COLOR_RGB_VALUE(0, 255, 0)) == 0);

	for (size_t i = 0; i < 8; ++i)
		source[i].glyph = (uint32_t)('A' + i);
	CHECK(st_session_history_create(&history, 3) == 0);
	CHECK(st_session_history_publish(history, source, 2, 4,
	    ST_SCREEN_MODE_C64, true, pixels) == 0);
	CHECK(st_session_history_copy_pixels(history, 0, &image) == 0);
	CHECK(image != NULL && image->width == 16 && image->height == 8);
	CHECK(image->primary[0] == ST_PIXEL_COLOR_RGB_VALUE(255, 0, 0));
	CHECK(image->alternate[0] == ST_PIXEL_COLOR_RGB_VALUE(0, 255, 0));
	st_pixel_buffer_destroy(image);
	image = NULL;
	CHECK(st_session_history_copy_pixels(history, 1, &image) == 0 && image == NULL);
	CHECK(st_session_history_copy(history, &copy, &width, &lines, &mode,
	    &status) == 0);
	CHECK(width == 2 && lines == 3 && mode == ST_SCREEN_MODE_C64 && status);
	CHECK(copy[0].glyph == 'C' && copy[5].glyph == 'H');
	free(copy);
	CHECK(st_session_history_set_capacity(history, 2) == 0);
	CHECK(st_session_history_capacity(history) == 2);
	CHECK(st_session_history_copy(history, &copy, &width, &lines, &mode,
	    &status) == 0);
	CHECK(lines == 2 && copy[0].glyph == 'E');
	CHECK(st_session_history_copy_pixels(history, 0, &image) == 0 && image == NULL);
	CHECK(st_session_history_copy_pixels(history, 2, &image) == IO_ERROR_OUT_OF_RANGE);
	free(copy);
	struct st_pixel_buffer *scaled = NULL;
	CHECK(st_pixel_buffer_create(&image, 4, 2, true) == 0);
	image->primary[0] = 1;
	image->primary[1] = 2;
	image->primary[2] = 3;
	image->primary[3] = 4;
	image->alternate[0] = 5;
	CHECK(st_history_resize_pixels(&scaled, image, 2, 3, 4, 4) == 0);
	CHECK(scaled->width == 12 && scaled->height == 4);
	CHECK(scaled->primary[0] == 1 && scaled->primary[1] == 1 && scaled->primary[2] == 2);
	CHECK(scaled->primary[4] == 3 && scaled->primary[6] == 4);
	CHECK(scaled->primary[8] == ST_PIXEL_COLOR_RGB_TAG && scaled->alternate[0] == 5);
	st_pixel_buffer_destroy(scaled);
	scaled = NULL;
	CHECK(st_history_resize_pixels(&scaled, image, 0, 3, 4, 4) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(scaled == NULL);
	st_pixel_buffer_destroy(image);
	image = NULL;
	st_session_history_clear(history);
	CHECK(st_session_history_copy(history, &copy, &width, &lines, &mode,
	    &status) != 0);
	st_session_history_destroy(history);
	return EXIT_SUCCESS;
}
