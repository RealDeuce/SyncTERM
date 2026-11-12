/* ?: independent reply bytes, event ownership and original MFORM inputs. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "igs_test_fixture.h"
#include "igs_mouse_form_inputs.h"
static uint8_t replies[4096],keys[64]; static size_t count,key_count;
static struct cterm_igs *live; static bool reset_reply;
static void response(const char *bytes,size_t size,void *context)
{
	(void)context; CHECK(size<=sizeof(replies)-count); memcpy(replies+count,bytes,size); count+=size;
	if(reset_reply && live!=NULL) cterm_igs_reset(live);
}
static void output(const char *bytes,size_t size,void *context)
{
	(void)context; CHECK(size<=sizeof(keys)-key_count); memcpy(keys+key_count,bytes,size); key_count+=size;
}
static void setup(struct fixture *f,unsigned resolution,bool status)
{
	create(f,resolution,status); f->terminal->response_cb=response; f->terminal->keystroke_cb=output;
	count=key_count=0; live=NULL; reset_reply=false;
	const struct st_console_rect rect={0,0,f->terminal->igs_engine->width,f->terminal->igs_engine->height}; seed(f,&rect,769,768);
}
static void expect(const void *bytes,size_t size)
{
	if(count!=size || memcmp(replies,bytes,size)!=0) {
		fprintf(stderr,"reply %zu/%zu:",count,size);
		for(size_t i=0;i<count;++i) fprintf(stderr," %02x",replies[i]);
		fprintf(stderr," expected:"); for(size_t i=0;i<size;++i) fprintf(stderr," %02x",((const uint8_t *)bytes)[i]);
		fprintf(stderr,"\n");
	}
	CHECK(count==size && memcmp(replies,bytes,size)==0); count=0;
}
static bool mouse(struct fixture *f,int x,int y,unsigned buttons)
{
	struct st_console_event event={.type=ST_CONSOLE_EVENT_MOUSE}; event.data.mouse.action=ST_CONSOLE_MOUSE_BUTTON;
	event.data.mouse.pixel_x=x; event.data.mouse.pixel_y=y; event.data.mouse.x=x/8; event.data.mouse.y=y/8;
	event.data.mouse.buttons=buttons; return st_cterm_handle_mouse(f->terminal,&event);
}
static void replies_and_cursor(void)
{
	struct fixture f; setup(&f,0,false); struct cterm_igs *igs=f.terminal->igs_engine;
	feed(&f,"G#? 0:"); expect("2.21",4); feed(&f,"G#? 3:"); expect("0:",2);
	feed(&f,"G#p>3,2:? 1,0:"); expect((uint8_t[]){35,34,32},3);
	static const struct {int x,y;const char *reply;} vectors[]={
		{420,150,"420,150,0:"},{1000,1001,"1000,1001,0:"},{1010,1100,"1010,1100,0:"},
		{32767,32767,"9999,9999,0:"},{10000,10000,"9999,9999,0:"},{-1,-32768,"0,0,0:"}};
	for(size_t i=0;i<sizeof(vectors)/sizeof(*vectors);++i) {
		igs->input.mouse_x=vectors[i].x; igs->input.mouse_y=vectors[i].y;
		feed(&f,"G#? 2,0:"); expect(vectors[i].reply,strlen(vectors[i].reply));
	}
	igs->math[0]=-1; feed(&f,"G#?>0]:M 4:"); CHECK(count==0 && igs->attributes.writing_mode==4);
	feed(&f,"G#?>4:M 2:"); CHECK(count==0 && igs->attributes.writing_mode==2);
	igs->random_seed=1; igs->random_range[0][0]=0; igs->random_range[0][1]=255;
	feed(&f,"G#? r:"); CHECK(count==0 && igs->random_seed==UINT32_C(0xbb40e62e));
	igs->math[1]=1; feed(&f,"G#? 1],"); igs->math[1]=3; feed(&f,"0:"); expect((uint8_t[]){35,34,32},3);
	feed(&f,"G#? 1,"); CHECK(st_cterm_finish(f.terminal)==0 && !igs->query.configuring && count==0);
	feed(&f,"G#? 2,"); local_ms+=5000; CHECK(st_cterm_poll(f.terminal)==0 && !igs->query.configuring);
	feed(&f,"G#? 1,10000:G#X 10,19,17:"); CHECK(!igs->query.active && !igs->query.configuring && igs->draw_x==19 && count==0);
	(void)mouse(&f,10,10,0); feed(&f,"G#? 1,1:"); CHECK(igs->query.active && st_cterm_mouse_mode(f.terminal)==3 && count==0);
	CHECK(mouse(&f,50,100,0)); CHECK(f.terminal->xpos==5 && f.terminal->ypos==4 && count==0);
	CHECK(mouse(&f,49,90,5) && igs->query.release); CHECK(f.terminal->xpos==4 && f.terminal->ypos==3);
	CHECK(st_cterm_handle_text_key(f.terminal,'k') && key_count==0);
	CHECK(mouse(&f,200,180,0)); expect((uint8_t[]){35,34,35},3); CHECK(key_count==1 && keys[0]=='k' && !cterm_igs_paused(igs)); key_count=0;
	/* Queries consume clicks independently of the outer X3/X6 behavior. */
	feed(&f,"G#X>6,2:X 3,2,1,1,1:Z!");
	(void)mouse(&f,10,10,0); feed(&f,"G#? 2,1:"); CHECK(mouse(&f,20,20,1)); CHECK(mouse(&f,21,22,0));
	expect("21,22,1:",8); CHECK(key_count==0 && !igs->input.left_held && !igs->input.held);
	feed(&f,"G#? 2,11:G#X 10,21,23:"); CHECK(!igs->query.active && igs->draw_x==21 && count==0);
	live=igs; reset_reply=true; feed(&f,"G#? 0:"); reset_reply=false; live=NULL;
	expect("2",1); CHECK(!igs->query.active && igs->input.left_mode==0);
	destroy(&f);
}
static unsigned word(const uint8_t *p) { return (unsigned)p[0]*256+p[1]; }
static void check_form(struct fixture *f,unsigned form,int x,int y,bool drawn)
{
	struct cterm_igs *igs=f->terminal->igs_engine;
	const struct st_console_rect rect={0,0,igs->width,igs->height}; struct st_pixel_buffer *p=NULL;
	CHECK(st_console_get_pixels(&p,f->console,&rect,false)==0);
	const uint8_t *bytes=original_forms[form]; int hx=(int)word(bytes),hy=(int)word(bytes+2);
	int height=f->terminal->height*(igs->resolution==2?16:8); unsigned mask=cterm_igs_plane_mask(igs->resolution);
	for(unsigned row=0;row<igs->height;++row) for(unsigned col=0;col<igs->width;++col) {
		unsigned primary=1,alternate=0;
		int fx=(int)col-x+hx,fy=(int)row-y+hy;
		if(drawn && fx>=0 && fx<16 && fy>=0 && fy<16 && (int)row<height) {
			bool b=(word(bytes+10+2*fy)&(0x8000u>>fx))!=0;
			bool d=(word(bytes+42+2*fy)&(0x8000u>>fx))!=0;
			/* Independent per-pixel interpretation of the original form:
			 * data paints foreground, mask-only background, else retain. */
			if(d) primary=alternate=mask;
			else if(b) primary=alternate=0;
		}
		size_t offset=(size_t)row*igs->width+col;
		CHECK(p->primary[offset]==768+primary && p->alternate[offset]==768+alternate);
	}
	st_pixel_buffer_destroy(p);
}
static void forms_and_cleanup(void)
{
	for(unsigned resolution=0;resolution<3;++resolution) for(unsigned status=0;status<2;++status) {
		struct fixture f; setup(&f,resolution,status!=0); struct cterm_igs *igs=f.terminal->igs_engine;
		igs->draw_x=31; igs->draw_y=32; igs->attributes.writing_mode=2;
		bool visible; CHECK(st_console_get_pointer_visible(f.console,&visible)==0);
		for(unsigned form=0;form<8;++form) for(unsigned phase=0;phase<16;++phase) {
			int x=phase==0?0:phase==15?(int)igs->width-1:32+(int)phase;
			int y=phase==0?0:phase==15?f.terminal->height*(resolution==2?16:8)-1:50;
			(void)mouse(&f,x,y,0); char wire[32]; snprintf(wire,sizeof(wire),"G#? 2,%u:",form+3); feed(&f,wire);
			CHECK(igs->query.active && igs->attributes.writing_mode==3); check_form(&f,form,x,y,true);
			CHECK(mouse(&f,x,y,1)); CHECK(igs->query.release && count==0);
			CHECK(mouse(&f,x+1<(int)igs->width?x+1:x,y,1));
			check_form(&f,form,x+1<(int)igs->width?x+1:x,y,true);
			CHECK(st_cterm_finish(f.terminal)==0 && !igs->query.active && igs->attributes.writing_mode==2 && count==0);
			check_form(&f,form,x,y,false); CHECK(igs->draw_x==31 && igs->draw_y==32);
			bool after; CHECK(st_console_get_pointer_visible(f.console,&after)==0 && after==visible);
			(void)mouse(&f,x,y,0);
		}
		/* Marker uses physical XOR and does not move during release. */
		(void)mouse(&f,10,20,0); feed(&f,"G#? 2,1:");
		struct st_pixel_buffer *p=NULL; const struct st_console_rect point={10,20,1,1};
		CHECK(st_console_get_pixels(&p,f.console,&point,false)==0);
		CHECK(p->primary[0]==768+(1^cterm_igs_plane_mask(resolution)) && p->alternate[0]==768+cterm_igs_plane_mask(resolution)); st_pixel_buffer_destroy(p);
		CHECK(mouse(&f,13,25,1)); CHECK(mouse(&f,15,27,1));
		CHECK(mouse(&f,18,30,0)); expect("18,30,1:",8);
		CHECK(igs->attributes.writing_mode==2 && igs->draw_x==31 && igs->draw_y==32);
		check_form(&f,0,0,0,false);
		destroy(&f);
	}
}
static void boundaries_and_reset(void)
{
	struct fixture f; setup(&f,0,false); struct cterm_igs *igs=f.terminal->igs_engine;
	igs->attributes.writing_mode=2;
	static const int styles[]={-32768,-1,1,2,11,254,255,256,257,258,259,260,32767};
	for(size_t i=0;i<sizeof(styles)/sizeof(*styles);++i) {
		(void)mouse(&f,10,20,0); igs->math[0]=(int16_t)styles[i]; feed(&f,"G#? 2,0]:");
		if(styles[i]<=2) {
			CHECK(igs->query.active && mouse(&f,12,22,4) && mouse(&f,20,30,0));
			expect("20,30,2:",8);
		} else CHECK(!igs->query.active && count==0);
		CHECK(igs->attributes.writing_mode==2); check_form(&f,0,0,0,false);
	}
	igs->math[0]=-1; feed(&f,"G#? 1,0]:"); CHECK(igs->query.active);
	CHECK(mouse(&f,20,30,1) && mouse(&f,20,30,0)); expect((uint8_t[]){33,33,33},3);
	/* Noncanonical service button words still emit their low-byte result. */
	igs->input.buttons=208; igs->input.mouse_x=igs->input.mouse_y=0;
	feed(&f,"G#? 2,0:"); expect((uint8_t[]){'0',',','0',',',0,':'},6);
	igs->input.buttons=0;
	/* Query keyboard ownership extends through a remaining graphics chain. */
	feed(&f,"G#?>1,1:"); CHECK(st_cterm_handle_key(f.terminal,'q',0));
	CHECK(mouse(&f,20,30,1) && mouse(&f,20,30,0)); expect((uint8_t[]){34,34,33},3);
	CHECK(key_count==0); feed(&f,"M 2:"); CHECK(key_count==1 && keys[0]=='q'); key_count=0;
	st_cterm_sync_mouse_buttons(f.terminal,1); feed(&f,"G#? 2,1:");
	CHECK(igs->query.active && !igs->query.release);
	st_cterm_sync_mouse_buttons(f.terminal,0); CHECK(!igs->query.release && count==0);
	CHECK(mouse(&f,20,30,1) && mouse(&f,20,30,0)); expect("20,30,1:",8);
	(void)mouse(&f,100,100,0);feed(&f,"G#? 2,1:");
	CHECK(!mouse(&f,-1,-1,1));CHECK(mouse(&f,100,100,1) && !igs->query.release);
	CHECK(mouse(&f,100,100,0) && igs->query.active);
	st_cterm_sync_mouse_buttons(f.terminal,1);CHECK(mouse(&f,100,100,1) && !igs->query.release);
	st_cterm_sync_mouse_buttons(f.terminal,0);CHECK(mouse(&f,100,100,1));CHECK(mouse(&f,101,101,0));
	expect("101,101,1:",10);
	(void)mouse(&f,100,100,0); feed(&f,"G#? 2,3:"); CHECK(mouse(&f,110,110,1));
	struct st_console_event claimed={.type=ST_CONSOLE_EVENT_MOUSE};
	claimed.data.mouse.pixel_x=120; claimed.data.mouse.pixel_y=130;
	st_cterm_observe_mouse(f.terminal,&claimed); st_cterm_sync_mouse_buttons(f.terminal,0);
	expect("120,130,1:",10); CHECK(!igs->query.active);
	(void)mouse(&f,100,100,0); feed(&f,"G#? 2,5:"); CHECK(igs->query.pointer.saved!=NULL);
	cterm_igs_reset(igs); CHECK(!igs->query.active && igs->query.pointer.saved==NULL && igs->query.selector==0 && count==0);
	check_form(&f,0,0,0,false);
	/* Reserving a host status row during a query must not let old save-under
	 * overwrite its newly painted UI pixels on cancellation. */
	(void)mouse(&f,40,193,0); feed(&f,"G#? 2,4:");
	CHECK(igs->query.pointer.saved!=NULL); f.terminal->height=24;
	const struct st_console_rect ui={0,192,320,8}; seed(&f,&ui,UINT32_C(0x80eeddcc),UINT32_C(0x80eeddcc));
	CHECK(st_cterm_finish(f.terminal)==0);
	struct st_pixel_buffer *pixels=NULL; CHECK(st_console_get_pixels(&pixels,f.console,&ui,false)==0);
	for(size_t i=0;i<320*8;++i) CHECK(pixels->primary[i]==UINT32_C(0x80eeddcc) && pixels->alternate[i]==UINT32_C(0x80eeddcc));
	st_pixel_buffer_destroy(pixels); f.terminal->height=25;
	destroy(&f);
}
static void partitions(void)
{
	static const char wire[]="G#?>0:?>1,0:?>2,0:? 3:";
	for(unsigned resolution=0;resolution<3;++resolution) for(unsigned status=0;status<2;++status) {
		struct fixture f; setup(&f,resolution,status!=0); struct cterm_igs *igs=f.terminal->igs_engine;
		feed(&f,"G#p 3,2:");
		for(unsigned encoding=0;encoding<3;++encoding) {
			CHECK(st_cterm_set_encoding(f.terminal,encoding==2?ST_CTERM_ENCODING_UTF8:ST_CTERM_ENCODING_CODEPAGE,
			    encoding==0?ST_CODEPAGE_ATARI_ST:ST_CODEPAGE_CP437)==0);
			for(int scale=0;scale<3;++scale) {
				cterm_igs_set_scaling(igs,scale);
				for(size_t split=0;split<sizeof(wire);++split) {
					size_t used; CHECK(st_cterm_write(f.terminal,wire,split,&used)==0 && used==split);
					CHECK(st_cterm_write(f.terminal,wire+split,sizeof(wire)-1-split,&used)==0 && used==sizeof(wire)-1-split);
					uint8_t expected[]={'2','.','2','1',35,34,32,'0',',','0',',','0',':',(uint8_t)(48+resolution),':'};
					expect(expected,sizeof(expected));
				}
				for(size_t i=0;i<sizeof(wire)-1;++i) {
					size_t used; CHECK(st_cterm_write(f.terminal,wire+i,1,&used)==0 && used==1);
				}
				uint8_t expected[]={'2','.','2','1',35,34,32,'0',',','0',',','0',':',(uint8_t)(48+resolution),':'};
				expect(expected,sizeof(expected));
			}
		}
		CHECK(f.terminal->xpos==4 && f.terminal->ypos==3); check_form(&f,0,0,0,false); destroy(&f);
	}
}
int main(void) { replies_and_cursor(); forms_and_cleanup(); boundaries_and_reset(); partitions(); return 0; }
