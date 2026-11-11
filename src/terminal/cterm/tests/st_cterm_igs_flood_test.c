/* F: independent connected components, native run order and serial ownership. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "igs_fill_test_helpers.h"

static void
literal(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f,resolution,false); struct cterm_igs *igs = f.terminal->igs_engine;
		igs->clip_x1 = 40; igs->clip_y1 = 60; igs->clip_x2 = 44; igs->clip_y2 = 64;
		igs->clipping = false; feed(&f,"G#F 42,62:");
		const struct st_console_rect area = {39,59,7,7}; struct st_pixel_buffer *b = NULL;
		CHECK(st_console_get_pixels(&b,f.console,&area,false) == 0);
		for (unsigned y = 0; y < 7; ++y) for (unsigned x = 0; x < 7; ++x)
			CHECK(b->primary[y*7+x] == 768u+(x >= 1 && x <= 5 && y >= 1 && y <= 5 ? cterm_igs_plane_mask(resolution) : 0)
			    && b->alternate[y*7+x] == b->primary[y*7+x]);
		CHECK(!igs->last_line && igs->draw_x == 0 && igs->draw_y == 0);
		st_pixel_buffer_destroy(b); destroy(&f);
	}
}

struct responses { struct fixture *f; unsigned count, action; uint8_t bytes[32]; };
static void
respond(const char *bytes, size_t count, void *data)
{
	struct responses *r = data;
	CHECK(count == 1 && r->count < sizeof(r->bytes));
	r->bytes[r->count++] = (uint8_t)*bytes;
	struct cterm_igs *igs = r->f->terminal->igs_engine;
	if (r->action == 1 && r->count == 1) {
		igs->flow_mode = 0; igs->flow_off = 0xab; igs->flow_off_count = 3;
	} else if (r->action == 2 && r->count == 1) igs->loop_mode = 1;
	else if (r->action == 3 && r->count == 1) CHECK(st_cterm_reset_terminal(r->f->terminal) == 0);
	else if (r->action == 4) {
		const struct st_console_rect area = {40,60,1,1}; struct st_pixel_buffer *b = NULL;
		CHECK(st_console_get_pixels(&b,r->f->console,&area,false) == 0);
		CHECK(b->primary[0] == (r->count == 1 ? 768u : 783u));
		st_pixel_buffer_destroy(b);
	}
}

static void
responses(struct fixture *f, struct responses *r)
{
	*r = (struct responses){.f = f};
	f->terminal->response_cb = respond; f->terminal->response_cbdata = r;
}

static void
count_response(const char *bytes, size_t count, void *data)
{
	unsigned *total = data;
	CHECK(count == 1 && *total < 65534);
	CHECK((uint8_t)*bytes == (*total < 32767 ? 19 : 17));
	++*total;
}

static void
flow_and_recovery(void)
{
	struct fixture f; create(&f,0,false); struct responses r; responses(&f,&r);
	struct cterm_igs *igs = f.terminal->igs_engine;
	CHECK(igs->flow_mode == 1 && igs->flow_off_count == 1 && igs->flow_on_count == 1);
	const char *partial[] = {"G#F ","G#F 40,","G#F r,","G#F 9999,"};
	for (unsigned i = 0; i < 4; ++i) {
		feed(&f,partial[i]); CHECK(r.count == 0); CHECK(st_cterm_finish(f.terminal) == 0);
		feed(&f,partial[i]); local_ms += 5001; CHECK(st_cterm_poll(f.terminal) == 0); CHECK(r.count == 0);
	}
	igs->random_range[0][0] = igs->random_range[0][1] = 2;
	feed(&f,"G#F r,"); CHECK(igs->random_seed == 1 && r.count == 0);
	CHECK(st_cterm_finish(f.terminal) == 0 && igs->random_seed == 1);
	feed(&f,"G#F r,"); local_ms += 5001; CHECK(st_cterm_poll(f.terminal) == 0);
	CHECK(igs->random_seed == 1 && r.count == 0);
	size_t used; const char bad[] = "G#F 40,10000:";
	CHECK(st_cterm_write(f.terminal,bad,sizeof(bad)-1,&used) == 0 && r.count == 0);
	const struct st_console_rect untouched = {0,0,320,200}; struct st_pixel_buffer *blank = NULL;
	CHECK(st_console_get_pixels(&blank,f.console,&untouched,false) == 0);
	for (size_t i = 0; i < 320u*200; ++i) CHECK(blank->primary[i] == 768 && blank->alternate[i] == 768);
	st_pixel_buffer_destroy(blank);
	igs->flow_mode = 0; feed(&f,"G#F 40,60:"); CHECK(r.count == 0);
	igs->flow_mode = -1; igs->loop_mode = 1; feed(&f,"G#F 40,60:"); CHECK(r.count == 0);
	igs->loop_mode = 2; igs->flow_off_count = -1; igs->flow_on_count = 0;
	feed(&f,"G#F 40,60:"); CHECK(r.count == 0);
	igs->loop_mode = 0; igs->flow_off_count = 2; igs->flow_on_count = 3; igs->flow_off = 0xfe; igs->flow_on = 0x81;
	feed(&f,"G#F 40,60:"); CHECK(r.count == 5 && memcmp(r.bytes,"\xfe\xfe\x81\x81\x81",5) == 0);
	r.count = 0; r.action = 1; igs->flow_off_count = 1; igs->flow_off = 19;
	feed(&f,"G#F 40,60:"); CHECK(r.count == 3 && memcmp(r.bytes,"\x13\xab\xab",3) == 0);
	r.count = 0; r.action = 2; igs->flow_mode = 1; igs->flow_off_count = 1; igs->flow_off = 19;
	feed(&f,"G#F 40,60:"); CHECK(r.count == 1 && r.bytes[0] == 19);
	r.count = 0; r.action = 3; igs->loop_mode = 0;
	feed(&f,"G#F 40,60:G#M 4:"); CHECK(r.count == 1 && igs->attributes.writing_mode == 4);
	CHECK(igs->flow_mode == 1 && igs->flow_on == 17 && igs->flow_off == 19 && igs->loop_mode == 0);
	r.count = 0; r.action = 0;
	igs->attributes.fill_interior = -1;
	const char rejected[] = "G#F 40,60:";
	CHECK(st_cterm_write(f.terminal,rejected,sizeof(rejected)-1,&used) == 0);
	CHECK(r.count == 2 && r.bytes[0] == 19 && r.bytes[1] == 17); igs->attributes.fill_interior = 1;
	r.count = 0;
	feed(&f,"G#F 40,"); CHECK(st_cterm_reset_terminal(f.terminal) == 0); CHECK(r.count == 0);
	feed(&f,"G#F 40,60:"); CHECK(r.count == 2);
	const struct st_console_rect area = {40,60,41,1}; seed(&f,&area,768,768);
	r.count = 0; r.action = 4; feed(&f,"G#M>1:F 40,60:"); CHECK(r.count == 2);
	unsigned total = 0; f.terminal->response_cb = count_response; f.terminal->response_cbdata = &total;
	igs->flow_on_count = igs->flow_off_count = INT16_MAX;
	feed(&f,"G#F 40,60:"); CHECK(total == 65534);
	destroy(&f);
}
struct flood_case {
	int resolution, mode, interior, index, perimeter, clipping, expected_last, shape;
	int x, y, seed_x, seed_y, bounds[4];
	uint64_t hashes[2];
};
#include "data/igs_flood_vectors.h"
_Static_assert(sizeof(flood_cases)/sizeof(*flood_cases) == 3660, "complete independent flood matrix");

static void
vectors(void)
{
	struct fixture f; bool active = false; int resolution = -1; struct responses r;
	for (size_t i = 0; i < sizeof(flood_cases)/sizeof(*flood_cases); ++i) {
		const struct flood_case *c = &flood_cases[i];
		if (resolution != c->resolution) {
			if (active) destroy(&f);
			create(&f,(unsigned)c->resolution,false); active = true; resolution = c->resolution; responses(&f,&r);
		}
		struct cterm_igs *igs = f.terminal->igs_engine; seed_words(&f);
		struct st_pixel_buffer *grid = NULL; CHECK(st_pixel_buffer_create(&grid,19,11,true) == 0);
		for (unsigned y = 0; y < 11; ++y) for (unsigned x = 0; x < 19; ++x) {
			grid->primary[y*19+x] = 768u+(unsigned)(flood_grids[c->shape][y][x]-'0');
			grid->alternate[y*19+x] = 768u+(unsigned)(flood_grids[c->shape][y][18-x]-'0');
		}
		const struct st_pixel_blit blit = {.width = 19, .height = 11, .destination_x = c->x,
		    .destination_y = c->y, .scale_x = 1, .scale_y = 1};
		CHECK(st_console_blit_pixels(f.console,grid,NULL,&blit) == 0); st_pixel_buffer_destroy(grid);
		igs->draw_start_x = 17; igs->draw_start_y = 23; igs->draw_x = 37; igs->draw_y = 47;
		igs->attributes.fill_pen = 7; igs->attributes.line_pen = 10;
		igs->attributes.line_type = 0; igs->attributes.line_width = 2;
		igs->attributes.line_begin = 1; igs->attributes.line_end = 2; igs->attributes.user_line = 0xa531;
		memcpy(igs->patterns[3],user_rows,sizeof(user_rows));
		char wire[96]; (void)snprintf(wire,sizeof(wire),"G#A>%d,%d,%d:M %d:",c->interior,c->interior == 4 ? 3 : c->index,c->perimeter,c->mode); feed(&f,wire);
		if (c->interior == 4) for (unsigned j = 0; j < 16; ++j) igs->patterns[3][j] ^= 0xffff;
		igs->clipping = c->clipping != 0; igs->last_line = true;
		igs->clip_x1 = c->x+c->bounds[0]; igs->clip_y1 = c->y+c->bounds[1];
		igs->clip_x2 = c->x+c->bounds[2]; igs->clip_y2 = c->y+c->bounds[3];
		struct cterm_igs_attributes before = igs->attributes;
		uint32_t palette[16]; memcpy(palette,igs->palette,sizeof(palette));
		int16_t math[128]; memcpy(math,igs->math,sizeof(math));
		uint16_t patterns[8][16]; memcpy(patterns,igs->patterns,sizeof(patterns));
		(void)snprintf(wire,sizeof(wire),"G#F %d,%d:",c->x+c->seed_x,c->y+c->seed_y);
		r.count = 0; feed(&f,wire);
		CHECK(r.count == 2 && r.bytes[0] == 19 && r.bytes[1] == 17);
		CHECK(memcmp(math,igs->math,sizeof(math)) == 0 && memcmp(patterns,igs->patterns,sizeof(patterns)) == 0);
		CHECK(igs->draw_start_x == 17 && igs->draw_start_y == 23 && igs->draw_x == 37 && igs->draw_y == 47);
		CHECK(memcmp(&before,&igs->attributes,sizeof(before)) == 0 && memcmp(palette,igs->palette,sizeof(palette)) == 0);
		CHECK(igs->last_line == (c->expected_last != 0));
		CHECK(igs->clipping == (c->clipping != 0) && igs->random_seed == 1);
		const struct st_console_rect full = {0,0,igs->width,igs->height}; struct st_pixel_buffer *b = NULL;
		CHECK(st_console_get_pixels(&b,f.console,&full,false) == 0);
		for (unsigned phase = 0; phase < 2; ++phase) {
			uint64_t hash = native_hash(b,(unsigned[]){4,2,1}[resolution],phase != 0);
			if (hash != c->hashes[phase]) fprintf(stderr,"F %zu res%d shape%d interior%d index%d per%d mode%d phase%u: %016" PRIx64 " != %016" PRIx64 "\n",
			    i,resolution,c->shape,c->interior,c->index,c->perimeter,c->mode,phase,hash,c->hashes[phase]);
			CHECK(hash == c->hashes[phase]);
		}
		st_pixel_buffer_destroy(b);
	}
	if (active) destroy(&f);
}

static void
partitions(void)
{
	const char *wires[] = {"AG#M>4:L>17,23,37,47:M>1:F>42,62:M 2:B","AG#g>1:F>r,R:M 2:B"};
	for (unsigned resolution = 0; resolution < 3; ++resolution) for (unsigned which = 0; which < 2; ++which)
		for (size_t split = 0; split <= strlen(wires[which])+1; ++split) {
			struct fixture f; create(&f,resolution,false); struct responses r; responses(&f,&r);
			struct cterm_igs *igs = f.terminal->igs_engine; igs->random_range[0][1] = igs->random_range[1][1] = 255;
			size_t used, size = strlen(wires[which]);
			if (split > size) for (size_t i = 0; i < size; ++i) CHECK(st_cterm_write(f.terminal,wires[which]+i,1,&used) == 0 && used == 1);
			else {
				CHECK(st_cterm_write(f.terminal,wires[which],split,&used) == 0 && used == split);
				CHECK(st_cterm_write(f.terminal,wires[which]+split,size-split,&used) == 0 && used == size-split);
			}
			CHECK(r.count == 2 && r.bytes[0] == 19 && r.bytes[1] == 17);
			CHECK(igs->attributes.writing_mode == 2 && igs->draw_x == (which ? 0 : 37));
			CHECK(igs->random_seed == (which ? UINT32_C(0x5eb5ca17) : 1));
			struct st_console_cell cells[2]; const struct st_console_rect text = {0,0,2,1};
			CHECK(st_console_read_cells(f.console,&text,cells,2) == 0 && cells[0].glyph == 'A' && cells[1].glyph == 'B');
			destroy(&f);
		}
}

static void
bounds_and_pens(void)
{
	static const unsigned map[16] = {0,15,1,2,4,6,3,5,7,8,9,10,12,14,11,13};
	for (unsigned resolution = 0; resolution < 3; ++resolution) for (unsigned status = 0; status < 2; ++status) {
		struct fixture f; create(&f,resolution,status != 0); f.terminal->response_cb = no_response;
		struct cterm_igs *igs = f.terminal->igs_engine; igs->flow_mode = 0;
		igs->clip_x1 = 40; igs->clip_y1 = 60; igs->clip_x2 = 44; igs->clip_y2 = 64;
		const struct st_console_rect area = {40,60,5,5};
		for (unsigned mode = 1; mode <= 4; ++mode) for (unsigned pen = 0; pen < 16; ++pen) {
			seed(&f,&area,768,768); char wire[96];
			(void)snprintf(wire,sizeof(wire),"G#C>2,%u:A>1,1,1:M>%u:F 42,62:",pen,mode); feed(&f,wire);
			struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f.console,&area,false) == 0);
			unsigned effective = pen > cterm_igs_plane_mask(resolution) ? 1 : pen;
			unsigned ink = mode == 3 ? cterm_igs_plane_mask(resolution) : mode == 4 ? 0 : map[effective]&cterm_igs_plane_mask(resolution);
			for (unsigned i = 0; i < 25; ++i) CHECK(b->primary[i] == 768u+ink && b->alternate[i] == b->primary[i]);
			CHECK(!igs->last_line); st_pixel_buffer_destroy(b);
		}
		cterm_igs_clip_screen(igs); const struct st_console_rect full = {0,0,igs->width,igs->height}; seed(&f,&full,768,768);
		/* Clamp escaped stored bounds to the real canvas before any probe. */
		igs->clip_x1 = igs->clip_y1 = INT16_MIN; igs->clip_x2 = igs->clip_y2 = INT16_MAX;
		feed(&f,"G#C>2,1:M>1:F 42,62:");
		struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f.console,&full,false) == 0);
		unsigned ch = resolution == 2 ? 16 : 8, drawable = igs->height-(status ? ch : 0);
		for (unsigned y = 0; y < igs->height; ++y) for (unsigned x = 0; x < igs->width; ++x)
			CHECK(b->primary[y*igs->width+x] == 768u+(y < drawable ? cterm_igs_plane_mask(resolution) : 0)
			    && b->alternate[y*igs->width+x] == b->primary[y*igs->width+x]);
		st_pixel_buffer_destroy(b); destroy(&f);
	}
}

