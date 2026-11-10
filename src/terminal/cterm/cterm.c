/****************************************************************************
 * @format.tab-size 4		(Plain Text/Source Code File Header)			*
 * @format.use-tabs true	(see http://www.synchro.net/ptsc_hdr.html)		*
 *																			*
 * Copyright (c) 2026 Stephen Hurd							*
 *																			*
 * SPDX-License-Identifier: BSD-2-Clause								*
 *																			*
 * For Synchronet coding style and modification guidelines, see				*
 * http://www.synchro.net/source.html										*
 *																			*
 * Note: If this box doesn't appear square, then you need to fix your tabs.	*
 ****************************************************************************/

/*
 * Coordinate systems in use here... text is 1-based.
 * "pixel"   the console raster in pixels.  Sixel commands happen here.
 * "screen"  the console cell grid and pointer coordinates.
 *           are in this space.
 * "absterm" position inside the full terminal area.  These coordinates
 *           are used when origin mode is off.
 * "term"    The scrolling region.  These coordinates are used when origin
 *           mode is on.
 * "curr"    Either absterm or term depending on the state of origin mode
 *           This is what's sent to and received from the remote, and
 *           matches the CTerm window.
 */

#include <assert.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
 #include <malloc.h>	/* alloca() on Win32 */
#endif

#include "cterm_render.h"

#include "cterm.h"
#include "cterm_skypix.h"
#include "cterm_igs.h"
#include "st_session_history.h"
#include "cterm_apc.h"
#include "cterm_internal.h"
#include "cterm_ecma48.h"
#include "cterm_dec.h"
#include "cterm_cterm.h"
#include "cterm_vt52.h"
#include "cterm_atascii.h"
#include "cterm_petscii.h"
#include "cterm_prestel.h"

/*
 * Disable warnings for well-defined standard C operations
 * 4244 appears to be for implicit conversions
 * warning C4244: '=': conversion from 'uint64_t' to 'int', possible loss of data
 * 4267 is the same thing, but from size_t.
 * 4018 warns when comparing signed and unsigned if the signed is implicitly
 *      converted to unsigned (so more sane than -Wsign-compare)
 */
#ifdef _MSC_VER
#pragma warning(disable : 4244 4267 4018)
#endif

// Hack for Visual Studio 2022
#ifndef UINT64_MAX
#define UINT64_MAX UINT64_C(-1)
#endif

#define	BUFSIZE	2048

/* cterm_accumulate_trailing is defined below and is installed from
 * dispatch handlers that consume N raw bytes (VT52 ESC Y / b / c, BEEB
 * VDU 23 / 28) — declared in cterm_internal.h so those handlers can
 * reach it. */

/*
 * Send a parser-generated response to the host (DSR, DECRQM, STS, etc.).
 * If response_cb is set, calls it directly; otherwise the response is dropped.
 */
void
cterm_respond(struct cterminal *cterm, const char *data, size_t len)
{
	if (cterm->response_cb) {
		cterm->response_cb(data, len, cterm->response_cbdata);
		if(cterm->log==CTERM_LOG_RAW && cterm->logfile != NULL)
			fwrite(data, len, 1, cterm->logfile);
	}
}

/*
 * Send a user-keystroke encoding to the host.  Distinct from
 * cterm_respond so a spy connection can mute parser auto-responses
 * (where the real client is doing the query/response handshake) while
 * still letting the local sysop's keystrokes reach the BBS.
 */
static void
cterm_send_keystroke(struct cterminal *cterm, const char *data, size_t len)
{
	if (cterm->keystroke_cb)
		cterm->keystroke_cb(data, len, cterm->keystroke_cbdata);
}

/* Printf-style wrapper for cterm_respond */
void
cterm_respond_printf(struct cterminal *cterm, const char *fmt, ...)
{
	char tmp[1024];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
	va_end(ap);
	if (n > 0)
		cterm_respond(cterm, tmp, n);
}

/* Key-to-sequence mapping for emulation-specific input translation.
 * Tables are sorted by key value for bsearch(). */
struct key_mapping {
	int         key;
	const char *seq;
	int         len;
};

#define KEY_MAP_SIZE(table) (int)(sizeof(table) / sizeof((table)[0]))
#define LOOKUP_KEY(table, key, len) \
	lookup_key((table), KEY_MAP_SIZE(table), (key), (len))

static const struct key_mapping atascii_keys[] = {
	{253,                      "\x07",        1},	/* Undo Unicode: Atascii beep -> ^G */
	{CTERM_KEY_DOS_UP,               "\x1c",        1},
	{CTERM_KEY_DOS_LEFT,             "\x1e",        1},
	{CTERM_KEY_DOS_RIGHT,            "\x1f",        1},
	{CTERM_KEY_DOS_DOWN,             "\x1d",        1},
	{CTERM_KEY_DOS_IC,               "\xff",        1},
	{CTERM_KEY_DOS_DC,               "\x7e",        1},	/* "Delete" key */
};

static const struct key_mapping petscii_keys[] = {
	{8,                        "\x14",        1},
	{'\n',                     "\x0d",        1},
	{'\r',                     "\x0d",        1},
	{CTERM_KEY_DOS_F(1),             "\x85",        1},
	{CTERM_KEY_DOS_F(2),             "\x89",        1},
	{CTERM_KEY_DOS_F(3),             "\x86",        1},
	{CTERM_KEY_DOS_F(4),             "\x8a",        1},
	{CTERM_KEY_DOS_F(5),             "\x87",        1},
	{CTERM_KEY_DOS_F(6),             "\x8b",        1},
	{CTERM_KEY_DOS_F(7),             "\x88",        1},
	{CTERM_KEY_DOS_F(8),             "\x8c",        1},
	{CTERM_KEY_DOS_HOME,             "\x13",        1},
	{CTERM_KEY_DOS_UP,               "\x91",        1},
	{CTERM_KEY_DOS_LEFT,             "\x9d",        1},
	{CTERM_KEY_DOS_RIGHT,            "\x1d",        1},
	{CTERM_KEY_DOS_END,              "\x93",        1},	/* Clear / Shift-Home */
	{CTERM_KEY_DOS_DOWN,             "\x11",        1},
	{CTERM_KEY_DOS_IC,               "\x94",        1},
	{CTERM_KEY_DOS_DC,               "\x14",        1},
};

static const struct key_mapping prestel_keys[] = {
	{8,                        "\x7f",        1},
	{9,                        "\x09",        1},
	{10,                       "\x0d",        1},	/* LF sends CR */
	{12,                       "\x0c",        1},   /* CTRL-L for "Clear Screen" -- nelgin */
	{13,                       "_",           1},	/* CR sends _ */
	{20,                       "\x18",        1},   /* CTRL-T for "Delete Line" -- nelgin */
	{27,                       "\x1b",        1},   /* Pass ESC through */
	{'#',                      "_",           1},
	{'_',                      "`",           1},
	{'`',                      "#",           1},
	{CTERM_KEY_DOS_F(7),             "\x1b",        1},
	{CTERM_KEY_DOS_HOME,             "\x1e",        1},
	{CTERM_KEY_DOS_UP,               "\x8f",        1},
	{CTERM_KEY_DOS_LEFT,             "\x8c",        1},
	{CTERM_KEY_DOS_RIGHT,            "\x8d",        1},
	{CTERM_KEY_DOS_DOWN,             "\x8e",        1},
	{CTERM_KEY_DOS_DC,               "\x7f",        1},
	{CTERM_KEY_DOS_SHIFT_F(1),       "\x91",        1},
	{CTERM_KEY_DOS_SHIFT_F(2),       "\x92",        1},
	{CTERM_KEY_DOS_SHIFT_F(3),       "\x93",        1},
	{CTERM_KEY_DOS_SHIFT_F(4),       "\x94",        1},
	{CTERM_KEY_DOS_SHIFT_F(5),       "\x95",        1},
	{CTERM_KEY_DOS_SHIFT_F(6),       "\x96",        1},
	{CTERM_KEY_DOS_SHIFT_F(7),       "\x97",        1},
	{CTERM_KEY_DOS_SHIFT_F(8),       "\x98",        1},
	{CTERM_KEY_DOS_SHIFT_F(9),       "\x99",        1},
	{CTERM_KEY_DOS_SHIFT_F(10),      "\x90",        1},	/* F0 */
	{CTERM_KEY_DOS_CTRL_F(1),        "\xa1",        1},
	{CTERM_KEY_DOS_CTRL_F(2),        "\xa2",        1},
	{CTERM_KEY_DOS_CTRL_F(3),        "\xa3",        1},
	{CTERM_KEY_DOS_CTRL_F(4),        "\xa4",        1},
	{CTERM_KEY_DOS_CTRL_F(5),        "\xa5",        1},
	{CTERM_KEY_DOS_CTRL_F(6),        "\xa6",        1},
	{CTERM_KEY_DOS_CTRL_F(7),        "\xa7",        1},
	{CTERM_KEY_DOS_CTRL_F(8),        "\xa8",        1},
	{CTERM_KEY_DOS_CTRL_F(9),        "\xa9",        1},
	{CTERM_KEY_DOS_CTRL_F(10),       "\xa0",        1},	/* F0 */
	{CTERM_KEY_DOS_CTRL_LEFT,        "\xac",        1},
	{CTERM_KEY_DOS_CTRL_RIGHT,       "\xad",        1},
	{CTERM_KEY_DOS_CTRL_END,         "\xab",        1},	/* Copy */
	{CTERM_KEY_DOS_CTRL_UP,          "\xaf",        1},
	{CTERM_KEY_DOS_CTRL_DOWN,        "\xae",        1},
	{CTERM_KEY_DOS_SHIFT_UP,         "\x9f",        1},
	{CTERM_KEY_DOS_SHIFT_LEFT,       "\x9c",        1},
	{CTERM_KEY_DOS_SHIFT_RIGHT,      "\x9d",        1},
	{CTERM_KEY_DOS_SHIFT_END,        "\x9b",        1},	/* Copy */
	{CTERM_KEY_DOS_SHIFT_DOWN,       "\x9e",        1},
};

static const struct key_mapping prestel_keys_cursor[] = {
	{8,                        "\x7f",        1},
	{9,                        "\x09",        1},
	{10,                       "\x0d",        1},	/* LF sends CR */
	{12,                       "\x0c",        1},   /* CTRL-L for "Clear Screen" -- nelgin */
	{13,                       "_",           1},	/* CR sends _ */
	{20,                       "\x18",        1},   /* CTRL-T for "Delete Line" -- nelgin */
	{27,                       "\x1b",        1},   /* Pass ESC through */
	{'#',                      "_",           1},
	{'_',                      "`",           1},
	{'`',                      "#",           1},
	{CTERM_KEY_DOS_F(7),             "\x1b",        1},
	{CTERM_KEY_DOS_HOME,             "\x1e",        1},
	{CTERM_KEY_DOS_UP,               "\x0b",        1},
	{CTERM_KEY_DOS_LEFT,             "\x08",        1},
	{CTERM_KEY_DOS_RIGHT,            "\x09",        1},
	{CTERM_KEY_DOS_DOWN,             "\x0a",        1},
	{CTERM_KEY_DOS_DC,               "\x7f",        1},
	{CTERM_KEY_DOS_SHIFT_F(1),       "\x91",        1},
	{CTERM_KEY_DOS_SHIFT_F(2),       "\x92",        1},
	{CTERM_KEY_DOS_SHIFT_F(3),       "\x93",        1},
	{CTERM_KEY_DOS_SHIFT_F(4),       "\x94",        1},
	{CTERM_KEY_DOS_SHIFT_F(5),       "\x95",        1},
	{CTERM_KEY_DOS_SHIFT_F(6),       "\x96",        1},
	{CTERM_KEY_DOS_SHIFT_F(7),       "\x97",        1},
	{CTERM_KEY_DOS_SHIFT_F(8),       "\x98",        1},
	{CTERM_KEY_DOS_SHIFT_F(9),       "\x99",        1},
	{CTERM_KEY_DOS_SHIFT_F(10),      "\x90",        1},	/* F0 */
	{CTERM_KEY_DOS_CTRL_F(1),        "\xa1",        1},
	{CTERM_KEY_DOS_CTRL_F(2),        "\xa2",        1},
	{CTERM_KEY_DOS_CTRL_F(3),        "\xa3",        1},
	{CTERM_KEY_DOS_CTRL_F(4),        "\xa4",        1},
	{CTERM_KEY_DOS_CTRL_F(5),        "\xa5",        1},
	{CTERM_KEY_DOS_CTRL_F(6),        "\xa6",        1},
	{CTERM_KEY_DOS_CTRL_F(7),        "\xa7",        1},
	{CTERM_KEY_DOS_CTRL_F(8),        "\xa8",        1},
	{CTERM_KEY_DOS_CTRL_F(9),        "\xa9",        1},
	{CTERM_KEY_DOS_CTRL_F(10),       "\xa0",        1},	/* F0 */
	{CTERM_KEY_DOS_CTRL_LEFT,        "\xac",        1},
	{CTERM_KEY_DOS_CTRL_RIGHT,       "\xad",        1},
	{CTERM_KEY_DOS_CTRL_END,         "\xab",        1},	/* Copy */
	{CTERM_KEY_DOS_CTRL_UP,          "\xaf",        1},
	{CTERM_KEY_DOS_CTRL_DOWN,        "\xae",        1},
	{CTERM_KEY_DOS_SHIFT_UP,         "\x9f",        1},
	{CTERM_KEY_DOS_SHIFT_LEFT,       "\x9c",        1},
	{CTERM_KEY_DOS_SHIFT_RIGHT,      "\x9d",        1},
	{CTERM_KEY_DOS_SHIFT_END,        "\x9b",        1},	/* Copy */
	{CTERM_KEY_DOS_SHIFT_DOWN,       "\x9e",        1},
};

