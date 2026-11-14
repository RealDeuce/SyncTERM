/* Independent BIG K/J/O/Q/V/Y wire/page/source/recovery expectations, original13/61. */
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
struct fixture { st_console_t *console; st_font_catalog_t *fonts; st_cterm_t *terminal; uint8_t output[128]; enum st_cterm_output_origin origins[128]; size_t size; unsigned bells; bool reset_bell, color_bell; };
struct dot { unsigned x,y; };
static const char *stage;
static uint64_t local_ns;
static int clock_error;
static int now(void *context, uint64_t *value) { (void)context; *value=local_ns; return clock_error; }
static void bell(void *context)
{
	struct cterm_big *b=context; struct fixture *f=b->terminal->api_output_context;
	++f->bells; if (f->color_bell) CHECK(st_big_draw_color(&b->state,5)==0); if (f->reset_bell) CHECK(st_cterm_reset_terminal(f->terminal)==0);
}
static void output(void *context, enum st_cterm_output_origin origin, const uint8_t *bytes, size_t size)
{
	struct fixture *f=context; CHECK(size<=sizeof(f->output)-f->size);
	for (size_t i=0;i<size;++i) f->origins[f->size+i]=origin;
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
	CHECK(st_cterm_create(&f->terminal,&config)==0); f->terminal->big_engine->numeric.io.now_ns=now; f->terminal->big_engine->numeric.io.bell=bell;
}
static void destroy(struct fixture *f) { st_cterm_destroy(&f->terminal); st_font_catalog_destroy(f->fonts); st_console_destroy(f->console); }
static void feed(struct fixture *f, const void *bytes, size_t size)
{
	size_t used; CHECK(st_cterm_write(f->terminal,bytes,size,&used)==0 && used==size);
}
static void string(struct fixture *f, const char *bytes) { feed(f,bytes,strlen(bytes)); }
static void setup(struct fixture *f, unsigned resolution)
{
	char wire[96]; snprintf(wire,sizeof(wire),"I#R %u:I#g 0:I#C 1,5:I#m 2,31,32:",resolution); string(f,wire); f->size=0;
	f->terminal->big_engine->state.bgi.error=-11;
}
static void canvas_with_fill(struct fixture *f, const struct dot *dots, size_t count, const struct dot *fill, size_t fill_count)
{
	const struct st_console_rect area={0,0,640,f->terminal->big_engine->state.bgi.height};
	struct st_pixel_buffer *p=NULL; CHECK(st_console_get_pixels(&p,f->console,&area,false)==0);
	uint8_t *expected=calloc((size_t)area.width*area.height,1); CHECK(expected!=NULL);
	for (size_t i=0;i<count;++i) expected[dots[i].y*640+dots[i].x]=5;
	for (size_t i=0;i<fill_count;++i) expected[fill[i].y*640+fill[i].x]=6;
	for (size_t i=0;i<(size_t)area.width*area.height;++i) {
		if (p->primary[i]!=800u+expected[i] || p->alternate[i]!=800u+expected[i])
			fprintf(stderr,"%s pixel%zu,%zu actual%u/%u expected%u, error%d span%u radius%u/%u\n",stage,i%640,i/640,p->primary[i],p->alternate[i],800u+expected[i],f->terminal->big_engine->last_error,f->terminal->big_engine->state.bgi.curve_span,f->terminal->big_engine->state.bgi.curve_radius[0],f->terminal->big_engine->state.bgi.curve_radius[1]);
		CHECK(p->primary[i]==800u+expected[i] && p->alternate[i]==800u+expected[i]);
	}
	free(expected); st_pixel_buffer_destroy(p);
}
static void canvas(struct fixture *f, const struct dot *dots, size_t count) { canvas_with_fill(f,dots,count,NULL,0); }
static void owners(struct fixture *f, unsigned x, unsigned y, unsigned device_x, unsigned device_y)
{
	const struct st_big_state *s=&f->terminal->big_engine->state;
	CHECK(s->bgi.x==31 && s->bgi.y==32 && s->bgi.ddo_x==x && s->bgi.ddo_y==y && s->bgi.device_x==device_x && s->bgi.device_y==device_y);
}
static const struct dot full[]={{100,99},{100,101},{99,100},{101,100}};
static void partitions_and_profiles(void)
{
	static const char *const wires[]={"I#K 100,100,1,0,360:\033[2;3H\033[6n", "I#J 100,100,1,1,0,360:\033[2;3H\033[6n", "I#H 1:I#O 100,100,1:\033[2;3H\033[6n", "I#H 1:I#Q 100,100,1,1:\033[2;3H\033[6n", "I#H 1:I#V 100,100,1,0,0:\033[2;3H\033[6n", "I#H 1:I#Y 100,100,1,1,0,0:\033[2;3H\033[6n"};
	for (unsigned utf8=0;utf8<2;++utf8) for (unsigned status=0;status<2;++status) {
		struct fixture f; create(&f,utf8,status); struct cterm_big *b=f.terminal->big_engine;
		for (size_t c=0;c<sizeof(wires)/sizeof(wires[0]);++c) for (unsigned resolution=1;resolution<=3;++resolution) for (size_t split=0;split<=strlen(wires[c]);++split) {
			setup(&f,resolution); feed(&f,wires[c],split); string(&f,wires[c]+split);
			CHECK(f.size==6 && memcmp(f.output,"\033[2;3R",6)==0 && b->rejected_commands==0 && b->state.bgi.error==-11);
			const struct dot center[]={{100,100}};
			owners(&f,100,100,c>=4?100:0xa014,c>=4?100:3); canvas(&f,c>=4?center:full,c>=4?1:4);
			CHECK(b->state.bgi.curve_radius[0]==1 && b->state.bgi.curve_radius[1]==((c==0 || c==2 || c==4) && resolution!=3?0:1) && b->producer.mode==0 && !b->producer.batch);
		}
		for (size_t c=0;c<sizeof(wires)/sizeof(wires[0]);++c) for (unsigned resolution=1;resolution<=3;++resolution) {
			setup(&f,resolution); for (size_t i=0;i<strlen(wires[c]);++i) feed(&f,wires[c]+i,1); const struct dot center[]={{100,100}}; canvas(&f,c>=4?center:full,c>=4?1:4);
		}
		destroy(&f);
	}
}
static void late_color_and_partial_lifecycle(void)
{
	static const char *const prefixes[]={"I#K 100,100,1,0,", "I#J 100,100,1,1,0,"};
	for (size_t command=0;command<2;++command) {
		struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine; setup(&f,2);
		b->state.application.colors[1]=6;
		string(&f,prefixes[command]); CHECK(b->state.bgi.color==15 && b->state.bgi.device_color==15); owners(&f,31,32,31,32);
		b->state.application.colors[1]=5; string(&f,"1:");
		const struct dot center[]={{100,100}}; canvas(&f,center,1); owners(&f,100,100,0x0f02,4); CHECK(b->state.bgi.color==5 && f.size==0);
		setup(&f,2); string(&f,prefixes[command]); CHECK(st_cterm_finish(f.terminal)==0); canvas(&f,NULL,0); owners(&f,31,32,31,32); CHECK(b->state.bgi.color==15);
		string(&f,prefixes[command]); CHECK(st_cterm_reset_terminal(f.terminal)==0); canvas(&f,NULL,0); owners(&f,31,32,31,32);
		/* Timeout completes only the final empty field; colour remains late. */
		string(&f,prefixes[command]); local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS; CHECK(st_cterm_poll(f.terminal)==0);
		CHECK(b->state.bgi.color==5 && b->state.bgi.curve_span==0 && b->state.bgi.ddo_x==100); canvas(&f,NULL,0);
		setup(&f,2); f.reset_bell=true; string(&f,prefixes[command]); local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS;
		CHECK(st_cterm_poll(f.terminal)==0 && b->state.bgi.color==15 && !st_cterm_input_paused(f.terminal)); owners(&f,31,32,31,32); f.reset_bell=false;
		setup(&f,2); string(&f,prefixes[command]); clock_error=IO_ERROR_IO;
		CHECK(st_cterm_poll(f.terminal)==0); clock_error=0; string(&f,"\033[6n"); CHECK(b->last_error==IO_ERROR_IO && f.size==6);
		canvas(&f,NULL,0); CHECK(b->state.bgi.color==15);
		setup(&f,2); string(&f,command==0?"I#K 100,100,":"I#J 100,100,"); clock_error=IO_ERROR_IO;
		CHECK(st_cterm_poll(f.terminal)==0); clock_error=0; string(&f,"\033[6n"); CHECK(b->last_error==IO_ERROR_IO && f.size==6);
		canvas(&f,NULL,0); owners(&f,31,32,31,32); CHECK(b->state.bgi.color==15); destroy(&f);
	}
}
static void query_zero_overflow_and_aspects(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	for (unsigned resolution=1;resolution<=3;++resolution) {
		setup(&f,resolution); string(&f,"I#K 100,100,2,0,90:");
		static const struct dot quarter0[]={{101,99},{102,100}},quarter1[]={{100,99},{101,99},{102,100}},quarter2[]={{100,98},{101,98},{102,99},{102,100}};
		canvas(&f,resolution==1?quarter0:resolution==2?quarter1:quarter2,resolution+1);
		owners(&f,100,100,0xa002,6); /* right102 pre-read02h, X&7=6 */
		uint16_t old[6]; memcpy(old,b->state.bgi.arc,sizeof(old));
		string(&f,"I#K 120,120,20,+1,0:"); CHECK(memcmp(old,b->state.bgi.arc,sizeof(old))==0 && f.size==0); owners(&f,120,120,120,120);
		string(&f,"I#K 120,120,+1,0,360:"); CHECK(b->state.bgi.curve_radius[0]==65535 && b->state.bgi.error==-11 && !b->producer.batch); owners(&f,120,120,120,120);
	}
	setup(&f,2); b->state.bgi.aspect[1]=0; string(&f,"I#K 100,100,1,0,1:\033[2;3H\033[6n");
	CHECK(b->last_error==IO_ERROR_OUT_OF_RANGE && b->rejected_commands==1 && b->state.bgi.color==5 && b->state.bgi.error==-11 && f.size==6);
	owners(&f,31,32,31,32); canvas(&f,NULL,0);
	b->state.bgi.aspect[1]=10000; string(&f,"I#K 100,100,1,0,1:"); const struct dot center[]={{100,100}}; canvas(&f,center,1);
	destroy(&f);
}
static void buffered_pages_source_and_storage(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	setup(&f,2); string(&f,"I#T 2,4,0,1,1:I#M 2:I#K 100,100,1,0,3:");
	const struct dot thick[]={{100,99},{101,99},{100,100},{101,100},{100,101},{101,101}};
	canvas(&f,thick,6); owners(&f,100,100,101,100);
	CHECK(b->producer.count==5 && b->producer.capacity==682 && b->producer.storage_count==1023 && b->state.bgi.line_mask==0x8000 && b->state.bgi.ddo_line_user==0);
	struct st_big_producer_point *storage=b->producer.points; struct st_big_producer_point saved[5]; memcpy(saved,storage,sizeof(saved));
	string(&f,"I#R 1:I#I 1:"); CHECK(b->producer.points==storage && memcmp(saved,storage,sizeof(saved))==0);
	CHECK(st_cterm_finish(f.terminal)==0 && st_cterm_reset_terminal(f.terminal)==0 && b->producer.points==storage && memcmp(saved,storage,sizeof(saved))==0);
	for (unsigned resolution=1;resolution<=3;++resolution) {
		setup(&f,resolution); string(&f,"I#G>6,1:K 100,100,1,0,360:");
		if (resolution<3) {
			canvas(&f,NULL,0); owners(&f,100,100,resolution==1?0xa414:0xa814,3);
			string(&f,"I#G 5,1:"); canvas(&f,full,4);
		} else { canvas(&f,full,4); owners(&f,100,100,0xa014,3); }
	}
	setup(&f,2); string(&f,"I#g 2:I#& 0,1,1,0,K@5,100,100,1,0,x:I#g 0:");
	const struct dot center[]={{100,100}}; canvas(&f,center,1); owners(&f,100,100,0x0f02,4);
	CHECK(b->numeric.loop.active==0 && f.size==0 && b->numeric.context.scaling==0);
	/* Capacity rejection is a returning BGI error, not a session error. */
	setup(&f,2); b->producer.capacity=5; string(&f,"I#T 2,4,0,1,1:I#K 100,100,1,0,3:\033[6n");
	CHECK(b->state.bgi.error==-6 && b->producer.mode==0 && f.size==6 && b->rejected_commands==0); canvas(&f,NULL,0);
	destroy(&f);
}
static int blink_seed(void *context, const struct st_pixel_buffer *p, struct st_console_rect *changed)
{
	(void)context;
	for (size_t i=0;i<4;++i) { size_t n=(size_t)full[i].y*p->width+full[i].x; p->primary[n]=803; p->alternate[n]=806; }
	*changed=(struct st_console_rect){99,99,3,3}; return 0;
}
static void banks_errors_and_footer(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine; setup(&f,2);
	CHECK(st_console_modify_pixels(f.console,blink_seed,NULL)==0);
	string(&f,"I#M 2:I#K 100,100,1,0,360:"); canvas(&f,full,4); owners(&f,100,100,0xa014,3);
	setup(&f,2); string(&f,"I#K 100,100,1,0,1:I#D 102,100:");
	const struct dot following[]={{100,100},{101,100},{102,100}}; canvas(&f,following,3);
	CHECK(b->state.bgi.x==102 && b->state.bgi.y==100 && b->state.bgi.ddo_x==102 && b->state.bgi.ddo_y==100 && b->state.bgi.device_x==102 && b->state.bgi.device_y==100);
	setup(&f,2); b->producer.read_plane=4;
	string(&f,"I#K 100,100,1,0,360:\033[6n");
	CHECK(b->last_error==IO_ERROR_OUT_OF_RANGE && b->rejected_commands==1 && f.size==6 && b->producer.mode==2 && b->producer.batch && b->state.bgi.error==-11);
	owners(&f,100,100,100,100); canvas(&f,NULL,0);
	b->producer.read_plane=0; string(&f,"I#K 100,100,1,0,360:"); canvas(&f,full,4); CHECK(!b->producer.batch);
	setup(&f,2); string(&f,"I#K +1,100,1,120,150:"); canvas(&f,NULL,0); owners(&f,65535,100,65534,100);
	setup(&f,2); b->control.input.pending=1; b->control.input.count=2; b->control.input.payload[1]=0; b->control.input.payload[2]=255;
	string(&f,"I#K>100,100,1,0,1:"); CHECK(f.size==0 && b->control.input.pending==1);
	string(&f,"~ "); CHECK(f.size==2 && f.output[0]==0 && f.output[1]==255 && f.origins[0]==ST_CTERM_OUTPUT_USER && f.origins[1]==ST_CTERM_OUTPUT_USER);
	destroy(&f);
}
static void shared_scratch_and_scaling(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine; setup(&f,2);
	string(&f,"I#T 2,4,0,1,1:I#M 2:I#K 100,100,1,0,3:");
	struct st_big_producer_point saved[5]; memcpy(saved,b->producer.points,sizeof(saved));
	b->state.bgi.viewport[0]=200; b->state.bgi.viewport[1]=100; b->state.bgi.viewport[2]=204; b->state.bgi.viewport[3]=104;
	string(&f,"I#C 2,5:I#F 1,1,1:"); CHECK(memcmp(saved,b->producer.points,sizeof(saved))!=0 && b->flood_work.count==0);
	b->state.bgi.viewport[0]=b->state.bgi.viewport[1]=0; b->state.bgi.viewport[2]=639; b->state.bgi.viewport[3]=349;
	string(&f,"I#K 100,100,1,0,3:");
	struct dot block[25]; size_t count=0;
	for (unsigned y=100;y<=104;++y) for (unsigned x=200;x<=204;++x) block[count++]=(struct dot){x,y};
	canvas(&f,block,25); owners(&f,100,100,101,100);
	setup(&f,2); string(&f,"I#g 2:I#K 100,100,2,0,90:");
	const struct dot scaled[]={{100,174},{101,174},{102,175}}; canvas(&f,scaled,3); owners(&f,100,175,0xa002,6);
	CHECK(b->state.bgi.curve_radius[0]==2 && b->state.bgi.curve_radius[1]==1);
	destroy(&f);
}
static void explicit_radius_J(void)
{
	static const struct dot tall[]={{100,98},{100,102},{99,99},{101,99},{99,101},{101,101},{99,100},{101,100}};
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	for (unsigned mode=1;mode<=3;++mode) {
		setup(&f,mode); string(&f,"I#J 100,100,1,2,0,360:"); canvas(&f,tall,8); owners(&f,100,100,0xa014,3);
		CHECK(b->state.bgi.curve_radius[0]==1 && b->state.bgi.curve_radius[1]==2);
	}
	setup(&f,2); string(&f,"I#J 100,100,0,1,0,90:"); const struct dot top[]={{100,99}};
	canvas(&f,top,1); owners(&f,100,100,0xa008,4); CHECK(b->state.bgi.curve_radius[0]==0 && b->state.bgi.curve_radius[1]==1);
	setup(&f,2); string(&f,"I#J 100,100,1,1,360,720:"); canvas(&f,full,4);
	setup(&f,2); string(&f,"I#J 100,100,1,1,120,150:"); const struct dot left[]={{99,100}};
	canvas(&f,left,1); owners(&f,100,100,0x0f02,3);
	/* Explicit radii do not enter circular aspect division. */
	setup(&f,2); b->state.bgi.aspect[0]=b->state.bgi.aspect[1]=0;
	string(&f,"I#J 100,100,1,1,0,1:"); const struct dot center[]={{100,100}}; canvas(&f,center,1); CHECK(b->rejected_commands==0);
	uint16_t old[6]; memcpy(old,b->state.bgi.arc,sizeof(old)); string(&f,"I#J 120,120,9,8,+1,0:");
	CHECK(memcmp(old,b->state.bgi.arc,sizeof(old))==0 && f.size==0); owners(&f,120,120,120,120); canvas(&f,center,1);
	setup(&f,2); string(&f,"I#g 2:I#J 100,100,2,2,0,90:");
	const struct dot scaled[]={{100,172},{101,172},{102,173},{102,174},{102,175}};
	canvas(&f,scaled,5); owners(&f,100,175,0xa002,6); CHECK(b->state.bgi.curve_radius[0]==2 && b->state.bgi.curve_radius[1]==3);
	setup(&f,3); string(&f,"I#g 3:I#J 100,5,2,5,10,10:"); canvas(&f,NULL,0); owners(&f,100,11,100,11);
	CHECK(b->state.bgi.curve_radius[0]==2 && b->state.bgi.curve_radius[1]==11);
	setup(&f,3); string(&f,"I#g 3:I#& 0,0,1,0,J@6,100,5,2,5,0,0:I#g 0:"); canvas(&f,NULL,0); owners(&f,100,5,100,5);
	CHECK(b->state.bgi.curve_radius[0]==2 && b->state.bgi.curve_radius[1]==5 && b->numeric.loop.active==0);
	setup(&f,2); string(&f,"I#J 100,100,+1,2,0,360:"); canvas(&f,NULL,0); owners(&f,100,100,100,100);
	CHECK(b->state.bgi.curve_radius[0]==65535 && b->state.bgi.curve_radius[1]==2 && b->state.bgi.error==-11 && !b->producer.batch);
	setup(&f,2); string(&f,"I#T 2,4,0,1,1:I#M 2:I#J 100,100,1,1,0,3:");
	const struct dot thick[]={{100,99},{101,99},{100,100},{101,100},{100,101},{101,101}};
	canvas(&f,thick,6); owners(&f,100,100,101,100); CHECK(b->producer.count==5 && b->state.bgi.line_mask==0x8000);
	setup(&f,2); b->producer.read_plane=4; string(&f,"I#J 100,100,1,1,0,360:\033[6n");
	CHECK(b->last_error==IO_ERROR_OUT_OF_RANGE && b->rejected_commands==1 && f.size==6 && b->producer.batch); canvas(&f,NULL,0);
	b->producer.read_plane=0; string(&f,"I#J 100,100,1,1,0,360:"); canvas(&f,full,4);
	for (unsigned mode=1;mode<=3;++mode) {
		setup(&f,mode); string(&f,"I#G>6,1:J 100,100,1,2,0,360:");
		if (mode<3) {
			canvas(&f,NULL,0); owners(&f,100,100,mode==1?0xa414:0xa814,3);
			string(&f,"I#G 5,1:");
		} else owners(&f,100,100,0xa014,3);
		canvas(&f,tall,8);
	}
	setup(&f,2); string(&f,"I#J 100,100,1,1,0,1:I#D 102,100:");
	const struct dot following[]={{100,100},{101,100},{102,100}}; canvas(&f,following,3);
	CHECK(b->state.bgi.x==102 && b->state.bgi.y==100 && b->state.bgi.ddo_x==102 && b->state.bgi.ddo_y==100 && b->state.bgi.device_x==102 && b->state.bgi.device_y==100);
	setup(&f,2); b->control.input.pending=1; b->control.input.count=2; b->control.input.payload[1]=0; b->control.input.payload[2]=255;
	string(&f,"I#J>100,100,1,1,0,1:"); CHECK(f.size==0 && b->control.input.pending==1);
	string(&f,"~ "); CHECK(f.size==2 && f.output[0]==0 && f.output[1]==255 && f.origins[0]==ST_CTERM_OUTPUT_USER && f.origins[1]==ST_CTERM_OUTPUT_USER);
	destroy(&f);
}

