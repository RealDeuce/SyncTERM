/* Independent BIG m/L/D/d wire expectations: chapters12/51/57. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm.h"
#include "cterm_big_internal.h"
#include "st_cterm.h"
#include "io_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
struct fixture {
	st_console_t *console; st_font_catalog_t *fonts; st_cterm_t *terminal;
	unsigned bells; bool reset_bell; uint8_t output[64]; size_t size;
};
struct dot { uint16_t x,y; uint32_t primary,alternate; };
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
static void point(struct fixture *f, uint16_t x, uint16_t y)
{
	const struct st_big_state *s=&f->terminal->big_engine->state;
	CHECK(s->bgi.x==x && s->bgi.y==y && s->bgi.ddo_x==x && s->bgi.ddo_y==y && s->bgi.device_x==x && s->bgi.device_y==y);
}
static void canvas(struct fixture *f, const struct dot *dots, size_t count)
{
	struct st_pixel_buffer *pixels=NULL;
	const struct st_console_rect r={0,0,640,f->terminal->big_engine->state.bgi.height};
	CHECK(st_console_get_pixels(&pixels,f->console,&r,false)==0);
	uint32_t *a=calloc((size_t)r.width*r.height,sizeof(*a)), *b=calloc((size_t)r.width*r.height,sizeof(*b));
	CHECK(a!=NULL && b!=NULL);
	for (size_t i=0;i<count;++i) { CHECK(dots[i].x<r.width && dots[i].y<r.height); size_t n=(size_t)dots[i].y*r.width+dots[i].x; a[n]=dots[i].primary; b[n]=dots[i].alternate; }
	for (size_t i=0;i<(size_t)r.width*r.height;++i) {
		if (pixels->primary[i]!=(a[i]?a[i]:800) || pixels->alternate[i]!=(b[i]?b[i]:800))
			fprintf(stderr,"pixel (%zu,%zu): %u/%u expected %u/%u\n",i%r.width,i/r.width,pixels->primary[i],pixels->alternate[i],a[i]?a[i]:800,b[i]?b[i]:800);
		CHECK(pixels->primary[i]==(a[i]?a[i]:800) && pixels->alternate[i]==(b[i]?b[i]:800));
	}
	free(a); free(b); st_pixel_buffer_destroy(pixels);
}
static void reset_mode(struct fixture *f, unsigned resolution)
{
	char wire[16]; snprintf(wire,sizeof(wire),"I#R %u:",resolution); string(f,wire);
}
static const struct dot absolute[]={{102,60,815,815},{103,60,815,815},{106,60,815,815},{107,60,815,815}};
static const struct dot target_first[]={{100,60,815,815},{101,60,815,815},{104,60,815,815},{105,60,815,815}};
static const struct dot relative[]={{120,60,815,815},{119,61,815,815},{118,61,815,815},{117,62,815,815},{116,62,815,815}};
static const struct {
	const char *wire; const struct dot *dots; size_t count; uint16_t x,y;
} cases[]={
	{"I#T>2,1,1,1,0:L 100,60,107,60:\033[2;3H",absolute,4,107,60},
	{"I#T>2,1,1,1,0:P>100,60:D 107,60:\033[2;3H",target_first,4,107,60},
	{"I#P>120,60:d 1,4,257,2:\033[2;3H",relative,5,116,62},
	{"I#m>256,120,60:m 1,4,257,2:\033[2;3H",NULL,0,116,62},
	{"I#m>2,+1,60:m 0,2,1,2:\033[2;3H",NULL,0,1,62},
	{"I#m>2,100,100:m +1,20,1,40:\033[2;3H",NULL,0,80,140},
	{"I#m>2,100,100:m x,20,1,+2:\033[2;3H",NULL,0,80,98},
};
static void partitions_and_profiles(void)
{
	struct fixture f; create(&f,false,false);
	for (size_t c=0;c<sizeof(cases)/sizeof(cases[0]);++c) for (size_t split=0;split<=strlen(cases[c].wire);++split) {
		reset_mode(&f,2); feed(&f,cases[c].wire,split); string(&f,cases[c].wire+split);
		point(&f,cases[c].x,cases[c].y); canvas(&f,cases[c].dots,cases[c].count);
		CHECK(f.terminal->xpos==3 && f.terminal->ypos==2 && f.size==0);
	}
	destroy(&f);
	for (unsigned utf8=0;utf8<2;++utf8) for (unsigned status=0;status<2;++status) {
		create(&f,utf8,status);
		for (unsigned resolution=1;resolution<=3;++resolution) for (size_t c=0;c<sizeof(cases)/sizeof(cases[0]);++c) {
			reset_mode(&f,resolution);
			for (size_t i=0;i<strlen(cases[c].wire);++i) feed(&f,cases[c].wire+i,1);
			point(&f,cases[c].x,cases[c].y); canvas(&f,cases[c].dots,cases[c].count);
			CHECK(f.terminal->xpos==3 && f.terminal->ypos==2 && f.size==0);
		}
		destroy(&f);
	}
}
static void color_and_waiting_prefix(void)
{
	static const char *begins[]={"I#L ","I#D ","I#d "}, *tails[]={"10,20,12,20:","12,20:","0,2,0,0:"};
	const struct dot expected[]={{10,20,803,803},{11,20,803,803},{12,20,803,803}};
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	for (size_t i=0;i<3;++i) {
		reset_mode(&f,2); b->state.bgi.x=b->state.bgi.ddo_x=b->state.bgi.device_x=10;
		b->state.bgi.y=b->state.bgi.ddo_y=b->state.bgi.device_y=20;
		b->state.application.colors[0]=9; b->state.application.colors[1]=0x123; b->state.bgi.error=-11;
		string(&f,begins[i]); CHECK(b->state.bgi.color==0x23 && b->state.bgi.device_color==3);
		b->state.application.colors[1]=6; string(&f,tails[i]);
		CHECK(b->state.bgi.error==-11 && b->state.bgi.color==0x23 && b->state.bgi.device_color==3);
		canvas(&f,expected,3); point(&f,12,20);
	}
	/* An installed device change during acquisition is also live: the
	 * command must not reinstall its entry colour just before drawing. */
	reset_mode(&f,2); string(&f,"I#L 10,20,"); b->state.bgi.device_color=9;
	string(&f,"12,20:");
	const struct dot current[]={{10,20,809,809},{11,20,809,809},{12,20,809,809}};
	canvas(&f,current,3); point(&f,12,20);
	string(&f,"I#L 30,31,"); CHECK(st_cterm_finish(f.terminal)==0); point(&f,12,20);
	string(&f,"I#D 32,"); CHECK(st_cterm_reset_terminal(f.terminal)==0); point(&f,12,20);
	string(&f,"I#d 1,2,1"); CHECK(st_cterm_finish(f.terminal)==0); point(&f,12,20);
	b->state.bgi.state=0; b->state.application.colors[1]=0x127;
	const char wire[]="I#L 1,2,3,4:"; size_t used;
	CHECK(st_cterm_write(f.terminal,wire,sizeof(wire)-1,&used)==0 && b->last_error==IO_ERROR_PROTOCOL && used==sizeof(wire)-1);
	CHECK(b->state.bgi.color==0x27 && b->state.bgi.device_color==6 && b->state.bgi.error==-1);
	destroy(&f);
}
static void repeat_and_deferred_reply(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#& 0,3,1,0,L@4,x,20,x,20:");
	const struct dot repeated[]={{0,20,815,815},{1,20,815,815},{2,20,815,815},{3,20,815,815}};
	canvas(&f,repeated,4); point(&f,3,20);
	CHECK(b->numeric.loop.active==0 && b->numeric.loop.current==3 && f.size==2 && memcmp(f.output,"\x13\x11",2)==0);
	b->control.input.pending=1; b->control.input.count=2; b->control.input.append_cr=1;
	b->control.input.payload[1]=0; b->control.input.payload[2]=255;
	string(&f,"I#L>10,20,12,20:"); CHECK(f.size==2 && b->control.input.pending==1);
	string(&f,"~ "); CHECK(f.size==5 && memcmp(f.output+2,"\0\xff\r",3)==0 && b->control.input.pending==0);
	destroy(&f);
}
static void clipping_and_thickness(void)
{
	struct fixture f; create(&f,false,false);
	const struct dot slope[]={{2,0,815,815},{3,0,815,815}};
	string(&f,"I#L +1,+1,2,0:"); canvas(&f,slope,2); point(&f,2,0);
	reset_mode(&f,2); const struct dot side[]={{0,0,815,815},{1,0,815,815},{2,0,815,815},{3,0,815,815}};
	string(&f,"I#T>2,0,1,1,1:L 0,+1,3,+1:"); canvas(&f,side,4); point(&f,3,65535);
	reset_mode(&f,2); string(&f,"I#T>2,4,1,1,0:L +1,20,3,20:");
	const struct dot phase[]={{0,20,815,815}}; canvas(&f,phase,1); point(&f,3,20);
	reset_mode(&f,2); string(&f,"I#L 9998,20,9999,20:"); canvas(&f,NULL,0); point(&f,9999,20);
	/* Invisible normal return still moves every point owner to the request. */
	string(&f,"I#D 9999,21:"); canvas(&f,NULL,0); point(&f,9999,21);
	destroy(&f);
}
static void sources_scaling_and_deadlines(void)
{
	struct fixture f; create(&f,true,true); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#R 3:I#g 3:I#L 5,5,5,5:");
	const struct dot scaled[]={{5,11,815,815}}; canvas(&f,scaled,1); point(&f,5,11);
	reset_mode(&f,3); string(&f,"I#& 0,1,1,0,L@4,7,5,7,5:");
	const struct dot stored[]={{7,5,815,815}}; canvas(&f,stored,1); point(&f,7,5);
	CHECK(f.size==2 && memcmp(f.output,"\x13\x11",2)==0);
	reset_mode(&f,3); string(&f,"I#& 0,1,1,0,L@3,7,5,7:5:");
	const struct dot mixed[]={{7,5,815,815},{7,6,815,815},{7,7,815,815},{7,8,815,815},{7,9,815,815},{7,10,815,815},{7,11,815,815}};
	canvas(&f,mixed,7); point(&f,7,11);
	reset_mode(&f,3); string(&f,"I#& 0,1,1,0,d@4,1,2,1,5:");
	CHECK(b->state.bgi.x==65534 && b->state.bgi.y==5 && b->numeric.loop.active==0);
	/* Last stored sign ends the source; only the following Y is scaled. */
	reset_mode(&f,3); string(&f,"I#& 0,1,1,0,d@3,1,2,1:5:"); point(&f,65534,11);
	reset_mode(&f,3); string(&f,"I#g 0:I#D 12,"); local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS;
	CHECK(st_cterm_poll(f.terminal)==0 && f.bells==1); point(&f,12,0);
	reset_mode(&f,3); string(&f,"I#D 12,"); f.reset_bell=true; local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS;
	CHECK(st_cterm_poll(f.terminal)==0 && f.bells==2); point(&f,0,0); canvas(&f,NULL,0);
	destroy(&f);
}
static int seed(void *context, const struct st_pixel_buffer *p, struct st_console_rect *changed)
{
	bool invalid=*(bool *)context; p->primary[20*p->width+11]=invalid?5:803; p->alternate[20*p->width+11]=806;
	*changed=(struct st_console_rect){11,20,1,1}; return 0;
}
static void point_owners_xor_and_failures(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	b->state.bgi.x=50; b->state.bgi.y=60; b->state.bgi.ddo_x=10; b->state.bgi.ddo_y=20;
	string(&f,"I#D 12,20:"); const struct dot old_ddo[]={{10,20,815,815},{11,20,815,815},{12,20,815,815}};
	canvas(&f,old_ddo,3); point(&f,12,20);
	reset_mode(&f,2); b->state.bgi.x=10; b->state.bgi.y=20; b->state.bgi.ddo_x=8; b->state.bgi.ddo_y=22;
	string(&f,"I#d 0,2,1,+2:");
	const struct dot separate[]={{12,18,815,815},{11,19,815,815},{10,20,815,815},{9,21,815,815},{8,22,815,815}};
	canvas(&f,separate,5); point(&f,12,18);
	reset_mode(&f,2); bool invalid=false; CHECK(st_console_modify_pixels(f.console,seed,&invalid)==0);
	b->state.application.colors[1]=5; string(&f,"I#M 2:");
	string(&f,"I#L 10,20,12,20:");
	const struct dot xor_dots[]={{10,20,805,805},{11,20,806,803},{12,20,805,805}};
	canvas(&f,xor_dots,3); point(&f,12,20);
	/* L keeps the accepted pixel prefix and skips its explicit final move. */
	reset_mode(&f,2); b->state.bgi.write_mode=1; invalid=true;
	CHECK(st_console_modify_pixels(f.console,seed,&invalid)==0);
	b->control.input.pending=1; b->control.input.count=1; b->control.input.payload[1]='R';
	const char l[]="I#L 10,20,12,20:"; size_t used;
	CHECK(st_cterm_write(f.terminal,l,sizeof(l)-1,&used)==0 && b->last_error==IO_ERROR_OUT_OF_RANGE && used==sizeof(l)-1);
	CHECK(f.size==0 && b->control.input.count==1 && b->control.input.payload[1]=='R');
	const struct dot partial[]={{10,20,805,805},{11,20,5,806}}; canvas(&f,partial,2); point(&f,0,0);
	/* D publishes DDO target first, but failure skips device/public tails. */
	b->state.bgi.ddo_x=12; b->state.bgi.ddo_y=20;
	const char d[]="I#D 10,20:";
	CHECK(st_cterm_write(f.terminal,d,sizeof(d)-1,&used)==0 && b->last_error==IO_ERROR_OUT_OF_RANGE && used==sizeof(d)-1);
	CHECK(b->state.bgi.x==0 && b->state.bgi.y==0 && b->state.bgi.device_x==0 && b->state.bgi.ddo_x==10 && b->state.bgi.ddo_y==20);
	/* Recovery uses COPY without inheriting a sticky rendering failure. */
	b->state.bgi.write_mode=0; string(&f,"I#L 10,20,12,20:");
	const struct dot recovered[]={{10,20,805,805},{11,20,805,805},{12,20,805,805}}; canvas(&f,recovered,3); point(&f,12,20);
	destroy(&f);
}
static void move_state_and_origin(void)
{
	struct fixture f; create(&f,true,true); struct cterm_big *b=f.terminal->big_engine;
	bool invalid=false; CHECK(st_console_modify_pixels(f.console,seed,&invalid)==0);
	string(&f,"\033[10;20HI#T 2,1,1,1,1:");
	b->state.application.marker=7; b->state.application.marker_size=3; b->state.application.colors[3]=4;
	b->state.application.hollow=7; b->state.application.fill=5; b->state.application.write_mode=1;
	b->state.bgi.color=b->state.bgi.device_color=6; b->state.bgi.background=9;
	b->state.bgi.write_mode=1; b->state.bgi.error=-11;
	string(&f,"I#m 256,120,60:"); point(&f,120,60);
	const struct dot unchanged[]={{11,20,803,806}}; canvas(&f,unchanged,1);
	CHECK(f.terminal->xpos==20 && f.terminal->ypos==10 && f.size==0);
	CHECK(b->state.application.marker==7 && b->state.application.marker_size==3 && b->state.application.colors[3]==4);
	CHECK(b->state.application.hollow==7 && b->state.application.fill==5 && b->state.application.write_mode==1);
	CHECK(b->state.bgi.color==6 && b->state.bgi.device_color==6 && b->state.bgi.background==9 && b->state.bgi.write_mode==1);
	CHECK(b->state.bgi.line_mask==0xcccc && b->state.bgi.ddo_line_width==3 && b->state.bgi.error==-11);
	b->state.bgi.viewport[0]=2; b->state.bgi.viewport[1]=3;
	string(&f,"I#m 2,+1,2:");
	CHECK(b->state.bgi.x==65535 && b->state.bgi.y==2 && b->state.bgi.ddo_x==1 && b->state.bgi.ddo_y==5 && b->state.bgi.device_x==1 && b->state.bgi.device_y==5);
	string(&f,"I#m 1,1,1,2:");
	CHECK(b->state.bgi.x==65534 && b->state.bgi.y==4 && b->state.bgi.ddo_x==0 && b->state.bgi.ddo_y==7 && b->state.bgi.device_x==0 && b->state.bgi.device_y==7);
	string(&f,"I#m 1,2,1,");
	CHECK(b->state.bgi.x==65534 && b->state.bgi.y==4 && b->state.bgi.ddo_x==0 && b->state.bgi.ddo_y==7);
	/* Relative m reads current public CP after operands, independently of
	 * the DDO/device start left by a previous operation. */
	b->state.bgi.x=100; b->state.bgi.y=200; b->state.bgi.ddo_x=500; b->state.bgi.ddo_y=600;
	string(&f,"5:");
	CHECK(b->state.bgi.x==98 && b->state.bgi.y==205 && b->state.bgi.ddo_x==100 && b->state.bgi.ddo_y==208 && b->state.bgi.device_x==100 && b->state.bgi.device_y==208);
	b->state.bgi.viewport[0]=b->state.bgi.viewport[1]=0;
	b->state.bgi.x=32767; b->state.bgi.y=65535;
	string(&f,"I#m 0,1,0,1:"); point(&f,32768,0);
	canvas(&f,unchanged,1); CHECK(f.terminal->xpos==20 && f.terminal->ypos==10 && f.size==0);
	destroy(&f);
}
static void move_sources_and_replies(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#R 3:I#g 3:I#m 2,5,5:"); point(&f,5,11);
	string(&f,"I#R 3:I#m 1,2,1,5:"); point(&f,65534,11);
	string(&f,"I#& 0,1,1,0,m@3,2,7,5:"); point(&f,7,5);
	string(&f,"I#& 0,1,1,0,m@2,2,7:5:"); point(&f,7,11);
	string(&f,"I#& 0,1,1,0,m@1,2:7,5:"); point(&f,7,11);
	string(&f,"I#R 3:I#& 0,1,1,0,m@4,1,2,1,5:"); point(&f,65534,5);
	string(&f,"I#R 3:I#& 0,1,1,0,m@3,1,2,1:5:"); point(&f,65534,11);
	string(&f,"I#R 3:I#& 0,2,1,0,m@4,1,x,1,x:"); point(&f,65533,3);
	CHECK(f.size==12 && memcmp(f.output,"\x13\x11\x13\x11\x13\x11\x13\x11\x13\x11\x13\x11",12)==0);
	canvas(&f,NULL,0);
	b->control.input.pending=1; b->control.input.count=2; b->control.input.append_cr=1;
	b->control.input.payload[1]=0; b->control.input.payload[2]=255;
	string(&f,"I#m>2,10,20:"); CHECK(f.size==12 && b->control.input.pending==1);
	string(&f,"~ "); CHECK(f.size==15 && memcmp(f.output+12,"\0\xff\r",3)==0 && b->control.input.pending==0);
	destroy(&f);
}
static void move_cancellation_and_failure(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#m 2,100,100:"); point(&f,100,100);
	string(&f,"I#m 2,12,"); point(&f,100,100); CHECK(st_cterm_finish(f.terminal)==0); point(&f,100,100);
	string(&f,"I#m 1,2,1"); CHECK(st_cterm_reset_terminal(f.terminal)==0); point(&f,100,100);
	string(&f,"I#m 2,12,"); local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS;
	CHECK(st_cterm_poll(f.terminal)==0 && f.bells==1); point(&f,12,0);
	string(&f,"I#m 1,2,1,"); local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS;
	CHECK(st_cterm_poll(f.terminal)==0 && f.bells==2); point(&f,10,0);
	string(&f,"I#m 2,12,"); f.reset_bell=true; local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS;
	CHECK(st_cterm_poll(f.terminal)==0 && f.bells==3); point(&f,10,0);
	b->state.bgi.state=0; b->state.bgi.color=7; b->state.bgi.error=0;
	string(&f,"I#m 2,120,"); point(&f,10,0); CHECK(b->state.bgi.error==0 && b->state.bgi.color==7);
	const char tail[]="60:"; size_t used;
	CHECK(st_cterm_write(f.terminal,tail,sizeof(tail)-1,&used)==0 && b->last_error==IO_ERROR_PROTOCOL && used==sizeof(tail)-1);
	point(&f,10,0); CHECK(b->state.bgi.error==-1 && b->state.bgi.color==7 && f.size==0);
	const char relative_wire[]="I#m 1,2,1,5:";
	CHECK(st_cterm_write(f.terminal,relative_wire,sizeof(relative_wire)-1,&used)==0 && b->last_error==IO_ERROR_PROTOCOL && used==sizeof(relative_wire)-1);
	point(&f,10,0); b->state.bgi.state=3;
	string(&f,"I#m 2,200,300:"); point(&f,200,300); canvas(&f,NULL,0);
	CHECK(b->state.bgi.error==-1 && b->state.bgi.color==7 && f.size==0);
	destroy(&f);
}
static void writing_mode_partitions(void)
{
	static const struct { const char *wire; uint16_t mode; } requests[]={
		{"I#M>2:L>100,60,102,60:L 100,60,102,60:\033[2;3H",1},
		{"I#M>3:L>100,60,102,60:L 100,60,102,60:\033[2;3H",1},
		{"I#M>258:L>100,60,102,60:L 100,60,102,60:\033[2;3H",0},
	};
	const struct dot copied[]={{100,60,815,815},{101,60,815,815},{102,60,815,815}};
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	for (size_t c=0;c<sizeof(requests)/sizeof(requests[0]);++c)
		for (size_t split=0;split<=strlen(requests[c].wire);++split) {
			reset_mode(&f,2); b->state.bgi.error=-11;
			feed(&f,requests[c].wire,split); string(&f,requests[c].wire+split);
			CHECK(b->state.bgi.write_mode==requests[c].mode && b->state.application.write_mode==requests[c].mode && b->state.bgi.error==-11);
			canvas(&f,requests[c].mode?NULL:copied,requests[c].mode?0:3); point(&f,102,60);
			CHECK(f.terminal->xpos==3 && f.terminal->ypos==2 && f.size==0);
		}
	destroy(&f);
	for (unsigned utf8=0;utf8<2;++utf8) for (unsigned status=0;status<2;++status) {
		create(&f,utf8,status); b=f.terminal->big_engine;
		for (unsigned resolution=1;resolution<=3;++resolution) for (size_t c=0;c<sizeof(requests)/sizeof(requests[0]);++c) {
			reset_mode(&f,resolution);
			for (size_t i=0;i<strlen(requests[c].wire);++i) feed(&f,requests[c].wire+i,1);
			CHECK(b->state.bgi.write_mode==requests[c].mode && b->state.application.write_mode==requests[c].mode);
			canvas(&f,requests[c].mode?NULL:copied,requests[c].mode?0:3); point(&f,102,60);
			CHECK(f.terminal->xpos==3 && f.terminal->ypos==2 && f.size==0);
		}
		destroy(&f);
	}
}
static void writing_mode_sources_and_failures(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#m 2,100,60:");
	static const struct { const char *word; uint16_t mode; } requests[]={
		{"0",0},{"1",0},{"2",1},{"3",1},{"4",0},{"256",0},{"258",0},{"+1",0},{"x",0}};
	for (size_t i=0;i<sizeof(requests)/sizeof(requests[0]);++i) {
		b->state.bgi.write_mode=b->state.application.write_mode=(uint16_t)(1-requests[i].mode);
		char wire[24]; snprintf(wire,sizeof(wire),"I#M %s:",requests[i].word); string(&f,wire);
		CHECK(b->state.bgi.write_mode==requests[i].mode && b->state.application.write_mode==requests[i].mode);
		point(&f,100,60); canvas(&f,NULL,0); CHECK(f.size==0 && f.terminal->xpos==1);
	}
	string(&f,"I#M 2"); CHECK(st_cterm_finish(f.terminal)==0 && b->state.bgi.write_mode==0 && b->state.application.write_mode==0);
	string(&f,"I#M 2:"); string(&f,"I#M "); CHECK(st_cterm_reset_terminal(f.terminal)==0 && b->state.bgi.write_mode==1 && b->state.application.write_mode==1);
	string(&f,"I#M "); local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS;
	CHECK(st_cterm_poll(f.terminal)==0 && f.bells==1 && b->state.bgi.write_mode==0 && b->state.application.write_mode==0);
	string(&f,"I#R 3:I#g 3:I#M 2:"); CHECK(b->state.bgi.write_mode==1 && b->state.application.write_mode==1);
	string(&f,"I#& 2,4,1,0,M@1,x:");
	CHECK(b->state.bgi.write_mode==0 && b->state.application.write_mode==0 && b->numeric.loop.active==0 && b->numeric.loop.current==4 && f.size==2 && memcmp(f.output,"\x13\x11",2)==0);
	b->control.input.pending=1; b->control.input.count=1; b->control.input.payload[1]='R';
	string(&f,"I#M>1:"); CHECK(b->state.bgi.write_mode==0 && b->state.application.write_mode==0 && f.size==2);
	string(&f,"~ "); CHECK(f.size==3 && f.output[2]=='R');
	b->state.bgi.state=0; b->state.bgi.write_mode=9; b->state.application.write_mode=7; b->state.bgi.error=0;
	string(&f,"I#M "); CHECK(b->state.bgi.error==0 && b->state.bgi.write_mode==9 && b->state.application.write_mode==7);
	const char tail[]="2:"; size_t used;
	CHECK(st_cterm_write(f.terminal,tail,sizeof(tail)-1,&used)==0 && b->last_error==IO_ERROR_PROTOCOL && used==sizeof(tail)-1);
	CHECK(b->state.bgi.error==-1 && b->state.bgi.write_mode==9 && b->state.application.write_mode==7 && f.size==3);
	b->state.bgi.state=3; string(&f,"I#M 3:");
	CHECK(b->state.bgi.error==-1 && b->state.bgi.write_mode==1 && b->state.application.write_mode==1);
	destroy(&f);
}
static void color_partitions(void)
{
	static const struct dot line_color[]={{100,60,803,803},{101,60,803,803},{102,60,803,803}};
	static const struct dot marker_color[]={{100,60,805,805}};
	static const struct dot unchanged_line[]={{100,60,815,815},{101,60,815,815},{102,60,815,815}};
	static const struct { const char *wire; const struct dot *dots; size_t count; uint16_t colors[4],draw_color,x; } requests[]={
		{"I#C>1,291:L 100,60,102,60:\033[2;3H",line_color,3,{15,291,15,15},0x23,102},
		{"I#C>258,261:P 100,60:\033[2;3H",marker_color,1,{261,15,15,15},5,100},
		{"I#C>2,31:C>3,+1:L 100,60,102,60:\033[2;3H",unchanged_line,3,{15,15,31,65535},15,102},
	};
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	for (size_t c=0;c<sizeof(requests)/sizeof(requests[0]);++c)
		for (size_t split=0;split<=strlen(requests[c].wire);++split) {
			reset_mode(&f,2); for (size_t i=0;i<4;++i) b->state.application.colors[i]=15;
			feed(&f,requests[c].wire,split); string(&f,requests[c].wire+split);
			CHECK(memcmp(b->state.application.colors,requests[c].colors,sizeof(requests[c].colors))==0);
			CHECK(b->state.bgi.color==requests[c].draw_color && b->state.bgi.device_color==(requests[c].draw_color&15));
			CHECK(b->state.bgi.fill_style==1 && b->state.bgi.fill_color==15 && f.terminal->xpos==3 && f.terminal->ypos==2 && f.size==0);
			canvas(&f,requests[c].dots,requests[c].count); point(&f,requests[c].x,60);
		}
	destroy(&f);
	for (unsigned utf8=0;utf8<2;++utf8) for (unsigned status=0;status<2;++status) {
		create(&f,utf8,status); b=f.terminal->big_engine;
		for (unsigned resolution=1;resolution<=3;++resolution) for (size_t c=0;c<sizeof(requests)/sizeof(requests[0]);++c) {
			reset_mode(&f,resolution); for (size_t i=0;i<4;++i) b->state.application.colors[i]=15;
			for (size_t i=0;i<strlen(requests[c].wire);++i) feed(&f,requests[c].wire+i,1);
			CHECK(memcmp(b->state.application.colors,requests[c].colors,sizeof(requests[c].colors))==0);
			CHECK(b->state.bgi.color==requests[c].draw_color && b->state.bgi.device_color==(requests[c].draw_color&15));
			canvas(&f,requests[c].dots,requests[c].count); point(&f,requests[c].x,60);
			CHECK(f.terminal->xpos==3 && f.terminal->ypos==2 && f.size==0);
		}
		destroy(&f);
	}
}
static void color_prefixes_sources_and_lifetime(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	const uint16_t initial[]={11,12,13,14}; memcpy(b->state.application.colors,initial,sizeof(initial));
	b->state.bgi.color=b->state.bgi.device_color=9; b->state.bgi.error=-11;
	string(&f,"I#C 9,"); CHECK(memcmp(b->state.application.colors,initial,sizeof(initial))==0 && b->state.bgi.color==9);
	string(&f,"31:"); CHECK(b->state.application.colors[0]==31 && b->state.application.colors[1]==12 && b->state.bgi.color==9 && b->state.bgi.fill_color==15);
	string(&f,"I#C 0,+1:I#C 3,291"); CHECK(st_cterm_finish(f.terminal)==0 && b->state.application.colors[0]==65535 && b->state.application.colors[3]==14);
	string(&f,"I#C 1,"); CHECK(st_cterm_reset_terminal(f.terminal)==0 && b->state.application.colors[1]==12);
	string(&f,"I#C 1,"); local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS;
	CHECK(st_cterm_poll(f.terminal)==0 && f.bells==1 && b->state.application.colors[1]==0);
	string(&f,"I#C 2,"); f.reset_bell=true; local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS;
	CHECK(st_cterm_poll(f.terminal)==0 && f.bells==2 && b->state.application.colors[2]==13);
	string(&f,"I#R 3:I#g 3:I#C 1,5:"); CHECK(b->state.application.colors[1]==5 && b->state.bgi.color==15);
	string(&f,"I#& 0,1,1,0,C@2,1,291:"); CHECK(b->state.application.colors[1]==291);
	string(&f,"I#& 0,1,1,0,C@1,2:5:"); CHECK(b->state.application.colors[2]==5 && f.size==4 && memcmp(f.output,"\x13\x11\x13\x11",4)==0);
	string(&f,"I#& 0,3,1,0,C@2,x,291:");
	for (size_t i=0;i<4;++i) CHECK(b->state.application.colors[i]==291);
	CHECK(b->numeric.loop.active==0 && b->numeric.loop.current==3 && f.size==6 && memcmp(f.output+4,"\x13\x11",2)==0);
	b->control.input.pending=1; b->control.input.count=2; b->control.input.payload[1]=0; b->control.input.payload[2]=255; b->control.input.append_cr=1;
	string(&f,"I#C>1,9:"); CHECK(f.size==6 && b->control.input.pending==1 && b->state.bgi.color==15);
	string(&f,"~ "); CHECK(f.size==9 && memcmp(f.output+6,"\0\xff\r",3)==0);
	string(&f,"I#C 0,261:I#C 2,31:I#C 3,+1:I#I 1:I#R 1:");
	const uint16_t retained[]={261,9,31,65535}; CHECK(memcmp(b->state.application.colors,retained,sizeof(retained))==0);
	CHECK(b->state.bgi.color==15 && b->state.bgi.fill_color==15 && b->state.bgi.error==-11);
	string(&f,"I#I 0:"); for (size_t i=0;i<4;++i) CHECK(b->state.application.colors[i]==15);
	b->state.bgi.state=0; b->state.bgi.color=b->state.bgi.device_color=9;
	string(&f,"I#C 1,8:"); CHECK(b->state.application.colors[1]==8 && b->state.bgi.color==9 && b->state.bgi.device_color==9 && b->state.bgi.error==-11);
	canvas(&f,NULL,0); CHECK(f.size==9 && f.terminal->xpos==1);
	destroy(&f);
}
int main(void)
{
	partitions_and_profiles(); color_and_waiting_prefix(); repeat_and_deferred_reply(); clipping_and_thickness();
	sources_scaling_and_deadlines(); point_owners_xor_and_failures(); move_state_and_origin(); move_sources_and_replies(); move_cancellation_and_failure();
	writing_mode_partitions(); writing_mode_sources_and_failures(); color_partitions(); color_prefixes_sources_and_lifetime(); return 0;
}
