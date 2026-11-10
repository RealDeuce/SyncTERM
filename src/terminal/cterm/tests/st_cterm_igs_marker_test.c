/* P: source-derived geometry, independent rational line oracle, and literal
 * clipped pixels. References 43/44/52/136; no external runtime assets. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm.h"
#include "cterm_igs.h"
#include "st_cterm.h"
#include "io_error.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); \
} } while (0)

struct fixture { st_console_t *console; st_font_catalog_t *fonts; st_cterm_t *terminal; };
static uint64_t local_ms;
static int now(uint64_t *value) { *value = local_ms; return 0; }

static void
create(struct fixture *f, unsigned resolution, bool status)
{
	memset(f, 0, sizeof(*f));
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry = {.columns = resolution == 0 ? 40 : 80,
	    .rows = 25, .cell_width = 8, .cell_height = resolution == 2 ? 16 : 8};
	CHECK(st_console_create(&f->console, &display, &geometry) == 0);
	CHECK(st_font_catalog_create(&f->fonts, f->console) == 0);
	const struct st_cterm_config config = {.console = f->console, .fonts = f->fonts, .font_selector = 44,
	    .terminal_rows = status ? 24 : 25, .status_line = status,
	    .emulation = ST_CTERM_ATARI_ST_VT52, .graphics_mode = ST_CTERM_GRAPHICS_IGS_221,
	    .encoding = ST_CTERM_ENCODING_CODEPAGE, .codepage = ST_CODEPAGE_ATARI_ST, .quiet = true};
	CHECK(st_cterm_create(&f->terminal, &config) == 0);
	f->terminal->igs_engine->now_ms = now; local_ms = 1000;
}

static void destroy(struct fixture *f)
{
	st_cterm_destroy(&f->terminal); st_font_catalog_destroy(f->fonts); st_console_destroy(f->console);
}

static void feed(struct fixture *f, const char *wire)
{
	size_t used;
	int status = st_cterm_write(f->terminal, wire, strlen(wire), &used);
	if (status != 0 || used != strlen(wire))
		fprintf(stderr,"feed %s: status %d used %zu\n",wire,status,used);
	CHECK(status == 0 && used == strlen(wire));
}

static void
seed(struct fixture *f, const struct st_console_rect *rect, unsigned primary, unsigned alternate)
{
	struct st_pixel_buffer *b = NULL;
	CHECK(st_pixel_buffer_create(&b, rect->width, rect->height, true) == 0);
	CHECK(st_pixel_buffer_fill(b, primary, alternate) == 0);
	const struct st_pixel_blit blit = {.width = rect->width, .height = rect->height,
	    .destination_x = (int)rect->x, .destination_y = (int)rect->y, .scale_x = 1, .scale_y = 1};
	CHECK(st_console_blit_pixels(f->console, b, NULL, &blit) == 0);
	st_pixel_buffer_destroy(b);
}

struct point { int x, y; };
struct component { unsigned count; struct point points[5]; };
/* Literal ROM coordinate arrays, including independent component boundaries. */
static const struct { unsigned count; struct component components[3]; } shapes[] = {
	{1, {{2,{{0,0},{0,0}}}}},
	{2, {{2,{{0,-3},{0,3}}},{2,{{-4,0},{4,0}}}}},
	{3, {{2,{{0,-3},{0,3}}},{2,{{3,2},{-3,-2}}},{2,{{3,-2},{-3,2}}}}},
	{1, {{5,{{-4,-3},{4,-3},{4,3},{-4,3},{-4,-3}}}}},
	{2, {{2,{{-4,-3},{4,3}}},{2,{{-4,3},{4,-3}}}}},
	{1, {{5,{{-4,0},{0,-3},{4,0},{0,3},{-4,0}}}}},
};

/* Closed-form rational samples, independent of the production error
 * accumulator. Only unclipped segments use this oracle. */
