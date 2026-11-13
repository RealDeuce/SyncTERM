/* Independent direct CHR text geometry/advance/style prefixes, BIG28/62. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_text.h"
#include "st_big_font.h"
#include "st_big_draw.h"
#include "st_bgi_chr.h"
#include "io_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
struct fixture { struct st_big_state s; struct st_big_fonts fonts; uint8_t pixels[64*64]; size_t calls,fail_at; bool mutate; const uint8_t *next_body; size_t next_size; };
static int pixel(void *context, uint16_t x, uint16_t y, uint8_t pen, bool xor_mode)
{
	struct fixture *f=context;
	if (x>=64 || y>=64 || pen!=5) fprintf(stderr,"font%u direction%u orientation%u pixel%u,%u pen%u\n",f->s.bgi.font,f->s.bgi.direction,f->s.bgi.orientation,x,y,pen);
	CHECK(x<64 && y<64 && pen==5);
	CHECK(f->s.bgi.line_style==2 && f->s.bgi.line_width==3);
	CHECK(f->s.bgi.ddo_line_style==0 && f->s.bgi.ddo_line_width==1 && f->s.bgi.device_line_width==1);
	if (++f->calls==f->fail_at) return IO_ERROR_IO;
	if (f->mutate) f->s.bgi.ddo_line_user=0x5678;
	if (f->next_body!=NULL) { f->s.bgi.text_body=f->next_body; f->s.bgi.text_body_size=f->next_size; }
	if (xor_mode) f->pixels[y*64+x]^=pen; else f->pixels[y*64+x]=pen;
	return 0;
}
static struct st_big_draw_sink sink(struct fixture *f) { return (struct st_big_draw_sink){f,pixel}; }
static void init(struct fixture *f, unsigned font, unsigned size, unsigned dir)
{
	memset(f,0,sizeof(*f)); st_big_state_init(&f->s); CHECK(st_big_fonts_init(&f->fonts,&f->s.bgi.error)==0);
	CHECK(st_big_text_style(&f->s,&f->fonts,(uint16_t)font,(uint16_t)dir,(uint16_t)size)==0);
	/* A bounded native surface also exercises ordinary edge clipping. */
	f->s.bgi.width=f->s.bgi.height=64; f->s.bgi.viewport[2]=f->s.bgi.viewport[3]=63;
	CHECK(st_big_draw_style(&f->s,2,0x1234,3)==0 && st_big_draw_color(&f->s,5)==0);
	f->s.bgi.x=f->s.bgi.y=100; f->s.bgi.ddo_x=f->s.bgi.device_x=20; f->s.bgi.ddo_y=f->s.bgi.device_y=30;
	f->s.bgi.error=-11;
}
static void retained(const struct fixture *f)
{
	CHECK(f->s.bgi.x==100 && f->s.bgi.y==100 && f->s.bgi.device_x==20 && f->s.bgi.device_y==30 && f->s.bgi.error==-11);
	CHECK(f->s.bgi.line_style==2 && f->s.bgi.line_width==3 && f->s.bgi.line_user==0x1234);
	CHECK(f->s.bgi.ddo_line_style==2 && f->s.bgi.ddo_line_width==3 && f->s.bgi.device_line_width==1 && f->s.bgi.line_mask==0xfc78);
}
static void small_AA(void)
{
	/* Original Small A records: M0,0 L0,4 L2,6 L4,4 L4,0
	 * L4,3 L0,3 M6,0 END. At3/5 these produce this literal mask. */
	static const uint16_t points[][2]={{20,34},{20,33},{20,32},{21,31},{22,32},{22,33},{22,34},{21,33}};
	for (unsigned dir=0;dir<2;++dir) for (unsigned xormode=0;xormode<2;++xormode) {
		struct fixture f; init(&f,2,1,dir); f.s.bgi.write_mode=(uint16_t)xormode;
		struct st_big_draw_sink io=sink(&f);
		CHECK(st_big_text_stroke_direct(&f.s,(const uint8_t *)"AA",2,&io)==0);
		CHECK(f.s.bgi.text_total_width==7 && f.s.bgi.text_scaled[0]==4 && f.s.bgi.text_scaled[1]==4 && f.calls==30);
		CHECK(f.s.bgi.ddo_x==(dir?24:26) && f.s.bgi.ddo_y==(dir?31:34));
		CHECK(f.s.bgi.text_stroke_point[0]==3 && f.s.bgi.text_stroke_point[1]==0); retained(&f);
		uint8_t expected[64*64]={0};
		for (unsigned copy=0;copy<2;++copy) for (size_t i=0;i<sizeof(points)/sizeof(points[0]);++i) {
			if (xormode && i!=0 && i!=5 && i!=7) continue;
			unsigned x=points[i][0]+3*copy,y=points[i][1];
			/* Quarter-turn of the source path about each baseline, with
			 * source terminal advance along negative device Y. */
			if (dir) { x=24-(34-points[i][1]); y=37-(points[i][0]-20)-3*copy; }
			expected[y*64+x]=5;
		}
		CHECK(memcmp(f.pixels,expected,sizeof(expected))==0);
	}
}
static void format_access_order(void)
{
	/* Width access must not inspect an offset table; program access must
	 * not inspect widths. Use WORD wrapping to separate their extents. */
	const uint8_t bytes[]={7,9}; uint8_t width=99;
	struct st_bgi_chr font={bytes,sizeof(bytes),32760,0,65};
	CHECK(st_bgi_chr_width(&font,65,&width)==0 && width==7);
	struct st_bgi_chr_glyph glyph;
	CHECK(st_bgi_chr_program(&font,65,&glyph)==IO_ERROR_OUT_OF_RANGE);
	uint8_t body[18]={0}; body[16]=9;
	font=(struct st_bgi_chr){body,sizeof(body),1,0,65};
	CHECK(st_bgi_chr_program(&font,65,&glyph)==0 && glyph.present && glyph.offset==9);
	width=99; CHECK(st_bgi_chr_width(&font,65,&width)==IO_ERROR_OUT_OF_RANGE && width==99);
	CHECK(st_bgi_chr_width(&font,64,&width)==0 && width==0);
}
static void missing_zero_and_layout(void)
{
	struct fixture f; init(&f,2,1,0); struct st_big_draw_sink io=sink(&f);
	CHECK(st_big_text_stroke_direct(&f.s,(const uint8_t[]){1,'A'},2,&io)==0 && f.calls==15 && f.s.bgi.text_total_width==3 && f.s.bgi.ddo_x==23 && f.s.bgi.ddo_y==34);
	init(&f,4,4,0); io=sink(&f);
	CHECK(st_big_text_stroke_direct(&f.s,(const uint8_t[]){22},1,&io)==0 && f.calls>0 && f.s.bgi.text_total_width==0 && f.s.bgi.ddo_x==34 && f.s.bgi.ddo_y==58);
	init(&f,4,4,0); io=sink(&f); f.s.bgi.text_justify_x=1;
	CHECK(st_big_text_stroke_direct(&f.s,(const uint8_t[]){22},1,&io)==0 && f.s.bgi.text_total_width==0 && f.s.bgi.ddo_x==35);
	for (unsigned dir=0;dir<2;++dir) for (unsigned orient=0;orient<3;++orient) for (unsigned jx=0;jx<4;++jx) for (unsigned jy=0;jy<4;++jy) {
		init(&f,2,1,dir); io=sink(&f); f.s.bgi.orientation=(uint8_t)orient;
		f.s.bgi.text_justify_x=(uint8_t)jx; f.s.bgi.text_justify_y=(uint8_t)jy;
		CHECK(st_big_text_stroke_direct(&f.s,(const uint8_t *)"AA",2,&io)==0);
		/* Hand-derived original placement extents: H uses7/4, V4/7.
		 * X-center uses SHR then DEC; Y-center only SHR. */
		unsigned h=dir?4:7,v=dir?7:4; int x=20+(dir?4:0),y=30;
		if (jx) { int adjustment=jx==1?(int)(h/2)-1:(int)h; if (orient) y+=adjustment; else x-=adjustment; }
		if (jy) { int adjustment=jy==1?(int)(v/2):(int)v; if (orient) x+=adjustment; else y+=adjustment; }
		if ((orient&1)==dir) { x+=dir?-6:6; } else y-=6;
		CHECK(f.s.bgi.ddo_x==(uint16_t)x && f.s.bgi.ddo_y==(uint16_t)y && f.calls==30); retained(&f);
	}
}
static void partials_and_lifetimes(void)
{
	struct fixture f; init(&f,2,1,0); struct st_big_draw_sink io=sink(&f); struct st_big_state before=f.s;
	CHECK(st_big_text_stroke_direct(&f.s,NULL,0,&io)==0 && memcmp(&before,&f.s,sizeof(before))==0 && f.calls==0);
	f.s.bgi.text_stroke_id=255; before=f.s;
	CHECK(st_big_text_stroke_direct(&f.s,(const uint8_t *)"A",1,&io)==0 && memcmp(&before,&f.s,sizeof(before))==0);
	init(&f,2,1,0); io=sink(&f); f.mutate=true;
	CHECK(st_big_text_stroke_direct(&f.s,(const uint8_t *)"A",1,&io)==0 && f.s.bgi.ddo_line_user==0x5678 && f.s.bgi.line_user==0x1234); retained(&f);
	init(&f,2,1,0); io=sink(&f); f.s.bgi.ddo_line_style=4; f.mutate=true;
	CHECK(st_big_text_stroke_direct(&f.s,(const uint8_t *)"A",1,&io)==0 && f.s.bgi.ddo_line_style==4 && f.s.bgi.line_mask==0x5678 && f.s.bgi.device_line_width==1);
	/* Original direct endpoints and ascending-Y thin-line order. */
	static const uint16_t trace[15][2]={{20,32},{20,33},{20,34},{21,31},{20,32},{21,31},{22,32},{22,32},{22,33},{22,34},{22,33},{22,34},{20,33},{21,33},{22,33}};
	static const uint16_t decoded[15][2]={{0,2},{0,2},{0,2},{1,3},{1,3},{2,2},{2,2},{2,0},{2,0},{2,0},{2,1},{2,1},{0,1},{0,1},{0,1}};
	for (size_t fail=1;fail<=15;++fail) {
		init(&f,2,1,0); io=sink(&f); f.fail_at=fail;
		CHECK(st_big_text_stroke_direct(&f.s,(const uint8_t *)"A",1,&io)==IO_ERROR_IO && f.calls==fail);
		CHECK(f.s.bgi.ddo_x==20 && f.s.bgi.ddo_y==34 && f.s.bgi.text_total_width==3);
		CHECK(f.s.bgi.ddo_line_style==0 && f.s.bgi.ddo_line_width==1 && f.s.bgi.device_line_width==1 && f.s.bgi.line_style==2 && f.s.bgi.line_width==3);
		CHECK(memcmp(f.s.bgi.text_stroke_point,decoded[fail-1],sizeof(decoded[0]))==0);
		uint8_t expected[64*64]={0};
		for (size_t i=0;i+1<fail;++i) expected[trace[i][1]*64+trace[i][0]]=5;
		CHECK(memcmp(f.pixels,expected,sizeof(expected))==0);
	}
	init(&f,2,1,0); io=sink(&f); f.s.bgi.effective_ratio[0]=65535; f.s.bgi.effective_ratio[1]=1;
	before=f.s; CHECK(st_big_text_stroke_direct(&f.s,(const uint8_t *)"A",1,&io)==IO_ERROR_OUT_OF_RANGE && f.calls==0);
	CHECK(f.s.bgi.ddo_x==20 && f.s.bgi.ddo_y==30 && f.s.bgi.ddo_line_style==2 && f.s.bgi.text_total_width==before.bgi.text_total_width);
	CHECK(st_big_text_stroke_direct(NULL,(const uint8_t *)"A",1,&io)==IO_ERROR_INVALID_ARGUMENT);
}
static void glyph_binding_lifetime(void)
{
	struct fixture f; init(&f,2,1,0); struct st_big_draw_sink io=sink(&f);
	uint8_t *replacement=malloc(f.s.bgi.text_body_size); CHECK(replacement!=NULL);
	memcpy(replacement,f.s.bgi.text_body,f.s.bgi.text_body_size);
	size_t offset_entry=16+2u*('A'-32);
	size_t program=304u+replacement[offset_entry]+256u*replacement[offset_entry+1];
	CHECK(replacement[program+14]==0x86 && replacement[program+15]==0);
	replacement[program+14]=0x8a; /* New terminal move10,0 instead of6,0. */
	f.next_body=replacement; f.next_size=f.s.bgi.text_body_size;
	CHECK(st_big_text_stroke_direct(&f.s,(const uint8_t *)"AA",2,&io)==0 && f.calls==30);
	/* The first glyph keeps its old bound stream; the second sees the
	 * changed binding. Advances3+6 differ from6+6 or3+3. */
	CHECK(f.s.bgi.text_total_width==7 && f.s.bgi.ddo_x==29 && f.s.bgi.ddo_y==34);
	CHECK(f.s.bgi.text_stroke_point[0]==6 && f.s.bgi.text_body==replacement);
	free(replacement);
}
static void synthetic_programs(void)
{
	/* An authored format fixture, not an image generated by the renderer:
	 * M0,-7 L4,-7 IGNORE63,63 M6,0 END3,4. Metric10 differs from advance6.
	 * The current body's header deliberately disagrees with the DDO copy. */
	uint8_t body[30]={0};
	const uint8_t program[]={0x80,0x79,0x84,0xf9,0x3f,0xbf,0x86,0,3,4};
	body[18]=10; memcpy(body+20,program,sizeof(program));
	struct fixture f; init(&f,2,1,0); struct st_big_draw_sink io=sink(&f);
	const uint8_t header[16]={'+',1,0,0,'A',20,0,0,4};
	f.s.bgi.text_body=body; f.s.bgi.text_body_size=sizeof(body); memcpy(f.s.bgi.text_header,header,16);
	CHECK(st_big_text_stroke_direct(&f.s,(const uint8_t *)"A",1,&io)==0);
	CHECK(f.s.bgi.text_total_width==6 && f.s.bgi.text_scaled[1]==2 && f.s.bgi.ddo_x==23 && f.s.bgi.ddo_y==32 && f.calls==3);
	for (unsigned y=0;y<64;++y) for (unsigned x=0;x<64;++x) CHECK(f.pixels[y*64+x]==(y==36 && x>=20 && x<=22?5:0));
	retained(&f);
	init(&f,2,1,0); io=sink(&f); f.s.bgi.text_body=body; f.s.bgi.text_body_size=22; memcpy(f.s.bgi.text_header,header,16);
	CHECK(st_big_text_stroke_direct(&f.s,(const uint8_t *)"A",1,&io)==IO_ERROR_OUT_OF_RANGE && f.calls==0);
	CHECK(f.s.bgi.ddo_y==32 && f.s.bgi.text_total_width==6 && f.s.bgi.text_stroke_point[0]==0 && f.s.bgi.text_stroke_point[1]==65532 && f.s.bgi.ddo_line_style==0);
	init(&f,2,1,0); io=sink(&f); f.s.bgi.text_body=body; f.s.bgi.text_body_size=18; memcpy(f.s.bgi.text_header,header,16);
	f.s.bgi.text_total_width=99;
	CHECK(st_big_text_stroke_direct(&f.s,(const uint8_t *)"A",1,&io)==IO_ERROR_OUT_OF_RANGE && f.calls==0);
	CHECK(f.s.bgi.ddo_y==30 && f.s.bgi.text_total_width==99 && f.s.bgi.text_scaled[0]==2 && f.s.bgi.ddo_line_style==2 && f.s.bgi.ddo_line_width==3);
	init(&f,2,1,0); io=sink(&f); const uint8_t short_body[]={7,9};
	f.s.bgi.text_body=short_body; f.s.bgi.text_body_size=2;
	f.s.bgi.text_header[1]=0xf8; f.s.bgi.text_header[2]=0x7f; f.s.bgi.text_header[4]='A';
	CHECK(st_big_text_stroke_direct(&f.s,(const uint8_t *)"A",1,&io)==IO_ERROR_OUT_OF_RANGE && f.calls==0);
	CHECK(f.s.bgi.text_total_width==4 && f.s.bgi.ddo_y==34 && f.s.bgi.ddo_line_style==0 && f.s.bgi.text_stroke_point[0]==0);
	init(&f,2,1,0); io=sink(&f);
	CHECK(st_big_text_stroke_direct(&f.s,(const uint8_t[]){'A',0,'A'},3,&io)==0 && f.calls==15 && f.s.bgi.text_total_width==7 && f.s.bgi.ddo_x==23);
	/* Divisor1 skips IDIV's overflow check and retains the low product;
	 * divisor2 executes it and fails before the point store. */
	for (unsigned denominator=1;denominator<=2;++denominator) {
		init(&f,2,1,0); io=sink(&f); body[18]=0; body[20]=0xbf; body[21]=0;
		body[22]=body[23]=0;
		f.s.bgi.text_body=body; f.s.bgi.text_body_size=sizeof(body); memcpy(f.s.bgi.text_header,header,16); f.s.bgi.text_header[8]=0;
		f.s.bgi.effective_ratio[0]=3000; f.s.bgi.effective_ratio[1]=(uint16_t)denominator;
		int status=st_big_text_stroke_direct(&f.s,(const uint8_t *)"A",1,&io);
		CHECK(status==(denominator==1?0:IO_ERROR_OUT_OF_RANGE) && f.calls==0);
		if (denominator==1) CHECK(f.s.bgi.ddo_x==57948 && f.s.bgi.text_stroke_point[0]==57928);
		else CHECK(f.s.bgi.ddo_x==20 && f.s.bgi.text_stroke_point[0]==0 && f.s.bgi.ddo_line_style==0);
	}
}
int main(void) { small_AA(); format_access_order(); missing_zero_and_layout(); partials_and_lifetimes(); synthetic_programs(); glyph_binding_lifetime(); return 0; }
