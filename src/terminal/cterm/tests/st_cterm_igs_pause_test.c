/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "igs_test_fixture.h"

struct replies {
	struct fixture *fixture;
	uint8_t bytes[32];
	size_t count, keys;
	unsigned action;
};
static int clock_error;
static unsigned clock_reads;

static int
pause_now(uint64_t *value)
{
	++clock_reads;
	if (clock_error != 0)
		return clock_error;
	*value = local_ms;
	return 0;
}

static void
respond(const char *bytes, size_t size, void *context)
{
	struct replies *r = context;
	CHECK(size <= sizeof(r->bytes) - r->count);
	memcpy(r->bytes + r->count, bytes, size);
	r->count += size;
	struct cterm_igs *igs = r->fixture->terminal->igs_engine;
	if (size == 1 && (uint8_t)bytes[0] == 19) {
		if (r->action == 1)
			igs->loop_mode = 1;
		if (r->action == 2)
			local_ms += 30000;
		if (r->action == 3)
			CHECK(st_cterm_reset_terminal(r->fixture->terminal) == 0);
	}
	if (size == 1 && (uint8_t)bytes[0] == 17 && r->action == 4)
		CHECK(st_cterm_reset_terminal(r->fixture->terminal) == 0);
}

static void
key_output(const char *bytes, size_t size, void *context)
{
	(void)bytes;
	((struct replies *)context)->keys += size;
}

static void
attach(struct fixture *f, struct replies *r)
{
	*r = (struct replies){.fixture = f};
	clock_reads = 0;
	clock_error = 0;
	f->terminal->igs_engine->now_ms = pause_now;
	f->terminal->response_cb = respond;
	f->terminal->response_cbdata = r;
	f->terminal->keystroke_cb = key_output;
	f->terminal->keystroke_cbdata = r;
}

static void
durations(void)
{
	static const int words[] = {INT16_MIN, -1, 0, 1, 30, 31, INT16_MAX};
	for (unsigned resolution = 0; resolution < 3; ++resolution)
		for (unsigned reserved = 0; reserved < 2; ++reserved) {
			struct fixture f;
			create(&f, resolution, reserved != 0);
			struct replies r;
			attach(&f, &r);
			struct cterm_igs *igs = f.terminal->igs_engine;
			feed(&f, "\033fG#p>3,2:X 10,19,17:");
			struct cterm_igs_attributes attributes;
			uint32_t palette[16];
			memcpy(&attributes, &igs->attributes, sizeof(attributes));
			memcpy(palette, igs->palette, sizeof(palette));
			const struct st_console_rect full = {0, 0, igs->width, igs->height};
			seed(&f, &full, 769, 768);
			for (unsigned scaling = 0; scaling < 3; ++scaling)
				for (unsigned fractional = 0; fractional < 2; ++fractional)
					for (size_t w = 0; w < sizeof(words) / sizeof(*words); ++w) {
						char setup[16];
						(void)snprintf(setup, sizeof(setup), "G#g %u:", scaling);
						feed(&f, setup);
						r.count = 0;
						local_ms = fractional ? 1999 : 1000;
						uint64_t start = local_ms;
						unsigned seconds = words[w] > 30 ? 30 : (uint16_t)words[w];
						igs->math[0] = (int16_t)words[w];
						feed(&f, "G#t 0]:");
						CHECK(st_cterm_input_paused(f.terminal));
						CHECK(igs->wait_deadline == start + (uint64_t)seconds * 1000);
						CHECK(r.count == 1 && r.bytes[0] == 19);
						CHECK(igs->input_deadline == 0 && igs->text_deadline == 0);
						size_t used;
						CHECK(st_cterm_write(f.terminal, "A", 1, &used) == 0 && used == 0);
						if (seconds != 0) {
							local_ms = igs->wait_deadline - 1;
							CHECK(st_cterm_poll(f.terminal) == 0 && st_cterm_input_paused(f.terminal));
							CHECK(r.count == 1);
						}
						local_ms = igs->wait_deadline;
						CHECK(st_cterm_poll(f.terminal) == 0 && !st_cterm_input_paused(f.terminal));
						CHECK(r.count == 2 && r.bytes[1] == 17 && igs->wait_deadline == 0);
						CHECK(f.terminal->xpos == 4 && f.terminal->ypos == 3);
					}
			CHECK(memcmp(&attributes, &igs->attributes, sizeof(attributes)) == 0);
			CHECK(memcmp(palette, igs->palette, sizeof(palette)) == 0);
			CHECK(igs->draw_x == 19 && igs->draw_y == 17 && r.keys == 0);
			struct st_pixel_buffer *pixels = NULL;
			CHECK(st_console_get_pixels(&pixels, f.console, &full, false) == 0);
			for (size_t i = 0; i < (size_t)igs->width * igs->height; ++i)
				CHECK(pixels->primary[i] == 769 && pixels->alternate[i] == 768);
			st_pixel_buffer_destroy(pixels);
			destroy(&f);
		}
}

