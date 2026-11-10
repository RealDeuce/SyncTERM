/* Atari raster policy; independent of the shared graphics wire parser.
 * US TOS 1.04 lines, polygons, rectangles, caps, arrows and markers. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_igs.h"
#include "cterm_igs_patterns.h"
#include "cterm_igs_trig.h"
#include "cterm.h"
#include "io_error.h"

#include <stdlib.h>

enum { CURVE_STEPS_MIN = 32, CURVE_STEPS_MAX = 128 };
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
	struct cterm_igs *igs;
	struct st_pixel_buffer *pixels;
	struct st_console_rect area;
	unsigned mode, foreground, plane_mask;
	uint16_t mask;
	bool last_line;
	bool touched;
	int left, top, right, bottom;
};

static void
plot(struct raster *raster, struct point p, bool pattern)
{
	if ((!pattern && (raster->mode == 2 || raster->mode == 3))
	    || (pattern && raster->mode == 4))
		return;
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
			phases[phase][offset] = CTERM_IGS_PALETTE_FIRST
			    + (raster->mode == 1 && !pattern ? 0 : raster->foreground);
	}
}

static void
pattern_segment(struct raster *raster, struct point first, struct point last, bool final)
{
	bool horizontal = first.y == last.y;
	uint16_t mask = raster->mask;
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
		unsigned phase = horizontal ? (unsigned)first.x & 15 : (unsigned)i & 15;
		plot(raster, first, (mask & (0x8000u >> phase)) != 0);
		if (error >= 0) {
			if (x_major) first.y += step_y; else ++first.x;
			error -= 2*major;
		}
		if (x_major) ++first.x; else first.y += step_y;
		error += 2*minor;
	}
	unsigned rotation = (unsigned)count & 15;
	if (rotation != 0)
		raster->mask = (uint16_t)((unsigned)mask << rotation | mask >> (16-rotation));
}

static int
publish(struct raster *raster)
{
	if (!raster->touched)
		return 0;
	const struct st_pixel_blit blit = {
	    .source_x = (unsigned)raster->left-raster->area.x,
	    .source_y = (unsigned)raster->top-raster->area.y,
	    .width = (unsigned)(raster->right-raster->left+1),
	    .height = (unsigned)(raster->bottom-raster->top+1),
	    .destination_x = raster->left, .destination_y = raster->top, .scale_x = 1, .scale_y = 1};
	return st_console_blit_pixels(raster->igs->terminal->screen.console, raster->pixels, NULL, &blit);
}

/* Canonical workstation patterns have row masks 0/3/7/15, so the native
 * rectangle's sequential row loop is equivalent to absolute Y anchoring.
 * IGS uploads only the single-plane sixteen-row user pattern. */
static int
fill_pattern(const struct cterm_igs *igs, const uint16_t **rows, unsigned *mask)
{
	static const uint16_t plain[] = {0,0xffff};
	int interior = igs->attributes.fill_interior, index = igs->attributes.fill_index;
	*mask = 0;
	switch (interior) {
	case 0: case 1: *rows = &plain[interior]; break;
	case 2:
		if (index < 1 || index > 24) return IO_ERROR_INVALID_ARGUMENT;
		if (index <= 8) { *rows = fill_dither[index-1]; *mask = 3; }
		else { *rows = fill_oempat[index-9]; *mask = 7; }
		break;
	case 3:
		if (index < 1 || index > 12) return IO_ERROR_INVALID_ARGUMENT;
		if (index <= 6) { *rows = fill_hatch0[index-1]; *mask = 7; }
		else { *rows = fill_hatch1[index-7]; *mask = 15; }
		break;
	case 4: *rows = igs->attributes.user_fill; *mask = 15; break;
	default: return IO_ERROR_INVALID_ARGUMENT;
	}
	return 0;
}

