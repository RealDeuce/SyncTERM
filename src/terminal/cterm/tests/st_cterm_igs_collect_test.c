/* Independent < collection, echo, mouse zones and deferred-reply cases. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "igs_test_fixture.h"
static uint8_t output[2048]; static size_t output_count;
static struct cterm_igs *live; static bool reset_output, mutate_output, poll_output;
static int sent_mode;
static void sent(const char *bytes,size_t size,void *context)
{
	(void)context; CHECK(size<=sizeof(output)-output_count);
	memcpy(output+output_count,bytes,size); output_count+=size;
	if(live!=NULL) {
		sent_mode=live->attributes.writing_mode;
		CHECK(!live->collect.pending);
		if(reset_output) cterm_igs_reset(live);
		if(poll_output) {poll_output=false;CHECK(cterm_igs_poll(live)==0);}
		if(mutate_output) {live->collect.count=1; live->collect.append_cr=1;}
	}
}
static void setup(struct fixture *f,unsigned resolution,bool status)
{
	create(f,resolution,status); f->terminal->keystroke_cb=sent;
	output_count=0; live=NULL; reset_output=mutate_output=poll_output=false;
	const struct st_console_rect rect={0,0,f->terminal->igs_engine->width,f->terminal->igs_engine->height};
	seed(f,&rect,769,768);
}
static void expect(const void *bytes,size_t count)
{
	CHECK(output_count==count && memcmp(output,bytes,count)==0); output_count=0;
}
static void key(struct fixture *f,int value)
{
	CHECK(cterm_igs_handle_key(f->terminal->igs_engine,&(struct cterm_igs_key){.kind=CTERM_IGS_KEY_NORMAL,.value=(uint16_t)value,.local=value}));
	CHECK(st_cterm_poll(f->terminal)==0);
}
static bool mouse(struct fixture *f,int x,int y,unsigned buttons)
{
	struct st_console_event e={.type=ST_CONSOLE_EVENT_MOUSE}; e.data.mouse.action=ST_CONSOLE_MOUSE_BUTTON;
	e.data.mouse.pixel_x=x; e.data.mouse.pixel_y=y; e.data.mouse.x=x/8; e.data.mouse.y=y/8;
	e.data.mouse.buttons=buttons; return st_cterm_handle_mouse(f->terminal,&e);
}
static void unchanged_at(struct fixture *f,int line)
{
	struct cterm_igs *igs=f->terminal->igs_engine; struct st_pixel_buffer *p=NULL;
	const struct st_console_rect rect={0,0,igs->width,igs->height};
	CHECK(st_console_get_pixels(&p,f->console,&rect,false)==0);
	for(size_t i=0;i<(size_t)igs->width*igs->height;++i) {
		if(p->primary[i]!=769 || p->alternate[i]!=768) fprintf(stderr,"scene line%d mode%u pixel%zu,%zu: %u/%u\n",line,igs->resolution,i%igs->width,i/igs->width,p->primary[i],p->alternate[i]);
		CHECK(p->primary[i]==769 && p->alternate[i]==768);
	}
	st_pixel_buffer_destroy(p);
}
#define unchanged(f) unchanged_at(f,__LINE__)
static void keyboard_cases(void)
{
	struct fixture f; setup(&f,0,false); struct cterm_igs *igs=f.terminal->igs_engine;
	static const int types[]={-32768,-1,0};
	for(size_t i=0;i<sizeof(types)/sizeof(*types);++i) for(unsigned value=0;value<256;++value) {
		igs->math[0]=(int16_t)types[i]; feed(&f,"G#< 0,0],0:"); CHECK(igs->collect.active);
		key(&f,(int)value);
		if(value>31 && value<127) {uint8_t b=(uint8_t)value; expect(&b,1);} else expect("",0);
		CHECK(!igs->collect.active && !igs->collect.pending && igs->collect.count==(value>31 && value<127?1u:0u));
	}
	/* Signed words are compared intact; controls never abort a string. */
	feed(&f,"G#< 1,1,0:");
	static const int ignored[]={0,3,24,10,27,0x0141,0x010d,-1,-32768,32767};
	for(size_t i=0;i<sizeof(ignored)/sizeof(*ignored);++i) {key(&f,ignored[i]);CHECK(igs->collect.active && output_count==0);}
	CHECK(st_cterm_handle_text_key(f.terminal,0x10041)); CHECK(igs->collect.count==0);
	key(&f,'A'); key(&f,13); expect("A\r",2);
	/* Host Delete must translate to the native127 ASCII result. */
	feed(&f,"G#< 0,1,0:");key(&f,'A');key(&f,'B');
	CHECK(st_cterm_handle_key(f.terminal,ST_CONSOLE_KEY_DELETE,0));CHECK(igs->collect.count==1);
	CHECK(st_cterm_handle_key(f.terminal,ST_CONSOLE_KEY_DELETE,ST_CONSOLE_MODIFIER_CONTROL));CHECK(igs->collect.count==1);
	CHECK(st_cterm_handle_key(f.terminal,ST_CONSOLE_KEY_DELETE,ST_CONSOLE_MODIFIER_SHIFT));CHECK(igs->collect.count==0);
	key(&f,13);expect("",0);
	/* Clamped Delete at empty, retained suffix, exact129-byte boundary. */
	feed(&f,"G#< 0,1,0:"); key(&f,127); CHECK(igs->collect.count==0);
	for(size_t i=0;i<150;++i) key(&f,'x'); CHECK(igs->collect.count==129);
	key(&f,127); key(&f,8); key(&f,'Y'); key(&f,'Z'); key(&f,'!'); key(&f,13);
	CHECK(output_count==129); for(size_t i=0;i<127;++i) CHECK(output[i]=='x');
	CHECK(output[127]=='Y' && output[128]=='Z'); output_count=0;
	for(int option=0;option<=3;++option) {
		char wire[32]; snprintf(wire,sizeof(wire),"G#< 1,1,%d:",option); feed(&f,wire);
		key(&f,'A');key(&f,'B');key(&f,127);key(&f,'C');key(&f,13);
		if(option<2) expect("AC\r",3); else expect("",0);
		CHECK(igs->collect.count==2 && igs->collect.bytes[0]=='A' && igs->collect.bytes[1]=='C');
	}
	/* Type is fixed before the option reader; negative options still echo. */
	igs->math[0]=0;feed(&f,"G#< 0,0],");igs->math[0]=2;feed(&f,"0:");key(&f,'A');expect("A",1);
	igs->math[0]=-1;feed(&f,"G#p>3,2:< 0,1,0]:");key(&f,'A');key(&f,13);expect("A",1);
	CHECK(f.terminal->xpos==5 && f.terminal->ypos==3);
	struct st_console_cell cell;const struct st_console_rect one={3,2,1,1};
	CHECK(st_console_read_cells(f.console,&one,&cell,1)==0 && cell.glyph=='A');
	/* Any nonzero low byte appends CR;256 has a zero low byte. */
	for(int cr=255;cr<=257;++cr) {
		char wire[32];snprintf(wire,sizeof(wire),"G#< %d,0,0:",cr);feed(&f,wire);key(&f,3);
		expect("\r",cr==256?0:1);
	}
	destroy(&f);
}
static void chains_and_lifetime(void)
{
	struct fixture f; setup(&f,0,false); struct cterm_igs *igs=f.terminal->igs_engine;
	feed(&f,"G#<>1,0,0:");key(&f,'A'); CHECK(!igs->collect.active && igs->collect.pending && output_count==0);
	feed(&f,"M>4:<>0,0,0:"); key(&f,'B');CHECK(output_count==0 && igs->collect.count==1);
	live=igs; feed(&f,"M 2:");expect("B",1);CHECK(sent_mode==2);live=NULL;
	/* A later discard replaces a previously pending collection. */
	feed(&f,"G#<>1,0,0:"); key(&f,'A');feed(&f,"< 1,0,3:");key(&f,'B');expect("",0);
	/* Partial second < stores its CR once, without a second RNG read. */
	feed(&f,"G#<>0,0,0:");key(&f,'A');igs->random_seed=1;
	feed(&f,"< r,");uint32_t rng=igs->random_seed;CHECK(igs->collect.configuring);
	feed(&f,"0,0:");CHECK(igs->random_seed==rng);key(&f,'B');expect("B\r",2);
	/* A pending reply survives an asynchronous tail, before queued keys. */
	feed(&f,"G#<>1,0,0:");key(&f,'C');feed(&f,"? 2,1:");
	CHECK(st_cterm_handle_text_key(f.terminal,'z'));CHECK(output_count==0);
	CHECK(mouse(&f,20,20,1));CHECK(mouse(&f,21,21,0));expect("C\rz",3);
	/* Unknown final command still exits the chain and sends once. */
	feed(&f,"G#<>0,0,0:");key(&f,'D');feed(&f,"~ ");expect("D",1);
	/* Callback reset stops before remaining bytes/CR, and pending is already0. */
	feed(&f,"G#< 1,1,0:");key(&f,'A');key(&f,'B');live=igs;reset_output=true;key(&f,13);
	expect("A",1);CHECK(!igs->collect.active && !igs->collect.pending && igs->collect.count==0);reset_output=false;live=NULL;
	/* Transport callbacks observe current count and CR, never a snapshot. */
	feed(&f,"G#< 0,1,0:");key(&f,'A');key(&f,'B');live=igs;mutate_output=true;key(&f,13);
	expect("A\r",2);mutate_output=false;live=NULL;
	feed(&f,"G#< 1,1,1:");key(&f,'x'); CHECK(st_cterm_finish(f.terminal)==0 && !igs->collect.active && output_count==0);
	feed(&f,"G#<>1,0,0:");key(&f,'y');CHECK(st_cterm_finish(f.terminal)==0 && !igs->collect.pending && output_count==0);
	feed(&f,"G#< 1,");CHECK(st_cterm_finish(f.terminal)==0 && !igs->collect.configuring);
	feed(&f,"G#< 1,");local_ms+=5000;CHECK(st_cterm_poll(f.terminal)==0 && !igs->collect.configuring);
	destroy(&f);
}
static void zones(void)
{
	for(unsigned resolution=0;resolution<3;++resolution) for(unsigned status=0;status<2;++status) {
		struct fixture f;setup(&f,resolution,status!=0);struct cterm_igs *igs=f.terminal->igs_engine;
		feed(&f,"G#X 4,9999:");
		/* Raw payload contains a NUL; write it explicitly, not through strlen. */
		static const uint8_t load[]="G#X 4,0,10,20,30,40,3:A\0B!";
		size_t used; CHECK(st_cterm_write(f.terminal,load,sizeof(load)-1,&used)==0 && used==sizeof(load)-1);
		feed(&f,"G#X>4,1,10,20,30,40,1:Z!X 4,47,100,100,110,110,1:D!");
		igs->attributes.writing_mode=2;igs->draw_x=31;igs->draw_y=32;
		static const int styles[]={2,3,4,5,6,7,8,9,10,11,32767};
		for(size_t i=0;i<sizeof(styles)/sizeof(*styles);++i) {
			(void)mouse(&f,50,60,0);igs->math[0]=(int16_t)styles[i];feed(&f,"G#< 1,0],0:");
			CHECK(igs->collect.active && st_cterm_mouse_mode(f.terminal)==3 && igs->attributes.writing_mode==3);
			CHECK(mouse(&f,30,40,5) && igs->collect.release);CHECK(mouse(&f,100,100,1));
			CHECK(output_count==0);CHECK(mouse(&f,101,101,0));expect((uint8_t[]){'A',0,'B',13},4);
			CHECK(igs->attributes.writing_mode==2 && igs->draw_x==31 && igs->draw_y==32);unchanged(&f);
		}
		/* Default miss returns zone47, strict miss retries without a reply. */
		feed(&f,"G#< 0,2,0:");CHECK(mouse(&f,70,70,1));CHECK(mouse(&f,70,70,0));expect("D",1);
		feed(&f,"G#X 4,9998:");feed(&f,"G#< 0,3,0:");CHECK(mouse(&f,70,70,4));CHECK(mouse(&f,70,70,0));
		CHECK(igs->collect.active && !igs->collect.release && output_count==0 && igs->attributes.writing_mode==3);
		CHECK(st_cterm_handle_text_key(f.terminal,'q'));CHECK(igs->collect.active);
		/* A transport callback must not drain queued keys between reply bytes. */
		live=igs;poll_output=true;
		CHECK(mouse(&f,10,20,4));CHECK(mouse(&f,200,180,0));expect("A\0Bq",4);live=NULL;unchanged(&f);
		/* All48 zone indices, press coordinates and binary lengths compose. */
		igs->input.zone_loopback=0;
		for(size_t z=0;z<CTERM_IGS_ZONES;++z) {
			int16_t x=(int16_t)(2+(z%12)*20),y=(int16_t)(5+(z/12)*30);
			igs->input.zones[z]=(struct cterm_igs_zone){.rectangle={x,y,(int16_t)(x+10),(int16_t)(y+10)},.length=2,.bytes={(uint8_t)z,0}};
		}
		feed(&f,"G#X>6,2:X 3,2,1,1,1:Q!");
		for(size_t z=0;z<CTERM_IGS_ZONES;++z) {
			(void)mouse(&f,50,60,0);feed(&f,"G#< 0,2,0:");
			int x=igs->input.zones[z].rectangle[0],y=igs->input.zones[z].rectangle[1];
			CHECK(mouse(&f,x,y,1));CHECK(!mouse(&f,-1,-1,0));expect((uint8_t[]){(uint8_t)z,0},2);
			CHECK(!igs->input.left_held && !igs->input.held);
		}
		unchanged(&f);feed(&f,"G#X>6,0:X 3,0:");
		/* An outside press cannot turn into a selection when dragged inside. */
		feed(&f,"G#< 0,2,0:");CHECK(!mouse(&f,-1,-1,1));
		CHECK(mouse(&f,2,5,1) && !igs->collect.release);CHECK(mouse(&f,2,5,0) && igs->collect.active);
		CHECK(mouse(&f,2,5,1));CHECK(mouse(&f,2,5,0));expect((uint8_t[]){0,0},2);unchanged(&f);
		/* Locally claimed press remains suppressed through held movement. */
		feed(&f,"G#< 0,2,0:");st_cterm_sync_mouse_buttons(f.terminal,1);
		CHECK(mouse(&f,2,5,1) && !igs->collect.release);st_cterm_sync_mouse_buttons(f.terminal,0);
		CHECK(mouse(&f,2,5,1) && igs->collect.release);st_cterm_sync_mouse_buttons(f.terminal,0);
		expect((uint8_t[]){0,0},2);unchanged(&f);
		/* Reversed bounds do not match; zero-length zones can still append CR. */
		igs->input.zones[0].rectangle[2]=1;igs->input.zones[47].length=0;
		feed(&f,"G#< 1,2,0:");CHECK(mouse(&f,2,5,4));CHECK(mouse(&f,2,5,0));expect("\r",1);
		/* EOF erases a live pointer and restores prior host visibility. */
		bool before,after;CHECK(st_console_get_pointer_visible(f.console,&before)==0);
		feed(&f,"G#< 1,4,0:");CHECK(igs->collect.pointer.saved!=NULL);
		CHECK(st_cterm_finish(f.terminal)==0);CHECK(st_console_get_pointer_visible(f.console,&after)==0 && before==after);
		CHECK(!igs->collect.active && !igs->collect.pending && igs->attributes.writing_mode==2);unchanged(&f);
		destroy(&f);
	}
}
static void partitions(void)
{
	static const char wire[]="G#<>1,0,0:M 4:";
	for(unsigned resolution=0;resolution<3;++resolution) for(unsigned status=0;status<2;++status) {
		struct fixture f;setup(&f,resolution,status!=0);struct cterm_igs *igs=f.terminal->igs_engine;
		for(unsigned encoding=0;encoding<3;++encoding) {
			CHECK(st_cterm_set_encoding(f.terminal,encoding==2?ST_CTERM_ENCODING_UTF8:ST_CTERM_ENCODING_CODEPAGE,
			    encoding==0?ST_CODEPAGE_ATARI_ST:ST_CODEPAGE_CP437)==0);
			for(int scale=0;scale<3;++scale) {
				cterm_igs_set_scaling(igs,scale);
				for(size_t split=0;split<sizeof(wire);++split) {
					size_t first=0,second=0;CHECK(st_cterm_write(f.terminal,wire,split,&first)==0);
					bool completed=false;
					if(igs->collect.active) {key(&f,'A');completed=true;}
					if(first<split) {CHECK(st_cterm_write(f.terminal,wire+first,split-first,&second)==0 && second==split-first);}
					first=0;CHECK(st_cterm_write(f.terminal,wire+split,sizeof(wire)-1-split,&first)==0);
					if(igs->collect.active) {key(&f,'A');completed=true;}
					if(first<sizeof(wire)-1-split) {CHECK(st_cterm_write(f.terminal,wire+split+first,sizeof(wire)-1-split-first,&second)==0 && second==sizeof(wire)-1-split-first);}
					CHECK(completed && !igs->collect.active && igs->attributes.writing_mode==4);expect("A\r",2);
				}
				for(size_t i=0;i<sizeof(wire)-1;++i) {
					size_t used;CHECK(st_cterm_write(f.terminal,wire+i,1,&used)==0 && used==1);
					if(igs->collect.active) key(&f,'B');
				}
				expect("B\r",2);CHECK(igs->attributes.writing_mode==4);
			}
		}
		unchanged(&f);destroy(&f);
	}
}
int main(void) {keyboard_cases();chains_and_lifetime();zones();partitions();return 0;}