static void
cancellation_and_flow(void)
{
	struct fixture f;
	create(&f, 0, false);
	struct replies r;
	attach(&f, &r);
	struct cterm_igs *igs = f.terminal->igs_engine;
	for (unsigned which = 0; which < 4; ++which) {
		r.count = 0;
		feed(&f, "G#t 30:");
		bool consumed = false;
		switch (which) {
		case 0: consumed = st_cterm_handle_key(f.terminal, 3, 0); break;
		case 1: consumed = st_cterm_handle_codepage_key(f.terminal, 255); break;
		case 2: consumed = st_cterm_handle_text_key(f.terminal, 0x1f600); break;
		case 3: consumed = st_cterm_handle_doorway_key(f.terminal, 59); break;
		}
		CHECK(consumed && r.keys == 0 && st_cterm_input_paused(f.terminal));
		CHECK(st_cterm_poll(f.terminal) == 0 && !st_cterm_input_paused(f.terminal));
		CHECK(r.count == 2 && r.bytes[0] == 19 && r.bytes[1] == 17 && igs->loop_mode == 0);
	}
	r.count = 0;
	feed(&f, "G#t 0:");
	CHECK(st_cterm_handle_key(f.terminal, 'x', 0) && r.keys == 0);
	CHECK(st_cterm_poll(f.terminal) == 0 && r.count == 2);
	for (unsigned mode = 0; mode < 3; ++mode) {
		r.count = 0;
		igs->loop_mode = mode == 2 ? 1 : 0;
		igs->flow_mode = mode == 1 ? 0 : 1;
		feed(&f, "G#t 0:");
		CHECK(st_cterm_input_paused(f.terminal));
		CHECK(st_cterm_poll(f.terminal) == 0 && !st_cterm_input_paused(f.terminal));
		CHECK(r.count == (mode == 0 ? 2u : 0u));
	}
	CHECK(clock_reads >= 2);
	igs->loop_mode = 1;
	feed(&f, "G#t 30:");
	CHECK(st_cterm_handle_key(f.terminal, 3, 0));
	CHECK(st_cterm_poll(f.terminal) == 0 && igs->loop_mode == 1 && r.count == 0);
	igs->loop_mode = 1;
	feed(&f, "G#t 0:");
	igs->loop_mode = 0;
	CHECK(st_cterm_poll(f.terminal) == 0 && r.count == 1 && r.bytes[0] == 17);
	r.count = 0;
	r.action = 1;
	feed(&f, "G#t 0:");
	CHECK(st_cterm_poll(f.terminal) == 0 && r.count == 1);
	igs->loop_mode = 0;
	r.action = 2;
	r.count = 0;
	local_ms = 1000;
	feed(&f, "G#t 30:");
	CHECK(igs->wait_deadline == 31000 && local_ms == 31000);
	CHECK(st_cterm_poll(f.terminal) == 0 && r.count == 2);
	r.action = 0;
	igs->flow_off = 0xfe;
	igs->flow_on = 0x81;
	igs->flow_off_count = 2;
	igs->flow_on_count = 3;
	r.count = 0;
	feed(&f, "G#t 0:");
	CHECK(st_cterm_poll(f.terminal) == 0);
	CHECK(r.count == 5 && memcmp(r.bytes, "\xfe\xfe\x81\x81\x81", 5) == 0);
	destroy(&f);
}

