/* Real wire streams through the owned separate-engine lifecycle. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_session.h"
#include "st_rip3_host_variables.h"
#include "st_rip3_user_variables.h"
#include "st_rip3_host_internal.h"
#include "st_rip3_number.h"
#include "st_rip3_audio.h"
#include "st_rip3_text_window.h"
#include "st_rip3_surface.h"
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
	unsigned windows;
	int window_status;
	unsigned moves, column, row;
	int move_status;
	unsigned cursor_changes;
	int cursor_status;
	bool cursor_visible;
	unsigned erases;
	unsigned eol_erases;
	unsigned observations,observed_window;
	int observation_status;
	int erase_status;
	bool erase_whole;
	uint32_t erase_rgb;
	bool identical;
	unsigned resets;
	bool reset_keep[36],reset_old_active;
	bool selected_first,selected_fresh,selected_active;
	uint32_t clear_rgb;
	struct st_rip3_text_window_layout window;
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
window(void *context, const struct st_rip3_text_window_layout *layout, bool identical, uint32_t clear_rgb)
{
	struct fixture *f=context;f->windows++;f->window=*layout;f->identical=identical;f->clear_rgb=clear_rgb;
	return f->window_status;
}

static int
move_cursor(void *context, unsigned number, const struct st_rip3_text_cursor_position *position)
{
	(void)number;
	struct fixture *f=context;f->moves++;
	if (position->set_column) f->column=position->column>UINT32_MAX ? UINT32_MAX : (uint32_t)position->column;
	if (position->set_row) f->row=position->row>UINT32_MAX ? UINT32_MAX : (uint32_t)position->row;
	return f->move_status;
}

static int
erase_text(void *context, unsigned number, bool whole, uint32_t rgb, struct st_rip3_rect *area)
{
	(void)number;
	struct fixture *f=context;f->erases++;f->erase_whole=whole;f->erase_rgb=rgb;
	*area=(struct st_rip3_rect){1,1,2,2};return f->erase_status;
}

static int
erase_eol(void *context, unsigned number)
{
	(void)number;
	struct fixture *f=context;f->eol_erases++;return f->erase_status;
}

static int
text_erases(void)
{
	struct fixture f={0};struct st_rip3_session_config options=config(&f,ST_RIP3_CANVAS_640_480);
	options.device_width=4;options.device_height=3;options.sink.text_erase=erase_text;
	CHECK(st_rip3_session_create(&f.session,&options)==0);
	CHECK(append(&f,"!|f0403|c02|S0102|B00000403|k01|W1|e\r")==0 && pump(&f)==0
	    && f.errors==0 && f.erases==1 && f.erase_whole && f.erase_rgb==0);
	struct st_rip3_session_capture capture={0};
	CHECK(st_rip3_session_capture(f.session,&capture)==0);
	for (size_t y=0;y<3;y++) for (size_t x=0;x<4;x++)
		CHECK(capture.pixels->primary[y*4+x]==(ST_PIXEL_COLOR_RGB_TAG|(x>=1 && x<=2 && y>=1 ? 0 : 0xaa00)));
	st_rip3_session_capture_dispose(&capture);
	CHECK(append(&f,"!|a0010|v00000000|>\r")==0 && pump(&f)==0
	    && f.erases==2 && !f.erase_whole && f.erase_rgb==0xaa && f.errors==0);
	CHECK(query(&f,"$ETW(ALL)$","")==0 && f.erases==3 && f.erase_whole && f.erase_rgb==0xff0000);
	CHECK(st_rip3_session_capture(f.session,&capture)==0);
	for (size_t y=0;y<3;y++) for (size_t x=0;x<4;x++)
		CHECK(capture.pixels->primary[y*4+x]==(ST_PIXEL_COLOR_RGB_TAG|(x>=1 && x<=2 && y>=1 ? 0xff0000 : 0xaa00)));
	st_rip3_session_capture_dispose(&capture);
	CHECK(append(&f,"!|a0109\r")==0 && pump(&f)==0);
	CHECK(query(&f,"$COLORMODE(CUR,RGB,1)$","")==0);
	CHECK(append(&f,"!|k1|>\r")==0 && pump(&f)==0 && f.erases==4 && !f.erase_whole && f.erase_rgb==0xff);
	CHECK(query(&f,"$DTW$$ETW$","")==0 && f.erases==4);
	CHECK(append(&f,"!|e|>|e0|>0\r")==0 && pump(&f)==0 && f.erases==4 && f.errors==2
	    && f.kind==ST_RIP3_COMMAND_INVALID);
	CHECK(query(&f,"$ETW(1,35)$$ATW$","")==0 && f.erases==4);
	CHECK(append(&f,"!|1\0330000$ETW(CUR,36)$\r")==0 && pump(&f)==0 && f.erases==4 && f.errors==3);
	f.erase_status=IO_ERROR_IO;
	CHECK(append(&f,"!|e\r")==0 && pump(&f)==0 && f.erases==5 && f.errors==4 && f.kind==ST_RIP3_COMMAND_OPERATION_FAILED);
	f.erase_status=IO_ERROR_OUT_OF_MEMORY;
	CHECK(append(&f,"!|>\r")==0 && pump(&f)==0 && f.erases==6 && f.errors==5 && f.kind==ST_RIP3_COMMAND_RESOURCE_FAILURE);
	CHECK(query(&f,"$RESX$","4")==0);
	CHECK(st_rip3_session_capture(f.session,&capture)==0);
	for (size_t y=0;y<3;y++) for (size_t x=0;x<4;x++)
		CHECK(capture.pixels->primary[y*4+x]==(ST_PIXEL_COLOR_RGB_TAG|(x>=1 && x<=2 && y>=1 ? 0xff : 0xaa00)));
	st_rip3_session_capture_dispose(&capture);
	st_rip3_session_destroy(f.session);
	/* ANSI erasure has its own service and does not use graphics STYLE/PAL. */
	f=(struct fixture){0};options=config(&f,ST_RIP3_CANVAS_640_480);
	options.sink.text_erase_eol=erase_eol;
	CHECK(st_rip3_session_create(&f.session,&options)==0);
	CHECK(query(&f,"$TWERASEEOL$","")==0 && f.eol_erases==1 && f.erases==0);
	CHECK(query(&f,"$TWERASEEOL(ALL,CUR,0,0)$","")==0 && f.eol_erases==2);
	CHECK(query(&f,"$DTW$$TWERASEEOL$$ATW$$TWERASEEOL(1,35)$","")==0 && f.eol_erases==2);
	CHECK(append(&f,"!|1\0330000$TWERASEEOL(CUR,36)$\r")==0 && pump(&f)==0
	    && f.eol_erases==2 && f.errors==1 && f.kind==ST_RIP3_COMMAND_INVALID);
	f.erase_status=IO_ERROR_OUT_OF_MEMORY;
	CHECK(append(&f,"!|1\0330000$TWERASEEOL$\r")==0 && pump(&f)==0
	    && f.eol_erases==3 && f.errors==2 && f.kind==ST_RIP3_COMMAND_RESOURCE_FAILURE);
	f.erase_status=IO_ERROR_IO;
	CHECK(append(&f,"!|1\0330000$TWERASEEOL$\r")==0 && pump(&f)==0
	    && f.eol_erases==4 && f.errors==3 && f.kind==ST_RIP3_COMMAND_OPERATION_FAILED);
	f.erase_status=0;
	CHECK(query(&f,"$TWERASEEOL$alive","alive")==0 && f.eol_erases==5);
	st_rip3_session_destroy(f.session);return 0;
}

