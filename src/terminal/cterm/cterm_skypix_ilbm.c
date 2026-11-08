/* Confined SkyPix brush payload: planar IFF ILBM with ByteRun1. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_skypix.h"
#include "io_error.h"
#include <stdlib.h>
#include <string.h>
static unsigned be16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }
static uint32_t be32(const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
int cterm_skypix_receive(struct cterm_skypix *s, const void *bytes, size_t size)
{
	s->receiving = false;
	if (bytes == NULL && size == 0)
		return 0;
	const uint8_t *input = bytes, *header = NULL, *body = NULL;
	size_t body_size = 0;
	const uint8_t *cmap = NULL;
	size_t cmap_size = 0;
	if (input == NULL || size < 12 || memcmp(input, "FORM", 4) != 0 ||
		memcmp(input + 8, "ILBM", 4) != 0)
		return IO_ERROR_INVALID_FORMAT;
	uint32_t declared = be32(input + 4);
	if (declared < 4 || declared > size - 8)
		return IO_ERROR_INVALID_FORMAT;
	size_t end = (size_t)declared + 8;
	for (size_t offset = 12; offset < end;) {
		if (end - offset < 8)
			return IO_ERROR_INVALID_FORMAT;
		size_t length = be32(input + offset + 4);
		if (length > end - offset - 8)
			return IO_ERROR_INVALID_FORMAT;
		if (memcmp(input + offset, "BMHD", 4) == 0) {
			if (header != NULL || length != 20)
				return IO_ERROR_INVALID_FORMAT;
			header = input + offset + 8;
		}
		if (memcmp(input + offset, "CMAP", 4) == 0) {
			if (cmap != NULL || body != NULL || length == 0 || length % 3 != 0 || length > 48)
				return IO_ERROR_INVALID_FORMAT;
			cmap = input + offset + 8;
			cmap_size = length / 3;
		}
		if (memcmp(input + offset, "BODY", 4) == 0) {
			if (cmap == NULL || body != NULL)
				return IO_ERROR_INVALID_FORMAT;
			body = input + offset + 8;
			body_size = length;
		}
		size_t next = offset + 8 + length;
		if ((length & 1) != 0) {
			if (next == end)
				return IO_ERROR_INVALID_FORMAT;
			++next;
		}
		offset = next;
	}
	if (header == NULL || body == NULL)
		return IO_ERROR_INVALID_FORMAT;
	unsigned width = be16(header), height = be16(header + 2), planes = header[8], mask = header[9],
			 compression = header[10];
	if (width == 0 || height == 0 || (uint64_t)width * height > 4 * 1024 * 1024 || planes == 0 ||
		planes > 4 || mask != 0 || compression > 1)
		return IO_ERROR_NOT_SUPPORTED;
	size_t stride = ((size_t)width + 15) / 16 * 2;
	size_t row_size = stride * (planes + (mask == 1 ? 1u : 0u));
	uint8_t *row = malloc(row_size);
	if (row == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	struct st_pixel_buffer *brush = NULL;
	int status = st_pixel_buffer_create(&brush, width, height, false);
	size_t offset = 0;
	for (unsigned y = 0; y < height && status == 0; ++y) {
		for (unsigned plane = 0; plane < planes + (mask == 1 ? 1u : 0u) && status == 0; ++plane) {
			size_t used = 0;
			while (used < stride) {
				if (offset == body_size) {
					status = IO_ERROR_INVALID_FORMAT;
					break;
				}
				unsigned control = body[offset++];
				if (compression == 0) {
					row[(size_t)plane * stride + used++] = (uint8_t)control;
					continue;
				}
				if (control == 128) continue;
				size_t count = control < 128 ? control + 1 : 257 - control;
				if (count > stride - used || (control < 128 ? count : 1) > body_size - offset) {
					status = IO_ERROR_INVALID_FORMAT;
					break;
				}
				if (control < 128) {
					memcpy(row + (size_t)plane * stride + used, body + offset, count);
					offset += count;
				} else
					memset(row + (size_t)plane * stride + used, body[offset++], count);
				used += count;
			}
		}
		if (status != 0)
			break;
		for (unsigned x = 0; x < width; ++x) {
			unsigned value = 0;
			for (unsigned plane = 0; plane < planes; ++plane)
				if (row[(size_t)plane * stride + x / 8] & (0x80u >> (x % 8)))
					value |= 1u << plane;
			brush->primary[(size_t)y * width + x] = value;
		}
	}
	free(row);
	if (status == 0) {
		st_pixel_buffer_destroy(s->brush);
		s->brush = brush;
		s->brush_depth = planes;
		for (size_t i = 0; i < cmap_size; ++i)
			s->brush_palette[i] = ((uint32_t)(cmap[i * 3] >> 4) * 17 << 16)
				| ((uint32_t)(cmap[i * 3 + 1] >> 4) * 17 << 8)
				| ((uint32_t)(cmap[i * 3 + 2] >> 4) * 17);
		s->brush_palette_size = cmap_size;
		s->brush_palette_pending = true;
	} else
		st_pixel_buffer_destroy(brush);
	return status;
}
