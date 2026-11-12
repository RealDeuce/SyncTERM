/* X3: source-derived literal stores, opaque bytes and right-button input. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "igs_test_fixture.h"
#include "cterm_igs_music.h"
#include "cterm_render.h"
static uint8_t keys[4096], replies[128];
static size_t key_count, reply_count;
static struct cterm_igs *live;
static int callback_action;
static bool slow_flow;
static void output(const char *bytes,size_t size,void *context)
{
	(void)context; CHECK(size<=sizeof(keys)-key_count); memcpy(keys+key_count,bytes,size); key_count+=size;
	if(live!=NULL && key_count==1) {
		if(callback_action==1) { live->input.length=3; live->input.bytes[1]=0xff; live->input.bytes[2]=0; live->input.cr=1; live->input.enabled=0; }
		if(callback_action==2) st_cterm_sync_mouse_buttons(live->terminal,0);
		if(callback_action==3) cterm_igs_reset(live);
	}
}
static void response(const char *bytes,size_t size,void *context)
{
	(void)context; CHECK(size<=sizeof(replies)-reply_count); memcpy(replies+reply_count,bytes,size); reply_count+=size;
	if(slow_flow) local_ms+=5001;
}
static void setup(struct fixture *f)
{
	create(f,0,false); f->terminal->keystroke_cb=output; f->terminal->response_cb=response;
	key_count=reply_count=0; live=NULL; callback_action=0; slow_flow=false;
	const struct st_console_rect rect={0,0,8,8}; seed(f,&rect,768,768);
}
static size_t write_bytes(struct fixture *f,const void *bytes,size_t size)
{
	size_t used; CHECK(st_cterm_write(f->terminal,bytes,size,&used)==0); return used;
}
static bool mouse(struct fixture *f,unsigned buttons)
{
	struct st_console_event event={.type=ST_CONSOLE_EVENT_MOUSE};
	event.data.mouse.action=ST_CONSOLE_MOUSE_BUTTON; event.data.mouse.buttons=buttons;
	event.data.mouse.x=1; event.data.mouse.y=1; event.data.mouse.pixel_x=8; event.data.mouse.pixel_y=8;
	return st_cterm_handle_mouse(f->terminal,&event);
}
static void release(struct fixture *f)
{
	(void)mouse(f,0); CHECK(!cterm_igs_paused(f->terminal->igs_engine));
}
static void load(struct fixture *f,int enable,int cr,int length,const uint8_t *bytes,size_t size)
{
	struct cterm_igs *igs=f->terminal->igs_engine;
	igs->math[0]=(int16_t)enable; igs->math[1]=(int16_t)cr; igs->math[2]=(int16_t)length;
	feed(f,"G#X 3,2,0],1],2]:"); CHECK(write_bytes(f,bytes,size)==size);
}
static void stores(void)
{
	struct fixture f; setup(&f); struct cterm_igs *igs=f.terminal->igs_engine; struct cterm_igs_input *m=&igs->input;
	CHECK(!m->enabled && !m->length && !m->cr && st_cterm_mouse_mode(f.terminal)==0);
	for(size_t i=0;i<80;++i) CHECK(m->bytes[i]==0);
	/* Every byte value is raw data, including local abort keys and a G#.
	 * Stable 80-byte loads also cover the maximum consumer length. */
	for(unsigned base=0;base<256;base+=64) {
		uint8_t raw[81]; for(unsigned i=0;i<81;++i) raw[i]=(uint8_t)(base+i);
		load(&f,1,0,80,raw,81); CHECK(memcmp(m->bytes,raw,80)==0);
		CHECK(mouse(&f,4) && key_count==80 && memcmp(keys,raw,80)==0); release(&f); key_count=0;
	}
	cterm_igs_reset(igs);
	/* Op1 enables before its CR reader, retaining the initial empty macro. */
	feed(&f,"G#X 3,1,"); CHECK(m->enabled==1 && m->cr==0 && m->length==0);
	CHECK(st_cterm_finish(f.terminal)==0 && m->enabled==1 && !m->configuring);
	CHECK(mouse(&f,4) && key_count==0 && m->held); release(&f);
	feed(&f,"G#X 3,1,257:"); CHECK(m->cr==1 && m->length==0);
	CHECK(mouse(&f,4) && key_count==1 && keys[0]==13); release(&f); key_count=0;
	static const int words[]={-32768,-257,-256,-200,-129,-128,-1,0,1,79,80,81,127,128,255,256,257,32767};
	static const unsigned lengths[]={1,1,1,56,80,1,1,1,1,79,80,80,80,1,1,1,1,1};
	uint8_t data[81]; for(size_t i=0;i<sizeof(data);++i) data[i]=(uint8_t)(128+i);
	memset(m->bytes,0x5a,80);
	for(size_t i=0;i<sizeof(words)/sizeof(*words);++i) {
		load(&f,0,256,words[i],data,lengths[i]+1); CHECK(!m->enabled && !m->cr && m->length==lengths[i] && !m->loading && !m->configuring);
		CHECK(memcmp(m->bytes,data,lengths[i])==0 && key_count==0 && reply_count==0);
	}
	load(&f,-1,-1,80,data,81); CHECK(m->enabled==1 && m->cr==255 && m->length==80);
	uint8_t saved[80]; memcpy(saved,m->bytes,80);
	feed(&f,"G#X 3,0:"); CHECK(!m->enabled && m->cr==255 && m->length==80 && memcmp(saved,m->bytes,80)==0);
	feed(&f,"G#X 3,1,0:"); CHECK(m->enabled==1 && !m->cr && m->length==80);
	feed(&f,"G#X 3,-1:"); CHECK(m->enabled==1 && m->length==80);
	feed(&f,"G#X 3,9999:"); CHECK(!m->enabled && m->length==80);
	load(&f,0,0,1,(uint8_t[]){0xff,0},2); CHECK(m->bytes[0]==255 && memcmp(saved+1,m->bytes+1,79)==0);
	feed(&f,"G#X 3,2,"); CHECK(m->enabled==0);
	feed(&f,"1,"); CHECK(m->enabled==1 && m->cr==0);
	feed(&f,"257,"); CHECK(m->cr==1 && m->length==1);
	feed(&f,"3:"); CHECK(m->length==3 && m->loading && m->received==0);
	CHECK(write_bytes(&f,"z",1)==1 && m->bytes[0]=='z' && m->bytes[1]==saved[1]);
	CHECK(st_cterm_finish(f.terminal)==0 && !m->loading && !m->configuring && m->bytes[0]=='z' && m->bytes[1]==saved[1]);
	/* Numeric timeout/rejection retain the stores already made. */
	feed(&f,"G#X 3,2,0,7,"); local_ms+=5000; CHECK(st_cterm_poll(f.terminal)==0 && !m->configuring && !m->enabled && m->cr==7);
	feed(&f,"G#X 3,2,1,9,10000:"); CHECK(m->enabled==1 && m->cr==9 && !m->configuring);
	feed(&f,"G#X 10,21,25:"); CHECK(igs->draw_x==21 && igs->draw_y==25);
	/* A frozen random operation/selector is never evaluated a second time. */
	igs->math[3]=3; feed(&f,"G#X 3],2,1,"); igs->math[3]=10; feed(&f,"0,1:AB"); CHECK(m->bytes[0]=='A');
	igs->random_seed=1; igs->random_range[0][0]=0; igs->random_range[0][1]=255;
	feed(&f,"G#X 3,1,r:"); CHECK(m->enabled==1 && m->cr==230 && igs->random_seed==UINT32_C(0xbb40e62e));
	igs->random_seed=1; feed(&f,"G#X 3,r:"); CHECK(m->enabled==0 && igs->random_seed==UINT32_C(0xbb40e62e));
	/* Separately received local Ctrl-C/X wait through X3's raw service;
	 * they never become a raw-reader abort or an immediate remote send. */
	feed(&f,"G#X 3,2,0,0,2:");
	CHECK(st_cterm_handle_key(f.terminal,3,0) && st_cterm_handle_codepage_key(f.terminal,24));
	CHECK(key_count==0 && igs->input.key_count==2);
	CHECK(write_bytes(&f,"AB!",3)==3 && m->bytes[0]=='A' && m->bytes[1]=='B' && !m->loading);
	CHECK(key_count==2 && keys[0]==3 && keys[1]==24 && igs->input.key_count==0); key_count=0;
	/* Every auxiliary slot, including the discarded one, times out alone. */
	feed(&f,"G#X 3,2,0,0,2:");
	for(unsigned i=1;i<=3;++i) { local_ms=m->deadline; CHECK(st_cterm_poll(f.terminal)==0 && m->received==i); }
	CHECK(!m->loading && m->bytes[0]==' ' && m->bytes[1]==' ');
	/* Output readiness precedes the FIRST auxiliary receive deadline. */
	igs->loop_mode=1; slow_flow=true; feed(&f,"G#X 3,2,0,0,1:"); slow_flow=false;
	CHECK(m->deadline==local_ms+5000 && reply_count==1 && replies[0]==17); reply_count=0;
	CHECK(write_bytes(&f,"a!",2)==2 && m->bytes[0]=='a');
	/* Only an exact loop==1 calls XON, even for an enable=0 load. */
	static const int loops[]={-1,0,1,2};
	for(size_t i=0;i<4;++i) {
		igs->loop_mode=loops[i]; load(&f,0,0,1,(uint8_t[]){3,24},2);
		CHECK(reply_count==(loops[i]==1?1u:0u)); if(reply_count) CHECK(replies[0]==17); reply_count=0;
	}
	igs->loop_mode=0; feed(&f,"G#I 1:"); CHECK(m->bytes[0]==3 && m->length==1);
	feed(&f,"G#R 1,0:"); local_ms=igs->wait_deadline; CHECK(st_cterm_poll(f.terminal)==0 && m->bytes[0]==3);
	cterm_igs_reset(igs); CHECK(!m->enabled && !m->length && !m->cr && m->bytes[0]==0);
	destroy(&f);
}
static void holds(void)
{
	struct fixture f; setup(&f); struct cterm_igs *igs=f.terminal->igs_engine; struct cterm_igs_input *m=&igs->input;
	load(&f,1,1,3,(uint8_t[]){0,0xff,27,':'},4);
	CHECK(st_cterm_set_encoding(f.terminal,ST_CTERM_ENCODING_UTF8,ST_CODEPAGE_CP437)==0);
	CHECK(!mouse(&f,5) && key_count==0); (void)mouse(&f,0);
	CHECK(mouse(&f,4) && key_count==4 && memcmp(keys,(uint8_t[]){0,255,27,13},4)==0 && cterm_igs_paused(igs));
	CHECK(mouse(&f,4) && key_count==4); CHECK(mouse(&f,1) && m->held); CHECK(mouse(&f,5) && m->held);
	CHECK(write_bytes(&f,"G#X 10,91,92:",13)==0 && igs->draw_x!=91);
	CHECK(st_cterm_handle_text_key(f.terminal,0xe9)); CHECK(st_cterm_handle_text_key(f.terminal,0x212b)); CHECK(st_cterm_handle_codepage_key(f.terminal,0xff));
	CHECK(st_cterm_handle_key(f.terminal,'a',0)); CHECK(key_count==4 && igs->input.key_count==4);
	release(&f); CHECK(key_count==10 && memcmp(keys+4,(uint8_t[]){0xc3,0xa9,0xc3,0x85,0xff,'a'},6)==0 && igs->input.key_count==0);
	feed(&f,"G#X 10,91,92:"); CHECK(igs->draw_x==91); key_count=0;
	/* Suppressed/UI presses don't become late clicks at command completion. */
	st_cterm_sync_mouse_buttons(f.terminal,4); feed(&f,"G#X 10,93,94:"); CHECK(key_count==0);
	CHECK(!mouse(&f,4)); st_cterm_sync_mouse_buttons(f.terminal,0);
	/* Sample while a raw command is active; consume its suffix after release. */
	feed(&f,"G#X 3,2,1,0,1:"); CHECK(mouse(&f,4) && key_count==0);
	const char wire[]="ABG#X 10,95,96:";
	CHECK(write_bytes(&f,wire,sizeof(wire)-1)==2 && m->held && m->suspended && key_count==1 && keys[0]=='A');
	CHECK(write_bytes(&f,wire+2,sizeof(wire)-3)==0); release(&f);
	CHECK(write_bytes(&f,wire+2,sizeof(wire)-3)==sizeof(wire)-3 && igs->draw_x==95); key_count=0;
	/* Releasing outside the terminal still finishes a hold. */
	CHECK(mouse(&f,4));
	struct st_console_event outside={.type=ST_CONSOLE_EVENT_MOUSE};
	outside.data.mouse.action=ST_CONSOLE_MOUSE_BUTTON;
	outside.data.mouse.x=1000; outside.data.mouse.y=1000;
	outside.data.mouse.pixel_x=10000; outside.data.mouse.pixel_y=10000;
	CHECK(st_cterm_handle_mouse(f.terminal,&outside) && !m->held); key_count=0;
	/* Pending button samples wait through an existing q pause. */
	feed(&f,"G#q 1:"); CHECK(mouse(&f,4) && key_count==0);
	local_ms=igs->wait_deadline; CHECK(st_cterm_poll(f.terminal)==0 && key_count==1 && m->held); release(&f); key_count=0;
	/* Current buffer/length/CR are reloaded per send; enabled gates once. */
	m->enabled=1; m->length=1; m->bytes[0]='Z'; m->cr=0; live=igs; callback_action=1;
	CHECK(mouse(&f,4) && key_count==4 && memcmp(keys,(uint8_t[]){'Z',255,0,13},4)==0); live=NULL; release(&f); key_count=0;
	m->enabled=2; CHECK(!mouse(&f,4) && key_count==0); release(&f); m->enabled=1;
	/* A release inside output neither reenters the parser nor sticks a hold. */
	live=igs; callback_action=2; CHECK(mouse(&f,4) && key_count==4 && !m->held); live=NULL; key_count=0;
	live=igs; callback_action=3; CHECK(mouse(&f,4) && key_count==1 && !m->enabled && !m->held); live=NULL;
	load(&f,1,0,1,(uint8_t[]){'a','b'},2); CHECK(mouse(&f,4) && m->held);
	CHECK(st_cterm_finish(f.terminal)==0 && !m->held && !m->suspended && m->enabled==1 && m->bytes[0]=='a');
	/* A corrupted length is bounded by the real object, without adjacent RAM. */
	m->length=81; m->buttons=2; key_count=0;
	CHECK(cterm_igs_input_activate(igs)==IO_ERROR_OUT_OF_RANGE && key_count==80); release(&f);
	m->length=255; m->cr=1; m->buttons=2; key_count=0;
	CHECK(cterm_igs_input_activate(igs)==0 && key_count==1 && keys[0]==13); release(&f);
	destroy(&f);
}
static void codepage_replay(void)
{
	struct fixture f; setup(&f);
	CHECK(st_cterm_set_encoding(f.terminal,ST_CTERM_ENCODING_CODEPAGE,ST_CODEPAGE_CP437)==0);
	load(&f,1,0,1,(uint8_t[]){'a','b'},2);
	CHECK(mouse(&f,4) && key_count==1 && keys[0]=='a');
	CHECK(st_cterm_handle_text_key(f.terminal,0x212b) && key_count==1);
	release(&f);
	CHECK(key_count==2 && keys[1]==0x8f); /* ANGSTROM SIGN -> A RING -> CP437. */
	destroy(&f);
}
static void partitions(void)
{
	static const uint8_t wire[]={'G','#','X','>','3',',','2',',','0',',','0',',','1','2',':',
	    0,13,10,'@','G','#',':',17,19,3,24,255,'!', 'X',' ','1','0',',','1','9',',','1','7',':'};
	for(unsigned resolution=0;resolution<3;++resolution) for(unsigned status=0;status<2;++status) {
		struct fixture f; create(&f,resolution,status!=0); struct cterm_igs *igs=f.terminal->igs_engine;
		f.terminal->keystroke_cb=output; f.terminal->response_cb=response; key_count=reply_count=0;
		feed(&f,"\033fG#p 3,2:");
		const struct st_console_rect rect={0,0,igs->width,igs->height}; seed(&f,&rect,769,768);
		uint32_t palette[16]; memcpy(palette,igs->palette,sizeof(palette)); struct cterm_igs_attributes attrs=igs->attributes;
		for(unsigned encoding=0;encoding<3;++encoding) {
			CHECK(st_cterm_set_encoding(f.terminal,encoding==2?ST_CTERM_ENCODING_UTF8:ST_CTERM_ENCODING_CODEPAGE,
			    encoding==0?ST_CODEPAGE_ATARI_ST:ST_CODEPAGE_CP437)==0);
			for(size_t split=0;split<=sizeof(wire);++split) {
				CHECK(write_bytes(&f,wire,split)==split && write_bytes(&f,wire+split,sizeof(wire)-split)==sizeof(wire)-split);
				CHECK(!igs->input.enabled && igs->input.length==12 && !igs->input.cr && memcmp(igs->input.bytes,wire+15,12)==0);
				CHECK(igs->draw_x==19 && igs->draw_y==17 && key_count==0 && reply_count==0);
			}
			for(size_t i=0;i<sizeof(wire);++i) CHECK(write_bytes(&f,wire+i,1)==1);
		}
		CHECK(memcmp(&attrs,&igs->attributes,sizeof(attrs))==0 && memcmp(palette,igs->palette,sizeof(palette))==0);
		CHECK(f.terminal->xpos==4 && f.terminal->ypos==3);
		struct st_pixel_buffer *p=NULL; CHECK(st_console_get_pixels(&p,f.console,&rect,false)==0);
		for(size_t i=0;i<(size_t)igs->width*igs->height;++i) CHECK(p->primary[i]==769 && p->alternate[i]==768);
		st_pixel_buffer_destroy(p); destroy(&f);
	}
}
int main(void) { stores(); holds(); codepage_replay(); partitions(); return 0; }
