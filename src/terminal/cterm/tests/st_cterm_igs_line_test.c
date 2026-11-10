/* L: independent native vectors and literal transport/state checks.
 * References 43/44/46/50/51/136; no external runtime assets. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "igs_line_test_helpers.h"

static void
literal(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f,resolution,false);
		feed(&f,"G#L 100,80,110,80:");
		struct cterm_igs *igs = f.terminal->igs_engine;
		CHECK(igs->draw_x == 110 && igs->draw_y == 80);
		const struct st_console_rect area = {98,78,15,5};
		struct st_pixel_buffer *b = NULL;
		CHECK(st_console_get_pixels(&b,f.console,&area,false) == 0);
		for (unsigned y = 0; y < 5; ++y) for (unsigned x = 0; x < 15; ++x) {
			unsigned mask = cterm_igs_plane_mask(resolution);
			CHECK(b->primary[y*15+x] == 768u + (y == 2 && x >= 2 && x <= 12 ? mask : 0));
		}
		st_pixel_buffer_destroy(b); destroy(&f);
	}
}

static void
vectors(void)
{
	static const int end_selectors[3][3] = {{0,52,62},{51,50,53},{61,54,60}};
	struct fixture f; bool active = false; int resolution = -1;
	for (size_t i = 0; i < sizeof(line_cases)/sizeof(*line_cases); ++i) {
		const struct line_case *c = &line_cases[i];
		if (resolution != c->resolution) {
			if (active) destroy(&f);
			create(&f,(unsigned)c->resolution,false); active = true; resolution = c->resolution;
		}
		struct cterm_igs *igs = f.terminal->igs_engine;
		const struct st_console_rect full = {0,0,igs->width,igs->height};
		seed(&f,&full,768,768+cterm_igs_plane_mask((unsigned)resolution));
		/* A logical selection retained from low resolution is mapped at use,
		 * as after R. Physical 9 is the independent oracle's foreground. */
		igs->attributes.line_pen = 10;
		igs->patterns[6][0] = 0xa531;
		igs->math[0] = (int16_t)c->x1; igs->math[1] = (int16_t)c->y1;
		igs->math[2] = (int16_t)c->x2; igs->math[3] = (int16_t)c->y2;
		if (!c->initial_last) {
			/* A vertically rejected shaft leaves LSTLIN false. The next
			 * initial cap must retain that actual command-sequence state. */
			igs->clipping = true; igs->clip_x1 = igs->clip_y1 = 0;
			igs->clip_x2 = 10; igs->clip_y2 = 10;
			feed(&f,"G#T>2,1,3:T>2,1,0:L 40,60,80,60:");
			CHECK(!igs->last_line);
		} else igs->last_line = true;
		igs->clipping = c->clipping != 0;
		igs->clip_x1 = c->left; igs->clip_y1 = c->top; igs->clip_x2 = c->right; igs->clip_y2 = c->bottom;
		char wire[128];
		(void)snprintf(wire,sizeof(wire),"G#T>2,%d,%d:T>2,%d,%d:M %d:",c->style,c->width,
		    c->style,end_selectors[c->begin][c->end],c->mode);
		feed(&f,wire);
		struct cterm_igs_attributes before; memcpy(&before,&igs->attributes,sizeof(before));
		feed(&f,"G#L 0],1],2],3]:");
		CHECK(igs->draw_start_x == c->x1 && igs->draw_start_y == c->y1 && igs->draw_x == c->x2 && igs->draw_y == c->y2);
		CHECK(memcmp(&before,&igs->attributes,sizeof(before)) == 0);
		CHECK(igs->clipping == (c->clipping != 0) && igs->clip_x1 == c->left && igs->clip_y2 == c->bottom);
		struct st_pixel_buffer *b = NULL;
		CHECK(st_console_get_pixels(&b,f.console,&full,false) == 0);
		for (unsigned phase = 0; phase < 2; ++phase) {
			uint64_t hash = native_hash(b,(unsigned[]){4,2,1}[resolution],phase != 0);
			if (hash != c->hashes[phase])
				fprintf(stderr,"vector %zu res%d type%d width%d caps%d/%d mode%d phase%u: %016" PRIx64 " != %016" PRIx64 "\n",
				    i,resolution,c->style,c->width,c->begin,c->end,c->mode,phase,hash,c->hashes[phase]);
			CHECK(hash == c->hashes[phase]);
		}
		st_pixel_buffer_destroy(b);
	}
	if (active) destroy(&f);
}