static void circle_O(void)
{
	static const struct dot center[]={{100,100}};
	static const struct dot flat[]={{100,99},{100,101},{99,99},{101,99},{99,101},{101,101},{98,100},{102,100}};
	static const struct dot round[]={{100,98},{100,102},{99,98},{101,98},{99,102},{101,102},{98,99},{102,99},{98,101},{102,101},{98,100},{102,100}};
	static const struct dot inside[]={{99,100},{100,100},{101,100},{99,99},{100,99},{101,99},{99,101},{100,101},{101,101}};
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	for (unsigned mode=1;mode<=3;++mode) {
		setup(&f,mode); string(&f,"I#H 0:I#C 2,6:I#O 100,100,1:"); canvas_with_fill(&f,full,4,center,1); owners(&f,100,100,0xa014,3);
		CHECK(b->state.bgi.fill_style==1 && b->state.bgi.fill_color==6 && b->state.bgi.device_fill_color==6 && b->flood_work.count==0 && f.size==0);
		setup(&f,mode); string(&f,"I#O 100,100,2:");
		canvas_with_fill(&f,mode==3?round:flat,mode==3?12:8,inside,mode==3?9:3); owners(&f,100,100,0xa022,2);
		CHECK(b->state.bgi.curve_radius[0]==2 && b->state.bgi.curve_radius[1]==(mode==1?0:mode==2?1:2));
		setup(&f,mode); string(&f,"I#H 1:I#O 100,100,1:"); canvas(&f,full,4); owners(&f,100,100,0xa014,3);
		CHECK(b->state.bgi.fill_color==6 && b->state.bgi.device_fill_color==6);
	}
	/* Even hollow O submits fill, after the last operand completes. */
	setup(&f,2); b->state.application.colors[1]=7; b->state.application.colors[2]=3;
	string(&f,"I#O 100,100,"); CHECK(b->state.bgi.color==15 && b->state.bgi.fill_color==15); canvas(&f,NULL,0);
	b->state.application.colors[1]=5; b->state.application.colors[2]=6; string(&f,"1:");
	CHECK(b->state.bgi.color==5 && b->state.bgi.fill_color==6); canvas(&f,full,4);
	setup(&f,2); string(&f,"I#O 100,100,"); CHECK(st_cterm_finish(f.terminal)==0); canvas(&f,NULL,0); CHECK(b->state.bgi.fill_color==15 && b->state.bgi.color==15);
	string(&f,"I#O 100,100,"); CHECK(st_cterm_reset_terminal(f.terminal)==0); canvas(&f,NULL,0); owners(&f,31,32,31,32);
	string(&f,"I#O 100,100,"); local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS; CHECK(st_cterm_poll(f.terminal)==0);
	CHECK(b->state.bgi.curve_radius[0]==0 && b->state.bgi.curve_radius[1]==0 && b->state.bgi.fill_color==6); canvas(&f,full,4);
	setup(&f,2); string(&f,"I#O 100,100,"); f.reset_bell=true; local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS;
	CHECK(st_cterm_poll(f.terminal)==0 && b->state.bgi.color==15 && b->state.bgi.fill_color==15); f.reset_bell=false; canvas(&f,NULL,0);
	setup(&f,2); string(&f,"I#O 100,100,"); clock_error=IO_ERROR_IO; CHECK(st_cterm_poll(f.terminal)==0); clock_error=0;
	CHECK(b->last_error==IO_ERROR_IO && b->state.bgi.color==15 && b->state.bgi.fill_color==15); canvas(&f,NULL,0);
	setup(&f,2); size_t rejected=b->rejected_commands; b->state.bgi.aspect[1]=0; string(&f,"I#H 0:I#O 100,100,1:\033[6n");
	CHECK(b->last_error==IO_ERROR_OUT_OF_RANGE && b->rejected_commands==rejected+1 && b->state.bgi.color==5 && b->state.bgi.fill_color==6 && f.size==6);
	owners(&f,31,32,31,32); canvas(&f,NULL,0); rejected=b->rejected_commands;
	/* An empty normal overflow still floods the existing clipped region. */
	setup(&f,2); b->state.bgi.viewport[0]=10; b->state.bgi.viewport[1]=10; b->state.bgi.viewport[2]=14; b->state.bgi.viewport[3]=14;
	string(&f,"I#O 2,2,+1:"); struct dot block[25]; size_t count=0;
	for (unsigned y=10;y<=14;++y) for (unsigned x=10;x<=14;++x) block[count++]=(struct dot){x,y};
	canvas_with_fill(&f,NULL,0,block,25); owners(&f,12,12,12,12);
	CHECK(b->state.bgi.curve_radius[0]==65535 && b->state.bgi.error==-11 && b->rejected_commands==rejected && !b->producer.batch);
	setup(&f,2); string(&f,"I#C 0,5:I#P 100,100:I#m 2,31,32:I#O 100,100,1:");
	const struct dot boundary[]={{100,99},{100,101},{99,100},{101,100},{100,100}}; canvas(&f,boundary,5); owners(&f,100,100,0xa01c,3);
	/* Cached XOR cannot change thin outline COPY or flood COPY. */
	setup(&f,2); string(&f,"I#M 2:I#O 100,100,1:"); canvas_with_fill(&f,full,4,center,1); owners(&f,100,100,0xa014,3);
	setup(&f,3); memset(b->state.application.patterns[0],0xaa,8); string(&f,"I#M 1:I#A 12:I#O 100,100,2:");
	static const struct dot stripe[]={{100,99},{100,100},{100,101}};
	canvas_with_fill(&f,round,12,stripe,3); CHECK(b->state.bgi.fill_style==12 && b->state.bgi.fill_color==6);
	/* Source values bypass Y scaling; ordinary circle radius stays class0. */
	setup(&f,2); string(&f,"I#H 1:I#g 2:I#O 100,100,2:"); owners(&f,100,175,0xa022,2);
	static const struct dot scaled[]={{100,174},{100,176},{99,174},{101,174},{99,176},{101,176},{98,175},{102,175}}; canvas(&f,scaled,8);
	setup(&f,2); string(&f,"I#g 2:I#& 0,0,1,0,O@3,100,100,2:I#g 0:"); canvas(&f,flat,8); owners(&f,100,100,0xa022,2);
	CHECK(b->numeric.loop.active==0 && f.size==0);
	for (unsigned mode=1;mode<=3;++mode) {
		setup(&f,mode); string(&f,"I#H 0:I#G>6,1:O 100,100,1:");
		if (mode<3) { canvas(&f,NULL,0); owners(&f,100,100,mode==1?0xa414:0xa814,3); string(&f,"I#G 5,1:"); }
		else owners(&f,100,100,0xa014,3);
		canvas_with_fill(&f,full,4,center,1);
	}
	setup(&f,2); b->producer.read_plane=4; string(&f,"I#O 100,100,1:\033[6n");
	CHECK(b->last_error==IO_ERROR_OUT_OF_RANGE && b->rejected_commands==rejected+1 && b->state.bgi.fill_color==6 && f.size==6); canvas(&f,NULL,0);
	b->producer.read_plane=0; string(&f,"I#O 100,100,1:"); canvas_with_fill(&f,full,4,center,1);
	/* The full angular XOR contour follows the hand-derived1098-write
	 * trace. Its center is boundary5, so O's flood returns at the seed. */
	setup(&f,3); string(&f,"I#A 1:I#T 2,4,0,1,1:I#M 2:I#O 100,100,1:");
	static const struct dot thick[]={{100,98},{99,99},{100,99},{101,99},{98,100},{99,100},{100,100},{101,100},{98,101},{100,101},{99,102},{100,102}};
	canvas(&f,thick,12); owners(&f,100,100,0x8000,0x8000); CHECK(b->producer.count==363 && b->state.bgi.line_mask==0x8000);
	struct st_big_producer_point saved[5]; memcpy(saved,b->producer.points,sizeof(saved));
	string(&f,"I#T 2,0,0,1,0:I#M 1:I#O 150,150,2:");
	struct dot outlines[24], filling[9]; memcpy(outlines,thick,sizeof(thick));
	for (size_t i=0;i<12;++i) outlines[12+i]=(struct dot){round[i].x+50,round[i].y+50};
	for (size_t i=0;i<9;++i) filling[i]=(struct dot){inside[i].x+50,inside[i].y+50};
	canvas_with_fill(&f,outlines,24,filling,9); owners(&f,150,150,0xa008,4);
	CHECK(memcmp(saved,b->producer.points,sizeof(saved))!=0 && b->producer.count==363 && b->flood_work.count==0);
	/* Returning custom-fill rejection retains the installed tile and
	 * still permits the outline and flood with that prior fill. */
	setup(&f,2); string(&f,"I#C 2,6:I#A 1:I#C 2,31:I#A 12:I#O 100,100,1:");
	canvas_with_fill(&f,full,4,center,1); CHECK(b->state.bgi.fill_style==1 && b->state.bgi.fill_color==6 && b->state.bgi.error==-11);
	b->control.input.pending=1; b->control.input.count=2; b->control.input.payload[1]=0; b->control.input.payload[2]=255;
	string(&f,"I#O>100,100,1:"); CHECK(f.size==0 && b->control.input.pending==1);
	string(&f,"~ "); CHECK(f.size==2 && f.output[0]==0 && f.output[1]==255 && f.origins[0]==ST_CTERM_OUTPUT_USER && f.origins[1]==ST_CTERM_OUTPUT_USER);
	setup(&f,2); string(&f,"I#C 2,6:I#A 1:I#O 100,100,1:I#D 102,100:");
	static const struct dot following[]={{100,99},{100,101},{99,100},{100,100},{101,100},{102,100}}; canvas(&f,following,6);
	CHECK(b->state.bgi.x==102 && b->state.bgi.y==100 && b->state.bgi.ddo_x==102 && b->state.bgi.ddo_y==100 && b->state.bgi.device_x==102 && b->state.bgi.device_y==100);
	destroy(&f);
}


