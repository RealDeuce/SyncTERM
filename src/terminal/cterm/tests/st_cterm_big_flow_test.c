/* Native X5 acquisition/state/flow expectations, original BIG33/52. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm.h"
#include "cterm_big_internal.h"
#include "st_cterm.h"
#include "io_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
struct fixture { st_console_t *console; st_font_catalog_t *fonts; st_cterm_t *terminal; uint8_t output[512]; size_t size; };
static uint64_t local_ns;
static int clock_error;
static int now(void *context, uint64_t *value) { (void)context; *value=local_ns; return clock_error; }
static void output(void *context, enum st_cterm_output_origin origin, const uint8_t *bytes, size_t size)
{
	struct fixture *f=context; (void)origin; CHECK(size<=sizeof(f->output)-f->size);
	memcpy(f->output+f->size,bytes,size); f->size+=size;
}
static void create(struct fixture *f, bool utf8, bool status)
{
	memset(f,0,sizeof(*f)); local_ns=1000; clock_error=0;
	const struct st_console_display_config display={.backend=ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry={.columns=80,.rows=25,.cell_width=8,.cell_height=14};
	CHECK(st_console_create(&f->console,&display,&geometry)==0 && st_font_catalog_create(&f->fonts,f->console)==0);
	const struct st_cterm_config config={.console=f->console,.fonts=f->fonts,.emulation=ST_CTERM_ANSI_BBS,
	    .graphics_mode=ST_CTERM_GRAPHICS_BIG_103,.encoding=utf8?ST_CTERM_ENCODING_UTF8:ST_CTERM_ENCODING_CODEPAGE,
	    .codepage=ST_CODEPAGE_CP437,.quiet=true,.status_line=status,.scrollback_lines=2,.output=output,.output_context=f};
	CHECK(st_cterm_create(&f->terminal,&config)==0); f->terminal->big_engine->numeric.io.now_ns=now;
}
static void destroy(struct fixture *f) { st_cterm_destroy(&f->terminal); st_font_catalog_destroy(f->fonts); st_console_destroy(f->console); }
static void feed(struct fixture *f, const void *bytes, size_t size) { size_t used; int status=st_cterm_write(f->terminal,bytes,size,&used); CHECK((status==0 || status==IO_ERROR_IN_PROGRESS) && used==size); }
static void string(struct fixture *f, const char *wire) { feed(f,wire,strlen(wire)); }
static void poll(struct fixture *f) { CHECK(st_cterm_poll(f->terminal)==0); }
static void settings(struct fixture *f, uint16_t mode, uint8_t on, uint8_t off, uint16_t count_on, uint16_t count_off)
{
	const struct st_big_control *c=&f->terminal->big_engine->control;
	CHECK(c->flow.mode==mode && c->flow.byte[0]==on && c->flow.byte[1]==off && c->flow.count[0]==count_on && c->flow.count[1]==count_off);
}
static void partitions(void)
{
	static const char wire[]="I#X>5,2,256,2:X 5,3,511,2:\033[6n";
	for (unsigned utf8=0;utf8<2;++utf8) for (unsigned status=0;status<2;++status) {
		struct fixture f; create(&f,utf8,status); struct cterm_big *b=f.terminal->big_engine;
		for (unsigned res=1;res<=3;++res) for (size_t split=0;split<=sizeof(wire);++split) {
			if (res!=2 && split!=sizeof(wire)) continue;
			char setup[100]; snprintf(setup,sizeof(setup),"I#R %u:I#C>0,5:P>100,100:G 0,0,100,100,100,100,120,100:",res);
			string(&f,setup); b->state.bgi.error=-11; f.size=0;
			struct st_big_state before=b->state; before.memory_page=1; uint8_t image[10]; memcpy(image,b->image_bytes,10);
			if (split==sizeof(wire)) for (size_t i=0;i<sizeof(wire)-1;++i) feed(&f,wire+i,1);
			else { feed(&f,wire,split); feed(&f,wire+split,sizeof(wire)-1-split); }
			settings(&f,3,0,255,2,2);
			CHECK(b->last_error==0 && b->rejected_commands==0 && memcmp(&before,&b->state,sizeof(before))==0 && memcmp(image,b->image_bytes,10)==0);
			CHECK(f.size==6 && memcmp(f.output,"\033[1;1R",6)==0);
			uint32_t width,height; CHECK(st_console_pixel_size(f.console,&width,&height)==0);
			const struct st_console_rect area={0,0,width,height}; struct st_pixel_buffer *p=NULL;
			CHECK(st_console_get_pixels(&p,f.console,&area,false)==0);
			for (size_t i=0;i<(size_t)width*height;++i) CHECK(p->primary[i]==((i==100u*width+100u || i==100u*width+120u)?805u:800u) && p->alternate[i]==p->primary[i]);
			st_pixel_buffer_destroy(p); uint32_t rgb; CHECK(st_console_get_palette(f.console,805,&rgb,1)==0 && rgb==0xaa00aa);
		}
		destroy(&f);
	}
}
static void acquisition_and_lifetimes(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#X 5,"); settings(&f,1,17,19,1,1);
	string(&f,"2,"); settings(&f,2,17,19,1,1);
	string(&f,"300,"); settings(&f,2,44,19,1,1);
	CHECK(st_cterm_finish(f.terminal)==0); settings(&f,2,44,19,1,1);
	string(&f,"I#X 5,3,511,"); settings(&f,3,44,255,1,1);
	CHECK(st_cterm_reset_terminal(f.terminal)==0); settings(&f,3,44,255,1,1);
	string(&f,"I#I 0:I#R 3:I#I 1:"); settings(&f,3,44,255,1,1);
	string(&f,"I#g 3:I#X 5,2,256,"); local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS; poll(&f); settings(&f,2,0,255,0,1);
	string(&f,"I#X 5,+1:"); settings(&f,0xffff,0,255,0,1);
	string(&f,"I#X 5,4:"); settings(&f,4,17,19,1,1);
	string(&f,"I#X 5,2,"); b->control.flow.mode=3; string(&f,"300,2,"); settings(&f,3,44,19,2,1);
	string(&f,"511,"); settings(&f,3,44,255,2,1); b->control.flow.mode=4; string(&f,"2:"); settings(&f,4,17,19,1,1);
	/* The operation read was already assigned to wire. Its post-read
	 * loop2 owner forces1 and consumes no custom operands. */
	string(&f,"I#X 5,"); b->numeric.loop.active=2; string(&f,"2:"); settings(&f,1,17,19,1,1); CHECK(st_cterm_finish(f.terminal)==0);
	/* Wire x's stored read ends loop before X5 applies its override,
	 * allowing the subsequent custom pair to come from wire. */
	string(&f,"I#X 5,"); b->numeric.loop=(struct st_big_loop){.active=1,.count=1,.step=1,.to=1,.no_parameters=1,.type='@',.words={2}};
	string(&f,"x,511,2:"); settings(&f,2,255,19,2,1); CHECK(b->numeric.loop.active==0);
	string(&f,"I#X 5,3,260,"); clock_error=IO_ERROR_IO; poll(&f); clock_error=0;
	settings(&f,3,255,4,2,1); CHECK(b->last_error==IO_ERROR_IO && b->rejected_commands==1);
	f.size=0; string(&f,"I#X 5,4:\033[6n"); settings(&f,4,17,19,1,1); CHECK(f.size==6 && memcmp(f.output,"\033[1;1R",6)==0);
	destroy(&f); create(&f,false,false); settings(&f,1,17,19,1,1); destroy(&f);
}
static void flow_and_footer(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#X>5,2,256,2:X 5,3,511,2:"); CHECK(f.size==0);
	string(&f,"I#X 8,3,4,+1,0:"); CHECK(f.size==4 && memcmp(f.output,"\xff\xff\0\0",4)==0);
	string(&f,"I#X 5,0:"); f.size=0; string(&f,"I#X 8,3,4,+1,0:"); CHECK(f.size==0);
	string(&f,"I#X 5,2,300,+1:"); string(&f,"I#X 8,3,4,+1,0:"); CHECK(f.size==2 && memcmp(f.output,"\xff\xff",2)==0);
	string(&f,"I#X>5,4:X 5,2,255,130:"); f.size=0;
	string(&f,"I#X 8,3,4,+1,0:"); CHECK(f.size==64 && b->rotation.active && f.output[0]==19);
	poll(&f); CHECK(f.size==128 && b->rotation.active); poll(&f); CHECK(f.size==131 && !b->rotation.active);
	for (size_t i=1;i<131;++i) CHECK(f.output[i]==255);
	string(&f,"I#X 5,4:"); f.size=0;
	/* First stored record retains loop and overrides3 to1. Final record
	 * clears loop during the operation read, then consumes custom wire. */
	string(&f,"I#& 0,2,1,0,X@2,5,3,260,2:"); settings(&f,3,17,4,1,2);
	CHECK(b->numeric.loop.active==0 && f.size==2 && memcmp(f.output,"\x13\x11",2)==0);
	f.size=0; string(&f,"I#"); b->control.input.pending=1; b->control.input.count=1; b->control.input.payload[1]=255;
	string(&f,"X>5,0:X 5,1:\033[6n"); CHECK(f.size==7 && memcmp(f.output,"\xff\033[1;1R",7)==0 && b->control.input.pending==0);
	destroy(&f);
}
int main(void) { partitions(); acquisition_and_lifetimes(); flow_and_footer(); return 0; }
