/* b caller policy, shared console PSG, local samples and session lifetime. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "igs_test_fixture.h"
#include "cterm_igs_audio.h"
#include "st_audio_runtime.h"

extern size_t st_igs_test_calls;
extern int st_igs_test_arguments[4];
extern const uint16_t *st_igs_test_patch;
extern unsigned st_igs_test_stops[3], st_igs_test_stop_count;
extern bool st_igs_test_allocation_failure, st_igs_test_open_failure;

static void
effect(struct fixture *f, const int *values, size_t count)
{
	CHECK(cterm_igs_audio_effect(f->terminal->igs_engine,values,count) == 0);
}

static void
request(struct fixture *f, const int *values, size_t count)
{
	char wire[80] = "G#b "; size_t offset = 4;
	for (size_t i = 0; i < count; ++i) {
		f->terminal->igs_engine->math[i] = (int16_t)values[i];
		int n = snprintf(wire+offset,sizeof(wire)-offset,"%zu]%c",i,i+1 == count ? ':' : ',');
		CHECK(n > 0 && (size_t)n < sizeof(wire)-offset); offset += (size_t)n;
	}
	feed(f,wire);
}

static void
counts(void)
{
	static const struct { int multiplier; size_t calls; } cases[] = {
		{5,1000},{1,200},{16,3200},{0,1},{-1,0},{-32768,0},{-164,32736}
	};
	struct fixture f; create(&f,0,false);
	for (size_t i = 0; i < sizeof(cases)/sizeof(*cases); ++i) {
		request(&f,(int[]){23,cases[i].multiplier},2);
		struct cterm_igs_audio *a = f.terminal->igs_engine->audio;
		for (int patch = 0; patch < 20; ++patch) {
			st_igs_test_calls = 0; request(&f,&patch,1);
			if (st_igs_test_calls != (patch < 5 ? cases[i].calls : 1))
				fprintf(stderr,"multiplier %d stored %d patch %d calls %zu expected %zu\n",cases[i].multiplier,a->multiplier,patch,st_igs_test_calls,patch < 5 ? cases[i].calls : 1);
			CHECK(st_igs_test_calls == (patch < 5 ? cases[i].calls : 1));
			if (st_igs_test_calls != 0) {
				CHECK(st_igs_test_patch == a->patches[patch]);
				CHECK(memcmp(st_igs_test_arguments,(int[]){-1,-1,-1,1},sizeof(st_igs_test_arguments)) == 0);
			}
			CHECK(!st_cterm_input_paused(f.terminal) && a->stream == NULL && local_ms == 1000);
		}
	}
	/* Every signed multiplier word, including the upper-only clamp. */
	for (int word = -32768; word <= 32767; ++word) {
		effect(&f,(int[]){23,word},2);
		CHECK(f.terminal->igs_engine->audio->multiplier == (word > 16 ? 5 : word));
	}
	static const int selectors[] = {-32768,-1,24,32767};
	effect(&f,(int[]){23,0},2);
	for (size_t i = 0; i < sizeof(selectors)/sizeof(*selectors); ++i) {
		st_igs_test_calls = 0; request(&f,&selectors[i],1);
		CHECK(st_igs_test_calls == (selectors[i] < 0 ? 0u : 1u));
	}
	struct cterm_igs_audio *a = f.terminal->igs_engine->audio;
	for (unsigned i = 0; i < 3; ++i) { a->gist.voice[i].priority = 7; a->gist.voice[i].parameters[0] = 1; }
	effect(&f,(int[]){23,1},2); st_igs_test_calls = 0;
	effect(&f,(int[]){0},1); CHECK(st_igs_test_calls == 200); /* Rejections do not stop the loop. */
	st_igs_test_stop_count = 0; request(&f,(int[]){21},1);
	CHECK(st_igs_test_stop_count == 3 && memcmp(st_igs_test_stops,(unsigned[]){0,1,2},sizeof(st_igs_test_stops)) == 0);
	destroy(&f);
}

