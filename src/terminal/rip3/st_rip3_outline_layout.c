/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_outline_layout.h"
#include "st_rip3_styles.h"
#include "io_error.h"

#include <math.h>

int
st_rip3_outline_layout_prepare(struct st_rip3_outline_layout *result,
    const struct st_rip3_outline_style *style, const struct st_rip3_frame *frame)
{
	static const struct st_rip3_point direction[]={{1,0},{0,-1},{-1,0},{0,1}};
	/* The original branches are not a general normal-to-string displacement.
	 * Entries multiply the signed, already converted device baseline. */
	static const struct st_rip3_point baseline[][4]={
		{{0,1},{1,0},{0,0},{0,0}},
		{{1,0},{1,0},{0,0},{0,0}},
		{{0,0},{1,0},{0,-1},{0,0}},
		{{0,1},{0,0},{0,0},{0,0}},
	};
	static const unsigned orientation[]={0,3,2,1};
	if (result==NULL || style==NULL || frame==NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (style->flags>0xffffff || style->size==0 || style->size>4095
	    || style->shadow>4095 || style->spacing==0 || style->spacing>4095
	    || style->baseline>INT32_MAX || style->string_rotation>2700 || style->string_rotation%900!=0
	    || style->character_rotation>2700 || style->character_rotation%900!=0)
		return IO_ERROR_OUT_OF_RANGE;
	struct st_rip3_frame checked=*frame;
	int status=st_rip3_frame_world(&checked,frame->world_width,frame->world_height);
	if (status!=0) return status;
	unsigned string=style->string_rotation/900, character=style->character_rotation/900;
	int32_t displacement=(int32_t)style->baseline;
	if ((style->flags&0x100)!=0) displacement=-displacement;
	struct st_rip3_outline_layout value={
	    .direction=direction[string],
	    .baseline={baseline[string][character].x*displacement,baseline[string][character].y*displacement},
	    .character_turns=character,.font_orientation=orientation[character],
	    .spacing=style->spacing,.bounding_box_spacing=(style->flags&0x400)!=0,
	    .justification=(style->flags>>4)&3,
	};
	if (style->shadow!=0) {
		int32_t distance=(int32_t)((uint64_t)style->shadow*frame->device_width/frame->world_width);
		if (distance==0) distance=1;
		unsigned which=(style->flags>>6)&3;
		value.shadow=(struct st_rip3_point){(which&1)!=0 ? distance : -distance,
		    (which&2)!=0 ? distance : -distance};
	}
	*result=value;
	return 0;
}

int
st_rip3_outline_metric_device(int32_t *result, int32_t advance, unsigned dpi)
{
	if (result==NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (dpi==0 || dpi>72*1024) return IO_ERROR_OUT_OF_RANGE;
	int64_t value=(int64_t)advance*dpi/1440;
	if (value<INT32_MIN || value>INT32_MAX) return IO_ERROR_OUT_OF_RANGE;
	*result=(int32_t)value;
	return 0;
}

static bool
valid_position(struct st_rip3_text_position position)
{
	return isfinite(position.x) && isfinite(position.y)
	    && fabs(position.x)<=4.0*INT32_MAX && fabs(position.y)<=4.0*INT32_MAX;
}

static int64_t
adjusted(const struct st_rip3_outline_layout *layout, int32_t advance, int32_t bbox_width)
{
	int32_t reference=layout->bounding_box_spacing ? bbox_width : advance;
	/* Signed C division truncates negative extra gaps toward zero. */
	return advance+((int64_t)layout->spacing-100)*reference/100;
}

int
st_rip3_outline_layout_positions(const struct st_rip3_outline_layout *layout,
    const struct st_rip3_text_position *anchor, const int32_t *advances,
    size_t length, int32_t bbox_width, struct st_rip3_text_position *positions,
    int64_t *total)
{
	if (layout==NULL || anchor==NULL || total==NULL
	    || (length!=0 && (advances==NULL || positions==NULL)))
		return IO_ERROR_INVALID_ARGUMENT;
	if (length>SIZE_MAX/sizeof(*positions) || bbox_width<0 || layout->spacing==0 || layout->spacing>4095 || layout->justification>3
	    || !((layout->direction.x==0 && (layout->direction.y==1 || layout->direction.y==-1))
	    || (layout->direction.y==0 && (layout->direction.x==1 || layout->direction.x==-1))))
		return IO_ERROR_OUT_OF_RANGE;
	struct st_rip3_outline_layout value=*layout;
	struct st_rip3_text_position origin=*anchor;
	if (!valid_position(origin)) return IO_ERROR_OUT_OF_RANGE;
	int64_t advance=0;
	for (size_t i=0; i<length; i++) {
		int64_t delta=adjusted(&value,advances[i],bbox_width);
		if ((delta>0 && advance>INT64_MAX-delta) || (delta<0 && advance<INT64_MIN-delta))
			return IO_ERROR_OUT_OF_RANGE;
		advance+=delta;
	}
	int64_t alignment=value.justification==1 ? advance : value.justification==2 ? advance/2 : 0;
	origin.x+=value.baseline.x-value.direction.x*(double)alignment;
	origin.y+=value.baseline.y-value.direction.y*(double)alignment;
	int64_t offset=0;
	for (size_t i=0; i<length; i++) {
		struct st_rip3_text_position position={origin.x+value.direction.x*(double)offset,
		    origin.y+value.direction.y*(double)offset};
		if (!valid_position(position)) return IO_ERROR_OUT_OF_RANGE;
		offset+=adjusted(&value,advances[i],bbox_width);
	}
	/* The preflight proved every position and sum. No allocation or fallible
	 * operation remains, so a later bad glyph cannot partially publish output. */
	offset=0;
	for (size_t i=0; i<length; i++) {
		positions[i]=(struct st_rip3_text_position){origin.x+value.direction.x*(double)offset,
		    origin.y+value.direction.y*(double)offset};
		offset+=adjusted(&value,advances[i],bbox_width);
	}
	*total=advance;
	return 0;
}
