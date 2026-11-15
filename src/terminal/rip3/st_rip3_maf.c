/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_maf.h"
#include "io_error.h"

#include <string.h>

static unsigned
word(const uint8_t *data)
{
	return (unsigned)data[0] | (unsigned)data[1] << 8;
}

static uint32_t
dword(const uint8_t *data)
{
	return (uint32_t)data[0] | (uint32_t)data[1] << 8
	    | (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
}

static int
read_font(struct st_rip3_maf_font *result, const uint8_t *data,
	size_t size, size_t offset)
{
	struct st_rip3_maf_font font;

	if (offset < 60 || offset > size || size - offset < 50)
		return IO_ERROR_INVALID_FORMAT;
	font.width = word(data + offset);
	font.height = word(data + offset + 2);
	font.row_bytes = (font.width + 7U) / 8U;
	font.glyph_size = word(data + offset + 36);
	if (font.width == 0 || font.height == 0 || font.glyph_size == 0
	    || word(data + offset + 34) != 255
	    || font.row_bytes * font.height != font.glyph_size
	    || (size - offset - 50) / font.glyph_size < 255)
		return IO_ERROR_INVALID_FORMAT;
	font.glyphs = data + offset + 50;
	*result = font;
	return 0;
}

int
st_rip3_maf_open(struct st_rip3_maf *result, const void *bytes, size_t size)
{
	static const char magic[] = "\004 RIPterm v2.0 MicroANSI Font File \004\n\r\0\032";
	const uint8_t *data = bytes;
	struct st_rip3_maf file;

	if (result == NULL || data == NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (size < 60 || memcmp(data, magic, sizeof(magic) - 1) != 0)
		return IO_ERROR_INVALID_FORMAT;
	file = (struct st_rip3_maf){data, size, dword(data + 42), word(data + 40)};
	if (file.resolutions == 0 || file.directory < 60 || file.directory > size
	    || file.resolutions > (size - file.directory) / 60)
		return IO_ERROR_INVALID_FORMAT;
	for (size_t i = 0; i < file.resolutions; ++i) {
		const uint8_t *record = data + file.directory + 60 * i;
		if (word(record) == 0 || word(record + 2) == 0)
			return IO_ERROR_INVALID_FORMAT;
		for (size_t mode = 0; mode < 5; ++mode) {
			struct st_rip3_maf_font font;
			int status = read_font(&font, data, size, dword(record + 4 + 4 * mode));
			if (status != 0) return status;
		}
	}
	*result = file;
	return 0;
}

int
st_rip3_maf_find(struct st_rip3_maf_font *result,
	const struct st_rip3_maf *file, unsigned width, unsigned height,
	enum st_rip3_maf_mode mode)
{
	if (result == NULL || file == NULL || file->data == NULL
	    || (unsigned)mode > ST_RIP3_MAF_40X25)
		return IO_ERROR_INVALID_ARGUMENT;
	for (size_t i = 0; i < file->resolutions; ++i) {
		const uint8_t *record = file->data + file->directory + 60 * i;
		if (word(record) == width && word(record + 2) == height)
			return read_font(result, file->data, file->size,
			    dword(record + 4 + 4 * (unsigned)mode));
	}
	return IO_ERROR_NOT_FOUND;
}

const uint8_t *
st_rip3_maf_glyph(const struct st_rip3_maf_font *font, uint8_t character)
{
	if (font == NULL || font->glyphs == NULL || character == 0) return NULL;
	return font->glyphs + (size_t)(character - 1) * font->glyph_size;
}
