/* X13: bounded signed-word arithmetic and numeric text ownership. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "igs_test_fixture.h"

static void
arithmetic(struct fixture *f, int operation, int a, int b)
{
	struct cterm_igs *igs = f->terminal->igs_engine;
	igs->math[0] = (int16_t)a; igs->math[1] = (int16_t)b;
	int64_t expected = a;
	switch (operation) {
	case 1: expected += b; break;
	case 2: expected -= b; break;
	case 3: expected *= b; break;
	case 4: expected /= b; break;
	case 5: expected %= b; break;
	}
	/* Mathematical residue, independent of host narrowing conversions. */
	expected %= 65536;
	if (expected < -32768) expected += 65536;
	if (expected > 32767) expected -= 65536;
	char wire[64];
	(void)snprintf(wire, sizeof(wire), operation == 0 ? "G#X 13,0,0],127:"
	    : "G#X 13,%d,0],1],127:", operation);
	feed(f, wire);
	CHECK(igs->math[127] == expected && igs->math[0] == a && igs->math[1] == b);
}

static void
matrix(void)
{
	static const int words[] = {INT16_MIN, -10000, -257, -7, -1, 0, 1, 3, 255, 10000, INT16_MAX};
	for (unsigned resolution = 0; resolution < 3; ++resolution)
		for (unsigned status = 0; status < 2; ++status) {
			struct fixture f; create(&f, resolution, status != 0);
			struct cterm_igs *igs = f.terminal->igs_engine;
			feed(&f, "\033fG#p>3,2:X 10,19,17:\033b");
			struct cterm_igs_attributes attributes; memcpy(&attributes, &igs->attributes, sizeof(attributes));
			const struct st_console_rect full = {0, 0, igs->width, igs->height};
			seed(&f, &full, 769, 768);
			for (unsigned scaling = 0; scaling < 3; ++scaling) {
				char wire[16]; (void)snprintf(wire, sizeof(wire), "G#g %u:", scaling); feed(&f, wire);
				for (int op = 0; op < 6; ++op)
					for (size_t a = 0; a < sizeof(words)/sizeof(*words); ++a)
						for (size_t b = 0; b < sizeof(words)/sizeof(*words); ++b)
							if (op < 4 || words[b] != 0) arithmetic(&f, op, words[a], words[b]);
			}
			if (resolution == 0 && !status)
				for (int a = INT16_MIN; a <= INT16_MAX; ++a)
					for (int op = 0; op < 6; ++op) arithmetic(&f, op, a, -1);
			CHECK(memcmp(&attributes, &igs->attributes, sizeof(attributes)) == 0);
			CHECK(igs->draw_x == 19 && igs->draw_y == 17 && f.terminal->seq_trailing_remain == 1);
			CHECK(f.terminal->xpos == 4 && f.terminal->ypos == 3 && igs->random_seed == 1);
			struct st_pixel_buffer *pixels = NULL;
			CHECK(st_console_get_pixels(&pixels, f.console, &full, false) == 0);
			for (size_t i = 0; i < (size_t)igs->width * igs->height; ++i)
				CHECK(pixels->primary[i] == 769 && pixels->alternate[i] == 768);
			st_pixel_buffer_destroy(pixels); feed(&f, "\001"); destroy(&f);
		}
}

static void
bounds_and_clear(void)
{
	struct fixture f; create(&f, 0, false);
	struct cterm_igs *igs = f.terminal->igs_engine;
	for (int index = INT16_MIN; index <= INT16_MAX; ++index) {
		igs->math[0] = (int16_t)index;
		const char wire[] = "G#X 13,0,37,0]:"; size_t used;
		int status = st_cterm_write(f.terminal, wire, sizeof(wire)-1, &used);
		CHECK(status == (index >= 0 && index < 128 ? 0 : IO_ERROR_OUT_OF_RANGE));
		CHECK(used == sizeof(wire)-1);
		if (status == 0) CHECK(igs->math[index] == 37);
	}
	static const int ranges[][2] = {{0,127},{3,7},{7,3},{127,127},{128,127},{32767,32767},
	    {0,32767},{0,-1},{127,128},{-1,7},{INT16_MIN,127}};
	for (size_t r = 0; r < sizeof(ranges)/sizeof(*ranges); ++r) {
		for (unsigned i = 0; i < 128; ++i) igs->math[i] = 37;
		igs->math[0] = (int16_t)ranges[r][0]; igs->math[1] = (int16_t)ranges[r][1];
		int16_t saved[128]; memcpy(saved, igs->math, sizeof(saved));
		const char wire[] = "G#X 13,6,0],1]:"; size_t used;
		int status = st_cterm_write(f.terminal, wire, sizeof(wire)-1, &used);
		CHECK(status == (ranges[r][0] < 0 ? IO_ERROR_OUT_OF_RANGE : 0));
		for (unsigned i = 0; i < 128; ++i) {
			bool cleared = ranges[r][0] >= 0 && (int)i >= (ranges[r][0] > 127 ? 127 : ranges[r][0])
			    && (int)i <= ranges[r][1];
			CHECK(igs->math[i] == (cleared ? 0 : saved[i]));
		}
	}
	for (int op = 4; op <= 5; ++op) {
		char wire[40]; (void)snprintf(wire,sizeof(wire),"G#X 13,%d,7,0,127:",op);
		igs->math[127] = 123; size_t used;
		CHECK(st_cterm_write(f.terminal,wire,strlen(wire),&used) == IO_ERROR_OUT_OF_RANGE);
		CHECK(used == strlen(wire) && igs->math[127] == 123);
	}
	feed(&f,"G#X>13,8:M>2:X>13,7,2,9999:M 3:");
	CHECK(igs->attributes.writing_mode == 3);
	destroy(&f);
}

