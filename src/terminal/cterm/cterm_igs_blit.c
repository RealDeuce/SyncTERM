/* IGS G routing, retained image views and indexed Boolean transfers. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_igs.h"
#include "cterm.h"
#include "st_igs_image.h"
#include "io_error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

struct copy_rect { int sx, sy, dx, dy, width, height; };
struct bank {
	struct st_pixel_buffer *pixels;
	int stride, id;
	size_t capacity;
	unsigned base;
};

static void
order(int *first, int *last)
{
	if (*first > *last) { int swap = *first; *first = *last; *last = swap; }
}

/* Source and destination clipping retain their displacement. Memory forms
 * have no logical width/height clip: only their full decoded capacity bounds
 * an address. The host canvas/status boundary always applies to screens. */
static void
clip_axis(int *source, int *destination, int *count, int lower, int upper, bool from)
{
	int *coordinate = from ? source : destination;
	if (*coordinate < lower) {
		int delta = lower-*coordinate;
		*source += delta; *destination += delta; *count -= delta;
	}
	if (*count > upper-*coordinate+1) *count = upper-*coordinate+1;
}

static int
prepare_rect(const struct cterm_igs *igs, int source, int destination,
    const int points[8], struct copy_rect *r)
{
	int p[8]; memcpy(p,points,sizeof(p));
	order(&p[0],&p[2]); order(&p[1],&p[3]);
	order(&p[4],&p[6]); order(&p[5],&p[7]);
	*r = (struct copy_rect){p[0],p[1],p[4],p[5],p[2]-p[0]+1,p[3]-p[1]+1};
	/* Native clipped screen copies derive maxima from source size. Other
	 * mismatches rely on unread locals/word fringes rather than an image
	 * geometry; reject those malformed transfers before touching pixels. */
	if (!(destination == 2 && igs->clipping)
	    && (p[6]-p[4]+1 != r->width || p[7]-p[5]+1 != r->height))
		return IO_ERROR_OUT_OF_RANGE;
	if (destination == 2 && igs->clipping) {
		clip_axis(&r->sx,&r->dx,&r->width,igs->clip_x1,igs->clip_x2,false);
		clip_axis(&r->sy,&r->dy,&r->height,igs->clip_y1,igs->clip_y2,false);
	}
	int bottom = igs->terminal->height * (igs->resolution == 2 ? 16 : 8)-1;
	if (destination == 2) {
		clip_axis(&r->sx,&r->dx,&r->width,0,(int)igs->width-1,false);
		clip_axis(&r->sy,&r->dy,&r->height,0,bottom,false);
	}
	if (source == 2) {
		clip_axis(&r->sx,&r->dx,&r->width,0,(int)igs->width-1,true);
		clip_axis(&r->sy,&r->dy,&r->height,0,bottom,true);
	}
	return 0;
}

static int
open_bank(struct cterm_igs *igs, int id, struct st_pixel_buffer *screen, struct bank *bank)
{
	*bank = (struct bank){.id = id};
	if (id == 2) {
		bank->pixels = screen; bank->stride = (int)igs->width;
		bank->base = CTERM_IGS_PALETTE_FIRST;
		bank->capacity = (size_t)screen->width * screen->height;
		return 0;
	}
	if (igs->memory_view[id].word_width <= 0) return IO_ERROR_OUT_OF_RANGE;
	unsigned planes = igs->resolution == 0 ? 4 : igs->resolution == 1 ? 2 : 1;
	int status = 0;
	if (igs->memory[id] == NULL) status = st_igs_image_create(&igs->memory[id],planes);
	if (status != 0) return status;
	if (igs->memory[id]->planes != planes) return IO_ERROR_INVALID_FORMAT;
	bank->pixels = igs->memory[id]->pixels;
	bank->stride = igs->memory_view[id].word_width*16;
	bank->capacity = bank->pixels->width;
	return 0;
}

static uint32_t *
phase_pixels(const struct bank *bank, unsigned phase)
{
	return phase != 0 && bank->pixels->alternate != NULL ? bank->pixels->alternate : bank->pixels->primary;
}

static int
check_region(const struct bank *bank, int x, int y, int width, int height, bool values)
{
	if (x < 0 || y < 0 || (int64_t)(y+height-1)*bank->stride + x+width > (int64_t)bank->capacity)
		return IO_ERROR_OUT_OF_RANGE;
	if (!values) return 0;
	for (unsigned phase = 0; phase < 2; ++phase) {
		const uint32_t *pixels = phase_pixels(bank,phase);
		for (int row = 0; row < height; ++row)
			for (int col = 0; col < width; ++col) {
				uint32_t value = pixels[(size_t)(y+row)*bank->stride+x+col];
				if (value < bank->base || value-bank->base >= 16) return IO_ERROR_INVALID_FORMAT;
			}
	}
	return 0;
}