static const struct key_mapping beeb_keys[] = {
	{8,                        "\x7f",        1},
	{CTERM_KEY_DOS_F(7),             "\x1b",        1},
	{CTERM_KEY_DOS_HOME,             "\x1f",        1},
	{CTERM_KEY_DOS_UP,               "\x8f",        1},
	{CTERM_KEY_DOS_LEFT,             "\x8c",        1},
	{CTERM_KEY_DOS_RIGHT,            "\x8d",        1},
	{CTERM_KEY_DOS_DOWN,             "\x8e",        1},
	{CTERM_KEY_DOS_DC,               "\x7f",        1},
	{CTERM_KEY_DOS_SHIFT_F(1),       "\x91",        1},
	{CTERM_KEY_DOS_SHIFT_F(2),       "\x92",        1},
	{CTERM_KEY_DOS_SHIFT_F(3),       "\x93",        1},
	{CTERM_KEY_DOS_SHIFT_F(4),       "\x94",        1},
	{CTERM_KEY_DOS_SHIFT_F(5),       "\x95",        1},
	{CTERM_KEY_DOS_SHIFT_F(6),       "\x96",        1},
	{CTERM_KEY_DOS_SHIFT_F(7),       "\x97",        1},
	{CTERM_KEY_DOS_SHIFT_F(8),       "\x98",        1},
	{CTERM_KEY_DOS_SHIFT_F(9),       "\x99",        1},
	{CTERM_KEY_DOS_SHIFT_F(10),      "\x90",        1},	/* F0 */
	{CTERM_KEY_DOS_CTRL_F(1),        "\xa1",        1},
	{CTERM_KEY_DOS_CTRL_F(2),        "\xa2",        1},
	{CTERM_KEY_DOS_CTRL_F(3),        "\xa3",        1},
	{CTERM_KEY_DOS_CTRL_F(4),        "\xa4",        1},
	{CTERM_KEY_DOS_CTRL_F(5),        "\xa5",        1},
	{CTERM_KEY_DOS_CTRL_F(6),        "\xa6",        1},
	{CTERM_KEY_DOS_CTRL_F(7),        "\xa7",        1},
	{CTERM_KEY_DOS_CTRL_F(8),        "\xa8",        1},
	{CTERM_KEY_DOS_CTRL_F(9),        "\xa9",        1},
	{CTERM_KEY_DOS_CTRL_F(10),       "\xa0",        1},	/* F0 */
	{CTERM_KEY_DOS_CTRL_LEFT,        "\xac",        1},
	{CTERM_KEY_DOS_CTRL_RIGHT,       "\xad",        1},
	{CTERM_KEY_DOS_CTRL_END,         "\xab",        1},	/* Copy */
	{CTERM_KEY_DOS_CTRL_UP,          "\xaf",        1},
	{CTERM_KEY_DOS_CTRL_DOWN,        "\xae",        1},
	{CTERM_KEY_DOS_SHIFT_UP,         "\x9f",        1},
	{CTERM_KEY_DOS_SHIFT_LEFT,       "\x9c",        1},
	{CTERM_KEY_DOS_SHIFT_RIGHT,      "\x9d",        1},
	{CTERM_KEY_DOS_SHIFT_END,        "\x9b",        1},	/* Copy */
	{CTERM_KEY_DOS_SHIFT_DOWN,       "\x9e",        1},
};

static const struct key_mapping vt52_keys[] = {
	{CTERM_KEY_DOS_F(1),             "\033P",       2},
	{CTERM_KEY_DOS_F(2),             "\033Q",       2},
	{CTERM_KEY_DOS_F(3),             "\033R",       2},
	{CTERM_KEY_DOS_UP,               "\033A",       2},
	{CTERM_KEY_DOS_LEFT,             "\033D",       2},
	{CTERM_KEY_DOS_RIGHT,            "\033C",       2},
	{CTERM_KEY_DOS_DOWN,             "\033B",       2},
};

static const struct key_mapping ansi_keys[] = {
	{CTERM_KEY_DOS_BACKTAB,          "\033[Z",      3},
	{CTERM_KEY_DOS_F(1),             "\033[11~",    5},
	{CTERM_KEY_DOS_F(2),             "\033[12~",    5},
	{CTERM_KEY_DOS_F(3),             "\033[13~",    5},
	{CTERM_KEY_DOS_F(4),             "\033[14~",    5},
	{CTERM_KEY_DOS_F(5),             "\033[15~",    5},
	{CTERM_KEY_DOS_F(6),             "\033[17~",    5},
	{CTERM_KEY_DOS_F(7),             "\033[18~",    5},
	{CTERM_KEY_DOS_F(8),             "\033[19~",    5},
	{CTERM_KEY_DOS_F(9),             "\033[20~",    5},
	{CTERM_KEY_DOS_F(10),            "\033[21~",    5},
	{CTERM_KEY_DOS_HOME,             "\033[H",      3},
	{CTERM_KEY_DOS_UP,               "\033[A",      3},
	{CTERM_KEY_DOS_PPAGE,            "\033[V",      3},
	{CTERM_KEY_DOS_LEFT,             "\033[D",      3},
	{CTERM_KEY_DOS_RIGHT,            "\033[C",      3},
	{CTERM_KEY_DOS_END,              "\033[K",      3},
#ifdef CTERM_KEY_DOS_SELECT
	{CTERM_KEY_DOS_SELECT,           "\033[K",      3},
#endif
	{CTERM_KEY_DOS_DOWN,             "\033[B",      3},
	{CTERM_KEY_DOS_NPAGE,            "\033[U",      3},
	{CTERM_KEY_DOS_IC,               "\033[@",      3},
	{CTERM_KEY_DOS_SHIFT_F(1),       "\033[11;2~",  7},
	{CTERM_KEY_DOS_SHIFT_F(2),       "\033[12;2~",  7},
	{CTERM_KEY_DOS_SHIFT_F(3),       "\033[13;2~",  7},
	{CTERM_KEY_DOS_SHIFT_F(4),       "\033[14;2~",  7},
	{CTERM_KEY_DOS_SHIFT_F(5),       "\033[15;2~",  7},
	{CTERM_KEY_DOS_SHIFT_F(6),       "\033[17;2~",  7},
	{CTERM_KEY_DOS_SHIFT_F(7),       "\033[18;2~",  7},
	{CTERM_KEY_DOS_SHIFT_F(8),       "\033[19;2~",  7},
	{CTERM_KEY_DOS_SHIFT_F(9),       "\033[20;2~",  7},
	{CTERM_KEY_DOS_SHIFT_F(10),      "\033[21;2~",  7},
	{CTERM_KEY_DOS_CTRL_F(1),        "\033[11;5~",  7},
	{CTERM_KEY_DOS_CTRL_F(2),        "\033[12;5~",  7},
	{CTERM_KEY_DOS_CTRL_F(3),        "\033[13;5~",  7},
	{CTERM_KEY_DOS_CTRL_F(4),        "\033[14;5~",  7},
	{CTERM_KEY_DOS_CTRL_F(5),        "\033[15;5~",  7},
	{CTERM_KEY_DOS_CTRL_F(6),        "\033[17;5~",  7},
	{CTERM_KEY_DOS_CTRL_F(7),        "\033[18;5~",  7},
	{CTERM_KEY_DOS_CTRL_F(8),        "\033[19;5~",  7},
	{CTERM_KEY_DOS_CTRL_F(9),        "\033[20;5~",  7},
	{CTERM_KEY_DOS_CTRL_F(10),       "\033[21;5~",  7},
	{CTERM_KEY_DOS_ALT_F(1),         "\033[11;3~",  7},
	{CTERM_KEY_DOS_ALT_F(2),         "\033[12;3~",  7},
	{CTERM_KEY_DOS_ALT_F(3),         "\033[13;3~",  7},
	{CTERM_KEY_DOS_ALT_F(4),         "\033[14;3~",  7},
	{CTERM_KEY_DOS_ALT_F(5),         "\033[15;3~",  7},
	{CTERM_KEY_DOS_ALT_F(6),         "\033[17;3~",  7},
	{CTERM_KEY_DOS_ALT_F(7),         "\033[18;3~",  7},
	{CTERM_KEY_DOS_ALT_F(8),         "\033[19;3~",  7},
	{CTERM_KEY_DOS_ALT_F(9),         "\033[20;3~",  7},
	{CTERM_KEY_DOS_ALT_F(10),        "\033[21;3~",  7},
	{CTERM_KEY_DOS_F(11),            "\033[23~",    5},
	{CTERM_KEY_DOS_F(12),            "\033[24~",    5},
	{CTERM_KEY_DOS_SHIFT_F(11),      "\033[23;2~",  7},
	{CTERM_KEY_DOS_SHIFT_F(12),      "\033[24;2~",  7},
	{CTERM_KEY_DOS_CTRL_F(11),       "\033[23;5~",  7},
	{CTERM_KEY_DOS_CTRL_F(12),       "\033[24;5~",  7},
	{CTERM_KEY_DOS_ALT_F(11),        "\033[23;3~",  7},
	{CTERM_KEY_DOS_ALT_F(12),        "\033[24;3~",  7},
};

static int
key_mapping_cmp(const void *a, const void *b)
{
	return ((const struct key_mapping *)a)->key
	     - ((const struct key_mapping *)b)->key;
}

static const char *
lookup_key(const struct key_mapping *map, int count, int key, int *len)
{
	struct key_mapping needle = {key, NULL, 0};
	const struct key_mapping *found;

	found = bsearch(&needle, map, count, sizeof(*map), key_mapping_cmp);
	if (found) {
		*len = found->len;
		return found->seq;
	}
	return NULL;
}

static void
verify_key_tables(void)
{
#define CHECK_SORTED(table) do {					\
	for (int i_ = 1; i_ < KEY_MAP_SIZE(table); i_++)		\
		assert((table)[i_ - 1].key < (table)[i_].key);		\
} while (0)

	CHECK_SORTED(atascii_keys);
	CHECK_SORTED(petscii_keys);
	CHECK_SORTED(prestel_keys);
	CHECK_SORTED(prestel_keys_cursor);
	CHECK_SORTED(beeb_keys);
	CHECK_SORTED(vt52_keys);
	CHECK_SORTED(ansi_keys);
#undef CHECK_SORTED
}

static void
toggle_prestel_reveal(struct cterminal *cterm)
{
	cterm_render_set_prestel_reveal(cterm,
	    !cterm->screen.prestel_reveal);
}

static int
cterm_encode_key_ex(struct cterminal *cterm, int key, bool emit)
{
	unsigned char             ch[2];
	int                       slen;
	const char               *seq;
	const struct key_mapping *table;
	int                       table_count;
	int                       raw_lo = 0;
	int                       raw_hi = 256;

	/* Doorway mode: NUL + scancode.  Any high-bit scancode
	 * (key > 0xff with low byte 0) goes through. */
	if (cterm->doorway_mode && (key > 0xff) && ((key & 0xff) == 0)) {
		ch[0] = 0;
		ch[1] = key >> 8;
		if (emit)
			cterm_send_keystroke(cterm, (const char *)ch, 2);
		return 2;
	}

	switch (cterm->emulation) {
		case CTERM_EMULATION_ATASCII:
			if (key == 96) {
				cterm->atascii_inverse = !cterm->atascii_inverse;
				return -1; /* Consumed locally, without encoded bytes. */
			}
			table = atascii_keys;
			table_count = KEY_MAP_SIZE(atascii_keys);
			break;
		case CTERM_EMULATION_PETASCII:
			table = petscii_keys;
			table_count = KEY_MAP_SIZE(petscii_keys);
			break;
		case CTERM_EMULATION_PRESTEL:
		case CTERM_EMULATION_BEEB:
			/* PRESTEL and BEEB: F3 or PgDn toggles reveal. */
			if (key == CTERM_KEY_DOS_F(3) || key == CTERM_KEY_DOS_NPAGE) {
				toggle_prestel_reveal(cterm);
				return -1;
			}
			switch (cterm->emulation) {
				case CTERM_EMULATION_PRESTEL:
					if (cterm->cursor == CTERM_CURSOR_UNDERLINE) {
						table = prestel_keys_cursor;
						table_count = KEY_MAP_SIZE(prestel_keys_cursor);
					}
					else {
						table = prestel_keys;
						table_count = KEY_MAP_SIZE(prestel_keys);
					}
					raw_lo = 32;
					raw_hi = 128;
					break;
				default:
					table = beeb_keys;
					table_count = KEY_MAP_SIZE(beeb_keys);
					raw_hi = 128;
					break;
			}
			break;
		default:
			/* VT52 and ANSI-BBS: DEL and BS respect DECBKM. */
			if (key == CTERM_KEY_DOS_DC) {
				if (cterm->extattr & CTERM_EXTATTR_DECBKM) {
					if (emit)
						cterm_send_keystroke(cterm, "\x7f", 1);
					return 1;
				}
				if (emit)
					cterm_send_keystroke(cterm, "\x1b[3~", 4);
				return 4;
			}
			if (key == '\b') {
				ch[0] = (cterm->extattr & CTERM_EXTATTR_DECBKM) ? '\b' : '\x7f';
				if (emit)
					cterm_send_keystroke(cterm, (const char *)ch, 1);
				return 1;
			}
			switch (cterm->emulation) {
				case CTERM_EMULATION_ATARIST_VT52:
					table = vt52_keys;
					table_count = KEY_MAP_SIZE(vt52_keys);
					break;
				default: /* ANSI-BBS */
					table = ansi_keys;
					table_count = KEY_MAP_SIZE(ansi_keys);
					break;
			}
			break;
	}

	seq = lookup_key(table, table_count, key, &slen);
	if (seq) {
		if (emit)
			cterm_send_keystroke(cterm, seq, slen);
		return slen;
	}
	if (key >= raw_lo && key < raw_hi) {
		ch[0] = key;
		if (emit)
			cterm_send_keystroke(cterm, (const char *)ch, 1);
		return 1;
	}
	return 0;
}

int
cterm_encode_key(struct cterminal *cterm, int key)
{
	int count = cterm_encode_key_ex(cterm, key, true);

	return count < 0 ? 0 : count;
}

enum cterm_key_result
cterm_handle_key(struct cterminal *cterm, int key)
{
	int n = cterm_encode_key_ex(cterm, key,
	    !cterm->suppress_translated_keys);

	return n != 0 ? CTERM_KEY_HANDLED : CTERM_KEY_UNHANDLED;
}

static bool
cterm_pk_get(const struct cterminal *cterm, uint16_t evdev)
{
	if (evdev >= CTERM_PK_MAX_EVDEV)
		return false;
	return (cterm->pk_reported[evdev / 8] & (1 << (evdev % 8))) != 0;
}

static void
cterm_pk_set(struct cterminal *cterm, uint16_t evdev, bool pressed)
{
	if (evdev >= CTERM_PK_MAX_EVDEV)
		return;
	if (pressed)
		cterm->pk_reported[evdev / 8] |= (1 << (evdev % 8));
	else
		cterm->pk_reported[evdev / 8] &= ~(1 << (evdev % 8));
}

