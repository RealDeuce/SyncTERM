/* U: rounded outline/fill/negative flag and preserved workstation attributes. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "igs_fill_test_helpers.h"
#include "igs_box_test_vectors.h"

static void
literal(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f,resolution,false);
		const struct st_console_rect area = {109,79,3,3}; seed(&f,&area,768,768);
		feed(&f,"G#U 100,80,140,110,0:");
		struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f.console,&area,false) == 0);
		for (unsigned x = 0; x < 3; ++x) CHECK(b->primary[3+x] == 768u+cterm_igs_plane_mask(resolution));
		st_pixel_buffer_destroy(b); destroy(&f);
	}
}

struct round_box_case {
	int resolution, mode, style, width, begin, end, clipping, initial_last, expected_last;
	int points[4], bounds[4];
	uint64_t hashes[2];
};
#include "data/igs_round_box_vectors.h"
_Static_assert(sizeof(round_box_cases)/sizeof(*round_box_cases) == 1845, "complete rounded outline matrix");

static void
verify(struct fixture *f, const uint64_t hashes[2], size_t number, bool filled)
{
	struct cterm_igs *igs = f->terminal->igs_engine;
	const struct st_console_rect full = {0,0,igs->width,igs->height}; struct st_pixel_buffer *b = NULL;
	CHECK(st_console_get_pixels(&b,f->console,&full,false) == 0);
	for (unsigned phase = 0; phase < 2; ++phase) {
		uint64_t hash = native_hash(b,(unsigned[]){4,2,1}[igs->resolution],phase != 0);
		if (hash != hashes[phase]) fprintf(stderr,"U %s %zu res%u width%d type%d caps%d/%d mode%d phase%u: %016" PRIx64 " != %016" PRIx64 "\n",
		    filled ? "fill" : "outline",number,igs->resolution,igs->attributes.line_width,igs->attributes.line_type,
		    igs->attributes.line_begin,igs->attributes.line_end,igs->attributes.writing_mode,phase,hash,hashes[phase]);
		CHECK(hash == hashes[phase]);
	}
	st_pixel_buffer_destroy(b);
}

static void
outlines(void)
{
	struct fixture f; bool active = false; int resolution = -1;
	for (size_t i = 0; i < sizeof(round_box_cases)/sizeof(*round_box_cases); ++i) {
		const struct round_box_case *c = &round_box_cases[i];
		if (resolution != c->resolution) {
			if (active) destroy(&f);
			create(&f,(unsigned)c->resolution,false); active = true; resolution = c->resolution;
			f.terminal->response_cb = no_response;
		}
		struct cterm_igs *igs = f.terminal->igs_engine; seed_words(&f);
		igs->draw_start_x = 17; igs->draw_start_y = 23; igs->draw_x = 37; igs->draw_y = 47;
		igs->attributes.line_type = c->style; igs->attributes.line_width = c->width;
		igs->attributes.line_begin = c->begin; igs->attributes.line_end = c->end;
		igs->attributes.user_line = 0xa531; igs->attributes.line_pen = 10;
		igs->attributes.writing_mode = c->mode;
		igs->attributes.fill_interior = 4; igs->attributes.fill_index = 24; igs->attributes.fill_pen = 7;
		igs->attributes.fill_perimeter = c->style%2 != 0; memcpy(igs->attributes.user_fill,user_rows,sizeof(user_rows));
		igs->last_line = c->initial_last != 0; igs->clipping = c->clipping != 0;
		igs->clip_x1 = c->bounds[0]; igs->clip_y1 = c->bounds[1]; igs->clip_x2 = c->bounds[2]; igs->clip_y2 = c->bounds[3];
		for (unsigned j = 0; j < 4; ++j) igs->math[j] = (int16_t)c->points[j];
		struct cterm_igs_attributes before; memcpy(&before,&igs->attributes,sizeof(before));
		uint32_t palette[16]; memcpy(palette,igs->palette,sizeof(palette));
		feed(&f,"G#U 0],1],2],3],0:");
		CHECK(memcmp(&before,&igs->attributes,sizeof(before)) == 0 && memcmp(palette,igs->palette,sizeof(palette)) == 0);
		CHECK(igs->draw_start_x == 17 && igs->draw_start_y == 23 && igs->draw_x == 37 && igs->draw_y == 47);
		CHECK(igs->last_line == (c->expected_last != 0));
		CHECK(igs->clipping == (c->clipping != 0) && igs->clip_x1 == c->bounds[0] && igs->clip_y2 == c->bounds[3]);
		for (unsigned j = 0; j < 4; ++j) CHECK(igs->math[j] == c->points[j]);
		CHECK(igs->random_seed == 1); verify(&f,c->hashes,i,false);
	}
	if (active) destroy(&f);
}

static void
filled(void)
{
	struct fixture f; bool active = false; int resolution = -1; size_t count = 0;
	for (size_t i = 0; i < sizeof(box_cases)/sizeof(*box_cases); ++i) {
		const struct box_case *c = &box_cases[i]; if (c->flag == 0) continue;
		++count;
		if (resolution != c->resolution) {
			if (active) destroy(&f);
			create(&f,(unsigned)c->resolution,false); active = true; resolution = c->resolution;
			f.terminal->response_cb = no_response;
		}
		struct cterm_igs *igs = f.terminal->igs_engine; seed_words(&f);
		igs->draw_start_x = 17; igs->draw_start_y = 23; igs->draw_x = 37; igs->draw_y = 47;
		igs->attributes.line_pen = 10; igs->attributes.line_type = 7; igs->attributes.line_width = 39;
		igs->attributes.line_begin = 1; igs->attributes.line_end = 2; igs->attributes.user_line = 0xa531;
		igs->attributes.fill_pen = 7; memcpy(igs->patterns[3],user_rows,sizeof(user_rows));
		char wire[80]; (void)snprintf(wire,sizeof(wire),"G#A>%d,%d,%d:M %d:",c->interior,c->interior == 4 ? 3 : c->index,c->perimeter,c->mode); feed(&f,wire);
		if (c->interior == 4) for (unsigned row = 0; row < 16; ++row) igs->patterns[3][row] ^= 0xffff;
		igs->clipping = c->clipping != 0; igs->clip_x1 = c->bounds[0]; igs->clip_y1 = c->bounds[1];
		igs->clip_x2 = c->bounds[2]; igs->clip_y2 = c->bounds[3]; igs->last_line = c->initial_last != 0;
		for (unsigned j = 0; j < 4; ++j) igs->math[j] = (int16_t)c->points[j];
		igs->math[127] = (int16_t[]){1,9999,INT16_MAX}[i%3];
		struct cterm_igs_attributes before; memcpy(&before,&igs->attributes,sizeof(before));
		feed(&f,"G#U 0],1],2],3],127]:");
		CHECK(memcmp(&before,&igs->attributes,sizeof(before)) == 0);
		CHECK(igs->draw_start_x == 17 && igs->draw_y == 47 && igs->last_line == (c->expected_last != 0));
		verify(&f,c->hashes,i,true);
	}
	CHECK(count == 1344); if (active) destroy(&f);
}

static void
negative_flags(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f,resolution,false); f.terminal->response_cb = no_response;
		struct cterm_igs *igs = f.terminal->igs_engine; seed_words(&f);
		const struct st_console_rect full = {0,0,igs->width,igs->height}; struct st_pixel_buffer *before = NULL;
		CHECK(st_console_get_pixels(&before,f.console,&full,false) == 0);
		for (unsigned g = 0; g < 3; ++g) for (unsigned flag = 0; flag < 2; ++flag) {
			char wire[96]; (void)snprintf(wire,sizeof(wire),"G#g %u:",g); feed(&f,wire);
			igs->math[127] = flag ? INT16_MIN : -1;
			igs->attributes.line_width = 0; igs->attributes.fill_interior = 99;
			igs->last_line = flag != 0;
			struct cterm_igs_attributes attrs; memcpy(&attrs,&igs->attributes,sizeof(attrs));
			feed(&f,"G#U 1,2,3,4,127]:");
			CHECK(igs->last_line == (flag != 0) && memcmp(&attrs,&igs->attributes,sizeof(attrs)) == 0);
		}
		struct st_pixel_buffer *after = NULL; CHECK(st_console_get_pixels(&after,f.console,&full,false) == 0);
		CHECK(memcmp(before->primary,after->primary,(size_t)igs->width*igs->height*sizeof(*before->primary)) == 0);
		CHECK(memcmp(before->alternate,after->alternate,(size_t)igs->width*igs->height*sizeof(*before->alternate)) == 0);
		st_pixel_buffer_destroy(before); st_pixel_buffer_destroy(after);
		/* Outline failure must not expose temporary cap clearing. */
		igs->attributes.line_begin = 1; igs->attributes.line_end = 2;
		struct cterm_igs_attributes attrs; memcpy(&attrs,&igs->attributes,sizeof(attrs));
		size_t used; const char bad_outline[] = "G#U 40,60,80,80,0:";
		CHECK(st_cterm_write(f.terminal,bad_outline,sizeof(bad_outline)-1,&used) == 0);
		CHECK(memcmp(&attrs,&igs->attributes,sizeof(attrs)) == 0);
		destroy(&f);
	}
}

