/* Independent BIG R wire/mode expectations: reference27/47/48/54/80. */
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
	unsigned resizes, bells;
	bool status;
	uint32_t heights[32]; uint8_t output[64]; size_t size;
};
static uint64_t local_ns;
static int now(void *context, uint64_t *value) { (void)context; *value=local_ns; return 0; }
static void bell(void *context) { struct cterm_big *b=context; ++((struct fixture *)b->terminal->api_output_context)->bells; }
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
	size_t used; CHECK(st_cterm_write(f->terminal,wire,size,&used)==0 && used==size);
}
static void string(struct fixture *f, const char *wire) { feed(f,wire,strlen(wire)); }
static void scene(struct fixture *f, unsigned resolution, bool status)
{
	static const uint32_t heights[]={200,350,480}, cell_heights[]={8,14,16}, rows[]={25,25,30};
	static const uint8_t a[3][16]={
		{0x30,0x78,0xcc,0xcc,0xfc,0xcc,0xcc,0},
		{0,0,0x10,0x38,0x6c,0xc6,0xc6,0xfe,0xc6,0xc6,0xc6,0,0,0},
		{0,0,0x10,0x38,0x6c,0xc6,0xc6,0xfe,0xc6,0xc6,0xc6,0xc6,0,0,0,0}};
	struct cterm_big *b=f->terminal->big_engine; unsigned i=resolution-1;
	uint32_t width,height,columns,count,cw,ch;
	CHECK(st_console_pixel_size(f->console,&width,&height)==0 && width==640 && height==heights[i]);
	CHECK(st_console_size(f->console,&columns,&count)==0 && columns==80 && count==rows[i]);
	CHECK(st_console_cell_pixel_size(f->console,&cw,&ch)==0 && cw==8 && ch==cell_heights[i]);
	CHECK(b->numeric.context.resolution==resolution && b->state.bgi.height==height);
	CHECK(b->state.bgi.viewport[0]==0 && b->state.bgi.viewport[1]==0
	    && b->state.bgi.viewport[2]==639 && b->state.bgi.viewport[3]==height-1 && b->state.bgi.clip==1);
	CHECK(f->terminal->bottom_margin==(int)(rows[i]-(status?1u:0u)) && f->terminal->xpos==3);
	struct st_console_cell cells[2]; const struct st_console_rect cell_rect={0,0,2,1}, rect={0,0,width,height};
	CHECK(st_console_read_cells(f->console,&cell_rect,cells,2)==0);
	CHECK(cells[0].glyph=='A' && cells[0].foreground==807 && cells[0].background==800);
	CHECK(cells[1].glyph==219 && cells[1].foreground==804 && cells[1].background==800);
	struct st_pixel_buffer *pixels=NULL; CHECK(st_console_get_pixels(&pixels,f->console,&rect,false)==0);
	for (unsigned y=0;y<height;++y) for (unsigned x=0;x<width;++x) {
		uint32_t expected=800;
		if (y<ch && x<8 && (a[i][y]&(128u>>x))) expected=807;
		if (y<ch && x>=8 && x<16) expected=804;
		CHECK(pixels->primary[(size_t)y*width+x]==expected && pixels->alternate[(size_t)y*width+x]==expected);
	}
	st_pixel_buffer_destroy(pixels);
}
static void partitions(void)
{
	for (unsigned resolution=1;resolution<=3;++resolution) for (unsigned utf8=0;utf8<2;++utf8) for (unsigned status=0;status<2;++status) {
		char wire[48]; int size=snprintf(wire,sizeof(wire),"I#R %u:A\033[31m%s",resolution,utf8?"\xe2\x96\x88":"\xdb");
		CHECK(size>0 && (size_t)size<sizeof(wire));
		for (size_t split=0;split<=(size_t)size;++split) {
			struct fixture f; create(&f,utf8,status);
			/* Dirty both the raster and ANSI colours before R. */
			string(&f,"\033[35;44mOLD"); feed(&f,wire,split); feed(&f,wire+split,(size_t)size-split);
			scene(&f,resolution,status); CHECK(f.resizes==2 && f.size==0); destroy(&f);
		}
		struct fixture f; create(&f,utf8,status);
		for (int k=0;k<size;++k) feed(&f,wire+k,1);
		scene(&f,resolution,status); destroy(&f);
	}
}
static void retained_and_normalized(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#"); b->state.application.hollow=7; b->state.application.marker=9;
	b->state.application.marker_size=4; b->state.application.colors[2]=3; b->state.application.fill=6;
	b->state.application.write_mode=1; b->state.application.patterns[11][7]=0x65;
	b->state.memory_page=3; b->state.active_page=1; b->state.visual_page=2;
	b->state.local_palette[5]=41; b->state.saved_palette[8]=37;
	b->state.bgi.saved_ratio[2]=19; b->state.bgi.effective_ratio[0]=5;
	b->numeric.context.scaling=3; b->numeric.random_state=123; b->numeric.random_from=4;
	b->numeric.loop.words[31]=0x1234;
	b->control.input.pending=1; b->control.input.count=2; b->control.input.payload[1]=0; b->control.input.payload[2]=255; b->control.input.append_cr=1;
	string(&f,"R>3:");
	CHECK(b->state.application.hollow==7 && b->state.application.marker==9 && b->state.application.marker_size==4);
	CHECK(b->state.application.colors[2]==3 && b->state.application.fill==6 && b->state.application.write_mode==1 && b->state.application.patterns[11][7]==0x65);
	CHECK(b->state.memory_page==3 && b->state.active_page==1 && b->state.visual_page==2 && b->state.local_palette[5]==41 && b->state.saved_palette[8]==37);
	CHECK(b->state.bgi.write_mode==0 && b->state.bgi.saved_ratio[2]==19 && b->state.bgi.effective_ratio[0]==5);
	CHECK(b->numeric.context.scaling==3 && b->numeric.random_state==123 && b->numeric.random_from==4 && b->numeric.loop.words[31]==0x1234);
	CHECK(f.size==0 && b->control.input.pending==1);
	string(&f,"~ "); CHECK(f.size==3 && memcmp(f.output,"\0\xff\r",3)==0);
	static const char *requests[]={"I#R 2:","I#R 2:","I#R 0:","I#R 4:","I#R 258:","I#R 259:","I#R x:","I#R +1:"};
	static const uint32_t heights[]={350,350,200,200,200,200,200,200};
	for (size_t i=0;i<sizeof(requests)/sizeof(requests[0]);++i) {
		unsigned before=f.resizes; string(&f,requests[i]);
		CHECK(f.resizes==before+1 && f.heights[before]==heights[i]);
	}
	destroy(&f);
}
static void source_and_recovery(void)
{
	struct fixture f; create(&f,true,true); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#& 1,3,1,0,R@1,x:");
	CHECK(f.resizes==4 && f.heights[1]==200 && f.heights[2]==350 && f.heights[3]==480);
	CHECK(b->numeric.loop.active==0 && b->numeric.loop.current==3 && f.size==2 && f.output[0]==19 && f.output[1]==17);
	string(&f,"I#R 1"); CHECK(b->numeric.context.resolution==3 && f.resizes==4);
	CHECK(st_cterm_finish(f.terminal)==0 && f.resizes==4);
	string(&f,"I#R "); local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS;
	CHECK(st_cterm_poll(f.terminal)==0 && b->numeric.context.resolution==1 && f.resizes==5 && f.bells==1);
	string(&f,"I#R 3"); CHECK(st_cterm_reset_terminal(f.terminal)==0 && f.resizes==5);
	string(&f,"I#R 2:"); CHECK(f.resizes==6 && b->numeric.context.resolution==2);
	destroy(&f);
}
static void mode_return_paths(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	b->state.bgi.driver=3; string(&f,"I#R 3:");
	CHECK(f.resizes==2 && f.heights[1]==350 && b->numeric.context.resolution==2);
	b->state.bgi.driver=9; b->state.bgi.max_mode=1;
	string(&f,"A"); b->state.bgi.viewport[0]=7; b->state.bgi.x=19;
	b->control.input.pending=1; b->control.input.count=1; b->control.input.payload[1]='z';
	string(&f,"I#R 3:");
	CHECK(f.resizes==2 && b->numeric.context.resolution==3 && b->state.bgi.error==-10);
	CHECK(b->state.bgi.height==350 && b->state.bgi.mode==1 && b->state.bgi.viewport[0]==0
	    && b->state.bgi.viewport[3]==349 && b->state.bgi.x==0);
	CHECK(f.terminal->xpos==2 && f.size==1 && f.output[0]=='z');
	struct st_console_cell cell; const struct st_console_rect rect={0,0,1,1};
	CHECK(st_console_read_cells(f.console,&rect,&cell,1)==0 && cell.glyph=='A');
	b->state.bgi.state=2; string(&f,"I#R 1:");
	CHECK(f.resizes==2 && b->numeric.context.resolution==1 && b->state.bgi.height==350 && b->state.bgi.error==-10);
	b->state.bgi.driver=42; b->state.bgi.viewport[0]=5; b->state.bgi.y=19;
	string(&f,"I#R 3:");
	CHECK(f.resizes==2 && b->numeric.context.resolution==1 && b->state.bgi.viewport[0]==0 && b->state.bgi.y==0);
	destroy(&f);
}
static void extended_ownership(void)
{
	const char wire[]="I#X 6,0,20:Z";
	for (unsigned utf8=0;utf8<2;++utf8) for (unsigned status=0;status<2;++status)
		for (size_t split=0;split<sizeof(wire);++split) {
			struct fixture f; create(&f,utf8,status);
			struct cterm_big *b=f.terminal->big_engine;
			CHECK(b->control.pointer.step_x==12 && b->control.pointer.step_y==6);
			string(&f,"\033[10;20H");
			b->state.bgi.x=19; b->state.bgi.y=27;
			feed(&f,wire,split); feed(&f,wire+split,sizeof(wire)-1-split);
			CHECK(f.terminal->xpos==21 && f.terminal->ypos==10 && f.resizes==1 && f.size==0);
			CHECK(b->state.bgi.x==19 && b->state.bgi.y==27);
			CHECK(b->control.pointer.step_x==1 && b->control.pointer.step_y==15);
			destroy(&f);
		}
	struct fixture f; create(&f,false,false);
	for (size_t i=0;i<sizeof(wire)-1;++i) feed(&f,wire+i,1);
	CHECK(f.terminal->big_engine->control.pointer.step_x==1 && f.terminal->big_engine->control.pointer.step_y==15);
	destroy(&f);
}
static void extended_prefix_and_unknown(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#X 6,0,");
	CHECK(b->control.pointer.step_x==0 && b->control.pointer.step_y==6);
	CHECK(st_cterm_finish(f.terminal)==0 && b->control.pointer.step_x==0 && b->control.pointer.step_y==6);
	string(&f,"I#X 6,20,"); CHECK(b->control.pointer.step_x==20 && b->control.pointer.step_y==6);
	/* The later clamps read current global X rather than reassigning the
	 * earlier packet value after the waiting Y read returns. */
	b->control.pointer.step_x=0; string(&f,"5:");
	CHECK(b->control.pointer.step_x==1 && b->control.pointer.step_y==5);
	string(&f,"I#X 6,+1,x:"); CHECK(b->control.pointer.step_x==1 && b->control.pointer.step_y==1);
	string(&f,"I#X 6,257,258:"); CHECK(b->control.pointer.step_x==15 && b->control.pointer.step_y==15);
	string(&f,"I#I 1:I#I 0:I#R 1:");
	CHECK(b->control.pointer.step_x==15 && b->control.pointer.step_y==15);
	static const char *unknown[]={"I#X 9:Z","I#X 262:Z","I#X +1:Z"};
	for (size_t i=0;i<sizeof(unknown)/sizeof(unknown[0]);++i) {
		string(&f,"\033[H"); string(&f,unknown[i]);
		CHECK(f.terminal->xpos==2 && b->control.pointer.step_x==15 && b->control.pointer.step_y==15);
	}
	string(&f,"I#& 0,2,1,0,X@1,9:");
	CHECK(b->numeric.loop.active==0 && b->numeric.loop.current==2 && f.size==2 && f.output[0]==19 && f.output[1]==17);
	string(&f,"I#"); b->control.input.pending=1; b->control.input.count=2;
	b->control.input.payload[1]=0; b->control.input.payload[2]=255;
	string(&f,"X>9,"); CHECK(b->control.input.pending==1 && f.size==2);
	string(&f,"~ "); CHECK(f.size==4 && f.output[2]==0 && f.output[3]==255);
	destroy(&f);
}
static void extended_numeric_sources(void)
{
	struct fixture f; create(&f,true,true); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#R 2:I#g 2:I#X 6,5,5:"); CHECK(b->control.pointer.step_x==5 && b->control.pointer.step_y==8);
	string(&f,"I#R 3:I#g 3:I#X 6,5,5:"); CHECK(b->control.pointer.step_x==5 && b->control.pointer.step_y==11);
	string(&f,"I#R 2:I#X 6,5,5:"); CHECK(b->control.pointer.step_x==5 && b->control.pointer.step_y==5);
	string(&f,"I#g 258:I#X 6,5,5:"); CHECK(b->control.pointer.step_y==5);
	string(&f,"I#R 3:I#g 3:");
	/* Stored selector ends the loop; both step reads now own wire bytes. */
	string(&f,"I#& 0,1,1,0,X@1,6:4,5:");
	CHECK(b->numeric.loop.active==0 && b->control.pointer.step_x==4 && b->control.pointer.step_y==11);
	/* Stored X ends the loop; only Y comes from the scaled wire reader. */
	string(&f,"I#& 0,1,1,0,X@2,6,5:5:");
	CHECK(b->numeric.loop.active==0 && b->control.pointer.step_x==5 && b->control.pointer.step_y==11);
	/* The final stored Y bypasses ordinary scaling, despite g3/cache3. */
	string(&f,"I#& 0,1,1,0,X@3,6,5,5:");
	CHECK(b->numeric.loop.active==0 && b->control.pointer.step_x==5 && b->control.pointer.step_y==5);
	CHECK(f.size==6 && memcmp(f.output,"\x13\x11\x13\x11\x13\x11",6)==0);
	string(&f,"I#X 6,0,5"); CHECK(b->control.pointer.step_x==0 && b->control.pointer.step_y==5);
	local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS;
	CHECK(st_cterm_poll(f.terminal)==0 && b->control.pointer.step_x==1 && b->control.pointer.step_y==11 && f.bells==1);
	string(&f,"I#X 6,16,5"); CHECK(st_cterm_reset_terminal(f.terminal)==0);
	CHECK(b->control.pointer.step_x==16 && b->control.pointer.step_y==11);
	string(&f,"I#X 6,5,5:"); CHECK(b->control.pointer.step_x==5 && b->control.pointer.step_y==11);
	destroy(&f);
}
int main(void)
{
	partitions(); retained_and_normalized(); source_and_recovery(); mode_return_paths();
	extended_ownership(); extended_prefix_and_unknown(); extended_numeric_sources(); return 0;
}