int
cterm_igs_rectangle(struct cterm_igs *igs, int x1, int y1, int x2, int y2)
{
	const uint16_t *rows;
	unsigned mask;
	int status = fill_pattern(igs, &rows, &mask);
	if (status != 0) return status;
	if (x1 > x2) { int swap = x1; x1 = x2; x2 = swap; }
	if (y1 > y2) { int swap = y1; y1 = y2; y2 = swap; }
	if (igs->clipping) {
		if (igs->clip_x1 > igs->clip_x2 || igs->clip_y1 > igs->clip_y2) return 0;
		if (x1 < igs->clip_x1) x1 = igs->clip_x1;
		if (x2 > igs->clip_x2) x2 = igs->clip_x2;
		if (y1 < igs->clip_y1) y1 = igs->clip_y1;
		if (y2 > igs->clip_y2) y2 = igs->clip_y2;
	}
	/* Host canvas/status bounds restrict writes without re-anchoring rows or
	 * turning private corner ordering into an IGS Drawto-state mutation. */
	int drawable = igs->terminal->height*(igs->resolution == 2 ? 16 : 8);
	if (x1 < 0) x1 = 0;
	if (y1 < 0) y1 = 0;
	if (x2 >= (int)igs->width) x2 = (int)igs->width-1;
	if (y2 >= drawable) y2 = drawable-1;
	if (x1 > x2 || y1 > y2) return 0;
	struct raster raster = {.igs = igs, .mode = (unsigned)igs->attributes.writing_mode,
	    .foreground = cterm_igs_physical_pen(igs, igs->attributes.fill_pen),
	    .plane_mask = cterm_igs_plane_mask(igs->resolution),
	    .area = {(unsigned)x1,(unsigned)y1,(unsigned)(x2-x1+1),(unsigned)(y2-y1+1)}};
	status = st_console_get_pixels(&raster.pixels, igs->terminal->screen.console, &raster.area, false);
	if (status != 0) return status;
	for (int y = y1; y <= y2; ++y) {
		uint16_t bits = rows[(unsigned)y & mask];
		for (int x = x1; x <= x2; ++x)
			plot(&raster, (struct point){x,y}, (bits & (0x8000u >> ((unsigned)x & 15))) != 0);
	}
	status = publish(&raster);
	st_pixel_buffer_destroy(raster.pixels);
	return status;
}

int
cterm_igs_marker(struct cterm_igs *igs, int x, int y)
{
	int scale = igs->attributes.marker_height/11;
	int type = igs->attributes.marker_type-1;
	if (type < 0 || type >= 6 || scale < 1 || scale > 8)
		return IO_ERROR_INVALID_ARGUMENT;
	struct raster raster = {.igs = igs, .mask = 0xffff, .mode = (unsigned)igs->attributes.writing_mode,
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
						pattern_segment(&raster, first, last, p+1 == component->count);
				}
			}
			status = publish(&raster);
		}
		st_pixel_buffer_destroy(raster.pixels);
	}
	if (status == 0) { igs->draw_x = x; igs->draw_y = y; igs->last_line = true; }
	return status;
}

static void
segment(struct raster *raster, struct point first, struct point last, bool force_clip)
{
	if ((force_clip || raster->igs->clipping) && !clip_segment(raster->igs, &first, &last))
		return;
	pattern_segment(raster, first, last, raster->last_line);
}

/* Native patterned spans and optional solid border. A full circle supplies
 * up to 129 vertices, including its explicit final point. */
