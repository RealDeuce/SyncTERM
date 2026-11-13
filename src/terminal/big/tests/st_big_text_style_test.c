/* Independent E/public/DDO/device expectations, original BIG28/30/53. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_text.h"
#include "st_big_font.h"
#include "io_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
struct fixture { struct st_big_state s; struct st_big_fonts fonts; };
static void init(struct fixture *f)
{
	memset(f,0,sizeof(*f)); st_big_state_init(&f->s);
	CHECK(st_big_fonts_init(&f->fonts,&f->s.bgi.error)==0); f->s.bgi.error=-11;
}
static void ordinary(void)
{
	/* Literal original metrics: cap-descender BYTE28,8,28,28 and ratios. */
	static const uint16_t heights[4][5]={{28,16,18,21,28},{8,4,5,6,8},{28,16,18,21,28},{28,16,18,21,28}};
	for (uint16_t font=0;font<=4;++font) for (uint16_t size=0;size<=4;++size) for (uint16_t dir=0;dir<2;++dir) {
		struct fixture f; init(&f); struct st_big_state before=f.s;
		uint16_t args[]={font,size,dir}; CHECK(st_big_text_e(&f.s,&f.fonts,args)==0);
		CHECK(f.s.bgi.font==font && f.s.bgi.direction==dir && f.s.bgi.text_size==size && f.s.bgi.error==-11);
		CHECK(f.s.bgi.text_font==font && f.s.bgi.text_direction==dir);
		CHECK(f.s.bgi.x==before.bgi.x && f.s.bgi.ddo_x==before.bgi.ddo_x && f.s.bgi.device_x==before.bgi.device_x);
		if (font) {
			CHECK(f.fonts.loaded==font && f.fonts.active==f.fonts.slots[font-1].body);
			CHECK(f.s.bgi.text_stroke_active==255 && f.s.bgi.text_suppression==65535 && f.s.bgi.text_stroke_id==font && f.s.bgi.text_stroke_direction==dir);
			CHECK(f.s.bgi.text_body==f.fonts.active && f.s.bgi.text_body_size==f.fonts.active_size && f.s.bgi.text_selected_size==size);
			CHECK(f.s.bgi.text_cached[0]==heights[font-1][size] && f.s.bgi.text_cached[1]==heights[font-1][size]);
			CHECK(f.s.bgi.text_scaled[0]==heights[font-1][size] && f.s.bgi.text_scaled[1]==heights[font-1][size]);
			CHECK(f.s.bgi.text_header[0]=='+' && f.s.bgi.text_header[7]==0);
			CHECK(f.s.bgi.device_text_font==0 && f.s.bgi.device_text_direction==0 && f.s.bgi.device_text_dimensions[0]==8 && f.s.bgi.device_text_magnification[0]==1);
		}
		else {
			uint16_t cell=size?8*size:8;
			CHECK(f.fonts.loaded==0 && f.fonts.active==NULL && f.s.bgi.text_stroke_active==0 && f.s.bgi.text_body==NULL && f.s.bgi.text_stroke_id==255);
			CHECK(f.s.bgi.text_cached[0]==cell && f.s.bgi.text_cached[1]==cell && f.s.bgi.text_selected_size==8);
			CHECK(f.s.bgi.device_text_font==0 && f.s.bgi.device_text_direction==dir);
			for (size_t axis=0;axis<2;++axis) CHECK(f.s.bgi.device_text_dimensions[axis]==cell && f.s.bgi.device_text_magnification[axis]==cell/8 && f.s.bgi.device_text_extent[axis]==cell-2);
		}
	}
}
static void late_clamps(void)
{
	for (size_t part=0;part<3;++part) for (unsigned word=0;word<65536;++word) {
		struct fixture f; init(&f); uint16_t args[]={2,3,1}; args[part]=(uint16_t)word;
		CHECK(st_big_text_e(&f.s,&f.fonts,args)==0);
		CHECK(f.s.bgi.font==(args[0]<=4?args[0]:0) && f.s.bgi.text_size==(args[1]<=4?args[1]:1) && f.s.bgi.direction==(args[2]<=1?args[2]:0));
	}
}
static void caches_and_custom(void)
{
	struct fixture f; init(&f);
	CHECK(st_big_text_custom(&f.s,(const uint16_t[]){2,1,3,2},NULL)==0);
	CHECK(st_big_text_style(&f.s,&f.fonts,1,1,0)==0 && f.s.bgi.text_cached[0]==56 && f.s.bgi.text_cached[1]==42);
	uint8_t replacement[16]={'+',1,0,0,32,16,0,7,6,0,254};
	f.fonts.slots[0].body=replacement; f.fonts.slots[0].size=sizeof(replacement);
	const uint8_t *old=f.fonts.active;
	CHECK(st_big_text_style(&f.s,&f.fonts,1,0,0)==0 && f.fonts.active==old && f.s.bgi.text_body==old);
	CHECK(st_big_text_style(&f.s,&f.fonts,0,1,0)==0 && f.fonts.loaded==1 && f.fonts.active==old);
	CHECK(f.s.bgi.text_body==NULL && f.s.bgi.text_stroke_active==0 && f.s.bgi.text_stroke_id==1 && f.s.bgi.text_header[8]==21);
	CHECK(st_big_text_style(&f.s,&f.fonts,1,0,0)==0 && f.fonts.active==replacement && f.s.bgi.text_body==replacement);
	/* Same remembered DDO id skips the copied header, despite a new body. */
	CHECK(f.s.bgi.text_header[8]==21 && f.s.bgi.text_cached[0]==56 && f.s.bgi.text_cached[1]==42);
	CHECK(st_big_text_style(&f.s,&f.fonts,2,0,4)==0 && f.s.bgi.text_header[8]==6 && f.s.bgi.text_cached[0]==8);
	CHECK(st_big_text_style(&f.s,&f.fonts,1,0,0)==0 && f.s.bgi.text_header[8]==6 && f.s.bgi.text_header[7]==0 && f.s.bgi.text_cached[0]==16 && f.s.bgi.text_cached[1]==12);
	CHECK(memcmp(f.s.bgi.saved_ratio,(uint16_t[]){2,1,3,2},8)==0);
	CHECK(st_big_text_style(&f.s,&f.fonts,0,0,257)==0 && f.s.bgi.text_size==257 && f.s.bgi.text_cached[0]==8);
	CHECK(st_big_text_style(&f.s,&f.fonts,0,0,256)==0 && f.s.bgi.text_size==256 && f.s.bgi.text_cached[0]==8);
	static const uint16_t height[]={16,18,21,28,37,46,56,70,84,112};
	init(&f);
	for (uint16_t size=1;size<=10;++size) {
		CHECK(st_big_text_style(&f.s,&f.fonts,1,0,size)==0 && f.s.bgi.text_cached[0]==height[size-1]);
		CHECK(memcmp(f.s.bgi.saved_ratio,(uint16_t[]){1,1,1,1},8)==0);
	}
}
static void failures_and_bypass(void)
{
	struct fixture f; init(&f); CHECK(st_big_text_style(&f.s,&f.fonts,1,0,4)==0);
	CHECK(st_big_text_style(&f.s,&f.fonts,11,1,4)==0 && f.s.bgi.error==-14 && f.s.bgi.font==0 && f.s.bgi.text_size==1 && f.s.bgi.direction==1 && f.fonts.loaded==1);
	f.fonts.slots[1].body=NULL;
	CHECK(st_big_text_style(&f.s,&f.fonts,2,1,3)==0 && f.s.bgi.error==-8 && f.s.bgi.font==0 && f.s.bgi.text_size==1 && f.fonts.loaded==1);
	f.s.bgi.state=0;
	CHECK(st_big_text_style(&f.s,&f.fonts,3,1,2)==IO_ERROR_PROTOCOL && f.s.bgi.error==-1);
	CHECK(f.s.bgi.font==3 && f.s.bgi.direction==1 && f.s.bgi.text_size==2 && f.fonts.loaded==3 && f.s.bgi.text_font==0);
	init(&f); f.s.bgi.text_control=1;
	CHECK(st_big_text_style(&f.s,&f.fonts,2,1,3)==0 && f.fonts.loaded==2 && f.s.bgi.font==2 && f.s.bgi.text_size==3);
	CHECK(f.s.bgi.text_font==0 && f.s.bgi.text_cached[0]==8 && f.s.bgi.text_stroke_id==255 && f.s.bgi.text_body==NULL);
	CHECK(f.s.bgi.device_text_font==2 && f.s.bgi.device_text_direction==1 && f.s.bgi.device_text_dimensions[0]==0 && f.s.bgi.device_text_magnification[0]==1);
	init(&f); uint8_t short_body[]={'+',0}; f.fonts.slots[0].body=short_body; f.fonts.slots[0].size=sizeof(short_body);
	CHECK(st_big_text_style(&f.s,&f.fonts,1,1,3)==IO_ERROR_OUT_OF_RANGE && f.s.bgi.font==1 && f.fonts.loaded==1);
	CHECK(f.s.bgi.text_stroke_active==255 && f.s.bgi.text_selected_size==3 && f.s.bgi.text_stroke_id==1 && f.s.bgi.text_cached[0]==3 && f.s.bgi.text_header[8]==0);
	init(&f); uint8_t nonstroke[16]={'?'}; f.fonts.slots[0].body=nonstroke; f.fonts.slots[0].size=sizeof(nonstroke);
	CHECK(st_big_text_style(&f.s,&f.fonts,1,1,3)==0 && f.s.bgi.font==1 && f.fonts.loaded==1 && f.s.bgi.text_stroke_active==0);
	CHECK(f.s.bgi.text_body==NULL && f.s.bgi.text_stroke_id==255 && f.s.bgi.text_cached[0]==0 && f.s.bgi.device_text_font==1 && f.s.bgi.device_text_magnification[0]==1);
	init(&f); f.s.bgi.saved_ratio[0]=65535; f.s.bgi.saved_ratio[1]=1;
	CHECK(st_big_text_style(&f.s,&f.fonts,1,0,0)==IO_ERROR_OUT_OF_RANGE && f.s.bgi.font==1 && f.s.bgi.text_stroke_id==1 && f.s.bgi.text_cached[0]==0 && f.s.bgi.text_scaled[0]==0);
	init(&f); f.s.bgi.saved_ratio[0]=2; f.s.bgi.saved_ratio[2]=65535;
	CHECK(st_big_text_style(&f.s,&f.fonts,1,0,0)==IO_ERROR_OUT_OF_RANGE && f.s.bgi.text_scaled[0]==56 && f.s.bgi.text_scaled[1]==0 && f.s.bgi.text_cached[0]==0);
	CHECK(st_big_text_e(NULL,&f.fonts,(uint16_t[]){1,2,0})==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_text_e(&f.s,&f.fonts,NULL)==IO_ERROR_INVALID_ARGUMENT);
}
int main(void) { ordinary(); late_clamps(); caches_and_custom(); failures_and_bypass(); return 0; }
