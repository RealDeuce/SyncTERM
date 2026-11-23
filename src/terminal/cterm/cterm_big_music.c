/* BIG15/16/67: live music storage, local sample playback and N0..5. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_big_internal.h"
#include "cterm.h"
#include "st_opl2.h"
#include "st_threads.h"
#include "st_time.h"
#include "io_error.h"
#include <stdlib.h>
#include <string.h>
_Static_assert(ST_BIG_MUSIC_RATE==AUDIO_SAMPLE_RATE,"BIG music mixer rate");
/* The terminal owns attachment and monotonic/mute bookkeeping. mutex owns
 * buffer, control.music, schedule, chip, blocked and error. The mixer source
 * never calls music/audio/terminal APIs. All such calls happen unlocked;
 * matching detach quiesces source before context destruction. */
struct cterm_big_music_audio {
	struct cterm_big *big;
	st_mutex_t mutex;
	struct st_big_music_buffer buffer;
	struct st_big_music_schedule schedule;
	struct st_opl2 chip;
	uint64_t local_ns, due;
	uint32_t fraction;
	int error;
	bool requested, selected, physical, allowed, unavailable, warned, sampled, blocked;
};
static void lock(struct cterm_big *b) { if (b->music_audio!=NULL) (void)st_mutex_lock(&b->music_audio->mutex); }
static void unlock(struct cterm_big *b) { if (b->music_audio!=NULL) (void)st_mutex_unlock(&b->music_audio->mutex); }
static int read_buffer(void *context, uint16_t offset, bool word, uint16_t *value)
{
	struct cterm_big_music_audio *a=context; return st_big_music_buffer_read(&a->buffer,offset,word,value);
}
static int write_register(void *context, uint16_t address, uint16_t value)
{
	struct cterm_big_music_audio *a=context; return st_opl2_write(&a->chip,(uint8_t)address,(uint8_t)value);
}
static int sample(struct cterm_big *b, uint64_t *ns)
{
	int status=b->numeric.io.now_ns==NULL?st_time_monotonic_ns(ns):b->numeric.io.now_ns(b->numeric.io.context,ns);
	if (status==IO_ERROR_IN_PROGRESS) status=IO_ERROR_PROTOCOL;
	return status==0 && b->reset_pending?IO_ERROR_INTERRUPTED:status;
}
static bool allow(struct cterm_big *b)
{
	st_audio_runtime_t *r=b->terminal->screen.audio;
	return r!=NULL && !st_audio_runtime_stopped(r)
	    && (st_audio_runtime_is_pull(r) || st_audio_runtime_allowed(r)!=0);
}
static bool quiet(const struct cterm_big_music_audio *a)
{
	if (a->schedule.armed || a->schedule.pending || a->big->control.music.running!=0) return false;
	if (a->chip.csmPending || (a->chip.compositeSine && (a->chip.timerControl&1)!=0)) return false;
	/* YM3812 envelope511 is fully attenuated. Retain every held melodic/
	 * rhythm key and release tail; discard only the silent quantization
	 * floor after no pending event can produce further sound. */
	for (unsigned i=0;i<ST_OPL2_OPERATORS;++i)
		if (a->chip.op[i].key!=0 || a->chip.op[i].envelope<511) return false;
	return true;
}
/* Caller holds mutex. A work suspension retains its sample boundary and
 * original callback prefix; only generated frames consume local duration. */