static bool
cterm_pk_emit(struct cterminal *cterm, const uint16_t *keys, size_t count,
    bool pressed)
{
	char buf[2048];
	size_t pos = 0;

	if (count == 0 || !cterm->pk_mode)
		return false;
	pos += snprintf(buf + pos, sizeof(buf) - pos, "\x1b[=");
	for (size_t i = 0; i < count; i++) {
		int n = snprintf(buf + pos, sizeof(buf) - pos, "%s%u",
		    i == 0 ? "" : ";", keys[i]);
		if (n < 0 || (size_t)n >= sizeof(buf) - pos)
			return false;
		pos += (size_t)n;
	}
	if (pos + 1 >= sizeof(buf))
		return false;
	buf[pos++] = pressed ? 'K' : 'k';
	cterm_send_keystroke(cterm, buf, pos);
	return true;
}

bool
cterm_pk_events(struct cterminal *cterm, const struct st_console_event *events,
    size_t count)
{
	uint16_t batch[64];
	size_t batch_count = 0;
	bool batch_pressed = false;
	bool emitted = false;

	if (cterm == NULL || events == NULL || count == 0 || !cterm->pk_mode)
		return false;
	for (size_t i = 0; i < count; i++) {
		uint16_t evdev;
		bool pressed;

		if (events[i].type != ST_CONSOLE_EVENT_PHYSICAL_KEY)
			return false;
		evdev = events[i].data.physical_key.evdev;
		pressed = events[i].data.physical_key.pressed;
		if (evdev >= CTERM_PK_MAX_EVDEV)
			continue;
		if (cterm_pk_get(cterm, evdev) == pressed)
			continue;
		cterm_pk_set(cterm, evdev, pressed);
		if (batch_count > 0 &&
		    (batch_pressed != pressed || batch_count == sizeof(batch) / sizeof(batch[0]))) {
			emitted |= cterm_pk_emit(cterm, batch, batch_count, batch_pressed);
			batch_count = 0;
		}
		batch_pressed = pressed;
		batch[batch_count++] = evdev;
	}
	if (batch_count > 0)
		emitted |= cterm_pk_emit(cterm, batch, batch_count, batch_pressed);
	return emitted;
}

bool
cterm_pk_synthesize(struct cterminal *cterm, uint16_t evdev, bool pressed)
{
	struct st_console_event event = {
		.type = ST_CONSOLE_EVENT_PHYSICAL_KEY,
		.data.physical_key = {.evdev = evdev, .pressed = pressed},
	};

	return cterm_pk_events(cterm, &event, 1);
}

bool
cterm_pk_resync(struct cterminal *cterm, const uint16_t *held_keys, size_t count)
{
	bool held[CTERM_PK_MAX_EVDEV] = {0};
	uint16_t batch[64];
	size_t batch_count = 0;
	bool emitted = false;

	if (cterm == NULL || !cterm->pk_mode)
		return false;
	for (size_t i = 0; i < count; i++) {
		if (held_keys[i] < CTERM_PK_MAX_EVDEV)
			held[held_keys[i]] = true;
	}
	for (uint16_t evdev = 0; evdev < CTERM_PK_MAX_EVDEV; evdev++) {
		if (cterm_pk_get(cterm, evdev) && !held[evdev]) {
			cterm_pk_set(cterm, evdev, false);
			batch[batch_count++] = evdev;
			if (batch_count == sizeof(batch) / sizeof(batch[0])) {
				emitted |= cterm_pk_emit(cterm, batch, batch_count, false);
				batch_count = 0;
			}
		}
	}
	if (batch_count > 0) {
		emitted |= cterm_pk_emit(cterm, batch, batch_count, false);
		batch_count = 0;
	}
	for (uint16_t evdev = 0; evdev < CTERM_PK_MAX_EVDEV; evdev++) {
		if (!cterm_pk_get(cterm, evdev) && held[evdev]) {
			cterm_pk_set(cterm, evdev, true);
			batch[batch_count++] = evdev;
			if (batch_count == sizeof(batch) / sizeof(batch[0])) {
				emitted |= cterm_pk_emit(cterm, batch, batch_count, true);
				batch_count = 0;
			}
		}
	}
	if (batch_count > 0)
		emitted |= cterm_pk_emit(cterm, batch, batch_count, true);
	return emitted;
}

bool
cterm_pk_enabled(const struct cterminal *cterm)
{
	return cterm != NULL && cterm->pk_mode;
}

bool
cterm_atascii_inverse(const struct cterminal *cterm)
{
	return cterm->atascii_inverse;
}

void
cterm_clear_selection(struct cterminal *cterm)
{
	cterm->ssa_row = 0;
	cterm->ssa_col = 0;
	cterm->esa_row = 0;
	cterm->esa_col = 0;
}

void
cterm_coord_conv_xy(struct cterminal *cterm, enum cterm_coordinates from_coord,
    enum cterm_coordinates to_coord, int *x, int *y)
{
	if (from_coord == to_coord)
		return;

	if (x) {
		if (from_coord == CTERM_COORD_CURR) {
			if (cterm->extattr & CTERM_EXTATTR_ORIGINMODE)
				from_coord = CTERM_COORD_TERM;
			else
				from_coord = CTERM_COORD_ABSTERM;
		}
		switch(from_coord) {
			case CTERM_COORD_SCREEN:
				break;
			case CTERM_COORD_TERM:
				*x += cterm->left_margin - 1;
				// Fall-through
			case CTERM_COORD_ABSTERM:
				*x += cterm->x - 1;
				break;
			default:
				// Silence warnings
				break;
		}
		if (to_coord == CTERM_COORD_CURR) {
			if (cterm->extattr & CTERM_EXTATTR_ORIGINMODE)
				to_coord = CTERM_COORD_TERM;
			else
				to_coord = CTERM_COORD_ABSTERM;
		}
		switch(to_coord) {
			case CTERM_COORD_SCREEN:
				break;
			case CTERM_COORD_TERM:
				*x -= cterm->left_margin - 1;
				// Fall-through
			case CTERM_COORD_ABSTERM:
				*x -= cterm->x - 1;
				break;
			default:
				// Silence warnings
				break;
		}
	}
	if (y) {
		if (from_coord == CTERM_COORD_CURR) {
			if (cterm->extattr & CTERM_EXTATTR_ORIGINMODE)
				from_coord = CTERM_COORD_TERM;
			else
				from_coord = CTERM_COORD_ABSTERM;
		}
		switch(from_coord) {
			case CTERM_COORD_SCREEN:
				break;
			case CTERM_COORD_TERM:
				*y += cterm->top_margin - 1;
				// Fall-through
			case CTERM_COORD_ABSTERM:
				*y += cterm->y - 1;
				break;
			default:
				// Silence warnings
				break;
		}
		if (to_coord == CTERM_COORD_CURR) {
			if (cterm->extattr & CTERM_EXTATTR_ORIGINMODE)
				to_coord = CTERM_COORD_TERM;
			else
				to_coord = CTERM_COORD_ABSTERM;
		}
		switch(to_coord) {
			case CTERM_COORD_SCREEN:
				break;
			case CTERM_COORD_TERM:
				*y -= cterm->top_margin - 1;
				// Fall-through
			case CTERM_COORD_ABSTERM:
				*y -= cterm->y - 1;
				break;
			default:
				// Silence warnings
				break;
		}
	}
}

void
cterm_coord_get_xy(struct cterminal *cterm, enum cterm_coordinates coord, int *x, int *y)
{
	if (x)
		*x = cterm_render_position_x(cterm);
	if (y)
		*y = cterm_render_position_y(cterm);

	cterm_coord_conv_xy(cterm, CTERM_COORD_CURR, coord, x, y);
}

void
cterm_apply_window(struct cterminal *cterm)
{
	int col, row, max_col, max_row;

	col = CURR_MINX;
	row = CURR_MINY;
	cterm_coord_conv_xy(cterm, CTERM_COORD_CURR, CTERM_COORD_SCREEN, &col, &row);
	max_col = CURR_MAXX;
	max_row = CURR_MAXY;
	cterm_coord_conv_xy(cterm, CTERM_COORD_CURR, CTERM_COORD_SCREEN, &max_col, &max_row);
	cterm_render_set_window(cterm, col, row, max_col, max_row);
}

/* Adjust cterm->height in place without a full reinit.  Used by the
 * DECSSDT callback when toggling between a statusless display and one
 * with a reserved bottom row.  Preserves existing content; on growth
 * blanks the newly-exposed row(s) with the current attribute; on
 * shrink clamps the cursor to the new last row.  bottom_margin follows
 * the old implied bottom — a user-configured DECSTBM region narrower
 * than the screen is left alone. */
void
cterm_resize_rows(struct cterminal *cterm, int new_rows)
{
	int old_rows = cterm->height;
	int tx, ty;

	if (new_rows < 1 || new_rows == old_rows)
		return;

	/* Capture terminal coordinates because changing the console window also
	 * changes the window-relative cursor; restore it after the resize. */
	TERM_XY(&tx, &ty);
	if (ty > new_rows)
		ty = new_rows;
	if (tx > cterm->width)
		tx = cterm->width;

	if (cterm->bottom_margin == old_rows || cterm->bottom_margin > new_rows)
		cterm->bottom_margin = new_rows;
	if (cterm->top_margin > new_rows)
		cterm->top_margin = 1;

	cterm->height = new_rows;
	cterm_apply_window(cterm);
	cterm_set_position(cterm, tx, ty);

	if (new_rows > old_rows) {
		struct st_console_cell *buf = calloc(cterm->width, sizeof(*buf));
		int i, r, sx, sy, ex;

		if (buf) {
			cterm_render_make_current_cell(cterm, &buf[0], ' ', 0);
			for (i = 1; i < cterm->width; i++)
				buf[i] = buf[0];
			for (r = old_rows + 1; r <= new_rows; r++) {
				sx = 1;
				sy = r;
				ex = cterm->width;
				cterm_coord_conv_xy(cterm, CTERM_COORD_TERM,
				    CTERM_COORD_SCREEN, &sx, &sy);
				cterm_coord_conv_xy(cterm, CTERM_COORD_TERM,
				    CTERM_COORD_SCREEN, &ex, NULL);
				cterm_render_write_cells(cterm, sx, sy, ex, sy, buf);
			}
			free(buf);
		}
	}

	if (cterm->size_change_cb) {
		int pixel_cols = -1;
		int pixel_rows = -1;
#ifdef HAS_VSTAT
		/* Normalize pixel dimensions: when the terminal fills every
		 * row/column of the framebuffer, report the framebuffer
		 * extent (vstat.scrnwidth/scrnheight) so the trailing rows
		 * that don't divide evenly into a character cell are still
		 * accounted for (e.g. EGA 80x43 has 350 scanlines but 8-pixel
		 * cells, leaving 6 leftover rows below row 43).  When the
		 * terminal is smaller than the framebuffer (e.g. status row
		 * visible), fall back to cell-size multiplication using the
		 * runtime vstat.charwidth/charheight. */
		assert_rwlock_rdlock(&vstatlock);
		if (vstat.charwidth > 0 && vstat.charheight > 0) {
			pixel_cols = (cterm->width == vstat.cols)
			    ? vstat.scrnwidth
			    : cterm->width * vstat.charwidth;
			pixel_rows = (cterm->height == vstat.rows)
			    ? vstat.scrnheight
			    : cterm->height * vstat.charheight;
		}
		assert_rwlock_unlock(&vstatlock);
#endif
		cterm->size_change_cb(cterm, cterm->width, cterm->height,
		    pixel_cols, pixel_rows, cterm->size_change_cbdata);
	}
}

static void
set_attr(struct cterminal *cterm, unsigned char colour, bool bg)
{
	if (bg) {
		cterm->attr &= 0x8F;
		cterm->attr |= ((colour & 0x07) << 4);
	}
	else {
		cterm->attr &= 0xF8;
		cterm->attr |= (colour & 0x07);
	}
	cterm_render_attribute_colours(cterm, cterm->attr, bg ? NULL : &cterm->fg_color, bg ? &cterm->bg_color : NULL);
	if (cterm->emulation == CTERM_EMULATION_PRESTEL || cterm->emulation == CTERM_EMULATION_BEEB) {
		if (cterm->extattr & CTERM_EXTATTR_PRESTEL_DOUBLE_HEIGHT)
			cterm->cell_flags |= ST_CONSOLE_CELL_DOUBLE_HEIGHT;
		cterm->cell_flags |= ST_CONSOLE_CELL_SAA5050;
		if (cterm->emulation == CTERM_EMULATION_PRESTEL)
			cterm->cell_flags |= ST_CONSOLE_CELL_TERMINAL_FONT;
	}
	if (bg)
		CTERM_FREE_AND_CLEAR(cterm->bg_tc_str);
	else
		CTERM_FREE_AND_CLEAR(cterm->fg_tc_str);
}

void
cterm_set_fgattr(struct cterminal *cterm, unsigned char colour)
{
	set_attr(cterm, colour, false ^ cterm->negative);
}

void
cterm_set_bgattr(struct cterminal *cterm, unsigned char colour)
{
	set_attr(cterm, colour, true ^ cterm->negative);
}

void
cterm_set_position(struct cterminal *cterm, int x, int y)
{
	++cterm->cursor_position_changes;
	cterm_coord_conv_xy(cterm, CTERM_COORD_TERM, CTERM_COORD_CURR, &x, &y);
	cterm_render_set_position(cterm, x, y);
	ABS_XY(&cterm->xpos, &cterm->ypos);
	if (cterm->emulation == CTERM_EMULATION_PRESTEL || cterm->emulation == CTERM_EMULATION_BEEB)
		cterm_prestel_get_state(cterm);
}

/*
 * cterm_crpos() returns the ABSTERM column where a CR would place the
 * cursor for the current row.  The logic mirrors the CR path in
 * cterm_adjust_currpos() above: inside the scroll region CR lands on
 * left_margin; outside it clamps to left_margin only when the cursor
 * was already right of it, otherwise it stays at ABSTERM column 1.
 *
 * Called from ripper.c's cb_accept_introducer() as the cursor-position
 * fallback for the RIP `!` BOL predicate.
 */
 int
cterm_crpos(struct cterminal *cterm)
{
	int tx, ty, ax, ay;

	TERM_XY(&tx, &ty);
	ABS_XY(&ax, &ay);

	if (tx >= TERM_MINX && tx <= TERM_MAXX &&
	    ty >= TERM_MINY && ty <= TERM_MAXY)
		return cterm->left_margin;
	if (ax >= cterm->left_margin)
		return cterm->left_margin;
	return ABS_MINX;
}