static void
oracle(unsigned visits[81][97], unsigned type, unsigned size, bool xor_mode)
{
	memset(visits, 0, 81 * 97 * sizeof(unsigned));
	for (unsigned c = 0; c < shapes[type].count; ++c) {
		const struct component *component = &shapes[type].components[c];
		for (unsigned p = 1; p < component->count; ++p) {
			int x1 = component->points[p-1].x * (int)size;
			int y1 = component->points[p-1].y * (int)size;
			int x2 = component->points[p].x * (int)size;
			int y2 = component->points[p].y * (int)size;
			bool nonfinal = xor_mode && p + 1 < component->count;
			if (y1 == y2 && nonfinal && x1 != x2) x2 += x1 < x2 ? -1 : 1;
			if (y1 != y2 && x1 > x2) {
				int t = x1; x1 = x2; x2 = t; t = y1; y1 = y2; y2 = t;
			}
			int dx = abs(x2-x1), dy = abs(y2-y1);
			int major = dx > dy ? dx : dy;
			int count = major + 1 - (nonfinal && y1 != y2 ? 1 : 0);
			for (int i = 0; i < count; ++i) {
				int x = x1, y = y1;
				if (major != 0) {
					x += (x2 < x1 ? -1 : 1) * ((2*i*dx + major)/(2*major));
					y += (y2 < y1 ? -1 : 1) * ((2*i*dy + major)/(2*major));
				}
				++visits[y+40][x+48];
			}
		}
	}
}

static void
matrix(void)
{
	const unsigned pens[] = {0,1,2,3,15};
	const unsigned mapped[] = {0,15,1,2,13};
	const struct st_console_rect area = {40,40,97,81};
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f, resolution, false);
		struct cterm_igs *igs = f.terminal->igs_engine;
		unsigned mask = (unsigned[]){15,3,1}[resolution];
		/* Marker rendering must ignore these line/fill attributes. */
		feed(&f, "G#T>2,1,39:T>2,7,60:A 4,0,1:");
		for (unsigned type = 0; type < 6; ++type)
			for (unsigned size = 1; size <= 8; ++size)
				for (unsigned mode = 1; mode <= 4; ++mode)
					for (unsigned pen = 0; pen < 5; ++pen) {
						char wire[96];
						(void)snprintf(wire, sizeof(wire), "G#T>1,%u,%u:C>0,%u:M %u:", type+1,size,pens[pen],mode);
						feed(&f, wire);
						struct cterm_igs_attributes before;
						memcpy(&before, &igs->attributes, sizeof(before));
						seed(&f, &area, 768+1, 768);
						feed(&f, "G#P 88,80:");
						CHECK(igs->draw_x == 88 && igs->draw_y == 80);
						CHECK(memcmp(&before, &igs->attributes, sizeof(before)) == 0);
						unsigned visits[81][97]; oracle(visits, type, size, mode == 3);
						unsigned physical = (pens[pen] <= mask ? mapped[pen] : 15) & mask;
						struct st_pixel_buffer *pixels = NULL;
						CHECK(st_console_get_pixels(&pixels, f.console, &area, false) == 0);
						for (unsigned plane = 0; plane < 2; ++plane)
							for (unsigned y = 0; y < 81; ++y)
								for (unsigned x = 0; x < 97; ++x) {
									unsigned wanted = plane == 0 ? 1 : 0;
									if (mode <= 2 && visits[y][x]) wanted = physical;
									if (mode == 3 && (visits[y][x] & 1)) wanted ^= mask;
									CHECK((plane == 0 ? pixels->primary : pixels->alternate)[y*97+x] == 768+wanted);
								}
						st_pixel_buffer_destroy(pixels);
					}
		destroy(&f);
	}
}

