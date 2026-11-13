/* Indexed address/backing adaptation; no controller or foreign CPU model. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_video.h"
#include "io_error.h"
#include <stdbool.h>

static bool valid(const struct st_big_video *v, const struct st_big_video_view *view)
{
	return v!=NULL && v->count<=ST_BIG_VIDEO_PENS
	    && (v->count==0 || (v->primary!=NULL && v->alternate!=NULL))
	    && (view==NULL || (view->count<=ST_BIG_VIDEO_PENS
	    && (view->count==0 || (view->primary!=NULL && view->alternate!=NULL))));
}
int st_big_video_bind(struct st_big_video *v, uint32_t *primary, uint32_t *alternate, size_t count)
{
	if (v==NULL || (count!=0 && (primary==NULL || alternate==NULL))) return IO_ERROR_INVALID_ARGUMENT;
	if (count>ST_BIG_VIDEO_PENS) return IO_ERROR_OUT_OF_RANGE;
	*v=(struct st_big_video){primary,alternate,count}; return 0;
}
uint16_t st_big_video_active_segment(uint8_t page, uint16_t paragraphs)
{
	return (uint16_t)(0xa000u+(uint32_t)page*paragraphs);
}
uint16_t st_big_video_display_start(uint8_t page, uint16_t bios_page_bytes)
{
	/* AH05 uses CBW before its unsigned MUL, then retains the low WORD. */
	uint16_t word=(uint16_t)(page<128?page:(0xff00u|page));
	return (uint16_t)((uint32_t)word*bios_page_bytes);
}
static int locate(const struct st_big_video *v, const struct st_big_video_view *view,
    uint16_t segment, uint16_t offset, uint8_t bit, size_t *index, size_t *visible)
{
	if (!valid(v,view)) return IO_ERROR_INVALID_ARGUMENT;
	if (bit>7) return IO_ERROR_OUT_OF_RANGE;
	uint32_t address=((uint32_t)segment<<4)+offset;
	if (address<0xa0000 || address>=0xb0000) return IO_ERROR_OUT_OF_RANGE;
	*index=(size_t)(address-0xa0000)*8+bit; *visible=SIZE_MAX;
	if (view!=NULL) {
		size_t relative=(*index+ST_BIG_VIDEO_PENS-(size_t)view->start*8)%ST_BIG_VIDEO_PENS;
		if (relative<view->count) { *visible=relative; return 0; }
	}
	return *index<v->count?0:IO_ERROR_OUT_OF_RANGE;
}
int st_big_video_read(const struct st_big_video *v, const struct st_big_video_view *view,
    uint16_t segment, uint16_t offset, uint8_t bit, struct st_big_video_pixel *result)
{
	if (result==NULL) return IO_ERROR_INVALID_ARGUMENT;
	size_t index,visible;
	int status=locate(v,view,segment,offset,bit,&index,&visible); if (status!=0) return status;
	const uint32_t *primary=visible==SIZE_MAX?v->primary:view->primary;
	const uint32_t *alternate=visible==SIZE_MAX?v->alternate:view->alternate;
	size_t at=visible==SIZE_MAX?index:visible;
	*result=(struct st_big_video_pixel){primary[at],alternate[at],visible}; return 0;
}
int st_big_video_write(struct st_big_video *v, const struct st_big_video_view *view,
    uint16_t segment, uint16_t offset, uint8_t bit, uint32_t primary, uint32_t alternate, size_t *displayed)
{
	size_t index,visible;
	int status=locate(v,view,segment,offset,bit,&index,&visible); if (status!=0) return status;
	uint32_t *a=visible==SIZE_MAX?v->primary:view->primary, *b=visible==SIZE_MAX?v->alternate:view->alternate;
	size_t at=visible==SIZE_MAX?index:visible;
	a[at]=primary; b[at]=alternate;
	if (displayed!=NULL) *displayed=visible;
	return 0;
}
static int transfer(const struct st_big_video *v, const struct st_big_video_view *view, size_t *accepted, bool save)
{
	if (accepted==NULL) return IO_ERROR_INVALID_ARGUMENT;
	*accepted=0;
	if (view==NULL || !valid(v,view)) return IO_ERROR_INVALID_ARGUMENT;
	size_t index=(size_t)view->start*8;
	for (size_t i=0;i<view->count;++i) {
		if (index>=v->count) return IO_ERROR_OUT_OF_RANGE;
		if (save) { v->primary[index]=view->primary[i]; v->alternate[index]=view->alternate[i]; }
		else { view->primary[i]=v->primary[index]; view->alternate[i]=v->alternate[index]; }
		++*accepted;
		if (++index==ST_BIG_VIDEO_PENS) index=0;
	}
	return 0;
}
int st_big_video_save(struct st_big_video *v, const struct st_big_video_view *view, size_t *accepted)
{
	return transfer(v,view,accepted,true);
}
int st_big_video_load(const struct st_big_video *v, const struct st_big_video_view *view, size_t *accepted)
{
	return transfer(v,view,accepted,false);
}
