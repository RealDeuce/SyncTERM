/* W: literal native glyph pixels and opaque incremental text acquisition. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "igs_fill_test_helpers.h"
#include "cterm_igs_audio.h"

static void
literal(void)
{
	static const uint8_t rows[8] = {0x18,0x3c,0x66,0x66,0x7e,0x66,0x66,0};
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f,resolution,false);
		const struct st_console_rect area = {40,54,8,8}; seed(&f,&area,768,768);
		feed(&f,"G#E>0,9,0:W 40,60,A@");
		struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f.console,&area,false) == 0);
		for (unsigned y = 0; y < 8; ++y) for (unsigned x = 0; x < 8; ++x)
			CHECK(b->primary[y*8+x] == 768u+((rows[y] & (128u >> x)) ? cterm_igs_plane_mask(resolution) : 0)
			    && b->alternate[y*8+x] == b->primary[y*8+x]);
		st_pixel_buffer_destroy(b); destroy(&f);
	}
}
static void
write_bytes(struct fixture *f, const void *bytes, size_t size)
{
	size_t used; int status = st_cterm_write(f->terminal,bytes,size,&used);
	if (status != 0 || used != size) fprintf(stderr,"binary W status%d used%zu/%zu\n",status,used,size);
	CHECK(status == 0 && used == size);
}

static void
scene_hashes(struct fixture *f, uint64_t hashes[2])
{
	struct cterm_igs *igs = f->terminal->igs_engine;
	const struct st_console_rect full = {0,0,igs->width,igs->height}; struct st_pixel_buffer *b = NULL;
	CHECK(st_console_get_pixels(&b,f->console,&full,false) == 0);
	unsigned planes = (unsigned[]){4,2,1}[igs->resolution];
	hashes[0] = native_hash(b,planes,false); hashes[1] = native_hash(b,planes,true); st_pixel_buffer_destroy(b);
}

static void
readers(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f,resolution,false); f.terminal->response_cb = no_response;
		struct cterm_igs *igs = f.terminal->igs_engine;
		const struct st_console_rect area = {40,54,24,8}; seed(&f,&area,768,768);
		const uint8_t nul[] = "G#E>0,9,0:W 40,60,A\r\n\0B@"; write_bytes(&f,nul,sizeof(nul)-1);
		CHECK(igs->shared_text_size == 3 && memcmp(igs->shared_text,"A\0B\0",4) == 0);
		static const uint8_t a[8] = {0x18,0x3c,0x66,0x66,0x7e,0x66,0x66,0};
		struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f.console,&area,false) == 0);
		for (unsigned y = 0; y < 8; ++y) for (unsigned x = 0; x < 24; ++x)
			CHECK(b->primary[y*24+x] == 768u+(x < 8 && (a[y] & (128u >> x)) ? cterm_igs_plane_mask(resolution) : 0)
			    && b->alternate[y*24+x] == b->primary[y*24+x]);
		st_pixel_buffer_destroy(b);
		const char control[] = "\x1b[31mG#M>4:"; char opaque[96];
		size_t prefix = (size_t)snprintf(opaque,sizeof(opaque),"G#W 80,80,");
		memcpy(opaque+prefix,control,sizeof(control)-1); opaque[prefix+sizeof(control)-1] = '@';
		write_bytes(&f,opaque,prefix+sizeof(control));
		CHECK(igs->shared_text_size == sizeof(control)-1 && memcmp(igs->shared_text,control,sizeof(control)) == 0);
		CHECK(igs->attributes.writing_mode == 1);
		feed(&f,"G#W 80,80,@"); CHECK(igs->shared_text_size == 1 && memcmp(igs->shared_text,"@\0",2) == 0);
		char wire[256]; prefix = (size_t)snprintf(wire,sizeof(wire),"G#W 40,60,");
		memset(wire+prefix,'X',128); memcpy(wire+prefix+128,"@END",4); write_bytes(&f,wire,prefix+132);
		CHECK(igs->shared_text_size == 128 && igs->shared_text[128] == 0);
		for (unsigned i = 0; i < 128; ++i) CHECK(igs->shared_text[i] == 'X');
		struct st_console_cell cells[5]; const struct st_console_rect text = {0,0,5,1};
		CHECK(st_console_read_cells(f.console,&text,cells,5) == 0);
		for (unsigned i = 0; i < 4; ++i) CHECK(cells[i].glyph == (unsigned char)"@END"[i]);
		prefix = (size_t)snprintf(wire,sizeof(wire),"G#W>40,60,"); memset(wire+prefix,'X',128);
		memcpy(wire+prefix+128,"M 2:B",5); write_bytes(&f,wire,prefix+133);
		CHECK(igs->attributes.writing_mode == 2 && igs->shared_text_size == 128);
		CHECK(st_console_read_cells(f.console,&text,cells,5) == 0 && cells[4].glyph == 'B');
		uint64_t before[2], after[2]; scene_hashes(&f,before);
		const uint8_t empty[] = "G#W 40,60,\0B@"; write_bytes(&f,empty,sizeof(empty)-1); scene_hashes(&f,after);
		CHECK(before[0] == after[0] && before[1] == after[1]);
		CHECK(igs->shared_text_size == 2 && igs->shared_text[0] == 0 && igs->shared_text[1] == 'B');
		CHECK(igs->text_deadline == 0 && igs->input_deadline == 0); destroy(&f);
	}
}

static void
partitions(void)
{
	const uint8_t first[] = "AG#E>31,18,4:M>3:W>100,100,A\x80\xff@M 2:B";
	const uint8_t second[] = "AG#g>1:W>r,R,A@M 2:B";
	const uint8_t third[] = "AG#W>40,60,\x1bG#M>4:\0A\r\n@M 2:B";
	const struct { const uint8_t *bytes; size_t size; } wires[] = {
		{first,sizeof(first)-1},{second,sizeof(second)-1},{third,sizeof(third)-1},
	};
	for (unsigned resolution = 0; resolution < 3; ++resolution) for (size_t which = 0; which < 3; ++which)
		for (size_t split = 0; split <= wires[which].size+1; ++split) {
			struct fixture f; create(&f,resolution,false); f.terminal->response_cb = no_response;
			struct cterm_igs *igs = f.terminal->igs_engine; igs->random_range[0][1] = igs->random_range[1][1] = 255;
			if (split > wires[which].size) for (size_t i = 0; i < wires[which].size; ++i) write_bytes(&f,wires[which].bytes+i,1);
			else { write_bytes(&f,wires[which].bytes,split); write_bytes(&f,wires[which].bytes+split,wires[which].size-split); }
			CHECK(igs->random_seed == (which == 1 ? UINT32_C(0x5eb5ca17) : 1));
			CHECK(igs->attributes.writing_mode == 2 && igs->draw_x == 0 && igs->draw_y == 0);
			CHECK(igs->shared_text_size == (which == 0 ? 3u : which == 1 ? 1u : 9u));
			CHECK(igs->text_deadline == 0 && igs->input_deadline == 0);
			struct st_console_cell cells[2]; const struct st_console_rect text = {0,0,2,1};
			CHECK(st_console_read_cells(f.console,&text,cells,2) == 0 && cells[0].glyph == 'A' && cells[1].glyph == 'B'); destroy(&f);
		}
}

struct replies { struct fixture *fixture; size_t count; unsigned action; uint8_t bytes[8]; };
static void
reply(const char *bytes, size_t size, void *context)
{
	struct replies *r = context;
	CHECK(size == 1 && r->count < sizeof(r->bytes)); r->bytes[r->count++] = (uint8_t)*bytes;
	if (r->action == 1 && r->count == 1) {
		struct cterm_igs *igs = r->fixture->terminal->igs_engine;
		igs->flow_mode = 0; igs->flow_on_count = 3; igs->flow_on = 0xab;
	} else if (r->action == 2 && r->count == 1) CHECK(st_cterm_reset_terminal(r->fixture->terminal) == 0);
}

static int
bad_clock(uint64_t *value)
{
	(void)value; return IO_ERROR_IO;
}

static unsigned clock_reads;
static int
first_clock(uint64_t *value)
{
	return clock_reads++ == 0 ? now(value) : IO_ERROR_IO;
}

static void
loops_and_deadlines(void)
{
	struct fixture f; create(&f,0,false); struct cterm_igs *igs = f.terminal->igs_engine;
	struct replies r = {.fixture = &f}; f.terminal->response_cb = reply; f.terminal->response_cbdata = &r;
	feed(&f,"G#W 40,60,A@"); CHECK(r.count == 0 && igs->shared_text_size == 1);
	igs->loop_mode = 1; igs->loop_kind = 'W'; feed(&f,"G#W 50,70:");
	CHECK(r.count == 0 && igs->shared_text[0] == 'A' && igs->text_deadline == 0);
	igs->loop_mode = 0; igs->loop_kind = 'Z'; feed(&f,"G#W 60,80:"); CHECK(igs->loop_kind == ' ' && r.count == 0);
	igs->loop_mode = 1; igs->loop_kind = '@'; feed(&f,"G#W 80,80,");
	CHECK(r.count == 1 && r.bytes[0] == 17 && igs->text_counter == 1 && igs->text_deadline == 6000);
	CHECK(igs->shared_text[0] == 'A'); feed(&f,"B@");
	CHECK(igs->shared_text[0] == 'B' && igs->text_deadline == 0 && r.count == 1);
	feed(&f,"G#W 90,90,C@"); CHECK(igs->text_counter == 2 && r.count == 1 && igs->shared_text[0] == 'C');
	igs->text_counter = UINT16_MAX; feed(&f,"G#W 90,90,D@"); CHECK(igs->text_counter == (size_t)UINT16_MAX+1 && r.count == 1);
	igs->text_counter = SIZE_MAX; feed(&f,"G#W 90,90,E@"); CHECK(igs->text_counter == SIZE_MAX && r.count == 1);
	r.count = 0; r.action = 1; igs->text_counter = 0; igs->flow_on = 0xfe; igs->flow_on_count = 1;
	feed(&f,"G#W 90,90,F@"); CHECK(r.count == 3 && memcmp(r.bytes,"\xfe\xab\xab",3) == 0);
	r.count = 0; r.action = 2; igs->text_counter = 0; igs->flow_mode = 1; igs->flow_on_count = 1; igs->flow_on = 17;
	feed(&f,"G#W 90,90,G#M 4:"); CHECK(r.count == 1 && igs->shared_text_size == 0 && igs->text_counter == 0 && igs->attributes.writing_mode == 4);
	r.action = 0; r.count = 0; f.terminal->response_cb = no_response;
	feed(&f,"G#M>1:W 40,60,OLD@"); seed_words(&f); memcpy(igs->patterns[2],user_rows,sizeof(user_rows));
	uint64_t before[2], after[2]; scene_hashes(&f,before);
	uint8_t saved[129]; memcpy(saved,igs->shared_text,sizeof(saved));
	feed(&f,"G#W 80,80,N"); CHECK(igs->text_deadline == local_ms+5000);
	uint64_t deadline = igs->text_deadline; local_ms += 4000; feed(&f,"E"); CHECK(igs->text_deadline == deadline && igs->input_deadline == deadline);
	scene_hashes(&f,after); CHECK(before[0] == after[0] && before[1] == after[1] && memcmp(saved,igs->shared_text,sizeof(saved)) == 0);
	local_ms = deadline; CHECK(st_cterm_poll(f.terminal) == 0 && igs->text_deadline == 0);
	CHECK(igs->shared_text_size == 3 && memcmp(saved,igs->shared_text,sizeof(saved)) == 0);
	feed(&f,"G#W 80,80,NEW@"); CHECK(memcmp(igs->shared_text,"NEW\0",4) == 0);
	feed(&f,"G#W 80,80,X"); CHECK(st_cterm_finish(f.terminal) == 0 && memcmp(igs->shared_text,"NEW\0",4) == 0);
	igs->random_range[0][1] = igs->random_range[1][1] = 255;
	feed(&f,"G#W r,R,"); CHECK(igs->random_seed == 1); CHECK(st_cterm_finish(f.terminal) == 0 && igs->random_seed == 1);
	size_t used; const char invalid[] = "G#W 80,10000,A@";
	CHECK(st_cterm_write(f.terminal,invalid,sizeof(invalid)-1,&used) == IO_ERROR_OUT_OF_RANGE);
	CHECK(memcmp(igs->shared_text,"NEW\0",4) == 0 && igs->text_deadline == 0);
	feed(&f,"G#W 80,80,late"); local_ms = igs->text_deadline; feed(&f,"G#M 2:");
	CHECK(igs->attributes.writing_mode == 2 && memcmp(igs->shared_text,"NEW\0",4) == 0 && igs->text_deadline == 0);
	igs->now_ms = bad_clock; const char header[] = "G#W 80,80,";
	CHECK(st_cterm_write(f.terminal,header,sizeof(header)-1,&used) == IO_ERROR_IO && igs->text_deadline == 0);
	igs->now_ms = now; feed(&f,header); deadline = igs->text_deadline; igs->now_ms = bad_clock;
	CHECK(st_cterm_write(f.terminal,"A@",2,&used) == IO_ERROR_IO && used == 0 && igs->text_deadline == deadline);
	igs->now_ms = now; feed(&f,"A@"); CHECK(igs->text_deadline == 0 && igs->shared_text_size == 1);
	for (unsigned i = 0; i < 16; ++i) CHECK(igs->patterns[2][i] == user_rows[i]);
	/* A later clock failure must not lose the already established deadline.
	 * Earlier timeout BELs can leave a console audio model polling the clock;
	 * quiesce it so this fault targets field entry and reader progress. */
	cterm_igs_audio_destroy(igs);
	igs->now_ms = first_clock; clock_reads = 0;
	CHECK(st_cterm_write(f.terminal,header,sizeof(header)-1,&used) == IO_ERROR_IO && used == sizeof(header)-1);
	CHECK(clock_reads == 2 && igs->text_deadline == local_ms+5000 && igs->input_deadline == igs->text_deadline);
	igs->now_ms = now; local_ms = igs->text_deadline;
	CHECK(st_cterm_poll(f.terminal) == 0 && igs->text_deadline == 0 && igs->input_deadline == 0);
	destroy(&f);
}

