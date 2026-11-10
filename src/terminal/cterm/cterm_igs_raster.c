/* Atari raster policy; independent of the shared graphics wire parser.
 * US TOS 1.04 marker/clip/software-line contracts: chapters 43/44/52. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_igs.h"
#include "cterm.h"
#include "io_error.h"

#include <stdlib.h>

struct point { int x, y; };
struct marker_component { unsigned count; struct point points[5]; };
static const struct { unsigned count; struct marker_component components[3]; } markers[] = {
	{1, {{2,{{0,0},{0,0}}}}},
	{2, {{2,{{0,-3},{0,3}}},{2,{{-4,0},{4,0}}}}},
	{3, {{2,{{0,-3},{0,3}}},{2,{{3,2},{-3,-2}}},{2,{{3,-2},{-3,2}}}}},
	{1, {{5,{{-4,-3},{4,-3},{4,3},{-4,3},{-4,-3}}}}},
	{2, {{2,{{-4,-3},{4,3}}},{2,{{-4,3},{4,-3}}}}},
	{1, {{5,{{-4,0},{0,-3},{4,0},{0,3},{-4,0}}}}},
};

static unsigned
outcode(const struct cterm_igs *igs, struct point p)
{
	return (p.x < igs->clip_x1 ? 1u : p.x > igs->clip_x2 ? 2u : 0u)
	    | (p.y < igs->clip_y1 ? 4u : p.y > igs->clip_y2 ? 8u : 0u);
}

static int
clip_ratio(int a, int b, int divisor)
{
	int64_t product = (int64_t)a * b;
	int64_t quotient = product / divisor, remainder = product % divisor;
	/* Native SMUL_DIV negates a packed remainder/quotient. The nonzero
	 * low word borrows one from a negative remainder's magnitude. Preserve
	 * that bounded rounding rule without word overflow or CPU exceptions. */
	int64_t magnitude = remainder < 0 ? -remainder : remainder;
	if (remainder < 0 && quotient != 0) --magnitude;
	if (2*magnitude >= abs(divisor))
		quotient += (product < 0) != (divisor < 0) ? -1 : 1;
	return (int)quotient;
}

static bool
clip_segment(const struct cterm_igs *igs, struct point *first, struct point *last)
{
	if (igs->clip_x1 > igs->clip_x2 || igs->clip_y1 > igs->clip_y2)
		return false;
	/* Wide coordinates avoid native wrapped deltas and cyclic raw-word
	 * clipping. The cap also makes malformed future clip state harmless. */
	for (unsigned pass = 0; pass < 16; ++pass) {
		unsigned a = outcode(igs, *first), b = outcode(igs, *last);
		if ((a | b) == 0) return true;
		if ((a & b) != 0) return false;
		unsigned selected = a != 0 ? a : b;
		struct point p = *first;
		int dx = last->x-first->x, dy = last->y-first->y;
		if (selected & 3) {
			if (dx == 0) return false;
			p.x = selected & 1 ? igs->clip_x1 : igs->clip_x2;
			p.y += clip_ratio(dy, p.x-first->x, dx);
		} else {
			if (dy == 0) return false;
			p.y = selected & 4 ? igs->clip_y1 : igs->clip_y2;
			p.x += clip_ratio(dx, p.y-first->y, dy);
		}
		*(a != 0 ? first : last) = p;
	}
	return false;
}

struct raster {
	struct st_pixel_buffer *pixels;
	struct st_console_rect area;
	unsigned mode, foreground, plane_mask;
	bool touched;
	int left, top, right, bottom;
};

static void
plot(struct raster *raster, struct point p)
{
	if (p.x < (int)raster->area.x || p.y < (int)raster->area.y
	    || p.x >= (int)(raster->area.x+raster->area.width)
	    || p.y >= (int)(raster->area.y+raster->area.height))
		return;
	if (!raster->touched) {
		raster->left = raster->right = p.x;
		raster->top = raster->bottom = p.y;
		raster->touched = true;
	} else {
		if (p.x < raster->left) raster->left = p.x;
		if (p.x > raster->right) raster->right = p.x;
		if (p.y < raster->top) raster->top = p.y;
		if (p.y > raster->bottom) raster->bottom = p.y;
	}
	size_t offset = (size_t)(p.y-(int)raster->area.y)*raster->area.width
	    + (unsigned)(p.x-(int)raster->area.x);
	uint32_t *phases[] = {raster->pixels->primary, raster->pixels->alternate};
	for (unsigned phase = 0; phase < 2; ++phase) {
		if (raster->mode == 3) {
			uint32_t old = phases[phase][offset];
			/* Only IGS indices have native plane bits. Host UI pixels are
			 * outside the writable canvas; preserve any foreign pixels. */
			if (old >= CTERM_IGS_PALETTE_FIRST && old < CTERM_IGS_PALETTE_FIRST+16)
				phases[phase][offset] = CTERM_IGS_PALETTE_FIRST
				    + ((old-CTERM_IGS_PALETTE_FIRST)^raster->plane_mask);
		} else
			phases[phase][offset] = CTERM_IGS_PALETTE_FIRST+raster->foreground;
	}
}

