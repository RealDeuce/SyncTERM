/* X11: literal native-byte fills, decoded indices and retained image views. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "igs_test_fixture.h"
#include "st_igs_image.h"

static void
literal(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f,resolution,false);
		struct cterm_igs *igs = f.terminal->igs_engine;
		const struct st_console_rect full = {0,0,igs->width,igs->height}; seed(&f,&full,769,768);
		feed(&f,"G#X 11,0,0,128:");
		CHECK(igs->memory[0] != NULL);
		unsigned mask = cterm_igs_plane_mask(resolution);
		for (size_t i = 0; i < igs->memory[0]->pixels->width; ++i)
			CHECK(igs->memory[0]->pixels->primary[i] == (i%8 == 0 ? mask : 0));
		CHECK(igs->memory_rect[0][2] == (int)igs->width-1 && igs->memory_rect[0][3] == (int)igs->height-1);
		CHECK(igs->memory_view[0].word_width == (resolution == 0 ? 20 : 40));
		struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f.console,&full,false) == 0);
		for (size_t i = 0; i < (size_t)igs->width*igs->height; ++i) CHECK(b->primary[i] == 769 && b->alternate[i] == 768);
		st_pixel_buffer_destroy(b); destroy(&f);
	}
}

static void
write_bytes(struct fixture *f, const void *bytes, size_t size)
{
	size_t used; int status = st_cterm_write(f->terminal,bytes,size,&used);
	if (status != 0 || used != size) fprintf(stderr,"bitmap status%d used%zu/%zu\n",status,used,size);
	CHECK(status == 0 && used == size);
}

static unsigned
native_pixel(const uint8_t *bytes, unsigned planes, size_t pixel)
{
	unsigned value = 0;
	for (unsigned plane = 0; plane < planes; ++plane) {
		size_t word = 2*((pixel/16)*planes+plane);
		unsigned bits = ((unsigned)bytes[word]<<8)|bytes[word+1];
		value |= ((bits >> (15-pixel%16))&1u)<<plane;
	}
	return value;
}

static void
prepare_image(struct fixture *f, uint8_t *primary, uint8_t *alternate)
{
	struct cterm_igs *igs = f->terminal->igs_engine;
	unsigned planes = (unsigned[]){4,2,1}[igs->resolution], mask = (1u<<planes)-1;
	if (igs->memory[0] == NULL) CHECK(st_igs_image_create(&igs->memory[0],planes) == 0);
	CHECK(st_pixel_buffer_ensure_alternate(igs->memory[0]->pixels) == 0);
	CHECK(st_pixel_buffer_fill(igs->memory[0]->pixels,5&mask,10&mask) == 0);
	for (unsigned i = 0; i < ST_IGS_IMAGE_BYTES; ++i) {
		primary[i] = 5&(1u<<((i/2)%planes)) ? 255 : 0;
		alternate[i] = 10&(1u<<((i/2)%planes)) ? 255 : 0;
	}
}

static void
check_image(struct fixture *f, const uint8_t *primary, const uint8_t *alternate)
{
	struct cterm_igs *igs = f->terminal->igs_engine;
	unsigned planes = (unsigned[]){4,2,1}[igs->resolution];
	CHECK(igs->memory[0] != NULL && igs->memory[0]->planes == planes);
	const struct st_pixel_buffer *pixels = igs->memory[0]->pixels;
	CHECK(pixels->width == 256000/planes && pixels->height == 1 && pixels->alternate != NULL);
	for (size_t i = 0; i < pixels->width; ++i) {
		CHECK(pixels->primary[i] == native_pixel(primary,planes,i));
		CHECK(pixels->alternate[i] == native_pixel(alternate,planes,i));
	}
}

static void
check_descriptor(struct fixture *f, int section)
{
	struct cterm_igs *igs = f->terminal->igs_engine;
	if (section > 8) section = 8;
	int band = igs->resolution == 2 ? 50 : 25;
	int first = section == 0 ? 0 : (section-1)*band;
	int last = section == 0 ? (int)igs->height-1 : section*band-1;
	CHECK(memcmp(igs->memory_rect[0],((int[4]){0,first,(int)igs->width-1,last}),sizeof(igs->memory_rect[0])) == 0);
	CHECK(igs->memory_view[0].width == (int)igs->width-1 && igs->memory_view[0].height == last-first);
	CHECK(igs->memory_view[0].word_width == (igs->resolution == 0 ? 20 : 40));
	CHECK(igs->memory_view[0].planes == ((unsigned[]){4,2,1})[igs->resolution] && !igs->memory_view[0].screen);
}

static void
check_screen(struct fixture *f, const uint8_t *primary, const uint8_t *alternate, int section, bool display)
{
	struct cterm_igs *igs = f->terminal->igs_engine;
	unsigned planes = (unsigned[]){4,2,1}[igs->resolution];
	int band = igs->resolution == 2 ? 50 : 25;
	int first = section == 0 ? 0 : (section-1)*band, last = section == 0 ? (int)igs->height-1 : section*band-1;
	const struct st_console_rect full = {0,0,igs->width,igs->height}; struct st_pixel_buffer *b = NULL;
	CHECK(st_console_get_pixels(&b,f->console,&full,false) == 0);
	unsigned drawable = (unsigned)f->terminal->height*(igs->resolution == 2 ? 16u : 8u);
	for (unsigned y = 0; y < igs->height; ++y) for (unsigned x = 0; x < igs->width; ++x) {
		size_t i = (size_t)y*igs->width+x;
		bool copied = display && (int)y >= first && (int)y <= last && y < drawable;
		CHECK(b->primary[i] == (copied ? 768+native_pixel(primary,planes,i) : 769));
		CHECK(b->alternate[i] == (copied ? 768+native_pixel(alternate,planes,i) : 768));
	}
	st_pixel_buffer_destroy(b);
}

static void
wipes(void)
{
	static const int sections[] = {INT16_MIN,-1,0,1,2,3,4,5,6,7,8,9,9999};
	static const int fills[] = {INT16_MIN,-1,0,1,128,255,256,511};
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f,resolution,true);
		struct cterm_igs *igs = f.terminal->igs_engine;
		feed(&f,"\033fG#g>1:X 10,5000,5000:");
		int draw_x = igs->draw_x, draw_y = igs->draw_y;
		struct cterm_igs_attributes attributes; memcpy(&attributes,&igs->attributes,sizeof(attributes));
		uint32_t palette[16]; memcpy(palette,igs->palette,sizeof(palette));
		unsigned planes = (unsigned[]){4,2,1}[resolution];
		CHECK(st_igs_image_create(&igs->memory[1],planes) == 0);
		CHECK(st_pixel_buffer_fill(igs->memory[1]->pixels,1,1) == 0);
		memcpy(igs->memory_rect[1],((int[4]){16,8,31,15}),sizeof(igs->memory_rect[1]));
		igs->memory_view[1] = (struct cterm_igs_memory_view){15,7,1,planes,true};
		const struct st_console_rect full = {0,0,igs->width,igs->height}; seed(&f,&full,769,768);
		uint8_t primary[ST_IGS_IMAGE_BYTES], alternate[ST_IGS_IMAGE_BYTES];
		for (size_t s = 0; s < sizeof(sections)/sizeof(*sections); ++s)
			for (size_t v = 0; v < sizeof(fills)/sizeof(*fills); ++v) {
				prepare_image(&f,primary,alternate);
				igs->random_seed = 1; igs->memory_route = 5; igs->double_step = 4; igs->bitmap_errors = 2;
				igs->math[0] = (int16_t)sections[s]; igs->math[1] = (int16_t)fills[v];
				feed(&f,"G#X 11,0,0],1]:");
				int section = sections[s] > 8 ? 8 : sections[s];
				size_t start = section > 0 ? (size_t)(section-1)*4000 : 0;
				size_t count = section < 0 ? 0 : section == 0 ? 32000 : 4000;
				uint32_t state = 1;
				for (size_t i = 0; i < count; ++i) {
					uint8_t byte = (uint8_t)fills[v];
					if (fills[v] == 256) { state = state*UINT32_C(0xbb40e62d)+1; byte = (uint8_t)(state>>8); }
					primary[start+i] = alternate[start+i] = byte;
				}
				check_image(&f,primary,alternate); check_descriptor(&f,section);
				CHECK(igs->random_seed == state && igs->bitmap_errors == 2);
				if (fills[v] == 256 && count != 0) CHECK(state == (count == 4000 ? UINT32_C(0xa7054a61) : UINT32_C(0xbb2c9301)));
				CHECK(igs->memory_route == 5 && igs->double_step == 4 && !igs->bitmap_loading && !st_cterm_input_paused(f.terminal));
				CHECK(igs->draw_x == draw_x && igs->draw_y == draw_y);
				CHECK(igs->text_counter == 0 && igs->text_deadline == 0 && igs->input_deadline == 0);
			}
		CHECK(memcmp(&attributes,&igs->attributes,sizeof(attributes)) == 0 && memcmp(palette,igs->palette,sizeof(palette)) == 0);
		CHECK(memcmp(igs->memory_rect[1],((int[4]){16,8,31,15}),sizeof(igs->memory_rect[1])) == 0);
		CHECK(igs->memory_view[1].screen && igs->memory_view[1].word_width == 1);
		for (size_t i = 0; i < igs->memory[1]->pixels->width; ++i) CHECK(igs->memory[1]->pixels->primary[i] == 1);
		check_screen(&f,primary,alternate,0,false); destroy(&f);
	}
}

static void
loads_and_eof(void)
{
	static const uint8_t literal[] = {0x80,0,0x40,0,0x20,0,0x10,0};
	static const int sections[] = {0,1,4,8,9,-1};
	for (unsigned resolution = 0; resolution < 3; ++resolution)
		for (unsigned status = 0; status < 2; ++status) {
			struct fixture f; create(&f,resolution,status != 0);
			struct cterm_igs *igs = f.terminal->igs_engine;
			uint8_t primary[32000], alternate[32000], payload[32000];
			for (unsigned i = 0; i < sizeof(payload); ++i) payload[i] = (uint8_t)i;
			for (size_t s = 0; s < sizeof(sections)/sizeof(*sections); ++s)
				for (int operation = 1; operation <= 2; ++operation) {
					prepare_image(&f,primary,alternate);
					igs->memory_route = 5; igs->double_step = 4; igs->loop_mode = 1; igs->loop_kind = '@';
					const struct st_console_rect full = {0,0,igs->width,igs->height}; seed(&f,&full,769,768);
					int section = sections[s] > 8 ? 8 : sections[s];
					igs->math[0] = (int16_t)sections[s];
					char header[32]; (void)snprintf(header,sizeof(header),"G#X 11,%d,0]:",operation); feed(&f,header);
					size_t offset = section > 0 ? (size_t)(section-1)*4000 : 0, count = section < 0 ? 0 : section == 0 ? 32000 : 4000;
					if (count != 0) {
						CHECK(igs->bitmap_loading && igs->bitmap_received == 0 && igs->bitmap_deadline == local_ms+5000);
						write_bytes(&f,payload,17); memcpy(primary+offset,payload,17); memcpy(alternate+offset,payload,17);
						check_image(&f,primary,alternate); check_screen(&f,primary,alternate,section,false);
						write_bytes(&f,payload+17,count-17); memcpy(primary+offset,payload,count); memcpy(alternate+offset,payload,count);
					}
					check_image(&f,primary,alternate); check_descriptor(&f,section); check_screen(&f,primary,alternate,section,operation == 1);
					CHECK(!igs->bitmap_loading && !st_cterm_input_paused(f.terminal) && igs->bitmap_errors == 0);
					CHECK(igs->memory_route == 5 && igs->double_step == 4 && igs->text_counter == 0 && igs->text_deadline == 0);
				}
			prepare_image(&f,primary,alternate);
			const struct st_console_rect full = {0,0,igs->width,igs->height}; seed(&f,&full,769,768);
			feed(&f,"G#X 11,1,4:"); write_bytes(&f,literal,sizeof(literal));
			memcpy(primary+12000,literal,sizeof(literal)); memcpy(alternate+12000,literal,sizeof(literal));
			CHECK(st_cterm_finish(f.terminal) == 0);
			check_image(&f,primary,alternate); check_descriptor(&f,4); check_screen(&f,primary,alternate,4,true);
			CHECK(!igs->bitmap_loading && igs->bitmap_errors == 0 && igs->bitmap_deadline == 0);
			destroy(&f);
		}
}

static int
bad_clock(uint64_t *value)
{
	(void)value; return IO_ERROR_IO;
}

static void
no_response(const char *bytes, size_t size, void *context)
{
	(void)bytes; (void)size; (void)context; CHECK(false);
}

static void
timeouts(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution)
		for (unsigned status = 0; status < 2; ++status) {
			struct fixture f; create(&f,resolution,status != 0); f.terminal->response_cb = no_response;
			struct cterm_igs *igs = f.terminal->igs_engine;
			uint8_t primary[32000], alternate[32000]; prepare_image(&f,primary,alternate);
			const struct st_console_rect full = {0,0,igs->width,igs->height}; seed(&f,&full,769,768);
			feed(&f,"\033bG#X 11,1,1:");
			local_ms = igs->bitmap_deadline-1; CHECK(st_cterm_poll(f.terminal) == 0 && igs->bitmap_received == 0);
			++local_ms; CHECK(st_cterm_poll(f.terminal) == 0 && igs->bitmap_received == 1 && igs->bitmap_errors == 1);
			primary[0] = alternate[0] = '*';
			CHECK(f.terminal->seq_trailing_remain == 0 && f.terminal->vt52_foreground == (uint16_t)(7-32));
			local_ms += 3000; const uint8_t first = 0x77; write_bytes(&f,&first,1);
			primary[1] = alternate[1] = first; CHECK(igs->bitmap_errors == 1);
			local_ms = igs->bitmap_deadline; CHECK(st_cterm_poll(f.terminal) == 0 && igs->bitmap_errors == 2);
			primary[2] = alternate[2] = '*';
			local_ms += 3000; const uint8_t second = 0x88; write_bytes(&f,&second,1);
			primary[3] = alternate[3] = second; CHECK(igs->bitmap_errors == 2);
			local_ms = igs->bitmap_deadline; CHECK(st_cterm_poll(f.terminal) == 0 && !igs->bitmap_loading);
			primary[4] = alternate[4] = '*';
			CHECK(igs->bitmap_errors == 0 && igs->bitmap_deadline == 0 && igs->input_deadline == 0);
			check_image(&f,primary,alternate); check_descriptor(&f,1); check_screen(&f,primary,alternate,1,true);
			prepare_image(&f,primary,alternate); seed(&f,&full,769,768);
			feed(&f,"G#X 11,2,1:"); local_ms = igs->bitmap_deadline;
			const uint8_t late[] = {0xa5,0x5a}; write_bytes(&f,late,sizeof(late));
			primary[0] = alternate[0] = '*'; primary[1] = alternate[1] = 0x5a;
			CHECK(igs->bitmap_errors == 1 && igs->bitmap_received == 2 && igs->bitmap_loading);
			CHECK(st_cterm_finish(f.terminal) == 0 && igs->bitmap_errors == 0);
			check_image(&f,primary,alternate); check_screen(&f,primary,alternate,1,false);
			igs->bitmap_errors = 2; igs->math[0] = -1; igs->now_ms = bad_clock;
			feed(&f,"G#X 11,0,0],49:"); CHECK(igs->bitmap_errors == 2);
			feed(&f,"G#X 11,9,0:"); CHECK(igs->bitmap_errors == 2);
			feed(&f,"G#X 11,1,0]:"); CHECK(igs->bitmap_errors == 0 && !igs->bitmap_loading);
			igs->now_ms = now; igs->bitmap_errors = 3;
			feed(&f,"G#X 11,2,2:"); const uint8_t abort[] = {0x55,'A'}; write_bytes(&f,abort,sizeof(abort));
			primary[4000] = alternate[4000] = 0x55;
			CHECK(!igs->bitmap_loading && igs->bitmap_errors == 0 && f.terminal->xpos == 2);
			struct st_console_cell cell; const struct st_console_rect one = {0,0,1,1};
			CHECK(st_console_read_cells(f.console,&one,&cell,1) == 0 && cell.glyph == 'A');
			check_image(&f,primary,alternate); check_descriptor(&f,2); destroy(&f);
		}
}

static void
partitions_and_random(void)
{
	struct fixture f; create(&f,0,false); f.terminal->response_cb = no_response;
	struct cterm_igs *igs = f.terminal->igs_engine;
	uint8_t wire[4100], primary[32000], alternate[32000];
	for (unsigned random = 0; random < 2; ++random) {
		const char *header = random ? "G#X>r,R,4:" : "G#X>11,2,4:";
		size_t prefix = strlen(header), size = prefix+4000+5;
		memcpy(wire,header,prefix);
		for (unsigned i = 0; i < 4000; ++i) wire[prefix+i] = (uint8_t)i;
		memcpy(wire+prefix+4000,"M 2:B",5);
		for (size_t trial = 0; trial <= prefix+42; ++trial) {
			/* Include raw word/band edges and the exact transition back to
			 * command chaining, independently of the header splits. */
			static const size_t edges[] = {999,1000,1001,3997,3998,3999,4000,4001,4004};
			size_t split = trial <= prefix+33 ? trial : prefix+edges[trial-prefix-34];
			CHECK(st_cterm_reset_terminal(f.terminal) == 0); prepare_image(&f,primary,alternate);
			igs->random_seed = random ? 178 : 1;
			igs->random_range[0][1] = 255; igs->random_range[1][0] = 1; igs->random_range[1][1] = 2;
			if (split == prefix+33) {
				for (size_t i = 0; i < size; ++i) write_bytes(&f,wire+i,1);
			} else { write_bytes(&f,wire,split); write_bytes(&f,wire+split,size-split); }
			memcpy(primary+12000,wire+prefix,4000); memcpy(alternate+12000,wire+prefix,4000);
			check_image(&f,primary,alternate); check_descriptor(&f,4);
			CHECK(igs->random_seed == (random ? UINT32_C(0x46d402ef) : 1));
			CHECK(!igs->bitmap_loading && igs->attributes.writing_mode == 2 && f.terminal->xpos == 2);
		}
	}
	CHECK(st_cterm_reset_terminal(f.terminal) == 0); prepare_image(&f,primary,alternate);
	feed(&f,"G#X>2,0,256:X>2,256,256,256:X 11,0,1,r:");
	CHECK(igs->random_seed == UINT32_C(0xb60774e5));
	uint8_t output[8]; CHECK(st_igs_image_export(igs->memory[0],0,output,sizeof(output)) == 0);
	CHECK(memcmp(output,((uint8_t[]){149,78,194,106,239,175,216,18}),sizeof(output)) == 0);
	CHECK(st_cterm_reset_terminal(f.terminal) == 0); prepare_image(&f,primary,alternate);
	igs->random_seed = 178; igs->random_range[0][1] = 255; igs->random_range[1][0] = 1; igs->random_range[1][1] = 2;
	feed(&f,"G#X r,R,4:"); CHECK(igs->random_seed == UINT32_C(0x46d402ef));
	CHECK(st_cterm_finish(f.terminal) == 0 && igs->random_seed == UINT32_C(0x46d402ef));
	check_image(&f,primary,alternate);
	destroy(&f);
}

