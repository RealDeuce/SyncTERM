/* Y: independent ellipse radii, patterned spans and native radial closure. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "igs_fill_test_helpers.h"

static void
literal(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f,resolution,false);
		const struct st_console_rect area = {95,75,16,10}; seed(&f,&area,768,768);
		feed(&f,"G#Y 100,80,10,4,0,90:");
		struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f.console,&area,false) == 0);
		CHECK(b->primary[5] == 768 && b->primary[area.width+5] == 768u+cterm_igs_plane_mask(resolution));
		CHECK(b->primary[3*area.width+10] == 768u+cterm_igs_plane_mask(resolution));
		CHECK(b->primary[8*area.width] == 768);
		st_pixel_buffer_destroy(b); destroy(&f);
	}
}

struct ellpie_case {
	int resolution, mode, interior, index, perimeter, clipping, initial_last, expected_last;
	int x, y, radius, radius_y, begin, end, bounds[4];
	uint64_t hashes[2];
};
#include "data/igs_ellpie_vectors.h"
_Static_assert(sizeof(ellpie_cases)/sizeof(*ellpie_cases) == 5286, "complete native elliptical pie matrix");

static void
vectors(void)
{
	struct fixture f; bool active = false; int resolution = -1;
	for (size_t i = 0; i < sizeof(ellpie_cases)/sizeof(*ellpie_cases); ++i) {
		const struct ellpie_case *c = &ellpie_cases[i];
		if (resolution != c->resolution) {
			if (active) destroy(&f);
			create(&f,(unsigned)c->resolution,false); active = true; resolution = c->resolution;
			f.terminal->response_cb = no_response;
		}
		struct cterm_igs *igs = f.terminal->igs_engine; seed_words(&f);
		igs->draw_start_x = 17; igs->draw_start_y = 23; igs->draw_x = 37; igs->draw_y = 47;
		igs->attributes.fill_pen = 7; igs->attributes.line_pen = 10;
		igs->attributes.line_type = 7; igs->attributes.line_width = 39;
		igs->attributes.line_begin = 1; igs->attributes.line_end = 2; igs->attributes.user_line = 0xa531;
		memcpy(igs->patterns[3],user_rows,sizeof(user_rows));
		char wire[96]; (void)snprintf(wire,sizeof(wire),"G#A>%d,%d,%d:M %d:",c->interior,c->interior == 4 ? 3 : c->index,c->perimeter,c->mode); feed(&f,wire);
		if (c->interior == 4) for (unsigned j = 0; j < 16; ++j) igs->patterns[3][j] ^= 0xffff;
		igs->clipping = c->clipping != 0; igs->last_line = c->initial_last != 0;
		igs->clip_x1 = c->bounds[0]; igs->clip_y1 = c->bounds[1]; igs->clip_x2 = c->bounds[2]; igs->clip_y2 = c->bounds[3];
		igs->math[0] = (int16_t)c->x; igs->math[1] = (int16_t)c->y; igs->math[2] = (int16_t)c->radius; igs->math[3] = (int16_t)c->radius_y; igs->math[4] = (int16_t)c->begin; igs->math[5] = (int16_t)c->end;
		struct cterm_igs_attributes before; memcpy(&before,&igs->attributes,sizeof(before));
		uint32_t palette[16]; memcpy(palette,igs->palette,sizeof(palette));
		int16_t math[128]; memcpy(math,igs->math,sizeof(math));
		uint16_t patterns[8][16]; memcpy(patterns,igs->patterns,sizeof(patterns));
		feed(&f,"G#Y 0],1],2],3],4],5]:");
		CHECK(memcmp(math,igs->math,sizeof(math)) == 0 && memcmp(patterns,igs->patterns,sizeof(patterns)) == 0);
		CHECK(igs->draw_start_x == 17 && igs->draw_start_y == 23 && igs->draw_x == 37 && igs->draw_y == 47);
		CHECK(memcmp(&before,&igs->attributes,sizeof(before)) == 0 && memcmp(palette,igs->palette,sizeof(palette)) == 0);
		CHECK(igs->last_line == (c->expected_last != 0));
		CHECK(igs->clipping == (c->clipping != 0) && igs->clip_x1 == c->bounds[0] && igs->clip_y2 == c->bounds[3]);
		CHECK(igs->math[0] == c->x && igs->math[1] == c->y && igs->math[2] == c->radius && igs->random_seed == 1);
		const struct st_console_rect full = {0,0,igs->width,igs->height}; struct st_pixel_buffer *b = NULL;
		CHECK(st_console_get_pixels(&b,f.console,&full,false) == 0);
		for (unsigned phase = 0; phase < 2; ++phase) {
			uint64_t hash = native_hash(b,(unsigned[]){4,2,1}[resolution],phase != 0);
			if (hash != c->hashes[phase]) fprintf(stderr,"Y %zu res%d radius%d interior%d index%d per%d mode%d phase%u: %016" PRIx64 " != %016" PRIx64 "\n",
			    i,resolution,c->radius,c->interior,c->index,c->perimeter,c->mode,phase,hash,c->hashes[phase]);
			CHECK(hash == c->hashes[phase]);
		}
		st_pixel_buffer_destroy(b);
	}
	if (active) destroy(&f);
}

static void
pens_and_radius_policy(void)
{
	static const unsigned map[16] = {0,15,1,2,4,6,3,5,7,8,9,10,12,14,11,13};
	for (unsigned resolution = 0; resolution < 3; ++resolution) for (unsigned status = 0; status < 2; ++status) {
		struct fixture f; create(&f,resolution,status != 0); f.terminal->response_cb = no_response;
		struct cterm_igs *igs = f.terminal->igs_engine; feed(&f,"G#A 1,1,0:");
		const struct st_console_rect point = {105,78,1,1};
		for (unsigned mode = 1; mode <= 4; ++mode) for (unsigned pen = 0; pen < 16; ++pen) {
			seed(&f,&point,768,768); char wire[80]; (void)snprintf(wire,sizeof(wire),"G#C>2,%u:M>%u:Y 100,80,10,4,0,90:",pen,mode); feed(&f,wire);
			struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f.console,&point,false) == 0);
			unsigned effective = pen > cterm_igs_plane_mask(resolution) ? 1 : pen;
			unsigned ink = mode == 3 ? cterm_igs_plane_mask(resolution) : mode == 4 ? 0 : map[effective]&cterm_igs_plane_mask(resolution);
			CHECK(b->primary[0] == 768u+ink && b->alternate[0] == 768u+ink); st_pixel_buffer_destroy(b);
		}
		/* Fill/perimeter are independent of even unusable line attributes. */
		igs->attributes.line_type = 0; igs->attributes.line_width = 2;
		feed(&f,"G#C>2,1:A>1,1,1:M>1:Y 100,80,10,4,0,90:");
		struct st_pixel_buffer *sample = NULL; CHECK(st_console_get_pixels(&sample,f.console,&point,false) == 0);
		CHECK(sample->primary[0] == 768u+cterm_igs_plane_mask(resolution) && sample->alternate[0] == sample->primary[0]);
		CHECK(igs->attributes.line_type == 0 && igs->attributes.line_width == 2); st_pixel_buffer_destroy(sample);
		feed(&f,"G#A 1,1,0:");
		/* Correct wrapped-radius/coordinate accidents using wide geometry.
		 * A full huge pie covers the host canvas; its border is outside it. */
		const struct st_console_rect full = {0,0,igs->width,igs->height};
		for (unsigned negative = 0; negative < 2; ++negative) {
			seed(&f,&full,768,768); igs->clipping = false; igs->math[0] = negative ? INT16_MIN : INT16_MAX;
			feed(&f,"G#C>2,1:M>1:Y 80,80,0],0],0,360:");
			struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f.console,&full,false) == 0);
			unsigned drawable = igs->height-(status ? (resolution == 2 ? 16u : 8u) : 0);
			for (unsigned y = 0; y < igs->height; ++y) for (unsigned x = 0; x < igs->width; ++x)
				CHECK(b->primary[y*igs->width+x] == 768u+(y < drawable ? cterm_igs_plane_mask(resolution) : 0)
				    && b->alternate[y*igs->width+x] == b->primary[y*igs->width+x]);
			CHECK(!igs->last_line); st_pixel_buffer_destroy(b);
		}
		cterm_igs_clip_screen(igs); seed(&f,&full,768,768); igs->last_line = true;
		feed(&f,"G#Y 80,80,0],0],0,360:"); CHECK(igs->last_line); /* negative native coarse rejection */
		struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f.console,&full,false) == 0);
		for (size_t i = 0; i < (size_t)igs->width*igs->height; ++i) CHECK(b->primary[i] == 768 && b->alternate[i] == 768);
		st_pixel_buffer_destroy(b); destroy(&f);
	}
}