static void
no_response(const char *bytes, size_t size, void *context)
{
	(void)bytes; (void)size; (void)context; CHECK(false);
}

static void
numeric_text(void)
{
	static const uint8_t zero[8] = {0x3c,0x66,0x6e,0x76,0x66,0x66,0x3c,0};
	for (unsigned resolution = 0; resolution < 3; ++resolution)
		for (unsigned status = 0; status < 2; ++status) {
			struct fixture f; create(&f,resolution,status != 0);
			f.terminal->response_cb = no_response;
			struct cterm_igs *igs = f.terminal->igs_engine;
			feed(&f,"\033fG#M>4:W>40,60,XXXXXXXXXXXX@M>1:E>0,9,0:X 10,19,17:");
			igs->math[0] = INT16_MIN;
			feed(&f,"G#X 13,7,0,0:");
			struct st_console_cell cells[8]; const struct st_console_rect line = {0,0,8,1};
			CHECK(st_console_read_cells(f.console,&line,cells,8) == 0);
			for (unsigned i = 0; i < 6; ++i) CHECK(cells[i].glyph == (unsigned char)"-32768"[i]);
			CHECK(f.terminal->xpos == 1 && f.terminal->ypos == 2);
			CHECK(igs->shared_text_size == 12 && igs->shared_text[0] == 'X');
			feed(&f,"\033H\033b"); igs->math[0] = 37;
			feed(&f,"G#X 13,7,0,0:");
			CHECK(f.terminal->seq_trailing_remain == 0 && f.terminal->vt52_foreground == 19);
			CHECK(st_console_read_cells(f.console,&line,cells,8) == 0 && cells[0].glyph == '7');
			feed(&f,"\033HG#c 1,15:");
			const struct st_console_rect area = {40,54,8,8}; seed(&f,&area,769,768);
			igs->math[0] = 0; igs->loop_mode = 1; igs->loop_kind = '@';
			feed(&f,"G#X 13,7,1,0,");
			CHECK(igs->math_printing && igs->shared_text_size == 1 && igs->shared_text[0] == '0');
			CHECK(igs->shared_text[1] == 0 && igs->shared_text[2] == 'X');
			CHECK(igs->text_counter == 0 && igs->text_deadline == 0 && igs->input_deadline == local_ms+5000);
			struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f.console,&area,false) == 0);
			for (unsigned i = 0; i < 64; ++i) CHECK(b->primary[i] == 769 && b->alternate[i] == 768);
			st_pixel_buffer_destroy(b);
			feed(&f,"40,60:");
			CHECK(!igs->math_printing && igs->draw_x == 19 && igs->draw_y == 17);
			CHECK(st_console_get_pixels(&b,f.console,&area,false) == 0);
			for (unsigned y = 0; y < 8; ++y) for (unsigned x = 0; x < 8; ++x) {
				unsigned expected = 768u + (zero[y] & (128u >> x) ? cterm_igs_plane_mask(resolution) : 0u);
				CHECK(b->primary[y*8+x] == expected && b->alternate[y*8+x] == expected);
			}
			st_pixel_buffer_destroy(b);
			igs->loop_kind = 'W';
			feed(&f,"G#W 40,60:");
			CHECK(igs->shared_text_size == 1 && igs->shared_text[2] == 'X' && igs->text_counter == 0);
			feed(&f,"G#X 7,2,");
			for (unsigned row = 0; row < 16; ++row) CHECK(igs->patterns[2][row] == 0x3ff0);
			for (unsigned stop = 0; stop < 3; ++stop) {
				igs->math[0] = -7;
				feed(&f,"G#X 13,7,1,0,40,");
				CHECK(igs->math_printing && memcmp(igs->shared_text,"-7\0X",4) == 0);
				if (stop == 0) CHECK(st_cterm_finish(f.terminal) == 0);
				else if (stop == 1) { local_ms += 5001; CHECK(st_cterm_poll(f.terminal) == 0); }
				else CHECK(st_cterm_reset_terminal(f.terminal) == 0);
				CHECK(!igs->math_printing && igs->input_deadline == 0);
				CHECK(igs->shared_text_size == (stop == 2 ? 0u : 2u));
			}
			destroy(&f);
		}
}

