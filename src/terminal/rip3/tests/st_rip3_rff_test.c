/* Literal FastFont records, aliases, framing, and shipped glyph corruption. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_rff.h"
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
test_records(void)
{
	uint8_t bytes[814] = {0}, bad[sizeof(bytes)];
	static const uint8_t signature[] = {0x10,0,2,2,0x2e,0,0x36,0,2,4,0x98,0x44};
	static const uint8_t path[] = {
		0, 0,0, 0xf6,0xff,                 /* move (0,-10) */
		1, 4,0, 0xf6,0xff,                 /* line (4,-10) */
		2, 6,0, 0xf8,0xff, 8,0, 0xfc,0xff, 4,0, 0,0, /* curve */
		1, 0,0, 0xf6,0xff,
	};
	struct st_rip3_rff file = {0}, unchanged;
	struct st_rip3_rff_glyph glyph, alias;
	struct st_rip3_rff_style style;
	struct st_rip3_rff_segment segment;
	size_t cursor = 0;
	int kerning = 77;

	memcpy(bytes + 16, signature, sizeof(signature));
	put32(bytes, 800);
	put16(bytes + 28, 250); put16(bytes + 30, 1000);
	put16(bytes + 32, 65536 - 921); put16(bytes + 34, 65536 - 44);
	put16(bytes + 36, 3); put16(bytes + 38, 32);
	put16(bytes + 42, 1); put16(bytes + 44, 1000);
	put16(bytes + 52, 0x5426);
	bytes[57] = 100;
	memset(bytes + 66, 0xa5, 34);
	memcpy(bytes + 66, "Test\0hidden", 11);
	put16(bytes + 100, 1); put16(bytes + 102, 116);
	put16(bytes + 116, 0xf31e); put16(bytes + 118, 0x100);
	put16(bytes + 128, 1000); put16(bytes + 130, 32); put16(bytes + 132, 3);
	put16(bytes + 138, 65536 - 44); put16(bytes + 140, 65536 - 921);
	put16(bytes + 142, 1000); put16(bytes + 144, 250);
	put16(bytes + 154, 0x4c); put16(bytes + 162, 0x20c);
	put16(bytes + 192, 277); put16(bytes + 194, 666); put16(bytes + 196, 666);
	put32(bytes + 640, 700 - 116); put32(bytes + 644, 720 - 116);
	put32(bytes + 648, 720 - 116); /* alias, not a following-record delimiter */
	put16(bytes + 700, 1);
	put16(bytes + 720, 1); put16(bytes + 724, 65536 - 10); put16(bytes + 726, 8);
	put16(bytes + 730, sizeof(path)); put16(bytes + 732, 12);
	memcpy(bytes + 734, path, sizeof(path));
	put16(bytes + 734 + sizeof(path), 2); /* tail marker is not a curve */
	put16(bytes + 736 + sizeof(path), 1); /* one X hint */
	put16(bytes + 800, 1); put16(bytes + 802, 14);
	put16(bytes + 804, 255); put16(bytes + 806, 300); put16(bytes + 808, 1);
	bytes[810] = '!'; bytes[811] = '"'; put16(bytes + 812, 65536 - 50);
	CHECK(st_rip3_rff_open(&file, bytes, sizeof(bytes)) == 0);
	unchanged = file;
	CHECK(file.characters == 3 && file.units_per_em == 1000
	    && file.xmin == -44 && file.ymin == -921 && file.xmax == 1000 && file.ymax == 250);
	CHECK(st_rip3_rff_find_style(&style, &file, "tEsT") == 0);
	CHECK(strcmp(style.name, "Test") == 0 && style.horizontal_percent == 100);
	CHECK(st_rip3_rff_find_style(&style, &file, "Testhidden") == IO_ERROR_NOT_FOUND);
	CHECK(st_rip3_rff_glyph(&glyph, &file, 32) == 0);
	CHECK(!glyph.damaged && glyph.path == NULL && glyph.path_size == 0 && glyph.advance == 277);
	CHECK(st_rip3_rff_glyph(&glyph, &file, '!') == 0);
	CHECK(!glyph.damaged && glyph.advance == 666 && glyph.xmin == 0
	    && glyph.ymin == -10 && glyph.xmax == 8 && glyph.ymax == 0);
	CHECK(st_rip3_rff_glyph(&alias, &file, '"') == 0);
	CHECK(alias.path == glyph.path && alias.path_size == sizeof(path));
	CHECK(st_rip3_rff_path_next(&segment, &glyph, &cursor) == 0);
	CHECK(segment.operation == ST_RIP3_RFF_MOVE && segment.points == 1
	    && segment.x[0] == 0 && segment.y[0] == -10 && cursor == 5);
	CHECK(st_rip3_rff_path_next(&segment, &glyph, &cursor) == 0);
	CHECK(segment.operation == ST_RIP3_RFF_LINE && segment.x[0] == 4 && segment.y[0] == -10);
	CHECK(st_rip3_rff_path_next(&segment, &glyph, &cursor) == 0);
	CHECK(segment.operation == ST_RIP3_RFF_CURVE && segment.points == 3
	    && segment.x[0] == 6 && segment.y[0] == -8 && segment.x[1] == 8
	    && segment.y[1] == -4 && segment.x[2] == 4 && segment.y[2] == 0);
	CHECK(st_rip3_rff_path_next(&segment, &glyph, &cursor) == 0);
	CHECK(st_rip3_rff_path_next(&segment, &glyph, &cursor) == IO_ERROR_NOT_FOUND && cursor == sizeof(path));
	CHECK(st_rip3_rff_kerning(&kerning, &file, '!', '"') == 0 && kerning == -50);
	CHECK(st_rip3_rff_kerning(&kerning, &file, '!', '!') == 0 && kerning == 0);
	CHECK(st_rip3_rff_glyph(&alias, &file, 31) == IO_ERROR_NOT_FOUND);
	CHECK(alias.path == glyph.path);
	for (size_t size = 0; size < 652; ++size) {
		CHECK(st_rip3_rff_open(&file, bytes, size) == IO_ERROR_INVALID_FORMAT);
		CHECK(file.data == unchanged.data && file.size == unchanged.size);
	}
	{
		static const struct { unsigned offset; uint32_t value; unsigned width; } corruptions[] = {
			{16,0,2}, {52,0,2}, {42,0,2}, {44,0,2}, {36,0,2}, {38,255,2},
			{100,0,2}, {102,0,2}, {116,0,2}, {118,0,2}, {128,1,2},
			{154,65535,2}, {162,65535,2},
		};
		for (size_t i = 0; i < sizeof(corruptions) / sizeof(*corruptions); ++i) {
			memcpy(bad, bytes, sizeof(bytes));
			if (corruptions[i].width == 2)
				put16(bad + corruptions[i].offset, (unsigned)corruptions[i].value);
			else put32(bad + corruptions[i].offset, corruptions[i].value);
			CHECK(st_rip3_rff_open(&file, bad, sizeof(bad)) == IO_ERROR_INVALID_FORMAT);
			CHECK(file.data == unchanged.data);
		}
	}
	memcpy(bad, bytes, sizeof(bytes));
	memset(bad + 66, 'X', 34);
	CHECK(st_rip3_rff_open(&file, bad, sizeof(bad)) == IO_ERROR_INVALID_FORMAT);
	for (unsigned problem = 0; problem < 5; ++problem) {
		memcpy(bad, bytes, sizeof(bytes));
		if (problem == 0) put32(bad + 644, UINT32_MAX);
		if (problem == 1) put16(bad + 730, 65535);
		if (problem == 2) bad[734] = 3;
		if (problem == 3) bad[734] = 1; /* line without a current contour */
		if (problem == 4) put16(bad + 736 + sizeof(path), 65535);
		CHECK(st_rip3_rff_open(&file, bad, sizeof(bad)) == 0);
		CHECK(st_rip3_rff_glyph(&alias, &file, '!') == 0 && alias.damaged
		    && alias.path == NULL && alias.advance == 666);
	}
	CHECK(st_rip3_rff_open(&file, bytes, 740) == 0);
	CHECK(st_rip3_rff_glyph(&alias, &file, '!') == 0 && alias.damaged);
	memcpy(bad, bytes, sizeof(bytes));
	put16(bad + 808, 65535);
	CHECK(st_rip3_rff_open(&file, bad, sizeof(bad)) == 0);
	kerning = 77;
	CHECK(st_rip3_rff_kerning(&kerning, &file, '!', '"') == IO_ERROR_INVALID_FORMAT && kerning == 77);
	{
		static const uint8_t extremes[] = {1, 0,0x80, 0xff,0x7f};
		struct st_rip3_rff_glyph fragment = {.path = extremes, .path_size = sizeof(extremes)};
		size_t position = 0;
		CHECK(st_rip3_rff_path_next(&segment, &fragment, &position) == 0);
		CHECK(segment.x[0] == -32768 && segment.y[0] == 32767);
		fragment.path_size = 4;
		position = 0;
		CHECK(st_rip3_rff_path_next(&segment, &fragment, &position) == IO_ERROR_INVALID_FORMAT);
		CHECK(position == 0 && segment.x[0] == -32768 && segment.y[0] == 32767);
		position = 5;
		CHECK(st_rip3_rff_path_next(&segment, &fragment, &position) == IO_ERROR_INVALID_ARGUMENT);
		CHECK(position == 5);
	}
	CHECK(st_rip3_rff_open(NULL, bytes, sizeof(bytes)) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_rff_open(&file, NULL, sizeof(bytes)) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_rff_style_at(NULL, &file, 0) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_rff_find_style(&style, &file, NULL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_rff_glyph(&alias, NULL, '!') == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_rff_kerning(NULL, &file, '!', '!') == IO_ERROR_INVALID_ARGUMENT);
	/* Every coordinate, including both upper bounds, is signed. The original
	 * consumer takes this rectangle from the metrics block, not header labels. */
	for (unsigned axis = 0; axis < 4; axis++) {
		static const unsigned header[] = {34,32,30,28};
		static const int values[] = {-32768,-1,32767};
		for (size_t i = 0; i < sizeof(values)/sizeof(values[0]); i++) {
			memcpy(bad,bytes,sizeof(bytes));
			put16(bad+116+0x16+2*axis,(unsigned)values[i]&65535);
			put16(bad+header[axis],1234); /* Ignored duplicate, deliberately different. */
			CHECK(st_rip3_rff_open(&file,bad,sizeof(bad)) == 0);
			const int bounds[] = {file.xmin,file.ymin,file.xmax,file.ymax};
			CHECK(bounds[axis] == values[i]);
		}
	}
	return 0;
}

