/* Independent BIG A state/wire expectations from chapters10/11/50/54/83. */
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
	unsigned bells; bool reset_bell; uint8_t output[64]; size_t size;
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
static void create(struct fixture *f, bool utf8, bool status)
{
	memset(f,0,sizeof(*f)); local_ns=1000;
	const struct st_console_display_config display={.backend=ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry={.columns=80,.rows=25,.cell_width=8,.cell_height=14};
	CHECK(st_console_create(&f->console,&display,&geometry)==0);
	CHECK(st_font_catalog_create(&f->fonts,f->console)==0);
	const struct st_cterm_config config={.console=f->console,.fonts=f->fonts,
	    .emulation=ST_CTERM_ANSI_BBS,.graphics_mode=ST_CTERM_GRAPHICS_BIG_103,
	    .encoding=utf8?ST_CTERM_ENCODING_UTF8:ST_CTERM_ENCODING_CODEPAGE,.codepage=ST_CODEPAGE_CP437,
	    .quiet=true,.status_line=status,.scrollback_lines=2,.output=output,.output_context=f};
	CHECK(st_cterm_create(&f->terminal,&config)==0);
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
static void blank(struct fixture *f)
{
	const struct st_console_rect r={0,0,640,f->terminal->big_engine->state.bgi.height}; struct st_pixel_buffer *p=NULL;
	CHECK(st_console_get_pixels(&p,f->console,&r,false)==0);
	for (size_t i=0;i<(size_t)r.width*r.height;++i) CHECK(p->primary[i]==800 && p->alternate[i]==800);
	st_pixel_buffer_destroy(p);
}
static void partitions(void)
{
	static const struct { const char *wire; uint16_t selected,style,color; int error; } cases[]={
		{"I#C>2,31:A 3:\033[2;3H",3,3,31,0},
		{"I#C>2,5:A 12:\033[2;3H",12,12,5,0},
		{"I#C>2,31:A 12:\033[2;3H",12,1,15,-11},
		{"I#C>2,256:A 258:\033[2;3H",1,1,256,0},
	};
	for (unsigned utf8=0;utf8<2;++utf8) for (unsigned status=0;status<2;++status) {
		struct fixture f; create(&f,utf8,status); struct cterm_big *b=f.terminal->big_engine;
		for (unsigned mode=1;mode<=3;++mode) for (size_t c=0;c<sizeof(cases)/sizeof(cases[0]);++c) {
			const size_t size=strlen(cases[c].wire);
			/* All partitions at the default profile, bytewise in all profiles. */
			size_t splits=mode==2 && !utf8 && !status?size+1:0;
			for (size_t split=0;split<=splits;++split) {
				char reset[16]; snprintf(reset,sizeof(reset),"I#R %u:",mode); string(&f,reset); b->state.bgi.error=0;
				if (split==splits) for (size_t i=0;i<size;++i) feed(&f,cases[c].wire+i,1);
				else { feed(&f,cases[c].wire,split); string(&f,cases[c].wire+split); }
				CHECK(b->state.application.fill==cases[c].selected && b->state.bgi.fill_style==cases[c].style && b->state.bgi.fill_color==cases[c].color && b->state.bgi.error==cases[c].error);
				CHECK(b->state.bgi.x==0 && b->state.bgi.ddo_x==0 && b->state.bgi.device_x==0 && b->state.bgi.color==15);
				CHECK(f.terminal->xpos==3 && f.terminal->ypos==2 && f.size==0); blank(&f);
			}
		}
		destroy(&f);
	}
}
static void prefixes_sources_and_errors(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#A 12"); CHECK(st_cterm_finish(f.terminal)==0 && b->state.application.fill==1 && b->state.bgi.fill_style==1);
	string(&f,"I#C 2,31:I#A "); CHECK(st_cterm_reset_terminal(f.terminal)==0 && b->state.application.fill==1);
	string(&f,"I#A "); local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS;
	CHECK(st_cterm_poll(f.terminal)==0 && f.bells==1 && b->state.application.fill==0 && b->state.bgi.fill_style==0 && b->state.bgi.fill_color==31);
	string(&f,"I#A 3:I#A "); f.reset_bell=true; local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS;
	CHECK(st_cterm_poll(f.terminal)==0 && f.bells==2 && b->state.application.fill==3);
	string(&f,"I#R 3:I#g 3:I#C 2,5:I#A 3:"); CHECK(b->state.application.fill==3 && b->state.bgi.fill_style==3);
	string(&f,"I#& 0,1,1,0,A@1,12:"); CHECK(b->state.application.fill==12 && b->state.bgi.fill_style==12 && b->state.bgi.fill_color==5 && f.size==2);
	string(&f,"I#& 0,3,1,0,A@1,x:"); CHECK(b->state.application.fill==3 && b->state.bgi.fill_style==3 && b->numeric.loop.active==0 && f.size==4 && memcmp(f.output,"\x13\x11\x13\x11",4)==0);
	b->control.input.pending=1; b->control.input.count=2; b->control.input.payload[1]=0; b->control.input.payload[2]=255; b->control.input.append_cr=1;
	string(&f,"I#C>2,31:A>12:"); CHECK(b->state.application.fill==12 && b->state.bgi.fill_style==3 && b->state.bgi.fill_color==5 && b->state.bgi.error==-11 && f.size==4);
	string(&f,"~ "); CHECK(f.size==7 && memcmp(f.output+4,"\0\xff\r",3)==0);
	string(&f,"I#C>2,5:A 12:I#I 1:"); CHECK(b->state.bgi.fill_style==12 && b->state.bgi.fill_color==5 && b->state.bgi.error==-11);
	CHECK(b->state.bgi.fill_user[0]==0xaa && b->state.bgi.device_fill_user[0]==0xaa && b->state.bgi.fill_pattern[0]==0xaa && b->state.bgi.fill_pixels[0]==5 && b->state.bgi.fill_pixels[1]==0);
	string(&f,"I#R 1:"); CHECK(b->state.application.fill==12 && b->state.application.colors[2]==5 && b->state.bgi.fill_style==1 && b->state.bgi.fill_color==15);
	for (size_t i=0;i<8;++i) CHECK(b->state.bgi.fill_user[i]==255 && b->state.bgi.device_fill_user[i]==255 && b->state.bgi.fill_pattern[i]==255);
	for (size_t i=0;i<64;++i) CHECK(b->state.bgi.fill_pixels[i]==15);
	string(&f,"I#I 0:"); CHECK(b->state.application.fill==1 && b->state.application.colors[2]==15);
	b->state.bgi.state=0; string(&f,"I#C 2,31:I#A ");
	const char builtin[]="3:"; size_t used;
	CHECK(st_cterm_write(f.terminal,builtin,sizeof(builtin)-1,&used)==IO_ERROR_PROTOCOL && used==sizeof(builtin)-1);
	CHECK(b->state.application.fill==3 && b->state.bgi.fill_style==3 && b->state.bgi.fill_color==31 && b->state.bgi.error==-1);
	CHECK(b->state.bgi.fill_low==31 && b->state.bgi.device_fill_style==1 && b->state.bgi.device_fill_color==15 && b->state.bgi.fill_pixels[0]==15);
	const char custom[]="I#A 12:";
	CHECK(st_cterm_write(f.terminal,custom,sizeof(custom)-1,&used)==IO_ERROR_PROTOCOL && used==sizeof(custom)-1);
	CHECK(b->state.application.fill==12 && b->state.bgi.fill_style==3 && b->state.bgi.fill_color==31 && b->state.bgi.error==-1);
	b->state.bgi.state=3; string(&f,"I#C>2,5:A 12:"); CHECK(b->state.bgi.fill_style==12 && b->state.bgi.fill_color==5 && b->state.bgi.error==-1);
	blank(&f); CHECK(f.size==7); destroy(&f);
}
static void edit_partitions(void)
{
	static const uint8_t default12[]={0xaa,0x55,0xaa,0x55,0xaa,0x55,0xaa,0x55};
	static const struct { const char *wire; unsigned slot; uint8_t rows[8]; } cases[]={
		{"I#X 1,12,1,128,0,0,0,0,0,0,1:\033[2;3H",0,{128,0,0,0,0,0,0,1}},
		{"I#X 1,269,1,257,258,+1,0,x,291,511,264:\033[2;3H",0,{1,2,255,0,1,35,255,8}},
		{"I#X 1,12,257:\033[2;3H",0,{0xaa,0x55,0xaa,0x55,0xaa,0x55,0xaa,0x55}},
		{"I#X 1,23,0:\033[2;3H",11,{0x20,0x50,0x8c,0x42,0x22,0x22,0x1e,0}},
	};
	for (unsigned utf8=0;utf8<2;++utf8) for (unsigned status=0;status<2;++status) {
		struct fixture f; create(&f,utf8,status); struct cterm_big *b=f.terminal->big_engine;
		for (unsigned mode=1;mode<=3;++mode) for (size_t c=0;c<sizeof(cases)/sizeof(cases[0]);++c) {
			size_t size=strlen(cases[c].wire), splits=mode==2 && !utf8 && !status?size+1:0;
			for (size_t split=0;split<=splits;++split) {
				char reset[16]; snprintf(reset,sizeof(reset),"I#R %u:",mode); string(&f,reset);
				memcpy(b->state.application.patterns,st_big_fill_restore,96);
				memset(b->state.application.patterns[cases[c].slot],0xcc,8);
				if (split==splits) for (size_t i=0;i<size;++i) feed(&f,cases[c].wire+i,1);
				else { feed(&f,cases[c].wire,split); string(&f,cases[c].wire+split); }
				CHECK(memcmp(b->state.application.patterns[cases[c].slot],cases[c].rows,8)==0);
				for (size_t slot=0;slot<12;++slot) if (slot!=cases[c].slot) CHECK(memcmp(b->state.application.patterns[slot],st_big_fill_restore[slot],8)==0);
				CHECK(b->state.application.fill==1 && b->state.bgi.fill_style==1 && b->state.bgi.fill_color==15 && b->state.bgi.color==15 && b->state.bgi.error==0);
				CHECK(b->state.bgi.x==0 && b->state.bgi.ddo_x==0 && b->state.bgi.device_x==0 && f.terminal->xpos==3 && f.terminal->ypos==2 && f.size==0);
				for (size_t i=0;i<8;++i) CHECK(b->state.bgi.fill_user[i]==255 && b->state.bgi.device_fill_user[i]==255);
				blank(&f);
			}
		}
		CHECK(memcmp(st_big_fill_restore[0],default12,8)==0); destroy(&f);
	}
}
static void edit_prefixes_and_sources(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	memset(b->state.application.patterns[0],0xcc,8);
	string(&f,"I#X 1,9,"); CHECK(st_cterm_finish(f.terminal)==0 && b->state.application.patterns[0][0]==0xcc);
	string(&f,"I#X 1,12,0:I#X 1,12,1,257,258,");
	CHECK(b->state.application.patterns[0][0]==1 && b->state.application.patterns[0][1]==2 && b->state.application.patterns[0][2]==0xaa);
	CHECK(st_cterm_finish(f.terminal)==0 && b->state.application.patterns[0][0]==1 && b->state.application.patterns[0][1]==2);
	string(&f,"I#X 1,12,0:I#X 1,12,1,3,"); f.reset_bell=true; local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS;
	CHECK(st_cterm_poll(f.terminal)==0 && f.bells==1 && b->state.application.patterns[0][0]==3 && b->state.application.patterns[0][1]==0x55);
	f.reset_bell=false; string(&f,"I#X 1,12,1,4,");
	for (unsigned row=1;row<8;++row) { local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS; CHECK(st_cterm_poll(f.terminal)==0); }
	CHECK(f.bells==8 && b->state.application.patterns[0][0]==4);
	for (size_t i=1;i<8;++i) CHECK(b->state.application.patterns[0][i]==0);
	string(&f,"I#R 3:I#g 3:I#& 0,1,1,0,X@3,1,12,1:257,258,259,260,261,262,263,264:");
	for (size_t i=0;i<8;++i) CHECK(b->state.application.patterns[0][i]==i+1);
	string(&f,"I#& 0,1,1,0,X@4,1,12,1,511:");
	CHECK(b->fill_edit.active && b->fill_edit.row==1 && b->state.application.patterns[0][0]==255 && b->numeric.loop.active==0);
	string(&f,"258,259,260,261,262,263,264:");
	for (size_t i=1;i<8;++i) CHECK(b->state.application.patterns[0][i]==i+1);
	string(&f,"I#& 0,1,1,0,X@11,1,12,1,128,0,0,0,0,0,0,1:");
	CHECK(b->state.application.patterns[0][0]==128 && b->state.application.patterns[0][7]==1);
	string(&f,"I#& 0,2,1,0,X@11,1,12,1,x,x,x,x,x,x,x,x:");
	for (size_t i=0;i<8;++i) CHECK(b->state.application.patterns[0][i]==2);
	CHECK(b->numeric.loop.active==0 && f.size==8 && memcmp(f.output,"\x13\x11\x13\x11\x13\x11\x13\x11",8)==0);
	b->control.input.pending=1; b->control.input.count=1; b->control.input.payload[1]='R';
	string(&f,"I#X>1,12,0:"); CHECK(f.size==8 && b->control.input.pending==1);
	string(&f,"~ "); CHECK(f.size==9 && f.output[8]=='R');
	string(&f,"I#X 1,12,1,128,0,0,0,0,0,0,1:I#C>2,5:A 12:");
	CHECK(b->state.bgi.fill_pixels[0]==5 && b->state.bgi.fill_pixels[63]==5);
	string(&f,"I#X 1,12,0:"); CHECK(b->state.application.patterns[0][0]==0xaa && b->state.bgi.fill_user[0]==128 && b->state.bgi.fill_pixels[63]==5);
	string(&f,"I#A 12:"); CHECK(b->state.bgi.fill_user[0]==0xaa && b->state.bgi.fill_pixels[0]==5 && b->state.bgi.fill_pixels[1]==0);
	string(&f,"I#X 1,12,1,1,2,3,4,5,6,7,8:I#I 1:I#R 1:I#I 0:");
	for (size_t i=0;i<8;++i) CHECK(b->state.application.patterns[0][i]==i+1);
	b->state.bgi.state=0; b->state.bgi.error=-11; string(&f,"I#X 1,12,0:");
	CHECK(b->state.application.patterns[0][0]==0xaa && b->state.bgi.error==-11 && b->state.bgi.fill_style==1);
	blank(&f); CHECK(f.size==9); destroy(&f);
}
static void edit_live_frame(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#X 1,12,1,1,"); CHECK(b->fill_edit.active && b->fill_edit.row==1);
	b->fill_edit.slot=13; b->fill_edit.alter=0;
	string(&f,"2,3,4,5,6,7,8:");
	CHECK(!b->fill_edit.active && memcmp(b->state.application.patterns[1],st_big_fill_restore[1],8)==0);
	CHECK(b->state.application.patterns[0][0]==1 && b->state.application.patterns[0][1]==0x55);
	string(&f,"I#X 1,12,1,9,"); b->fill_edit.slot=24;
	const char bad[]="10,TAIL"; size_t used;
	CHECK(st_cterm_write(f.terminal,bad,sizeof(bad)-1,&used)==IO_ERROR_OUT_OF_RANGE && used==3);
	CHECK(!b->fill_edit.active && b->state.application.patterns[0][0]==9 && b->state.application.patterns[0][1]==0x55 && b->state.bgi.error==0);
	string(&f,"I#X 1,12,0:"); CHECK(memcmp(b->state.application.patterns[0],st_big_fill_restore[0],8)==0);
	blank(&f); CHECK(f.size==0); destroy(&f);
}
int main(void) { partitions(); prefixes_sources_and_errors(); edit_partitions(); edit_prefixes_and_sources(); edit_live_frame(); return 0; }
