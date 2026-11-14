/* Public/DDO/device text style and custom ratios, original BIG28/30/53. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_text.h"
#include "st_big_font.h"
#include "st_big_bitmap.h"
#include "st_big_draw.h"
#include "st_big_number.h"
#include "st_bgi_data.h"
#include "st_bgi_chr.h"
#include "io_error.h"
#include <string.h>
static int metrics(struct st_big_state *s)
{
	uint8_t height=(uint8_t)(s->bgi.text_header[8]-s->bgi.text_header[10]);
	for (size_t axis=0;axis<2;++axis) {
		uint16_t denominator=s->bgi.effective_ratio[2*axis+1];
		if (denominator==0) return IO_ERROR_OUT_OF_RANGE;
		uint32_t scaled=(uint32_t)height*s->bgi.effective_ratio[2*axis]/denominator;
		if (scaled>UINT16_MAX) return IO_ERROR_OUT_OF_RANGE;
		s->bgi.text_scaled[axis]=(uint16_t)scaled;
	}
	return 0;
}
static void custom_ratios(struct st_big_state *s)
{
	for (size_t i=0;i<4;++i) s->bgi.effective_ratio[i]=s->bgi.saved_ratio[i];
	if (s->bgi.effective_ratio[1]==0) s->bgi.effective_ratio[1]=1;
	if (s->bgi.effective_ratio[3]==0) s->bgi.effective_ratio[3]=1;
}
static void bitmap_style(struct st_big_state *s, uint8_t font, uint8_t direction, uint16_t dimensions[2])
{
	s->bgi.device_text_font=font; s->bgi.device_text_direction=direction;
	for (size_t axis=0;axis<2;++axis) {
		dimensions[axis]&=0xf8;
		s->bgi.device_text_dimensions[axis]=dimensions[axis];
		uint8_t mag=(uint8_t)(dimensions[axis]/8);
		if (mag==0) mag=1;
		s->bgi.device_text_magnification[axis]=mag;
		s->bgi.device_text_extent[axis]=(uint16_t)(8u*mag-2);
	}
}
int st_big_text_style(struct st_big_state *s, struct st_big_fonts *fonts, uint16_t font, uint16_t direction, uint16_t size)
{
	if (s==NULL || direction>1 || (font!=0 && fonts==NULL)) return IO_ERROR_INVALID_ARGUMENT;
	if (font!=0) {
		if (font>32767 || font>fonts->count) { s->bgi.error=-14; font=0; size=1; }
		else if (font!=s->bgi.font) {
			/* Only borrowed native bindings exist. Repeating the selected
			 * font skips this refresh; repeating loaded after font0 does not. */
			if (font>ST_BIG_FONT_SLOTS) return IO_ERROR_OUT_OF_RANGE;
			const struct st_big_font_binding *slot=&fonts->slots[font-1];
			if (font!=fonts->loaded && slot->body==NULL) {
				s->bgi.error=-8; font=0; size=1;
			}
			else {
				fonts->loaded=font; fonts->active=slot->body; fonts->active_size=slot->size;
			}
		}
	}
	s->bgi.font=font; s->bgi.direction=direction; s->bgi.text_size=size;
	if (s->bgi.state==0) { s->bgi.error=-1; return IO_ERROR_PROTOCOL; }
	uint8_t device_font=(uint8_t)font, selected_size=(uint8_t)size;
	/* Supplied EGAVGA descriptor cell bytes are8/8 in every BIG mode;
	 * these BGI bitmap metrics are independent of console8/14/16 fonts. */
	uint16_t dimensions[2]={8u*selected_size,8u*selected_size};
	if (device_font!=0) dimensions[0]=dimensions[1]=selected_size;
	if (s->bgi.text_control!=0) {
		bitmap_style(s,device_font,(uint8_t)direction,dimensions); return 0;
	}
	s->bgi.text_font=device_font; s->bgi.text_direction=(uint8_t)direction;
	s->bgi.text_cached[0]=dimensions[0]; s->bgi.text_cached[1]=dimensions[1];
	s->bgi.text_selected_size=8; s->bgi.text_stroke_active=0;
	s->bgi.text_suppression=0; s->bgi.text_body=NULL; s->bgi.text_body_size=0;
	if (device_font!=0 && fonts->active!=NULL) {
		if (fonts->active_size==0) return IO_ERROR_OUT_OF_RANGE;
		if (fonts->active[0]=='+') {
			s->bgi.text_body=fonts->active; s->bgi.text_body_size=fonts->active_size;
			s->bgi.text_stroke_active=255; s->bgi.text_selected_size=dimensions[0]; s->bgi.text_suppression=65535;
		}
	}
	if (s->bgi.text_stroke_active) {
		s->bgi.text_stroke_direction=(uint8_t)direction;
		if (device_font!=s->bgi.text_stroke_id) {
			s->bgi.text_stroke_id=device_font;
			if (s->bgi.text_body_size<sizeof(s->bgi.text_header)) return IO_ERROR_OUT_OF_RANGE;
			memcpy(s->bgi.text_header,s->bgi.text_body,sizeof(s->bgi.text_header));
			s->bgi.text_header[7]=0;
		}
		/* DDO private aspect starts10000 and is not the library's per-mode
		 * curve aspect. No ordinary BIG command changes that private value. */
		if (selected_size==0) custom_ratios(s);
		else {
			size_t index=selected_size<10?selected_size:10;
			s->bgi.effective_ratio[0]=s->bgi.effective_ratio[2]=(uint16_t)st_bgi_stroke_mults[index];
			s->bgi.effective_ratio[1]=s->bgi.effective_ratio[3]=(uint16_t)st_bgi_stroke_divs[index];
		}
		int status=metrics(s); if (status!=0) return status;
		dimensions[0]=s->bgi.text_scaled[0]; dimensions[1]=s->bgi.text_scaled[1];
	}
	else {
		if (dimensions[0]==0) dimensions[0]=dimensions[1]=8;
		bitmap_style(s,device_font,(uint8_t)direction,dimensions);
	}
	s->bgi.text_cached[0]=dimensions[0]; s->bgi.text_cached[1]=dimensions[1];
	return 0;
}
int st_big_text_e(struct st_big_state *s, struct st_big_fonts *fonts, const uint16_t arguments[3])
{
	if (arguments==NULL) return IO_ERROR_INVALID_ARGUMENT;
	uint16_t font=arguments[0]<=4?arguments[0]:0;
	uint16_t size=arguments[1]<=4?arguments[1]:1;
	uint16_t direction=arguments[2]<=1?arguments[2]:0;
	return st_big_text_style(s,fonts,font,direction,size);
}
int st_big_text_custom(struct st_big_state *s, const uint16_t ratios[4], const struct st_big_text_sink *sink)
{
	if (s==NULL || ratios==NULL || (sink!=NULL && sink->custom==NULL)) return IO_ERROR_INVALID_ARGUMENT;
	uint16_t r[4]; memcpy(r,ratios,sizeof(r));
	uint16_t max_x=(uint16_t)(s->bgi.width-1u), max_y=(uint16_t)(s->bgi.height-1u);
	if (r[0]>max_x || r[1]>max_x || r[2]>max_y || r[3]>max_y) {
		s->bgi.text_size=0; return 0;
	}
	if (s->bgi.state==0) { s->bgi.error=-1; return IO_ERROR_PROTOCOL; }
	/* The front's direct-device path skips all DDO caches/private ratios.
	 * Native owner changes occur only at serialized callback boundaries. */
	bool bypass=s->bgi.text_control!=0;
	uint16_t dimensions[2]={r[1],r[2]};
	if (sink!=NULL) {
		int status=sink->custom(sink->context,r,dimensions);
		if (status==IO_ERROR_IN_PROGRESS) status=IO_ERROR_PROTOCOL;
		if (status!=0) return status;
	}
	if (!bypass) {
		s->bgi.text_cached[0]=dimensions[0]; s->bgi.text_cached[1]=dimensions[1];
		/* Lower custom dispatch sets then clears AH's sign bit before
		 * saving its restored original arguments, even for raw API words. */
		s->bgi.saved_ratio[0]=(uint16_t)(r[0]&0x7fff);
		for (size_t i=1;i<4;++i) s->bgi.saved_ratio[i]=r[i];
		s->bgi.text_selected_size=0;
		custom_ratios(s);
		int status=metrics(s); if (status!=0) return status;
	}
	s->bgi.text_size=0;
	return 0;
}
int st_big_text_x3(struct st_big_state *s, const uint16_t ratios[4], const struct st_big_text_sink *sink)
{
	if (s==NULL || ratios==NULL) return IO_ERROR_INVALID_ARGUMENT;
	uint16_t r[4];
	for (size_t i=0;i<4;++i) r[i]=ratios[i]>=1 && ratios[i]<=4?ratios[i]:1;
	return st_big_text_custom(s,r,sink);
}
static struct st_bgi_chr stroke_font(const struct st_big_state *s)
{
	const uint8_t *h=s->bgi.text_header;
	return (struct st_bgi_chr){s->bgi.text_body,s->bgi.text_body_size,
	    (uint16_t)(h[1]+256u*h[2]),(uint16_t)(h[5]+256u*h[6]),h[4]};
}
static int stroke_metrics(struct st_big_state *s, const struct st_bgi_chr *font, const uint8_t *bytes, uint8_t count)
{
	int status=metrics(s); if (status!=0) return status;
	uint16_t width=0;
	for (unsigned i=0;i<count;++i) {
		uint8_t value; status=st_bgi_chr_width(font,bytes[i],&value); if (status!=0) return status;
		width=(uint16_t)(width+value);
	}
	uint16_t denominator=s->bgi.effective_ratio[1];
	if (denominator==0) return IO_ERROR_OUT_OF_RANGE;
	uint32_t scaled=(uint32_t)width*s->bgi.effective_ratio[0]/denominator;
	if (scaled>UINT16_MAX) return IO_ERROR_OUT_OF_RANGE;
	s->bgi.text_total_width=(uint16_t)scaled; return 0;
}
static void stroke_align(struct st_big_state *s)
{
	uint16_t extent=s->bgi.text_total_width;
	if (s->bgi.text_stroke_direction!=0) {
		extent=s->bgi.text_scaled[1]; s->bgi.ddo_x=(uint16_t)(s->bgi.ddo_x+extent);
	}
	uint8_t justify=s->bgi.text_justify_x;
	if (justify!=0) {
		if (justify==1) extent=(uint16_t)((extent>>1)-1u);
		if (s->bgi.orientation!=0) s->bgi.ddo_y=(uint16_t)(s->bgi.ddo_y+extent);
		else s->bgi.ddo_x=(uint16_t)(s->bgi.ddo_x-extent);
	}
	extent=s->bgi.text_stroke_direction!=0?s->bgi.text_total_width:s->bgi.text_scaled[1];
	justify=s->bgi.text_justify_y;
	if (justify!=0) {
		if (justify==1) extent>>=1;
		if (s->bgi.orientation!=0) s->bgi.ddo_x=(uint16_t)(s->bgi.ddo_x+extent);
		else s->bgi.ddo_y=(uint16_t)(s->bgi.ddo_y+extent);
	}
}
static int stroke_style(struct st_big_state *s, uint8_t style)
{
	s->bgi.ddo_line_style=style; s->bgi.ddo_line_width=1;
	if (style>4) return IO_ERROR_OUT_OF_RANGE;
	s->bgi.line_mask=style==4?s->bgi.ddo_line_user:st_bgi_line_patterns[style];
	s->bgi.device_line_width=1; return 0;
}
static int stroke_scale(const struct st_big_state *s, int coordinate, size_t axis, uint16_t *out)
{
	int32_t value=coordinate*st_big_number_signed(s->bgi.effective_ratio[2*axis]);
	uint16_t raw_divisor=s->bgi.effective_ratio[2*axis+1];
	if (raw_divisor!=1) {
		int32_t divisor=st_big_number_signed(raw_divisor);
		if (divisor==0) return IO_ERROR_OUT_OF_RANGE;
		value/=divisor;
		if (value<-32768 || value>32767) return IO_ERROR_OUT_OF_RANGE;
	}
	/* Divisor1 skips IDIV entirely; the original keeps the low product
	 * WORD, without a signed-quotient check on that particular path. */
	*out=(uint16_t)value; return 0;
}
static void stroke_advance(struct st_big_state *s)
{
	uint16_t x=s->bgi.text_stroke_point[0],y=s->bgi.text_stroke_point[1];
	uint8_t orientation=s->bgi.orientation&1;
	if (orientation==s->bgi.text_stroke_direction) {
		if (orientation!=0) x=(uint16_t)(0u-x);
	} else { uint16_t swap=x; x=y; y=swap; }
	s->bgi.ddo_x=(uint16_t)(s->bgi.ddo_x+x); s->bgi.ddo_y=(uint16_t)(s->bgi.ddo_y-y);
}
static void stroke_transform(const struct st_big_state *s, uint16_t *x, uint16_t *y)
{
	uint8_t orientation=s->bgi.orientation&1;
	if (orientation==s->bgi.text_stroke_direction) {
		if (orientation!=0) *x=(uint16_t)(0u-*x);
		else *y=(uint16_t)(0u-*y);
	} else { uint16_t swap=*x; *x=(uint16_t)(0u-*y); *y=(uint16_t)(0u-swap); }
	*x=(uint16_t)(*x+s->bgi.ddo_x); *y=(uint16_t)(*y+s->bgi.ddo_y);
}
static int stroke_glyph(struct st_big_state *s, const struct st_bgi_chr *font, uint8_t byte, const struct st_big_draw_sink *sink)
{
	s->bgi.text_stroke_point[0]=s->bgi.text_stroke_point[1]=0;
	struct st_bgi_chr_glyph glyph; int status=st_bgi_chr_program(font,byte,&glyph);
	if (status!=0 || !glyph.present) return status;
	for (;;) {
		struct st_bgi_chr_record record; bool available;
		status=st_bgi_chr_next(&glyph,&record,&available); if (status!=0) return status;
		if (!available) return IO_ERROR_PROTOCOL;
		if (record.opcode==ST_BGI_CHR_END) { stroke_advance(s); return 0; }
		if (record.opcode==ST_BGI_CHR_IGNORE) continue;
		uint16_t point[2];
		status=stroke_scale(s,record.x,0,point); if (status!=0) return status;
		status=stroke_scale(s,record.y,1,point+1); if (status!=0) return status;
		uint16_t line[]={point[0],point[1],s->bgi.text_stroke_point[0],s->bgi.text_stroke_point[1]};
		s->bgi.text_stroke_point[0]=point[0]; s->bgi.text_stroke_point[1]=point[1];
		if (record.opcode==ST_BGI_CHR_DRAW) {
			stroke_transform(s,line,line+1); stroke_transform(s,line+2,line+3);
			status=st_big_draw_physical_line(s,line,sink); if (status!=0) return status;
		}
	}
}
int st_big_text_stroke_direct(struct st_big_state *s, const uint8_t *bytes, uint8_t count, const struct st_big_draw_sink *sink)
{
	if (s==NULL || (bytes==NULL && count!=0) || sink==NULL || sink->pixel==NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (s->bgi.text_stroke_id==255 || count==0) return 0;
	if (s->bgi.state==0) { s->bgi.error=-1; return IO_ERROR_PROTOCOL; }
	uint8_t saved_style=s->bgi.ddo_line_style, saved_width=s->bgi.ddo_line_width;
	struct st_bgi_chr font=stroke_font(s);
	int status=stroke_metrics(s,&font,bytes,count); if (status!=0) return status;
	stroke_align(s);
	status=stroke_style(s,0); if (status!=0) return status;
	for (unsigned i=0;i<count && bytes[i]!=0;++i) {
		/* Each glyph binds the current DDO body/header, while its record
		 * stream retains that borrowed binding across line callbacks. */
		font=stroke_font(s);
		status=stroke_glyph(s,&font,bytes[i],sink); if (status!=0) return status;
	}
	status=stroke_style(s,saved_style); if (status!=0) return status;
	s->bgi.ddo_line_style=saved_style; s->bgi.ddo_line_width=saved_width;
	return 0;
}
static void bitmap_align(struct st_big_state *s, uint8_t count)
{
	uint16_t x=s->bgi.ddo_x,y=s->bgi.ddo_y;
	if ((uint8_t)s->bgi.text_suppression==0) {
		uint8_t justify=s->bgi.text_justify_x;
		if (s->bgi.text_direction==1 || justify!=0) {
			uint16_t extent=s->bgi.text_direction==1?s->bgi.text_cached[1]:(uint16_t)(count*s->bgi.text_cached[0]);
			if (justify==1) extent=(uint16_t)((extent>>1)-1u);
			x=(uint16_t)(x-extent);
		}
	}
	if ((s->bgi.text_suppression>>8)==0 && s->bgi.text_justify_y!=2) {
		uint16_t extent=s->bgi.text_direction==1?(uint16_t)(count*s->bgi.text_cached[0]):s->bgi.text_cached[1];
		if (s->bgi.text_justify_y==1) extent>>=1;
		y=(uint16_t)(y-extent);
	}
	s->bgi.ddo_x=s->bgi.device_x=x; s->bgi.ddo_y=s->bgi.device_y=y;
}
int st_big_text_out(struct st_big_state *s, uint16_t x, uint16_t y, const uint8_t *bytes, size_t extent,
    const struct st_big_bitmap_font *font, const struct st_big_draw_sink *sink)
{
	if (s==NULL || bytes==NULL || sink==NULL || sink->pixel==NULL || (font!=NULL && font->row==NULL)) return IO_ERROR_INVALID_ARGUMENT;
	if (s->bgi.state==0) { s->bgi.error=-1; return IO_ERROR_PROTOCOL; }
	s->bgi.ddo_x=s->bgi.device_x=(uint16_t)(x+s->bgi.viewport[0]);
	s->bgi.ddo_y=s->bgi.device_y=(uint16_t)(y+s->bgi.viewport[1]);
	size_t limit=extent<65535?extent:65535;
	const uint8_t *end=memchr(bytes,0,limit);
	if (end==NULL) return IO_ERROR_OUT_OF_RANGE;
	uint8_t count=(uint8_t)(end-bytes);
	if (count==0) return 0;
	s->bgi.text_count=count; bitmap_align(s,count);
	if ((uint8_t)s->bgi.text_suppression==0 && st_big_number_signed(s->bgi.ddo_x)<0) return 0;
	return s->bgi.text_stroke_active!=0?st_big_text_stroke_direct(s,bytes,count,sink):st_big_text_bitmap(s,bytes,count,font,sink);
}
