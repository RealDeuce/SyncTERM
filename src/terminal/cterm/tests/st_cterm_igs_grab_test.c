/* G: independently specified Boolean copies and retained indexed views. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "igs_test_fixture.h"
#include "st_igs_image.h"

struct alias_case { unsigned resolution; int route, operation, source_words, destination_words;
	int sx, sy, width, height, dx, dy; uint64_t hashes[2]; };
#include "data/igs_grab_vectors.h"

static uint64_t
hash(const uint32_t *pixels)
{
	uint64_t value = UINT64_C(14695981039346656037);
	for (unsigned i = 0; i < 512; ++i) value = (value ^ pixels[i])*UINT64_C(1099511628211);
	return value;
}

static void
row_aliases(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f,resolution,false);
		struct cterm_igs *igs = f.terminal->igs_engine;
		unsigned mask = cterm_igs_plane_mask(resolution);
		for (unsigned bank = 0; bank < 2; ++bank)
			CHECK(st_igs_image_create(&igs->memory[bank],resolution == 0 ? 4 : resolution == 1 ? 2 : 1) == 0);
		for (size_t i = 0; i < sizeof(alias_cases)/sizeof(*alias_cases); ++i) {
			const struct alias_case *c = &alias_cases[i];
			if (c->resolution != resolution) continue;
			int source = (c->route == 2 && c->operation == 3) || (c->route == 3 && c->operation == 1) ? 1 : 0;
			int destination = c->operation == 4 ? source : 1-source;
			igs->memory_route = c->route;
			for (unsigned bank = 0; bank < 2; ++bank) {
				CHECK(st_pixel_buffer_fill(igs->memory[bank]->pixels,0,0) == 0);
				for (unsigned pixel = 0; pixel < 512; ++pixel)
					igs->memory[bank]->pixels->primary[pixel] = (bank == 0 ? (7*pixel)^(pixel>>2)^3 : (3*pixel)^(pixel>>3)^5)&mask;
				memcpy(igs->memory_rect[bank],((int[4]){0,0,15,15}),sizeof(igs->memory_rect[bank]));
			}
			igs->memory_rect[source][2] = c->source_words*16-1;
			igs->memory_rect[destination][2] = c->destination_words*16-1;
			char wire[96];
			if (c->operation == 1)
				(void)snprintf(wire,sizeof(wire),"G#G 1,%zu,%d,%d,%d,%d:",i%16,c->sx,c->sy,c->sx+c->width-1,c->sy+c->height-1);
			else
				(void)snprintf(wire,sizeof(wire),"G#G %d,%zu,%d,%d,%d,%d,%d,%d:",c->operation,i%16,c->sx,c->sy,
				    c->sx+c->width-1,c->sy+c->height-1,c->dx,c->dy);
			feed(&f,wire);
			for (unsigned bank = 0; bank < 2; ++bank) {
				if (hash(igs->memory[bank]->pixels->primary) != c->hashes[bank]) fprintf(stderr,"alias case%zu bank%u: %016" PRIx64 " != %016" PRIx64 "\n",i,bank,hash(igs->memory[bank]->pixels->primary),c->hashes[bank]);
				CHECK(hash(igs->memory[bank]->pixels->primary) == c->hashes[bank]);
			}
		}
		destroy(&f);
	}
}

static void
literal(void)
{
	struct fixture f; create(&f,0,false);
	const struct st_console_rect source = {0,0,16,1}, destination = {32,0,16,1};
	seed(&f,&source,769,770); seed(&f,&destination,772,776);
	feed(&f,"G#G 0,6,0,0,15,0,32,0:");
	struct st_pixel_buffer *b = NULL;
	CHECK(st_console_get_pixels(&b,f.console,&destination,false) == 0);
	for (unsigned x = 0; x < 16; ++x) CHECK(b->primary[x] == 773 && b->alternate[x] == 778);
	st_pixel_buffer_destroy(b); destroy(&f);
}

static unsigned
truth(unsigned logic, unsigned source, unsigned destination, unsigned mask)
{
	unsigned value = 0;
	for (unsigned bit = 1; bit <= mask; bit <<= 1) {
		unsigned entry = (source & bit ? 0 : 2) + (destination & bit ? 0 : 1);
		if (logic & (1u << entry)) value |= bit;
	}
	return value;
}

static void
screen_matrix(void)
{
	static const int offsets[][2] = {{0,0},{3,7},{15,0},{0,15},{8,3},{7,15}};
	static const int widths[] = {1,15,16,17,31,33,49};
	for (unsigned resolution = 0; resolution < 3; ++resolution)
		for (unsigned status = 0; status < 2; ++status) {
			struct fixture f; create(&f,resolution,status != 0);
			struct cterm_igs *igs = f.terminal->igs_engine;
			feed(&f,"\033fG#p>3,2:X 10,19,17:\033b");
			struct cterm_igs_attributes saved = igs->attributes;
			const struct st_console_rect area = {0,0,128,12};
			struct st_pixel_buffer *initial = NULL;
			CHECK(st_pixel_buffer_create(&initial,area.width,area.height,true) == 0);
			unsigned mask = cterm_igs_plane_mask(resolution);
			for (unsigned y = 0; y < area.height; ++y) for (unsigned x = 0; x < area.width; ++x) {
				initial->primary[y*128+x] = 768 + (((7*x+3*y)^(x>>3))&mask);
				initial->alternate[y*128+x] = 768 + (((3*x+7*y)^(y>>1))&mask);
			}
			const struct st_pixel_blit restore = {.width = area.width,.height = area.height,.scale_x = 1,.scale_y = 1};
			for (unsigned overlap = 0; overlap < 3; ++overlap)
				for (size_t o = 0; o < sizeof(offsets)/sizeof(*offsets); ++o)
					for (size_t w = 0; w < sizeof(widths)/sizeof(*widths); ++w)
						for (unsigned logic = 0; logic < 16; ++logic) {
							CHECK(st_console_blit_pixels(f.console,initial,NULL,&restore) == 0);
							int sx = offsets[o][0], dx = offsets[o][1]+(overlap == 0 ? 64 : 0);
							int sy = overlap == 2 ? 3 : 1, dy = overlap == 0 ? 6 : 2;
							char wire[96]; (void)snprintf(wire,sizeof(wire),"G#G 0,%u,%d,%d,%d,%d,%d,%d:",
							    logic,sx,sy,sx+widths[w]-1,sy+2,dx,dy); feed(&f,wire);
							struct st_pixel_buffer *got = NULL;
							CHECK(st_console_get_pixels(&got,f.console,&area,false) == 0);
							for (unsigned phase = 0; phase < 2; ++phase) {
								const uint32_t *before = phase == 0 ? initial->primary : initial->alternate;
								const uint32_t *after = phase == 0 ? got->primary : got->alternate;
								for (int y = 0; y < 12; ++y) for (int x = 0; x < 128; ++x) {
									unsigned expected = before[y*128+x];
									if (x >= dx && x < dx+widths[w] && y >= dy && y < dy+3)
										expected = 768 + truth(logic,before[(sy+y-dy)*128+sx+x-dx]-768,expected-768,mask);
									if (after[y*128+x] != expected) fprintf(stderr,"res%u overlap%u phase%u off%zu width%d logic%u at%d,%d: %u != %u\n",
									    resolution,overlap,phase,o,widths[w],logic,x,y,after[y*128+x],expected);
									CHECK(after[y*128+x] == expected);
								}
							}
							st_pixel_buffer_destroy(got);
						}
			CHECK(memcmp(&saved,&igs->attributes,sizeof(saved)) == 0);
			CHECK(igs->draw_x == 19 && igs->draw_y == 17 && f.terminal->xpos == 4 && f.terminal->ypos == 3);
			CHECK(f.terminal->seq_trailing_remain == 1 && igs->last_line);
			CHECK(igs->memory_view[0].screen && igs->memory[0] == NULL);
			st_pixel_buffer_destroy(initial); destroy(&f);
		}
}

static void
capture_and_routes(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f,resolution,true);
		struct cterm_igs *igs = f.terminal->igs_engine;
		unsigned mask = cterm_igs_plane_mask(resolution);
		const struct st_console_rect region = {32,16,32,3}; seed(&f,&region,768+mask,769);
		feed(&f,"G#G 1,3,32,16,63,18:");
		CHECK(igs->memory[0] != NULL && igs->memory_view[0].word_width == 2);
		CHECK(igs->memory_rect[0][0] == 32 && igs->memory_rect[0][1] == 16);
		struct st_pixel_buffer *m = igs->memory[0]->pixels;
		CHECK(m->alternate != NULL && m->primary[16*32+32] == mask && m->alternate[16*32+32] == 1);
		const struct st_console_rect replay = {80,40,32,3}; seed(&f,&replay,768,768);
		feed(&f,"G#G 2,3,80,40:");
		struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f.console,&replay,false) == 0);
		for (unsigned i = 0; i < 96; ++i) CHECK(b->primary[i] == 768+mask && b->alternate[i] == 769);
		st_pixel_buffer_destroy(b);
		for (int route = -1; route <= 6; ++route) for (int op = -1; op <= 4; ++op) {
			igs->memory_route = route;
			for (unsigned i = 0; i < 2; ++i) {
				if (igs->memory[i] == NULL) CHECK(st_igs_image_create(&igs->memory[i],resolution == 0 ? 4 : resolution == 1 ? 2 : 1) == 0);
				CHECK(st_pixel_buffer_fill(igs->memory[i]->pixels,i == 0 ? mask : 0,i == 0 ? mask : 0) == 0);
				memcpy(igs->memory_rect[i],((int[4]){0,0,15,2}),sizeof(igs->memory_rect[i]));
			}
			const struct st_console_rect full = {0,0,igs->width,igs->height}; seed(&f,&full,769,769);
			igs->clipping = false;
			const char *wire = op == 0 || op == 3 || op == 4 ? "G#G 0],3,0,0,15,2,0,4:"
			    : op == 1 ? "G#G 0],3,0,0,15,2:" : "G#G 0],3,0,0:";
			igs->math[0] = (int16_t)op;
			size_t used;
			int status = st_cterm_write(f.terminal,wire,strlen(wire),&used);
			CHECK(status == (op == 2 && (route == 2 || route == 3) ? IO_ERROR_OUT_OF_RANGE : 0));
			CHECK(used == strlen(wire));
			int source = -1, destination = -1;
			if (op == 0 && (route == 0 || route == 1)) source = destination = 2;
			if (op == 4 && (route == 0 || route == 1)) source = destination = route;
			if (op == 1) {
				if (route == 0 || route == 1) { source = 2; destination = route; }
				if (route == 2 || route == 3) { source = route-2; destination = 3-route; }
			}
			if (op == 3 || (op == 2 && route != 2 && route != 3)) {
				if (route == 0 || route == 1) { source = route; destination = 2; }
				if (route == 2 || route == 3) { source = 3-route; destination = route-2; }
				if (route == 4 || route == 5) { source = 2; destination = route-4; }
			}
			unsigned value = source == 0 ? mask : source == 1 ? 0 : 1;
			for (int i = 0; i < 2; ++i) {
				unsigned row = op == 0 || op == 3 || op == 4 ? 4 : 0;
				CHECK(igs->memory[i]->pixels->primary[row*16] == (i == destination ? value : i == 0 ? mask : 0));
			}
			const struct st_console_rect output = {0,op == 0 || op == 3 || op == 4 ? 4u : 0u,16,1};
			CHECK(st_console_get_pixels(&b,f.console,&output,false) == 0);
			CHECK(b->primary[0] == 768+(destination == 2 ? value : 1)); st_pixel_buffer_destroy(b);
		}
		destroy(&f);
	}
}

static int
bad_clock(uint64_t *value)
{
	(void)value; return IO_ERROR_IO;
}

static void
check_row(struct fixture *f, unsigned primary, unsigned alternate)
{
	const struct st_console_rect destination = {32,0,16,1};
	struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f->console,&destination,false) == 0);
	for (unsigned i = 0; i < 16; ++i) CHECK(b->primary[i] == primary && b->alternate[i] == alternate);
	st_pixel_buffer_destroy(b);
}

static void
second_copy(void)
{
	struct fixture f; create(&f,0,false);
	struct cterm_igs *igs = f.terminal->igs_engine;
	const struct st_console_rect source = {0,0,16,1}, destination = {32,0,16,1};
	for (int control = -1; control <= 4; ++control) {
		seed(&f,&source,769,770); seed(&f,&destination,772,776);
		igs->double_step = control;
		feed(&f,"G#G 0,6,0,0,15,0,32,0:");
		if (control <= 1) { CHECK(!igs->copy_pending); check_row(&f,control == 0 ? 773 : 772,control == 0 ? 778 : 776); }
		else {
			CHECK(igs->copy_pending && st_cterm_input_paused(f.terminal)); check_row(&f,773,778);
			CHECK(igs->wait_deadline == local_ms+(uint64_t)((control-1)*1000+59)/60);
			local_ms = igs->wait_deadline-1; CHECK(st_cterm_poll(f.terminal) == 0 && igs->copy_pending);
			/* Completion must reacquire current source/destination pixels. */
			seed(&f,&source,771,775);
			++local_ms; CHECK(st_cterm_poll(f.terminal) == 0 && !igs->copy_pending);
			check_row(&f,774,781);
		}
	}
	igs->double_step = 4;
	seed(&f,&source,769,770); seed(&f,&destination,772,776);
	const char wire[] = "G#G>0,6,0,0,15,0,32,0:M 2:B"; size_t used;
	const char *tail = strchr(wire,'M');
	CHECK(st_cterm_write(f.terminal,wire,sizeof(wire)-1,&used) == 0 && used == (size_t)(tail-wire));
	local_ms = igs->wait_deadline; CHECK(st_cterm_poll(f.terminal) == 0);
	feed(&f,tail); CHECK(igs->attributes.writing_mode == 2); check_row(&f,772,776);
	for (unsigned stop = 0; stop < 3; ++stop) {
		seed(&f,&source,769,770); seed(&f,&destination,772,776); igs->double_step = 4;
		feed(&f,"G#G 0,6,0,0,15,0,32,0:"); CHECK(igs->copy_pending);
		if (stop == 0) CHECK(st_cterm_finish(f.terminal) == 0);
		else if (stop == 1) CHECK(st_cterm_reset_terminal(f.terminal) == 0);
		else {
			seed(&f,&source,ST_PIXEL_COLOR_RGB_VALUE(1,2,3),770);
			local_ms = igs->wait_deadline; CHECK(st_cterm_poll(f.terminal) == IO_ERROR_INVALID_FORMAT);
		}
		CHECK(!igs->copy_pending && !st_cterm_input_paused(f.terminal));
		if (stop != 1) check_row(&f,773,778);
	}
	seed(&f,&source,769,770); seed(&f,&destination,772,776); igs->double_step = 4;
	igs->now_ms = bad_clock;
	const char failed[] = "G#G 0,6,0,0,15,0,32,0:";
	CHECK(st_cterm_write(f.terminal,failed,sizeof(failed)-1,&used) == IO_ERROR_IO && used == sizeof(failed)-1);
	CHECK(!igs->copy_pending && !st_cterm_input_paused(f.terminal)); check_row(&f,773,778);
	igs->now_ms = now; igs->double_step = 0;
	feed(&f,"G#G 0,3,0,0,15,0,32,0:"); check_row(&f,769,770);
	igs->double_step = 4; local_ms = UINT64_MAX-1;
	CHECK(st_cterm_write(f.terminal,failed,sizeof(failed)-1,&used) == IO_ERROR_OUT_OF_RANGE);
	CHECK(!igs->copy_pending && !st_cterm_input_paused(f.terminal)); local_ms = 1000;
	igs->double_step = 0; feed(&f,"G#G 1,3,0,0,15,0:");
	igs->double_step = 4; feed(&f,"G#G 4,10,0,0,15,0,0,0:");
	CHECK(!st_cterm_input_paused(f.terminal) && igs->memory[0]->pixels->primary[0] == 14);
	igs->memory_route = 2; feed(&f,"G#G 1,3,0,0,15,0:");
	CHECK(!st_cterm_input_paused(f.terminal) && igs->memory[1]->pixels->primary[0] == 14);
	igs->memory_route = 4; feed(&f,"G#G 3,6,0,0,15,0,0,0:");
	CHECK(!st_cterm_input_paused(f.terminal) && igs->memory[0]->pixels->primary[0] == 15);
	destroy(&f);
}

