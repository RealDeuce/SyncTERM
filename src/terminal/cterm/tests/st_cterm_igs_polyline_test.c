/* z: independent source-derived multisegment pixels and literal serial state.
 * References 15/20/43/44/46/50/51/137; no external runtime assets. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "igs_pixel_test_helpers.h"

struct polyline_case {
	int resolution, style, width, begin, end, mode, clipping, initial_last, count;
	int coordinates[10], bounds[4];
	uint64_t hashes[2];
};
#include "data/igs_polyline_vectors.h"
_Static_assert(sizeof(polyline_cases)/sizeof(*polyline_cases) == 1692, "complete polyline matrix");

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
vectors(void)
{
	static const int ends[3][3] = {{0,52,62},{51,50,53},{61,54,60}};
	struct fixture f; bool active = false; int resolution = -1;
	struct responses r;
	for (size_t i = 0; i < sizeof(polyline_cases)/sizeof(*polyline_cases); ++i) {
		const struct polyline_case *c = &polyline_cases[i];
		if (resolution != c->resolution) {
			if (active) destroy(&f);
			create(&f,(unsigned)c->resolution,false); active = true; resolution = c->resolution;
			responses(&f,&r);
		}
		struct cterm_igs *igs = f.terminal->igs_engine;
		const struct st_console_rect full = {0,0,igs->width,igs->height};
		seed(&f,&full,768,768+cterm_igs_plane_mask((unsigned)resolution));
		igs->attributes.line_pen = 10; igs->patterns[6][0] = 0xa531;
		igs->draw_start_x = 17; igs->draw_start_y = 23; igs->draw_x = 37; igs->draw_y = 47;
		igs->last_line = c->initial_last != 0;
		igs->clipping = c->clipping != 0;
		igs->clip_x1 = c->bounds[0]; igs->clip_y1 = c->bounds[1];
		igs->clip_x2 = c->bounds[2]; igs->clip_y2 = c->bounds[3];
		char wire[256];
		(void)snprintf(wire,sizeof(wire),"G#T>2,%d,%d:T>2,%d,%d:M %d:",c->style,c->width,
		    c->style,ends[c->begin][c->end],c->mode); feed(&f,wire);
		struct cterm_igs_attributes before = igs->attributes;
		for (int j = 0; j < 2*c->count; ++j) igs->math[j] = (int16_t)c->coordinates[j];
		size_t length = (size_t)snprintf(wire,sizeof(wire),"G#z %d",c->count);
		for (int j = 0; j < 2*c->count; ++j)
			length += (size_t)snprintf(wire+length,sizeof(wire)-length,",%d]",j);
		CHECK(length+2 <= sizeof(wire)); strcpy(wire+length,":");
		r.count = 0; feed(&f,wire);
		CHECK(r.count == 2 && r.bytes[0] == 19 && r.bytes[1] == 17);
		CHECK(igs->draw_start_x == 17 && igs->draw_start_y == 23 && igs->draw_x == 37 && igs->draw_y == 47);
		CHECK(memcmp(&before,&igs->attributes,sizeof(before)) == 0);
		for (int j = 0; j < 2*c->count; ++j) CHECK(igs->math[j] == c->coordinates[j]);
		CHECK(igs->patterns[6][0] == 0xa531);
		CHECK(igs->clipping == (c->clipping != 0) && igs->clip_x1 == c->bounds[0] && igs->clip_y2 == c->bounds[3]);
		struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f.console,&full,false) == 0);
		for (unsigned phase = 0; phase < 2; ++phase) {
			uint64_t hash = native_hash(b,(unsigned[]){4,2,1}[resolution],phase != 0);
			if (hash != c->hashes[phase]) fprintf(stderr,"polyline %zu res%d type%d width%d caps%d/%d mode%d phase%u: %016" PRIx64 " != %016" PRIx64 "\n",
			    i,resolution,c->style,c->width,c->begin,c->end,c->mode,phase,hash,c->hashes[phase]);
			CHECK(hash == c->hashes[phase]);
		}
		st_pixel_buffer_destroy(b);
	}
	if (active) destroy(&f);
}

static void
literal(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution)
		for (unsigned status = 0; status < 2; ++status) {
			struct fixture f; create(&f,resolution,status != 0); struct responses r; responses(&f,&r);
			struct cterm_igs *igs = f.terminal->igs_engine;
			const struct st_console_rect area = {38,58,45,25};
			for (unsigned mode = 1; mode <= 4; ++mode) {
				seed(&f,&area,768,768);
				char wire[128]; (void)snprintf(wire,sizeof(wire),"G#M>%u:z 3,40,60,80,60,80,80:",mode);
				feed(&f,wire);
				struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f.console,&area,false) == 0);
				for (unsigned y = 0; y < area.height; ++y) for (unsigned x = 0; x < area.width; ++x) {
					bool ink = mode != 4 && ((y == 2 && x >= 2 && x <= 42) || (x == 42 && y >= 2 && y <= 22));
					CHECK(b->primary[y*area.width+x] == 768u+(ink ? cterm_igs_plane_mask(resolution) : 0));
				}
				st_pixel_buffer_destroy(b);
			}
			CHECK(igs->draw_x == 0 && igs->draw_y == 0 && igs->last_line);
			unsigned ch = resolution == 2 ? 16 : 8;
			const struct st_console_rect bottom = {0,igs->height-ch,igs->width,ch}; seed(&f,&bottom,768,768);
			char wire[128]; (void)snprintf(wire,sizeof(wire),"G#g>1:M>1:z 3,0,9999,5000,9999,9999,9999:"); feed(&f,wire);
			struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f.console,&bottom,false) == 0);
			for (unsigned y = 0; y < ch; ++y) for (unsigned x = 0; x < igs->width; ++x)
				CHECK(b->primary[y*igs->width+x] == 768u+(y == ch-1 && !status ? cterm_igs_plane_mask(resolution) : 0));
			st_pixel_buffer_destroy(b); destroy(&f);
		}
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
			char wire[2048]; size_t length = (size_t)snprintf(wire,sizeof(wire),"G#g>%u:M>4:z>127]",g);
			unsigned count = counts[k] < 2 ? 2 : counts[k] > 128 ? 128 : (unsigned)counts[k];
			for (unsigned i = 0; i < count; ++i) length += (size_t)snprintf(wire+length,sizeof(wire)-length,",1,2");
			CHECK(length+6 < sizeof(wire)); strcpy(wire+length,":M 2:");
			r.count = 0; feed(&f,wire); CHECK(igs->attributes.writing_mode == 2 && r.count == 2);
		}
		/* Count129 consumes exactly128 pairs; the discarded pair is ordinary text. */
		char wire[2048]; size_t length = (size_t)snprintf(wire,sizeof(wire),"G#g>0:M>1:z 129");
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
	const char *chains[] = {"AG#M>4:L>17,23,37,47:M>3:z>3,40,60,80,60,80,80:D 37,47:B",
	    "AG#M>4:z>r,r,R,r,R:M 2:B"};
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
	const char *partial[] = {"G#z ","G#z 2,","G#z 2,40,60,80,","G#z 128,40,60,"};
	for (unsigned i = 0; i < 4; ++i) {
		feed(&f,partial[i]); CHECK(r.count == 0); CHECK(st_cterm_finish(f.terminal) == 0);
		feed(&f,partial[i]); local_ms += 5001; CHECK(st_cterm_poll(f.terminal) == 0); CHECK(r.count == 0);
	}
	igs->random_range[0][0] = igs->random_range[0][1] = 2;
	feed(&f,"G#z r,40,"); CHECK(igs->random_seed == 1 && r.count == 0);
	CHECK(st_cterm_finish(f.terminal) == 0 && igs->random_seed == 1);
	feed(&f,"G#z r,40,"); local_ms += 5001; CHECK(st_cterm_poll(f.terminal) == 0);
	CHECK(igs->random_seed == 1 && r.count == 0);
	size_t used; const char bad[] = "G#z 2,40,60,80,10000:";
	CHECK(st_cterm_write(f.terminal,bad,sizeof(bad)-1,&used) == 0 && r.count == 0);
	const struct st_console_rect untouched = {0,0,320,200}; struct st_pixel_buffer *blank = NULL;
	CHECK(st_console_get_pixels(&blank,f.console,&untouched,false) == 0);
	for (size_t i = 0; i < 320u*200; ++i) CHECK(blank->primary[i] == 768 && blank->alternate[i] == 768);
	st_pixel_buffer_destroy(blank);
	igs->flow_mode = 0; feed(&f,"G#z 2,40,60,80,60:"); CHECK(r.count == 0);
	igs->flow_mode = -1; igs->loop_mode = 1; feed(&f,"G#z 2,40,60,80,60:"); CHECK(r.count == 0);
	igs->loop_mode = 2; igs->flow_off_count = -1; igs->flow_on_count = 0;
	feed(&f,"G#z 2,40,60,80,60:"); CHECK(r.count == 0);
	igs->loop_mode = 0; igs->flow_off_count = 2; igs->flow_on_count = 3; igs->flow_off = 0xfe; igs->flow_on = 0x81;
	feed(&f,"G#z 2,40,60,80,60:"); CHECK(r.count == 5 && memcmp(r.bytes,"\xfe\xfe\x81\x81\x81",5) == 0);
	r.count = 0; r.action = 1; igs->flow_off_count = 1; igs->flow_off = 19;
	feed(&f,"G#z 2,40,60,80,60:"); CHECK(r.count == 3 && memcmp(r.bytes,"\x13\xab\xab",3) == 0);
	r.count = 0; r.action = 2; igs->flow_mode = 1; igs->flow_off_count = 1; igs->flow_off = 19;
	feed(&f,"G#z 2,40,60,80,60:"); CHECK(r.count == 1 && r.bytes[0] == 19);
	r.count = 0; r.action = 3; igs->loop_mode = 0;
	feed(&f,"G#z 2,40,60,80,60:G#M 4:"); CHECK(r.count == 1 && igs->attributes.writing_mode == 4);
	CHECK(igs->flow_mode == 1 && igs->flow_on == 17 && igs->flow_off == 19 && igs->loop_mode == 0);
	r.count = 0; r.action = 0;
	igs->attributes.line_width = 2;
	const char rejected[] = "G#z 2,40,60,80,60:";
	CHECK(st_cterm_write(f.terminal,rejected,sizeof(rejected)-1,&used) == 0);
	CHECK(r.count == 2 && r.bytes[0] == 19 && r.bytes[1] == 17); igs->attributes.line_width = 1;
	r.count = 0;
	feed(&f,"G#z 2,40,"); CHECK(st_cterm_reset_terminal(f.terminal) == 0); CHECK(r.count == 0);
	feed(&f,"G#z 2,40,60,80,60:"); CHECK(r.count == 2);
	const struct st_console_rect area = {40,60,41,1}; seed(&f,&area,768,768);
	r.count = 0; r.action = 4; feed(&f,"G#M>1:z 2,40,60,80,60:"); CHECK(r.count == 2);
	bool last = igs->last_line;
	CHECK(cterm_igs_polyline(igs,NULL,2) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(cterm_igs_polyline(igs,(int[]){0,0},1) == IO_ERROR_INVALID_ARGUMENT && igs->last_line == last);
	unsigned total = 0; f.terminal->response_cb = count_response; f.terminal->response_cbdata = &total;
	igs->flow_on_count = igs->flow_off_count = INT16_MAX;
	feed(&f,"G#z 2,40,60,80,60:"); CHECK(total == 65534);
	destroy(&f);
}

