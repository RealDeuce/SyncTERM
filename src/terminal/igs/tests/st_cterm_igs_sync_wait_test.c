/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "igs_test_fixture.h"
#include "st_graphics_input.h"

static int    clock_error;
static size_t keys;

static int
sync_now(uint64_t *value)
{
	*value = local_ms;
	return clock_error;
}

static void
no_reply(const char *bytes, size_t size, void *context)
{
	(void)bytes;
	(void)context;
	CHECK(size == 0);
}

static void
key_output(const char *bytes, size_t size, void *context)
{
	(void)bytes;
	(void)context;
	keys += size;
}

static void
attach(struct fixture *f)
{
	clock_error                     = 0;
	f->igs->now_ms                  = sync_now;
	f->terminal->response_cb        = no_reply;
	f->terminal->keystroke_cb       = key_output;
}

static void
wait_case(struct fixture *f, int word)
{
	struct cterm_igs *igs = f->igs;
	igs->double_step      = 7;
	igs->math[0]          = (int16_t)word;
	uint32_t final        = literal_words(igs, &word, 1);
	local_ms              = 1999;
	feed(f, "G#q 0]:");
	CHECK(cterm_igs_paused(f->igs));
	CHECK(igs->random_seed == final);
	unsigned count    = word > 9994 || word < 1 ? 1 : word > 180 ? 180 : (unsigned)word;
	uint64_t duration = ((uint64_t)(count + 1) * 1000 + igs->sync_wait_hz - 1) / igs->sync_wait_hz;
	CHECK(igs->wait_deadline == 1999 + duration);
	CHECK(igs->double_step == (word > 9994 ? 9999 - word : 7));
	CHECK(igs->pending_resolution == -1 && !igs->timed_pause);
	CHECK(igs->input_deadline == 0 && igs->text_deadline == 0);
	size_t used;
	CHECK(cterm_igs_feed(f->igs, "A", 1, &used) == 0 && used == 0);
	local_ms = igs->wait_deadline - 1;
	CHECK(cterm_igs_poll(f->igs) == 0 && cterm_igs_paused(f->igs));
	local_ms = igs->wait_deadline;
	CHECK(cterm_igs_poll(f->igs) == 0 && !cterm_igs_paused(f->igs));
	CHECK(igs->wait_deadline == 0 && igs->double_step == (word > 9994 ? 9999 - word : 7));
}

static void
durations(void)
{
	static const int      words[] = {INT16_MIN, -1, 0, 1, 2, 179, 180, 181, 9994, 9995, 9996, 9997, 9998, 9999,
	    INT16_MAX};
	static const unsigned rates[] = {50, 60, 1000};
	for (unsigned resolution = 0; resolution < 3; ++resolution)
		for (unsigned reserved = 0; reserved < 2; ++reserved) {
			struct fixture f;
			create(&f, resolution, reserved != 0);
			attach(&f);
			struct cterm_igs *igs = f.igs;
			feed(&f, "\033fG#p>3,2:X 10,19,17:");
			struct cterm_igs_attributes attributes;
			uint32_t                    palette[16];
			memcpy(&attributes, &igs->attributes, sizeof(attributes));
			memcpy(palette, igs->palette, sizeof(palette));
			const struct st_console_rect full = {0, 0, igs->width, igs->height};
			seed(&f, &full, 769, 768);
			for (unsigned scaling = 0; scaling < 3; ++scaling) {
				char setup[16];
				(void)snprintf(setup, sizeof(setup), "G#g %u:", scaling);
				feed(&f, setup);
				for (size_t rate = 0; rate < sizeof(rates) / sizeof(*rates); ++rate) {
					igs->sync_wait_hz = rates[rate];
					for (size_t word = 0; word < sizeof(words) / sizeof(*words); ++word)
						wait_case(&f, words[word]);
				}
			}
			if (resolution == 0 && !reserved) {
				igs->sync_wait_hz = 60;
				for (int word = INT16_MIN; word <= INT16_MAX; ++word)
					wait_case(&f, word);
			}
			CHECK(f.terminal->xpos == 4 && f.terminal->ypos == 3);
			CHECK(memcmp(&attributes, &igs->attributes, sizeof(attributes)) == 0);
			CHECK(memcmp(palette, igs->palette, sizeof(palette)) == 0);
			CHECK(igs->draw_x == 19 && igs->draw_y == 17);
			struct st_pixel_buffer *pixels = NULL;
			CHECK(st_console_get_pixels(&pixels, f.console, &full, false) == 0);
			for (size_t i = 0; i < (size_t)igs->width * igs->height; ++i)
				CHECK(pixels->primary[i] == 769 && pixels->alternate[i] == 768);
			st_pixel_buffer_destroy(pixels);
			destroy(&f);
		}
}

