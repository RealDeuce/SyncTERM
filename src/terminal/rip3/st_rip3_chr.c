/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_chr.h"

#include "st_rip3_font_data.h"
#include "io_error.h"

#include <string.h>

static unsigned
word(const uint8_t *data)
{
	return (unsigned)data[0] | (unsigned)data[1] << 8;
}

static int
signed_byte(uint8_t value)
{
	return value < 128 ? value : (int)value - 256;
}

static bool
view_valid(const struct st_rip3_chr *font)
{
	return font->body != NULL && font->size >= 16 && font->size <= UINT16_MAX
	    && font->characters != 0 && font->characters <= 256
	    && font->first_character < 256
	    && font->characters <= 256 - font->first_character
	    && font->strokes >= 16 + 3 * font->characters
	    && font->strokes <= font->size;
}

static int
glyph_at(struct st_rip3_chr_glyph *result,
    const struct st_rip3_chr *font, size_t index)
{
	struct st_rip3_chr_glyph glyph = {0};
	size_t offset = word(font->body + 16 + 2 * index);
	size_t start, position;

	if (offset > font->size - font->strokes)
		return IO_ERROR_INVALID_FORMAT;
	start = font->strokes + offset;
	for (position = start; position < font->size && font->size - position >= 2; position += 2) {
		unsigned opcode = 2U * (font->body[position] >> 7)
		    + (font->body[position + 1] >> 7);

		if (opcode == 0) {
			glyph.program = font->body + start;
			glyph.program_size = position - start;
			glyph.advance = font->body[16 + 2 * font->characters + index];
			*result = glyph;
			return 0;
		}
		if (opcode == 1)
			glyph.has_scan = true;
	}
	return IO_ERROR_INVALID_FORMAT;
}

int
st_rip3_chr_open(struct st_rip3_chr *result, const void *bytes, size_t size)
{
	static const uint8_t signature[] = {'P','K',8,8};
	const uint8_t *data = bytes;
	struct st_rip3_chr font = {0};
	size_t eof, body, declared;

	if (result == NULL || data == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (size < sizeof(signature) || memcmp(data, signature, sizeof(signature)) != 0)
		return IO_ERROR_INVALID_FORMAT;
	/* A 16-bit header offset cannot address a text prefix beyond this bound. */
	for (eof = 4; eof < size && eof <= UINT16_MAX - 13U; eof++) {
		if (data[eof] == 0x1a)
			break;
	}
	if (eof >= size || size - eof < 13 || data[eof] != 0x1a)
		return IO_ERROR_INVALID_FORMAT;
	body = word(data + eof + 1);
	declared = word(data + eof + 7);
	if (body < eof + 13 || body > size || declared != size - body || declared < 16)
		return IO_ERROR_INVALID_FORMAT;
	if (data[eof + 9] != 1 || data[eof + 10] != 0
	    || data[eof + 11] > 1 || (data[eof + 11] == 1 && data[eof + 12] != 0))
		return IO_ERROR_NOT_SUPPORTED;
	font.body = data + body;
	font.size = declared;
	if (font.body[0] != '+')
		return IO_ERROR_INVALID_FORMAT;
	font.characters = word(font.body + 1);
	font.first_character = font.body[4];
	font.strokes = word(font.body + 5);
	font.scan_flag = font.body[7];
	font.capital = signed_byte(font.body[8]);
	font.baseline = signed_byte(font.body[9]);
	font.descender = signed_byte(font.body[10]);
	memcpy(font.name, data + eof + 3, 4);
	if (!view_valid(&font))
		return IO_ERROR_INVALID_FORMAT;
	for (size_t i = 0; i < font.characters; i++) {
		struct st_rip3_chr_glyph glyph;
		int status = glyph_at(&glyph, &font, i);

		if (status != 0)
			return status;
	}
	*result = font;
	return 0;
}

int
st_rip3_chr_builtin(struct st_rip3_chr *result, unsigned font_number)
{
	static const char *const names[] = {"TRIP.CHR","LITT.CHR","SANS.CHR",
	    "GOTH.CHR","SCRI.CHR","SIMP.CHR","TSCR.CHR","LCOM.CHR","EURO.CHR","BOLD.CHR"};

	if (result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (font_number == 0 || font_number > 10)
		return IO_ERROR_OUT_OF_RANGE;
	for (size_t i = 0; i < st_rip3_font_asset_count(); i++) {
		const struct st_rip3_font_asset *asset = st_rip3_font_asset_at(i);

		if (strcmp(asset->filename, names[font_number - 1]) == 0)
			return st_rip3_chr_open(result, asset->data, asset->size);
	}
	return IO_ERROR_NOT_FOUND;
}

int
st_rip3_chr_glyph(struct st_rip3_chr_glyph *result,
    const struct st_rip3_chr *font, uint8_t character)
{
	if (result == NULL || font == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (!view_valid(font))
		return IO_ERROR_INVALID_FORMAT;
	if (character < font->first_character
	    || (size_t)(character - font->first_character) >= font->characters)
		return IO_ERROR_NOT_FOUND;
	return glyph_at(result, font, character - font->first_character);
}

int
st_rip3_chr_next(struct st_rip3_chr_record *result,
    const struct st_rip3_chr_glyph *glyph, size_t *cursor)
{
	struct st_rip3_chr_record record;
	const uint8_t *data;
	unsigned opcode;

	if (result == NULL || glyph == NULL || cursor == NULL || glyph->program == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (*cursor > glyph->program_size || (*cursor & 1) != 0)
		return IO_ERROR_OUT_OF_RANGE;
	if (*cursor == glyph->program_size)
		return IO_ERROR_NOT_FOUND;
	if (glyph->program_size - *cursor < 2)
		return IO_ERROR_INVALID_FORMAT;
	data = glyph->program + *cursor;
	opcode = 2U * (data[0] >> 7) + (data[1] >> 7);
	if (opcode == 0)
		return IO_ERROR_INVALID_FORMAT;
	record.operation = opcode == 1 ? ST_RIP3_CHR_SCAN
	    : opcode == 2 ? ST_RIP3_CHR_MOVE : ST_RIP3_CHR_DRAW;
	record.x = (int)(data[0] & 63) - (int)(data[0] & 64);
	record.y = (int)(data[1] & 63) - (int)(data[1] & 64);
	*result = record;
	*cursor += 2;
	return 0;
}