static int render(struct cterm_big_music_audio *a, int32_t *pcm, size_t count, size_t budget, size_t *used)
{
	const struct st_big_music_sink sink={a,read_buffer,write_register};
	*used=0; a->blocked=false;
	for (;;) {
		int status=st_big_music_schedule_poll(&a->schedule,&a->big->control.music,&sink,&budget);
		if (status==IO_ERROR_IN_PROGRESS) { a->blocked=true; return status; }
		if (status!=0) return status;
		if (quiet(a)) return 0;
		if (*used==count) return 0;
		size_t span=count-*used;
		if (a->schedule.armed && span>a->schedule.remaining) span=(size_t)a->schedule.remaining;
		if (span>256) span=256;
		int32_t mono[256]; status=st_opl2_render(&a->chip,mono,span);
		if (status!=0) return status;
		if (pcm!=NULL) for (size_t i=0;i<span;++i) {
			/* One development measurement against ANSI/beep, not a mutable
			 * calibration. Keep the polyphonic sum wide until mixer gain. */
			int32_t value=(int32_t)((int64_t)mono[i]*325718/100000);
			pcm[2*(*used+i)]=pcm[2*(*used+i)+1]=value;
		}
		status=st_big_music_schedule_consume(&a->schedule,span); if (status!=0) return status;
		*used+=span;
	}
}
static size_t source(void *context, int32_t *pcm, size_t count)
{
	struct cterm_big_music_audio *a=context; size_t used=0;
	if (st_mutex_lock(&a->mutex)!=ST_THREAD_SUCCESS) return 0;
	int status=render(a,pcm,count,1048576,&used);
	if (status!=0 && status!=IO_ERROR_IN_PROGRESS) a->error=status;
	(void)st_mutex_unlock(&a->mutex); return used;
}
void cterm_big_music_setup(struct cterm_big *b)
{
	struct cterm_big_music_audio *a=calloc(1,sizeof(*a));
	if (a==NULL) goto unavailable;
	if (st_mutex_init(&a->mutex,ST_MUTEX_PLAIN)!=ST_THREAD_SUCCESS) { free(a); goto unavailable; }
	int status=st_opl2_init(&a->chip,ST_BIG_MUSIC_RATE);
	for (unsigned pass=0;status==0 && pass<2;++pass) {
		/* Two original ColdInit calls. Detection reads need no emulated
		 * bus delay/device probe; retain their reached register state before
		 * each normal warm sequence, including masked timer1=ff. */
		static const uint8_t cold[][2]={{4,0x60},{4,0x80},{2,0xff},{4,0x21},{4,0x60},{4,0x80}};
		for (unsigned i=0;status==0 && i<sizeof(cold)/sizeof(cold[0]);++i) status=st_opl2_write(&a->chip,cold[i][0],cold[i][1]);
		for (unsigned i=0;status==0 && i<ST_BIG_MUSIC_WARM_WRITES;++i) {
			uint8_t address,value; status=st_big_music_warm_write(i,&address,&value);
			if (status==0) status=st_opl2_write(&a->chip,address,value);
		}
	}
	if (status!=0) { st_mutex_destroy(&a->mutex); free(a); goto unavailable; }
	a->big=b; b->music_audio=a; b->music_memory=1; b->control.music.enabled=1; return;
unavailable:
	b->music_memory=0; b->control.music.enabled=0;
	cterm_log(b->terminal,ST_CTERM_LOG_WARNING,"BIG music buffer unavailable; N playback disabled");
}
static void detach(struct cterm_big_music_audio *a)
{
	if (a->selected) (void)st_music_clear_pcm_source(a->big->terminal->music_player,a);
	a->selected=a->physical=false;
}
void cterm_big_music_reset(struct cterm_big *b)
{
	struct cterm_big_music_audio *a=b->music_audio; if (a==NULL) return;
	detach(a); a->requested=a->sampled=false; a->due=a->fraction=0;
	lock(b); st_big_music_schedule_cancel(&a->schedule); b->control.music.running=0;
	a->error=0; a->blocked=false; unlock(b);
}
void cterm_big_music_destroy(struct cterm_big *b)
{
	if (b->music_audio==NULL) return;
	cterm_big_music_reset(b); st_mutex_destroy(&b->music_audio->mutex); free(b->music_audio); b->music_audio=NULL;
}
static void attach(struct cterm_big_music_audio *a, bool physical)
{
	struct cterminal *t=a->big->terminal;
	if (t->screen.audio==NULL) return;
	int status=t->music_player==NULL?st_music_create(&t->music_player,t->screen.audio,&t->music_stream):0;
	if (status==0) status=st_music_set_pcm_source(t->music_player,source,a,physical);
	if (status==0) { a->selected=true; a->physical=physical; return; }
	a->selected=a->physical=false; a->unavailable=true;
	if (physical && t->music_player!=NULL && st_music_set_pcm_source(t->music_player,source,a,false)==0) a->selected=true;
	if (!a->warned) { a->warned=true; cterm_log(t,ST_CTERM_LOG_WARNING,"BIG music output unavailable; continuing muted"); }
}
static int activate(struct cterm_big *b)
{
	struct cterm_big_music_audio *a=b->music_audio; if (a==NULL) return IO_ERROR_NOT_SUPPORTED;
	uint64_t ns; int status=sample(b,&ns); if (status!=0) return status;
	a->requested=true; a->unavailable=false; a->allowed=allow(b);
	a->sampled=true; a->local_ns=ns; a->due=a->fraction=0;
	attach(a,a->allowed); return 0;
}
bool cterm_big_music_needs_poll(const struct cterm_big *b)
{
	return b->music_audio != NULL && b->music_audio->requested;
}