static void
partitions(void)
{
	static const char *wires[] = {"AG#X>13,0,37,0:M 2:B", "AG#X>13,1,0],7,127:M 2:B",
	    "AG#X>13,6,2,127:M 2:B", "AG#X>13,7,1,0,40,60:M 2:B",
	    "AG#X>13,7,2,9999:M 2:B", "AG#X>13,9999:M 2:B"};
	struct fixture f; create(&f,0,false);
	struct cterm_igs *igs = f.terminal->igs_engine;
	for (size_t w = 0; w < sizeof(wires)/sizeof(*wires); ++w) {
		size_t size = strlen(wires[w]);
		for (size_t split = 0; split <= size+1; ++split) {
			CHECK(st_cterm_reset_terminal(f.terminal) == 0);
			igs->math[0] = -7; igs->math[127] = 123;
			size_t used;
			if (split > size) {
				for (size_t i = 0; i < size; ++i)
					CHECK(st_cterm_write(f.terminal,wires[w]+i,1,&used) == 0 && used == 1);
			} else {
				CHECK(st_cterm_write(f.terminal,wires[w],split,&used) == 0 && used == split);
				CHECK(st_cterm_write(f.terminal,wires[w]+split,size-split,&used) == 0 && used == size-split);
			}
			CHECK(igs->attributes.writing_mode == 2 && f.terminal->xpos == 3);
			CHECK(!igs->math_printing && igs->text_deadline == 0 && igs->input_deadline == 0);
			CHECK(igs->math[0] == (w == 0 ? 37 : -7));
			CHECK(igs->math[127] == (w == 1 || w == 2 ? 0 : 123));
			if (w == 3) CHECK(igs->shared_text_size == 2 && memcmp(igs->shared_text,"-7",3) == 0);
		}
	}
	destroy(&f);
}

static int
bad_clock(uint64_t *value)
{
	(void)value; return IO_ERROR_IO;
}

static void
console_partitions(void)
{
	const char wire[] = "G#X>13,7,0,0:M 2:B";
	struct fixture f; create(&f,0,false);
	for (size_t split = 0; split <= sizeof(wire); ++split) {
		CHECK(st_cterm_reset_terminal(f.terminal) == 0);
		f.terminal->igs_engine->math[0] = INT16_MAX;
		size_t used, size = sizeof(wire)-1;
		if (split > size) {
			for (size_t i = 0; i < size; ++i)
				CHECK(st_cterm_write(f.terminal,wire+i,1,&used) == 0 && used == 1);
		} else {
			CHECK(st_cterm_write(f.terminal,wire,split,&used) == 0 && used == split);
			CHECK(st_cterm_write(f.terminal,wire+split,size-split,&used) == 0 && used == size-split);
		}
		struct st_console_cell cells[6]; const struct st_console_rect line = {0,0,6,1};
		CHECK(st_console_read_cells(f.console,&line,cells,6) == 0);
		for (unsigned i = 0; i < 5; ++i) CHECK(cells[i].glyph == (unsigned char)"32767"[i]);
		const struct st_console_rect second = {0,1,1,1};
		CHECK(st_console_read_cells(f.console,&second,cells,1) == 0 && cells[0].glyph == 'B');
		CHECK(f.terminal->xpos == 2 && f.terminal->ypos == 2);
		CHECK(f.terminal->igs_engine->attributes.writing_mode == 2);
	}
	feed(&f,"\033vG#p 39,24:");
	feed(&f,"G#X 13,7,0,0:");
	CHECK(f.terminal->xpos == 1 && f.terminal->ypos == 25);
	struct st_console_cell cells[4]; const struct st_console_rect end = {0,23,4,1};
	CHECK(st_console_read_cells(f.console,&end,cells,4) == 0);
	for (unsigned i = 0; i < 4; ++i) CHECK(cells[i].glyph == (unsigned char)"2767"[i]);
	destroy(&f);
}

