/* Independent t wire, native deadline, keyboard and flow expectations. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm.h"
#include "cterm_big_internal.h"
#include "st_cterm.h"
#include "io_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
struct fixture { st_console_t *console; st_font_catalog_t *fonts; st_cterm_t *terminal; uint8_t output[128]; size_t size; };
static uint64_t local_ns;
static unsigned clock_calls,reads;
static int clock_error,read_error,peek_error;
static bool reset_on_clock,reset_on_read;
static int now(void *context, uint64_t *value) { (void)context; ++clock_calls; *value=local_ns; if (reset_on_clock) { reset_on_clock=false; CHECK(st_cterm_reset_terminal(((struct cterm_big *)context)->terminal)==0); } return clock_error; }
static int read_key(void *context, uint8_t *value) { ++reads; if (reset_on_read) { reset_on_read=false; CHECK(st_cterm_reset_terminal(((struct cterm_big *)context)->terminal)==0); } return read_error?read_error:cterm_big_input_read(context,value); }
static int peek_key(void *context, bool *value) { return peek_error?peek_error:cterm_big_input_peek(context,value); }
static void output(void *context, enum st_cterm_output_origin origin, const uint8_t *bytes, size_t size)
{
	struct fixture *f=context; (void)origin; CHECK(size<=sizeof(f->output)-f->size);
	memcpy(f->output+f->size,bytes,size); f->size+=size;
}
static void create(struct fixture *f, bool utf8, bool status)
{
	memset(f,0,sizeof(*f)); local_ns=1000; clock_calls=reads=0; clock_error=read_error=peek_error=0; reset_on_clock=reset_on_read=false;
	const struct st_console_display_config display={.backend=ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry={.columns=80,.rows=25,.cell_width=8,.cell_height=14};
	CHECK(st_console_create(&f->console,&display,&geometry)==0 && st_font_catalog_create(&f->fonts,f->console)==0);
	const struct st_cterm_config config={.console=f->console,.fonts=f->fonts,.emulation=ST_CTERM_ANSI_BBS,
	    .graphics_mode=ST_CTERM_GRAPHICS_BIG_103,.encoding=utf8?ST_CTERM_ENCODING_UTF8:ST_CTERM_ENCODING_CODEPAGE,
	    .codepage=ST_CODEPAGE_CP437,.quiet=true,.status_line=status,.scrollback_lines=2,.output=output,.output_context=f};
	CHECK(st_cterm_create(&f->terminal,&config)==0);
	f->terminal->big_engine->numeric.io.now_ns=now; f->terminal->big_engine->numeric.io.read_key=read_key; f->terminal->big_engine->numeric.io.peek_key=peek_key;
}
static void destroy(struct fixture *f) { st_cterm_destroy(&f->terminal); st_font_catalog_destroy(f->fonts); st_console_destroy(f->console); }
static void feed(struct fixture *f, const void *bytes, size_t size) { size_t used; int status=st_cterm_write(f->terminal,bytes,size,&used); CHECK((status==0 || status==IO_ERROR_IN_PROGRESS) && used==size); }
static void string(struct fixture *f, const char *wire) { feed(f,wire,strlen(wire)); }
static void poll(struct fixture *f) { CHECK(st_cterm_poll(f->terminal)==0); }
static void key(struct fixture *f, struct cterm_key k) { CHECK(cterm_big_handle_key(f->terminal->big_engine,&k)); }
static void setup(struct fixture *f, unsigned resolution)
{
	char wire[128]; snprintf(wire,sizeof(wire),"I#R %u:I#C>0,5:P>100,100:G 0,0,100,100,100,100,120,100:",resolution);
	string(f,wire); f->size=0; f->terminal->big_engine->state.bgi.error=-11;
}
static void canvas(struct fixture *f)
{
	uint32_t width,height; CHECK(st_console_pixel_size(f->console,&width,&height)==0);
	struct st_pixel_buffer *pixels=NULL; const struct st_console_rect area={0,0,width,height};
	CHECK(st_console_get_pixels(&pixels,f->console,&area,false)==0);
	for (size_t i=0;i<(size_t)width*height;++i)
		CHECK(pixels->primary[i]==((i==100u*width+100u || i==100u*width+120u)?805u:800u) && pixels->alternate[i]==pixels->primary[i]);
	st_pixel_buffer_destroy(pixels);
}
static void splits_and_owners(void)
{
	const char wire[]="I#t 1:";
	for (unsigned utf8=0;utf8<2;++utf8) for (unsigned status=0;status<2;++status) {
		struct fixture f; create(&f,utf8,status); struct cterm_big *b=f.terminal->big_engine;
		for (unsigned res=1;res<=3;++res) for (size_t split=0;split<=sizeof(wire);++split) {
			setup(&f,res); struct st_big_state state; memcpy(&state,&b->state,sizeof(state)); state.memory_page=1; state.active_page=state.visual_page=0; state.local_palette[0]=state.bgi.palette_size; memcpy(state.local_palette+1,state.bgi.palette,16);
			struct st_big_image image; memcpy(&image,&b->image,sizeof(image)); uint8_t bytes[10]; memcpy(bytes,b->image_bytes,10);
			if (split==sizeof(wire)) for (size_t i=0;i<sizeof(wire)-1;++i) feed(&f,wire+i,1);
			else { feed(&f,wire,split); feed(&f,wire+split,sizeof(wire)-1-split); }
			CHECK(st_cterm_input_paused(f.terminal) && b->pause.seconds==1 && b->pause.deadline==local_ns+UINT64_C(1000000000) && f.size==0);
			local_ns+=999999999; poll(&f); CHECK(st_cterm_input_paused(f.terminal) && f.size==0);
			++local_ns; poll(&f); CHECK(!st_cterm_input_paused(f.terminal) && b->pause.phase==CTERM_BIG_PAUSE_OFF && f.size==0);
			CHECK(memcmp(&state,&b->state,sizeof(state))==0 && memcmp(&image,&b->image,sizeof(image))==0 && memcmp(bytes,b->image_bytes,10)==0);
			CHECK(b->last_error==0 && b->rejected_commands==0); canvas(&f);
			string(&f,"\033[6n"); CHECK(f.size==6 && memcmp(f.output,"\033[1;1R",6)==0);
		}
		destroy(&f);
	}
}
static void keys_and_prefix(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#t "); CHECK(b->pause.phase==CTERM_BIG_PAUSE_NUMBER && !st_cterm_input_paused(f.terminal));
	key(&f,(struct cterm_key){.kind=CTERM_KEY_TEXT,.value='A'}); unsigned before=reads;
	string(&f,"2:"); CHECK(!st_cterm_input_paused(f.terminal) && reads==before+1 && f.size==0); /* No second predrain. */
	/* Predrain precedes even entering the class0 number. Retry a selected
	 * getch without another peek, clock sample or argument consumption. */
	string(&f,"I#t 2:"); CHECK(st_cterm_finish(f.terminal)==0);
	string(&f,"I#"); const struct cterm_big_event a={.data.key={.kind=CTERM_KEY_TEXT,.value='A'}};
	CHECK(cterm_big_input_push(b,&a)); read_error=IO_ERROR_IN_PROGRESS;
	size_t used; CHECK(st_cterm_write(f.terminal,"t 2:",4,&used)==IO_ERROR_IN_PROGRESS && used==2);
	CHECK(b->pause.phase==CTERM_BIG_PAUSE_DRAIN_READ && !b->pause.number_started && b->pause.deadline==0);
	unsigned calls=clock_calls; poll(&f); CHECK(clock_calls==calls && b->input.count==1);
	read_error=0; poll(&f); CHECK(b->input.count==0 && b->pause.phase==CTERM_BIG_PAUSE_NUMBER);
	string(&f,"2:"); CHECK(b->pause.seconds==2 && st_cterm_input_paused(f.terminal));
	/* A private scan alone cannot cancel. A real event admits its read;
	 * the new event remains available for ordinary terminal forwarding. */
	b->input.scan=0x48; b->input.scan_pending=true; poll(&f); CHECK(st_cterm_input_paused(f.terminal) && b->input.scan_pending);
	key(&f,(struct cterm_key){.kind=CTERM_KEY_TEXT,.value='B'}); poll(&f);
	CHECK(!st_cterm_input_paused(f.terminal) && !b->input.scan_pending && f.size==1 && f.output[0]=='B');
	const struct cterm_key cases[]={ {.kind=CTERM_KEY_NORMAL,.local=0x4800}, {.kind=CTERM_KEY_CODEPAGE,.local=0}, {.kind=CTERM_KEY_TEXT,.value=0x1f600} };
	for (size_t i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
		f.size=0; string(&f,"I#t 1:"); key(&f,cases[i]); local_ns+=UINT64_C(1000000000); poll(&f);
		CHECK(!st_cterm_input_paused(f.terminal) && f.size==0 && b->input.count==0);
		CHECK(b->input.scan_pending==(i==0)); b->input.scan_pending=false;
	}
	string(&f,"I#t 1:"); key(&f,a.data.key); read_error=IO_ERROR_IN_PROGRESS; before=reads; poll(&f); poll(&f);
	CHECK(st_cterm_input_paused(f.terminal) && reads==before+2 && b->pause.phase==CTERM_BIG_PAUSE_READ);
	read_error=0; poll(&f); CHECK(!st_cterm_input_paused(f.terminal) && f.size==0);
	destroy(&f);
}
static void limits_and_recovery(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	static const struct { const char *wire; unsigned seconds; } cases[]={ {"0:",1},{"+1:",1},{"1:",1},{"29:",29},{"30:",30},{"31:",30},{"9999:",30} };
	for (size_t i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
		string(&f,"I#t "); string(&f,cases[i].wire); CHECK(b->pause.seconds==cases[i].seconds);
		CHECK(b->pause.deadline==local_ns+cases[i].seconds*UINT64_C(1000000000));
		CHECK(st_cterm_finish(f.terminal)==0 && !st_cterm_input_paused(f.terminal) && f.size==0);
	}
	string(&f,"I#t "); local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS; poll(&f); CHECK(b->pause.seconds==1 && st_cterm_input_paused(f.terminal));
	CHECK(st_cterm_reset_terminal(f.terminal)==0 && !st_cterm_input_paused(f.terminal));
	string(&f,"I#t 1:"); clock_error=IO_ERROR_IO; poll(&f); clock_error=0;
	CHECK(b->last_error==IO_ERROR_IO && !st_cterm_input_paused(f.terminal) && f.size==0);
	string(&f,"I#t 1:"); --local_ns; poll(&f); CHECK(b->last_error==IO_ERROR_OUT_OF_RANGE && !st_cterm_input_paused(f.terminal));
	string(&f,"I#t 1:"); peek_error=IO_ERROR_IN_PROGRESS; poll(&f); peek_error=0;
	CHECK(b->last_error==IO_ERROR_PROTOCOL && !st_cterm_input_paused(f.terminal));
	string(&f,"I#t 1:"); key(&f,(struct cterm_key){.kind=CTERM_KEY_TEXT,.value='Q'}); read_error=IO_ERROR_IO; poll(&f); read_error=0;
	CHECK(b->last_error==IO_ERROR_IO && !st_cterm_input_paused(f.terminal) && f.size==0);
	string(&f,"I#t 1:"); reset_on_clock=true; poll(&f); CHECK(!st_cterm_input_paused(f.terminal) && !b->reset_pending);
	string(&f,"I#t 1:"); key(&f,(struct cterm_key){.kind=CTERM_KEY_TEXT,.value='Q'}); reset_on_read=true; poll(&f); CHECK(!st_cterm_input_paused(f.terminal) && f.size==0);
	local_ns=UINT64_MAX-100; string(&f,"I#t 30:"); CHECK(b->pause.deadline==UINT64_MAX);
	local_ns=UINT64_MAX; poll(&f); CHECK(!st_cterm_input_paused(f.terminal) && f.size==0);
	string(&f,"\033[6n"); CHECK(f.size==6 && memcmp(f.output,"\033[1;1R",6)==0);
	destroy(&f);
}
static void sources_and_footer(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#"); b->control.input.pending=1; b->control.input.count=1; b->control.input.payload[1]=255;
	string(&f,"t>1:"); CHECK(st_cterm_input_paused(f.terminal) && f.size==0);
	local_ns+=UINT64_C(1000000000); poll(&f); CHECK(b->control.input.pending==1 && !st_cterm_input_paused(f.terminal));
	string(&f,"S 5,7:\033[6n"); CHECK(f.size==7 && memcmp(f.output,"\xff\033[1;1R",7)==0 && b->state.bgi.palette[5]==7);
	f.size=0; string(&f,"I#g 3:I#& 0,2,1,0,t@1,1:");
	CHECK(b->pause.seconds==1 && st_cterm_input_paused(f.terminal) && f.size==0);
	local_ns+=UINT64_C(1000000000); poll(&f); CHECK(st_cterm_input_paused(f.terminal) && b->pause.deadline==local_ns+UINT64_C(1000000000));
	local_ns+=UINT64_C(1000000000); poll(&f); CHECK(!st_cterm_input_paused(f.terminal) && b->numeric.loop.active==0 && b->numeric.loop.current==2 && f.size==0);
	/* Supplied high-bit seconds clamp as signed, without wire scaling. */
	string(&f,"I#"); b->numeric.loop=(struct st_big_loop){.active=1,.count=1,.step=1,.to=1,.no_parameters=1,.type='@',.words={0x8000}};
	string(&f,"t "); CHECK(b->pause.seconds==1); CHECK(st_cterm_finish(f.terminal)==0);
	string(&f,"I#t 1:"); CHECK(st_cterm_reset_terminal(f.terminal)==0); string(&f,"I#C 0,7:"); CHECK(b->state.application.colors[0]==7 && f.size==0);
	destroy(&f);
}
int main(void) { splits_and_owners(); keys_and_prefix(); limits_and_recovery(); sources_and_footer(); return 0; }
