/* f: counted polygons, native scan conversion and serial ownership. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "igs_fill_test_helpers.h"

static void
literal(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f,resolution,false);
		feed(&f,"G#A>1,1,0:f 3,10,10,15,14,20,10:");
		const struct st_console_rect area = {10,10,11,5}; struct st_pixel_buffer *b = NULL;
		CHECK(st_console_get_pixels(&b,f.console,&area,false) == 0);
		for (unsigned y = 0; y < 5; ++y) for (unsigned x = 0; x < 11; ++x) {
			bool ink = (y == 1 && x >= 1 && x <= 9) || (y == 2 && x >= 3 && x <= 8)
			    || (y == 3 && x >= 4 && x <= 6);
			CHECK(b->primary[y*11+x] == 768u+(ink ? cterm_igs_plane_mask(resolution) : 0));
			CHECK(b->alternate[y*11+x] == b->primary[y*11+x]);
		}
		st_pixel_buffer_destroy(b);
		/* Native offset rounding depends on edge orientation. */
		feed(&f,"G#f 3,40,10,50,10,45,14:");
		const struct st_console_rect reversed = {40,12,11,1};
		CHECK(st_console_get_pixels(&b,f.console,&reversed,false) == 0);
		for (unsigned x = 0; x < 11; ++x)
			CHECK(b->primary[x] == 768u+(x >= 2 && x <= 7 ? cterm_igs_plane_mask(resolution) : 0));
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
arity(void)
{
	static const int counts[] = {INT16_MIN,-1,0,1,2,3,127,128,129,9999,INT16_MAX};
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f,resolution,false); struct responses r; responses(&f,&r);
		struct cterm_igs *igs = f.terminal->igs_engine;
		for (unsigned g = 0; g < 3; ++g) for (size_t k = 0; k < sizeof(counts)/sizeof(*counts); ++k) {
			igs->math[127] = (int16_t)counts[k];
			char wire[2048]; size_t length = (size_t)snprintf(wire,sizeof(wire),"G#g>%u:M>4:f>127]",g);
			unsigned count = counts[k] < 1 ? 1 : counts[k] > 128 ? 128 : (unsigned)counts[k];
			for (unsigned i = 0; i < count; ++i) length += (size_t)snprintf(wire+length,sizeof(wire)-length,",1,2");
			CHECK(length+6 < sizeof(wire)); strcpy(wire+length,":M 2:");
			r.count = 0; feed(&f,wire); CHECK(igs->attributes.writing_mode == 2 && r.count == 2);
		}
		/* Count129 consumes exactly128 pairs; the discarded pair is ordinary text. */
		char wire[2048]; size_t length = (size_t)snprintf(wire,sizeof(wire),"G#g>0:M>1:f 129");
		for (unsigned i = 0; i < 128; ++i) length += (size_t)snprintf(wire+length,sizeof(wire)-length,",%u,60",20+i);
		strcpy(wire+length,",777,888:END"); feed(&f,wire);
		const struct st_console_rect area = {18,59,132,3}; struct st_pixel_buffer *b = NULL;
		CHECK(st_console_get_pixels(&b,f.console,&area,false) == 0);
		for (unsigned y = 0; y < 3; ++y) for (unsigned x = 0; x < 132; ++x)
			CHECK(b->primary[y*132+x] == 768u+(y == 1 && x >= 2 && x <= 129 ? cterm_igs_plane_mask(resolution) : 0));
		st_pixel_buffer_destroy(b);
		struct st_console_cell cells[11]; const struct st_console_rect text = {0,0,11,1};
		CHECK(st_console_read_cells(f.console,&text,cells,11) == 0);
		for (unsigned i = 0; i < 11; ++i) CHECK(cells[i].glyph == (unsigned char)"777,888:END "[i]);
		destroy(&f);
	}
}

