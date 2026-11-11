/* D: original endpoint ownership and independent line pixels. Ref 15/136. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "igs_line_test_helpers.h"

static void
chained_pixels(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f,resolution,false);
		for (unsigned mode = 1; mode <= 4; ++mode) {
			const struct st_console_rect area = {0,0,65,65};
			seed(&f,&area,768,768);
			CHECK(st_cterm_reset_terminal(f.terminal) == 0);
			char wire[96]; (void)snprintf(wire,sizeof(wire),"G#M>%u:D>40,40:D>60,40:D 60,60:",mode);
			feed(&f,wire);
			struct cterm_igs *igs = f.terminal->igs_engine;
			CHECK(igs->draw_start_x == 60 && igs->draw_start_y == 60 && igs->draw_x == 60 && igs->draw_y == 60);
			struct st_pixel_buffer *b = NULL;
			CHECK(st_console_get_pixels(&b,f.console,&area,false) == 0);
			for (unsigned y = 0; y < 65; ++y) for (unsigned x = 0; x < 65; ++x) {
				unsigned visits = (x == y && x <= 40) + (y == 40 && x >= 40 && x <= 60)
				    + (x == 60 && y >= 40 && y <= 60);
				bool ink = mode == 3 ? (visits & 1) != 0 : mode != 4 && visits != 0;
				unsigned wanted = 768u+(ink ? cterm_igs_plane_mask(resolution) : 0);
				CHECK(b->primary[y*65+x] == wanted && b->alternate[y*65+x] == wanted);
			}
			st_pixel_buffer_destroy(b);
		}
		destroy(&f);
	}
}

static void
vectors(void)
{
	static const int ends[3][3] = {{0,52,62},{51,50,53},{61,54,60}};
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
		igs->attributes.line_pen = 10; igs->patterns[6][0] = 0xa531;
		igs->math[0] = (int16_t)c->x1; igs->math[1] = (int16_t)c->y1;
		igs->math[2] = (int16_t)c->x2; igs->math[3] = (int16_t)c->y2;
		if (c->initial_last) {
			feed(&f,"G#M>4:P 0],1]:");
		} else {
			/* A rejected previous shaft supplies both D's origin and the
			 * nonfinal cap state, without injecting either into the result. */
			igs->math[4] = (int16_t)c->x1; igs->math[5] = (int16_t)(c->y1+5);
			igs->clipping = true; igs->clip_x1 = igs->clip_y1 = 0;
			igs->clip_x2 = igs->clip_y2 = 10;
			feed(&f,"G#T>2,1,3:T>2,1,0:L 4],5],0],1]:");
		}
		CHECK(igs->draw_x == c->x1 && igs->draw_y == c->y1 && igs->last_line == (c->initial_last != 0));
		igs->clipping = c->clipping != 0;
		igs->clip_x1 = c->left; igs->clip_y1 = c->top; igs->clip_x2 = c->right; igs->clip_y2 = c->bottom;
		char wire[128]; (void)snprintf(wire,sizeof(wire),"G#T>2,%d,%d:T>2,%d,%d:M %d:",
		    c->style,c->width,c->style,ends[c->begin][c->end],c->mode);
		feed(&f,wire);
		struct cterm_igs_attributes before; memcpy(&before,&igs->attributes,sizeof(before));
		feed(&f,"G#D 2],3]:");
		CHECK(igs->draw_start_x == c->x2 && igs->draw_start_y == c->y2 && igs->draw_x == c->x2 && igs->draw_y == c->y2);
		CHECK(memcmp(&before,&igs->attributes,sizeof(before)) == 0);
		CHECK(igs->clipping == (c->clipping != 0) && igs->clip_x1 == c->left && igs->clip_y2 == c->bottom);
		struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f.console,&full,false) == 0);
		for (unsigned phase = 0; phase < 2; ++phase) {
			uint64_t hash = native_hash(b,(unsigned[]){4,2,1}[resolution],phase != 0);
			if (hash != c->hashes[phase]) fprintf(stderr,"D vector %zu phase%u: %016" PRIx64 " != %016" PRIx64 "\n",i,phase,hash,c->hashes[phase]);
			CHECK(hash == c->hashes[phase]);
		}
		st_pixel_buffer_destroy(b);
	}
	if (active) destroy(&f);
}

static void
position(const struct cterm_igs *igs, int x1, int y1, int x2, int y2)
{
	CHECK(igs->draw_start_x == x1 && igs->draw_start_y == y1 && igs->draw_x == x2 && igs->draw_y == y2);
}

