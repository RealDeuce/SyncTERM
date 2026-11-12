/* IGS & setup, numeric evaluation and local delay ownership. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_igs.h"
#include "cterm_igs_loop.h"
#include "st_graphics_parser.h"
#include "io_error.h"
#include <limits.h>
#include <string.h>

static const struct st_graphics_parameter numeric={.kind=ST_GRAPHICS_NUMBER};
static const struct st_graphics_parameter byte={.kind=ST_GRAPHICS_BYTES,.size=1};
static const struct st_graphics_parameter text={.kind=ST_GRAPHICS_TEXT,.size=128,.terminator='@',.skip_crlf=true,.empty_is_terminator=true};
static size_t count_index(const struct cterm_igs_loop *l) {return l->ganged?7:6;}
static size_t clamp_count(int value) {return value<1?1:value>2048?2048:(size_t)value;}
static bool varying(int value) {return value==-32767 || value==-32766 || (value>-30000 && value<0);}
/* Reject arithmetic that would make an intended bounded loop wrap into an
 * accidental nonterminating foreign-word sequence. Other IGS word math
 * retains its separately specified arithmetic contract. */
static int word(int value,int *result)
{
	if(value<INT16_MIN || value>INT16_MAX) return IO_ERROR_OUT_OF_RANGE;
	*result=value;return 0;
}
int cterm_igs_loop_next(void *context,const struct st_graphics_command_view *v,struct st_graphics_parameter *p)
{
	struct cterm_igs *igs=context;struct cterm_igs_loop *l=&igs->loop;
	*p=(struct st_graphics_parameter){.kind=ST_GRAPHICS_END};
	if(v->count==4) {*p=byte;return 0;}
	bool gang=v->arguments[4].bytes[0]=='>';
	size_t kind_index=gang?6:5, ci=kind_index+1;
	if(gang && v->count==5) {*p=text;return 0;}
	if(v->count==kind_index) {*p=byte;return 0;}
	if(v->count==ci) {*p=numeric;return 0;}
	size_t count=l->count;
	if(!l->loading) {
		struct cterm_igs preview=*igs;int value;
		int status=cterm_igs_argument_value(&preview,&v->arguments[ci],&value);
		if(status!=0) return status;
		count=clamp_count(value);
	}
	if(v->count<ci+1+count) *p=numeric;
	return 0;
}
static int store_operand(struct cterm_igs *igs,const struct st_graphics_command_view *v)
{
	struct cterm_igs_loop *l=&igs->loop;size_t first=count_index(l)+1;
	if(v->count>first && l->loaded<v->count-first) {
		int value;int status=cterm_igs_argument_value(igs,&v->arguments[v->count-1],&value);
		if(status!=0) return status;
		if(l->loaded>=l->count || l->loaded>=2048) return IO_ERROR_PROTOCOL;
		l->operands[l->loaded++]=(int16_t)value;
	}
	return 0;
}
int cterm_igs_loop_field(struct cterm_igs *igs,const struct st_graphics_command_view *v,const struct st_graphics_parameter *p)
{
	struct cterm_igs_loop *l=&igs->loop;
	if(v->code!='&') {
		if(l->run && (p->kind==ST_GRAPHICS_NUMBER || p->kind==ST_GRAPHICS_TEXT)) l->bad=true;
		return 0;
	}
	if(v->count==0) {
		igs->loop_mode=0;l->initializing=true;l->loading=false;l->loaded=0;
		l->run=l->next=l->selecting=l->waiting=l->cooperative=false;l->delay_started=false;
		return 0;
	}
	if(v->count<=4) {
		int value;int status=cterm_igs_argument_value(igs,&v->arguments[v->count-1],&value);
		if(status!=0) return status;
		if(v->count==1) l->current=value;
		else if(v->count==2) l->to=value;
		else if(v->count==3) l->step=value;
		else {
			l->delay=value;
			if(l->current>l->to) {status=word(-l->step,&l->step);if(status!=0) return status;}
			if(l->step==0) l->step=1;
		}
		return 0;
	}
	if(v->count==5) {l->target=v->arguments[4].bytes[0];l->ganged=l->target=='>';}
	if(l->ganged && v->count==6) {
		const struct st_graphics_argument *a=&v->arguments[5];
		if(a->size>128) return IO_ERROR_PROTOCOL;
		memcpy(igs->shared_text,a->bytes,a->size);igs->shared_text[a->size]=0;igs->shared_text_size=a->size;
		memcpy(l->gang,igs->shared_text,sizeof(l->gang));igs->text_deadline=0;
	}
	size_t ci=count_index(l);
	if(v->count==ci) igs->loop_kind=v->arguments[ci-1].bytes[0];
	if(v->count==ci+1) {
		int value;int status=cterm_igs_argument_value(igs,&v->arguments[ci],&value);
		if(status!=0) return status;
		l->count=clamp_count(value);l->original=l->current;l->loading=true;
	}
	int status=l->loading?store_operand(igs,v):0;
	if(status==0 && p->kind==ST_GRAPHICS_TEXT) {
		uint64_t now;status=igs->now_ms(&now);
		if(status==0) igs->text_deadline=now>UINT64_MAX-5000?UINT64_MAX:now+5000;
	}
	return status;
}
int cterm_igs_loop_begin(struct cterm_igs *igs,const struct st_graphics_command_view *v)
{
	struct cterm_igs_loop *l=&igs->loop;
	int status=store_operand(igs,v);
	if(status!=0) return status;
	if(!l->loading || l->loaded!=l->count) return IO_ERROR_PROTOCOL;
	l->loading=l->initializing=false;l->index=0;l->first=l->pass=0;
	l->no_parameters=true;
	for(size_t i=0;i<l->count;++i) if(varying(l->operands[i])) l->no_parameters=false;
	igs->loop_mode=1;igs->text_counter=0;
	l->run=l->next=l->initial=l->did_flow=true;l->chained=v->chained;l->bad=false;
	l->command=l->target;
	status=cterm_igs_flow(igs,false);
	return status==0?IO_ERROR_IN_PROGRESS:status;
}
int cterm_igs_loop_value(struct cterm_igs *igs,int *result)
{
	struct cterm_igs_loop *l=&igs->loop;
	if(igs->loop_mode!=1 || l->count==0 || l->index>=l->count) return IO_ERROR_PROTOCOL;
	l->waiting=false;
	if(l->index==0) {
		uint64_t now;int status=igs->now_ms(&now);if(status!=0) return status;
		if(!l->delay_started) {
			l->varying=0;l->delay_started=true;
			uint64_t duration=(uint64_t)(uint16_t)l->delay*5;
			l->deadline=now>UINT64_MAX-duration?UINT64_MAX:now+duration;
		}
		if(igs->input.key_count==0 && now<l->deadline) {l->waiting=true;return IO_ERROR_IN_PROGRESS;}
	}
	int operand=l->operands[l->index],value=operand,status=0;
	if(varying(operand)) {
		if(l->varying<INT_MAX) ++l->varying;
		if(l->pass==0) l->saved=l->current;
		if(l->varying<2 && l->first!=0) {
			status=word(l->current+l->step,&l->current);if(status!=0) return status;
			if(l->pass<INT_MAX) ++l->pass;
			if(l->pass==1 && igs->loop_kind=='|') l->current=l->saved;
			if(l->pass==2 && igs->loop_kind=='|') l->pass=0;
		}
		if(operand < -29999) value=l->current;
		if(l->first<INT_MAX) ++l->first;
		if(operand==-32766) {
			status=word(l->original<l->to?l->to-l->current:l->original>l->to?l->original-l->current:0,&value);
			if(status!=0) return status;
		}
		if(value>-10000 && value<0) status=word(-value+l->current,&value);
		if(status==0 && value>-20000 && value<-9999) {int n=value+10000;if(n<0) n=-n;status=word(l->current-n,&value);}
		if(status==0 && value>-30000 && value<-19999) {int n=value+20000;if(n<0) n=-n;status=word(n-l->current,&value);}
		if(status!=0) return status;
	}
	if(igs->input.key_count!=0) {int key=cterm_igs_take_key(igs);if(key==3 || key==24) igs->loop_mode=0;}
	if(l->no_parameters && l->index==0) {status=word(l->current+l->step,&l->current);if(status!=0) return status;}
	if(l->index==l->count-1 && ((l->step>=1 && l->current>=l->to) || (l->step<0 && l->current<=l->to))) igs->loop_mode=0;
	if(igs->loop_kind!='@' && igs->loop_mode==0 && l->command=='W') igs->loop_kind='Z';
	l->index=(l->index+1)%l->count;l->delay_started=false;l->bad=true;
	for(unsigned r=0;r<2;++r) if(value==(-32765+(int)r)) {
		const struct st_graphics_argument a={.kind=ST_GRAPHICS_NUMBER,.operand={.source=r==0?ST_GRAPHICS_RANDOM_R:ST_GRAPHICS_RANDOM_R2}};
		status=cterm_igs_argument_value(igs,&a,&value);if(status!=0) return status;
	}
	*result=value;return 0;
}
int cterm_igs_loop_number(void *context,const struct st_graphics_command_view *v,const struct st_graphics_parameter *p,int *value,bool *supplied)
{
	(void)p;struct cterm_igs *igs=context;struct cterm_igs_loop *l=&igs->loop;
	*supplied=false;
	/* Flow/text primitives can be exercised without a stored source.
	 * A completed & always has count>=1; absent array state supplies no
	 * numeric values and leaves the field with its ordinary wire owner. */
	if(igs->loop_mode!=1 || l->initializing || l->count==0) return 0;
	if(!l->run) {l->run=true;l->chained=v->chained;l->command=l->target=v->code;l->ganged=false;l->did_flow=false;l->bad=false;}
	if(l->work>=256) {l->cooperative=l->waiting=true;return IO_ERROR_IN_PROGRESS;}
	int status=cterm_igs_loop_value(igs,value);
	if(status==0) {++l->work;*supplied=true;}
	return status;
}
int cterm_igs_loop_end(struct cterm_igs *igs)
{
	struct cterm_igs_loop *l=&igs->loop;bool flow=l->did_flow;
	l->run=l->next=l->selecting=l->waiting=l->cooperative=l->did_flow=false;
	l->delay_started=false;l->ganged=false;igs->loop_kind=' ';
	return flow?cterm_igs_flow(igs,true):0;
}
int cterm_igs_loop_cancel(struct cterm_igs *igs)
{
	igs->loop_mode=0;igs->loop.initializing=igs->loop.loading=false;
	return cterm_igs_loop_end(igs);
}
