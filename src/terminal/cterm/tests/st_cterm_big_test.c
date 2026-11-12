/* Independent BIG live I/framing expectations: references8/27/43/54. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm.h"
#include "cterm_big_internal.h"
#include "st_cterm.h"
#include "io_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
struct fixture { st_console_t *console; st_font_catalog_t *fonts; st_cterm_t *terminal; unsigned bells, resizes; bool reset, block_output, reset_output, reset_ready; uint8_t output[1024]; enum st_cterm_output_origin origins[1024]; size_t output_size; };
static uint64_t local_ns;
static int clock_ns(void *context, uint64_t *value) { (void)context; *value = local_ns; return 0; }
static void bell(void *context) { struct cterm_big *b = context; ++((struct fixture *)b->terminal->api_input_filter_context)->bells; }
static int filter(void *context, uint32_t scalar, bool *consumed, char *replacement, size_t capacity, size_t *size)
{
	struct fixture *f = context; (void)replacement; (void)capacity; *consumed = false; *size = 0;
	if (f->reset && scalar == '!') { f->reset = false; CHECK(st_cterm_reset_terminal(f->terminal) == 0); *consumed = true; }
	return 0;
}
static void output(void *context, enum st_cterm_output_origin origin, const uint8_t *bytes, size_t size)
{
	struct fixture *f=context;
	CHECK(size<=sizeof(f->output)-f->output_size);
	for (size_t i=0;i<size;++i) f->origins[f->output_size+i]=origin;
	memcpy(f->output+f->output_size,bytes,size); f->output_size+=size;
	if (f->reset_output) { f->reset_output=false; CHECK(st_cterm_reset_terminal(f->terminal)==0); }
}
static void resized(void *context, uint32_t columns, uint32_t rows, uint32_t width, uint32_t height)
{
	struct fixture *f=context; (void)rows; CHECK(columns==80 && width==640 && height==350); ++f->resizes;
}
static int ready_output(void *context, enum st_big_output_kind kind)
{
	struct cterm_big *b=context; struct fixture *f=b->terminal->api_input_filter_context;
	if (kind==ST_BIG_OUTPUT_REPLY) CHECK(b->control.input.pending==0);
	if (f->reset_ready) { f->reset_ready=false; CHECK(st_cterm_reset_terminal(f->terminal)==0); }
	return f->block_output?IO_ERROR_IN_PROGRESS:0;
}
static void create(struct fixture *f, bool utf8, bool status)
{
	memset(f,0,sizeof(*f)); local_ns = 1000;
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry = {.columns=80,.rows=25,.cell_width=8,.cell_height=14};
	CHECK(st_console_create(&f->console,&display,&geometry) == 0);
	CHECK(st_font_catalog_create(&f->fonts,f->console) == 0);
	const struct st_cterm_config config = {
		.console=f->console,.fonts=f->fonts,.font_selector=0,.emulation=ST_CTERM_ANSI_BBS,
		.graphics_mode=ST_CTERM_GRAPHICS_BIG_103,.encoding=utf8?ST_CTERM_ENCODING_UTF8:ST_CTERM_ENCODING_CODEPAGE,
		.codepage=ST_CODEPAGE_CP437,.quiet=true,.status_line=status,.scrollback_lines=2,
		.input_filter=filter,.input_filter_context=f,.output=output,.output_context=f,
		.size_changed=resized,.size_context=f,
	};
	CHECK(st_cterm_create(&f->terminal,&config) == 0);
	f->terminal->big_engine->numeric.io.now_ns = clock_ns;
	f->terminal->big_engine->numeric.io.bell = bell;
	f->terminal->big_engine->control_io.ready=ready_output;
}
static void destroy(struct fixture *f)
{
	st_cterm_destroy(&f->terminal); st_font_catalog_destroy(f->fonts); st_console_destroy(f->console);
}
static void feed(struct fixture *f, const void *wire, size_t size)
{
	size_t used; CHECK(st_cterm_write(f->terminal,wire,size,&used) == 0 && used == size);
}
static void string(struct fixture *f, const char *wire) { feed(f,wire,strlen(wire)); }
static void palette(struct fixture *f)
{
	static const uint32_t expected[] = {0,0xaa,0xaa00,0xaaaa,0xaa0000,0xaa00aa,0xaa5500,0xaaaaaa,0x555555,0x5555ff,0x55ff55,0x55ffff,0xff5555,0xff55ff,0xffff55,0xffffff};
	uint32_t actual[16]; CHECK(st_console_get_palette(f->console,CTERM_BIG_PALETTE_FIRST,actual,16) == 0);
	CHECK(memcmp(expected,actual,sizeof(expected)) == 0);
}
static void scene(struct fixture *f)
{
	/* Output has stopped; all observations refer to this single stable scene. */
	struct st_console_cell cells[2]; struct st_pixel_buffer *pixels = NULL;
	const struct st_console_rect rect = {2,1,2,1}, raster = {0,0,32,28};
	CHECK(st_console_read_cells(f->console,&rect,cells,2) == 0);
	if (cells[0].glyph != 219 || cells[0].foreground != CTERM_BIG_PALETTE_FIRST+4) fprintf(stderr,"actual glyph=%u fg=%u bg=%u cursor=%d,%d\n",cells[0].glyph,cells[0].foreground,cells[0].background,f->terminal->xpos,f->terminal->ypos);
	CHECK(cells[0].glyph == 219 && cells[0].foreground == CTERM_BIG_PALETTE_FIRST+4);
	CHECK(cells[1].glyph == 'Z' && f->terminal->xpos == 5 && f->terminal->ypos == 2);
	CHECK(st_console_get_pixels(&pixels,f->console,&raster,false) == 0);
	for (unsigned y=0;y<14;++y) for (unsigned x=0;x<8;++x) {
		size_t i=(y+14)*32+x+16; CHECK(pixels->primary[i] == CTERM_BIG_PALETTE_FIRST+4 && pixels->alternate[i] == pixels->primary[i]);
	}
	/* Original IBM 8x14 A rows, independently transcribed from ROM data. */
	static const uint8_t a[14]={0,0,0x10,0x38,0x6c,0xc6,0xc6,0xfe,0xc6,0xc6,0xc6,0,0,0};
	for (unsigned y=0;y<14;++y) for (unsigned x=0;x<8;++x) {
		uint32_t expected=CTERM_BIG_PALETTE_FIRST+((a[y]&(0x80u>>x))?7u:0u);
		CHECK(pixels->primary[y*32+x]==expected && pixels->alternate[y*32+x]==expected);
	}
	st_pixel_buffer_destroy(pixels); palette(f);
}
static void partitions(void)
{
	for (unsigned utf8=0;utf8<2;++utf8) for (unsigned status=0;status<2;++status) {
		const char *glyph = utf8?"\xe2\x96\x88":"\xdb";
		char wire[64]; int n=snprintf(wire,sizeof(wire),"A\033[2;3H\033[31m%sI#I 1:Z",glyph);
		CHECK(n>0 && (size_t)n<sizeof(wire));
		for (size_t split=0;split<=(size_t)n+1;++split) {
			struct fixture f; create(&f,utf8!=0,status!=0);
			f.terminal->big_engine->state.application.marker=7;
			if (split==(size_t)n+1) { for (int i=0;i<n;++i) feed(&f,wire+i,1); }
			else { feed(&f,wire,split); feed(&f,wire+split,(size_t)n-split); }
			CHECK(st_cterm_finish(f.terminal) == 0); scene(&f);
			CHECK(f.terminal->big_engine->state.application.marker==7);
			CHECK(f.terminal->height==(status?24:25)); destroy(&f);
		}
	}
}
static void framing_and_state(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	b->state.application.marker=7;
	const uint8_t filtered[] = {'I',0xff,'#','I',0, '1',':','A'};
	feed(&f,filtered,sizeof(filtered)); CHECK(b->state.application.marker==7 && f.terminal->xpos==2);
	/* New frames reset frame locals; chained pairs keep them. */
	string(&f,"I#I>"); b->state.memory_page=3;
	string(&f,"1:I 1:"); CHECK(b->state.memory_page==3);
	string(&f,"I#I 1:"); CHECK(b->state.memory_page==1);
	b->state.application.marker=7;
	const uint8_t numeric_ff[]={'I','#','I',' ',0xff};
	feed(&f,numeric_ff,sizeof(numeric_ff)); CHECK(b->state.application.marker==0);
	/* Unknown pairs own no guessed payload. The chain still reaches I. */
	b->state.application.marker=7; string(&f,"I#@>I 1:Q"); CHECK(b->state.application.marker==7);
	CHECK(f.terminal->xpos==2);
	/* I0 clears the real indexed raster and resets base cursor/attributes. */
	CHECK(st_console_set_pixel(f.console,100,100,ST_PIXEL_COLOR_RGB_TAG|0x123456) == 0);
	string(&f,"I#I 0:"); CHECK(b->state.application.marker==0 && f.terminal->xpos==1 && f.terminal->ypos==1 && f.terminal->attr==7);
	struct st_pixel_buffer *p=NULL; const struct st_console_rect point={100,100,1,1};
	CHECK(st_console_get_pixels(&p,f.console,&point,false)==0 && p->primary[0]==CTERM_BIG_PALETTE_FIRST && p->alternate[0]==p->primary[0]); st_pixel_buffer_destroy(p);
	CHECK(st_cterm_resize(f.terminal,81,25)==IO_ERROR_NOT_SUPPORTED);
	destroy(&f);
}
static void deadlines_and_recovery(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	b->state.application.marker=7; string(&f,"I");
	local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS*2; CHECK(st_cterm_poll(f.terminal)==0 && f.terminal->xpos==1);
	string(&f,"#I"); uint64_t pair_start=local_ns;
	local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS-1; CHECK(st_cterm_poll(f.terminal)==0 && b->pair_wait);
	local_ns=pair_start+ST_BIG_NUMERIC_TIMEOUT_NS;
	CHECK(st_cterm_poll(f.terminal)==0 && !b->pair_wait && b->state.application.marker==7);
	/* The identifier survives pair timeout and its own reader starts afresh. */
	CHECK(st_big_numeric_deadline(&b->numeric,&pair_start));
	local_ns=pair_start; CHECK(st_cterm_poll(f.terminal)==0 && b->state.application.marker==0);
	/* Late ready bytes retain original consume-before-timeout ownership. */
	b->state.application.marker=7; string(&f,"I#I");
	local_ns=b->pair_deadline; string(&f,">1:Z");
	CHECK(b->state.application.marker==7 && f.terminal->xpos==2);
	string(&f,"I#"); local_ns=b->pair_deadline; string(&f,"I1:");
	CHECK(b->state.application.marker==7 && !b->pair_wait);
	string(&f,"\033[HI#I 1"); CHECK(st_big_numeric_deadline(&b->numeric,&pair_start));
	local_ns=pair_start; string(&f,":Z");
	CHECK(b->state.application.marker==7 && f.terminal->xpos==2); /* Timeout retains the digit1. */
	string(&f,"I#I 0"); CHECK(st_big_numeric_deadline(&b->numeric,&pair_start));
	local_ns=pair_start; string(&f,":Z");
	CHECK(b->state.application.marker==0 && f.terminal->xpos==2);
	CHECK(f.bells==6); /* Two earlier timeouts, four late-byte timeouts. */
	/* No identifier at timeout yields only the unknown-pair footer. */
	b->state.application.marker=7; string(&f,"I#");
	local_ns=b->pair_deadline; CHECK(st_cterm_poll(f.terminal)==0);
	CHECK(b->state.application.marker==7 && !b->pair_wait);
	/* Backwards local pair samples fail and cancel only pending acquisition. */
	string(&f,"I#I"); --local_ns;
	CHECK(st_cterm_poll(f.terminal)==IO_ERROR_PROTOCOL && !b->pair_wait && b->state.application.marker==7);
	++local_ns;
	/* An unreachable random source pauses wire input but not session polls. */
	b->numeric.random_from=b->numeric.random_to=1;
	size_t accepted;
	CHECK(st_cterm_write(f.terminal,"I#I r:Z",7,&accepted)==IO_ERROR_IN_PROGRESS && accepted==6);
	CHECK(st_cterm_input_paused(f.terminal));
	CHECK(st_cterm_poll(f.terminal)==0 && st_cterm_input_paused(f.terminal));
	b->numeric.random_from=b->numeric.random_to=0;
	CHECK(st_cterm_poll(f.terminal)==0 && !st_cterm_input_paused(f.terminal));
	string(&f,"Z"); CHECK(f.terminal->xpos==2);
	b->state.application.marker=7;
	/* Explicit EOF never completes a partial pair or number. */
	b->state.application.marker=7; string(&f,"I#I 0"); CHECK(st_cterm_finish(f.terminal)==0 && b->state.application.marker==7);
	string(&f,"I#I 1:"); CHECK(b->state.application.marker==7);
	string(&f,"I#I"); CHECK(st_cterm_reset_terminal(f.terminal)==0);
	string(&f,"I#I 1:"); CHECK(b->state.application.marker==7);
	/* Reset from an ordinary-text semantic callback must not reenter parser. */
	f.reset=true; string(&f,"!"); CHECK(!f.reset && !b->feeding && !b->reset_pending);
	string(&f,"I#I 1:"); CHECK(b->state.application.marker==7);
	destroy(&f);
}
static void reply_and_footer(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	b->control.input.pending=1; b->control.input.count=4; b->control.input.append_cr=1;
	memcpy(b->control.input.payload+1,(uint8_t[]){0,0x7c,0x7e,0xff},4);
	b->numeric.loop.type='@'; string(&f,"I#I>1:");
	CHECK(f.output_size==0 && b->control.input.pending==1 && b->numeric.loop.type==' ');
	string(&f,"@ Z");
	CHECK(f.output_size==5 && memcmp(f.output,(uint8_t[]){0,0x7c,0x7e,0xff,13},5)==0);
	CHECK(b->control.input.pending==0 && b->control.frame.command==0 && f.terminal->xpos==2);
	for (size_t i=0;i<f.output_size;++i) CHECK(f.origins[i]==ST_CTERM_OUTPUT_USER);
	/* No reader call on an unknown pair means inherited loop1 does not repeat. */
	string(&f,"I#"); b->numeric.loop.active=1; b->numeric.loop.command_read=true;
	string(&f,"@ Z"); CHECK(b->numeric.loop.active==1 && f.terminal->xpos==3);
	CHECK(st_cterm_finish(f.terminal)==0 && b->numeric.loop.active==0);
	destroy(&f);
}
static void stored_repetition(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#"); b->state.memory_page=3; b->control.frame.did_loop=1;
	b->numeric.loop=(struct st_big_loop){.active=1,.count=1,.step=1,.to=2,.type='@',.words={0x8001}};
	b->control.input.pending=1; b->control.input.count=2;
	b->control.input.payload[1]=0; b->control.input.payload[2]=0xff; b->control.input.append_cr=1;
	string(&f,"I Z");
	CHECK(b->numeric.loop.active==0 && b->numeric.loop.current==2 && b->numeric.loop.varying_total==3);
	CHECK(f.resizes==3 && b->state.memory_page==3 && b->numeric.loop.type==' ');
	CHECK(f.output_size==4 && memcmp(f.output,(uint8_t[]){17,0,0xff,13},4)==0 && f.terminal->xpos==2);
	CHECK(f.origins[0]==ST_CTERM_OUTPUT_RESPONSE);
	for (size_t i=1;i<4;++i) CHECK(f.origins[i]==ST_CTERM_OUTPUT_USER);
	/* Normal large constant loop yields cooperatively without consuming tail. */
	string(&f,"I#"); b->numeric.loop=(struct st_big_loop){.active=1,.count=1,.step=1,.to=30000,.no_parameters=1,.type='@',.words={1}};
	b->control.frame.did_loop=1; b->control.input.pending=1; size_t used;
	CHECK(st_cterm_write(f.terminal,"I ZZ",4,&used)==IO_ERROR_IN_PROGRESS && used==2);
	CHECK(st_cterm_input_paused(f.terminal) && b->numeric.loop.current==65 && b->control.frame.did_loop==1 && f.output_size==4);
	CHECK(st_cterm_poll(f.terminal)==0 && b->numeric.loop.current==129 && st_cterm_input_paused(f.terminal));
	CHECK(st_cterm_finish(f.terminal)==0 && !st_cterm_input_paused(f.terminal) && b->control.input.pending==0 && b->numeric.loop.active==0 && f.output_size==4);
	destroy(&f);
}
static void gang_source_seams(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#"); b->control.frame.gang_marker='>'; b->control.frame.gang[0]='I'; b->control.frame.gang[1]='@'; b->control.frame.did_loop=1;
	b->numeric.loop=(struct st_big_loop){.active=1,.count=3,.step=1,.to=2,.no_parameters=1,.type=',',.words={1,0,1}};
	string(&f,"I Z");
	CHECK(b->numeric.loop.active==0 && b->numeric.loop.current==2 && b->numeric.loop.index==0 && b->numeric.loop.command=='I');
	CHECK(b->control.frame.gang_marker=='X' && f.output_size==1 && f.output[0]==17 && f.terminal->xpos==2);
	/* The selector itself ends looping; selected I then reads wire1. */
	string(&f,"I#"); b->state.application.marker=7; b->control.frame.gang_marker='>'; b->control.frame.gang[0]='I';
	b->numeric.loop=(struct st_big_loop){.active=1,.count=2,.step=1,.to=1,.no_parameters=1,.type=',',.command='W',.words={1,0}};
	string(&f,"I 1:Z"); CHECK(b->state.application.marker==7 && b->numeric.loop.active==0 && b->numeric.loop.command=='I' && f.terminal->xpos==3);
	destroy(&f);
}
static void footer_wait_reset(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	b->control.input.pending=1; b->control.input.count=2; memcpy(b->control.input.payload+1,"AB",2);
	f.block_output=true; size_t used;
	CHECK(st_cterm_write(f.terminal,"I#I 0:TAIL",10,&used)==IO_ERROR_IN_PROGRESS && used==6);
	CHECK(f.resizes==2 && b->control.input.pending==0 && st_cterm_input_paused(f.terminal));
	CHECK(st_cterm_poll(f.terminal)==0 && f.resizes==2 && f.output_size==0);
	f.block_output=false; CHECK(st_cterm_poll(f.terminal)==0 && !st_cterm_input_paused(f.terminal));
	CHECK(f.output_size==2 && memcmp(f.output,"AB",2)==0 && f.resizes==2);
	string(&f,"TAIL"); CHECK(f.terminal->xpos==5);
	b->control.input.pending=1; b->control.input.count=3; memcpy(b->control.input.payload+1,"XYZ",3);
	f.reset_output=true;
	CHECK(st_cterm_write(f.terminal,"I#I 1:TAIL",10,&used)==0 && used==6 && !f.reset_output);
	CHECK(f.output_size==3 && f.output[2]=='X' && !b->footer_active && b->control.input.pending==0);
	string(&f,"I#I 1:"); CHECK(f.output_size==3);
	b->control.input.pending=1; b->control.input.count=1; f.reset_ready=true;
	CHECK(st_cterm_write(f.terminal,"I#I 1:",6,&used)==0 && used==6);
	CHECK(f.output_size==3 && !b->footer_active && !f.reset_ready);
	b->control.input.pending=1; b->control.input.count=1; f.block_output=true;
	CHECK(st_cterm_write(f.terminal,"I#I 1:",6,&used)==IO_ERROR_IN_PROGRESS && used==6);
	CHECK(st_cterm_finish(f.terminal)==0 && !st_cterm_input_paused(f.terminal));
	f.block_output=false; CHECK(st_cterm_poll(f.terminal)==0 && f.output_size==3);
	/* A chained pair deadline starts after finishing flow has returned. */
	string(&f,"I#"); b->control.frame.did_loop=1; f.block_output=true;
	CHECK(st_cterm_write(f.terminal,"I>1:",4,&used)==IO_ERROR_IN_PROGRESS && used==4 && !b->pair_wait);
	local_ns+=2*ST_BIG_NUMERIC_TIMEOUT_NS;
	CHECK(st_cterm_poll(f.terminal)==0 && !b->pair_wait && f.bells==0);
	f.block_output=false;
	CHECK(st_cterm_poll(f.terminal)==0 && b->pair_wait && b->pair_deadline==local_ns+ST_BIG_NUMERIC_TIMEOUT_NS);
	CHECK(f.output_size==4 && f.output[3]==17 && f.origins[3]==ST_CTERM_OUTPUT_RESPONSE);
	string(&f,"I 1:"); CHECK(f.bells==0 && !b->pair_wait);
	destroy(&f);
}
int main(void)
{
	partitions(); framing_and_state(); deadlines_and_recovery(); reply_and_footer();
	stored_repetition(); gang_source_seams(); footer_wait_reset(); return 0;
}