static void
input(void)
{
	const char *chains[] = {"AG#M>4:L>17,23,37,47:M>3:f>3,40,60,80,60,80,80:D 37,47:B",
	    "AG#M>4:f>r,r,R,r,R:M 2:B"};
	for (unsigned resolution = 0; resolution < 3; ++resolution)
		for (unsigned chain = 0; chain < 2; ++chain) for (size_t split = 0; split <= strlen(chains[chain])+1; ++split) {
			struct fixture f; create(&f,resolution,false); struct responses r; responses(&f,&r);
			struct cterm_igs *igs = f.terminal->igs_engine; size_t used, size = strlen(chains[chain]);
			if (chain == 1) { igs->random_range[0][0] = igs->random_range[0][1] = 2; igs->random_range[1][0] = igs->random_range[1][1] = 2; }
			if (split > size) {
				for (size_t i = 0; i < size; ++i) CHECK(st_cterm_write(f.terminal,chains[chain]+i,1,&used) == 0 && used == 1);
			} else {
				CHECK(st_cterm_write(f.terminal,chains[chain],split,&used) == 0 && used == split);
				CHECK(st_cterm_write(f.terminal,chains[chain]+split,size-split,&used) == 0 && used == size-split);
			}
			CHECK(r.count == 2 && r.bytes[0] == 19 && r.bytes[1] == 17);
			CHECK(igs->draw_start_x == (chain ? 0 : 37) && igs->draw_start_y == (chain ? 0 : 47));
			CHECK(igs->draw_x == (chain ? 0 : 37) && igs->draw_y == (chain ? 0 : 47));
			if (chain) CHECK(igs->attributes.writing_mode == 2 && igs->random_seed == UINT32_C(0x151d0217));
			struct st_console_cell cells[2]; const struct st_console_rect text = {0,0,2,1};
			CHECK(st_console_read_cells(f.console,&text,cells,2) == 0 && cells[0].glyph == 'A' && cells[1].glyph == 'B');
			destroy(&f);
		}
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
	const char *partial[] = {"G#f ","G#f 2,","G#f 2,40,60,80,","G#f 128,40,60,"};
	for (unsigned i = 0; i < 4; ++i) {
		feed(&f,partial[i]); CHECK(r.count == 0); CHECK(st_cterm_finish(f.terminal) == 0);
		feed(&f,partial[i]); local_ms += 5001; CHECK(st_cterm_poll(f.terminal) == 0); CHECK(r.count == 0);
	}
	igs->random_range[0][0] = igs->random_range[0][1] = 2;
	feed(&f,"G#f r,40,"); CHECK(igs->random_seed == 1 && r.count == 0);
	CHECK(st_cterm_finish(f.terminal) == 0 && igs->random_seed == 1);
	feed(&f,"G#f r,40,"); local_ms += 5001; CHECK(st_cterm_poll(f.terminal) == 0);
	CHECK(igs->random_seed == 1 && r.count == 0);
	size_t used; const char bad[] = "G#f 2,40,60,80,10000:";
	CHECK(st_cterm_write(f.terminal,bad,sizeof(bad)-1,&used) == 0 && r.count == 0);
	const struct st_console_rect untouched = {0,0,320,200}; struct st_pixel_buffer *blank = NULL;
	CHECK(st_console_get_pixels(&blank,f.console,&untouched,false) == 0);
	for (size_t i = 0; i < 320u*200; ++i) CHECK(blank->primary[i] == 768 && blank->alternate[i] == 768);
	st_pixel_buffer_destroy(blank);
	igs->flow_mode = 0; feed(&f,"G#f 2,40,60,80,60:"); CHECK(r.count == 0);
	igs->flow_mode = -1; igs->loop_mode = 1; feed(&f,"G#f 2,40,60,80,60:"); CHECK(r.count == 0);
	igs->loop_mode = 2; igs->flow_off_count = -1; igs->flow_on_count = 0;
	feed(&f,"G#f 2,40,60,80,60:"); CHECK(r.count == 0);
	igs->loop_mode = 0; igs->flow_off_count = 2; igs->flow_on_count = 3; igs->flow_off = 0xfe; igs->flow_on = 0x81;
	feed(&f,"G#f 2,40,60,80,60:"); CHECK(r.count == 5 && memcmp(r.bytes,"\xfe\xfe\x81\x81\x81",5) == 0);
	r.count = 0; r.action = 1; igs->flow_off_count = 1; igs->flow_off = 19;
	feed(&f,"G#f 2,40,60,80,60:"); CHECK(r.count == 3 && memcmp(r.bytes,"\x13\xab\xab",3) == 0);
	r.count = 0; r.action = 2; igs->flow_mode = 1; igs->flow_off_count = 1; igs->flow_off = 19;
	feed(&f,"G#f 2,40,60,80,60:"); CHECK(r.count == 1 && r.bytes[0] == 19);
	r.count = 0; r.action = 3; igs->loop_mode = 0;
	feed(&f,"G#f 2,40,60,80,60:G#M 4:"); CHECK(r.count == 1 && igs->attributes.writing_mode == 4);
	CHECK(igs->flow_mode == 1 && igs->flow_on == 17 && igs->flow_off == 19 && igs->loop_mode == 0);
	r.count = 0; r.action = 0;
	igs->attributes.fill_interior = -1;
	const char rejected[] = "G#f 2,40,60,80,60:";
	CHECK(st_cterm_write(f.terminal,rejected,sizeof(rejected)-1,&used) == 0);
	CHECK(r.count == 2 && r.bytes[0] == 19 && r.bytes[1] == 17); igs->attributes.fill_interior = 1;
	r.count = 0;
	feed(&f,"G#f 2,40,"); CHECK(st_cterm_reset_terminal(f.terminal) == 0); CHECK(r.count == 0);
	feed(&f,"G#f 2,40,60,80,60:"); CHECK(r.count == 2);
	const struct st_console_rect area = {40,60,41,1}; seed(&f,&area,768,768);
	r.count = 0; r.action = 4; feed(&f,"G#M>1:f 2,40,60,80,60:"); CHECK(r.count == 2);
	bool last = igs->last_line;
	CHECK(cterm_igs_polygon(igs,NULL,2) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(cterm_igs_polygon(igs,(int[]){0,0},129) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(cterm_igs_polygon(igs,(int[]){0,0},0) == IO_ERROR_INVALID_ARGUMENT && igs->last_line == last);
	unsigned total = 0; f.terminal->response_cb = count_response; f.terminal->response_cbdata = &total;
	igs->flow_on_count = igs->flow_off_count = INT16_MAX;
	feed(&f,"G#f 2,40,60,80,60:"); CHECK(total == 65534);
	destroy(&f);
}

struct polygon_shape { unsigned count; int coordinates[2*CTERM_IGS_MAX_POINTS]; };
struct polygon_case {
	int resolution, mode, interior, index, perimeter, clipping, expected_last, shape, bounds[4];
	uint64_t hashes[2];
};
#include "data/igs_polygon_vectors.h"
_Static_assert(sizeof(polygon_cases)/sizeof(*polygon_cases) == 4056, "complete native polygon matrix");

static void
vectors(void)
{
	struct fixture f; bool active = false; int resolution = -1; struct responses r;
	for (size_t i = 0; i < sizeof(polygon_cases)/sizeof(*polygon_cases); ++i) {
		const struct polygon_case *c = &polygon_cases[i];
		const struct polygon_shape *shape = &polygon_shapes[c->shape];
		if (resolution != c->resolution) {
			if (active) destroy(&f);
			create(&f,(unsigned)c->resolution,false); active = true; resolution = c->resolution;
			responses(&f,&r);
		}
		struct cterm_igs *igs = f.terminal->igs_engine; seed_words(&f);
		igs->draw_start_x = 17; igs->draw_start_y = 23; igs->draw_x = 37; igs->draw_y = 47;
		igs->attributes.fill_pen = 7; igs->attributes.line_pen = 10;
		igs->attributes.line_type = 0; igs->attributes.line_width = 2;
		igs->attributes.line_begin = 1; igs->attributes.line_end = 2; igs->attributes.user_line = 0xa531;
		memcpy(igs->patterns[3],user_rows,sizeof(user_rows));
		char wire[4096]; (void)snprintf(wire,sizeof(wire),"G#A>%d,%d,%d:M %d:",c->interior,c->interior == 4 ? 3 : c->index,c->perimeter,c->mode); feed(&f,wire);
		if (c->interior == 4) for (unsigned j = 0; j < 16; ++j) igs->patterns[3][j] ^= 0xffff;
		igs->clipping = c->clipping != 0; igs->last_line = i%2 != 0;
		igs->clip_x1 = c->bounds[0]; igs->clip_y1 = c->bounds[1]; igs->clip_x2 = c->bounds[2]; igs->clip_y2 = c->bounds[3];
		size_t length = (size_t)snprintf(wire,sizeof(wire),"G#f %u",shape->count);
		for (unsigned j = 0; j < shape->count*2; ++j) {
			int value = shape->coordinates[j];
			if (value < 0) {
				CHECK(j < 128); igs->math[j] = (int16_t)value;
				length += (size_t)snprintf(wire+length,sizeof(wire)-length,",%u]",j);
			} else length += (size_t)snprintf(wire+length,sizeof(wire)-length,",%d",value);
		}
		CHECK(length+2 < sizeof(wire)); strcpy(wire+length,":");
		struct cterm_igs_attributes before = igs->attributes;
		uint32_t palette[16]; memcpy(palette,igs->palette,sizeof(palette));
		int16_t math[128]; memcpy(math,igs->math,sizeof(math));
		uint16_t patterns[8][16]; memcpy(patterns,igs->patterns,sizeof(patterns));
		r.count = 0; feed(&f,wire);
		CHECK(r.count == 2 && r.bytes[0] == 19 && r.bytes[1] == 17);
		CHECK(memcmp(math,igs->math,sizeof(math)) == 0 && memcmp(patterns,igs->patterns,sizeof(patterns)) == 0);
		CHECK(igs->draw_start_x == 17 && igs->draw_start_y == 23 && igs->draw_x == 37 && igs->draw_y == 47);
		CHECK(memcmp(&before,&igs->attributes,sizeof(before)) == 0 && memcmp(palette,igs->palette,sizeof(palette)) == 0);
		CHECK(igs->last_line == (c->expected_last != 0));
		CHECK(igs->clipping == (c->clipping != 0) && igs->clip_x1 == c->bounds[0] && igs->clip_y2 == c->bounds[3]);
		CHECK(igs->random_seed == 1);
		const struct st_console_rect full = {0,0,igs->width,igs->height}; struct st_pixel_buffer *b = NULL;
		CHECK(st_console_get_pixels(&b,f.console,&full,false) == 0);
		for (unsigned phase = 0; phase < 2; ++phase) {
			uint64_t hash = native_hash(b,(unsigned[]){4,2,1}[resolution],phase != 0);
			if (hash != c->hashes[phase]) fprintf(stderr,"f %zu res%d shape%d interior%d index%d per%d mode%d phase%u: %016" PRIx64 " != %016" PRIx64 "\n",
			    i,resolution,c->shape,c->interior,c->index,c->perimeter,c->mode,phase,hash,c->hashes[phase]);
			CHECK(hash == c->hashes[phase]);
		}
		st_pixel_buffer_destroy(b);
	}
	if (active) destroy(&f);
}

static void
pens_and_boundaries(void)
{
	static const unsigned map[16] = {0,15,1,2,4,6,3,5,7,8,9,10,12,14,11,13};
	for (unsigned resolution = 0; resolution < 3; ++resolution) for (unsigned status = 0; status < 2; ++status) {
		struct fixture f; create(&f,resolution,status != 0); f.terminal->response_cb = no_response;
		struct cterm_igs *igs = f.terminal->igs_engine; igs->flow_mode = 0;
		feed(&f,"G#A 1,1,0:"); const struct st_console_rect point = {50,70,1,1};
		for (unsigned mode = 1; mode <= 4; ++mode) for (unsigned pen = 0; pen < 16; ++pen) {
			seed(&f,&point,768,768); char wire[128];
			(void)snprintf(wire,sizeof(wire),"G#C>2,%u:M>%u:f 4,40,60,80,60,80,80,40,80:",pen,mode); feed(&f,wire);
			struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f.console,&point,false) == 0);
			unsigned effective = pen > cterm_igs_plane_mask(resolution) ? 1 : pen;
			unsigned ink = mode == 3 ? cterm_igs_plane_mask(resolution) : mode == 4 ? 0 : map[effective]&cterm_igs_plane_mask(resolution);
			CHECK(b->primary[0] == 768u+ink && b->alternate[0] == 768u+ink); st_pixel_buffer_destroy(b);
		}
		/* Safe wide intersections replace original signed-word overflows. */
		igs->math[0] = INT16_MIN; igs->math[1] = INT16_MAX;
		const struct st_console_rect full = {0,0,igs->width,igs->height}; seed(&f,&full,768,768);
		feed(&f,"G#C>2,1:M>1:f 4,0],0],1],0],1],1],0],1]:");
		struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f.console,&full,false) == 0);
		unsigned ch = resolution == 2 ? 16 : 8, drawable = igs->height-(status ? ch : 0);
		for (unsigned y = 0; y < igs->height; ++y) for (unsigned x = 0; x < igs->width; ++x)
			CHECK(b->primary[y*igs->width+x] == 768u+(y < drawable ? cterm_igs_plane_mask(resolution) : 0)
			    && b->alternate[y*igs->width+x] == b->primary[y*igs->width+x]);
		st_pixel_buffer_destroy(b);
		seed(&f,&full,768,768); feed(&f,"G#f 3,0],0],1],1],0],1]:");
		CHECK(st_console_get_pixels(&b,f.console,&full,false) == 0);
		for (unsigned y = 0; y < igs->height; ++y) for (unsigned x = 0; x < igs->width; ++x)
			CHECK(b->primary[y*igs->width+x] == 768u+(y < drawable && x <= y ? cterm_igs_plane_mask(resolution) : 0)
			    && b->alternate[y*igs->width+x] == b->primary[y*igs->width+x]);
		st_pixel_buffer_destroy(b);

		/* A single normalized bottom vertex is retained; status stays protected. */
		seed(&f,&full,768,768); feed(&f,"G#g>1:A>1,1,1:f 0,9999,9999:");
		CHECK(st_console_get_pixels(&b,f.console,&full,false) == 0);
		for (unsigned y = 0; y < igs->height; ++y) for (unsigned x = 0; x < igs->width; ++x)
			CHECK(b->primary[y*igs->width+x] == 768u+(!status && y+1 == igs->height && x+1 == igs->width ? cterm_igs_plane_mask(resolution) : 0));
		st_pixel_buffer_destroy(b);
		/* Resolution-relative X/Y scaling also applies to each counted pair. */
		feed(&f,"G#g>2:f 1,40,60:");
		const struct st_console_rect scaled = {40,resolution == 2 ? 120u : 60u,1,1};
		CHECK(st_console_get_pixels(&b,f.console,&scaled,false) == 0 && b->primary[0] == 768u+cterm_igs_plane_mask(resolution));
		st_pixel_buffer_destroy(b); destroy(&f);
	}
}

