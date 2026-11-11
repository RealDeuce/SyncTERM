/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_igs_audio.h"
#include "cterm_igs.h"
#include "cterm.h"
#include "cterm_internal.h"
#include "st_audio_runtime.h"
#include "io_error.h"
#include "st_console_test.h"

#include <stdlib.h>
#include <string.h>

_Static_assert(ST_IGS_PSG_RATE == AUDIO_SAMPLE_RATE, "IGS audio rate");
_Static_assert(ST_IGS_PSG_CHANNELS == AUDIO_CHANNELS, "IGS audio channels");

static void
write_psg(void *context, unsigned reg, uint8_t value)
{
	st_igs_psg_write(context,reg,value);
}

static int
create(struct cterm_igs *igs)
{
	if (igs->audio != NULL) return 0;
	struct cterm_igs_audio *a = calloc(1,sizeof(*a));
	if (a == NULL) return IO_ERROR_OUT_OF_MEMORY;
	int status = igs->now_ms(&a->local_ms);
	if (status == 0 && st_mutex_init(&a->mutex,ST_MUTEX_PLAIN) != ST_THREAD_SUCCESS)
		status = IO_ERROR_IO;
	if (status != 0) { free(a); return status; }
	st_igs_gist_init(&a->gist); st_igs_psg_init(&a->psg);
	a->gist.write = write_psg; a->gist.context = &a->psg;
	memcpy(a->patches,st_igs_gist_patches,sizeof(a->patches));
	memcpy(a->saved,a->patches,sizeof(a->saved));
	a->multiplier = 5; a->console_allowed = true;
	/* Ceiling conversion keeps an exact local 5ms deadline from waiting
	 * for the next millisecond just because it falls between PCM frames. */
	a->local_fraction = 999;
	/* The first native console service is on the third 5ms update,
	 * followed by 20ms intervals. Carry only local sample fractions. */
	a->console_credit = AUDIO_SAMPLE_RATE/4;
	igs->audio = a;
	return 0;
}

static void
console_service(struct cterm_igs_audio *a)
{
	/* Complete US TOS 1.04 built-ins, chapter 55. These fixed streams
	 * contain only immediate register pairs and the zero terminator;
	 * there is no unused general Dosound memory interpreter here. */
	static const uint8_t bell[] = {
		0,0x34,1,0,2,0,3,0,4,0,5,0,6,0,7,0xfe,
		8,0x10,9,0,10,0,11,0,12,0x10,13,9,0xff,0
	};
	static const uint8_t click[] = {
		0,0x3b,1,0,2,0,3,0,4,0,5,0,6,0,7,0xfe,
		8,0x10,13,3,11,0x80,12,1,0xff,0
	};
	if (a->console_pending == 0) return;
	const uint8_t *bytes = a->console_pending == 1 ? bell : click;
	for (size_t i = 0; bytes[i] != 0xff; i += 2) {
		unsigned reg = bytes[i]; uint8_t value = bytes[i+1];
		if (reg == 7) value = (uint8_t)((a->psg.registers[7]&0xc0) | (value&0x3f));
		st_igs_psg_write(&a->psg,reg,value);
		/* GIST's mirror also observes writes from the shared console path. */
		a->gist.registers[reg] = a->psg.registers[reg];
	}
	a->console_pending = 0;
}

static void
advance(struct cterm_igs_audio *a, int16_t *frames, uint64_t count)
{
	while (count != 0) {
		if (frames == NULL && !st_igs_gist_active(&a->gist) && a->console_pending == 0) {
			st_igs_psg_skip(&a->psg,count);
			if (count >= (AUDIO_SAMPLE_RATE-a->envelope_credit+199)/200)
				a->console_allowed = true;
			a->envelope_credit = (unsigned)(a->envelope_credit+(count%AUDIO_SAMPLE_RATE)*200)%AUDIO_SAMPLE_RATE;
			a->console_credit = (unsigned)(a->console_credit+(count%AUDIO_SAMPLE_RATE)*50)%AUDIO_SAMPLE_RATE;
			return;
		}
		size_t step = (AUDIO_SAMPLE_RATE-a->envelope_credit+199)/200;
		size_t console = (AUDIO_SAMPLE_RATE-a->console_credit+49)/50;
		if (step > console) step = console;
		if (step > count) step = count;
		if (frames != NULL) { st_igs_psg_render(&a->psg,frames,step); frames += 2*step; }
		else st_igs_psg_skip(&a->psg,step);
		count -= step;
		a->envelope_credit += (unsigned)step*200;
		a->console_credit += (unsigned)step*50;
		if (a->envelope_credit >= AUDIO_SAMPLE_RATE) {
			a->envelope_credit -= AUDIO_SAMPLE_RATE;
			st_igs_gist_update(&a->gist);
			a->console_allowed = !st_igs_gist_active(&a->gist);
		}
		/* When both are due, the chained console handler follows GIST. */
		if (a->console_credit >= AUDIO_SAMPLE_RATE) {
			a->console_credit -= AUDIO_SAMPLE_RATE; console_service(a);
		}
	}
}