static void
clipping_and_status(void)
{
	/* Cross at (0,1). Native clip arithmetic yields these exact visits. */
	const struct point expected[] = {{0,1},{1,2},{2,3},{3,3},{4,4},{1,0}};
	for (unsigned resolution = 0; resolution < 3; ++resolution)
		for (unsigned status = 0; status < 2; ++status) {
			struct fixture f; create(&f, resolution, status != 0);
			struct cterm_igs *igs = f.terminal->igs_engine;
			const struct st_console_rect area = {0,0,12,12};
			for (unsigned mode = 1; mode <= 4; ++mode) {
				seed(&f, &area, 768, 768);
				igs->clipping = false;
				char wire[64]; (void)snprintf(wire,sizeof(wire),"G#T>1,5,1:M>%u:P 0,1:",mode); feed(&f,wire);
				CHECK(!igs->clipping && igs->clip_x1 == 0 && igs->clip_y1 == 0);
				struct st_pixel_buffer *b = NULL;
				CHECK(st_console_get_pixels(&b,f.console,&area,false) == 0);
				for (int y = 0; y < 12; ++y) for (int x = 0; x < 12; ++x) {
					bool set = false;
					for (unsigned i = 0; i < sizeof(expected)/sizeof(*expected); ++i)
						if (x == expected[i].x && y == expected[i].y) set = true;
					if (mode == 3 && x == 0 && y == 1) set = false;
					unsigned wanted = 768 + (set && mode != 4 ? (unsigned[]){15,3,1}[resolution] : 0);
					CHECK(b->primary[y*12+x] == wanted && b->alternate[y*12+x] == wanted);
				}
				st_pixel_buffer_destroy(b);
			}
			/* Forced clipping uses inherited bounds even with record flag off. */
			seed(&f,&area,768,768);
			igs->clip_x1 = 3; igs->clip_y1 = 4; igs->clip_x2 = 7; igs->clip_y2 = 6;
			feed(&f,"G#T>1,2,2:M>1:P 5,5:");
			struct st_pixel_buffer *b = NULL;
			CHECK(st_console_get_pixels(&b,f.console,&area,false) == 0);
			for (int y = 0; y < 12; ++y) for (int x = 0; x < 12; ++x) {
				bool set = (x == 5 && y >= 4 && y <= 6) || (y == 5 && x >= 3 && x <= 7);
				CHECK(b->primary[y*12+x] == 768 + (set ? (unsigned[]){15,3,1}[resolution] : 0));
			}
			st_pixel_buffer_destroy(b);
			CHECK(!igs->clipping && igs->clip_x1 == 3 && igs->clip_y2 == 6);
			cterm_igs_clip_screen(igs);
			/* Diamond corner: the negative remainder borrow makes (0,1)
			 * ->(1,0), rather than a symmetric nearest-rounded intersection. */
			const unsigned rows[2][6] = {{0x12,0x61,0x80,0x61,0x16,0x08},
			    {0x10,0x61,0x00,0x61,0x16,0x00}};
			for (unsigned mode = 0; mode < 2; ++mode) {
				seed(&f,&area,768,768);
				feed(&f,mode == 0 ? "G#T>1,6,1:M>1:P 3,2:" : "G#M>3:P 3,2:");
				CHECK(st_console_get_pixels(&b,f.console,&area,false) == 0);
				for (unsigned y = 0; y < 12; ++y) for (unsigned x = 0; x < 12; ++x) {
					bool set = y < 6 && (rows[mode][y] & (1u << x));
					CHECK(b->primary[y*12+x] == 768u + (set ? (unsigned[]){15,3,1}[resolution] : 0));
				}
				st_pixel_buffer_destroy(b);
			}
			unsigned ch = resolution == 2 ? 16 : 8;
			const struct st_console_rect bottom = {0,igs->height-ch,igs->width,ch};
			seed(&f,&bottom,768,768);
			char wire[64]; (void)snprintf(wire,sizeof(wire),"G#T>1,2,8:M>1:P 0,%u:",igs->height-1); feed(&f,wire);
			CHECK(st_console_get_pixels(&b,f.console,&bottom,false) == 0);
			CHECK(b->primary[(ch-1)*bottom.width] == (status ? 768u : 768u+(unsigned[]){15,3,1}[resolution]));
			if (status)
				for (size_t p = 0; p < (size_t)bottom.width*bottom.height; ++p)
					CHECK(b->primary[p] == 768 && b->alternate[p] == 768);
			st_pixel_buffer_destroy(b);
			/* A foreign host pixel has no native plane index. */
			seed(&f,&area,ST_PIXEL_COLOR_RGB_VALUE(1,2,3),42);
			feed(&f,"G#T>1,1,8:M>3:P 5,5:");
			CHECK(st_console_get_pixels(&b,f.console,&area,false) == 0);
			CHECK(b->primary[5*12+5] == ST_PIXEL_COLOR_RGB_VALUE(1,2,3) && b->alternate[5*12+5] == 42);
			st_pixel_buffer_destroy(b);
			/* Large/signed centres are clipped safely without native word wrap. */
			igs->math[0] = INT16_MIN; igs->math[1] = INT16_MAX;
			feed(&f,"G#P 0],1]:"); CHECK(igs->draw_x == INT16_MIN && igs->draw_y == INT16_MAX);
			feed(&f,"G#P 9999,9999:"); CHECK(igs->draw_x == 9999 && igs->draw_y == 9999);
			destroy(&f);
		}
	/* Axis metadata never rescales values produced by r/R. Every input
	 * partition advances each operand once, only at command completion. */
	const char random_wire[] = "G#g>1:P r,R:";
	for (size_t split = 0; split <= sizeof(random_wire); ++split) {
		struct fixture f; create(&f,0,false); size_t used;
		struct cterm_igs *igs = f.terminal->igs_engine;
		igs->random_range[0][1] = igs->random_range[1][1] = 255;
		if (split == sizeof(random_wire)) {
			for (size_t i = 0; i < sizeof(random_wire)-1; ++i)
				CHECK(st_cterm_write(f.terminal,random_wire+i,1,&used) == 0 && used == 1);
		} else {
			CHECK(st_cterm_write(f.terminal,random_wire,split,&used) == 0 && used == split);
			CHECK(st_cterm_write(f.terminal,random_wire+split,sizeof(random_wire)-1-split,&used) == 0
			    && used == sizeof(random_wire)-1-split);
		}
		CHECK(igs->draw_x == 230 && igs->draw_y == 202 && igs->random_seed == UINT32_C(0x5eb5ca17));
		igs->random_seed = 1;
		feed(&f,"G#P r,"); CHECK(st_cterm_finish(f.terminal) == 0 && igs->random_seed == 1);
		destroy(&f);
	}
}