static void
fill_polygon(struct raster *raster, const struct point *points, unsigned count,
    const uint16_t *rows, unsigned row_mask, bool perimeter)
{
	int low = points[0].y, high = low;
	raster->last_line = false;
	for (unsigned i = 1; i < count; ++i) {
		if (points[i].y < low) low = points[i].y;
		if (points[i].y > high) high = points[i].y;
	}
	const struct cterm_igs *igs = raster->igs;
	if (igs->clipping) {
		if (igs->clip_x1 > igs->clip_x2 || igs->clip_y1 > igs->clip_y2)
			return;
		if (low < igs->clip_y1) {
			if (high < igs->clip_y1) return;
			low = igs->clip_y1-1;
		}
		if (high > igs->clip_y2) {
			if (low > igs->clip_y2) return;
			high = igs->clip_y2;
		}
	}
	int first = high, limit = low;
	if (first >= (int)(raster->area.y+raster->area.height))
		first = (int)(raster->area.y+raster->area.height)-1;
	if (limit < (int)raster->area.y-1) limit = (int)raster->area.y-1;
	for (int y = first; y > limit; --y) {
		int intersections[CURVE_STEPS_MAX+1]; unsigned found = 0;
		for (unsigned i = 0; i < count; ++i) {
			struct point a = points[i], b = points[(i+1)%count];
			if (!((a.y <= y && y < b.y) || (b.y <= y && y < a.y))) continue;
			int64_t q = (INT64_C(2)*(b.x-a.x)*(y-a.y))/(b.y-a.y);
			int x = a.x + (int)(q < 0 ? -((-q+1)/2) : (q+1)/2);
			unsigned j = found++;
			while (j != 0 && intersections[j-1] > x) {
				intersections[j] = intersections[j-1]; --j;
			}
			intersections[j] = x;
		}
		for (unsigned i = 1; i < found; i += 2) {
			int left = intersections[i-1], right = intersections[i];
			if (igs->clipping) {
				if (left < igs->clip_x1) left = igs->clip_x1;
				if (right > igs->clip_x2) right = igs->clip_x2;
			}
			if (left < (int)raster->area.x) left = (int)raster->area.x;
			if (right >= (int)(raster->area.x+raster->area.width))
				right = (int)(raster->area.x+raster->area.width)-1;
			uint16_t bits = rows[(unsigned)y & row_mask];
			for (int x = left; x <= right; ++x)
				plot(raster, (struct point){x,y}, (bits & (0x8000u >> ((unsigned)x & 15))) != 0);
		}
	}
	if (!perimeter) return;
	raster->mask = 0xffff;
	for (unsigned i = 0; i < count; ++i) {
		raster->last_line = i+1 == count;
		segment(raster, points[i], points[(i+1)%count], false);
	}
}

/* Wide shafts and arrowheads temporarily select solid fill/perimeter. */
static void
polygon(struct raster *raster, const struct point *points, unsigned count)
{
	static const uint16_t solid = 0xffff;
	fill_polygon(raster, points, count, &solid, 0, true);
}

/* Preserve native interpolation on periodic angles without signed-word
 * overflow or indexing the foreign ROM before its actual sine table. */
static int
curve_sine(int angle)
{
	angle %= 3600;
	if (angle < 0) angle += 3600;
	unsigned quadrant = (unsigned)angle/900;
	if (quadrant == 1) angle = 1800-angle;
	else if (quadrant == 2) angle -= 1800;
	else if (quadrant == 3) angle = 3600-angle;
	unsigned index = (unsigned)angle/10, remainder = (unsigned)angle%10;
	int value = curve_sine_table[index];
	if (remainder != 0)
		value += ((int)curve_sine_table[index+1]-value)*(int)remainder/10;
	return quadrant > 1 ? -value : value;
}

static bool
curve_rejected(const struct cterm_igs *igs, int x, int y, int radius, int yr)
{
	/* Keep native signed-radius predicates, widening sums so coordinates
	 * cannot become accidental wrapped rejection or foreign addresses. */
	return igs->clipping && (x+radius < igs->clip_x1 || x-radius > igs->clip_x2
	    || y+yr < igs->clip_y1 || y-yr > igs->clip_y2);
}