/* Two independent standard checksums over the fixed native frame byte stream. */
static uint64_t
text_checksum(const struct st_pixel_buffer *b, unsigned planes, bool alternate)
{
	static uint32_t table[256]; static bool initialized;
	if (!initialized) {
		for (unsigned i = 0; i < 256; ++i) {
			uint32_t value = i;
			for (unsigned j = 0; j < 8; ++j) value = (value >> 1)^((value & 1) ? UINT32_C(0xedb88320) : 0);
			table[i] = value;
		}
		initialized = true;
	}
	uint32_t crc = UINT32_MAX, a = 1, sum = 0;
	const uint32_t *pixels = alternate ? b->alternate : b->primary;
	for (unsigned y = 0; y < b->height; ++y) for (unsigned group = 0; group < b->width/16; ++group) {
		uint16_t words[4] = {0};
		for (unsigned bit = 0; bit < 16; ++bit) {
			uint32_t colour = pixels[y*b->width+group*16+bit];
			CHECK(colour >= 768 && colour < 768+(1u << planes));
			for (unsigned p = 0; p < planes; ++p)
				if ((colour-768) & (1u << p)) words[p] |= (uint16_t)(0x8000u >> bit);
		}
		for (unsigned p = 0; p < planes; ++p) for (int shift = 8; shift >= 0; shift -= 8) {
			unsigned byte = (words[p] >> shift) & 255;
			crc = table[(crc^byte)&255]^(crc >> 8);
			a += byte; if (a >= 65521) a -= 65521;
			sum += a; if (sum >= 65521) sum -= 65521;
		}
	}
	return ((uint64_t)(crc^UINT32_MAX) << 32) | (sum << 16) | a;
}
struct text_string { size_t size; uint8_t bytes[128]; };
struct text_case {
	int resolution, mode, points, style, rotation, x, y, clipping, last;
	int align_h, align_v, direct, text_id, wire_id, bounds[4];
	uint64_t hashes[2];
};
#include "data/igs_text_vectors.h"
_Static_assert(sizeof(text_cases)/sizeof(*text_cases) == 22554, "complete independent text matrix");

