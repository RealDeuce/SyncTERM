/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_igs_image.h"
#include "io_error.h"

#include <stdbool.h>
#include <stdlib.h>

static bool
valid_planes(unsigned planes)
{
	return planes == 1 || planes == 2 || planes == 4;
}

static int
validate(const struct st_igs_image *image, size_t offset, size_t count)
{
	if (image == NULL || !valid_planes(image->planes) || image->pixels == NULL
	    || image->pixels->primary == NULL || image->pixels->alternate != NULL
	    || image->pixels->height != 1
	    || image->pixels->width != (ST_IGS_IMAGE_BYTES * 8u) / image->planes)
		return IO_ERROR_INVALID_ARGUMENT;
	if (offset > ST_IGS_IMAGE_BYTES || count > ST_IGS_IMAGE_BYTES - offset)
		return IO_ERROR_OUT_OF_RANGE;
	/* Reject foreign palette/RGB values before changing any output. Native
	 * byte positions address a plane and eight indices in a 16-pixel group. */
	for (size_t i = offset; i < offset + count; ++i) {
		size_t first = (i / (2 * image->planes)) * 16 + (i % 2) * 8;
		for (size_t bit = 0; bit < 8; ++bit) {
			if (image->pixels->primary[first + bit] >= (1u << image->planes))
				return IO_ERROR_INVALID_FORMAT;
		}
	}
	return 0;
}

int
st_igs_image_create(struct st_igs_image **result, unsigned planes)
{
	struct st_igs_image *image;
	int status;
	if (result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	*result = NULL;
	if (!valid_planes(planes))
		return IO_ERROR_INVALID_ARGUMENT;
	image = calloc(1, sizeof(*image));
	if (image == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	status = st_pixel_buffer_create(&image->pixels,
	    (ST_IGS_IMAGE_BYTES * 8u) / planes, 1, false);
	if (status != 0) {
		free(image);
		return status;
	}
	image->planes = planes;
	*result = image;
	return 0;
}

void
st_igs_image_destroy(struct st_igs_image *image)
{
	if (image == NULL)
		return;
	st_pixel_buffer_destroy(image->pixels);
	free(image);
}

int
st_igs_image_import(struct st_igs_image *image, size_t offset,
    const uint8_t *bytes, size_t count)
{
	int status;
	if (bytes == NULL && count != 0)
		return IO_ERROR_INVALID_ARGUMENT;
	status = validate(image, offset, count);
	if (status != 0)
		return status;
	for (size_t i = 0; i < count; ++i) {
		size_t native = offset + i;
		size_t first = (native / (2 * image->planes)) * 16 + (native % 2) * 8;
		unsigned plane = (unsigned)((native / 2) % image->planes);
		uint32_t mask = UINT32_C(1) << plane;
		for (unsigned bit = 0; bit < 8; ++bit) {
			uint32_t *pixel = &image->pixels->primary[first + bit];
			*pixel = (*pixel & ~mask) | (((bytes[i] >> (7 - bit)) & 1u) << plane);
		}
	}
	return 0;
}

int
st_igs_image_export(const struct st_igs_image *image, size_t offset,
    uint8_t *bytes, size_t count)
{
	int status;
	if (bytes == NULL && count != 0)
		return IO_ERROR_INVALID_ARGUMENT;
	status = validate(image, offset, count);
	if (status != 0)
		return status;
	for (size_t i = 0; i < count; ++i) {
		size_t native = offset + i;
		size_t first = (native / (2 * image->planes)) * 16 + (native % 2) * 8;
		unsigned plane = (unsigned)((native / 2) % image->planes);
		unsigned byte = 0;
		for (unsigned bit = 0; bit < 8; ++bit)
			byte |= ((image->pixels->primary[first + bit] >> plane) & 1u) << (7 - bit);
		bytes[i] = (uint8_t)byte;
	}
	return 0;
}

int
st_igs_image_replane(struct st_igs_image *image, unsigned planes)
{
	struct st_igs_image next = {0};
	uint8_t *native;
	int status;
	if (!valid_planes(planes))
		return IO_ERROR_INVALID_ARGUMENT;
	status = validate(image, 0, 0);
	if (status != 0)
		return status;
	if (planes == image->planes)
		return 0;
	native = malloc(ST_IGS_IMAGE_BYTES);
	if (native == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	status = st_igs_image_export(image, 0, native, ST_IGS_IMAGE_BYTES);
	if (status == 0)
		status = st_pixel_buffer_create(&next.pixels,
		    (ST_IGS_IMAGE_BYTES * 8u) / planes, 1, false);
	next.planes = planes;
	if (status == 0)
		status = st_igs_image_import(&next, 0, native, ST_IGS_IMAGE_BYTES);
	free(native);
	if (status != 0) {
		st_pixel_buffer_destroy(next.pixels);
		return status;
	}
	st_pixel_buffer_destroy(image->pixels);
	*image = next;
	return 0;
}