static unsigned
curve_points(struct point *points, int x, int y, int radius, int yr,
    int begin, int end, int delta)
{
	int maximum = radius > yr ? radius : yr;
	unsigned steps = maximum/4 < CURVE_STEPS_MIN ? CURVE_STEPS_MIN
	    : maximum/4 > CURVE_STEPS_MAX ? CURVE_STEPS_MAX : (unsigned)(maximum/4);
	for (unsigned i = 0; i <= steps; ++i) {
		int angle = i == steps ? end : begin+clip_ratio(delta,(int)i,(int)steps);
		/* The native divisor32767 comparison never increments a quotient;
		 * use signed truncation, keeping safe coordinates wider than a word. */
		points[i] = (struct point){x+curve_sine(angle+900)*radius/32767,
		    y-curve_sine(angle)*yr/32767};
	}
	return steps+1;
}

static int
filled_curve(struct cterm_igs *igs, int x, int y, int radius, int yr, int end_angle)
{
	if (curve_rejected(igs,x,y,radius,yr)) return 0;
	const uint16_t *rows; unsigned row_mask;
	int status = fill_pattern(igs,&rows,&row_mask);
	if (status != 0) return status;
	struct point points[CURVE_STEPS_MAX+1];
	unsigned count = curve_points(points,x,y,radius,yr,0,end_angle,3600);
	int left = points[0].x, right = left, top = points[0].y, bottom = top;
	for (unsigned i = 1; i < count; ++i) {
		if (points[i].x < left) left = points[i].x;
		if (points[i].x > right) right = points[i].x;
		if (points[i].y < top) top = points[i].y;
		if (points[i].y > bottom) bottom = points[i].y;
	}
	int drawable = igs->terminal->height*(igs->resolution == 2 ? 16 : 8);
	if (left < 0) left = 0;
	if (top < 0) top = 0;
	if (right >= (int)igs->width) right = (int)igs->width-1;
	if (bottom >= drawable) bottom = drawable-1;
	struct raster raster = {.igs = igs, .mode = (unsigned)igs->attributes.writing_mode,
	    .foreground = cterm_igs_physical_pen(igs,igs->attributes.fill_pen),
	    .plane_mask = cterm_igs_plane_mask(igs->resolution)};
	if (left <= right && top <= bottom) {
		raster.area = (struct st_console_rect){(unsigned)left,(unsigned)top,
		    (unsigned)(right-left+1),(unsigned)(bottom-top+1)};
		status = st_console_get_pixels(&raster.pixels,igs->terminal->screen.console,&raster.area,false);
	}
	if (status != 0) return status;
	fill_polygon(&raster,points,count,rows,row_mask,igs->attributes.fill_perimeter);
	status = publish(&raster); st_pixel_buffer_destroy(raster.pixels);
	if (status == 0) igs->last_line = raster.last_line;
	return status;
}

int
cterm_igs_circle(struct cterm_igs *igs, int x, int y, int radius)
{
	int xs = igs->resolution == 0 ? 338 : igs->resolution == 1 ? 169 : 372;
	return filled_curve(igs,x,y,radius,clip_ratio(radius,xs,372),3600);
}

int
cterm_igs_ellipse(struct cterm_igs *igs, int x, int y, int x_radius, int y_radius)
{
	/* IGS opens its workstation in raster-coordinate transformation2.
	 * GDP5's full interior sweep ends with an explicit angle0 point. */
	return filled_curve(igs,x,y,x_radius,y_radius,0);
}