static void
random_operands(void)
{
	struct fixture f; create(&f,0,false);
	struct cterm_igs *igs = f.terminal->igs_engine;
	const char wire[] = "G#G r,R,0,0,15,0,32,0:";
	const struct st_console_rect source = {0,0,16,1}, destination = {32,0,16,1};
	for (size_t split = 0; split <= sizeof(wire); ++split) {
		CHECK(st_cterm_reset_terminal(f.terminal) == 0);
		igs->random_range[0][0] = igs->random_range[0][1] = 0;
		igs->random_range[1][0] = igs->random_range[1][1] = 6;
		seed(&f,&source,769,770); seed(&f,&destination,772,776);
		size_t used, size = sizeof(wire)-1;
		if (split > size) {
			for (size_t i = 0; i < size; ++i) CHECK(st_cterm_write(f.terminal,wire+i,1,&used) == 0 && used == 1);
		} else {
			CHECK(st_cterm_write(f.terminal,wire,split,&used) == 0 && used == split);
			CHECK(st_cterm_write(f.terminal,wire+split,size-split,&used) == 0 && used == size-split);
		}
		CHECK(igs->random_seed == UINT32_C(0xc0e5067f)); check_row(&f,773,778);
	}
	igs->random_seed = 1; feed(&f,"G#G r,R,0,0,15,0,32,");
	CHECK(st_cterm_finish(f.terminal) == 0 && igs->random_seed == 1);
	destroy(&f);
}