static void
banks(void)
{
	struct fixture f; create(&f,1,true); request(&f,(int[]){23,0},2);
	struct cterm_igs *igs = f.terminal->igs_engine;
	struct cterm_igs_audio *a = igs->audio;
	CHECK(memcmp(a->patches,st_igs_gist_patches,sizeof(a->patches)) == 0);
	CHECK(memcmp(a->saved,st_igs_gist_patches,sizeof(a->saved)) == 0);
	CHECK(a->patches[3] != a->patches[11] && a->saved[3] != a->saved[11]);
	for (int patch = 0; patch < 20; ++patch)
		for (int word = 0; word < 56; ++word) {
			int value = patch*137+word*17+123;
			request(&f,(int[]){20,0,patch,word,0,0,value},7);
			CHECK(a->patches[patch][word] == value);
		}
	CHECK(memcmp(a->saved,st_igs_gist_patches,sizeof(a->saved)) == 0);
	for (int patch = 0; patch < 20; ++patch) {
		request(&f,(int[]){22,patch},2);
		CHECK(memcmp(a->patches[patch],st_igs_gist_patches[patch],sizeof(a->patches[patch])) == 0);
	}
	static const struct { int args[6]; uint16_t value; int patch,word; } edits[] = {
		{{0,20,56,0,33,32767},64767,19,55},
		{{0,3,3,0,-1,999},65535,3,3},{{0,3,3,1,-1,999},1,3,3},
		{{0,3,4,-1,32,768},32768,3,4},{{0,3,4,1,-32768,1},65535,3,4}
	};
	for (size_t i = 0; i < sizeof(edits)/sizeof(*edits); ++i) {
		int v[7] = {20}; memcpy(v+1,edits[i].args,sizeof(edits[i].args)); request(&f,v,7);
		CHECK(a->patches[edits[i].patch][edits[i].word] == edits[i].value);
	}
	CHECK(a->patches[11][3] == st_igs_gist_patches[11][3]);
	request(&f,(int[]){22,11},2); CHECK(a->patches[3][3] == 1);
	request(&f,(int[]){22,3},2);
	/* Restoration uses the current saved snapshot, without touching another bank. */
	a->saved[3][55] = 1234; request(&f,(int[]){22,3},2); CHECK(a->patches[3][55] == 1234);
	CHECK(a->saved[11][55] == st_igs_gist_patches[11][55]);
	request(&f,(int[]){20,0,0,0,0,0,2},7);
	request(&f,(int[]){22,-1},2); CHECK(a->patches[0][0] == st_igs_gist_patches[0][0]);
	request(&f,(int[]){20,0,0,0,0,0,2},7);
	request(&f,(int[]){22,20},2); CHECK(a->patches[0][0] == st_igs_gist_patches[0][0]);
	/* Editing/restore affects a source record, never a previously copied voice. */
	request(&f,(int[]){20,1,5,0,0,0,2},7);
	CHECK(a->gist.voice[0].parameters[0] == 2);
	struct st_igs_gist_voice voice = a->gist.voice[0];
	request(&f,(int[]){22,5},2); CHECK(memcmp(&voice,&a->gist.voice[0],sizeof(voice)) == 0);
	uint16_t snapshot[20][56]; memcpy(snapshot,a->patches,sizeof(snapshot));
	st_igs_test_calls = 0; request(&f,(int[]){20,1,-1,-32768,1,32,999},7);
	CHECK(st_igs_test_calls == 0 && memcmp(snapshot,a->patches,sizeof(snapshot)) == 0);
	for (int word = -1; word >= -32768; word -= 32767) {
		CHECK(cterm_igs_audio_effect(igs,(int[]){20,1,0,word,0,0,1},7) == IO_ERROR_OUT_OF_RANGE);
		CHECK(st_igs_test_calls == 0 && memcmp(snapshot,a->patches,sizeof(snapshot)) == 0);
	}
	CHECK(cterm_igs_audio_effect(igs,(int[]){20},1) == IO_ERROR_PROTOCOL);
	CHECK(cterm_igs_audio_effect(igs,NULL,0) == IO_ERROR_INVALID_ARGUMENT);
	for (int scaling = 0; scaling < 3; ++scaling) {
		cterm_igs_set_scaling(igs,scaling); feed(&f,"G#b 23,16:");
		CHECK(a->multiplier == 16); /* b's decimal constants remain unscaled. */
	}
	CHECK(cterm_igs_initialize(igs,0) == 0 && igs->audio == a);
	memcpy(snapshot,a->patches,sizeof(snapshot));
	feed(&f,"G#R 0,1:"); CHECK(st_cterm_input_paused(f.terminal));
	local_ms = igs->wait_deadline; CHECK(st_cterm_poll(f.terminal) == 0);
	CHECK(igs->audio == a && a->multiplier == 16 && memcmp(snapshot,a->patches,sizeof(snapshot)) == 0);
	destroy(&f);
}

