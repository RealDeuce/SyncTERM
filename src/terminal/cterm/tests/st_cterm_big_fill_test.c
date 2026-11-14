/* Independent BIG A state/wire expectations from chapters10/11/50/54/83. */
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
	unsigned ready_calls;
};
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
	size_t used; int status=st_cterm_write(f->terminal,wire,size,&used);
	if (status!=0 || used!=size) fprintf(stderr,"feed status=%d used=%zu/%zu wire=%.*s\n",status,used,size,(int)size,(const char *)wire);
	CHECK(status==0 && used==size);
}
static void string(struct fixture *f, const char *wire) { feed(f,wire,strlen(wire)); }
static void blank(struct fixture *f)
{
	const struct st_console_rect r={0,0,640,f->terminal->big_engine->state.bgi.height}; struct st_pixel_buffer *p=NULL;
	CHECK(st_console_get_pixels(&p,f->console,&r,false)==0);
	for (size_t i=0;i<(size_t)r.width*r.height;++i) CHECK(p->primary[i]==800 && p->alternate[i]==800);
	st_pixel_buffer_destroy(p);
}
static void partitions(void)
{
	static const struct { const char *wire; uint16_t selected,style,color; int error; } cases[]={
		{"I#C>2,31:A 3:\033[2;3H",3,3,31,0},
		{"I#C>2,5:A 12:\033[2;3H",12,12,5,0},
		{"I#C>2,31:A 12:\033[2;3H",12,1,15,-11},
		{"I#C>2,256:A 258:\033[2;3H",1,1,256,0},
	};
	for (unsigned utf8=0;utf8<2;++utf8) for (unsigned status=0;status<2;++status) {
		struct fixture f; create(&f,utf8,status); struct cterm_big *b=f.terminal->big_engine;
		for (unsigned mode=1;mode<=3;++mode) for (size_t c=0;c<sizeof(cases)/sizeof(cases[0]);++c) {
			const size_t size=strlen(cases[c].wire);
			/* All partitions at the default profile, bytewise in all profiles. */
			size_t splits=mode==2 && !utf8 && !status?size+1:0;
			for (size_t split=0;split<=splits;++split) {
				char reset[16]; snprintf(reset,sizeof(reset),"I#R %u:",mode); string(&f,reset); b->state.bgi.error=0;
				if (split==splits) for (size_t i=0;i<size;++i) feed(&f,cases[c].wire+i,1);
				else { feed(&f,cases[c].wire,split); string(&f,cases[c].wire+split); }
				CHECK(b->state.application.fill==cases[c].selected && b->state.bgi.fill_style==cases[c].style && b->state.bgi.fill_color==cases[c].color && b->state.bgi.error==cases[c].error);
				CHECK(b->state.bgi.x==0 && b->state.bgi.ddo_x==0 && b->state.bgi.device_x==0 && b->state.bgi.color==15);
				CHECK(f.terminal->xpos==3 && f.terminal->ypos==2 && f.size==0); blank(&f);
			}
		}
		destroy(&f);
	}
}
static void prefixes_sources_and_errors(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#A 12"); CHECK(st_cterm_finish(f.terminal)==0 && b->state.application.fill==1 && b->state.bgi.fill_style==1);
	string(&f,"I#C 2,31:I#A "); CHECK(st_cterm_reset_terminal(f.terminal)==0 && b->state.application.fill==1);
	string(&f,"I#A "); local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS;
	CHECK(st_cterm_poll(f.terminal)==0 && f.bells==1 && b->state.application.fill==0 && b->state.bgi.fill_style==0 && b->state.bgi.fill_color==31);
	string(&f,"I#A 3:I#A "); f.reset_bell=true; local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS;
	CHECK(st_cterm_poll(f.terminal)==0 && f.bells==2 && b->state.application.fill==3);
	string(&f,"I#R 3:I#g 3:I#C 2,5:I#A 3:"); CHECK(b->state.application.fill==3 && b->state.bgi.fill_style==3);
	string(&f,"I#& 0,1,1,0,A@1,12:"); CHECK(b->state.application.fill==12 && b->state.bgi.fill_style==12 && b->state.bgi.fill_color==5 && f.size==0);
	string(&f,"I#& 0,3,1,0,A@1,x:"); CHECK(b->state.application.fill==3 && b->state.bgi.fill_style==3 && b->numeric.loop.active==0 && f.size==0);
	b->control.input.pending=1; b->control.input.count=2; b->control.input.payload[1]=0; b->control.input.payload[2]=255; b->control.input.append_cr=1;
	string(&f,"I#C>2,31:A>12:"); CHECK(b->state.application.fill==12 && b->state.bgi.fill_style==3 && b->state.bgi.fill_color==5 && b->state.bgi.error==-11 && f.size==0);
	string(&f,"~ "); CHECK(f.size==3 && memcmp(f.output,"\0\xff\r",3)==0);
	string(&f,"I#C>2,5:A 12:I#I 1:"); CHECK(b->state.bgi.fill_style==12 && b->state.bgi.fill_color==5 && b->state.bgi.error==-11);
	CHECK(b->state.bgi.fill_user[0]==0xaa && b->state.bgi.device_fill_user[0]==0xaa && b->state.bgi.fill_pattern[0]==0xaa && b->state.bgi.fill_pixels[0]==5 && b->state.bgi.fill_pixels[1]==0);
	string(&f,"I#R 1:"); CHECK(b->state.application.fill==12 && b->state.application.colors[2]==5 && b->state.bgi.fill_style==1 && b->state.bgi.fill_color==15);
	for (size_t i=0;i<8;++i) CHECK(b->state.bgi.fill_user[i]==255 && b->state.bgi.device_fill_user[i]==255 && b->state.bgi.fill_pattern[i]==255);
	for (size_t i=0;i<64;++i) CHECK(b->state.bgi.fill_pixels[i]==15);
	string(&f,"I#I 0:"); CHECK(b->state.application.fill==1 && b->state.application.colors[2]==15);
	b->state.bgi.state=0; string(&f,"I#C 2,31:I#A ");
	const char builtin[]="3:"; size_t used;
	CHECK(st_cterm_write(f.terminal,builtin,sizeof(builtin)-1,&used)==0 && b->last_error==IO_ERROR_PROTOCOL && used==sizeof(builtin)-1);
	CHECK(b->state.application.fill==3 && b->state.bgi.fill_style==3 && b->state.bgi.fill_color==31 && b->state.bgi.error==-1);
	CHECK(b->state.bgi.fill_low==31 && b->state.bgi.device_fill_style==1 && b->state.bgi.device_fill_color==15 && b->state.bgi.fill_pixels[0]==15);
	const char custom[]="I#A 12:";
	CHECK(st_cterm_write(f.terminal,custom,sizeof(custom)-1,&used)==0 && b->last_error==IO_ERROR_PROTOCOL && used==sizeof(custom)-1);
	CHECK(b->state.application.fill==12 && b->state.bgi.fill_style==3 && b->state.bgi.fill_color==31 && b->state.bgi.error==-1);
	b->state.bgi.state=3; string(&f,"I#C>2,5:A 12:"); CHECK(b->state.bgi.fill_style==12 && b->state.bgi.fill_color==5 && b->state.bgi.error==-1);
	blank(&f); CHECK(f.size==3); destroy(&f);
}
static void edit_partitions(void)
{
	static const uint8_t default12[]={0xaa,0x55,0xaa,0x55,0xaa,0x55,0xaa,0x55};
	static const struct { const char *wire; unsigned slot; uint8_t rows[8]; } cases[]={
		{"I#X 1,12,1,128,0,0,0,0,0,0,1:\033[2;3H",0,{128,0,0,0,0,0,0,1}},
		{"I#X 1,269,1,257,258,+1,0,x,291,511,264:\033[2;3H",0,{1,2,255,0,1,35,255,8}},
		{"I#X 1,12,257:\033[2;3H",0,{0xaa,0x55,0xaa,0x55,0xaa,0x55,0xaa,0x55}},
		{"I#X 1,23,0:\033[2;3H",11,{0x20,0x50,0x8c,0x42,0x22,0x22,0x1e,0}},
	};
	for (unsigned utf8=0;utf8<2;++utf8) for (unsigned status=0;status<2;++status) {
		struct fixture f; create(&f,utf8,status); struct cterm_big *b=f.terminal->big_engine;
		for (unsigned mode=1;mode<=3;++mode) for (size_t c=0;c<sizeof(cases)/sizeof(cases[0]);++c) {
			size_t size=strlen(cases[c].wire), splits=mode==2 && !utf8 && !status?size+1:0;
			for (size_t split=0;split<=splits;++split) {
				char reset[16]; snprintf(reset,sizeof(reset),"I#R %u:",mode); string(&f,reset);
				memcpy(b->state.application.patterns,st_big_fill_restore,96);
				memset(b->state.application.patterns[cases[c].slot],0xcc,8);
				if (split==splits) for (size_t i=0;i<size;++i) feed(&f,cases[c].wire+i,1);
				else { feed(&f,cases[c].wire,split); string(&f,cases[c].wire+split); }
				CHECK(memcmp(b->state.application.patterns[cases[c].slot],cases[c].rows,8)==0);
				for (size_t slot=0;slot<12;++slot) if (slot!=cases[c].slot) CHECK(memcmp(b->state.application.patterns[slot],st_big_fill_restore[slot],8)==0);
				CHECK(b->state.application.fill==1 && b->state.bgi.fill_style==1 && b->state.bgi.fill_color==15 && b->state.bgi.color==15 && b->state.bgi.error==0);
				CHECK(b->state.bgi.x==0 && b->state.bgi.ddo_x==0 && b->state.bgi.device_x==0 && f.terminal->xpos==3 && f.terminal->ypos==2 && f.size==0);
				for (size_t i=0;i<8;++i) CHECK(b->state.bgi.fill_user[i]==255 && b->state.bgi.device_fill_user[i]==255);
				blank(&f);
			}
		}
		CHECK(memcmp(st_big_fill_restore[0],default12,8)==0); destroy(&f);
	}
}
static void edit_prefixes_and_sources(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	memset(b->state.application.patterns[0],0xcc,8);
	string(&f,"I#X 1,9,"); CHECK(st_cterm_finish(f.terminal)==0 && b->state.application.patterns[0][0]==0xcc);
	string(&f,"I#X 1,12,0:I#X 1,12,1,257,258,");
	CHECK(b->state.application.patterns[0][0]==1 && b->state.application.patterns[0][1]==2 && b->state.application.patterns[0][2]==0xaa);
	CHECK(st_cterm_finish(f.terminal)==0 && b->state.application.patterns[0][0]==1 && b->state.application.patterns[0][1]==2);
	string(&f,"I#X 1,12,0:I#X 1,12,1,3,"); f.reset_bell=true; local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS;
	CHECK(st_cterm_poll(f.terminal)==0 && f.bells==1 && b->state.application.patterns[0][0]==3 && b->state.application.patterns[0][1]==0x55);
	f.reset_bell=false; string(&f,"I#X 1,12,1,4,");
	for (unsigned row=1;row<8;++row) { local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS; CHECK(st_cterm_poll(f.terminal)==0); }
	CHECK(f.bells==8 && b->state.application.patterns[0][0]==4);
	for (size_t i=1;i<8;++i) CHECK(b->state.application.patterns[0][i]==0);
	string(&f,"I#R 3:I#g 3:I#& 0,1,1,0,X@3,1,12,1:257,258,259,260,261,262,263,264:");
	for (size_t i=0;i<8;++i) CHECK(b->state.application.patterns[0][i]==i+1);
	string(&f,"I#& 0,1,1,0,X@4,1,12,1,511:");
	CHECK(b->fill_edit.active && b->fill_edit.row==1 && b->state.application.patterns[0][0]==255 && b->numeric.loop.active==0);
	string(&f,"258,259,260,261,262,263,264:");
	for (size_t i=1;i<8;++i) CHECK(b->state.application.patterns[0][i]==i+1);
	string(&f,"I#& 0,1,1,0,X@11,1,12,1,128,0,0,0,0,0,0,1:");
	CHECK(b->state.application.patterns[0][0]==128 && b->state.application.patterns[0][7]==1);
	string(&f,"I#& 0,2,1,0,X@11,1,12,1,x,x,x,x,x,x,x,x:");
	for (size_t i=0;i<8;++i) CHECK(b->state.application.patterns[0][i]==2);
	CHECK(b->numeric.loop.active==0 && f.size==0);
	b->control.input.pending=1; b->control.input.count=1; b->control.input.payload[1]='R';
	string(&f,"I#X>1,12,0:"); CHECK(f.size==0 && b->control.input.pending==1);
	string(&f,"~ "); CHECK(f.size==1 && f.output[0]=='R');
	string(&f,"I#X 1,12,1,128,0,0,0,0,0,0,1:I#C>2,5:A 12:");
	CHECK(b->state.bgi.fill_pixels[0]==5 && b->state.bgi.fill_pixels[63]==5);
	string(&f,"I#X 1,12,0:"); CHECK(b->state.application.patterns[0][0]==0xaa && b->state.bgi.fill_user[0]==128 && b->state.bgi.fill_pixels[63]==5);
	string(&f,"I#A 12:"); CHECK(b->state.bgi.fill_user[0]==0xaa && b->state.bgi.fill_pixels[0]==5 && b->state.bgi.fill_pixels[1]==0);
	string(&f,"I#X 1,12,1,1,2,3,4,5,6,7,8:I#I 1:I#R 1:I#I 0:");
	for (size_t i=0;i<8;++i) CHECK(b->state.application.patterns[0][i]==i+1);
	b->state.bgi.state=0; b->state.bgi.error=-11; string(&f,"I#X 1,12,0:");
	CHECK(b->state.application.patterns[0][0]==0xaa && b->state.bgi.error==-11 && b->state.bgi.fill_style==1);
	blank(&f); CHECK(f.size==1); destroy(&f);
}
static void edit_live_frame(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#X 1,12,1,1,"); CHECK(b->fill_edit.active && b->fill_edit.row==1);
	b->fill_edit.slot=13; b->fill_edit.alter=0;
	string(&f,"2,3,4,5,6,7,8:");
	CHECK(!b->fill_edit.active && memcmp(b->state.application.patterns[1],st_big_fill_restore[1],8)==0);
	CHECK(b->state.application.patterns[0][0]==1 && b->state.application.patterns[0][1]==0x55);
	string(&f,"I#X 1,12,1,9,"); b->fill_edit.slot=24;
	const char bad[]="10,TAIL"; size_t used;
	CHECK(st_cterm_write(f.terminal,bad,sizeof(bad)-1,&used)==0 && b->last_error==IO_ERROR_OUT_OF_RANGE && used==sizeof(bad)-1);
	CHECK(!b->fill_edit.active && b->state.application.patterns[0][0]==9 && b->state.application.patterns[0][1]==0x55 && b->state.bgi.error==0);
	string(&f,"I#X 1,12,0:"); CHECK(memcmp(b->state.application.patterns[0],st_big_fill_restore[0],8)==0);
	string(&f,"\033[2J"); blank(&f); CHECK(f.size==0); destroy(&f);
}
static void flood_scene(struct fixture *f, unsigned mode, unsigned border)
{
	char wire[160];
	snprintf(wire,sizeof(wire),"I#R %u:I#A 1:I#g 0:I#m 2,31,32:I#C 1,%u:I#C 2,3:I#B 10,10,14,14,1:I#C 0,271:I#C 1,271:I#C 2,5:I#C 3,271:",mode,border);
	string(f,wire); f->size=0;
	f->terminal->big_engine->state.application.hollow=1;
}
static void flood_pixels(struct fixture *f, unsigned border, unsigned inside)
{
	const struct st_console_rect r={0,0,640,f->terminal->big_engine->state.bgi.height};
	struct st_pixel_buffer *p=NULL; CHECK(st_console_get_pixels(&p,f->console,&r,false)==0);
	for (unsigned y=0;y<r.height;++y) for (unsigned x=0;x<r.width;++x) {
		unsigned pen=0;
		if (x>=10 && x<=14 && y>=10 && y<=14) pen=x==10 || x==14 || y==10 || y==14?border:inside;
		size_t i=(size_t)y*r.width+x;
		CHECK(p->primary[i]==800+pen && p->alternate[i]==800+pen);
	}
	st_pixel_buffer_destroy(p);
}
static void flood_partitions(void)
{
	static const char *selectors[]={"0","1","2","3","9","+1"};
	for (unsigned utf8=0;utf8<2;++utf8) for (unsigned status=0;status<2;++status) {
		struct fixture f; create(&f,utf8,status); struct cterm_big *b=f.terminal->big_engine;
		for (unsigned mode=1;mode<=3;++mode) for (size_t c=0;c<6;++c) {
			char wire[48]; snprintf(wire,sizeof(wire),"I#F 12,12,%s:\033[2;3H",selectors[c]);
			size_t size=strlen(wire), splits=mode==2 && !utf8 && !status?size+1:0;
			for (size_t split=0;split<=splits;++split) {
				unsigned border=c==2?5:15; flood_scene(&f,mode,border);
				b->state.bgi.error=-11;
				if (split==splits) for (size_t i=0;i<size;++i) feed(&f,wire+i,1);
				else { feed(&f,wire,split); string(&f,wire+split); }
				flood_pixels(&f,border,5);
				CHECK(f.size==0);
				CHECK(b->state.bgi.fill_style==1 && b->state.bgi.fill_color==5 && b->state.bgi.color==border && b->state.bgi.error==-11);
				CHECK(b->state.bgi.x==31 && b->state.bgi.y==32 && b->state.bgi.ddo_x==14 && b->state.bgi.ddo_y==14 && b->state.bgi.device_x==14 && b->state.bgi.device_y==14);
				CHECK(f.terminal->xpos==3 && f.terminal->ypos==2);
			}
		}
		destroy(&f);
	}
}
static int flood_ready(void *context, enum st_big_output_kind kind)
{
	struct cterm_big *b=context; struct fixture *f=b->terminal->api_output_context;
	(void)kind; ++f->ready_calls; return IO_ERROR_IO;
}
static void flood_waits(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	b->control_io.ready=flood_ready;
	for (unsigned mode=0;mode<4;++mode) {
		flood_scene(&f,2,15); b->control.flow.mode=(uint16_t)mode;
		b->control.flow.count[0]=b->control.flow.count[1]=32767;
		b->control.flow.byte[0]=0; b->control.flow.byte[1]=255;
		string(&f,"I#F 12,12,0:");
		CHECK(!b->flood.active && f.size==0 && f.ready_calls==0 && b->last_error==0);
		flood_pixels(&f,15,5);
		CHECK(st_cterm_finish(f.terminal)==0 && st_cterm_reset_terminal(f.terminal)==0 && f.size==0);
		flood_pixels(&f,15,5);
	}
	destroy(&f);
}
struct corrupt_pixel { unsigned x,y; uint32_t a,b; };
static int corrupt(void *context, const struct st_pixel_buffer *p, struct st_console_rect *r)
{
	const struct corrupt_pixel *c=context; size_t i=(size_t)c->y*p->width+c->x;
	p->primary[i]=c->a; p->alternate[i]=c->b; *r=(struct st_console_rect){c->x,c->y,1,1}; return 0;
}
static void flood_prefixes_errors_sources(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	flood_scene(&f,2,15); string(&f,"I#F 12,12,");
	CHECK(st_cterm_finish(f.terminal)==0 && b->state.bgi.fill_color==3 && f.size==0); flood_pixels(&f,15,3);
	string(&f,"I#F 12,12,"); local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS;
	CHECK(st_cterm_poll(f.terminal)==0 && f.bells==1 && f.size==0 && b->state.bgi.fill_color==5); flood_pixels(&f,15,5);
	flood_scene(&f,2,15); string(&f,"I#F 12,12,"); f.reset_bell=true; local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS;
	CHECK(st_cterm_poll(f.terminal)==0 && !b->flood.active && b->state.bgi.fill_color==3 && f.size==0); flood_pixels(&f,15,3);
	f.reset_bell=false;
	/* Rejected custom attributes retain the previous fill but do not suppress F. */
	flood_scene(&f,2,15); string(&f,"I#C 2,31:I#A 12:I#F 12,12,0:");
	CHECK(f.size==0 && b->state.bgi.error==-11 && b->state.bgi.fill_style==1 && b->state.bgi.fill_color==3); flood_pixels(&f,15,3);
	flood_scene(&f,2,15); b->state.bgi.state=0;
	const char wire[]="I#F 12,12,0:"; size_t used;
	CHECK(st_cterm_write(f.terminal,wire,sizeof(wire)-1,&used)==0 && b->last_error==IO_ERROR_PROTOCOL && used==sizeof(wire)-1);
	CHECK(f.size==0 && b->state.bgi.error==-1 && b->state.bgi.fill_color==5); flood_pixels(&f,15,3); b->state.bgi.state=3;
	/* Bounded exhaustion completes the accepted seed span without wire flow. */
	flood_scene(&f,2,15); b->flood_work.capacity=0; string(&f,"I#F 12,12,0:");
	CHECK(f.size==0 && b->flood_work.overflow && b->state.bgi.error==-7);
	const struct st_console_rect r={10,10,5,5}; struct st_pixel_buffer *p=NULL;
	CHECK(st_console_get_pixels(&p,f.console,&r,false)==0);
	for (unsigned y=0;y<5;++y) for (unsigned x=0;x<5;++x) {
		unsigned pen=x==0 || x==4 || y==0 || y==4?15:y==2?5:3;
		CHECK(p->primary[y*5+x]==800+pen && p->alternate[y*5+x]==800+pen);
	}
	st_pixel_buffer_destroy(p); b->flood_work.capacity=CTERM_BIG_FLOOD_RECORDS;
	/* A host storage error recovers without wire flow bytes. */
	flood_scene(&f,2,15); const struct corrupt_pixel c={8,12,42,42};
	CHECK(st_console_modify_pixels(f.console,corrupt,(void *)&c)==0);
	CHECK(st_cterm_write(f.terminal,wire,sizeof(wire)-1,&used)==0 && b->last_error==IO_ERROR_OUT_OF_RANGE && used==sizeof(wire)-1);
	CHECK(f.size==0 && !b->flood.active && b->state.bgi.error==-7);
	flood_scene(&f,2,15); string(&f,wire); CHECK(f.size==0); flood_pixels(&f,15,5);
	/* A supplied Y is unscaled; source exhaustion retains the normal result. */
	flood_scene(&f,3,15); string(&f,"I#B 10,3,14,7,1:I#g 3:"); f.size=0;
	string(&f,"I#& 0,1,1,0,F@3,12,5,0:");
	CHECK(f.size==0 && b->numeric.loop.active==0);
	const struct st_console_rect two={12,5,1,8}; CHECK(st_console_get_pixels(&p,f.console,&two,false)==0);
	CHECK(p->primary[0]==805 && p->alternate[0]==805 && p->primary[7]==803 && p->alternate[7]==803); st_pixel_buffer_destroy(p);
	flood_scene(&f,2,15); string(&f,"I#& 0,2,1,0,F@3,12,12,0:");
	CHECK(f.size==0 && b->numeric.loop.active==0 && b->numeric.loop.current==2); flood_pixels(&f,15,5);
	flood_scene(&f,2,15); b->control.input.pending=1; b->control.input.count=2; b->control.input.payload[1]=0; b->control.input.payload[2]=255;
	string(&f,"I#F>12,12,0:"); CHECK(f.size==0 && b->control.input.pending==1);
	string(&f,"~ "); CHECK(f.size==2 && f.output[0]==0 && f.output[1]==255);
	/* Signed-negative coordinates clamp before the flood request. */
	flood_scene(&f,2,15); string(&f,"I#C 2,15:I#B 0,0,0,0,0:I#C 2,5:I#F +1,+2,0:");
	CHECK(f.size==0 && b->state.bgi.error==-7 && b->state.bgi.fill_color==5);
	destroy(&f);
}
static void flood_native_byte_planes(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	flood_scene(&f,2,15); string(&f,"I#C 1,15:I#C 2,3:I#B 10,10,35,14,1:I#C 2,5:");
	/* One byte reaches all four plane-mask passes. Unselected alternate
	 * pixels are neither validated as primary reads nor overwritten. */
	const struct corrupt_pixel inside={20,12,803,814}, outside={8,12,800,42};
	CHECK(st_console_modify_pixels(f.console,corrupt,(void *)&inside)==0);
	CHECK(st_console_modify_pixels(f.console,corrupt,(void *)&outside)==0);
	string(&f,"I#F 12,12,0:"); CHECK(f.size==0 && b->last_error==0);
	const struct st_console_rect r={8,10,28,5}; struct st_pixel_buffer *p=NULL;
	CHECK(st_console_get_pixels(&p,f.console,&r,false)==0);
	for (unsigned y=0;y<5;++y) for (unsigned x=0;x<28;++x) {
		unsigned pen=x<2?0:x==2 || x==27 || y==0 || y==4?15:5;
		size_t i=y*28+x;
		CHECK(p->primary[i]==800+pen);
		CHECK(p->alternate[i]==(x==0 && y==2?42:800+pen));
	}
	st_pixel_buffer_destroy(p); destroy(&f);
}
int main(void) { flood_partitions(); flood_waits(); flood_prefixes_errors_sources(); flood_native_byte_planes(); partitions(); prefixes_sources_and_errors(); edit_partitions(); edit_prefixes_and_sources(); edit_live_frame(); return 0; }