static void
vectors(void)
{
	struct fixture f; bool active = false; int resolution = -1;
	for (size_t i = 0; i < sizeof(text_cases)/sizeof(*text_cases); ++i) {
		const struct text_case *c = &text_cases[i];
		if (resolution != c->resolution) {
			if (active) destroy(&f);
			create(&f,(unsigned)c->resolution,false); active = true; resolution = c->resolution;
			f.terminal->response_cb = no_response;
		}
		struct cterm_igs *igs = f.terminal->igs_engine; seed_words(&f);
		igs->draw_start_x = 17; igs->draw_start_y = 23; igs->draw_x = 37; igs->draw_y = 47;
		igs->attributes.line_type = 7; igs->attributes.line_width = 39;
		igs->attributes.line_begin = 1; igs->attributes.line_end = 2; igs->attributes.user_line = 0xa531;
		igs->attributes.text_pen = 7; memcpy(igs->patterns[3],user_rows,sizeof(user_rows));
		char wire[256]; igs->math[0] = (int16_t)c->rotation;
		(void)snprintf(wire,sizeof(wire),"G#g>0:E>%d,%d,0]:M %d:",c->style,c->points,c->mode); feed(&f,wire);
		igs->attributes.text_align_h = c->align_h; igs->attributes.text_align_v = c->align_v;
		igs->clipping = c->clipping != 0; igs->last_line = c->last != 0;
		igs->clip_x1 = c->bounds[0]; igs->clip_y1 = c->bounds[1]; igs->clip_x2 = c->bounds[2]; igs->clip_y2 = c->bounds[3];
		igs->math[1] = (int16_t)c->x; igs->math[2] = (int16_t)c->y;
		struct cterm_igs_attributes before = igs->attributes;
		uint32_t palette[16]; memcpy(palette,igs->palette,sizeof(palette));
		int16_t math[128]; memcpy(math,igs->math,sizeof(math));
		uint16_t patterns[8][16]; memcpy(patterns,igs->patterns,sizeof(patterns));
		const struct text_string *text = &text_strings[c->text_id], *raw = &text_strings[c->wire_id];
		if (c->direct) CHECK(cterm_igs_graphic_text(igs,c->x,c->y,text->bytes,text->size) == 0);
		else {
			size_t count = (size_t)snprintf(wire,sizeof(wire),"G#W 1],2],");
			memcpy(wire+count,raw->bytes,raw->size); count += raw->size;
			if (raw->size < 128) wire[count++] = '@';
			write_bytes(&f,wire,count);
			size_t collected = 0;
			for (size_t j = 0; j < raw->size; ++j)
				if (raw->bytes[j] != 10 && raw->bytes[j] != 13) CHECK(igs->shared_text[collected++] == raw->bytes[j]);
			if (collected == 0) { CHECK(igs->shared_text[0] == '@'); collected = 1; }
			CHECK(igs->shared_text_size == collected && igs->shared_text[collected] == 0);
		}
		CHECK(memcmp(&before,&igs->attributes,sizeof(before)) == 0 && memcmp(palette,igs->palette,sizeof(palette)) == 0);
		CHECK(memcmp(math,igs->math,sizeof(math)) == 0 && memcmp(patterns,igs->patterns,sizeof(patterns)) == 0);
		CHECK(igs->draw_start_x == 17 && igs->draw_start_y == 23 && igs->draw_x == 37 && igs->draw_y == 47);
		CHECK(igs->last_line == (c->last != 0) && igs->random_seed == 1 && igs->pattern_seed == 0);
		CHECK(igs->text_deadline == 0 && igs->input_deadline == 0);
		const struct st_console_rect full = {0,0,igs->width,igs->height}; struct st_pixel_buffer *b = NULL;
		CHECK(st_console_get_pixels(&b,f.console,&full,false) == 0);
		for (unsigned phase = 0; phase < 2; ++phase) {
			uint64_t hash = text_checksum(b,(unsigned[]){4,2,1}[resolution],phase != 0);
			if (hash != c->hashes[phase]) fprintf(stderr,"W %zu res%d mode%d point%d style%d rot%d xy%d,%d clip%d align%d,%d phase%u: %016" PRIx64 " != %016" PRIx64 "\n",
			    i,resolution,c->mode,c->points,c->style,c->rotation,c->x,c->y,c->clipping,c->align_h,c->align_v,phase,hash,c->hashes[phase]);
			CHECK(hash == c->hashes[phase]);
		}
		st_pixel_buffer_destroy(b);
	}
	if (active) destroy(&f);
}

