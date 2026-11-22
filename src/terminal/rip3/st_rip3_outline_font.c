/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_outline_font.h"
#include "st_rip3_outline_font_internal.h"
#include "st_rip3_font_data.h"
#include "io_error.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static int
request(struct st_rip3_outline_font *value, const struct st_rip3_frame *frame,
    unsigned points, uint32_t flags, unsigned orientation)
{
	if (frame == NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (points == 0 || points > 4095 || flags > 0xffffff || orientation > 3)
		return IO_ERROR_OUT_OF_RANGE;
	struct st_rip3_frame checked = *frame;
	int status = st_rip3_frame_world(&checked,frame->world_width,frame->world_height);
	if (status != 0) return status;
	uint64_t dx=UINT64_C(72)*checked.device_width/checked.world_width;
	uint64_t dy=UINT64_C(72)*checked.device_height/checked.world_height;
	if (dx>72*1024 || dy>72*1024) return IO_ERROR_OUT_OF_RANGE;
	*value = (struct st_rip3_outline_font){.points=points,.flags=flags,.orientation=orientation,
	    .dpi_x=(unsigned)dx,.dpi_y=(unsigned)dy};
	return 0;
}

/* Exact rational edge rounding, independent of floating-point half ties.
 * scaled is a signed s16 coordinate multiplied by at most 255; legal point/
 * DPI products fit int64_t, including custom units_per_em=1 realizations. */
static int64_t
edge(int64_t scaled, unsigned points, unsigned dpi, unsigned units)
{
	int64_t numerator=scaled*(int64_t)points*(int64_t)dpi, denominator=(int64_t)units*72*100;
	int64_t quotient=numerator/denominator, remainder=numerator%denominator;
	if (remainder*2>=denominator) quotient++;
	else if (remainder*2 < -denominator) quotient--;
	return quotient;
}

static void
realize_bounds(struct st_rip3_outline_font *font)
{
	for (unsigned corner=0; corner<4; corner++) {
		int64_t u=(int64_t)((corner&1)!=0 ? font->file.xmax : font->file.xmin)*(int64_t)font->style.horizontal_percent;
		int64_t v=(int64_t)((corner&2)!=0 ? font->file.ymax : font->file.ymin)*100;
		int64_t x=u,y=v;
		switch (font->orientation) {
			case 1:x=-v;y=u;break;
			case 2:x=-u;y=-v;break;
			case 3:x=v;y=-u;break;
		}
		x=edge(x,font->points,font->dpi_x,font->file.units_per_em);
		y=edge(y,font->points,font->dpi_y,font->file.units_per_em);
		if (corner==0) font->bounds=(struct st_rip3_outline_bounds){x,y,x,y};
		else {
			if (x<font->bounds.xmin) font->bounds.xmin=x;
			if (x>font->bounds.xmax) font->bounds.xmax=x;
			if (y<font->bounds.ymin) font->bounds.ymin=y;
			if (y>font->bounds.ymax) font->bounds.ymax=y;
		}
	}
}

static int
bind_face(struct st_rip3_outline_font *value, const struct st_rip3_rff *file, size_t face)
{
	struct st_rip3_rff_style style;
	int status = st_rip3_rff_style_at(&style,file,face);
	if (status != 0) return status;
	if (style.horizontal_percent == 0) return IO_ERROR_OUT_OF_RANGE;
	if (file->xmin>file->xmax || file->ymin>file->ymax) return IO_ERROR_INVALID_FORMAT;
	value->file = *file;
	value->style = style;
	value->face = face;
	value->scale_x = (double)value->points*value->dpi_x/(72.0*file->units_per_em);
	value->scale_y = (double)value->points*value->dpi_y/(72.0*file->units_per_em);
	realize_bounds(value);
	if (style.hollow) {
		unsigned dpi = value->orientation%2 == 0 ? value->dpi_x : value->dpi_y;
		bool bold = (value->flags&1) != 0;
		unsigned coefficient = bold ? file->bold_hollow_strength : file->hollow_strength;
		/* The original coefficient/DPI product is a WORD before division.
		 * Apply its narrowing explicitly, independently of native int width. */
		uint64_t strength = ((uint64_t)coefficient*dpi)&UINT64_C(65535);
		unsigned index = (unsigned)((strength*value->points/7200)&UINT64_C(65535));
		unsigned maximum = bold ? 7 : 4;
		if (index > maximum) index = maximum;
		if (bold && index == 0) index = 1;
		value->hollow_thickness = index+1;
	}
	return 0;
}

int
st_rip3_outline_font_bind(struct st_rip3_outline_font *result,
    const struct st_rip3_rff *file, size_t face, const struct st_rip3_frame *frame,
    unsigned points, uint32_t flags, unsigned orientation)
{
	struct st_rip3_outline_font value;
	struct st_rip3_rff checked;
	if (result == NULL || file == NULL) return IO_ERROR_INVALID_ARGUMENT;
	int status = request(&value,frame,points,flags,orientation);
	if (status == 0) status = st_rip3_rff_open(&checked,file->data,file->size);
	if (status == 0) status = bind_face(&value,&checked,face);
	if (status == 0) *result = value;
	return status;
}

int
st_rip3_outline_font_builtin(struct st_rip3_outline_font *result,
    const char *name, const struct st_rip3_frame *frame,
    unsigned points, uint32_t flags, unsigned orientation)
{
	struct st_rip3_outline_font value;
	if (result == NULL || name == NULL) return IO_ERROR_INVALID_ARGUMENT;
	int status = request(&value,frame,points,flags,orientation);
	if (status != 0) return status;
	for (size_t i = 0; i < st_rip3_font_asset_count(); i++) {
		const struct st_rip3_font_asset *asset = st_rip3_font_asset_at(i);
		size_t size = strlen(asset->filename);
		if (size < 4 || strcmp(asset->filename+size-4,".RFF") != 0) continue;
		struct st_rip3_rff file;
		struct st_rip3_rff_style found;
		status = st_rip3_rff_open(&file,asset->data,asset->size);
		if (status == 0) status = st_rip3_rff_find_style(&found,&file,name);
		if (status == IO_ERROR_NOT_FOUND) continue;
		if (status != 0) return status;
		/* The decoder owns record layout and case matching. Recover its face
		 * index through validated views, without duplicating format offsets. */
		for (size_t face = 0; face < file.styles; face++) {
			struct st_rip3_rff_style candidate;
			status = st_rip3_rff_style_at(&candidate,&file,face);
			if (status != 0) return status;
			if (candidate.name != found.name) continue;
			status = bind_face(&value,&file,face);
			if (status == 0) *result = value;
			return status;
		}
		return IO_ERROR_INVALID_FORMAT;
	}
	return IO_ERROR_NOT_FOUND;
}

int
st_rip3_outline_font_bbox_width(int32_t *result, const struct st_rip3_outline_font *font)
{
	if (result==NULL || font==NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (font->bounds.xmax<font->bounds.xmin) return IO_ERROR_OUT_OF_RANGE;
	/* Unsigned subtraction also handles an externally forged ordered pair
	 * spanning INT64_MIN..INT64_MAX without signed overflow. */
	uint64_t span=(uint64_t)font->bounds.xmax-(uint64_t)font->bounds.xmin;
	if (span>INT32_MAX) return IO_ERROR_OUT_OF_RANGE;
	*result=(int32_t)span;
	return 0;
}

static int
metric_parameters(struct st_rip3_rff *file, struct st_rip3_rff_style *style,
    const struct st_rip3_outline_font *font)
{
	if (font==NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (font->points==0 || font->points>4095
	    || font->dpi_x>72*1024 || font->dpi_y>72*1024
	    || font->orientation>3 || font->flags>0xffffff)
		return IO_ERROR_OUT_OF_RANGE;
	int status=st_rip3_rff_open(file,font->file.data,font->file.size);
	if (status==0) status=st_rip3_rff_style_at(style,file,font->face);
	if (status==0 && (style->horizontal_percent==0 || file->xmin>file->xmax || file->ymin>file->ymax))
		status=IO_ERROR_OUT_OF_RANGE;
	return status;
}

static uint64_t
logical_advance(unsigned advance, const struct st_rip3_outline_font *font,
    const struct st_rip3_rff *file, const struct st_rip3_rff_style *style)
{
	uint64_t numerator=(uint64_t)advance*font->points*20*style->horizontal_percent;
	uint64_t denominator=(uint64_t)file->units_per_em*100;
	return (numerator+denominator/2)/denominator;
}

int
st_rip3_outline_font_metric_map(uint64_t result[256], uint64_t *height, const struct st_rip3_outline_font *font)
{
	if (result==NULL || height==NULL) return IO_ERROR_INVALID_ARGUMENT;
	struct st_rip3_rff file;
	struct st_rip3_rff_style style;
	int status=metric_parameters(&file,&style,font);
	if (status!=0) return status;
	uint64_t values[256]={0};
	for (unsigned code=0; code<256; code++) {
		unsigned advance;
		status=st_rip3_rff_advance(&advance,&file,(uint8_t)code);
		if (status==IO_ERROR_NOT_FOUND) continue;
		if (status!=0) return status;
		values[code]=logical_advance(advance,font,&file,&style);
	}
	/* Geometric font-axis height: round in 1/1440 inch before DPI.
	 * Validated signed 16-bit bbox, Q and DPI keep each product in u64. */
	uint64_t numerator=(uint64_t)(file.ymax-file.ymin)*font->points*20;
	uint64_t logical_height=(numerator+file.units_per_em/2)/file.units_per_em;
	*height=logical_height*font->dpi_y/1440;
	memcpy(result,values,sizeof(values));
	return 0;
}

int
st_rip3_outline_font_advances(int32_t *result,
    const struct st_rip3_outline_font *font, unsigned string_rotation,
    const uint8_t *text, size_t length)
{
	if (font==NULL || (length!=0 && (result==NULL || text==NULL))) return IO_ERROR_INVALID_ARGUMENT;
	if (length>SIZE_MAX/sizeof(*result) || string_rotation>2700 || string_rotation%900!=0)
		return IO_ERROR_OUT_OF_RANGE;
	/* Raster advances retain their positive-DPI realization gate. The
	 * logical metric snapshot also accepts DPI truncated to zero by a
	 * positive WORLD, where the protocol formulas produce zero metrics. */
	if (font->dpi_x==0 || font->dpi_y==0) return IO_ERROR_OUT_OF_RANGE;
	struct st_rip3_rff file;
	struct st_rip3_rff_style style;
	int status=metric_parameters(&file,&style,font);
	if (status!=0) return status;
	unsigned dpi=string_rotation%1800==0 ? font->dpi_x : font->dpi_y;
	int32_t advances[256]={0};
	bool measured[256]={false};
	for (size_t i=0; i<length; i++) {
		uint8_t code=text[i];
		if (measured[code]) continue;
		unsigned advance;
		status=st_rip3_rff_advance(&advance,&file,code);
		if (status!=0 && status!=IO_ERROR_NOT_FOUND) return status;
		if (status==0) {
			/* Max u16 advance, wire point size and byte percentage fit uint64_t.
			 * Round logical units before device truncation, never a string sum. */
			uint64_t logical=logical_advance(advance,font,&file,&style);
			uint64_t device=logical*dpi/1440;
			if (device>INT32_MAX) return IO_ERROR_OUT_OF_RANGE;
			advances[code]=(int32_t)device;
		}
		measured[code]=true;
	}
	/* Publish only after every distinct code succeeded. This also keeps a
	 * late oversized advance from leaving a partially written layout input. */
	for (size_t i=0; i<length; i++) result[i]=advances[text[i]];
	return 0;
}
