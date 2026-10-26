/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_pixel_image.h"

#include "io_error.h"
#include "st_pixels.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

struct pnm_reader {
	const uint8_t *data;
	size_t length;
	size_t position;
	bool cterm;
};

static bool
pnm_whitespace(const struct pnm_reader *reader, uint8_t value)
{
	return value == ' ' || value == '\t' || value == '\r'
	    || value == '\n' || (!reader->cterm && (value == '\v' || value == '\f'));
}

static bool
pnm_skip_space(struct pnm_reader *reader)
{
	bool skipped = false;

	while (reader->position < reader->length) {
		uint8_t value = reader->data[reader->position];

		if (pnm_whitespace(reader, value)) {
			reader->position++;
			skipped = true;
			continue;
		}
		if (value != '#')
			break;
		skipped = true;
		while (reader->position < reader->length) {
			value = reader->data[reader->position++];
			if (value == '\r' || value == '\n')
				break;
		}
	}
	return skipped && reader->position < reader->length;
}

static bool
pnm_read_number(struct pnm_reader *reader, uintmax_t *result)
{
	uintmax_t value = 0;
	size_t start;

	if (!pnm_skip_space(reader))
		return false;
	start = reader->position;
	while (reader->position < reader->length) {
		uint8_t digit = reader->data[reader->position];

		if (digit < '0' || digit > '9')
			break;
		if (value > (UINTMAX_MAX - (digit - '0')) / 10)
			return false;
		value = value * 10 + (digit - '0');
		reader->position++;
	}
	if (reader->position == start)
		return false;
	*result = value;
	return true;
}

static bool
pnm_dimensions(uintmax_t width, uintmax_t height, size_t element_size,
	size_t *count)
{
	size_t pixels;

	if (width == 0 || height == 0 || width > UINT32_MAX
	    || height > UINT32_MAX || (size_t)height > SIZE_MAX / (size_t)width)
		return false;
	pixels = (size_t)width * (size_t)height;
	if (pixels > SIZE_MAX / element_size)
		return false;
	*count = pixels;
	return true;
}

static uint8_t
bt709_to_srgb(uintmax_t sample, uintmax_t maximum)
{
	double encoded = (double)sample / (double)maximum;
	double linear;
	double srgb;

	if (encoded < 0.081)
		linear = encoded / 4.5;
	else
		linear = pow((encoded + 0.099) / 1.099, 1.0 / 0.45);
	if (linear <= 0.0031308)
		srgb = 12.92 * linear;
	else
		srgb = 1.055 * pow(linear, 1.0 / 2.4) - 0.055;
	if (srgb <= 0.0)
		return 0;
	if (srgb >= 1.0)
		return UINT8_MAX;
	return (uint8_t)(srgb * 255.0 + 0.5);
}

static bool
pnm_raw_separator(struct pnm_reader *reader)
{
	uint8_t first;

	if (reader->position >= reader->length
	    || !pnm_whitespace(reader, reader->data[reader->position]))
		return false;
	first = reader->data[reader->position++];
	if (!reader->cterm && first == '\r' && reader->position < reader->length
	    && reader->data[reader->position] == '\n')
		reader->position++;
	return true;
}

static int
decode_pbm(struct st_pixel_mask **result, struct pnm_reader *reader,
	bool raw, uint32_t width, uint32_t height, size_t count)
{
	struct st_pixel_mask *mask = NULL;
	int status;

	status = st_pixel_mask_create(&mask, width, height);
	if (status != 0)
		return status;
	if (raw) {
		size_t row_bytes = ((size_t)width + 7) / 8;

		if (row_bytes != 0
		    && height > (reader->length - reader->position) / row_bytes)
			status = IO_ERROR_INVALID_FORMAT;
		for (uint32_t y = 0; status == 0 && y < height; ++y) {
			for (size_t byte = 0; byte < row_bytes; ++byte) {
				uint8_t value = reader->data[reader->position++];

				for (unsigned bit = 0; bit < 8; ++bit) {
					uint32_t x = (uint32_t)(byte * 8 + bit);

					if (x >= width)
						break;
					if ((value & (UINT8_C(0x80) >> bit)) != 0)
						(void)st_pixel_mask_set(mask, x, y, true);
				}
			}
		}
	}
	else {
		for (size_t i = 0; status == 0 && i < count; ++i) {
			uintmax_t value;

			if (!pnm_read_number(reader, &value) || value > 1)
				status = IO_ERROR_INVALID_FORMAT;
			else if (value != 0)
				mask->bits[i / 8] |= (uint8_t)(UINT8_C(0x80)
				    >> (i % 8));
		}
	}
	if (status != 0) {
		st_pixel_mask_destroy(mask);
		return status;
	}
	*result = mask;
	return 0;
}