static int
select_text(void *context, unsigned number, bool first, bool active,
    const struct st_rip3_text_window_layout *fresh)
{
	struct fixture *f=context;(void)number;
	f->selected_first=first;f->selected_fresh=fresh!=NULL;f->selected_active=active;
	if (fresh!=NULL) f->window=*fresh;
	return f->window_status;
}

static int
reset_text(void *context, const struct st_rip3_text_window_layout *layout,
    const bool keep[36], bool old_active)
{
	struct fixture *f=context;f->resets++;f->window=*layout;
	memcpy(f->reset_keep,keep,sizeof(f->reset_keep));f->reset_old_active=old_active;
	return f->window_status;
}

static int
soft_reset(void)
{
	struct fixture f={0};struct st_rip3_session_config options=config(&f,ST_RIP3_CANVAS_640_480);
	options.sink.text_select=select_text;options.sink.text_window=window;options.sink.text_reset=reset_text;
	options.default_text_font=2;options.default_text_wrap=true;
	CHECK(st_rip3_session_create(&f.session,&options)==0);
	CHECK(query(&f,"$-=KEPT=value$","")==0 && query(&f,"[5:]held","")==0);
	CHECK(append(&f,"!|2Y10|c02|2Y20|c03\r")==0 && pump(&f)==0);
	CHECK(query(&f,"$PROT(STYLE,1)$$CUR(PAL,5)$","")==0);
	CHECK(append(&f,"!|a0010\r")==0 && pump(&f)==0);
	CHECK(query(&f,"$PROT(PAL,5)$$CUR(PORT,5)$$PROT(PORT,5)$$CUR(PORT,6)$$CUR(TW,1)$$PROT(TW,1)$$CUR(TW,2)$","")==0);
	CHECK(query(&f,"$COPY(PAL,TBL,BASE,S0,PUSH)$$COPY(PORT,TBL,BASE)$$COPY(STYLE,TBL,BASE)$$COPY(ENV,TBL,BASE)$","")==0);
	CHECK(query(&f,"$WORLD(CUR,0,0)$$BASEMATH(CUR,64)$$COORDSIZE(CUR,5)$$COLORMODE(CUR,RGB,8)$","")==0);
	CHECK(append(&f,"!|*\r")==0 && pump(&f)==0 && f.resets==1 && f.reset_old_active && f.reset_keep[1]
	    && !f.reset_keep[2] && f.window.font==2 && f.window.columns==80 && f.window.rows==25 && f.window.wrap);
	CHECK(query(&f,"$CUR(TW)$:$CUR(STYLE)$:$CUR(PORT)$:$CUR(ENV)$:$CUR(PAL)$","0:0:0:0:5")==0);
	CHECK(query(&f,"$INUSE(TW,1)$$INUSE(TW,2)$$INUSE(STYLE,1)$$INUSE(STYLE,2)$$INUSE(PORT,5)$$INUSE(PORT,6)$$INUSE(PAL,5)$","1010101")==0);
	CHECK(query(&f,"$WORLD$:$BASEMATH$:$COORDSIZE$","640:350:36:2")==0);
	CHECK(query(&f,"$&KEPT$:$INUSE(PAL,BASE)$$INUSE(PAL,S0)$$INUSE(PAL,STACK)$$INUSE(PORT,BASE)$$INUSE(STYLE,BASE)$$INUSE(ENV,BASE)$","value:111111")==0);
	CHECK(query(&f,"[5]","held")==0);
	struct st_rip3_session_capture capture={0};CHECK(st_rip3_session_capture(f.session,&capture)==0);
	for (size_t i=0;i<640*480;i++) CHECK(capture.pixels->primary[i]==ST_PIXEL_COLOR_RGB_TAG);
	st_rip3_session_capture_dispose(&capture);
	/* SOFT inside a query must not cancel that query or its following reply. */
	CHECK(query(&f,"$RESET(SOFT)$alive","alive")==0 && f.resets==2);
	f.window_status=IO_ERROR_OUT_OF_MEMORY;
	CHECK(append(&f,"!|*\r")==0 && pump(&f)==0 && f.errors==1 && f.kind==ST_RIP3_COMMAND_RESOURCE_FAILURE);
	f.window_status=0;
	CHECK(append(&f,"!|*x\r")==0 && pump(&f)==0 && f.errors==2 && f.resets==3);
	CHECK(query(&f,"$RESET(SOFT)$alive","alive")==0 && f.resets==4);
	st_rip3_session_destroy(f.session);return 0;
}