static void ellipse_Q(void)
{
	static const struct dot center[]={{100,100}};
	static const struct dot tall[]={{100,98},{100,102},{99,99},{101,99},{99,101},{101,101},{99,100},{101,100}};
	static const struct dot vertical[]={{100,99},{100,100},{100,101}};
	static const struct dot round[]={{100,98},{100,102},{99,98},{101,98},{99,102},{101,102},{98,99},{102,99},{98,101},{102,101},{98,100},{102,100}};
	static const struct dot inside[]={{99,100},{100,100},{101,100},{99,99},{100,99},{101,99},{99,101},{100,101},{101,101}};
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	for (unsigned mode=1;mode<=3;++mode) {
		setup(&f,mode); string(&f,"I#H 0:I#C 2,6:I#Q 100,100,1,2:");
		canvas_with_fill(&f,tall,8,vertical,3); owners(&f,100,100,0xa014,3);
		CHECK(b->state.bgi.curve_radius[0]==1 && b->state.bgi.curve_radius[1]==2 && b->state.bgi.fill_color==6 && f.size==0);
		setup(&f,mode); string(&f,"I#H 1:I#Q 100,100,1,2:"); canvas(&f,tall,8);
		CHECK(b->state.bgi.fill_style==1 && b->state.bgi.fill_color==15 && b->state.bgi.color==5);
	}
	/* Early colour is visible before the first read, never refreshed
	 * from the application line colour after the operands return. */
	setup(&f,2); string(&f,"I#Q "); CHECK(b->state.bgi.color==5 && b->state.bgi.fill_color==15); owners(&f,31,32,31,32);
	b->state.application.colors[1]=7; string(&f,"100,100,1,1:"); canvas(&f,full,4); CHECK(b->state.bgi.color==5 && b->state.bgi.fill_color==15);
	b->state.application.colors[1]=5;
	setup(&f,2); string(&f,"I#Q 100,100,1,"); CHECK(st_cterm_finish(f.terminal)==0); canvas(&f,NULL,0); CHECK(b->state.bgi.color==5 && b->state.bgi.fill_color==15);
	string(&f,"I#Q 100,100,1,"); CHECK(st_cterm_reset_terminal(f.terminal)==0); canvas(&f,NULL,0); owners(&f,31,32,31,32);
	string(&f,"I#Q 100,100,1,"); local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS; CHECK(st_cterm_poll(f.terminal)==0);
	canvas(&f,full,4); CHECK(b->state.bgi.curve_radius[1]==0 && b->state.bgi.fill_color==15);
	setup(&f,2); string(&f,"I#Q 100,100,1,"); f.reset_bell=true; local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS;
	CHECK(st_cterm_poll(f.terminal)==0 && b->state.bgi.color==5 && b->state.bgi.fill_color==15); f.reset_bell=false; canvas(&f,NULL,0);
	setup(&f,2); b->state.application.colors[1]=6; string(&f,"I#Q 100,100,1,"); CHECK(b->state.bgi.color==6);
	f.color_bell=true; local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS; CHECK(st_cterm_poll(f.terminal)==0); f.color_bell=false;
	canvas(&f,full,4); CHECK(b->state.bgi.color==5 && b->state.application.colors[1]==6); b->state.application.colors[1]=5;
	setup(&f,2); string(&f,"I#Q 100,100,1,"); clock_error=IO_ERROR_IO; CHECK(st_cterm_poll(f.terminal)==0); clock_error=0;
	CHECK(b->last_error==IO_ERROR_IO && b->state.bgi.color==5 && b->state.bgi.fill_color==15); canvas(&f,NULL,0);
	/* Explicit radii never enter the circular aspect divider. */
	setup(&f,2); b->state.bgi.aspect[0]=b->state.bgi.aspect[1]=0; string(&f,"I#H 0:I#Q 100,100,1,2:"); canvas_with_fill(&f,tall,8,vertical,3);
	setup(&f,2); b->state.bgi.viewport[0]=10; b->state.bgi.viewport[1]=10; b->state.bgi.viewport[2]=14; b->state.bgi.viewport[3]=14;
	string(&f,"I#Q 2,2,+1,2:"); struct dot block[25]; size_t count=0;
	for (unsigned y=10;y<=14;++y) for (unsigned x=10;x<=14;++x) block[count++]=(struct dot){x,y};
	canvas_with_fill(&f,NULL,0,block,25); owners(&f,12,12,12,12); CHECK(b->state.bgi.error==-11 && b->state.bgi.fill_color==6);
	/* Producer-6 is a normal BGI return, so Q still submits fill/flood. */
	setup(&f,3); b->state.bgi.viewport[0]=10; b->state.bgi.viewport[1]=10; b->state.bgi.viewport[2]=14; b->state.bgi.viewport[3]=14;
	size_t capacity=b->producer.capacity; b->producer.capacity=0;
	string(&f,"I#T 2,4,0,1,1:I#Q 2,2,1,1:");
	canvas_with_fill(&f,NULL,0,block,25); owners(&f,12,12,12,12);
	CHECK(b->state.bgi.error==-6 && b->state.bgi.fill_color==6 && b->producer.count==0 && b->flood_work.count==0); b->producer.capacity=capacity;
	setup(&f,2); size_t rejected=b->rejected_commands; b->producer.read_plane=4; string(&f,"I#Q 100,100,1,1:\033[6n");
	CHECK(b->rejected_commands==rejected+1 && b->last_error==IO_ERROR_OUT_OF_RANGE && b->state.bgi.color==5 && b->state.bgi.fill_color==15 && f.size==6); canvas(&f,NULL,0);
	b->producer.read_plane=0; string(&f,"I#Q 100,100,1,1:"); canvas_with_fill(&f,full,4,center,1); CHECK(b->state.bgi.fill_color==6);
	setup(&f,2); string(&f,"I#C 2,6:I#A 1:I#C 2,31:I#A 12:I#Q 100,100,1,1:");
	canvas_with_fill(&f,full,4,center,1); CHECK(b->state.bgi.fill_style==1 && b->state.bgi.fill_color==6 && b->state.bgi.error==-11);
	setup(&f,2); string(&f,"I#C 2,6:I#A 1:I#H 1:I#g 2:I#Q 100,100,2,2:");
	static const struct dot scaled[]={{100,172},{100,178},{99,172},{101,172},{99,178},{101,178},{98,173},{102,173},{98,177},{102,177},{98,174},{102,174},{98,176},{102,176},{98,175},{102,175}};
	canvas(&f,scaled,16); owners(&f,100,175,0xa022,2); CHECK(b->state.bgi.curve_radius[0]==2 && b->state.bgi.curve_radius[1]==3);
	setup(&f,2); string(&f,"I#g 2:I#& 0,0,1,0,Q@4,100,100,2,2:I#g 0:"); canvas(&f,round,12); owners(&f,100,100,0xa022,2); CHECK(b->state.bgi.curve_radius[1]==2 && b->numeric.loop.active==0);
	/* Literal6554 overflows the outline without becoming a stored-loop
	 * expression, keeping the g3 center/RY bypass test independent. */
	setup(&f,3); string(&f,"I#g 3:I#Q 100,5,6554,5:"); canvas(&f,NULL,0); owners(&f,100,11,100,11); CHECK(b->state.bgi.curve_radius[1]==11);
	setup(&f,3); string(&f,"I#g 3:I#& 0,0,1,0,Q@4,100,5,6554,5:I#g 0:"); canvas(&f,NULL,0); owners(&f,100,5,100,5); CHECK(b->state.bgi.curve_radius[1]==5);
	for (unsigned mode=1;mode<=3;++mode) {
		setup(&f,mode); string(&f,"I#H 0:I#G>6,1:Q 100,100,1,1:");
		if (mode<3) { canvas(&f,NULL,0); owners(&f,100,100,mode==1?0xa414:0xa814,3); string(&f,"I#G 5,1:"); }
		else owners(&f,100,100,0xa014,3);
		canvas_with_fill(&f,full,4,center,1);
	}
	setup(&f,3); string(&f,"I#T 2,4,0,1,1:I#M 2:I#Q 100,100,1,1:");
	static const struct dot thick[]={{100,98},{99,99},{100,99},{101,99},{98,100},{99,100},{100,100},{101,100},{98,101},{100,101},{99,102},{100,102}};
	canvas(&f,thick,12); owners(&f,100,100,0x8000,0x8000); CHECK(b->producer.count==363 && b->state.bgi.fill_color==6);
	struct st_big_producer_point saved[5]; memcpy(saved,b->producer.points,sizeof(saved));
	string(&f,"I#T 2,0,0,1,0:I#M 1:I#Q 150,150,2,2:");
	struct dot outlines[24], filling[9]; memcpy(outlines,thick,sizeof(thick));
	for (size_t i=0;i<12;++i) outlines[12+i]=(struct dot){round[i].x+50,round[i].y+50};
	for (size_t i=0;i<9;++i) filling[i]=(struct dot){inside[i].x+50,inside[i].y+50};
	canvas_with_fill(&f,outlines,24,filling,9); CHECK(memcmp(saved,b->producer.points,sizeof(saved))!=0 && b->producer.count==363 && b->flood_work.count==0);
	setup(&f,2); b->control.input.pending=1; b->control.input.count=2; b->control.input.payload[1]=0; b->control.input.payload[2]=255;
	string(&f,"I#Q>100,100,1,1:"); CHECK(f.size==0 && b->control.input.pending==1); string(&f,"~ ");
	CHECK(f.size==2 && f.output[0]==0 && f.output[1]==255 && f.origins[0]==ST_CTERM_OUTPUT_USER && f.origins[1]==ST_CTERM_OUTPUT_USER);
	setup(&f,2); string(&f,"I#Q 100,100,1,1:I#D 102,100:");
	static const struct dot following[]={{100,99},{100,101},{99,100},{100,100},{101,100},{102,100}}; canvas(&f,following,6);
	CHECK(b->state.bgi.x==102 && b->state.bgi.y==100 && b->state.bgi.ddo_x==102 && b->state.bgi.ddo_y==100 && b->state.bgi.device_x==102 && b->state.bgi.device_y==100);
	destroy(&f);
}