static void
parser(void)
{
	struct fixture f; create(&f,0,false);
	const char wire[] = "AG#b>23,0:b>20,1,5,0,0,0,2:b>21:M 2:B";
	for (size_t split = 0; split <= sizeof(wire); ++split) {
		CHECK(st_cterm_reset_terminal(f.terminal) == 0); size_t offset = 0, size = sizeof(wire)-1;
		while (offset < size) {
			size_t piece = split > size ? 1 : offset < split ? split-offset : size-offset, used;
			CHECK(st_cterm_write(f.terminal,wire+offset,piece,&used) == 0 && used == piece); offset += used;
		}
		struct cterm_igs_audio *a = f.terminal->igs_engine->audio;
		CHECK(a != NULL && a->multiplier == 0 && a->patches[5][0] == 2 && !st_igs_gist_active(&a->gist));
		CHECK(f.terminal->igs_engine->attributes.writing_mode == 2 && !st_cterm_input_paused(f.terminal));
		struct st_console_cell cells[2]; const struct st_console_rect rect = {0,0,2,1};
		CHECK(st_console_read_cells(f.console,&rect,cells,2) == 0 && cells[0].glyph == 'A' && cells[1].glyph == 'B');
	}
	CHECK(st_cterm_reset_terminal(f.terminal) == 0);
	feed(&f,"G#b>23,0:b 20,0,5,0,0,0");
	CHECK(f.terminal->igs_engine->audio->patches[5][0] == st_igs_gist_patches[5][0]);
	CHECK(st_cterm_finish(f.terminal) == 0);
	CHECK(f.terminal->igs_engine->audio->patches[5][0] == st_igs_gist_patches[5][0]);
	feed(&f,"G#b 20,0,5,0,0,0"); local_ms += 5000; CHECK(st_cterm_poll(f.terminal) == 0);
	CHECK(f.terminal->igs_engine->audio->patches[5][0] == st_igs_gist_patches[5][0]);
	/* Reject unsafe edits while preserving the source and passing the tail
	 * through to the terminal in the same write. */
	struct cterm_igs *bad = f.terminal->igs_engine; bad->math[0] = -1;
	const char invalid[] = "G#b 20,1,0,0],0,0,1:TAIL"; size_t used;
	st_igs_test_calls = 0;
	CHECK(st_cterm_write(f.terminal,invalid,sizeof(invalid)-1,&used) == 0);
	CHECK(used == sizeof(invalid)-1 && st_igs_test_calls == 0);
	CHECK(bad->audio->patches[0][0] == st_igs_gist_patches[0][0]);
	struct st_console_cell tail[4];
	const struct st_console_rect rect = {0,0,4,1};
	CHECK(st_console_read_cells(f.console,&rect,tail,4) == 0);
	for (size_t i = 0; i < 4; ++i) CHECK(tail[i].glyph == (unsigned char)"TAIL"[i]);
	/* Random selector arity previews do not consume the real source twice. */
	CHECK(st_cterm_reset_terminal(f.terminal) == 0);
	struct cterm_igs *igs = f.terminal->igs_engine;
	igs->random_range[0][0] = igs->random_range[0][1] = 23;
	/* Independent recurrence: seed 1 first accepts byte 23 on sample 281. */
	feed(&f,"G#b r,0:"); CHECK(igs->random_seed == UINT32_C(0xc29817f6) && igs->audio->multiplier == 0);
	feed(&f,"G#M 1:");
	destroy(&f);
}

static st_audio_runtime_t *
attach(struct fixture *f, bool pull)
{
	st_audio_runtime_t *runtime = NULL;
	CHECK((pull ? st_audio_runtime_create_pull(&runtime) : st_audio_runtime_create(&runtime,0)) == 0);
	f->terminal->screen.audio = runtime;
	return runtime;
}

static void
short_patch(struct fixture *f)
{
	effect(f,(int[]){23,0},2);
	/* A constant-volume 1000Hz tone lasting exactly two 5ms updates. */
	struct cterm_igs_audio *a = f->terminal->igs_engine->audio;
	CHECK(st_mutex_lock(&a->mutex) == ST_THREAD_SUCCESS);
	memset(a->patches[0],0,sizeof(a->patches[0]));
	a->patches[0][0] = 2; a->patches[0][1] = 125; a->patches[0][2] = 65535; a->patches[0][3] = 15;
	CHECK(st_mutex_unlock(&a->mutex) == ST_THREAD_SUCCESS);
}