static int
window_table(void)
{
	struct fixture f={0};struct st_rip3_session_config options=config(&f,ST_RIP3_CANVAS_640_480);
	options.default_text_font=2;options.default_text_wrap=true;
	options.sink.text_select=select_text;options.sink.text_window=window;
	options.sink.text_cursor=move_cursor;options.sink.text_erase=erase_text;
	CHECK(st_rip3_session_create(&f.session,&options)==0);
	CHECK(query(&f,"$CUR(TW)$:$INUSE(TW,ANY)$:$INUSE(TW,1)$:$ISPROT(TW,CUR)$:$TWIN$:$ISEXTWIN$","0:0:0:0:YES:0")==0);
	CHECK(append(&f,"!|w0A050H0A10|2T10\r")==0 && pump(&f)==0 && f.errors==0);
	CHECK(query(&f,"$CUR(TW)$:$INUSE(TW,1)$:$ISPROT(TW,1)$","1:1:0")==0);
	CHECK(query(&f,"$PROT(TW,1)$$ISPROT(TW,CUR)$","1")==0);
	unsigned windows=f.windows;
	CHECK(append(&f,"!|w0000000000|w00000A0510\r")==0 && pump(&f)==0 && f.windows==windows
	    && f.errors==2 && st_rip3_session_text_active(f.session));
	CHECK(query(&f,"$DTW(1)$$TWIN(1)$","YES")==0);
	CHECK(query(&f,"$UNPROT(TW,1)$$DTW(1)$$PROT(TW,1)$$ATW(1)$$TWIN(1)$","NO")==0);
	CHECK(query(&f,"$UNPROT(TW,1)$$INUSE(TW,1)$:$TWIN(1)$:$ISEXTWIN(1)$$ATW(1)$$PROT(TW,1)$$CUR(TW,0)$$TWGOTO(1,3,2)$","1:NO:-1")==0
	    && f.moves==1 && f.column==3 && f.row==2);
	CHECK(query(&f,"$ETW(1)$","")==0 && f.erases==1);
	CHECK(query(&f,"$CUR(TW,1)$$UNPROT(TW,CUR)$$ISPROT(TW,1)$","0")==0);
	f.window_status=IO_ERROR_OUT_OF_MEMORY;
	CHECK(append(&f,"!|2T20\r")==0 && pump(&f)==0 && f.errors==3 && f.kind==ST_RIP3_COMMAND_RESOURCE_FAILURE);
	CHECK(query(&f,"$CUR(TW)$:$INUSE(TW,2)$","1:0")==0);
	f.window_status=0;
	CHECK(append(&f,"!|2T2\r")==0 && pump(&f)==0 && f.errors==3);
	CHECK(query(&f,"$CUR(TW)$:$INUSE(TW,2)$","2:1")==0);
	CHECK(append(&f,"!|2T2?\r")==0 && pump(&f)==0 && f.errors==4);
	CHECK(query(&f,"$CUR(TW)$","2")==0);
	CHECK(query(&f,"$PROT(TW,2)$","")==0 && append(&f,"!|2T30\r")==0 && pump(&f)==0
	    && f.selected_first && !f.selected_fresh && f.selected_active);
	CHECK(query(&f,"$CUR(TW)$:$ISPROT(TW,CUR)$:$ISPROT(TW,2)$","3:0:1")==0);
	CHECK(query(&f,"$DTW$$CUR(TW,4)$","")==0 && f.selected_first && f.selected_fresh && f.selected_active
	    && f.window.font==2 && f.window.columns==80 && f.window.rows==25 && f.window.wrap
	    && f.window.x0==0 && f.window.y0==0 && f.window.x1==79 && f.window.y1==24);
	CHECK(query(&f,"$TWIN$:$ISPROT(TW,CUR)$:$TWIN(3)$","YES:0:NO")==0);
	CHECK(query(&f,"$CUR(TW,2)$","")==0 && !f.selected_first && !f.selected_fresh);
	st_rip3_session_destroy(f.session);return 0;
}

static int
observe_window(void *context, unsigned number, struct st_rip3_text_window_snapshot *result)
{
	struct fixture *f=context;f->observations++;f->observed_window=number;
	if (f->observation_status!=0) return f->observation_status;
	*result=(struct st_rip3_text_window_snapshot){.column=7,.row=5,.cursor_visible=true,
	    .layout={.columns=31,.rows=13,.cell_width=8,.cell_height=8}};
	return 0;
}

static int
window_queries(void)
{
	struct fixture f={0};struct st_rip3_session_config options=config(&f,ST_RIP3_CANVAS_640_480);
	options.sink.text_snapshot=observe_window;options.sink.text_window=window;options.sink.text_select=select_text;
	CHECK(st_rip3_session_create(&f.session,&options)==0);
	CHECK(query(&f,"$CURX$:$CURY$:$CURSOR$:$TWW$:$TWH$","7:5:YES:31:13")==0);
	CHECK(append(&f,"!|1\0330000$TWFONT$\r")==0 && pump(&f)==0 && f.errors==1
	    && f.kind==ST_RIP3_COMMAND_UNSUPPORTED);
	CHECK(append(&f,"!|w0A050H0A10\r")==0 && pump(&f)==0);
	CHECK(query(&f,"$TWW$:$TWH$:$TWFONT$:$TWH(CUR,TEXT,WORLD)$","8:6:1:48")==0);
	CHECK(append(&f,"!|2T70\r")==0 && pump(&f)==0);
	CHECK(query(&f,"$CUR(TW,0)$$CURX(7)$:$CUR(TW)$","7:0")==0 && f.observed_window==7);
	unsigned observations=f.observations;
	CHECK(query(&f,"$DTW(7)$$CURY(7)$:$CURSOR(7)$:$TWX0(7)$","0:NO:-1")==0 && f.observations==observations);
	CHECK(query(&f,"$ATW(7)$$WORLD(CUR,0,0)$$TWW(7,TEXT,DEVICE)$","64")==0);
	CHECK(append(&f,"!|1\0330000$TWW(7,TEXT,WORLD)$\r")==0 && pump(&f)==0 && f.errors==2);
	f.observation_status=IO_ERROR_OUT_OF_MEMORY;
	CHECK(append(&f,"!|1\0330000$CURX$\r")==0 && pump(&f)==0 && f.errors==3 && f.kind==ST_RIP3_COMMAND_RESOURCE_FAILURE);
	f.observation_status=0;CHECK(query(&f,"$CURY$alive","5alive")==0);
	st_rip3_session_destroy(f.session);return 0;
}

