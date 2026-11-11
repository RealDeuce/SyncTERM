/* B: native bar/border and rounded filled boxes.
 * References 15/46/47/48/49/131/134; no external runtime assets. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "igs_fill_test_helpers.h"

static void
literal(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f,resolution,false);
		const struct st_console_rect area = {98,78,7,7}; seed(&f,&area,768,768);
		feed(&f,"G#A>1,1,0:B 100,80,102,82,0:");
		struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f.console,&area,false) == 0);
		for (unsigned y = 0; y < 7; ++y) for (unsigned x = 0; x < 7; ++x) {
			unsigned expected = 768u+(x >= 2 && x <= 4 && y >= 2 && y <= 4 ? cterm_igs_plane_mask(resolution) : 0);
			CHECK(b->primary[y*7+x] == expected && b->alternate[y*7+x] == expected);
		}
		st_pixel_buffer_destroy(b); destroy(&f);
	}
}

#include "igs_box_test_vectors.h"

static void
vectors(void)
{
	struct fixture f; int resolution = -1; bool active = false;
	for (size_t i = 0; i < sizeof(box_cases)/sizeof(*box_cases); ++i) {
		const struct box_case *c = &box_cases[i];
		if (resolution != c->resolution) {
			if (active) destroy(&f);
			create(&f,(unsigned)c->resolution,false); active = true; resolution = c->resolution;
			f.terminal->response_cb = no_response;
		}
		struct cterm_igs *igs = f.terminal->igs_engine; seed_words(&f);
		igs->draw_start_x = 17; igs->draw_start_y = 23; igs->draw_x = 37; igs->draw_y = 47;
		igs->last_line = c->initial_last != 0;
		igs->clipping = c->clipping != 0; igs->clip_x1 = c->bounds[0]; igs->clip_y1 = c->bounds[1];
		igs->clip_x2 = c->bounds[2]; igs->clip_y2 = c->bounds[3];
		igs->attributes.fill_pen = 7; /* Logical7 retained, physical5 at use. */
		igs->attributes.line_pen = 3; igs->attributes.marker_pen = 4; igs->attributes.text_pen = 2;
		igs->attributes.line_type = 7; igs->attributes.line_width = 39;
		igs->attributes.line_begin = 1; igs->attributes.line_end = 2; igs->attributes.user_line = 0xa531;
		memcpy(igs->patterns[3],user_rows,sizeof(user_rows));
		char wire[128]; (void)snprintf(wire,sizeof(wire),"G#A>%d,%d,%d:M %d:",c->interior,c->interior == 4 ? 3 : c->index,c->perimeter,c->mode); feed(&f,wire);
		if (c->interior == 4)
			for (unsigned row = 0; row < 16; ++row) igs->patterns[3][row] ^= 0xffff;
		for (unsigned j = 0; j < 4; ++j) igs->math[j] = (int16_t)c->points[j];
		struct cterm_igs_attributes before; memcpy(&before,&igs->attributes,sizeof(before));
		uint32_t palette[16]; memcpy(palette,igs->palette,sizeof(palette));
		igs->math[127] = (int16_t)c->flag;
		feed(&f,"G#B 0],1],2],3],127]:");
		CHECK(igs->draw_start_x == 17 && igs->draw_start_y == 23 && igs->draw_x == 37 && igs->draw_y == 47);
		CHECK(igs->last_line == (c->expected_last != 0));
		CHECK(memcmp(&before,&igs->attributes,sizeof(before)) == 0 && memcmp(palette,igs->palette,sizeof(palette)) == 0);
		CHECK(igs->clipping == (c->clipping != 0) && igs->clip_x1 == c->bounds[0] && igs->clip_y2 == c->bounds[3]);
		for (unsigned j = 0; j < 4; ++j) CHECK(igs->math[j] == c->points[j]);
		const struct st_console_rect full = {0,0,igs->width,igs->height}; struct st_pixel_buffer *b = NULL;
		CHECK(st_console_get_pixels(&b,f.console,&full,false) == 0);
		for (unsigned phase = 0; phase < 2; ++phase) {
			uint64_t hash = native_hash(b,(unsigned[]){4,2,1}[resolution],phase != 0);
			if (hash != c->hashes[phase]) fprintf(stderr,"box %zu res%d interior%d index%d mode%d phase%u: %016" PRIx64 " != %016" PRIx64 "\n",
			    i,resolution,c->interior,c->index,c->mode,phase,hash,c->hashes[phase]);
			CHECK(hash == c->hashes[phase]);
		}
		st_pixel_buffer_destroy(b);
	}
	if (active) destroy(&f);
}