/* Corners arrive ordered; build native GDP8/9's private, closed 21-point path. */
static void
rounded_points(const struct cterm_igs *igs, int x1, int y1, int x2, int y2,
    struct point points[21])
{
	/* These are the native interpolated trig results for 675/450/225.
	 * Divisor32767's signed-word remainder comparison never rounds up;
	 * all products here are nonnegative and safely bounded by radius9. */
	int xr = (int)(igs->width-1)/64;
	if (xr > (x2-x1)/2) xr = (x2-x1)/2;
	int xs = igs->resolution == 0 ? 338 : igs->resolution == 1 ? 169 : 372;
	int yr = clip_ratio(xr,xs,372);
	if (yr > (y2-y1)/2) yr = (y2-y1)/2;
	struct point quarter[5] = {{0,yr},{0,0},{0,0},{0,0},{xr,0}};
	for (unsigned i = 0; i < 3; ++i) {
		int angle = 675-225*(int)i;
		quarter[i+1] = (struct point){curve_sine(angle+900)*xr/32767,curve_sine(angle)*yr/32767};
	}
	for (unsigned i = 0; i < 5; ++i) {
		struct point q = quarter[i], reverse = quarter[4-i];
		points[i] = (struct point){x2-xr+q.x,y1+yr-q.y};
		points[5+i] = (struct point){x2-xr+reverse.x,y2-yr+reverse.y};
		points[10+i] = (struct point){x1+xr-q.x,y2-yr+q.y};
		points[15+i] = (struct point){x1+xr-reverse.x,y1+yr-reverse.y};
	}
	points[20] = points[0];
}

int
cterm_igs_box(struct cterm_igs *igs, int x1, int y1, int x2, int y2, bool rounded)
{
	const uint16_t *rows;
	unsigned row_mask;
	int status = fill_pattern(igs, &rows, &row_mask);
	if (status != 0) return status;
	if (x1 > x2) { int swap = x1; x1 = x2; x2 = swap; }
	if (y1 > y2) { int swap = y1; y1 = y2; y2 = swap; }
	if (!rounded) {
		status = cterm_igs_rectangle(igs,x1,y1,x2,y2);
		if (status != 0 || !igs->attributes.fill_perimeter) return status;
	}
	struct point points[21]; unsigned count;
	if (rounded) {
		rounded_points(igs,x1,y1,x2,y2,points); count = 21;
	} else {
		points[0] = (struct point){x1,y1}; points[1] = (struct point){x2,y1};
		points[2] = (struct point){x2,y2}; points[3] = (struct point){x1,y2}; count = 4;
	}
	struct raster raster = {.igs = igs, .mode = (unsigned)igs->attributes.writing_mode,
	    .foreground = cterm_igs_physical_pen(igs,igs->attributes.fill_pen),
	    .plane_mask = cterm_igs_plane_mask(igs->resolution), .mask = 0xffff,
	    .last_line = igs->last_line};
	int drawable = igs->terminal->height*(igs->resolution == 2 ? 16 : 8);
	int left = x1 < 0 ? 0 : x1, top = y1 < 0 ? 0 : y1;
	int right = x2 >= (int)igs->width ? (int)igs->width-1 : x2;
	int bottom = y2 >= drawable ? drawable-1 : y2;
	if (left <= right && top <= bottom) {
		raster.area = (struct st_console_rect){(unsigned)left,(unsigned)top,
		    (unsigned)(right-left+1),(unsigned)(bottom-top+1)};
		status = st_console_get_pixels(&raster.pixels,igs->terminal->screen.console,&raster.area,false);
	}
	if (status != 0) return status;
	if (rounded) fill_polygon(&raster,points,count,rows,row_mask,igs->attributes.fill_perimeter);
	else for (unsigned i = 0; i < count; ++i) {
		raster.last_line = i+1 == count;
		segment(&raster,points[i],points[(i+1)%count],false);
	}
	status = publish(&raster); st_pixel_buffer_destroy(raster.pixels);
	if (status == 0) igs->last_line = raster.last_line;
	return status;
}

static unsigned
circle_table(int width, int xsize, int ysize, int circle[40])
{
	int x = 0, y = (width+1)/2, decision = 3-2*y;
	while (x < y) {
		circle[y] = x; circle[x] = y;
		if (decision < 0) decision += 4*x+6;
		else { decision += 4*(x-y)+10; --y; }
		++x;
	}
	if (x == y) circle[x] = x;
	unsigned rows = (unsigned)((width*xsize/ysize)/2+1);
	x = 0;
	for (unsigned i = 0; i < rows; ++i) {
		y = ((2*(int)i+1)*ysize/xsize)/2;
		int sum = 0;
		for (int j = x; j <= y; ++j) sum += circle[j];
		circle[i] = sum/(y-x+1);
		x = y+1;
	}
	return rows;
}