/* This source-derived comb needs32 active triples (native queue model),
 * exercising growth of the small owned queue without changing run order. */
static void
queue_growth(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution) for (unsigned solid = 0; solid < 2; ++solid) {
		struct fixture f; create(&f,resolution,false); struct cterm_igs *igs = f.terminal->igs_engine;
		igs->flow_mode = 0; f.terminal->response_cb = no_response;
		struct st_pixel_buffer *b = NULL; CHECK(st_pixel_buffer_create(&b,34,5,true) == 0);
		for (unsigned y = 0; y < 5; ++y) for (unsigned x = 0; x < 34; ++x) {
			unsigned value = y == 0 || y == 4 || x == 0 || x == 33 || (y != 2 && (x-1)%2 == 1);
			b->primary[y*34+x] = b->alternate[y*34+x] = 768+value;
		}
		const struct st_pixel_blit blit = {.width = 34, .height = 5, .destination_x = 40,
		    .destination_y = 60, .scale_x = 1, .scale_y = 1};
		CHECK(st_console_blit_pixels(f.console,b,NULL,&blit) == 0); st_pixel_buffer_destroy(b);
		igs->clip_x1 = 40; igs->clip_y1 = 60; igs->clip_x2 = 73; igs->clip_y2 = 64;
		feed(&f,solid ? "G#A>1,1,1:M>1:F 57,62:" : "G#A>0,1,1:M>2:F 57,62:");
		const struct st_console_rect area = {40,60,34,5}; CHECK(st_console_get_pixels(&b,f.console,&area,false) == 0);
		for (unsigned y = 0; y < 5; ++y) for (unsigned x = 0; x < 34; ++x) {
			bool boundary = y == 0 || y == 4 || x == 0 || x == 33 || (y != 2 && (x-1)%2 == 1);
			CHECK(b->primary[y*34+x] == 768u+(boundary ? 1 : solid ? cterm_igs_plane_mask(resolution) : 0)
			    && b->alternate[y*34+x] == b->primary[y*34+x]);
		}
		CHECK(!igs->last_line); st_pixel_buffer_destroy(b); destroy(&f);
	}
}

