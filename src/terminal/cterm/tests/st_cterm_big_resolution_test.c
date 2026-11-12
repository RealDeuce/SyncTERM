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
	bool status, block_on, reset_on, reset_bell;
	uint32_t heights[32]; uint8_t output[64]; size_t size;
};
static uint64_t local_ns;
static int now(void *context, uint64_t *value) { (void)context; *value=local_ns; return 0; }
static void bell(void *context)
{
	struct cterm_big *b=context; struct fixture *f=b->terminal->api_output_context;
	++f->bells; if (f->reset_bell) CHECK(st_cterm_reset_terminal(f->terminal)==0);
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
/* X4 expectations transcribed from chapter32/64, including the extra read. */
static void zone_wire_ownership(void)
{
	static const uint8_t wire[]="I#X 4,0,1,2,3,4,6:I#R 3:!Z";
	for (unsigned utf8=0;utf8<2;++utf8) for (unsigned status=0;status<2;++status)
		for (size_t split=0;split<sizeof(wire);++split) {
			struct fixture f; create(&f,utf8,status); struct cterm_big *b=f.terminal->big_engine;
			for (size_t i=0;i<ST_BIG_ZONE_COUNT;++i)
				for (size_t j=0;j<4;++j) CHECK(b->control.zones.rectangles[i][j]==0);
			for (size_t i=0;i<ST_BIG_ZONE_DATA_SIZE;++i) CHECK(b->control.zones.payload[i]==0);
			CHECK(b->control.zones.loopback==0);
			string(&f,"\033[10;20H"); feed(&f,wire,split); feed(&f,wire+split,sizeof(wire)-1-split);
			CHECK(f.resizes==1 && f.terminal->xpos==21 && f.terminal->ypos==10 && f.size==0);
			for (size_t j=0;j<4;++j) CHECK(b->control.zones.rectangles[0][j]==j+1);
			CHECK(memcmp(b->control.zones.payload,"\6I#R 3:",7)==0 && b->zone.phase==CTERM_BIG_ZONE_OFF);
			destroy(&f);
		}
	struct fixture f; create(&f,false,false);
	for (size_t i=0;i<sizeof(wire)-1;++i) feed(&f,wire+i,1);
	CHECK(f.terminal->xpos==2 && f.terminal->big_engine->control.zones.payload[6]==':');
	destroy(&f);
}
static void zone_prefixes_and_specials(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#X 4,0,10,"); CHECK(b->control.zones.rectangles[0][0]==10 && b->control.zones.rectangles[0][1]==0);
	CHECK(st_cterm_finish(f.terminal)==0 && b->control.zones.rectangles[0][0]==10);
	string(&f,"I#R 2:I#g 2:I#X 4,0,10,5,1,7,6:abcdef!");
	static const uint16_t r2[]={10,8,1,12};
	CHECK(memcmp(b->control.zones.rectangles[0],r2,sizeof(r2))==0);
	string(&f,"I#R 3:I#g 3:I#X 4,0,1,5,3,5,2:");
	CHECK(b->control.zones.payload[0]==2 && b->zone.phase==CTERM_BIG_ZONE_READ);
	static const uint8_t raw[]={0,255,'@'}; feed(&f,raw,sizeof(raw));
	static const uint16_t r3[]={1,11,3,11};
	CHECK(memcmp(b->control.zones.rectangles[0],r3,sizeof(r3))==0);
	CHECK(memcmp(b->control.zones.payload,"\2\0\xff" "cdef",7)==0);
	string(&f,"I#X 4,9998:I#I 1:I#I 0:I#R 2:");
	CHECK(b->control.zones.loopback==1 && memcmp(b->control.zones.rectangles[0],r3,sizeof(r3))==0);
	string(&f,"I#X 4,9999:");
	for (size_t i=0;i<ST_BIG_ZONE_COUNT;++i)
		for (size_t j=0;j<4;++j) CHECK(b->control.zones.rectangles[i][j]==65535);
	CHECK(b->control.zones.loopback==1 && memcmp(b->control.zones.payload,"\2\0\xff" "cdef",7)==0);
	string(&f,"I#X 4,9997:"); CHECK(b->control.zones.loopback==0);
	/* Special ids take no coordinates or raw bytes. */
	string(&f,"\033[HI#X 4,9999:Z"); CHECK(f.terminal->xpos==2);
	string(&f,"I#g 0:I#X 4,257,1,2,3,4,0:A!");
	CHECK(b->control.zones.rectangles[47][0]==1 && b->control.zones.payload[47*81]==1 && b->control.zones.payload[47*81+1]=='A');
	string(&f,"I#X 4,0,7,8,9,10,+1:B!"); CHECK(b->control.zones.payload[0]==1 && b->control.zones.payload[1]=='B');
	string(&f,"I#X 4,0,11,12,13,"); CHECK(st_cterm_reset_terminal(f.terminal)==0);
	CHECK(b->control.zones.rectangles[0][0]==11 && b->control.zones.rectangles[0][2]==13 && b->control.zones.payload[1]=='B');
	/* Negative ids fail at the first actual store, after reading X1 only. */
	const char bad[]="I#X 4,+1,9,8,7,6,1:A!"; size_t used;
	CHECK(st_cterm_write(f.terminal,bad,sizeof(bad)-1,&used)==0 && b->last_error==IO_ERROR_OUT_OF_RANGE && used==sizeof(bad)-1);
	CHECK(b->control.zones.rectangles[47][0]==1 && b->zone.phase==CTERM_BIG_ZONE_OFF);
	string(&f,"I#X 4,0,1,2,3,4,255:"); CHECK(b->control.zones.payload[0]==80);
	for (unsigned i=0;i<80;++i) { uint8_t byte=(uint8_t)i; feed(&f,&byte,1); }
	CHECK(b->zone.phase==CTERM_BIG_ZONE_READ);
	for (unsigned i=0;i<80;++i) CHECK(b->control.zones.payload[i+1]==i);
	string(&f,"!Z"); CHECK(b->zone.phase==CTERM_BIG_ZONE_OFF);
	destroy(&f);
}
static void zone_live_lengths_and_bounds(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#X 4,0,1,2,3,4,1:"); b->control.zones.payload[0]=3;
	string(&f,"ABC!Z"); CHECK(memcmp(b->control.zones.payload,"\3ABC",4)==0 && f.terminal->xpos==2);
	string(&f,"\033[HI#X 4,0,1,2,3,4,1:A"); CHECK(b->zone.extra);
	b->control.zones.payload[0]=3; string(&f,"BC");
	CHECK(b->control.zones.payload[2]=='B' && f.terminal->xpos==2); /* B discarded, C ordinary. */
	/* Cross-record writes remain inside the flat table, including lengths. */
	string(&f,"I#X 4,0,1,2,3,4,1:"); b->control.zones.payload[0]=82;
	for (unsigned i=0;i<82;++i) string(&f,"Q");
	CHECK(b->control.zones.payload[81]=='Q' && b->control.zones.payload[82]=='Q'); string(&f,"!");
	string(&f,"I#X 4,47,1,2,3,4,1:"); b->control.zones.payload[47*81]=255;
	for (unsigned i=0;i<80;++i) string(&f,"R");
	size_t used; CHECK(st_cterm_write(f.terminal,"ST",2,&used)==0 && b->last_error==IO_ERROR_OUT_OF_RANGE && used==2);
	CHECK(b->control.zones.payload[ST_BIG_ZONE_DATA_SIZE-1]=='R' && b->zone.phase==CTERM_BIG_ZONE_OFF);
	/* A full payload yields at a work budget, without truncating data. */
	string(&f,"I#X 4,0,1,2,3,4,80:"); uint8_t raw[82]; memset(raw,'K',80); raw[80]='!'; raw[81]='Z';
	CHECK(st_cterm_write(f.terminal,raw,sizeof(raw),&used)==IO_ERROR_IN_PROGRESS && used==64 && st_cterm_input_paused(f.terminal));
	feed(&f,raw+used,sizeof(raw)-used);
	for (size_t i=1;i<=80;++i) CHECK(b->control.zones.payload[i]=='K');
	CHECK(b->zone.phase==CTERM_BIG_ZONE_OFF && !st_cterm_input_paused(f.terminal));
	destroy(&f);
}
static int zone_ready(void *context, enum st_big_output_kind kind)
{
	struct cterm_big *b=context; struct fixture *f=b->terminal->api_output_context;
	if (kind!=ST_BIG_OUTPUT_XON) return 0;
	if (f->reset_on) { CHECK(st_cterm_reset_terminal(f->terminal)==0); return IO_ERROR_INTERRUPTED; }
	return f->block_on?IO_ERROR_IN_PROGRESS:0;
}
static void zone_sources_and_flow(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#R 3:I#g 3:I#& 0,1,1,0,X@7,4,0,1,2,3,4,2:AB!Z");
	static const uint16_t coords[]={1,2,3,4};
	CHECK(memcmp(b->control.zones.rectangles[0],coords,sizeof(coords))==0);
	CHECK(f.size==2 && memcmp(f.output,"\x13\x11",2)==0 && memcmp(b->control.zones.payload,"\2AB",3)==0);
	string(&f,"I#& 0,1,1,0,X@8,4,0,1,2,3,4,2,99:CD!Z");
	CHECK(f.size==5 && memcmp(f.output+2,"\x13\x11\x11",3)==0 && memcmp(b->control.zones.payload,"\2CD",3)==0);
	b->control_io.ready=zone_ready; f.block_on=true;
	const char blocked[]="I#& 0,1,1,0,X@8,4,0,1,2,3,4,2,99:"; size_t used;
	CHECK(st_cterm_write(f.terminal,blocked,sizeof(blocked)-1,&used)==IO_ERROR_IN_PROGRESS && used==sizeof(blocked)-1);
	CHECK(f.size==6 && f.output[5]==19 && b->control.zones.payload[0]==2 && st_cterm_input_paused(f.terminal));
	local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS*2;
	/* Flow gate was taken already; changing loop does not remove its XON.
	 * The first auxiliary deadline starts after output becomes ready. */
	b->numeric.loop.active=0; f.block_on=false; CHECK(st_cterm_poll(f.terminal)==0);
	CHECK(f.size==7 && f.output[6]==17 && f.bells==0 && b->zone.phase==CTERM_BIG_ZONE_READ);
	string(&f,"EF!Z"); CHECK(f.size==8 && f.output[7]==17 && f.bells==0 && memcmp(b->control.zones.payload,"\2EF",3)==0);
	f.reset_on=true;
	CHECK(st_cterm_write(f.terminal,blocked,sizeof(blocked)-1,&used)==0 && used==sizeof(blocked)-1);
	CHECK(f.size==9 && f.output[8]==19 && b->zone.phase==CTERM_BIG_ZONE_OFF && !st_cterm_input_paused(f.terminal));
	CHECK(b->control.zones.payload[0]==2 && b->control.zones.payload[1]=='E');
	destroy(&f);
}
static void zone_chain_and_key_ownership(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#"); b->control.input.pending=1; b->control.input.count=1; b->control.input.payload[1]='r';
	string(&f,"X>4,0,1,2,3,4,1:A!");
	CHECK(b->control.input.pending==1 && f.size==0 && b->zone.phase==CTERM_BIG_ZONE_OFF);
	string(&f,"X 4,9998:"); CHECK(f.size==1 && f.output[0]=='r' && b->control.zones.loopback==1);
	string(&f,"I#X 6,4,9:I#X 4,9999:");
	CHECK(b->control.pointer.step_x==4 && b->control.pointer.step_y==9);
	string(&f,"I#X 4,0,1,2,3,4,1:");
	/* Raw loading itself never claims local keys. Previously admitted loop
	 * events stay queued until the body/footer has finished. */
	CHECK(!cterm_big_input_owned(b));
	CHECK(st_cterm_handle_key(f.terminal,'z',0) && f.size==2 && f.output[1]=='z');
	b->numeric.loop.active=1;
	CHECK(st_cterm_handle_key(f.terminal,'a',0) && b->input.count==1 && f.size==2);
	b->numeric.loop.active=0;
	CHECK(st_cterm_poll(f.terminal)==0 && b->input.count==1 && f.size==2);
	string(&f,"B!"); CHECK(b->input.count==0 && f.size==3 && f.output[2]=='a');
	destroy(&f);
}
static void zone_timeouts_and_eof(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#X 4,0,1,2,3,4,2:A"); local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS;
	/* A late ready byte is consumed but replaced by SPACE. */
	string(&f,"B"); CHECK(b->control.zones.payload[1]=='A' && b->control.zones.payload[2]==' ' && f.bells==1);
	CHECK(b->zone.extra && b->zone.phase==CTERM_BIG_ZONE_READ);
	local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS; CHECK(st_cterm_poll(f.terminal)==0 && f.bells==2 && b->zone.phase==CTERM_BIG_ZONE_OFF);
	b->numeric.context.auto_mode=true; string(&f,"I#X 4,0,1,2,3,4,1:"); local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS;
	CHECK(st_cterm_poll(f.terminal)==0 && b->control.zones.payload[1]==' ' && f.bells==2 && b->zone.extra);
	CHECK(st_cterm_finish(f.terminal)==0 && b->zone.phase==CTERM_BIG_ZONE_OFF && f.bells==2);
	string(&f,"I#X 4,0,9,8,7,6,2:T"); CHECK(st_cterm_finish(f.terminal)==0);
	CHECK(b->control.zones.payload[0]==2 && b->control.zones.payload[1]=='T' && b->control.zones.payload[2]==' ');
	string(&f,"I#X 4,9998:"); CHECK(b->control.zones.loopback==1);
	destroy(&f);
}
static void marker_wire_pixel(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#P 11,12:");
	const struct st_console_rect rect={0,0,640,350}; struct st_pixel_buffer *pixels=NULL;
	CHECK(st_console_get_pixels(&pixels,f.console,&rect,false)==0);
	for (size_t y=0;y<350;++y) for (size_t x=0;x<640;++x) {
		uint32_t expected=x==11 && y==12?815:800;
		CHECK(pixels->primary[y*640+x]==expected && pixels->alternate[y*640+x]==expected);
	}
	st_pixel_buffer_destroy(pixels);
	CHECK(b->state.bgi.x==11 && b->state.bgi.y==12 && f.terminal->xpos==1 && f.size==0);
	destroy(&f);
}
static void marker_style_wire(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#T 1,264,"); CHECK(b->state.application.marker==7 && b->state.application.marker_size==1);
	string(&f,"264:"); CHECK(b->state.application.marker_size==8);
	string(&f,"I#T 2,1,3,4,0:");
	CHECK(b->state.bgi.line_mask==0xcccc && b->state.bgi.line_user==12 && b->state.bgi.line_width==1);
	destroy(&f);
}
static void marker_partitions(void)
{
	const char wire[]="I#T>1,2,1:P 20,20:";
	for (unsigned resolution=1;resolution<=3;++resolution)
		for (unsigned utf8=0;utf8<2;++utf8) for (unsigned status=0;status<2;++status)
			for (size_t split=0;split<sizeof(wire);++split) {
				struct fixture f; create(&f,utf8,status); struct cterm_big *b=f.terminal->big_engine;
				char mode[16]; snprintf(mode,sizeof(mode),"I#R %u:",resolution); string(&f,mode);
				feed(&f,wire,split); feed(&f,wire+split,sizeof(wire)-1-split);
				CHECK(b->state.bgi.x==20 && b->state.bgi.y==20 && b->state.bgi.ddo_x==20 && b->state.bgi.device_y==20);
				CHECK(f.size==0 && f.terminal->xpos==1 && b->state.application.marker==2 && b->state.application.marker_size==1);
				struct st_pixel_buffer *pixels=NULL; const struct st_console_rect rect={0,0,640,b->state.bgi.height};
				CHECK(st_console_get_pixels(&pixels,f.console,&rect,false)==0);
				for (size_t y=0;y<rect.height;++y) for (size_t x=0;x<640;++x) {
					bool cross=(x==20 && y>=17 && y<=23) || (y==20 && x>=16 && x<=24);
					CHECK(pixels->primary[y*640+x]==(cross?815u:800u) && pixels->alternate[y*640+x]==(cross?815u:800u));
				}
				st_pixel_buffer_destroy(pixels); destroy(&f);
			}
	struct fixture f; create(&f,false,false);
	for (size_t i=0;i<sizeof(wire)-1;++i) feed(&f,wire+i,1);
	CHECK(f.terminal->big_engine->state.bgi.x==20 && f.terminal->big_engine->state.application.marker==2);
	destroy(&f);
}
static void marker_pixel(struct fixture *f, uint32_t x, uint32_t y, uint32_t color)
{
	const struct st_console_rect rect={x,y,1,1}; struct st_pixel_buffer *p=NULL;
	CHECK(st_console_get_pixels(&p,f->console,&rect,false)==0 && p->primary[0]==color && p->alternate[0]==color);
	st_pixel_buffer_destroy(p);
}
static void marker_prefix_sources_and_recovery(void)
{
	struct fixture f; create(&f,true,true); struct cterm_big *b=f.terminal->big_engine;
	b->state.application.colors[0]=5; string(&f,"I#P "); CHECK(b->state.bgi.color==5);
	b->state.application.colors[0]=9; string(&f,"11,12:"); marker_pixel(&f,11,12,809);
	CHECK(b->state.bgi.color==5 && b->state.bgi.device_color==5 && b->state.application.colors[1]==15);
	string(&f,"I#T 1,2,1:"); b->state.application.colors[0]=6; string(&f,"I#P 20,");
	CHECK(b->state.bgi.color==6); b->state.application.colors[0]=10; string(&f,"20:");
	CHECK(b->state.bgi.color==10 && b->state.bgi.x==20 && b->state.bgi.y==20); marker_pixel(&f,20,20,810);
	string(&f,"I#T 1,0,1:I#P +1,10:");
	CHECK(b->state.bgi.x==65535 && b->state.bgi.ddo_x==65535 && b->state.bgi.device_x==65535);
	b->state.application.colors[0]=7; string(&f,"I#P 31"); CHECK(st_cterm_finish(f.terminal)==0);
	CHECK(b->state.bgi.color==7 && b->state.bgi.x==65535); marker_pixel(&f,31,0,800);
	string(&f,"I#P 32,"); CHECK(st_cterm_reset_terminal(f.terminal)==0 && b->state.bgi.x==65535);
	string(&f,"I#R 2:I#g 2:I#P 5,5:"); marker_pixel(&f,5,8,807);
	string(&f,"I#R 3:I#g 3:I#P 5,5:"); marker_pixel(&f,5,11,807);
	string(&f,"I#& 0,1,1,0,P@2,7,5:"); marker_pixel(&f,7,5,807);
	string(&f,"I#& 0,1,1,0,P@1,9:5:"); marker_pixel(&f,9,11,807);
	CHECK(f.size==4 && memcmp(f.output,"\x13\x11\x13\x11",4)==0);
	string(&f,"I#P 12,"); local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS;
	CHECK(st_cterm_poll(f.terminal)==0 && f.bells==1 && b->state.bgi.x==12 && b->state.bgi.y==0); marker_pixel(&f,12,0,807);
	destroy(&f);
}
static void style_normalization_and_lifetime(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#T +1,256,264:"); CHECK(b->state.application.marker==0 && b->state.application.marker_size==8);
	string(&f,"I#T 0,264,265:"); CHECK(b->state.application.marker==7 && b->state.application.marker_size==1);
	string(&f,"I#T 1,+1,8:I#T 1,2,");
	CHECK(b->state.application.marker==2 && b->state.application.marker_size==8 && st_cterm_finish(f.terminal)==0);
	CHECK(b->state.application.marker==2 && b->state.application.marker_size==8);
	string(&f,"I#T 257,+1,257,257,2:");
	CHECK(b->state.bgi.line_style==4 && b->state.bgi.line_user==0x201 && b->state.bgi.line_mask==0x201 && b->state.bgi.line_width==2);
	string(&f,"I#T 2,0,1,1,0:I#T 2,4,1,1,9:");
	CHECK(b->state.bgi.error==-11 && b->state.bgi.line_mask==65535 && b->state.bgi.line_width==1);
	string(&f,"I#T 2,1,1,1,1:"); CHECK(b->state.bgi.line_width==3 && b->state.bgi.ddo_line_width==3 && b->state.bgi.line_mask==0xcccc && b->state.bgi.error==-11);
	string(&f,"I#T 2,258,257,"); CHECK(st_cterm_finish(f.terminal)==0 && b->state.bgi.line_mask==0xcccc);
	string(&f,"I#R 1:"); CHECK(b->state.application.marker==2 && b->state.application.marker_size==8 && b->state.bgi.line_width==1 && b->state.bgi.line_mask==65535);
	string(&f,"I#I 1:"); CHECK(b->state.application.marker==2 && b->state.application.marker_size==8);
	string(&f,"I#I 0:"); CHECK(b->state.application.marker==0 && b->state.application.marker_size==1);
	string(&f,"I#& 0,1,1,0,T@3,1,2,1:"); CHECK(b->state.application.marker==2 && b->state.application.marker_size==1 && f.size==2 && f.output[0]==19 && f.output[1]==17);
	destroy(&f);
}
static int seed_marker_pixel(void *context, const struct st_pixel_buffer *p, struct st_console_rect *changed)
{
	bool invalid=*(bool *)context;
	p->primary[20*p->width+20]=invalid?5:803; p->alternate[20*p->width+20]=806;
	*changed=(struct st_console_rect){20,20,1,1}; return 0;
}
static void marker_native_xor_and_failure(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	bool invalid=false; CHECK(st_console_modify_pixels(f.console,seed_marker_pixel,&invalid)==0);
	b->state.application.colors[0]=5; b->state.bgi.write_mode=1;
	string(&f,"I#T>1,1,1:P 20,20:");
	struct st_pixel_buffer *p=NULL; const struct st_console_rect rect={20,20,1,1};
	CHECK(st_console_get_pixels(&p,f.console,&rect,false)==0 && p->primary[0]==806 && p->alternate[0]==803);
	st_pixel_buffer_destroy(p);
	invalid=true; CHECK(st_console_modify_pixels(f.console,seed_marker_pixel,&invalid)==0);
	const char wire[]="I#P 20,20:"; size_t used;
	CHECK(st_cterm_write(f.terminal,wire,sizeof(wire)-1,&used)==0 && b->last_error==IO_ERROR_OUT_OF_RANGE && used==sizeof(wire)-1);
	CHECK(b->state.bgi.x==20 && b->state.bgi.y==20 && b->state.bgi.ddo_x==24 && b->state.bgi.ddo_y==24 && b->state.bgi.device_x==20);
	/* COPY recovers without a sticky backend-error latch; marker0 ignores
	 * the installed line XOR/pattern/width and replaces both blink planes. */
	string(&f,"I#T>1,0,1:P 20,20:"); marker_pixel(&f,20,20,805);
	destroy(&f);
}
static void marker_reset_during_timeout(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#P 11,"); f.reset_bell=true; local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS;
	CHECK(st_cterm_poll(f.terminal)==0 && f.bells==1 && b->state.bgi.x==0 && b->state.bgi.y==0);
	marker_pixel(&f,11,0,800);
	string(&f,"I#T 1,4,8:I#T 1,2,"); local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS;
	CHECK(st_cterm_poll(f.terminal)==0 && f.bells==2 && b->state.application.marker==2 && b->state.application.marker_size==8);
	destroy(&f);
}
int main(void)
{
	marker_wire_pixel(); marker_style_wire(); marker_partitions(); marker_prefix_sources_and_recovery();
	style_normalization_and_lifetime(); marker_native_xor_and_failure(); marker_reset_during_timeout(); partitions(); retained_and_normalized(); source_and_recovery(); mode_return_paths();
	extended_ownership(); extended_prefix_and_unknown(); extended_numeric_sources(); zone_wire_ownership(); zone_prefixes_and_specials(); zone_live_lengths_and_bounds();
	zone_sources_and_flow(); zone_chain_and_key_ownership(); zone_timeouts_and_eof(); return 0;
}