static struct point
perpendicular(struct point vector, const int circle[40], unsigned rows)
{
	int x = abs(vector.x), y = abs(vector.y);
	int u = circle[0], v = 0, best_u = u, best_v = 0;
	int64_t minimum = INT64_MAX;
	for (;;) {
		int64_t magnitude = (int64_t)u*y-(int64_t)v*x;
		if (magnitude < 0) magnitude = -magnitude;
		if (magnitude < minimum || (magnitude == minimum && abs(u-v) < abs(best_u-best_v))) {
			minimum = magnitude; best_u = u; best_v = v;
		} else break;
		if (v == (int)rows-1) { if (u == 1) break; --u; }
		else if (circle[v+1] >= u-1) { ++v; u = circle[v]; }
		else --u;
	}
	return (struct point){vector.x < 0 ? -best_u : best_u, vector.y < 0 ? -best_v : best_v};
}

static void
disc(struct raster *raster, struct point centre, const int circle[40], unsigned rows)
{
	for (unsigned i = 0; i < rows; ++i) {
		segment(raster, (struct point){centre.x-circle[i],centre.y-(int)i},
		    (struct point){centre.x+circle[i],centre.y-(int)i}, true);
		if (i != 0)
			segment(raster, (struct point){centre.x-circle[i],centre.y+(int)i},
			    (struct point){centre.x+circle[i],centre.y+(int)i}, true);
	}
}

static int
vector_length(int x, int y)
{
	uint64_t value = (uint64_t)((int64_t)x*x+(int64_t)y*y), root = 0;
	uint64_t bit = UINT64_C(1) << 62;
	while (bit > value) bit >>= 2;
	while (bit != 0) {
		if (value >= root+bit) { value -= root+bit; root = (root >> 1)+bit; }
		else root >>= 1;
		bit >>= 2;
	}
	return (int)root;
}

static void
arrow(struct raster *raster, struct point *points, size_t count, bool begin, int width, int xs, int ys)
{
	size_t tip = begin ? 0 : count-1, candidate = tip;
	struct point end = points[tip];
	int length = width == 1 ? 8 : 3*width-1, half = length/2;
	int dx = 0, dy = 0, distance = 0;
	/* Find the first vertex beyond the head, skipping short/repeated pairs.
	 * Only the private point copy is shortened, including skipped vertices. */
	for (size_t i = 1; i < count; ++i) {
		candidate = begin ? i : count-1-i;
		dx = end.x-points[candidate].x;
		dy = clip_ratio(end.y-points[candidate].y, ys, xs);
		distance = vector_length(dx, dy);
		if (distance >= length) break;
	}
	if (distance < length) return;
	/* Keep both rounds of the native rotation; combining the fractions
	 * changes small heads and aspect-corrected diagonals. */
	int hx = clip_ratio(length, clip_ratio(dx,1000,distance), 1000);
	int hy = clip_ratio(length, clip_ratio(dy,1000,distance), 1000);
	int bx = clip_ratio(half, clip_ratio(dy,-1000,distance), 1000);
	int by = clip_ratio(half, clip_ratio(dx,1000,distance), 1000);
	hy = clip_ratio(hy,xs,ys); by = clip_ratio(by,xs,ys);
	const struct point triangle[] = {{end.x+bx-hx,end.y+by-hy},
	    {end.x-bx-hx,end.y-by-hy},end};
	polygon(raster, triangle, 3);
	points[tip] = (struct point){end.x-hx,end.y-hy};
	if (begin) {
		for (size_t i = 1; i < candidate; ++i) points[i] = points[tip];
	} else {
		for (size_t i = count-2; i > candidate; --i) points[i] = points[tip];
	}
}