static void
state_lifetime(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f,resolution,false); f.terminal->response_cb = no_response;
		struct cterm_igs *igs = f.terminal->igs_engine; igs->flow_mode = 0;
		feed(&f,"G#M>4:L>17,23,37,47:I>2:F 40,60:");
		CHECK(igs->draw_start_x == 17 && igs->draw_y == 47 && !igs->last_line);
		const char *wire = resolution == 0 ? "G#R>1,0:F 40,60:" : "G#R>0,0:F 40,60:";
		size_t used, prefix = strlen("G#R>1,0:");
		CHECK(st_cterm_write(f.terminal,wire,strlen(wire),&used) == 0);
		if (resolution == 2) CHECK(used == strlen(wire));
		else { CHECK(used == prefix); local_ms += 17; CHECK(st_cterm_poll(f.terminal) == 0); feed(&f,wire+prefix); }
		CHECK(igs->draw_x == 37 && igs->scaling_resolution == (int)resolution);
		feed(&f,"G#I 5:"); if (cterm_igs_paused(igs)) { local_ms += 17; CHECK(st_cterm_poll(f.terminal) == 0); }
		CHECK(igs->resolution == resolution && igs->draw_y == 47);
		igs->clip_x1 = igs->clip_x2 = 40; igs->clip_y1 = igs->clip_y2 = 60;
		igs->math[3] = igs->math[6] = 40; igs->math[4] = igs->math[8] = 60;
		const struct st_console_rect point = {40,60,1,1}; seed(&f,&point,768,768);
		feed(&f,"G#g>1:F 100],200]:"); struct st_pixel_buffer *b = NULL;
		CHECK(st_console_get_pixels(&b,f.console,&point,false) == 0);
		CHECK(b->primary[0] == 768u+cterm_igs_plane_mask(resolution) && b->alternate[0] == b->primary[0]);
		CHECK(igs->draw_x == 37 && !igs->last_line); st_pixel_buffer_destroy(b);
		igs->clip_y1 = igs->clip_y2 = resolution == 2 ? 120 : 60;
		const struct st_console_rect relative = {40,(unsigned)igs->clip_y1,1,1}; seed(&f,&relative,768,768);
		feed(&f,"G#g>2:F 40,60:"); CHECK(st_console_get_pixels(&b,f.console,&relative,false) == 0);
		CHECK(b->primary[0] == 768u+cterm_igs_plane_mask(resolution)); st_pixel_buffer_destroy(b);
		/* An out-of-bounds seed has no setup side effects, even with invalid
		 * unreachable fill attributes. Negative coordinates come from words. */
		igs->attributes.fill_interior = -1; igs->math[0] = INT16_MIN; igs->last_line = true;
		feed(&f,"G#F 0],0]:"); CHECK(igs->last_line && igs->draw_x == 37);
		igs->attributes.fill_interior = 1; igs->clip_y1 = igs->clip_y2 = 60;
		/* Foreign host pixels have no IGS physical index; each phase chooses
		 * its own eligibility rather than interpreting an RGB tag as planes. */
		seed(&f,&point,ST_PIXEL_COLOR_RGB_VALUE(1,2,3),768); feed(&f,"G#g>0:F 40,60:");
		CHECK(st_console_get_pixels(&b,f.console,&point,false) == 0);
		CHECK(b->primary[0] == ST_PIXEL_COLOR_RGB_VALUE(1,2,3) && b->alternate[0] == 768u+cterm_igs_plane_mask(resolution));
		st_pixel_buffer_destroy(b); destroy(&f);
	}
}

int
st_cterm_igs_flood_test_main(void)
{
	literal(); flow_and_recovery(); partitions(); bounds_and_pens(); queue_growth(); state_lifetime(); vectors();
	return 0;
}