static void
partitions(void)
{
	const char   wire[] = "AG#g>2:q>r:M 2:B";
	const size_t size   = sizeof(wire) - 1;
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f;
		create(&f, resolution, false);
		attach(&f);
		for (size_t split = 0; split <= size + 1; ++split) {
			CHECK(cterm_igs_reset_display(f.igs) == 0);
			struct cterm_igs *igs   = f.igs;
			igs->double_step        = 4;
			igs->random_range[0][1] = 255;
			size_t offset           = 0;
			while (offset < size) {
				size_t piece = split > size ? 1 : offset < split ? split - offset : size - offset;
				size_t used;
				CHECK(cterm_igs_feed(f.igs, wire + offset, piece, &used) == 0);
				offset += used;
				if (cterm_igs_paused(f.igs)) {
					CHECK(igs->wait_deadline == local_ms + 3017);
					local_ms = igs->wait_deadline;
					CHECK(cterm_igs_poll(f.igs) == 0);
				}
				CHECK(used != 0 || offset == size);
			}
			CHECK(igs->double_step == 4 && igs->random_seed == UINT32_C(0xbb40e62e));
			CHECK(igs->attributes.writing_mode == 2);
			const struct st_console_rect area = {0, 0, 2, 1};
			struct st_console_cell       cells[2];
			CHECK(st_console_read_cells(f.console, &area, cells, 2) == 0);
			CHECK(cells[0].glyph == 'A' && cells[1].glyph == 'B');
		}
		destroy(&f);
	}
}

static void
finish_wait(struct fixture *f)
{
	local_ms = f->igs->wait_deadline;
	CHECK(cterm_igs_poll(f->igs) == 0 && !cterm_igs_paused(f->igs));
}

static void
lifetime_and_errors(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f;
		create(&f, resolution, false);
		attach(&f);
		struct cterminal *t   = f.terminal;
		struct cterm_igs *igs = f.igs;
		feed(&f, "\033bG#q 9995:");
		keys = 0;
		CHECK(st_graphics_handle_key(t, 'x', 0) && keys == 1 && cterm_igs_paused(f.igs));
		finish_wait(&f);
		CHECK(t->accumulator != NULL);
		feed(&f, "A");
		CHECK(t->vt52_foreground == 33 && t->xpos == 1);
		feed(&f, "G#q 0:");
		CHECK(cterm_igs_finish_display(f.igs) == 0 && !cterm_igs_paused(f.igs) && igs->double_step == 4);
		feed(&f, "G#I>1:I>3:I 4:");
		CHECK(igs->double_step == 4);
		feed(&f, "G#I 5:");
		if (cterm_igs_paused(f.igs))
			finish_wait(&f);
		CHECK(igs->double_step == 4);
		feed(&f, resolution == 0 ? "G#R 1,0:" : "G#R 0,0:");
		if (cterm_igs_paused(f.igs))
			finish_wait(&f);
		CHECK(igs->double_step == 4);
		feed(&f, "G#I 2:");
		CHECK(igs->double_step == 0);
		feed(&f, "G#q 9996:");
		finish_wait(&f);
		feed(&f, "G#I 0:");
		CHECK(igs->double_step == 0);
		feed(&f, "G#q 9995:");
		CHECK(cterm_igs_reset_display(f.igs) == 0 && igs->double_step == 0 && !cterm_igs_paused(f.igs));
		feed(&f, "G#q r");
		CHECK(cterm_igs_finish_display(f.igs) == 0 && igs->double_step == 0 && igs->random_seed == 1);
		feed(&f, "G#q r");
		local_ms += 5001;
		CHECK(cterm_igs_poll(f.igs) == 0 && igs->double_step == 0 && igs->random_seed == 1);
		const char bad[] = "G#q 10000:";
		size_t     used;
		CHECK(cterm_igs_feed(f.igs, bad, sizeof(bad) - 1, &used) == 0);
		clock_error          = IO_ERROR_IO;
		const char command[] = "G#q 9997:";
		CHECK(cterm_igs_feed(f.igs, command, sizeof(command) - 1, &used) == 0);
		CHECK(igs->double_step == 2 && !cterm_igs_paused(f.igs));
		clock_error = 0;
		local_ms    = UINT64_MAX - 33;
		CHECK(cterm_igs_feed(f.igs, command, sizeof(command) - 1, &used) == 0);
		CHECK(igs->double_step == 2 && !cterm_igs_paused(f.igs));
		local_ms          = 1000;
		igs->sync_wait_hz = 0;
		CHECK(cterm_igs_feed(f.igs, command, sizeof(command) - 1, &used) == 0);
		igs->sync_wait_hz = 1001;
		CHECK(cterm_igs_feed(f.igs, command, sizeof(command) - 1, &used) == 0);
		igs->sync_wait_hz = 60;
		feed(&f, command);
		clock_error = IO_ERROR_IO;
		CHECK(cterm_igs_poll(f.igs) == IO_ERROR_IO && cterm_igs_paused(f.igs));
		clock_error = 0;
		finish_wait(&f);
		destroy(&f);
	}
}

int
st_cterm_igs_sync_wait_test_main(void)
{
	durations();
	partitions();
	lifetime_and_errors();
	return 0;
}