static void
partitions(void)
{
	const char *wires[] = {"AG#M>4:L>17,23,37,47:M>1:Y>80,80,17,9,25,275:M 2:B","AG#g>1:Y>r,R,r,R,r,R:M 2:B"};
	for (unsigned resolution = 0; resolution < 3; ++resolution) for (unsigned which = 0; which < 2; ++which)
		for (size_t split = 0; split <= strlen(wires[which])+1; ++split) {
			struct fixture f; create(&f,resolution,false); f.terminal->response_cb = no_response;
			struct cterm_igs *igs = f.terminal->igs_engine;
			igs->random_range[0][1] = igs->random_range[1][1] = 255;
			size_t used, size = strlen(wires[which]);
			if (split > size) for (size_t i = 0; i < size; ++i) CHECK(st_cterm_write(f.terminal,wires[which]+i,1,&used) == 0 && used == 1);
			else {
				CHECK(st_cterm_write(f.terminal,wires[which],split,&used) == 0 && used == split);
				CHECK(st_cterm_write(f.terminal,wires[which]+split,size-split,&used) == 0 && used == size-split);
			}
			CHECK(igs->attributes.writing_mode == 2 && igs->draw_x == (which ? 0 : 37));
			CHECK(igs->random_seed == (which ? UINT32_C(0x9b958193) : 1));
			struct st_console_cell cells[2]; const struct st_console_rect text = {0,0,2,1};
			CHECK(st_console_read_cells(f.console,&text,cells,2) == 0 && cells[0].glyph == 'A' && cells[1].glyph == 'B');
			destroy(&f);
		}
}