static void
solid_segment(struct raster *raster, struct point first, struct point last, bool final)
{
	bool omit = raster->mode == 3 && !final;
	if (first.y == last.y && omit && first.x != last.x)
		last.x += first.x < last.x ? -1 : 1;
	/* Horizontal spans and sloped lines are ordered by X. Vertical lines
	 * retain their original Y direction, including XOR endpoint ownership. */
	if (first.x > last.x) { struct point swap = first; first = last; last = swap; }
	int dx = last.x-first.x, dy = abs(last.y-first.y);
	int step_y = first.y <= last.y ? 1 : -1;
	bool x_major = dx >= dy;
	int major = x_major ? dx : dy, minor = x_major ? dy : dx;
	int error = 2*minor-major;
	int count = major+1 - (omit && first.y != last.y ? 1 : 0);
	for (int i = 0; i < count; ++i) {
		plot(raster, first);
		if (error >= 0) {
			if (x_major) first.y += step_y; else ++first.x;
			error -= 2*major;
		}
		if (x_major) ++first.x; else first.y += step_y;
		error += 2*minor;
	}
}

int
cterm_igs_marker(struct cterm_igs *igs, int x, int y)
{
	int scale = igs->attributes.marker_height/11;
	int type = igs->attributes.marker_type-1;
	if (type < 0 || type >= 6 || scale < 1 || scale > 8)
		return IO_ERROR_INVALID_ARGUMENT;
	struct raster raster = {.mode = (unsigned)igs->attributes.writing_mode,
	    .foreground = cterm_igs_physical_pen(igs, igs->attributes.marker_pen),
	    .plane_mask = cterm_igs_plane_mask(igs->resolution)};
	/* Read/modify/publish one small region in both blink phases. Clip the
	 * native segments before restricting writes to the host terminal rows;
	 * reserving a UI status row must not change the native line's phase. */
	int left = x-4*scale, top = y-3*scale;
	int right = x+4*scale, bottom = y+3*scale;
	int drawable_height = igs->terminal->height*(igs->resolution == 2 ? 16 : 8);
	if (left < 0) left = 0;
	if (top < 0) top = 0;
	if (right >= (int)igs->width) right = (int)igs->width-1;
	if (bottom >= drawable_height) bottom = drawable_height-1;
	int status = 0;
	if (left <= right && top <= bottom && raster.mode != 4) {
		raster.area = (struct st_console_rect){(unsigned)left,(unsigned)top,
		    (unsigned)(right-left+1),(unsigned)(bottom-top+1)};
		status = st_console_get_pixels(&raster.pixels, igs->terminal->screen.console, &raster.area, false);
		if (status == 0) {
			for (unsigned c = 0; c < markers[type].count; ++c) {
				const struct marker_component *component = &markers[type].components[c];
				for (unsigned p = 1; p < component->count; ++p) {
					struct point first = {x+scale*component->points[p-1].x,y+scale*component->points[p-1].y};
					struct point last = {x+scale*component->points[p].x,y+scale*component->points[p].y};
					/* v_pmarker always activates inherited clip bounds. It
					 * leaves the workstation's stored clip flag untouched. */
					if (clip_segment(igs, &first, &last))
						solid_segment(&raster, first, last, p+1 == component->count);
				}
			}
			if (raster.touched) {
				const struct st_pixel_blit blit = {
				    .source_x = (unsigned)(raster.left-left), .source_y = (unsigned)(raster.top-top),
				    .width = (unsigned)(raster.right-raster.left+1), .height = (unsigned)(raster.bottom-raster.top+1),
				    .destination_x = raster.left, .destination_y = raster.top, .scale_x = 1, .scale_y = 1};
				status = st_console_blit_pixels(igs->terminal->screen.console, raster.pixels, NULL, &blit);
			}
		}
		st_pixel_buffer_destroy(raster.pixels);
	}
	if (status == 0) { igs->draw_x = x; igs->draw_y = y; }
	return status;
}