static void pieslice_V(void)
{
	static const struct dot center[]={{100,100}},quarter[]={{100,99},{100,100},{101,100}};
	static const struct dot outer[]={{100,98},{101,98},{102,99},{102,100},{101,100},{100,100},{100,99}},inside[]={{101,99}};
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	stage="V unit quarter"; setup(&f,3); string(&f,"I#H 0:I#C 2,6:I#V 100,100,1,0,90:"); canvas(&f,quarter,3); owners(&f,100,100,100,99);
	CHECK(b->state.bgi.fill_style==1 && b->state.bgi.fill_color==6 && b->state.bgi.device_fill_color==6 && b->producer.count==3 && !b->producer.batch && f.size==0);
	stage="V filled quarter"; setup(&f,3); string(&f,"I#V 100,100,2,0,90:"); canvas_with_fill(&f,outer,7,inside,1); owners(&f,100,100,100,98);
	CHECK(b->state.bgi.arc[2]==102 && b->state.bgi.arc[3]==100 && b->state.bgi.arc[4]==100 && b->state.bgi.arc[5]==98);
	stage="V alternate fill after outline"; setup(&f,3); b->state.bgi.curve_fallback=1;
	uint16_t old_arc[6]; memcpy(old_arc,b->state.bgi.arc,sizeof(old_arc)); string(&f,"I#H 0:I#V 100,100,1,0,360:");
	static const struct dot alternate_line[]={{99,101},{100,101}},alternate_fill[]={{100,99},{99,100},{100,100},{101,100}};
	canvas_with_fill(&f,alternate_line,2,alternate_fill,4); owners(&f,100,100,100,100);
	CHECK(b->producer.count==365 && memcmp(b->state.bgi.arc+2,old_arc+2,4*sizeof(*old_arc))==0 && b->state.bgi.fill_color==6); b->state.bgi.curve_fallback=0;
	stage="V normal custom width"; setup(&f,3); string(&f,"I#T 2,4,0,1,1:I#V 100,100,1,0,90:");
	static const struct dot top_fill[]={{100,99}}; canvas_with_fill(&f,NULL,0,top_fill,1); owners(&f,100,100,100,99);
	CHECK(b->state.bgi.line_mask==0 && b->state.bgi.device_line_width==3 && b->state.bgi.ddo_line_width==3);
	stage="V opaque pattern hole"; setup(&f,3); memset(b->state.application.patterns[0],0xaa,8);
	string(&f,"I#C 0,5:I#P 101,99:I#m 2,31,32:I#A 12:I#V 100,100,2,0,90:"); canvas(&f,outer,7);
	CHECK(b->state.bgi.fill_style==12 && b->state.bgi.fill_color==6 && b->state.bgi.fill_user[0]==0xaa);
	string(&f,"I#A 1:");
	stage="V hollow word"; setup(&f,3); string(&f,"I#H 9999:I#V 100,100,2,0,90:"); canvas(&f,outer,7); CHECK(b->state.application.hollow==9999 && b->state.bgi.fill_style==1 && b->state.bgi.fill_color==6);
	/* Both custom submissions can reject; hollow's real empty style then
	 * remains installed on normal return instead of restoring a snapshot. */
	stage="V rejected hollow fill"; setup(&f,3); string(&f,"I#C 2,6:I#A 1:I#C 2,31:I#A 12:I#V 100,100,2,0,90:");
	canvas(&f,outer,7); CHECK(b->state.application.fill==12 && b->state.bgi.fill_style==0 && b->state.bgi.fill_color==0 && b->state.bgi.error==-11);
	stage="V rejected fill"; setup(&f,3); string(&f,"I#H 0:I#C 2,6:I#A 1:I#C 2,31:I#A 12:I#V 100,100,2,0,90:");
	canvas_with_fill(&f,outer,7,inside,1); CHECK(b->state.bgi.fill_style==1 && b->state.bgi.fill_color==6);
	stage="V ffff angle"; setup(&f,3); string(&f,"I#C 2,6:I#A 1:I#V 100,100,1,+1,0:");
	static const struct dot short_arc[]={{100,100},{101,100}}; canvas(&f,short_arc,2); CHECK(b->state.bgi.curve_span==15 && b->state.bgi.arc[2]==101);
	for (unsigned mode=1;mode<=3;++mode) {
		stage="V mode aspects"; setup(&f,mode); string(&f,"I#V 100,100,20,0,0:"); canvas(&f,center,1); owners(&f,100,100,100,100);
		CHECK(b->state.bgi.curve_radius[0]==20 && b->state.bgi.curve_radius[1]==(mode==1?9:mode==2?15:20));
		stage="V double XOR radial"; setup(&f,mode); string(&f,"I#M 2:I#V 100,100,20,0,0:"); canvas(&f,NULL,0); owners(&f,100,100,100,100);
	}
	stage="V partial"; setup(&f,2); string(&f,"I#H 1:I#V 100,100,2,0,");
	CHECK(b->state.bgi.color==15 && b->state.bgi.fill_color==15); canvas(&f,NULL,0); owners(&f,31,32,31,32);
	b->state.application.colors[1]=5; b->state.application.colors[2]=6; string(&f,"0:"); canvas(&f,center,1); CHECK(b->state.bgi.color==5 && b->state.bgi.fill_color==6);
	stage="V EOF/reset/timeout"; setup(&f,2); string(&f,"I#V 100,100,2,0,"); CHECK(st_cterm_finish(f.terminal)==0); canvas(&f,NULL,0); CHECK(b->state.bgi.fill_color==15);
	string(&f,"I#V 100,100,2,0,"); CHECK(st_cterm_reset_terminal(f.terminal)==0); canvas(&f,NULL,0); owners(&f,31,32,31,32);
	string(&f,"I#V 100,100,2,0,"); local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS; CHECK(st_cterm_poll(f.terminal)==0); canvas(&f,center,1); CHECK(b->state.bgi.fill_color==6);
	stage="V timeout reset"; setup(&f,2); string(&f,"I#V 100,100,2,0,"); f.reset_bell=true; local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS; CHECK(st_cterm_poll(f.terminal)==0); f.reset_bell=false; canvas(&f,NULL,0); CHECK(b->state.bgi.fill_color==15);
	stage="V clock error"; setup(&f,2); string(&f,"I#V 100,100,2,0,"); clock_error=IO_ERROR_IO; CHECK(st_cterm_poll(f.terminal)==0); clock_error=0; canvas(&f,NULL,0); CHECK(b->state.bgi.fill_color==15 && b->last_error==IO_ERROR_IO);
	/* Failure after temporary empty fill has no final fill application. */
	stage="V aspect failure"; setup(&f,2); size_t rejected=b->rejected_commands; b->state.bgi.aspect[1]=0; string(&f,"I#V 100,100,1,0,90:\033[6n");
	CHECK(b->last_error==IO_ERROR_OUT_OF_RANGE && b->rejected_commands==rejected+1 && b->state.bgi.fill_style==0 && b->state.bgi.fill_color==0 && b->state.bgi.color==5 && f.size==6); canvas(&f,NULL,0); owners(&f,31,32,31,32);
	b->state.bgi.aspect[1]=10000; string(&f,"I#V 100,100,1,0,0:"); canvas(&f,center,1); CHECK(b->state.bgi.fill_style==1 && b->state.bgi.fill_color==6);
	stage="V storage failure"; setup(&f,3); size_t extent=b->producer.storage_count; b->producer.storage_count=0; string(&f,"I#V 100,100,1,0,90:\033[6n");
	CHECK(b->last_error==IO_ERROR_OUT_OF_RANGE && b->state.bgi.fill_style==0 && b->state.bgi.fill_color==0 && f.size==6); canvas(&f,NULL,0);
	b->producer.storage_count=extent; string(&f,"I#V 100,100,1,0,90:"); canvas(&f,quarter,3); CHECK(b->state.bgi.fill_color==6);
	stage="V collector read plane"; setup(&f,3); b->producer.read_plane=4; string(&f,"I#V 100,100,1,0,90:"); canvas(&f,quarter,3); CHECK(!b->producer.batch); b->producer.read_plane=0;
	stage="V wire scaling"; setup(&f,3); string(&f,"I#g 3:I#V 100,5,2,0,0:"); static const struct dot scaled[]={{100,11}}; canvas(&f,scaled,1); owners(&f,100,11,100,11); CHECK(b->state.bgi.curve_radius[0]==2 && b->state.bgi.curve_radius[1]==2);
	stage="V stored scaling"; setup(&f,3); string(&f,"I#g 3:I#& 0,0,1,0,V@5,100,5,2,0,0:I#g 0:"); static const struct dot stored[]={{100,5}}; canvas(&f,stored,1); owners(&f,100,5,100,5); CHECK(b->numeric.loop.active==0);
	for (unsigned mode=1;mode<=3;++mode) {
		stage="V active pages"; setup(&f,mode); string(&f,"I#G>6,1:V 100,100,1,0,0:");
		if (mode<3) { canvas(&f,NULL,0); string(&f,"I#G 5,1:"); } canvas(&f,center,1);
	}
	stage="V deferred reply"; setup(&f,3); b->control.input.pending=1; b->control.input.count=2; b->control.input.payload[1]=0; b->control.input.payload[2]=255;
	string(&f,"I#V>100,100,1,0,0:"); CHECK(f.size==0 && b->control.input.pending==1); string(&f,"~ ");
	CHECK(f.size==2 && f.output[0]==0 && f.output[1]==255 && f.origins[0]==ST_CTERM_OUTPUT_USER && f.origins[1]==ST_CTERM_OUTPUT_USER);
	stage="V following drawto"; setup(&f,3); string(&f,"I#H 0:I#V 100,100,2,0,90:I#D 102,100:"); canvas_with_fill(&f,outer,7,inside,1);
	CHECK(b->state.bgi.x==102 && b->state.bgi.y==100 && b->state.bgi.ddo_x==102 && b->state.bgi.device_x==102);
	destroy(&f);
}