static void
partitions_and_errors(void)
{
	const char wire[] = "AG#g>2:t>r:M 2:B";
	const size_t size = sizeof(wire) - 1;
	struct fixture f;
	create(&f, 0, false);
	struct replies r;
	attach(&f, &r);
	for (size_t split = 0; split <= size + 1; ++split) {
		CHECK(st_cterm_reset_terminal(f.terminal) == 0);
		r.count = r.keys = 0;
		f.terminal->igs_engine->random_range[0][1] = 255;
		size_t offset = 0;
		while (offset < size) {
			size_t piece = split > size ? 1 : offset < split ? split - offset : size - offset;
			size_t used;
			CHECK(st_cterm_write(f.terminal, wire + offset, piece, &used) == 0);
			offset += used;
			if (st_cterm_input_paused(f.terminal)) {
				CHECK(r.count == 1);
				CHECK(st_cterm_handle_key(f.terminal, 'x', 0));
				CHECK(st_cterm_poll(f.terminal) == 0);
			}
			CHECK(used != 0 || offset == size);
		}
		CHECK(r.count == 2 && r.keys == 0);
		CHECK(f.terminal->igs_engine->random_seed == UINT32_C(0xbb40e62e));
		CHECK(f.terminal->igs_engine->attributes.writing_mode == 2);
		const struct st_console_rect area = {0, 0, 2, 1};
		struct st_console_cell cells[2];
		CHECK(st_console_read_cells(f.console, &area, cells, 2) == 0);
		CHECK(cells[0].glyph == 'A' && cells[1].glyph == 'B');
	}
	r.count = 0;
	feed(&f, "G#t r");
	CHECK(st_cterm_finish(f.terminal) == 0 && r.count == 0);
	feed(&f, "G#t r");
	local_ms += 5001;
	CHECK(st_cterm_poll(f.terminal) == 0 && r.count == 0);
	const char bad[] = "G#t 10000:";
	size_t used;
	CHECK(st_cterm_write(f.terminal, bad, sizeof(bad) - 1, &used) == 0);
	clock_error = IO_ERROR_IO;
	const char pause[] = "G#t 30:";
	CHECK(st_cterm_write(f.terminal, pause, sizeof(pause) - 1, &used) == 0);
	CHECK(!st_cterm_input_paused(f.terminal) && r.count == 0);
	clock_error = 0;
	local_ms = UINT64_MAX - 1000;
	CHECK(st_cterm_write(f.terminal, pause, sizeof(pause) - 1, &used) == 0);
	CHECK(!st_cterm_input_paused(f.terminal) && r.count == 0);
	local_ms = 1000;
	feed(&f, pause);
	clock_error = IO_ERROR_IO;
	CHECK(st_cterm_poll(f.terminal) == IO_ERROR_IO && st_cterm_input_paused(f.terminal));
	clock_error = 0;
	CHECK(st_cterm_finish(f.terminal) == 0 && !st_cterm_input_paused(f.terminal));
	CHECK(r.count == 2);
	feed(&f, "G#t 30:");
	CHECK(st_cterm_handle_key(f.terminal, 'x', 0));
	CHECK(st_cterm_handle_key(f.terminal, 'z', 0) && r.keys == 1);
	clock_error = IO_ERROR_IO;
	CHECK(st_cterm_poll(f.terminal) == 0 && !st_cterm_input_paused(f.terminal));
	clock_error = 0;
	r.count = 0;
	feed(&f, "\033bG#t 30:");
	CHECK(st_cterm_reset_terminal(f.terminal) == 0 && !st_cterm_input_paused(f.terminal));
	CHECK(r.count == 2 && f.terminal->accumulator == NULL);
	for (unsigned action = 3; action <= 4; ++action) {
		r.count = 0;
		r.action = action;
		feed(&f, "G#t 0:");
		if (st_cterm_input_paused(f.terminal))
			CHECK(st_cterm_poll(f.terminal) == 0);
		CHECK(!st_cterm_input_paused(f.terminal) && f.terminal->igs_engine->loop_mode == 0);
	}
	r.action = 0;
	destroy(&f);
}

int
st_cterm_igs_pause_test_main(void)
{
	durations();
	cancellation_and_flow();
	partitions_and_errors();
	return 0;
}
