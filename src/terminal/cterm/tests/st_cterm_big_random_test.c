/* Native X2 seed/range/source expectations, original BIG33/52. */
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
static int seconds(void *context, uint64_t *value) { (void)context; ++wall_calls; *value=wall_seconds; return wall_error; }
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
	struct cterm_big *b=f->terminal->big_engine; uint64_t sample;
	CHECK(b->random_seconds!=NULL && b->random_seconds(b,&sample)==0);
	b->random_seconds=seconds;
}
static void destroy(struct fixture *f) { st_cterm_destroy(&f->terminal); st_font_catalog_destroy(f->fonts); st_console_destroy(f->console); }
static void feed(struct fixture *f, const void *bytes, size_t size) { size_t used; int status=st_cterm_write(f->terminal,bytes,size,&used); CHECK((status==0 || status==IO_ERROR_IN_PROGRESS) && used==size); }
static void string(struct fixture *f, const char *wire) { feed(f,wire,strlen(wire)); }
static void poll(struct fixture *f) { CHECK(st_cterm_poll(f->terminal)==0); }
static void bounds(struct fixture *f, uint16_t from, uint16_t to)
{
	const struct st_big_numeric *n=&f->terminal->big_engine->numeric;
	CHECK(n->random_from==from && n->random_to==to);
}
static void partitions(void)
{
	static const char wire[]="I#X>2,10,4:X 2,+1,+1:\033[6n";
	for (unsigned utf8=0;utf8<2;++utf8) for (unsigned status=0;status<2;++status) {
		struct fixture f; create(&f,utf8,status); struct cterm_big *b=f.terminal->big_engine;
		for (unsigned res=1;res<=3;++res) for (size_t split=0;split<=sizeof(wire);++split) {
			if (res!=2 && split!=sizeof(wire)) continue;
			char setup[100]; snprintf(setup,sizeof(setup),"I#R %u:I#C>0,5:P>100,100:G 0,0,100,100,100,100,120,100:",res);
			string(&f,setup); b->state.bgi.error=-11; f.size=0; wall_calls=0; wall_seconds=UINT64_C(0x0123456789abcdef);
			struct st_big_state before=b->state; before.memory_page=1; uint8_t image[10]; memcpy(image,b->image_bytes,10);
			if (split==sizeof(wire)) for (size_t i=0;i<sizeof(wire)-1;++i) feed(&f,wire+i,1);
			else { feed(&f,wire,split); feed(&f,wire+split,sizeof(wire)-1-split); }
			bounds(&f,0,1); CHECK(wall_calls==2 && b->numeric.random_state==0xcdef);
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
static void partials_lifetime_and_failures(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#X 2,"); bounds(&f,0,199); CHECK(wall_calls==1 && b->numeric.random_state==1);
	string(&f,"200,"); bounds(&f,200,199); CHECK(st_cterm_finish(f.terminal)==0); bounds(&f,200,199);
	wall_seconds=UINT64_C(0x12340000); string(&f,"I#X 2,+1,"); bounds(&f,65535,199); CHECK(b->numeric.random_state==0);
	CHECK(st_cterm_reset_terminal(f.terminal)==0); bounds(&f,65535,199); CHECK(b->numeric.random_state==0);
	string(&f,"I#I 0:I#R 3:I#I 1:"); bounds(&f,65535,199); CHECK(b->numeric.random_state==0);
	string(&f,"I#g 3:I#X 2,10,"); bounds(&f,10,199); local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS; poll(&f); bounds(&f,10,11);
	wall_seconds=1; string(&f,"I#X 2,"); uint32_t previous=b->numeric.random_state;
	CHECK(st_cterm_finish(f.terminal)==0 && b->numeric.random_state==previous);
	wall_error=IO_ERROR_IN_PROGRESS; string(&f,"I#X 2,"); wall_error=0;
	CHECK(b->last_error==IO_ERROR_PROTOCOL && b->rejected_commands==1 && b->numeric.random_state==previous); bounds(&f,10,11);
	wall_error=IO_ERROR_IO; string(&f,"I#X 2,"); wall_error=0;
	CHECK(b->last_error==IO_ERROR_IO && b->rejected_commands==2 && b->numeric.random_state==previous);
	string(&f,"I#X 2,200,"); clock_error=IO_ERROR_IO; poll(&f); clock_error=0; bounds(&f,200,11);
	CHECK(b->last_error==IO_ERROR_IO && b->rejected_commands==3);
	f.size=0; string(&f,"I#X 2,0,199:\033[6n"); bounds(&f,0,199); CHECK(f.size==6 && memcmp(f.output,"\033[1;1R",6)==0);
	destroy(&f); create(&f,false,false); bounds(&f,0,199); CHECK(f.terminal->big_engine->numeric.random_state==1); destroy(&f);
}
static void substitutions_and_stored_sources(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	/* R346 from seed1 under old TO199 supplies2; final TO1 repairs to3. */
	string(&f,"I#X 2,r,1:"); bounds(&f,2,3); CHECK(b->numeric.random_state==UINT32_C(0x015a4e36) && wall_calls==1);
	/* TO's r sees raw FROM200 against old TO3, so no repair has occurred
	 * and polls advance the same shared stream without another seed. */
	string(&f,"I#X 2,200,r:"); bounds(&f,200,3); CHECK(st_cterm_input_paused(f.terminal));
	uint32_t previous=b->numeric.random_state; unsigned calls=wall_calls; poll(&f);
	CHECK(b->numeric.random_state!=previous && wall_calls==calls); bounds(&f,200,3);
	b->numeric.random_from=b->numeric.random_to=0; poll(&f); bounds(&f,0,0); CHECK(!st_cterm_input_paused(f.terminal));
	/* Constant records end the loop before the final TO, but both X2
	 * bodies reseed. Only setup/footer own flow, and g3 has no effect. */
	string(&f,"I#g 3:I#& 0,2,1,0,X@3,2,10,4:"); bounds(&f,10,11);
	CHECK(wall_calls==calls+2 && b->numeric.random_state==1 && f.size==0);
	/* Supplied WORD32767 bypasses the four wire-digit slots. Repair
	 * wraps to8000 and does not recheck that negative upper bound. */
	string(&f,"I#"); b->numeric.loop=(struct st_big_loop){.active=1,.count=3,.step=1,.to=1,.no_parameters=1,.type='@',.words={2,32767,0}};
	string(&f,"X "); bounds(&f,32767,32768); CHECK(b->numeric.loop.active==0);
	string(&f,"I#"); b->control.input.pending=1; b->control.input.count=1; b->control.input.payload[1]=255;
	f.size=0; string(&f,"X>2,10,10:S 5,5:\033[6n"); bounds(&f,10,10);
	CHECK(f.size==7 && memcmp(f.output,"\xff\033[1;1R",7)==0 && b->control.input.pending==0);
	/* Equal positive ranges remain pending; EOF keeps accepted range
	 * and draws, then a later command can repair the range normally. */
	string(&f,"I#S 5,r:"); CHECK(st_cterm_input_paused(f.terminal)); previous=b->numeric.random_state;
	CHECK(st_cterm_finish(f.terminal)==0 && b->numeric.random_state==previous); bounds(&f,10,10);
	string(&f,"I#X 2,0,0:I#S 5,r:"); CHECK(b->state.bgi.palette[5]==0 && !st_cterm_input_paused(f.terminal));
	destroy(&f);
}
int main(void) { partitions(); partials_lifetime_and_failures(); substitutions_and_stored_sources(); return 0; }
