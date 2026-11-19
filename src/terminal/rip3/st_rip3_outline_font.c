/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_outline_font.h"
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
	*value = (struct st_rip3_outline_font){.points=points,.flags=flags,.orientation=orientation,
	    .dpi_x=72*checked.device_width/checked.world_width,
	    .dpi_y=72*checked.device_height/checked.world_height};
	return 0;
}

static int
bind_face(struct st_rip3_outline_font *value, const struct st_rip3_rff *file, size_t face)
{
	struct st_rip3_rff_style style;
	int status = st_rip3_rff_style_at(&style,file,face);
	if (status != 0) return status;
	if (style.horizontal_percent == 0) return IO_ERROR_OUT_OF_RANGE;
	value->file = *file;
	value->style = style;
	value->face = face;
	value->scale_x = (double)value->points*value->dpi_x/(72.0*file->units_per_em);
	value->scale_y = (double)value->points*value->dpi_y/(72.0*file->units_per_em);
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
