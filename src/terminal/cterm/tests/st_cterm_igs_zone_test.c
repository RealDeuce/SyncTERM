/* X4: independent literal zones, incremental stores and raw ownership. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "igs_test_fixture.h"
static uint8_t keys[128], replies[128];
static size_t key_count, reply_count;
static struct cterm_igs *live;
static bool slow_flow, reset_flow;
static void output(const char *bytes,size_t size,void *context)
{
	(void)context; CHECK(size<=sizeof(keys)-key_count); memcpy(keys+key_count,bytes,size); key_count+=size;
}
static void response(const char *bytes,size_t size,void *context)
{
	(void)context; CHECK(size<=sizeof(replies)-reply_count); memcpy(replies+reply_count,bytes,size); reply_count+=size;
	if(slow_flow) local_ms+=5001;
	if(reset_flow) cterm_igs_reset(live);
}
static void setup(struct fixture *f)
{
	create(f,0,false); f->terminal->response_cb=response; f->terminal->keystroke_cb=output;
	key_count=reply_count=0; slow_flow=reset_flow=false; live=f->terminal->igs_engine;
	const struct st_console_rect rect={0,0,8,8}; seed(f,&rect,768,768);
}
static size_t write_bytes(struct fixture *f,const void *bytes,size_t size)
{
	size_t used; CHECK(st_cterm_write(f->terminal,bytes,size,&used)==0); return used;
}
static void load(struct fixture *f,int zone,const int coordinates[4],int length,const uint8_t *bytes,size_t size)
{
	struct cterm_igs *igs=f->terminal->igs_engine;
	igs->math[0]=(int16_t)zone;
	for(unsigned i=0;i<4;++i) igs->math[i+1]=(int16_t)coordinates[i];
	igs->math[5]=(int16_t)length;
	feed(f,"G#X 4,0],1],2],3],4],5]:"); CHECK(write_bytes(f,bytes,size)==size);
}
static void equal_rectangle(const struct cterm_igs_zone *zone,const int expected[4])
{
	for(unsigned i=0;i<4;++i) CHECK(zone->rectangle[i]==expected[i]);
}
static void stores(void)
{
	struct fixture f; setup(&f); struct cterm_igs *igs=f.terminal->igs_engine; struct cterm_igs_input *input=&igs->input;
	CHECK(input->zones!=NULL && input->zone_loopback==0);
	for(unsigned i=0;i<48;++i) {
		CHECK(input->zones[i].length==0); equal_rectangle(&input->zones[i],(int[]){0,0,0,0});
		for(unsigned j=0;j<80;++j) CHECK(input->zones[i].bytes[j]==0);
	}
	for(int i=0;i<48;++i) {
		int coordinates[]={-32768,i,32767,-i}; uint8_t bytes[81];
		for(unsigned j=0;j<sizeof(bytes);++j) bytes[j]=(uint8_t)(i+j);
		load(&f,i,coordinates,80,bytes,81); equal_rectangle(&input->zones[i],coordinates);
		CHECK(input->zones[i].length==80 && memcmp(input->zones[i].bytes,bytes,80)==0);
	}
	struct cterm_igs_zone before[48]; memcpy(before,input->zones,sizeof(before));
	feed(&f,"G#X>4,9998:X 4,9999:"); CHECK(input->zone_loopback==1 && reply_count==0 && key_count==0);
	for(unsigned i=0;i<48;++i) {
		equal_rectangle(&input->zones[i],(int[]){-1,-1,-1,-1});
		CHECK(input->zones[i].length==80 && memcmp(input->zones[i].bytes,before[i].bytes,80)==0);
	}
	feed(&f,"G#X 4,9997:"); CHECK(input->zone_loopback==0);
	static const int words[]={-32768,-256,-200,-1,0,1,79,80,81,127,128,255,256,32767};
	static const unsigned lengths[]={1,1,1,1,1,1,79,80,80,80,80,80,80,80};
	uint8_t bytes[81]; for(unsigned i=0;i<sizeof(bytes);++i) bytes[i]=(uint8_t)(255-i);
	for(size_t i=0;i<sizeof(words)/sizeof(*words);++i) {
		load(&f,13,(int[]){9999,0,-1,-32768},words[i],bytes,lengths[i]+1);
		CHECK(input->zones[13].length==lengths[i] && memcmp(input->zones[13].bytes,bytes,lengths[i])==0);
	}
	memcpy(before,input->zones,sizeof(before));
	/* Every raw byte value is data, not a command, delimiter or local abort. */
	for(unsigned base=0;base<256;base+=64) {
		for(unsigned i=0;i<sizeof(bytes);++i) bytes[i]=(uint8_t)(base+i);
		load(&f,7,(int[]){1,2,3,4},80,bytes,81); CHECK(memcmp(input->zones[7].bytes,bytes,80)==0);
	}
	uint8_t suffix[79]; memcpy(suffix,input->zones[7].bytes+1,79);
	load(&f,7,(int[]){4,3,2,1},1,(uint8_t[]){0,255},2);
	CHECK(input->zones[7].bytes[0]==0 && memcmp(input->zones[7].bytes+1,suffix,79)==0);
	for(unsigned i=0;i<48;++i) if(i!=7) CHECK(memcmp(&input->zones[i],&before[i],sizeof(before[i]))==0);
	/* Other nonnegative zone selectors clamp to47; special words don't load. */
	static const int selectors[]={48,9996,10000,32767};
	for(size_t i=0;i<4;++i) {
		load(&f,selectors[i],(int[]){3,4,1,2},1,(uint8_t[]){'Q','!'},2);
		equal_rectangle(&input->zones[47],(int[]){3,4,1,2}); CHECK(input->zones[47].bytes[0]=='Q');
	}
	CHECK(reply_count==0 && key_count==0 && f.terminal->xpos==1 && f.terminal->ypos==1);
	memcpy(before,input->zones,sizeof(before));
	/* Each corner is committed before the next reader, not at command end. */
	feed(&f,"G#X 4,5,11,"); CHECK(input->zone_configuring && input->zones[5].rectangle[0]==11 && input->zones[5].rectangle[1]==-1);
	feed(&f,"22,"); CHECK(input->zones[5].rectangle[1]==22);
	CHECK(st_cterm_finish(f.terminal)==0 && !input->zone_configuring && input->zones[5].rectangle[0]==11);
	feed(&f,"G#X 4,5,31,32,33,34,3:"); CHECK(input->zone_loading && !input->loading && input->zones[5].length==3);
	CHECK(write_bytes(&f,"z",1)==1 && input->zones[5].bytes[0]=='z' && input->zones[5].bytes[1]==before[5].bytes[1]);
	CHECK(st_cterm_finish(f.terminal)==0 && !input->zone_loading && input->zones[5].bytes[1]==before[5].bytes[1]);
	feed(&f,"G#X 4,5,41,42,"); local_ms+=5000; CHECK(st_cterm_poll(f.terminal)==0 && !input->zone_configuring);
	equal_rectangle(&input->zones[5],(int[]){41,42,33,34});
	feed(&f,"G#X 4,5,51,52,10000:G#X 10,91,92:"); CHECK(!input->zone_configuring && igs->draw_x==91);
	equal_rectangle(&input->zones[5],(int[]){51,52,33,34});
	memcpy(before,input->zones,sizeof(before));
	static const int rejected[]={-32768,-4096,-1};
	for(size_t i=0;i<3;++i) {
		igs->math[0]=(int16_t)rejected[i]; feed(&f,"G#X 4,0]:G#X 10,93,94:");
		CHECK(memcmp(before,input->zones,sizeof(before))==0 && !input->zone_configuring && igs->draw_x==93);
	}
	/* Field prefixes survive changes to source scalars and RNG state. */
	igs->math[9]=4; igs->math[10]=6; feed(&f,"G#X 9],10],"); igs->math[9]=10; igs->math[10]=47;
	feed(&f,"1,2,3,4,1:AB"); equal_rectangle(&input->zones[6],(int[]){1,2,3,4}); CHECK(input->zones[6].bytes[0]=='A');
	igs->random_seed=1; igs->random_range[0][0]=0; igs->random_range[0][1]=255;
	feed(&f,"G#X 4,r,1,2,3,4,1:AB"); CHECK(input->zones[47].bytes[0]=='A' && igs->random_seed==UINT32_C(0xbb40e62e));
	igs->random_seed=1; feed(&f,"G#X 4,0,r,"); CHECK(input->zones[0].rectangle[0]==230 && igs->random_seed==UINT32_C(0xbb40e62e));
	feed(&f,"2,3,4,1:AB"); CHECK(igs->random_seed==UINT32_C(0xbb40e62e));
	/* Raw helper deadlines, including the discard, substitute space alone. */
	feed(&f,"G#X 4,5,1,2,3,4,2:");
	for(unsigned i=1;i<=3;++i) { local_ms=input->zone_deadline; CHECK(st_cterm_poll(f.terminal)==0 && input->zone_received==i); }
	CHECK(!input->zone_loading && input->zones[5].bytes[0]==' ' && input->zones[5].bytes[1]==' ');
	feed(&f,"G#X 4,5,1,2,3,4,2:"); CHECK(st_cterm_handle_key(f.terminal,3,0) && st_cterm_handle_codepage_key(f.terminal,24));
	CHECK(key_count==0 && input->key_count==2); CHECK(write_bytes(&f,"AB!",3)==3);
	CHECK(key_count==2 && keys[0]==3 && keys[1]==24 && !input->zone_loading && input->zones[5].bytes[1]=='B'); key_count=0;
	/* Exact loop1/XON guard and first deadline AFTER output readiness. */
	static const int loops[]={-1,0,1,2};
	for(size_t i=0;i<4;++i) {
		igs->loop_mode=loops[i]; load(&f,5,(int[]){1,2,3,4},1,(uint8_t[]){'A','!'},2);
		CHECK(reply_count==(loops[i]==1?1u:0u)); if(reply_count) CHECK(replies[0]==17); reply_count=0;
		feed(&f,"G#X>4,9998:X>4,9999:X 4,9997:"); CHECK(reply_count==0 && input->zone_loopback==0);
	}
	igs->loop_mode=1; slow_flow=true; feed(&f,"G#X 4,5,1,2,3,4,1:"); slow_flow=false;
	CHECK(reply_count==1 && replies[0]==17 && input->zone_deadline==local_ms+5000); reply_count=0;
	CHECK(write_bytes(&f,"a!",2)==2 && input->zones[5].bytes[0]=='a'); igs->loop_mode=0;
	memcpy(before,input->zones,sizeof(before));
	feed(&f,"G#I 1:"); CHECK(memcmp(before,input->zones,sizeof(before))==0);
	feed(&f,"G#R 1,0:"); local_ms=igs->wait_deadline; CHECK(st_cterm_poll(f.terminal)==0 && memcmp(before,input->zones,sizeof(before))==0);
	igs->loop_mode=1; reset_flow=true; feed(&f,"G#X 4,5,1,2,3,4,1:"); reset_flow=false;
	CHECK(!input->zone_loading && !input->zone_configuring && input->zone_loopback==0 && input->zones[5].length==0);
	for(unsigned i=0;i<48;++i) equal_rectangle(&input->zones[i],(int[]){0,0,0,0});
	destroy(&f);
}
static void macro_coexistence(void)
{
	struct fixture f; setup(&f); struct cterm_igs *igs=f.terminal->igs_engine;
	feed(&f,"G#X 3,2,1,1,1:Z!G#X 4,0,1,2,3,4,1:");
	struct st_console_event event={.type=ST_CONSOLE_EVENT_MOUSE};
	event.data.mouse.action=ST_CONSOLE_MOUSE_BUTTON; event.data.mouse.buttons=ST_CONSOLE_MOUSE_BUTTON_RIGHT;
	event.data.mouse.x=1; event.data.mouse.y=1; event.data.mouse.pixel_x=8; event.data.mouse.pixel_y=8;
	CHECK(st_cterm_handle_mouse(f.terminal,&event) && key_count==0);
	CHECK(st_cterm_handle_text_key(f.terminal,'k') && igs->input.key_count==1);
	const char wire[]="ABG#X 10,19,17:";
	CHECK(write_bytes(&f,wire,sizeof(wire)-1)==2 && igs->input.held && igs->input.suspended);
	CHECK(key_count==2 && memcmp(keys,"Z\r",2)==0 && !igs->input.zone_loading && igs->input.zones[0].bytes[0]=='A');
	CHECK(write_bytes(&f,wire+2,sizeof(wire)-3)==0);
	event.data.mouse.buttons=0; CHECK(st_cterm_handle_mouse(f.terminal,&event));
	CHECK(key_count==3 && keys[2]=='k' && !cterm_igs_paused(igs));
	CHECK(write_bytes(&f,wire+2,sizeof(wire)-3)==sizeof(wire)-3 && igs->draw_x==19 && igs->draw_y==17);
	CHECK(igs->input.enabled==1 && igs->input.length==1 && igs->input.cr==1 && igs->input.bytes[0]=='Z');
	destroy(&f);
}
static void profiles(void)
{
	static const uint8_t prefix[]="G#X>4,0,1,2,3,4,12:";
	static const uint8_t wire[]="G#X>4,0,1,2,3,4,12:" "\0\r\n@G#:\x11\x13\x03\x18\xff" "!" "X 10,19,17:";
	static const int normalized[3][4]={{319,100,160,199},{639,100,320,199},{639,200,320,399}};
	for(unsigned resolution=0;resolution<3;++resolution) for(unsigned status=0;status<2;++status) {
		struct fixture f; create(&f,resolution,status!=0); struct cterm_igs *igs=f.terminal->igs_engine;
		f.terminal->response_cb=response; f.terminal->keystroke_cb=output; key_count=reply_count=0;
		feed(&f,"\033fG#p 3,2:");
		const struct st_console_rect rect={0,0,igs->width,igs->height}; seed(&f,&rect,769,768);
		uint32_t palette[16]; memcpy(palette,igs->palette,sizeof(palette)); struct cterm_igs_attributes attributes=igs->attributes;
		for(unsigned encoding=0;encoding<3;++encoding) {
			CHECK(st_cterm_set_encoding(f.terminal,encoding==2?ST_CTERM_ENCODING_UTF8:ST_CTERM_ENCODING_CODEPAGE,
			    encoding==0?ST_CODEPAGE_ATARI_ST:ST_CODEPAGE_CP437)==0);
			igs->scaling_mode=0;
			for(size_t split=0;split<sizeof(wire);++split) {
				CHECK(write_bytes(&f,wire,split)==split && write_bytes(&f,wire+split,sizeof(wire)-1-split)==sizeof(wire)-1-split);
				CHECK(igs->input.zones[0].length==12 && memcmp(igs->input.zones[0].bytes,wire+sizeof(prefix)-1,12)==0);
				CHECK(!igs->input.zone_loading && !igs->input.zone_configuring && igs->draw_x==19 && igs->draw_y==17);
			}
			for(size_t i=0;i<sizeof(wire)-1;++i) CHECK(write_bytes(&f,wire+i,1)==1);
			for(int mode=0;mode<3;++mode) for(int captured=0;captured<3;++captured) {
				igs->scaling_mode=mode; igs->scaling_resolution=captured;
				feed(&f,"G#X 4,2,9999,5000,5000,9999,1:AB");
				int expected[]={9999,5000,5000,9999};
				if(mode==1) memcpy(expected,normalized[captured],sizeof(expected));
				if(mode==2 && captured==2) { expected[1]=10000; expected[3]=19998; }
				equal_rectangle(&igs->input.zones[2],expected);
			}
		}
		CHECK(key_count==0 && reply_count==0 && f.terminal->xpos==4 && f.terminal->ypos==3);
		CHECK(memcmp(palette,igs->palette,sizeof(palette))==0 && memcmp(&attributes,&igs->attributes,sizeof(attributes))==0);
		struct st_pixel_buffer *p=NULL; CHECK(st_console_get_pixels(&p,f.console,&rect,false)==0);
		for(size_t i=0;i<(size_t)igs->width*igs->height;++i) CHECK(p->primary[i]==769 && p->alternate[i]==768);
		st_pixel_buffer_destroy(p); destroy(&f);
	}
}
int main(void) { stores(); macro_coexistence(); profiles(); return 0; }
