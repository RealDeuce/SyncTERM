/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "io_error.h"
#include "st_rip3_chr.h"

#define CHECK(expression)                                                                        \
	do {                                                                                     \
		if (!(expression)) {                                                             \
			fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
			return 1;                                                                \
		}                                                                                \
	} while (0)

static void
put_word(uint8_t *data, unsigned value)
{
	data[0] = (uint8_t)value;
	data[1] = (uint8_t)(value >> 8);
}

static void
fixture(uint8_t data[167])
{
	static const uint8_t program[] = {0xc0, 0x3f, 0xbf, 0xc0, 0x00, 0x80, 0, 0, 0xff, 0x7f, 0x80, 0x80, 0, 0};

	memset(data, 0, 167);
	memcpy(data, "PK\010\010TEST\032", 9);
	put_word(data + 9, 128);
	memcpy(data + 11, "TEST", 4);
	put_word(data + 15, 39);
	data[17] = data[19] = 1;
	data[128]           = '+';
	put_word(data + 129, 3);
	data[132] = 32;
	put_word(data + 133, 25);
	data[135] = 7;
	data[136] = 63;
	data[137] = 1;
	data[138] = 192;
	put_word(data + 128 + 16 + 2, 8);
	data[128 + 22] = 5;
	data[128 + 23] = 0;
	data[128 + 24] = 5;
	memcpy(data + 128 + 25, program, sizeof(program));
}

static int
synthetic(void)
{
	uint8_t                   data[167];
	struct st_rip3_chr        font;
	struct st_rip3_chr_glyph  glyph, alias;
	struct st_rip3_chr_record record;
	size_t                    cursor = 0;

	fixture(data);
	CHECK(st_rip3_chr_open(&font, data, sizeof(data)) == 0);
	CHECK(font.size == 39 && font.strokes == 25 && font.characters == 3 && font.first_character == 32
	      && font.scan_flag == 7 && strcmp(font.name, "TEST") == 0);
	CHECK(font.capital == 63 && font.baseline == 1 && font.descender == -64);
	CHECK(st_rip3_chr_glyph(&glyph, &font, 32) == 0);
	CHECK(glyph.advance == 5 && glyph.program_size == 6 && glyph.has_scan);
	CHECK(st_rip3_chr_glyph(&alias, &font, 34) == 0);
	CHECK(alias.program == glyph.program && alias.advance == glyph.advance);
	CHECK(st_rip3_chr_next(&record, &glyph, &cursor) == 0);
	CHECK(record.operation == ST_RIP3_CHR_MOVE && record.x == -64 && record.y == 63);
	CHECK(st_rip3_chr_next(&record, &glyph, &cursor) == 0);
	CHECK(record.operation == ST_RIP3_CHR_DRAW && record.x == 63 && record.y == -64);
	CHECK(st_rip3_chr_next(&record, &glyph, &cursor) == 0);
	CHECK(record.operation == ST_RIP3_CHR_SCAN && record.x == 0 && record.y == 0);
	CHECK(st_rip3_chr_next(&record, &glyph, &cursor) == IO_ERROR_NOT_FOUND && cursor == 6);
	CHECK(st_rip3_chr_glyph(&glyph, &font, 33) == 0);
	CHECK(glyph.advance == 0 && glyph.program_size == 4 && !glyph.has_scan);
	cursor = 0;
	CHECK(st_rip3_chr_next(&record, &glyph, &cursor) == 0);
	CHECK(record.operation == ST_RIP3_CHR_MOVE && record.x == -1 && record.y == -1);
	cursor = 1;
	record = (struct st_rip3_chr_record) {ST_RIP3_CHR_DRAW, 11, 12};
	CHECK(st_rip3_chr_next(&record, &glyph, &cursor) == IO_ERROR_OUT_OF_RANGE);
	CHECK(cursor == 1 && record.operation == ST_RIP3_CHR_DRAW && record.x == 11 && record.y == 12);
	CHECK(st_rip3_chr_glyph(&alias, &font, 31) == IO_ERROR_NOT_FOUND);
	CHECK(alias.program_size == 6 && alias.advance == 5);
	{
		uint8_t relocated[256 + 39] = {0};
		uint8_t padded[128 + 43]    = {0};

		memcpy(relocated, data, 128);
		put_word(relocated + 9, 256);
		memcpy(relocated + 256, data + 128, 39);
		CHECK(st_rip3_chr_open(&font, relocated, sizeof(relocated)) == 0);
		CHECK(font.body == relocated + 256 && font.strokes == 25);
		CHECK(st_rip3_chr_glyph(&glyph, &font, 32) == 0 && glyph.advance == 5 && glyph.has_scan);
		memcpy(padded, data, 128 + 25);
		put_word(padded + 15, 43);
		put_word(padded + 133, 29);
		memcpy(padded + 128 + 29, data + 128 + 25, 14);
		CHECK(st_rip3_chr_open(&font, padded, sizeof(padded)) == 0);
		CHECK(font.strokes == 29);
		CHECK(st_rip3_chr_glyph(&glyph, &font, 32) == 0 && glyph.advance == 5 && glyph.program_size == 6);
	}
	return 0;
}

