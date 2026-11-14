/* Independent W acquisition, opaque payload and drawing expectations, BIG62. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm.h"
#include "cterm_big_internal.h"
#include "st_cterm.h"
#include "io_error.h"
#include "st_big_draw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
struct fixture { st_console_t *console; st_font_catalog_t *fonts; st_cterm_t *terminal; uint8_t output[512]; size_t size; };
static uint64_t local_ns;
static int clock_error;
static bool reset_on_bell;
static unsigned timeout_bells;
static int now(void *context, uint64_t *value) { (void)context; *value=local_ns; return clock_error; }
static void timeout_bell(void *context)
{
	struct cterm_big *b=context; ++timeout_bells;
	if (reset_on_bell) CHECK(st_cterm_reset_terminal(b->terminal)==0);
}
static void output(void *context, enum st_cterm_output_origin origin, const uint8_t *bytes, size_t size)
{
	struct fixture *f=context; (void)origin; CHECK(size<=sizeof(f->output)-f->size);
	memcpy(f->output+f->size,bytes,size); f->size+=size;
}
static void create(struct fixture *f, bool utf8, bool status)
{
	memset(f,0,sizeof(*f)); local_ns=1000; clock_error=0; reset_on_bell=false; timeout_bells=0;
	const struct st_console_display_config display={.backend=ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry={.columns=80,.rows=25,.cell_width=8,.cell_height=14};
	CHECK(st_console_create(&f->console,&display,&geometry)==0 && st_font_catalog_create(&f->fonts,f->console)==0);
	const struct st_cterm_config config={.console=f->console,.fonts=f->fonts,.emulation=ST_CTERM_ANSI_BBS,
	    .graphics_mode=ST_CTERM_GRAPHICS_BIG_103,.encoding=utf8?ST_CTERM_ENCODING_UTF8:ST_CTERM_ENCODING_CODEPAGE,
	    .codepage=ST_CODEPAGE_CP437,.quiet=true,.status_line=status,.scrollback_lines=2,.output=output,.output_context=f};
	CHECK(st_cterm_create(&f->terminal,&config)==0); f->terminal->big_engine->numeric.io.now_ns=now;
	f->terminal->big_engine->numeric.io.bell=timeout_bell;
}
static void destroy(struct fixture *f) { st_cterm_destroy(&f->terminal); st_font_catalog_destroy(f->fonts); st_console_destroy(f->console); }
static void feed(struct fixture *f, const void *bytes, size_t size) { size_t used; int status=st_cterm_write(f->terminal,bytes,size,&used); CHECK((status==0 || status==IO_ERROR_IN_PROGRESS) && used==size); }
static void string(struct fixture *f, const char *wire) { feed(f,wire,strlen(wire)); }
static void setup(struct fixture *f, unsigned resolution)
{
	char wire[64]; snprintf(wire,sizeof(wire),"I#R %u:I#C 3,5:",resolution); string(f,wire);
	struct st_big_state *s=&f->terminal->big_engine->state;
	CHECK(st_big_draw_move(s,100,101)==0); s->bgi.error=-11; f->size=0;
}
static void canvas_colour(struct fixture *f, unsigned x, unsigned y, bool visible, unsigned pen)
{
	/* Current shared bitmap A, literal source rows. No output from the
	 * emulator under test is used to construct this independent mask. */
	static const uint8_t rows[]={0x38,0x6c,0xc6,0xc6,0xfe,0xc6,0xc6,0};
	uint32_t width,height; CHECK(st_console_pixel_size(f->console,&width,&height)==0);
	const struct st_console_rect area={0,0,width,height}; struct st_pixel_buffer *p=NULL;
	CHECK(st_console_get_pixels(&p,f->console,&area,false)==0);
	for (unsigned py=0;py<height;++py) for (unsigned px=0;px<width;++px) {
		bool bit=visible && px>=x && px<x+8 && py>=y && py<y+8 && (rows[py-y]&(128u>>(px-x)));
		uint32_t expected=bit?800+pen:800; size_t index=(size_t)py*width+px;
		CHECK(p->primary[index]==expected && p->alternate[index]==expected);
	}
	st_pixel_buffer_destroy(p); uint32_t rgb;
	CHECK(st_console_get_palette(f->console,(uint16_t)(800+pen),&rgb,1)==0 && rgb==(pen==5?0xaa00aa:0x5555ff));
}
static void canvas(struct fixture *f, unsigned x, unsigned y, bool visible) { canvas_colour(f,x,y,visible,5); }
static void partitions(void)
{
	static const char wire[]="I#W 20,30,A@\033[6n";
	for (unsigned utf8=0;utf8<2;++utf8) for (unsigned status=0;status<2;++status) {
		struct fixture f; create(&f,utf8,status); struct cterm_big *b=f.terminal->big_engine;
		for (unsigned res=1;res<=3;++res) for (size_t split=0;split<=sizeof(wire);++split) {
			if (res!=2 && split!=sizeof(wire)) continue;
			setup(&f,res); struct st_big_image image; memcpy(&image,&b->image,sizeof(image));
			uint8_t saved[32]; memcpy(saved,b->image_bytes,sizeof(saved));
			if (split==sizeof(wire)) for (size_t i=0;i<sizeof(wire)-1;++i) feed(&f,wire+i,1);
			else { feed(&f,wire,split); feed(&f,wire+split,sizeof(wire)-1-split); }
			CHECK(b->last_error==0 && b->rejected_commands==0 && !b->text_output.active && !b->text.active);
			CHECK(b->state.bgi.color==5 && b->state.bgi.device_color==5 && b->state.bgi.error==-11);
			CHECK(b->state.bgi.x==100 && b->state.bgi.y==101 && b->state.bgi.ddo_x==20 && b->state.bgi.ddo_y==30 && b->state.bgi.device_x==28 && b->state.bgi.device_y==30);
			CHECK(b->text.count==1 && b->text.bytes[0]=='A' && b->text.bytes[1]==0 && b->state.bgi.text_count==1);
			CHECK(memcmp(&image,&b->image,sizeof(image))==0 && memcmp(saved,b->image_bytes,sizeof(saved))==0);
			CHECK(f.size==6 && memcmp(f.output,"\033[1;1R",6)==0); canvas(&f,20,30,true);
		}
		destroy(&f);
	}
}
static void partials_and_recovery(void)
{
	struct fixture f; create(&f,false,false); setup(&f,2); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#W 20,"); CHECK(b->state.bgi.color==15 && !b->text_output.active && b->state.bgi.ddo_x==100);
	string(&f,"30,"); CHECK(b->state.bgi.color==5 && b->text_output.active && b->text.active && b->state.bgi.ddo_x==100);
	CHECK(st_cterm_finish(f.terminal)==0 && !b->text_output.active && b->state.bgi.ddo_x==100); canvas(&f,0,0,false);
	string(&f,"I#W 20,30,A"); CHECK(st_cterm_reset_terminal(f.terminal)==0 && !b->text_output.active && b->state.bgi.ddo_x==100);
	setup(&f,2); memcpy(b->text.bytes,"OLD",4);
	string(&f,"I#W 20,30,A"); local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS; CHECK(st_cterm_poll(f.terminal)==0);
	CHECK(memcmp(b->text.bytes,"ALD",4)==0 && b->text.count==1 && b->state.bgi.text_count==3 && b->state.bgi.device_x==44 && !b->text_output.active && timeout_bells==1);
	setup(&f,2); string(&f,"I#W 20,30,"); CHECK(st_big_draw_color(&b->state,9)==0); string(&f,"A@");
	CHECK(b->state.bgi.color==9 && b->state.bgi.device_color==9 && b->state.application.colors[3]==5); canvas_colour(&f,20,30,true,9);
	setup(&f,2); string(&f,"I#W 20,30,A"); clock_error=IO_ERROR_IO; CHECK(st_cterm_poll(f.terminal)==0); clock_error=0;
	CHECK(b->last_error==IO_ERROR_IO && b->rejected_commands==1 && !b->text_output.active && b->state.bgi.ddo_x==100);
	string(&f,"I#W 20,30,A@\033[6n"); CHECK(f.size==6); canvas(&f,20,30,true);
	setup(&f,2); b->state.bgi.state=0; string(&f,"I#W 20,30,A@\033[6n");
	CHECK(b->last_error==IO_ERROR_PROTOCOL && b->rejected_commands==2 && b->state.bgi.color==5 && b->state.bgi.error==-1 && !b->text_output.active);
	b->state.bgi.state=3; setup(&f,2); b->state.bgi.mode=99;
	string(&f,"I#W +1,30,A@\033[6n");
	CHECK(b->rejected_commands==2 && b->state.bgi.ddo_x==65535 && b->state.bgi.device_x==65535 && b->state.bgi.text_count==1 && f.size==6); f.size=0;
	string(&f,"I#W 20,30,A@I#S 5,6:\033[6n");
	CHECK(b->last_error==IO_ERROR_OUT_OF_RANGE && b->rejected_commands==3 && b->state.bgi.ddo_x==20 && b->state.bgi.device_x==20 && b->state.bgi.text_count==1 && b->state.bgi.palette[5]==6 && f.size==6);
	b->state.bgi.mode=1; destroy(&f);
}
static void payloads_loops_and_pages(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	setup(&f,2); static const char nul[]="I#W 20,30,A\0B@\033[6n"; feed(&f,nul,sizeof(nul)-1);
	CHECK(b->text.count==3 && memcmp(b->text.bytes,(uint8_t[]){'A',0,'B',0},4)==0 && b->state.bgi.text_count==1 && b->state.bgi.device_x==28 && f.size==6); canvas(&f,20,30,true);
	setup(&f,2); static const char ff[]="I#W 20,30,A\xff\0@\033[6n"; feed(&f,ff,sizeof(ff)-1);
	CHECK(b->text.count==3 && b->text.bytes[1]==255 && b->state.bgi.text_count==2 && b->state.bgi.device_x==36 && f.size==6); canvas(&f,20,30,true);
	setup(&f,2); string(&f,"I#W 20,30,\r\n@\033[6n"); CHECK(b->text.count==1 && b->text.bytes[0]=='@' && b->state.bgi.device_x==28 && f.size==6);
	setup(&f,2); string(&f,"I#W 20,30,I#R 1:\033[31m@\033[6n");
	CHECK(b->numeric.context.resolution==2 && f.terminal->attr==7 && f.terminal->xpos==1 && f.size==6 && b->text.bytes[0]=='I');
	setup(&f,2); char cap[200]; size_t length=strlen("I#W 0,30,"); memcpy(cap,"I#W 0,30,",length); memset(cap+length,'A',128); length+=128;
	memcpy(cap+length,"@\033[6n",5); length+=5; feed(&f,cap,length);
	CHECK(b->text.count==128 && b->state.bgi.text_count==128 && b->state.bgi.device_x==1024 && b->text.bytes[128]==0 && f.size==6 && memcmp(f.output,"\033[1;2R",6)==0);
	setup(&f,3); string(&f,"I#g 3:I#W 20,5,A@"); CHECK(b->state.bgi.ddo_y==11); canvas(&f,20,11,true);
	setup(&f,3); memcpy(b->text.bytes,"A",2);
	string(&f,"I#g 3:I#& 0,2,1,0,W!2,20,5:\033[6n");
	CHECK(b->state.bgi.ddo_y==5 && b->text.count==0 && b->state.bgi.text_count==1 && f.size==6 && memcmp(f.output,"\033[1;1R",6)==0); canvas(&f,20,5,true);
	setup(&f,2); string(&f,"I#& 0,2,1,0,W@2,20,30:A@A@\033[6n");
	CHECK(!b->text_output.active && b->text.count==1 && b->state.bgi.device_x==28 && f.size==6 && memcmp(f.output,"\033[1;1R",6)==0); canvas(&f,20,30,true);
	setup(&f,2); string(&f,"I#G 6,1:I#W 20,30,A@"); canvas(&f,0,0,false);
	string(&f,"I#G 5,1:"); canvas(&f,20,30,true);
	setup(&f,2); string(&f,"I#"); b->control.input.pending=1; b->control.input.count=1; b->control.input.payload[1]=255;
	string(&f,"W>20,30,A@S 5,5:\033[6n");
	CHECK(f.size==7 && memcmp(f.output,"\xff\033[1;1R",7)==0 && !b->control.input.pending); canvas(&f,20,30,true);
	setup(&f,2); memset(b->text.bytes,'A',sizeof(b->text.bytes)); b->state.bgi.text_count=7;
	string(&f,"I#& 0,1,1,0,W!2,20,30:\033[6n");
	CHECK(b->last_error==IO_ERROR_OUT_OF_RANGE && b->rejected_commands==1 && b->state.bgi.text_count==7 && b->state.bgi.ddo_x==20 && b->state.bgi.device_x==20);
	CHECK(f.size==6 && memcmp(f.output,"\033[1;1R",6)==0); canvas(&f,0,0,false);
	destroy(&f);
}
static void stroke_text(void)
{
	for (unsigned res=1;res<=3;++res) for (unsigned utf8=0;utf8<2;++utf8) for (unsigned status=0;status<2;++status) {
		struct fixture f; create(&f,utf8,status); setup(&f,res); struct cterm_big *b=f.terminal->big_engine;
		string(&f,"I#E 2,1,0:I#T 2,2,1,1,1:I#M 2:I#W 20,30,AA@\033[6n");
		CHECK(b->state.bgi.text_total_width==7 && b->state.bgi.text_count==2 && b->state.bgi.text_stroke_point[0]==3 && b->state.bgi.text_stroke_point[1]==0);
		CHECK(b->state.bgi.ddo_x==26 && b->state.bgi.ddo_y==35 && b->state.bgi.device_x==20 && b->state.bgi.device_y==30 && b->state.bgi.x==100 && b->state.bgi.y==101);
		CHECK(b->state.bgi.line_style==2 && b->state.bgi.line_width==3 && b->state.bgi.ddo_line_style==2 && b->state.bgi.ddo_line_width==3 && b->state.bgi.device_line_width==1 && b->state.bgi.line_mask==0xfc78);
		CHECK(b->state.bgi.write_mode==1 && b->state.bgi.error==-11 && f.size==6);
		uint32_t width,height; CHECK(st_console_pixel_size(f.console,&width,&height)==0); const struct st_console_rect area={0,0,width,height}; struct st_pixel_buffer *p=NULL;
		CHECK(st_console_get_pixels(&p,f.console,&area,false)==0);
		static const uint16_t odd[][2]={{20,35},{21,34},{22,34},{23,35},{24,34},{25,34}};
		for (unsigned y=0;y<height;++y) for (unsigned x=0;x<width;++x) {
			bool set=false; for (size_t i=0;i<sizeof(odd)/sizeof(odd[0]);++i) if (x==odd[i][0] && y==odd[i][1]) set=true;
			size_t at=(size_t)y*width+x; CHECK(p->primary[at]==(set?805u:800u) && p->alternate[at]==p->primary[at]);
		}
		st_pixel_buffer_destroy(p); destroy(&f);
	}
}
static void reset_during_timeout(void)
{
	struct fixture f; create(&f,false,false); setup(&f,2); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#W 20,30,A"); reset_on_bell=true; local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS;
	CHECK(st_cterm_poll(f.terminal)==0 && timeout_bells==1 && !b->text_output.active && !b->text.active && b->rejected_commands==0);
	CHECK(b->state.bgi.ddo_x==100 && b->state.bgi.device_x==100 && b->state.bgi.text_count==0); canvas(&f,0,0,false);
	reset_on_bell=false; string(&f,"I#W 20,30,A@"); canvas(&f,20,30,true); destroy(&f);
}
static void all_current_faces(void)
{
	/* Current shared A metrics and first explicit MOVE/DRAW endpoints.
	 * These landmarks are hand-transformed at ratio1, not captured from
	 * the renderer. Full Small masks are independently checked above. */
	static const uint16_t heights[]={31,9,32,32}, widths[]={19,6,21,26};
	static const uint16_t points[4][2][2]={{{29,40},{22,60}},{{20,39},{20,35}},{{28,41},{20,62}},{{25,44},{27,42}}};
	for (unsigned res=1;res<=3;++res) for (unsigned font=1;font<=4;++font) {
		struct fixture f; create(&f,false,false); setup(&f,res); struct cterm_big *b=f.terminal->big_engine;
		char wire[64]; snprintf(wire,sizeof(wire),"I#E %u,4,0:I#W 20,30,AA@",font); string(&f,wire);
		CHECK(b->state.bgi.font==font && b->fonts.loaded==font && b->state.bgi.text_total_width==2*widths[font-1]);
		CHECK(b->state.bgi.text_scaled[1]==heights[font-1] && b->state.bgi.ddo_x==20+2*widths[font-1] && b->state.bgi.ddo_y==30+heights[font-1]);
		CHECK(b->state.bgi.device_x==20 && b->state.bgi.device_y==30 && b->state.bgi.x==100 && b->state.bgi.y==101);
		uint32_t width,height; CHECK(st_console_pixel_size(f.console,&width,&height)==0); const struct st_console_rect area={0,0,width,height}; struct st_pixel_buffer *p=NULL;
		CHECK(st_console_get_pixels(&p,f.console,&area,false)==0);
		for (unsigned glyph=0;glyph<2;++glyph) for (unsigned endpoint=0;endpoint<2;++endpoint) {
			unsigned x=points[font-1][endpoint][0]+glyph*widths[font-1],y=points[font-1][endpoint][1];
			CHECK(p->primary[y*width+x]==805 && p->alternate[y*width+x]==805);
		}
		CHECK(p->primary[100u*width+100u]==800 && p->alternate[100u*width+100u]==800);
		st_pixel_buffer_destroy(p); destroy(&f);
	}
}
int main(void) { partitions(); partials_and_recovery(); payloads_loops_and_pages(); stroke_text(); reset_during_timeout(); all_current_faces(); return 0; }
