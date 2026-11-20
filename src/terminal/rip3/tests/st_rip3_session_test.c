/* Real wire streams through the owned separate-engine lifecycle. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_session.h"
#include "st_rip3_host_variables.h"
#include "st_rip3_user_variables.h"
#include "st_rip3_host_internal.h"
#include "st_rip3_number.h"
#include "st_rip3_audio.h"
#include "st_pixels.h"
#include "io_error.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1; } } while (0)
extern size_t st_rip3_session_test_fail_at,st_rip3_session_test_attempts,st_rip3_session_test_live,st_rip3_session_test_stops;

struct fixture {
	struct st_rip3_session *session;
	uint8_t queue[8192],text[2048],reply[8192];
	size_t length,read,text_length,reply_length;
	unsigned reads,clocks,errors,replies;
	uint64_t now;
	enum st_rip3_command_diagnostic kind;
	bool overflow;
};

static uint64_t
now(void *context)
{
	struct fixture *f=context;f->clocks++;return f->now;
}

static bool
next(void *context, uint8_t *byte)
{
	struct fixture *f=context;f->reads++;
	if (f->read==f->length) return false;
	*byte=f->queue[f->read++];return true;
}

static void
text(void *context, const uint8_t *bytes, size_t length)
{
	struct fixture *f=context;
	if (length>sizeof(f->text)-f->text_length) {f->overflow=true;return;}
	memcpy(f->text+f->text_length,bytes,length);f->text_length+=length;
}

static void
reply(void *context, const uint8_t *bytes, size_t length)
{
	struct fixture *f=context;f->replies++;
	if (length>sizeof(f->reply)-f->reply_length) {f->overflow=true;return;}
	memcpy(f->reply+f->reply_length,bytes,length);f->reply_length+=length;
}

static void
diagnostic(void *context, enum st_rip3_command_diagnostic kind)
{
	struct fixture *f=context;f->errors++;f->kind=kind;
}

static struct st_rip3_session_config
config(struct fixture *f, enum st_rip3_canvas canvas)
{
	return (struct st_rip3_session_config){.canvas=canvas,.encoding=ST_RIP3_TEXT_CP437,
	    .source={f,now,next},.sink={.context=f,.text=text,.reply=reply,.diagnostic=diagnostic}};
}

static int
append(struct fixture *f, const char *source)
{
	size_t size=strlen(source);CHECK(size<=sizeof(f->queue)-f->length);
	memcpy(f->queue+f->length,source,size);f->length+=size;return 0;
}

static int
pump(struct fixture *f)
{
	st_rip3_session_process(f->session,8192);CHECK(!f->overflow);return 0;
}

static int
query(struct fixture *f, const char *source, const char *expected)
{
	char wire[1024];CHECK(strlen(source)<sizeof(wire)-10);
	(void)snprintf(wire,sizeof(wire),"!|1\0330000%s\r",source);
	f->reply_length=0;unsigned errors=f->errors;
	CHECK(append(f,wire)==0 && pump(f)==0 && f->read==f->length && f->errors==errors);
	CHECK(f->reply_length==strlen(expected) && memcmp(f->reply,expected,f->reply_length)==0);return 0;
}

static int
font_wire(char *wire, size_t size, const char *name, unsigned points)
{
	static const size_t widths[]={1,1,4,2,2,2,2,2,2,2,6};
	const uint64_t values[]={0,0,0,points,0,0,0,0,100,0,0};
	CHECK(size>32+strlen(name));memcpy(wire,"!|y",3);size_t at=3;
	for (size_t i=0; i<sizeof(widths)/sizeof(widths[0]); i++) {
		CHECK(st_rip3_number_encode(wire+at,size-at,values[i],widths[i],64)==0);at+=widths[i];
	}
	memcpy(wire+at,name,strlen(name));at+=strlen(name);wire[at++]='\r';wire[at]=0;return 0;
}

static int
graphics(enum st_rip3_canvas canvas, unsigned width, unsigned height)
{
	struct fixture f={0};struct st_rip3_session_config options=config(&f,canvas);
	CHECK(st_rip3_session_create(&f.session,&options)==0 && f.reads==0 && f.clocks==0 && f.errors==0);
	/* The factory copies config structs, including source/sink callbacks. */
	memset(&options,0,sizeof(options));
	struct st_rip3_session_capture capture={0};CHECK(st_rip3_session_capture(f.session,&capture)==0);
	CHECK(capture.frame.device_width==width && capture.frame.device_height==height && capture.frame.world_width==640
	    && capture.frame.world_height==350 && capture.environment==0 && capture.port==0 && capture.style==0 && capture.palette==0
	    && capture.cursor_x==0 && capture.cursor_y==0 && !capture.outline_cache_valid && !capture.query_pending && capture.viewport_active);
	st_rip3_session_capture_dispose(&capture);
	char setup[100];(void)snprintf(setup,sizeof(setup),"$WORLD(CUR,%u,%u)$$BASEMATH(CUR,64)$",width,height);
	CHECK(query(&f,setup,"")==0 && append(&f,"!|c0C|X0101\r")==0);
	/* Deliver each wire byte separately through the public owner. */
	while (f.read<f.length) st_rip3_session_process(f.session,1);
	CHECK(st_rip3_session_capture(f.session,&capture)==0 && capture.frame.world_width==width && capture.frame.world_height==height);
	for (unsigned y=0; y<height; y++) for (unsigned x=0; x<width; x++)
		CHECK(capture.pixels->primary[(size_t)y*width+x]==(ST_PIXEL_COLOR_RGB_TAG|((x==1 && y==1) ? 0xff5555 : 0)));
	st_rip3_session_capture_dispose(&capture);
	char face[100];CHECK(font_wire(face,sizeof(face),"Marin",90)==0 && append(&f,face)==0
	    && append(&f,"!|c0F|@1a1GF|TF\r")==0 && pump(&f)==0);
	CHECK(st_rip3_session_capture(f.session,&capture)==0 && capture.cursor_x==200 && capture.cursor_y==80 && capture.outline_cache_valid);
	bool ink=false;for (size_t i=0; i<(size_t)width*height; i++) ink|=capture.pixels->primary[i]==(ST_PIXEL_COLOR_RGB_TAG|0xffffff);
	CHECK(ink);st_rip3_session_capture_dispose(&capture);
	CHECK(append(&f,"!|Y00000100|@1a1GF\r")==0 && pump(&f)==0 && st_rip3_session_capture(f.session,&capture)==0
	    && capture.cursor_x==150 && capture.cursor_y==80 && capture.outline_cache_valid && f.errors==0);
	st_rip3_session_capture_dispose(&capture);
	/* Host actions resolve STYLE at execution, including a change earlier in
	 * this same query, rather than keeping the construction-time background. */
	CHECK(append(&f,"!|k0E\r")==0 && pump(&f)==0 && query(&f,"$CUR(STYLE,1)$$EGW$","")==0
	    && st_rip3_session_capture(f.session,&capture)==0 && capture.style==1);
	for (size_t i=0; i<(size_t)width*height; i++) CHECK(capture.pixels->primary[i]==ST_PIXEL_COLOR_RGB_TAG);
	st_rip3_session_capture_dispose(&capture);
	CHECK(query(&f,"$CUR(STYLE,0)$$EGW$","")==0 && st_rip3_session_capture(f.session,&capture)==0 && capture.style==0);
	for (size_t i=0; i<(size_t)width*height; i++) CHECK(capture.pixels->primary[i]==(ST_PIXEL_COLOR_RGB_TAG|0xffff55));
	st_rip3_session_capture_dispose(&capture);
	CHECK(query(&f,"$COLORMODE(CUR,RGB,8)$","")==0);
	char background[16]="!|k";CHECK(st_rip3_number_encode(background+3,sizeof(background)-3,0xff5555,4,64)==0);
	background[7]='\r';background[8]=0;
	CHECK(append(&f,background)==0 && pump(&f)==0 && query(&f,"$EGW$","")==0
	    && f.errors==0 && st_rip3_session_capture(f.session,&capture)==0);
	for (size_t i=0; i<(size_t)width*height; i++) CHECK(capture.pixels->primary[i]==(ST_PIXEL_COLOR_RGB_TAG|0xff5555));
	st_rip3_session_capture_dispose(&capture);
	CHECK(query(&f,"$COLORMODE(CUR,PAL)$$CUR(PAL,5)$$RESET(PAL)$$CUR(PAL)$:$INUSE(PAL,5)$:$ISPROT(PAL,5)$:$ISPROT(PAL,CUR)$",
	    "5:0:-1:0")==0 && append(&f,"!|c02|X0202\r")==0 && pump(&f)==0
	    && st_rip3_session_capture(f.session,&capture)==0 && capture.palette==5 && capture.outline_cache_valid);
	/* Reset does not reselect/define PAL 5 or prevent drawing/observation. */
	for (unsigned y=0; y<height; y++) for (unsigned x=0; x<width; x++)
		CHECK(capture.pixels->primary[(size_t)y*width+x]==(ST_PIXEL_COLOR_RGB_TAG|((x==2 && y==2) ? 0xaa00 : 0xff5555)));
	st_rip3_session_capture_dispose(&capture);st_rip3_session_destroy(f.session);return 0;
}