/* Original right-edge ink distinguishes clip-before-bold from crop-after-bold. */
static void
bold_edges(void)
{
	static const struct { int point,code,width,height,top; uint8_t rows[16]; } glyphs[] = {
		{8,3,6,6,4,{0x10,0x18,0xec,0x18,0x10,0}},
		{9,1,8,8,6,{0x18,0x3c,0x66,0xc3,0xe7,0x24,0x24,0x3c}},
		{10,1,8,16,13,{0,0,0,0x18,0x3c,0x66,0xc3,0x81,0xe7,0x24,0x24,0x24,0x3c,0,0,0}},
	};
	struct fixture f; create(&f,0,false); struct cterm_igs *igs = f.terminal->igs_engine;
	for (size_t i = 0; i < 3; ++i) for (int factor = 1; factor <= 2; ++factor) for (int cut = 0; cut <= 3; ++cut) {
		int width = glyphs[i].width*factor, height = glyphs[i].height*factor;
		const struct st_console_rect area = {40,40,(unsigned)width,(unsigned)height}; seed(&f,&area,768,768);
		char wire[48]; (void)snprintf(wire,sizeof(wire),"G#E 1,%d,0:",glyphs[i].point*factor); feed(&f,wire);
		igs->clip_x1 = 40+cut; igs->clip_x2 = 40+width-1-cut;
		igs->clip_y1 = 40; igs->clip_y2 = 40+height-1;
		uint8_t code = (uint8_t)glyphs[i].code;
		CHECK(cterm_igs_graphic_text(igs,40,40+glyphs[i].top*factor+(factor-1),&code,1) == 0);
		struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f.console,&area,false) == 0);
		for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
			bool ink = false;
			for (int n = 0; n <= factor; ++n) {
				int source = x-n;
				if (source >= cut && source < width-cut-factor && (glyphs[i].rows[y/factor] & (128u >> (source/factor)))) ink = true;
			}
			if (x < cut || x >= width-cut) ink = false;
			CHECK(b->primary[y*width+x] == (ink ? 783u : 768u) && b->alternate[y*width+x] == b->primary[y*width+x]);
		}
		st_pixel_buffer_destroy(b);
	}
	destroy(&f);
}

