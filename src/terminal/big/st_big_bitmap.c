/* Original EGAVGA bitmap source/rotation/expansion, BIG28/29/46/62. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_bitmap.h"
#include "st_bgi_data.h"
#include "st_bgi_bitmap.h"
#include "st_big_number.h"
#include "io_error.h"
int st_big_bitmap_row(void *context, uint8_t code, unsigned row, uint8_t *bits)
{
	(void)context;
	if (row>=8 || bits==NULL) return IO_ERROR_INVALID_ARGUMENT;
	/* BIG's source-selection alias is separate from font-version data. */
	if (code==0x30) code=0x4f;
	*bits=st_bgi_bitmap_font[8u*code+row];
	return 0;
}
static int source(const struct st_big_bitmap_font *font, uint8_t code, unsigned row, uint8_t *bits)
{
	int status=font==NULL?st_big_bitmap_row(NULL,code,row,bits):font->row(font->context,code,row,bits);
	return status==IO_ERROR_IN_PROGRESS?IO_ERROR_PROTOCOL:status;
}
struct bitmap_row { const struct st_big_draw_sink *sink; uint16_t x,y; uint8_t pen; };
static int bitmap_pixel(void *context, unsigned x, unsigned y)
{
	const struct bitmap_row *row=context;
	return row->sink->pixel(row->sink->context,(uint16_t)(row->x+x),(uint16_t)(row->y+y),row->pen,false);
}
static int glyph(struct st_big_state *s, uint8_t code, const struct st_big_bitmap_font *font, const struct st_big_draw_sink *sink)
{
	uint16_t x=s->bgi.device_x,y=s->bgi.device_y;
	if (st_big_number_signed(x)<0) return 0;
	uint16_t right=(uint8_t)s->bgi.clip!=0?s->bgi.viewport[2]:(uint16_t)(s->bgi.width-1);
	if (x>=(uint16_t)(right-s->bgi.device_text_extent[0])) return 0;
	if (st_big_number_signed(y)<0) return 0;
	uint16_t bottom=(uint8_t)s->bgi.clip!=0?s->bgi.viewport[3]:(uint16_t)(s->bgi.height-1);
	if (y>=(uint16_t)(bottom-s->bgi.device_text_extent[1])) return 0;
	uint8_t rotated[8]={0}; bool rotate=s->bgi.device_text_direction!=0;
	if (rotate) {
		for (unsigned row=0;row<8;++row) {
			uint8_t bits; int status=source(font,code,row,&bits); if (status!=0) return status;
			for (unsigned bit=0;bit<8;++bit) if ((bits&(1u<<bit))!=0) rotated[bit]|=(uint8_t)(0x80u>>row);
		}
	}
	uint8_t pen=s->bgi.device_color;
	if (pen>15) return IO_ERROR_OUT_OF_RANGE;
	for (unsigned row=0;row<8;++row) {
		unsigned repeated=s->bgi.device_text_magnification[1]; if (repeated==0) repeated=256;
		for (unsigned repeat=0;repeat<repeated;++repeat) {
			uint8_t bits=rotated[row];
			if (!rotate) { int status=source(font,code,row,&bits); if (status!=0) return status; }
			unsigned columns=s->bgi.device_text_magnification[0]; if (columns==0) columns=256;
			struct bitmap_row output={sink,x,y,pen};
			int status=st_bgi_bitmap_expand_row(bits,columns,1,bitmap_pixel,&output);
			if (status!=0) return status;
			y=(uint16_t)(y+1u);
		}
	}
	return 0;
}
int st_big_text_bitmap(struct st_big_state *s, const uint8_t *bytes, uint8_t count,
    const struct st_big_bitmap_font *font, const struct st_big_draw_sink *sink)
{
	if (s==NULL || (bytes==NULL && count!=0) || sink==NULL || sink->pixel==NULL || (font!=NULL && font->row==NULL)) return IO_ERROR_INVALID_ARGUMENT;
	if (s->bgi.state==0) { s->bgi.error=-1; return IO_ERROR_PROTOCOL; }
	bool reverse=s->bgi.device_text_direction==1;
	for (unsigned i=0;i<count;++i) {
		uint8_t code=bytes[reverse?count-1u-i:i]; if (code==0) break;
		int status=glyph(s,code,font,sink); if (status!=0) return status;
		if (s->bgi.device_text_direction==1) s->bgi.device_y=(uint16_t)(s->bgi.device_y+s->bgi.device_text_dimensions[0]);
		else s->bgi.device_x=(uint16_t)(s->bgi.device_x+s->bgi.device_text_dimensions[0]);
	}
	return 0;
}
