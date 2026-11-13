/* Independent VGA-profile indexed address/window expectations, BIG25/48/56. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_video.h"
#include "io_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
struct fixture {
	struct st_big_video video;
	struct st_big_video_view view;
	uint32_t *primary,*alternate,*shown,*blink;
};
static uint32_t primary(size_t i) { return UINT32_C(0x10000000)+(uint32_t)i; }
static uint32_t alternate(size_t i) { return UINT32_C(0x80000000)^(uint32_t)(7*i); }
static void init(struct fixture *f)
{
	memset(f,0,sizeof(*f));
	f->primary=malloc(ST_BIG_VIDEO_PENS*sizeof(*f->primary));
	f->alternate=malloc(ST_BIG_VIDEO_PENS*sizeof(*f->alternate));
	f->shown=malloc(ST_BIG_VIDEO_PENS*sizeof(*f->shown));
	f->blink=malloc(ST_BIG_VIDEO_PENS*sizeof(*f->blink));
	CHECK(f->primary!=NULL && f->alternate!=NULL && f->shown!=NULL && f->blink!=NULL);
	for (size_t i=0;i<ST_BIG_VIDEO_PENS;++i) {
		f->primary[i]=primary(i); f->alternate[i]=alternate(i);
		f->shown[i]=UINT32_C(0x40000000)+(uint32_t)i; f->blink[i]=UINT32_C(0x20000000)+(uint32_t)i;
	}
	CHECK(st_big_video_bind(&f->video,f->primary,f->alternate,ST_BIG_VIDEO_PENS)==0);
	f->view=(struct st_big_video_view){f->shown,f->blink,0,0};
}
static void destroy(struct fixture *f)
{
	free(f->primary); free(f->alternate); free(f->shown); free(f->blink);
}
static void physical_addresses(void)
{
	struct fixture f; init(&f); struct st_big_video_pixel p;
	static const struct { uint16_t segment,offset; uint8_t bit; size_t index; } cases[]={
		{0xa000,0,0,0},{0xa000,0,7,7},{0xa000,65535,7,524287},
		{0x9fff,16,0,0},{0xa001,0,0,128},{0xa400,0,0,131072},
		{0xa800,0,0,262144},{0xa960,0,0,307200},{0xabff,16,0,393216},
		{0xac00,0,0,393216},{0xa400,49151,0,524280},{0xafff,15,7,524287},
	};
	for (size_t i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
		CHECK(st_big_video_read(&f.video,NULL,cases[i].segment,cases[i].offset,cases[i].bit,&p)==0);
		CHECK(p.primary==primary(cases[i].index) && p.alternate==alternate(cases[i].index) && p.visible==SIZE_MAX);
	}
	static const uint16_t invalid[][2]={{0,0},{0x9fff,15},{0xa400,49152},{0xafff,16},{0xb000,0},{0xb800,0},{0xffff,65535}};
	for (size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i) {
		p=(struct st_big_video_pixel){1,2,3}; size_t visible=4;
		CHECK(st_big_video_read(&f.video,NULL,invalid[i][0],invalid[i][1],0,&p)==IO_ERROR_OUT_OF_RANGE);
		CHECK(p.primary==1 && p.alternate==2 && p.visible==3);
		CHECK(st_big_video_write(&f.video,NULL,invalid[i][0],invalid[i][1],0,5,6,&visible)==IO_ERROR_OUT_OF_RANGE && visible==4);
	}
	/* Sequential CPU offsets reach every stored pen, with no RGB reduction. */
	for (size_t i=0;i<ST_BIG_VIDEO_PENS;++i) {
		CHECK(st_big_video_read(&f.video,NULL,0xa000,(uint16_t)(i/8),(uint8_t)(i%8),&p)==0);
		CHECK(p.primary==primary(i) && p.alternate==alternate(i));
	}
	/* WORD cursor wrap is explicit at the caller; it returns to byte0. */
	uint16_t offset=65535; ++offset;
	CHECK(st_big_video_read(&f.video,NULL,0xa000,offset,0,&p)==0 && p.primary==primary(0));
	destroy(&f);
}
static void page_arithmetic(void)
{
	/* Literal reference48 active/visual results, including signed BIOS AL. */
	CHECK(st_big_video_active_segment(3,0x400)==0xac00);
	CHECK(st_big_video_active_segment(3,0x800)==0xb800);
	CHECK(st_big_video_active_segment(1,0x960)==0xa960);
	CHECK(st_big_video_display_start(1,0xa000)==0xa000);
	CHECK(st_big_video_display_start(2,0x8000)==0);
	CHECK(st_big_video_display_start(128,3)==0xfe80);
	CHECK(st_big_video_display_start(255,3)==0xfffd);
	for (unsigned page=0;page<256;++page) {
		/* Repeated modular additions form a separate finite oracle. */
		uint16_t active=0xa000,visual=0;
		for (unsigned prior=0;prior<page;++prior) active=(uint16_t)(active+0x960u);
		int signed_page=page<128?(int)page:(int)page-256;
		for (int step=0;step<(signed_page<0?-signed_page:signed_page);++step)
			visual=(uint16_t)(signed_page<0?visual-3u:visual+3u);
		CHECK(st_big_video_active_segment((uint8_t)page,0x960)==active);
		CHECK(st_big_video_display_start((uint8_t)page,3)==visual);
	}
	/* All WORD pitches with representative signed/unsigned page bytes. */
	static const uint8_t pages[]={0,1,2,3,127,128,254,255};
	for (uint32_t pitch=0;pitch<65536;++pitch) for (size_t p=0;p<sizeof(pages);++p) {
		uint16_t a=0xa000,b=0; int page=pages[p]<128?pages[p]:(int)pages[p]-256;
		for (unsigned i=0;i<pages[p];++i) a=(uint16_t)(a+pitch);
		for (int i=0;i<(page<0?-page:page);++i) b=(uint16_t)(page<0?b-pitch:b+pitch);
		CHECK(st_big_video_active_segment(pages[p],(uint16_t)pitch)==a);
		CHECK(st_big_video_display_start(pages[p],(uint16_t)pitch)==b);
	}
}
static void visible_authority(void)
{
	struct fixture f; init(&f); struct st_big_video_pixel p;
	f.view.start=65535; f.view.count=16;
	/* View wraps at display byteffff; CPU segment addressing does not. */
	CHECK(st_big_video_read(&f.video,&f.view,0xa000,65535,0,&p)==0 && p.visible==0 && p.primary==f.shown[0]);
	CHECK(st_big_video_read(&f.video,&f.view,0xa000,0,7,&p)==0 && p.visible==15 && p.alternate==f.blink[15]);
	CHECK(st_big_video_read(&f.video,&f.view,0xa000,1,0,&p)==0 && p.visible==SIZE_MAX && p.primary==primary(8));
	size_t visible;
	CHECK(st_big_video_write(&f.video,&f.view,0xa000,0,0,42,43,&visible)==0 && visible==8);
	CHECK(f.shown[8]==42 && f.blink[8]==43 && f.primary[0]==primary(0) && f.alternate[0]==alternate(0));
	CHECK(st_big_video_write(&f.video,&f.view,0xa000,1,0,44,45,&visible)==0 && visible==SIZE_MAX);
	CHECK(f.primary[8]==44 && f.alternate[8]==45);
	/* A visible-only owner needs no redundant indexed framebuffer copy. */
	CHECK(st_big_video_bind(&f.video,NULL,NULL,0)==0);
	CHECK(st_big_video_read(&f.video,&f.view,0xa000,0,0,&p)==0 && p.primary==42 && p.alternate==43);
	p=(struct st_big_video_pixel){1,2,3};
	CHECK(st_big_video_read(&f.video,&f.view,0xa000,1,0,&p)==IO_ERROR_OUT_OF_RANGE && p.primary==1);
	destroy(&f);
}
static void switching_and_padding(void)
{
	static const size_t visible[]={128000,224000,307200}, padded[]={131072,262144,307200};
	static const uint16_t starts[]={0x4000,0x8000,0xa000};
	for (unsigned mode=0;mode<3;++mode) {
		struct fixture f; init(&f); size_t count; f.view.count=visible[mode];
		CHECK(st_big_video_save(&f.video,&f.view,&count)==0 && count==visible[mode]);
		for (size_t i=0;i<ST_BIG_VIDEO_PENS;++i) {
			CHECK(f.primary[i]==(i<visible[mode]?f.shown[i]:primary(i)));
			CHECK(f.alternate[i]==(i<visible[mode]?f.blink[i]:alternate(i)));
		}
		/* Padding remains separate indexed storage, not another visible row. */
		for (size_t i=visible[mode];i<padded[mode];++i) CHECK(f.primary[i]==primary(i));
		f.view.start=starts[mode];
		CHECK(st_big_video_load(&f.video,&f.view,&count)==0 && count==visible[mode]);
		for (size_t i=0,at=(size_t)starts[mode]*8;i<visible[mode];++i) {
			CHECK(f.shown[i]==f.primary[at] && f.blink[i]==f.alternate[at]);
			if (++at==ST_BIG_VIDEO_PENS) at=0;
		}
		/* Save a modification on page1, return to page0, then reveal it. */
		f.shown[0]=17; f.blink[0]=18;
		CHECK(st_big_video_save(&f.video,&f.view,&count)==0);
		f.view.start=0; CHECK(st_big_video_load(&f.video,&f.view,&count)==0);
		CHECK(f.shown[0]==UINT32_C(0x40000000) && f.blink[0]==UINT32_C(0x20000000));
		f.view.start=starts[mode]; CHECK(st_big_video_load(&f.video,&f.view,&count)==0);
		CHECK(f.shown[0]==17 && f.blink[0]==18);
		destroy(&f);
	}
}
static void every_view_address(void)
{
	struct fixture f; init(&f); struct st_big_video_pixel p;
	size_t *where=malloc(ST_BIG_VIDEO_PENS*sizeof(*where)); CHECK(where!=NULL);
	static const uint16_t starts[]={0,0x4000,0x8000,0xa000,65535};
	static const size_t counts[]={0,1,8,16,128000,224000,307200,ST_BIG_VIDEO_PENS};
	for (size_t s=0;s<sizeof(starts)/sizeof(starts[0]);++s) for (size_t c=0;c<sizeof(counts)/sizeof(counts[0]);++c) {
		f.view.start=starts[s]; f.view.count=counts[c];
		for (size_t i=0;i<ST_BIG_VIDEO_PENS;++i) where[i]=SIZE_MAX;
		/* Build the independent inverse by walking a finite display ring. */
		for (size_t i=0,at=(size_t)starts[s]*8;i<counts[c];++i) {
			where[at]=i; if (at==ST_BIG_VIDEO_PENS-1) at=0; else ++at;
		}
		for (size_t i=0;i<ST_BIG_VIDEO_PENS;++i) {
			CHECK(st_big_video_read(&f.video,&f.view,0xa000,(uint16_t)(i/8),(uint8_t)(i%8),&p)==0 && p.visible==where[i]);
			CHECK(p.primary==(where[i]==SIZE_MAX?primary(i):f.shown[where[i]]));
			CHECK(p.alternate==(where[i]==SIZE_MAX?alternate(i):f.blink[where[i]]));
		}
	}
	free(where); destroy(&f);
}
static void prefixes_and_guards(void)
{
	uint32_t a[16],b[16],shown[16],blink[16];
	for (unsigned i=0;i<16;++i) { a[i]=10+i; b[i]=30+i; shown[i]=50+i; blink[i]=70+i; }
	struct st_big_video v; CHECK(st_big_video_bind(&v,a,b,12)==0);
	struct st_big_video_view view={shown,blink,8,1}; size_t count=99;
	CHECK(st_big_video_save(&v,&view,&count)==IO_ERROR_OUT_OF_RANGE && count==4);
	for (unsigned i=0;i<16;++i) CHECK(a[i]==(i>=8 && i<12?50+i-8:10+i) && b[i]==(i>=8 && i<12?70+i-8:30+i));
	for (unsigned i=0;i<16;++i) { shown[i]=90+i; blink[i]=110+i; }
	CHECK(st_big_video_load(&v,&view,&count)==IO_ERROR_OUT_OF_RANGE && count==4);
	for (unsigned i=0;i<16;++i) CHECK(shown[i]==(i<4?50+i:90+i) && blink[i]==(i<4?70+i:110+i));
	CHECK(view.start==1 && view.count==8);
	struct st_big_video saved=v; struct st_big_video_pixel pixel={1,2,3};
	CHECK(st_big_video_bind(&v,a,b,ST_BIG_VIDEO_PENS+1)==IO_ERROR_OUT_OF_RANGE && v.count==saved.count && v.primary==saved.primary);
	CHECK(st_big_video_bind(&v,NULL,b,12)==IO_ERROR_INVALID_ARGUMENT && v.primary==saved.primary);
	CHECK(st_big_video_bind(NULL,a,b,12)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_video_read(&v,&view,0xa000,1,8,&pixel)==IO_ERROR_OUT_OF_RANGE && pixel.primary==1);
	CHECK(st_big_video_read(NULL,&view,0xa000,0,0,&pixel)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_video_read(&v,&view,0xa000,0,0,NULL)==IO_ERROR_INVALID_ARGUMENT);
	view.count=ST_BIG_VIDEO_PENS+1;
	CHECK(st_big_video_load(&v,&view,&count)==IO_ERROR_INVALID_ARGUMENT && count==0);
	view.count=1; view.primary=NULL;
	CHECK(st_big_video_save(&v,&view,&count)==IO_ERROR_INVALID_ARGUMENT && count==0);
	view.count=0;
	CHECK(st_big_video_load(&v,&view,&count)==0 && count==0);
	CHECK(st_big_video_save(&v,NULL,&count)==IO_ERROR_INVALID_ARGUMENT && count==0);
	CHECK(st_big_video_load(&v,&view,NULL)==IO_ERROR_INVALID_ARGUMENT);
}
int main(void)
{
	physical_addresses(); page_arithmetic(); visible_authority(); switching_and_padding(); every_view_address(); prefixes_and_guards(); return 0;
}
