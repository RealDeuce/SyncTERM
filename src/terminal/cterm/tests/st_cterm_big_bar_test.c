/* Independent B/Z wire/area/point expectations, original BIG23/59. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm.h"
#include "cterm_big_internal.h"
#include "st_cterm.h"
#include "io_error.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
struct fixture { st_console_t *console; st_font_catalog_t *fonts; st_cterm_t *terminal;
	unsigned bells; bool reset_bell; uint8_t output[64]; size_t size; };
static uint64_t local_ns;
static int now(void *context, uint64_t *value) { (void)context; *value=local_ns; return 0; }
static void bell(void *context)
{
	struct cterm_big *b=context; struct fixture *f=b->terminal->api_output_context;
	++f->bells; if (f->reset_bell) CHECK(st_cterm_reset_terminal(f->terminal)==0);
}
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
	CHECK(st_console_create(&f->console,&display,&geometry)==0);
	CHECK(st_font_catalog_create(&f->fonts,f->console)==0);
	const struct st_cterm_config config={.console=f->console,.fonts=f->fonts,
	    .emulation=ST_CTERM_ANSI_BBS,.graphics_mode=ST_CTERM_GRAPHICS_BIG_103,
	    .encoding=utf8?ST_CTERM_ENCODING_UTF8:ST_CTERM_ENCODING_CODEPAGE,.codepage=ST_CODEPAGE_CP437,
	    .quiet=true,.status_line=status,.scrollback_lines=2,.output=output,.output_context=f};
	CHECK(st_cterm_create(&f->terminal,&config)==0);
	f->terminal->big_engine->numeric.io.now_ns=now; f->terminal->big_engine->numeric.io.bell=bell;
}
static void destroy(struct fixture *f)
{
	st_cterm_destroy(&f->terminal); st_font_catalog_destroy(f->fonts); st_console_destroy(f->console);
}
static void feed(struct fixture *f, const void *wire, size_t size)
{
	size_t used; CHECK(st_cterm_write(f->terminal,wire,size,&used)==0 && used==size);
}
static void string(struct fixture *f, const char *wire) { feed(f,wire,strlen(wire)); }
static void prepare(struct fixture *f, unsigned mode)
{
	char wire[128]; snprintf(wire,sizeof(wire),"I#A 1:I#R %u:I#g 0:I#m 2,31,32:I#C 1,9:I#C 2,5:",mode); string(f,wire);
	f->terminal->big_engine->state.application.hollow=1;
}
static void points(struct fixture *f, uint16_t dx, uint16_t dy, uint16_t vx, uint16_t vy)
{
	struct cterm_big *b=f->terminal->big_engine;
	if (b->state.bgi.x!=31 || b->state.bgi.y!=32 || b->state.bgi.ddo_x!=dx || b->state.bgi.ddo_y!=dy
	    || b->state.bgi.device_x!=vx || b->state.bgi.device_y!=vy)
		fprintf(stderr,"points public=%u,%u lower=%u,%u device=%u,%u; expected31,32 %u,%u %u,%u\n",
		    b->state.bgi.x,b->state.bgi.y,b->state.bgi.ddo_x,b->state.bgi.ddo_y,b->state.bgi.device_x,b->state.bgi.device_y,dx,dy,vx,vy);
	CHECK(b->state.bgi.x==31 && b->state.bgi.y==32 && b->state.bgi.ddo_x==dx && b->state.bgi.ddo_y==dy);
	CHECK(b->state.bgi.device_x==vx && b->state.bgi.device_y==vy);
}
/* Literal side/top line candidates; no expected result calls a renderer. */
static const uint16_t sides[][2]={{18,11},{17,12},{16,13},{15,13},{14,14},{18,7},{18,8},{18,9},{18,10},{18,11}};
static const uint16_t tops[][2]={{14,7},{15,7},{16,7},{17,7},{18,7},{14,7},{13,8},{12,9},{11,9},{10,10},{18,7},{17,8},{16,9},{15,9},{14,10}};
static void scene(struct fixture *f, unsigned kind, uint8_t fill)
{
	const struct st_console_rect r={0,0,640,f->terminal->big_engine->state.bgi.height}; struct st_pixel_buffer *p=NULL;
	CHECK(st_console_get_pixels(&p,f->console,&r,false)==0);
	for (unsigned y=0;y<r.height;++y) for (unsigned x=0;x<640;++x) {
		unsigned pen=0;
		if (kind==0 && x>=10 && x<=14 && y>=10 && y<=14) pen=fill;
		if (kind>=1 && kind<=3 && x>=10 && x<=14 && y>=10 && y<=14) pen=x==10 || x==14 || y==10 || y==14?9:fill;
		if (kind==2 || kind==3) for (size_t i=0;i<sizeof(sides)/sizeof(sides[0]);++i) if (x==sides[i][0] && y==sides[i][1]) pen=9;
		if (kind==3) for (size_t i=0;i<sizeof(tops)/sizeof(tops[0]);++i) if (x==tops[i][0] && y==tops[i][1]) pen=9;
		if (kind==4 && x>=9 && x<=11 && y>=11 && y<=19) pen=fill;
		if (kind==5 && x>=8 && x<=23 && y>=16 && y<=31) pen=x%8==y%8?fill:0;
		if (kind==6) {
			if (x>=11 && x<=13 && y>=11 && y<=13) pen=fill;
			if ((x>=10 && x<=14 && ((y>=9 && y<=11) || (y>=13 && y<=15)))
			    || (y>=10 && y<=14 && ((x>=9 && x<=11) || (x>=13 && x<=15)))) pen=9;
		}
		if (kind==8 && x>=10 && x<=12 && y==11) pen=fill;
		if (kind==9) {
			if (x==11 && (y==10 || y==12)) pen=fill;
			if (x>=10 && x<=12 && y==11) pen=9;
		}
		if (kind==10 && x<=4 && y>=10 && y<=14) pen=fill;
		if (p->primary[(size_t)y*640+x]!=800+pen || p->alternate[(size_t)y*640+x]!=800+pen)
			fprintf(stderr,"scene%u pixel%u,%u got%u/%u expected%u\n",kind,x,y,
			    p->primary[(size_t)y*640+x],p->alternate[(size_t)y*640+x],800+pen);
		CHECK(p->primary[(size_t)y*640+x]==800+pen && p->alternate[(size_t)y*640+x]==800+pen);
	}
	st_pixel_buffer_destroy(p);
}
static void partitions(void)
{
	static const struct { const char *setup,*wire; unsigned scene; uint16_t dx,dy,vx,vy; } cases[]={
		{"","I#B 10,10,14,14,0:\033[2;3H",0,31,32,31,32},
		{"","I#B 14,14,10,10,1:\033[2;3H",1,14,14,14,14},
		{"","I#B 10,10,14,14,4,256:\033[2;3H",2,18,7,14,14},
		{"","I#B 10,10,14,14,4,257:\033[2;3H",3,18,7,14,10},
		{"","I#B 10,10,14,14,+1:\033[2;3H",7,31,32,31,32},
		{"I#T 2,4,0,1,0:","I#B 10,10,10,20,1:\033[2;3H",4,10,20,10,20},
		{"I#X 1,12,1,128,64,32,16,8,4,2,1:I#A 12:","I#B 23,31,8,16,0:\033[2;3H",5,31,32,31,32},
		{"I#T 2,0,0,0,1:","I#B 10,10,14,14,1:\033[2;3H",6,14,14,14,14},
	};
	for (unsigned utf8=0;utf8<2;++utf8) for (unsigned status=0;status<2;++status) {
		struct fixture f; create(&f,utf8,status); struct cterm_big *b=f.terminal->big_engine;
		for (unsigned mode=1;mode<=3;++mode) for (size_t c=0;c<sizeof(cases)/sizeof(cases[0]);++c) {
			size_t size=strlen(cases[c].wire), splits=mode==2 && !utf8 && !status?size+1:0;
			for (size_t split=0;split<=splits;++split) {
				prepare(&f,mode); string(&f,cases[c].setup);
				if (split==splits) for (size_t i=0;i<size;++i) feed(&f,cases[c].wire+i,1);
				else { feed(&f,cases[c].wire,split); string(&f,cases[c].wire+split); }
				CHECK(!b->bar.active && b->state.bgi.color==9 && b->state.bgi.device_color==9 && b->state.bgi.fill_color==5);
				CHECK(f.terminal->xpos==3 && f.terminal->ypos==2 && f.size==0);
				points(&f,cases[c].dx,cases[c].dy,cases[c].vx,cases[c].vy); scene(&f,cases[c].scene,5);
			}
		}
		destroy(&f);
	}
}
static void prefixes(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	prepare(&f,2); string(&f,"I#B 10,10,14,14,");
	CHECK(b->state.bgi.color==15 && b->state.bgi.fill_color==15 && !b->bar.active); scene(&f,7,0);
	CHECK(st_cterm_finish(f.terminal)==0 && b->state.bgi.color==15 && b->state.bgi.fill_color==15);
	prepare(&f,2); string(&f,"I#B 10,10,14,14,");
	string(&f,"4,"); CHECK(b->bar.active && b->state.bgi.color==9 && b->state.bgi.fill_color==5);
	points(&f,31,32,31,32); scene(&f,7,0);
	CHECK(st_cterm_finish(f.terminal)==0 && !b->bar.active); scene(&f,7,0);
	prepare(&f,2); string(&f,"I#B 10,10,14,14,4,"); local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS;
	CHECK(st_cterm_poll(f.terminal)==0 && f.bells==1 && !b->bar.active); scene(&f,2,5);
	prepare(&f,2); string(&f,"I#B 10,10,14,14,4,"); f.reset_bell=true; local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS;
	CHECK(st_cterm_poll(f.terminal)==0 && f.bells==2 && !b->bar.active); points(&f,31,32,31,32); scene(&f,7,0);
	f.reset_bell=false; prepare(&f,2); string(&f,"I#B 10,10,14,14,4,"); b->bar.kind=30000;
	string(&f,"0:"); points(&f,30014,59430,14,14);
	prepare(&f,2); string(&f,"I#B 10,10,14,14,");
	b->state.application.colors[1]=7; b->state.application.colors[2]=6; b->state.application.fill=0;
	string(&f,"+1:"); CHECK(b->state.bgi.color==7 && b->state.bgi.fill_style==0 && b->state.bgi.fill_color==6); scene(&f,7,0);
	prepare(&f,2); string(&f,"I#B 10,10,14,14,4,");
	b->state.application.colors[1]=7; b->state.application.colors[2]=6; b->state.application.fill=0;
	string(&f,"256:"); scene(&f,2,5);
	CHECK(b->state.bgi.color==9 && b->state.bgi.fill_style==1 && b->state.bgi.fill_color==5 && b->state.application.fill==0);
	prepare(&f,2); string(&f,"I#C 2,31:I#A 12:I#B 10,10,14,14,0:");
	CHECK(b->state.bgi.error==-11 && b->state.bgi.color==9 && b->state.bgi.fill_color==15); scene(&f,0,15);
	prepare(&f,2); b->state.bgi.state=0; size_t used; const char bad[]="I#B 10,10,14,14,+1:";
	CHECK(st_cterm_write(f.terminal,bad,sizeof(bad)-1,&used)==0 && b->last_error==IO_ERROR_PROTOCOL && used==sizeof(bad)-1);
	CHECK(b->state.bgi.color==9 && b->state.bgi.device_color==15 && b->state.bgi.fill_color==15 && b->state.bgi.error==-1 && !b->bar.active);
	b->state.bgi.state=3; prepare(&f,2); string(&f,"I#B 10,10,14,14,0:"); scene(&f,0,5);
	destroy(&f);
}
static void sources_and_reply(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	prepare(&f,3); string(&f,"I#g 3:I#& 0,1,1,0,B@5,10,10,14,14,4:");
	CHECK(b->bar.active && b->numeric.loop.active==0 && b->state.bgi.fill_color==5); scene(&f,7,0);
	string(&f,"256:"); scene(&f,2,5); points(&f,18,7,14,14);
	prepare(&f,3); string(&f,"I#g 3:I#& 0,1,1,0,B@6,10,10,14,14,4,257:"); scene(&f,3,5); points(&f,18,7,14,10);
	CHECK(f.size==0);
	b->control.input.pending=1; b->control.input.count=2; b->control.input.payload[1]='O'; b->control.input.payload[2]='K'; b->control.input.append_cr=1;
	string(&f,"I#B>10,10,14,14,+1:"); CHECK(f.size==0 && b->control.input.pending==1);
	string(&f,"~ "); CHECK(f.size==3 && memcmp(f.output,"OK\r",3)==0);
	/* The sixth read keeps its admitted branch but takes current kind as depth. */
	prepare(&f,2); string(&f,"I#g 0:I#B 10,10,14,14,4,"); b->bar.kind=0;
	string(&f,"257:"); scene(&f,1,5); points(&f,14,14,14,14);
	/* Actual batched byte reads use indexed primary-plane bits; mode2's
	 * first clipped lower stroke paints row14 x0..10 before later reads. */
	for (unsigned plane=0;plane<4;++plane) {
		prepare(&f,2); b->producer.mode=2; b->producer.batch=true; b->producer.read_plane=(uint8_t)plane;
		string(&f,"I#B 10,10,14,14,4,0:"); points(&f,0xa000,2,plane==0 || plane==3?0xa0e2:0xa000,6);
	}
	b->producer.mode=0; b->producer.batch=false;
	destroy(&f);
}
static void scaling_and_repetition(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	prepare(&f,3); string(&f,"I#g 3:I#B 10,5,12,5,0:"); scene(&f,8,5); points(&f,31,32,31,32);
	prepare(&f,3); string(&f,"I#g 3:I#B 10,5,12,5,1:"); scene(&f,9,5); points(&f,12,11,12,11);
	prepare(&f,3); string(&f,"I#g 3:I#& 0,2,1,0,B@5,x,10,4,14,0:"); scene(&f,10,5); points(&f,31,32,31,32);
	/* Dynamic FROM0..TO2 includes all three values0,1,2 (chapter21);
	 * it differs from the constant-only boundary's pre-increment rule. */
	CHECK(b->numeric.loop.active==0 && b->numeric.loop.current==2 && b->numeric.loop.varying_total==3);
	CHECK(!b->bar.active && f.size==0);
	destroy(&f);
}
static void rectangle_scene(struct fixture *f, unsigned kind, unsigned border, unsigned fill)
{
	struct cterm_big *big=f->terminal->big_engine;
	const struct st_console_rect r={0,0,640,big->state.bgi.height}; struct st_pixel_buffer *p=NULL;
	const struct st_big_mode *mode=st_big_mode((unsigned)big->state.bgi.mode+1);
	unsigned visible=r.height-(f->terminal->status_display_type>=1?mode->cell_height:0);
	CHECK(st_console_get_pixels(&p,f->console,&r,false)==0);
	for (unsigned y=0;y<r.height;++y) for (unsigned x=0;x<r.width;++x) {
		unsigned pen=kind==3 || kind==8?fill:0;
		bool inside=x>10 && x<14 && y>10 && y<14;
		bool edge=x>=10 && x<=14 && y>=10 && y<=14 && !inside;
		if (kind<=4 || kind==10) {
			if (edge) pen=kind==4 && (x==10 || x==14) && (y==10 || y==14)?0:border;
			if (inside) pen=kind==1?fill:kind==2?((x+y)%2==0?fill:0):kind==10 && y==11?fill:0;
		}
		if (kind==6 && ((x<=4 && (y==10 || y==14)) || (x==4 && y>=10 && y<=14))) pen=border;
		if (kind==7 && ((x>=10 && x<=14 && ((y>=9 && y<=11) || (y>=13 && y<=15)))
		    || (y>=10 && y<=14 && ((x>=9 && x<=11) || (x>=13 && x<=15))))) pen=border;
		if (kind==9 && x>=10 && x<=14 && y<=10 && (x==10 || x==14 || y==0 || y==10)) pen=border;
		if (kind==11 && x<=4 && y>=10 && y<=14) pen=x==4 || y==10 || y==14?border:fill;
		if (kind==12 && x>=10 && x<=14 && y<=4) pen=x==10 || x==14 || y==4?border:fill;
		if (kind==13 && x>=10 && x<=14 && y>=11 && y<=14) pen=x==10 || x==14 || y==11 || y==14?border:fill;
		size_t i=(size_t)y*r.width+x;
		if (y>=visible) {
			/* The source page continues behind the host status UI. Its
			 * expected flood result is retained in indexed backing. */
			CHECK(big->video.count>i && big->video.primary[i]==800+pen && big->video.alternate[i]==800+pen);
			pen=0;
		}
		if (p->primary[i]!=800+pen || p->alternate[i]!=800+pen) fprintf(stderr,"rectangle%u pixel%u,%u got%u/%u expected%u\n",kind,x,y,p->primary[i],p->alternate[i],800+pen);
		CHECK(p->primary[i]==800+pen && p->alternate[i]==800+pen);
	}
	st_pixel_buffer_destroy(p);
}
static void rectangle_partitions(void)
{
	static const struct { const char *setup,*wire; unsigned shape; } cases[]={
		{"","I#Z 10,10,14,14:\033[2;3H",0},
		{"I#H 0:","I#Z 10,10,14,14:\033[2;3H",1},
		{"I#H 256:","I#Z 10,10,14,14:\033[2;3H",0},
		{"I#H 0:I#A 12:","I#Z 10,10,14,14:\033[2;3H",2},
		{"I#H 0:","I#Z 14,10,10,14:\033[2;3H",3},
		{"I#M 2:","I#Z 10,10,14,14:\033[2;3H",4},
		{"I#M 2:","I#Z 12,12,12,12:\033[2;3H",5},
		{"","I#Z +1,10,4,14:\033[2;3H",6},
		{"I#H 0:I#T 2,0,0,0,1:","I#Z 10,10,14,14:\033[2;3H",7},
		{"I#H 0:I#T 2,4,0,1,0:","I#Z 10,10,14,14:\033[2;3H",8},
		{"I#H 0:","I#Z +1,10,4,14:\033[2;3H",11},
		{"I#H 0:","I#Z 10,+1,14,4:\033[2;3H",12},
	};
	for (unsigned utf8=0;utf8<2;++utf8) for (unsigned status=0;status<2;++status) {
		struct fixture f; create(&f,utf8,status); struct cterm_big *b=f.terminal->big_engine;
		for (unsigned mode=1;mode<=3;++mode) for (size_t c=0;c<sizeof(cases)/sizeof(cases[0]);++c) {
			size_t size=strlen(cases[c].wire), splits=mode==2 && !utf8 && !status?size+1:0;
			for (size_t split=0;split<=splits;++split) {
				prepare(&f,mode); string(&f,cases[c].setup); b->state.bgi.error=-11;
				if (split==splits) for (size_t i=0;i<size;++i) feed(&f,cases[c].wire+i,1);
				else { feed(&f,cases[c].wire,split); string(&f,cases[c].wire+split); }
				CHECK(b->state.bgi.color==9 && b->state.bgi.fill_color==5 && b->state.bgi.error==-11 && b->last_error==0);
				CHECK(f.terminal->xpos==3 && f.terminal->ypos==2 && f.size==0);
				points(&f,31,32,31,32); rectangle_scene(&f,cases[c].shape,9,5);
			}
		}
		destroy(&f);
	}
}
static int corrupt_flood_byte(void *context, const struct st_pixel_buffer *p, struct st_console_rect *r)
{
	(void)context; size_t i=11*p->width+8; p->primary[i]=42; p->alternate[i]=43;
	*r=(struct st_console_rect){8,11,1,1}; return 0;
}
static void rectangle_prefixes_and_sources(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	prepare(&f,2); string(&f,"I#Z 10,10,14,");
	CHECK(b->state.bgi.color==15 && b->state.bgi.fill_color==15);
	CHECK(st_cterm_finish(f.terminal)==0); rectangle_scene(&f,5,9,5);
	string(&f,"I#Z 10,10,14,"); CHECK(st_cterm_reset_terminal(f.terminal)==0 && b->state.bgi.color==15);
	string(&f,"I#Z 10,10,14,"); local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS;
	CHECK(st_cterm_poll(f.terminal)==0 && f.bells==1 && b->state.bgi.color==9 && b->state.bgi.fill_color==5);
	rectangle_scene(&f,9,9,5); points(&f,31,32,31,32);
	prepare(&f,2); string(&f,"I#Z 10,10,14,"); f.reset_bell=true; local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS;
	CHECK(st_cterm_poll(f.terminal)==0 && b->state.bgi.color==15 && b->state.bgi.fill_color==15); rectangle_scene(&f,5,9,5); f.reset_bell=false;
	string(&f,"I#Z 10,10,14,"); b->state.application.hollow=256; b->state.application.colors[1]=7; b->state.application.colors[2]=6;
	string(&f,"14:"); rectangle_scene(&f,0,7,6);
	CHECK(b->state.bgi.color==7 && b->state.bgi.fill_color==6); points(&f,31,32,31,32);
	/* A full border WORD is not an invented max-colour validation. */
	prepare(&f,2); string(&f,"I#H 0:I#C 1,271:I#Z 10,10,14,14:"); rectangle_scene(&f,1,15,5);
	/* Rejected custom fill retains the previous installed solid15. */
	prepare(&f,2); string(&f,"I#H 0:I#C 2,31:I#A 12:I#Z 10,10,14,14:");
	CHECK(b->state.bgi.error==-11 && b->state.bgi.fill_color==15); rectangle_scene(&f,1,9,15);
	prepare(&f,2); string(&f,"I#H 0:"); b->flood_work.capacity=0; string(&f,"I#Z 10,10,14,14:");
	CHECK(b->state.bgi.error==-7 && b->flood_work.overflow && f.size==0); rectangle_scene(&f,10,9,5); b->flood_work.capacity=CTERM_BIG_FLOOD_RECORDS;
	prepare(&f,2); b->state.bgi.state=0; string(&f,"I#Z 10,10,14,14:");
	CHECK(b->last_error==IO_ERROR_PROTOCOL && b->state.bgi.color==9 && b->state.bgi.device_color==15 && b->state.bgi.fill_color==15 && b->state.bgi.error==-1);
	rectangle_scene(&f,5,9,5); b->state.bgi.state=3;
	prepare(&f,2); string(&f,"I#Z 10,10,14,14:"); rectangle_scene(&f,0,9,5);
	/* A failed flood keeps the completed outline and no session-fatal error. */
	prepare(&f,2); string(&f,"I#H 0:"); CHECK(st_console_modify_pixels(f.console,corrupt_flood_byte,NULL)==0);
	string(&f,"I#Z 10,10,14,14:"); CHECK(b->last_error==IO_ERROR_OUT_OF_RANGE && f.size==0);
	const struct st_console_rect outline={10,10,5,5}; struct st_pixel_buffer *prefix=NULL;
	CHECK(st_console_get_pixels(&prefix,f.console,&outline,false)==0);
	for (unsigned y=0;y<5;++y) for (unsigned x=0;x<5;++x) {
		unsigned pen=x==0 || x==4 || y==0 || y==4?9:0;
		CHECK(prefix->primary[y*5+x]==800+pen && prefix->alternate[y*5+x]==800+pen);
	}
	st_pixel_buffer_destroy(prefix); points(&f,31,32,31,32);
	prepare(&f,2); string(&f,"I#H 0:I#Z 10,10,14,14:"); rectangle_scene(&f,1,9,5);
	prepare(&f,3); string(&f,"I#g 3:I#H 0:I#Z 10,5,14,6:"); rectangle_scene(&f,13,9,5); points(&f,31,32,31,32);
	/* Supplied coordinate fields bypass g3 and end the source on Y2. */
	prepare(&f,3); string(&f,"I#g 3:I#H 0:I#& 0,1,1,0,Z@4,10,10,14,14:");
	rectangle_scene(&f,1,9,5); points(&f,31,32,31,32);
	CHECK(f.size==0 && b->numeric.loop.active==0);
	f.size=0; prepare(&f,2); string(&f,"I#& 0,2,1,0,Z@4,x,10,x,12:");
	CHECK(f.size==0 && b->numeric.loop.current==2 && b->numeric.loop.active==0);
	const struct st_console_rect r={0,10,3,3}; struct st_pixel_buffer *p=NULL;
	CHECK(st_console_get_pixels(&p,f.console,&r,false)==0);
	for (unsigned i=0;i<9;++i) CHECK(p->primary[i]==809 && p->alternate[i]==809); st_pixel_buffer_destroy(p);
	points(&f,31,32,31,32);
	f.size=0; prepare(&f,2); b->control.input.pending=1; b->control.input.count=2; b->control.input.payload[1]=0; b->control.input.payload[2]=255;
	string(&f,"I#H>0:Z>10,10,14,14:"); CHECK(f.size==0 && b->control.input.pending==1); rectangle_scene(&f,1,9,5);
	string(&f,"~ "); CHECK(f.size==2 && f.output[0]==0 && f.output[1]==255);
	destroy(&f);
}
int main(void) { rectangle_partitions(); rectangle_prefixes_and_sources(); partitions(); prefixes(); sources_and_reply(); scaling_and_repetition(); return 0; }