static void
layout_lifetime(void)
{
	struct fixture f; create(&f,0,false);
	struct cterm_igs *igs = f.terminal->igs_engine;
	const struct st_console_rect source = {0,0,16,1}; seed(&f,&source,769,776);
	feed(&f,"G#G 1,3,0,0,15,0:");
	CHECK(igs->memory[0]->pixels->alternate[0] == 8);
	feed(&f,"G#g 1:");
	feed(&f,"G#R 1,0:"); local_ms = igs->wait_deadline; CHECK(st_cterm_poll(f.terminal) == 0);
	CHECK(igs->memory[0]->planes == 2 && igs->memory[0]->pixels->width == 128000);
	CHECK(igs->memory[0]->pixels->primary[0] == 1 && igs->memory[0]->pixels->alternate[0] == 0);
	CHECK(igs->memory[0]->pixels->primary[16] == 0 && igs->memory[0]->pixels->alternate[16] == 2);
	feed(&f,"G#G 2,3,5000,5000:");
	const struct st_console_rect scaled = {160,100,16,1}; struct st_pixel_buffer *scaled_pixels = NULL;
	CHECK(st_console_get_pixels(&scaled_pixels,f.console,&scaled,false) == 0);
	for (unsigned x = 0; x < 16; ++x) CHECK(scaled_pixels->primary[x] == 769 && scaled_pixels->alternate[x] == 768);
	st_pixel_buffer_destroy(scaled_pixels);
	CHECK(igs->scaling_resolution == 0 && igs->memory_view[0].planes == 2);
	feed(&f,"G#g>0:G>2,3,80,40:G 3,3,16,0,31,0,96,40:");
	const struct st_console_rect destination = {80,40,32,1}; struct st_pixel_buffer *b = NULL;
	CHECK(st_console_get_pixels(&b,f.console,&destination,false) == 0);
	for (unsigned x = 0; x < 32; ++x) CHECK(b->primary[x] == (x < 16 ? 769u : 768u) && b->alternate[x] == (x < 16 ? 768u : 770u));
	st_pixel_buffer_destroy(b);
	CHECK(igs->memory_view[0].planes == 2 && igs->memory_view[0].word_width == 1);
	feed(&f,"G#R 0,0:"); local_ms = igs->wait_deadline; CHECK(st_cterm_poll(f.terminal) == 0);
	for (unsigned operation = 0; operation < 6; ++operation) {
		char wire[20]; (void)snprintf(wire,sizeof(wire),"G#I %u:",operation); feed(&f,wire);
		if (st_cterm_input_paused(f.terminal)) { local_ms = igs->wait_deadline; CHECK(st_cterm_poll(f.terminal) == 0); }
		CHECK(igs->memory[0] != NULL && igs->memory_rect[0][2] == 15);
		CHECK(igs->memory[0]->pixels->primary[0] == 1 && igs->memory[0]->pixels->alternate[0] == 8);
	}
	CHECK(st_cterm_reset_terminal(f.terminal) == 0 && igs->memory[0] == NULL && igs->memory[1] == NULL);
	CHECK(igs->memory_rect[0][2] == 0 && igs->memory_view[0].word_width == 0);
	destroy(&f);
}