static size_t
source(void *context, int16_t *frames, size_t count)
{
	struct cterm_igs_audio *a = context;
	if (st_mutex_lock(&a->mutex) != ST_THREAD_SUCCESS) return 0;
	advance(a,frames,count);
	(void)st_mutex_unlock(&a->mutex);
	/* A shared chip remains continuous after an effect ends, including its
	 * filter tail and generator phases. EOF/reset/disable quiesces it. */
	return count;
}

static bool
enabled(struct cterm_igs *igs)
{
	st_audio_runtime_t *runtime = igs->terminal->screen.audio;
	return runtime != NULL && !st_audio_runtime_stopped(runtime)
	    && (st_audio_runtime_is_pull(runtime) || st_audio_runtime_allowed(runtime) != 0);
}

static int
local_advance(struct cterm_igs *igs, bool start)
{
	struct cterm_igs_audio *a = igs->audio;
	start = start || a->local_anchor_needed;
	uint64_t now; int status = igs->now_ms(&now);
	if (status != 0) return status;
	if (now < a->local_ms && !start) return IO_ERROR_OUT_OF_RANGE;
	uint64_t elapsed = start ? 0 : now-a->local_ms;
	if (start) a->local_fraction = 999;
	/* Reject an unrepresentable local interval before multiplying. */
	if (elapsed/1000 > UINT64_MAX/AUDIO_SAMPLE_RATE) return IO_ERROR_OUT_OF_RANGE;
	uint64_t fraction = (elapsed%1000)*AUDIO_SAMPLE_RATE+a->local_fraction;
	uint64_t frames = (elapsed/1000)*AUDIO_SAMPLE_RATE;
	if (fraction/1000 > UINT64_MAX-frames) return IO_ERROR_OUT_OF_RANGE;
	frames += fraction/1000;
	if (st_mutex_lock(&a->mutex) != ST_THREAD_SUCCESS) return IO_ERROR_IO;
	/* Muted voices and PSG phases advance in local time without discarded
	 * PCM. Expired envelopes and filter tails must not replay on re-enable. */
	advance(a,NULL,frames);
	a->local_ms = now; a->local_fraction = (unsigned)(fraction%1000);
	a->local_anchor_needed = false;
	(void)st_mutex_unlock(&a->mutex);
	return 0;
}

int
cterm_igs_audio_poll(struct cterm_igs *igs)
{
	struct cterm_igs_audio *a = igs->audio;
	if (a == NULL) return 0;
	bool allow = enabled(igs);
	if (a->stream != NULL) {
		if (allow && audio_stream_source_active(a->stream,true) == 0) return 0;
		audio_stream_destroy(a->stream); a->stream = NULL;
		/* A clock failure during handoff must not later replay elapsed
		 * wall time already consumed in the audio sample domain. */
		a->local_anchor_needed = true;
		int status = local_advance(igs,true);
		if (status != 0) return status;
	}
	int status = local_advance(igs,false);
	if (status != 0 || !allow || !a->running) return status;
	audio_engine_t *engine = NULL;
	status = st_audio_runtime_acquire(igs->terminal->screen.audio,&engine);
	if (status == 0) status = audio_stream_create(&a->stream,engine,-12,-12);
	if (status == 0) status = audio_stream_set_source(a->stream,source,a);
	if (status == 0) status = audio_stream_source_active(a->stream,true);
	if (status != 0) {
		audio_stream_destroy(a->stream); a->stream = NULL;
		bool warn = !a->warned;
		a->warned = true;
		if (warn) cterm_log(igs->terminal,ST_CTERM_LOG_WARNING,"IGS audio output unavailable");
	}
	/* Device failures do not discard source edits or interrupt the wire. */
	return 0;
}

static void
play(struct cterm_igs_audio *a, unsigned patch)
{
	int bound = a->multiplier == 0 ? 1 : (int)((uint32_t)((int32_t)a->multiplier*200)&65535u);
	if (bound > 32767) bound -= 65536;
	if (patch >= 5) bound = 1;
	for (int i = 0; i < bound; ++i)
		(void)st_igs_gist_on(&a->gist,a->patches[patch],-1,-1,-1,1);
	if (bound > 0) a->running = true;
}