static unsigned
boolean_pixel(int logic, unsigned source, unsigned destination, unsigned mask)
{
	switch (logic) {
	case 0: return 0;
	case 1: return source & destination;
	case 2: return source & ~destination & mask;
	case 3: return source;
	case 4: return ~source & destination & mask;
	case 5: return destination;
	case 6: return source ^ destination;
	case 7: return source | destination;
	case 8: return ~(source | destination) & mask;
	case 9: return ~(source ^ destination) & mask;
	case 10: return ~destination & mask;
	case 11: return (source | ~destination) & mask;
	case 12: return ~source & mask;
	case 13: return (~source | destination) & mask;
	case 14: return ~(source & destination) & mask;
	default: return mask;
	}
}

/* Keep the two neighboring indexed groups needed by a shifted output group.
 * Fetch each new source group before writing the output; keep the carried
 * group across fringes, including overlapping/self and aliased-row copies.
 * This is a small source cache, not a planar image or whole-source snapshot. */
struct source_cache { size_t key[2]; unsigned next; uint32_t pixels[2][16]; };
static const uint32_t *
source_group(struct source_cache *cache, const struct bank *source, unsigned phase, size_t key)
{
	for (unsigned i = 0; i < 2; ++i) if (cache->key[i] == key) return cache->pixels[i];
	unsigned slot = cache->next; cache->next ^= 1;
	cache->key[slot] = key;
	const uint32_t *pixels = phase_pixels(source,phase);
	for (unsigned i = 0; i < 16; ++i) cache->pixels[slot][i] = pixels[key+i]-source->base;
	return cache->pixels[slot];
}

static int
copy_pixels(struct cterm_igs *igs, int logic, int source_id, int destination_id, const int points[8])
{
	if (logic < 0 || logic == 5) return 0;
	if (source_id < 2 && igs->memory_view[source_id].screen) source_id = 2;
	if (destination_id < 2 && igs->memory_view[destination_id].screen) destination_id = 2;
	struct copy_rect r;
	int status = prepare_rect(igs,source_id,destination_id,points,&r);
	if (status != 0 || r.width <= 0 || r.height <= 0) return status;
	struct st_pixel_buffer *screen = NULL;
	const struct st_console_rect area = {0,0,igs->width,
	    (uint32_t)(igs->terminal->height*(igs->resolution == 2 ? 16 : 8))};
	if (source_id == 2 || destination_id == 2)
		status = st_console_get_pixels(&screen,igs->terminal->screen.console,&area,false);
	struct bank source, destination;
	if (status == 0) status = open_bank(igs,source_id,screen,&source);
	if (status == 0) status = open_bank(igs,destination_id,screen,&destination);
	bool unary = logic == 0 || logic == 10 || logic == 15;
	if (status == 0 && !unary) status = check_region(&source,r.sx,r.sy,r.width,r.height,true);
	if (status == 0) status = check_region(&destination,r.dx,r.dy,r.width,r.height,true);
	if (status == 0 && source.pixels->alternate != NULL)
		status = st_pixel_buffer_ensure_alternate(destination.pixels);
	if (status != 0) { st_pixel_buffer_destroy(screen); return status; }
	size_t source_start = (size_t)r.sy*source.stride + (unsigned)r.sx/16*16;
	size_t destination_start = (size_t)r.dy*destination.stride + (unsigned)r.dx/16*16;
	/* Normal accessory memory precedes the logical screen. Only ordering,
	 * never a foreign pointer or process address, is needed for traversal. */
	bool reverse = source.id < destination.id || (source.id == destination.id
	    && (source_start < destination_start || (source_start == destination_start && r.sx%16 < r.dx%16)));
	unsigned mask = cterm_igs_plane_mask(igs->resolution);
	unsigned phases = destination.pixels->alternate == NULL ? 1 : 2;
	for (unsigned phase = 0; phase < phases; ++phase) {
		uint32_t *output = phase_pixels(&destination,phase);
		for (int row = 0; row < r.height; ++row) {
			int y = reverse ? r.height-1-row : row;
			struct source_cache cache = {.key = {SIZE_MAX,SIZE_MAX}};
			int first = r.dx/16, last = (r.dx+r.width-1)/16;
			for (int group = reverse ? last : first; reverse ? group >= first : group <= last; group += reverse ? -1 : 1) {
				int left = group*16 > r.dx ? group*16 : r.dx;
				int right = group*16+15 < r.dx+r.width-1 ? group*16+15 : r.dx+r.width-1;
				int sx = r.sx+left-r.dx, ex = r.sx+right-r.dx;
				size_t base = (size_t)(r.sy+y)*source.stride;
				const uint32_t *low = NULL, *high = NULL;
				if (!unary) {
					if (reverse) high = source_group(&cache,&source,phase,base+(unsigned)ex/16*16);
					low = source_group(&cache,&source,phase,base+(unsigned)sx/16*16);
					if (!reverse) high = source_group(&cache,&source,phase,base+(unsigned)ex/16*16);
				}
				for (int x = left; x <= right; ++x) {
					int input = r.sx+x-r.dx;
					unsigned s = unary ? 0 : (input/16 == sx/16 ? low : high)[input%16] & mask;
					size_t index = (size_t)(r.dy+y)*destination.stride+x;
					unsigned d = (output[index]-destination.base) & mask;
					output[index] = destination.base+boolean_pixel(logic,s,d,mask);
				}
			}
		}
	}
	if (destination_id == 2) {
		const struct st_pixel_blit blit = {.source_x = (unsigned)r.dx,.source_y = (unsigned)r.dy,
		    .width = (unsigned)r.width,.height = (unsigned)r.height,
		    .destination_x = r.dx,.destination_y = r.dy,.scale_x = 1,.scale_y = 1};
		status = st_console_blit_pixels(igs->terminal->screen.console,screen,NULL,&blit);
	}
	st_pixel_buffer_destroy(screen);
	return status;
}

