/* Tests for locale-independent legacy codepage conversion. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_codepage.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, \
		    __LINE__, #expression); \
		return 1; \
	} \
} while (0)

struct mapping_hashes {
	uint64_t normal;
	uint64_t graphic;
	uint64_t reverse;
};

static uint64_t
hash_codepoint(uint64_t hash, uint32_t codepoint)
{
	for (unsigned int shift = 0; shift < 32; shift += 8) {
		hash ^= (codepoint >> shift) & 0xffU;
		hash *= UINT64_C(1099511628211);
	}
	return hash;
}

static uint64_t
mapping_hash(enum st_codepage codepage, bool graphic_controls)
{
	uint64_t hash = UINT64_C(14695981039346656037);

	for (unsigned int value = 0; value <= UINT8_MAX; ++value)
		hash = hash_codepoint(hash, st_codepage_to_unicode(codepage,
		    (uint8_t)value, graphic_controls));
	return hash;
}

static uint64_t
hash_byte(uint64_t hash, uint8_t value)
{
	hash ^= value;
	return hash * UINT64_C(1099511628211);
}

static uint64_t
reverse_hash(enum st_codepage codepage)
{
	uint64_t hash = UINT64_C(14695981039346656037);

	for (unsigned int graphic = 0; graphic < 2; ++graphic) {
		for (unsigned int value = 0; value <= UINT8_MAX; ++value) {
			uint32_t codepoint = st_codepage_to_unicode(codepage,
			    (uint8_t)value, graphic != 0);
			uint8_t encoded = 0;
			bool mapped;

			if (codepoint == UINT32_C(0xfffd))
				continue;
			mapped = st_codepage_from_unicode(&encoded, codepage, codepoint);
			hash = hash_byte(hash, mapped ? 1 : 0);
			if (mapped)
				hash = hash_byte(hash, encoded);
		}
	}
	return hash;
}

int
main(void)
{
	static const char *const names[ST_CODEPAGE_COUNT] = {
		"CP437", "CP1251", "CP1251B", "KOI8-R", "ISO-8859-2",
		"ISO-8859-4", "CP866M", "ISO-8859-9", "ISO-8859-8",
		"KOI8-U", "ISO-8859-15", "ISO-8859-5", "CP850",
		"CP850B", "CP865", "CP865B", "ISO-8859-7", "ISO-8859-1",
		"CP866M2", "CP866U", "CP1131", "ARMSCII-8", "HAIK8",
		"ATASCII", "PETSCIIU", "PETSCIIL", "PRESTEL",
		"PRESTEL-SEP", "ATARI-ST",
	};
	static const bool broken_vertical[ST_CODEPAGE_COUNT] = {
		true, false, true, true, true, false, true, false, false, true,
		false, true, false, true, false, true, false, false, true, true,
		false, true, true, false, false, false, false, false, false,
	};
	/* Frozen legacy tables, with deterministic ordering fixes and the
	 * existing CP437 graphic DEL mapping retained. */
	static const struct mapping_hashes hashes[ST_CODEPAGE_COUNT] = {
		{UINT64_C(0xdf85318e5e41b5df), UINT64_C(0xd2a17e39773a07b1), UINT64_C(0x2715bd5691424125)},
		{UINT64_C(0x851582fd14212a57), UINT64_C(0x775e55d6033c3cdd), UINT64_C(0x9b94819825bd27e3)},
		{UINT64_C(0x20c9524c939df561), UINT64_C(0x644f0cdde6ab5dc3), UINT64_C(0x64fe060056bf9337)},
		{UINT64_C(0x65e3115aed9247b0), UINT64_C(0x386a750830181430), UINT64_C(0x73a3e62086aba713)},
		{UINT64_C(0x1ad5f7df73d776c8), UINT64_C(0x00ed62adee404848), UINT64_C(0x4d6945146ecaa893)},
		{UINT64_C(0x82ad8c3133b543bc), UINT64_C(0x744d9d023095213c), UINT64_C(0xc8034da73b233845)},
		{UINT64_C(0x647aa89bf0ac59f2), UINT64_C(0x2617f7ebdc0bf448), UINT64_C(0x17abd6ba5837ef73)},
		{UINT64_C(0x09d10fe5fee76dc4), UINT64_C(0x48c6becef10f1844), UINT64_C(0xc8034da73b233845)},
		{UINT64_C(0xf8986a19f0994efd), UINT64_C(0x80fd3118a0c0da7d), UINT64_C(0x71d584cb0e63395d)},
		{UINT64_C(0x0d7ea0bcf5cbd55d), UINT64_C(0x2ebec6362ce1c3dd), UINT64_C(0x73a3e62086aba713)},
		{UINT64_C(0x1613888431662a58), UINT64_C(0x6fc5c078f85f2dd8), UINT64_C(0x3a34ad1da43686c5)},
		{UINT64_C(0x1ddd954ab81ddf7c), UINT64_C(0x4ead48ec9c47cdfc), UINT64_C(0x1a96f0ff245b4573)},
		{UINT64_C(0x7161c70c005d57a5), UINT64_C(0x06602c4263c01407), UINT64_C(0x49418e41143a0da5)},
		{UINT64_C(0x691306fef070d1a3), UINT64_C(0x94f11e2551ea4aa9), UINT64_C(0x828f462c64468e25)},
		{UINT64_C(0x555ebe1f706e3b1d), UINT64_C(0x4a385bd29870cfa7), UINT64_C(0x2715bd5691424125)},
		{UINT64_C(0x66b900962450f84b), UINT64_C(0xc15916fb6ed72469), UINT64_C(0x17abd6ba5837ef73)},
		{UINT64_C(0xe738b7f198baf398), UINT64_C(0x5142ad6ce43f0718), UINT64_C(0xf86a31f4f46b5a61)},
		{UINT64_C(0xc83870528524eda5), UINT64_C(0x81411615e1b54a25), UINT64_C(0xc8034da73b233845)},
		{UINT64_C(0x6d455c5a892acacc), UINT64_C(0x2a45a895567a92ce), UINT64_C(0x17abd6ba5837ef73)},
		{UINT64_C(0x1982daa9effa854b), UINT64_C(0x850dec9a7439ce19), UINT64_C(0x17abd6ba5837ef73)},
		{UINT64_C(0x085c16f892c4bd56), UINT64_C(0x2ebff7ef889f7b70), UINT64_C(0x2715bd5691424125)},
		{UINT64_C(0xee50f1ded43430a0), UINT64_C(0x7d08945f439fcec6), UINT64_C(0x32c8e7a659cc0e68)},
		{UINT64_C(0x4e277a4bd25277a0), UINT64_C(0x6127bf8e9c36f0c6), UINT64_C(0xcc7715e54376314d)},
		{UINT64_C(0x2368c1bcef1bf8a8), UINT64_C(0x567670aa09042e82), UINT64_C(0x7960701fe8452325)},
		{UINT64_C(0x1be326a992a18a2e), UINT64_C(0x1be326a992a18a2e), UINT64_C(0xc582e4cf69193225)},
		{UINT64_C(0xb7d7ae455c7b6c05), UINT64_C(0xb7d7ae455c7b6c05), UINT64_C(0xc582e4cf69193225)},
		{UINT64_C(0xc0ee67560132b72b), UINT64_C(0x0e859d93412a2dab), UINT64_C(0xad00611e73237925)},
		{UINT64_C(0xc12865594b83b8b3), UINT64_C(0x73357758c34f9533), UINT64_C(0xad00611e73237925)},
		{UINT64_C(0x87feb2e1d2f00818), UINT64_C(0x5eef148adda6118c), UINT64_C(0xa0bb9af0fa13578c)},
	};
	uint8_t byte = 0xa5;

	CHECK(st_codepage_count() == ST_CODEPAGE_COUNT);
	for (size_t i = 0; i < ST_CODEPAGE_COUNT; ++i) {
		enum st_codepage codepage = (enum st_codepage)i;

		CHECK(strcmp(st_codepage_name(codepage), names[i]) == 0);
		CHECK(st_codepage_broken_vertical(codepage)
		    == broken_vertical[i]);
		CHECK(mapping_hash(codepage, false) == hashes[i].normal);
		CHECK(mapping_hash(codepage, true) == hashes[i].graphic);
		CHECK(reverse_hash(codepage) == hashes[i].reverse);
	}
	CHECK(st_codepage_name((enum st_codepage)-1) == NULL);
	CHECK(st_codepage_name(ST_CODEPAGE_COUNT) == NULL);
	CHECK(!st_codepage_broken_vertical(ST_CODEPAGE_COUNT));
	CHECK(st_codepage_to_unicode(ST_CODEPAGE_COUNT, 'A', false)
	    == UINT32_C(0xfffd));
	CHECK(!st_codepage_from_unicode(&byte, ST_CODEPAGE_COUNT, 'A'));
	CHECK(byte == 0xa5);
	CHECK(!st_codepage_from_unicode(NULL, ST_CODEPAGE_CP437, 'A'));
	CHECK(!st_codepage_from_unicode(&byte, ST_CODEPAGE_CP437,
	    UINT32_C(0xd800)));
	CHECK(byte == 0xa5);
	CHECK(!st_codepage_from_unicode(&byte, ST_CODEPAGE_CP437,
	    UINT32_MAX));
	CHECK(byte == 0xa5);
	CHECK(st_codepage_from_unicode(&byte, ST_CODEPAGE_CP1251,
	    UINT32_C(0x0410)) && byte == 0xc0);
	CHECK(st_codepage_from_unicode(&byte, ST_CODEPAGE_ATASCII,
	    UINT32_C(0x2665)) && byte == 0x3f);
	CHECK(st_codepage_from_unicode(&byte, ST_CODEPAGE_CP866M,
	    UINT32_C(0x2248)) && byte == 0xf7);
	CHECK(st_codepage_from_unicode(&byte, ST_CODEPAGE_CP866M2,
	    UINT32_C(0x2248)) && byte == 0xf7);
	CHECK(st_codepage_from_unicode(&byte, ST_CODEPAGE_ATASCII,
	    UINT32_C(0x21b0)) && byte == 0x7d);
	CHECK(st_codepage_from_unicode(&byte, ST_CODEPAGE_ATASCII,
	    UINT32_C(0x1f8b0)) && byte == 0x7d);
	for (unsigned int value = 0; value <= UINT8_MAX; ++value) {
		uint32_t codepoint = st_codepage_to_unicode(ST_CODEPAGE_CP437,
		    (uint8_t)value, true);

		CHECK(st_codepage_from_unicode(&byte, ST_CODEPAGE_CP437,
		    codepoint));
		CHECK(byte == value);
	}
	CHECK(st_codepage_to_unicode(ST_CODEPAGE_CP437, 0x7f, false) == 0x7f);
	CHECK(st_codepage_to_unicode(ST_CODEPAGE_CP437, 0x7f, true)
	    == UINT32_C(0x2302));
	return 0;
}
