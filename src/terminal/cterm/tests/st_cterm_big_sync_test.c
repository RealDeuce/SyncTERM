/* Native X7 pair-entry/progress expectations, original BIG15/52. */
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
static void partitions(void)
{
	static const char wire[]="I#X>7,+1:X 7,9999:\033[6n";
	for (unsigned utf8=0;utf8<2;++utf8) for (unsigned status=0;status<2;++status) {
		struct fixture f; create(&f,utf8,status); struct cterm_big *b=f.terminal->big_engine;
		for (unsigned res=1;res<=3;++res) for (size_t split=0;split<=sizeof(wire);++split) {
			if (res!=2 && split!=sizeof(wire)) continue;
			char setup[100]; snprintf(setup,sizeof(setup),"I#R %u:I#C>0,5:P 100,100:",res);
			string(&f,setup); b->state.bgi.error=-11; f.size=0;
			struct st_big_state before=b->state; before.memory_page=1;
			if (split==sizeof(wire)) for (size_t i=0;i<sizeof(wire)-1;++i) feed(&f,wire+i,1);
			else { feed(&f,wire,split); feed(&f,wire+split,sizeof(wire)-1-split); }
			CHECK(b->control.music.sync==9999 && !b->sync_entry.waiting);
			CHECK(b->last_error==0 && b->rejected_commands==0 && memcmp(&before,&b->state,sizeof(before))==0);
			CHECK(f.size==6 && memcmp(f.output,"\033[1;1R",6)==0);
			uint32_t width,height; CHECK(st_console_pixel_size(f.console,&width,&height)==0);
			const struct st_console_rect area={0,0,width,height}; struct st_pixel_buffer *p=NULL;
			CHECK(st_console_get_pixels(&p,f.console,&area,false)==0);
			for (size_t i=0;i<(size_t)width*height;++i) CHECK(p->primary[i]==(i==100u*width+100u?805u:800u) && p->alternate[i]==p->primary[i]);
			st_pixel_buffer_destroy(p);
		}
		destroy(&f);
	}
}
static size_t write_some(struct fixture *f, const char *wire)
{
	size_t used; int status=st_cterm_write(f->terminal,wire,strlen(wire),&used);
	CHECK(status==0 || status==IO_ERROR_IN_PROGRESS); return used;
}
static void entry_progress(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#X 7,1:"); b->control.music.running=1; b->control.music.position_low=5;
	enum st_big_numeric_phase old_phase=b->numeric.field.phase;
	CHECK(write_some(&f,"I#X 7,0:\033[6n")==4);
	CHECK(st_cterm_input_paused(f.terminal) && b->control.music.scratch==5 && b->control.music.sync==1);
	CHECK(b->numeric.field.phase==old_phase && !b->pair_wait);
	const struct cterm_key key={.kind=CTERM_KEY_TEXT,.value='A'};
	CHECK(cterm_big_handle_key(b,&key) && b->input.count==1);
	/* Neither clocks, flags, keys nor more wire can advance an entered gate. */
	local_ns+=10*ST_BIG_NUMERIC_TIMEOUT_NS; b->control.music.running=0; b->control.music.sync=0;
	for (unsigned i=0;i<4;++i) { poll(&f); CHECK(write_some(&f,"7,0:")==0); }
	CHECK(st_cterm_input_paused(f.terminal) && b->input.count==1 && f.size==0);
	b->control.music.scratch=6; /* Live scratch alone releases. */
	poll(&f); CHECK(!st_cterm_input_paused(f.terminal));
	string(&f,"7,0:\033[6n"); CHECK(b->control.music.sync==0 && f.size==7 && memcmp(f.output,"A\033[1;1R",7)==0);
	f.size=0; b->control.music.position_low=5; b->control.music.running=1;
	string(&f,"I#X 7,+1:"); CHECK(b->control.music.sync==65535);
	CHECK(write_some(&f,"I#X 7,0:")==4); CHECK(b->control.music.scratch==5);
	b->control.music.position_high=1; poll(&f); string(&f,"7,0:");
	CHECK(b->control.music.sync==0 && !st_cterm_input_paused(f.terminal));
	/* Every chained pair snapshots independently, including unknown codes. */
	b->control.music.position_high=0; string(&f,"I#X 7,1:");
	CHECK(write_some(&f,"I#@>X 7,0:")==4); ++b->control.music.position_low; poll(&f);
	CHECK(write_some(&f,"X 7,0:")==2 && b->control.music.scratch==6);
	CHECK(st_cterm_input_paused(f.terminal)); ++b->control.music.position_low; poll(&f);
	string(&f,"7,0:"); CHECK(b->control.music.sync==0 && b->rejected_commands==0);
	/* Skipped gates never turn into waits partway through an operand. */
	b->control.music.running=0; string(&f,"I#X 7,"); b->control.music.running=1;
	string(&f,"6554:"); CHECK(b->control.music.sync==6554 && !st_cterm_input_paused(f.terminal));
	destroy(&f);
}
static void repeats_and_lifetimes(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	/* The & gate precedes loop setup. Its internally selected X7 bodies
	 * must not reenter synchronization when the first record enables it. */
	b->control.music.running=1; b->control.music.position_low=19;
	string(&f,"I#& 0,2,1,0,X@2,7,9999,0:");
	CHECK(!st_cterm_input_paused(f.terminal) && b->control.music.sync==9999 && b->numeric.loop.active==0);
	for (unsigned cancel=0;cancel<3;++cancel) {
		CHECK(write_some(&f,"I#X 7,0:")==4 && st_cterm_input_paused(f.terminal));
		if (cancel==0) cterm_big_reset(b);
		else if (cancel==1) CHECK(st_cterm_finish(f.terminal)==0);
		else CHECK(st_cterm_reset_terminal(f.terminal)==0);
		CHECK(!st_cterm_input_paused(f.terminal) && b->control.music.sync==9999 && b->control.music.position_low==19);
	}
	b->control.music.running=0;
	string(&f,"I#I 0:I#R 3:I#I 1:"); CHECK(b->control.music.sync==9999);
	/* Partial operand has no store; timeout obtains the numeric default. */
	string(&f,"I#X 7,12"); CHECK(b->control.music.sync==9999);
	CHECK(st_cterm_finish(f.terminal)==0 && b->control.music.sync==9999);
	string(&f,"I#X 7,"); local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS; poll(&f); CHECK(b->control.music.sync==0);
	string(&f,"I#X 7,3:"); string(&f,"I#X 7,"); clock_error=IO_ERROR_IO; poll(&f); clock_error=0;
	CHECK(b->control.music.sync==3 && b->last_error==IO_ERROR_IO && b->rejected_commands==1);
	string(&f,"I#X 7,0:\033[6n"); CHECK(b->control.music.sync==0 && f.size==6 && memcmp(f.output,"\033[1;1R",6)==0);
	destroy(&f); create(&f,false,false); CHECK(f.terminal->big_engine->control.music.sync==0); destroy(&f);
}
int main(void) { partitions(); entry_progress(); repeats_and_lifetimes(); return 0; }