static void
lifecycle_and_coordinates(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f,resolution,false); struct cterm_igs *igs = f.terminal->igs_engine;
		feed(&f,"G#W 40,60,SAVED@"); memcpy(igs->patterns[2],user_rows,sizeof(user_rows));
		for (unsigned operation = 0; operation < 3; operation += 2) {
			char wire[64]; (void)snprintf(wire,sizeof(wire),"G#E>31,20,4:I>%u:W 40,60,A@",operation); feed(&f,wire);
			CHECK(igs->attributes.text_points == (resolution == 2 ? 10 : 9));
			CHECK(memcmp(igs->patterns[2],user_rows,sizeof(user_rows)) == 0);
			CHECK(igs->shared_text_size == 1 && igs->shared_text[0] == 'A');
		}
		/* Loaded word coordinates are used after expression-index scaling. */
		feed(&f,"G#E>0,9,0:g 0:"); igs->math[1] = -1000; igs->math[2] = 60;
		char wire[160]; size_t count = (size_t)snprintf(wire,sizeof(wire),"G#W 1],2],"); memset(wire+count,'X',128);
		feed(&f,"\x1b" "E"); write_bytes(&f,wire,count+128);
		static const uint8_t rows[8] = {0x66,0x66,0x3c,0x18,0x3c,0x66,0x66,0};
		const struct st_console_rect area = {0,54,32,8}; struct st_pixel_buffer *b = NULL;
		CHECK(st_console_get_pixels(&b,f.console,&area,false) == 0);
		for (unsigned y = 0; y < 8; ++y) for (unsigned x = 0; x < 32; ++x)
			CHECK(b->primary[y*32+x] == 768u+(x < 24 && (rows[y] & (128u >> (x%8))) ? cterm_igs_plane_mask(resolution) : 0));
		st_pixel_buffer_destroy(b);
		feed(&f,"G#I 5:"); if (cterm_igs_paused(igs)) { local_ms += 17; CHECK(st_cterm_poll(f.terminal) == 0); }
		CHECK(igs->shared_text_size == 128 && igs->shared_text[0] == 'X');
		CHECK(memcmp(igs->patterns[2],user_rows,sizeof(user_rows)) == 0);
		feed(&f,"G#I 0:"); CHECK(igs->shared_text_size == 128 && igs->shared_text[0] == 'X');
		feed(&f,resolution == 0 ? "G#R 1,0:" : "G#R 0,0:");
		if (cterm_igs_paused(igs)) { local_ms += 17; CHECK(st_cterm_poll(f.terminal) == 0); }
		CHECK(igs->shared_text_size == 128 && igs->shared_text[0] == 'X');
		feed(&f,"G#I 5:"); if (cterm_igs_paused(igs)) { local_ms += 17; CHECK(st_cterm_poll(f.terminal) == 0); }
		CHECK(igs->shared_text_size == 128 && igs->shared_text[0] == 'X');
		uint64_t before[2], after[2]; scene_hashes(&f,before);
		CHECK(cterm_igs_graphic_text(igs,0,0,NULL,1) == IO_ERROR_INVALID_ARGUMENT);
		CHECK(cterm_igs_graphic_text(igs,0,0,igs->shared_text,129) == IO_ERROR_INVALID_ARGUMENT);
		igs->attributes.text_face = 2; CHECK(cterm_igs_graphic_text(igs,0,0,(const uint8_t *)"A",1) == IO_ERROR_NOT_SUPPORTED);
		igs->attributes.text_face = 1; igs->attributes.text_rotation = 1;
		CHECK(cterm_igs_graphic_text(igs,0,0,(const uint8_t *)"A",1) == IO_ERROR_INVALID_ARGUMENT);
		scene_hashes(&f,after); CHECK(before[0] == after[0] && before[1] == after[1]);
		feed(&f,"G#W 80,80,PARTIAL"); CHECK(st_cterm_reset_terminal(f.terminal) == 0);
		CHECK(igs->shared_text_size == 0 && igs->text_counter == 0 && igs->text_deadline == 0);
		destroy(&f);
	}
}