static void
pcm_and_console(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f,resolution,true); st_audio_runtime_t *runtime = attach(&f,true);
		short_patch(&f); effect(&f,(int[]){0},1);
		struct cterm_igs_audio *a = f.terminal->igs_engine->audio;
		CHECK(a->stream != NULL && f.terminal->fx_stream == NULL && f.terminal->music_stream == NULL);
		CHECK(audio_stream_set_volume(a->stream,0,0) == 0);
		int16_t out[882]; CHECK(st_audio_runtime_pull(runtime,out,220) == 0);
		CHECK(a->gist.voice[0].parameters[0] == 2);
		bool nonzero = false; for (unsigned i = 0; i < 440; ++i) nonzero |= out[i] != 0;
		CHECK(nonzero);
		CHECK(st_audio_runtime_pull(runtime,out,1) == 0 && a->gist.voice[0].parameters[0] == 1);
		CHECK(!a->console_allowed);
		CHECK(cterm_igs_audio_console(f.terminal->igs_engine,false) == 0 && a->console_pending == 0);
		CHECK(st_audio_runtime_pull(runtime,out,219) == 0 && a->gist.voice[0].parameters[0] == 1);
		CHECK(st_audio_runtime_pull(runtime,out,1) == 0 && !st_igs_gist_active(&a->gist) && a->console_allowed);
		CHECK(a->psg.registers[8] == 0);
		/* Existing console scripts are allowed to overwrite the same PSG
		 * after GIST, even if another effect became active meanwhile. */
		f.terminal->quiet = false; feed(&f,"\a"); CHECK(a->console_pending == 1);
		effect(&f,(int[]){0},1); CHECK(st_audio_runtime_pull(runtime,out,221) == 0);
		CHECK(a->console_pending == 0 && a->psg.registers[0] == 0x34 && a->psg.registers[13] == 9);
		CHECK(a->psg.registers[7] == 0x3e && a->psg.registers[8] == 16);
		CHECK(!a->console_allowed); CHECK(cterm_igs_audio_console(f.terminal->igs_engine,true) == 0);
		CHECK(a->console_pending == 0);
		effect(&f,(int[]){21},1); CHECK(st_audio_runtime_pull(runtime,out,220) == 0 && a->console_allowed);
		CHECK(cterm_igs_audio_console(f.terminal->igs_engine,true) == 0 && a->console_pending == 2);
		CHECK(st_audio_runtime_pull(runtime,out,441) == 0); CHECK(st_audio_runtime_pull(runtime,out,221) == 0);
		CHECK(a->console_pending == 0 && a->psg.registers[0] == 0x3b && a->psg.registers[13] == 3);
		CHECK(a->psg.registers[11] == 0x80 && a->psg.registers[12] == 1);
		/* Muting quiesces the callback; unheard envelopes advance without
		 * PCM, so an expired console sound cannot replay on re-enable. */
		f.terminal->screen.audio = NULL; CHECK(st_cterm_poll(f.terminal) == 0 && a->stream == NULL);
		local_ms += 1000; CHECK(st_cterm_poll(f.terminal) == 0 && a->psg.envelope >= 32);
		f.terminal->screen.audio = runtime; CHECK(st_cterm_poll(f.terminal) == 0 && a->stream != NULL);
		CHECK(st_audio_runtime_pull(runtime,out,32) == 0);
		for (unsigned i = 0; i < 64; ++i) CHECK(out[i] == 0);
		/* Stop/reset affects only this chip's stream. */
		audio_engine_t *engine = NULL; CHECK(st_audio_runtime_acquire(runtime,&engine) == 0);
		CHECK(audio_stream_create(&f.terminal->fx_stream,engine,0,0) == 0);
		int16_t independent[] = {1000,-1000}; struct audio_buffer_options loop = AUDIO_BUFFER_OPTIONS_INIT; loop.loop = true;
		CHECK(audio_stream_append(f.terminal->fx_stream,independent,1,&loop) == 0);
		effect(&f,(int[]){21},1); CHECK(audio_stream_buffered_frames(f.terminal->fx_stream) == SIZE_MAX);
		CHECK(st_cterm_finish(f.terminal) == 0 && a->stream == NULL && !a->running);
		CHECK(audio_stream_buffered_frames(f.terminal->fx_stream) == SIZE_MAX);
		CHECK(st_cterm_reset_terminal(f.terminal) == 0 && f.terminal->igs_engine->audio == NULL);
		CHECK(st_audio_runtime_pull(runtime,out,1) == 0 && out[0] > 0 && out[1] < 0);
		destroy(&f); st_audio_runtime_destroy(runtime);
	}
}