static void
prefix_and_recovery(void)
{
	static const char *wires[] = {"AG#X>r,7,1,r,r,R:M 2:B", "AG#X>13,r,R,0,r,R:M 2:B"};
	struct fixture f; create(&f,0,false);
	struct cterm_igs *igs = f.terminal->igs_engine;
	f.terminal->response_cb = no_response;
	for (unsigned w = 0; w < 2; ++w) {
		size_t size = strlen(wires[w]);
		for (size_t split = 0; split <= size+1; ++split) {
			CHECK(st_cterm_reset_terminal(f.terminal) == 0);
			igs->math[119] = -123; igs->math[0] = 37;
			igs->random_seed = w == 0 ? 168 : 1;
			igs->random_range[0][0] = w == 0 ? 0 : 7;
			igs->random_range[0][1] = w == 0 ? 255 : 7;
			igs->random_range[1][0] = w == 0 ? 0 : 1;
			igs->random_range[1][1] = w == 0 ? 255 : 1;
			size_t used;
			if (split > size) {
				for (size_t i = 0; i < size; ++i)
					CHECK(st_cterm_write(f.terminal,wires[w]+i,1,&used) == 0 && used == 1);
			} else {
				CHECK(st_cterm_write(f.terminal,wires[w],split,&used) == 0 && used == split);
				CHECK(st_cterm_write(f.terminal,wires[w]+split,size-split,&used) == 0 && used == size-split);
			}
			CHECK(igs->random_seed == (w == 0 ? UINT32_C(0x3623cb34) : UINT32_C(0x47ce0142)));
			CHECK(strcmp((const char *)igs->shared_text,w == 0 ? "-123" : "37") == 0);
			CHECK(igs->attributes.writing_mode == 2 && !igs->math_printing);
		}
	}
	CHECK(st_cterm_reset_terminal(f.terminal) == 0);
	igs->math[0] = -7;
	igs->now_ms = bad_clock;
	const char prefix[] = "G#X 13,7,1,0,"; size_t used;
	CHECK(st_cterm_write(f.terminal,prefix,sizeof(prefix)-1,&used) == IO_ERROR_IO);
	CHECK(used == sizeof(prefix)-1 && igs->math_printing && strcmp((const char *)igs->shared_text,"-7") == 0);
	igs->now_ms = now; feed(&f,"40,60:"); CHECK(!igs->math_printing);
	const char bad[] = "G#X 13,7,1,0,40,10000:";
	CHECK(st_cterm_write(f.terminal,bad,sizeof(bad)-1,&used) == IO_ERROR_OUT_OF_RANGE);
	CHECK(!igs->math_printing && igs->shared_text_size == 2);
	for (unsigned kind = 0; kind < 2; ++kind) {
		char wire[40]; (void)snprintf(wire,sizeof(wire),"G#X 13,7,%u,128,",kind);
		CHECK(st_cterm_write(f.terminal,wire,strlen(wire),&used) == IO_ERROR_OUT_OF_RANGE);
		CHECK(used == strlen(wire) && !igs->math_printing && igs->shared_text_size == 2);
	}
	static const char *partial[] = {"G#X 13,0,37,", "G#X 13,1,1,2,", "G#X 13,6,0,"};
	for (size_t i = 0; i < sizeof(partial)/sizeof(*partial); ++i) {
		int16_t saved[128]; memcpy(saved,igs->math,sizeof(saved));
		feed(&f,partial[i]); CHECK(st_cterm_finish(f.terminal) == 0);
		CHECK(memcmp(saved,igs->math,sizeof(saved)) == 0);
		feed(&f,partial[i]); local_ms += 5001; CHECK(st_cterm_poll(f.terminal) == 0);
		CHECK(memcmp(saved,igs->math,sizeof(saved)) == 0);
	}
	feed(&f,"G#X 13,0,37,127:");
	for (unsigned operation = 0; operation < 6; ++operation) {
		char wire[20]; (void)snprintf(wire,sizeof(wire),"G#I %u:",operation); feed(&f,wire);
		if (st_cterm_input_paused(f.terminal)) { local_ms = igs->wait_deadline; CHECK(st_cterm_poll(f.terminal) == 0); }
		CHECK(igs->math[127] == 37);
	}
	feed(&f,"G#R 1,0:");
	if (st_cterm_input_paused(f.terminal)) { local_ms = igs->wait_deadline; CHECK(st_cterm_poll(f.terminal) == 0); }
	CHECK(igs->math[127] == 37);
	CHECK(st_cterm_reset_terminal(f.terminal) == 0);
	for (unsigned i = 0; i < 128; ++i) CHECK(igs->math[i] == 0);
	destroy(&f);
}

int
st_cterm_igs_math_test_main(void)
{
	matrix(); bounds_and_clear(); numeric_text(); partitions(); console_partitions(); prefix_and_recovery();
	return 0;
}
