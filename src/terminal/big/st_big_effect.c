/* BIG14/66: all twenty original procedural speaker effects. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_effect.h"
#include "st_big_random.h"
#include "io_error.h"
uint16_t st_big_effect_selector(uint16_t word, bool equal)
{
	if (word>19) word=19;
	return !equal && word>9?9:word;
}
int st_big_effect_begin(struct st_big_effect *e, uint16_t selector, bool enabled)
{
	if (e==NULL) return IO_ERROR_INVALID_ARGUMENT;
	*e=(struct st_big_effect){.selector=selector,.enabled=enabled}; return 0;
}
static struct st_big_effect_event frequency(int value)
{
	return (struct st_big_effect_event){ST_BIG_EFFECT_FREQUENCY,(uint16_t)value};
}
static struct st_big_effect_event delay(unsigned value)
{
	return (struct st_big_effect_event){ST_BIG_EFFECT_DELAY,(uint16_t)value};
}
int st_big_effect_next(struct st_big_effect *e, uint32_t *rng,
    struct st_big_effect_event *event, bool *complete)
{
	if (e==NULL || event==NULL || complete==NULL) return IO_ERROR_INVALID_ARGUMENT;
	*complete=false;
	const struct st_big_effect_event stop={ST_BIG_EFFECT_STOP,0};
	if (!e->enabled) { *complete=true; return 0; }
	if (e->position==0) { *event=stop; ++e->position; return 0; }
	/* These row counts include the original last executed loop value;
	 * effects4/5/6 end at their own last Z, without a second case-tail Z. */
	static const unsigned rows[]={24,24,19,5,50,138,86,32700,20,20,39,25,162,33,80,1,250,1000,64,10};
	static const unsigned width[]={2,2,20,4,4,5,3,1,8,8,8,8,3,6,4,9,6,4,4,8};
	unsigned selector=e->selector;
	if (selector>19) { *complete=true; return 0; }
	uint32_t body=e->position-1, count=rows[selector]*width[selector];
	if (body>=count) {
		if (body==count && selector!=4 && selector!=5 && selector!=6 && selector!=15) {
			*event=stop; ++e->position; return 0;
		}
		*complete=true; return 0;
	}
	unsigned row=body/width[selector], slot=body%width[selector];
	struct st_big_effect_event out=stop;
	int pitch;
	switch (selector) {
	case 0: out=slot==0?delay(5):frequency(1450-(int)row*50); break;
	case 1: out=slot==0?delay(5):frequency(250+(int)row*50); break;
	case 2:
		if (slot%2) out=delay((row+1)/2);
		else out=frequency(slot==0?900:24-(int)slot);
		break;
	case 3: out=slot%2?delay(93):frequency(slot==0?660:990); break;
	case 4:
		if (slot==0) out=frequency(3000-(int)row*60);
		else if (slot==1) out=delay(2);
		else if (slot==3) out=delay(1);
		break;
	case 5: {
		unsigned part=row%23;
		pitch=part<14?750-(int)part*50:10+(int)(part-14)*80;
		if (slot==0) out=frequency(part<14?pitch:pitch+900);
		else if (slot==1) out=delay(5);
		else if (slot==2) out=frequency(part<14?160:pitch);
		else if (slot==3) out=delay(3);
		break;
	}
	case 6:
		pitch=row/2<27?6000-(int)(row/2)*150:2000+(int)(row/2-27)*250;
		if (slot==0) out=frequency(pitch); else if (slot==1) out=delay(5);
		break;
	case 7:
		if (rng==NULL) return IO_ERROR_INVALID_ARGUMENT;
		out=frequency((st_big_random_next(rng)%1280)<<4); break;
	case 8:
	case 9: {
		static const int offset[]={1700,1675,1650,1675};
		pitch=selector==8?0:(int)row*75;
		out=slot%2?delay(selector==8?18:7):frequency(pitch+offset[slot/2]); break;
	}
	case 10: {
		static const int offset[]={1200,1175,1150,1175};
		out=slot%2?delay(7):frequency(3000-(int)row*75+offset[slot/2]); break;
	}
	case 11:
		pitch=(int)row*75;
		if (slot==0) out=frequency(2100-pitch);
		else if (slot==1) out=frequency(pitch+1975);
		else if (slot==2 || slot==4) out=delay(2);
		else if (slot==5) out=frequency(pitch+1650);
		else if (slot==6) out=frequency(1675-pitch);
		else if (slot==7) out=delay(3);
		break;
	case 12:
		out=slot==1?delay(2):frequency(slot==0?4100-(int)row*25:4100+(int)row*25); break;
	case 13:
		pitch=3000+(int)row*32;
		if (slot==0) out=frequency(pitch+4400);
		else if (slot==1 || slot==5) out=delay(7);
		else if (slot==3) out=delay(1);
		else if (slot==4) out=frequency(4400-pitch);
		break;
	case 14: out=slot%2?delay(5):frequency(slot==0?(int)row:100-(int)row); break;
	case 15:
		if (slot==0) out=frequency(1440);
		else if (slot==1) out=delay(50);
		else if (slot==2) out=frequency(770);
		else if (slot==3 || slot==7) out=delay(25);
		else if (slot==5) out=delay(75);
		else if (slot==6) out=frequency(330);
		break;
	case 16:
		if (slot==0) out=frequency(40);
		else if (slot==1) out=delay(6);
		else if (slot==3) out=delay(2);
		else if (slot==4) out=frequency(9660+(int)row*4);
		else if (slot==5) out=delay(3);
		break;
	case 17:
	case 18:
		if (slot==0) out=frequency(selector==17?1660+(int)row:16660-(int)row*222);
		else if (slot==1) out=delay(1);
		else if (slot==3) out=delay(selector==17?3:2);
		break;
	case 19:
		if (slot==0) out=frequency(1440);
		else if (slot==2) out=frequency(770);
		else if (slot==6) out=frequency(330);
		else if (slot%2) out=delay(row*2+1);
		break;
	default: return IO_ERROR_PROTOCOL;
	}
	*event=out; ++e->position; return 0;
}
int st_big_effect_player_begin(struct st_big_effect_player *p, uint16_t selector,
    bool enabled, uint32_t rng)
{
	if (p==NULL) return IO_ERROR_INVALID_ARGUMENT;
	*p=(struct st_big_effect_player){.random_state=rng,.active=true};
	return st_big_effect_begin(&p->sequence,selector,enabled);
}
static int prepare(struct st_big_effect_player *p, struct st_big_speaker *s)
{
	while (p->active && p->remaining==0) {
		struct st_big_effect_event event; bool complete;
		int status=st_big_effect_next(&p->sequence,&p->random_state,&event,&complete);
		if (status!=0) return status;
		if (complete) { p->active=false; break; }
		switch (event.operation) {
		case ST_BIG_EFFECT_FREQUENCY: (void)st_big_speaker_frequency(s,event.value); p->remaining=1; break;
		case ST_BIG_EFFECT_STOP: st_big_speaker_stop(s); break;
		case ST_BIG_EFFECT_DELAY: {
			uint64_t scaled=(uint64_t)event.value*ST_BIG_SPEAKER_RATE+p->fraction;
			p->remaining=scaled/1000; p->fraction=(unsigned)(scaled%1000); break;
		}
		}
	}
	return 0;
}
int st_big_effect_render(struct st_big_effect_player *p, struct st_big_speaker *s, int16_t *pcm, size_t frames)
{
	if (p==NULL || s==NULL || (pcm==NULL && frames!=0)) return IO_ERROR_INVALID_ARGUMENT;
	if (frames>SIZE_MAX/(2*sizeof(*pcm))) return IO_ERROR_OUT_OF_RANGE;
	size_t used=0;
	for (;;) {
		int status=prepare(p,s); if (status!=0) return status;
		if (!p->active) return st_big_speaker_render(s,pcm==NULL?NULL:pcm+used*2,frames-used);
		if (used==frames) return 0;
		size_t count=frames-used; if (count>p->remaining) count=(size_t)p->remaining;
		status=st_big_speaker_render(s,pcm+used*2,count); if (status!=0) return status;
		used+=count; p->remaining-=count;
	}
}
int st_big_effect_skip(struct st_big_effect_player *p, struct st_big_speaker *s, uint64_t frames)
{
	if (p==NULL || s==NULL) return IO_ERROR_INVALID_ARGUMENT;
	for (;;) {
		int status=prepare(p,s); if (status!=0) return status;
		if (!p->active) { st_big_speaker_skip(s,frames); return 0; }
		if (frames==0) return 0;
		uint64_t count=frames<p->remaining?frames:p->remaining;
		st_big_speaker_skip(s,count); frames-=count; p->remaining-=count;
	}
}
