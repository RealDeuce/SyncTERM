/* Native S/cache/RGB/state expectations, original BIG26/45/55. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm.h"
#include "cterm_big_internal.h"
#include "st_cterm.h"
#include "io_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
struct fixture { st_console_t *console; st_font_catalog_t *fonts; st_cterm_t *terminal; uint8_t output[64]; size_t size; bool utf8,status; };
static uint64_t local_ns;
static const uint8_t defaults[]={0,1,2,3,4,5,20,7,56,57,58,59,60,61,62,63};
static int now(void *context, uint64_t *value) { (void)context; *value=local_ns; return 0; }
static void output(void *context, enum st_cterm_output_origin origin, const uint8_t *bytes, size_t size)
{
	struct fixture *f=context; (void)origin; CHECK(size<=sizeof(f->output)-f->size);
	memcpy(f->output+f->size,bytes,size); f->size+=size;
}
static void terminal(struct fixture *f)
{
	const struct st_cterm_config config={.console=f->console,.fonts=f->fonts,.emulation=ST_CTERM_ANSI_BBS,
	    .graphics_mode=ST_CTERM_GRAPHICS_BIG_103,.encoding=f->utf8?ST_CTERM_ENCODING_UTF8:ST_CTERM_ENCODING_CODEPAGE,
	    .codepage=ST_CODEPAGE_CP437,.quiet=true,.status_line=f->status,.scrollback_lines=2,.output=output,.output_context=f};
	CHECK(st_cterm_create(&f->terminal,&config)==0); f->terminal->big_engine->numeric.io.now_ns=now;
}
static void create(struct fixture *f, bool utf8, bool status)
{
	memset(f,0,sizeof(*f)); local_ns=1000; f->utf8=utf8; f->status=status;
	const struct st_console_display_config display={.backend=ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry={.columns=80,.rows=25,.cell_width=8,.cell_height=14};
	CHECK(st_console_create(&f->console,&display,&geometry)==0 && st_font_catalog_create(&f->fonts,f->console)==0); terminal(f);
}
static void destroy(struct fixture *f) { st_cterm_destroy(&f->terminal); st_font_catalog_destroy(f->fonts); st_console_destroy(f->console); }
static void feed(struct fixture *f, const void *bytes, size_t size) { size_t used; CHECK(st_cterm_write(f->terminal,bytes,size,&used)==0 && used==size); }
static void string(struct fixture *f, const char *wire) { feed(f,wire,strlen(wire)); }
static uint32_t rgb(unsigned code)
{
	/* Primary/secondary B/G/R component weights from reference45,
	 * independent of the shared native64-entry table. */
	unsigned r=85u*(2u*((code>>2)&1u)+((code>>5)&1u));
	unsigned g=85u*(2u*((code>>1)&1u)+((code>>4)&1u));
	unsigned b=85u*(2u*(code&1u)+((code>>3)&1u));
	return r*65536u+g*256u+b;
}
static void color(struct fixture *f, unsigned index, uint32_t expected)
{
	uint32_t value; CHECK(st_console_get_palette(f->console,(uint16_t)(800u+index),&value,1)==0 && value==expected);
}
static void image(struct fixture *f, unsigned res)
{
	char wire[100]; snprintf(wire,sizeof(wire),"I#R %u:I#C>0,5:P>100,100:G 0,0,100,100,100,100,120,100:",res); string(f,wire);
	f->terminal->big_engine->state.bgi.error=-11; f->terminal->big_engine->state.bgi.background=7; f->size=0;
}
static void canvas(struct fixture *f, unsigned copies)
{
	uint32_t width,height; CHECK(st_console_pixel_size(f->console,&width,&height)==0);
	const struct st_console_rect area={0,0,width,height}; struct st_pixel_buffer *p=NULL;
	CHECK(st_console_get_pixels(&p,f->console,&area,false)==0);
	for (size_t i=0;i<(size_t)width*height;++i) {
		bool dot=false;
		for (unsigned n=0;n<copies;++n) if (i==100u*width+100u+20u*n) dot=true;
		CHECK(p->primary[i]==(dot?805u:800u) && p->alternate[i]==p->primary[i]);
	}
	st_pixel_buffer_destroy(p);
}
static void partitions(void)
{
	static const char wire[]="I#S>5,128:S>5,262:S>16,63:S 5,+1:\033[6n";
	for (unsigned utf8=0;utf8<2;++utf8) for (unsigned status=0;status<2;++status) {
		struct fixture f; create(&f,utf8,status); struct cterm_big *b=f.terminal->big_engine;
		for (unsigned res=1;res<=3;++res) for (size_t split=0;split<=sizeof(wire);++split) {
			if (res!=2 && split!=sizeof(wire)) continue;
			image(&f,res); uint16_t saved[16]; memcpy(saved,b->state.saved_palette,sizeof(saved));
			if (split==sizeof(wire)) for (size_t i=0;i<sizeof(wire)-1;++i) feed(&f,wire+i,1);
			else { feed(&f,wire,split); feed(&f,wire+split,sizeof(wire)-1-split); }
			CHECK(b->last_error==0 && b->rejected_commands==0 && b->state.bgi.palette_size==16 && b->state.bgi.palette[5]==6);
			CHECK(b->state.bgi.background==7 && b->state.bgi.error==-11 && b->state.bgi.x==100 && b->state.bgi.ddo_x==100 && b->state.bgi.device_y==100);
			CHECK(b->state.memory_page==1 && b->state.active_page==0 && b->state.visual_page==0 && b->video_active_page==0 && b->video_visual_page==0);
			CHECK(b->state.local_palette[6]==5 && memcmp(saved,b->state.saved_palette,sizeof(saved))==0);
			CHECK(memcmp(b->image_bytes,"\0\0\0\0\0\200\0\200",8)==0 && b->image_size==10);
			CHECK(f.size==6 && memcmp(f.output,"\033[1;1R",6)==0); color(&f,5,0xaaaa00); canvas(&f,2);
			CHECK(st_cterm_poll(f.terminal)==0 && st_cterm_finish(f.terminal)==0);
		}
		destroy(&f);
	}
}
static void codes_images_and_restart(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	for (unsigned res=1;res<=3;++res) {
		image(&f,res);
		for (unsigned code=0;code<256;++code) {
			char wire[32]; snprintf(wire,sizeof(wire),"I#S 5,%u:",256u+code); string(&f,wire);
			CHECK(b->state.bgi.palette[5]==code && b->state.bgi.error==-11 && b->last_error==0);
			color(&f,5,rgb(code));
		}
		CHECK(b->state.local_palette[6]==254 && b->state.saved_palette[5]==0);
		string(&f,"I#G 2,0,140,100:"); CHECK(b->state.local_palette[6]==255); canvas(&f,3);
		/* RGB aliases do not merge stored pens, even when capturing anew. */
		string(&f,"I#S>0,63:G 0,0,100,100,100,100,160,100:");
		CHECK(b->state.bgi.background==0 && b->state.bgi.palette[0]==63 && b->state.bgi.palette[5]==255);
		CHECK(b->image_bytes[5]==128 && b->image_bytes[7]==128); color(&f,0,0xffffff); color(&f,5,0xffffff); canvas(&f,4);
		string(&f,"I#I 1:"); CHECK(memcmp(b->state.bgi.palette,defaults,16)==0); color(&f,0,0); color(&f,5,0xaa00aa); canvas(&f,4);
	}
	string(&f,"I#S 5,63:"); color(&f,5,0xffffff);
	st_cterm_destroy(&f.terminal); terminal(&f); b=f.terminal->big_engine;
	CHECK(b->state.bgi.palette_size==16 && memcmp(b->state.bgi.palette,defaults,16)==0); color(&f,5,0xaa00aa); canvas(&f,0);
	destroy(&f);
}
static void acquisition_gates_and_recovery(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine; image(&f,2);
	string(&f,"I#S 5,"); CHECK(b->state.bgi.palette[5]==5); color(&f,5,0xaa00aa);
	b->feature_gates[2][0]=1; string(&f,"63:"); CHECK(b->state.bgi.palette[5]==5 && b->last_error==0);
	b->feature_gates[2][1]=1; b->feature_gates[0][0]=1;
	string(&f,"I#S 5,63:"); CHECK(b->state.bgi.palette[5]==5);
	b->feature_gates[0][1]=1; b->feature_gates[1][0]=b->feature_gates[3][0]=1;
	string(&f,"I#S 5,63:"); CHECK(b->state.bgi.palette[5]==63); color(&f,5,0xffffff);
	/* Internal I1 setters bypass S gates, as future X8 setters must. */
	b->feature_gates[0][0]=b->feature_gates[2][0]=0;
	string(&f,"I#I 1:"); CHECK(memcmp(b->state.bgi.palette,defaults,16)==0); color(&f,5,0xaa00aa);
	memset(b->feature_gates,0,sizeof(b->feature_gates));
	string(&f,"I#S 5,"); b->state.bgi.palette_size=4; string(&f,"63:"); CHECK(b->state.bgi.palette[5]==5);
	b->state.bgi.palette_size=16;
	string(&f,"I#S 5,"); b->state.bgi.state=0; string(&f,"63:");
	CHECK(b->state.bgi.palette[5]==5 && b->state.bgi.error==-11 && b->last_error==0); b->state.bgi.state=3;
	string(&f,"I#S 5,"); local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS;
	CHECK(st_cterm_poll(f.terminal)==0 && b->state.bgi.palette[5]==0); color(&f,5,0);
	string(&f,"I#S 5,63:I#S 5,"); CHECK(st_cterm_finish(f.terminal)==0 && b->state.bgi.palette[5]==63);
	CHECK(st_cterm_reset_terminal(f.terminal)==0 && b->state.bgi.palette[5]==63); color(&f,5,0xffffff);
	/* Checked corrupt-cache bounds reject nonfatally before an out-of-
	 * array store, preserving the next valid S and ANSI reply. */
	b->state.bgi.palette_size=17; f.size=0;
	string(&f,"I#S 16,63:I#S 5,262:\033[6n");
	CHECK(b->last_error==IO_ERROR_OUT_OF_RANGE && b->rejected_commands==1 && b->state.bgi.palette[5]==6 && b->state.bgi.error==-11);
	CHECK(f.size==6 && memcmp(f.output,"\033[1;1R",6)==0); color(&f,5,0xaaaa00);
	b->state.bgi.palette_size=16; string(&f,"I#R 3:I#g 3:I#S 5,63:");
	CHECK(b->state.bgi.palette[5]==63 && b->numeric.context.scaling==3); color(&f,5,0xffffff);
	/* Supplied class0 values retain the exact stored index/value and use
	 * common loop flow, independent of g3 and the resolution cache. */
	f.size=0; string(&f,"I#& 0,2,1,0,S@2,5,x:");
	CHECK(b->state.bgi.palette[5]==2 && f.size==2 && memcmp(f.output,"\x13\x11",2)==0);
	CHECK(st_cterm_poll(f.terminal)==0 && st_cterm_finish(f.terminal)==0); color(&f,5,0xaa00); destroy(&f);
}
int main(void) { partitions(); codes_images_and_restart(); acquisition_gates_and_recovery(); return 0; }
