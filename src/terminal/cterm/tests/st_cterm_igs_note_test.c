/* n: independent note-helper arguments, local waits and release/stop order. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "igs_test_fixture.h"
#include "cterm_igs_audio.h"
#include "st_audio_runtime.h"

extern size_t st_igs_test_calls;
extern int st_igs_test_arguments[4];
extern const uint16_t *st_igs_test_patch;
extern unsigned st_igs_test_stops[3], st_igs_test_stop_count;
extern unsigned st_igs_test_releases[3], st_igs_test_release_count;
extern bool st_igs_test_allocation_failure, st_igs_test_open_failure;

struct note_case { int values[6],normalized[6],patch; unsigned action_count; int actions[3][2]; };
#include "data/igs_note_vectors.h"
_Static_assert(sizeof(note_cases)/sizeof(*note_cases) == 235,"complete independent note cases");
static unsigned clock_reads, stops_at_clock, releases_at_clock;
static size_t on_at_clock, forwarded;
static int clock_error;

static int
note_now(uint64_t *value)
{
	++clock_reads; on_at_clock = st_igs_test_calls;
	stops_at_clock = st_igs_test_stop_count; releases_at_clock = st_igs_test_release_count;
	if (clock_error != 0) return clock_error;
	*value = local_ms; return 0;
}
static void
no_reply(const char *bytes, size_t count, void *context)
{
	(void)bytes; (void)count; (void)context; CHECK(false);
}
static void
key_output(const char *bytes, size_t count, void *context)
{
	(void)bytes; (void)context; forwarded += count;
}
static st_audio_runtime_t *
prepare(struct fixture *f)
{
	st_audio_runtime_t *runtime = NULL; CHECK(st_audio_runtime_create_pull(&runtime) == 0);
	f->terminal->screen.audio = runtime;
	CHECK(cterm_igs_audio_effect(f->terminal->igs_engine,(int[]){23,0},2) == 0);
	CHECK(cterm_igs_audio_effect(f->terminal->igs_engine,(int[]){5},1) == 0);
	CHECK(f->terminal->igs_engine->audio->stream != NULL);
	f->terminal->igs_engine->now_ms = note_now;
	f->terminal->response_cb = no_reply; f->terminal->keystroke_cb = key_output;
	return runtime;
}
static void
clear(struct fixture *f)
{
	CHECK(cterm_igs_audio_effect(f->terminal->igs_engine,(int[]){21},1) == 0);
	st_igs_test_calls = 0; st_igs_test_stop_count = st_igs_test_release_count = 0;
	clock_reads = 0; clock_error = 0; forwarded = 0;
}
static void
request(struct fixture *f, const int v[6])
{
	for (unsigned i = 0; i < 6; ++i) f->terminal->igs_engine->math[i] = (int16_t)v[i];
	feed(f,"G#n 0],1],2],3],4],5]:");
}

static void
vectors(void)
{
	struct fixture f; create(&f,0,false); st_audio_runtime_t *runtime = prepare(&f);
	struct cterm_igs *igs = f.terminal->igs_engine; struct cterm_igs_audio *a = igs->audio;
	for (size_t i = 0; i < sizeof(note_cases)/sizeof(*note_cases); ++i) {
		const struct note_case *c = &note_cases[i]; clear(&f);
		int values[6]; memcpy(values,c->values,sizeof(values));
		int status = cterm_igs_audio_note_begin(igs,values);
		CHECK(status == (c->normalized[4] > 0 ? IO_ERROR_IN_PROGRESS : 0));
		CHECK(memcmp(values,c->values,sizeof(values)) == 0);
		CHECK(clock_reads == 1 && on_at_clock == (c->patch >= 0 ? 1u : 0u));
		CHECK(stops_at_clock == 0 && releases_at_clock == 0);
		CHECK(st_igs_test_calls == (c->patch >= 0 ? 1u : 0u));
		if (c->patch >= 0) {
			CHECK(st_igs_test_patch == a->patches[c->patch]);
			CHECK(memcmp(st_igs_test_arguments,(int[]){c->normalized[1],c->normalized[2],c->normalized[3],7},sizeof(st_igs_test_arguments)) == 0);
		}
		if (c->normalized[4] > 0) {
			CHECK(igs->note.active && !igs->note.key && st_cterm_input_paused(f.terminal));
			CHECK(igs->note.voice == c->normalized[1] && igs->note.stop == c->normalized[5]);
			CHECK(igs->wait_deadline == local_ms+(uint64_t)c->normalized[4]*5);
			CHECK(st_igs_test_stop_count == 0 && st_igs_test_release_count == 0);
			CHECK(cterm_igs_audio_note_begin(igs,values) == IO_ERROR_BUSY);
			CHECK(st_igs_test_calls == (c->patch >= 0 ? 1u : 0u));
			CHECK(cterm_igs_audio_note_end(igs) == 0);
		}
		CHECK(!igs->note.active && !st_cterm_input_paused(f.terminal) && igs->wait_deadline == 0);
		CHECK(clock_reads == 1 && st_igs_test_release_count+st_igs_test_stop_count == c->action_count);
		for (unsigned k = 0; k < c->action_count; ++k)
			CHECK((c->actions[k][0] == 1 ? st_igs_test_releases[k] : st_igs_test_stops[k]) == (unsigned)c->actions[k][1]);
		CHECK(memcmp(a->patches,st_igs_gist_patches,sizeof(a->patches)) == 0);
		CHECK(memcmp(a->saved,st_igs_gist_patches,sizeof(a->saved)) == 0);
	}
	CHECK(cterm_igs_audio_note_begin(igs,NULL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(cterm_igs_audio_note_end(igs) == IO_ERROR_INVALID_ARGUMENT);
	clear(&f); igs->random_seed = 1; igs->random_range[0][0] = 0; igs->random_range[0][1] = 255;
	feed(&f,"G#n r,1,r,60,0,4:");
	CHECK(igs->random_seed == UINT32_C(0x5eb5ca17) && st_igs_test_calls == 1);
	CHECK(st_igs_test_patch == a->patches[19]);
	CHECK(memcmp(st_igs_test_arguments,(int[]){1,15,60,7},sizeof(st_igs_test_arguments)) == 0);
	destroy(&f); st_audio_runtime_destroy(runtime);
}

static void
waits_and_keys(void)
{
	struct fixture f; create(&f,0,false); st_audio_runtime_t *runtime = prepare(&f);
	struct cterm_igs *igs = f.terminal->igs_engine; struct cterm_igs_audio *a = igs->audio;
	static const int delays[] = {-32768,-1,0,1,2,32767};
	for (size_t i = 0; i < sizeof(delays)/sizeof(*delays); ++i) {
		clear(&f); uint64_t start = local_ms;
		request(&f,(int[]){0,1,15,60,delays[i],2});
		if (delays[i] <= 0) {
			CHECK(!igs->note.active && a->gist.voice[1].parameters[0] == 0);
			continue;
		}
		CHECK(igs->wait_deadline == start+(uint64_t)delays[i]*5 && igs->note.active);
		CHECK(igs->input_deadline == 0 && igs->text_deadline == 0 && st_igs_test_stop_count == 0);
		local_ms = igs->wait_deadline-1;
		CHECK(st_cterm_poll(f.terminal) == 0 && igs->note.active && st_igs_test_stop_count == 0);
		++local_ms; CHECK(st_cterm_poll(f.terminal) == 0 && !igs->note.active);
		CHECK(st_igs_test_stop_count == 1 && st_igs_test_stops[0] == 1 && a->gist.voice[1].parameters[0] == 0);
	}
	/* Cross the old 200Hz low-word boundary without an accidental early exit. */
	local_ms = UINT64_C(327675); clear(&f); request(&f,(int[]){125,0,0,0,10,2});
	local_ms = UINT64_C(327680); CHECK(st_cterm_poll(f.terminal) == 0 && igs->note.active);
	local_ms = UINT64_C(327724); CHECK(st_cterm_poll(f.terminal) == 0 && igs->note.active);
	++local_ms; CHECK(st_cterm_poll(f.terminal) == 0 && !igs->note.active);
	for (unsigned which = 0; which < 7; ++which) {
		clear(&f); igs->loop_mode = 1;
		request(&f,(int[]){0,2,15,60,100,2});
		bool consumed;
		switch (which) {
		case 0: consumed = st_cterm_handle_key(f.terminal,0,0); break;
		case 1: consumed = st_cterm_handle_key(f.terminal,3,0); break;
		case 2: consumed = st_cterm_handle_key(f.terminal,24,0); break;
		case 3: consumed = st_cterm_handle_codepage_key(f.terminal,255); break;
		case 4: consumed = st_cterm_handle_text_key(f.terminal,0x1f600); break;
		case 5: consumed = st_cterm_handle_doorway_key(f.terminal,59); break;
		default: consumed = st_cterm_handle_key(f.terminal,'+',0); break;
		}
		CHECK(consumed && igs->note.key && forwarded == 0);
		CHECK(st_cterm_poll(f.terminal) == 0 && !igs->note.active && igs->loop_mode == 1);
		CHECK(st_igs_test_stop_count == 1 && st_igs_test_stops[0] == 2);
		CHECK(st_cterm_handle_codepage_key(f.terminal,'a') && forwarded == 1);
	}
	/* An event pending on the final iteration is consumed even at the deadline. */
	clear(&f); request(&f,(int[]){125,0,0,0,1,0}); local_ms = igs->wait_deadline;
	CHECK(st_cterm_handle_key(f.terminal,'x',0) && forwarded == 0);
	CHECK(st_cterm_poll(f.terminal) == 0 && !igs->note.active);
	clear(&f); request(&f,(int[]){125,0,0,0,0,0});
	CHECK(st_cterm_handle_codepage_key(f.terminal,'x') && forwarded == 1);
	igs->loop_mode = 0;
	destroy(&f); st_audio_runtime_destroy(runtime);
}