static void
pens_and_edges(void)
{
	static const unsigned physical[16] = {0,15,1,2,4,6,3,5,7,8,9,10,12,14,11,13};
	for (unsigned resolution = 0; resolution < 3; ++resolution)
		for (unsigned status = 0; status < 2; ++status) {
			struct fixture f; create(&f,resolution,status != 0); f.terminal->response_cb = no_response;
			struct cterm_igs *igs = f.terminal->igs_engine; feed(&f,"G#A 1,1,0:");
			const struct st_console_rect area = {30,30,4,4};
			for (unsigned pen = 0; pen < 16; ++pen) {
				seed(&f,&area,768,768); char wire[64];
				(void)snprintf(wire,sizeof(wire),"G#C>2,%u:B 30,30,33,33,0:",pen); feed(&f,wire);
				struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f.console,&area,false) == 0);
				unsigned effective = pen > cterm_igs_plane_mask(resolution) ? 1 : pen;
				for (unsigned i = 0; i < 16; ++i) CHECK(b->primary[i] == 768u+(physical[effective]&cterm_igs_plane_mask(resolution)));
				st_pixel_buffer_destroy(b);
			}
			unsigned ch = resolution == 2 ? 16 : 8;
			const struct st_console_rect bottom = {0,igs->height-ch,igs->width,ch}; seed(&f,&bottom,768,768);
			feed(&f,"G#g>1:C>2,1:B 0,9999,9999,9999,0:");
			struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f.console,&bottom,false) == 0);
			for (unsigned y = 0; y < ch; ++y) for (unsigned x = 0; x < igs->width; ++x)
				CHECK(b->primary[y*igs->width+x] == 768u+(y == ch-1 && !status ? cterm_igs_plane_mask(resolution) : 0));
			st_pixel_buffer_destroy(b);
			igs->math[0] = INT16_MIN; igs->math[1] = INT16_MIN; igs->math[2] = INT16_MAX; igs->math[3] = INT16_MAX;
			feed(&f,"G#g>0:B 0],1],2],3],1:");
			const struct st_console_rect full = {0,0,igs->width,igs->height};
			CHECK(st_console_get_pixels(&b,f.console,&full,false) == 0);
			for (unsigned y = 0; y < igs->height; ++y) for (unsigned x = 0; x < igs->width; ++x) {
				bool ink = y < igs->height-(status ? ch : 0);
				CHECK(b->primary[y*igs->width+x] == 768u+(ink ? cterm_igs_plane_mask(resolution) : 0));
			}
			st_pixel_buffer_destroy(b); destroy(&f);
		}
}