static void
arrows(struct raster *raster, struct point *points, size_t count, int width, int begin, int end, int xs, int ys)
{
	raster->mask = 0xffff;
	struct point original = points[0];
	if (begin & 1) arrow(raster, points, count, true, width, xs, ys);
	struct point shortened = points[0];
	if (end & 1) {
		points[0] = original;
		arrow(raster, points, count, false, width, xs, ys);
		points[0] = shortened;
	}
}

static int
render_polyline(struct cterm_igs *igs, const int *coordinates, size_t count, bool caps)
{
	if (coordinates == NULL || count < 2 || count > CURVE_STEPS_MAX+1)
		return IO_ERROR_INVALID_ARGUMENT;
	struct point points[CURVE_STEPS_MAX+1];
	for (size_t i = 0; i < count; ++i)
		points[i] = (struct point){coordinates[2*i],coordinates[2*i+1]};
	static const uint16_t styles[] = {0xffff,0xfff0,0xc0c0,0xff18,0xff00,0xf191};
	int type = igs->attributes.line_type, width = igs->attributes.line_width;
	if (type < 1 || type > 7 || width < 1 || width > 39 || (width & 1) == 0)
		return IO_ERROR_INVALID_ARGUMENT;
	struct raster raster = {.igs = igs, .mode = (unsigned)igs->attributes.writing_mode,
	    .foreground = cterm_igs_physical_pen(igs, igs->attributes.line_pen),
	    .plane_mask = cterm_igs_plane_mask(igs->resolution), .last_line = igs->last_line,
	    .mask = type == 7 ? igs->attributes.user_line : styles[type-1]};
	/* Native bytes never back the screen. Restrict a temporary indexed
	 * capture to the endpoint box plus the largest head/cap extent. */
	int margin = 5*width+8;
	int left = points[0].x, right = left, top = points[0].y, bottom = top;
	for (size_t i = 1; i < count; ++i) {
		if (points[i].x < left) left = points[i].x;
		if (points[i].x > right) right = points[i].x;
		if (points[i].y < top) top = points[i].y;
		if (points[i].y > bottom) bottom = points[i].y;
	}
	left -= margin; right += margin; top -= margin; bottom += margin;
	int drawable = igs->terminal->height*(igs->resolution == 2 ? 16 : 8);
	if (left < 0) left = 0;
	if (top < 0) top = 0;
	if (right >= (int)igs->width) right = (int)igs->width-1;
	if (bottom >= drawable) bottom = drawable-1;
	int status = 0;
	if (left <= right && top <= bottom) {
		raster.area = (struct st_console_rect){(unsigned)left,(unsigned)top,
		    (unsigned)(right-left+1),(unsigned)(bottom-top+1)};
		status = st_console_get_pixels(&raster.pixels, igs->terminal->screen.console, &raster.area, false);
	}
	if (status != 0) return status;
	int xs = igs->resolution == 0 ? 338 : igs->resolution == 1 ? 169 : 372;
	int ys = 372, begin = caps ? igs->attributes.line_begin : 0, end = caps ? igs->attributes.line_end : 0;
	if (width == 1) {
		for (size_t i = 1; i < count; ++i) {
			raster.last_line = i+1 == count;
			segment(&raster, points[i-1], points[i], false);
		}
		if ((begin | end) & 1) arrows(&raster, points, count, width, begin, end, xs, ys);
	} else {
		int circle[40] = {0};
		unsigned rows = circle_table(width, xs, ys, circle);
		if ((begin | end) & 1) arrows(&raster, points, count, width, begin, end, xs, ys);
		raster.mask = 0xffff;
		if (begin != 0) disc(&raster, points[0], circle, rows);
		struct point first = points[0];
		for (size_t i = 1; i < count; ++i) {
			struct point last = points[i];
			int dx = last.x-first.x, dy = last.y-first.y;
			if (dx == 0 && dy == 0) continue;
			struct point offset;
			if (dx == 0) offset = (struct point){circle[0],0};
			else if (dy == 0) offset = (struct point){0,(int)rows-1};
			else offset = perpendicular((struct point){clip_ratio(-dy,ys,xs),clip_ratio(dx,xs,ys)},circle,rows);
			const struct point box[] = {{first.x+offset.x,first.y+offset.y},
			    {first.x-offset.x,first.y-offset.y},
			    {last.x-offset.x,last.y-offset.y},
			    {last.x+offset.x,last.y+offset.y}};
			polygon(&raster,box,4);
			if (i+1 < count || end != 0) disc(&raster,last,circle,rows);
			first = last;
		}
	}
	status = publish(&raster);
	st_pixel_buffer_destroy(raster.pixels);
	if (status == 0) igs->last_line = raster.last_line;
	return status;
}