static int
literal_dixon(enum st_rip3_canvas canvas, unsigned width, unsigned height, unsigned f_width)
{
	struct fixture f={0};const struct st_rip3_session_config options=config(&f,canvas);
	CHECK(st_rip3_session_create(&f.session,&options)==0);
	CHECK(append(&f,"!|y00000W0M000000001a1a000000Dixon\r\n")==0 && pump(&f)==0 && f.errors==0);
	struct st_rip3_session_capture capture={0};CHECK(st_rip3_session_capture(f.session,&capture)==0 && !capture.outline_cache_valid);
	for (size_t i=0; i<(size_t)width*height; i++) CHECK(capture.pixels->primary[i]==ST_PIXEL_COLOR_RGB_TAG);
	st_rip3_session_capture_dispose(&capture);
	/* y's numeric block is UltraNum even though this fresh interpreter still
	 * has base-36 drawing coordinates. Dixon F has 610/1000 em advance:
	 * 22-point logical half-up width 268, then X-DPI truncation 13/16/21. */
	CHECK(append(&f,"!|@2S2SF|TF\r\n")==0 && pump(&f)==0 && f.errors==0
	    && st_rip3_session_capture(f.session,&capture)==0 && capture.outline_cache_valid
	    && capture.cursor_x==(int64_t)(100*width/640+2*f_width) && capture.cursor_y==(int64_t)(100*height/350));
	bool ink=false;for (size_t i=0; i<(size_t)width*height; i++) ink|=capture.pixels->primary[i]==(ST_PIXEL_COLOR_RGB_TAG|0xffffff);
	CHECK(ink);st_rip3_session_capture_dispose(&capture);st_rip3_session_destroy(f.session);return 0;
}

