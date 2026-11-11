/* X dispatch and intended X10 position semantics. Ref 19/22, IG221.TXT.
 * Correct the original 0/1 coordinate classes to the documented X/Y canvas. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "igs_test_fixture.h"

static void
position(const struct cterm_igs *igs, int x1, int y1, int x2, int y2)
{
	CHECK(igs->draw_start_x == x1 && igs->draw_start_y == y1 && igs->draw_x == x2 && igs->draw_y == y2);
}

static void
warning(void *context, enum st_cterm_log_level level, const char *message)
{
	unsigned *count = context;
	CHECK(level == ST_CTERM_LOG_WARNING && message[0] != '\0');
	++*count;
}

static void
rejected_input(void)
{
	/* Rejected graphics must not prevent the following VT52 parser, or a
	 * subsequent graphics command, from receiving its part of the stream. */
	const char *wires[] = {"G#X>5,0:\x1bHOKG#X>10,17,23:",
	    "G#P>10000,1:\x1bHOKG#X>10,17,23:", "G#X>10,+2,3:\x1bHOKG#X>10,17,23:"};
	for (size_t w = 0; w < sizeof(wires)/sizeof(*wires); ++w)
	for (size_t split = 0; split <= strlen(wires[w])+1; ++split) {
		const char *wire = wires[w];
		struct fixture f; create(&f,0,false);
		unsigned warnings = 0;
		f.terminal->api_log_level = ST_CTERM_LOG_WARNING;
		f.terminal->api_diagnostic = warning;
		f.terminal->api_diagnostic_context = &warnings;
		size_t used, size = strlen(wire);
		if (split == size+1) {
			for (size_t i = 0; i < size; ++i)
				CHECK(st_cterm_write(f.terminal,wire+i,1,&used) == 0 && used == 1);
		} else {
			CHECK(st_cterm_write(f.terminal,wire,split,&used) == 0 && used == split);
			CHECK(st_cterm_write(f.terminal,wire+split,size-split,&used) == 0 && used == size-split);
		}
		struct st_console_cell cells[2];
		const struct st_console_rect area = {0,0,2,1};
		CHECK(st_console_read_cells(f.console,&area,cells,2) == 0);
		CHECK(cells[0].glyph == 'O' && cells[1].glyph == 'K');
		/* X5 may gain an implementation; malformed operands must always warn. */
		if (w != 0) CHECK(warnings != 0);
		position(f.terminal->igs_engine,0,0,17,23);
		destroy(&f);
	}
}

static void
coordinates(void)
{
	const int values[] = {0,1,100,5000,9999};
	const int normalized_x[2][5] = {{0,0,3,160,319},{0,0,6,320,639}};
	const int normalized_y[2][5] = {{0,0,2,100,199},{0,0,4,200,399}};
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f,resolution,false);
		struct cterm_igs *igs = f.terminal->igs_engine;
		const struct st_console_rect full = {0,0,igs->width,igs->height};
		seed(&f,&full,769,768);
		igs->draw_start_x = 17; igs->draw_start_y = 23;
		igs->clipping = false; igs->last_line = false;
		igs->clip_x1 = 10; igs->clip_y1 = 20; igs->clip_x2 = 50; igs->clip_y2 = 60;
		igs->memory_route = 5; igs->double_step = 4; igs->patterns[3][7] = 0xbeef;
		igs->attributes.line_width = 5; igs->attributes.fill_interior = 4;
		struct cterm_igs_attributes attributes; memcpy(&attributes,&igs->attributes,sizeof(attributes));
		uint32_t palette[16]; memcpy(palette,igs->palette,sizeof(palette));
		for (unsigned scaling = 0; scaling < 3; ++scaling) {
			char wire[80]; (void)snprintf(wire,sizeof(wire),"G#g %u:",scaling); feed(&f,wire);
			for (unsigned x = 0; x < 5; ++x) for (unsigned y = 0; y < 5; ++y) {
				(void)snprintf(wire,sizeof(wire),"G#X 10,%d,%d:",values[x],values[y]); feed(&f,wire);
				int expected_x = scaling == 1 ? normalized_x[resolution != 0][x] : values[x];
				int expected_y = scaling == 1 ? normalized_y[resolution == 2][y]
				    : scaling == 2 && resolution == 2 ? values[y]*2 : values[y];
				position(igs,17,23,expected_x,expected_y);
				CHECK(memcmp(&attributes,&igs->attributes,sizeof(attributes)) == 0);
				CHECK(memcmp(palette,igs->palette,sizeof(palette)) == 0);
				CHECK(!igs->clipping && !igs->last_line && igs->clip_x1 == 10 && igs->clip_y1 == 20
				    && igs->clip_x2 == 50 && igs->clip_y2 == 60);
				CHECK(igs->memory_route == 5 && igs->double_step == 4 && igs->patterns[3][7] == 0xbeef);
				CHECK(igs->scaling_mode == (int)scaling && igs->scaling_resolution == (int)resolution
				    && igs->random_seed == 1 && igs->pattern_seed == 0);
			}
		}
		struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f.console,&full,false) == 0);
		for (size_t i = 0; i < (size_t)full.width*full.height; ++i)
			CHECK(b->primary[i] == 769 && b->alternate[i] == 768);
		st_pixel_buffer_destroy(b); destroy(&f);
	}
}