static int
errors(void)
{
	uint8_t                  data[167], bad[167];
	struct st_rip3_chr       font;
	uint8_t                  previous[sizeof(font)];
	struct st_rip3_chr_glyph glyph;

	fixture(data);
	CHECK(st_rip3_chr_open(&font, data, sizeof(data)) == 0);
	memcpy(previous, &font, sizeof(font));
	for (size_t size = 0; size < sizeof(data); size++) {
		CHECK(st_rip3_chr_open(&font, data, size) == IO_ERROR_INVALID_FORMAT);
		CHECK(memcmp(&font, previous, sizeof(font)) == 0);
	}
	for (unsigned variant = 0; variant < 9; variant++) {
		memcpy(bad, data, sizeof(bad));
		switch (variant) {
			case 0:
				bad[0] = 0;
				break;
			case 1:
				bad[8] = 0;
				break; /* Missing prefix terminator. */
			case 2:
				put_word(bad + 9, 16);
				break;
			case 3:
				put_word(bad + 15, 38);
				break;
			case 4:
				bad[128] = '-';
				break;
			case 5:
				bad[132] = 254;
				break;
			case 6:
				put_word(bad + 133, 24);
				break;
			case 7:
				put_word(bad + 144, 65535);
				break;
			case 8:
				bad[165] = bad[166] = 0x80;
				break; /* No final END. */
		}
		CHECK(st_rip3_chr_open(&font, bad, sizeof(bad)) == IO_ERROR_INVALID_FORMAT);
		CHECK(memcmp(&font, previous, sizeof(font)) == 0);
	}
	memcpy(bad, data, sizeof(bad));
	bad[17] = 2;
	CHECK(st_rip3_chr_open(&font, bad, sizeof(bad)) == IO_ERROR_NOT_SUPPORTED);
	CHECK(memcmp(&font, previous, sizeof(font)) == 0);
	CHECK(st_rip3_chr_open(NULL, data, sizeof(data)) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_chr_open(&font, NULL, sizeof(data)) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_chr_builtin(&font, 0) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_chr_builtin(&font, 11) == IO_ERROR_OUT_OF_RANGE);
	CHECK(memcmp(&font, previous, sizeof(font)) == 0);
	CHECK(st_rip3_chr_glyph(&glyph, NULL, 32) == IO_ERROR_INVALID_ARGUMENT);
	return 0;
}

static int
full_byte_range(void)
{
	uint8_t                  data[128 + 16 + 3 * 256 + 2] = {0};
	struct st_rip3_chr       font;
	struct st_rip3_chr_glyph glyph;

	memcpy(data, "PK\010\010TEST\032", 9);
	put_word(data + 9, 128);
	memcpy(data + 11, "TEST", 4);
	put_word(data + 15, sizeof(data) - 128);
	data[17] = data[19] = 1;
	data[128]           = '+';
	put_word(data + 129, 256);
	put_word(data + 133, 16 + 3 * 256);
	CHECK(st_rip3_chr_open(&font, data, sizeof(data)) == 0);
	CHECK(font.characters == 256 && font.first_character == 0);
	for (unsigned code = 0; code < 256; code++) {
		CHECK(st_rip3_chr_glyph(&glyph, &font, (uint8_t)code) == 0);
		CHECK(glyph.program_size == 0 && glyph.advance == 0 && !glyph.has_scan);
	}
	return 0;
}

