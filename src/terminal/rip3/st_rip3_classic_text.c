/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_classic_text.h"
#include "st_rip3_font_data.h"
#include "st_rip3_outline_font.h"
#include "st_rip3_outline_layout.h"
#include "st_rip3_ports.h"
#include "io_error.h"

#include <math.h>
#include <string.h>

int
st_rip3_classic_text_style_set(struct st_rip3_classic_text_style *result,
    unsigned font, unsigned direction, unsigned magnification, unsigned flags)
{
	unsigned horizontal_flags = flags & (ST_RIP3_CLASSIC_LEFT | ST_RIP3_CLASSIC_RIGHT | ST_RIP3_CLASSIC_CENTER);
	unsigned vertical_flags = flags & (ST_RIP3_CLASSIC_TOP | ST_RIP3_CLASSIC_BOTTOM
	    | ST_RIP3_CLASSIC_BASELINE | ST_RIP3_CLASSIC_MIDDLE);

	if (result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (font > 10 || direction > 1 || magnification == 0 || magnification > 10
	    || flags > 4095 || (horizontal_flags & (horizontal_flags - 1)) != 0
	    || (vertical_flags & (vertical_flags - 1)) != 0)
		return IO_ERROR_OUT_OF_RANGE;
	if (font == 0) {
		if (horizontal_flags == 0)
			flags |= direction == 0 ? ST_RIP3_CLASSIC_LEFT : ST_RIP3_CLASSIC_RIGHT;
		if (vertical_flags == 0)
			flags |= ST_RIP3_CLASSIC_TOP;
	}
	*result = (struct st_rip3_classic_text_style){font,direction,magnification,flags};
	return 0;
}

int
st_rip3_classic_default_outline(struct st_rip3_outline_font *font,
    struct st_rip3_outline_layout *layout, const struct st_rip3_classic_text_style *style,
    const struct st_rip3_frame *frame)
{
	static const unsigned points[] = {11,22,33,44,56,68,78,90,101,112};
	static const unsigned spacing[][10] = {
		{127,107,105,107,103,103,104,103,103,103},
		{115,109,109,107,104,102,104,103,103,103},
		{109,109,109,107,104,102,104,103,103,103},
		{108,109,109,107,104,102,104,103,103,103},
	};
	if (font == NULL || layout == NULL || style == NULL || frame == NULL || style->font != 0)
		return IO_ERROR_INVALID_ARGUMENT;
	struct st_rip3_classic_text_style checked;
	int status = st_rip3_classic_text_style_set(&checked, 0, style->direction,
	    style->magnification, style->flags);
	if (status != 0)
		return status;
	uint32_t width = frame->device_width, height = frame->device_height;
	unsigned row = 3;
	if (width <= 640 && height <= 480)
		row = 0;
	else if (width > 640 && width <= 800 && height <= 600)
		row = 1;
	else if (width > 800 && width <= 1024 && height <= 768)
		row = 2;
	unsigned size = checked.magnification - 1;
	bool vertical = checked.direction != 0;
	unsigned orientation = vertical ? 3 : 0;
	status = st_rip3_outline_font_builtin(font, "DEFAULT EX", frame, points[size], 0, orientation);
	if (status != 0)
		return status;
	int reference = (checked.flags & ST_RIP3_CLASSIC_BASELINE) != 0 ? 0
	    : (checked.flags & ST_RIP3_CLASSIC_BOTTOM) != 0 ? 250
	    : (checked.flags & ST_RIP3_CLASSIC_MIDDLE) != 0 ? -250 : -750;
	unsigned dpi = vertical ? font->dpi_x : font->dpi_y;
	int64_t displacement = -(int64_t)reference * points[size] * dpi / (72 * 1000);
	if (displacement < INT32_MIN || displacement > INT32_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	int32_t cross = (int32_t)displacement;
	*layout = (struct st_rip3_outline_layout){
		.direction = vertical ? (struct st_rip3_point){0,-1} : (struct st_rip3_point){1,0},
		.baseline = vertical ? (struct st_rip3_point){cross,0} : (struct st_rip3_point){0,cross},
		.shadow = (checked.flags & ST_RIP3_CLASSIC_SHADOW) != 0
		    ? (struct st_rip3_point){1,1} : (struct st_rip3_point){0,0},
		.character_turns = checked.direction,
		.font_orientation = orientation,
		.spacing = spacing[row][size],
		.justification = (checked.flags & ST_RIP3_CLASSIC_RIGHT) != 0 ? 1
		    : (checked.flags & ST_RIP3_CLASSIC_CENTER) != 0 ? 2 : 0,
	};
	return 0;
}

struct st_rip3_classic_scale
st_rip3_classic_stroke_scale(unsigned magnification)
{
	static const struct st_rip3_classic_scale scales[] = {
		{3,5}, {2,3}, {3,4}, {1,1}, {4,3}, {5,3}, {2,1}, {5,2}, {3,1}, {4,1},
	};
	if (magnification < 1 || magnification > sizeof(scales) / sizeof(scales[0]))
		return (struct st_rip3_classic_scale){0};
	return scales[magnification - 1];
}

int
st_rip3_classic_font_builtin(struct st_rip3_classic_font *result,
    enum st_rip3_canvas canvas, unsigned number)
{
	struct st_rip3_frame frame;
	int status=st_rip3_frame_init(&frame,canvas);
	if (status!=0) return status;
	status=st_rip3_classic_font_builtin_device(result,frame.device_width,frame.device_height,number);
	if (status==0) {result->canvas=canvas;result->device_width=result->device_height=0;}
	return status;
}

int
st_rip3_classic_font_builtin_device(struct st_rip3_classic_font *result,
    uint32_t width, uint32_t height, unsigned number)
{
	struct st_rip3_classic_font font = {.number = number,.device_width=width,.device_height=height};
	struct st_rip3_frame frame;
	int status;

	if (result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (number > 10)
		return IO_ERROR_OUT_OF_RANGE;
	status = st_rip3_frame_init_device(&frame, width, height);
	if (status != 0)
		return status;
	if (number != 0)
		status = st_rip3_chr_builtin(&font.data.stroke, number);
	else {
		const struct st_rip3_font_asset *asset = NULL;
		struct st_rip3_maf maf;

		for (size_t i = 0; i < st_rip3_font_asset_count(); i++) {
			const struct st_rip3_font_asset *candidate = st_rip3_font_asset_at(i);
			if (strcmp(candidate->filename, "RIPscrip.maf") == 0) {
				asset = candidate;
				break;
			}
		}
		if (asset == NULL)
			return IO_ERROR_NOT_FOUND;
		status = st_rip3_maf_open(&maf, asset->data, asset->size);
		if (status == 0)
			status = st_rip3_maf_find(&font.data.bitmap, &maf, frame.device_width,
			    frame.device_height, ST_RIP3_MAF_80X43);
		if (status==IO_ERROR_NOT_FOUND)
			status=st_rip3_maf_find(&font.data.bitmap,&maf,640,480,ST_RIP3_MAF_80X43);
	}
	if (status == 0)
		*result = font;
	return status;
}

static int
round_origin(double x, double y, struct st_rip3_text_position *result)
{
	x = floor(x + 0.5); y = floor(y + 0.5);
	if (!isfinite(x) || !isfinite(y) || fabs(x) > 1024.0*INT32_MAX
	    || fabs(y) > 1024.0*INT32_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	*result = (struct st_rip3_text_position){x,y};
	return 0;
}

int
st_rip3_classic_text_plan(const struct st_rip3_classic_font *font,
    const struct st_rip3_classic_text_style *style, const uint8_t *text, size_t length,
    const struct st_rip3_text_position *anchor, struct st_rip3_classic_text_plan *result)
{
	struct st_rip3_classic_text_plan plan = {0};
	struct st_rip3_classic_text_style checked;
	struct st_rip3_frame frame;
	double u, v, baseline = 0;
	int status;

	if (font == NULL || style == NULL || result == NULL || anchor == NULL
	    || (text == NULL && length != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	if (!isfinite(anchor->x) || !isfinite(anchor->y)
	    || fabs(anchor->x) > 1024.0*INT32_MAX || fabs(anchor->y) > 1024.0*INT32_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	status = st_rip3_classic_text_style_set(&checked, style->font, style->direction,
	    style->magnification, style->flags);
	if (status != 0)
		return status;
	if (font->number != style->font)
		return IO_ERROR_INVALID_ARGUMENT;
	status = font->device_width!=0 || font->device_height!=0
	    ? st_rip3_frame_init_device(&frame,font->device_width,font->device_height)
	    : st_rip3_frame_init(&frame, font->canvas);
	if (status != 0)
		return status;
	if (font->number == 0) {
		struct st_rip3_bitmap_text_metrics metrics;
		const struct st_rip3_bitmap_text_style bitmap = {.magnification = style->magnification};

		status = st_rip3_bitmap_text_measure(&font->data.bitmap, &bitmap, length, &metrics);
		if (status != 0)
			return status;
		plan.advance = metrics.width;
		plan.integer_advance = metrics.width;
		plan.cell_height = (uint64_t)font->data.bitmap.height * style->magnification;
		plan.ascent = (7.0 * style->magnification - 1) * font->data.bitmap.height / 8;
		plan.descent = (double)font->data.bitmap.height * style->magnification - 1 - plan.ascent;
	}
	else {
		struct st_rip3_chr_text_metrics metrics;
		struct st_rip3_classic_scale scale = st_rip3_classic_stroke_scale(style->magnification);
		double factor = (double)scale.numerator / scale.denominator;
		double sx = factor * frame.device_width / 640;
		double sy = factor * frame.device_height / 350;

		status = st_rip3_chr_text_measure(&font->data.stroke, text, length, &metrics);
		if (status != 0)
			return status;
		plan.advance = metrics.advance * sx;
		plan.integer_advance = (uint64_t)metrics.advance*scale.numerator*frame.device_width
		    / ((uint64_t)scale.denominator*640);
		plan.cell_height = (uint64_t)metrics.height*scale.numerator*frame.device_height
		    / ((uint64_t)scale.denominator*350) + 1;
		plan.ascent = (font->data.stroke.capital - font->data.stroke.baseline) * sy;
		plan.descent = (font->data.stroke.baseline - font->data.stroke.descender) * sy;
		baseline = font->data.stroke.baseline * sy;
		plan.matrix = style->direction == 0 ? (struct st_rip3_chr_text_matrix){sx,0,0,-sy}
		    : (struct st_rip3_chr_text_matrix){0,-sy,-sx,0};
	}
	u = (checked.flags & ST_RIP3_CLASSIC_RIGHT) != 0 ? -plan.advance
	    : (checked.flags & ST_RIP3_CLASSIC_CENTER) != 0 ? -plan.advance / 2 : 0;
	v = (checked.flags & ST_RIP3_CLASSIC_BOTTOM) != 0 ? -plan.descent
	    : (checked.flags & ST_RIP3_CLASSIC_BASELINE) != 0 ? 0
	    : (checked.flags & ST_RIP3_CLASSIC_MIDDLE) != 0 ? (plan.ascent - plan.descent) / 2 : plan.ascent;
	/* Bitmap kernels take a cell origin, CHR kernels take the untranslated
	 * source-coordinate origin. Compensate a nonzero CHR header baseline. */
	v += font->number == 0 ? -plan.ascent : baseline;
	status = style->direction == 0 ? round_origin(anchor->x + u, anchor->y + v, &plan.origin)
	    : round_origin(anchor->x + v, anchor->y - u, &plan.origin);
	if (status != 0)
		return status;
	*result = plan;
	return 0;
}

int
st_rip3_draw_classic_text(struct st_rip3_ports *ports,
    const struct st_rip3_classic_font *font,
    const struct st_rip3_classic_text_style *style, const uint8_t *text, size_t length,
    const struct st_rip3_text_position *anchor, uint32_t foreground, uint32_t background,
    enum st_rip3_write_mode mode)
{
	struct st_rip3_classic_text_plan plan;
	struct st_rip3_frame frame;
	struct st_rip3_port_info screen;
	int status;

	if (ports == NULL || font == NULL || mode < ST_RIP3_COPY || mode > ST_RIP3_NOT)
		return IO_ERROR_INVALID_ARGUMENT;
	status = font->device_width!=0 || font->device_height!=0
	    ? st_rip3_frame_init_device(&frame,font->device_width,font->device_height)
	    : st_rip3_frame_init(&frame, font->canvas);
	if (status == 0)
		status = st_rip3_ports_info(ports, 0, &screen);
	if (status != 0)
		return status;
	if ((uint32_t)screen.bounds.x1 + 1 != frame.device_width
	    || (uint32_t)screen.bounds.y1 + 1 != frame.device_height)
		return IO_ERROR_CONFLICT;
	status = st_rip3_classic_text_plan(font, style, text, length, anchor, &plan);
	if (status != 0)
		return status;
	if (font->number == 0) {
		const struct st_rip3_bitmap_text_style bitmap = {
			.magnification = style->magnification, .vertical = style->direction == 1,
			.shadow = (style->flags & ST_RIP3_CLASSIC_SHADOW) != 0,
		};
		return st_rip3_draw_bitmap_text(ports, &font->data.bitmap, &bitmap, text, length,
		    &plan.origin, foreground, background, mode);
	}
	return st_rip3_draw_chr_text(ports, &font->data.stroke, text, length,
	    &plan.origin, &plan.matrix, (style->flags & ST_RIP3_CLASSIC_SHADOW) != 0, foreground, background, mode);
}