static int
prompts_and_delays(void)
{
	struct fixture f={0};const struct st_rip3_session_config options=config(&f,ST_RIP3_CANVAS_640_480);
	CHECK(st_rip3_session_create(&f.session,&options)==0);
	CHECK(append(&f,"!|1\0330000a$D(1)$$*=NEW$z|c0C|X0202\rraw\r\n")==0 && pump(&f)==0);
	size_t stopped=f.read;unsigned reads=f.reads;
	CHECK(st_rip3_session_pending(f.session) && f.reply_length==0 && st_rip3_session_wait_ns(f.session)==16666667);
	struct st_rip3_session_prompt prompt;
	CHECK(!st_rip3_session_prompt(f.session,&prompt) && pump(&f)==0 && f.reads==reads);
	f.now=16666667;CHECK(pump(&f)==0 && f.read==stopped && st_rip3_session_prompt(f.session,&prompt)
	    && strcmp(prompt.reference->name,"NEW")==0 && prompt.initial->length==0);
	st_rip3_session_respond(f.session,NULL,0,true);
	CHECK(st_rip3_session_prompt(f.session,&prompt) && f.errors==1 && f.kind==ST_RIP3_COMMAND_OPERATION_FAILED);
	st_rip3_session_respond(f.session,(const uint8_t *)"Ada",3,false);
	CHECK(!st_rip3_session_pending(f.session) && f.read==stopped && f.reply_length==5 && memcmp(f.reply,"aAdaz",5)==0);
	CHECK(pump(&f)==0 && f.read==f.length && f.text_length==5 && memcmp(f.text,"raw\r\n",5)==0);
	CHECK(query(&f,"$&NEW$","Ada")==0);
	/* A continuation must finish even after the last source byte was taken. */
	char many[300]="!|1\0330000";size_t at=strlen(many);
	for (unsigned i=0; i<130; i++) {many[at++]='^';many[at++]='m';}many[at++]='\r';many[at]=0;
	f.reply_length=0;CHECK(append(&f,many)==0 && pump(&f)==0 && st_rip3_session_pending(f.session) && f.read==f.length);
	for (unsigned i=0; i<8 && st_rip3_session_pending(f.session); i++) st_rip3_session_process(f.session,1);
	CHECK(!st_rip3_session_pending(f.session) && f.reply_length==130);
	for (size_t i=0; i<f.reply_length; i++) CHECK(f.reply[i]==13);
	CHECK(query(&f,"<<IF RESX=640>>[3:]yes[]$?3$<<ELSE>>no<<ENDIF>>","yes")==0
	    && query(&f,"<<IF ?3=yes>>ready<<ELSE>>bad<<ENDIF>>","ready")==0);
	f.reply_length=0;CHECK(append(&f,"!|1\0330000<<*=ANGLE>>\rnext\r\n")==0 && pump(&f)==0
	    && st_rip3_session_prompt(f.session,&prompt) && strcmp(prompt.reference->name,"ANGLE")==0);
	stopped=f.read;st_rip3_session_respond(f.session,(const uint8_t *)"<<RESX>>",8,false);
	CHECK(f.read==stopped && f.reply_length==3 && memcmp(f.reply,"640",3)==0 && !st_rip3_session_pending(f.session));
	CHECK(pump(&f)==0 && f.text_length==11 && memcmp(f.text+5,"next\r\n",6)==0
	    && query(&f,"$&ANGLE$","<<RESX>>")==0);
	st_rip3_session_destroy(f.session);return 0;
}