static void
mutable_and_priority(void)
{
	struct fixture f; create(&f,0,false); st_audio_runtime_t *runtime = prepare(&f);
	struct cterm_igs *igs = f.terminal->igs_engine; struct cterm_igs_audio *a = igs->audio;
	clear(&f);
	CHECK(cterm_igs_audio_effect(igs,(int[]){20,0,3,0,0,0,2},7) == 0);
	uint16_t snapshot[20][56]; memcpy(snapshot,a->patches,sizeof(snapshot));
	for (int voice = 0; voice < 3; ++voice) request(&f,(int[]){3,voice,10,60,0,0});
	CHECK(st_igs_test_calls == 3);
	for (unsigned i = 0; i < 3; ++i)
		CHECK(a->gist.voice[i].parameters[0] == 2 && a->gist.voice[i].priority == 7 && a->gist.voice[i].control == 60);
	struct st_igs_gist_voice before[3]; memcpy(before,a->gist.voice,sizeof(before));
	CHECK(cterm_igs_audio_effect(igs,(int[]){0},1) == 0);
	CHECK(memcmp(before,a->gist.voice,sizeof(before)) == 0); /* b priority 1 cannot replace any n voice. */
	CHECK(cterm_igs_audio_effect(igs,(int[]){22,3},2) == 0);
	CHECK(memcmp(before,a->gist.voice,sizeof(before)) == 0);
	for (unsigned i = 0; i < 3; ++i) CHECK(a->patches[3][i] == st_igs_gist_patches[3][i]);
	memcpy(snapshot,a->patches,sizeof(snapshot));
	/* Higher-priority rejection still waits and applies the requested stop. */
	a->gist.voice[1].priority = 8; st_igs_test_calls = st_igs_test_stop_count = 0;
	request(&f,(int[]){2,1,15,60,1,2}); CHECK(st_igs_test_calls == 1 && igs->note.active);
	CHECK(a->gist.voice[1].priority == 8 && st_igs_test_stop_count == 0);
	local_ms = igs->wait_deadline; CHECK(st_cterm_poll(f.terminal) == 0);
	CHECK(a->gist.voice[1].parameters[0] == 0 && a->gist.voice[0].parameters[0] == 2 && a->gist.voice[2].parameters[0] == 2);
	CHECK(memcmp(snapshot,a->patches,sizeof(snapshot)) == 0);
	destroy(&f); st_audio_runtime_destroy(runtime);
}