static void
dispatch_and_arrays(void)
{
	const int invalid[] = {INT16_MIN,-1,16,9999,INT16_MAX};
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f,resolution,false);
		struct cterm_igs *igs = f.terminal->igs_engine;
		feed(&f,"G#M>4:L 10,20,30,40:");
		for (unsigned g = 0; g < 3; ++g) {
			char wire[80]; (void)snprintf(wire,sizeof(wire),"G#g %u:",g); feed(&f,wire);
			for (unsigned i = 0; i < sizeof(invalid)/sizeof(*invalid); ++i) {
				igs->math[0] = (int16_t)invalid[i];
				feed(&f,"G#X 0]:"); position(igs,10,20,30,40);
			}
			/* X9 remains pending. Rejection cancels the graphics chain, so
			 * the following M remains ordinary input, not a graphics command. */
			const unsigned pending[] = {9};
			for (size_t s = 0; s < sizeof(pending)/sizeof(*pending); ++s) {
				unsigned selector = pending[s];
				int prefix = snprintf(wire,sizeof(wire),"G#X>%u,",selector);
				CHECK(prefix > 0 && (size_t)prefix+sizeof("1234:M 1:") <= sizeof(wire));
				memcpy(wire+prefix,"1234:M 1:",sizeof("1234:M 1:"));
				size_t used;
				CHECK(st_cterm_write(f.terminal,wire,strlen(wire),&used) == 0);
				CHECK(used == strlen(wire) && igs->attributes.writing_mode == 4);
				position(igs,10,20,30,40);
			}
		}
		for (unsigned i = 0; i < 128; ++i) igs->math[i] = (int16_t)(1000+i);
		igs->math[0] = -300; igs->math[1] = -123; igs->math[2] = -456; igs->math[127] = -2222;
		feed(&f,"G#g>1:X 10,1],5000]:"); position(igs,10,20,-300,resolution == 2 ? -2222 : 1100);
		feed(&f,"G#g>2:X 10,1],2]:"); position(igs,10,20,-123,resolution == 2 ? 1004 : -456);
		feed(&f,"G#g>0:X 10,0],127]:"); position(igs,10,20,-300,-2222);
		CHECK(st_cterm_reset_terminal(f.terminal) == 0);
		feed(&f,"AG#X 16,7,8:B");
		const struct st_console_rect text = {0,0,6,1}; struct st_console_cell cells[6];
		CHECK(st_console_read_cells(f.console,&text,cells,6) == 0);
		const char expected[] = "A7,8:B";
		for (unsigned i = 0; i < 6; ++i) CHECK(cells[i].glyph == (unsigned char)expected[i]);
		position(igs,0,0,0,0); destroy(&f);
	}
}