static void
state_lifetime(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f,resolution,false); f.terminal->response_cb = no_response;
		struct cterm_igs *igs = f.terminal->igs_engine; igs->flow_mode = 0;
		feed(&f,"G#M>4:L>17,23,37,47:I>2:f 1,40,60:");
		CHECK(igs->draw_start_x == 17 && igs->draw_y == 47);
		const char *wire = resolution == 0 ? "G#R>1,0:f 1,40,60:" : "G#R>0,0:f 1,40,60:";
		size_t used, prefix = strlen("G#R>1,0:");
		CHECK(st_cterm_write(f.terminal,wire,strlen(wire),&used) == 0);
		if (resolution == 2) CHECK(used == strlen(wire));
		else { CHECK(used == prefix); local_ms += 17; CHECK(st_cterm_poll(f.terminal) == 0); feed(&f,wire+prefix); }
		CHECK(igs->draw_x == 37 && igs->scaling_resolution == (int)resolution);
		feed(&f,"G#I 5:"); if (cterm_igs_paused(igs)) { local_ms += 17; CHECK(st_cterm_poll(f.terminal) == 0); }
		CHECK(igs->resolution == resolution && igs->draw_y == 47);
		/* Transform the index by its axis before lookup, not the stored value;
		 * the count index remains unscaled. Both depths read the same point. */
		igs->math[127] = 1; igs->math[3] = igs->math[6] = 40; igs->math[4] = igs->math[8] = 60;
		const struct st_console_rect point = {40,60,1,1}; seed(&f,&point,768,768);
		feed(&f,"G#g>1:f 127],100],200]:"); struct st_pixel_buffer *b = NULL;
		CHECK(st_console_get_pixels(&b,f.console,&point,false) == 0);
		CHECK(b->primary[0] == 768u+cterm_igs_plane_mask(resolution) && b->alternate[0] == b->primary[0]);
		CHECK(igs->math[127] == 1 && igs->draw_x == 37); st_pixel_buffer_destroy(b); destroy(&f);
	}
}

int
st_cterm_igs_polygon_test_main(void)
{
	literal(); arity(); input(); flow_and_recovery(); pens_and_boundaries(); state_lifetime(); vectors();
	return 0;
}