static int
decode_ppm(struct st_pixel_buffer **result, struct pnm_reader *reader,
	bool raw, uint32_t width, uint32_t height, size_t count,
	uintmax_t maximum)
{
	struct st_pixel_buffer *pixels = NULL;
	uint8_t *gamma;
	int status;

	gamma = malloc((size_t)maximum + 1);
	if (gamma == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	for (uintmax_t i = 0; i <= maximum; ++i)
		gamma[i] = bt709_to_srgb(i, maximum);
	status = st_pixel_buffer_create(&pixels, width, height, false);
	if (status != 0) {
		free(gamma);
		return status;
	}
	for (size_t pixel = 0; status == 0 && pixel < count; ++pixel) {
		uint32_t colour = ST_PIXEL_COLOR_RGB_TAG;

		for (unsigned component = 0; component < 3; ++component) {
			uintmax_t value;

			if (!raw) {
				if (!pnm_read_number(reader, &value)) {
					status = IO_ERROR_INVALID_FORMAT;
					break;
				}
			}
			else if (maximum < 256) {
				if (reader->position >= reader->length) {
					status = IO_ERROR_INVALID_FORMAT;
					break;
				}
				value = reader->data[reader->position++];
			}
			else {
				if (reader->length - reader->position < 2) {
					status = IO_ERROR_INVALID_FORMAT;
					break;
				}
				value = (uintmax_t)reader->data[reader->position] << 8;
				value |= reader->data[reader->position + 1];
				reader->position += 2;
			}
			if (value > maximum) {
				status = IO_ERROR_INVALID_FORMAT;
				break;
			}
			colour |= (uint32_t)gamma[value] << (16 - component * 8);
		}
		pixels->primary[pixel] = colour;
	}
	free(gamma);
	if (status != 0) {
		st_pixel_buffer_destroy(pixels);
		return status;
	}
	*result = pixels;
	return 0;
}

static int
decode_pnm(struct st_pixel_buffer **pixel_result,
	struct st_pixel_mask **mask_result, const void *data, size_t length,
	bool bitmap, bool cterm)
{
	struct pnm_reader reader = {.data = data, .length = length, .cterm = cterm};
	uintmax_t width;
	uintmax_t height;
	uintmax_t maximum = 1;
	size_t count;
	uint8_t kind;
	bool raw;

	if ((bitmap ? mask_result == NULL : pixel_result == NULL) || data == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (length < 2 || reader.data[0] != 'P')
		return IO_ERROR_INVALID_FORMAT;
	kind = reader.data[1];
	reader.position = 2;
	if ((bitmap && kind != '1' && kind != '4')
	    || (!bitmap && kind != '3' && kind != '6'))
		return IO_ERROR_INVALID_FORMAT;
	if (!pnm_read_number(&reader, &width)
	    || !pnm_read_number(&reader, &height)
	    || !pnm_dimensions(width, height, bitmap ? 1 : sizeof(uint32_t),
	    &count))
		return IO_ERROR_INVALID_FORMAT;
	if (!bitmap && (!pnm_read_number(&reader, &maximum) || maximum == 0
	    || maximum > (cterm ? UINT8_MAX : UINT16_MAX)))
		return IO_ERROR_INVALID_FORMAT;
	raw = kind == '4' || kind == '6';
	if (raw && !pnm_raw_separator(&reader))
		return IO_ERROR_INVALID_FORMAT;
	if (bitmap)
		return decode_pbm(mask_result, &reader, raw,
		    (uint32_t)width, (uint32_t)height, count);
	return decode_ppm(pixel_result, &reader, raw,
	    (uint32_t)width, (uint32_t)height, count, maximum);
}

int
st_pixel_image_decode_ppm(struct st_pixel_buffer **result, const void *data,
	size_t length)
{
	return decode_pnm(result, NULL, data, length, false, false);
}

int
st_pixel_image_decode_pbm(struct st_pixel_mask **result, const void *data,
	size_t length)
{
	return decode_pnm(NULL, result, data, length, true, false);
}

int
st_pixel_image_decode_cterm_ppm(struct st_pixel_buffer **result,
	const void *data, size_t length)
{
	return decode_pnm(result, NULL, data, length, false, true);
}

int
st_pixel_image_decode_cterm_pbm(struct st_pixel_mask **result,
	const void *data, size_t length)
{
	return decode_pnm(NULL, result, data, length, true, true);
}
