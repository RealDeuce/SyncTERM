/* Source-supplied numbers/direct commands: independent literal ownership tests. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_graphics_parser.h"
#include "io_error.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while (0)

struct answer {int value,status;bool supplied;};
struct capture {
	struct answer answers[16];size_t answer_count,numbers,fields,boundaries,commands,unknown,text_size;
	struct st_graphics_argument arguments[8][8];size_t counts[8];uint8_t codes[8],payload[8][64],text[128];bool chains[8];
	const uint8_t *borrowed;size_t borrowed_size;
	int boundary_error,command_error;bool suspend,early;size_t raws;
};
static int text(void *context,const uint8_t *bytes,size_t size)
{
	struct capture *c=context;CHECK(size<=sizeof(c->text)-c->text_size);
	memcpy(c->text+c->text_size,bytes,size);c->text_size+=size;return 0;
}
static int boundary(void *context)
{
	struct capture *c=context;++c->boundaries;return c->boundary_error;
}
static int field(void *context,const struct st_graphics_command_view *v,const struct st_graphics_parameter *p)
{
	(void)v;(void)p;++((struct capture *)context)->fields;return 0;
}
static int number(void *context,const struct st_graphics_command_view *v,const struct st_graphics_parameter *p,int *value,bool *supplied)
{
	struct capture *c=context;CHECK(p->kind==ST_GRAPHICS_NUMBER && v->count<8);
	struct answer a=c->numbers<c->answer_count?c->answers[c->numbers]:(struct answer){0};
	++c->numbers;*supplied=a.supplied;*value=a.value;return a.status;
}
static int command(void *context,const struct st_graphics_command_view *v)
{
	struct capture *c=context;size_t index=c->commands++;CHECK(index<8 && v->count<=8);
	c->counts[index]=v->count;c->codes[index]=v->code;c->chains[index]=v->chained;
	size_t offset=0;
	for(size_t i=0;i<v->count;++i) {
		c->arguments[index][i]=v->arguments[i];
		if(v->arguments[i].kind!=ST_GRAPHICS_NUMBER) {
			CHECK(v->arguments[i].size<=sizeof(c->payload[index])-offset);
			if(v->arguments[i].size!=0) memcpy(c->payload[index]+offset,v->arguments[i].bytes,v->arguments[i].size);
			offset+=v->arguments[i].size;c->borrowed=v->arguments[i].bytes;c->borrowed_size=v->arguments[i].size;
		}
	}
	return c->command_error!=0?c->command_error:c->suspend?IO_ERROR_IN_PROGRESS:0;
}
static void unknown(void *context,uint8_t code) {(void)code;++((struct capture *)context)->unknown;}
static int next(void *context,const struct st_graphics_command_view *v,struct st_graphics_parameter *p)
{
	(void)context;*p=(struct st_graphics_parameter){.kind=ST_GRAPHICS_END};
	if(v->count==1) {
		const struct st_graphics_argument *a=&v->arguments[0];
		int size=a->has_value?a->value:(int)a->operand.value;
		if(size<0 || size>8) return IO_ERROR_OUT_OF_RANGE;
		*p=(struct st_graphics_parameter){.kind=ST_GRAPHICS_BYTES,.size=(size_t)size};
	}
	return 0;
}
static int raw(void *context,const struct st_graphics_command_view *v,const struct st_graphics_parameter *p,size_t offset,const uint8_t *bytes,size_t size,size_t *accepted,bool *end)
{
	struct capture *c=context;(void)bytes;
	CHECK(v->code=='V' && v->count==1 && p->kind==ST_GRAPHICS_BYTES);++c->raws;
	if(c->early) {
		CHECK(offset<2);
		*accepted=size>2-offset?2-offset:size;*end=offset+*accepted==2;
	}
	return 0;
}
static const uint8_t attention[]={'G','#'};
static const struct st_graphics_dialect dialect={.attention=attention,.attention_size=2,.chain_indicator=true,.chain='>',.number_digits=4};
static const struct st_graphics_parameter pair[]={{.kind=ST_GRAPHICS_NUMBER,.axis=ST_GRAPHICS_X},{.kind=ST_GRAPHICS_NUMBER,.axis=ST_GRAPHICS_Y}};
static const struct st_graphics_parameter scalar[]={{.kind=ST_GRAPHICS_NUMBER}};
static const struct st_graphics_parameter string[]={{.kind=ST_GRAPHICS_NUMBER},{.kind=ST_GRAPHICS_TEXT,.size=4,.terminator='@'}};
static const struct st_graphics_command commands[]={{'P',pair,2,NULL},{'S',scalar,1,NULL},{'V',scalar,1,next},{'T',string,2,NULL},{'Z',NULL,0,NULL}};
static int create(st_graphics_parser_t **p,struct st_graphics_parser_sink *sink,struct capture *c,size_t capacity)
{
	const struct st_graphics_parser_config cfg={.dialect=&dialect,.commands=commands,.command_count=5,.max_arguments=capacity,.max_payload=64};
	*sink=(struct st_graphics_parser_sink){.context=c,.text=text,.command=command,.field=field,.number=number,.boundary=boundary,.unknown=unknown,.raw=raw};
	return st_graphics_parser_create(p,&cfg);
}
static int feed(st_graphics_parser_t *p,struct st_graphics_parser_sink *sink,const void *bytes,size_t size)
{
	size_t used;CHECK(st_graphics_parser_feed(p,sink,bytes,size,&used)==0 && used==size);return 0;
}
static int mixed_partitions(void)
{
	static const uint8_t wire[]="G#P 7,tailG#S 4:";
	for(size_t split=0;split<sizeof(wire);++split) {
		st_graphics_parser_t *p=NULL;struct st_graphics_parser_sink sink;struct capture c={0};
		c.answers[0]=(struct answer){.value=17,.supplied=true};c.answer_count=1;
		CHECK(create(&p,&sink,&c,8)==0);
		CHECK(feed(p,&sink,wire,split)==0 && feed(p,&sink,wire+split,sizeof(wire)-1-split)==0);
		CHECK(c.commands==2 && c.fields==3 && c.numbers==3 && c.boundaries==2);
		CHECK(c.codes[0]=='P' && c.counts[0]==2 && !c.chains[0]);
		CHECK(c.arguments[0][0].has_value && c.arguments[0][0].value==17 && c.arguments[0][0].axis==ST_GRAPHICS_X);
		CHECK(!c.arguments[0][1].has_value && c.arguments[0][1].operand.value==7 && c.arguments[0][1].axis==ST_GRAPHICS_Y);
		CHECK(c.codes[1]=='S' && c.arguments[1][0].operand.value==4);
		CHECK(c.text_size==4 && memcmp(c.text,"tail",4)==0 && !st_graphics_parser_in_command(p));
		st_graphics_parser_destroy(p);
	}
	st_graphics_parser_t *p=NULL;struct st_graphics_parser_sink sink;struct capture c={0};
	c.answers[0]=(struct answer){.value=17,.supplied=true};c.answer_count=1;CHECK(create(&p,&sink,&c,8)==0);
	for(size_t i=0;i<sizeof(wire)-1;++i) CHECK(feed(p,&sink,wire+i,1)==0);
	CHECK(c.commands==2 && c.fields==3 && c.numbers==3 && c.text_size==4);
	CHECK(c.arguments[0][0].has_value && c.arguments[0][0].value==17 && c.arguments[0][1].operand.value==7);
	st_graphics_parser_destroy(p);return 0;
}
static int waits_and_fallback(void)
{
	st_graphics_parser_t *p=NULL;struct st_graphics_parser_sink sink;struct capture c={0};
	c.answers[0].status=c.answers[1].status=IO_ERROR_IN_PROGRESS;
	c.answers[2]=(struct answer){.value=-32768,.supplied=true};c.answer_count=3;
	CHECK(create(&p,&sink,&c,8)==0);size_t used;
	CHECK(st_graphics_parser_feed(p,&sink,(const uint8_t *)"G#P 9,",6,&used)==IO_ERROR_IN_PROGRESS && used==4);
	CHECK(c.fields==1 && c.numbers==1 && c.commands==0 && st_graphics_parser_in_command(p));
	CHECK(st_graphics_parser_resume(p)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_graphics_parser_feed(p,&sink,(const uint8_t *)"9,",2,&used)==IO_ERROR_IN_PROGRESS && used==0);
	CHECK(c.fields==1 && c.numbers==2 && c.commands==0);
	CHECK(feed(p,&sink,NULL,0)==0 && c.fields==2 && c.numbers==4 && c.commands==0);
	CHECK(feed(p,&sink,"9,",2)==0 && c.commands==1);
	CHECK(c.arguments[0][0].has_value && c.arguments[0][0].value==-32768 && !c.arguments[0][1].has_value && c.arguments[0][1].operand.value==9);
	st_graphics_parser_destroy(p);
	/* A declined field keeps wire ownership across input splits even after
	 * another source becomes available. Only its next field can use it. */
	c=(struct capture){0};CHECK(create(&p,&sink,&c,8)==0);
	CHECK(feed(p,&sink,"G#P 1",5)==0 && c.numbers==1 && c.fields==1);
	c.answers[1]=(struct answer){.value=42,.supplied=true};c.answer_count=2;
	CHECK(feed(p,&sink,"2,3,",4)==0 && c.commands==1 && c.numbers==2 && c.fields==2);
	CHECK(!c.arguments[0][0].has_value && c.arguments[0][0].operand.value==12);
	CHECK(c.arguments[0][1].has_value && c.arguments[0][1].value==42);
	CHECK(c.text_size==2 && memcmp(c.text,"3,",2)==0);st_graphics_parser_destroy(p);
	/* EOF/cancel never fabricates a value for a waiting source. */
	c=(struct capture){0};c.answers[0].status=IO_ERROR_IN_PROGRESS;c.answer_count=1;CHECK(create(&p,&sink,&c,8)==0);
	CHECK(st_graphics_parser_feed(p,&sink,(const uint8_t *)"G#S ",4,&used)==IO_ERROR_IN_PROGRESS && used==4);
	CHECK(st_graphics_parser_finish(p,&sink)==IO_ERROR_PROTOCOL && c.commands==0 && !st_graphics_parser_in_command(p));
	CHECK(feed(p,&sink,"G#S 6:",6)==0 && c.commands==1 && c.arguments[0][0].operand.value==6);
	st_graphics_parser_cancel(p);CHECK(!st_graphics_parser_in_command(p));st_graphics_parser_destroy(p);return 0;
}
static int payloads_and_errors(void)
{
	static const uint8_t wire[]={'G','#','V',' ',0,'G','#','x'};
	for(size_t split=0;split<=sizeof(wire);++split) {
		st_graphics_parser_t *p=NULL;struct st_graphics_parser_sink sink;struct capture c={0};
		c.answers[0]=(struct answer){.value=3,.supplied=true};c.answer_count=1;CHECK(create(&p,&sink,&c,8)==0);
		CHECK(feed(p,&sink,wire,split)==0 && feed(p,&sink,wire+split,sizeof(wire)-split)==0);
		CHECK(c.commands==1 && c.numbers==1 && c.fields==2 && c.counts[0]==2);
		CHECK(c.arguments[0][1].kind==ST_GRAPHICS_BYTES && !c.arguments[0][1].has_value && c.arguments[0][1].size==3);
		CHECK(memcmp(c.payload[0],"\0G#",3)==0 && c.text_size==1 && c.text[0]=='x');st_graphics_parser_destroy(p);
	}
	st_graphics_parser_t *p=NULL;struct st_graphics_parser_sink sink;struct capture c={0};
	c.answers[0]=(struct answer){.value=0,.supplied=true};c.answer_count=1;CHECK(create(&p,&sink,&c,8)==0);
	CHECK(feed(p,&sink,"G#T G#@x",8)==0 && c.numbers==1 && c.commands==1 && c.fields==2);
	CHECK(c.arguments[0][1].kind==ST_GRAPHICS_TEXT && c.arguments[0][1].size==2 && memcmp(c.payload[0],"G#",2)==0 && c.text_size==1);
	st_graphics_parser_destroy(p);
	/* Incremental raw early-end keeps its suffix on the wire even with a
	 * supplied length. Both fields remain in the same final command view. */
	c=(struct capture){0};c.early=true;c.answers[0]=(struct answer){.value=3,.supplied=true};c.answer_count=1;
	CHECK(create(&p,&sink,&c,8)==0);CHECK(feed(p,&sink,wire,sizeof(wire))==0);
	CHECK(c.commands==1 && c.numbers==1 && c.raws==1 && c.arguments[0][1].size==2);
	CHECK(memcmp(c.payload[0],"\0G",2)==0 && c.text_size==2 && memcmp(c.text,"#x",2)==0);st_graphics_parser_destroy(p);
	c=(struct capture){0};c.answers[0]=(struct answer){.value=0,.supplied=true};c.answer_count=1;
	CHECK(create(&p,&sink,&c,8)==0);CHECK(st_graphics_parser_start_command(p,&sink,'V',false)==0);
	CHECK(feed(p,&sink,NULL,0)==0 && c.commands==1 && c.fields==2 && c.numbers==1 && c.raws==0 && c.arguments[0][1].size==0);
	st_graphics_parser_destroy(p);
	const int errors[]={IO_ERROR_OUT_OF_RANGE,IO_ERROR_IO};
	for(size_t i=0;i<sizeof(errors)/sizeof(*errors);++i) {
		c=(struct capture){0};c.answers[0]=(struct answer){.value=17,.supplied=true};
		c.answers[1].status=errors[i];c.answer_count=2;CHECK(create(&p,&sink,&c,8)==0);size_t used;
		CHECK(st_graphics_parser_feed(p,&sink,(const uint8_t *)"G#P 77,",7,&used)==errors[i] && used==4);
		CHECK(c.commands==0 && c.numbers==2 && c.fields==2 && !st_graphics_parser_in_command(p));
		CHECK(feed(p,&sink,"77,",3)==0 && c.text_size==3);st_graphics_parser_destroy(p);
	}
	c=(struct capture){0};c.answers[0]=(struct answer){.status=IO_ERROR_IN_PROGRESS,.supplied=true};c.answer_count=1;
	CHECK(create(&p,&sink,&c,8)==0);size_t used;
	CHECK(st_graphics_parser_feed(p,&sink,(const uint8_t *)"G#S ",4,&used)==IO_ERROR_PROTOCOL && used==4 && !st_graphics_parser_in_command(p));st_graphics_parser_destroy(p);
	/* Limits still apply to source values and dynamic descriptors. */
	c=(struct capture){0};c.answers[0]=(struct answer){.value=17,.supplied=true};c.answer_count=1;CHECK(create(&p,&sink,&c,1)==0);
	CHECK(st_graphics_parser_feed(p,&sink,(const uint8_t *)"G#P ",4,&used)==IO_ERROR_OUT_OF_RANGE && used==4 && c.fields==1 && c.numbers==1);st_graphics_parser_destroy(p);
	c=(struct capture){0};c.answers[0]=(struct answer){.value=-1,.supplied=true};c.answer_count=1;CHECK(create(&p,&sink,&c,8)==0);
	CHECK(st_graphics_parser_feed(p,&sink,(const uint8_t *)"G#V ",4,&used)==IO_ERROR_OUT_OF_RANGE && used==4 && c.commands==0);st_graphics_parser_destroy(p);return 0;
}
static int direct_commands(void)
{
	st_graphics_parser_t *p=NULL;struct st_graphics_parser_sink sink;struct capture c={0};
	c.answers[0]=(struct answer){.value=INT_MIN,.supplied=true};c.answers[1]=(struct answer){.value=INT_MAX,.supplied=true};c.answer_count=2;
	CHECK(create(&p,&sink,&c,8)==0);
	CHECK(st_graphics_parser_start_command(p,&sink,'P',false)==0 && c.boundaries==1 && c.fields==0 && c.commands==0);
	CHECK(feed(p,&sink,NULL,0)==0 && c.commands==1 && c.numbers==2 && c.fields==2);
	CHECK(c.arguments[0][0].has_value && c.arguments[0][0].value==INT_MIN && c.arguments[0][1].has_value && c.arguments[0][1].value==INT_MAX);
	CHECK(!st_graphics_parser_in_command(p));
	CHECK(st_graphics_parser_start_command(p,&sink,'Z',true)==0 && c.boundaries==2);
	CHECK(feed(p,&sink,"S 4:",4)==0 && c.commands==3 && c.boundaries==3);
	CHECK(c.codes[1]=='Z' && c.chains[1] && c.counts[1]==0 && c.codes[2]=='S' && !c.chains[2] && c.arguments[2][0].operand.value==4);
	CHECK(st_graphics_parser_start_command(p,&sink,'?',false)==IO_ERROR_NOT_SUPPORTED && c.boundaries==3 && c.unknown==0 && !st_graphics_parser_in_command(p));
	CHECK(st_graphics_parser_start_command(NULL,&sink,'S',false)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_graphics_parser_start_command(p,NULL,'S',false)==IO_ERROR_INVALID_ARGUMENT);st_graphics_parser_destroy(p);
	/* Ordinary held attention, framing and partial numbers are not replaced. */
	c=(struct capture){0};CHECK(create(&p,&sink,&c,8)==0);
	CHECK(feed(p,&sink,"G",1)==0 && st_graphics_parser_start_command(p,&sink,'S',false)==IO_ERROR_BUSY);
	CHECK(feed(p,&sink,"xG#",3)==0 && c.text_size==2 && memcmp(c.text,"Gx",2)==0);
	CHECK(st_graphics_parser_start_command(p,&sink,'S',false)==IO_ERROR_BUSY && c.boundaries==1);
	CHECK(feed(p,&sink,"S 1",3)==0 && st_graphics_parser_start_command(p,&sink,'Z',false)==IO_ERROR_BUSY);
	CHECK(feed(p,&sink,"2,",2)==0 && c.commands==1 && c.arguments[0][0].operand.value==12);
	/* With no source callback the same direct descriptor uses normal input. */
	sink.number=NULL;CHECK(st_graphics_parser_start_command(p,&sink,'S',false)==0);
	CHECK(feed(p,&sink,"8,",2)==0 && c.commands==2 && !c.arguments[1][0].has_value && c.arguments[1][0].operand.value==8);
	st_graphics_parser_destroy(p);return 0;
}
static int suspended_selection(void)
{
	st_graphics_parser_t *p=NULL;struct st_graphics_parser_sink sink;struct capture c={0};
	c.suspend=true;c.answers[0]=(struct answer){.value=3,.supplied=true};c.answer_count=1;CHECK(create(&p,&sink,&c,8)==0);
	CHECK(st_graphics_parser_start_command(p,&sink,'V',false)==0);size_t used;
	CHECK(st_graphics_parser_feed(p,&sink,(const uint8_t *)"\0G#tail",7,&used)==IO_ERROR_IN_PROGRESS && used==3 && c.commands==1);
	CHECK(c.borrowed_size==3 && memcmp(c.borrowed,"\0G#",3)==0);
	const uint8_t *borrowed=c.borrowed;size_t boundaries=c.boundaries;
	CHECK(st_graphics_parser_start_command(p,&sink,'?',false)==IO_ERROR_NOT_SUPPORTED && c.boundaries==boundaries);
	CHECK(c.borrowed==borrowed && memcmp(borrowed,"\0G#",3)==0);
	c.boundary_error=IO_ERROR_IO;CHECK(st_graphics_parser_start_command(p,&sink,'S',false)==IO_ERROR_IO);
	CHECK(c.commands==1 && c.fields==2 && c.numbers==1 && c.borrowed==borrowed && memcmp(borrowed,"\0G#",3)==0);
	c.boundary_error=IO_ERROR_IN_PROGRESS;CHECK(st_graphics_parser_start_command(p,&sink,'S',false)==IO_ERROR_PROTOCOL);
	CHECK(memcmp(borrowed,"\0G#",3)==0 && c.commands==1);
	c.boundary_error=0;CHECK(st_graphics_parser_resume(p)==0 && !st_graphics_parser_in_command(p));
	CHECK(feed(p,&sink,"tail",4)==0 && c.text_size==4);st_graphics_parser_destroy(p);
	/* Successful replacement ends the previous borrowed view and starts a
	 * new descriptor without calling the accepted command a second time. */
	c=(struct capture){0};c.suspend=true;c.answers[0]=(struct answer){.value=3,.supplied=true};
	c.answers[1]=(struct answer){.value=-9,.supplied=true};c.answer_count=2;CHECK(create(&p,&sink,&c,8)==0);
	CHECK(st_graphics_parser_start_command(p,&sink,'V',true)==0);
	CHECK(st_graphics_parser_feed(p,&sink,(const uint8_t *)"abc",3,&used)==IO_ERROR_IN_PROGRESS && used==3 && c.commands==1);
	c.suspend=false;CHECK(st_graphics_parser_start_command(p,&sink,'S',false)==0 && c.commands==1);
	CHECK(feed(p,&sink,NULL,0)==0 && c.commands==2 && c.codes[1]=='S' && !c.chains[1]);
	CHECK(c.arguments[1][0].has_value && c.arguments[1][0].value==-9 && c.fields==3 && c.numbers==2 && !st_graphics_parser_in_command(p));
	st_graphics_parser_destroy(p);return 0;
}
int main(void)
{
	CHECK(mixed_partitions()==0);CHECK(waits_and_fallback()==0);CHECK(payloads_and_errors()==0);
	CHECK(direct_commands()==0);CHECK(suspended_selection()==0);return 0;
}