static void
scaled_and_status(void)
{
	static const uint8_t rows[8] = {0x18,0x3c,0x66,0x66,0x7e,0x66,0x66,0};
	for (unsigned resolution = 0; resolution < 3; ++resolution) for (unsigned mode = 0; mode < 3; ++mode) {
		struct fixture f; create(&f,resolution,false); struct cterm_igs *igs = f.terminal->igs_engine;
		char wire[80]; (void)snprintf(wire,sizeof(wire),"G#E>0,9,0:g>%u:W %s,A@",mode,mode == 1 ? "5000,5000" : "40,60"); feed(&f,wire);
		int x = mode == 1 ? (resolution == 0 ? 160 : 320) : 40;
		int y = mode == 1 ? (resolution == 2 ? 200 : 100) : mode == 2 && resolution == 2 ? 120 : 60;
		struct st_console_rect area = {(unsigned)x,(unsigned)(y-6),8,8}; struct st_pixel_buffer *b = NULL;
		CHECK(st_console_get_pixels(&b,f.console,&area,false) == 0);
		for (unsigned row = 0; row < 8; ++row) for (unsigned col = 0; col < 8; ++col)
			CHECK(b->primary[row*8+col] == 768u+((rows[row] & (128u >> col)) ? cterm_igs_plane_mask(resolution) : 0));
		st_pixel_buffer_destroy(b);
		/* In normalized mode an operand index5000 scales/clamps before lookup;
		 * the loaded signed word itself remains a physical coordinate. */
		igs->math[100] = igs->math[127] = 80; feed(&f,"G#g>1:W 5000],5000],A@");
		area = (struct st_console_rect){80,74,8,8}; CHECK(st_console_get_pixels(&b,f.console,&area,false) == 0);
		for (unsigned row = 0; row < 8; ++row) for (unsigned col = 0; col < 8; ++col)
			CHECK(b->primary[row*8+col] == 768u+((rows[row] & (128u >> col)) ? cterm_igs_plane_mask(resolution) : 0));
		st_pixel_buffer_destroy(b); destroy(&f);
	}
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f,resolution,true); struct cterm_igs *igs = f.terminal->igs_engine;
		unsigned ch = resolution == 2 ? 16 : 8;
		const struct st_console_rect status = {0,24*ch,igs->width,ch}; seed(&f,&status,769,768);
		char wire[64]; (void)snprintf(wire,sizeof(wire),"G#E>31,20,0:W 40,%u,A@",24*ch); feed(&f,wire);
		struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f.console,&status,false) == 0);
		for (size_t i = 0; i < (size_t)igs->width*ch; ++i) CHECK(b->primary[i] == 769 && b->alternate[i] == 768);
		st_pixel_buffer_destroy(b); destroy(&f);
	}
}

int
st_cterm_igs_text_test_main(void)
{
	literal(); readers(); partitions(); loops_and_deadlines(); bold_edges(); lifecycle_and_coordinates(); scaled_and_status(); vectors();
	return 0;
}