int cterm_big_music_poll(struct cterm_big *b)
{
	struct cterm_big_music_audio *a=b->music_audio;
	if (a==NULL || !a->requested) return 0;
	st_audio_runtime_t *runtime=b->terminal->screen.audio;
	if (runtime!=NULL && st_audio_runtime_stopped(runtime)) { cterm_big_music_reset(b); return 0; }
	uint64_t ns; int status=sample(b,&ns);
	if (status==0 && a->sampled && ns<a->local_ns) status=IO_ERROR_OUT_OF_RANGE;
	if (status!=0) { cterm_big_music_reset(b); return status; }
	bool enabled=allow(b);
	if (enabled!=a->allowed) a->unavailable=false;
	a->allowed=enabled;
	if (a->selected && !st_music_pcm_source_selected(b->terminal->music_player,a)) {
		a->selected=a->physical=false; cterm_big_music_reset(b); return 0;
	}
	lock(b); status=a->error; bool blocked=a->blocked; unlock(b);
	if (status!=0) { cterm_big_music_reset(b); return status; }
	if (a->physical) {
		bool active=st_music_pcm_source_active(b->terminal->music_player,a);
		a->local_ns=ns; a->sampled=true; a->fraction=0; a->due=0;
		if (enabled && active) return 0;
		if (enabled && !blocked) { cterm_big_music_reset(b); return 0; }
		if (enabled) {
			size_t used; lock(b); status=render(a,NULL,0,4096,&used); unlock(b);
			if (status==IO_ERROR_IN_PROGRESS) return 0;
			if (status!=0) { cterm_big_music_reset(b); return status; }
			attach(a,true); return 0;
		}
		attach(a,false); return 0;
	}
	uint64_t elapsed=a->sampled?ns-a->local_ns:0;
	a->local_ns=ns; a->sampled=true;
	uint64_t fraction=elapsed%UINT64_C(1000000000)*ST_BIG_MUSIC_RATE+a->fraction;
	uint64_t frames=elapsed/UINT64_C(1000000000)*ST_BIG_MUSIC_RATE+fraction/UINT64_C(1000000000);
	a->fraction=(uint32_t)(fraction%UINT64_C(1000000000));
	if (frames>UINT64_MAX-a->due) { cterm_big_music_reset(b); return IO_ERROR_OUT_OF_RANGE; }
	a->due+=frames;
	size_t count=a->due>4096?4096:(size_t)a->due, used;
	lock(b); status=render(a,NULL,count,65536,&used); bool silent=quiet(a); unlock(b); a->due-=used;
	if (status==IO_ERROR_IN_PROGRESS) return 0;
	if (status!=0) { cterm_big_music_reset(b); return status; }
	if (silent) { cterm_big_music_reset(b); return 0; }
	if (enabled && !a->unavailable && a->due==0) attach(a,true);
	return 0;
}
int cterm_big_music_clock(struct cterm_big *b, bool restore)
{
	if (b->music_memory==0) return 0;
	if (b->music_audio==NULL) return IO_ERROR_NOT_SUPPORTED;
	lock(b); int status=st_big_music_schedule_rate(&b->music_audio->schedule,&b->control.music,
	    restore?b->control.music.cached_divider:0); unlock(b); return status;
}
void cterm_big_music_sync_begin(struct cterm_big *b) { lock(b); st_big_sync_begin(&b->sync_entry,&b->control); unlock(b); }
int cterm_big_music_sync_poll(struct cterm_big *b) { lock(b); int status=st_big_sync_poll(&b->sync_entry,&b->control); unlock(b); return status; }
int cterm_big_music_sync_set(struct cterm_big *b, uint16_t value) { lock(b); b->control.music.sync=value; unlock(b); return 0; }
int cterm_big_music_snapshot(const struct cterm_big *b, struct st_big_music *m)
{
	if (b==NULL || m==NULL) return IO_ERROR_INVALID_ARGUMENT;
	struct cterm_big_music_audio *a=b->music_audio;
	if (a!=NULL && st_mutex_lock(&a->mutex)!=ST_THREAD_SUCCESS) return IO_ERROR_IO;
	memcpy(m,&b->control.music,sizeof(*m));
	if (a!=NULL) (void)st_mutex_unlock(&a->mutex); return 0;
}
int cterm_big_music_read(struct cterm_big *b, uint16_t offset, bool word, uint16_t *value)
{
	if (b==NULL || b->music_audio==NULL) return IO_ERROR_INVALID_ARGUMENT;
	lock(b); int status=st_big_music_buffer_read(&b->music_audio->buffer,offset,word,value); unlock(b); return status;
}
void cterm_big_music_begin(struct cterm_big *b, bool chained)
{
	b->music_command=(struct cterm_big_music_command){.phase=CTERM_BIG_MUSIC_SELECT,.chained=chained};
}
static int number(struct cterm_big *b, const uint8_t *bytes, size_t size, size_t *used, uint16_t *value, bool *done)
{
	static const struct st_graphics_parameter scalar={.kind=ST_GRAPHICS_NUMBER};
	static const struct st_graphics_command_view view={.code='N'};
	struct cterm_big_music_command *n=&b->music_command; *used=0; *done=false;
	int status,result=0;
	if (!n->number_started) { status=st_big_numeric_field(&b->numeric,&view,&scalar); if (status!=0) return status; n->number_started=true; }
	if (!n->wire) {
		status=st_big_numeric_number(&b->numeric,&view,&scalar,&result,done); if (status!=0) return status;
		if (!*done) n->wire=true;
	}
	if (n->wire) {
		status=st_big_numeric_read_number(&b->numeric,&view,&scalar,bytes,size,used,&result,done);
		if (status!=0 || !*done) return status;
	}
	if (b->reset_pending) return IO_ERROR_INTERRUPTED;
	*value=(uint16_t)result; n->number_started=n->wire=false; return 0;
}
static int command_sample(struct cterm_big *b, uint64_t *ns)
{
	int status=sample(b,ns); if (status!=0) return status;
	struct cterm_big_music_command *n=&b->music_command;
	if (n->sampled && *ns<n->sample) return IO_ERROR_OUT_OF_RANGE;
	n->sampled=true; n->sample=*ns; return 0;
}
int cterm_big_music_command_poll(struct cterm_big *b, const uint8_t *bytes, size_t size,
    size_t *used, size_t *budget, bool *complete)
{
	struct cterm_big_music_command *n=&b->music_command;
	struct cterm_big_music_audio *a=b->music_audio;
	struct st_big_music *m=&b->control.music;
	*used=0; *complete=false;
	for (;;) {
		if (b->reset_pending) return IO_ERROR_INTERRUPTED;
		int status=0;
		switch (n->phase) {
		case CTERM_BIG_MUSIC_SELECT:
		case CTERM_BIG_MUSIC_NUMBER: {
			uint16_t value; bool done; size_t accepted;
			status=number(b,bytes==NULL?NULL:bytes+*used,size-*used,&accepted,&value,&done);
			*used+=accepted; if (status!=0 || !done) return status;
			if (n->phase==CTERM_BIG_MUSIC_SELECT) { n->mode=st_big_music_selector(value); n->phase=CTERM_BIG_MUSIC_GATE; break; }
			if (n->index%2==0) n->whole=value;
			else {
				uint16_t pair=st_big_music_pair(n->whole,value,n->mode<2);
				if (n->mode==5) { lock(b); m->scratch=pair; unlock(b); n->phase=CTERM_BIG_MUSIC_CLOCK; break; }
				if (n->mode==3 && n->index==1) n->from=pair;
				else if (n->mode==3) { n->to=pair; n->phase=CTERM_BIG_MUSIC_ENABLE; break; }
				else { lock(b); m->length=pair; m->scratch=0; unlock(b); n->phase=CTERM_BIG_MUSIC_RAW_START; break; }
			}
			++n->index; break;
		}
		case CTERM_BIG_MUSIC_GATE:
			n->index=0;
			if (n->mode==5) n->phase=CTERM_BIG_MUSIC_NUMBER;
			else if (b->music_memory==0) n->phase=CTERM_BIG_MUSIC_DONE;
			else n->phase=n->mode==4?CTERM_BIG_MUSIC_WARM:n->mode==2?CTERM_BIG_MUSIC_ENABLE:CTERM_BIG_MUSIC_NUMBER;
			break;
		case CTERM_BIG_MUSIC_RAW_START: {
			lock(b); bool more=m->scratch<m->length; unlock(b);
			if (!more) { n->phase=CTERM_BIG_MUSIC_ENABLE; break; }
			uint64_t ns; status=command_sample(b,&ns); if (status!=0) break;
			n->deadline=UINT64_MAX-ns<ST_BIG_NUMERIC_TIMEOUT_NS?UINT64_MAX:ns+ST_BIG_NUMERIC_TIMEOUT_NS;
			n->phase=CTERM_BIG_MUSIC_RAW_READ; break;
		}
		case CTERM_BIG_MUSIC_RAW_READ: {
			if (*budget==0) return IO_ERROR_IN_PROGRESS;
			uint64_t ns; status=command_sample(b,&ns); if (status!=0) break;
			bool available=*used<size, expired=ns>=n->deadline;
			if (!available && !expired) return 0;
			if (a==NULL) return IO_ERROR_NOT_SUPPORTED;
			if (available) {
				uint8_t byte=bytes[(*used)++]; lock(b);
				status=st_big_music_buffer_store(&a->buffer,m->scratch,byte);
				if (status==0) m->scratch=(uint16_t)(m->scratch+1);
				unlock(b); if (status!=0) break;
			}
			--*budget;
			if (expired) {
				lock(b); status=st_big_music_buffer_store(&a->buffer,m->scratch,0); unlock(b);
				if (status!=0) break;
				if (b->numeric.io.bell!=NULL) b->numeric.io.bell(b->numeric.io.context);
				if (b->reset_pending) return IO_ERROR_INTERRUPTED;
				lock(b); m->length=0; m->scratch=m->length; unlock(b);
			}
			n->phase=expired?CTERM_BIG_MUSIC_ENABLE:CTERM_BIG_MUSIC_RAW_START; break;
		}
		case CTERM_BIG_MUSIC_ENABLE:
			lock(b); if (n->mode==0 || st_big_number_signed(n->mode)>3) { n->saved_enable=m->enabled; m->enabled=0; } unlock(b);
			n->phase=CTERM_BIG_MUSIC_START; break;
		case CTERM_BIG_MUSIC_START:
			lock(b); bool start=m->enabled!=0 && st_big_number_signed(n->mode)<4 && m->length!=0; unlock(b);
			n->phase=start?CTERM_BIG_MUSIC_END_HIGH:CTERM_BIG_MUSIC_RESTORE; break;
		case CTERM_BIG_MUSIC_END_HIGH:
			lock(b); m->end_high=0; unlock(b); n->phase=CTERM_BIG_MUSIC_END_LOW; break;
		case CTERM_BIG_MUSIC_END_LOW:
			lock(b); uint32_t end=st_big_music_end(m->length,n->mode==3,n->to); m->end_low=(uint16_t)end; m->end_high=(uint16_t)(end>>16); unlock(b);
			n->phase=CTERM_BIG_MUSIC_HEADER; break;
		case CTERM_BIG_MUSIC_HEADER:
			if (a==NULL) return IO_ERROR_NOT_SUPPORTED;
			lock(b); status=st_big_music_buffer_read(&a->buffer,0,true,&m->cached_divider);
			if (status==0 && b->music_memory!=0) status=st_big_music_schedule_rate(&a->schedule,m,m->cached_divider);
			unlock(b); n->phase=CTERM_BIG_MUSIC_POSITION_HIGH; break;
		case CTERM_BIG_MUSIC_POSITION_HIGH:
			lock(b); m->position_high=0; unlock(b); n->phase=CTERM_BIG_MUSIC_POSITION_LOW; break;
		case CTERM_BIG_MUSIC_POSITION_LOW:
			lock(b); uint32_t limit=(uint32_t)m->end_low|((uint32_t)m->end_high<<16);
			uint32_t position=st_big_music_start(limit,n->mode==3,n->from);
			m->position_low=(uint16_t)position; m->position_high=(uint16_t)(position>>16); unlock(b);
			n->phase=CTERM_BIG_MUSIC_RUNNING; break;
		case CTERM_BIG_MUSIC_RUNNING:
			lock(b); m->running=1; unlock(b); n->phase=CTERM_BIG_MUSIC_DELAY; break;
		case CTERM_BIG_MUSIC_DELAY:
			lock(b); m->delay=1; unlock(b); n->phase=CTERM_BIG_MUSIC_FEATURE; break;
		case CTERM_BIG_MUSIC_FEATURE:
			if (b->feature_gates[2][0]!=b->feature_gates[2][1] || b->feature_gates[0][0]!=b->feature_gates[0][1]) {
				lock(b); m->running=0; unlock(b); n->phase=CTERM_BIG_MUSIC_RESTORE;
			} else n->phase=CTERM_BIG_MUSIC_ARM;
			break;
		case CTERM_BIG_MUSIC_ARM:
			if (a==NULL) return IO_ERROR_NOT_SUPPORTED;
			lock(b); status=st_big_music_schedule_arm(&a->schedule,m,m->delay>1?m->delay:1); a->error=0; a->blocked=false; unlock(b);
			if (status==0) status=activate(b); n->phase=CTERM_BIG_MUSIC_RESTORE; break;
		case CTERM_BIG_MUSIC_RESTORE:
			if (n->mode==0) { lock(b); m->enabled=n->saved_enable; unlock(b); }
			n->phase=n->mode==4?CTERM_BIG_MUSIC_WARM:n->mode==5?CTERM_BIG_MUSIC_CLOCK:CTERM_BIG_MUSIC_DONE; break;
		case CTERM_BIG_MUSIC_WARM:
			if (!n->warm_begun) {
				if (b->music_memory==0) { n->phase=CTERM_BIG_MUSIC_DONE; break; }
				n->warm_begun=true;
			}
			if (n->warm<ST_BIG_MUSIC_WARM_WRITES) {
				if (*budget==0) return IO_ERROR_IN_PROGRESS;
				if (a==NULL) return IO_ERROR_NOT_SUPPORTED;
				uint8_t address,value; status=st_big_music_warm_write(n->warm,&address,&value);
				if (status==0) { lock(b); status=st_opl2_write(&a->chip,address,value); unlock(b); }
				if (status==0) { ++n->warm; --*budget; } break;
			}
			n->phase=CTERM_BIG_MUSIC_DONE; break;
		case CTERM_BIG_MUSIC_CLOCK:
			if (b->music_memory!=0) {
				if (a==NULL) return IO_ERROR_NOT_SUPPORTED;
				lock(b); status=st_big_music_schedule_rate(&a->schedule,m,m->scratch); unlock(b);
			}
			n->phase=CTERM_BIG_MUSIC_DONE; break;
		case CTERM_BIG_MUSIC_DONE:
			n->phase=CTERM_BIG_MUSIC_OFF; *complete=true; return 0;
		default: return IO_ERROR_PROTOCOL;
		}
		if (status!=0) return status;
	}
}