static void
header_recovery(void)
{
	static const char *const partial[] = {"G#X 11", "G#X 11,", "G#X 11,0,",
	    "G#X 11,0,4,", "G#X 11,0,4,256", "G#X 11,2,", "G#X 11,2,4", "G#X r,R,"};
	struct fixture f; create(&f,0,false);
	struct cterm_igs *igs = f.terminal->igs_engine;
	uint8_t primary[32000], alternate[32000]; prepare_image(&f,primary,alternate);
	feed(&f,"G#X 11,9,4:");
	igs->random_seed = 178; igs->random_range[0][1] = 255;
	igs->random_range[1][0] = 1; igs->random_range[1][1] = 2;
	for (size_t i = 0; i < sizeof(partial)/sizeof(*partial); ++i) {
		feed(&f,partial[i]); CHECK(st_cterm_finish(f.terminal) == 0);
		CHECK(!igs->bitmap_loading && igs->random_seed == 178); check_descriptor(&f,4);
		feed(&f,partial[i]); local_ms += 5001; CHECK(st_cterm_poll(f.terminal) == 0);
		CHECK(!igs->bitmap_loading && igs->random_seed == 178); check_descriptor(&f,4);
	}
	const char invalid[] = "G#X 11,0,4,10000:"; size_t used;
	CHECK(st_cterm_write(f.terminal,invalid,sizeof(invalid)-1,&used) == IO_ERROR_OUT_OF_RANGE);
	CHECK(used == sizeof(invalid)-1 && !igs->bitmap_loading && igs->random_seed == 178);
	check_image(&f,primary,alternate); check_descriptor(&f,4);
	feed(&f,"G#X 11,0,1,0:"); CHECK(igs->memory_rect[0][1] == 0);
	destroy(&f);
}