int
cterm_igs_grab_resume(struct cterm_igs *igs)
{
	igs->copy_pending = false;
	return copy_pixels(igs,igs->copy_logic,igs->copy_source,igs->copy_destination,igs->copy_points);
}

int
cterm_igs_grab(struct cterm_igs *igs, const int *values, size_t count)
{
	if (count < 4) return IO_ERROR_PROTOCOL;
	int operation = values[0], logic = values[1] > 15 ? 3 : values[1], route = igs->memory_route;
	size_t required = operation == 0 || operation == 3 || operation == 4 ? 8 : operation == 1 ? 6 : 4;
	if (count != required) return IO_ERROR_PROTOCOL;
	if (operation > 4 || (operation == 2 && (route == 2 || route == 3)))
		return IO_ERROR_OUT_OF_RANGE; /* The native handler uses unread stack coordinates. */
	int p[8] = {values[2],values[3],0,0,0,0,0,0};
	if (count >= 6) { p[2] = values[4]; p[3] = values[5]; }
	if (count == 8) { p[4] = values[6]; p[5] = values[7]; }
	int selected = route == 1 || route == 5 ? 1 : 0;
	if (operation == 2 && (route == 0 || route == 1 || route == 4 || route == 5)) {
		p[2] = igs->memory_rect[selected][2]-igs->memory_rect[selected][0]+p[0];
		p[3] = igs->memory_rect[selected][3]-igs->memory_rect[selected][1]+p[1];
	}
	for (unsigned i = 0; i < 6; ++i) {
		int limit = i%2 == 0 ? (int)igs->width-1 : (int)igs->height-1;
		if (p[i] > limit) p[i] = limit;
	}
	if (operation == 1 && (route == 0 || route == 1 || route == 4 || route == 5))
		memcpy(igs->memory_rect[selected],p,4*sizeof(*p));
	for (unsigned i = 0; i < 2; ++i) {
		const int *saved = igs->memory_rect[i];
		int width = saved[2]-saved[0], height = saved[3]-saved[1];
		int words = width/16+1;
		igs->memory_view[i] = (struct cterm_igs_memory_view){width,height,words > 40 ? 40 : words,
		    igs->resolution == 0 ? 4u : igs->resolution == 1 ? 2u : 1u,false};
	}
	int origin_x = p[0], origin_y = p[1];
	if (operation == 2 && (route == 0 || route == 1)) {
		memcpy(p,igs->memory_rect[selected],4*sizeof(*p));
		p[4] = origin_x; p[5] = origin_y;
		p[6] = p[2]-p[0]+origin_x; p[7] = p[3]-p[1]+origin_y;
	} else if (operation == 0 || operation == 3 || operation == 4) {
		p[6] = p[2]-p[0]+p[4]; p[7] = p[3]-p[1]+p[5];
	} else { p[4] = p[0]; p[5] = p[1]; p[6] = p[2]; p[7] = p[3]; }
	for (unsigned i = 0; i < 8; ++i) if (p[i] < 0) p[i] = 0;
	int source = -1, destination = -1;
	bool repeat = false;
	if (operation == 0 && (route == 0 || route == 1)) {
		source = destination = 2; repeat = true;
		igs->memory_view[route] = (struct cterm_igs_memory_view){.screen = true};
	} else if (operation == 4 && (route == 0 || route == 1)) source = destination = route;
	else if (operation == 1) {
		if (route == 0 || route == 1) { source = 2; destination = route; repeat = true; }
		else if (route == 2) { source = 0; destination = 1; }
		else if (route == 3) { source = 1; destination = 0; }
	} else if (operation == 2 || operation == 3) {
		if (route == 0 || route == 1) { source = route; destination = 2; repeat = true; }
		else if (route == 2) { source = 1; destination = 0; }
		else if (route == 3) { source = 0; destination = 1; }
		else if (route == 4 || route == 5) { source = 2; destination = route-4; }
	}
	if (source < 0) return 0;
	int status = copy_pixels(igs,logic,source,destination,p);
	if (status != 0 || !repeat || igs->double_step == 0) return status;
	if (igs->double_step <= 1) return copy_pixels(igs,logic,source,destination,p);
	igs->copy_source = source; igs->copy_destination = destination; igs->copy_logic = logic;
	memcpy(igs->copy_points,p,sizeof(p));
	status = cterm_igs_wait_sync(igs,(unsigned)igs->double_step-1);
	igs->copy_pending = status == IO_ERROR_IN_PROGRESS;
	return status;
}
