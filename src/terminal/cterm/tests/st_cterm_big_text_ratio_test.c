/* Native X3 custom text ratio/state expectations, original BIG28/53. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm.h"
#include "cterm_big_internal.h"
#include "st_cterm.h"
#include "io_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
struct fixture { st_console_t *console; st_font_catalog_t *fonts; st_cterm_t *terminal; uint8_t output[512]; size_t size; };
static uint64_t local_ns;
static int clock_error;
static int now(void *context, uint64_t *value) { (void)context; *value=local_ns; return clock_error; }
static void output(void *context, enum st_cterm_output_origin origin, const uint8_t *bytes, size_t size)
{
	struct fixture *f=context; (void)origin; CHECK(size<=sizeof(f->output)-f->size);
	memcpy(f->output+f->size,bytes,size); f->size+=size;
}
static void create(struct fixture *f, bool utf8, bool status)
{
	memset(f,0,sizeof(*f)); local_ns=1000; clock_error=0;
	const struct st_console_display_config display={.backend=ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry={.columns=80,.rows=25,.cell_width=8,.cell_height=14};
	CHECK(st_console_create(&f->console,&display,&geometry)==0 && st_font_catalog_create(&f->fonts,f->console)==0);
	const struct st_cterm_config config={.console=f->console,.fonts=f->fonts,.emulation=ST_CTERM_ANSI_BBS,
	    .graphics_mode=ST_CTERM_GRAPHICS_BIG_103,.encoding=utf8?ST_CTERM_ENCODING_UTF8:ST_CTERM_ENCODING_CODEPAGE,
	    .codepage=ST_CODEPAGE_CP437,.quiet=true,.status_line=status,.scrollback_lines=2,.output=output,.output_context=f};
	CHECK(st_cterm_create(&f->terminal,&config)==0); f->terminal->big_engine->numeric.io.now_ns=now;
}
static void destroy(struct fixture *f) { st_cterm_destroy(&f->terminal); st_font_catalog_destroy(f->fonts); st_console_destroy(f->console); }
static void feed(struct fixture *f, const void *bytes, size_t size) { size_t used; int status=st_cterm_write(f->terminal,bytes,size,&used); CHECK((status==0 || status==IO_ERROR_IN_PROGRESS) && used==size); }
static void string(struct fixture *f, const char *wire) { feed(f,wire,strlen(wire)); }
static void poll(struct fixture *f) { CHECK(st_cterm_poll(f->terminal)==0); }
static void ratios(struct fixture *f, const uint16_t r[4])
{
	const struct st_big_state *s=&f->terminal->big_engine->state;
	CHECK(memcmp(s->bgi.saved_ratio,r,8)==0 && memcmp(s->bgi.effective_ratio,r,8)==0);
}
static void partitions(void)
{
	static const char wire[]="I#X 3,2,1,3,2:\033[6n";
	for (unsigned utf8=0;utf8<2;++utf8) for (unsigned status=0;status<2;++status) {
		struct fixture f; create(&f,utf8,status); struct cterm_big *b=f.terminal->big_engine;
		for (unsigned res=1;res<=3;++res) for (size_t split=0;split<=sizeof(wire);++split) {
			if (res!=2 && split!=sizeof(wire)) continue;
			char setup[100]; snprintf(setup,sizeof(setup),"I#R %u:I#C>0,5:P>100,100:G 0,0,100,100,100,100,120,100:",res);
			string(&f,setup); b->state.bgi.error=-11; f.size=0;
			struct st_big_state expected=b->state; expected.memory_page=1; uint8_t image[10]; memcpy(image,b->image_bytes,10);
			const uint16_t r[]={2,1,3,2}; memcpy(expected.bgi.saved_ratio,r,8); memcpy(expected.bgi.effective_ratio,r,8);
			expected.bgi.text_size=expected.bgi.text_selected_size=0; expected.bgi.text_cached[0]=1; expected.bgi.text_cached[1]=3;
			if (split==sizeof(wire)) for (size_t i=0;i<sizeof(wire)-1;++i) feed(&f,wire+i,1);
			else { feed(&f,wire,split); feed(&f,wire+split,sizeof(wire)-1-split); }
			CHECK(b->last_error==0 && b->rejected_commands==0 && memcmp(&expected,&b->state,sizeof(expected))==0 && memcmp(image,b->image_bytes,10)==0);
			CHECK(f.size==6 && memcmp(f.output,"\033[1;1R",6)==0);
			uint32_t width,height; CHECK(st_console_pixel_size(f.console,&width,&height)==0);
			const struct st_console_rect area={0,0,width,height}; struct st_pixel_buffer *p=NULL;
			CHECK(st_console_get_pixels(&p,f.console,&area,false)==0);
			for (size_t i=0;i<(size_t)width*height;++i) CHECK(p->primary[i]==((i==100u*width+100u || i==100u*width+120u)?805u:800u) && p->alternate[i]==p->primary[i]);
			st_pixel_buffer_destroy(p); uint32_t rgb; CHECK(st_console_get_palette(f.console,805,&rgb,1)==0 && rgb==0xaa00aa);
		}
		destroy(&f);
	}
}
static void partials_defaults_and_sources(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	ratios(&f,(const uint16_t[]){1,1,1,1}); CHECK(b->state.bgi.text_header[0]=='+' && b->state.bgi.text_cached[0]==8 && b->state.bgi.text_selected_size==8);
	b->state.bgi.font=3; b->state.bgi.direction=1; b->state.bgi.text_size=3;
	string(&f,"I#X 3,2,1,3,"); ratios(&f,(const uint16_t[]){1,1,1,1}); CHECK(b->state.bgi.text_size==3 && b->state.bgi.text_cached[0]==8);
	CHECK(st_cterm_finish(f.terminal)==0); ratios(&f,(const uint16_t[]){1,1,1,1}); CHECK(b->state.bgi.text_size==3);
	string(&f,"I#X 3,2,1,3,"); CHECK(st_cterm_reset_terminal(f.terminal)==0 && b->state.bgi.text_size==3);
	string(&f,"I#X 3,2,1,3,"); local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS; poll(&f);
	ratios(&f,(const uint16_t[]){2,1,3,1}); CHECK(b->state.bgi.font==3 && b->state.bgi.direction==1 && b->state.bgi.text_size==0);
	string(&f,"I#I 1:"); CHECK(b->state.bgi.font==3 && b->state.bgi.text_size==0 && b->state.bgi.text_cached[1]==3);
	string(&f,"I#R 3:I#I 0:"); ratios(&f,(const uint16_t[]){2,1,3,1});
	CHECK(b->state.bgi.font==0 && b->state.bgi.direction==0 && b->state.bgi.text_size==1 && b->state.bgi.text_cached[0]==8 && b->state.bgi.text_cached[1]==8 && b->state.bgi.text_selected_size==8);
	string(&f,"I#g 3:I#X 3,4,3,2,1:"); ratios(&f,(const uint16_t[]){4,3,2,1});
	f.size=0; string(&f,"I#& 0,2,1,0,X@5,3,2,1,3,2:"); ratios(&f,(const uint16_t[]){2,1,3,2});
	CHECK(f.size==0);
	f.size=0; string(&f,"I#"); b->control.input.pending=1; b->control.input.count=1; b->control.input.payload[1]=255;
	string(&f,"X>3,0,5,+1,4:S 5,5:\033[6n"); ratios(&f,(const uint16_t[]){1,1,1,4});
	CHECK(f.size==7 && memcmp(f.output,"\xff\033[1;1R",7)==0 && b->control.input.pending==0);
	destroy(&f); create(&f,false,false); ratios(&f,(const uint16_t[]){1,1,1,1}); CHECK(f.terminal->big_engine->state.bgi.text_size==1); destroy(&f);
}
static void validation_control_and_recovery(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	b->state.bgi.error=-11; b->state.bgi.text_size=3;
	string(&f,"I#X 3,2,1,3,"); b->state.bgi.width=1; string(&f,"2:"); ratios(&f,(const uint16_t[]){1,1,1,1});
	CHECK(b->state.bgi.text_size==0 && b->state.bgi.error==-11 && b->last_error==0); b->state.bgi.width=640;
	b->state.bgi.text_control=1; string(&f,"I#X 3,2,1,3,2:"); ratios(&f,(const uint16_t[]){1,1,1,1});
	CHECK(b->state.bgi.text_cached[0]==8 && b->state.bgi.text_size==0); b->state.bgi.text_control=0;
	b->state.bgi.state=0; b->state.bgi.text_size=3;
	string(&f,"I#X 3,2,1,3,2:"); CHECK(b->state.bgi.text_size==3 && b->state.bgi.error==-1 && b->last_error==IO_ERROR_PROTOCOL && b->rejected_commands==1);
	b->state.bgi.state=3; f.size=0; string(&f,"I#X 3,2,1,3,2:\033[6n"); ratios(&f,(const uint16_t[]){2,1,3,2}); CHECK(f.size==6);
	b->state.bgi.text_size=3; string(&f,"I#X 3,4,3,2,"); clock_error=IO_ERROR_IO; poll(&f); clock_error=0;
	ratios(&f,(const uint16_t[]){2,1,3,2}); CHECK(b->state.bgi.text_size==3 && b->last_error==IO_ERROR_IO && b->rejected_commands==2);
	destroy(&f);
}
int main(void) { partitions(); partials_defaults_and_sources(); validation_control_and_recovery(); return 0; }