static void
input_and_recovery(void)
{
	const char *chains[] = {"AG#M>4:L>17,23,37,47:M>1:B>40,60,43,62,0:M 2:B",
	    "AG#g>1:B>r,R,r,R,0:M 2:B"};
	for (unsigned resolution = 0; resolution < 3; ++resolution) for (unsigned chain = 0; chain < 2; ++chain)
		for (size_t split = 0; split <= strlen(chains[chain])+1; ++split) {
			struct fixture f; create(&f,resolution,false); f.terminal->response_cb = no_response;
			struct cterm_igs *igs = f.terminal->igs_engine; size_t size = strlen(chains[chain]), used;
			if (chain) igs->random_range[0][1] = igs->random_range[1][1] = 255;
			if (split > size) {
				for (size_t i = 0; i < size; ++i) CHECK(st_cterm_write(f.terminal,chains[chain]+i,1,&used) == 0 && used == 1);
			} else {
				CHECK(st_cterm_write(f.terminal,chains[chain],split,&used) == 0 && used == split);
				CHECK(st_cterm_write(f.terminal,chains[chain]+split,size-split,&used) == 0 && used == size-split);
			}
			CHECK(igs->attributes.writing_mode == 2 && igs->last_line);
			CHECK(igs->draw_start_x == (chain ? 0 : 17) && igs->draw_y == (chain ? 0 : 47));
			CHECK(igs->random_seed == (chain ? UINT32_C(0x58543a1d) : 1));
			const struct st_console_rect area = chain ? (struct st_console_rect){47,57,185,resolution == 2 ? 147u : 143u} : (struct st_console_rect){39,59,6,5};
			struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f.console,&area,false) == 0);
			for (unsigned y = 0; y < area.height; ++y) for (unsigned x = 0; x < area.width; ++x) {
				unsigned px = area.x+x, py = area.y+y;
				bool ink = chain ? px >= 48 && px <= 230 && py >= 58 && py <= 202 : px >= 40 && px <= 43 && py >= 60 && py <= 62;
				/* Random values bypass g transforms. Limit colour-mode height. */
				if (py >= igs->height) continue;
				CHECK(b->primary[y*area.width+x] == 768u+(ink ? cterm_igs_plane_mask(resolution) : 0));
			}
			st_pixel_buffer_destroy(b);
			struct st_console_cell cells[2]; const struct st_console_rect text = {0,0,2,1};
			CHECK(st_console_read_cells(f.console,&text,cells,2) == 0 && cells[0].glyph == 'A' && cells[1].glyph == 'B');
			destroy(&f);
		}
	struct fixture f; create(&f,0,false); struct cterm_igs *igs = f.terminal->igs_engine;
	f.terminal->response_cb = no_response;
	const char *partial[] = {"G#B ","G#B 1,","G#B 1,2,","G#B 1,2,3,4,","G#B r,R,r,R,","G#B 1,2,3,4,r"};
	const struct st_console_rect full = {0,0,320,200}; seed_words(&f);
	struct st_pixel_buffer *before = NULL; CHECK(st_console_get_pixels(&before,f.console,&full,false) == 0);
	for (size_t i = 0; i < sizeof(partial)/sizeof(*partial); ++i) {
		feed(&f,partial[i]); CHECK(st_cterm_finish(f.terminal) == 0);
		feed(&f,partial[i]); local_ms += 5001; CHECK(st_cterm_poll(f.terminal) == 0);
		CHECK(igs->random_seed == 1 && igs->draw_x == 0);
	}
	size_t used; const char bad[] = "G#B 1,2,3,4,10000:";
	CHECK(st_cterm_write(f.terminal,bad,sizeof(bad)-1,&used) == 0);
	struct st_pixel_buffer *after = NULL; CHECK(st_console_get_pixels(&after,f.console,&full,false) == 0);
	CHECK(memcmp(before->primary,after->primary,320u*200*sizeof(*before->primary)) == 0);
	CHECK(memcmp(before->alternate,after->alternate,320u*200*sizeof(*before->alternate)) == 0);
	st_pixel_buffer_destroy(before); st_pixel_buffer_destroy(after);
	feed(&f,"G#B r,1,"); CHECK(st_cterm_reset_terminal(f.terminal) == 0);
	feed(&f,"G#B 40,60,43,62,0:"); CHECK(igs->random_seed == 1 && igs->draw_x == 0);
	destroy(&f);
}