static int
original_fonts(void)
{
	static const char *const names[] = {"TRIP", "LITT", "SANS", "GOTH", "SCRI", "SIMP", "TSCR", "LCOM", "EURO",
	    "BOLD"};
	static const size_t body_sizes[] = {16549, 5023, 13468, 17935, 10859, 8309, 17227, 11951, 8311, 14542};
	static const int    capitals[]   = {24, 7, 25, 25, 25, 28, 24, 28, 45, 53};
	static const int    descenders[] = {-7, -2, -7, -7, -12, -7, -7, -7, -10, -7};
	unsigned            glyph_count = 0, scan_count = 0, scan_glyphs = 0;

	for (unsigned number = 1; number <= 10; number++) {
		struct st_rip3_chr font;

		CHECK(st_rip3_chr_builtin(&font, number) == 0);
		CHECK(strcmp(font.name, names[number - 1]) == 0 && font.size == body_sizes[number - 1]);
		CHECK(font.scan_flag == 0 && font.capital == capitals[number - 1] && font.baseline == 0
		      && font.descender == descenders[number - 1]);
		CHECK(font.characters == (number == 3 ? 254U : number == 10 ? 224U : 223U));
		CHECK(font.first_character == (number == 3 ? 1U : 32U));
		for (size_t i = 0; i < font.characters; i++) {
			unsigned                  code = font.first_character + (unsigned)i;
			struct st_rip3_chr_glyph  glyph;
			struct st_rip3_chr_record record;
			size_t                    cursor = 0;
			bool                      scan   = false;
			int                       status;

			CHECK(st_rip3_chr_glyph(&glyph, &font, (uint8_t)code) == 0);
			glyph_count++;
			while ((status = st_rip3_chr_next(&record, &glyph, &cursor)) == 0) {
				CHECK(record.x >= -64 && record.x <= 63 && record.y >= -64 && record.y <= 63);
				if (record.operation == ST_RIP3_CHR_SCAN) {
					CHECK(number == 10
					      && (code == 0x22 || code == 0x25 || code == 0x3a || code == 0x3b
					          || code == 0x3d));
					CHECK(record.x == 0 && record.y == 0);
					scan = true;
					scan_count++;
				}
			}
			CHECK(status == IO_ERROR_NOT_FOUND && cursor == glyph.program_size);
			CHECK(glyph.has_scan == scan);
			if (scan)
				scan_glyphs++;
		}
	}
	CHECK(glyph_count == 2262 && scan_count == 6 && scan_glyphs == 5);
	{
		struct st_rip3_chr        font;
		struct st_rip3_chr_glyph  glyph;
		struct st_rip3_chr_record record;
		/* The inherited worked A geometry also appears in RIPtel LITT,
		 * at the later file's offset 0x5b7 rather than the old 0x5a3. */
		const int expected[][3] = {
		    {ST_RIP3_CHR_MOVE, 0, 0},
		    {ST_RIP3_CHR_DRAW, 0, 4},
		    {ST_RIP3_CHR_DRAW, 2, 6},
		    {ST_RIP3_CHR_DRAW, 4, 4},
		    {ST_RIP3_CHR_DRAW, 4, 0},
		    {ST_RIP3_CHR_DRAW, 4, 3},
		    {ST_RIP3_CHR_DRAW, 0, 3},
		    {ST_RIP3_CHR_MOVE, 6, 0},
		};
		size_t cursor = 0;

		CHECK(st_rip3_chr_builtin(&font, 2) == 0);
		CHECK(st_rip3_chr_glyph(&glyph, &font, 'A') == 0);
		CHECK(glyph.advance == 6 && glyph.program_size == 16);
		for (size_t i = 0; i < 8; i++) {
			CHECK(st_rip3_chr_next(&record, &glyph, &cursor) == 0);
			CHECK((int)record.operation == expected[i][0] && record.x == expected[i][1]
			      && record.y == expected[i][2]);
		}
		CHECK(st_rip3_chr_next(&record, &glyph, &cursor) == IO_ERROR_NOT_FOUND && cursor == 16);
	}
	return 0;
}

int
main(void)
{
	CHECK(synthetic() == 0);
	CHECK(errors() == 0);
	CHECK(full_byte_range() == 0);
	CHECK(original_fonts() == 0);
	return 0;
}