static void
partitions(void)
{
	const char *wires[] = {"AG#M>4:L>17,23,37,47:M>1:U>40,60,80,80,0:M 2:B",
	    "AG#g>1:U>r,R,r,R,127]:M 2:B","AG#U>40,60,80,80,r:M 2:B"};
	for (unsigned resolution = 0; resolution < 3; ++resolution) for (unsigned which = 0; which < 3; ++which)
		for (size_t split = 0; split <= strlen(wires[which])+1; ++split) {
			struct fixture f; create(&f,resolution,false); f.terminal->response_cb = no_response;
			struct cterm_igs *igs = f.terminal->igs_engine; igs->math[127] = -1;
			igs->random_range[0][1] = igs->random_range[1][1] = 255;
			size_t used, size = strlen(wires[which]);
			if (split > size) for (size_t i = 0; i < size; ++i) CHECK(st_cterm_write(f.terminal,wires[which]+i,1,&used) == 0 && used == 1);
			else {
				CHECK(st_cterm_write(f.terminal,wires[which],split,&used) == 0 && used == split);
				CHECK(st_cterm_write(f.terminal,wires[which]+split,size-split,&used) == 0 && used == size-split);
			}
			CHECK(igs->attributes.writing_mode == 2 && igs->draw_x == (which ? 0 : 37));
			CHECK(igs->random_seed == (which == 0 ? 1 : which == 1 ? UINT32_C(0x58543a1d) : UINT32_C(0xbb40e62e)));
			struct st_console_cell cells[2]; const struct st_console_rect text = {0,0,2,1};
			CHECK(st_console_read_cells(f.console,&text,cells,2) == 0 && cells[0].glyph == 'A' && cells[1].glyph == 'B');
			destroy(&f);
		}
}