static void
errors_and_layout(void)
{
	struct fixture f; create(&f,0,false);
	struct cterm_igs *igs = f.terminal->igs_engine;
	uint8_t primary[32000], alternate[32000], payload[32000]; prepare_image(&f,primary,alternate);
	for (unsigned i = 0; i < sizeof(payload); ++i) payload[i] = (uint8_t)i;
	feed(&f,"G#X 11,2,1:"); write_bytes(&f,payload,17);
	memcpy(primary,payload,17); memcpy(alternate,payload,17);
	igs->now_ms = bad_clock; CHECK(st_cterm_poll(f.terminal) == IO_ERROR_IO && igs->bitmap_loading && igs->bitmap_received == 17);
	igs->now_ms = now; CHECK(st_cterm_finish(f.terminal) == 0); check_image(&f,primary,alternate);
	feed(&f,"G#X 11,2,1:"); igs->now_ms = bad_clock; size_t used;
	CHECK(st_cterm_write(f.terminal,payload,1,&used) == IO_ERROR_IO && used == 0 && !igs->bitmap_loading);
	igs->now_ms = now; check_image(&f,primary,alternate);
	igs->now_ms = bad_clock; const char header[] = "G#X 11,2,1:";
	CHECK(st_cterm_write(f.terminal,header,sizeof(header)-1,&used) == IO_ERROR_IO && used == sizeof(header)-1 && !igs->bitmap_loading);
	igs->now_ms = now;
	igs->memory[0]->pixels->primary[0] = ST_PIXEL_COLOR_RGB_TAG;
	feed(&f,header); CHECK(st_cterm_write(f.terminal,payload,1,&used) == IO_ERROR_INVALID_FORMAT && used == 0);
	CHECK(!igs->bitmap_loading && igs->memory[0]->pixels->primary[0] == ST_PIXEL_COLOR_RGB_TAG);
	prepare_image(&f,primary,alternate);
	const struct st_console_rect full = {0,0,igs->width,igs->height}; seed(&f,&full,769,768);
	igs->memory[0]->pixels->primary[32] = ST_PIXEL_COLOR_RGB_TAG;
	feed(&f,"G#X 11,1,1:"); write_bytes(&f,payload,1);
	CHECK(st_cterm_finish(f.terminal) == IO_ERROR_INVALID_FORMAT && !igs->bitmap_loading);
	check_screen(&f,primary,alternate,1,false);
	prepare_image(&f,primary,alternate);
	feed(&f,"G#X 11,2,0:"); write_bytes(&f,payload,16);
	memcpy(primary,payload,16); memcpy(alternate,payload,16);
	CHECK(cterm_igs_set_resolution(f.terminal,1) == 0 && igs->bitmap_loading && igs->bitmap_received == 16);
	check_image(&f,primary,alternate);
	write_bytes(&f,payload+16,sizeof(payload)-16);
	check_image(&f,payload,payload); check_descriptor(&f,0);
	CHECK(igs->resolution == 1 && igs->memory[0]->planes == 2);
	feed(&f,"G#X 11,1,1:"); write_bytes(&f,payload,3);
	CHECK(st_cterm_reset_terminal(f.terminal) == 0 && !igs->bitmap_loading && igs->memory[0] == NULL && igs->bitmap_errors == 0);
	feed(&f,"G#X 11,0,0,0:"); CHECK(igs->memory[0] != NULL);
	destroy(&f);
}

int
st_cterm_igs_bitmap_test_main(void)
{
	literal(); wipes(); loads_and_eof(); timeouts(); partitions_and_random(); header_recovery(); errors_and_layout(); return 0;
}
