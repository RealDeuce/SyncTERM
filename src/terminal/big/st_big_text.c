/* Public/DDO/device text style and custom ratios, original BIG28/30/53. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_text.h"
#include "st_big_font.h"
#include "st_bgi_data.h"
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