static void
partitions_and_bounds(void)
{
	static const char *wires[] = {"G#G>0,6,0,0,15,0,32,0:M 2:B", "G#G>1,3,0,0,15,0:M 2:B",
	    "G#G>1,3,0,0,15,0:G>2,3,32,0:M 2:B", "G#G>1,3,0,0,15,0:G>3,3,0,0,7,0,32,0:M 2:B"};
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f,resolution,true);
		struct cterm_igs *igs = f.terminal->igs_engine;
		unsigned mask = cterm_igs_plane_mask(resolution);
		const struct st_console_rect source = {0,0,16,1}, destination = {32,0,16,1};
		for (size_t w = 0; w < sizeof(wires)/sizeof(*wires); ++w) {
			size_t size = strlen(wires[w]);
			for (size_t split = 0; split <= size+1; ++split) {
				CHECK(st_cterm_reset_terminal(f.terminal) == 0);
				seed(&f,&source,769,768); seed(&f,&destination,768,768);
				size_t used;
				if (split > size) {
					for (size_t i = 0; i < size; ++i) CHECK(st_cterm_write(f.terminal,wires[w]+i,1,&used) == 0 && used == 1);
				} else {
					CHECK(st_cterm_write(f.terminal,wires[w],split,&used) == 0 && used == split);
					CHECK(st_cterm_write(f.terminal,wires[w]+split,size-split,&used) == 0 && used == size-split);
				}
				CHECK(igs->attributes.writing_mode == 2 && f.terminal->xpos == 2 && !igs->copy_pending);
				struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f.console,&destination,false) == 0);
				for (unsigned x = 0; x < 16; ++x) {
					unsigned expected = w == 1 || (w == 3 && x >= 8) ? 768 : 769;
					CHECK(b->primary[x] == expected && b->alternate[x] == 768);
				}
				st_pixel_buffer_destroy(b);
				if (w != 0) CHECK(igs->memory[0]->pixels->primary[0] == 1 && igs->memory[0]->pixels->alternate[0] == 0);
			}
		}
		CHECK(st_cterm_reset_terminal(f.terminal) == 0);
		seed(&f,&source,769,768);
		feed(&f,"G#g>1:G 0,3,0,0,0,0,5000,5000:");
		const struct st_console_rect center = {resolution == 0 ? 160u : 320u,resolution == 2 ? 200u : 100u,1,1};
		struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f.console,&center,false) == 0);
		CHECK(b->primary[0] == 769 && b->alternate[0] == 768); st_pixel_buffer_destroy(b); feed(&f,"G#g 0:");
		if (resolution == 2) {
			const struct st_console_rect from = {0,2,1,1}, to = {32,40,1,1};
			seed(&f,&from,769,768); seed(&f,&to,768,768);
			feed(&f,"G#g>2:G 0,3,0,1,0,1,32,20:");
			CHECK(st_console_get_pixels(&b,f.console,&to,false) == 0);
			CHECK(b->primary[0] == 769 && b->alternate[0] == 768); st_pixel_buffer_destroy(b); feed(&f,"G#g 0:");
		}
		const char *partial[] = {"G#G 0,3,", "G#G 1,3,0,0,15,", "G#G 2,3,0,", "G#G 4,3,0,0,15,0,32,"};
		for (size_t i = 0; i < sizeof(partial)/sizeof(*partial); ++i) {
			feed(&f,partial[i]); CHECK(st_cterm_finish(f.terminal) == 0);
			CHECK(igs->memory[0] == NULL && igs->input_deadline == 0);
			feed(&f,partial[i]); local_ms += 5001; CHECK(st_cterm_poll(f.terminal) == 0);
			CHECK(igs->memory[0] == NULL && igs->input_deadline == 0);
		}
		const char bad[] = "G#G 0,3,0,0,10000,0,32,0:"; size_t used;
		CHECK(st_cterm_write(f.terminal,bad,sizeof(bad)-1,&used) == IO_ERROR_OUT_OF_RANGE && igs->memory[0] == NULL);
		const char unknown[] = "G#G 5,3,0,0:remainder";
		CHECK(st_cterm_write(f.terminal,unknown,sizeof(unknown)-1,&used) == IO_ERROR_OUT_OF_RANGE);
		CHECK(used == strlen("G#G 5,3,0,0:"));
		igs->memory_rect[0][2] = 639;
		const char outside[] = "G#G 4,3,0,199,15,199,0,0:";
		if (resolution == 0) CHECK(st_cterm_write(f.terminal,outside,sizeof(outside)-1,&used) == IO_ERROR_OUT_OF_RANGE);
		igs->memory_rect[0][2] = -17;
		const char stride[] = "G#G 4,3,0,0,15,0,0,0:";
		CHECK(st_cterm_write(f.terminal,stride,sizeof(stride)-1,&used) == IO_ERROR_OUT_OF_RANGE);
		CHECK(igs->memory_view[0].word_width == 0);
		/* Host status rows are never read into a native image or overwritten. */
		CHECK(st_cterm_reset_terminal(f.terminal) == 0);
		unsigned ch = resolution == 2 ? 16 : 8, bottom = igs->height-ch;
		const struct st_console_rect status_row = {0,bottom,igs->width,ch}; seed(&f,&status_row,ST_PIXEL_COLOR_RGB_VALUE(1,2,3),777);
		char wire[96]; (void)snprintf(wire,sizeof(wire),"G#G 0,15,0,0,15,15,0,%u:",bottom-1); feed(&f,wire);
		CHECK(st_console_get_pixels(&b,f.console,&status_row,false) == 0);
		for (size_t i = 0; i < (size_t)igs->width*ch; ++i) CHECK(b->primary[i] == ST_PIXEL_COLOR_RGB_VALUE(1,2,3) && b->alternate[i] == 777);
		st_pixel_buffer_destroy(b);
		CHECK(st_cterm_reset_terminal(f.terminal) == 0);
		seed(&f,&source,768+mask,768); igs->clip_x1 = 35; igs->clip_x2 = 40; igs->clip_y1 = igs->clip_y2 = 0;
		seed(&f,&destination,768,768); feed(&f,"G#G 0,3,0,0,15,0,32,0:");
		CHECK(st_console_get_pixels(&b,f.console,&destination,false) == 0);
		for (unsigned x = 0; x < 16; ++x) CHECK(b->primary[x] == 768u+(x >= 3 && x <= 8 ? mask : 0) && b->alternate[x] == 768);
		st_pixel_buffer_destroy(b);
		CHECK(st_cterm_reset_terminal(f.terminal) == 0);
		feed(&f,"G#G 1,3,9999,9999,9999,9999:");
		CHECK(igs->memory_rect[0][0] == (int)igs->width-1 && igs->memory_rect[0][1] == (int)igs->height-1);
		CHECK(igs->memory_rect[0][2] == (int)igs->width-1 && igs->memory_rect[0][3] == (int)igs->height-1);
		CHECK(igs->memory[0] == NULL); destroy(&f);
	}
}

int
st_cterm_igs_grab_test_main(void)
{
	literal(); screen_matrix(); capture_and_routes(); row_aliases(); second_copy(); random_operands(); layout_lifetime(); partitions_and_bounds(); return 0;
}
