/* X7: literal source rows, shared text and independent active-pattern copies. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "igs_test_fixture.h"

static const uint16_t rows[16] = {0x8000, 0x4000, 0x2000, 0x1000, 0x0800, 0x0400, 0x0200, 0x0100,
    0x0080, 0x0040, 0x0020, 0x0010, 0x0008, 0x0004, 0x0002, 0x0001};

static void
write_bytes(struct fixture *f, const void *bytes, size_t size)
{
	size_t used;
	CHECK(st_cterm_write(f->terminal, bytes, size, &used) == 0 && used == size);
}

static size_t
load_wire(char *wire, size_t capacity, const char *prefix)
{
	size_t size = (size_t)snprintf(wire, capacity, "%s", prefix);
	CHECK(size + 16 * 17 < capacity);
	for (unsigned row = 0; row < 16; ++row) {
		for (unsigned col = 0; col < 16; ++col)
			wire[size++] = col == row ? (row % 2 ? 'x' : 'X') : '.';
		wire[size++] = '@';
	}
	return size;
}

static void
matrix(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution)
		for (unsigned status = 0; status < 2; ++status) {
			struct fixture f;
			create(&f, resolution, status != 0);
			struct cterm_igs *igs = f.terminal->igs_engine;
			feed(&f, "\033fG#p>3,2:X 10,19,17:\033b");
			const struct st_console_rect full = {0, 0, igs->width, igs->height};
			seed(&f, &full, 769, 768);
			struct cterm_igs_attributes attributes;
			memcpy(&attributes, &igs->attributes, sizeof(attributes));
			uint32_t palette[16];
			memcpy(palette, igs->palette, sizeof(palette));
			for (unsigned scaling = 0; scaling < 3; ++scaling)
				for (unsigned slot = 0; slot < 8; ++slot) {
					char prefix[64], wire[512];
					(void)snprintf(prefix, sizeof(prefix), "G#g %u:G#X 7,%u,", scaling, slot);
					uint16_t expected[8][16];
					memcpy(expected, igs->patterns, sizeof(expected));
					memcpy(expected[slot], rows, sizeof(rows));
					write_bytes(&f, wire, load_wire(wire, sizeof(wire), prefix));
					CHECK(memcmp(expected, igs->patterns, sizeof(expected)) == 0);
					CHECK(memcmp(&attributes, &igs->attributes, sizeof(attributes)) == 0);
					CHECK(memcmp(palette, igs->palette, sizeof(palette)) == 0);
					CHECK(igs->shared_text_size == 16 && igs->shared_text[15] == 'X');
					CHECK(igs->draw_x == 19 && igs->draw_y == 17 && igs->scaling_mode == (int)scaling);
					CHECK(igs->random_seed == 1 && igs->pattern_seed == 0);
					CHECK(igs->text_deadline == 0 && igs->input_deadline == 0);
				}
			CHECK(f.terminal->xpos == 4 && f.terminal->ypos == 3 && f.terminal->seq_trailing_remain == 1);
			struct st_pixel_buffer *pixels = NULL;
			CHECK(st_console_get_pixels(&pixels, f.console, &full, false) == 0);
			for (size_t i = 0; i < (size_t)igs->width * igs->height; ++i)
				CHECK(pixels->primary[i] == 769 && pixels->alternate[i] == 768);
			st_pixel_buffer_destroy(pixels);
			feed(&f, "\001G#A 4,7,0:");
			CHECK(memcmp(igs->attributes.user_fill, rows, sizeof(rows)) == 0);
			feed(&f, "G#X 7,7,................@");
			CHECK(igs->patterns[7][0] == 0 && memcmp(igs->attributes.user_fill, rows, sizeof(rows)) == 0);
			CHECK(st_cterm_finish(f.terminal) == 0);
			feed(&f, "G#I 2:");
			CHECK(memcmp(igs->attributes.user_fill, rows, sizeof(rows)) == 0);
			CHECK(st_cterm_reset_terminal(f.terminal) == 0);
			CHECK(igs->shared_text_size == 0 && igs->patterns[7][15] == 0);
			destroy(&f);
		}
}

static void
readers(void)
{
	struct fixture f;
	create(&f, 0, false);
	struct cterm_igs *igs = f.terminal->igs_engine;
	feed(&f, "G#X 7,3,xxxxxxxxxxxxxxxx@");
	CHECK(igs->patterns[3][0] == 0xffff && igs->shared_text[0] == 'X');
	uint64_t deadline = igs->text_deadline;
	CHECK(deadline == local_ms + 5000);
	local_ms += 1000;
	feed(&f, "X@");
	CHECK(igs->patterns[3][1] == 0xbfff && igs->text_deadline == local_ms + 5000);
	static const uint8_t embedded[] = {'q', 0, 'x', '.', '@'};
	write_bytes(&f, embedded, sizeof(embedded));
	CHECK(igs->patterns[3][2] == 0x27ff && igs->shared_text[2] == 'X');
	feed(&f, "@");
	CHECK(igs->patterns[3][3] == 0x27ff && igs->shared_text[0] == '@');
	char maximum[128];
	memset(maximum, 'x', sizeof(maximum));
	write_bytes(&f, maximum, sizeof(maximum));
	CHECK(igs->patterns[3][4] == 0xffff && igs->shared_text_size == 128);
	CHECK(igs->shared_text[15] == 'X' && igs->shared_text[16] == 'x' && igs->shared_text[127] == 'x');
	feed(&f, "@");
	CHECK(igs->patterns[3][5] == 0x3fff && igs->shared_text_size == 1);
	feed(&f, "G#\033\200\377x\r\nX@");
	CHECK(igs->patterns[3][6] == 0x06ff);
	feed(&f, "unfinished");
	local_ms = igs->text_deadline;
	CHECK(st_cterm_poll(f.terminal) == 0 && igs->patterns[3][7] == 0);
	CHECK(igs->shared_text[0] == 'G' && igs->text_deadline == 0);
	feed(&f, "G#X 7,4,X@");
	CHECK(igs->patterns[4][0] == 0x86ff);
	CHECK(st_cterm_finish(f.terminal) == 0 && igs->patterns[4][0] == 0x86ff);
	for (unsigned byte = 0; byte < 256; ++byte) {
		if (byte == '@' || byte == '\r' || byte == '\n') continue;
		feed(&f, "G#X 7,4,");
		uint8_t row[17]; memset(row, '.', 16); row[0] = (uint8_t)byte; row[16] = '@';
		write_bytes(&f, row, sizeof(row));
		CHECK(igs->patterns[4][0] == (byte == 'x' || byte == 'X' ? 0x8000 : 0));
		CHECK(st_cterm_finish(f.terminal) == 0);
	}
	destroy(&f);
}

static void
partitions_and_errors(void)
{
	struct fixture f;
	create(&f, 0, false);
	struct cterm_igs *igs = f.terminal->igs_engine;
	for (unsigned random = 0; random < 2; ++random) {
		char wire[512];
		size_t size = load_wire(wire, sizeof(wire), random ? "AG#X>r,R," : "AG#X>7,4,");
		memcpy(wire + size, "M 2:B", 5); size += 5;
		for (size_t split = 0; split <= size + 1; ++split) {
			CHECK(st_cterm_reset_terminal(f.terminal) == 0);
			igs->random_seed = 2250;
			igs->random_range[0][1] = igs->random_range[1][1] = 255;
			if (split > size)
				for (size_t i = 0; i < size; ++i) write_bytes(&f, wire + i, 1);
			else { write_bytes(&f, wire, split); write_bytes(&f, wire + split, size - split); }
			CHECK(memcmp(igs->patterns[4], rows, sizeof(rows)) == 0);
			CHECK(igs->random_seed == (random ? UINT32_C(0x5c9c0408) : 2250));
			CHECK(igs->attributes.writing_mode == 2 && f.terminal->xpos == 3);
			struct st_console_cell cells[2]; const struct st_console_rect area = {0, 0, 2, 1};
			CHECK(st_console_read_cells(f.console, &area, cells, 2) == 0);
			CHECK(cells[0].glyph == 'A' && cells[1].glyph == 'B');
		}
	}
	static const int invalid[] = {INT16_MIN, -1, 8, 1024, 2048, 9999, INT16_MAX};
	for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i) {
		igs->math[0] = (int16_t)invalid[i];
		uint16_t saved[8][16]; memcpy(saved, igs->patterns, sizeof(saved));
		const char wire[] = "G#X 7,0],XXXXXXXXXXXXXXXX@"; size_t used;
		CHECK(st_cterm_write(f.terminal, wire, sizeof(wire) - 1, &used) == 0);
		CHECK(used == sizeof(wire)-1 && memcmp(saved, igs->patterns, sizeof(saved)) == 0);
	}
	feed(&f, "G#X 7,0,X@");
	CHECK(st_cterm_reset_terminal(f.terminal) == 0 && igs->shared_text_size == 0);
	igs->random_seed = 2250;
	igs->random_range[0][1] = igs->random_range[1][1] = 255;
	feed(&f, "G#X r,R,partial");
	CHECK(igs->random_seed == UINT32_C(0x5c9c0408) && igs->pattern_loading);
	CHECK(st_cterm_finish(f.terminal) == 0 && !igs->pattern_loading);
	CHECK(igs->random_seed == UINT32_C(0x5c9c0408));
	destroy(&f);
}

static void
reply(const char *bytes, size_t size, void *context)
{
	unsigned *count = context;
	CHECK(size == 1 && (uint8_t)*bytes == 0xfe);
	++*count;
}

static void
reset_reply(const char *bytes, size_t size, void *context)
{
	struct fixture *f = context;
	CHECK(size == 1 && (uint8_t)*bytes == 0xfe);
	CHECK(st_cterm_reset_terminal(f->terminal) == 0);
}

static void
reuse_and_flow(void)
{
	struct fixture f;
	create(&f, 0, false);
	struct cterm_igs *igs = f.terminal->igs_engine;
	memset(igs->shared_text, 'x', 16); igs->shared_text[16] = 0; igs->shared_text_size = 16;
	igs->loop_mode = 1; igs->loop_kind = 'W';
	feed(&f, "G#X 7,2:");
	for (unsigned row = 0; row < 16; ++row) CHECK(igs->patterns[2][row] == 0xffff);
	CHECK(igs->shared_text[0] == 'X' && igs->text_deadline == 0 && igs->text_counter == 0);
	igs->loop_mode = 0; igs->loop_kind = 'Z';
	feed(&f, "G#X 7,3,");
	CHECK(igs->patterns[3][0] == 0xffff && igs->loop_kind == ' ');
	for (unsigned row = 1; row < 16; ++row) feed(&f, "................@");
	for (unsigned row = 1; row < 16; ++row) CHECK(igs->patterns[3][row] == 0);
	unsigned count = 0;
	f.terminal->response_cb = reply; f.terminal->response_cbdata = &count;
	igs->loop_mode = 1; igs->loop_kind = '@'; igs->flow_on = 0xfe; igs->flow_on_count = 2;
	char wire[512]; size_t size = load_wire(wire, sizeof(wire), "G#X 7,4,");
	write_bytes(&f, wire, size);
	CHECK(count == 2 && igs->text_counter == 16 && memcmp(igs->patterns[4], rows, sizeof(rows)) == 0);
	f.terminal->response_cb = reset_reply; f.terminal->response_cbdata = &f;
	igs->text_counter = 0;
	feed(&f, "G#X 7,4,G#M 2:");
	CHECK(!igs->pattern_loading && igs->patterns[4][0] == 0 && igs->shared_text_size == 0);
	CHECK(igs->attributes.writing_mode == 2);
	destroy(&f);
}

static unsigned clock_reads;
static int
first_clock(uint64_t *value)
{
	return clock_reads++ == 0 ? now(value) : IO_ERROR_IO;
}

static void
clock_errors(void)
{
	struct fixture f;
	create(&f, 0, false);
	struct cterm_igs *igs = f.terminal->igs_engine;
	const char header[] = "G#X 7,0,";
	size_t used;
	clock_reads = 0; igs->now_ms = first_clock;
	CHECK(st_cterm_write(f.terminal, header, sizeof(header) - 1, &used) == IO_ERROR_IO);
	/* The reader's entry sample succeeded. A later progress-clock error
	 * retains the acquired prefix and its original deadline for recovery. */
	CHECK(used == sizeof(header) - 1 && igs->pattern_loading && igs->text_deadline == local_ms + 5000);
	clock_reads = 0;
	CHECK(st_cterm_write(f.terminal, "x@", 2, &used) == 0 && used == 2);
	/* Row acceptance precedes the next reader's failing entry sample. */
	CHECK(igs->patterns[0][0] == 0x8000 && !igs->pattern_loading && igs->text_deadline == 0);
	igs->now_ms = now;
	feed(&f, "G#M 2:"); CHECK(igs->attributes.writing_mode == 2);
	destroy(&f);
}

int
st_cterm_igs_pattern_load_test_main(void)
{
	matrix();
	readers();
	partitions_and_errors();
	reuse_and_flow();
	clock_errors();
	return 0;
}