static void sector_Y(void)
{
	static const struct dot center[]={{100,100}},tall[]={{100,98},{100,99},{100,100},{101,99},{101,100}},wide[]={{100,99},{101,99},{100,100},{101,100},{102,100}};
	static const struct dot outer[]={{100,98},{101,98},{102,99},{102,100},{101,100},{100,100},{100,99}},inside[]={{101,99}};
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	for (unsigned mode=1;mode<=3;++mode) {
		stage="Y explicit tall"; setup(&f,mode); string(&f,"I#H 0:I#C 2,6:I#Y 100,100,1,2,0,90:");
		canvas(&f,tall,5); owners(&f,100,100,100,98); CHECK(b->state.bgi.curve_radius[0]==1 && b->state.bgi.curve_radius[1]==2 && b->producer.count==4 && b->state.bgi.curve_span==0 && b->state.bgi.fill_color==6);
		stage="Y explicit wide"; setup(&f,mode); string(&f,"I#Y 100,100,2,1,0,90:"); canvas(&f,wide,5); owners(&f,100,100,100,99); CHECK(b->producer.count==4);
		stage="Y real interior"; setup(&f,mode); string(&f,"I#Y 100,100,2,2,0,90:"); canvas_with_fill(&f,outer,7,inside,1); owners(&f,100,100,100,98); CHECK(b->producer.count==5);
		stage="Y hollow"; setup(&f,mode); string(&f,"I#H 256:I#Y 100,100,2,2,0,90:"); canvas(&f,outer,7); CHECK(b->state.bgi.fill_style==1 && b->state.bgi.fill_color==6);
		stage="Y equal XOR"; setup(&f,mode); string(&f,"I#M 2:I#Y 100,100,1,2,0,0:"); canvas(&f,NULL,0); owners(&f,100,100,100,100);
	}
	stage="Y no aspect dependency"; setup(&f,2); b->state.bgi.aspect[0]=b->state.bgi.aspect[1]=0;
	string(&f,"I#H 0:I#Y 100,100,1,2,0,90:"); canvas(&f,tall,5); owners(&f,100,100,100,98); CHECK(b->state.bgi.fill_color==6);
	stage="Y zero X radius"; setup(&f,2); string(&f,"I#Y 100,100,0,1,0,90:"); static const struct dot vertical[]={{100,99},{100,100}}; canvas(&f,vertical,2); owners(&f,100,100,100,100); CHECK(b->producer.count==1 && b->state.bgi.curve_radius[0]==0 && b->state.bgi.curve_radius[1]==1);
	stage="Y zero Y radius"; setup(&f,2); string(&f,"I#Y 100,100,1,0,0,90:"); static const struct dot horizontal[]={{100,100},{101,100}}; canvas(&f,horizontal,2); owners(&f,100,100,100,100); CHECK(b->producer.count==1 && b->state.bgi.curve_radius[1]==0);
	stage="Y high WORD radii"; setup(&f,2); string(&f,"I#Y 100,100,+1,+1,0,0:"); canvas(&f,center,1); CHECK(b->state.bgi.curve_radius[0]==0xffff && b->state.bgi.curve_radius[1]==0xffff);
	stage="Y angle is not query"; setup(&f,2); string(&f,"I#Y 100,100,1,1,+1,0:"); canvas(&f,horizontal,2); CHECK(b->state.bgi.curve_span==15);
	stage="Y orientation"; setup(&f,2); b->state.bgi.orientation=1; string(&f,"I#Y 100,100,1,2,0,90:");
	static const struct dot rotated[]={{100,99},{99,99},{98,100},{99,100},{100,100}}; canvas(&f,rotated,5);
	CHECK(b->state.bgi.curve_radius[0]==2 && b->state.bgi.curve_radius[1]==1 && b->state.bgi.arc[2]==100 && b->state.bgi.arc[3]==99 && b->state.bgi.arc[4]==98 && b->state.bgi.arc[5]==100); b->state.bgi.orientation=0;
	stage="Y alternate"; setup(&f,3); b->state.bgi.curve_fallback=1; string(&f,"I#Y 100,100,1,1,0,360:");
	static const struct dot alternate_line[]={{99,101},{100,101}},alternate_fill[]={{100,99},{99,100},{100,100},{101,100}};
	canvas_with_fill(&f,alternate_line,2,alternate_fill,4); owners(&f,100,100,100,100); CHECK(b->producer.count==365); b->state.bgi.curve_fallback=0;
	stage="Y wire scaling"; setup(&f,2); string(&f,"I#g 2:I#Y 100,100,1,2,0,0:"); static const struct dot scaled[]={{100,175}}; canvas(&f,scaled,1); owners(&f,100,175,100,175); CHECK(b->state.bgi.curve_radius[1]==3);
	stage="Y stored scaling"; setup(&f,2); string(&f,"I#g 2:I#& 0,0,1,0,Y@6,100,100,1,2,0,90:I#g 0:"); canvas(&f,tall,5); owners(&f,100,100,100,98); CHECK(b->state.bgi.curve_radius[1]==2 && b->numeric.loop.active==0);
	stage="Y original g3 rounding"; setup(&f,3); string(&f,"I#g 3:I#Y 100,5,1,5,0,0:"); static const struct dot rounded[]={{100,11}}; canvas(&f,rounded,1); CHECK(b->state.bgi.curve_radius[1]==11);
	stage="Y stored g3 bypass"; setup(&f,3); string(&f,"I#g 3:I#& 0,0,1,0,Y@6,100,5,1,5,0,0:I#g 0:"); static const struct dot stored[]={{100,5}}; canvas(&f,stored,1); CHECK(b->state.bgi.curve_radius[1]==5);
	stage="Y late setters"; setup(&f,2); string(&f,"I#H 0:I#Y 100,100,1,2,0,"); canvas(&f,NULL,0); CHECK(b->state.bgi.color==15 && b->state.bgi.fill_color==15);
	b->state.application.colors[1]=5; b->state.application.colors[2]=6; string(&f,"90:"); canvas(&f,tall,5); CHECK(b->state.bgi.color==5 && b->state.bgi.fill_color==6);
	stage="Y EOF/reset/timeout"; setup(&f,2); string(&f,"I#Y 100,100,1,2,0,"); CHECK(st_cterm_finish(f.terminal)==0); canvas(&f,NULL,0); CHECK(b->state.bgi.fill_color==15);
	string(&f,"I#Y 100,100,1,2,0,"); CHECK(st_cterm_reset_terminal(f.terminal)==0); canvas(&f,NULL,0); owners(&f,31,32,31,32);
	string(&f,"I#Y 100,100,1,2,0,"); local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS; CHECK(st_cterm_poll(f.terminal)==0); canvas(&f,center,1); CHECK(b->state.bgi.fill_color==6);
	stage="Y clock failure"; setup(&f,2); string(&f,"I#Y 100,100,1,2,0,"); clock_error=IO_ERROR_IO; CHECK(st_cterm_poll(f.terminal)==0); clock_error=0; canvas(&f,NULL,0); CHECK(b->last_error==IO_ERROR_IO && b->state.bgi.fill_color==15);
	stage="Y rejected hollow restoration"; setup(&f,2); string(&f,"I#H 1:I#C 2,6:I#A 1:I#C 2,31:I#A 12:I#Y 100,100,1,2,0,90:"); canvas(&f,tall,5); CHECK(b->state.bgi.fill_style==0 && b->state.bgi.fill_color==0 && b->state.bgi.error==-11);
	stage="Y reached storage failure"; setup(&f,2); string(&f,"I#C 2,6:I#A 1:"); size_t extent=b->producer.storage_count; b->producer.storage_count=0; size_t rejected=b->rejected_commands;
	string(&f,"I#Y 100,100,1,2,0,90:\033[6n"); canvas(&f,NULL,0); CHECK(b->last_error==IO_ERROR_OUT_OF_RANGE && b->rejected_commands==rejected+1 && b->state.bgi.fill_style==0 && b->state.bgi.fill_color==0 && f.size==6);
	b->producer.storage_count=extent; string(&f,"I#Y 100,100,1,2,0,90:"); canvas(&f,tall,5); CHECK(b->state.bgi.fill_color==6);
	for (unsigned mode=1;mode<=3;++mode) {
		stage="Y active pages"; setup(&f,mode); string(&f,"I#G>6,1:Y 100,100,1,2,0,0:"); if (mode<3) { canvas(&f,NULL,0); string(&f,"I#G 5,1:"); } canvas(&f,center,1);
	}
	stage="Y deferred reply"; setup(&f,2); b->control.input.pending=1; b->control.input.count=2; b->control.input.payload[1]=0; b->control.input.payload[2]=255;
	string(&f,"I#Y>100,100,1,2,0,0:"); CHECK(f.size==0 && b->control.input.pending==1); string(&f,"~ "); CHECK(f.size==2 && f.output[0]==0 && f.output[1]==255 && f.origins[0]==ST_CTERM_OUTPUT_USER);
	stage="Y following drawto"; setup(&f,2); string(&f,"I#H 0:I#Y 100,100,1,2,0,90:I#D 102,100:"); static const struct dot follow[]={{100,98},{100,99},{100,100},{101,99},{101,100},{102,100}}; canvas(&f,follow,6);
	CHECK(b->state.bgi.x==102 && b->state.bgi.y==100 && b->state.bgi.ddo_x==102 && b->state.bgi.device_x==102);
	destroy(&f);
}

int main(void) { stage="partitions"; partitions_and_profiles(); stage="partial"; late_color_and_partial_lifecycle(); stage="query/aspect"; query_zero_overflow_and_aspects(); stage="buffer/page/source"; buffered_pages_source_and_storage(); stage="banks/error/footer"; banks_errors_and_footer(); stage="shared scratch/scaling"; shared_scratch_and_scaling(); stage="J explicit radii"; explicit_radius_J(); stage="O circle/flood"; circle_O(); stage="Q ellipse/fill timing"; ellipse_Q(); stage="V pieslice"; pieslice_V(); stage="Y sector"; sector_Y(); return 0; }