static void
partitions_and_recovery(void)
{
	const char *wires[] = {"AG#L>10,20,30,40:X>16:X>10,40,60:D 60,60:B",
	    "AG#X>r,100,5000:M 4:B", "AG#X>r:M 4:B", "AG#X>r,r,R:M 4:B"};
	for (unsigned resolution = 0; resolution < 3; ++resolution)
		for (unsigned w = 0; w < 4; ++w)
			for (size_t split = 0; split <= strlen(wires[w])+1; ++split) {
				struct fixture f; create(&f,resolution,false); size_t used, size = strlen(wires[w]);
				struct cterm_igs *igs = f.terminal->igs_engine;
				if (w != 0) feed(&f,"G#g 1:");
				igs->random_seed = w == 2 ? UINT32_C(0x7c1bf65b) : UINT32_C(0x8602185b);
				igs->random_range[0][1] = igs->random_range[1][1] = 255;
				if (split == size+1) {
					for (size_t i = 0; i < size; ++i)
						CHECK(st_cterm_write(f.terminal,wires[w]+i,1,&used) == 0 && used == 1);
				} else {
					CHECK(st_cterm_write(f.terminal,wires[w],split,&used) == 0 && used == split);
					CHECK(st_cterm_write(f.terminal,wires[w]+split,size-split,&used) == 0 && used == size-split);
				}
				CHECK(st_cterm_finish(f.terminal) == 0);
				if (w == 0) {
					position(igs,60,60,60,60);
					const struct st_console_rect area = {59,59,3,3}; struct st_pixel_buffer *b = NULL;
					CHECK(st_console_get_pixels(&b,f.console,&area,false) == 0);
					for (unsigned y = 0; y < 3; ++y) for (unsigned x = 0; x < 3; ++x)
						CHECK(b->primary[y*3+x] == 768u+(y == 1 && x <= 1 ? cterm_igs_plane_mask(resolution) : 0));
					st_pixel_buffer_destroy(b);
				} else if (w == 1) {
					position(igs,0,0,resolution == 0 ? 3 : 6,resolution == 2 ? 200 : 100);
					CHECK(igs->random_seed == UINT32_C(0xa00));
				} else if (w == 2) {
					position(igs,0,0,0,0); CHECK(igs->random_seed == UINT32_C(0x1000));
				} else {
					position(igs,0,0,194,0); CHECK(igs->random_seed == UINT32_C(0x4c28002e));
				}
				struct st_console_cell cells[2]; const struct st_console_rect text = {0,0,2,1};
				CHECK(st_console_read_cells(f.console,&text,cells,2) == 0 && cells[0].glyph == 'A' && cells[1].glyph == 'B');
				int sx = igs->draw_start_x, sy = igs->draw_start_y, ex = igs->draw_x, ey = igs->draw_y;
				const char *partial[] = {"G#X", "G#X 10,", "G#X 10,1,", "G#X 10,1,2"};
				for (unsigned p = 0; p < 4; ++p) {
					feed(&f,partial[p]); CHECK(st_cterm_finish(f.terminal) == 0); position(igs,sx,sy,ex,ey);
					feed(&f,partial[p]); local_ms += 5001; CHECK(st_cterm_poll(f.terminal) == 0); position(igs,sx,sy,ex,ey);
				}
				const char bad[] = "G#X 10,1,10000:";
				CHECK(st_cterm_write(f.terminal,bad,sizeof(bad)-1,&used) == 0);
				position(igs,sx,sy,ex,ey);
				igs->random_seed = UINT32_C(0x8602185b);
				feed(&f,"G#X r,1,"); CHECK(st_cterm_finish(f.terminal) == 0);
				CHECK(igs->random_seed == UINT32_C(0x8602185b)); position(igs,sx,sy,ex,ey);
				igs->random_seed = UINT32_C(0x07a8735b);
				const char unsupported[] = "G#X r:";
				CHECK(st_cterm_write(f.terminal,unsupported,sizeof(unsupported)-1,&used) == 0);
				CHECK(igs->random_seed == UINT32_C(0x900)); position(igs,sx,sy,ex,ey);
				feed(&f,"G#X 10,10,20:");
				feed(&f,"G#X 10,1,"); CHECK(st_cterm_reset_terminal(f.terminal) == 0); position(igs,0,0,0,0);
				feed(&f,"G#X 10,10,20:"); position(igs,0,0,10,20);
				destroy(&f);
			}
}

static void
lifetime(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution)
		for (unsigned status = 0; status < 2; ++status) {
			struct fixture f; create(&f,resolution,status != 0);
			struct cterm_igs *igs = f.terminal->igs_engine;
			feed(&f,"G#M>4:L>10,20,30,40:X 10,50,60:"); position(igs,10,20,50,60);
			feed(&f,"G#I>0:I>1:I 2:"); position(igs,10,20,50,60);
			char wire[96]; (void)snprintf(wire,sizeof(wire),"G#g>1:R>%u,0:X 10,5000,5000:",resolution == 0 ? 1 : 0);
			size_t used, size = strlen(wire); CHECK(st_cterm_write(f.terminal,wire,size,&used) == 0);
			if (resolution != 2) {
				CHECK(used < size && st_cterm_input_paused(f.terminal)); position(igs,10,20,50,60);
				local_ms += 17; CHECK(st_cterm_poll(f.terminal) == 0); position(igs,10,20,50,60);
				feed(&f,wire+used);
			} else CHECK(used == size);
			int x = resolution == 0 ? 160 : 320, y = resolution == 2 ? 200 : 100;
			position(igs,10,20,x,y);
			feed(&f,"G#I 5:"); local_ms += 17; CHECK(st_cterm_poll(f.terminal) == 0); position(igs,10,20,x,y);
			CHECK(igs->scaling_mode == 1 && igs->scaling_resolution == (int)resolution);
			feed(&f,"G#g 0:"); igs->math[0] = INT16_MIN; igs->math[1] = INT16_MAX;
			feed(&f,"G#X 10,0],1]:"); position(igs,10,20,INT16_MIN,INT16_MAX);
			destroy(&f);
		}
}

int st_cterm_igs_x10_test_main(void)
{
	rejected_input(); coordinates(); dispatch_and_arrays(); partitions_and_recovery(); lifetime(); return 0;
}
