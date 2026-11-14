/* Independent BIG K wire/page/source/recovery expectations, original13/61. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm.h"
#include "cterm_big_internal.h"
#include "st_cterm.h"
#include "io_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
struct fixture { st_console_t *console; st_font_catalog_t *fonts; st_cterm_t *terminal; uint8_t output[128]; enum st_cterm_output_origin origins[128]; size_t size; unsigned bells; bool reset_bell; };
struct dot { unsigned x,y; };
static const char *stage;
static uint64_t local_ns;
static int clock_error;
static int now(void *context, uint64_t *value) { (void)context; *value=local_ns; return clock_error; }
static void bell(void *context)
{
	struct cterm_big *b=context; struct fixture *f=b->terminal->api_output_context;
	++f->bells; if (f->reset_bell) CHECK(st_cterm_reset_terminal(f->terminal)==0);
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
static void canvas(struct fixture *f, const struct dot *dots, size_t count)
{
	const struct st_console_rect area={0,0,640,f->terminal->big_engine->state.bgi.height};
	struct st_pixel_buffer *p=NULL; CHECK(st_console_get_pixels(&p,f->console,&area,false)==0);
	uint8_t *expected=calloc((size_t)area.width*area.height,1); CHECK(expected!=NULL);
	for (size_t i=0;i<count;++i) expected[dots[i].y*640+dots[i].x]=5;
	for (size_t i=0;i<(size_t)area.width*area.height;++i) {
		if (p->primary[i]!=800u+expected[i] || p->alternate[i]!=800u+expected[i])
			fprintf(stderr,"%s pixel%zu,%zu actual%u/%u expected%u, error%d span%u radius%u/%u\n",stage,i%640,i/640,p->primary[i],p->alternate[i],800u+expected[i],f->terminal->big_engine->last_error,f->terminal->big_engine->state.bgi.curve_span,f->terminal->big_engine->state.bgi.curve_radius[0],f->terminal->big_engine->state.bgi.curve_radius[1]);
		CHECK(p->primary[i]==800u+expected[i] && p->alternate[i]==800u+expected[i]);
	}
	free(expected); st_pixel_buffer_destroy(p);
}
static void owners(struct fixture *f, unsigned x, unsigned y, unsigned device_x, unsigned device_y)
{
	const struct st_big_state *s=&f->terminal->big_engine->state;
	CHECK(s->bgi.x==31 && s->bgi.y==32 && s->bgi.ddo_x==x && s->bgi.ddo_y==y && s->bgi.device_x==device_x && s->bgi.device_y==device_y);
}
static const struct dot full[]={{100,99},{100,101},{99,100},{101,100}};
static void partitions_and_profiles(void)
{
	static const char wire[]="I#K 100,100,1,0,360:\033[2;3H\033[6n";
	for (unsigned utf8=0;utf8<2;++utf8) for (unsigned status=0;status<2;++status) {
		struct fixture f; create(&f,utf8,status); struct cterm_big *b=f.terminal->big_engine;
		for (unsigned resolution=1;resolution<=3;++resolution) for (size_t split=0;split<=sizeof(wire)-1;++split) {
			setup(&f,resolution); feed(&f,wire,split); feed(&f,wire+split,sizeof(wire)-1-split);
			CHECK(f.size==6 && memcmp(f.output,"\033[2;3R",6)==0 && b->rejected_commands==0 && b->state.bgi.error==-11);
			owners(&f,100,100,0xa014,3); canvas(&f,full,4);
			CHECK(b->state.bgi.curve_radius[0]==1 && b->state.bgi.curve_radius[1]==(resolution==3?1:0) && b->producer.mode==0 && !b->producer.batch);
		}
		setup(&f,2); for (size_t i=0;i<sizeof(wire)-1;++i) feed(&f,wire+i,1); canvas(&f,full,4); destroy(&f);
	}
}
static void late_color_and_partial_lifecycle(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine; setup(&f,2);
	b->state.application.colors[1]=6;
	string(&f,"I#K 100,100,1,0,"); CHECK(b->state.bgi.color==15 && b->state.bgi.device_color==15); owners(&f,31,32,31,32);
	b->state.application.colors[1]=5; string(&f,"1:");
	const struct dot center[]={{100,100}}; canvas(&f,center,1); owners(&f,100,100,0x0f02,4); CHECK(b->state.bgi.color==5 && f.size==0);
	setup(&f,2); string(&f,"I#K 100,100,1,0,"); CHECK(st_cterm_finish(f.terminal)==0); canvas(&f,NULL,0); owners(&f,31,32,31,32); CHECK(b->state.bgi.color==15);
	string(&f,"I#K 100,100,1,0,"); CHECK(st_cterm_reset_terminal(f.terminal)==0); canvas(&f,NULL,0); owners(&f,31,32,31,32);
	/* A real timeout completes the final empty numeric field; colour
	 * publication still follows all five completed readers. */
	string(&f,"I#K 100,100,1,0,"); local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS; CHECK(st_cterm_poll(f.terminal)==0);
	CHECK(b->state.bgi.color==5 && b->state.bgi.curve_span==0 && b->state.bgi.ddo_x==100); canvas(&f,NULL,0);
	setup(&f,2); string(&f,"I#K 100,100,"); clock_error=IO_ERROR_IO;
	CHECK(st_cterm_poll(f.terminal)==0); clock_error=0; string(&f,"\033[6n"); CHECK(b->last_error==IO_ERROR_IO && f.size==6);
	canvas(&f,NULL,0); CHECK(b->state.bgi.color==15); destroy(&f);
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
int main(void) { stage="partitions"; partitions_and_profiles(); stage="partial"; late_color_and_partial_lifecycle(); stage="query/aspect"; query_zero_overflow_and_aspects(); stage="buffer/page/source"; buffered_pages_source_and_storage(); stage="banks/error/footer"; banks_errors_and_footer(); stage="shared scratch/scaling"; shared_scratch_and_scaling(); return 0; }
