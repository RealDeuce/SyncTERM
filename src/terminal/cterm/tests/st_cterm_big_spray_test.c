/* Native X0 sampling/marker/page/error expectations, original BIG22/58. */
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
static int clock_error,wall_error;
static uint64_t wall_seconds;
static unsigned wall_calls;
static int seconds(void *context, uint64_t *value) { struct cterm_big *b=context; CHECK(b->state.bgi.color==(uint8_t)b->state.application.colors[0]); ++wall_calls; *value=wall_seconds; return wall_error; }
static int now(void *context, uint64_t *value) { (void)context; *value=local_ns; return clock_error; }
static void output(void *context, enum st_cterm_output_origin origin, const uint8_t *bytes, size_t size)
{
	struct fixture *f=context; (void)origin; CHECK(size<=sizeof(f->output)-f->size);
	memcpy(f->output+f->size,bytes,size); f->size+=size;
}
static void create(struct fixture *f, bool utf8, bool status)
{
	memset(f,0,sizeof(*f)); local_ns=1000; clock_error=wall_error=0; wall_seconds=1; wall_calls=0;
	const struct st_console_display_config display={.backend=ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry={.columns=80,.rows=25,.cell_width=8,.cell_height=14};
	CHECK(st_console_create(&f->console,&display,&geometry)==0 && st_font_catalog_create(&f->fonts,f->console)==0);
	const struct st_cterm_config config={.console=f->console,.fonts=f->fonts,.emulation=ST_CTERM_ANSI_BBS,
	    .graphics_mode=ST_CTERM_GRAPHICS_BIG_103,.encoding=utf8?ST_CTERM_ENCODING_UTF8:ST_CTERM_ENCODING_CODEPAGE,
	    .codepage=ST_CODEPAGE_CP437,.quiet=true,.status_line=status,.scrollback_lines=2,.output=output,.output_context=f};
	CHECK(st_cterm_create(&f->terminal,&config)==0); f->terminal->big_engine->numeric.io.now_ns=now;
	struct cterm_big *b=f->terminal->big_engine; CHECK(b->random_seconds!=NULL);
	b->random_seconds=seconds;
}
static void destroy(struct fixture *f) { st_cterm_destroy(&f->terminal); st_font_catalog_destroy(f->fonts); st_console_destroy(f->console); }
static void feed(struct fixture *f, const void *bytes, size_t size) { size_t used; int status=st_cterm_write(f->terminal,bytes,size,&used); CHECK((status==0 || status==IO_ERROR_IN_PROGRESS) && used==size); }
static void string(struct fixture *f, const char *wire) { feed(f,wire,strlen(wire)); }
static void poll(struct fixture *f) { CHECK(st_cterm_poll(f->terminal)==0); }
static void setup(struct fixture *f, unsigned res)
{
	char wire[100]; snprintf(wire,sizeof(wire),"I#R %u:I#C>0,5:m 2,80,90:",res); string(f,wire);
	f->terminal->big_engine->state.bgi.error=-11; f->size=0; wall_calls=0;
}
static void canvas(struct fixture *f, const uint16_t (*dots)[2], size_t count)
{
	uint32_t width,height; CHECK(st_console_pixel_size(f->console,&width,&height)==0);
	const struct st_console_rect area={0,0,width,height}; struct st_pixel_buffer *p=NULL;
	CHECK(st_console_get_pixels(&p,f->console,&area,false)==0);
	for (size_t i=0;i<(size_t)width*height;++i) {
		bool dot=false; for (size_t j=0;j<count;++j) if (i==(size_t)dots[j][1]*width+dots[j][0]) dot=true;
		CHECK(p->primary[i]==(dot?805u:800u) && p->alternate[i]==p->primary[i]);
	}
	st_pixel_buffer_destroy(p);
}
static void partitions(void)
{
	static const char wire[]="I#X 0,100,100,100,100,4:\033[6n";
	static const uint16_t dots[][2]={{101,100},{133,103},{135,121},{153,119}};
	for (unsigned utf8=0;utf8<2;++utf8) for (unsigned status=0;status<2;++status) {
		struct fixture f; create(&f,utf8,status); struct cterm_big *b=f.terminal->big_engine;
		for (unsigned res=1;res<=3;++res) for (size_t split=0;split<=sizeof(wire);++split) {
			if (res!=2 && split!=sizeof(wire)) continue;
			setup(&f,res);
			if (split==sizeof(wire)) for (size_t i=0;i<sizeof(wire)-1;++i) feed(&f,wire+i,1);
			else { feed(&f,wire,split); feed(&f,wire+split,sizeof(wire)-1-split); }
			CHECK(b->last_error==0 && b->rejected_commands==0 && wall_calls==1 && b->numeric.random_state==UINT32_C(0x190f3a19));
			CHECK(b->spray.counter==4 && b->state.bgi.color==5 && b->state.bgi.x==80 && b->state.bgi.y==90 && b->state.bgi.error==-11);
			CHECK(f.size==6 && memcmp(f.output,"\033[1;1R",6)==0); canvas(&f,dots,4);
		}
		setup(&f,2); string(&f,"I#X 0,100,100,+100,+100,4:");
		static const uint16_t negative[][2]={{99,100},{67,97},{65,79},{47,81}};
		canvas(&f,negative,4); CHECK(b->state.bgi.x==80 && b->state.bgi.y==90);
		destroy(&f);
	}
}
static void prefix_scaling_and_lifetimes(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine; setup(&f,2);
	b->spray.counter=77; wall_seconds=UINT64_C(0x12340000); string(&f,"I#X 0,");
	CHECK(wall_calls==1 && b->numeric.random_state==0 && b->state.bgi.color==5 && b->spray.counter==77);
	string(&f,"100,100,0,0,"); CHECK(b->spray.x==100 && b->spray.y==100 && b->spray.counter==77);
	CHECK(st_cterm_finish(f.terminal)==0 && b->spray.counter==77 && b->numeric.random_state==0); canvas(&f,NULL,0);
	wall_seconds=1; string(&f,"I#X 0,100,100,0,0,"); local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS; poll(&f);
	CHECK(b->spray.counter==0 && b->spray.count==0 && b->numeric.random_state==1); canvas(&f,NULL,0);
	string(&f,"I#g 2:I#X 0,4,4,4,4,0:");
	CHECK(b->spray.x==4 && b->spray.y==7 && b->spray.width==4 && b->spray.height==7 && b->spray.count==0 && b->numeric.random_state==1);
	/* Supplied coordinate values bypass g2; class0 count does as well. */
	string(&f,"I#& 0,1,1,0,X@6,0,4,4,4,4,0:");
	CHECK(b->spray.y==4 && b->spray.height==4 && b->spray.counter==0);
	string(&f,"I#g 0:I#X 0,100,100,0,0,");
	CHECK(st_cterm_reset_terminal(f.terminal)==0 && b->spray.counter==0 && b->numeric.random_state==1);
	string(&f,"I#X 0,100,100,0,0,2:"); uint32_t state=b->numeric.random_state;
	CHECK(state==UINT32_C(0x0442c2ed) && b->spray.counter==2);
	string(&f,"I#I 1:I#R 3:I#I 0:"); CHECK(b->numeric.random_state==state && b->spray.counter==2);
	setup(&f,2); wall_error=IO_ERROR_IO; string(&f,"I#X 0,"); wall_error=0;
	CHECK(b->last_error==IO_ERROR_IO && b->state.bgi.color==5 && b->numeric.random_state==state && b->spray.counter==2);
	f.size=0; string(&f,"I#X 0,100,100,0,0,+1:\033[6n");
	CHECK(b->spray.counter==0 && b->numeric.random_state==1 && f.size==6);
	destroy(&f);
}
static void helper_pages_sources_and_recovery(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine; setup(&f,2);
	string(&f,"I#T>0,2,1:X 0,100,100,0,0,1:");
	CHECK(b->state.bgi.x==104 && b->state.bgi.y==100 && b->spray.counter==1);
	/* Direct helper ends at its fourth ray tip; P adds a separate anchor. */
	string(&f,"I#P 100,100:"); CHECK(b->state.bgi.x==100 && b->state.bgi.y==100);
	setup(&f,2); string(&f,"I#T>0,0,1:G>6,1:X 0,100,100,100,100,4:"); canvas(&f,NULL,0);
	CHECK(b->video_active_page==1 && b->video_visual_page==0 && b->spray.counter==4);
	string(&f,"I#G 5,1:");
	static const uint16_t dots[][2]={{101,100},{133,103},{135,121},{153,119}};
	canvas(&f,dots,4);
	setup(&f,2); string(&f,"I#& 0,2,1,0,X@6,0,100,100,0,0,1:");
	CHECK(wall_calls==2 && b->numeric.random_state==UINT32_C(0x8082a52f) && b->spray.counter==1 && f.size==2 && memcmp(f.output,"\x13\x11",2)==0);
	/* Operand r shares the freshly seeded stream and is not reseeded
	 * before the sampling loop. Default TO199 makes first X origin2. */
	setup(&f,2); string(&f,"I#X 0,r,100,0,0,1:");
	static const uint16_t substituted[][2]={{2,100}}; canvas(&f,substituted,1);
	CHECK(b->numeric.random_state==UINT32_C(0xaae684bc) && wall_calls==1);
	setup(&f,2); string(&f,"I#"); b->control.input.pending=1; b->control.input.count=1; b->control.input.payload[1]=255;
	string(&f,"X>0,100,100,0,0,1:S 5,5:\033[6n");
	CHECK(f.size==7 && memcmp(f.output,"\xff\033[1;1R",7)==0 && b->control.input.pending==0);
	/* Corrupt aperture failure occurs after two draws but before the
	 * first write/counter increment; recovery preserves the next command. */
	setup(&f,2); b->state.bgi.mode=99;
	string(&f,"I#X 0,100,100,0,0,1:I#S 5,6:\033[6n");
	CHECK(b->last_error!=0 && b->rejected_commands==1 && b->spray.counter==0 && b->numeric.random_state==UINT32_C(0x8082a52f) && b->state.bgi.palette[5]==6);
	CHECK(f.size==6 && memcmp(f.output,"\033[1;1R",6)==0); b->state.bgi.mode=1;
	destroy(&f); create(&f,false,false); CHECK(f.terminal->big_engine->spray.counter==0 && f.terminal->big_engine->numeric.random_state==1); destroy(&f);
}
int main(void) { partitions(); prefix_scaling_and_lifetimes(); helper_pages_sources_and_recovery(); return 0; }