static int
show_cursor(void *context, unsigned number, bool visible)
{
	(void)number;
	struct fixture *f=context;f->cursor_changes++;
	if (f->cursor_status==0) f->cursor_visible=visible;
	return f->cursor_status;
}

static int
cursor_variables(void)
{
	struct fixture f={0};struct st_rip3_session_config options=config(&f,ST_RIP3_CANVAS_640_480);
	options.sink.text_cursor=move_cursor;options.sink.text_cursor_visible=show_cursor;
	CHECK(st_rip3_session_create(&f.session,&options)==0);
	CHECK(query(&f,"$TWGOTO(0,123,456)$","")==0 && f.moves==1 && f.column==123 && f.row==456);
	CHECK(query(&f,"$TWGOTO(CUR,CUR,2)$","")==0 && f.moves==2 && f.column==123 && f.row==2);
	CHECK(query(&f,"$TWGOTO$$TWGOTO(CUR)$","")==0 && f.moves==2);
	CHECK(query(&f,"$TWHOME$","")==0 && f.moves==3 && f.column==0 && f.row==0);
	CHECK(query(&f,"$TWGOTO(1,2,3)$$TWHOME(35)$$CON(1,35)$","")==0 && f.moves==3 && f.cursor_changes==0);
	CHECK(query(&f,"$CON$","")==0 && f.cursor_changes==1 && f.cursor_visible);
	CHECK(query(&f,"$COFF$","")==0 && f.cursor_changes==2 && !f.cursor_visible);
	CHECK(append(&f,"!|1\0330000$TWGOTO(CUR,0,NO)$$CON(CUR,36)$\r")==0 && pump(&f)==0
	    && f.moves==3 && f.cursor_changes==2 && f.errors==2 && f.kind==ST_RIP3_COMMAND_INVALID);
	f.move_status=IO_ERROR_IO;
	CHECK(append(&f,"!|1\0330000$TWHOME$\r")==0 && pump(&f)==0 && f.moves==4 && f.errors==3
	    && f.kind==ST_RIP3_COMMAND_OPERATION_FAILED);
	CHECK(query(&f,"$RESX$","640")==0);
	f.move_status=0;f.cursor_status=IO_ERROR_OUT_OF_MEMORY;
	CHECK(append(&f,"!|1\0330000$CON$\r")==0 && pump(&f)==0 && f.cursor_changes==3 && f.errors==4
	    && f.kind==ST_RIP3_COMMAND_RESOURCE_FAILURE && !f.cursor_visible);
	f.cursor_status=0;
	CHECK(query(&f,"$DTW$$CON$$TWGOTO(CUR,2,3)$$TWHOME$","")==0 && f.cursor_changes==3 && f.moves==4);
	CHECK(query(&f,"$ATW$$CON$","")==0 && f.cursor_changes==4 && f.cursor_visible && f.errors==4);
	st_rip3_session_destroy(f.session);
	struct fixture missing={0};options=config(&missing,ST_RIP3_CANVAS_640_480);
	CHECK(st_rip3_session_create(&missing.session,&options)==0);
	CHECK(query(&missing,"$TWGOTO(1,0,0)$$COFF(35)$","")==0 && missing.errors==0);
	CHECK(append(&missing,"!|1\0330000$COFF$$TWHOME$\r")==0 && pump(&missing)==0
	    && missing.errors==2 && missing.kind==ST_RIP3_COMMAND_UNSUPPORTED);
	CHECK(append(&missing,"!|e|>\r")==0 && pump(&missing)==0
	    && missing.errors==4 && missing.kind==ST_RIP3_COMMAND_UNSUPPORTED);
	CHECK(query(&missing,"$RESY$","480")==0);
	st_rip3_session_destroy(missing.session);return 0;
}

static int
text_cursor(void)
{
	struct fixture f={0};struct st_rip3_session_config options=config(&f,ST_RIP3_CANVAS_640_480);
	options.sink.text_cursor=move_cursor;
	CHECK(st_rip3_session_create(&f.session,&options)==0);
	CHECK(append(&f,"!|g0509|H\r")==0 && pump(&f)==0 && f.moves==2 && f.column==0 && f.row==0);
	CHECK(append(&f,"!|J1S|n5|g0l0A\r")==0 && pump(&f)==0 && f.moves==3 && f.column==47 && f.row==10);
	CHECK(append(&f,"!|g??00|g000|H0|g00000\r")==0 && pump(&f)==0 && f.moves==3 && f.errors==4);
	f.move_status=IO_ERROR_IO;
	CHECK(append(&f,"!|H\r")==0 && pump(&f)==0 && f.moves==4 && f.errors==5
	    && f.kind==ST_RIP3_COMMAND_OPERATION_FAILED);
	CHECK(query(&f,"$RESX$","640")==0);
	f.move_status=0;
	CHECK(append(&f,"!|H\r")==0 && pump(&f)==0 && f.moves==5 && f.column==0 && f.row==0);
	CHECK(append(&f,"!|J10|n2|w0000000000|g0102|H\r")==0 && pump(&f)==0 && f.moves==5 && f.errors==5);
	CHECK(query(&f,"$RESX$","640")==0 && !st_rip3_session_text_active(f.session));
	st_rip3_session_destroy(f.session);return 0;
}