static int
cancellation_and_isolation(void)
{
	struct fixture a={0},b={0};struct st_rip3_session_config options=config(&a,ST_RIP3_CANVAS_640_480);
	CHECK(st_rip3_session_create(&a.session,&options)==0);options=config(&b,ST_RIP3_CANVAS_800_600);
	CHECK(st_rip3_session_create(&b.session,&options)==0);
	CHECK(append(&a,"!|1\0330000$-=SECRET=kept$$D(60)$lost\r")==0 && pump(&a)==0 && st_rip3_session_pending(a.session));
	unsigned replies=a.replies,errors=a.errors;size_t read=a.read;
	st_rip3_session_cancel(a.session);
	CHECK(a.read==read && a.replies==replies && a.errors==errors && st_rip3_session_wait_ns(a.session)==0 && !st_rip3_session_pending(a.session));
	CHECK(query(&a,"$&SECRET$","kept")==0 && query(&b,"$&SECRET=second$","second")==0);
	CHECK(append(&a,"!|c0")==0 && pump(&a)==0);st_rip3_session_cancel(a.session);
	CHECK(append(&a,"plain\r\n")==0 && pump(&a)==0 && a.text_length==7 && memcmp(a.text,"plain\r\n",7)==0);
	CHECK(append(&a,"!|1\0330000$*=PENDING$\r")==0 && pump(&a)==0 && st_rip3_session_pending(a.session));
	st_rip3_session_cancel(a.session);CHECK(!st_rip3_session_pending(a.session));
	CHECK(query(&a,"$&PENDING=after$","after")==0);
	/* Missing reply sinks reject immediate actions before changing radix. */
	struct fixture silent={0};options=config(&silent,ST_RIP3_CANVAS_640_480);options.sink.reply=NULL;
	CHECK(st_rip3_session_create(&silent.session,&options)==0 && append(&silent,"!|1\0330000$BASEMATH(CUR,64)$\r")==0 && pump(&silent)==0
	    && silent.errors==1 && silent.kind==ST_RIP3_COMMAND_UNSUPPORTED);
	CHECK(append(&silent,"!|J??|c0C|X0101\r")==0 && pump(&silent)==0 && silent.errors==2); /* Invalid J recovers to drawing. */
	st_rip3_session_destroy(silent.session);st_rip3_session_destroy(a.session);st_rip3_session_destroy(b.session);return 0;
}

