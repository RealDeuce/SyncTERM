/* BIG packed image edges over an indexed cache, references25/40/56. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_image.h"
#include "io_error.h"
#include <string.h>

static size_t reachable(const struct st_big_image *image)
{
	size_t limit=65536u-ST_BIG_IMAGE_BASE;
	return image->capacity<limit?image->capacity:limit;
}
static uint16_t word(const uint8_t *bytes)
{
	return (uint16_t)((unsigned)bytes[0]|((unsigned)bytes[1]<<8));
}
static void put_word(uint8_t *bytes, uint16_t value)
{
	bytes[0]=(uint8_t)value; bytes[1]=(uint8_t)(value>>8);
}
int st_big_image_header(const struct st_big_image *image, uint16_t *width, uint16_t *height)
{
	if (image==NULL || width==NULL || height==NULL || image->bytes==NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (reachable(image)<4) return IO_ERROR_OUT_OF_RANGE;
	*width=word(image->bytes); *height=word(image->bytes+2);
	return 0;
}
/* Payload byte to a padded indexed row. Partial trailing planes retain
 * their imported values; unavailable planes are never readable by source. */
static size_t position(const struct st_big_image *image, size_t payload, uint8_t *plane)
{
	size_t row_bytes=4*image->plane_bytes;
	*plane=(uint8_t)(3-(payload%row_bytes)/image->plane_bytes);
	return (payload/row_bytes*image->plane_bytes+payload%image->plane_bytes)*8;
}
static void decode_byte(struct st_big_image *image, size_t payload, uint8_t value)
{
	uint8_t plane; size_t first=position(image,payload,&plane);
	uint8_t bit=(uint8_t)(1u<<plane);
	for (unsigned i=0;i<8;++i) {
		uint8_t *pen=&image->pens[first+i];
		*pen=(uint8_t)((*pen&~bit)|((value&(0x80u>>i))!=0?bit:0));
	}
}
int st_big_image_import(struct st_big_image *image)
{
	if (image==NULL || image->bytes==NULL || image->pens==NULL) return IO_ERROR_INVALID_ARGUMENT;
	uint16_t width,height; int status=st_big_image_header(image,&width,&height);
	if (status!=0) return status;
	(void)height;
	size_t plane_bytes=(size_t)width/8+1, payload=reachable(image)-4;
	size_t rows=(payload+4*plane_bytes-1)/(4*plane_bytes), count=rows*plane_bytes*8;
	/* A failed reimport must not expose pens tiled for an older header. */
	image->plane_bytes=plane_bytes; image->pen_count=0;
	if (count>image->pen_capacity) return IO_ERROR_OUT_OF_RANGE;
	image->pen_count=count;
	memset(image->pens,0,count);
	for (size_t i=0;i<payload;++i) decode_byte(image,i,image->bytes[4+i]);
	return 0;
}
int st_big_image_bind(struct st_big_image *image, uint8_t *bytes, size_t capacity,
    uint8_t *pens, size_t pen_capacity)
{
	if (image==NULL || bytes==NULL || pens==NULL || capacity>ST_BIG_IMAGE_CAPACITY) return IO_ERROR_INVALID_ARGUMENT;
	struct st_big_image binding={.bytes=bytes,.pens=pens,.capacity=capacity,.pen_capacity=pen_capacity};
	int status=st_big_image_import(&binding);
	if (status==0) *image=binding;
	return status;
}
static uint16_t folded(uint16_t a, uint16_t b)
{
	uint16_t delta=(uint16_t)(a-b);
	return (delta&0x8000u)!=0?(uint16_t)(0u-delta):delta;
}
uint16_t st_big_image_size(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
	uint16_t row=(uint16_t)(folded(x1,x2)+8u);
	row=(uint16_t)((row>>3)*4u);
	uint16_t height=(uint16_t)(folded(y1,y2)+1u);
	uint32_t result=(uint32_t)row*height+6u;
	return result>65535u?0:(uint16_t)result;
}
int st_big_image_source(const struct st_big_image *image, uint16_t cursor,
    const uint8_t **pens, uint8_t *plane)
{
	if (image==NULL || pens==NULL || plane==NULL || image->bytes==NULL || image->pens==NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (cursor<ST_BIG_IMAGE_BASE+4 || (size_t)(cursor-ST_BIG_IMAGE_BASE)>=reachable(image)) return IO_ERROR_OUT_OF_RANGE;
	if (image->plane_bytes==0 || image->plane_bytes>8192) return IO_ERROR_PROTOCOL;
	uint8_t selected; size_t first=position(image,cursor-ST_BIG_IMAGE_BASE-4,&selected);
	if (first>image->pen_count || image->pen_count-first<8
	    || first>image->pen_capacity || image->pen_capacity-first<8) return IO_ERROR_OUT_OF_RANGE;
	*pens=image->pens+first; *plane=selected;
	return 0;
}
static int store(struct st_big_image *image, uint16_t cursor, uint8_t value)
{
	const uint8_t *pens; uint8_t plane;
	int status=st_big_image_source(image,cursor,&pens,&plane);
	if (status!=0) return status;
	size_t index=cursor-ST_BIG_IMAGE_BASE;
	image->bytes[index]=value; decode_byte(image,index-4,value);
	return 0;
}
static int read_plane(const struct st_big_memory_sink *sink, uint16_t address, unsigned plane, uint8_t *value)
{
	uint8_t pens[8]; int status=sink->read(sink->context,address,pens);
	if (status!=0) return status==IO_ERROR_IN_PROGRESS?IO_ERROR_PROTOCOL:status;
	uint8_t bits=0;
	for (unsigned i=0;i<8;++i) {
		if (pens[i]>15) return IO_ERROR_OUT_OF_RANGE;
		if ((pens[i]&(1u<<plane))!=0) bits|=(uint8_t)(0x80u>>i);
	}
	*value=bits; return 0;
}
int st_big_image_capture(struct st_big_image *image, struct st_big_state *state,
    const struct st_big_memory_sink *sink, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
	if (image==NULL || state==NULL || sink==NULL || sink->read==NULL || image->bytes==NULL || image->pens==NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (reachable(image)<4) return IO_ERROR_OUT_OF_RANGE;
	if (x1>x2) { uint16_t swap=x1; x1=x2; x2=swap; }
	if (y1>y2) { uint16_t swap=y1; y1=y2; y2=swap; }
	uint16_t width=(uint16_t)(x2-x1), height=(uint16_t)(y2-y1);
	put_word(image->bytes,width); put_word(image->bytes+2,height);
	int status=st_big_image_import(image); if (status!=0) return status;
	if (state->bgi.state==0) { state->bgi.error=-1; return IO_ERROR_PROTOCOL; }
	/* DDO translates only the starting point; the device reconstructs
	 * the end from these header deltas. All device offsets are WORDs. */
	uint16_t x=(uint16_t)(x1+state->bgi.viewport[0]), y=(uint16_t)(y1+state->bgi.viewport[1]);
	uint16_t pitch=(uint16_t)(state->bgi.width/8);
	uint16_t row=(uint16_t)((uint32_t)y*pitch+x/8u), cursor=ST_BIG_IMAGE_BASE+4;
	unsigned shift=x&7u, last=width&7u;
	uint8_t mask=(uint8_t)(0xffu<<(7u-last));
	for (uint32_t line=0;line<=(uint32_t)height;++line) {
		for (unsigned plane=4;plane--!=0;) {
			uint16_t source=row; uint8_t current;
			status=read_plane(sink,source,plane,&current); if (status!=0) return status;
			for (size_t byte=0;byte<image->plane_bytes;++byte) {
				uint8_t next; source=(uint16_t)(source+1u);
				status=read_plane(sink,source,plane,&next); if (status!=0) return status;
				uint8_t packed=(uint8_t)(((unsigned)current<<shift)|((unsigned)next>>(8u-shift)));
				if (byte+1==image->plane_bytes) packed&=mask;
				status=store(image,cursor,packed); if (status!=0) return status;
				cursor=(uint16_t)(cursor+1u); current=next;
			}
		}
		row=(uint16_t)(row+pitch);
	}
	return 0;
}