void
cterm_extend_tabstops(struct cterminal *cterm, int width)
{
	if (width <= cterm->tab_width)
		return;
	int first = cterm->tab_width == 0 ? 0 : (cterm->tab_width - 1) / 8 + 1;
	/* Even if allocation fails, later growth must not append defaults
	 * before custom stops subsequently set in these columns. */
	cterm->tab_width = width;
	if (cterm->default_tabs) {
		int count = (width - 1) / 8 + 1 - first;
		if (count > 0) {
			size_t total = (size_t)cterm->tab_count + count;
			if (total > SIZE_MAX / sizeof(*cterm->tabs))
				return;
			int *tabs = realloc(cterm->tabs,
			    total * sizeof(*tabs));
			if (tabs == NULL)
				return;
			cterm->tabs = tabs;
			/* Stops in previously exposed columns are retained on shrink,
			 * so every new default sorts after all existing stops. */
			for (int i = 0; i < count; i++)
				cterm->tabs[cterm->tab_count++] = 1 + 8 * (first + i);
		}
	}
}

void
cterm_insert_tabstop(struct cterminal *cterm, int pos)
{
	int i;
	int *new_stops;

	for (i = 0; i < cterm->tab_count && cterm->tabs[i] < pos; i++);
	if (i < cterm->tab_count && cterm->tabs[i] == pos)
		return;

	new_stops = realloc(cterm->tabs, (cterm->tab_count + 1) * sizeof(cterm->tabs[0]));
	if (new_stops == NULL)
		return;
	cterm->tabs = new_stops;
	if (i < cterm->tab_count)
		memmove(&cterm->tabs[i + 1], &cterm->tabs[i], (cterm->tab_count - i) * sizeof(cterm->tabs[0]));
	cterm->tabs[i] = pos;
	cterm->tab_count++;
}

void
cterm_delete_tabstop(struct cterminal *cterm, int pos)
{
	int i;

	for (i = 0; i < cterm->tab_count && cterm->tabs[i] <= pos; i++) {
		if (cterm->tabs[i] == pos) {
			memmove(&cterm->tabs[i], &cterm->tabs[i+1], (cterm->tab_count - i - 1) * sizeof(cterm->tabs[0]));
			cterm->tab_count--;
			return;
		}
	}
	return;
}

void
cterm_insert_vtabstop(struct cterminal *cterm, int line)
{
	int i;
	int *new_stops;

	for (i = 0; i < cterm->vtab_count && cterm->vtabs[i] < line; i++);
	if (i < cterm->vtab_count && cterm->vtabs[i] == line)
		return;

	new_stops = realloc(cterm->vtabs, (cterm->vtab_count + 1) * sizeof(cterm->vtabs[0]));
	if (new_stops == NULL)
		return;
	cterm->vtabs = new_stops;
	if (i < cterm->vtab_count)
		memmove(&cterm->vtabs[i + 1], &cterm->vtabs[i], (cterm->vtab_count - i) * sizeof(cterm->vtabs[0]));
	cterm->vtabs[i] = line;
	cterm->vtab_count++;
}

void
cterm_delete_vtabstop(struct cterminal *cterm, int line)
{
	int i;

	for (i = 0; i < cterm->vtab_count && cterm->vtabs[i] <= line; i++) {
		if (cterm->vtabs[i] == line) {
			memmove(&cterm->vtabs[i], &cterm->vtabs[i+1], (cterm->vtab_count - i - 1) * sizeof(cterm->vtabs[0]));
			cterm->vtab_count--;
			return;
		}
	}
}

/*
 * Fill a rectangular area with a character using current attributes.
 * Coordinates are in SCREEN space.  Caller must validate and clamp.
 */
void
cterm_fill_rect(struct cterminal *cterm, int left, int top, int right, int bottom, uint8_t ch)
{
	int width = right - left + 1;
	int height = bottom - top + 1;
	struct st_console_cell *buf;
	int i, r;

	if (width <= 0 || height <= 0)
		return;
	buf = calloc(width, sizeof(*buf));
	if (buf == NULL)
		return;
	/* Initialize one row of cells with current attributes, no hyperlink */
	cterm_render_make_current_cell(cterm, &buf[0], ch, 0);
	for (i = 1; i < width; i++)
		buf[i] = buf[0];
	/* Write row by row */
	for (r = top; r <= bottom; r++)
		cterm_render_write_cells(cterm, left, r, right, r, buf);
	free(buf);
}

void
cterm_clear_line(struct cterminal *cterm)
{
	int x, y;
	int rm;
	struct st_console_cell *buf;
	int i;
	int width;

	CURR_XY(&x, &y);
	rm = CURR_MAXX;

	width = rm - x + 1;
	if (width < 1)
		return;
	buf = calloc(width, sizeof(*buf));
	if (!buf)
		return;
	for (i = 0; i < width; i++) {
		if (i > 0)
			buf[i] = buf[0];
		else {
			cterm_render_make_current_cell(cterm, &buf[i], ' ', 0);
		}
	}
	cterm_coord_conv_xy(cterm, CTERM_COORD_CURR, CTERM_COORD_SCREEN, &x, &y);
	cterm_coord_conv_xy(cterm, CTERM_COORD_CURR, CTERM_COORD_SCREEN, &rm, NULL);
	cterm_render_write_cells(cterm, x, y, rm, y, buf);
	free(buf);
}

void
cterm_clrblk(struct cterminal *cterm, int sx, int sy, int ex, int ey)
{
	int i;
	struct st_console_cell *buf;
	int chars = (ex - sx + 1) * (ey - sy + 1);

	buf = calloc(chars, sizeof(*buf));
	if (!buf)
		return;
	for (i = 0; i < chars ; i++) {
		if (i > 0)
			buf[i] = buf[0];
		else {
			cterm_render_make_current_cell(cterm, &buf[i], ' ', 0);
		}
	}
	cterm_render_write_cells(cterm, sx, sy, ex, ey, buf);
	free(buf);
}

void
cterm_scrolldown(struct cterminal *cterm)
{
	if (cterm->skypix_engine != NULL) {
		int status = cterm_skypix_hide_cursor(cterm->skypix_engine);
		if (status != 0) {
			cterm_render_record_error(cterm, status);
			return;
		}
	}

	int minx = TERM_MINX;
	int miny = TERM_MINY;
	int maxx = TERM_MAXX;
	int maxy = TERM_MAXY;
	int x,y;
	struct st_console_cell fill;
	cterm_render_make_current_cell(cterm, &fill, ' ', 0);

	cterm_coord_conv_xy(cterm, CTERM_COORD_TERM, CTERM_COORD_SCREEN, &minx, &miny);
	cterm_coord_conv_xy(cterm, CTERM_COORD_TERM, CTERM_COORD_SCREEN, &maxx, &maxy);
	CURR_XY(&x, &y);
	cterm_render_move_cells_clear(cterm, minx, miny, maxx, maxy - 1, minx, miny + 1, &fill);
	cterm_render_set_position(cterm, x, y);
}

