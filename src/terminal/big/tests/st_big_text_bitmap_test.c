/* Independent bitmap/source/outtextxy expectations, original BIG28/29/62. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_text.h"
#include "st_big_bitmap.h"
#include "st_big_draw.h"
#include "st_big_font.h"
#include "st_bgi_data.h"
#include "st_bgi_bitmap.h"
#include "io_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
struct fixture { struct st_big_state s; struct st_big_fonts fonts; uint8_t pixels[640*480]; size_t calls,reads,fail_at,row_fail_at; int row_error; uint8_t codes[1024],rows[1024]; bool varying,stroke; };
static int pixel(void *context, uint16_t x, uint16_t y, uint8_t pen, bool xor_mode)
{
	struct fixture *f=context; CHECK(x<640 && y<480 && pen==5 && xor_mode==f->stroke);
	if (++f->calls==f->fail_at) return IO_ERROR_IO;
	if (xor_mode) f->pixels[y*640+x]^=pen; else f->pixels[y*640+x]=pen;
	return 0;
}
static int row(void *context, uint8_t code, unsigned index, uint8_t *value)
{
	struct fixture *f=context; CHECK(index<8 && f->reads<1024);
	f->codes[f->reads]=code; f->rows[f->reads++]=(uint8_t)index;
	if (f->reads==f->row_fail_at) return f->row_error;
	if (f->varying) { *value=(f->reads&1)?0x80:0x01; return 0; }
	/* A sparse authored glyph with asymmetric corners proves rotation. */
	static const uint8_t bits[]={0x80,0x40,0,0,0,0,0,0x03};
	*value=bits[index]; return 0;
}
static void init(struct fixture *f, unsigned size, unsigned dir, unsigned height)
{
	memset(f,0,sizeof(*f)); st_big_state_init(&f->s); CHECK(st_big_fonts_init(&f->fonts,&f->s.bgi.error)==0);
	f->s.bgi.height=height; f->s.bgi.viewport[3]=(uint16_t)(height-1);
	CHECK(st_big_text_style(&f->s,&f->fonts,0,(uint16_t)dir,(uint16_t)size)==0);
	CHECK(st_big_draw_color(&f->s,5)==0); f->s.bgi.write_mode=1; f->s.bgi.error=-11;
	f->s.bgi.x=100; f->s.bgi.y=101;
}
static uint32_t crc(const uint8_t *bytes, size_t size)
{
	uint32_t value=UINT32_MAX;
	for (size_t i=0;i<size;++i) { value^=bytes[i]; for (unsigned bit=0;bit<8;++bit) value=(value>>1)^((value&1)?UINT32_C(0xedb88320):0); }
	return value^UINT32_MAX;
}
struct expansion { unsigned points[8][2]; size_t count; };
static int expanded(void *context, unsigned x, unsigned y)
{
	struct expansion *e=context; CHECK(e->count<8);
	e->points[e->count][0]=x; e->points[e->count++][1]=y; return 0;
}
static void shared_expansion_order(void)
{
	struct expansion e={0};
	static const unsigned expected[8][2]={{0,0},{1,0},{0,1},{1,1},{14,0},{15,0},{14,1},{15,1}};
	CHECK(st_bgi_bitmap_expand_row(0x81,2,2,expanded,&e)==0 && e.count==8 && memcmp(e.points,expected,sizeof(expected))==0);
	e.count=0; CHECK(st_bgi_bitmap_expand_row(0,2,2,expanded,&e)==0 && e.count==0);
}
static void sources(void)
{
	uint8_t bytes[2048];
	for (unsigned code=0;code<256;++code) for (unsigned y=0;y<8;++y) CHECK(st_big_bitmap_row(NULL,(uint8_t)code,y,bytes+8*code+y)==0);
	/* Computed independently from the pinned shared RIP table and BIG zero-selection alias. */
	CHECK(crc(bytes,sizeof(bytes))==UINT32_C(0xf7612cfe));
	CHECK(crc(st_bgi_bitmap_font,sizeof(st_bgi_bitmap_font))==UINT32_C(0xf7612cfe));
	CHECK(memcmp(bytes+48*8,bytes+79*8,8)==0);
	CHECK(memcmp(bytes+10*8,(uint8_t[]){254,198,186,186,186,198,254,254},8)==0);
	CHECK(memcmp(bytes+13*8,(uint8_t[]){108,108,254,108,254,108,108,0},8)==0);
	CHECK(memcmp(bytes+0xe2*8,(uint8_t[]){0,252,204,192,192,192,192,0},8)==0);
	CHECK(memcmp(bytes+0xfa*8,(uint8_t[]){0,0,0,0,24,0,0,0},8)==0);
	CHECK(st_big_bitmap_row(NULL,65,8,bytes)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_bitmap_row(NULL,65,0,NULL)==IO_ERROR_INVALID_ARGUMENT);
}
static void magnification_rotation_and_order(void)
{
	for (unsigned height=200;height<=480;height=height==200?350:height==350?480:481) for (unsigned dir=0;dir<2;++dir) for (unsigned size=1;size<=4;++size) {
		struct fixture f; init(&f,size,dir,height); const struct st_big_draw_sink pixels={&f,pixel}; const struct st_big_bitmap_font font={&f,row};
		CHECK(st_big_text_out(&f.s,80,30,(const uint8_t *)"AB",3,&font,&pixels)==0);
		CHECK(f.s.bgi.x==100 && f.s.bgi.y==101 && f.s.bgi.ddo_x==(dir?80-8*size:80) && f.s.bgi.ddo_y==30);
		CHECK(f.s.bgi.device_x==(dir?80-8*size:80+16*size) && f.s.bgi.device_y==(dir?30+16*size:30));
		CHECK(f.s.bgi.text_count==2 && f.calls==8*size*size && f.reads==(dir?16:16*size));
		uint8_t expected[640*480]={0};
		static const unsigned source[][2]={{0,0},{1,1},{6,7},{7,7}};
		for (unsigned glyph=0;glyph<2;++glyph) for (size_t i=0;i<4;++i) for (unsigned dy=0;dy<size;++dy) for (unsigned dx=0;dx<size;++dx) {
			unsigned x=source[i][0],y=source[i][1];
			if (dir) { unsigned old=x; x=y; y=7-old; }
			unsigned px=dir?80-8*size+x*size+dx:80+glyph*8*size+x*size+dx;
			unsigned py=dir?30+glyph*8*size+y*size+dy:30+y*size+dy;
			expected[py*640+px]=5;
		}
		CHECK(memcmp(f.pixels,expected,sizeof(expected))==0);
		for (size_t i=0;i<f.reads;++i) {
			CHECK(f.codes[i]==(dir?(i<8?'B':'A'):(i<8*size?'A':'B')));
			CHECK(f.rows[i]==(dir?i%8:(i/size)%8));
		}
	}
}
static void bounds_empty_and_failures(void)
{
	struct fixture f; init(&f,1,0,350); struct st_big_draw_sink pixels={&f,pixel}; struct st_big_bitmap_font font={&f,row};
	CHECK(st_big_text_out(&f.s,632,342,(const uint8_t *)"A",2,&font,&pixels)==0 && f.calls==4 && f.reads==8 && f.s.bgi.device_x==640);
	init(&f,1,0,350); CHECK(st_big_text_out(&f.s,633,342,(const uint8_t *)"AB",3,&font,&pixels)==0 && f.calls==0 && f.reads==0 && f.s.bgi.device_x==649);
	init(&f,1,0,350); CHECK(st_big_text_out(&f.s,632,343,(const uint8_t *)"A",2,&font,&pixels)==0 && f.calls==0 && f.s.bgi.device_x==640);
	init(&f,1,0,350); CHECK(st_big_text_out(&f.s,65535,30,(const uint8_t *)"AB",3,&font,&pixels)==0 && f.calls==0 && f.s.bgi.device_x==65535);
	init(&f,1,0,350); f.s.bgi.text_count=7;
	CHECK(st_big_text_out(&f.s,20,30,(const uint8_t *)"",1,&font,&pixels)==0 && f.s.bgi.text_count==7 && f.s.bgi.ddo_x==20 && f.s.bgi.device_x==20 && f.calls==0);
	uint8_t text[258]; memset(text,'A',sizeof(text)); text[256]=0;
	CHECK(st_big_text_out(&f.s,22,33,text,sizeof(text),&font,&pixels)==0 && f.s.bgi.text_count==7 && f.s.bgi.device_x==22 && f.calls==0);
	text[256]='A'; text[257]=0;
	CHECK(st_big_text_out(&f.s,22,33,text,sizeof(text),&font,&pixels)==0 && f.s.bgi.text_count==1 && f.s.bgi.device_x==30 && f.calls==4);
	init(&f,1,0,350); CHECK(st_big_text_out(&f.s,20,30,(const uint8_t *)"ABC",3,&font,&pixels)==IO_ERROR_OUT_OF_RANGE && f.s.bgi.ddo_x==20 && f.s.bgi.device_x==20 && f.reads==0);
	for (size_t fail=1;fail<=4;++fail) {
		init(&f,1,0,350); f.fail_at=fail;
		CHECK(st_big_text_out(&f.s,20,30,(const uint8_t *)"A",2,&font,&pixels)==IO_ERROR_IO && f.calls==fail && f.s.bgi.device_x==20);
		uint8_t expected[640*480]={0};
		static const unsigned trace[4][2]={{20,30},{21,31},{26,37},{27,37}};
		for (size_t i=0;i+1<fail;++i) expected[trace[i][1]*640+trace[i][0]]=5;
		CHECK(memcmp(f.pixels,expected,sizeof(expected))==0);
	}
	for (size_t fail=1;fail<=8;++fail) {
		init(&f,1,0,350); f.row_fail_at=fail; f.row_error=IO_ERROR_IO;
		CHECK(st_big_text_out(&f.s,20,30,(const uint8_t *)"A",2,&font,&pixels)==IO_ERROR_IO && f.reads==fail && f.s.bgi.device_x==20);
		CHECK(f.calls==(fail==1?0:fail==2?1:2));
	}
	init(&f,1,0,350); f.row_fail_at=1; f.row_error=IO_ERROR_IN_PROGRESS;
	CHECK(st_big_text_out(&f.s,20,30,(const uint8_t *)"A",2,&font,&pixels)==IO_ERROR_PROTOCOL && f.calls==0);
}
static void placement_and_suppression(void)
{
	for (unsigned dir=0;dir<2;++dir) for (unsigned jx=0;jx<4;++jx) for (unsigned jy=0;jy<4;++jy) for (unsigned suppress=0;suppress<4;++suppress) {
		struct fixture f; init(&f,1,dir,350); struct st_big_draw_sink pixels={&f,pixel}; const struct st_big_bitmap_font font={&f,row};
		f.s.bgi.viewport[0]=5; f.s.bgi.viewport[1]=7;
		f.s.bgi.text_justify_x=(uint8_t)jx; f.s.bgi.text_justify_y=(uint8_t)jy;
		f.s.bgi.text_suppression=(uint16_t)((suppress&1?255:0)+(suppress&2?65280:0));
		CHECK(st_big_text_out(&f.s,100,100,(const uint8_t *)"AB",3,&font,&pixels)==0 && f.calls==8);
		unsigned x=105,y=107;
		if (!(suppress&1) && (dir || jx)) { unsigned width=dir?8:16; x-=jx==1?width/2-1:width; }
		if (!(suppress&2) && jy!=2) { unsigned height=dir?16:8; y-=jy==1?height/2:height; }
		CHECK(f.s.bgi.ddo_x==x && f.s.bgi.ddo_y==y && f.s.bgi.device_x==x+(dir?0:16) && f.s.bgi.device_y==y+(dir?16:0));
	}
	struct fixture f; init(&f,1,0,350); struct st_big_draw_sink pixels={&f,pixel}; const struct st_big_bitmap_font font={&f,row};
	f.s.bgi.text_cached[0]=0; f.s.bgi.text_justify_x=1;
	CHECK(st_big_text_out(&f.s,20,30,(const uint8_t *)"A",2,&font,&pixels)==0 && f.s.bgi.ddo_x==21 && f.s.bgi.device_x==29);
}
static void live_rows_custom_and_strokes(void)
{
	struct fixture f; init(&f,2,0,350); struct st_big_draw_sink pixels={&f,pixel}; struct st_big_bitmap_font font={&f,row}; f.varying=true;
	CHECK(st_big_text_out(&f.s,20,30,(const uint8_t *)"A",2,&font,&pixels)==0 && f.reads==16 && f.calls==32);
	for (unsigned y=0;y<16;++y) for (unsigned x=0;x<16;++x) CHECK(f.pixels[(y+30)*640+x+20]==((y&1)?(x>=14?5:0):(x<2?5:0)));
	init(&f,1,1,350); CHECK(st_big_text_custom(&f.s,(uint16_t[]){2,1,3,2},NULL)==0);
	CHECK(st_big_text_out(&f.s,20,30,(const uint8_t *)"AB",3,&font,&pixels)==0 && f.s.bgi.ddo_x==17 && f.s.bgi.device_x==17 && f.s.bgi.device_y==46);
	/* Actual Small AA: complete outtextxy preserves public CP and the
	 * unaligned initial device anchor while its DDO stroke path advances. */
	init(&f,1,0,350); CHECK(st_big_text_style(&f.s,&f.fonts,2,0,1)==0);
	f.stroke=true;
	CHECK(st_big_text_out(&f.s,20,30,(const uint8_t *)"AA",3,NULL,&pixels)==0 && f.calls==30 && f.reads==0);
	CHECK(f.s.bgi.text_total_width==7 && f.s.bgi.ddo_x==26 && f.s.bgi.ddo_y==35 && f.s.bgi.device_x==20 && f.s.bgi.device_y==30 && f.s.bgi.x==100 && f.s.bgi.y==101);
	struct st_big_draw_sink solid=pixels; solid.pixel=NULL;
	CHECK(st_big_text_out(&f.s,20,30,(const uint8_t *)"AA",3,NULL,&solid)==IO_ERROR_INVALID_ARGUMENT);
}
int main(void) { sources(); shared_expansion_order(); magnification_rotation_and_order(); bounds_empty_and_failures(); placement_and_suppression(); live_rows_custom_and_strokes(); return 0; }
