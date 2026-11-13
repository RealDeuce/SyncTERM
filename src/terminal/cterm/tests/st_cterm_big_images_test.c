/* Independent native BIG image routes/prefixes/lifetimes, references25/56. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm.h"
#include "cterm_big_internal.h"
#include "st_cterm.h"
#include "io_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
struct fixture { st_console_t *console; st_font_catalog_t *fonts; st_cterm_t *terminal; uint8_t output[64]; size_t size; };
struct dot { unsigned x,y; uint8_t a,b; };
static uint64_t local_ns;
static int now(void *context, uint64_t *value) { (void)context; *value=local_ns; return 0; }
static void output(void *context, enum st_cterm_output_origin origin, const uint8_t *bytes, size_t size)
{
	struct fixture *f=context; (void)origin; CHECK(size<=sizeof(f->output)-f->size);
	memcpy(f->output+f->size,bytes,size); f->size+=size;
}
static void create(struct fixture *f, bool utf8, bool status)
{
	memset(f,0,sizeof(*f)); local_ns=1000;
	const struct st_console_display_config display={.backend=ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry={.columns=80,.rows=25,.cell_width=8,.cell_height=14};
	CHECK(st_console_create(&f->console,&display,&geometry)==0 && st_font_catalog_create(&f->fonts,f->console)==0);
	const struct st_cterm_config config={.console=f->console,.fonts=f->fonts,.emulation=ST_CTERM_ANSI_BBS,
	    .graphics_mode=ST_CTERM_GRAPHICS_BIG_103,.encoding=utf8?ST_CTERM_ENCODING_UTF8:ST_CTERM_ENCODING_CODEPAGE,
	    .codepage=ST_CODEPAGE_CP437,.quiet=true,.status_line=status,.scrollback_lines=2,.output=output,.output_context=f};
	CHECK(st_cterm_create(&f->terminal,&config)==0);
	f->terminal->big_engine->numeric.io.now_ns=now;
}
static void destroy(struct fixture *f) { st_cterm_destroy(&f->terminal); st_font_catalog_destroy(f->fonts); st_console_destroy(f->console); }
static void feed(struct fixture *f, const void *bytes, size_t size)
{
	size_t used; CHECK(st_cterm_write(f->terminal,bytes,size,&used)==0 && used==size);
}
static void string(struct fixture *f, const char *wire) { feed(f,wire,strlen(wire)); }
static void resolution(struct fixture *f, unsigned value)
{
	char wire[16]; snprintf(wire,sizeof(wire),"I#R %u:",value); string(f,wire); f->size=0;
}
struct seed { const struct dot *dots; size_t count; };
static int seed(void *context, const struct st_pixel_buffer *p, struct st_console_rect *changed)
{
	const struct seed *s=context;
	for (size_t i=0;i<s->count;++i) {
		CHECK(s->dots[i].x<p->width && s->dots[i].y<p->height);
		size_t at=(size_t)s->dots[i].y*p->width+s->dots[i].x;
		p->primary[at]=800u+s->dots[i].a; p->alternate[at]=800u+s->dots[i].b;
	}
	*changed=(struct st_console_rect){0,0,p->width,p->height}; return 0;
}
static void paint(struct fixture *f, const struct dot *dots, size_t count)
{
	struct seed s={dots,count}; CHECK(st_console_modify_pixels(f->console,seed,&s)==0);
}
static void canvas(struct fixture *f, const struct dot *dots, size_t count)
{
	uint32_t width,height; CHECK(st_console_pixel_size(f->console,&width,&height)==0);
	const struct st_console_rect area={0,0,width,height}; struct st_pixel_buffer *p=NULL;
	CHECK(st_console_get_pixels(&p,f->console,&area,false)==0);
	for (size_t i=0;i<(size_t)p->width*p->height;++i) {
		unsigned a=0,b=0;
		for (size_t d=0;d<count;++d) if (i==(size_t)dots[d].y*p->width+dots[d].x) { a=dots[d].a; b=dots[d].b; }
		if (p->primary[i]!=800u+a || p->alternate[i]!=800u+b) {
			fprintf(stderr,"pixel%zu,%zu actual%u/%u expected%u/%u\n",i%p->width,i/p->width,p->primary[i],p->alternate[i],800u+a,800u+b); CHECK(false);
		}
	}
	st_pixel_buffer_destroy(p);
}
static void partitions_and_modes(void)
{
	static const uint8_t a[]={5,3,7,4,10}, b[]={5,12,13,1,10};
	for (unsigned utf8=0;utf8<2;++utf8) for (unsigned status=0;status<2;++status) {
		struct fixture f; create(&f,utf8,status); struct cterm_big *big=f.terminal->big_engine;
		for (unsigned res=1;res<=3;++res) for (unsigned mode=0;mode<5;++mode) {
			char wire[128]; int size=snprintf(wire,sizeof(wire),"I#C>0,5:P>20,20:G>0,%u,20,20,20,20,40,40:G 2,%u,60,60:\033[6n",mode,mode);
			for (size_t split=0;split<=(size_t)size+1;++split) {
				/* All partitions on default mode; bytewise other profiles. */
				if (res!=2 && split!=(size_t)size+1) continue;
				resolution(&f,res); const struct dot start[]={{40,40,6,9},{60,60,6,9}}; paint(&f,start,2);
				size_t calls=big->image_page_calls;
				if (split==(size_t)size+1) for (int i=0;i<size;++i) feed(&f,wire+i,1);
				else { feed(&f,wire,split); feed(&f,wire+split,(size_t)size-split); }
				CHECK(big->image_size==10 && big->image_mode==mode && big->image_page_calls==calls+2);
				CHECK(big->last_error==0 && big->video_backing!=NULL && big->video_active_page==0);
				CHECK(big->state.bgi.x==20 && big->state.bgi.y==20 && big->state.bgi.ddo_x==20 && big->state.bgi.device_x==20);
				CHECK(big->state.memory_page==(res==3?0:1) && f.size==6 && memcmp(f.output,"\033[1;1R",6)==0);
				CHECK(memcmp(big->image_bytes,"\0\0\0\0\0\200\0\200",8)==0);
				const struct dot expected[]={{20,20,5,5},{40,40,a[mode],b[mode]},{60,60,a[mode],b[mode]}};
				canvas(&f,expected,3); CHECK(st_cterm_poll(f.terminal)==0 && st_cterm_finish(f.terminal)==0);
			}
		}
		destroy(&f);
	}
}
static void page_routes(void)
{
	const struct dot source[]={{20,20,1,9},{21,20,2,10},{22,20,4,12},{23,20,8,0}};
	for (unsigned res=1;res<=3;++res) {
		struct fixture f; create(&f,false,false); struct cterm_big *big=f.terminal->big_engine; resolution(&f,res);
		paint(&f,source,4); size_t calls=big->image_page_calls;
		string(&f,"I#G>1,0,23,20,20,20:G 5,1:");
		CHECK(big->image_page_calls==calls+(res==3?1u:3u) && big->video_active_page==0 && big->last_error==0);
		CHECK(memcmp(big->image_bytes,"\3\0\0\0\20\40\100\200",8)==0);
		struct dot expected[]={{23,20,1,1},{24,20,2,2},{25,20,4,4},{26,20,8,8}};
		canvas(&f,res==3?source:expected,4);
		resolution(&f,res); string(&f,"I#G 5,1:"); paint(&f,source,4); string(&f,"I#G 5,0:");
		calls=big->image_page_calls; string(&f,"I#G 3,0,20,20,23,20,40,40:");
		CHECK(big->image_page_calls==calls+3 && big->video_active_page==0 && big->last_error==0);
		struct dot restored[8]; size_t count=0;
		if (res==3) { memcpy(restored,source,sizeof(source)); count=4; }
		for (unsigned i=0;i<4;++i) restored[count++]=(struct dot){40+i,40,(uint8_t)(1u<<i),(uint8_t)(1u<<i)};
		canvas(&f,restored,count);
		/* G0 captures before overlapping paste, instead of rereading the
		 * progressively overwritten source. */
		resolution(&f,res); paint(&f,source,4); string(&f,"I#G 0,0,20,20,23,20,22,20:");
		const struct dot overlap[]={{20,20,1,9},{21,20,2,10},{22,20,1,1},{23,20,2,2},{24,20,4,4},{25,20,8,8}};
		canvas(&f,overlap,6); destroy(&f);
	}
	/* R1 exposes all four pages: compose remembered3, active2 and a
	 * separate visual page through one chained interpreter frame. */
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine; resolution(&f,1);
	string(&f,"I#G 5,2:"); paint(&f,source,4); string(&f,"I#G 5,0:");
	string(&f,"I#G>4,3:G>6,2:G>1,0,20,20,23,20:G 5,3:");
	CHECK(b->state.memory_page==3 && b->state.active_page==2 && b->video_active_page==2 && b->video_visual_page==3);
	const struct dot memory[]={{20,20,1,1},{21,20,2,2},{22,20,4,4},{23,20,8,8}}; canvas(&f,memory,4);
	string(&f,"I#G>4,3:G>6,2:G>3,0,20,20,23,20,40,40:G 5,2:");
	CHECK(b->state.memory_page==3 && b->state.active_page==2 && b->video_active_page==2 && b->video_visual_page==2 && b->last_error==0);
	struct dot active_page[8]; memcpy(active_page,source,sizeof(source));
	for (unsigned i=0;i<4;++i) active_page[4+i]=(struct dot){40+i,40,(uint8_t)(1u<<i),(uint8_t)(1u<<i)};
	canvas(&f,active_page,8); destroy(&f);
}
static void prefixes_gates_and_errors(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#G 6,1:"); size_t calls=b->image_page_calls;
	string(&f,"I#G 0,"); CHECK(b->video_active_page==0 && b->image_page_calls==calls+1);
	string(&f,"9,"); CHECK(b->image_mode==0); CHECK(st_cterm_finish(f.terminal)==0);
	string(&f,"I#G 2,9,"); CHECK(b->image_mode==9);
	string(&f,"40,"); CHECK(b->image_mode==9); string(&f,"40:"); CHECK(b->image_mode==0 && b->last_error==0);
	for (unsigned op=0;op<4;++op) {
		resolution(&f,2); b->image_gates[op][0]=1; b->image_gates[op][1]=0; b->image_size=777;
		memset(b->image_bytes,0,sizeof(b->image_bytes)); CHECK(st_big_image_import(&b->image)==0);
		calls=b->image_page_calls;
		static const char *const wire[]={"I#G 0,9,20,20,20,20,40,40:","I#G 1,9,20,20,20,20:","I#G 2,9,40,40:","I#G 3,9,20,20,20,20,40,40:"};
		string(&f,wire[op]); CHECK(b->image_page_calls==calls+(op==3?2u:1u));
		CHECK(b->image_mode==0 && b->image_size==(op==2?777:10) && b->image_bytes[4]==0 && b->video_active_page==0);
		b->image_gates[op][0]=b->image_gates[op][1]=65535;
	}
	/* A false gate cannot suppress the earlier query/dispatch failure. */
	b->image_gates[0][0]=1; b->image_gates[0][1]=0;
	string(&f,"I#G 0,0,"); b->state.bgi.state=0; b->image_size=777;
	string(&f,"20,20,20,20,40,40:"); CHECK(b->last_error==IO_ERROR_PROTOCOL && b->image_size==777 && b->state.bgi.error==-1);
	b->state.bgi.state=3; b->image_gates[0][0]=0;
	resolution(&f,2); b->image_gates[3][0]=1; b->image_gates[3][1]=0;
	string(&f,"I#G 3,0,"); calls=b->image_page_calls; b->state.bgi.state=0;
	string(&f,"20,20,20,20,40,40:"); CHECK(b->image_page_calls==calls+1 && b->video_active_page==0 && b->last_error==IO_ERROR_PROTOCOL);
	b->state.bgi.state=3;
	/* Unknown operation has no guessed arguments/page call, but cache3
	 * still clears all interpreter page locals on operation return. */
	b->numeric.context.resolution=3; calls=b->image_page_calls;
	string(&f,"I#G 257:"); CHECK(b->image_operation==257 && b->image_page_calls==calls && b->state.memory_page==0);
	CHECK(st_cterm_poll(f.terminal)==0 && st_cterm_finish(f.terminal)==0); destroy(&f);
}
static void lifetime_scaling_and_sources(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	const struct dot source[]={{20,20,5,10}}; paint(&f,source,1); string(&f,"I#G 0,0,20,20,20,20,40,40:");
	b->image_bytes[ST_BIG_IMAGE_CAPACITY-1]=0xa5;
	string(&f,"I#I 0:"); CHECK(b->image_bytes[5]==0x80 && b->image_bytes[ST_BIG_IMAGE_CAPACITY-1]==0xa5);
	resolution(&f,1); string(&f,"I#I 1:I#G 2,0,60,60:"); const struct dot copied[]={{60,60,5,5}}; canvas(&f,copied,1);
	string(&f,"I#G 3,4,1,"); CHECK(st_cterm_finish(f.terminal)==0 && b->image_bytes[5]==0x80);
	CHECK(st_cterm_reset_terminal(f.terminal)==0 && b->image_bytes[5]==0x80);
	resolution(&f,3); const struct dot scaled[]={{20,11,5,10}}; paint(&f,scaled,1);
	string(&f,"I#g 3:I#G 0,0,20,5,20,5,40,5:");
	const struct dot two[]={{20,11,5,10},{40,11,5,5}}; canvas(&f,two,2);
	string(&f,"I#& 0,2,1,0,G@4,2,0,x,5:");
	const struct dot loop[]={{20,11,5,10},{40,11,5,5},{0,5,5,5},{1,5,5,5},{2,5,5,5}}; canvas(&f,loop,5);
	CHECK(f.size==2 && memcmp(f.output,"\x13\x11",2)==0 && b->numeric.loop.active==0);
	destroy(&f); create(&f,false,false); b=f.terminal->big_engine;
	const struct dot fresh[]={{60,60,6,9}}; paint(&f,fresh,1); string(&f,"I#G 2,0,60,60:"); canvas(&f,NULL,0);
	CHECK(b->image_bytes[ST_BIG_IMAGE_CAPACITY-1]==0 && b->image_size==0); destroy(&f);
}
static uint8_t operation(uint8_t source, uint8_t destination, unsigned mode)
{
	static const unsigned truth[]={0xa,0x6,0xe,0x8,0x5}; uint8_t result=0;
	for (unsigned bit=0;bit<4;++bit) {
		unsigned select=((destination>>bit)&1u)*2+((source>>bit)&1u);
		if ((truth[mode]&(1u<<select))!=0) result|=(uint8_t)(1u<<bit);
	}
	return result;
}
static void unaligned_blocks(void)
{
	static const uint8_t pens[]={1,2,4,8,15,0,7,9,2};
	struct fixture f; create(&f,true,true); struct cterm_big *b=f.terminal->big_engine;
	for (unsigned res=1;res<=3;++res) for (unsigned mode=0;mode<5;++mode) {
		resolution(&f,res); struct dot dots[36]; size_t count=0;
		for (unsigned row=0;row<2;++row) for (unsigned x=0;x<9;++x) {
			uint8_t pen=(uint8_t)(pens[x]^(row?15:0));
			dots[count++]=(struct dot){21+x,20+row,pen,(uint8_t)(pen^8)};
			dots[count++]=(struct dot){45+x,50+row,6,9};
		}
		paint(&f,dots,count); char wire[64]; snprintf(wire,sizeof(wire),"I#G 0,%u,21,20,29,21,45,50:",mode); string(&f,wire);
		for (size_t i=1;i<count;i+=2) {
			uint8_t pen=dots[i-1].a; dots[i].a=operation(pen,6,mode); dots[i].b=operation(pen,9,mode);
		}
		canvas(&f,dots,count); CHECK(b->last_error==0 && b->image_size==22 && b->image_bytes[0]==8 && b->image_bytes[2]==1);
	}
	destroy(&f);
}
static void failure_prefixes(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	const struct dot source[]={{20,20,5,10}};
	paint(&f,source,1); string(&f,"I#G 6,1:");
	size_t hidden=0x8000u*8+20*640+20;
	b->video_backing->alternate[hidden]=ST_PIXEL_COLOR_RGB_TAG|0x123456;
	size_t calls=b->image_page_calls, errors=b->rejected_commands;
	string(&f,"I#G 1,0,20,20,20,20:");
	CHECK(b->last_error==IO_ERROR_OUT_OF_RANGE && b->rejected_commands==errors+1 && b->image_page_calls==calls+2);
	CHECK(b->video_active_page==1 && b->image_bytes[5]==0x80); canvas(&f,source,1);
	/* Failed G3 paste keeps active0, with no final duplicate selection.
	 * Its source is visual1; poison hidden0 without touching that view. */
	resolution(&f,2); string(&f,"I#G 5,1:"); paint(&f,source,1);
	b->video_backing->alternate[40*640+40]=ST_PIXEL_COLOR_RGB_TAG|0xabcdef;
	calls=b->image_page_calls; errors=b->rejected_commands;
	string(&f,"I#G 3,0,20,20,20,20,40,40:I#C>0,6:P 41,41:\033[6n");
	CHECK(b->image_page_calls==calls+2 && b->video_active_page==0 && b->video_visual_page==1 && b->rejected_commands==errors+1);
	CHECK(b->last_error==IO_ERROR_OUT_OF_RANGE && b->state.bgi.x==41 && b->state.bgi.y==41);
	CHECK(b->video_backing->primary[41*640+41]==806 && f.size==6 && memcmp(f.output,"\033[1;1R",6)==0);
	canvas(&f,source,1); CHECK(st_cterm_poll(f.terminal)==0 && st_cterm_finish(f.terminal)==0);
	/* A native byte callback can fail partway through selected pixels.
	 * Keep the first three plane3 writes and both blink destinations. */
	resolution(&f,2); string(&f,"I#G 5,1:"); struct dot row[8];
	for (unsigned i=0;i<8;++i) {
		row[i]=(struct dot){20+i,20,5,10};
		b->video_backing->primary[40*640+40+i]=806;
		b->video_backing->alternate[40*640+40+i]=809;
	}
	paint(&f,row,8); b->video_backing->alternate[40*640+43]=ST_PIXEL_COLOR_RGB_TAG|0xabcdef;
	calls=b->image_page_calls; string(&f,"I#G 3,0,20,20,27,20,40,40:");
	CHECK(b->image_page_calls==calls+2 && b->last_error==IO_ERROR_OUT_OF_RANGE);
	for (unsigned i=0;i<8;++i) {
		CHECK(b->video_backing->primary[40*640+40+i]==806);
		uint32_t expected=i<3?801:i==3?(ST_PIXEL_COLOR_RGB_TAG|0xabcdef):809;
		CHECK(b->video_backing->alternate[40*640+40+i]==expected);
	}
	canvas(&f,row,8);
	/* The ineffective size guard reaches capture, accepts its real buffer
	 * prefix, then rejects cursor0 without attempting paste or a footer. */
	resolution(&f,2); calls=b->image_page_calls; errors=b->rejected_commands;
	b->image_bytes[ST_BIG_IMAGE_CAPACITY-1]=0xa5;
	string(&f,"I#G 0,0,0,0,639,349,0,0:\033[6n");
	CHECK(b->image_size==0 && b->image_page_calls==calls+1 && b->rejected_commands==errors+1);
	CHECK(b->last_error==IO_ERROR_OUT_OF_RANGE && b->image_bytes[0]==127 && b->image_bytes[1]==2 && b->image_bytes[2]==93 && b->image_bytes[3]==1);
	CHECK(b->image_bytes[ST_BIG_IMAGE_CAPACITY-1]==0xa5 && f.size==6 && memcmp(f.output,"\033[1;1R",6)==0); canvas(&f,NULL,0);
	/* The failed large capture's first200 rows are still usable after R1.
	 * G2 crops to them and restores old height349, without a validity flag. */
	resolution(&f,1); errors=b->rejected_commands; calls=b->image_page_calls;
	string(&f,"I#G 2,0,0,0:\033[6n");
	CHECK(b->rejected_commands==errors && b->image_page_calls==calls+1 && b->image_size==0);
	CHECK(b->image_bytes[2]==93 && b->image_bytes[3]==1 && f.size==6 && memcmp(f.output,"\033[1;1R",6)==0); canvas(&f,NULL,0);
	/* A corrupt frame page is rejected by the actual CPU aperture access,
	 * after memory selection/query/header, with no active restore. */
	resolution(&f,2); string(&f,"I#G 3,0,"); b->state.memory_page=3; calls=b->image_page_calls;
	string(&f,"20,20,20,20,40,40:");
	CHECK(b->video_active_page==3 && b->image_page_calls==calls+1 && b->image_size==10 && b->last_error==IO_ERROR_OUT_OF_RANGE);
	CHECK(b->image_bytes[0]==0 && b->image_bytes[2]==0 && st_cterm_finish(f.terminal)==0);
	destroy(&f);
}
static void status_source(void)
{
	struct fixture f; create(&f,false,true); struct cterm_big *b=f.terminal->big_engine;
	for (unsigned res=1;res<=3;++res) {
		resolution(&f,res); const struct st_big_mode *m=st_big_mode(res); unsigned y=m->height-m->cell_height;
		CHECK(cterm_big_video_prepare(b,true)==0);
		struct dot dots[24]; size_t count=0;
		for (unsigned x=0;x<8;++x) {
			dots[count++]=(struct dot){x,y,100,101};
			dots[count++]=(struct dot){80+x,80,6,9}; dots[count++]=(struct dot){88+x,80,6,9};
			b->video_backing->primary[y*640+x]=805; b->video_backing->alternate[y*640+x]=810;
		}
		paint(&f,dots,count); char wire[96]; snprintf(wire,sizeof(wire),"I#G>0,0,0,%u,7,%u,80,80:G 2,0,88,80:",y,y); string(&f,wire);
		for (size_t i=0;i<count;++i) if (dots[i].y==80) dots[i].a=dots[i].b=5;
		CHECK(b->last_error==0 && b->image_size==10 && b->image_bytes[0]==7); canvas(&f,dots,count);
	}
	destroy(&f);
}
int main(void)
{
	partitions_and_modes(); page_routes(); prefixes_gates_and_errors(); lifetime_scaling_and_sources();
	unaligned_blocks(); failure_prefixes(); status_source(); return 0;
}
