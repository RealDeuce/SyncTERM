/* X5: literal flow state, incremental stores and shared emitter behavior. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "igs_test_fixture.h"
#include "cterm_igs_music.h"
static uint8_t output[40000];
static size_t count;
static struct cterm_igs *live;
static void response(const char *bytes,size_t size,void *context)
{
	(void)context; CHECK(size<=sizeof(output)-count); memcpy(output+count,bytes,size); count+=size;
	if(live!=NULL && count==1) { live->flow_mode=0; live->flow_on=0xaa; live->flow_on_count=3; }
}
static void prepare(struct fixture *f)
{
	create(f,0,false); f->terminal->response_cb=response; count=0; live=NULL;
	const struct st_console_rect rect={0,0,8,8}; seed(f,&rect,768,768);
}
static void mode(struct fixture *f,int mode,int byte,int repeats)
{
	struct cterm_igs *igs=f->terminal->igs_engine;
	igs->math[0]=(int16_t)mode; igs->math[1]=(int16_t)byte; igs->math[2]=(int16_t)repeats;
	feed(f,mode==2||mode==3?"G#X 5,0],1],2]:":"G#X 5,0]:");
}
static void values(void)
{
	struct fixture f; prepare(&f); struct cterm_igs *igs=f.terminal->igs_engine;
	static const int bytes[]={-32768,-32767,-257,-256,-1,0,1,17,19,127,128,255,256,257,511,32767};
	static const int repeats[]={-32768,-1,0,1,2,32767};
	for(int kind=2;kind<=3;++kind) for(size_t b=0;b<sizeof(bytes)/sizeof(*bytes);++b)
		for(size_t n=0;n<sizeof(repeats)/sizeof(*repeats);++n) {
			mode(&f,kind,bytes[b],repeats[n]); CHECK(count==0 && igs->flow_mode==kind);
			CHECK((kind==2?igs->flow_on:igs->flow_off)==((unsigned)bytes[b]&255));
			CHECK((kind==2?igs->flow_on_count:igs->flow_off_count)==repeats[n]);
			if(repeats[n]<=2) {
				CHECK(cterm_igs_flow(igs,kind==2)==0 && count==(size_t)(repeats[n]>0?repeats[n]:0));
				for(size_t i=0;i<count;++i) CHECK(output[i]==((unsigned)bytes[b]&255));
				count=0;
			}
		}
	mode(&f,2,511,2); mode(&f,3,256,3); CHECK(count==0);
	feed(&f,"G#N 4,0:"); CHECK(count==5 && memcmp(output,(uint8_t[]){0,0,0,255,255},5)==0); count=0;
	mode(&f,0,0,0); CHECK(igs->flow_on==255 && igs->flow_off==0 && igs->flow_on_count==2 && igs->flow_off_count==3);
	feed(&f,"G#N 4,0:"); CHECK(count==0);
	mode(&f,-32768,0,0); CHECK(igs->flow_mode==-32768 && igs->flow_on==255 && igs->flow_off==0);
	feed(&f,"G#N 4,0:"); CHECK(count==5); count=0;
	for(int value=4;value<=32767;value+=32763) {
		mode(&f,value,0,0); CHECK(igs->flow_mode==value && igs->flow_on==17 && igs->flow_off==19);
		CHECK(igs->flow_on_count==1 && igs->flow_off_count==1 && count==0);
	}
	mode(&f,2,0xe1,32767); CHECK(cterm_igs_flow(igs,true)==0 && count==32767);
	for(size_t i=0;i<count;++i) CHECK(output[i]==0xe1);
	count=0;
	mode(&f,2,17,4); live=igs; CHECK(cterm_igs_flow(igs,true)==0); live=NULL;
	CHECK(count==3 && memcmp(output,(uint8_t[]){17,170,170},3)==0 && igs->flow_mode==0); count=0;
	CHECK(cterm_igs_flow(igs,true)==0 && count==0);
	destroy(&f);
}
static void partial_and_loops(void)
{
	struct fixture f; prepare(&f); struct cterm_igs *igs=f.terminal->igs_engine;
	feed(&f,"G#X 5,2,"); CHECK(igs->flow_mode==2 && igs->flow_on==17 && igs->flow_on_count==1);
	feed(&f,"511,"); CHECK(igs->flow_on==255 && igs->flow_on_count==1);
	CHECK(st_cterm_finish(f.terminal)==0 && igs->flow_mode==2 && igs->flow_on==255 && igs->flow_on_count==1);
	feed(&f,"G#X 5,3,256,"); CHECK(igs->flow_mode==3 && igs->flow_off==0 && igs->flow_off_count==1);
	local_ms+=5000; CHECK(st_cterm_poll(f.terminal)==0);
	feed(&f,"G#X 10,7,9:"); CHECK(igs->draw_x==7 && igs->draw_y==9 && igs->flow_off==0);
	feed(&f,"G#X 5,2,256,"); size_t consumed;
	int rejected=st_cterm_write(f.terminal,"10000:",6,&consumed);
	CHECK((rejected==IO_ERROR_OUT_OF_RANGE || rejected==0) && igs->flow_mode==2 && igs->flow_on==0 && igs->flow_on_count==1 && igs->flow_configuring==0);
	feed(&f,"G#X 10,21,25:"); CHECK(igs->draw_x==21 && igs->draw_y==25);
	feed(&f,"G#X 5,2,511,1:");
	static const int loops[]={-32768,-1,1,2,32767};
	for(size_t i=0;i<sizeof(loops)/sizeof(*loops);++i) {
		igs->loop_mode=loops[i]; feed(&f,"G#X>5,2:X 10,13,15:");
		CHECK(igs->flow_mode==1 && igs->draw_x==13 && igs->draw_y==15 && igs->flow_on==255);
		feed(&f,"G#X 5,9999:"); CHECK(igs->flow_mode==1 && igs->flow_on==255 && igs->flow_off==0);
	}
	igs->loop_mode=0; feed(&f,"G#X 5,"); igs->loop_mode=2; feed(&f,"3:"); CHECK(igs->flow_mode==1);
	igs->loop_mode=1; feed(&f,"G#X 5,"); igs->loop_mode=0; feed(&f,"3,33,2:"); CHECK(igs->flow_mode==3 && igs->flow_off==33 && igs->flow_off_count==2);
	/* Later scalar/loop changes cannot reinterpret an accepted X5 prefix. */
	igs->math[3]=5; feed(&f,"G#X 3],2,511,");
	igs->math[3]=10; igs->loop_mode=1; feed(&f,"2:");
	CHECK(igs->flow_mode==2 && igs->flow_on==255 && igs->flow_on_count==2);
	igs->loop_mode=0;
	igs->random_seed=1; igs->random_range[0][0]=0; igs->random_range[0][1]=255;
	feed(&f,"G#X 5,r:"); CHECK(igs->flow_mode==230 && igs->random_seed==UINT32_C(0xbb40e62e));
	CHECK(igs->flow_on==17 && igs->flow_off==19 && count==0);
	igs->random_seed=1; feed(&f,"G#X 5,2,r,r:");
	CHECK(igs->flow_on==230 && igs->flow_on_count==202 && igs->random_seed==UINT32_C(0x5eb5ca17) && count==0);
	cterm_igs_reset(igs); CHECK(igs->flow_mode==1 && igs->flow_on==17 && igs->flow_off==19 && igs->flow_on_count==1 && igs->flow_off_count==1);
	destroy(&f);
}
static void partitions_and_pixels(void)
{
	for(unsigned resolution=0;resolution<3;++resolution) for(unsigned status=0;status<2;++status) {
		struct fixture f; create(&f,resolution,status!=0); struct cterm_igs *igs=f.terminal->igs_engine;
		f.terminal->response_cb=response; count=0;
		feed(&f,"\033fG#p>3,2:X 10,19,17:");
		const struct st_console_rect rect={0,0,igs->width,igs->height}; seed(&f,&rect,769,768);
		struct cterm_igs_attributes attrs=igs->attributes; uint32_t palette[16]; memcpy(palette,igs->palette,sizeof(palette));
		const char wire[]="G#X>5,2,511,2:X 5,3,256,3:";
		for(int scaling=0;scaling<3;++scaling) {
			cterm_igs_set_scaling(igs,scaling);
			for(size_t split=0;split<sizeof(wire);++split) {
				size_t used; CHECK(st_cterm_write(f.terminal,wire,split,&used)==0 && used==split);
				CHECK(st_cterm_write(f.terminal,wire+split,sizeof(wire)-1-split,&used)==0 && used==sizeof(wire)-1-split);
				CHECK(igs->flow_mode==3 && igs->flow_on==255 && igs->flow_off==0 && igs->flow_on_count==2 && igs->flow_off_count==3 && count==0);
			}
		}
		CHECK(memcmp(&attrs,&igs->attributes,sizeof(attrs))==0 && memcmp(palette,igs->palette,sizeof(palette))==0);
		CHECK(igs->draw_x==19 && igs->draw_y==17 && f.terminal->xpos==4 && f.terminal->ypos==3);
		struct st_pixel_buffer *p=NULL; CHECK(st_console_get_pixels(&p,f.console,&rect,false)==0);
		for(size_t i=0;i<(size_t)igs->width*igs->height;++i) CHECK(p->primary[i]==769 && p->alternate[i]==768);
		st_pixel_buffer_destroy(p); destroy(&f);
	}
}
int main(void) { values(); partial_and_loops(); partitions_and_pixels(); return 0; }