static int bad_clock(uint64_t *value) { (void)value; return IO_ERROR_IO; }
static void
muted_and_errors(void)
{
	struct fixture f; create(&f,0,false); st_audio_runtime_t *runtime = attach(&f,false);
	struct cterm_igs *igs = f.terminal->igs_engine;
	st_igs_test_allocation_failure = true;
	CHECK(cterm_igs_audio_effect(igs,(int[]){23,0},2) == IO_ERROR_OUT_OF_MEMORY && igs->audio == NULL);
	st_igs_test_allocation_failure = false; igs->now_ms = bad_clock;
	CHECK(cterm_igs_audio_effect(igs,(int[]){23,0},2) == IO_ERROR_IO && igs->audio == NULL);
	igs->now_ms = now; short_patch(&f); effect(&f,(int[]){0},1);
	struct cterm_igs_audio *a = igs->audio;
	CHECK(a->stream == NULL && a->gist.voice[0].parameters[0] == 2);
	local_ms += 4; CHECK(st_cterm_poll(f.terminal) == 0 && a->gist.voice[0].parameters[0] == 2);
	++local_ms; CHECK(st_cterm_poll(f.terminal) == 0 && a->gist.voice[0].parameters[0] == 1);
	local_ms += 5; CHECK(st_cterm_poll(f.terminal) == 0 && !st_igs_gist_active(&a->gist));
	igs->now_ms = bad_clock; CHECK(st_cterm_poll(f.terminal) == IO_ERROR_IO); igs->now_ms = now;
	--local_ms; CHECK(st_cterm_poll(f.terminal) == IO_ERROR_OUT_OF_RANGE); ++local_ms;
	uint64_t saved_time = local_ms; local_ms = UINT64_MAX;
	CHECK(st_cterm_poll(f.terminal) == IO_ERROR_OUT_OF_RANGE); local_ms = saved_time;
	local_ms += UINT64_C(172800000); /* Two muted idle days, including 32-bit hosts. */
	CHECK(st_cterm_poll(f.terminal) == 0 && !st_igs_gist_active(&a->gist));
	/* An unavailable enabled backend retains logical playback and edits. */
	st_igs_test_open_failure = true; st_audio_runtime_set_allowed(runtime,AUDIO_BACKEND_OSS);
	effect(&f,(int[]){0},1); CHECK(a->stream == NULL && a->warned);
	local_ms += 10; CHECK(st_cterm_poll(f.terminal) == 0 && !st_igs_gist_active(&a->gist));
	st_igs_test_open_failure = false; st_audio_runtime_set_allowed(runtime,0);
	st_audio_runtime_stop(runtime); effect(&f,(int[]){23,-164},2);
	CHECK(a->stream == NULL && a->multiplier == -164);
	CHECK(st_cterm_reset_terminal(f.terminal) == 0 && igs->audio == NULL);
	effect(&f,(int[]){23,5},2); CHECK(igs->audio->multiplier == 5);
	CHECK(memcmp(igs->audio->patches,st_igs_gist_patches,sizeof(igs->audio->patches)) == 0);
	destroy(&f); st_audio_runtime_destroy(runtime);
	/* Clock failure precisely at output-to-local handoff cannot later
	 * double-count the interval the audio consumer already advanced. */
	create(&f,0,false); runtime = attach(&f,true); igs = f.terminal->igs_engine;
	short_patch(&f); effect(&f,(int[]){0},1); a = igs->audio;
	int16_t frame[2]; CHECK(st_audio_runtime_pull(runtime,frame,1) == 0);
	f.terminal->screen.audio = NULL; igs->now_ms = bad_clock;
	CHECK(st_cterm_poll(f.terminal) == IO_ERROR_IO && a->stream == NULL && a->local_anchor_needed);
	local_ms += 1000; igs->now_ms = now;
	CHECK(st_cterm_poll(f.terminal) == 0 && !a->local_anchor_needed && a->gist.voice[0].parameters[0] == 2);
	local_ms += 5; CHECK(st_cterm_poll(f.terminal) == 0 && a->gist.voice[0].parameters[0] == 1);
	local_ms += 5; CHECK(st_cterm_poll(f.terminal) == 0 && !st_igs_gist_active(&a->gist));
	f.terminal->screen.audio = runtime; CHECK(st_cterm_poll(f.terminal) == 0 && a->stream != NULL);
	destroy(&f); st_audio_runtime_destroy(runtime);
}