int
cterm_igs_audio_effect(struct cterm_igs *igs, const int *v, size_t count)
{
	if (v == NULL || count == 0) return IO_ERROR_INVALID_ARGUMENT;
	int status = create(igs);
	if (status == 0) status = cterm_igs_audio_poll(igs);
	if (status != 0) return status;
	if (igs->audio == NULL || igs->reset_pending) return IO_ERROR_INTERRUPTED;
	int selector = v[0] > 23 ? 0 : v[0];
	if (count != (selector == 20 ? 7u : selector == 22 || selector == 23 ? 2u : 1u))
		return IO_ERROR_PROTOCOL;
	struct cterm_igs_audio *a = igs->audio;
	if (st_mutex_lock(&a->mutex) != ST_THREAD_SUCCESS) return IO_ERROR_IO;
	if (selector >= 0 && selector < 20) play(a,(unsigned)selector);
	else if (selector == 20) {
		int patch = v[2] > 19 ? 19 : v[2], word = v[3] > 55 ? 55 : v[3];
		if (patch >= 0) {
			if (word < 0) status = IO_ERROR_OUT_OF_RANGE;
			else {
				int thousands = v[5] > 32 ? 32 : v[5];
				uint16_t value = (uint16_t)((int32_t)thousands*1000+v[6]);
				if (v[4] != 0) value = (uint16_t)(0u-value);
				a->patches[patch][word] = value;
				if (v[1] != 0) play(a,(unsigned)patch);
			}
		}
	}
	else if (selector == 21) {
		for (int i = 0; i < 3; ++i) st_igs_gist_stop(&a->gist,i);
	}
	else if (selector == 22) {
		int patch = v[1] < 0 || v[1] > 19 ? 0 : v[1];
		memcpy(a->patches[patch],a->saved[patch],sizeof(a->patches[patch]));
	}
	else if (selector == 23) a->multiplier = v[1] > 16 ? 5 : v[1];
	(void)st_mutex_unlock(&a->mutex);
	return status == 0 ? cterm_igs_audio_poll(igs) : status;
}

int
cterm_igs_audio_console(struct cterm_igs *igs, bool click)
{
	int status = create(igs);
	if (status == 0) status = cterm_igs_audio_poll(igs);
	if (status != 0) return status;
	if (igs->audio == NULL || igs->reset_pending) return IO_ERROR_INTERRUPTED;
	struct cterm_igs_audio *a = igs->audio;
	if (st_mutex_lock(&a->mutex) != ST_THREAD_SUCCESS) return IO_ERROR_IO;
	if (a->console_allowed) { a->console_pending = click ? 2u : 1u; a->running = true; }
	(void)st_mutex_unlock(&a->mutex);
	return cterm_igs_audio_poll(igs);
}

void
cterm_igs_audio_stop(struct cterm_igs *igs)
{
	struct cterm_igs_audio *a = igs->audio;
	if (a == NULL) return;
	audio_stream_destroy(a->stream); a->stream = NULL;
	(void)st_mutex_lock(&a->mutex);
	for (int i = 0; i < 3; ++i) st_igs_gist_stop(&a->gist,i);
	a->psg.low = a->psg.previous_low = a->psg.high = 0;
	a->running = false; a->console_pending = 0; a->console_allowed = true;
	a->local_anchor_needed = true;
	(void)st_mutex_unlock(&a->mutex);
	(void)local_advance(igs,true);
}

void
cterm_igs_audio_destroy(struct cterm_igs *igs)
{
	if (igs->audio == NULL) return;
	cterm_igs_audio_stop(igs);
	st_mutex_destroy(&igs->audio->mutex);
	free(igs->audio); igs->audio = NULL;
}

#if ST_CONSOLE_TEST
static uint32_t
inspect_patch_bank(uint16_t bank[ST_IGS_GIST_PATCHES][ST_IGS_GIST_WORDS])
{
	uint32_t hash = UINT32_C(2166136261);
	for (unsigned p = 0; p < ST_IGS_GIST_PATCHES; ++p)
		for (unsigned w = 0; w < ST_IGS_GIST_WORDS; ++w) {
			hash = (hash ^ (bank[p][w] >> 8))*UINT32_C(16777619);
			hash = (hash ^ (bank[p][w] & 255))*UINT32_C(16777619);
		}
	return hash;
}

void
cterm_igs_audio_inspect(const struct cterm_igs *igs, struct st_console_test_application *app)
{
	struct cterm_igs_audio *sound = igs->audio;
	st_console_test_number(app,"cterm.igs_sound_present",sound != NULL);
	if (sound != NULL && st_mutex_lock(&sound->mutex) == ST_THREAD_SUCCESS) {
		st_console_test_number(app,"cterm.igs_sound_multiplier",sound->multiplier);
		st_console_test_number(app,"cterm.igs_sound_patches",inspect_patch_bank(sound->patches));
		st_console_test_number(app,"cterm.igs_sound_saved",inspect_patch_bank(sound->saved));
		st_console_test_number(app,"cterm.igs_sound_output",sound->stream != NULL);
		st_console_test_number(app,"cterm.igs_sound_running",sound->running);
		st_console_test_number(app,"cterm.igs_sound_console",sound->console_allowed);
		unsigned voices = 0;
		for (unsigned i = 0; i < 3; ++i) if (sound->gist.voice[i].parameters[0] != 0) voices |= 1u << i;
		st_console_test_number(app,"cterm.igs_sound_voices",voices);
		(void)st_mutex_unlock(&sound->mutex);
	}
}
#endif