static void
last_segment_state(void)
{
	struct fixture f; create(&f,0,false);
	struct cterm_igs *igs = f.terminal->igs_engine;
	igs->clip_x2 = igs->clip_y2 = 100;
	igs->last_line = false;
	feed(&f,"G#M>3:z 4,10,20,20,20,200,200,210,210:");
	CHECK(igs->last_line); /* Marked final before the last clip rejection. */
	igs->last_line = false;
	feed(&f,"G#T>2,1,3:z 3,40,60,40,60,40,60:");
	CHECK(!igs->last_line); /* No shaft or terminal circle on zero segments. */
	CHECK(igs->draw_start_x == 0 && igs->draw_x == 0);
	destroy(&f);
}

static void
scaling_and_lifetime(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f,resolution,false); struct responses r; responses(&f,&r);
		struct cterm_igs *igs = f.terminal->igs_engine;
		feed(&f,"G#M>4:L>17,23,37,47:g>2:M>1:z 3,40,60,80,60,80,80:");
		unsigned sy = resolution == 2 ? 2 : 1;
		const struct st_console_rect area = {39,60*sy-1,43,20*sy+3}; struct st_pixel_buffer *b = NULL;
		CHECK(st_console_get_pixels(&b,f.console,&area,false) == 0);
		for (unsigned y = 0; y < area.height; ++y) for (unsigned x = 0; x < area.width; ++x) {
			bool ink = (y == 1 && x >= 1 && x <= 41) || (x == 41 && y >= 1 && y <= 20*sy+1);
			CHECK(b->primary[y*area.width+x] == 768u+(ink ? cterm_igs_plane_mask(resolution) : 0));
		}
		st_pixel_buffer_destroy(b);
		feed(&f,"G#I 2:"); CHECK(igs->draw_start_x == 17 && igs->draw_x == 37);
		const char *wire = resolution == 0 ? "G#R>1,0:z 2,40,60,80,60:" : "G#R>0,0:z 2,40,60,80,60:";
		size_t used, prefix = strlen("G#R>1,0:"); r.count = 0;
		CHECK(st_cterm_write(f.terminal,wire,strlen(wire),&used) == 0);
		if (resolution == 2) CHECK(used == strlen(wire) && r.count == 2);
		else {
			CHECK(used == prefix && r.count == 0);
			local_ms += 17; CHECK(st_cterm_poll(f.terminal) == 0);
			feed(&f,wire+prefix);
		}
		CHECK(r.count == 2 && igs->draw_x == 37 && igs->draw_start_x == 17);
		CHECK(igs->scaling_resolution == (int)resolution);
		feed(&f,"G#I 5:");
		if (cterm_igs_paused(igs)) { local_ms += 17; CHECK(st_cterm_poll(f.terminal) == 0); }
		CHECK(igs->resolution == resolution && igs->draw_x == 37);
		/* Signed operands are values after lookup, regardless of g mode. */
		igs->math[0] = INT16_MIN; igs->math[1] = INT16_MIN;
		igs->math[2] = INT16_MAX; igs->math[3] = INT16_MAX;
		feed(&f,"G#g>0:z 2,0],1],2],3]:"); CHECK(igs->draw_x == 37);
		feed(&f,"G#z 128,1,2,"); CHECK(st_cterm_reset_terminal(f.terminal) == 0);
		CHECK(igs->draw_x == 0 && igs->draw_start_x == 0 && igs->last_line);
		destroy(&f);
	}
}

int
st_cterm_igs_polyline_test_main(void)
{
	literal(); arity(); input(); flow_and_recovery(); last_segment_state(); scaling_and_lifetime(); vectors();
	return 0;
}