static void
scaling_and_lifetime(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f,resolution,false); f.terminal->response_cb = no_response;
		struct cterm_igs *igs = f.terminal->igs_engine;
		feed(&f,"G#M>4:L>17,23,37,47:M>1:g>2:Y 40,60,10,4,0,90:");
		static const unsigned top[3] = {56,56,112};
		const struct st_console_rect area = {40,top[resolution]-1,1,2}; struct st_pixel_buffer *b = NULL;
		CHECK(st_console_get_pixels(&b,f.console,&area,false) == 0);
		CHECK(b->primary[0] == 768 && b->primary[1] == 768u+cterm_igs_plane_mask(resolution)); st_pixel_buffer_destroy(b);
		CHECK(igs->draw_start_x == 17 && igs->draw_y == 47);
		feed(&f,"G#I 2:");
		const char *wire = resolution == 0 ? "G#R>1,0:Y 40,60,10,4,0,90:" : "G#R>0,0:Y 40,60,10,4,0,90:";
		size_t used, prefix = strlen("G#R>1,0:"); CHECK(st_cterm_write(f.terminal,wire,strlen(wire),&used) == 0);
		if (resolution == 2) CHECK(used == strlen(wire));
		else { CHECK(used == prefix); local_ms += 17; CHECK(st_cterm_poll(f.terminal) == 0); feed(&f,wire+prefix); }
		CHECK(igs->draw_x == 37 && igs->scaling_resolution == (int)resolution);
		feed(&f,"G#I 5:"); if (cterm_igs_paused(igs)) { local_ms += 17; CHECK(st_cterm_poll(f.terminal) == 0); }
		CHECK(igs->resolution == resolution && igs->draw_y == 47);
		igs->math[0] = 100; igs->math[1] = igs->math[2] = 80; igs->math[3] = igs->math[6] = 10; igs->math[4] = igs->math[8] = 4; igs->math[67] = 0; igs->math[68] = 90;
		const struct st_console_rect tip = {110,80,1,1}; seed(&f,&tip,768,768);
		feed(&f,"G#g>1:Y 0],50],100],200],67],68]:");
		CHECK(st_console_get_pixels(&b,f.console,&tip,false) == 0 && b->primary[0] == 768u+cterm_igs_plane_mask(resolution)); st_pixel_buffer_destroy(b);
		const char *partial[] = {"G#Y ","G#Y 1,","G#Y 1,2,","G#Y 1,2,3,","G#Y 1,2,3,4,","G#Y 1,2,3,4,5,","G#Y r,R,r,R,r,"};
		const struct st_console_rect full = {0,0,igs->width,igs->height}; seed_words(&f);
		struct st_pixel_buffer *before = NULL; CHECK(st_console_get_pixels(&before,f.console,&full,false) == 0);
		for (unsigned i = 0; i < 7; ++i) {
			feed(&f,partial[i]); CHECK(st_cterm_finish(f.terminal) == 0);
			feed(&f,partial[i]); local_ms += 5001; CHECK(st_cterm_poll(f.terminal) == 0);
		}
		const char bad[] = "G#Y 1,2,3,4,5,10000:";
		CHECK(st_cterm_write(f.terminal,bad,sizeof(bad)-1,&used) == 0);
		struct st_pixel_buffer *after = NULL; CHECK(st_console_get_pixels(&after,f.console,&full,false) == 0);
		CHECK(memcmp(before->primary,after->primary,(size_t)igs->width*igs->height*sizeof(*before->primary)) == 0);
		CHECK(memcmp(before->alternate,after->alternate,(size_t)igs->width*igs->height*sizeof(*before->alternate)) == 0);
		CHECK(igs->random_seed == 1 && igs->draw_x == 37);
		st_pixel_buffer_destroy(after);
		/* Renderer validation must also leave an acquired command atomic. */
		for (unsigned invalid = 0; invalid < 2; ++invalid) {
			igs->attributes.fill_interior = invalid ? 5 : 3; igs->attributes.fill_index = 13;
			struct cterm_igs_attributes attrs; memcpy(&attrs,&igs->attributes,sizeof(attrs));
			bool last = igs->last_line;
			const char rejected[] = "G#Y 80,80,17,9,25,275:";
			CHECK(st_cterm_write(f.terminal,rejected,sizeof(rejected)-1,&used) == 0);
			CHECK(memcmp(&attrs,&igs->attributes,sizeof(attrs)) == 0 && igs->last_line == last && igs->draw_x == 37);
			CHECK(st_console_get_pixels(&after,f.console,&full,false) == 0);
			CHECK(memcmp(before->primary,after->primary,(size_t)igs->width*igs->height*sizeof(*before->primary)) == 0);
			CHECK(memcmp(before->alternate,after->alternate,(size_t)igs->width*igs->height*sizeof(*before->alternate)) == 0);
			st_pixel_buffer_destroy(after);
			/* Coarse rejection precedes fill validation and preserves last. */
			igs->math[0] = -1; feed(&f,"G#Y 0],0],0],0],0,90:"); CHECK(igs->last_line == last);
		}
		st_pixel_buffer_destroy(before);
		feed(&f,"G#Y 1,"); CHECK(st_cterm_reset_terminal(f.terminal) == 0);
		CHECK(igs->draw_x == 0 && igs->draw_start_x == 0 && igs->last_line);
		feed(&f,"G#Y 80,80,17,9,25,275:"); CHECK(igs->draw_x == 0);
		destroy(&f);
	}
}

int
st_cterm_igs_ellpie_test_main(void)
{
	literal(); pens_and_radius_policy(); partitions(); scaling_and_lifetime(); vectors();
	return 0;
}