static void
scaling_and_lifetime(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f,resolution,false); f.terminal->response_cb = no_response;
		struct cterm_igs *igs = f.terminal->igs_engine;
		feed(&f,"G#M>4:L>17,23,37,47:g>2:M>1:B 40,60,43,62,0:");
		unsigned sy = resolution == 2 ? 2 : 1;
		const struct st_console_rect area = {39,60*sy-1,6,2*sy+3}; struct st_pixel_buffer *b = NULL;
		CHECK(st_console_get_pixels(&b,f.console,&area,false) == 0);
		for (unsigned y = 0; y < area.height; ++y) for (unsigned x = 0; x < area.width; ++x)
			CHECK(b->primary[y*area.width+x] == 768u+(x >= 1 && x <= 4 && y >= 1 && y <= 2*sy+1 ? cterm_igs_plane_mask(resolution) : 0));
		st_pixel_buffer_destroy(b);
		feed(&f,"G#I 2:"); CHECK(igs->draw_start_x == 17 && igs->draw_y == 47);
		const char *wire = resolution == 0 ? "G#R>1,0:B 40,60,43,62,0:" : "G#R>0,0:B 40,60,43,62,0:";
		size_t used, prefix = strlen("G#R>1,0:");
		CHECK(st_cterm_write(f.terminal,wire,strlen(wire),&used) == 0);
		if (resolution == 2) CHECK(used == strlen(wire));
		else {
			CHECK(used == prefix); local_ms += 17; CHECK(st_cterm_poll(f.terminal) == 0); feed(&f,wire+prefix);
		}
		CHECK(igs->scaling_resolution == (int)resolution && igs->draw_x == 37);
		feed(&f,"G#I 5:"); if (cterm_igs_paused(igs)) { local_ms += 17; CHECK(st_cterm_poll(f.terminal) == 0); }
		CHECK(igs->resolution == resolution && igs->draw_x == 37);
		/* Scale the array index before lookup, then preserve signed loaded values. */
		igs->math[0] = 40; igs->math[2] = igs->math[3] = igs->math[4] = 43;
		const struct st_console_rect indexed = {39,39,6,6}; seed(&f,&indexed,768,768);
		feed(&f,"G#g>1:B 0],0],50],100],0:"); CHECK(igs->draw_x == 37);
		CHECK(st_console_get_pixels(&b,f.console,&indexed,false) == 0);
		for (unsigned y = 0; y < 6; ++y) for (unsigned x = 0; x < 6; ++x)
			CHECK(b->primary[y*6+x] == 768u+(x >= 1 && x <= 4 && y >= 1 && y <= 4 ? cterm_igs_plane_mask(resolution) : 0));
		st_pixel_buffer_destroy(b);
		feed(&f,"G#B 1,2,"); CHECK(st_cterm_reset_terminal(f.terminal) == 0);
		CHECK(igs->draw_x == 0 && igs->draw_start_x == 0 && igs->last_line);
		destroy(&f);
	}
}

static void
invalid_state(void)
{
	struct fixture f; create(&f,0,false); struct cterm_igs *igs = f.terminal->igs_engine;
	const struct st_console_rect area = {30,30,4,4}; seed(&f,&area,769,770);
	igs->clip_x1 = 100; igs->clip_x2 = 10;
	feed(&f,"G#B 30,30,33,33,0:");
	struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f.console,&area,false) == 0);
	for (unsigned i = 0; i < 16; ++i) CHECK(b->primary[i] == 769 && b->alternate[i] == 770);
	st_pixel_buffer_destroy(b); cterm_igs_clip_screen(igs);
	igs->attributes.fill_interior = 3; igs->attributes.fill_index = 13;
	size_t used; const char wire[] = "G#B 30,30,33,33,0:";
	CHECK(st_cterm_write(f.terminal,wire,sizeof(wire)-1,&used) == 0);
	igs->attributes.fill_interior = 5;
	CHECK(st_cterm_write(f.terminal,wire,sizeof(wire)-1,&used) == 0);
	CHECK(st_console_get_pixels(&b,f.console,&area,false) == 0);
	for (unsigned i = 0; i < 16; ++i) CHECK(b->primary[i] == 769 && b->alternate[i] == 770);
	st_pixel_buffer_destroy(b); destroy(&f);
}