int
main(void)
{
	static const char *const families[] = {"Brush", "Cobb", "DEFAULT", "Dixon", "Eureka", "Marin", "Oakland", "Symbol"};
	static const unsigned flags[] = {0,1,2,4,8,16,17,18,20,24};
	static const unsigned percent[] = {100,85,75,125,150,100,85,75,125,150};
	static const int bounds[][4] = {
		{-159,-767,1184,305},{-32,-775,613,274},{-2,-750,500,250},{-44,-921,1000,250},
		{-36,-952,1093,358},{-78,-916,1059,250},{-127,-894,1249,250},{-133,-1110,1095,328},
	};
	unsigned damaged = 0;

	for (size_t i = 0; i < 8; ++i) {
		const struct st_rip3_font_asset *asset = st_rip3_font_asset_at(i);
		struct st_rip3_rff file;
		struct st_rip3_rff_style style;
		int kern = 1;

		CHECK(st_rip3_rff_open(&file, asset->data, asset->size) == 0);
		CHECK(file.characters == 224 && file.first_character == 32 && file.styles == 10 && file.units_per_em == 1000);
		CHECK(file.xmin == bounds[i][0] && file.ymin == bounds[i][1]
		    && file.xmax == bounds[i][2] && file.ymax == bounds[i][3]);
		CHECK(st_rip3_rff_find_style(&style, &file, families[i]) == 0);
		CHECK(style.flags == 0 && style.horizontal_percent == 100);
		for (size_t j = 0; j < 10; ++j) {
			CHECK(st_rip3_rff_style_at(&style, &file, j) == 0);
			CHECK(style.flags == flags[j] && style.hollow == (j >= 5));
			CHECK(style.horizontal_percent == (i == 2 && j == 4 ? 140 : percent[j]));
			CHECK(st_rip3_rff_find_style(&style, &file, style.name) == 0);
		}
		CHECK(st_rip3_rff_style_at(&style, &file, 10) == IO_ERROR_NOT_FOUND);
		CHECK(st_rip3_rff_find_style(&style, &file, "missing") == IO_ERROR_NOT_FOUND);
		for (unsigned c = 32; c < 256; ++c) {
			struct st_rip3_rff_glyph glyph;
			size_t cursor = 0;
			CHECK(st_rip3_rff_glyph(&glyph, &file, (uint8_t)c) == 0);
			if (glyph.damaged) {
				CHECK(i == 7 && (c == 247 || c == 250 || c >= 251));
				++damaged;
			}
			else {
				while (cursor < glyph.path_size) {
					struct st_rip3_rff_segment segment;
					CHECK(st_rip3_rff_path_next(&segment, &glyph, &cursor) == 0);
				}
			}
			if (i == 3 && c == 'A') CHECK(glyph.advance == 666);
			if (i == 3 && c == ' ') CHECK(glyph.advance == 277 && glyph.path_size == 0);
			if (i == 7 && (c == 248 || c == 249)) CHECK(!glyph.damaged);
		}
		CHECK(st_rip3_rff_kerning(&kern, &file, 'F', 'A') == 0);
		if (i == 3) CHECK(kern == -118);
		if (i == 1 || i == 2 || i == 7) CHECK(kern == 0);
	}
	CHECK(damaged == 7);
	CHECK(test_records() == 0);
	return 0;
}