static void
recovery_and_lifetime(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f,resolution,false); f.terminal->response_cb = no_response;
		struct cterm_igs *igs = f.terminal->igs_engine;
		feed(&f,"G#M>4:L>17,23,37,47:T>2,1,50:M>1:g>2:U 40,60,80,80,0:");
		CHECK(igs->draw_start_x == 17 && igs->draw_y == 47 && igs->attributes.line_begin == 1 && igs->attributes.line_end == 1);
		const struct st_console_rect point = {50,resolution == 2 ? 120u : 60u,1,1}; struct st_pixel_buffer *b = NULL;
		CHECK(st_console_get_pixels(&b,f.console,&point,false) == 0 && b->primary[0] == 768u+cterm_igs_plane_mask(resolution)); st_pixel_buffer_destroy(b);
		feed(&f,"G#I 2:"); CHECK(igs->draw_x == 37);
		const char *wire = resolution == 0 ? "G#R>1,0:U 40,60,80,80,0:" : "G#R>0,0:U 40,60,80,80,0:";
		size_t used, prefix = strlen("G#R>1,0:"); CHECK(st_cterm_write(f.terminal,wire,strlen(wire),&used) == 0);
		if (resolution == 2) CHECK(used == strlen(wire));
		else { CHECK(used == prefix); local_ms += 17; CHECK(st_cterm_poll(f.terminal) == 0); feed(&f,wire+prefix); }
		CHECK(igs->draw_x == 37 && igs->scaling_resolution == (int)resolution);
		feed(&f,"G#I 5:"); if (cterm_igs_paused(igs)) { local_ms += 17; CHECK(st_cterm_poll(f.terminal) == 0); }
		CHECK(igs->resolution == resolution && igs->draw_x == 37);
		const char *partial[] = {"G#U ","G#U 1,2,3,4,","G#U r,R,r,R,","G#U 1,2,3,4,r"};
		const struct st_console_rect full = {0,0,igs->width,igs->height}; seed_words(&f);
		struct st_pixel_buffer *before = NULL; CHECK(st_console_get_pixels(&before,f.console,&full,false) == 0);
		for (unsigned i = 0; i < 4; ++i) {
			feed(&f,partial[i]); CHECK(st_cterm_finish(f.terminal) == 0);
			feed(&f,partial[i]); local_ms += 5001; CHECK(st_cterm_poll(f.terminal) == 0);
		}
		const char bad[] = "G#U 1,2,3,4,10000:";
		CHECK(st_cterm_write(f.terminal,bad,sizeof(bad)-1,&used) == 0);
		struct st_pixel_buffer *after = NULL; CHECK(st_console_get_pixels(&after,f.console,&full,false) == 0);
		CHECK(memcmp(before->primary,after->primary,(size_t)igs->width*igs->height*sizeof(*before->primary)) == 0);
		CHECK(memcmp(before->alternate,after->alternate,(size_t)igs->width*igs->height*sizeof(*before->alternate)) == 0);
		CHECK(igs->draw_x == 37 && igs->random_seed == 1);
		st_pixel_buffer_destroy(before); st_pixel_buffer_destroy(after);
		feed(&f,"G#U 1,2,"); CHECK(st_cterm_reset_terminal(f.terminal) == 0);
		CHECK(igs->draw_x == 0 && igs->draw_start_x == 0 && igs->attributes.line_begin == 0);
		feed(&f,"G#U 40,60,80,80,0:"); CHECK(igs->draw_x == 0);
		destroy(&f);
	}
}

int
st_cterm_igs_round_box_test_main(void)
{
	literal(); negative_flags(); partitions(); recovery_and_lifetime(); outlines(); filled();
	return 0;
}
