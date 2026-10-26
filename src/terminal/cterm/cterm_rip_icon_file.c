/* RIP .ICN four-plane file format. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "cterm_rip_icon_file.h"

#include "io_error.h"

#include <stdbool.h>
#include <stdlib.h>

static bool
icon_layout(uint32_t width, uint32_t height, size_t *row_bytes,
    size_t *pixel_count, size_t *file_size)
{
	if (width == 0 || height == 0 || width > 65536 || height > 65536)
		return false;
	*row_bytes = (width + 7U) / 8U;
	/* The old BGI image buffer has a 16-bit byte count, including
	 * six bytes of in-memory header. Check before multiplying. */
	if (height > (65535U - 6U) / (4U * *row_bytes))
		return false;
	*pixel_count = (size_t)width * height;
	*file_size = 4U + 4U * *row_bytes * height;
	return true;
}

int
cterm_rip_icon_decode(uint8_t **pixels, uint32_t *width,
    uint32_t *height, const uint8_t *bytes, size_t size)
{
	uint32_t decoded_width, decoded_height;
	size_t row_bytes, pixel_count, file_size;
	uint8_t *decoded;

	if (pixels == NULL || width == NULL || height == NULL || bytes == NULL
	    || size < 4)
		return IO_ERROR_INVALID_ARGUMENT;
	decoded_width = (uint32_t)bytes[0] + ((uint32_t)bytes[1] << 8) + 1U;
	decoded_height = (uint32_t)bytes[2] + ((uint32_t)bytes[3] << 8) + 1U;
	if (!icon_layout(decoded_width, decoded_height, &row_bytes,
	    &pixel_count, &file_size) || size < file_size)
		return IO_ERROR_OUT_OF_RANGE;
	decoded = malloc(pixel_count);
	if (decoded == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	for (uint32_t y = 0; y < decoded_height; y++) {
		const uint8_t *row = bytes + 4U + (size_t)y * 4U * row_bytes;

		for (uint32_t x = 0; x < decoded_width; x++) {
			uint8_t colour = 0;
			uint8_t mask = (uint8_t)(0x80U >> (x & 7U));

			for (unsigned plane = 0; plane < 4; plane++) {
				if ((row[(3U - plane) * row_bytes + x / 8U]
				    & mask) != 0)
					colour |= (uint8_t)(1U << plane);
			}
			decoded[(size_t)y * decoded_width + x] = colour;
		}
	}
	*pixels = decoded;
	*width = decoded_width;
	*height = decoded_height;
	return 0;
}

int
cterm_rip_icon_encode(uint8_t **bytes, size_t *size,
    const uint8_t *pixels, uint32_t width, uint32_t height)
{
	size_t row_bytes, pixel_count, file_size;
	uint8_t *encoded;

	if (bytes == NULL || size == NULL || pixels == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (!icon_layout(width, height, &row_bytes, &pixel_count, &file_size))
		return IO_ERROR_OUT_OF_RANGE;
	encoded = calloc(file_size + 1U, 1);
	if (encoded == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	encoded[0] = (uint8_t)(width - 1U);
	encoded[1] = (uint8_t)((width - 1U) >> 8);
	encoded[2] = (uint8_t)(height - 1U);
	encoded[3] = (uint8_t)((height - 1U) >> 8);
	for (uint32_t y = 0; y < height; y++) {
		uint8_t *row = encoded + 4U + (size_t)y * 4U * row_bytes;

		for (uint32_t x = 0; x < width; x++) {
			uint8_t colour = pixels[(size_t)y * width + x];
			uint8_t mask = (uint8_t)(0x80U >> (x & 7U));

			if (colour > 15U) {
				free(encoded);
				return IO_ERROR_OUT_OF_RANGE;
			}
			for (unsigned plane = 0; plane < 4; plane++) {
				if ((colour & (1U << plane)) != 0)
					row[(3U - plane) * row_bytes + x / 8U]
					    |= mask;
			}
		}
	}
	encoded[file_size] = 'F';
	*bytes = encoded;
	*size = file_size + 1U;
	return 0;
}
