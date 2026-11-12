/* Independent native BIG < input/zone/hybrid expectations:32/64/81. */
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
	unsigned resizes, bells; uint16_t bell_mode;
	bool status, reset_bell;
	uint32_t heights[32]; uint8_t output[2048]; size_t size;
};
static uint64_t local_ns;
static int now(void *context, uint64_t *value) { (void)context; *value=local_ns; return 0; }
static void bell(void *context)
{
	struct cterm_big *b=context; struct fixture *f=b->terminal->api_output_context;
	++f->bells; f->bell_mode=b->state.bgi.write_mode; if (f->reset_bell) CHECK(st_cterm_reset_terminal(f->terminal)==0);
}
static void output(void *context, enum st_cterm_output_origin origin, const uint8_t *bytes, size_t size)
{
	struct fixture *f=context; (void)origin; CHECK(size<=sizeof(f->output)-f->size);
	memcpy(f->output+f->size,bytes,size); f->size+=size;
}
static void resized(void *context, uint32_t columns, uint32_t rows, uint32_t width, uint32_t height)
{
	struct fixture *f=context;
	CHECK(columns==80 && width==640 && rows==(height==480?30u:25u)-(f->status?1u:0u));
	CHECK(f->resizes<32); f->heights[f->resizes++]=height;
}
static void create(struct fixture *f, bool utf8, bool status)
{
	memset(f,0,sizeof(*f)); local_ns=1000; f->status=status;
	const struct st_console_display_config display={.backend=ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry={.columns=80,.rows=25,.cell_width=8,.cell_height=14};
	CHECK(st_console_create(&f->console,&display,&geometry)==0);
	CHECK(st_font_catalog_create(&f->fonts,f->console)==0);
	const struct st_cterm_config config={.console=f->console,.fonts=f->fonts,
	    .emulation=ST_CTERM_ANSI_BBS,.graphics_mode=ST_CTERM_GRAPHICS_BIG_103,
	    .encoding=utf8?ST_CTERM_ENCODING_UTF8:ST_CTERM_ENCODING_CODEPAGE,.codepage=ST_CODEPAGE_CP437,
	    .quiet=true,.status_line=status,.scrollback_lines=2,.output=output,.output_context=f,
	    .size_changed=resized,.size_context=f};
	CHECK(st_cterm_create(&f->terminal,&config)==0 && f->resizes==1 && f->heights[0]==350);
	f->terminal->big_engine->numeric.io.now_ns=now; f->terminal->big_engine->numeric.io.bell=bell;
}
static void destroy(struct fixture *f)
{
	st_cterm_destroy(&f->terminal); st_font_catalog_destroy(f->fonts); st_console_destroy(f->console);
}
static void feed(struct fixture *f, const void *wire, size_t size)
{
	size_t used; int status=st_cterm_write(f->terminal,wire,size,&used);
	CHECK((status==0 || status==IO_ERROR_IN_PROGRESS) && used==size);
}
static void string(struct fixture *f, const char *wire) { feed(f,wire,strlen(wire)); }
static void one_key(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#< 1,0,0:");
	CHECK(st_cterm_input_paused(f.terminal) && b->control.input.pending==1 && b->control.input.count==0);
	CHECK(st_cterm_handle_key(f.terminal,'A',0) && st_cterm_poll(f.terminal)==0);
	CHECK(!st_cterm_input_paused(f.terminal) && f.size==2 && memcmp(f.output,"A\r",2)==0);
	destroy(&f);
}
static void key(struct fixture *f, int code)
{
	CHECK(st_cterm_handle_key(f->terminal,code,0)); CHECK(st_cterm_poll(f->terminal)==0);
	for (unsigned i=0;i<16 && !f->terminal->big_engine->collect.active && st_cterm_input_paused(f->terminal);++i)
		CHECK(st_cterm_poll(f->terminal)==0);
}
static bool mouse(struct fixture *f, int x, int y, uint32_t buttons, enum st_console_mouse_action action)
{
	const struct st_console_event e={.type=ST_CONSOLE_EVENT_MOUSE,.data.mouse={
	    .action=action,.pixel_x=x,.pixel_y=y,.x=x/8,.y=y/14,.buttons=buttons}};
	st_cterm_observe_mouse(f->terminal,&e); return st_cterm_handle_mouse(f->terminal,&e);
}
static void canvas(struct fixture *f, bool pointer, unsigned cx, unsigned cy)
{
	struct cterm_big *b=f->terminal->big_engine; struct st_pixel_buffer *pixels=NULL;
	const struct st_console_rect r={0,0,640,b->state.bgi.height};
	CHECK(st_console_get_pixels(&pixels,f->console,&r,false)==0);
	for (unsigned y=0;y<r.height;++y) for (unsigned x=0;x<640;++x) {
		bool on=pointer && (((x==cx && y+3>=cy && y<=cy+3) || (y==cy && x+4>=cx && x<=cx+4)) && !(x==cx && y==cy));
		CHECK(pixels->primary[(size_t)y*640+x]==(on?815u:800u) && pixels->alternate[(size_t)y*640+x]==(on?815u:800u));
	}
	st_pixel_buffer_destroy(pixels);
}
static void partitions_and_prefixes(void)
{
	const char wire[]="I#< 1,0,0:";
	for (unsigned utf8=0;utf8<2;++utf8) for (unsigned status=0;status<2;++status)
		for (size_t split=0;split<sizeof(wire);++split) {
			struct fixture f; create(&f,utf8,status); feed(&f,wire,split); feed(&f,wire+split,sizeof(wire)-1-split);
			CHECK(st_cterm_input_paused(f.terminal)); key(&f,'K');
			CHECK(f.size==2 && memcmp(f.output,"K\r",2)==0 && f.terminal->xpos==1);
			destroy(&f);
		}
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	for (size_t i=0;i<sizeof(wire)-1;++i) feed(&f,wire+i,1); key(&f,'Q'); CHECK(f.size==2);
	string(&f,"I#"); b->control.input.pending=1; b->control.input.count=2; memcpy(b->control.input.payload+1,"XY",2);
	string(&f,"< 256,"); CHECK(b->control.input.append_cr==0 && b->control.input.pending==1 && b->control.input.count==2);
	CHECK(st_cterm_finish(f.terminal)==0 && b->control.input.count==2 && b->control.input.payload[1]=='X');
	const char tail[]="I#< 0,0,0:Z"; size_t used;
	CHECK(st_cterm_write(f.terminal,tail,sizeof(tail)-1,&used)==IO_ERROR_IN_PROGRESS && used==sizeof(tail)-2);
	key(&f,'B'); feed(&f,tail+used,sizeof(tail)-1-used); CHECK(f.terminal->xpos==2);
	destroy(&f);
}
static void keyboard_echo_and_replay(void)
{
	struct fixture f; create(&f,true,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"\033[10;10HI#< 0,1,1:");
	const int keys[]={'A','B',8,'C',13,'Z'};
	for (size_t i=0;i<sizeof(keys)/sizeof(keys[0]);++i) CHECK(st_cterm_handle_key(f.terminal,keys[i],0));
	CHECK(st_cterm_poll(f.terminal)==0 && f.size==3 && memcmp(f.output,"ACZ",3)==0);
	CHECK(f.terminal->xpos==13 && f.terminal->ypos==10 && b->control.input.count==2);
	struct st_console_cell cells[3]; const struct st_console_rect r={9,9,3,1};
	CHECK(st_console_read_cells(f.console,&r,cells,3)==0 && cells[0].glyph=='A' && cells[1].glyph=='B' && cells[2].glyph=='C');
	f.size=0; string(&f,"I#< 1,0,2:"); key(&f,'D'); CHECK(f.size==0 && f.terminal->xpos==14);
	string(&f,"I#< 1,0,3:"); key(&f,'E'); CHECK(f.size==0 && f.terminal->xpos==14);
	string(&f,"I#< 0,0,3:"); key(&f,8); CHECK(f.size==0 && f.terminal->xpos==14);
	string(&f,"\033[1;80HI#< 0,0,1:"); key(&f,'F'); CHECK(f.terminal->xpos==1 && f.terminal->ypos==2 && f.size==1 && f.output[0]=='F');
	destroy(&f);
}
static void keyboard_raw_bounds_sources_and_reset(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#< 0,1,0:");
	for (unsigned i=0;i<130;++i) key(&f,'A');
	CHECK(b->control.input.count==129); key(&f,13); CHECK(f.size==129);
	f.size=0; string(&f,"I#< 1,+1,0:"); CHECK(st_cterm_handle_codepage_key(f.terminal,255)); CHECK(st_cterm_poll(f.terminal)==0 && f.size==1 && f.output[0]==13);
	f.size=0; string(&f,"I#< 0,0,0:"); key(&f,ST_CONSOLE_KEY_UP); CHECK(f.size==0 && b->input.scan_pending);
	string(&f,"I#< 0,1,0:"); CHECK(b->control.input.count==1 && b->control.input.payload[1]=='H'); key(&f,13); CHECK(f.size==1 && f.output[0]=='H');
	f.size=0; string(&f,"I#< 0,1,0:"); key(&f,127); key(&f,'A'); key(&f,13); CHECK(f.size==0 && b->control.input.payload[0]=='A');
	string(&f,"I#< 0,1,0:"); key(&f,127); key(&f,127);
	CHECK(st_cterm_handle_key(f.terminal,'B',0)); CHECK(st_cterm_poll(f.terminal)==IO_ERROR_OUT_OF_RANGE && b->control.input.count==65535 && !b->collect.active && !b->control.input.pending);
	string(&f,"I#< 0,1,0:"); key(&f,'C'); CHECK(st_cterm_finish(f.terminal)==0 && !b->collect.active && b->control.input.payload[1]=='C' && f.size==0);
	string(&f,"I#< 0,1,0:"); key(&f,'D'); CHECK(st_cterm_reset_terminal(f.terminal)==0 && !b->collect.active && b->control.input.payload[1]=='D' && f.size==0);
	string(&f,"I#& 0,2,1,0,<@3,0,0,0:"); CHECK(b->collect.active); key(&f,'A'); CHECK(b->collect.active && f.size==1 && f.output[0]==19);
	key(&f,'B'); CHECK(!b->collect.active && f.size==3 && memcmp(f.output,"\x13\x11" "B",3)==0);
	destroy(&f);
}
static void hybrid_pointer(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#X 4,0,112,60,112,60,3:"); const uint8_t payload[]={0,'|','~','!'}; feed(&f,payload,sizeof(payload));
	string(&f,"I#T 1,2,1:I#< 1,2,0:"); CHECK(st_cterm_mouse_mode(f.terminal)==4 && b->collect.x==318 && b->collect.y==98 && b->state.bgi.x==322 && b->state.bgi.write_mode==1);
	canvas(&f,true,318,98);
	CHECK(mouse(&f,100,60,0,ST_CONSOLE_MOUSE_MOVE)); canvas(&f,true,100,60);
	CHECK(st_cterm_handle_key(f.terminal,ST_CONSOLE_KEY_RIGHT,0));
	CHECK(mouse(&f,100,60,ST_CONSOLE_MOUSE_BUTTON_LEFT,ST_CONSOLE_MOUSE_BUTTON));
	CHECK(b->collect.x==112 && b->collect.y==60 && b->collect.armed); canvas(&f,true,112,60);
	b->state.application.write_mode=1;
	CHECK(mouse(&f,100,60,0,ST_CONSOLE_MOUSE_BUTTON));
	CHECK(st_cterm_mouse_mode(f.terminal)==0 && !b->collect.active && b->state.bgi.write_mode==1 && f.size==4 && memcmp(f.output,"\0|~\r",4)==0);
	canvas(&f,false,0,0); bool visible=false; CHECK(st_console_get_pointer_visible(f.console,&visible)==0 && visible);
	destroy(&f);
}
static void mouse_suppression_and_confirmation(void)
{
	struct fixture f; create(&f,true,true); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#X 4,47,0,0,0,0,1:A!I#T 1,2,1:I#< 0,2,0:");
	CHECK(mouse(&f,50,60,1,ST_CONSOLE_MOUSE_BUTTON) && b->collect.armed);
	st_cterm_sync_mouse_buttons(f.terminal,0); CHECK(st_cterm_poll(f.terminal)==0 && b->collect.active && !b->collect.armed);
	CHECK(mouse(&f,50,60,0,ST_CONSOLE_MOUSE_BUTTON) && b->collect.active);
	CHECK(mouse(&f,50,340,1,ST_CONSOLE_MOUSE_BUTTON) && !b->collect.armed);
	CHECK(mouse(&f,50,60,1,ST_CONSOLE_MOUSE_BUTTON) && !b->collect.armed);
	CHECK(mouse(&f,50,60,0,ST_CONSOLE_MOUSE_BUTTON) && b->collect.active);
	const struct st_console_event ui={.type=ST_CONSOLE_EVENT_MOUSE,.data.mouse={.action=ST_CONSOLE_MOUSE_BUTTON,.pixel_x=70,.pixel_y=70,.buttons=1}};
	st_cterm_observe_mouse(f.terminal,&ui); CHECK(st_cterm_poll(f.terminal)==0 && !b->collect.armed);
	CHECK(mouse(&f,70,70,0,ST_CONSOLE_MOUSE_BUTTON) && b->collect.active);
	key(&f,ST_CONSOLE_KEY_RIGHT); CHECK(b->collect.x==62);
	CHECK(mouse(&f,-1,-1,1,ST_CONSOLE_MOUSE_BUTTON));
	CHECK(mouse(&f,70,70,0,ST_CONSOLE_MOUSE_BUTTON) && b->collect.x==62 && !b->collect.armed);
	CHECK(!mouse(&f,70,70,0,ST_CONSOLE_MOUSE_WHEEL));
	CHECK(st_cterm_handle_key(f.terminal,13,0) && st_cterm_handle_key(f.terminal,ST_CONSOLE_KEY_RIGHT,0));
	CHECK(st_cterm_poll(f.terminal)==0 && !b->collect.active && b->collect.x==62 && f.size==1 && f.output[0]=='A');
	canvas(&f,false,0,0);
	CHECK(!mouse(&f,-1,-1,0,ST_CONSOLE_MOUSE_MOVE));
	string(&f,"I#< 0,2,0:"); key(&f,ST_CONSOLE_KEY_RIGHT);
	CHECK(mouse(&f,70,70,1,ST_CONSOLE_MOUSE_BUTTON) && b->collect.x==330);
	CHECK(mouse(&f,70,70,0,ST_CONSOLE_MOUSE_BUTTON) && !b->collect.active && f.size==2);
	destroy(&f);
}
static void strict_retry_private_scan_and_cleanup(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#X 4,0,318,98,318,98,1:R!I#X 4,9998:I#T 1,2,1:I#< 0,2,0:");
	CHECK(mouse(&f,50,60,0,ST_CONSOLE_MOUSE_MOVE)); key(&f,13);
	CHECK(b->collect.active && b->collect.x==318 && b->collect.y==98 && f.bells==1 && f.bell_mode==0 && f.size==0);
	canvas(&f,true,318,98); key(&f,13); CHECK(f.size==1 && f.output[0]=='R'); canvas(&f,false,0,0);
	f.size=0; string(&f,"I#< 0,0,0:"); key(&f,ST_CONSOLE_KEY_UP); CHECK(b->input.scan_pending);
	string(&f,"I#< 0,2,0:"); CHECK(b->input.scan_pending && b->collect.y==98); key(&f,13);
	CHECK(f.size==1 && f.output[0]=='R' && b->input.scan_pending);
	string(&f,"I#< 0,0,0:"); CHECK(f.size==2 && f.output[1]=='H' && !b->collect.active);
	string(&f,"I#< 0,2,0:"); CHECK(mouse(&f,318,98,1,ST_CONSOLE_MOUSE_BUTTON));
	CHECK(st_cterm_reset_terminal(f.terminal)==0 && !b->collect.active && !b->collect.armed); canvas(&f,false,0,0);
	CHECK(!mouse(&f,318,98,0,ST_CONSOLE_MOUSE_BUTTON) && f.size==2);
	string(&f,"I#< 0,2,0:"); CHECK(st_cterm_finish(f.terminal)==0 && !b->collect.active); canvas(&f,false,0,0);
	bool visible=false; CHECK(st_console_get_pointer_visible(f.console,&visible)==0 && visible);
	destroy(&f);
}
static void early_key_and_mouse_overflow(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#< 1,"); CHECK(st_cterm_handle_key(f.terminal,'P',0) && f.size==0 && b->input.count==1);
	string(&f,"0,0:"); CHECK(f.size==2 && memcmp(f.output,"P\r",2)==0);
	f.size=0; string(&f,"I#X 4,47,0,0,0,0,1:A!I#T 1,2,1:I#< 0,2,0:");
	for (unsigned i=0;i<CTERM_BIG_INPUT_KEYS;++i) CHECK(st_cterm_handle_key(f.terminal,'x',0));
	b->feeding=true;
	CHECK(mouse(&f,50,60,1,ST_CONSOLE_MOUSE_BUTTON)); CHECK(mouse(&f,50,60,0,ST_CONSOLE_MOUSE_BUTTON));
	b->feeding=false;
	CHECK(st_cterm_poll(f.terminal)==0 && b->collect.active && !b->collect.armed && f.size==0);
	CHECK(mouse(&f,50,60,1,ST_CONSOLE_MOUSE_BUTTON)); CHECK(mouse(&f,50,60,0,ST_CONSOLE_MOUSE_BUTTON));
	CHECK(!b->collect.active && f.size==1 && f.output[0]=='A');
	destroy(&f);
}
static int invalid_pixel(void *context, const struct st_pixel_buffer *pixels, struct st_console_rect *changed)
{
	(void)context; pixels->primary[57*pixels->width+50]=5;
	*changed=(struct st_console_rect){50,57,1,1}; return 0;
}
static void failure_and_destroy_cleanup(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#T 1,2,1:I#< 0,2,0:");
	CHECK(st_console_modify_pixels(f.console,invalid_pixel,NULL)==0);
	CHECK(mouse(&f,50,60,0,ST_CONSOLE_MOUSE_MOVE));
	CHECK(st_cterm_poll(f.terminal)==IO_ERROR_OUT_OF_RANGE && !b->collect.active && !b->control.input.pending && f.size==0);
	bool visible=false; CHECK(st_console_get_pointer_visible(f.console,&visible)==0 && visible && b->state.bgi.write_mode==0);
	string(&f,"I#< 0,0,0:"); key(&f,'Z'); CHECK(f.size==1 && f.output[0]=='Z');
	destroy(&f);
	create(&f,false,false); b=f.terminal->big_engine;
	string(&f,"I#X 4,9998:I#T 1,2,1:I#< 0,2,0:"); f.reset_bell=true;
	key(&f,13); CHECK(f.bells==1 && !b->collect.active && f.size==0 && b->state.bgi.write_mode==0);
	canvas(&f,false,0,0);
	string(&f,"I#< 0,2,0:"); CHECK(st_cterm_mouse_mode(f.terminal)==4);
	st_cterm_destroy(&f.terminal); CHECK(st_console_get_pointer_visible(f.console,&visible)==0 && visible);
	st_font_catalog_destroy(f.fonts); st_console_destroy(f.console);
}
static void stale_mouse_generation(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#X 4,47,0,0,0,0,1:A!I#< 0,2,0:");
	uint64_t old=b->collect.generation; key(&f,13); CHECK(f.size==1);
	string(&f,"I#< 0,2,0:");
	const struct cterm_big_event stale={.mouse=true,.data.sample={.x=50,.y=60,.buttons=1,.generation=old,.allowed=true}};
	CHECK(cterm_big_input_push(b,&stale) && st_cterm_poll(f.terminal)==0 && !b->collect.armed);
	CHECK(mouse(&f,50,60,0,ST_CONSOLE_MOUSE_BUTTON) && b->collect.active && f.size==1);
	CHECK(mouse(&f,50,60,1,ST_CONSOLE_MOUSE_BUTTON)); CHECK(mouse(&f,50,60,0,ST_CONSOLE_MOUSE_BUTTON));
	CHECK(!b->collect.active && f.size==2);
	destroy(&f);
}
static void wire_writing_mode_lifetime(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#M 2:I#I 1:"); CHECK(b->state.bgi.write_mode==1 && b->state.application.write_mode==1);
	string(&f,"I#R 2:I#I 1:I#T 1,2,1:"); CHECK(b->state.bgi.write_mode==0 && b->state.application.write_mode==1);
	string(&f,"I#< 0,2,0:"); key(&f,13);
	CHECK(!b->collect.active && b->state.bgi.write_mode==1 && b->state.application.write_mode==1 && f.size==0); canvas(&f,false,0,0);
	string(&f,"I#M 1:I#< 0,2,0:"); CHECK(b->state.bgi.write_mode==1 && b->state.application.write_mode==0);
	key(&f,13); CHECK(b->state.bgi.write_mode==0 && b->state.application.write_mode==0); canvas(&f,false,0,0);
	string(&f,"I#M 3:I#R 2:I#< 0,2,0:"); CHECK(st_cterm_finish(f.terminal)==0);
	CHECK(b->state.bgi.write_mode==1 && b->state.application.write_mode==1); canvas(&f,false,0,0);
	string(&f,"I#R 2:I#< 0,2,0:"); CHECK(st_cterm_reset_terminal(f.terminal)==0);
	CHECK(b->state.bgi.write_mode==1 && b->state.application.write_mode==1); canvas(&f,false,0,0);
	string(&f,"I#I 0:"); CHECK(b->state.bgi.write_mode==0 && b->state.application.write_mode==0 && f.size==0);
	destroy(&f);
}
int main(void)
{
	one_key(); partitions_and_prefixes(); keyboard_echo_and_replay(); keyboard_raw_bounds_sources_and_reset();
	hybrid_pointer(); mouse_suppression_and_confirmation(); strict_retry_private_scan_and_cleanup(); early_key_and_mouse_overflow(); failure_and_destroy_cleanup(); stale_mouse_generation(); wire_writing_mode_lifetime(); return 0;
}