static int
borrowed_user(void)
{
	struct st_rip3_variables *values=NULL,*persistent=NULL;struct st_rip3_user_variables *user=NULL;
	CHECK(st_rip3_variables_create(&values)==0 && st_rip3_variables_create(&persistent)==0);
	const struct st_rip3_user_variable_host user_host={.session=values,.persistent=persistent};
	CHECK(st_rip3_user_variables_create(&user,&user_host)==0);
	struct st_rip3_variable_reference foreign;CHECK(st_rip3_variable_reference_parse(&foreign,(const uint8_t *)"$FOREIGN$",9)==0);
	st_rip3_user_variables_process(user,&foreign);
	struct fixture f={0};struct st_rip3_session_config options=config(&f,ST_RIP3_CANVAS_640_480);
	const struct st_rip3_host_variables services={.user_variables=user};options.services=&services;
	CHECK(st_rip3_session_create(&f.session,&options)==0 && append(&f,"!|1\0330000$D(1)$\r")==0 && pump(&f)==0);
	struct st_rip3_session_prompt prompt;
	CHECK(st_rip3_session_pending(f.session) && !st_rip3_session_prompt(f.session,&prompt));
	st_rip3_session_respond(f.session,(const uint8_t *)"wrong",5,false);
	CHECK(f.errors==1 && st_rip3_user_variables_state(user)==ST_RIP3_USER_VARIABLE_PROMPT);
	st_rip3_session_destroy(f.session);f.session=NULL;
	CHECK(st_rip3_user_variables_state(user)==ST_RIP3_USER_VARIABLE_PROMPT);
	st_rip3_user_variables_abort(user);f.read=f.length=0;
	CHECK(st_rip3_session_create(&f.session,&options)==0 && append(&f,"!|1\0330000$*=OWNED$\r")==0 && pump(&f)==0
	    && st_rip3_session_prompt(f.session,&prompt));
	st_rip3_session_destroy(f.session);
	CHECK(st_rip3_user_variables_state(user)==ST_RIP3_USER_VARIABLE_IDLE
	    && st_rip3_variables_set(values,"AFTER",5,(const uint8_t *)"ok",2)==0);
	st_rip3_user_variables_destroy(user);st_rip3_variables_destroy(values);st_rip3_variables_destroy(persistent);return 0;
}

static int
text_deactivation(void)
{
	for (unsigned width=2;width<=5;width++) for (unsigned base=0;base<2;base++) {
		struct fixture f={0};struct st_rip3_session_config options=config(&f,ST_RIP3_CANVAS_640_480);
		CHECK(st_rip3_session_create(&f.session,&options)==0 && st_rip3_session_text_active(f.session));
		char setup[32];(void)snprintf(setup,sizeof(setup),"!|J%s|n%u\r",base ? "1S" : "10",width);
		CHECK(append(&f,setup)==0 && pump(&f)==0 && f.errors==0);
		char line[32]="!|w";size_t length=4*width+2;
		memset(line+3,'0',length);line[3+length]='\r';line[4+length]=0;
		/* Reject partial/malformed/unsupported layouts without disabling TTY. */
		line[3+length-1]='?';CHECK(append(&f,line)==0 && pump(&f)==0 && f.errors==1
		    && st_rip3_session_text_active(f.session));
		line[3+length-1]='1';CHECK(append(&f,line)==0 && pump(&f)==0 && f.errors==2
		    && st_rip3_session_text_active(f.session));
		line[3+length-1]='0';CHECK(append(&f,line)==0 && pump(&f)==0 && f.errors==2
		    && !st_rip3_session_text_active(f.session));
		CHECK(query(&f,"$RESX$","640")==0);
		st_rip3_session_cancel(f.session);CHECK(!st_rip3_session_text_active(f.session));
		st_rip3_session_destroy(f.session);
	}
	CHECK(!st_rip3_session_text_active(NULL));return 0;
}

