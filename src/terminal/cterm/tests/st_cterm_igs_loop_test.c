/* Independent & evaluator traces, wire execution and cancellation cases. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "igs_test_fixture.h"
#include "cterm_igs_music.h"
static uint8_t output[8192],keys[128];static size_t output_count,key_count;
static struct cterm_igs *live;static bool reset_on_flow;
static void response(const char *bytes,size_t count,void *context)
{
	(void)context;CHECK(count<=sizeof(output)-output_count);memcpy(output+output_count,bytes,count);output_count+=count;
	if(reset_on_flow && live!=NULL) cterm_igs_reset(live);
}
static void user(const char *bytes,size_t count,void *context)
{(void)context;CHECK(count<=sizeof(keys)-key_count);memcpy(keys+key_count,bytes,count);key_count+=count;}
static void setup(struct fixture *f,unsigned mode,bool status)
{
	create(f,mode,status);f->terminal->response_cb=response;f->terminal->keystroke_cb=user;
	output_count=key_count=0;live=NULL;reset_on_flow=false;
	const struct st_console_rect full={0,0,f->terminal->igs_engine->width,f->terminal->igs_engine->height};seed(f,&full,769,768);
}
static void flow(void) {CHECK(output_count==2 && output[0]==19 && output[1]==17);output_count=0;}
static void clear(struct fixture *f)
{
	const struct st_console_rect full={0,0,f->terminal->igs_engine->width,f->terminal->igs_engine->height};seed(f,&full,769,768);output_count=0;
}
static void points(struct fixture *f,const int *xs,size_t count,int y,bool xor)
{
	struct cterm_igs *igs=f->terminal->igs_engine;const struct st_console_rect full={0,0,igs->width,igs->height};
	struct st_pixel_buffer *p=NULL;CHECK(st_console_get_pixels(&p,f->console,&full,false)==0);
	unsigned mask=(unsigned[]){15,3,1}[igs->resolution];
	for(unsigned row=0;row<igs->height;++row) for(unsigned col=0;col<igs->width;++col) {
		bool hit=false;for(size_t i=0;i<count;++i) if((int)row==y && (int)col==xs[i]) hit=true;
		unsigned a=hit?(xor?1^mask:mask):1,b=hit?mask:0;size_t pos=(size_t)row*igs->width+col;
		if(p->primary[pos]!=768+a || p->alternate[pos]!=768+b) fprintf(stderr,"pixel%u,%u %u/%u expected%u/%u\n",col,row,p->primary[pos],p->alternate[pos],768+a,768+b);
		CHECK(p->primary[pos]==768+a && p->alternate[pos]==768+b);
	}
	st_pixel_buffer_destroy(p);
}
static void evaluator(void)
{
	static const struct {int from,to,step,kind,ops[3];size_t count,n;int values[10],current,first,pass;} cases[]={
		{0,2,1,',',{-32767,7},2,6,{0,7,1,7,2,7},2,3,2},
		{0,2,1,'|',{-32767},1,5,{0,0,1,1,2},2,5,0},
		{0,2,1,',',{-32767,7,-32766},3,9,{0,7,2,1,7,1,2,7,0},2,6,2},
		{4,0,-2,',',{-32766},1,3,{0,2,4},0,3,2},
		{0,2,1,',',{7,8},2,4,{7,8,7,8},2,0,0},
		{5,5,1,',',{-32767},1,1,{5},5,1,0},
		{-20001,-20001,1,',',{-5},1,1,{29998},-20001,1,0},
		{-1,-1,1,',',{-32767},1,1,{0},-1,1,0},
		{0,1,1,',',{0},1,1,{0},1,0,0},
		{5,5,1,',',{-10000},1,1,{5},5,1,0},
		{5,5,1,',',{-20000},1,1,{-5},5,1,0},
	};
	struct fixture f;setup(&f,0,false);struct cterm_igs *igs=f.terminal->igs_engine;
	for(size_t i=0;i<sizeof(cases)/sizeof(*cases);++i) {
		igs->loop=(struct cterm_igs_loop){.current=cases[i].from,.to=cases[i].to,.original=cases[i].from,.step=cases[i].step,.count=cases[i].count,.command='P',.no_parameters=true};
		igs->loop_mode=1;igs->loop_kind=cases[i].kind;
		for(size_t j=0;j<cases[i].count;++j) {
			int v=cases[i].ops[j];igs->loop.operands[j]=(int16_t)v;
			if(v==-32767 || v==-32766 || (v<0 && v>-30000)) igs->loop.no_parameters=false;
		}
		for(size_t j=0;j<cases[i].n;++j) {int value;CHECK(cterm_igs_loop_value(igs,&value)==0);CHECK(value==cases[i].values[j]);}
		CHECK(igs->loop_mode==0 && igs->loop.index==0 && igs->loop.current==cases[i].current && igs->loop.first==cases[i].first && igs->loop.pass==cases[i].pass);
	}
	igs->loop=(struct cterm_igs_loop){.current=0,.to=5,.step=1,.count=1,.delay=10,.operands={-32767},.command='W'};
	igs->loop_mode=1;igs->loop_kind=',';int value=999;
	CHECK(cterm_igs_loop_value(igs,&value)==IO_ERROR_IN_PROGRESS && value==999 && igs->loop.index==0 && igs->loop.first==0);
	local_ms+=49;CHECK(cterm_igs_loop_value(igs,&value)==IO_ERROR_IN_PROGRESS && igs->loop.first==0);
	++local_ms;CHECK(cterm_igs_loop_value(igs,&value)==0 && value==0 && igs->loop_mode==1);
	igs->input.keys[0]=(struct cterm_key){.local=3};igs->input.key_first=0;igs->input.key_count=1;
	CHECK(cterm_igs_loop_value(igs,&value)==0 && value==1 && igs->loop_mode==0 && igs->loop_kind=='Z' && igs->input.key_count==0);
	igs->loop=(struct cterm_igs_loop){.current=0,.to=1,.step=1,.count=1,.delay=-1,.operands={-32767}};
	igs->loop_mode=1;igs->loop_kind=',';local_ms=1000;
	CHECK(cterm_igs_loop_value(igs,&value)==IO_ERROR_IN_PROGRESS && igs->loop.deadline==328675);
	local_ms=328674;CHECK(cterm_igs_loop_value(igs,&value)==IO_ERROR_IN_PROGRESS);
	++local_ms;CHECK(cterm_igs_loop_value(igs,&value)==0 && value==0);
	igs->loop=(struct cterm_igs_loop){.to=0,.step=1,.count=1,.delay=10,.operands={-32767}};
	igs->loop_mode=1;local_ms=UINT64_MAX-3;
	CHECK(cterm_igs_loop_value(igs,&value)==IO_ERROR_IN_PROGRESS && igs->loop.deadline==UINT64_MAX);
	local_ms=UINT64_MAX;CHECK(cterm_igs_loop_value(igs,&value)==0 && value==0 && igs->loop_mode==0);local_ms=1000;
	igs->loop=(struct cterm_igs_loop){.current=32766,.to=32767,.step=2,.count=1,.operands={-32767}};igs->loop_mode=1;
	CHECK(cterm_igs_loop_value(igs,&value)==0 && value==32766);
	CHECK(cterm_igs_loop_value(igs,&value)==IO_ERROR_OUT_OF_RANGE && igs->loop.current==32766);
	destroy(&f);
}
static void wire_bodies(void)
{
	struct fixture f;setup(&f,0,false);struct cterm_igs *igs=f.terminal->igs_engine;
	feed(&f,"G#& 0,2,1,0,P,2,x,7:");flow();points(&f,(int[]){0,1,2},3,7,false);
	CHECK(!igs->loop.run && igs->loop_mode==0 && igs->draw_x==2 && igs->draw_y==7);
	clear(&f);feed(&f,"G#M>3:& 0,2,1,0,P|2,x,7:");flow();points(&f,(int[]){2},1,7,true);
	feed(&f,"G#M 1:");clear(&f);feed(&f,"G#& 4,0,2,0,P,2,y,7:");flow();points(&f,(int[]){0,2,4},3,7,false);
	clear(&f);feed(&f,"G#& 0,2,1,0,>P?@,3,0,x,7:");flow();points(&f,(int[]){0,1,2},3,7,false);
	/* The last stored operand can stop the loop before a command is done. */
	clear(&f);feed(&f,"G#& 0,0,1,0,P,1,x:");CHECK(igs->loop.run && igs->loop_mode==0 && !cterm_igs_paused(igs));
	CHECK(output_count==1 && output[0]==19);feed(&f,"9:G#M 4:");flow();points(&f,(int[]){0},1,9,false);CHECK(igs->attributes.writing_mode==4);
	feed(&f,"G#M 1:");clear(&f);feed(&f,"G#& 0,1,1,0,>~P@,1,x:3,4:");flow();points(&f,(int[]){3},1,4,false);
	clear(&f);igs->shared_text[10]='P';feed(&f,"G#& 0,0,1,0,>~@,1,10:3,4:");flow();points(&f,(int[]){3},1,4,false);
	/* Unknown fixed command (and fixed &) executes no numeric reader and
	 * retains the global loop flag for the next graphics entry. */
	clear(&f);feed(&f,"G#& 0,0,1,0,~,1,x:");flow();CHECK(igs->loop_mode==1 && !igs->loop.run);
	feed(&f,"G#M ");CHECK(igs->loop_mode==0 && igs->attributes.writing_mode==1 && output_count==0);
	feed(&f,"G#& 0,0,1,0,&,1,x:");flow();CHECK(igs->loop_mode==1 && !igs->loop.run);feed(&f,"G#M ");
	/* Dynamic X arity uses supplied typed values without advancing twice. */
	feed(&f,"G#& 0,2,1,0,X,4,13,0,x,0:");flow();CHECK(igs->math[0]==2 && igs->loop.current==2 && igs->loop.index==0);
	/* Random substitutions stay encoded until a body actually requests one. */
	igs->random_seed=1;feed(&f,"G#& 0,1,1,0,P,2,r,R:");flow();CHECK(igs->loop.operands[0]==-32765 && igs->loop.operands[1]==-32764 && igs->random_seed!=1);
	/* A dynamic query selector observes one actual RNG call, not another
	 * call from arity preview or the command's numeric execution pass. */
	igs->random_seed=1;igs->random_range[0][1]=255;
	feed(&f,"G#& 0,0,1,0,?,1,r:");flow();
	CHECK(igs->random_seed==UINT32_C(0xbb40e62e));igs->random_range[0][1]=199;
	/* Zero/negative count clamps to1; header fields commit before later input. */
	igs->math[0]=-1;feed(&f,"G#& 0,0,0,0,?,0],0:");CHECK(output_count==6 && memcmp(output,"\x13""2.21\x11",6)==0);output_count=0;
	CHECK(igs->loop.count==1 && igs->loop.step==1);
	destroy(&f);
}
static void text_raw_and_setup(void)
{
	struct fixture f;setup(&f,0,false);struct cterm_igs *igs=f.terminal->igs_engine;
	feed(&f,"G#E>0,9,0:W 40,60,A@");clear(&f);
	feed(&f,"G#& 40,56,8,0,W,2,x,60:");flow();
	CHECK(igs->shared_text_size==1 && igs->shared_text[0]=='A' && igs->loop_kind==' ');
	const uint8_t rows[]={0x18,0x3c,0x66,0x66,0x7e,0x66,0x66,0};
	const struct st_console_rect area={40,54,24,8};struct st_pixel_buffer *pixels=NULL;
	CHECK(st_console_get_pixels(&pixels,f.console,&area,false)==0);
	for(unsigned y=0;y<8;++y) for(unsigned x=0;x<24;++x) CHECK(pixels->primary[y*24+x]==768u+((rows[y]&(128u>>(x%8)))?15u:0u) && pixels->alternate[y*24+x]==pixels->primary[y*24+x]);
	st_pixel_buffer_destroy(pixels);clear(&f);
	feed(&f,"G#& 40,56,8,0,W@2,x,60:A@A@A@");
	CHECK(output_count==3 && output[0]==19 && output[1]==17 && output[2]==17 && igs->text_counter==2);output_count=0;
	CHECK(st_console_get_pixels(&pixels,f.console,&area,false)==0);
	for(unsigned y=0;y<8;++y) for(unsigned x=0;x<24;++x) CHECK(pixels->primary[y*24+x]==768u+((rows[y]&(128u>>(x%8)))?15u:0u) && pixels->alternate[y*24+x]==pixels->primary[y*24+x]);
	st_pixel_buffer_destroy(pixels);
	const uint8_t raw[]="G#& 0,2,1,0,X,5,3,2,1,0,2:G#!\0X!";
	size_t used;CHECK(st_cterm_write(f.terminal,raw,sizeof(raw)-1,&used)==0 && used==sizeof(raw)-1);
	CHECK(output_count==3 && output[0]==19 && output[1]==17 && output[2]==17);output_count=0;
	CHECK(igs->input.enabled==1 && igs->input.length==2 && igs->input.bytes[0]==0 && igs->input.bytes[1]=='X');
	feed(&f,"G#X 3,0:");clear(&f);
	igs->math[1]=4;feed(&f,"G#& 1],");CHECK(igs->loop.current==4 && igs->loop.initializing);
	igs->math[1]=9;feed(&f,"0,2,0,P,2,y,7:");flow();points(&f,(int[]){0,2,4},3,7,false);
	clear(&f);igs->random_seed=1;feed(&f,"G#& 0,1,1,0,P,2,r,");CHECK(igs->random_seed==1 && igs->loop.operands[0]==-32765);
	feed(&f,"R:");flow();CHECK(igs->loop.operands[1]==-32764 && igs->random_seed!=1);
	/* Full clamped operand capacity remains bounded and cooperatively
	 * executable, without an arbitrary total-iteration cutoff. */
	char wire[5000];size_t size=(size_t)snprintf(wire,sizeof(wire),"G#& 0,0,1,0,M,9999:");
	for(size_t i=0;i<2048;++i) {wire[size++]='0';wire[size++]=',';}
	CHECK(st_cterm_write(f.terminal,wire,size,&used)==0 && used==size && igs->loop.count==2048 && igs->loop.cooperative);
	for(unsigned i=0;i<40 && igs->loop.run;++i) CHECK(st_cterm_poll(f.terminal)==0);
	CHECK(!igs->loop.run && igs->loop_mode==0 && igs->loop.index==0);flow();
	destroy(&f);
}
static void waits_keys_and_cleanup(void)
{
	struct fixture f;setup(&f,0,false);struct cterm_igs *igs=f.terminal->igs_engine;
	feed(&f,"G#& 0,1,1,10,P,2,x,7:");CHECK(cterm_igs_paused(igs) && igs->loop.waiting && output_count==1);
	local_ms+=49;CHECK(st_cterm_poll(f.terminal)==0 && igs->loop.first==0);
	++local_ms;CHECK(st_cterm_poll(f.terminal)==0 && igs->loop.first==1 && igs->loop.waiting);
	local_ms+=50;CHECK(st_cterm_poll(f.terminal)==0);flow();points(&f,(int[]){0,1},2,7,false);
	/* Async body completion returns to the loop owner, not the outer chain. */
	feed(&f,"G#& 0,2,1,0,q,1,x:");CHECK(igs->wait_deadline!=0 && igs->loop.run);
	for(unsigned i=0;i<10 && igs->loop.run;++i) {local_ms+=100;CHECK(st_cterm_poll(f.terminal)==0);}
	CHECK(!igs->loop.run);flow();
	/* Long loops yield without a total iteration limit; Ctrl-C is consumed
	 * at the next numeric read, leaving the final command result intact. */
	feed(&f,"G#& 0,9999,1,0,?,1,0:");CHECK(igs->loop.run && igs->loop.cooperative && output_count==257);
	CHECK(st_cterm_handle_key(f.terminal,3,0) && key_count==0);CHECK(st_cterm_poll(f.terminal)==0);
	CHECK(!igs->loop.run && igs->loop_mode==0 && output_count==262 && output[0]==19 && output[261]==17 && key_count==0);output_count=0;
	/* Flow callbacks can reset before a first body; no stale operands resume. */
	live=igs;reset_on_flow=true;feed(&f,"G#& 0,2,1,0,P,2,x,7:");reset_on_flow=false;live=NULL;
	CHECK(!igs->loop.run && !igs->loop.initializing && igs->loop_mode==0);output_count=0;
	feed(&f,"G#& 0,2,1,10,P,2,x,7:");CHECK(igs->loop.run);
	CHECK(st_cterm_finish(f.terminal)==0 && !igs->loop.run && igs->loop_mode==0);flow();
	feed(&f,"G#& 0,2,");CHECK(igs->loop.initializing);CHECK(st_cterm_finish(f.terminal)==0 && !igs->loop.initializing);
	feed(&f,"G#& 0,2,");local_ms+=5000;CHECK(st_cterm_poll(f.terminal)==0 && !igs->loop.initializing);
	destroy(&f);
}
static void invalid_body_recovery(void)
{
	struct fixture f;setup(&f,0,false);struct cterm_igs *igs=f.terminal->igs_engine;
	igs->random_range[0][0]=2;igs->random_range[0][1]=1;
	feed(&f,"G#& 0,0,1,0,P,2,r,7:G#X 10,9,11:");flow();
	CHECK(!igs->loop.run && igs->loop_mode==0 && igs->draw_x==9 && igs->draw_y==11 && igs->random_seed==1);
	igs->random_range[0][0]=0;igs->random_range[0][1]=199;
	igs->math[0]=32766;igs->math[1]=32767;
	feed(&f,"G#& 0],1],2,0,P,2,x,7:G#X 10,13,15:");flow();
	CHECK(!igs->loop.run && igs->loop_mode==0 && igs->draw_x==13 && igs->draw_y==15);
	points(&f,NULL,0,0,false);destroy(&f);
}
static void inner_key_owners(void)
{
	struct fixture f;setup(&f,0,false);struct cterm_igs *igs=f.terminal->igs_engine;
	feed(&f,"G#& 0,1,1,0,t,1,30:");CHECK(igs->timed_pause && igs->loop.run);
	CHECK(st_cterm_handle_key(f.terminal,'x',0) && igs->pause_key && igs->input.key_count==0);
	CHECK(st_cterm_poll(f.terminal)==0 && !igs->timed_pause);
	CHECK(st_cterm_poll(f.terminal)==0 && !igs->loop.run && key_count==0);
	CHECK(output_count==4 && memcmp(output,"\x13\x13\x11\x11",4)==0);output_count=0;
	feed(&f,"G#& 0,1,1,0,X,5,8,0,1,2,100:");CHECK(igs->palette_rotation.active && igs->loop.run);
	CHECK(st_cterm_handle_key(f.terminal,'x',0) && igs->palette_rotation.key && igs->input.key_count==0);
	for(unsigned i=0;i<12 && igs->loop.run;++i) {local_ms+=20;CHECK(st_cterm_poll(f.terminal)==0);}
	CHECK(!igs->palette_rotation.active && !igs->loop.run && key_count==0);flow();
	/* Header loading queues local keys. Numeric polls consume one per call;
	 * the remaining key belongs to the inner helper once it starts. */
	feed(&f,"G#& 0,1,1,0,t,1,");
	CHECK(st_cterm_handle_key(f.terminal,'a',0) && st_cterm_handle_key(f.terminal,'b',0));
	feed(&f,"30:");CHECK(igs->timed_pause && igs->input.key_count==1);
	CHECK(st_cterm_poll(f.terminal)==0 && !igs->timed_pause && igs->input.key_count==0);
	CHECK(st_cterm_poll(f.terminal)==0 && !igs->loop.run && key_count==0);
	CHECK(output_count==4 && memcmp(output,"\x13\x13\x11\x11",4)==0);output_count=0;
	feed(&f,"G#& 0,1,1,0,X,5,");
	for(unsigned i=0;i<6;++i) CHECK(st_cterm_handle_key(f.terminal,(uint16_t)('a'+i),0));
	feed(&f,"8,0,1,2,100:");CHECK(igs->palette_rotation.active && igs->input.key_count==1);
	CHECK(st_cterm_poll(f.terminal)==0 && igs->palette_rotation.key && igs->input.key_count==0);
	for(unsigned i=0;i<12 && igs->loop.run;++i) {local_ms+=20;CHECK(st_cterm_poll(f.terminal)==0);}
	CHECK(!igs->palette_rotation.active && !igs->loop.run && key_count==0);flow();
	destroy(&f);
}
static bool mouse(struct fixture *f,int x,int y,unsigned buttons)
{
	struct st_console_event e={.type=ST_CONSOLE_EVENT_MOUSE};e.data.mouse.action=ST_CONSOLE_MOUSE_BUTTON;
	e.data.mouse.pixel_x=x;e.data.mouse.pixel_y=y;e.data.mouse.x=x/8;e.data.mouse.y=y/8;e.data.mouse.buttons=buttons;
	return st_cterm_handle_mouse(f->terminal,&e);
}
static void mouse_bodies(void)
{
	struct fixture f;setup(&f,0,false);struct cterm_igs *igs=f.terminal->igs_engine;
	feed(&f,"G#& 0,1,1,0,?,2,2,1:");CHECK(igs->loop.run && igs->query.active);
	CHECK(st_cterm_handle_text_key(f.terminal,'q') && key_count==0);
	CHECK(mouse(&f,20,30,1) && mouse(&f,40,50,0));
	CHECK(!igs->loop.run && !igs->query.active && output_count==10 && memcmp(output,"\x13""40,50,1:\x11",10)==0);
	CHECK(key_count==1 && keys[0]=='q');key_count=output_count=0;points(&f,NULL,0,0,false);
	static const uint8_t zone[]="G#X>4,9999:X 4,0,10,20,30,40,2:\0B!";size_t used;
	CHECK(st_cterm_write(f.terminal,zone,sizeof(zone)-1,&used)==0 && used==sizeof(zone)-1);
	feed(&f,"G#& 0,1,1,0,<,3,1,2,0:");CHECK(igs->loop.run && igs->collect.active);
	CHECK(mouse(&f,30,40,1) && mouse(&f,200,180,0));flow();
	CHECK(!igs->loop.run && !igs->collect.active && key_count==3 && memcmp(keys,"\0B\r",3)==0);
	points(&f,NULL,0,0,false);destroy(&f);
}
static void partitions(void)
{
	static const char wire[]="G#& 0,2,1,0,P,2,x,7:";
	for(unsigned mode=0;mode<3;++mode) for(unsigned status=0;status<2;++status) {
		struct fixture f;setup(&f,mode,status!=0);struct cterm_igs *igs=f.terminal->igs_engine;
		for(unsigned encoding=0;encoding<3;++encoding) {
			CHECK(st_cterm_set_encoding(f.terminal,encoding==2?ST_CTERM_ENCODING_UTF8:ST_CTERM_ENCODING_CODEPAGE,encoding==0?ST_CODEPAGE_ATARI_ST:ST_CODEPAGE_CP437)==0);
			for(int scaling=0;scaling<3;++scaling) {
				cterm_igs_set_scaling(igs,scaling);
				for(size_t split=0;split<sizeof(wire);++split) {
					size_t used;CHECK(st_cterm_write(f.terminal,wire,split,&used)==0 && used==split);
					CHECK(st_cterm_write(f.terminal,wire+split,sizeof(wire)-1-split,&used)==0 && used==sizeof(wire)-1-split);flow();
				}
				for(size_t i=0;i<sizeof(wire)-1;++i) {size_t used;CHECK(st_cterm_write(f.terminal,wire+i,1,&used)==0 && used==1);}flow();
				CHECK(!igs->loop.run && igs->loop_mode==0 && igs->loop.current==2);points(&f,(int[]){0,1,2},3,7,false);
			}
		}
		destroy(&f);
	}
}
int main(void) {evaluator();wire_bodies();text_raw_and_setup();waits_keys_and_cleanup();invalid_body_recovery();inner_key_owners();mouse_bodies();partitions();return 0;}