static void
pcm_release(void)
{
	struct fixture f; create(&f,0,false); st_audio_runtime_t *runtime = prepare(&f); clear(&f);
	struct cterm_igs *igs = f.terminal->igs_engine; struct cterm_igs_audio *a = igs->audio;
	/* Attack to 15, sustain there, release by one amplitude unit per update. */
	memset(a->patches[0],0,sizeof(a->patches[0]));
	a->patches[0][0] = 100; a->patches[0][1] = 125; a->patches[0][2] = 65535; a->patches[0][3] = 15;
	a->patches[0][4] = 1; a->patches[0][5] = 15; a->patches[0][9] = 15; a->patches[0][11] = 65535;
	request(&f,(int[]){0,0,15,60,1,1});
	int16_t out[442]; CHECK(st_audio_runtime_pull(runtime,out,221) == 0);
	CHECK(a->psg.registers[8] == 15 && a->gist.voice[0].parameters[0] == 100);
	local_ms = igs->wait_deadline; CHECK(st_cterm_poll(f.terminal) == 0);
	CHECK(!igs->note.active && a->gist.voice[0].parameters[0] == 1 && a->psg.registers[8] == 15);
	CHECK(st_audio_runtime_pull(runtime,out,220) == 0 && a->gist.voice[0].parameters[4] == 4);
	CHECK(st_audio_runtime_pull(runtime,out,221) == 0 && a->psg.registers[8] == 14);
	bool energy = false;
	for (unsigned i = 0; i < 442; ++i) energy |= out[i] != 0;
	CHECK(energy);
	for (unsigned i = 0; i < 20; ++i) CHECK(st_audio_runtime_pull(runtime,out,221) == 0);
	CHECK(!st_igs_gist_active(&a->gist) && a->psg.registers[8] == 0);
	request(&f,(int[]){0,2,15,60,1,2}); CHECK(st_audio_runtime_pull(runtime,out,221) == 0);
	local_ms = igs->wait_deadline; CHECK(st_cterm_poll(f.terminal) == 0);
	CHECK(a->gist.voice[2].parameters[0] == 0 && a->psg.registers[10] == 0);
	CHECK(a->patches[0][0] == 100 && a->patches[0][4] == 1);
	destroy(&f); st_audio_runtime_destroy(runtime);
}

