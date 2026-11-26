/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <string.h>

#include "io_error.h"
#include "st_rip3_rff.h"

static unsigned
word(const uint8_t *data)
{
	return (unsigned)data[0] | (unsigned)data[1] << 8;
}

static int
signed_word(const uint8_t *data)
{
	unsigned value = word(data);
	return value <= 32767 ? (int)value : -(int)(65535 - value) - 1;
}

static uint32_t
dword(const uint8_t *data)
{
	return (uint32_t)data[0] | (uint32_t)data[1] << 8 | (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
}

static bool
span(size_t size, size_t offset, size_t count)
{
	return offset <= size && count <= size - offset;
}

int
st_rip3_rff_open(struct st_rip3_rff *result, const void *bytes, size_t size)
{
	static const uint8_t signature[] = {0x10, 0, 2, 2, 0x2e, 0, 0x36, 0, 2, 4, 0x98, 0x44};
	const uint8_t       *data        = bytes;
	struct st_rip3_rff   file        = {0};
	size_t               section, advances, offsets;

	if (result == NULL || data == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (size < 54 || memcmp(data + 16, signature, sizeof(signature)) != 0 || word(data + 52) != 0x5426)
		return IO_ERROR_INVALID_FORMAT;
	file.data            = data;
	file.size            = size;
	file.style_offset    = word(data + 22);
	file.styles          = word(data + 42);
	file.units_per_em    = word(data + 44);
	file.characters      = word(data + 36);
	file.first_character = word(data + 38);
	file.trailer         = dword(data);
	if (file.styles == 0 || file.units_per_em == 0 || file.characters == 0 || file.first_character > 255
	    || file.characters > 256 - file.first_character || file.style_offset > size
	    || file.styles > (size - file.style_offset) / 46)
		return IO_ERROR_INVALID_FORMAT;
	section = file.style_offset + file.styles * 46;
	if (!span(size, section, 4) || word(data + section) != 1)
		return IO_ERROR_INVALID_FORMAT;
	file.base = word(data + section + 2);
	if (file.base < section + 4 || !span(size, file.base, 76) || word(data + file.base) != 0xf31e
	    || word(data + file.base + 2) != 0x100 || word(data + file.base + 12) != file.units_per_em
	    || word(data + file.base + 14) != file.first_character
	    || word(data + file.base + 16) != file.characters)
		return IO_ERROR_INVALID_FORMAT;
	file.bold_hollow_strength = data[file.base + 0x12];
	file.hollow_strength      = data[file.base + 0x13];
	file.xmin                 = signed_word(data + file.base + 0x16);
	file.ymin                 = signed_word(data + file.base + 0x18);
	file.xmax                 = signed_word(data + file.base + 0x1a);
	file.ymax                 = signed_word(data + file.base + 0x1c);
	file.cap_alignment        = signed_word(data + file.base + 0x42);
	file.ascent_alignment     = signed_word(data + file.base + 0x44);
	file.descent_alignment    = signed_word(data + file.base + 0x46);
	advances                  = word(data + file.base + 38);
	offsets                   = word(data + file.base + 46);
	if (advances > size - file.base || offsets > size - file.base)
		return IO_ERROR_INVALID_FORMAT;
	file.advances = file.base + advances;
	file.offsets  = file.base + offsets;
	if (!span(size, file.advances, (size_t)file.characters * 2)
	    || !span(size, file.offsets, (size_t)file.characters * 4))
		return IO_ERROR_INVALID_FORMAT;
	for (size_t i = 0; i < file.styles; ++i) {
		const uint8_t *style = data + file.style_offset + i * 46;
		if (memchr(style + 12, 0, 34) == NULL)
			return IO_ERROR_INVALID_FORMAT;
	}
	*result = file;
	return 0;
}

int
st_rip3_rff_style_at(struct st_rip3_rff_style *result, const struct st_rip3_rff *file, size_t index)
{
	const uint8_t *style;
	if (result == NULL || file == NULL || file->data == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (index >= file->styles)
		return IO_ERROR_NOT_FOUND;
	style   = file->data + file->style_offset + index * 46;
	*result = (struct st_rip3_rff_style) {
	    .name               = (const char *)style + 12,
	    .flags              = style[2],
	    .horizontal_percent = style[3],
	    .leading            = style[5] < 128 ? style[5] : (int)style[5] - 256,
	    .hollow             = style[4] != 0,
	};
	return 0;
}

static unsigned char
ascii_lower(unsigned char byte)
{
	return byte >= 'A' && byte <= 'Z' ? byte + ('a' - 'A') : byte;
}

static bool
same_name(const char *left, const char *right)
{
	while (*left != 0 && *right != 0) {
		if (ascii_lower((unsigned char)*left++) != ascii_lower((unsigned char)*right++))
			return false;
	}
	return *left == *right;
}

int
st_rip3_rff_find_style(struct st_rip3_rff_style *result, const struct st_rip3_rff *file, const char *name)
{
	if (result == NULL || file == NULL || file->data == NULL || name == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	for (size_t i = 0; i < file->styles; ++i) {
		struct st_rip3_rff_style style;
		int                      status = st_rip3_rff_style_at(&style, file, i);
		if (status != 0)
			return status;
		if (same_name(style.name, name)) {
			*result = style;
			return 0;
		}
	}
	return IO_ERROR_NOT_FOUND;
}

int
st_rip3_rff_path_next(struct st_rip3_rff_segment *result, const struct st_rip3_rff_glyph *glyph, size_t *cursor)
{
	struct st_rip3_rff_segment segment = {0};
	const uint8_t             *data;
	size_t                     size;

	if (result == NULL || glyph == NULL || cursor == NULL || *cursor > glyph->path_size
	    || (glyph->path_size != 0 && glyph->path == NULL))
		return IO_ERROR_INVALID_ARGUMENT;
	if (*cursor == glyph->path_size)
		return IO_ERROR_NOT_FOUND;
	data = glyph->path + *cursor;
	size = glyph->path_size - *cursor;
	if (data[0] > 2)
		return IO_ERROR_INVALID_FORMAT;
	segment.operation = (enum st_rip3_rff_operation)data[0];
	segment.points    = data[0] == 2 ? 3 : 1;
	if (size < 1 + 4 * segment.points)
		return IO_ERROR_INVALID_FORMAT;
	for (unsigned i = 0; i < segment.points; ++i) {
		segment.x[i] = signed_word(data + 1 + 4 * i);
		segment.y[i] = signed_word(data + 3 + 4 * i);
	}
	*cursor += 1 + 4 * segment.points;
	*result = segment;
	return 0;
}

static bool
read_glyph(struct st_rip3_rff_glyph *glyph, const struct st_rip3_rff *file, size_t index)
{
	size_t         relative = dword(file->data + file->offsets + index * 4);
	size_t         offset, path_size, tail_size, cursor = 0;
	const uint8_t *record;
	bool           positioned = false;

	if (relative > file->size - file->base)
		return false;
	offset = file->base + relative;
	if (!span(file->size, offset, 14))
		return false;
	record    = file->data + offset;
	path_size = word(record + 10);
	tail_size = word(record + 12);
	if (word(record) != 1 || !span(file->size - offset - 14, path_size, tail_size))
		return false;
	if (tail_size != 0) {
		const uint8_t *tail = record + 14 + path_size;
		if (tail_size < 6 || word(tail) != 2
		    || (size_t)word(tail + 2) + word(tail + 4) != (tail_size - 6) / 6 || (tail_size - 6) % 6 != 0)
			return false;
	}
	glyph->path      = path_size == 0 ? NULL : record + 14;
	glyph->path_size = path_size;
	while (cursor < path_size) {
		struct st_rip3_rff_segment segment;
		if (st_rip3_rff_path_next(&segment, glyph, &cursor) != 0)
			return false;
		if (segment.operation == ST_RIP3_RFF_MOVE)
			positioned = true;
		else if (!positioned)
			return false;
	}
	glyph->xmin = signed_word(record + 2);
	glyph->ymin = signed_word(record + 4);
	glyph->xmax = signed_word(record + 6);
	glyph->ymax = signed_word(record + 8);
	return true;
}

int
st_rip3_rff_advance(unsigned *result, const struct st_rip3_rff *file, uint8_t character)
{
	if (result == NULL || file == NULL || file->data == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (character < file->first_character || (unsigned)character - file->first_character >= file->characters)
		return IO_ERROR_NOT_FOUND;
	size_t index = (unsigned)character - file->first_character;
	if (file->advances > file->size || !span(file->size - file->advances, 2 * index, 2))
		return IO_ERROR_INVALID_FORMAT;
	*result = word(file->data + file->advances + 2 * index);
	return 0;
}

int
st_rip3_rff_glyph(struct st_rip3_rff_glyph *result, const struct st_rip3_rff *file, uint8_t character)
{
	struct st_rip3_rff_glyph glyph = {0};
	size_t                   index;

	if (result == NULL || file == NULL || file->data == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	int status = st_rip3_rff_advance(&glyph.advance, file, character);
	if (status != 0)
		return status;
	index = character - file->first_character;
	if (!read_glyph(&glyph, file, index)) {
		glyph = (struct st_rip3_rff_glyph) {.advance = glyph.advance, .damaged = true};
	}
	*result = glyph;
	return 0;
}

int
st_rip3_rff_kerning(int *result, const struct st_rip3_rff *file, uint8_t left, uint8_t right)
{
	const uint8_t *record;
	size_t         size, count;
	int            value = 0;

	if (result == NULL || file == NULL || file->data == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (!span(file->size, file->trailer, 10)) {
		*result = 0;
		return 0;
	}
	record = file->data + file->trailer;
	if (word(record) != 1 || word(record + 4) != 255 || word(record + 6) != 300) {
		*result = 0;
		return 0;
	}
	size  = word(record + 2);
	count = word(record + 8);
	if (size < 10 || !span(file->size, file->trailer, size) || count != (size - 10) / 4
	    || (size - 10) % 4 != 0)
		return IO_ERROR_INVALID_FORMAT;
	for (size_t i = 0; i < count; ++i) {
		const uint8_t *pair = record + 10 + 4 * i;
		if (pair[0] == left && pair[1] == right) {
			value = signed_word(pair + 2);
			break;
		}
	}
	*result = value;
	return 0;
}
