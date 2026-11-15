/* Literal MAF directory/glyph expectations plus malformed-file boundaries. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_maf.h"
#include "st_rip3_font_data.h"
#include "io_error.h"

#include <stdio.h>
#include <string.h>

#define CHECK(test) do { if (!(test)) { \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test); return 1; \
} } while (0)

static void
put16(uint8_t *data, unsigned value)
{
	data[0] = (uint8_t)value;
	data[1] = (uint8_t)(value >> 8);
}

static void
put32(uint8_t *data, uint32_t value)
{
	put16(data, value & 65535);
	put16(data + 2, value >> 16);
}

static int
test_bounds(void)
{
	/* The valid directory is not the plausible-looking snapshot at 0x3c. */
	uint8_t bytes[128 + 60 + 5 * (50 + 255)] = {0};
	uint8_t bad[sizeof(bytes)];
	struct st_rip3_maf file = {0}, unchanged;
	struct st_rip3_maf_font font = {0}, old_font;
	static const char magic[] = "\004 RIPterm v2.0 MicroANSI Font File \004\n\r\0\032";

	memcpy(bytes, magic, sizeof(magic) - 1);
	put16(bytes + 40, 1);
	put32(bytes + 42, 128);
	put16(bytes + 128, 640);
	put16(bytes + 130, 480);
	for (unsigned mode = 0; mode < 5; ++mode) {
		unsigned offset = 188 + mode * 305;
		put32(bytes + 132 + mode * 4, offset);
		put16(bytes + offset, 8);
		put16(bytes + offset + 2, 1);
		put16(bytes + offset + 34, 255);
		put16(bytes + offset + 36, 1);
		for (unsigned character = 1; character < 256; ++character)
			bytes[offset + 50 + character - 1] = (uint8_t)character;
	}
	CHECK(st_rip3_maf_open(&file, bytes, sizeof(bytes)) == 0);
	CHECK(file.directory == 128 && file.resolutions == 1);
	unchanged = file;
	for (size_t size = 0; size < sizeof(bytes); ++size) {
		CHECK(st_rip3_maf_open(&file, bytes, size) == IO_ERROR_INVALID_FORMAT);
		CHECK(file.data == unchanged.data && file.size == unchanged.size
		    && file.directory == unchanged.directory && file.resolutions == unchanged.resolutions);
	}
	for (unsigned mode = 0; mode < 5; ++mode) {
		CHECK(st_rip3_maf_find(&font, &file, 640, 480, (enum st_rip3_maf_mode)mode) == 0);
		CHECK(font.width == 8 && font.height == 1 && font.row_bytes == 1 && font.glyph_size == 1);
		CHECK(st_rip3_maf_glyph(&font, 0) == NULL);
		for (unsigned character = 1; character < 256; ++character)
			CHECK(*st_rip3_maf_glyph(&font, (uint8_t)character) == character);
	}
	old_font = font;
	CHECK(st_rip3_maf_find(&font, &file, 640, 350, ST_RIP3_MAF_80X25) == IO_ERROR_NOT_FOUND);
	CHECK(st_rip3_maf_find(&font, &file, 640, 480, (enum st_rip3_maf_mode)-1) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_maf_find(&font, &file, 640, 480, (enum st_rip3_maf_mode)5) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(font.glyphs == old_font.glyphs && font.width == old_font.width);
	CHECK(st_rip3_maf_open(NULL, bytes, sizeof(bytes)) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_maf_open(&file, NULL, sizeof(bytes)) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_maf_find(NULL, &file, 640, 480, ST_RIP3_MAF_80X25) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_maf_find(&font, NULL, 640, 480, ST_RIP3_MAF_80X25) == IO_ERROR_INVALID_ARGUMENT);
	{
		static const struct { unsigned offset; uint32_t value; unsigned width; } corruptions[] = {
			{0, 0, 2}, {40, 0, 2}, {40, 65535, 2},
			{42, 0, 4}, {42, UINT32_MAX, 4},
			{128, 0, 2}, {130, 0, 2}, {132, UINT32_MAX, 4},
			{132, 1, 4}, {188, 0, 2}, {190, 0, 2},
			{188, 16, 2}, {222, 254, 2}, {224, 0, 2}, {224, 65535, 2},
		};
		for (size_t i = 0; i < sizeof(corruptions) / sizeof(*corruptions); ++i) {
			memcpy(bad, bytes, sizeof(bytes));
			if (corruptions[i].width == 2)
				put16(bad + corruptions[i].offset, (unsigned)corruptions[i].value);
			else
				put32(bad + corruptions[i].offset, corruptions[i].value);
			CHECK(st_rip3_maf_open(&file, bad, sizeof(bad)) == IO_ERROR_INVALID_FORMAT);
			CHECK(file.data == unchanged.data && file.directory == 128);
		}
	}
	return 0;
}