static int
extended_wire(char *wire, size_t capacity, unsigned radix, unsigned xy, unsigned flags)
{
	const uint64_t fields[]={10,20,1010,620,40,25,4,flags};
	size_t at=2;CHECK(capacity>2);wire[0]='!';wire[1]='|';wire[at++]='b';
	for (unsigned i=0;i<8;i++) {
		unsigned width=i<4 ? xy : i<6 ? 2 : i==6 ? 1 : 4;
		CHECK(st_rip3_number_encode(wire+at,capacity-at,fields[i],width,radix)==0);at+=width;
	}
	CHECK(capacity>at+1);wire[at++]='\r';wire[at]=0;return 0;
}

static int
extended_window(void)
{
	struct fixture f={0};struct st_rip3_session_config options=config(&f,ST_RIP3_CANVAS_1024_768);
	options.sink.text_window=window;
	CHECK(st_rip3_session_create(&f.session,&options)==0 && query(&f,"$WORLD(CUR,1024,768)$","")==0);
	CHECK(append(&f,"!|b0A0KS2H8140PZ8\r")==0);
	while (f.read<f.length) st_rip3_session_process(f.session,1);
	CHECK(!f.overflow && f.windows==1 && !f.identical
	    && f.window.extended && f.window.font==4 && f.window.x==110 && f.window.y==20
	    && f.window.columns==40 && f.window.rows==25 && f.window.cell_width==20 && f.window.cell_height==24);
	CHECK(append(&f,"!|a0010|b0A0KS2H8140P4000HXYZ\r")==0 && pump(&f)==0 && f.windows==2
	    && !f.identical && f.window.flags==17 && f.clear_rgb==0xff0000);
	CHECK(append(&f,"!|b0A0KS2H8140P4000H001\r")==0 && pump(&f)==0 && f.windows==3 && !f.identical && f.window.flags==17);
	CHECK(append(&f,"!|b0A0KS2H8140P4000H0|b0A0KS2H8140P4000H00|b0A0KS2H8140P4H\r")==0
	    && pump(&f)==0 && f.windows==6 && !f.identical && f.window.flags==17);
	const char *const invalid[]={"b","b0A0KS2H8140PZ","b0A0KS2H8000P48","b0A0KS2H8140048",
	    "b0A0KS2H8140P50","b0A0KS2H8140P41S","b0A0KS2H8140P48?","b0A0KS2H8140P?8",
	    "b0A0KS2H8140P4000H00?","b0A0KS2H8140P4000H0000"};
	unsigned errors=f.errors;
	for (size_t i=0;i<sizeof(invalid)/sizeof(*invalid);i++) {
		char bad[100];snprintf(bad,sizeof(bad),"!|%s\r",invalid[i]);
		CHECK(append(&f,bad)==0 && pump(&f)==0 && f.windows==6 && f.errors==errors+i+1 && f.kind==ST_RIP3_COMMAND_INVALID);
	}
	for (unsigned radix=36;radix<=64;radix+=28) {
		CHECK(append(&f,radix==36 ? "!|J10|n2\r" : "!|J1S|n2\r")==0 && pump(&f)==0);
		for (unsigned flags=0;flags<64;flags++) {
			char wire[100];CHECK(extended_wire(wire,sizeof(wire),radix,2,flags)==0);
			unsigned windows=f.windows;errors=f.errors;
			CHECK(append(&f,wire)==0 && pump(&f)==0 && f.windows==windows+1 && !f.identical
			    && f.window.flags==flags && f.window.wrap==((flags&1)!=0)
			    && st_rip3_session_text_active(f.session)==((flags&4)==0));
			CHECK(f.errors==errors+((flags&32)!=0)); /* TW0 cannot be protected. */
		}
	}
	CHECK(query(&f,"$ATW$","")==0 && st_rip3_session_text_active(f.session));
	f.window_status=IO_ERROR_OUT_OF_MEMORY;
	unsigned windows=f.windows;errors=f.errors;
	CHECK(append(&f,"!|b0A0KFo9i0e0P44\r")==0 && pump(&f)==0 && f.windows==windows+1
	    && f.errors==errors+1 && f.kind==ST_RIP3_COMMAND_RESOURCE_FAILURE && st_rip3_session_text_active(f.session));
	f.window_status=0;
	CHECK(append(&f,"!|J10|w0A050C0710\r")==0 && pump(&f)==0 && !f.identical && !f.window.extended);
	CHECK(append(&f,"!|w0A050C0710\r")==0 && pump(&f)==0 && f.identical);
	CHECK(query(&f,"$WORLD(CUR,0,0)$","")==0);windows=f.windows;errors=f.errors;
	CHECK(append(&f,"!|b0A0KS2H8140P48\r")==0 && pump(&f)==0 && f.windows==windows && f.errors==errors+1);
	CHECK(append(&f,"!|w0A050C0710\r")==0 && pump(&f)==0 && f.windows==windows+1 && !f.window.extended);
	CHECK(query(&f,"$WORLD(CUR,1024,768)$$RESX$","1024")==0);
	st_rip3_session_destroy(f.session);return 0;
}

