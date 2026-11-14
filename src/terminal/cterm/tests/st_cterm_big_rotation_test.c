/* Independent X8 wire, native deadline/input and recovery expectations. */
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
static int clock_error,read_error;
static bool blocked;
static enum st_big_output_kind blocked_kind;
static int ready(void *context, enum st_big_output_kind kind) { (void)context; return blocked && kind==blocked_kind?IO_ERROR_IN_PROGRESS:0; }
static int now(void *context, uint64_t *value) { (void)context; ++clock_calls; *value=local_ns; return clock_error; }
static int read_key(void *context, uint8_t *value) { ++reads; return read_error?read_error:cterm_big_input_read(context,value); }
static void output(void *context, enum st_cterm_output_origin origin, const uint8_t *bytes, size_t size)
{
	struct fixture *f=context; (void)origin; CHECK(size<=sizeof(f->output)-f->size);
	memcpy(f->output+f->size,bytes,size); f->size+=size;
}
static void create(struct fixture *f, bool utf8, bool status)
{
	memset(f,0,sizeof(*f)); local_ns=1000; clock_calls=reads=0; clock_error=read_error=0;
	const struct st_console_display_config display={.backend=ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry={.columns=80,.rows=25,.cell_width=8,.cell_height=14};
	CHECK(st_console_create(&f->console,&display,&geometry)==0 && st_font_catalog_create(&f->fonts,f->console)==0);
	const struct st_cterm_config config={.console=f->console,.fonts=f->fonts,.emulation=ST_CTERM_ANSI_BBS,
	    .graphics_mode=ST_CTERM_GRAPHICS_BIG_103,.encoding=utf8?ST_CTERM_ENCODING_UTF8:ST_CTERM_ENCODING_CODEPAGE,
	    .codepage=ST_CODEPAGE_CP437,.quiet=true,.status_line=status,.scrollback_lines=2,.output=output,.output_context=f};
	CHECK(st_cterm_create(&f->terminal,&config)==0);
	f->terminal->big_engine->numeric.io.now_ns=now; f->terminal->big_engine->numeric.io.read_key=read_key;
}
static void destroy(struct fixture *f) { st_cterm_destroy(&f->terminal); st_font_catalog_destroy(f->fonts); st_console_destroy(f->console); }
static void feed(struct fixture *f, const void *bytes, size_t size) { size_t used; int status=st_cterm_write(f->terminal,bytes,size,&used); CHECK((status==0 || status==IO_ERROR_IN_PROGRESS) && used==size); }
static void string(struct fixture *f, const char *wire) { feed(f,wire,strlen(wire)); }
static void poll(struct fixture *f) { CHECK(st_cterm_poll(f->terminal)==0); }
static void key(struct fixture *f, struct cterm_key k) { CHECK(cterm_big_handle_key(f->terminal->big_engine,&k)); }
static void setup(struct fixture *f, unsigned res)
{
	char wire[160]; snprintf(wire,sizeof(wire),"I#R %u:I#C>0,5:P>100,100:G>0,0,100,100,100,100,120,100:S>2,7:S>3,10:S>4,20:X 8,1,1,1,1:",res);
	string(f,wire); f->size=0; struct cterm_big *b=f->terminal->big_engine;
	CHECK(b->state.saved_palette[2]==7 && b->state.saved_palette[3]==10 && b->state.saved_palette[4]==20);
	b->state.bgi.error=-11; b->state.bgi.background=7;
}
static void canvas(struct fixture *f)
{
	uint32_t width,height; CHECK(st_console_pixel_size(f->console,&width,&height)==0);
	const struct st_console_rect area={0,0,width,height}; struct st_pixel_buffer *p=NULL;
	CHECK(st_console_get_pixels(&p,f->console,&area,false)==0);
	for (size_t i=0;i<(size_t)width*height;++i) CHECK(p->primary[i]==((i==100u*width+100u || i==100u*width+120u)?805u:800u) && p->alternate[i]==p->primary[i]);
	st_pixel_buffer_destroy(p);
}
static void partitions_and_owners(void)
{
	static const char wire[]="I#X 8,3,4,1,0:\033[6n";
	for (unsigned utf8=0;utf8<2;++utf8) for (unsigned status=0;status<2;++status) {
		struct fixture f; create(&f,utf8,status); struct cterm_big *b=f.terminal->big_engine;
		for (unsigned res=1;res<=3;++res) for (size_t split=0;split<=sizeof(wire);++split) {
			if (res!=2 && split!=sizeof(wire)) continue;
			setup(&f,res); unsigned before=clock_calls;
			b->feature_gates[0][0]=b->feature_gates[2][0]=1;
			if (split==sizeof(wire)) for (size_t i=0;i<sizeof(wire)-1;++i) feed(&f,wire+i,1);
			else { feed(&f,wire,split); feed(&f,wire+split,sizeof(wire)-1-split); }
			CHECK(!b->rotation.active && b->last_error==0 && b->rejected_commands==0 && b->rotation.counter==2);
			CHECK(b->state.bgi.palette[3]==10 && b->state.bgi.palette[4]==7 && b->state.local_palette[4]==7);
			CHECK(b->state.saved_palette[3]==10 && b->state.saved_palette[4]==20 && b->state.bgi.error==-11 && b->state.bgi.background==7);
			CHECK(b->state.bgi.x==100 && b->state.bgi.y==100 && b->state.memory_page==1 && b->video_active_page==0 && b->video_visual_page==0);
			CHECK(b->image_size==10 && b->image_bytes[5]==128 && b->image_bytes[7]==128 && b->image_pens[0]==5);
			CHECK(f.size==6 && memcmp(f.output,"\033[1;1R",6)==0 && clock_calls>=before+4);
			uint32_t rgb; CHECK(st_console_get_palette(f.console,804,&rgb,1)==0 && rgb==0xaaaaaa); canvas(&f);
			memset(b->feature_gates,0,sizeof(b->feature_gates));
		}
		destroy(&f);
	}
}
static void capture_restore_and_skips(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#X 8,0,15,0,+1:"); CHECK(f.size==0 && b->state.bgi.palette[15]==0);
	setup(&f,2); string(&f,"I#S>8,128:X 8,1,1,1,1:"); CHECK(b->state.saved_palette[8]==0xff80 && b->state.local_palette[9]==128);
	unsigned calls=clock_calls; b->rotation.counter=91;
	string(&f,"I#X>8,+1,+1,1,0:X>8,3,4,+1,0:X 8,1,1,1,1:");
	CHECK(b->rotation.counter==0 && !b->rotation.active && clock_calls>calls && f.size==0); /* header sampling only */
	CHECK(b->state.bgi.palette[3]==10 && b->state.bgi.palette[4]==20);
	f.size=0; string(&f,"I#S>8,5:X 8,0,0,0,0:");
	CHECK(b->state.bgi.palette[8]==5 && b->state.bgi.palette[3]==10 && b->state.local_palette[9]==128 && f.size==0);
	setup(&f,2); string(&f,"I#X 8,4,3,1,0:");
	CHECK(b->state.bgi.palette[3]==5 && b->state.bgi.palette[4]==20 && b->state.local_palette[5]==5);
	string(&f,"I#X 8,0,1,1,0:"); CHECK(b->state.bgi.palette[0]==0 && b->state.bgi.palette[1]==16 && b->state.local_palette[1]==16);
	string(&f,"I#X 8,15,14,1,0:"); CHECK(b->state.bgi.palette[14]==0 && b->state.bgi.palette[15]==63 && b->state.local_palette[16]==0);
	destroy(&f);
}
static void deadlines_keys_and_wrap(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine; setup(&f,2);
	string(&f,"I#X 8,3,4,9,20:"); CHECK(b->rotation.active && b->rotation.counter==0 && b->state.bgi.palette[3]==20 && f.size==0);
	key(&f,(struct cterm_key){.kind=CTERM_KEY_TEXT,.value='A'});
	local_ns+=19999999; poll(&f); CHECK(b->rotation.active && reads==0 && b->input.count==1);
	++local_ns; poll(&f); CHECK(!b->rotation.active && b->rotation.counter==9 && reads==1 && b->input.count==0 && f.size==0);
	/* All native key identities cancel, including extended keys whose
	 * getch returns0. A plain NUL status does not cancel. */
	const struct cterm_key keys[]={ {.kind=CTERM_KEY_NORMAL,.local=0x4800}, {.kind=CTERM_KEY_CODEPAGE,.local=0xe9},
	    {.kind=CTERM_KEY_TEXT,.value=0x1f600}, {.kind=CTERM_KEY_DOORWAY,.local=3} };
	for (size_t i=0;i<sizeof(keys)/sizeof(keys[0]);++i) {
		setup(&f,2); string(&f,"I#X 8,3,4,9,1:"); key(&f,keys[i]); local_ns+=1000000; poll(&f);
		CHECK(!b->rotation.active && b->rotation.counter==9);
		CHECK(b->input.scan_pending==(i==0)); b->input.scan_pending=false;
	}
	setup(&f,2); string(&f,"I#X 8,3,4,9,1:"); key(&f,(struct cterm_key){.kind=CTERM_KEY_TEXT,.value=0});
	local_ns+=1000000; poll(&f); CHECK(b->rotation.active && b->rotation.counter==1 && b->input.count==1);
	struct cterm_big_event discarded; CHECK(cterm_big_input_take(b,&discarded));
	key(&f,(struct cterm_key){.kind=CTERM_KEY_TEXT,.value='B'}); read_error=IO_ERROR_IN_PROGRESS;
	local_ns+=1000000; poll(&f); unsigned pending_reads=reads; uint16_t current=b->state.bgi.palette[3];
	poll(&f); CHECK(b->rotation.active && b->rotation.counter==1 && reads==pending_reads+1 && b->state.bgi.palette[3]==current);
	read_error=0; poll(&f); CHECK(!b->rotation.active && b->rotation.counter==9);
	/* A newly ready hardware event can authorize consumption of the old
	 * private scan; the new event remains available for ordinary replay. */
	setup(&f,2); string(&f,"I#X 8,3,4,9,1:"); b->input.scan=0x48; b->input.scan_pending=true;
	local_ns+=1000000; poll(&f); CHECK(b->rotation.active && b->input.scan_pending);
	key(&f,(struct cterm_key){.kind=CTERM_KEY_TEXT,.value='C'}); local_ns+=1000000; poll(&f);
	CHECK(!b->rotation.active && !b->input.scan_pending && f.output[f.size-1]=='C');
	setup(&f,2); string(&f,"I#X 8,3,4,9,0:"); CHECK(!b->rotation.active && b->rotation.counter==10);
	/* Inject a supplied WORD count after normal parsing: wire reader has
	 * four digit slots, so decimal32767 is deliberately not used on wire. */
	string(&f,"I#X 8,3,4,9,1:"); b->rotation.colors.count=32767; b->rotation.counter=32767;
	local_ns+=1000000; poll(&f); CHECK(b->rotation.active && b->rotation.counter==32768);
	key(&f,(struct cterm_key){.kind=CTERM_KEY_TEXT,.value='D'}); local_ns+=1000000; poll(&f); CHECK(!b->rotation.active && b->rotation.counter==32767);
	destroy(&f);
}
static void errors_eof_reset(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine; setup(&f,2);
	string(&f,"I#X 8,3,4,9,+1:"); CHECK(b->rotation.active && b->rotation.deadline==local_ns+UINT64_C(65535000000));
	CHECK(st_cterm_finish(f.terminal)==0 && !b->rotation.active && f.size==0 && b->state.bgi.palette[3]==20);
	setup(&f,2); string(&f,"I#X 8,3,4,9,1:"); CHECK(st_cterm_reset_terminal(f.terminal)==0 && !b->rotation.active && f.size==0 && b->state.bgi.palette[3]==20);
	setup(&f,2); string(&f,"I#X 8,3,4,9,1:"); --local_ns; poll(&f);
	CHECK(!b->rotation.active && b->last_error==IO_ERROR_OUT_OF_RANGE && b->rejected_commands==1 && f.size==0);
	string(&f,"I#S 5,262:\033[6n"); CHECK(b->state.bgi.palette[5]==6 && f.size==6);
	setup(&f,2); string(&f,"I#X 8,3,4,9,1:"); clock_error=IO_ERROR_IN_PROGRESS; poll(&f); clock_error=0;
	CHECK(!b->rotation.active && b->last_error==IO_ERROR_PROTOCOL && f.size==0);
	setup(&f,2); local_ns=UINT64_MAX-100; string(&f,"I#X 8,3,4,9,1:");
	CHECK(b->rotation.deadline==UINT64_MAX && b->rotation.active); local_ns=UINT64_MAX;
	key(&f,(struct cterm_key){.kind=CTERM_KEY_TEXT,.value='A'}); poll(&f); CHECK(!b->rotation.active);
	destroy(&f);
}
static void flow_footer_and_sources(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine; setup(&f,2);
	b->control_io.ready=ready; blocked=true; blocked_kind=ST_BIG_OUTPUT_XOFF;
	string(&f,"I#X 8,3,4,1,0:");
	CHECK(!b->rotation.active && b->rotation.counter==2 && b->state.bgi.palette[3]==10 && b->state.bgi.palette[4]==7 && f.size==0);
	blocked=false;
	setup(&f,2); string(&f,"I#X 8,3,4,9,1:"); CHECK(f.size==0);
	b->numeric.loop.active=1; key(&f,(struct cterm_key){.kind=CTERM_KEY_TEXT,.value='A'}); local_ns+=1000000;
	size_t budget=64,work=64; bool complete; const struct st_big_state_sink unused={0};
	CHECK(cterm_big_rotation_poll(b,&budget,&work,&unused,&complete)==0 && complete && f.size==0); /* no tail XON */
	CHECK(st_cterm_finish(f.terminal)==0); setup(&f,2);
	/* Converse: entry skipped XOFF, exit rereads loop0 and emits XON. */
	b->numeric.loop.active=1;
	const struct st_graphics_argument args[]={ {.has_value=true,.value=8},{.has_value=true,.value=3},
	    {.has_value=true,.value=4},{.has_value=true,.value=1},{.has_value=true,.value=0} };
	const struct st_graphics_command_view view={.code='X',.arguments=args,.count=5};
	CHECK(cterm_big_rotation_begin(b,&view)==IO_ERROR_IN_PROGRESS);
	/* Suspend before a turn, then change loop/count before the exit. */
	work=0; budget=64;
	CHECK(cterm_big_rotation_poll(b,&budget,&work,&unused,&complete)==IO_ERROR_IN_PROGRESS && f.size==0);
	b->numeric.loop.active=0; b->rotation.colors.count=0xffff; work=64;
	CHECK(cterm_big_rotation_poll(b,&budget,&work,&unused,&complete)==0 && complete && f.size==0 );
	CHECK(st_cterm_finish(f.terminal)==0); setup(&f,2);
	string(&f,"I#g 3:I#& 0,2,1,0,X@5,8,3,4,1,0:");
	/* Constant-only source steps at index0, so its final record has
	 * already cleared loop before X8's independent flow checks. */
	CHECK(!b->rotation.active && b->state.bgi.palette[3]==7 && b->state.bgi.palette[4]==7 && f.size==0);
	setup(&f,2); f.size=0;
	string(&f,"I#"); b->control.input.pending=1; b->control.input.count=1; b->control.input.payload[1]='!';
	string(&f,"X>8,3,4,1,0:S 6,20:\033[6n");
	CHECK(!b->rotation.active && b->control.input.pending==0 && f.size==7 && memcmp(f.output,"!\033[1;1R",7)==0);
	/* Acquiring the fourth argument is the first point allowed to capture. */
	string(&f,"I#X 8,1,1,1,"); b->state.bgi.palette[3]=42; CHECK(b->state.saved_palette[3]==10);
	string(&f,"1:"); CHECK(b->state.saved_palette[3]==42);
	destroy(&f);
}
int main(void) { partitions_and_owners(); capture_restore_and_skips(); deadlines_keys_and_wrap(); errors_eof_reset(); flow_footer_and_sources(); return 0; }
