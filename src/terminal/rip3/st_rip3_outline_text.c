/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_outline_text.h"
#include "st_rip3_outline_font.h"
#include "st_rip3_outline_layout.h"
#include "st_rip3_draw_internal.h"
#include "st_rip3_ports.h"
#include "io_error.h"

#include <math.h>
#include <stdlib.h>

struct glyph_group {
	size_t first, count;
};

struct outlines {
	struct st_rip3_vector *vertices;
	size_t vertex_count, vertex_capacity;
	struct st_rip3_vector_run *runs;
	size_t run_count, run_capacity;
	struct glyph_group *glyphs;
	size_t glyph_count;
};

/* Ordinary-text ink policy. Raw positioned RFF callers leave this absent. */
struct effects {
	double bold;
	uint32_t flags;
};

static int
next_glyph(const struct st_rip3_rff *font, const uint8_t *text, size_t index,
    bool kerning, int32_t *advance, int32_t *position,
    struct st_rip3_rff_glyph *glyph)
{
	int status = st_rip3_rff_glyph(glyph, font, text[index]);
	int delta = 0;
	int64_t start, next;

	*position = *advance;
	if (status == IO_ERROR_NOT_FOUND) {
		*glyph = (struct st_rip3_rff_glyph){0};
		return 0;
	}
	if (status != 0)
		return status;
	if (kerning && index != 0 && text[index - 1] >= font->first_character
	    && (unsigned)text[index - 1] - font->first_character < font->characters) {
		status = st_rip3_rff_kerning(&delta, font, text[index - 1], text[index]);
		if (status != 0)
			return status;
	}
	start = (int64_t)*advance + delta;
	next = start + glyph->advance;
	if (start < INT32_MIN || start > INT32_MAX || next < INT32_MIN || next > INT32_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	*position = (int32_t)start;
	*advance = (int32_t)next;
	return 0;
}

static int
measure(const struct st_rip3_rff *font, const uint8_t *text, size_t length,
    bool kerning, int32_t *result)
{
	int32_t advance = 0, position;

	for (size_t i = 0; i < length; i++) {
		struct st_rip3_rff_glyph glyph;
		int status = next_glyph(font, text, i, kerning, &advance, &position, &glyph);

		if (status != 0)
			return status;
	}
	*result = advance;
	return 0;
}

int
st_rip3_rff_text_measure(const struct st_rip3_rff *font,
    const uint8_t *text, size_t length, bool kerning, int32_t *result)
{
	struct st_rip3_rff checked;
	int status;

	if (font == NULL || result == NULL || (text == NULL && length != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_rip3_rff_open(&checked, font->data, font->size);
	return status != 0 ? status : measure(&checked, text, length, kerning, result);
}

static bool
valid_vector(struct st_rip3_vector point)
{
	return isfinite(point.x) && isfinite(point.y)
	    && fabs(point.x) <= 4.0 * INT32_MAX && fabs(point.y) <= 4.0 * INT32_MAX;
}

static int
append_point(struct outlines *path, struct st_rip3_vector point)
{
	if (!valid_vector(point))
		return IO_ERROR_OUT_OF_RANGE;
	if (path->vertex_count == path->vertex_capacity) {
		size_t limit = SIZE_MAX / sizeof(*path->vertices);
		size_t capacity = path->vertex_capacity;
		struct st_rip3_vector *vertices;

		if (capacity == limit)
			return IO_ERROR_OUT_OF_RANGE;
		capacity = capacity == 0 ? 64 : capacity > limit / 2 ? limit : capacity * 2;
		vertices = realloc(path->vertices, capacity * sizeof(*vertices));
		if (vertices == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		path->vertices = vertices;
		path->vertex_capacity = capacity;
	}
	path->vertices[path->vertex_count++] = point;
	return 0;
}

static int
start_run(struct outlines *path, struct st_rip3_vector point)
{
	int status;

	if (path->run_count == path->run_capacity) {
		size_t limit = SIZE_MAX / sizeof(*path->runs);
		size_t capacity = path->run_capacity;
		struct st_rip3_vector_run *runs;

		if (capacity == limit)
			return IO_ERROR_OUT_OF_RANGE;
		capacity = capacity == 0 ? 16 : capacity > limit / 2 ? limit : capacity * 2;
		runs = realloc(path->runs, capacity * sizeof(*runs));
		if (runs == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		path->runs = runs;
		path->run_capacity = capacity;
	}
	path->runs[path->run_count++] = (struct st_rip3_vector_run){path->vertex_count,0};
	status = append_point(path, point);
	return status;
}

static int
close_run(struct outlines *path)
{
	struct st_rip3_vector_run *run = &path->runs[path->run_count - 1];

	run->count = path->vertex_count - run->first;
	if (run->count == 1) {
		/* A move without a drawing operation has no ink, including hollow. */
		path->run_count--;
		path->vertex_count--;
		return 0;
	}
	int status = append_point(path, path->vertices[run->first]);
	if (status == 0)
		run->count++;
	return status;
}

static struct st_rip3_vector
midpoint(struct st_rip3_vector a, struct st_rip3_vector b)
{
	return (struct st_rip3_vector){(a.x + b.x) / 2, (a.y + b.y) / 2};
}

static double
distance_to_segment(struct st_rip3_vector p, struct st_rip3_vector a,
    struct st_rip3_vector b)
{
	double dx = b.x - a.x, dy = b.y - a.y;
	double squared = dx * dx + dy * dy;
	double t = squared == 0 ? 0 : ((p.x - a.x) * dx + (p.y - a.y) * dy) / squared;

	t = fmax(0, fmin(1, t));
	return hypot(p.x - a.x - t * dx, p.y - a.y - t * dy);
}

static int
flatten(struct outlines *path, const struct st_rip3_vector p[4], unsigned depth)
{
	if (distance_to_segment(p[1], p[0], p[3]) <= 1.0 / 64
	    && distance_to_segment(p[2], p[0], p[3]) <= 1.0 / 64)
		return append_point(path, p[3]);
	if (depth == 32)
		return IO_ERROR_OUT_OF_RANGE;
	struct st_rip3_vector a = midpoint(p[0], p[1]);
	struct st_rip3_vector b = midpoint(p[1], p[2]);
	struct st_rip3_vector c = midpoint(p[2], p[3]);
	struct st_rip3_vector d = midpoint(a, b), e = midpoint(b, c), f = midpoint(d, e);
	const struct st_rip3_vector left[] = {p[0],a,d,f}, right[] = {f,e,c,p[3]};
	int status = flatten(path, left, depth + 1);

	return status == 0 ? flatten(path, right, depth + 1) : status;
}

static struct st_rip3_vector
transform(const struct st_rip3_text_position *origin,
    const struct st_rip3_rff_text_style *style, unsigned percent,
    int32_t advance, int x, int y)
{
	double u = ((double)advance + x) * percent / 100.0;

	return (struct st_rip3_vector){origin->x + style->xx * u + style->xy * y,
	    origin->y + style->yx * u + style->yy * y};
}

static int
build(struct outlines *path, const struct st_rip3_rff *font,
    const uint8_t *text, size_t length, const struct st_rip3_point *origin,
    const struct st_rip3_text_position *positions,
    const struct st_rip3_rff_text_style *style, unsigned percent)
{
	int32_t advance = 0, position;

	for (size_t i = 0; i < length; i++) {
		struct st_rip3_rff_glyph glyph;
		struct st_rip3_rff_segment segment;
		struct glyph_group *group = &path->glyphs[i];
		size_t cursor = 0;
		bool positioned = false;
		struct st_rip3_text_position placed;
		int status;

		if (positions != NULL) {
			placed = positions[i];
			position = 0;
			status = st_rip3_rff_glyph(&glyph,font,text[i]);
			if (status == IO_ERROR_NOT_FOUND)
				continue;
		}
		else {
			placed = (struct st_rip3_text_position){origin->x,origin->y};
			status = next_glyph(font, text, i, style->kerning, &advance, &position, &glyph);
		}

		if (status != 0)
			return status;
		group->first = path->run_count;
		while ((status = st_rip3_rff_path_next(&segment, &glyph, &cursor)) == 0) {
			struct st_rip3_vector points[4];

			for (unsigned j = 0; j < segment.points; j++) {
				points[j + 1] = transform(&placed, style, percent, position, segment.x[j], segment.y[j]);
				if (!valid_vector(points[j + 1]))
					return IO_ERROR_OUT_OF_RANGE;
			}
			if (segment.operation == ST_RIP3_RFF_MOVE) {
				if (positioned && (status = close_run(path)) != 0)
					return status;
				status = start_run(path, points[1]);
				positioned = true;
			}
			else if (!positioned)
				return IO_ERROR_INVALID_FORMAT;
			else if (segment.operation == ST_RIP3_RFF_LINE)
				status = append_point(path, points[1]);
			else {
				points[0] = path->vertices[path->vertex_count - 1];
				status = flatten(path, points, 0);
			}
			if (status != 0)
				return status;
		}
		if (status != IO_ERROR_NOT_FOUND)
			return status;
		if (positioned && (status = close_run(path)) != 0)
			return status;
		group->count = path->run_count - group->first;
	}
	return 0;
}

struct crossing {
	double x;
	int direction;
};

static int
compare_crossings(const void *left, const void *right)
{
	double a = ((const struct crossing *)left)->x;
	double b = ((const struct crossing *)right)->x;

	return (a > b) - (a < b);
}

static void
fill_span(struct st_rip3_stroke_mask *mask, int32_t y, double left, double right)
{
	double first = fmax(mask->bounds.x0, ceil(left - 0.5));
	double last = fmin(mask->bounds.x1, ceil(right - 0.5) - 1);

	if (first > last)
		return;
	for (int32_t x = (int32_t)first; x <= (int32_t)last; x++)
		mask->coverage[(size_t)(y - mask->bounds.y0) * mask->width
		    + (size_t)(x - mask->bounds.x0)] = 2;
}

static int
build_bands(struct outlines *path, const struct st_rip3_rff *font,
    const uint8_t *text, size_t length, const struct st_rip3_text_position *positions,
    const struct st_rip3_rff_text_style *style, unsigned percent, uint32_t flags)
{
	if ((flags&12)==0) return 0;
	if (length>SIZE_MAX/2/sizeof(*path->glyphs)) return IO_ERROR_OUT_OF_RANGE;
	path->glyphs=calloc(length*2,sizeof(*path->glyphs));
	if (path->glyphs==NULL) return IO_ERROR_OUT_OF_MEMORY;
	path->glyph_count=length*2;
	int thickness=(int)(font->units_per_em/16);
	if (thickness==0) thickness=1;
	for (size_t i=0; i<length; i++) {
		unsigned advance;int status=st_rip3_rff_advance(&advance,font,text[i]);
		if (status==IO_ERROR_NOT_FOUND) continue;
		if (status!=0) return status;
		if (advance==0) continue;
		for (unsigned band=0; band<2; band++) {
			if ((flags&(4u<<band))==0) continue;
			int y=band==0 ? (int)(font->units_per_em/10) : -(int)(font->units_per_em/3);
			const int x[]={0,(int)advance,(int)advance,0},ys[]={y,y,y+thickness,y+thickness};
			struct glyph_group *group=&path->glyphs[2*i+band];group->first=path->run_count;
			for (unsigned corner=0; corner<4; corner++) {
				struct st_rip3_vector point=transform(&positions[i],style,percent,0,x[corner],ys[corner]);
				status=corner==0 ? start_run(path,point) : append_point(path,point);
				if (status!=0) return status;
			}
			status=close_run(path);if (status!=0) return status;
			group->count=path->run_count-group->first;
		}
	}
	return 0;
}

static int
fill_collect(const struct outlines *path, const struct st_rip3_port_info *info,
    enum st_rip3_outline_fill_rule rule, double bold, struct st_rip3_stroke_mask *result)
{
	struct st_rip3_stroke_mask mask = {0};
	struct crossing *crossings;
	double xmin, xmax, ymin, ymax, left, right, top, bottom;

	if (path->vertex_count == 0) {
		*result = mask;
		return 0;
	}
	xmin = xmax = path->vertices[0].x;
	ymin = ymax = path->vertices[0].y;
	for (size_t i = 1; i < path->vertex_count; i++) {
		xmin = fmin(xmin, path->vertices[i].x); xmax = fmax(xmax, path->vertices[i].x);
		ymin = fmin(ymin, path->vertices[i].y); ymax = fmax(ymax, path->vertices[i].y);
	}
	left = fmax(0, ceil(xmin - bold - 0.5));
	right = fmin(info->clip.x1, ceil(xmax + bold - 0.5) - 1);
	top = fmax(0, ceil(ymin - 0.5));
	bottom = fmin(info->clip.y1, ceil(ymax - 0.5) - 1);
	if (left > right || top > bottom) {
		*result = mask;
		return 0;
	}
	mask.bounds = (struct st_rip3_rect){(int32_t)left,(int32_t)top,(int32_t)right,(int32_t)bottom};
	mask.width = (size_t)(mask.bounds.x1 - mask.bounds.x0) + 1;
	mask.height = (size_t)(mask.bounds.y1 - mask.bounds.y0) + 1;
	if (mask.width > SIZE_MAX / mask.height || path->vertex_count > SIZE_MAX / sizeof(*crossings))
		return IO_ERROR_OUT_OF_RANGE;
	mask.coverage = calloc(mask.width * mask.height, 1);
	crossings = malloc(path->vertex_count * sizeof(*crossings));
	if (mask.coverage == NULL || crossings == NULL) {
		free(mask.coverage);
		free(crossings);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	for (size_t g = 0; g < path->glyph_count; g++) {
		const struct glyph_group *group = &path->glyphs[g];

		for (int32_t y = mask.bounds.y0; y <= mask.bounds.y1; y++) {
			size_t count = 0;
			double scan_y = y + 0.5;
			int64_t winding = 0;

			for (size_t r = group->first; r < group->first + group->count; r++) {
				const struct st_rip3_vector_run *run = &path->runs[r];

				for (size_t i = 1; i < run->count; i++) {
					struct st_rip3_vector a = path->vertices[run->first + i - 1];
					struct st_rip3_vector b = path->vertices[run->first + i];
					int direction = a.y < b.y ? 1 : -1;

					if (a.y > b.y) {
						struct st_rip3_vector swap = a; a = b; b = swap;
					}
					if (a.y <= scan_y && scan_y < b.y)
						crossings[count++] = (struct crossing){a.x
						    + (scan_y - a.y) * (b.x - a.x) / (b.y - a.y), direction};
				}
			}
			qsort(crossings, count, sizeof(*crossings), compare_crossings);
			for (size_t i = 0; i < count;) {
				double x = crossings[i].x;
				bool inside = rule == ST_RIP3_OUTLINE_NONZERO ? winding != 0 : winding % 2 != 0;

				if (inside)
					fill_span(&mask, y, crossings[i - 1].x-bold, x+bold);
				do {
					winding += crossings[i++].direction;
				} while (i < count && crossings[i].x == x);
			}
		}
	}
	free(crossings);
	*result = mask;
	return 0;
}

static int
unite(struct st_rip3_stroke_mask *ink, struct st_rip3_stroke_mask *bands)
{
	if (bands->coverage==NULL) return 0;
	if (ink->coverage==NULL) {*ink=*bands;*bands=(struct st_rip3_stroke_mask){0};return 0;}
	struct st_rip3_stroke_mask value={0};
	value.bounds=(struct st_rip3_rect){
	    ink->bounds.x0<bands->bounds.x0 ? ink->bounds.x0 : bands->bounds.x0,
	    ink->bounds.y0<bands->bounds.y0 ? ink->bounds.y0 : bands->bounds.y0,
	    ink->bounds.x1>bands->bounds.x1 ? ink->bounds.x1 : bands->bounds.x1,
	    ink->bounds.y1>bands->bounds.y1 ? ink->bounds.y1 : bands->bounds.y1};
	value.width=(size_t)(value.bounds.x1-value.bounds.x0)+1;
	value.height=(size_t)(value.bounds.y1-value.bounds.y0)+1;
	if (value.width>SIZE_MAX/value.height) return IO_ERROR_OUT_OF_RANGE;
	value.coverage=calloc(value.width*value.height,1);
	if (value.coverage==NULL) return IO_ERROR_OUT_OF_MEMORY;
	const struct st_rip3_stroke_mask *sources[]={ink,bands};
	for (unsigned i=0; i<2; i++) {
		const struct st_rip3_stroke_mask *source=sources[i];
		for (size_t y=0; y<source->height; y++) for (size_t x=0; x<source->width; x++) {
			if (source->coverage[y*source->width+x]!=0)
				value.coverage[(y+(size_t)(source->bounds.y0-value.bounds.y0))*value.width
				    +x+(size_t)(source->bounds.x0-value.bounds.x0)]=2;
		}
	}
	st_rip3_stroke_free(ink);*ink=value;return 0;
}

static int
collect(struct st_rip3_ports *ports, const struct outlines *path, const struct outlines *bands,
    const struct st_rip3_port_info *info, const struct st_rip3_rff_text_style *style,
    bool hollow, double bold, struct st_rip3_stroke_mask *result)
{
	struct st_rip3_stroke_mask ink={0},decoration={0};int status=0;
	if (path->vertex_count!=0)
		status=hollow ? st_rip3_stroke_vectors(ports,path->vertices,path->vertex_count,
		    path->runs,path->run_count,style->hollow_thickness,&ink)
		    : fill_collect(path,info,style->fill_rule,bold,&ink);
	if (status==0) status=fill_collect(bands,info,ST_RIP3_OUTLINE_NONZERO,0,&decoration);
	if (status==0) status=unite(&ink,&decoration);
	st_rip3_stroke_free(&decoration);
	if (status!=0) st_rip3_stroke_free(&ink);
	else *result=ink;
	return status;
}

static int
draw(struct st_rip3_ports *ports,
    const struct st_rip3_rff *font, const uint8_t *text, size_t length,
    const struct st_rip3_point *origin, const struct st_rip3_text_position *positions,
    const struct st_rip3_rff_text_style *style, const struct effects *effects,
    uint32_t foreground, uint32_t background, enum st_rip3_write_mode mode)
{
	struct st_rip3_rff checked;
	struct st_rip3_rff_style face;
	struct st_rip3_port_info info;
	struct st_rip3_stroke_mask ink = {0}, shade = {0};
	struct outlines path = {0},bands = {0};
	int32_t advance;
	unsigned current;
	int status;

	if (ports == NULL || font == NULL || style == NULL
	    || (text == NULL && length != 0) || mode < ST_RIP3_COPY || mode > ST_RIP3_NOT
	    || (style->fill_rule != ST_RIP3_OUTLINE_NONZERO && style->fill_rule != ST_RIP3_OUTLINE_EVEN_ODD))
		return IO_ERROR_INVALID_ARGUMENT;
	if (length > SIZE_MAX / sizeof(*path.glyphs))
		return IO_ERROR_OUT_OF_RANGE;
	if (!isfinite(style->xx) || !isfinite(style->xy) || !isfinite(style->yx)
	    || !isfinite(style->yy) || fabs(style->xx) > INT32_MAX || fabs(style->xy) > INT32_MAX
	    || fabs(style->yx) > INT32_MAX || fabs(style->yy) > INT32_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	status = st_rip3_rff_open(&checked, font->data, font->size);
	if (status == 0)
		status = st_rip3_rff_style_at(&face, &checked, style->face);
	if (status != 0)
		return status;
	if (face.horizontal_percent == 0)
		return IO_ERROR_INVALID_FORMAT;
	if (face.hollow && (style->hollow_thickness == 0 || style->hollow_thickness > INT32_MAX))
		return IO_ERROR_OUT_OF_RANGE;
	if (positions != NULL) {
		for (size_t i = 0; i < length; i++) {
			if (!valid_vector((struct st_rip3_vector){positions[i].x,positions[i].y}))
				return IO_ERROR_OUT_OF_RANGE;
		}
	}
	else
		status = measure(&checked, text, length, style->kerning, &advance);
	if (status != 0 || length == 0)
		return status;
	status = st_rip3_ports_current(ports, &current);
	if (status == 0)
		status = st_rip3_ports_info(ports, current, &info);
	if (status != 0 || (!info.viewport_active || info.clip.x1 < 0 || info.clip.y1 < 0))
		return status;
	path.glyphs = calloc(length, sizeof(*path.glyphs));
	if (path.glyphs == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	path.glyph_count = length;
	status = build(&path, &checked, text, length, origin, positions, style, face.horizontal_percent);
	if (status==0 && effects!=NULL)
		status=build_bands(&bands,&checked,text,length,positions,style,face.horizontal_percent,effects->flags);
	if (status != 0)
		goto done;
	double bold=effects==NULL || face.hollow ? 0 : effects->bold;
	status=collect(ports,&path,&bands,&info,style,face.hollow,bold,&ink);
	if (status != 0)
		goto done;
	if (style->shadow.x != 0 || style->shadow.y != 0) {
		struct outlines *paths[]={&path,&bands};
		for (unsigned p=0; p<2; p++) for (size_t i=0; i<paths[p]->vertex_count; i++) {
			struct st_rip3_vector *point=&paths[p]->vertices[i];
			point->x+=style->shadow.x;point->y+=style->shadow.y;
			if (!valid_vector(*point)) {
				status=IO_ERROR_OUT_OF_RANGE;goto done;
			}
		}
		status=collect(ports,&path,&bands,&info,style,face.hollow,bold,&shade);
		if (status == 0)
			status = st_rip3_stroke_shadow_apply(ports, &ink, &shade, foreground, background, mode);
	}
	else
		status = st_rip3_stroke_apply(ports, &ink, foreground, background, mode);
done:
	st_rip3_stroke_free(&ink);
	st_rip3_stroke_free(&shade);
	free(path.vertices);
	free(path.runs);
	free(path.glyphs);
	free(bands.vertices);free(bands.runs);free(bands.glyphs);
	return status;
}

int
st_rip3_draw_rff_text(struct st_rip3_ports *ports,
    const struct st_rip3_rff *font, const uint8_t *text, size_t length,
    const struct st_rip3_point *origin, const struct st_rip3_rff_text_style *style,
    uint32_t foreground, uint32_t background, enum st_rip3_write_mode mode)
{
	if (origin == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	return draw(ports,font,text,length,origin,NULL,style,NULL,foreground,background,mode);
}

int
st_rip3_draw_rff_positioned(struct st_rip3_ports *ports,
    const struct st_rip3_rff *font, const uint8_t *text, size_t length,
    const struct st_rip3_text_position *positions, const struct st_rip3_rff_text_style *style,
    uint32_t foreground, uint32_t background, enum st_rip3_write_mode mode)
{
	if (positions == NULL && length != 0)
		return IO_ERROR_INVALID_ARGUMENT;
	return draw(ports,font,text,length,NULL,positions,style,NULL,foreground,background,mode);
}

int
st_rip3_draw_outline_positioned(struct st_rip3_ports *ports,
    const struct st_rip3_outline_font *font, const struct st_rip3_outline_layout *layout,
    const uint8_t *text, size_t length, const struct st_rip3_text_position *positions,
    uint32_t foreground, uint32_t background, enum st_rip3_write_mode mode)
{
	static const unsigned orientation[]={0,3,2,1};
	static const int cosine[]={1,0,-1,0}, sine[]={0,1,0,-1};
	if (font==NULL || layout==NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (font->points==0 || font->points>4095 || font->flags>0xffffff
	    || font->dpi_x==0 || font->dpi_y==0
	    || !isfinite(font->scale_x) || !isfinite(font->scale_y) || font->scale_x<=0 || font->scale_y<=0
	    || layout->character_turns>3 || layout->font_orientation!=orientation[layout->character_turns]
	    || font->orientation!=layout->font_orientation)
		return IO_ERROR_OUT_OF_RANGE;
	if (positions==NULL && length!=0) return IO_ERROR_INVALID_ARGUMENT;
	unsigned turn=layout->character_turns;
	struct st_rip3_rff_text_style style={
	    .face=font->face,
	    .xx=cosine[turn]*font->scale_x,.xy=sine[turn]*font->scale_x,
	    .yx=-sine[turn]*font->scale_y,.yy=cosine[turn]*font->scale_y,
	    .shadow=layout->shadow,.fill_rule=ST_RIP3_OUTLINE_NONZERO,
	    .hollow_thickness=font->hollow_thickness,
	};
	if ((font->flags&2)!=0) {
		double shear=font->style.horizontal_percent/400.0;
		style.xy-=style.xx*shear;style.yy-=style.yx*shear;
	}
	const struct effects effects={.flags=font->flags,
	    .bold=(font->flags&1)!=0 ? fmax(1,ceil((double)font->points*font->dpi_x/1728)) : 0};
	return draw(ports,&font->file,text,length,NULL,positions,&style,&effects,
	    foreground,background,mode);
}
