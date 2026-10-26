/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "console_internal.h"

#include "st_codepage.h"

static bool
cp437_alternate_character(unsigned char value, unsigned char *character)
{
	switch (value) {
	case 30: *character = '-'; break;
	case 31: *character = '.'; break;
	case 176: *character = 'a'; break;
	case 177: case 178: *character = 'h'; break;
	case 179: case 186: *character = 'x'; break;
	case 180: case 181: case 182: case 185: *character = 'u'; break;
	case 183: case 184: case 187: case 191: *character = 'k'; break;
	case 188: case 189: case 190: case 217: *character = 'j'; break;
	case 192: case 200: *character = 'm'; break;
	case 193: case 202: *character = 'v'; break;
	case 194: case 203: *character = 'w'; break;
	case 195: case 198: case 199: *character = 't'; break;
	case 196: case 205: *character = 'q'; break;
	case 197: case 206: case 215: case 216: *character = 'n'; break;
	case 201: case 213: case 214: case 218: *character = 'l'; break;
	case 219: *character = '0'; break;
	case 254: *character = '~'; break;
	default: return false;
	}
	return true;
}

unsigned char
st_console_cp437_character(uint32_t codepoint)
{
	unsigned char character;

	return st_codepage_from_unicode(&character, ST_CODEPAGE_CP437, codepoint)
	    ? character : '?';
}

unsigned char
st_console_ascii_character(st_console_t *console,
	const struct st_console_cell *cell, bool *alternate)
{
	unsigned char character = '?';
	unsigned char cp437;
	uint32_t codepoint;

	(void)st_console_cell_codepoint_locked(console, cell, &codepoint);

	*alternate = true;
	switch (codepoint) {
	case UINT32_C(0x2191): character = '-'; break;
	case UINT32_C(0x2193): character = '.'; break;
	case UINT32_C(0x2500): case UINT32_C(0x2501):
	case UINT32_C(0x2550): character = 'q'; break;
	case UINT32_C(0x2502): case UINT32_C(0x2503):
	case UINT32_C(0x2551): character = 'x'; break;
	case UINT32_C(0x250c): case UINT32_C(0x250f):
	case UINT32_C(0x2554): character = 'l'; break;
	case UINT32_C(0x2510): case UINT32_C(0x2513):
	case UINT32_C(0x2557): character = 'k'; break;
	case UINT32_C(0x2514): case UINT32_C(0x2517):
	case UINT32_C(0x255a): character = 'm'; break;
	case UINT32_C(0x2518): case UINT32_C(0x251b):
	case UINT32_C(0x255d): character = 'j'; break;
	case UINT32_C(0x251c): case UINT32_C(0x2523):
	case UINT32_C(0x2560): character = 't'; break;
	case UINT32_C(0x2524): case UINT32_C(0x252b):
	case UINT32_C(0x2563): character = 'u'; break;
	case UINT32_C(0x252c): case UINT32_C(0x2533):
	case UINT32_C(0x2566): character = 'w'; break;
	case UINT32_C(0x2534): case UINT32_C(0x253b):
	case UINT32_C(0x2569): character = 'v'; break;
	case UINT32_C(0x253c): case UINT32_C(0x254b):
	case UINT32_C(0x256c): character = 'n'; break;
	case UINT32_C(0x2591): character = 'a'; break;
	case UINT32_C(0x2592): case UINT32_C(0x2593): character = 'h'; break;
	case UINT32_C(0x2588): character = '0'; break;
	case UINT32_C(0x25cf): case UINT32_C(0x2022): character = '~'; break;
	default: *alternate = false; break;
	}
	if (*alternate)
		return character;
	if (codepoint >= 0x80
	    && st_codepage_from_unicode(&cp437, ST_CODEPAGE_CP437,
	    codepoint)
	    && cp437_alternate_character(cp437, &character)) {
		*alternate = true;
		return character;
	}
	*alternate = false;
	if (codepoint >= 0x20 && codepoint <= 0x7e)
		return (unsigned char)codepoint;
	if (codepoint > 0 && codepoint < 0x20)
		return (unsigned char)(codepoint + 'A' - 1);
	return '?';
}
