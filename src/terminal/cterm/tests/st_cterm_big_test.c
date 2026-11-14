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
	CHECK(st_cterm_poll(f.terminal)==0 && b->last_error==IO_ERROR_PROTOCOL && !b->pair_wait && b->state.application.marker==7);
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
	CHECK(f.output_size==3 && memcmp(f.output,(uint8_t[]){0,0xff,13},3)==0 && f.terminal->xpos==2);
	for (size_t i=0;i<3;++i) CHECK(f.origins[i]==ST_CTERM_OUTPUT_USER);
	/* Normal large constant loop yields cooperatively without consuming tail. */
	string(&f,"I#"); b->numeric.loop=(struct st_big_loop){.active=1,.count=1,.step=1,.to=30000,.no_parameters=1,.type='@',.words={1}};
	b->control.frame.did_loop=1; b->control.input.pending=1; size_t used;
	CHECK(st_cterm_write(f.terminal,"I ZZ",4,&used)==IO_ERROR_IN_PROGRESS && used==2);
	CHECK(st_cterm_input_paused(f.terminal) && b->numeric.loop.current==65 && b->control.frame.did_loop==1 && f.output_size==3);
	CHECK(st_cterm_poll(f.terminal)==0 && b->numeric.loop.current==129 && st_cterm_input_paused(f.terminal));
	CHECK(st_cterm_finish(f.terminal)==0 && !st_cterm_input_paused(f.terminal) && b->control.input.pending==0 && b->numeric.loop.active==0 && f.output_size==3);
	destroy(&f);
}
static void gang_source_seams(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#"); b->control.frame.gang_marker='>'; b->control.frame.gang[0]='I'; b->control.frame.gang[1]='@'; b->control.frame.did_loop=1;
	b->numeric.loop=(struct st_big_loop){.active=1,.count=3,.step=1,.to=2,.no_parameters=1,.type=',',.words={1,0,1}};
	string(&f,"I Z");
	CHECK(b->numeric.loop.active==0 && b->numeric.loop.current==2 && b->numeric.loop.index==0 && b->numeric.loop.command=='I');
	CHECK(b->control.frame.gang_marker=='X' && f.output_size==0 && f.terminal->xpos==2);
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
	/* Finishing legacy flow does not consult transport readiness. The
	 * chained pair deadline starts immediately and expires normally. */
	string(&f,"I#"); b->control.frame.did_loop=1; f.block_output=true;
	CHECK(st_cterm_write(f.terminal,"I>1:",4,&used)==0 && used==4 && b->pair_wait);
	CHECK(b->pair_deadline==local_ns+ST_BIG_NUMERIC_TIMEOUT_NS && f.output_size==3);
	local_ns+=2*ST_BIG_NUMERIC_TIMEOUT_NS;
	CHECK(st_cterm_poll(f.terminal)==0 && !b->pair_wait && f.bells==1 && f.output_size==3);
	f.block_output=false; string(&f,"I#I 1:"); CHECK(!b->pair_wait);
	destroy(&f);
}
static void key_loop(struct fixture *f)
{
	struct cterm_big *b=f->terminal->big_engine;
	string(f,"I#"); b->state.application.marker=7; b->control.frame.did_loop=1;
	b->numeric.loop=(struct st_big_loop){.active=1,.count=1,.step=1,.to=1000,
	    .delay=100,.no_parameters=1,.type='@',.words={1}};
	size_t used;
	CHECK(st_cterm_write(f->terminal,"I ",2,&used)==IO_ERROR_IN_PROGRESS && used==2);
	CHECK(b->numeric.loop.current==0 && st_cterm_input_paused(f->terminal));
}
static void local_keys(void)
{
	for (unsigned utf8=0;utf8<2;++utf8) for (unsigned cancel=0;cancel<2;++cancel) {
		struct fixture f; create(&f,utf8,false); struct cterm_big *b=f.terminal->big_engine;
		CHECK(f.resizes==1); /* Initial BIG profile installation. */
		key_loop(&f);
		CHECK(st_cterm_handle_key(f.terminal,cancel?24:3,ST_CONSOLE_MODIFIER_CONTROL));
		CHECK(st_cterm_handle_text_key(f.terminal,0x212b)); /* NFC becomes U+00c5. */
		CHECK(st_cterm_handle_key(f.terminal,ST_CONSOLE_KEY_UP,0));
		CHECK(st_cterm_handle_key(f.terminal,ST_CONSOLE_KEY_F3,ST_CONSOLE_MODIFIER_CONTROL));
		CHECK(st_cterm_handle_doorway_key(f.terminal,0x50));
		CHECK(st_cterm_handle_codepage_key(f.terminal,0xff));
		CHECK(f.output_size==0 && b->numeric.loop.active==1);
		CHECK(st_cterm_poll(f.terminal)==0 && b->numeric.loop.active==0);
		CHECK(b->numeric.loop.current==1 && b->state.application.marker==7 && f.resizes==1);
		const uint8_t expected_utf8[]={0xc3,0x85,27,'[','A',27,'[','1','3',';','5','~',27,'[','B',0xff};
		const uint8_t expected_cp[]={0x8f,27,'[','A',27,'[','1','3',';','5','~',27,'[','B',0xff};
		const uint8_t *expected=utf8?expected_utf8:expected_cp;
		size_t size=utf8?sizeof(expected_utf8):sizeof(expected_cp);
		CHECK(f.output_size==size && memcmp(f.output,expected,size)==0);
		for (size_t i=0;i<size;++i) CHECK(f.origins[i]==ST_CTERM_OUTPUT_USER);
		CHECK(st_cterm_poll(f.terminal)==0 && f.output_size==size);
		CHECK(st_cterm_handle_key(f.terminal,'z',0) && f.output_size==size+1 && f.output[size]=='z');
		destroy(&f);
	}
}
static void local_key_bytes(void)
{
	struct fixture f; create(&f,true,false); struct cterm_big *b=f.terminal->big_engine;
	key_loop(&f);
	CHECK(st_cterm_handle_key(f.terminal,ST_CONSOLE_KEY_UP,0));
	CHECK(st_cterm_poll(f.terminal)==0 && b->numeric.loop.current==1 && b->numeric.loop.active==1 && f.output_size==0);
	/* The first getch returns zero; its pending scan byte is not a new
	 * BIOS key. A later real key admits the scan read before that key. */
	CHECK(st_cterm_poll(f.terminal)==0 && b->numeric.loop.current==1);
	CHECK(st_cterm_handle_text_key(f.terminal,3));
	CHECK(st_cterm_poll(f.terminal)==0 && b->numeric.loop.current==3 && b->numeric.loop.active==0);
	CHECK(f.output_size==0);
	key_loop(&f);
	/* Unicode is converted to local CP437, never truncated to a C0 byte. */
	CHECK(st_cterm_handle_text_key(f.terminal,0x1f603));
	CHECK(st_cterm_handle_text_key(f.terminal,0xe9));
	CHECK(st_cterm_handle_codepage_key(f.terminal,0));
	CHECK(st_cterm_poll(f.terminal)==0 && b->numeric.loop.current==3 && b->numeric.loop.active==1 && f.output_size==0);
	CHECK(st_cterm_handle_codepage_key(f.terminal,24));
	CHECK(st_cterm_poll(f.terminal)==0 && b->numeric.loop.current==4 && b->numeric.loop.active==0 && f.output_size==0);
	destroy(&f);
}
static void local_key_lifecycle(void)
{
	struct fixture f; create(&f,true,false); struct cterm_big *b=f.terminal->big_engine;
	key_loop(&f); CHECK(st_cterm_handle_key(f.terminal,'a',0));
	CHECK(st_cterm_handle_text_key(f.terminal,0x1f603));
	CHECK(st_cterm_finish(f.terminal)==0 && st_cterm_poll(f.terminal)==0 && f.output_size==0);
	key_loop(&f); CHECK(st_cterm_handle_key(f.terminal,ST_CONSOLE_KEY_UP,0));
	CHECK(st_cterm_poll(f.terminal)==0 && b->numeric.loop.current==1);
	CHECK(st_cterm_reset_terminal(f.terminal)==0);
	key_loop(&f); CHECK(st_cterm_handle_key(f.terminal,3,0));
	CHECK(st_cterm_poll(f.terminal)==0 && b->numeric.loop.current==1 && f.output_size==0);
	/* A reset from the first replayed key discards the unread suffix. */
	key_loop(&f); CHECK(st_cterm_handle_key(f.terminal,3,0));
	CHECK(st_cterm_handle_key(f.terminal,'A',0) && st_cterm_handle_key(f.terminal,'B',0));
	b->control.frame.did_loop=0; f.reset_output=true;
	CHECK(st_cterm_poll(f.terminal)==0 && f.output_size==1 && f.output[0]=='A' && !f.reset_output);
	CHECK(st_cterm_poll(f.terminal)==0 && f.output_size==1);
	/* Saturation must never leak local input to the BBS. */
	key_loop(&f); CHECK(st_cterm_handle_key(f.terminal,3,0));
	for (unsigned i=0;i<100;++i) CHECK(st_cterm_handle_key(f.terminal,'Q',0));
	CHECK(f.output_size==1);
	CHECK(st_cterm_poll(f.terminal)==0 && f.output_size==64);
	for (size_t i=1;i<f.output_size;++i) CHECK(f.output[i]=='Q');
	destroy(&f);
}
static void local_key_return(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	key_loop(&f); b->numeric.loop.to=1;
	/* A real counted reply can still block the footer independently of
	 * legacy flow. Buffered local keys retain their original order. */
	b->control.input.pending=1; b->control.input.count=1; b->control.input.payload[1]='!';
	f.block_output=true;
	CHECK(st_cterm_handle_text_key(f.terminal,'A') && st_cterm_handle_text_key(f.terminal,'B'));
	CHECK(st_cterm_poll(f.terminal)==0 && b->numeric.loop.active==0 && b->footer_active && f.output_size==0);
	CHECK(st_cterm_handle_key(f.terminal,'C',0) && f.output_size==0);
	f.block_output=false;
	CHECK(st_cterm_poll(f.terminal)==0 && f.output_size==3 && memcmp(f.output,"!BC",3)==0);
	/* An inherited loop flag outside the interpreter cannot claim keys. */
	b->numeric.loop.active=1;
	CHECK(st_cterm_handle_key(f.terminal,3,0) && f.output_size==4 && f.output[3]==3);
	CHECK(st_cterm_finish(f.terminal)==0);
	/* Noncanonical active values supply zero without a local checkpoint. */
	string(&f,"I#"); b->numeric.loop.active=2;
	CHECK(st_cterm_handle_key(f.terminal,'D',0) && f.output_size==5 && f.output[4]=='D');
	CHECK(st_cterm_finish(f.terminal)==0);
	destroy(&f);
}
static void wire_loop_scene(struct fixture *f)
{
	struct cterm_big *b=f->terminal->big_engine;
	CHECK(b->numeric.loop.active==0 && b->numeric.loop.current==3);
	CHECK(b->numeric.loop.count==1 && b->numeric.loop.words[0]==1 && b->numeric.loop.no_parameters==1);
	CHECK(b->numeric.loop.random_suppression==0 && b->control.frame.did_loop==0);
	CHECK(b->state.application.marker==7 && f->resizes==1 && f->terminal->xpos==2);
	CHECK(f->output_size==0);
}
static void wire_loops(void)
{
	static const char wire[]="I#& 0,3,1,0,I@1,1:Z";
	for (size_t split=0;split<sizeof(wire);++split) {
		struct fixture f; create(&f,split%2,split%3==0);
		f.terminal->big_engine->state.application.marker=7;
		feed(&f,wire,split); feed(&f,wire+split,sizeof(wire)-1-split);
		wire_loop_scene(&f); destroy(&f);
	}
	struct fixture f; create(&f,false,false); f.terminal->big_engine->state.application.marker=7;
	for (size_t i=0;i<sizeof(wire)-1;++i) feed(&f,wire+i,1);
	wire_loop_scene(&f);
	/* Gang text is followed immediately by the raw loop type. */
	string(&f,"I#& 0,2,1,0,>I@@2,0,1:Z");
	struct cterm_big *b=f.terminal->big_engine;
	CHECK(b->numeric.loop.active==0 && b->numeric.loop.current==2 && b->numeric.loop.count==2);
	CHECK(b->control.frame.gang[0]=='I' && b->control.frame.gang[1]==0);
	CHECK(f.output_size==0 && f.terminal->xpos==3);
	/* Initial & owns setup; a stored target & only reaches the common body. */
	string(&f,"I#& 0,2,1,0,&@1,1:Z");
	CHECK(b->numeric.loop.active==1 && b->numeric.loop.current==0 && b->numeric.loop.words[0]==1);
	CHECK(f.output_size==0 && f.terminal->xpos==4);
	CHECK(st_cterm_finish(f.terminal)==0);
	destroy(&f);
}
static void wire_setup_prefixes(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	b->numeric.loop.to=91; b->numeric.loop.type='Z'; b->numeric.loop.random_suppression=1;
	string(&f,"I#& 7,");
	CHECK(b->numeric.loop.current==7 && b->numeric.loop.to==91 && b->numeric.loop.type=='Z' && b->numeric.loop.random_suppression==1);
	CHECK(st_cterm_finish(f.terminal)==0 && b->numeric.loop.current==7 && b->numeric.loop.random_suppression==1);
	b->numeric.loop.random_suppression=0;
	string(&f,"I#& 3,0,2,0,&@1,1:");
	CHECK(b->numeric.loop.step==65534 && b->numeric.loop.origin==3 && b->numeric.loop.active==1);
	string(&f,"I#& 3,0,0,0,&@1,1:");
	CHECK(b->numeric.loop.step==1 && b->numeric.loop.current==3 && b->numeric.loop.active==1);
	CHECK(st_cterm_finish(f.terminal)==0);
	/* The old Z type makes gang text reuse the shared buffer before raw @. */
	memcpy(b->text.bytes,"~~~",4); b->numeric.loop.type='Z';
	string(&f,"I#& 0,1,1,0,>@1,1:");
	CHECK(memcmp(b->text.bytes,"~~~",4)==0 && memcmp(b->control.frame.gang,"~~~",4)==0 && b->text.count==0);
	/* Embedded NUL ends the copy, while the text reader consumed through @. */
	string(&f,"I#"); b->control.frame.gang[2]='~';
	static const uint8_t binary[]={'&',' ','0',',','1',',','1',',','0',',','>','I',0,'W','@','@','1',',','2',':'};
	feed(&f,binary,sizeof(binary));
	CHECK(b->text.count==3 && memcmp(b->text.bytes,"I\0W",4)==0);
	CHECK(b->control.frame.gang[0]=='I' && b->control.frame.gang[1]==0 && b->control.frame.gang[2]=='~');
	/* The cap leaves @ for the raw type instead of draining it as text. */
	string(&f,"I#& 0,1,1,0,>"); uint8_t cap[128]; memset(cap,'I',sizeof(cap)); feed(&f,cap,sizeof(cap));
	string(&f,"@1,0:1:");
	CHECK(b->text.count==128 && b->control.frame.gang[127]=='I' && b->control.frame.gang[128]==0);
	/* Auxiliary timeout consumes the late target but substitutes SPACE. */
	string(&f,"I#& 0,1,1,0,"); local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS;
	string(&f,"I"); CHECK(b->numeric.loop.command==' ' && f.bells==1);
	string(&f,"@1,1:"); CHECK(b->numeric.loop.active==1 && b->numeric.loop.current==0);
	CHECK(st_cterm_finish(f.terminal)==0);
	/* A partial-pair timeout still enters initial & setup, not selected &. */
	string(&f,"I#&"); local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS;
	CHECK(st_cterm_poll(f.terminal)==0 && b->setup.phase==CTERM_BIG_SETUP_HEADER && f.bells==2);
	string(&f,"0,1,1,0,I@1,1:"); CHECK(b->numeric.loop.active==0 && b->numeric.loop.current==1);
	destroy(&f);
}
static void wire_setup_keys(void)
{
	for (unsigned late=0;late<2;++late) {
		struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
		string(&f,late?"I#& 0,2,1,0,I@2,1":"I#& 0,2,1,0,I@2");
		CHECK(st_cterm_handle_key(f.terminal,late?24:3,0) && f.output_size==0);
		string(&f,late?":Z":",Z");
		CHECK(b->numeric.loop.words[0]==late && b->numeric.loop.words[1]==0);
		CHECK(b->numeric.loop.active==0 && b->numeric.loop.current==2 && f.terminal->xpos==2);
		CHECK(f.output_size==0);
		CHECK(f.resizes==(late?3u:5u)); /* Zero-filled slots still execute I0. */
		destroy(&f);
	}
	struct fixture f; create(&f,true,false); struct cterm_big *b=f.terminal->big_engine; size_t used;
	CHECK(st_cterm_write(f.terminal,"I#& 0,1000,1,100,I@1,1:",23,&used)==IO_ERROR_IN_PROGRESS && used==23);
	CHECK(b->numeric.loop.current==0 && f.output_size==0);
	CHECK(st_cterm_handle_key(f.terminal,3,0) && st_cterm_handle_text_key(f.terminal,'k'));
	CHECK(st_cterm_poll(f.terminal)==0 && b->numeric.loop.current==1 && b->numeric.loop.active==0);
	CHECK(f.output_size==1 && memcmp(f.output,"k",1)==0);
	destroy(&f);
}
static void wire_setup_flow_lifecycle(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine; size_t used;
	f.block_output=true;
	const char *wire="I#& 1,1,1,0,I@1,x:";
	CHECK(st_cterm_write(f.terminal,wire,strlen(wire),&used)==0 && used==strlen(wire));
	CHECK(b->numeric.loop.active==0 && b->numeric.loop.no_parameters==0 && b->numeric.loop.words[0]==0x8001);
	CHECK(b->control.frame.did_loop==0 && b->numeric.loop.random_suppression==0 && f.output_size==0 && f.resizes==1);
	f.block_output=false;
	/* EOF/reset while loading retains acquired prefixes, without a body. */
	static const char *partial[]={"I#& 7,","I#& 0,1,1,0,","I#& 0,1,1,0,>ABC","I#& 0,1,1,0,I@2,1:"};
	for (size_t i=0;i<sizeof(partial)/sizeof(partial[0]);++i) {
		string(&f,partial[i]); CHECK(st_cterm_handle_key(f.terminal,3,0));
		CHECK(st_cterm_finish(f.terminal)==0 && b->setup.phase==CTERM_BIG_SETUP_OFF && b->numeric.loop.active==0);
		CHECK(st_cterm_poll(f.terminal)==0 && f.output_size==0 && b->text.active==0);
	}
	b->numeric.loop.random_suppression=0;
	f.reset_output=true; string(&f,"I#& 0,1,1,0,I@1,0:");
	CHECK(f.reset_output && b->setup.phase==CTERM_BIG_SETUP_OFF && b->numeric.loop.active==0 && f.resizes==2);
	CHECK(f.output_size==0); f.reset_output=false;
	destroy(&f);
}
static void pump(struct fixture *f, const char *wire, size_t size)
{
	size_t consumed=0;
	for (unsigned turns=0;consumed<size || st_cterm_input_paused(f->terminal);++turns) {
		CHECK(turns<1000);
		if (st_cterm_input_paused(f->terminal)) CHECK(st_cterm_poll(f->terminal)==0);
		else {
			size_t used; int status=st_cterm_write(f->terminal,wire+consumed,size-consumed,&used);
			CHECK(status==0 || status==IO_ERROR_IN_PROGRESS); CHECK(used!=0 || st_cterm_input_paused(f->terminal));
			consumed+=used;
		}
	}
}
static void wire_setup_bounds(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	b->numeric.loop.words[1]=0x55;
	string(&f,"I#& 0,1,1,0,&@0,1:");
	CHECK(b->numeric.loop.count==1 && b->numeric.loop.words[0]==1 && b->numeric.loop.words[1]==0x55);
	string(&f,"I#& 0,1,1,0,&@x,1:"); CHECK(b->numeric.loop.count==1 && b->numeric.loop.words[1]==0x55);
	char large[4200]; const char *header="I#& 0,1,1,0,&@2050,";
	size_t size=strlen(header); memcpy(large,header,size);
	for (unsigned i=0;i<2048;++i) { large[size++]='1'; large[size++]=i==2047?':':','; }
	large[size++]='Z'; pump(&f,large,size);
	CHECK(b->numeric.loop.count==2048 && b->numeric.loop.active==1 && b->numeric.loop.current==0 && f.terminal->xpos==2);
	for (size_t i=0;i<2048;++i) CHECK(b->numeric.loop.words[i]==1);
	CHECK(f.output_size==0);
	CHECK(st_cterm_finish(f.terminal)==0);
	/* Corrupted text without a terminator cannot copy into adjacent state. */
	memset(b->text.bytes,'Q',sizeof(b->text.bytes)); b->numeric.loop.type='Z';
	size_t used;
	CHECK(st_cterm_write(f.terminal,"I#& 7,1,1,0,>",13,&used)==0 && b->last_error==IO_ERROR_OUT_OF_RANGE && used==13);
	CHECK(b->numeric.loop.current==7 && b->control.frame.gang[0]==0 && b->numeric.loop.active==0);
	destroy(&f);
}
static void rejection_recovery(void)
{
	static const char wire[]="I#X 4,+1,9,TAIL\033[HI#C>0,5:P 20,20:OK\033[6n";
	for (unsigned utf8=0;utf8<2;++utf8) for (unsigned status=0;status<2;++status) {
		struct fixture f; create(&f,utf8,status); struct cterm_big *b=f.terminal->big_engine;
		for (size_t split=0;split<sizeof(wire);++split) {
			string(&f,"I#R 2:"); f.output_size=0;
			size_t rejected=b->rejected_commands;
			if (split==sizeof(wire)-1) for (size_t i=0;i<sizeof(wire)-1;++i) feed(&f,wire+i,1);
			else { feed(&f,wire,split); feed(&f,wire+split,sizeof(wire)-1-split); }
			CHECK(!st_cterm_input_paused(f.terminal) && b->zone.phase==CTERM_BIG_ZONE_OFF);
			CHECK(b->last_error==IO_ERROR_OUT_OF_RANGE && b->rejected_commands==rejected+1);
			CHECK(b->state.bgi.x==20 && b->state.bgi.y==20 && f.terminal->xpos==3 && f.terminal->ypos==1);
			CHECK(f.output_size==6 && memcmp(f.output,"\033[1;3R",6)==0);
			const struct st_console_rect r={20,20,1,1}; struct st_pixel_buffer *p=NULL;
			CHECK(st_console_get_pixels(&p,f.console,&r,false)==0 && p->primary[0]==805 && p->alternate[0]==805); st_pixel_buffer_destroy(p);
			CHECK(st_cterm_poll(f.terminal)==0 && st_cterm_finish(f.terminal)==0);
		}
		destroy(&f);
	}
}
static void wire_setup_sources(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	b->numeric.random_from=b->numeric.random_to=0;
	string(&f,"I#& r,1,1,0,&@1,r:");
	CHECK(b->numeric.loop.current==0 && b->numeric.random_state==0x015a4e36);
	CHECK(b->numeric.loop.words[0]==0x8003 && b->numeric.loop.no_parameters==1 && b->numeric.loop.random_suppression==0);
	b->numeric.loop.random_suppression=1; b->numeric.random_state=1;
	string(&f,"I#& r,1,1,0,&@1,r:");
	CHECK(b->numeric.loop.current==0x8003 && b->numeric.loop.origin==0x8003 && b->numeric.random_state==1);
	CHECK(st_cterm_finish(f.terminal)==0);
	/* Raw NUL/typeff retain byte identity inside the interpreter. */
	static const uint8_t raw[]={'I','#','&',' ','0',',','1',',','1',',','0',',',0,255,'1',',','1',':'};
	feed(&f,raw,sizeof(raw)); CHECK(b->numeric.loop.command==0 && b->numeric.loop.active==1);
	CHECK(st_cterm_finish(f.terminal)==0);
	/* Whole-call text expiry copies the bounded old suffix, without a new NUL. */
	memcpy(b->text.bytes,"OLD",4); b->numeric.loop.type=' ';
	string(&f,"I#& 0,1,1,0,>~"); local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS;
	CHECK(st_cterm_poll(f.terminal)==0 && b->setup.phase==CTERM_BIG_SETUP_TYPE && f.bells==1);
	CHECK(b->text.count==1 && memcmp(b->text.bytes,"~LD",4)==0 && memcmp(b->control.frame.gang,"~LD",4)==0);
	string(&f,"@1,0:"); CHECK(b->numeric.loop.active==0);
	/* A timed-out pair still publishes I as the repeated body identifier. */
	string(&f,"I#& 0,2,1,0,&@1,1:"); string(&f,"I#I");
	local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS;
	CHECK(st_cterm_poll(f.terminal)==0 && b->numeric.loop.current==2 && b->numeric.loop.active==0 && f.bells==2);
	destroy(&f);
}
static void scaling_probe(struct fixture *f, enum st_graphics_axis axis, int expected)
{
	/* Probe the production reader using state acquired by real wire g/R.
	 * No synthetic coordinate command is registered in the live grammar. */
	struct st_big_numeric *n=&f->terminal->big_engine->numeric;
	const struct st_graphics_parameter parameter={.kind=ST_GRAPHICS_NUMBER,.axis=axis};
	const struct st_graphics_command_view view={0};
	CHECK(st_big_numeric_field(n,&view,&parameter)==0);
	int value=0; bool complete=true;
	CHECK(st_big_numeric_number(n,&view,&parameter,&value,&complete)==0 && !complete);
	size_t used;
	CHECK(st_big_numeric_read_number(n,&view,&parameter,(const uint8_t *)"5:",2,&used,&value,&complete)==0);
	CHECK(complete && used==2 && value==expected); st_big_numeric_cancel(n);
}
static void scaling_wire(void)
{
	const char wire[]="I#g 258:Z";
	for (unsigned utf8=0;utf8<2;++utf8) for (unsigned status=0;status<2;++status)
		for (size_t split=0;split<sizeof(wire);++split) {
			struct fixture f; create(&f,utf8,status);
			CHECK(f.terminal->big_engine->numeric.context.scaling==0);
			string(&f,"\033[10;20H");
			f.terminal->big_engine->state.bgi.x=19; f.terminal->big_engine->state.bgi.y=27;
			feed(&f,wire,split); feed(&f,wire+split,sizeof(wire)-1-split);
			CHECK(f.terminal->big_engine->numeric.context.scaling==258 && f.resizes==1 && f.output_size==0);
			CHECK(f.terminal->xpos==21 && f.terminal->ypos==10);
			CHECK(f.terminal->big_engine->state.bgi.x==19 && f.terminal->big_engine->state.bgi.y==27);
			destroy(&f);
		}
	struct fixture f; create(&f,true,false); struct cterm_big *b=f.terminal->big_engine;
	for (size_t i=0;i<sizeof(wire)-1;++i) feed(&f,wire+i,1);
	CHECK(b->numeric.context.scaling==258); scaling_probe(&f,ST_GRAPHICS_Y,5);
	string(&f,"I#g 2:"); scaling_probe(&f,ST_GRAPHICS_Y,8); scaling_probe(&f,ST_GRAPHICS_X,5);
	/* Failed R publishes cache3 without installing; g observes that cache. */
	b->state.bgi.max_mode=1; string(&f,"I#R 3:");
	CHECK(b->numeric.context.resolution==3 && b->state.bgi.height==350 && f.resizes==1);
	string(&f,"I#g 3:"); scaling_probe(&f,ST_GRAPHICS_Y,11);
	scaling_probe(&f,ST_GRAPHICS_UNSCALED,5); scaling_probe(&f,ST_GRAPHICS_X,5);
	string(&f,"I#g 2:"); CHECK(b->numeric.context.scaling==2); scaling_probe(&f,ST_GRAPHICS_Y,5);
	string(&f,"I#g +1:"); CHECK(b->numeric.context.scaling==65535); scaling_probe(&f,ST_GRAPHICS_Y,5);
	string(&f,"I#g x:"); CHECK(b->numeric.context.scaling==0x8001);
	string(&f,"I#g 12345:"); CHECK(b->numeric.context.scaling==0);
	string(&f,"I#g 3:"); string(&f,"I#I 1:I#I 0:I#R 2:");
	CHECK(b->numeric.context.scaling==3 && b->numeric.context.resolution==2);
	scaling_probe(&f,ST_GRAPHICS_Y,5);
	string(&f,"I#g 2"); CHECK(b->numeric.context.scaling==3);
	CHECK(st_cterm_finish(f.terminal)==0 && b->numeric.context.scaling==3);
	string(&f,"I#g 2"); CHECK(st_cterm_reset_terminal(f.terminal)==0 && b->numeric.context.scaling==3);
	string(&f,"I#g "); local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS;
	CHECK(st_cterm_poll(f.terminal)==0 && b->numeric.context.scaling==0 && f.bells==1);
	destroy(&f);
}
static void scaling_sources_and_footer(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine; size_t used;
	const char wire[]="I#& 1,3,1,1,g@1,x:";
	CHECK(st_cterm_write(f.terminal,wire,sizeof(wire)-1,&used)==IO_ERROR_IN_PROGRESS && used==sizeof(wire)-1);
	CHECK(b->numeric.context.scaling==0 && f.output_size==0);
	for (unsigned value=1;value<=3;++value) {
		local_ns+=UINT64_C(1000000000); CHECK(st_cterm_poll(f.terminal)==0);
		CHECK(b->numeric.context.scaling==value && b->numeric.loop.current==value);
	}
	CHECK(b->numeric.loop.active==0 && f.output_size==0);
	string(&f,"I#"); b->control.input.pending=1; b->control.input.count=2;
	b->control.input.payload[1]=0; b->control.input.payload[2]=255;
	string(&f,"g>2:"); CHECK(b->numeric.context.scaling==2 && b->control.input.pending==1 && f.output_size==0);
	string(&f,"~ "); CHECK(f.output_size==2 && f.output[0]==0 && f.output[1]==255);
	destroy(&f);
}
static void hollow_partitions(void)
{
	static const struct { const char *word; uint16_t expected; } cases[]={
		{"0",0},{"1",1},{"2",2},{"128",128},{"256",256},{"258",258},{"9999",9999},{"+1",0}};
	for (unsigned utf8=0;utf8<2;++utf8) for (unsigned status=0;status<2;++status) {
		struct fixture f; create(&f,utf8,status); struct cterm_big *b=f.terminal->big_engine;
		f.terminal->api_size_changed=NULL;
		for (unsigned mode=1;mode<=3;++mode) for (size_t c=0;c<sizeof(cases)/sizeof(cases[0]);++c) {
			char wire[32]; snprintf(wire,sizeof(wire),"I#H %s:\033[2;3H",cases[c].word);
			size_t size=strlen(wire), splits=mode==2 && !utf8 && !status?size+1:0;
			for (size_t split=0;split<=splits;++split) {
				char reset[16]; snprintf(reset,sizeof(reset),"I#R %u:",mode); string(&f,reset);
				b->state.application.hollow=7;
				struct st_big_state expected; memcpy(&expected,&b->state,sizeof(expected)); expected.application.hollow=cases[c].expected;
				if (split==splits) for (size_t i=0;i<size;++i) feed(&f,wire+i,1);
				else { feed(&f,wire,split); string(&f,wire+split); }
				CHECK(b->state.application.hollow==cases[c].expected && b->state.bgi.error==0 && b->last_error==0);
				CHECK(memcmp(&expected,&b->state,sizeof(expected))==0);
				const struct st_console_rect r={0,0,640,b->state.bgi.height}; struct st_pixel_buffer *p=NULL;
				CHECK(st_console_get_pixels(&p,f.console,&r,false)==0);
				for (size_t i=0;i<(size_t)r.width*r.height;++i) CHECK(p->primary[i]==800 && p->alternate[i]==800);
				st_pixel_buffer_destroy(p);
				CHECK(f.terminal->xpos==3 && f.terminal->ypos==2 && f.output_size==0);
			}
		}
		destroy(&f);
	}
}
static void hollow_lifecycle(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#H 2:I#H 0"); CHECK(st_cterm_finish(f.terminal)==0 && b->state.application.hollow==2);
	string(&f,"I#H "); local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS;
	CHECK(st_cterm_poll(f.terminal)==0 && b->state.application.hollow==0 && f.bells==1);
	string(&f,"I#H 256:I#H "); CHECK(st_cterm_reset_terminal(f.terminal)==0 && b->state.application.hollow==256);
	string(&f,"I#H 128:I#R 2:"); CHECK(b->state.application.hollow==128);
	string(&f,"I#I 1"); CHECK(b->state.application.hollow==128);
	string(&f,":"); CHECK(b->state.application.hollow==0);
	string(&f,"I#H 256:I#I 2:"); CHECK(b->state.application.hollow==0);
	string(&f,"I#g 3:I#H 5:"); CHECK(b->state.application.hollow==5);
	b->state.bgi.state=0; b->state.bgi.error=-11; string(&f,"I#H 258:");
	CHECK(b->state.application.hollow==258 && b->state.bgi.error==-11 && b->last_error==0); b->state.bgi.state=3;
	string(&f,"I#& 0,3,1,0,H@1,x:");
	CHECK(b->state.application.hollow==3 && b->numeric.loop.active==0 && b->numeric.loop.current==3 && f.output_size==0);
	f.output_size=0; b->control.input.pending=1; b->control.input.count=2; b->control.input.payload[1]=0; b->control.input.payload[2]=255;
	string(&f,"I#H>256:"); CHECK(b->state.application.hollow==256 && b->control.input.pending==1 && f.output_size==0);
	string(&f,"~ "); CHECK(f.output_size==2 && memcmp(f.output,"\0\xff",2)==0);
	/* A blocked footer follows the committed H store. */
	f.output_size=0; b->control.input.pending=1; b->control.input.count=1; b->control.input.payload[1]='K'; f.block_output=true;
	const char wire[]="I#H 2:"; size_t used;
	CHECK(st_cterm_write(f.terminal,wire,sizeof(wire)-1,&used)==IO_ERROR_IN_PROGRESS && used==sizeof(wire)-1 && b->state.application.hollow==2 && b->footer_active);
	CHECK(st_cterm_finish(f.terminal)==0 && b->state.application.hollow==2 && !b->control.input.pending && f.output_size==0);
	destroy(&f);
}
int main(void)
{
	hollow_partitions(); hollow_lifecycle(); rejection_recovery(); partitions(); framing_and_state(); deadlines_and_recovery(); reply_and_footer();
	stored_repetition(); gang_source_seams(); footer_wait_reset();
	local_keys(); local_key_bytes(); local_key_lifecycle(); local_key_return(); wire_loops();
	wire_setup_prefixes(); wire_setup_keys(); wire_setup_flow_lifecycle(); wire_setup_bounds(); wire_setup_sources();
	scaling_wire(); scaling_sources_and_footer(); return 0;
}