static void
partitions_and_errors(void)
{
	struct fixture f; create(&f,0,false);
	const char wire[] = "AG#n>2,1,10,60,1,2:M 2:B";
	for (size_t split = 0; split <= sizeof(wire); ++split) {
		CHECK(st_cterm_reset_terminal(f.terminal) == 0); st_igs_test_calls = 0;
		size_t offset = 0, size = sizeof(wire)-1;
		while (offset < size) {
			size_t count = split > size ? 1 : offset < split ? split-offset : size-offset, used;
			CHECK(st_cterm_write(f.terminal,wire+offset,count,&used) == 0); offset += used;
			if (f.terminal->igs_engine->note.active) {
				local_ms = f.terminal->igs_engine->wait_deadline; CHECK(st_cterm_poll(f.terminal) == 0);
			}
		}
		CHECK(st_igs_test_calls == 1 && !st_cterm_input_paused(f.terminal));
		CHECK(f.terminal->igs_engine->attributes.writing_mode == 2);
		struct st_console_cell cells[2]; const struct st_console_rect rect = {0,0,2,1};
		CHECK(st_console_read_cells(f.console,&rect,cells,2) == 0 && cells[0].glyph == 'A' && cells[1].glyph == 'B');
	}
	CHECK(st_cterm_reset_terminal(f.terminal) == 0); st_igs_test_calls = 0;
	feed(&f,"G#n 2,1,10,60,1"); CHECK(st_cterm_finish(f.terminal) == 0 && st_igs_test_calls == 0);
	feed(&f,"G#n 2,1,10,60,1"); local_ms += 5000; CHECK(st_cterm_poll(f.terminal) == 0 && st_igs_test_calls == 0);
	const char invalid[] = "G#n 0,0,15,60,10000,4:"; size_t used;
	CHECK(st_cterm_write(f.terminal,invalid,sizeof(invalid)-1,&used) == 0 && st_igs_test_calls == 0);
	feed(&f,"G#M 1:"); request(&f,(int[]){0,0,15,60,32767,0});
	CHECK(st_cterm_finish(f.terminal) == 0 && !st_cterm_input_paused(f.terminal));
	CHECK(!st_igs_gist_active(&f.terminal->igs_engine->audio->gist));
	request(&f,(int[]){0,0,15,60,32767,0});
	CHECK(st_cterm_reset_terminal(f.terminal) == 0 && !st_cterm_input_paused(f.terminal) && f.terminal->igs_engine->audio == NULL);
	destroy(&f);
	/* Trigger-before-clock-error and retained wait state on a transient poll error. */
	create(&f,0,false); st_audio_runtime_t *runtime = prepare(&f); clear(&f);
	struct cterm_igs *igs = f.terminal->igs_engine; struct cterm_igs_audio *a = igs->audio;
	clock_error = IO_ERROR_IO;
	CHECK(cterm_igs_audio_note_begin(igs,(int[]){2,0,15,60,0,2}) == IO_ERROR_IO);
	CHECK(st_igs_test_calls == 1 && stops_at_clock == 0 && a->gist.voice[0].parameters[0] != 0 && !igs->note.active);
	clock_error = 0; clear(&f); local_ms = UINT64_MAX-4;
	CHECK(cterm_igs_audio_note_begin(igs,(int[]){2,0,15,60,1,2}) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_igs_test_calls == 1 && st_igs_test_stop_count == 0 && !igs->note.active);
	local_ms = 1000; clear(&f); request(&f,(int[]){2,0,15,60,1,2});
	clock_error = IO_ERROR_IO; CHECK(st_cterm_poll(f.terminal) == IO_ERROR_IO && igs->note.active);
	clock_error = 0; local_ms = igs->wait_deadline; CHECK(st_cterm_poll(f.terminal) == 0 && !igs->note.active);
	clear(&f); request(&f,(int[]){2,0,15,60,1,2}); CHECK(st_cterm_handle_key(f.terminal,3,0));
	clock_error = IO_ERROR_IO; CHECK(st_cterm_poll(f.terminal) == IO_ERROR_IO && igs->note.key);
	clock_error = 0; CHECK(st_cterm_poll(f.terminal) == 0 && !igs->note.active);
	clear(&f); local_ms = UINT64_MAX-UINT64_C(163835); request(&f,(int[]){125,0,0,0,32767,0});
	CHECK(igs->wait_deadline == UINT64_MAX); ++local_ms;
	CHECK(st_cterm_handle_key(f.terminal,0,0) && st_cterm_poll(f.terminal) == 0 && !igs->note.active);
	local_ms = 1000; destroy(&f); st_audio_runtime_destroy(runtime);
	/* Fresh-model failures cannot trigger a note or leave a pending wait. */
	create(&f,0,false); igs = f.terminal->igs_engine; st_igs_test_calls = 0;
	st_igs_test_allocation_failure = true;
	CHECK(cterm_igs_audio_note_begin(igs,(int[]){0,0,15,60,1,2}) == IO_ERROR_OUT_OF_MEMORY);
	st_igs_test_allocation_failure = false;
	CHECK(igs->audio == NULL && !igs->note.active && st_igs_test_calls == 0);
	igs->now_ms = note_now; clock_error = IO_ERROR_IO;
	CHECK(cterm_igs_audio_note_begin(igs,(int[]){0,0,15,60,1,2}) == IO_ERROR_IO && igs->audio == NULL);
	clock_error = 0;
	CHECK(st_audio_runtime_create(&runtime,AUDIO_BACKEND_OSS) == 0); f.terminal->screen.audio = runtime;
	st_igs_test_open_failure = true; request(&f,(int[]){0,0,15,60,1,2});
	CHECK(igs->note.active && igs->audio->stream == NULL && igs->audio->warned);
	local_ms = igs->wait_deadline; CHECK(st_cterm_poll(f.terminal) == 0 && !igs->note.active);
	CHECK(!st_igs_gist_active(&igs->audio->gist));
	st_igs_test_open_failure = false; st_audio_runtime_set_allowed(runtime,0);
	destroy(&f); st_audio_runtime_destroy(runtime);
}