static int
history_store_row(struct cterminal *cterm, const struct st_console_cell *cells,
	struct st_pixel_buffer *pixels, size_t width)
{
	if (pixels != NULL && cterm->backpixels == NULL) {
		cterm->backpixels = calloc((size_t)cterm->backlines, sizeof(*cterm->backpixels));
		if (cterm->backpixels == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
	}
	struct st_console_cell *destination = cterm->scrollback +
		(size_t)cterm->backpos * cterm->backwidth;
	memcpy(destination, cells, width * sizeof(*cells));
	if (width < (size_t)cterm->backwidth)
		memset(destination + width, 0, ((size_t)cterm->backwidth - width) * sizeof(*cells));
	if (cterm->backpixels != NULL) {
		st_pixel_buffer_destroy(cterm->backpixels[cterm->backpos]);
		cterm->backpixels[cterm->backpos] = pixels;
	}
	if (cterm->backfilled == cterm->backlines)
		cterm->backstart = (cterm->backstart + 1) % cterm->backlines;
	else
		++cterm->backfilled;
	cterm->backpos = (cterm->backpos + 1) % cterm->backlines;
	return 0;
}

int
cterm_history_append_pixels(struct cterminal *cterm, const struct st_pixel_buffer *strip,
	const struct st_console_cell *cells, uint32_t cell_height)
{
	if (cterm->scrollback == NULL)
		return 0;
	uint32_t old_rows = cterm->backpartial_rows;
	size_t total = (size_t)old_rows + strip->height;
	size_t full = total / cell_height;
	uint32_t remainder = (uint32_t)(total % cell_height);
	size_t count = full + (remainder != 0);
	struct st_pixel_buffer **prepared = calloc(count, sizeof(*prepared));
	struct st_pixel_buffer **table = NULL;
	struct st_console_cell *blank = NULL;
	struct st_console_cell fill = {0};
	int status = prepared == NULL ? IO_ERROR_OUT_OF_MEMORY
		: cterm_render_make_current_cell(cterm, &fill, ' ', 0);
	fill.flags |= ST_CONSOLE_CELL_PIXEL_GRAPHICS;
	fill.hyperlink = 0;
	if (status == 0 && (cells == NULL || old_rows != 0)) {
		blank = malloc((size_t)cterm->backwidth * sizeof(*blank));
		if (blank == NULL)
			status = IO_ERROR_OUT_OF_MEMORY;
		else
			for (int x = 0; x < cterm->backwidth; ++x) blank[x] = fill;
	}
	bool alternate = strip->alternate != NULL ||
		(cterm->backpartial != NULL && cterm->backpartial->alternate != NULL);
	for (size_t row = 0; row < count && status == 0; ++row) {
		status = st_pixel_buffer_create(&prepared[row], strip->width, cell_height, alternate);
		if (status == 0)
			status = st_pixel_buffer_fill(prepared[row], ST_PIXEL_COLOR_RGB_TAG, ST_PIXEL_COLOR_RGB_TAG);
	}
	if (status == 0 && full != 0 && cterm->backpixels == NULL) {
		table = calloc((size_t)cterm->backlines, sizeof(*table));
		if (table == NULL) status = IO_ERROR_OUT_OF_MEMORY;
	}
	/* Prepare every row before publishing anything: allocation failure must
	 * leave both the ring and its pending RGB prefix intact. */
	if (status == 0) {
		for (size_t y = 0; y < total; ++y) {
			const struct st_pixel_buffer *source = y < old_rows ? cterm->backpartial : strip;
			size_t from = (y < old_rows ? y : y - old_rows) * strip->width;
			struct st_pixel_buffer *target = prepared[y / cell_height];
			size_t to = y % cell_height * strip->width;
			memcpy(target->primary + to, source->primary + from, strip->width * sizeof(uint32_t));
			if (alternate)
				memcpy(target->alternate + to, (source->alternate == NULL ? source->primary : source->alternate) + from,
					strip->width * sizeof(uint32_t));
		}
		if (table != NULL) {
			cterm->backpixels = table;
			table = NULL;
		}
		for (size_t row = 0; row < full; ++row) {
			(void)history_store_row(cterm, blank == NULL ? cells + row * cterm->backwidth : blank, prepared[row],
				(size_t)cterm->backwidth);
			prepared[row] = NULL;
		}
		st_pixel_buffer_destroy(cterm->backpartial);
		cterm->backpartial = remainder == 0 ? NULL : prepared[full];
		if (remainder != 0) prepared[full] = NULL;
		cterm->backpartial_rows = remainder;
		cterm->backpartial_cell = fill;
	}
	st_history_free_pixels(prepared, count);
	free(table);
	free(blank);
	return status;
}

size_t
cterm_history_rows(const struct cterminal *cterm)
{
	size_t rows = (size_t)cterm->backfilled + (cterm->backpartial_rows != 0);
	return rows > (size_t)cterm->backlines ? (size_t)cterm->backlines : rows;
}

void
cterm_history_copy_cells(const struct cterminal *cterm, size_t row,
	struct st_console_cell *destination, size_t columns)
{
	if (cterm->backpartial_rows != 0) {
		for (size_t x = 0; x < columns; ++x) destination[x] = cterm->backpartial_cell;
	} else {
		size_t physical = ((size_t)cterm->backstart + row) % (size_t)cterm->backlines;
		memcpy(destination, cterm->scrollback + physical * cterm->backwidth,
			columns * sizeof(*destination));
	}
}

int
cterm_history_copy_pixels(const struct cterminal *cterm, size_t row, struct st_pixel_buffer **result)
{
	if (cterm->backpartial_rows == 0) {
		size_t physical = ((size_t)cterm->backstart + row) % (size_t)cterm->backlines;
		if (cterm->backpixels == NULL || cterm->backpixels[physical] == NULL) {
			*result = NULL;
			return 0;
		}
		return st_pixel_buffer_clone(result, cterm->backpixels[physical]);
	}
	const struct st_pixel_buffer *partial = cterm->backpartial;
	struct st_pixel_buffer *copy = NULL;
	int status = st_pixel_buffer_create(&copy, partial->width, partial->height, true);
	if (status != 0) return status;
	st_pixel_buffer_fill(copy, ST_PIXEL_COLOR_RGB_TAG, ST_PIXEL_COLOR_RGB_TAG);
	size_t skipped = (size_t)cterm->backfilled + 1 - cterm_history_rows(cterm);
	size_t first = (row + skipped) * partial->height;
	size_t pad = partial->height - cterm->backpartial_rows;
	size_t complete = (size_t)cterm->backfilled * partial->height;
	/* Align at the live boundary. Any padding belongs at the oldest end,
	 * never between a pending strip and the current screen. */
	for (size_t y = 0; y < partial->height; ++y) {
		if (first + y < pad) continue;
		size_t at = first + y - pad;
		const struct st_pixel_buffer *source = partial;
		size_t sy;
		if (at < complete) {
			size_t physical = ((size_t)cterm->backstart + at / partial->height) % (size_t)cterm->backlines;
			source = cterm->backpixels[physical];
			sy = at % partial->height;
		} else
			sy = at - complete;
		memcpy(copy->primary + y * copy->width, source->primary + sy * source->width,
			copy->width * sizeof(uint32_t));
		memcpy(copy->alternate + y * copy->width,
			(source->alternate == NULL ? source->primary : source->alternate) + sy * source->width,
			copy->width * sizeof(uint32_t));
	}
	if (memcmp(copy->primary, copy->alternate, (size_t)copy->width * copy->height * sizeof(uint32_t)) == 0)
		st_pixel_buffer_remove_alternate(copy);
	*result = copy;
	return 0;
}

static void
cterm_line_to_scrollback(struct cterminal *cterm, int row)
{
	if (cterm->scrollback == NULL)
		return;
	int width = cterm->backwidth < cterm->width ? cterm->backwidth : cterm->width;
	struct st_console_cell local[256];
	struct st_console_cell *cells = width <= 256 ? local : malloc((size_t)width * sizeof(*cells));
	struct st_pixel_buffer *pixels = NULL;
	int status = cells == NULL ? IO_ERROR_OUT_OF_MEMORY : 0;
	uint32_t cw, ch;
	if (status == 0 && !cterm_render_read_cells(cterm, cterm->x, row,
		cterm->x + width - 1, row, cells))
		status = IO_ERROR_IO;
	if (status == 0)
		status = st_console_cell_pixel_size(cterm->screen.console, &cw, &ch);
	if (status == 0) {
		const struct st_console_rect region = {
			(uint32_t)(cterm->x - 1) * cw, (uint32_t)(row - 1) * ch, (uint32_t)width * cw, ch};
		uint32_t flags = cells[0].flags;
		if (cterm->skypix_engine != NULL) cells[0].flags |= ST_CONSOLE_CELL_PIXEL_GRAPHICS;
		status = st_history_capture_pixels(&pixels, cterm->screen.console, cells,
			(uint32_t)width, &region);
		cells[0].flags = flags;
	}
	if (status == 0) {
		if (cterm->skypix_engine != NULL)
			status = cterm_history_append_pixels(cterm, pixels, cells, ch);
		else {
			status = history_store_row(cterm, cells, pixels, (size_t)width);
			if (status == 0) pixels = NULL;
		}
	}
	cterm_render_record_error(cterm, status);
	st_pixel_buffer_destroy(pixels);
	if (cells != local)
		free(cells);
}

void
cterm_scrollup(struct cterminal *cterm)
{
	if (cterm->skypix_engine != NULL) {
		int status = cterm_skypix_hide_cursor(cterm->skypix_engine);
		if (status != 0) {
			cterm_render_record_error(cterm, status);
			return;
		}
	}

	int minx = TERM_MINX;
	int miny = TERM_MINY;
	int maxx = TERM_MAXX;
	int maxy = TERM_MAXY;
	int x,y;
	struct st_console_cell fill;
	cterm_render_make_current_cell(cterm, &fill, ' ', 0);
	if (cterm->igs_engine != NULL)
		fill.background = cterm_vt52_background_color(cterm);

	cterm_coord_conv_xy(cterm, CTERM_COORD_TERM, CTERM_COORD_SCREEN, &minx, &miny);
	cterm_coord_conv_xy(cterm, CTERM_COORD_TERM, CTERM_COORD_SCREEN, &maxx, &maxy);
	cterm_line_to_scrollback(cterm, miny);
	CURR_XY(&x, &y);
	if (cterm_render_move_cells_clear(cterm, minx, miny + 1, maxx, maxy, minx, miny, &fill))
		cterm_text_scrolled(cterm, miny, maxy);
	cterm_render_set_position(cterm, x, y);
}

void
cterm_cond_scrollup(struct cterminal *cterm)
{
	int x, y;

	TERM_XY(&x, &y);
	if (x >= TERM_MINX && x <= TERM_MAXX &&
	    y >= TERM_MINY && y <= TERM_MAXY)
		cterm_scrollup(cterm);
}

void
cterm_dellines(struct cterminal * cterm, int lines)
{
	int i;
	int minx = TERM_MINX;
	int miny = TERM_MINY;
	int maxx = TERM_MAXX;
	int maxy = TERM_MAXY;
	int sx,sy;
	int x,y;

	if(lines<1)
		return;
	cterm_coord_conv_xy(cterm, CTERM_COORD_TERM, CTERM_COORD_SCREEN, &minx, &miny);
	cterm_coord_conv_xy(cterm, CTERM_COORD_TERM, CTERM_COORD_SCREEN, &maxx, &maxy);
	TERM_XY(&x, &y);
	// Can't delete lines outside of window
	if (x < TERM_MINX || x > TERM_MAXX || y < TERM_MINY || y > TERM_MAXY)
		return;
	SCR_XY(&sx, &sy);
	if (lines > maxy - sy + 1) // Delete all the lines... ie: clear screen...
		lines = maxy - sy + 1;
	if (sy + lines <= maxy)
		cterm_render_move_cells(cterm, minx, sy + lines, maxx, maxy, minx, sy);
	for(i = TERM_MAXY - lines + 1; i <= TERM_MAXY; i++) {
		cterm_set_position(cterm, TERM_MINX, i);
		cterm_clear_line(cterm);
	}
	cterm_set_position(cterm, x, y);
}

void
cterm_clear2bol(struct cterminal * cterm)
{
	struct st_console_cell *buf;
	int i;
	int x, y;
	int minx = CURR_MINX;

	CURR_XY(&x, &y);
	buf = calloc(x, sizeof(*buf));
	if (buf) {
		for(i = 0; i < x; i++) {
			if (i > 0)
				buf[i] = buf[0];
			else {
				cterm_render_make_current_cell(cterm, &buf[i], ' ', 0);
			}
		}
		cterm_coord_conv_xy(cterm, CTERM_COORD_CURR, CTERM_COORD_SCREEN, &x, &y);
		cterm_coord_conv_xy(cterm, CTERM_COORD_CURR, CTERM_COORD_SCREEN, &minx, NULL);
		cterm_render_write_cells(cterm, minx, y, x, y, buf);
		free(buf);
	}
	else {
		cterm_log(cterm, ST_CTERM_LOG_ERROR, "Unable to allocate clear-to-beginning-of-line buffer");
	}
}

void
cterm_clearscreen(struct cterminal *cterm, char attr)
{
	if (cterm->skypix_engine != NULL) {
		int status = cterm_skypix_hide_cursor(cterm->skypix_engine);
		if (status != 0) {
			cterm_render_record_error(cterm, status);
			return;
		}
	}

	(void)attr;
	if(!cterm->started)
		cterm_start(cterm);

	/* Save a complete display snapshot from the terminal origin. Starting
	 * at the scrolling margin shifts the page and reads past its bottom. */
	for (int i = 0; i < cterm->height; ++i)
		cterm_line_to_scrollback(cterm, cterm->y + i);
	cterm_render_clear_window(cterm);
	cterm_render_set_position(cterm, CURR_MINX, CURR_MINY);
	if (cterm->skypix_engine != NULL)
		cterm_skypix_home(cterm->skypix_engine);
}

/*
 * Parses an ESC sequence and returns it broken down
 */


bool cterm_parse_sub_parameters(struct sub_params *sub, struct cterminal *cterm, int param)
{
	int i;
	char *p;

	sub->param_count = 1;
	sub->param_int = NULL;

	if (param < 0 || param >= cterm->seq_param_count)
		return false;
	for (p = (char *)cterm->seq_param_strs[param]; *p; p++) {
		if (*p == ':') {
			if (sub->param_count == INT_MAX)
				return false;
			sub->param_count++;
		}
	}
	if (sub->param_count == 0)
		return true;
	sub->param_int = malloc(sub->param_count * sizeof(sub->param_int[0]));
	if (sub->param_int == NULL)
		return false;
	p = (char *)cterm->seq_param_strs[param];
	for (i = 0; i < sub->param_count; i++) {
		p++;
		sub->param_int[i] = strtoull(p, &p, 10);
		if (*p != ':' && *p != 0) {
			CTERM_FREE_AND_CLEAR(sub->param_int);
			return false;
		}
	}
	return true;
}

void cterm_free_sub_parameters(struct sub_params *sub)
{
	CTERM_FREE_AND_CLEAR(sub->param_int);
}

/* ------------------------------------------------------------------------
 * Dispatch-table helpers
 *
 * cterm_seq_dispatch_cmp is the bsearch() comparator used by every
 * accumulator that looks up a completed sequence in a per-emulation
 * dispatch table (cterm_ansi_dispatch[], cterm_st_vt52_dispatch[]).
 * Tables must be sorted ascending by the 21-bit key for this to work.
 *
 * cterm_seq_default fills in a default value for a parameter slot on
 * cterm->seq_param_int[].  seq_feed() stores UINT64_MAX in slots that
 * were explicitly empty (e.g. the first param of "CSI ;5H") and leaves
 * slots past seq_param_count untouched; handlers call this once per
 * parameter that has a non-zero default to promote those sentinel /
 * missing slots to the real default before reading them.
 * ------------------------------------------------------------------------ */

int
cterm_seq_dispatch_cmp(const void *key, const void *entry)
{
	uint32_t k = *(const uint32_t *)key;
	uint32_t e = ((const struct seq_dispatch *)entry)->key;
	return (k > e) - (k < e);
}

void
cterm_seq_default(struct cterminal *cterm, int index, uint64_t val)
{
	if (index < 0 || index >= MAX_SEQ_PARAMS)
		return;
	if (index >= cterm->seq_param_count) {
		for (int i = cterm->seq_param_count; i <= index; i++)
			cterm->seq_param_int[i] = UINT64_MAX;
		cterm->seq_param_count = index + 1;
	}
	if (cterm->seq_param_int[index] == UINT64_MAX)
		cterm->seq_param_int[index] = val;
}

int
cterm_setpalette(struct cterminal *cterm, uint32_t entry, uint16_t r, uint16_t g, uint16_t b)
{
	const uint32_t palette_offset = cterm->screen.capabilities & ST_CONSOLE_CAP_EXTENDED_PALETTE ? 16 : 0;
	if (entry == 7 + palette_offset)
		cterm->default_fg_palette = (uint32_t)r >> 8 << 16 | (uint32_t)g >> 8 << 8 | (uint32_t)b >> 8;
	else if (entry == palette_offset)
		cterm->default_bg_palette = (uint32_t)r >> 8 << 16 | (uint32_t)g >> 8 << 8 | (uint32_t)b >> 8;
	return cterm_render_set_palette(cterm, entry, r, g, b);
}

int
cterm_resetpalette(struct cterminal *cterm, uint32_t entry)
{
	const uint32_t palette_offset = cterm->screen.capabilities
	    & ST_CONSOLE_CAP_EXTENDED_PALETTE ? 16 : 0;
	uint8_t red;
	uint8_t green;
	uint8_t blue;

	if (!cterm_render_default_palette(entry, &red, &green, &blue))
		return 0;
	if (cterm->yellow_is_yellow && entry == 3) {
		uint32_t defaults[3];
		if (st_console_get_default_palette(0, defaults, 3) == 0) {
			red = (uint8_t)(defaults[1] >> 16);
			green = (uint8_t)(defaults[2] >> 8);
			blue = (uint8_t)defaults[0];
		}
	}
	return cterm_setpalette(cterm, entry + palette_offset,
	    (uint16_t)red << 8 | red, (uint16_t)green << 8 | green,
	    (uint16_t)blue << 8 | blue);
}

void
cterm_do_tab(struct cterminal *cterm)
{
	int i;
	int cx, cy, ox;
	int lm, rm, bm;

	rm = TERM_MAXX;
	bm = TERM_MAXY;
	cterm_coord_conv_xy(cterm, CTERM_COORD_TERM, CTERM_COORD_CURR, &rm, &bm);
	lm = TERM_MINX;
	cterm_coord_conv_xy(cterm, CTERM_COORD_TERM, CTERM_COORD_CURR, &lm, NULL);

	CURR_XY(&cx, &cy);
	ox = cx;
	for (i = 0; i < cterm->tab_count; i++) {
		if (cterm->tabs[i] > cx) {
			cx = cterm->tabs[i];
			break;
		}
	}
	if ((ox > rm && cx > CURR_MAXX) || (ox <= rm && cx > rm) || i == cterm->tab_count) {
		cx = lm;
		if (cy == bm) {
			cterm_cond_scrollup(cterm);
			cy = bm;
		}
		else if(cy < CURR_MAXY)
			cy++;
	}
	cterm_render_set_position(cterm, cx, cy);
}

void
cterm_do_backtab(struct cterminal *cterm)
{
	int i;
	int cx, cy, ox;
	int lm;

	lm = TERM_MINX;
	cterm_coord_conv_xy(cterm, CTERM_COORD_TERM, CTERM_COORD_CURR, &lm, NULL);

	CURR_XY(&cx, &cy);
	ox = cx;
	for (i = cterm->tab_count - 1; i >= 0; i--) {
		if (cterm->tabs[i] < cx) {
			cx = cterm->tabs[i];
			break;
		}
	}
	if (ox >= lm && cx < lm)
		cx = lm;
	cterm_render_set_position(cterm, cx, cy);
}

void
cterm_do_vtab(struct cterminal *cterm)
{
	int i;
	int cx, cy;
	int bm;

	bm = TERM_MAXY;
	cterm_coord_conv_xy(cterm, CTERM_COORD_TERM, CTERM_COORD_CURR, NULL, &bm);

	CURR_XY(&cx, &cy);
	for (i = 0; i < cterm->vtab_count; i++) {
		if (cterm->vtabs[i] > cy) {
			cy = cterm->vtabs[i];
			break;
		}
	}
	if (i == cterm->vtab_count) {
		/* No next line tab stop — scroll up one, cursor on last row */
		cterm_cond_scrollup(cterm);
		cy = bm;
	}
	cterm_render_set_position(cterm, cx, cy);
}

void
cterm_adjust_currpos(struct cterminal *cterm, int xadj, int yadj, int scroll)
{
	int x, y;
	int tx, ty;

	/*
	 * If we're inside the scrolling margins, we simply move inside
	 * them, and scrolling works.
	 */
	TERM_XY(&tx, &ty);
	if (tx >= TERM_MINX && tx <= TERM_MAXX &&
	    ty >= TERM_MINY && ty <= TERM_MAXY) {
		if (xadj == INT_MIN)
			tx = TERM_MINX;
		else
			tx += xadj;
		if (yadj == INT_MIN)
			ty = TERM_MINX;
		else
			ty += yadj;
		if (cterm->emulation == CTERM_EMULATION_PRESTEL || cterm->emulation == CTERM_EMULATION_BEEB) {
			while (ty > TERM_MAXY) {
				ty -= TERM_MAXY;
			}
			while (ty < TERM_MINY) {
				ty += TERM_MAXY;
			}
		}
		if (scroll) {
			while(ty > TERM_MAXY) {
				cterm_scrollup(cterm);
				ty--;
			}
			while(ty < TERM_MINY) {
				cterm_scrolldown(cterm);
				ty++;
			}
		}
		else {
			if (ty > TERM_MAXY)
				ty = TERM_MAXY;
			if (ty < TERM_MINY)
				ty = TERM_MINY;
		}
		if (tx > TERM_MAXX)
			tx = TERM_MAXX;
		if (tx < TERM_MINX)
			tx = TERM_MINX;
		cterm_set_position(cterm, tx, ty);
		return;
	}
	/*
	 * Outside of the scrolling margins, we can cross a margin
	 * into the scrolling region, but can't cross going out.
	 */
	cterm_coord_conv_xy(cterm, CTERM_COORD_TERM, CTERM_COORD_CURR, &tx, &ty);
	x = tx;
	y = ty;
	if (xadj == INT_MIN)
		tx = TERM_MINX;
	else
		tx += xadj;
	if (yadj == INT_MIN)
		ty = TERM_MINX;
	else
		ty += yadj;
	if (x <= cterm->right_margin && tx > cterm->right_margin)
		tx = cterm->right_margin;
	if (y <= cterm->bottom_margin && ty > cterm->bottom_margin)
		ty = cterm->bottom_margin;
	if (tx < cterm->left_margin && x >= cterm->left_margin)
		tx = cterm->left_margin;
	if (ty < cterm->top_margin && y >= cterm->top_margin)
		ty = cterm->top_margin;
	cterm_render_set_position(cterm, tx, ty);
	if (cterm->emulation == CTERM_EMULATION_PRESTEL || cterm->emulation == CTERM_EMULATION_BEEB)
		cterm_prestel_get_state(cterm);
}

void
cterm_clear_lcf(struct cterminal *cterm)
{
	cterm->last_column_flag &= ~CTERM_LCF_SET;
}

/* ------------------------------------------------------------------------
 * cterm_skypix_color — SkyPix palette translation.
 *
 * SkyPix is its own graphics mode with a 16-colour palette in a
 * different order than the standard ANSI/IBM palette.  The ECMA-48
 * SGR colour cases (30-37, 40-47) call this inline so the same handler
 * paints the right index whether or not SkyPix is active: when
 * cterm->skypix is clear, this is a no-op pass-through.  Lives in
 * cterm.c rather than any emulation-specific file because SkyPix
 * interacts with the base attribute path rather than sitting cleanly
 * inside one standard.
 * ------------------------------------------------------------------------ */
unsigned char
cterm_skypix_color(struct cterminal *cterm, unsigned char color)
{
	if (!cterm->skypix)
		return color;
	switch (color) {
		case CTERM_COLOR_BLACK:        return 0;
		case CTERM_COLOR_RED:          return 3;
		case CTERM_COLOR_GREEN:        return 4;
		case CTERM_COLOR_BROWN:        return 6;
		case CTERM_COLOR_BLUE:         return 1;
		case CTERM_COLOR_MAGENTA:      return 7;
		case CTERM_COLOR_CYAN:         return 5;
		case CTERM_COLOR_LIGHT_GRAY:    return 2;
		case CTERM_COLOR_DARK_GRAY:     return 8;
		case CTERM_COLOR_LIGHT_RED:     return 11;
		case CTERM_COLOR_LIGHT_GREEN:   return 12;
		case CTERM_COLOR_YELLOW:       return 14;
		case CTERM_COLOR_LIGHT_BLUE:    return 9;
		case CTERM_COLOR_LIGHT_MAGENTA: return 15;
		case CTERM_COLOR_LIGHT_CYAN:    return 13;
		case CTERM_COLOR_WHITE:        return 10;
	}
	return color;
}

void
cterm_set_negative(struct cterminal *cterm, bool on)
{
	unsigned char bg;
	unsigned char fg;
	uint32_t tmp_colour;

	// TODO: This one is great...
	if (on != cterm->negative) {
		if (cterm->emulation == CTERM_EMULATION_ATARIST_VT52) {
			bg = cterm->attr & 0xF0;
			fg = cterm->attr & 0x0F;
			cterm->attr = (bg >> 4) | (fg << 4);
			tmp_colour = cterm->fg_color;
			cterm->fg_color = cterm->bg_color;
			cterm->bg_color = tmp_colour;
			cterm->negative = on;
		}
		else {
			bg = cterm->attr & 0x70;
			fg = cterm->attr & 0x07;
			cterm->attr &= 0x88;
			cterm->attr |= (bg >> 4);
			cterm->attr |= (fg << 4);
			tmp_colour = cterm->fg_color;
			cterm->fg_color = cterm->bg_color;
			cterm->bg_color = tmp_colour;
			cterm->negative = on;
		}
	}
}

/* ------------------------------------------------------------------------
 * cterm_apply_sgr — chain-of-responsibility root for SGR parameter
 * application.  Each level (cterm -> DEC -> ECMA-48) handles the
 * parameters it owns and tail-calls the next level if the param
 * wasn't its to act on; ECMA-48 is the base case and silently ignores
 * anything nobody claimed.  ECMA-48 reserves unassigned SGR values,
 * so extensions (38/48 extended colour, 90-107 aixterm bright, any
 * future DEC additions) have to intercept before the ECMA-48 table
 * is consulted.
 *
 * Used from the CSI m handler (cterm_handle_sgr) and from DECCARA.
 * ------------------------------------------------------------------------ */
void
cterm_apply_sgr(struct cterminal *cterm, int *pi)
{
	cterm_seq_default(cterm, *pi, 0);
	cterm_cterm_apply_sgr(cterm, pi);
}

/* ------------------------------------------------------------------------
 * ANSI-BBS dispatch table (sorted ascending by key).
 *
 * New entries are added here as do_ansi()'s switch cases are migrated.
 * When the table covers every sequence that do_ansi() handles, the
 * legacy switch will be removed and seq_feed() becomes the parser.
 * ------------------------------------------------------------------------ */

const struct seq_dispatch cterm_ansi_dispatch[] = {
	/* Entries are sorted ascending by seq_key.  With the current bit
	 * layout (intro > final > priv > interm) that groups by introducer,
	 * then by final byte within each introducer.  Variants sharing a
	 * final byte (plain CSI, CSI = ..., CSI ? ..., CSI $ ..., CSI * ...)
	 * therefore appear consecutively. */

	/* ESC + Fp/Fs (intro = ESC = 0x1B) — by final byte */
	{ cterm_handle_decsc,     SEQ_DECSC, 0 },			/* ESC 7 */
	{ cterm_handle_decrc,     SEQ_DECRC, 0 },			/* ESC 8 */
	{ cterm_handle_ris,       SEQ_RIS,   0 },			/* ESC c */

	/* C1 bare controls with introducer < CSI (0x5B) */
	{ cterm_handle_nel,       SEQ_NEL,       0 },			/* ESC E */
	{ cterm_handle_ssa,       SEQ_C1('F'),   0 },			/* ESC F */
	{ cterm_handle_esa,       SEQ_C1('G'),   0 },			/* ESC G */
	{ cterm_handle_hts,       SEQ_HTS,       0 },			/* ESC H */
	{ cterm_handle_vts,       SEQ_C1('J'),   0 },			/* ESC J */
	{ cterm_handle_ri,        SEQ_RI,        0 },			/* ESC M */
	{ cterm_handle_dcs_start, SEQ_DCS,       0 },			/* ESC P */
	{ cterm_handle_sts,       SEQ_STS,       0 },			/* ESC S */
	{ cterm_handle_sos_start, SEQ_SOS,       0 },			/* ESC X */

	/* CSI family (intro = CSI) — sorted by final byte, then priv, then interm */
	{ cterm_handle_ich, SEQ_ICH, 0 },					/* CSI @ */
	{ cterm_handle_sl,
	  (SEQ_CSI | SEQ_INTERM(' ') | SEQ_FINAL('@')), 0 },			/* CSI SP @ */
	{ cterm_handle_cuu, SEQ_CUU, 0 },					/* CSI A */
	{ cterm_handle_sr,
	  (SEQ_CSI | SEQ_INTERM(' ') | SEQ_FINAL('A')), 0 },			/* CSI SP A */
	{ cterm_handle_cud, SEQ_CUD, 0 },					/* CSI B */
	{ cterm_handle_cuf, SEQ_CUF, 0 },					/* CSI C */
	{ cterm_handle_cub, SEQ_CUB, 0 },					/* CSI D */
	{ cterm_handle_fnt,
	  (SEQ_CSI | SEQ_INTERM(' ') | SEQ_FINAL('D')), 0 },			/* CSI SP D */
	{ cterm_handle_cnl, SEQ_CNL, 0 },					/* CSI E */
	{ cterm_handle_cpl, SEQ_CPL, 0 },					/* CSI F */
	{ cterm_handle_cha, SEQ_CHA, 0 },					/* CSI G */
	{ cterm_handle_cup, SEQ_CUP, 0 },					/* CSI H */
	{ cterm_handle_cht, SEQ_CHT, 0 },					/* CSI I */
	{ cterm_handle_ed,  SEQ_ED,  0 },					/* CSI J */
	{ cterm_handle_el,  SEQ_EL,  0 },					/* CSI K */
	{ cterm_handle_il,  SEQ_IL,  0 },					/* CSI L */
	{ cterm_handle_ansi_music, SEQ_DL, 0 },					/* CSI M (DL or ANSI music) */
	{ cterm_handle_ansi_music_setup,
	  (SEQ_CSI | SEQ_PRIV('=') | SEQ_FINAL('M')), 0 },			/* CSI = M */
	{ cterm_handle_bansi_music,
	  (SEQ_CSI | SEQ_FINAL('N')), 0 },					/* CSI N */
	{ cterm_handle_dch, SEQ_DCH, 0 },					/* CSI P */
	{ cterm_handle_su,  SEQ_SU,  0 },					/* CSI S */
	{ cterm_handle_xterm_gfx_query,
	  (SEQ_CSI | SEQ_PRIV('?') | SEQ_FINAL('S')), 0 },			/* CSI ? S */
	{ cterm_handle_sd,  SEQ_SD,  0 },					/* CSI T */
	{ cterm_handle_ech, SEQ_ECH, 0 },					/* CSI X */
	{ cterm_handle_cvt, SEQ_CVT, 0 },					/* CSI Y */
	{ cterm_handle_cbt, SEQ_CBT, 0 },					/* CSI Z */
	{ cterm_handle_cha, SEQ_HPA, 0 },					/* CSI ` (HPA — alias of CHA) */
	{ cterm_handle_hpr, SEQ_HPR, 0 },					/* CSI a */
	{ cterm_handle_rep, SEQ_REP, 0 },					/* CSI b */
	{ cterm_handle_da,  SEQ_DA,  0 },					/* CSI c */
	{ cterm_handle_syncterm_ext_da,
	  (SEQ_CSI | SEQ_PRIV('<') | SEQ_FINAL('c')), 0 },			/* CSI < c */
	{ cterm_handle_vpa, SEQ_VPA, 0 },					/* CSI d */
	{ cterm_handle_tsr,
	  (SEQ_CSI | SEQ_INTERM(' ') | SEQ_FINAL('d')), 0 },			/* CSI SP d */
	{ cterm_handle_vpr, SEQ_VPR, 0 },					/* CSI e */
	{ cterm_handle_cup, SEQ_HVP, 0 },					/* CSI f (HVP — alias of CUP) */
	{ cterm_handle_tbc, SEQ_TBC, 0 },					/* CSI g */
	{ cterm_handle_sm,  SEQ_SM,  0 },					/* CSI h */
	{ cterm_handle_syncterm_sm_ext,
	  (SEQ_CSI | SEQ_PRIV('=') | SEQ_FINAL('h')), 0 },			/* CSI = h */
	{ cterm_handle_decsm_cterm, SEQ_DECSM, 0 },				/* CSI ? h (cascade: cterm -> dec) */
	{ cterm_handle_hpb, SEQ_HPB, 0 },					/* CSI j */
	{ cterm_handle_vpb, SEQ_VPB, 0 },					/* CSI k */
	{ cterm_handle_rm,  SEQ_RM,  0 },					/* CSI l */
	{ cterm_handle_syncterm_rm_ext,
	  (SEQ_CSI | SEQ_PRIV('=') | SEQ_FINAL('l')), 0 },			/* CSI = l */
	{ cterm_handle_decrm_cterm, SEQ_DECRM, 0 },				/* CSI ? l (cascade: cterm -> dec) */
	{ cterm_handle_sgr, SEQ_SGR, 0 },					/* CSI m */
	{ cterm_handle_dsr_cterm, SEQ_DSR, 0 },					/* CSI n (cascade: cterm -> ecma48) */
	{ cterm_handle_ext_state_query,
	  (SEQ_CSI | SEQ_PRIV('=') | SEQ_FINAL('n')), 0 },			/* CSI = n */
	{ cterm_handle_macro_query,
	  (SEQ_CSI | SEQ_PRIV('?') | SEQ_FINAL('n')), 0 },			/* CSI ? n */
	{ cterm_handle_decrqm_public,
	  (SEQ_CSI | SEQ_INTERM('$') | SEQ_FINAL('p')), 0 },			/* CSI $ p */
	{ cterm_handle_decrqm_syncterm,
	  (SEQ_CSI | SEQ_PRIV('=') | SEQ_INTERM('$') | SEQ_FINAL('p')), 0 },	/* CSI = $ p */
	{ cterm_handle_decrqm_dec,
	  (SEQ_CSI | SEQ_PRIV('?') | SEQ_INTERM('$') | SEQ_FINAL('p')), 0 },	/* CSI ? $ p */
	{ cterm_handle_decscusr, SEQ_DECSCUSR, 0 },				/* CSI SP q */
	{ cterm_handle_decstbm, SEQ_DECSTBM, 0 },				/* CSI r */
	{ cterm_handle_deccara,
	  (SEQ_CSI | SEQ_INTERM('$') | SEQ_FINAL('r')), 0 },			/* CSI $ r */
	{ cterm_handle_decscs,
	  (SEQ_CSI | SEQ_INTERM('*') | SEQ_FINAL('r')), 0 },			/* CSI * r */
	{ cterm_handle_csi_s, SEQ_SCOSC, 0 },					/* CSI s (SCOSC or DECSLRM) */
	{ cterm_handle_save_dec_modes,
	  (SEQ_CSI | SEQ_PRIV('?') | SEQ_FINAL('s')), 0 },			/* CSI ? s */
	{ cterm_handle_csi_t,
	  (SEQ_CSI | SEQ_FINAL('t')), 0 },					/* CSI t */
	{ cterm_handle_decrara,
	  (SEQ_CSI | SEQ_INTERM('$') | SEQ_FINAL('t')), 0 },			/* CSI $ t */
	{ cterm_handle_scorc, SEQ_SCORC, 0 },					/* CSI u */
	{ cterm_handle_restore_dec_modes,
	  (SEQ_CSI | SEQ_PRIV('?') | SEQ_FINAL('u')), 0 },			/* CSI ? u */
	{ cterm_handle_deccra, SEQ_DECCRA, 0 },					/* CSI $ v */
	{ cterm_handle_dectabsr, SEQ_DECTABSR, 0 },				/* CSI $ w */
	{ cterm_handle_decfra, SEQ_DECFRA, 0 },					/* CSI $ x */
	{ cterm_handle_decsace, SEQ_DECSACE, 0 },				/* CSI * x */
	{ cterm_handle_deccksr,
	  (SEQ_CSI | SEQ_INTERM('*') | SEQ_FINAL('y')), 0 },			/* CSI * y */
	{ cterm_handle_decera, SEQ_DECERA, 0 },					/* CSI $ z */
	{ cterm_handle_decmacro,
	  (SEQ_CSI | SEQ_INTERM('*') | SEQ_FINAL('z')), 0 },			/* CSI * z */
	{ cterm_handle_font_load,
	  (SEQ_CSI | SEQ_PRIV('=') | SEQ_FINAL('{')), 0 },			/* CSI = { */
	{ cterm_handle_syncterm_music,
	  (SEQ_CSI | SEQ_FINAL('|')), 0 },					/* CSI | */
	{ cterm_handle_decsasd, SEQ_DECSASD, 0 },				/* CSI $ } */
	{ cterm_handle_decic, SEQ_DECIC, 0 },					/* CSI ' } */
	{ cterm_handle_decssdt, SEQ_DECSSDT, 0 },				/* CSI $ ~ */
	{ cterm_handle_decdc, SEQ_DECDC, 0 },					/* CSI ' ~ */

	/* C1 bare controls with introducer > CSI */
	{ cterm_handle_st,        SEQ_ST,  0 },					/* ESC \ (ST) */
	{ cterm_handle_osc_start, SEQ_OSC, 0 },					/* ESC ] */
	{ cterm_handle_pm_start,  SEQ_PM,  0 },					/* ESC ^ */
	{ cterm_handle_apc_start, SEQ_APC, 0 },					/* ESC _ */
};
const size_t cterm_ansi_dispatch_len = SEQ_ARRAY_SIZE(cterm_ansi_dispatch);


void
cterm_reset(struct cterminal *cterm)
{
	cterm_igs_reset(cterm->igs_engine);
	cterm_dec_sixel_cancel(cterm);
	(void)st_music_flush(cterm->music_player, 0);
	/* Retained history and all three ring indices are session state;
	 * resetting only the write position corrupts its chronological order. */
	cterm_text_forget(cterm);
	st_unicode_presentation_reset(cterm->screen.unicode);
	int  i;

	cterm->altfont[0] = cterm->altfont[1] = cterm->altfont[2] = cterm->altfont[3] = cterm_render_font_selector(cterm, 1);
	cterm->top_margin=1;
	cterm->bottom_margin=cterm->height;
	cterm->left_margin=1;
	cterm->right_margin=cterm->width;
	cterm->save_xpos=0;
	cterm->save_ypos=0;
	cterm->ssa_row = 0;
	cterm->ssa_col = 0;
	cterm->esa_row = 0;
	cterm->esa_col = 0;
	cterm->fetm = 0;
	cterm->ttm = 0;
	cterm->escbuf[0]=0;
	cterm->sequence=0;
	cterm->string = 0;
	cterm->accumulator = NULL;
	cterm->saved_accumulator = NULL;
	CTERM_FREE_AND_CLEAR(cterm->strbuf);
	cterm->strbuflen = 0;
	cterm->strbufsize = 0;
	cterm->musicbuf[0] = 0;
	cterm->music_enable=CTERM_MUSIC_BANSI;
	cterm->music=0;
	cterm->tempo=120;
	cterm->octave=4;
	cterm->notelen=4;
	cterm->noteshape=CTERM_MUSIC_NORMAL;
	cterm->musicfore=true;
	cterm->xpos = TERM_MINX;
	cterm->ypos = TERM_MINY;
	cterm->cursor=CTERM_CURSOR_UNDERLINE;
	cterm->cursor_style = 0;
	cterm->extattr = CTERM_EXTATTR_AUTOWRAP | CTERM_EXTATTR_SXSCROLL | CTERM_EXTATTR_DECBKM;
	cterm->cell_flags = 0;
	if (cterm->emulation == CTERM_EMULATION_PRESTEL
	    || cterm->emulation == CTERM_EMULATION_BEEB)
		cterm->cell_flags |= ST_CONSOLE_CELL_SAA5050;
	if (cterm->emulation == CTERM_EMULATION_PRESTEL)
		cterm->cell_flags |= ST_CONSOLE_CELL_TERMINAL_FONT;
	if (cterm->emulation == CTERM_EMULATION_ATARIST_VT52)
		cterm->extattr &= ~CTERM_EXTATTR_AUTOWRAP;
	CTERM_FREE_AND_CLEAR(cterm->tabs);
	cterm->tab_count = 0;
	cterm->tab_width = 0;
	cterm->default_tabs = true;
	/* Exhausting the list already wraps HT; no off-screen stop is needed. */
	cterm_extend_tabstops(cterm, cterm->width);
	CTERM_FREE_AND_CLEAR(cterm->vtabs);
	cterm->vtab_count = 0;
	cterm->decsace = 0;
	cterm->decscs_speed = 0;
	cterm->setfont_result = CTERM_NO_SETFONT_REQUESTED;
	cterm->saved_mode = 0;
	cterm->saved_mode_mask = 0;
	cterm->negative = false;
	cterm->c64reversemode = false;
	switch (cterm->screen.mode) {
		case CTERM_MODE_C64_40:
		case CTERM_MODE_C128_40:
		case CTERM_MODE_C128_80:
			cterm->attr = 15;
			break;
		default:
			cterm->attr = cterm->screen.default_attribute;
			break;
	}
	cterm_vt52_reset_console(cterm);
	cterm_render_attribute_colours(cterm, cterm->attr, &cterm->fg_color, &cterm->bg_color);
	if (cterm->last_column_flag & CTERM_LCF_FORCED)
		cterm->last_column_flag &= ~(CTERM_LCF_SET);
	else
		cterm->last_column_flag &= ~(CTERM_LCF_SET | CTERM_LCF_ENABLED);
	cterm->doorway_mode = 0;
	cterm->doorway_char = 0;
	CTERM_FREE_AND_CLEAR(cterm->fg_tc_str);
	CTERM_FREE_AND_CLEAR(cterm->bg_tc_str);
	cterm->sixel = SIXEL_INACTIVE;
	cterm->sx_iv = 0;
	cterm->sx_ih = 0;
	cterm->sx_trans = 0;
	cterm->sx_repeat = 0;
	cterm->sx_left = 0;
	cterm->sx_x = 0;
	cterm->sx_y = 0;
	cterm->sx_bg = 0;
	cterm->sx_fg = 0;
	cterm->sx_pixels_sent = 0;
	cterm->sx_first_pass = 0;
	cterm->sx_hold_update = 0;
	cterm->sx_start_x = 0;
	cterm->sx_start_y = 0;
	cterm->sx_row_max_x = 0;
	cterm->sx_width = 0;
	cterm->sx_height = 0;
	cterm_render_set_hyperlink(cterm, 0);
	for (i = 0; i < (int)(sizeof(cterm->macros)
	    / sizeof(cterm->macros[0])); i++) {
		CTERM_FREE_AND_CLEAR(cterm->macros[i]);
		cterm->macro_lens[i] = 0;
	}
	cterm_apply_window(cterm);

	cterm->atascii_inverse = false;

	/* Set up a shadow palette */
	if (cterm->screen.capabilities & ST_CONSOLE_CAP_EXTENDED_PALETTE) {
		for (i = 0; i < CTERM_PALETTE_SIZE; i++)
			(void)cterm_resetpalette(cterm, (uint32_t)i);
		if (cterm->has_palette_override) {
			uint32_t op[16];
			cterm_render_get_modepalette(cterm, op);
			for (i = 0; i < 16; i++) {
				uint32_t c = cterm->palette_override[i];
				cterm_setpalette(cterm, op[i] + 16,
					((c & 0x00FF0000) >> 8) | ((c & 0x00FF0000) >> 16),
					((c & 0x0000FF00)) | ((c & 0x0000FF00) >> 8),
					((c & 0x000000FF) << 8) | ((c & 0x000000FF)));
			}
		}
	}

	/* Reset mouse state */
	if (cterm->mouse_state_change) {
		cterm->mouse_state_change(9, 0, cterm->mouse_state_change_cbdata);
		cterm->mouse_state_change(1000, 0, cterm->mouse_state_change_cbdata);
		cterm->mouse_state_change(1001, 0, cterm->mouse_state_change_cbdata);
		cterm->mouse_state_change(1002, 0, cterm->mouse_state_change_cbdata);
		cterm->mouse_state_change(1003, 0, cterm->mouse_state_change_cbdata);
		cterm->mouse_state_change(1004, 0, cterm->mouse_state_change_cbdata);
		cterm->mouse_state_change(1005, 0, cterm->mouse_state_change_cbdata);
		cterm->mouse_state_change(1006, 0, cterm->mouse_state_change_cbdata);
		cterm->mouse_state_change(1007, 0, cterm->mouse_state_change_cbdata);
		cterm->mouse_state_change(1015, 0, cterm->mouse_state_change_cbdata);
		cterm->mouse_state_change(1016, 0, cterm->mouse_state_change_cbdata);
	}
	if (cterm->skypix_engine != NULL)
		cterm_render_record_error(cterm, cterm_skypix_reset(cterm->skypix_engine));
}

struct cterminal* cterm_init(int height, int width, int xpos, int ypos,
    int backlines, int backcols, struct st_console_cell *scrollback, int emulation,
    const struct cterm_render_state *screen,
    const struct st_cterm_config *initial)
{
	static bool key_tables_verified = false;
	char	*revision="$Revision: 1.333 $";
	char *in;
	char	*out;
	struct cterminal *cterm;
	int flags;

	if (!key_tables_verified) {
		verify_key_tables();
		key_tables_verified = true;
	}

	if((cterm=calloc(1, sizeof(struct cterminal)))==NULL)
		return cterm;
	cterm->screen = *screen;

	cterm->x=xpos;
	cterm->y=ypos;
	cterm->height=height;
	cterm->width=width;
	cterm->backlines=backlines;
	cterm->backwidth = backcols;
	cterm->scrollback=scrollback;
	cterm->log=CTERM_LOG_NONE;
	cterm->logfile=NULL;
	cterm->emulation=emulation;
	if (initial != NULL) {
		cterm->api_log_level = initial->log_level;
		cterm->api_diagnostic = initial->diagnostic;
		cterm->api_diagnostic_context = initial->diagnostic_context;
		if (initial->force_lcf || (initial->graphics_mode == ST_CTERM_GRAPHICS_RIP_1 || initial->graphics_mode == ST_CTERM_GRAPHICS_RIP_3))
			cterm->last_column_flag = CTERM_LCF_FORCED | CTERM_LCF_ENABLED;
		cterm->yellow_is_yellow = initial->yellow_is_yellow;
		if (initial->palette_override != NULL) {
			cterm->has_palette_override = true;
			memcpy(cterm->palette_override, initial->palette_override,
			    sizeof(cterm->palette_override));
		}
	}
	cterm->music_stream = NULL;
	cterm->fx_stream = NULL;
	/* Install the per-emulation dispatcher and its dispatch table.
	 * The dispatcher handles single bytes outside sequence state; the
	 * dispatch table is searched (via bsearch) when a sequence or C1
	 * function completes. */
	cterm->dispatch = NULL;
	cterm->dispatch_table = NULL;
	cterm->dispatch_table_len = 0;
	cterm->ctrl_bitmap = NULL;
	cterm->byte_handlers = NULL;
	switch (emulation) {
		case CTERM_EMULATION_ANSI_BBS:
			cterm->dispatch = cterm_dispatch_ecma;
			cterm->dispatch_table = cterm_ansi_dispatch;
			cterm->dispatch_table_len = cterm_ansi_dispatch_len;
			break;
		case CTERM_EMULATION_ATARIST_VT52:
			cterm->dispatch = cterm_dispatch_vt52;
			cterm->dispatch_table = cterm_st_vt52_dispatch;
			cterm->dispatch_table_len = cterm_st_vt52_dispatch_len;
			break;
		case CTERM_EMULATION_ATASCII:
			cterm->dispatch = cterm_dispatch_byte;
			cterm->ctrl_bitmap = cterm_atascii_ctrl_bitmap;
			cterm->byte_handlers = cterm_atascii_handlers;
			break;
		case CTERM_EMULATION_PETASCII:
			cterm->dispatch = cterm_dispatch_byte;
			cterm->ctrl_bitmap = cterm_petscii_ctrl_bitmap;
			cterm->byte_handlers = cterm_petscii_handlers;
			break;
		case CTERM_EMULATION_PRESTEL:
			/* Prestel's ESC+byte handling is driven by a custom
			 * programmable-memory state machine (cterm_accumulate_
			 * prestel_seq); it does not benefit from a bsearch table,
			 * so the table stays NULL and the Prestel dispatcher
			 * handles the byte repertoire directly. */
			cterm->dispatch = cterm_dispatch_prestel;
			break;
		case CTERM_EMULATION_BEEB:
			/* BEEB uses cterm_accumulate_trailing for VDU 23/28 but
			 * otherwise shares Prestel's C1 handling; no dispatch
			 * table needed. */
			cterm->dispatch = cterm_dispatch_beeb;
			break;
	}
	cterm->accumulator = NULL;
	cterm->saved_accumulator = NULL;
	cterm->lf_expand = false;
	cterm_reset(cterm);
	if(cterm->scrollback!=NULL) {
		memset(cterm->scrollback, 0, cterm->backwidth * cterm->backlines * sizeof(*cterm->scrollback));
	}
	strcpy(cterm->DA,"\x1b[=67;84;101;114;109;");
	out=strchr(cterm->DA, 0);
	if(out != NULL) {
		for(in=revision; *in; in++) {
			if(*in >= '0' && *in <= '9')
				*(out++)=*in;
			if(*in=='.')
				*(out++)=';';
		}
		*out=0;
	}
	strcat(cterm->DA,"c");
	if (cterm->emulation == CTERM_EMULATION_PRESTEL) {
		cterm->cursor = CTERM_CURSOR_HIDDEN;
		cterm_render_set_cursor(cterm, cterm->cursor);
		memcpy(cterm->prestel_data[0], "????????????????", PRESTEL_MEM_SLOT_SIZE);
		memcpy(cterm->prestel_data[1], ":2:?4967?89:1???", PRESTEL_MEM_SLOT_SIZE);
		memcpy(cterm->prestel_data[2], ":1632?123?456???", PRESTEL_MEM_SLOT_SIZE);
		memcpy(cterm->prestel_data[3], ";???????????????", PRESTEL_MEM_SLOT_SIZE);
		memcpy(cterm->prestel_data[4], ";???????????????", PRESTEL_MEM_SLOT_SIZE);
		memcpy(cterm->prestel_data[5], ";???????????????", PRESTEL_MEM_SLOT_SIZE);
		memcpy(cterm->prestel_data[6], ";???????????????", PRESTEL_MEM_SLOT_SIZE);
	}
	if (cterm->emulation == CTERM_EMULATION_ATARIST_VT52) {
		flags = cterm_render_get_video_flags(cterm);
		flags |= ST_CONSOLE_VIDEO_BRIGHT_BACKGROUND;
		cterm_render_set_video_flags(cterm, flags);
	}

	return cterm;
}

void cterm_start(struct cterminal *cterm)
{
	if (!cterm->started) {
		switch (cterm->screen.mode) {
			case CTERM_MODE_C64_40:
			case CTERM_MODE_C128_40:
			case CTERM_MODE_C128_80:
				cterm->attr = 15;
				break;
			default:
				cterm->attr = cterm->screen.default_attribute;
				break;
		}
		cterm_render_attribute_colours(cterm, cterm->attr, &cterm->fg_color, &cterm->bg_color);
		CTERM_FREE_AND_CLEAR(cterm->fg_tc_str);
		CTERM_FREE_AND_CLEAR(cterm->bg_tc_str);
		if (cterm->screen.capabilities & ST_CONSOLE_CAP_EXTENDED_PALETTE) {
			cterm->fg_color += 16;
			cterm->bg_color += 16;
		}
		cterm_render_set_attribute(cterm, cterm->attr);
		cterm_render_set_colours(cterm, cterm->fg_color, cterm->bg_color);
		cterm_render_set_cursor(cterm, cterm->cursor);
		cterm->started=1;
		cterm_apply_window(cterm);
		/* The pre-session console is not received terminal history. */
		cterm_render_clear_window(cterm);
		cterm_set_position(cterm, 1, 1);
	}
}

void
cterm_advance_char(struct cterminal *cterm, int *x, int *y, int move)
{
	int lm = TERM_MINX, rm = TERM_MAXX;
	int tm = TERM_MINY, bm = TERM_MAXY;

	cterm_coord_conv_xy(cterm, CTERM_COORD_TERM, CTERM_COORD_CURR, &lm, &tm);
	cterm_coord_conv_xy(cterm, CTERM_COORD_TERM, CTERM_COORD_CURR, &rm, &bm);

	if (cterm->emulation == CTERM_EMULATION_PRESTEL || cterm->emulation == CTERM_EMULATION_BEEB) {
		cterm_prestel_fix_line(cterm, *x, *y, true, false);
		cterm_render_set_attribute(cterm, cterm->attr);
		cterm_render_set_colours(cterm, cterm->fg_color, cterm->bg_color);
	}
	if((*x == rm || *x == CURR_MAXX) && (!(cterm->extattr & CTERM_EXTATTR_AUTOWRAP))) {
		cterm_render_set_position(cterm, *x, *y);
		return;
	}
	else {
		if((*x == rm || *x == CURR_MAXX) && ((cterm->last_column_flag & (CTERM_LCF_ENABLED | CTERM_LCF_SET)) == CTERM_LCF_ENABLED)) {
			cterm->last_column_flag |= CTERM_LCF_SET;
			cterm_render_set_position(cterm, *x, *y);
		}
		else if(*y == bm && (*x == rm || *x == CURR_MAXX)) {
			if (cterm->emulation == CTERM_EMULATION_PRESTEL) {
				*y = tm;
				move = 1;
				*x = lm;
				cterm_prestel_new_line(cterm);
			}
			else {
				cterm_cond_scrollup(cterm);
				move = 1;
				*x = lm;
				if (cterm->emulation == CTERM_EMULATION_BEEB)
					cterm_prestel_new_line(cterm);
			}
		}
		else {
			if(*x == rm || *x == CURR_MAXX) {
				if (cterm->emulation == CTERM_EMULATION_PRESTEL || cterm->emulation == CTERM_EMULATION_BEEB) {
					cterm_prestel_new_line(cterm);
				}
				*x=lm;
				if (*y < CURR_MAXY)
					(*y)++;
				if (move)
					cterm_render_set_position(cterm, *x, *y);
			}
			else {
				(*x)++;
			}
		}
	}
	if (move)
		cterm_render_set_position(cterm, *x, *y);
}

void
cterm_ctputs(struct cterminal *cterm, char *buf)
{
	for (const unsigned char *p = (const unsigned char *)buf; *p; ++p) {
		if (*p != '\n' && *p != '\r' && *p != '\b' && *p != '\t' && *p != 7) {
			cterm_text_byte(cterm, *p);
			continue;
		}
		cterm_render_record_error(cterm, cterm_text_boundary(cterm));
		if (cterm->log == CTERM_LOG_ASCII && cterm->logfile != NULL)
			fputc(*p, cterm->logfile);
		cterm_clear_lcf(cterm);
		switch (*p) {
		case '\n':
			cterm_adjust_currpos(cterm, 0, 1, 1);
			if (cterm->lf_expand)
				cterm_adjust_currpos(cterm, INT_MIN, 0, 0);
			break;
		case '\r': cterm_adjust_currpos(cterm, INT_MIN, 0, 0); break;
		case '\b': cterm_adjust_currpos(cterm, -1, 0, 0); break;
		case '\t': cterm_do_tab(cterm); break;
		default: break;
		}
		ABS_XY(&cterm->xpos, &cterm->ypos);
	}
}

/* ustrlen / cterm_uctputs / ustrcat live in cterm_internal.h */

/* ------------------------------------------------------------------------
 * cterm_dispatch_byte — shared dispatcher for PETSCII / ATASCII
 *
 * PETSCII and ATASCII don't use ECMA-48 sequence state; every received
 * byte is either a control code or a printable cell.  Each emulation
 * installs a 256-bit bitmap marking which byte values are controls and
 * a handler table indexed by byte value.  Printable bytes go through
 * the shared C64 attribute path (cterm_c64_get_attr returns cterm->attr
 * when the reverse flag is clear, so the same path works for both
 * emulations).  Bytes whose bitmap bit is set but whose handler slot is
 * NULL are silently dropped (PETSCII uses this for its reserved/
 * non-printable byte ranges).
 * ------------------------------------------------------------------------ */
void
cterm_dispatch_byte(struct cterminal *cterm, unsigned char byte, int *speed)
{
	(void)speed;

	if (cterm->ctrl_bitmap != NULL
	    && (cterm->ctrl_bitmap[byte >> 3] & (1u << (byte & 7)))) {
		if (cterm->byte_handlers != NULL
		    && cterm->byte_handlers[byte] != NULL)
			cterm->byte_handlers[byte](cterm);
		/* else: control byte with no handler — drop silently */
	}
	else {
		uint8_t attribute = cterm_c64_get_attr(cterm);
		uint8_t saved = cterm->attr;
		uint32_t foreground = cterm->fg_color, background = cterm->bg_color;
		cterm_render_set_attribute(cterm, attribute);
		cterm_render_attribute_colours(cterm, attribute, &cterm->fg_color, &cterm->bg_color);
		cterm_render_set_colours(cterm, cterm->fg_color, cterm->bg_color);
		cterm_text_byte(cterm, byte);
		cterm->attr = saved;
		cterm->fg_color = foreground;
		cterm->bg_color = background;
	}
}

/* ------------------------------------------------------------------------
 * cterm_accumulate_trailing — generic "read N raw bytes, then dispatch"
 *
 * Activated by a dispatch-table entry with a non-zero `trailing` field.
 * When cterm_accumulate_ecma_seq matches such an entry, it stashes the
 * handler and byte count on cterm and returns without calling the
 * handler; subsequent bytes are funnelled here.  Each byte lands in
 * cterm->seq_param_int[] so handlers can read the payload the same way
 * they read ordinary integer parameters.  When the count reaches zero
 * the stored handler is invoked and the accumulator clears itself.
 * ------------------------------------------------------------------------ */
bool
cterm_accumulate_trailing(struct cterminal *cterm, unsigned char byte, int *speed)
{
	if (cterm->seq_param_count < MAX_SEQ_PARAMS) {
		cterm->seq_param_int[cterm->seq_param_count] = byte;
		cterm->seq_param_count++;
	}
	if (cterm->seq_trailing_remain > 0)
		cterm->seq_trailing_remain--;
	if (cterm->seq_trailing_remain == 0) {
		void (*handler)(struct cterminal *, int *) = cterm->seq_trailing_handler;
		cterm->seq_trailing_handler = NULL;
		cterm->accumulator = NULL;
		if (handler != NULL)
			handler(cterm, speed);
	}
	return true;
}

bool
cterm_parse_byte(struct cterminal *cterm, unsigned char byte, int *speed)
{
	bool consumed = true;
	int i, x, y;
	int olddmc;
	uint32_t palette[16];
	int mpalette;
	int orig_fonts[4];
	int palette_offset = 0;

	if(!cterm->started)
		cterm_start(cterm);

	/* Now rejigger the current modes palette... */
	if (cterm->screen.capabilities & ST_CONSOLE_CAP_EXTENDED_PALETTE)
		palette_offset = 16;
	mpalette = cterm_render_get_modepalette(cterm, palette);
	if (mpalette) {
		for (i=0; i < 16; i++)
			palette[i] += palette_offset;
		cterm_render_set_modepalette(cterm, palette);
	}

	/* Deedle up the fonts */
	orig_fonts[0] = cterm_render_font_selector(cterm, 1);
	orig_fonts[1] = cterm_render_font_selector(cterm, 2);
	orig_fonts[2] = cterm_render_font_selector(cterm, 3);
	orig_fonts[3] = cterm_render_font_selector(cterm, 4);
	cterm_render_set_font_selector(cterm, cterm->altfont[0], false, 1);
	cterm_render_set_font_selector(cterm, cterm->altfont[1], false, 2);
	cterm_render_set_font_selector(cterm, cterm->altfont[2], false, 3);
	cterm_render_set_font_selector(cterm, cterm->altfont[3], false, 4);

	olddmc = cterm_render_hold_update(cterm);
	(void)cterm_render_set_hold_update(cterm, 1);
	cterm_apply_window(cterm);
	x = cterm->xpos;
	y = cterm->ypos;
	cterm_coord_conv_xy(cterm, CTERM_COORD_ABSTERM, CTERM_COORD_CURR, &x, &y);
	cterm_render_set_position(cterm, x, y);
	cterm_render_set_attribute(cterm, cterm->attr);
	cterm_render_set_colours(cterm, cterm->fg_color, cterm->bg_color);
	cterm_render_set_cursor(cterm, cterm->cursor);
	cterm->print_buf[0] = 0;
	cterm->print_pos = cterm->print_buf;
	if (cterm->emulation == CTERM_EMULATION_PRESTEL || cterm->emulation == CTERM_EMULATION_BEEB)
		cterm_prestel_get_state(cterm);
	if (cterm->accumulator != NULL)
		consumed = cterm->accumulator(cterm, byte, speed);
	else
		cterm->dispatch(cterm, byte, speed);
	cterm_uctputs(cterm, cterm->print_buf);
	cterm->print_buf[0] = 0;
	cterm->print_pos = cterm->print_buf;
	ABS_XY(&cterm->xpos, &cterm->ypos);

	(void)cterm_render_set_hold_update(cterm, olddmc);
	CURR_XY(&x, &y);
	cterm_render_set_position(cterm, x, y);
	cterm_render_set_cursor(cterm, cterm->cursor);

	/* Now rejigger the current modes palette... */
	if (mpalette) {
		for (i=0; i < 16; i++)
			palette[i] -= palette_offset;
		cterm_render_set_modepalette(cterm, palette);
	}

	/* De-doodle the fonts */
	cterm_render_set_font_selector(cterm, orig_fonts[0], false, 1);
	cterm_render_set_font_selector(cterm, orig_fonts[1], false, 2);
	cterm_render_set_font_selector(cterm, orig_fonts[2], false, 3);
	cterm_render_set_font_selector(cterm, orig_fonts[3], false, 4);

	return consumed;
}

int cterm_openlog(struct cterminal *cterm, char *logfile, int logtype)
{
	if(!cterm->started)
		cterm_start(cterm);

	cterm->logfile=fopen(logfile, "ab");
	if(cterm->logfile==NULL)
		return(0);
	cterm->log=logtype;
	return(1);
}

void cterm_closelog(struct cterminal *cterm)
{
	if(!cterm->started)
		cterm_start(cterm);

	if(cterm->logfile != NULL)
		fclose(cterm->logfile);
	cterm->logfile=NULL;
	cterm->log=CTERM_LOG_NONE;
}

FILE *dbg;
void cterm_end(struct cterminal *cterm, int free_fonts)
{
	int i;
	(void)free_fonts;

	if(cterm) {
		cterm_dec_sixel_cancel(cterm);
		st_history_free_pixels(cterm->backpixels, (size_t)cterm->backlines);
		st_pixel_buffer_destroy(cterm->backpartial);
		cterm_text_destroy(cterm);
		cterm_apc_destroy(cterm);
		cterm_closelog(cterm);
		st_music_destroy(cterm->music_player);
		cterm->music_player = NULL;
		if (cterm->music_stream != NULL) {
			audio_stream_destroy(cterm->music_stream);
			cterm->music_stream = NULL;
		}
		if (cterm->fx_stream != NULL) {
			audio_stream_destroy(cterm->fx_stream);
			cterm->fx_stream = NULL;
		}

		CTERM_FREE_AND_CLEAR(cterm->strbuf);
		CTERM_FREE_AND_CLEAR(cterm->tabs);
		CTERM_FREE_AND_CLEAR(cterm->vtabs);
		CTERM_FREE_AND_CLEAR(cterm->fg_tc_str);
		CTERM_FREE_AND_CLEAR(cterm->bg_tc_str);
		for (i = 0; i < (int)(sizeof(cterm->macros)
		    / sizeof(cterm->macros[0])); i++) {
			CTERM_FREE_AND_CLEAR(cterm->macros[i]);
		}

		free(cterm);
	}
}
