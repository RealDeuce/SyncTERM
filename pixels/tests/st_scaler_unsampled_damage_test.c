/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

/* Standalone regression for damage to a source pixel not sampled by a
 * downscaled output. Compile with the unchanged pixels implementation.
 */
#include "st_pixels.h"

#include <stdio.h>
#include <stdlib.h>

int
main(void)
{
	uint32_t source[8] = {0};
	struct st_rgb_buffer input = {source, 8, 1};
	st_pixel_scaler_t *scaler = NULL;
	const struct st_rgb_buffer *output = NULL;
	struct st_pixel_rect damage = {7, 0, 1, 1};
	struct st_pixel_rect changed;
	int status;

	if (st_pixel_scaler_create(&scaler) != 0)
		return 2;
	if (st_pixel_scaler_scale(scaler, &output, &input, 2, 1,
	    ST_PIXEL_SCALING_BLOCKY) != 0) {
		st_pixel_scaler_destroy(scaler);
		return 2;
	}
	source[7] = UINT32_C(0xffffff);
	status = st_pixel_scaler_scale_region(scaler, &output, &input,
	    &damage, &changed);
	if (status != 0 || changed.x > 2 || changed.width > 2 - changed.x
	    || changed.y > 1 || changed.height > 1 - changed.y
	    || output->pixels[0] != 0 || output->pixels[1] != 0) {
		fprintf(stderr, "unsampled source damage produced an invalid update\n");
		st_pixel_scaler_destroy(scaler);
		return EXIT_FAILURE;
	}
	st_pixel_scaler_destroy(scaler);
	return EXIT_SUCCESS;
}