static void
input_and_scaling(void)
{
	const char wire[] = "AG#T>1,1,1:P>20,30:P 40,50:B";
	for (unsigned resolution = 0; resolution < 3; ++resolution)
		for (size_t split = 0; split <= sizeof(wire); ++split) {
			struct fixture f; create(&f,resolution,false); size_t used;
			if (split == sizeof(wire)) {
				for (size_t i = 0; i < sizeof(wire)-1; ++i)
					CHECK(st_cterm_write(f.terminal,wire+i,1,&used) == 0 && used == 1);
			} else {
				CHECK(st_cterm_write(f.terminal,wire,split,&used) == 0 && used == split);
				CHECK(st_cterm_write(f.terminal,wire+split,sizeof(wire)-1-split,&used) == 0 && used == sizeof(wire)-1-split);
			}
			CHECK(st_cterm_finish(f.terminal) == 0);
			struct cterm_igs *igs = f.terminal->igs_engine;
			CHECK(igs->draw_x == 40 && igs->draw_y == 50);
			const struct st_console_rect area = {20,30,1,1}; struct st_pixel_buffer *b = NULL;
			CHECK(st_console_get_pixels(&b,f.console,&area,false) == 0);
			CHECK((b->primary[0] == 768u+(unsigned[]){15,3,1}[resolution])); st_pixel_buffer_destroy(b);
			struct st_console_cell cells[2]; const struct st_console_rect text = {0,0,2,1};
			CHECK(st_console_read_cells(f.console,&text,cells,2) == 0 && cells[0].glyph == 'A' && cells[1].glyph == 'B');
			feed(&f,"G#P 10,"); CHECK(st_cterm_finish(f.terminal) == 0 && igs->draw_x == 40);
			feed(&f,"G#P 10,"); local_ms += 5001; CHECK(st_cterm_poll(f.terminal) == 0 && igs->draw_x == 40);
			CHECK(st_cterm_write(f.terminal,"G#P 10000,1:",strlen("G#P 10000,1:"),&used) == IO_ERROR_OUT_OF_RANGE);
			CHECK(igs->draw_x == 40);
			feed(&f,"G#g>1:P 5000,5000:");
			CHECK(igs->draw_x == (resolution == 0 ? 160 : 320) && igs->draw_y == (resolution == 2 ? 200 : 100));
			feed(&f,"G#g>2:P 20,30:"); CHECK(igs->draw_x == 20 && igs->draw_y == (resolution == 2 ? 60 : 30));
			igs->math[20] = -1; igs->math[resolution == 2 ? 60 : 30] = 70;
			feed(&f,"G#P 20],30]:"); CHECK(igs->draw_x == -1 && igs->draw_y == 70);
			CHECK(st_cterm_reset_terminal(f.terminal) == 0 && igs->draw_x == 0 && igs->draw_y == 0);
			feed(&f,"G#P 2,3:"); CHECK(igs->draw_x == 2 && igs->draw_y == 3);
			destroy(&f);
		}
}

int st_cterm_igs_marker_test_main(void)
{
	matrix(); clipping_and_status(); input_and_scaling(); return 0;
}