static void
partitions_and_recovery(void)
{
	const char wire[] = "AG#L>10,20,30,40:D>50,40:P>50,60:D 70,60:B";
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
			position(igs,70,60,70,60);
			const struct st_console_rect area = {59,49,3,13}; struct st_pixel_buffer *before = NULL;
			CHECK(st_console_get_pixels(&before,f.console,&area,false) == 0);
			for (unsigned y = 0; y < 13; ++y) for (unsigned x = 0; x < 3; ++x)
				CHECK(before->primary[y*3+x] == 768u+(y == 11 ? cterm_igs_plane_mask(resolution) : 0));
			struct st_console_cell cells[2]; const struct st_console_rect text = {0,0,2,1};
			CHECK(st_console_read_cells(f.console,&text,cells,2) == 0 && cells[0].glyph == 'A' && cells[1].glyph == 'B');
			feed(&f,"G#L 10,20,30,40:"); position(igs,10,20,30,40);
			const char *partial[] = {"G#D", "G#D ", "G#D 1,", "G#D 1,2"};
			for (unsigned p = 0; p < 4; ++p) {
				feed(&f,partial[p]); position(igs,10,20,30,40);
				CHECK(st_cterm_finish(f.terminal) == 0); position(igs,10,20,30,40);
				feed(&f,partial[p]); local_ms += 5001;
				CHECK(st_cterm_poll(f.terminal) == 0); position(igs,10,20,30,40);
			}
			const char bad[] = "G#D 1,10000:";
			CHECK(st_cterm_write(f.terminal,bad,sizeof(bad)-1,&used) == 0);
			position(igs,10,20,30,40);
			igs->attributes.line_width = 2;
			const char failed[] = "G#D 1,2:";
			CHECK(st_cterm_write(f.terminal,failed,sizeof(failed)-1,&used) == 0);
			position(igs,10,20,30,40); igs->attributes.line_width = 1;
			struct st_pixel_buffer *after = NULL;
			CHECK(st_console_get_pixels(&after,f.console,&area,false) == 0);
			CHECK(memcmp(before->primary,after->primary,3*13*sizeof(uint32_t)) == 0
			    && memcmp(before->alternate,after->alternate,3*13*sizeof(uint32_t)) == 0);
			st_pixel_buffer_destroy(before); st_pixel_buffer_destroy(after);
			feed(&f,"G#D 50,40:"); position(igs,50,40,50,40);
			feed(&f,"G#D 5,"); CHECK(st_cterm_reset_terminal(f.terminal) == 0);
			position(igs,0,0,0,0);
			feed(&f,"G#D 10,10:"); position(igs,10,10,10,10);
			destroy(&f);
		}
}

static void
lifetime_and_operands(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution)
		for (unsigned status = 0; status < 2; ++status) {
			struct fixture f; create(&f,resolution,status != 0);
			struct cterm_igs *igs = f.terminal->igs_engine;
			feed(&f,"G#M>4:L>10,20,30,40:P 50,60:"); position(igs,10,20,50,60);
			feed(&f,"G#D 70,80:"); position(igs,70,80,70,80);
			feed(&f,"G#I>0:I>1:I 2:"); position(igs,70,80,70,80);
			char wire[96]; (void)snprintf(wire,sizeof(wire),"G#R>%u,0:D 90,80:",resolution == 0 ? 1 : 0);
			size_t size = strlen(wire), used;
			CHECK(st_cterm_write(f.terminal,wire,size,&used) == 0);
			if (resolution != 2) {
				CHECK(used < size && st_cterm_input_paused(f.terminal)); position(igs,70,80,70,80);
				local_ms += 17; CHECK(st_cterm_poll(f.terminal) == 0 && !st_cterm_input_paused(f.terminal));
				position(igs,70,80,70,80); feed(&f,wire+used);
			} else CHECK(used == size);
			position(igs,90,80,90,80);
			feed(&f,"G#I 5:"); local_ms += 17; CHECK(st_cterm_poll(f.terminal) == 0);
			position(igs,90,80,90,80);
			feed(&f,"G#M>4:g>1:D 5000,5000:");
			int x = resolution == 0 ? 160 : 320, y = resolution == 2 ? 200 : 100;
			position(igs,x,y,x,y);
			feed(&f,"G#g>2:D 20,30:"); position(igs,20,resolution == 2 ? 60 : 30,20,resolution == 2 ? 60 : 30);
			igs->math[20] = 80; igs->math[resolution == 2 ? 60 : 30] = 90;
			feed(&f,"G#D 20],30]:"); position(igs,80,90,80,90);
			igs->random_seed = 1; igs->random_range[0][1] = igs->random_range[1][1] = 255;
			feed(&f,"G#D r,R:"); position(igs,230,202,230,202);
			CHECK(igs->random_seed == UINT32_C(0x5eb5ca17));
			feed(&f,"G#g 0:");
			igs->math[0] = igs->math[1] = INT16_MIN; igs->math[2] = igs->math[3] = INT16_MAX;
			feed(&f,"G#P>0],1]:M>1:D 2],3]:"); position(igs,INT16_MAX,INT16_MAX,INT16_MAX,INT16_MAX);
			const struct st_console_rect full = {0,0,igs->width,igs->height};
			struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f.console,&full,false) == 0);
			unsigned drawable = igs->height-(status ? (resolution == 2 ? 16u : 8u) : 0u);
			for (unsigned row = 0; row < drawable; ++row)
				CHECK(b->primary[row*igs->width+row] == 768u+cterm_igs_plane_mask(igs->resolution));
			st_pixel_buffer_destroy(b);
			feed(&f,"G#M>4:P>9999,9999:D 8888,8888:"); position(igs,8888,8888,8888,8888);
			destroy(&f);
		}
	const char wire[] = "G#g>1:D r,R:";
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
		position(igs,230,202,230,202); CHECK(igs->random_seed == UINT32_C(0x5eb5ca17));
		igs->random_seed = 1; feed(&f,"G#D r,");
		CHECK(st_cterm_finish(f.terminal) == 0 && igs->random_seed == 1);
		destroy(&f);
	}
}

int st_cterm_igs_drawto_test_main(void)
{
	chained_pixels(); vectors(); partitions_and_recovery(); lifetime_and_operands(); return 0;
}