static void
input_and_lifetime(void)
{
	const char wire[] = "AG#L>20,30,40,50:P 60,70:B";
	for (unsigned resolution = 0; resolution < 3; ++resolution)
		for (size_t split = 0; split <= sizeof(wire); ++split) {
			struct fixture f; create(&f,resolution,false); size_t used;
			if (split == sizeof(wire)) {
				for (size_t i = 0; i < sizeof(wire)-1; ++i)
					CHECK(st_cterm_write(f.terminal,wire+i,1,&used) == 0 && used == 1);
			} else {
				CHECK(st_cterm_write(f.terminal,wire,split,&used) == 0 && used == split);
				CHECK(st_cterm_write(f.terminal,wire+split,sizeof(wire)-1-split,&used) == 0 && used == sizeof(wire)-1-split);
			}
			struct cterm_igs *igs = f.terminal->igs_engine;
			CHECK(igs->draw_start_x == 20 && igs->draw_start_y == 30 && igs->draw_x == 60 && igs->draw_y == 70);
			struct st_console_cell cells[2]; const struct st_console_rect text = {0,0,2,1};
			CHECK(st_console_read_cells(f.console,&text,cells,2) == 0 && cells[0].glyph == 'A' && cells[1].glyph == 'B');
			const char *partial[] = {"G#L 1,", "G#L 1,2,", "G#L 1,2,3,"};
			for (unsigned i = 0; i < 3; ++i) {
				feed(&f,partial[i]); CHECK(st_cterm_finish(f.terminal) == 0);
				feed(&f,partial[i]); local_ms += 5001; CHECK(st_cterm_poll(f.terminal) == 0);
				CHECK(igs->draw_start_x == 20 && igs->draw_start_y == 30 && igs->draw_x == 60 && igs->draw_y == 70);
			}
			const char bad[] = "G#L 1,2,3,10000:";
			CHECK(st_cterm_write(f.terminal,bad,sizeof(bad)-1,&used) == IO_ERROR_OUT_OF_RANGE);
			CHECK(igs->draw_start_x == 20 && igs->draw_y == 70);
			feed(&f,"G#g>1:L 2500,7500,9999,0:");
			CHECK(igs->draw_start_x == (resolution == 0 ? 80 : 160));
			CHECK(igs->draw_start_y == (resolution == 2 ? 299 : 149));
			CHECK(igs->draw_x == (resolution == 0 ? 319 : 639) && igs->draw_y == 0);
			feed(&f,"G#g>2:L 20,30,40,50:");
			CHECK(igs->draw_start_y == (resolution == 2 ? 60 : 30) && igs->draw_y == (resolution == 2 ? 100 : 50));
			feed(&f,"G#I 2:"); CHECK(igs->draw_start_x == 20 && igs->draw_x == 40);
			feed(&f,resolution == 0 ? "G#R 1,0:" : "G#R 0,0:");
			local_ms += 17; CHECK(st_cterm_poll(f.terminal) == 0);
			CHECK(igs->draw_start_x == 20 && igs->draw_x == 40 && igs->last_line);
			feed(&f,"G#L 10,20,"); CHECK(st_cterm_reset_terminal(f.terminal) == 0);
			CHECK(igs->draw_start_x == 0 && igs->draw_start_y == 0 && igs->draw_x == 0 && igs->draw_y == 0);
			feed(&f,"G#L 10,20,30,40:"); CHECK(igs->draw_start_x == 10 && igs->draw_x == 30);
			destroy(&f);
		}
}

