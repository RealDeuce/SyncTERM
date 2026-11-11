/* X6: literal byte modes and owner-driven left/right/key/serial ordering. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "igs_test_fixture.h"
static uint8_t keys[2048]; static size_t count;
static struct cterm_igs *live;
static int action;
static void output(const char *bytes,size_t size,void *context)
{
	(void)context; CHECK(size<=sizeof(keys)-count); memcpy(keys+count,bytes,size); count+=size;
	if(live!=NULL && count==1) {
		if(action==1) live->input.left_mode=2;
		if(action==2) live->input.left_mode=1;
		if(action==3) live->input.left_mode=0;
		if(action==4) cterm_igs_reset(live);
		if(action==5) st_cterm_sync_mouse_buttons(live->terminal,0);
	}
}
static void setup(struct fixture *f)
{
	create(f,0,false); f->terminal->keystroke_cb=output; count=0; action=0; live=NULL;
	const struct st_console_rect rect={0,0,8,8}; seed(f,&rect,768,768);
}
static bool mouse(struct fixture *f,unsigned buttons)
{
	struct st_console_event event={.type=ST_CONSOLE_EVENT_MOUSE};
	event.data.mouse.action=ST_CONSOLE_MOUSE_BUTTON; event.data.mouse.buttons=buttons;
	event.data.mouse.x=1; event.data.mouse.y=1; event.data.mouse.pixel_x=8; event.data.mouse.pixel_y=8;
	return st_cterm_handle_mouse(f->terminal,&event);
}
static size_t write_bytes(struct fixture *f,const void *bytes,size_t size)
{
	size_t used; CHECK(st_cterm_write(f->terminal,bytes,size,&used)==0); return used;
}
static void mode(struct fixture *f,int value)
{
	f->terminal->igs_engine->math[0]=(int16_t)value;
	feed(f,"G#X 6,0]:"); CHECK(f->terminal->igs_engine->input.left_mode==((unsigned)value&255));
}
static void values(void)
{
	struct fixture f; setup(&f); struct cterm_igs *igs=f.terminal->igs_engine; struct cterm_igs_input *m=&igs->input;
	CHECK(m->left_mode==0 && !m->left_held && st_cterm_mouse_mode(f.terminal)==0);
	m->length=3; m->cr=7; memset(m->bytes,0xa5,80);
	for(int value=0;value<256;++value) {
		mode(&f,value); CHECK(count==0 && m->length==3 && m->cr==7 && m->enabled==0 && m->bytes[79]==0xa5);
		CHECK(st_cterm_mouse_mode(f.terminal)==(value!=0?3:0));
		CHECK(!mouse(&f,5)); (void)mouse(&f,0); CHECK(count==0);
		CHECK(mouse(&f,1)==(value!=0)); CHECK(count==0 && m->left_held==(value!=0) && !m->held);
		CHECK(cterm_igs_paused(igs)==(value!=0));
		CHECK(mouse(&f,1)==(value!=0) && count==0);
		CHECK(mouse(&f,0)==(value!=0));
		CHECK(count==(value==0?0u:value==2?2u:1u));
		if(count) CHECK(keys[0]==13); if(count==2) CHECK(keys[1]==10);
		CHECK(!m->left_held && !cterm_igs_paused(igs)); count=0;
	}
	static const int words[]={-32768,-257,-256,-255,-254,-2,-1,0,1,2,3,255,256,257,258,32767};
	for(size_t i=0;i<sizeof(words)/sizeof(*words);++i) {
		mode(&f,words[i]); unsigned byte=(unsigned)words[i]&255;
		CHECK(mouse(&f,1)==(byte!=0) && count==0); (void)mouse(&f,0);
		CHECK(count==(byte==0?0u:byte==2?2u:1u)); count=0;
	}
	mode(&f,2); feed(&f,"G#X 6,"); CHECK(m->left_configuring && m->left_mode==2);
	CHECK(st_cterm_finish(f.terminal)==0 && !m->left_configuring && m->left_mode==2);
	feed(&f,"G#X 6,"); local_ms+=5000; CHECK(st_cterm_poll(f.terminal)==0 && !m->left_configuring && m->left_mode==2);
	feed(&f,"G#X 6,10000:G#X 10,91,92:"); CHECK(m->left_mode==2 && !m->left_configuring && igs->draw_x==91);
	igs->math[1]=6; feed(&f,"G#X 1],"); igs->math[1]=10; feed(&f,"1:"); CHECK(m->left_mode==1);
	igs->random_seed=1; igs->random_range[0][0]=0; igs->random_range[0][1]=255;
	feed(&f,"G#X 6,r:"); CHECK(m->left_mode==230 && igs->random_seed==UINT32_C(0xbb40e62e) && count==0);
	feed(&f,"G#I 1:"); CHECK(m->left_mode==230);
	feed(&f,"G#R 1,0:"); local_ms=igs->wait_deadline; CHECK(st_cterm_poll(f.terminal)==0 && m->left_mode==230);
	cterm_igs_reset(igs); CHECK(m->left_mode==0 && !m->left_held);
	destroy(&f);
}
static void ownership(void)
{
	struct fixture f; setup(&f); struct cterm_igs *igs=f.terminal->igs_engine; struct cterm_igs_input *m=&igs->input;
	CHECK(st_cterm_set_encoding(f.terminal,ST_CTERM_ENCODING_UTF8,ST_CODEPAGE_CP437)==0);
	mode(&f,2); CHECK(mouse(&f,1) && count==0);
	CHECK(write_bytes(&f,"G#X 6,0:",8)==0 && m->left_mode==2);
	CHECK(st_cterm_handle_text_key(f.terminal,0xe9) && m->key_count==1 && count==0);
	CHECK(mouse(&f,5) && count==4 && memcmp(keys,"\r\n\xc3\xa9",4)==0 && !m->left_held && !m->held && m->buttons==3);
	(void)mouse(&f,0); count=0;
	feed(&f,"G#X 3,2,1,1,2:");
	/* feed() is text-only, so load its two opaque bytes explicitly. */
	CHECK(write_bytes(&f,(uint8_t[]){0,255,'!'},3)==3);
	CHECK(mouse(&f,1) && count==0); CHECK(mouse(&f,4));
	CHECK(count==5 && memcmp(keys,(uint8_t[]){13,10,0,255,13},5)==0 && !m->left_held && m->held);
	CHECK(mouse(&f,1) && count==5 && m->held); CHECK(mouse(&f,0) && !m->held); count=0;
	/* A pending left sample belongs after the enclosing raw command/wait. */
	feed(&f,"G#X 4,0,1,2,3,4,1:"); CHECK(mouse(&f,1) && !m->left_held);
	const char wire[]="ABG#X 10,19,17:";
	CHECK(write_bytes(&f,wire,sizeof(wire)-1)==2 && m->left_held && m->suspended && count==0);
	CHECK(write_bytes(&f,wire+2,sizeof(wire)-3)==0); CHECK(mouse(&f,0));
	CHECK(count==2 && memcmp(keys,"\r\n",2)==0 && !cterm_igs_paused(igs)); count=0;
	CHECK(write_bytes(&f,wire+2,sizeof(wire)-3)==sizeof(wire)-3 && igs->draw_x==19);
	feed(&f,"G#q 1:"); CHECK(mouse(&f,1) && !m->left_held);
	local_ms=igs->wait_deadline; CHECK(st_cterm_poll(f.terminal)==0 && m->left_held && count==0);
	CHECK(mouse(&f,0) && count==2); count=0;
	/* Enabling while left is already down starts a hold at command end. */
	mode(&f,0); CHECK(!mouse(&f,1));
	const char enable[]="G#X 6,2:G#X 10,31,32:";
	CHECK(write_bytes(&f,enable,sizeof(enable)-1)==8 && m->left_held && m->suspended && count==0);
	CHECK(mouse(&f,0) && count==2); count=0;
	CHECK(write_bytes(&f,enable+8,sizeof(enable)-9)==sizeof(enable)-9 && igs->draw_x==31);
	/* Claimed/disabled/UI presses cannot become a late activation. */
	st_cterm_sync_mouse_buttons(f.terminal,1); feed(&f,"G#X 10,33,34:"); CHECK(!m->left_held && count==0);
	CHECK(!mouse(&f,1)); st_cterm_sync_mouse_buttons(f.terminal,0);
	CHECK(mouse(&f,1)); struct st_console_event outside={.type=ST_CONSOLE_EVENT_MOUSE};
	outside.data.mouse.action=ST_CONSOLE_MOUSE_BUTTON; outside.data.mouse.x=outside.data.mouse.y=1000;
	outside.data.mouse.pixel_x=outside.data.mouse.pixel_y=10000;
	CHECK(st_cterm_handle_mouse(f.terminal,&outside) && !m->left_held && count==2); count=0;
	/* Mode is reloaded AFTER CR. A reset interrupts LF and later branches. */
	for(action=1;action<=5;++action) {
		mode(&f,action==1?1:2); CHECK(mouse(&f,1)); live=igs; CHECK(mouse(&f,0)); live=NULL;
		CHECK(count==(action==1||action==5?2u:1u) && keys[0]==13 && !m->left_held && !m->sending);
		if(action==4) CHECK(m->left_mode==0 && m->enabled==0);
		count=0;
	}
	mode(&f,1); CHECK(mouse(&f,1)); m->left_mode=0; CHECK(mouse(&f,0) && count==1 && keys[0]==13); count=0;
	mode(&f,2); CHECK(mouse(&f,1)); CHECK(st_cterm_handle_key(f.terminal,'x',0));
	CHECK(st_cterm_finish(f.terminal)==0 && !m->left_held && m->left_mode==2 && m->key_count==0 && count==0);
	destroy(&f);
}
static void profiles(void)
{
	static const char wire[]="G#X>6,258:X 6,2:";
	for(unsigned resolution=0;resolution<3;++resolution) for(unsigned status=0;status<2;++status) {
		struct fixture f; create(&f,resolution,status!=0); struct cterm_igs *igs=f.terminal->igs_engine;
		f.terminal->keystroke_cb=output; count=0;
		feed(&f,"\033fG#p 3,2:"); const struct st_console_rect rect={0,0,igs->width,igs->height}; seed(&f,&rect,769,768);
		uint32_t palette[16]; memcpy(palette,igs->palette,sizeof(palette)); struct cterm_igs_attributes attributes=igs->attributes;
		for(unsigned encoding=0;encoding<3;++encoding) {
			CHECK(st_cterm_set_encoding(f.terminal,encoding==2?ST_CTERM_ENCODING_UTF8:ST_CTERM_ENCODING_CODEPAGE,
			    encoding==0?ST_CODEPAGE_ATARI_ST:ST_CODEPAGE_CP437)==0);
			for(int scaling=0;scaling<3;++scaling) {
				cterm_igs_set_scaling(igs,scaling);
				for(size_t split=0;split<sizeof(wire);++split) {
					CHECK(write_bytes(&f,wire,split)==split && write_bytes(&f,wire+split,sizeof(wire)-1-split)==sizeof(wire)-1-split);
					CHECK(igs->input.left_mode==2 && count==0 && !igs->input.left_configuring);
				}
				for(size_t i=0;i<sizeof(wire)-1;++i) CHECK(write_bytes(&f,wire+i,1)==1);
				CHECK(mouse(&f,1) && count==0 && mouse(&f,0) && count==2 && memcmp(keys,"\r\n",2)==0); count=0;
			}
		}
		CHECK(f.terminal->xpos==4 && f.terminal->ypos==3 && memcmp(palette,igs->palette,sizeof(palette))==0);
		CHECK(memcmp(&attributes,&igs->attributes,sizeof(attributes))==0);
		struct st_pixel_buffer *p=NULL; CHECK(st_console_get_pixels(&p,f.console,&rect,false)==0);
		for(size_t i=0;i<(size_t)igs->width*igs->height;++i) CHECK(p->primary[i]==769 && p->alternate[i]==768);
		st_pixel_buffer_destroy(p); destroy(&f);
	}
}
int main(void) { values(); ownership(); profiles(); return 0; }