static void
pixels_and_muted(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution)
		for (unsigned status = 0; status < 2; ++status) {
			struct fixture f; create(&f,resolution,status != 0);
			struct cterm_igs *igs = f.terminal->igs_engine;
			f.terminal->response_cb = no_reply; feed(&f,"\033fG#p>3,2:X 10,19,17:");
			const struct st_console_rect rect = {0,0,igs->width,igs->height}; seed(&f,&rect,769,768);
			struct cterm_igs_attributes attrs = igs->attributes; uint32_t palette[16]; memcpy(palette,igs->palette,sizeof(palette));
			for (int scaling = 0; scaling < 3; ++scaling) {
				cterm_igs_set_scaling(igs,scaling);
				for (unsigned arrays = 0; arrays < 2; ++arrays) {
					if (arrays) request(&f,(int[]){2,1,10,60,1,2});
					else feed(&f,"G#n 2,1,10,60,1,2:");
					CHECK(memcmp(st_igs_test_arguments,(int[]){1,10,60,7},sizeof(st_igs_test_arguments)) == 0);
					CHECK(igs->wait_deadline == local_ms+5);
					local_ms += 5; CHECK(st_cterm_poll(f.terminal) == 0 && !igs->note.active);
					CHECK(igs->audio->stream == NULL && igs->audio->gist.voice[1].parameters[0] == 0);
				}
			}
			CHECK(memcmp(&attrs,&igs->attributes,sizeof(attrs)) == 0 && memcmp(palette,igs->palette,sizeof(palette)) == 0);
			CHECK(igs->draw_x == 19 && igs->draw_y == 17 && f.terminal->xpos == 4 && f.terminal->ypos == 3);
			struct st_pixel_buffer *b = NULL; CHECK(st_console_get_pixels(&b,f.console,&rect,false) == 0);
			for (size_t i = 0; i < (size_t)igs->width*igs->height; ++i) CHECK(b->primary[i] == 769 && b->alternate[i] == 768);
			st_pixel_buffer_destroy(b); destroy(&f);
		}
}

int main(void) { vectors(); waits_and_keys(); mutable_and_priority(); pcm_release(); partitions_and_errors(); pixels_and_muted(); return 0; }