struct consumer {
	st_audio_runtime_t *runtime;
	st_mutex_t mutex; st_condition_t condition;
	unsigned permits,completed;
	bool stop;
};
static int
consume(void *context)
{
	struct consumer *c = context; int16_t frames[512];
	for (;;) {
		CHECK(st_mutex_lock(&c->mutex) == ST_THREAD_SUCCESS);
		while (c->permits == 0 && !c->stop)
			CHECK(st_condition_wait(&c->condition,&c->mutex) == ST_THREAD_SUCCESS);
		bool stop = c->stop;
		if (!stop) --c->permits;
		CHECK(st_mutex_unlock(&c->mutex) == ST_THREAD_SUCCESS);
		if (stop) break;
		int status = st_audio_runtime_pull(c->runtime,frames,256);
		CHECK(status == 0 || status == IO_ERROR_CANCELLED);
		CHECK(st_mutex_lock(&c->mutex) == ST_THREAD_SUCCESS);
		++c->completed; CHECK(st_condition_broadcast(&c->condition) == ST_THREAD_SUCCESS);
		CHECK(st_mutex_unlock(&c->mutex) == ST_THREAD_SUCCESS);
	}
	return 0;
}
static void
concurrency(void)
{
	struct fixture f; create(&f,0,false); st_audio_runtime_t *runtime = attach(&f,true);
	struct consumer c = {.runtime = runtime};
	CHECK(st_mutex_init(&c.mutex,ST_MUTEX_PLAIN) == ST_THREAD_SUCCESS);
	CHECK(st_condition_init(&c.condition) == ST_THREAD_SUCCESS);
	st_thread_t thread; CHECK(st_thread_create(&thread,consume,&c) == ST_THREAD_SUCCESS);
	for (unsigned i = 0; i < 8; ++i) {
		short_patch(&f); request(&f,(int[]){0},1);
		CHECK(st_mutex_lock(&c.mutex) == ST_THREAD_SUCCESS); ++c.permits;
		CHECK(st_condition_broadcast(&c.condition) == ST_THREAD_SUCCESS);
		CHECK(st_mutex_unlock(&c.mutex) == ST_THREAD_SUCCESS);
		request(&f,(int[]){20,0,0,0,0,0,3},7); request(&f,(int[]){22,0},2);
		CHECK(st_cterm_reset_terminal(f.terminal) == 0 && f.terminal->igs_engine->audio == NULL);
		CHECK(st_mutex_lock(&c.mutex) == ST_THREAD_SUCCESS);
		while (c.completed <= i) CHECK(st_condition_wait(&c.condition,&c.mutex) == ST_THREAD_SUCCESS);
		CHECK(st_mutex_unlock(&c.mutex) == ST_THREAD_SUCCESS);
	}
	CHECK(st_mutex_lock(&c.mutex) == ST_THREAD_SUCCESS); c.stop = true;
	CHECK(st_condition_broadcast(&c.condition) == ST_THREAD_SUCCESS);
	CHECK(st_mutex_unlock(&c.mutex) == ST_THREAD_SUCCESS);
	st_audio_runtime_stop(runtime);
	CHECK(st_thread_join(thread,NULL) == ST_THREAD_SUCCESS);
	st_condition_destroy(&c.condition); st_mutex_destroy(&c.mutex);
	destroy(&f); st_audio_runtime_destroy(runtime);
}

int
main(void)
{
	counts(); banks(); parser(); pcm_and_console(); muted_and_errors(); concurrency();
	/* Exercise the fixture's indexed seed helper as an independent no-draw
	 * assertion after b. Audio state must not disturb either pixel phase. */
	struct fixture f; create(&f,0,false); feed(&f,"\033f");
	const struct st_console_rect rect = {0,0,320,200}; seed(&f,&rect,769,770);
	request(&f,(int[]){23,0},2); request(&f,(int[]){5},1);
	struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f.console,&rect,false) == 0);
	for (size_t i = 0; i < 320*200; ++i) CHECK(b->primary[i] == 769 && b->alternate[i] == 770);
	st_pixel_buffer_destroy(b); destroy(&f);
	return 0;
}
