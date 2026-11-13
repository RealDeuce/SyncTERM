/* Pinned reference font/profile identities and independent EGA colour bits. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_profile.h"
#include "st_big_state.h"
#include "st_bgi_data.h"
#include "st_builtin_fonts.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while (0)
int main(void)
{
	static const unsigned heights[] = {200,350,480}, cells[] = {8,14,16}, rows[] = {25,25,30};
	static const uint8_t bios[] = {0x0e,0x10,0x12};
	static const size_t pitches[] = {16384,32768,38400};
	static const uint16_t visual_pitches[] = {16384,32768,40960};
	static const uint64_t hashes[] = {UINT64_C(0xd9dbbc3df2c7b6b6),UINT64_C(0x6496a654283abdd1),UINT64_C(0x6fff41d644f1e76c)};
	for (unsigned i = 0; i < 3; ++i) {
		const struct st_big_mode *m = st_big_mode(i+1); struct st_console_font font;
		CHECK(m != NULL && m->width == 640 && m->height == heights[i] && m->columns == 80 && m->rows == rows[i]);
		CHECK(m->cell_height == cells[i] && m->page_bytes == pitches[i] && m->aspect_width == 4 && m->aspect_height == 3);
		CHECK(m->bios_mode == bios[i]);
		CHECK(m->visual_page_bytes==visual_pitches[i]);
		CHECK(st_big_console_font(i+1,&font) && font.width == 8 && font.height == cells[i] && font.glyph_count == 256);
		CHECK(font.row_stride == 1 && font.glyph_stride == cells[i] && font.bitmap_size == cells[i]*256);
		uint64_t hash = UINT64_C(0xcbf29ce484222325);
		for (size_t n = 0; n < font.bitmap_size; ++n) hash = (hash^font.bitmap[n])*UINT64_C(0x100000001b3);
		CHECK(hash == hashes[i]); /* Independently calculated from supplied ROM banks. */
	}
	CHECK(st_big_mode(0) == NULL && st_big_mode(4) == NULL);
	static const uint8_t expected[] = {0,1,2,3,4,5,20,7,56,57,58,59,60,61,62,63};
	CHECK(memcmp(st_bgi_ega_defaults,expected,16) == 0);
	for (unsigned i = 0; i < 64; ++i) {
		CHECK(st_bgi_ega_colors[i][0] == ((i&4 ? 0xaaaa : 0)+(i&32 ? 0x5555 : 0)));
		CHECK(st_bgi_ega_colors[i][1] == ((i&2 ? 0xaaaa : 0)+(i&16 ? 0x5555 : 0)));
		CHECK(st_bgi_ega_colors[i][2] == ((i&1 ? 0xaaaa : 0)+(i&8 ? 0x5555 : 0)));
	}
	struct st_big_state state; st_big_state_init(&state);
	CHECK(memcmp(state.application.patterns,st_big_fill_restore,96) == 0);
	CHECK(state.application.patterns[0][0] == 0xaa && state.application.patterns[11][7] == 0);
	uint64_t restore_hash = UINT64_C(0xcbf29ce484222325);
	for (size_t row = 0; row < 12; ++row)
		for (size_t col = 0; col < 8; ++col)
			restore_hash = (restore_hash^st_big_fill_restore[row][col])*UINT64_C(0x100000001b3);
	CHECK(restore_hash == UINT64_C(0x762192d1c8a12df5));
	return 0;
}