static int
construction(bool device)
{
	struct fixture f={0};struct st_rip3_session_config options=config(&f,ST_RIP3_CANVAS_640_480);
	if (device) {options.device_width=720;options.device_height=400;}
	audio_engine_t *engine=NULL;struct st_rip3_audio *player=NULL;
	CHECK(audio_engine_create_pull(&engine)==0 && st_rip3_audio_create(&player,engine,0,0)==0);options.audio=player;
	st_rip3_session_test_attempts=0;CHECK(st_rip3_session_create(&f.session,&options)==0);
	size_t attempts=st_rip3_session_test_attempts,live=st_rip3_session_test_live,stops=st_rip3_session_test_stops;
	CHECK(attempts>12 && live==attempts);
	for (size_t at=0; at<attempts; at++) {
		st_rip3_session_test_attempts=0;st_rip3_session_test_fail_at=at;
		struct st_rip3_session *unchanged=f.session;
		CHECK(st_rip3_session_create(&unchanged,&options)==IO_ERROR_OUT_OF_MEMORY && unchanged==f.session
		    && st_rip3_session_test_live==live && st_rip3_session_test_stops==stops && f.reads==0 && f.clocks==0 && f.errors==0);
	}
	st_rip3_session_test_fail_at=SIZE_MAX;
	st_rip3_session_cancel(f.session);CHECK(st_rip3_session_test_stops==stops+1);
	st_rip3_session_destroy(f.session);f.session=NULL;
	CHECK(st_rip3_session_test_live==0 && st_rip3_session_test_stops==stops+2 && st_rip3_audio_stop(player)==0);
	st_rip3_audio_destroy(player);audio_engine_destroy(engine);
	CHECK(st_rip3_session_create(NULL,&options)==IO_ERROR_INVALID_ARGUMENT && st_rip3_session_create(&f.session,NULL)==IO_ERROR_INVALID_ARGUMENT);
	options.source.next=NULL;CHECK(st_rip3_session_create(&f.session,&options)==IO_ERROR_INVALID_ARGUMENT && f.session==NULL);
	st_rip3_session_process(NULL,1);st_rip3_session_cancel(NULL);st_rip3_session_respond(NULL,NULL,0,false);st_rip3_session_destroy(NULL);
	CHECK(!st_rip3_session_pending(NULL) && st_rip3_session_wait_ns(NULL)==0 && !st_rip3_session_prompt(NULL,NULL));
	CHECK(st_rip3_session_capture(NULL,NULL)==IO_ERROR_INVALID_ARGUMENT);st_rip3_session_capture_dispose(NULL);
	return 0;
}

int
main(void)
{
	CHECK(graphics(ST_RIP3_CANVAS_640_480,640,480)==0 && graphics(ST_RIP3_CANVAS_800_600,800,600)==0
	    && graphics(ST_RIP3_CANVAS_1024_768,1024,768)==0);
	CHECK(literal_dixon(ST_RIP3_CANVAS_640_480,640,480,13)==0 && literal_dixon(ST_RIP3_CANVAS_800_600,800,600,16)==0
	    && literal_dixon(ST_RIP3_CANVAS_1024_768,1024,768,21)==0);
	CHECK(prompts_and_delays()==0 && cancellation_and_isolation()==0 && borrowed_user()==0
	    && text_deactivation()==0 && construction(false)==0 && construction(true)==0);
	CHECK(st_rip3_session_test_live==0);return 0;
}
