/* Independent BIG G page routing and indexed pixels, reference25/48/56. */
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
	CHECK(st_console_create(&f->console,&display,&geometry)==0);
	CHECK(st_font_catalog_create(&f->fonts,f->console)==0);
	const struct st_cterm_config config={.console=f->console,.fonts=f->fonts,
	    .emulation=ST_CTERM_ANSI_BBS,.graphics_mode=ST_CTERM_GRAPHICS_BIG_103,
	    .encoding=utf8?ST_CTERM_ENCODING_UTF8:ST_CTERM_ENCODING_CODEPAGE,.codepage=ST_CODEPAGE_CP437,
	    .quiet=true,.status_line=status,.scrollback_lines=2,.output=output,.output_context=f};
	CHECK(st_cterm_create(&f->terminal,&config)==0);
	f->terminal->big_engine->numeric.io.now_ns=now;
}
static void destroy(struct fixture *f)
{
	st_cterm_destroy(&f->terminal); st_font_catalog_destroy(f->fonts); st_console_destroy(f->console);
}
static void feed(struct fixture *f, const void *bytes, size_t size)
{
	size_t used; CHECK(st_cterm_write(f->terminal,bytes,size,&used)==0 && used==size);
}
static void string(struct fixture *f, const char *bytes) { feed(f,bytes,strlen(bytes)); }
struct dot { unsigned x,y; uint32_t primary,alternate; };
static void canvas(struct fixture *f, const struct dot *dots, size_t count)
{
	struct cterm_big *b=f->terminal->big_engine;
	const struct st_console_rect r={0,0,640,b->state.bgi.height}; struct st_pixel_buffer *p=NULL;
	CHECK(st_console_get_pixels(&p,f->console,&r,false)==0);
	for (unsigned y=0;y<r.height;++y) for (unsigned x=0;x<r.width;++x) {
		uint32_t a=800,b=800; size_t i=(size_t)y*r.width+x;
		for (size_t n=0;n<count;++n) if (dots[n].x==x && dots[n].y==y) { a=dots[n].primary; b=dots[n].alternate; }
		if (p->primary[i]!=a || p->alternate[i]!=b) fprintf(stderr,"page pixel%u,%u got%u/%u expected%u/%u\n",x,y,p->primary[i],p->alternate[i],a,b);
		CHECK(p->primary[i]==a && p->alternate[i]==b);
	}
	st_pixel_buffer_destroy(p);
}
static void scene(struct fixture *f, unsigned pen)
{
	const struct dot dot={20,20,800+pen,800+pen}; canvas(f,&dot,1);
}
static void partitions(void)
{
	const char wire[]="I#G>4,1:G>6,1:P>20,20:G 5,1:\033[2;3H";
	for (unsigned utf8=0;utf8<2;++utf8) for (unsigned status=0;status<2;++status) {
		struct fixture f; create(&f,utf8,status); struct cterm_big *b=f.terminal->big_engine;
		for (unsigned mode=1;mode<=3;++mode) {
			size_t size=sizeof(wire)-1,splits=mode==2 && !utf8 && !status?size+1:0;
			for (size_t split=0;split<=splits;++split) {
				char reset[64]; snprintf(reset,sizeof(reset),"I#R %u:I#C 0,5:",mode); string(&f,reset);
				if (split==splits) for (size_t i=0;i<size;++i) feed(&f,wire+i,1);
				else { feed(&f,wire,split); string(&f,wire+split); }
				unsigned page=mode==3?0:1;
				CHECK(b->state.memory_page==page && b->state.active_page==page && b->state.visual_page==page);
				CHECK(b->state.bgi.active_page==page && b->state.bgi.visual_page==page && b->last_error==0);
				CHECK(b->state.bgi.x==20 && b->state.bgi.y==20 && b->state.bgi.ddo_x==20 && b->state.bgi.device_x==20);
				CHECK(f.terminal->xpos==3 && f.terminal->ypos==2 && f.size==0); scene(&f,5);
			}
		}
		destroy(&f);
	}
}
static void active_visual_and_consumers(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#R 2:I#C 0,5:I#G 6,1:"); scene(&f,0);
	CHECK(b->state.active_page==1 && b->video_active_page==1 && b->video_visual_page==0);
	string(&f,"I#P 20,20:"); scene(&f,0);
	CHECK(b->state.memory_page==1 && b->state.active_page==0 && b->state.visual_page==0 && b->video_active_page==1);
	string(&f,"I#G 5,1:"); scene(&f,5);
	CHECK(b->video_start==0x8000 && b->video_active_page==1 && b->video_visual_page==1);
	string(&f,"I#G 5,0:"); scene(&f,0);
	/* Primary/alternate XOR reads the active hidden page, not visible0. */
	size_t index=262144+20*640+20;
	b->video.primary[index]=803; b->video.alternate[index]=806;
	string(&f,"I#C 1,5:I#M 2:I#L 20,20,20,20:I#G 5,1:");
	const struct dot xor_dot={20,20,806,803}; canvas(&f,&xor_dot,1);
	CHECK(b->state.bgi.write_mode==1 && b->state.bgi.x==20 && b->state.bgi.ddo_x==20);
	string(&f,"I#G 5,0:"); scene(&f,0);
	/* B, F, Z and standalone lines use the same addressed drawing sink. */
	string(&f,"I#R 2:I#C 0,6:I#C 1,7:I#C 2,3:I#G 6,1:I#B 40,40,44,44,1:"); scene(&f,0);
	string(&f,"I#C 2,5:I#F 42,42,1:I#P 20,20:I#L 50,40,52,40:I#D 54,40:I#d 0,0,1,2:I#H 0:I#Z 60,40,64,44:"); scene(&f,0);
	struct dot expected[64]; size_t n=0;
	for (unsigned y=40;y<=44;++y) for (unsigned x=40;x<=44;++x) expected[n++]=(struct dot){x,y,x==40 || x==44 || y==40 || y==44?807:805,x==40 || x==44 || y==40 || y==44?807:805};
	for (unsigned y=40;y<=44;++y) for (unsigned x=60;x<=64;++x) expected[n++]=(struct dot){x,y,x==60 || x==64 || y==40 || y==44?807:805,x==60 || x==64 || y==40 || y==44?807:805};
	expected[n++]=(struct dot){20,20,806,806};
	for (unsigned x=50;x<=54;++x) expected[n++]=(struct dot){x,40,807,807};
	for (unsigned y=41;y<=42;++y) expected[n++]=(struct dot){54,y,807,807};
	string(&f,"I#G 5,1:"); canvas(&f,expected,n);
	CHECK(b->state.bgi.x==54 && b->state.bgi.y==42 && b->state.bgi.ddo_x==54 && b->state.bgi.device_x==54 && b->last_error==0);
	CHECK(f.size==0);
	string(&f,"I#G 6,0:I#G 5,0:"); scene(&f,0);
	string(&f,"I#G 5,1:"); canvas(&f,expected,n);
	string(&f,"I#I 1:"); canvas(&f,expected,n); CHECK(b->video_active_page==0 && b->video_visual_page==1);
	string(&f,"I#R 2:I#G 5,1:"); scene(&f,0);
	CHECK(b->video_active_page==0 && b->video_visual_page==1);
	destroy(&f);
}
static void lifetimes_and_limits(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#R 1:I#G>4,3:G>6,2:G 5,1:");
	CHECK(b->state.memory_page==3 && b->state.active_page==2 && b->state.visual_page==1 && b->video_active_page==2 && b->video_visual_page==1);
	string(&f,"I#G 4,"); CHECK(b->state.memory_page==1 && b->video_active_page==2 && b->video_visual_page==1);
	CHECK(st_cterm_finish(f.terminal)==0 && b->video_active_page==2 && b->video_visual_page==1);
	string(&f,"I#G 6,"); CHECK(st_cterm_reset_terminal(f.terminal)==0 && b->video_active_page==2 && b->video_visual_page==1);
	string(&f,"I#G 4,"); local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS;
	CHECK(st_cterm_poll(f.terminal)==0 && b->state.memory_page==1 && b->video_active_page==2);
	b->numeric.context.resolution=3; b->state.memory_page=3; b->state.active_page=2; b->state.visual_page=1;
	string(&f,"I#G 4,"); CHECK(b->state.memory_page==0 && b->state.active_page==0 && b->state.visual_page==0);
	b->numeric.context.resolution=1; string(&f,"3:"); CHECK(b->state.memory_page==3 && b->video_visual_page==1);
	b->numeric.context.resolution=3; string(&f,"I#G 9:");
	CHECK(b->state.memory_page==0 && b->state.active_page==0 && b->state.visual_page==0 && b->video_active_page==2 && b->video_visual_page==1);
	/* Local publication precedes a failed public request. */
	b->numeric.context.resolution=1; b->state.bgi.state=0;
	string(&f,"I#G 6,3:"); CHECK(b->state.active_page==3 && b->video_active_page==2 && b->last_error==IO_ERROR_PROTOCOL && b->state.bgi.error==-1);
	b->state.bgi.state=3; string(&f,"I#G 6,0:"); CHECK(b->video_active_page==0);
	/* Actual mode12 keeps active9600 and visuala000 distinct from rez cache1. */
	string(&f,"I#R 3:"); b->numeric.context.resolution=1;
	string(&f,"I#C 0,5:I#G 6,1:I#P 0,32:I#G 5,1:");
	const struct dot shifted={0,0,805,805}; canvas(&f,&shifted,1);
	CHECK(b->video_start==0xa000 && b->state.bgi.x==0 && b->state.bgi.y==32);
	string(&f,"I#P 0,400:"); CHECK(b->last_error==IO_ERROR_OUT_OF_RANGE && b->video_active_page==1 && b->state.bgi.y==32);
	string(&f,"I#P 1,32:"); const struct dot recovered[]={{0,0,805,805},{1,0,805,805}}; canvas(&f,recovered,2);
	CHECK(st_cterm_finish(f.terminal)==0 && b->video_start==0xa000 && b->video_active_page==1);
	destroy(&f);
}
static void console_cursor_pages(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"\033[3;4HI#G 5,1:"); CHECK(f.terminal->xpos==1 && f.terminal->ypos==1);
	string(&f,"\033[5;6HI#G 5,0:"); CHECK(f.terminal->xpos==4 && f.terminal->ypos==3);
	string(&f,"I#G 5,1:"); CHECK(f.terminal->xpos==6 && f.terminal->ypos==5);
	/* Teletype uses the visual page even when graphics remain active0. */
	string(&f,"\033[1;1H"); const uint8_t block=219; feed(&f,&block,1);
	string(&f,"I#G 5,0:"); scene(&f,0);
	string(&f,"I#G 5,1:"); struct dot glyph[112]; size_t n=0;
	for (unsigned y=0;y<14;++y) for (unsigned x=0;x<8;++x) glyph[n++]=(struct dot){x,y,807,807};
	canvas(&f,glyph,n); CHECK(b->video_active_page==0 && f.terminal->xpos==2 && f.terminal->ypos==1);
	/* Mode10 BIOS display page2 aliases page0 while retaining another cursor. */
	b->numeric.context.resolution=1; string(&f,"I#G 5,2:"); scene(&f,0);
	CHECK(b->video_start==0 && b->video_visual_page==2 && f.terminal->xpos==1 && f.terminal->ypos==1);
	string(&f,"I#G 5,0:"); CHECK(f.terminal->xpos==4 && f.terminal->ypos==3);
	destroy(&f);
}
static int status_art(void *context, const struct st_pixel_buffer *p, struct st_console_rect *changed)
{
	(void)context;
	for (unsigned y=336;y<350;++y) for (unsigned x=0;x<640;++x) {
		p->primary[(size_t)y*640+x]=900; p->alternate[(size_t)y*640+x]=901;
	}
	*changed=(struct st_console_rect){0,336,640,14}; return 0;
}
static void status_and_prefix_failures(void)
{
	struct fixture f; create(&f,false,true); struct cterm_big *b=f.terminal->big_engine;
	CHECK(st_console_modify_pixels(f.console,status_art,NULL)==0);
	string(&f,"I#C 0,5:I#P 20,349:I#G 5,1:I#G 5,0:");
	const struct st_console_rect r={0,336,640,14}; struct st_pixel_buffer *p=NULL;
	CHECK(st_console_get_pixels(&p,f.console,&r,false)==0);
	for (size_t i=0;i<640*14;++i) CHECK(p->primary[i]==900 && p->alternate[i]==901);
	st_pixel_buffer_destroy(p);
	for (unsigned y=336;y<350;++y) for (unsigned x=0;x<640;++x) {
		size_t i=(size_t)y*640+x; unsigned expected=x==20 && y==349?805:800;
		CHECK(b->video.primary[i]==expected && b->video.alternate[i]==expected);
	}
	destroy(&f);
	create(&f,false,false); b=f.terminal->big_engine;
	string(&f,"I#G 6,1:I#G 6,0:"); b->video.count=10;
	string(&f,"I#G 5,1:"); CHECK(b->state.visual_page==1 && b->video_visual_page==0 && b->video_start==0 && b->last_error==IO_ERROR_OUT_OF_RANGE); scene(&f,0);
	string(&f,"I#C 0,5:I#P 20,20:"); scene(&f,5);
	string(&f,"I#R 2:I#C 2,3:I#B 0,0,5,0,0:I#G 6,1:");
	b->video.count=262147;
	for (size_t i=262144;i<262147;++i) { b->video.primary[i]=805; b->video.alternate[i]=806; }
	string(&f,"I#G 5,1:");
	CHECK(b->state.visual_page==1 && b->video_visual_page==1 && b->video_start==0x8000 && b->last_error==IO_ERROR_OUT_OF_RANGE);
	const struct dot prefix[]={{0,0,805,806},{1,0,805,806},{2,0,805,806},{3,0,803,803},{4,0,803,803},{5,0,803,803}};
	canvas(&f,prefix,6);
	CHECK(st_cterm_finish(f.terminal)==0); destroy(&f);
}
static void source_repetition_and_point_bytes(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#R 1:I#g 3:I#& 0,1,1,0,G@2,4,3:");
	CHECK(b->state.memory_page==3 && b->numeric.loop.active==0 && f.size==0);
	f.size=0; string(&f,"I#& 0,2,1,0,G@2,6,x:");
	CHECK(b->video_active_page==2 && b->state.active_page==2 && b->numeric.loop.current==2 && b->numeric.loop.active==0 && f.size==0);
	f.size=0; b->control.input.pending=1; b->control.input.count=2; b->control.input.payload[1]=0; b->control.input.payload[2]=255;
	string(&f,"I#G>5,1:"); CHECK(b->video_visual_page==1 && b->control.input.pending==1 && f.size==0);
	string(&f,"~ "); CHECK(f.size==2 && f.output[0]==0 && f.output[1]==255);
	f.size=0;
	for (unsigned plane=0;plane<4;++plane) {
		string(&f,"I#R 2:I#g 0:I#m 2,31,32:I#C 1,9:I#C 2,5:I#G 6,1:");
		b->producer.mode=2; b->producer.batch=true; b->producer.read_plane=(uint8_t)plane;
		string(&f,"I#B 10,10,14,14,4,0:"); scene(&f,0);
		CHECK(b->producer.video_segment==0xa800 && b->state.bgi.x==31 && b->state.bgi.y==32 && b->state.bgi.ddo_x==0xa800 && b->state.bgi.ddo_y==2);
		CHECK(b->state.bgi.device_x==(plane==0 || plane==3?0xa8e2:0xa800) && b->state.bgi.device_y==6);
	}
	b->producer.mode=0; b->producer.batch=false;
	destroy(&f);
}
static void normalization_wire(void)
{
	static const struct { const char *wire; unsigned value; } cases[]={
		{"0",0},{"1",1},{"2",2},{"3",3},{"4",4},{"256",256},{"257",257},{"258",258},{"259",259},{"9999",9999},{"+1",65535}};
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	for (unsigned mode=1;mode<=3;++mode) for (unsigned operation=4;operation<=6;++operation) for (size_t n=0;n<sizeof(cases)/sizeof(cases[0]);++n) {
		char reset[32],wire[32]; snprintf(reset,sizeof(reset),"I#R %u:I#g 3:",mode); string(&f,reset);
		snprintf(wire,sizeof(wire),"I#G %u,%s:",operation,cases[n].wire);
		for (size_t i=0;i<strlen(wire);++i) feed(&f,wire+i,1);
		unsigned page=operation==4?(cases[n].value>=1 && cases[n].value<=3?cases[n].value:1):(cases[n].value<=3?cases[n].value:0);
		if (mode==2 && page>1) page=operation==4?1:0;
		if (mode==3) page=0;
		CHECK((operation==4?b->state.memory_page:operation==5?b->state.visual_page:b->state.active_page)==page);
		CHECK(b->video_active_page==(operation==6?page:0) && b->video_visual_page==(operation==5?page:0));
		CHECK(b->last_error==0 && f.size==0 && b->state.bgi.x==0 && b->state.bgi.ddo_x==0 && b->state.bgi.device_x==0);
	}
	scene(&f,0); destroy(&f);
}
static void pointer_marker_page(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#G 6,1:I#C 0,5:I#T 1,4,1:I#M 2:");
	CHECK(cterm_big_pmarker(b,90,50)==0); scene(&f,0);
	struct dot border[32]; size_t n=0;
	for (unsigned y=46;y<=54;++y) for (unsigned x=86;x<=94;++x) {
		bool edge=x==86 || x==94 || y==46 || y==54, corner=(x==86 || x==94) && (y==46 || y==54);
		if (edge && !corner) border[n++]=(struct dot){x,y,805,805};
	}
	string(&f,"I#G 5,1:"); canvas(&f,border,n);
	CHECK(cterm_big_pmarker(b,90,50)==0); scene(&f,0);
	string(&f,"I#G 5,0:I#G 5,1:"); scene(&f,0);
	destroy(&f);
}
int main(void) { partitions(); active_visual_and_consumers(); lifetimes_and_limits(); console_cursor_pages(); status_and_prefix_failures(); source_repetition_and_point_bytes(); normalization_wire(); pointer_marker_page(); return 0; }
