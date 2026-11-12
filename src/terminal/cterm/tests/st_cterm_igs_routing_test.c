/* X14: unscaled persistent routing, without immediate raster side effects. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "igs_test_fixture.h"
#include "st_igs_image.h"

static void
matrix(void)
{
	static const int words[] = {INT16_MIN,-1,0,1,2,3,4,5,6,9999,INT16_MAX};
	for (unsigned resolution = 0; resolution < 3; ++resolution)
		for (unsigned status = 0; status < 2; ++status) {
			struct fixture f; create(&f,resolution,status != 0);
			struct cterm_igs *igs = f.terminal->igs_engine;
			feed(&f,"\033fG#p>3,2:X 10,19,17:\033b");
			const struct st_console_rect full = {0,0,igs->width,igs->height}; seed(&f,&full,769,768);
			struct cterm_igs_attributes attributes; memcpy(&attributes,&igs->attributes,sizeof(attributes));
			uint32_t palette[16]; memcpy(palette,igs->palette,sizeof(palette));
			for (unsigned bank = 0; bank < 2; ++bank) {
				CHECK(st_igs_image_create(&igs->memory[bank],resolution == 0 ? 4 : resolution == 1 ? 2 : 1) == 0);
				CHECK(st_pixel_buffer_fill(igs->memory[bank]->pixels,1,1) == 0);
				memcpy(igs->memory_rect[bank],((int[4]){16,8,47,15}),sizeof(igs->memory_rect[bank]));
				igs->memory_view[bank] = (struct cterm_igs_memory_view){31,7,2,4,false};
			}
			for (unsigned scaling = 0; scaling < 3; ++scaling) {
				char wire[24]; (void)snprintf(wire,sizeof(wire),"G#g %u:",scaling); feed(&f,wire);
				for (size_t w = 0; w < sizeof(words)/sizeof(*words); ++w) {
					igs->math[0] = (int16_t)words[w];
					if (words[w] >= 0 && words[w] <= 9999) {
						(void)snprintf(wire,sizeof(wire),"G#X 14,%d:",words[w]); feed(&f,wire);
					} else feed(&f,"G#X 14,0]:");
					CHECK(igs->memory_route == (words[w] > 5 ? 0 : words[w]));
				}
			}
			if (resolution == 0 && !status)
				for (int word = INT16_MIN; word <= INT16_MAX; ++word) {
					uint32_t final = literal_words(igs, &word, 1);
					igs->math[0] = (int16_t)word; feed(&f,"G#X 14,0]:");
					CHECK(igs->random_seed == final);
					CHECK(igs->memory_route == (word > 5 ? 0 : word));
				}
			CHECK(memcmp(&attributes,&igs->attributes,sizeof(attributes)) == 0);
			CHECK(memcmp(palette,igs->palette,sizeof(palette)) == 0);
			CHECK(igs->draw_x == 19 && igs->draw_y == 17 && igs->random_seed == 1);
			CHECK(f.terminal->xpos == 4 && f.terminal->ypos == 3 && f.terminal->seq_trailing_remain == 1);
			CHECK(igs->wait_deadline == 0 && igs->input_deadline == 0 && igs->text_deadline == 0);
			for (unsigned bank = 0; bank < 2; ++bank) {
				CHECK(igs->memory_view[bank].word_width == 2 && igs->memory_view[bank].planes == 4);
				CHECK(memcmp(igs->memory_rect[bank],((int[4]){16,8,47,15}),sizeof(igs->memory_rect[bank])) == 0);
				for (size_t i = 0; i < igs->memory[bank]->pixels->width; ++i) CHECK(igs->memory[bank]->pixels->primary[i] == 1);
			}
			struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f.console,&full,false) == 0);
			for (size_t i = 0; i < (size_t)full.width*full.height; ++i) CHECK(b->primary[i] == 769 && b->alternate[i] == 768);
			st_pixel_buffer_destroy(b); destroy(&f);
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
partitions_and_lifetime(void)
{
	static const char *wires[] = {"AG#X>14,5:M 2:B", "AG#X>0],1]:M 2:B", "AG#X>r,R:M 2:B", "AG#X>14,6:M 2:B"};
	static const int routes[] = {5,-1,1,0};
	struct fixture f; create(&f,0,false); f.terminal->response_cb = no_response;
	struct cterm_igs *igs = f.terminal->igs_engine;
	for (size_t w = 0; w < sizeof(wires)/sizeof(*wires); ++w) {
		size_t size = strlen(wires[w]);
		for (size_t split = 0; split <= size+1; ++split) {
			CHECK(st_cterm_reset_terminal(f.terminal) == 0);
			igs->math[0] = 14; igs->math[1] = -1; igs->random_seed = 49;
			igs->random_range[0][1] = 255; igs->random_range[1][1] = 5;
			size_t used;
			if (split > size) {
				for (size_t i = 0; i < size; ++i) CHECK(st_cterm_write(f.terminal,wires[w]+i,1,&used) == 0 && used == 1);
			} else {
				CHECK(st_cterm_write(f.terminal,wires[w],split,&used) == 0 && used == split);
				CHECK(st_cterm_write(f.terminal,wires[w]+split,size-split,&used) == 0 && used == size-split);
			}
			CHECK(igs->memory_route == routes[w] && igs->attributes.writing_mode == 2);
			CHECK(igs->random_seed == (w == 2 ? UINT32_C(0xa80501b4) : 49));
			CHECK(f.terminal->xpos == 3 && igs->wait_deadline == 0 && igs->input_deadline == 0);
			struct st_console_cell cells[2]; const struct st_console_rect line = {0,0,2,1};
			CHECK(st_console_read_cells(f.console,&line,cells,2) == 0 && cells[0].glyph == 'A' && cells[1].glyph == 'B');
		}
	}
	feed(&f,"G#X 14,4:");
	static const char *partial[] = {"G#X 14,", "G#X 14,5", "G#X r,"};
	for (size_t i = 0; i < sizeof(partial)/sizeof(*partial); ++i) {
		igs->random_seed = 49;
		feed(&f,partial[i]); CHECK(st_cterm_finish(f.terminal) == 0);
		CHECK(igs->memory_route == 4 && igs->random_seed == 49);
		feed(&f,partial[i]); local_ms += 5001; CHECK(st_cterm_poll(f.terminal) == 0);
		CHECK(igs->memory_route == 4 && igs->random_seed == 49);
	}
	const char invalid[] = "G#X 14,10000:"; size_t used;
	CHECK(st_cterm_write(f.terminal,invalid,sizeof(invalid)-1,&used) == 0);
	CHECK(igs->memory_route == 4 && used == sizeof(invalid)-1);
	igs->now_ms = bad_clock; feed(&f,"G#X 14,3:");
	CHECK(igs->memory_route == 3); igs->now_ms = now;
	feed(&f,"G#G 1,3,0,0,15,0:"); CHECK(igs->memory_route == 3);
	const struct st_console_rect remembered0 = {32,8,32,8}, remembered1 = {64,16,16,4};
	seed(&f,&remembered0,769,768); seed(&f,&remembered1,769,768);
	feed(&f,"G#X>14,4:G>1,3,32,8,63,15:X>14,5:G 1,3,64,16,79,19:");
	CHECK(memcmp(igs->memory_rect[0],((int[4]){32,8,63,15}),sizeof(igs->memory_rect[0])) == 0);
	CHECK(memcmp(igs->memory_rect[1],((int[4]){64,16,79,19}),sizeof(igs->memory_rect[1])) == 0);
	CHECK(igs->memory_view[0].word_width == 2 && igs->memory_view[1].word_width == 1);
	for (unsigned bank = 0; bank < 2; ++bank) {
		CHECK(igs->memory[bank]->pixels->alternate == NULL);
		for (size_t i = 0; i < igs->memory[bank]->pixels->width; ++i) CHECK(igs->memory[bank]->pixels->primary[i] == 0);
	}
	const struct st_console_rect capture0 = {80,40,32,8}, capture1 = {120,60,16,4};
	seed(&f,&capture0,769,768); seed(&f,&capture1,769,768);
	feed(&f,"G#X>14,4:G>2,3,80,40:X>14,5:G 2,3,120,60:");
	CHECK(igs->memory_route == 5);
	for (unsigned bank = 0; bank < 2; ++bank) {
		const struct st_pixel_buffer *pixels = igs->memory[bank]->pixels;
		CHECK(pixels->alternate != NULL);
		for (size_t i = 0; i < pixels->width; ++i) {
			bool copied = bank == 0 ? i >= 1360 && i < 1616 : i >= 1080 && i < 1144;
			CHECK(pixels->primary[i] == (copied ? 1u : 0u) && pixels->alternate[i] == 0);
		}
	}
	for (unsigned operation = 0; operation < 6; ++operation) {
		feed(&f,"G#X 14,5:");
		char wire[20]; (void)snprintf(wire,sizeof(wire),"G#I %u:",operation); feed(&f,wire);
		if (st_cterm_input_paused(f.terminal)) { local_ms = igs->wait_deadline; CHECK(st_cterm_poll(f.terminal) == 0); }
		CHECK(igs->memory_route == (operation == 0 || operation == 2 ? 0 : 5));
	}
	feed(&f,"G#X>14,5:R 1,0:"); local_ms = igs->wait_deadline; CHECK(st_cterm_poll(f.terminal) == 0);
	CHECK(igs->memory_route == 5);
	feed(&f,"G#R 0,0:"); local_ms = igs->wait_deadline; CHECK(st_cterm_poll(f.terminal) == 0);
	CHECK(igs->memory_route == 5);
	feed(&f,"G#X 14,"); CHECK(st_cterm_reset_terminal(f.terminal) == 0 && igs->memory_route == 0);
	const struct st_console_rect source = {0,0,16,1}, destination = {32,0,16,1};
	seed(&f,&source,769,768); seed(&f,&destination,768,768); igs->double_step = 4;
	const char chained[] = "G#G>0,6,0,0,15,0,32,0:X 14,5:";
	const char *tail = strstr(chained,"X 14");
	CHECK(st_cterm_write(f.terminal,chained,sizeof(chained)-1,&used) == 0 && used == (size_t)(tail-chained));
	CHECK(igs->memory_route == 0 && igs->copy_pending);
	local_ms = igs->wait_deadline; CHECK(st_cterm_poll(f.terminal) == 0);
	CHECK(igs->memory_route == 0 && !igs->copy_pending);
	feed(&f,tail); CHECK(igs->memory_route == 5);
	destroy(&f);
}

int
st_cterm_igs_routing_test_main(void)
{
	matrix(); partitions_and_lifetime(); return 0;
}