int
cterm_igs_polyline(struct cterm_igs *igs, const int *coordinates, size_t count)
{
	if (count > CTERM_IGS_MAX_POINTS) return IO_ERROR_INVALID_ARGUMENT;
	return render_polyline(igs,coordinates,count,true);
}

static int
outline_curve(struct cterm_igs *igs, int x, int y, int radius, int yr, int begin_degrees, int end_degrees)
{
	if (curve_rejected(igs,x,y,radius,yr)) return 0;
	/* Use wide degree-to-tenths arithmetic and retain the native single
	 * negative-delta adjustment. Trig alone reduces periodic arguments. */
	int begin = begin_degrees*10, end = end_degrees*10, delta = end-begin;
	if (delta < 0) delta += 3600;
	struct point points[CURVE_STEPS_MAX+1];
	unsigned count = curve_points(points,x,y,radius,yr,begin,end,delta);
	int coordinates[2*(CURVE_STEPS_MAX+1)];
	for (unsigned i = 0; i < count; ++i) {
		coordinates[2*i] = points[i].x; coordinates[2*i+1] = points[i].y;
	}
	return render_polyline(igs,coordinates,count,true);
}

int
cterm_igs_arc(struct cterm_igs *igs, int x, int y, int radius, int begin_degrees, int end_degrees)
{
	int xs = igs->resolution == 0 ? 338 : igs->resolution == 1 ? 169 : 372;
	return outline_curve(igs,x,y,radius,clip_ratio(radius,xs,372),begin_degrees,end_degrees);
}

int
cterm_igs_ellarc(struct cterm_igs *igs, int x, int y, int x_radius, int y_radius,
    int begin_degrees, int end_degrees)
{
	/* IGS's raster-coordinate workstation preserves both supplied radii. */
	return outline_curve(igs,x,y,x_radius,y_radius,begin_degrees,end_degrees);
}

int
cterm_igs_rounded_outline(struct cterm_igs *igs, int x1, int y1, int x2, int y2)
{
	if (x1 > x2) { int swap = x1; x1 = x2; x2 = swap; }
	if (y1 > y2) { int swap = y1; y1 = y2; y2 = swap; }
	struct point points[21]; int coordinates[42];
	rounded_points(igs,x1,y1,x2,y2,points);
	for (unsigned i = 0; i < 21; ++i) {
		coordinates[2*i] = points[i].x; coordinates[2*i+1] = points[i].y;
	}
	/* GDP8 disables endpoint caps for both thin and wide traversal, then
	 * restores the caller's settings. Keep those settings untouched here. */
	return render_polyline(igs,coordinates,21,false);
}

int
cterm_igs_line(struct cterm_igs *igs, int x1, int y1, int x2, int y2)
{
	const int coordinates[] = {x1,y1,x2,y2};
	int status = cterm_igs_polyline(igs, coordinates, 2);
	if (status == 0) {
		igs->draw_start_x = x1; igs->draw_start_y = y1;
		igs->draw_x = x2; igs->draw_y = y2;
	}
	return status;
}
