/* CHR body data access; BIG28/62 and the common encoded format. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_bgi_chr.h"
#include "io_error.h"
static uint16_t word(const uint8_t *p) { return (uint16_t)(p[0]|(unsigned)p[1]<<8); }
struct st_bgi_chr_record st_bgi_chr_decode(uint8_t x, uint8_t y)
{
	return (struct st_bgi_chr_record){(enum st_bgi_chr_opcode)(2u*(x>>7)+(y>>7)),(int)(x&63)-(int)(x&64),(int)(y&63)-(int)(y&64)};
}
int st_bgi_chr_bind(struct st_bgi_chr *out, const uint8_t *body, size_t size)
{
	if (out==NULL || body==NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (size<16) return IO_ERROR_OUT_OF_RANGE;
	if (body[0]!=0x2b) return IO_ERROR_PROTOCOL;
	*out=(struct st_bgi_chr){body,size,word(body+1),word(body+5),body[4]};
	return 0;
}
int st_bgi_chr_width(const struct st_bgi_chr *font, uint8_t code, uint8_t *out)
{
	if (font==NULL || font->body==NULL || out==NULL) return IO_ERROR_INVALID_ARGUMENT;
	uint8_t index=(uint8_t)(code-font->first), width=0;
	if (index<(uint8_t)font->count) {
		uint16_t address=(uint16_t)(16u+2u*font->count+index);
		if ((size_t)address>=font->size) return IO_ERROR_OUT_OF_RANGE;
		width=font->body[address];
	}
	*out=width; return 0;
}
int st_bgi_chr_program(const struct st_bgi_chr *font, uint8_t code, struct st_bgi_chr_glyph *out)
{
	if (font==NULL || font->body==NULL || out==NULL) return IO_ERROR_INVALID_ARGUMENT;
	uint8_t index=(uint8_t)(code-font->first);
	struct st_bgi_chr_glyph glyph={.body=font->body,.size=font->size,.finished=true};
	if (index<(uint8_t)font->count) {
		uint16_t address=(uint16_t)(16u+2u*index);
		if ((size_t)address>=font->size || font->size-address<2) return IO_ERROR_OUT_OF_RANGE;
		glyph.offset=(uint16_t)(font->strokes+word(font->body+address));
		glyph.present=true; glyph.finished=false;
	}
	*out=glyph; return 0;
}
int st_bgi_chr_glyph(const struct st_bgi_chr *font, uint8_t code, struct st_bgi_chr_glyph *out)
{
	if (out==NULL) return IO_ERROR_INVALID_ARGUMENT;
	uint8_t width; int status=st_bgi_chr_width(font,code,&width);
	if (status!=0) return status;
	struct st_bgi_chr_glyph glyph; status=st_bgi_chr_program(font,code,&glyph);
	if (status!=0) return status;
	glyph.width=width; *out=glyph; return 0;
}
int st_bgi_chr_next(struct st_bgi_chr_glyph *glyph, struct st_bgi_chr_record *out, bool *available)
{
	if (glyph==NULL || glyph->body==NULL || out==NULL || available==NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (glyph->finished) { *available=false; return 0; }
	if (glyph->records>=32768 || glyph->offset==UINT16_MAX || (size_t)glyph->offset>=glyph->size || glyph->size-glyph->offset<2) return IO_ERROR_OUT_OF_RANGE;
	uint8_t x=glyph->body[glyph->offset], y=glyph->body[glyph->offset+1u];
	*out=st_bgi_chr_decode(x,y);
	glyph->offset=(uint16_t)(glyph->offset+2u); ++glyph->records;
	if (out->opcode==ST_BGI_CHR_END) glyph->finished=true;
	*available=true; return 0;
}