static void
flags_and_rounding(void)
{
	static const int flags[] = {INT16_MIN,-1,0,1,9999,INT16_MAX};
	static const unsigned left[3][3] = {{40,32,40},{40,64,40},{40,64,40}};
	static const unsigned top[3][3] = {{40,40,40},{40,40,40},{40,80,80}};
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f,resolution,false); f.terminal->response_cb = no_response;
		struct cterm_igs *igs = f.terminal->igs_engine;
		feed(&f,"G#A 1,1,0:");
		for (unsigned g = 0; g < 3; ++g) for (size_t k = 0; k < sizeof(flags)/sizeof(*flags); ++k) {
			const struct st_console_rect area = {left[resolution][g],top[resolution][g],1,1}; seed(&f,&area,768,768);
			igs->math[127] = (int16_t)flags[k]; char wire[96];
			(void)snprintf(wire,sizeof(wire),"G#g>%u:B %s,127]:",g,g == 1 ? "1000,2000,3000,5000" : "40,40,100,80");
			bool before_last = igs->last_line;
			feed(&f,wire); struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f.console,&area,false) == 0);
			CHECK(b->primary[0] == 768u+(flags[k] == 0 ? cterm_igs_plane_mask(resolution) : 0));
			CHECK(igs->last_line == (flags[k] == 0 ? before_last : false));
			st_pixel_buffer_destroy(b);
		}
		destroy(&f);
	}
	/* Chapter49's hand-derived radius4 path has exclusive top/bottom rows
	 * for fill alone. These literal limits also detect host trig rounding. */
	struct fixture f; create(&f,0,false); struct cterm_igs *igs = f.terminal->igs_engine;
	feed(&f,"G#A>1,1,0:B 33,32,93,74,1:");
	const struct st_console_rect area = {32,31,63,45}; struct st_pixel_buffer *b = NULL;
	CHECK(st_console_get_pixels(&b,f.console,&area,false) == 0);
	for (unsigned y = 0; y < area.height; ++y) for (unsigned x = 0; x < area.width; ++x) {
		unsigned py = area.y+y, px = area.x+x, lo = 100, hi = 0;
		if (py >= 36 && py <= 70) { lo = 33; hi = 93; }
		else if (py == 33 || py == 73) { lo = 36; hi = 90; }
		else if (py == 34 || py == 72) { lo = 35; hi = 91; }
		else if (py == 35 || py == 71) { lo = 34; hi = 92; }
		CHECK(b->primary[y*area.width+x] == 768u+(px >= lo && px <= hi ? 15 : 0));
	}
	CHECK(!igs->last_line); st_pixel_buffer_destroy(b);
	igs->random_range[0][1] = 255;
	feed(&f,"G#B 40,40,100,80,r:"); CHECK(igs->random_seed == UINT32_C(0xbb40e62e) && !igs->last_line);
	destroy(&f);
}

static void
random_flag_partitions(void)
{
	const char wire[] = "AG#g>1:B>1000,2000,3000,5000,r:M 2:B";
	for (unsigned resolution = 0; resolution < 3; ++resolution) for (size_t split = 0; split <= sizeof(wire); ++split) {
		struct fixture f; create(&f,resolution,false); f.terminal->response_cb = no_response;
		struct cterm_igs *igs = f.terminal->igs_engine; igs->random_range[0][1] = 255;
		size_t used;
		if (split == sizeof(wire)) {
			for (size_t i = 0; i < sizeof(wire)-1; ++i) CHECK(st_cterm_write(f.terminal,wire+i,1,&used) == 0 && used == 1);
		} else {
			CHECK(st_cterm_write(f.terminal,wire,split,&used) == 0 && used == split);
			CHECK(st_cterm_write(f.terminal,wire+split,sizeof(wire)-1-split,&used) == 0 && used == sizeof(wire)-1-split);
		}
		CHECK(igs->random_seed == UINT32_C(0xbb40e62e) && igs->attributes.writing_mode == 2);
		CHECK(igs->draw_x == 0 && igs->draw_start_x == 0 && igs->last_line);
		const struct st_console_rect area = {resolution == 0 ? 32u : 64u,resolution == 2 ? 80u : 40u,21,21};
		struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f.console,&area,false) == 0);
		CHECK(b->primary[0] == 768 && b->primary[20*21+20] == 768u+cterm_igs_plane_mask(resolution));
		st_pixel_buffer_destroy(b);
		struct st_console_cell cells[2]; const struct st_console_rect text = {0,0,2,1};
		CHECK(st_console_read_cells(f.console,&text,cells,2) == 0 && cells[0].glyph == 'A' && cells[1].glyph == 'B');
		destroy(&f);
	}
}

int
st_cterm_igs_box_test_main(void)
{
	literal(); pens_and_edges(); input_and_recovery(); scaling_and_lifetime(); invalid_state(); flags_and_rounding(); random_flag_partitions(); vectors();
	return 0;
}