static int
normal_window(void)
{
	struct fixture f={0};struct st_rip3_session_config options=config(&f,ST_RIP3_CANVAS_640_480);
	options.sink.text_window=window;
	CHECK(st_rip3_session_create(&f.session,&options)==0);
	CHECK(append(&f,"!|w0A050C0700\r")==0 && pump(&f)==0 && f.windows==1 && !f.identical
	    && f.window.x==80 && f.window.y==54 && f.window.columns==3 && f.window.rows==3
	    && f.window.cell_width==8 && f.window.cell_height==11 && !f.window.wrap);
	CHECK(append(&f,"!|w0000000000\r")==0 && pump(&f)==0 && !st_rip3_session_text_active(f.session));
	f.window_status=IO_ERROR_OUT_OF_MEMORY;
	CHECK(append(&f,"!|w0A050C0710\r")==0 && pump(&f)==0 && f.windows==2 && f.identical
	    && f.errors==1 && f.kind==ST_RIP3_COMMAND_RESOURCE_FAILURE && !st_rip3_session_text_active(f.session));
	CHECK(query(&f,"$RESX$","640")==0);
	f.window_status=0;
	CHECK(append(&f,"!|w0A050C0710\r")==0 && pump(&f)==0 && f.windows==3 && f.identical
	    && f.window.wrap && st_rip3_session_text_active(f.session));
	CHECK(append(&f,"!|w0A050C0715\r")==0 && pump(&f)==0 && f.windows==3 && f.errors==2
	    && f.kind==ST_RIP3_COMMAND_INVALID && st_rip3_session_text_active(f.session));
	CHECK(append(&f,"!|w0A050A0710\r")==0 && pump(&f)==0 && f.windows==4 && !f.identical && f.window.columns==1);
	CHECK(append(&f,"!|w00001B0M10\r")==0 && pump(&f)==0 && f.windows==5
	    && f.window.columns==48 && f.window.rows==23 && f.window.x==0 && f.window.y==0);
	st_rip3_session_destroy(f.session);
	for (unsigned width=2;width<=5;width++) for (unsigned base=0;base<2;base++) {
		struct fixture encoded={0};options=config(&encoded,ST_RIP3_CANVAS_640_480);
		options.sink.text_window=window;
		CHECK(st_rip3_session_create(&encoded.session,&options)==0);
		char setup[32];(void)snprintf(setup,sizeof(setup),"!|J%s|n%u\r",base ? "1S" : "10",width);
		CHECK(append(&encoded,setup)==0 && pump(&encoded)==0);
		/* 47 distinguishes MegaNum 1B from UltraNum 0l. */
		const unsigned values[]={10,5,47,22};char line[32]="!|w";
		for (unsigned i=0;i<4;i++) CHECK(st_rip3_number_encode(line+3+i*width,width,
		    values[i],width,base ? 64 : 36)==0);
		size_t at=3+4*width;memcpy(line+at,"10\r",4);
		CHECK(append(&encoded,line)==0 && pump(&encoded)==0 && encoded.errors==0 && encoded.windows==1
		    && encoded.window.columns==38 && encoded.window.rows==18 && encoded.window.x==80 && encoded.window.y==54);
		st_rip3_session_destroy(encoded.session);
	}
	return 0;
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
normalized_text_window(void)
{
	struct fixture f={0};struct st_rip3_session_config options=config(&f,ST_RIP3_CANVAS_640_480);
	options.sink.text_window=window;
	CHECK(st_rip3_session_create(&f.session,&options)==0);
	CHECK(append(&f,"!|w0A0F0K0P12\r")==0 && pump(&f)==0 && f.windows==1 && f.errors==1
	    && f.kind==ST_RIP3_COMMAND_COORDINATES_CLAMPED && f.window.x==80 && f.window.y==288
	    && f.window.columns==11 && f.window.rows==10 && f.window.y1==24);
	CHECK(append(&f,"!|w0K0O0A0F02\r")==0 && pump(&f)==0 && f.windows==2 && f.identical
	    && f.errors==1 && !f.window.wrap);
	CHECK(append(&f,"!|w0K0P0A0F12\r")==0 && pump(&f)==0 && f.windows==3 && f.identical
	    && f.errors==2 && f.kind==ST_RIP3_COMMAND_COORDINATES_CLAMPED);
	CHECK(append(&f,"!|w0A0F0K0P15\r")==0 && pump(&f)==0 && f.windows==3 && f.errors==3
	    && f.kind==ST_RIP3_COMMAND_INVALID);
	CHECK(append(&f,"!|wZZZZZZZZ14\r")==0 && pump(&f)==0 && f.windows==4 && !f.identical
	    && f.errors==4 && f.kind==ST_RIP3_COMMAND_COORDINATES_CLAMPED
	    && f.window.x0==39 && f.window.y0==24 && f.window.columns==1 && f.window.rows==1);
	CHECK(query(&f,"$RESX$","640")==0 && st_rip3_session_text_active(f.session));
	st_rip3_session_destroy(f.session);return 0;
}

static int
text_activation_variables(void)
{
	struct fixture f={0};struct st_rip3_session_config options=config(&f,ST_RIP3_CANVAS_640_480);
	CHECK(st_rip3_session_create(&f.session,&options)==0);
	CHECK(query(&f,"$DTW$","")==0 && !st_rip3_session_text_active(f.session));
	CHECK(query(&f,"$RESX$","640")==0);
	/* Undefined noncurrent slots do not define or reactivate a window. */
	CHECK(query(&f,"$ATW(1,35)$","")==0 && !st_rip3_session_text_active(f.session));
	CHECK(query(&f,"$ATW(0)$","")==0 && st_rip3_session_text_active(f.session));
	CHECK(append(&f,"!|1\0330000$DTW(CUR,36)$\r")==0 && pump(&f)==0
	    && f.errors==1 && f.kind==ST_RIP3_COMMAND_INVALID && st_rip3_session_text_active(f.session));
	CHECK(query(&f,"$dtw(ALL)$","")==0 && !st_rip3_session_text_active(f.session));
	CHECK(append(&f,"!|1\0330000$ATW(CUR,OTHER)$\r")==0 && pump(&f)==0
	    && f.errors==2 && !st_rip3_session_text_active(f.session));
	CHECK(query(&f,"$ATW( cur, 0, 0 )$","")==0 && st_rip3_session_text_active(f.session));
	CHECK(append(&f,"!|w0000000000\r")==0 && pump(&f)==0 && !st_rip3_session_text_active(f.session));
	CHECK(query(&f,"$ATW()$","")==0 && st_rip3_session_text_active(f.session));
	CHECK(query(&f,"$DTW(CUR)$","")==0 && !st_rip3_session_text_active(f.session));
	st_rip3_session_cancel(f.session);CHECK(!st_rip3_session_text_active(f.session));
	CHECK(query(&f,"$ATW(ALL)$","")==0 && st_rip3_session_text_active(f.session));
	st_rip3_session_destroy(f.session);return 0;
}

static int
world_state(void)
{
	struct fixture f={0};struct st_rip3_session_config options=config(&f,ST_RIP3_CANVAS_640_480);
	CHECK(st_rip3_session_create(&f.session,&options)==0);
	CHECK(append(&f,"!|n4|f1EKF1EKF|c0C|X0PA70PA7\r")==0 && pump(&f)==0 && f.errors==0);
	struct st_rip3_session_capture capture={0};
	CHECK(st_rip3_session_capture(f.session,&capture)==0 && capture.frame.world_width==65535);
	for (unsigned y=0;y<480;y++) for (unsigned x=0;x<640;x++)
		CHECK(capture.pixels->primary[(size_t)y*640+x]==(ST_PIXEL_COLOR_RGB_TAG|((x==319 && y==239) ? 0xff5555 : 0)));
	st_rip3_session_capture_dispose(&capture);
	CHECK(append(&f,"!|f00000000|L0000000000010001\r")==0 && pump(&f)==0 && f.errors==1);
	CHECK(st_rip3_session_capture(f.session,&capture)==0 && capture.frame.world_width==0 && capture.frame.world_height==0);
	CHECK(capture.pixels->primary[239*640+319]==(ST_PIXEL_COLOR_RGB_TAG|0xff5555));
	st_rip3_session_capture_dispose(&capture);
	CHECK(query(&f,"$WORLD$","0:0")==0 && query(&f,"$WORLD(CUR,65536,1)$$WORLD$","0:1")==0);
	CHECK(query(&f,"$WORLD(CUR,640,480)$$WORLD$","640:480")==0);
	CHECK(append(&f,"!|n2|X0101\r")==0 && pump(&f)==0 && f.errors==1);
	CHECK(st_rip3_session_capture(f.session,&capture)==0
	    && capture.pixels->primary[641]==(ST_PIXEL_COLOR_RGB_TAG|0xff5555));
	st_rip3_session_capture_dispose(&capture);st_rip3_session_destroy(f.session);return 0;
}

static int
keyboard_buttons(void)
{
	struct fixture f={0};struct st_rip3_session_config options=config(&f,ST_RIP3_CANVAS_640_480);
	options.sink.text_reset=reset_text;
	CHECK(st_rip3_session_create(&f.session,&options)==0);
	CHECK(query(&f,"$WORLD(CUR,640,480)$","")==0);
	CHECK(append(&f,"!|1B00000200ZK000F000F080700000F07000000|1U0A0A1E0U1T00ID=1:<>A<>A$FIELDID$|1U0A141E1O2P00ID=2:<>a<>a$FIELDID$\r")==0 && pump(&f)==0 && f.errors==0);
	CHECK(st_rip3_session_key(f.session,'A') && f.reply_length==2 && memcmp(f.reply,"a2",2)==0);
	f.reply_length=0;
	CHECK(st_rip3_session_key(f.session,'a') && f.reply_length==2 && memcmp(f.reply,"a2",2)==0);
	CHECK(query(&f,"$HKEYOFF$","")==0 && !st_rip3_session_key(f.session,'a'));
	CHECK(append(&f,"!|1\0330000$HKEYON(1)$\r")==0 && pump(&f)==0 && !st_rip3_session_key(f.session,'a'));
	CHECK(query(&f,"$HKEYON$","")==0);
	/* Reset deletes the source field before evaluating its copied action. */
	CHECK(append(&f,"!|1B00000200ZO000F000F080700000F07000000|1U0A0A1E0U2G00ID=8:<><>$FIELDID$:$CUR(BUT)$\r")==0 && pump(&f)==0);
	CHECK(st_rip3_session_key(f.session,'X') && f.resets==1 && f.reply_length==3 && memcmp(f.reply,"8:0",3)==0);
	CHECK(!st_rip3_session_mouse_active(f.session));
	CHECK(query(&f,"alive","alive")==0);
	st_rip3_session_destroy(f.session);return 0;
}

static int
enclosed_fields(void)
{
	struct fixture f={0};struct st_rip3_session_config options=config(&f,ST_RIP3_CANVAS_640_480);
	CHECK(st_rip3_session_create(&f.session,&options)==0);
	CHECK(query(&f,"$WORLD(CUR,640,480)$","")==0);
	CHECK(append(&f,"!|1M000A0A0K0K0000000simple|1B00000200ZK000F000F080700000F07000000|1U0U0U14140000<><>button\r")==0
	    && pump(&f)==0 && f.errors==0);
	/* Every byte passes the public void stream boundary independently. Flag
	 * 8 retains simple fields and removes only the enclosed button. */
	const char *wire="!|1k00001E1E0009\r";
	for (size_t i=0;i<strlen(wire);i++) {
		char byte[2]={wire[i],0};CHECK(append(&f,byte)==0);
		st_rip3_session_process(f.session,1);
	}
	CHECK(f.errors==0 && !st_rip3_session_mouse(f.session,35,35,true) && !st_rip3_session_mouse(f.session,35,35,false));
	CHECK(st_rip3_session_mouse(f.session,15,15,true) && st_rip3_session_mouse(f.session,15,15,false));
	CHECK(f.reply_length==6 && memcmp(f.reply,"simple",6)==0);f.reply_length=0;
	/* Malformed bodies, degenerate selectors and deferred painting flags
	 * leave the usable field intact and cannot terminate the owner. */
	CHECK(append(&f,"!|1kbad|1k000000000001|1k00001E1E000X\r")==0 && pump(&f)==0 && f.errors==3);
	CHECK(st_rip3_session_mouse(f.session,15,15,true) && st_rip3_session_mouse(f.session,15,15,false));
	CHECK(f.reply_length==6 && memcmp(f.reply,"simple",6)==0);f.reply_length=0;
	CHECK(query(&f,"$COORDSIZE(CUR,5)$$BASEMATH(CUR,64)$","")==0);
	CHECK(append(&f,"!|1k00000000000000e0000e0001\r")==0 && pump(&f)==0 && f.errors==3);
	CHECK(!st_rip3_session_mouse_active(f.session));
	CHECK(query(&f,"alive","alive")==0);
	st_rip3_session_destroy(f.session);return 0;
}

static int
plain_button(void)
{
	struct fixture f={0};struct st_rip3_session_config options=config(&f,ST_RIP3_CANVAS_640_480);
	options.sink.text_reset=reset_text;
	CHECK(st_rip3_session_create(&f.session,&options)==0);
	CHECK(query(&f,"$WORLD(CUR,640,480)$","")==0);
	CHECK(append(&f,"!|1B00000200ZO000F000F080700000F07000000|1U0A0A1O1O0000ID=7:<><>$FIELDID$:$CUR(BUT)$\r")==0
	    && pump(&f)==0 && f.errors==0 && f.resets==0 && st_rip3_session_mouse_active(f.session));
	CHECK(st_rip3_session_mouse(f.session,20,20,true) && f.reply_length==0);
	CHECK(st_rip3_session_mouse(f.session,20,20,false) && f.errors==0 && f.resets==1);
	CHECK(f.reply_length==3 && memcmp(f.reply,"7:0",3)==0 && !st_rip3_session_mouse_active(f.session));
	char template_wire[4200];const char *prefix="!|1\0330000[3:]";
	size_t prefix_length=strlen(prefix);memcpy(template_wire,prefix,prefix_length);
	memset(template_wire+prefix_length,'X',4096);template_wire[prefix_length+4096]='\r';template_wire[prefix_length+4097]=0;
	CHECK(append(&f,template_wire)==0 && pump(&f)==0 && f.errors==0);
	CHECK(query(&f,"$-=CLIPPED=old$","")==0);
	CHECK(append(&f,"!|1B00000200ZK000F000F080700000F07000000|1U0A0A1O1O0000<><>$?3$$D(60)$$-=CLIPPED=changed$$RAND(\"bad\")$\r")==0
	    && pump(&f)==0 && f.errors==0 && f.reply_length==0);
	CHECK(st_rip3_session_mouse(f.session,20,20,true) && st_rip3_session_mouse(f.session,20,20,false));
	CHECK(f.reply_length==4096 && f.errors==0 && !st_rip3_session_pending(f.session) && st_rip3_session_wait_ns(f.session)==0);
	for (size_t i=0;i<f.reply_length;i++) CHECK(f.reply[i]=='X');
	CHECK(query(&f,"$&CLIPPED$","old")==0);
	st_rip3_session_destroy(f.session);return 0;
}

static int
field_context(void)
{
	struct fixture f={0};struct st_rip3_session_config options=config(&f,ST_RIP3_CANVAS_640_480);
	CHECK(st_rip3_session_create(&f.session,&options)==0);
	CHECK(query(&f,"$WORLD(CUR,640,480)$","")==0);
	CHECK(append(&f,"!|1M000A0A1O1O0000000ID=7:$FIELDID$:$D(3)$$MKILL$$FIELDID$\r")==0 && pump(&f)==0);
	CHECK(st_rip3_session_mouse(f.session,20,20,true) && f.reply_length==0);
	CHECK(st_rip3_session_mouse(f.session,20,20,false) && st_rip3_session_pending(f.session));
	/* A logical handler resumes with its captured identity. A separate wire
	 * query queued during its wait has no callback context when it executes. */
	CHECK(append(&f,"!|1\0330000$FIELDID$outside\r")==0);
	f.now=UINT64_C(100000000);
	CHECK(pump(&f)==0 && !st_rip3_session_pending(f.session) && f.errors==1);
	CHECK(f.reply_length==10 && memcmp(f.reply,"7:7outside",10)==0);
	CHECK(!st_rip3_session_mouse_active(f.session));
	f.reply_length=0;
	CHECK(append(&f,"!|1M000A0A1O1O0000000ID=8:$D(3)$$FIELDID$\r")==0 && pump(&f)==0);
	CHECK(st_rip3_session_mouse(f.session,20,20,true));
	CHECK(st_rip3_session_mouse(f.session,20,20,false) && st_rip3_session_pending(f.session));
	st_rip3_session_cancel(f.session);
	CHECK(!st_rip3_session_pending(f.session));
	CHECK(append(&f,"!|1\0330000$FIELDID$outside\r")==0 && pump(&f)==0 && f.errors==2);
	CHECK(f.reply_length==7 && memcmp(f.reply,"outside",7)==0);
	st_rip3_session_destroy(f.session);return 0;
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
	struct st_rip3_text_window_operations windows={0};
	const struct st_rip3_host_variables owned={.text_windows=&windows};
	options.services=&owned;struct st_rip3_session *unchanged=f.session;
	CHECK(st_rip3_session_create(&unchanged,&options)==IO_ERROR_INVALID_ARGUMENT && unchanged==f.session
	    && st_rip3_session_test_live==live && f.reads==0 && f.errors==0);
	options.services=NULL;
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
	CHECK(keyboard_buttons()==0);
	CHECK(enclosed_fields()==0);
	CHECK(field_context()==0);
	CHECK(plain_button()==0);
	CHECK(world_state()==0);
	CHECK(soft_reset()==0);
	CHECK(graphics(ST_RIP3_CANVAS_640_480,640,480)==0 && graphics(ST_RIP3_CANVAS_800_600,800,600)==0
	    && graphics(ST_RIP3_CANVAS_1024_768,1024,768)==0);
	CHECK(literal_dixon(ST_RIP3_CANVAS_640_480,640,480,13)==0 && literal_dixon(ST_RIP3_CANVAS_800_600,800,600,16)==0
	    && literal_dixon(ST_RIP3_CANVAS_1024_768,1024,768,21)==0);
	CHECK(prompts_and_delays()==0 && cancellation_and_isolation()==0 && borrowed_user()==0
	    && normal_window()==0 && normalized_text_window()==0 && text_cursor()==0 && cursor_variables()==0 && text_erases()==0 && window_table()==0 && window_queries()==0 && extended_window()==0
	    && text_deactivation()==0 && text_activation_variables()==0
	    && construction(false)==0 && construction(true)==0);
	CHECK(st_rip3_session_test_live==0);return 0;
}