static void
clipping_and_status(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution)
		for (unsigned status = 0; status < 2; ++status) {
			struct fixture f; create(&f,resolution,status != 0);
			struct cterm_igs *igs = f.terminal->igs_engine;
			const struct st_console_rect area = {45,55,31,11};
			for (unsigned mode = 1; mode <= 4; ++mode) {
				seed(&f,&area,768,768);
				igs->clip_x1 = igs->clip_y1 = 50; igs->clip_x2 = igs->clip_y2 = 70;
				char wire[96]; (void)snprintf(wire,sizeof(wire),"G#T>2,1,3:M>%u:L 40,60,80,60:",mode); feed(&f,wire);
				struct st_pixel_buffer *b = NULL;
				CHECK(st_console_get_pixels(&b,f.console,&area,false) == 0);
				for (unsigned y = 0; y < 11; ++y) for (unsigned x = 0; x < 31; ++x) {
					bool ink = x >= 5 && x <= 25 && y >= 4 && y <= 6;
					if (mode == 3 && y == 4 && x == 25) ink = false;
					/* Medium width 3 has one circle row: the shaft polygon
					 * is horizontal and XOR's doubled border leaves one end. */
					if (resolution == 1)
						ink = y == 5 && (mode == 3 ? x == 25 : x >= 5 && x <= 25);
					if (mode == 4) ink = false;
					CHECK(b->primary[y*31+x] == 768u+(ink ? cterm_igs_plane_mask(resolution) : 0));
				}
				st_pixel_buffer_destroy(b);
			}
			cterm_igs_clip_screen(igs);
			unsigned ch = resolution == 2 ? 16 : 8;
			const struct st_console_rect bottom = {0,igs->height-ch,igs->width,ch};
			seed(&f,&bottom,768,768);
			char wire[96]; (void)snprintf(wire,sizeof(wire),"G#T>2,1,1:M>1:L 0,%u,%u,%u:",igs->height-1,igs->width-1,igs->height-1); feed(&f,wire);
			struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f.console,&bottom,false) == 0);
			for (unsigned y = 0; y < ch; ++y) for (unsigned x = 0; x < igs->width; ++x)
				CHECK(b->primary[y*igs->width+x] == 768u+(y == ch-1 && !status ? cterm_igs_plane_mask(resolution) : 0));
			st_pixel_buffer_destroy(b);
			CHECK(igs->draw_y == (int)igs->height-1);
			destroy(&f);
		}
}

static void
extreme_and_random(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f,resolution,false);
		struct cterm_igs *igs = f.terminal->igs_engine;
		igs->math[0] = igs->math[1] = INT16_MIN;
		igs->math[2] = igs->math[3] = INT16_MAX;
		feed(&f,"G#L 0],1],2],3]:");
		CHECK(igs->draw_start_x == INT16_MIN && igs->draw_y == INT16_MAX);
		const struct st_console_rect full = {0,0,igs->width,igs->height};
		struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f.console,&full,false) == 0);
		for (unsigned y = 0; y < igs->height; ++y) for (unsigned x = 0; x < igs->width; ++x)
			CHECK(b->primary[y*igs->width+x] == 768u+(x == y ? cterm_igs_plane_mask(resolution) : 0));
		st_pixel_buffer_destroy(b); destroy(&f);
	}
	const char wire[] = "G#g>1:L r,R,r,R:";
	for (size_t split = 0; split <= sizeof(wire); ++split) {
		struct fixture f; create(&f,0,false); size_t used;
		struct cterm_igs *igs = f.terminal->igs_engine;
		igs->random_range[0][1] = igs->random_range[1][1] = 255;
		if (split == sizeof(wire)) {
			for (size_t i = 0; i < sizeof(wire)-1; ++i)
				CHECK(st_cterm_write(f.terminal,wire+i,1,&used) == 0 && used == 1);
		} else {
			CHECK(st_cterm_write(f.terminal,wire,split,&used) == 0 && used == split);
			CHECK(st_cterm_write(f.terminal,wire+split,sizeof(wire)-1-split,&used) == 0 && used == sizeof(wire)-1-split);
		}
		CHECK(igs->draw_start_x == 230 && igs->draw_start_y == 202 && igs->draw_x == 48 && igs->draw_y == 58);
		CHECK(igs->random_seed == UINT32_C(0x58543a1d));
		igs->random_seed = 1; feed(&f,"G#L r,R,r,");
		CHECK(st_cterm_finish(f.terminal) == 0 && igs->random_seed == 1);
		destroy(&f);
	}
}

int st_cterm_igs_line_test_main(void)
{
	literal(); vectors(); input_and_lifetime(); clipping_and_status(); extreme_and_random(); return 0;
}