int
main(void)
{
	static const size_t sizes[] = {87155, 62725, 31596, 42071, 70789, 56526,
		64213, 43601, 270945, 16677, 5151, 13596, 18063, 10987, 8437,
		17355, 12079, 8439, 14670};
	static const unsigned dimensions[6][2] = {
		{640,480}, {800,600}, {1024,768}, {639,479}, {799,599}, {1023,767},
	};
	static const size_t first_offsets[] = {0x1a4, 0x643c, 0x11b7f, 0x21f75, 0x27c13, 0x31e6b};
	static const unsigned cells[6][5][2] = {
		{{8,11},{7,11},{8,19},{7,19},{16,19}},
		{{10,13},{8,13},{10,24},{8,24},{20,24}},
		{{12,17},{11,17},{12,30},{11,30},{20,24}},
		{{7,10},{6,10},{7,18},{6,18},{14,18}},
		{{9,12},{8,12},{9,21},{8,21},{18,21}},
		{{12,16},{11,16},{12,28},{11,28},{24,28}},
	};
	static const uint8_t smiley[] = {0x7e,0x81,0xa5,0xa5,0x81,0x81,0xbd,0x99,0x81,0x81,0x7e};
	struct st_rip3_maf file;
	struct st_rip3_maf_font font;
	const struct st_rip3_font_asset *asset;

	CHECK(st_rip3_font_asset_count() == 19);
	CHECK(st_rip3_font_asset_at(19) == NULL && st_rip3_font_asset_at(SIZE_MAX) == NULL);
	for (size_t i = 0; i < sizeof(sizes) / sizeof(*sizes); ++i) {
		asset = st_rip3_font_asset_at(i);
		CHECK(asset != NULL && asset->size == sizes[i]);
		CHECK(memcmp(asset->data, "version https:", 14) != 0);
	}
	asset = st_rip3_font_asset_at(8);
	CHECK(strcmp(asset->filename, "RIPscrip.maf") == 0);
	CHECK(st_rip3_maf_open(&file, asset->data, asset->size) == 0);
	CHECK(file.directory == 60 && file.resolutions == 6);
	for (size_t r = 0; r < 6; ++r) {
		for (unsigned mode = 0; mode < 5; ++mode) {
			CHECK(st_rip3_maf_find(&font, &file, dimensions[r][0], dimensions[r][1],
			    (enum st_rip3_maf_mode)mode) == 0);
			CHECK(font.width == cells[r][mode][0] && font.height == cells[r][mode][1]);
			CHECK(font.row_bytes == (font.width + 7U) / 8U);
			if (mode == 0) CHECK(font.glyphs == asset->data + first_offsets[r] + 50);
			CHECK(st_rip3_maf_glyph(&font, 0) == NULL);
			CHECK(st_rip3_maf_glyph(&font, 255) + font.glyph_size <= asset->data + asset->size);
		}
	}
	CHECK(st_rip3_maf_find(&font, &file, 640, 480, ST_RIP3_MAF_80X43) == 0);
	CHECK(memcmp(st_rip3_maf_glyph(&font, 1), smiley, sizeof(smiley)) == 0);
	CHECK(st_rip3_maf_find(&font, &file, 640, 350, ST_RIP3_MAF_80X25) == IO_ERROR_NOT_FOUND);
	CHECK(test_bounds() == 0);
	return 0;
}
