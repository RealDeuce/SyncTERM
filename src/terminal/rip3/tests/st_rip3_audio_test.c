/* Literal PCM fixtures and headless playback/lifetime expectations. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_audio.h"
#include "st_audio_file.h"
#include "filesystem.h"
#include "path.h"
#include "st_threads.h"
#include "io_error.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
		return 1; \
	} \
} while (0)

/* Four stereo frames at the engine's rate: 1000/-1000 in every frame. */
static const uint8_t wave[] = {
	'R','I','F','F',52,0,0,0,'W','A','V','E',
	'f','m','t',' ',16,0,0,0,1,0,2,0,0x44,0xac,0,0,
	0x10,0xb1,2,0,4,0,16,0,'d','a','t','a',16,0,0,0,
	0xe8,3,0x18,0xfc,0xe8,3,0x18,0xfc,0xe8,3,0x18,0xfc,0xe8,3,0x18,0xfc,
};

static int
put(const char *path, const void *bytes, size_t size)
{
	CHECK(fs_replace_file(path, bytes, size) == 0);
	return 0;
}

static int
pull(audio_engine_t *engine, size_t count, size_t sounding, int16_t left, int16_t right)
{
	int16_t frames[32 * 2];
	CHECK(count <= 32 && sounding <= count);
	memset(frames, 0x55, sizeof(frames));
	CHECK(audio_engine_pull(engine, frames, count) == 0);
	for (size_t i = 0; i < count; i++) {
		CHECK(frames[i * 2] == (i < sounding ? left : 0));
		CHECK(frames[i * 2 + 1] == (i < sounding ? right : 0));
	}
	return 0;
}

static void
little(uint8_t *bytes, uint32_t value, unsigned count)
{
	for (unsigned i = 0; i < count; i++) bytes[i] = (uint8_t)(value >> (8 * i));
}

static int
profile(struct st_rip3_audio *player, audio_engine_t *engine,
    const struct st_rip3_resource_paths *paths, const char *file,
    unsigned bits, unsigned channels, unsigned rate,
    const uint8_t *samples, size_t bytes, size_t sounding, int16_t left, int16_t right)
{
	uint8_t encoded[96] = {0};
	unsigned align = channels * (bits / 8);
	size_t size = 44 + bytes + (bytes & 1);

	CHECK(size <= sizeof(encoded));
	memcpy(encoded, wave, 44);
	little(encoded + 4, (uint32_t)size - 8, 4);
	little(encoded + 22, channels, 2); little(encoded + 24, rate, 4);
	little(encoded + 28, rate * align, 4); little(encoded + 32, align, 2);
	little(encoded + 34, bits, 2); little(encoded + 40, (uint32_t)bytes, 4);
	memcpy(encoded + 44, samples, bytes);
	CHECK(put(file, encoded, size) == 0);
	CHECK(st_rip3_audio_play(player, paths, "bad.wav") == 0);
	CHECK(pull(engine, sounding + 2, sounding, left, right) == 0);
	return 0;
}

struct consumer {
	audio_engine_t *engine;
	st_mutex_t mutex;
	st_condition_t condition;
	atomic_bool stop;
	bool ready, failed;
	unsigned pulls;
};

static int
consume(void *context)
{
	struct consumer *consumer = context;
	int16_t frames[7 * 2];

	(void)st_mutex_lock(&consumer->mutex);
	consumer->ready = true;
	(void)st_condition_signal(&consumer->condition);
	(void)st_mutex_unlock(&consumer->mutex);
	do {
		if (audio_engine_pull(consumer->engine, frames, 7) != 0) {
			consumer->failed = true;
			return 1;
		}
		for (size_t i = 0; i < 7; i++) {
			if (!((frames[i * 2] == 0 && frames[i * 2 + 1] == 0)
			    || (frames[i * 2] == 998 && frames[i * 2 + 1] == -999))) {
				consumer->failed = true;
				return 1;
			}
		}
		consumer->pulls++;
		/* A headless consumer has no device clock to yield execution. Give
		 * control operations a turn, including under instrumented mutexes. */
		st_thread_yield();
	} while (!atomic_load_explicit(&consumer->stop, memory_order_acquire));
	return 0;
}

int
main(int argc, char **argv)
{
	audio_engine_t *engine = NULL;
	struct st_rip3_audio *player = NULL;
	struct st_rip3_resource_paths paths = {0};
	char *file = NULL, *replacement = NULL, *bad_file = NULL;
	uint8_t changed[sizeof(wave)], malformed[sizeof(wave)];
	CHECK(argc == 2 && fs_create_directories(argv[1]) == 0);
	CHECK(path_join_alloc(&file, argv[1], "tone.wav") == 0);
	CHECK(path_join_alloc(&replacement, argv[1], "other.dat") == 0);
	CHECK(path_join_alloc(&bad_file, argv[1], "bad.wav") == 0);
	paths.host = argv[1];
	CHECK(put(file, wave, sizeof(wave)) == 0);
	memcpy(changed, wave, sizeof(wave));
	for (size_t i = 44; i < sizeof(changed); i += 4) {
		changed[i] = 0xd0; changed[i + 1] = 7;
		changed[i + 2] = 0x30; changed[i + 3] = 0xf8;
	}
	CHECK(put(replacement, changed, sizeof(changed)) == 0);
	CHECK(audio_engine_create_pull(&engine) == 0);
	CHECK(st_rip3_audio_create(NULL, engine, 0, 0) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_audio_create(&player, NULL, 0, 0) == IO_ERROR_INVALID_ARGUMENT && player == NULL);
	CHECK(st_rip3_audio_create(&player, engine, 97, 0) == IO_ERROR_INVALID_ARGUMENT && player == NULL);
	CHECK(st_rip3_audio_create(&player, engine, 0, 0) == 0);
	CHECK(st_rip3_audio_play(player, &paths, NULL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_audio_stop(NULL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_audio_stop(player) == 0 && pull(engine, 5, 0, 0, 0) == 0);
	/* Every short prefix fails before changing the stream. */
	for (size_t size = 0; size < sizeof(wave); size++) {
		CHECK(put(bad_file, wave, size) == 0);
		CHECK(st_rip3_audio_play(player, &paths, "bad.wav") == IO_ERROR_INVALID_FORMAT);
	}
	memcpy(malformed, wave, sizeof(wave)); malformed[20] = 3; /* IEEE float. */
	CHECK(put(bad_file, malformed, sizeof(malformed)) == 0);
	CHECK(st_rip3_audio_play(player, &paths, "bad.wav") == IO_ERROR_NOT_SUPPORTED);
	memcpy(malformed, wave, sizeof(wave)); malformed[32] = 3; /* Bad block alignment. */
	CHECK(put(bad_file, malformed, sizeof(malformed)) == 0);
	CHECK(st_rip3_audio_play(player, &paths, "bad.wav") == IO_ERROR_INVALID_FORMAT);
	memcpy(malformed, wave, sizeof(wave)); memset(malformed + 40, 0xff, 4);
	CHECK(put(bad_file, malformed, sizeof(malformed)) == 0);
	CHECK(st_rip3_audio_play(player, &paths, "bad.wav") == IO_ERROR_INVALID_FORMAT);
	CHECK(st_rip3_audio_play(player, &paths, "missing.wav") == 0);
	CHECK(st_rip3_audio_play(player, &paths, "../tone.wav") == IO_ERROR_INVALID_ARGUMENT);
	if (st_audio_file_available()) {
		CHECK(st_rip3_audio_play(player, &paths, "TONE.WAV") == 0);
		/* Existing mixer Q15 scaling then tanh limiting gives literal pairs
		 * 1000/-1000 -> 998/-999 and 2000/-2000 -> 1996/-1997. */
		CHECK(pull(engine, 2, 2, 998, -999) == 0);
		CHECK(st_rip3_audio_play(player, &paths, "missing.wav") == 0);
		/* Valid container but empty PCM reaches the shared decoder's failure
		 * path; the previous decoded tail remains owned by the player. */
		uint8_t empty[44];
		memcpy(empty, wave, sizeof(empty)); empty[4] = 36; memset(empty + 40, 0, 4);
		CHECK(put(bad_file, empty, sizeof(empty)) == 0);
		CHECK(st_rip3_audio_play(player, &paths, "bad.wav") == IO_ERROR_INVALID_FORMAT);
		CHECK(pull(engine, 5, 2, 998, -999) == 0); /* Failed lookup/load retained the tail. */
		CHECK(st_rip3_audio_play(player, &paths, "tone.wav") == 0);
		CHECK(pull(engine, 1, 1, 998, -999) == 0);
		CHECK(st_rip3_audio_play(player, &paths, "other.dat") == 0);
		CHECK(pull(engine, 6, 4, 1996, -1997) == 0); /* Replace, no queued old tail/mixing. */
		CHECK(st_rip3_audio_play(player, &paths, "tone.wav") == 0);
		CHECK(st_rip3_audio_play(player, NULL, "$OFF$") == 0);
		CHECK(pull(engine, 8, 0, 0, 0) == 0);
		CHECK(st_rip3_audio_play(player, &paths, "tone.wav") == 0);
		CHECK(st_rip3_audio_stop(player) == 0 && pull(engine, 8, 0, 0, 0) == 0);
		/* Unknown chunks are walked with RIFF word padding. */
		uint8_t padded[sizeof(wave) + 10];
		memcpy(padded, wave, 36); padded[4] = 62;
		memcpy(padded + 36, "JUNK\001\000\000\000\127\000", 10);
		memcpy(padded + 46, wave + 36, sizeof(wave) - 36);
		CHECK(put(bad_file, padded, sizeof(padded)) == 0);
		CHECK(st_rip3_audio_play(player, &paths, "bad.wav") == 0);
		CHECK(pull(engine, 6, 4, 998, -999) == 0);
		static const uint8_t mono8[] = {132,132,132}; /* +1024, with RIFF padding. */
		static const uint8_t mono16[] = {0xe8,3,0xe8,3,0xe8,3,0xe8,3};
		static const uint8_t stereo24[] = {
			0,0xe8,3,0,0x18,0xfc,0,0xe8,3,0,0x18,0xfc,
			0,0xe8,3,0,0x18,0xfc,0,0xe8,3,0,0x18,0xfc};
		static const uint8_t stereo32[] = {
			0,0,0xe8,3,0,0,0x18,0xfc,0,0,0xe8,3,0,0,0x18,0xfc,
			0,0,0xe8,3,0,0,0x18,0xfc,0,0,0xe8,3,0,0,0x18,0xfc};
		static const uint8_t three16[] = {
			0xe8,3,0x18,0xfc,0x30,0x75,0xe8,3,0x18,0xfc,0x30,0x75,
			0xe8,3,0x18,0xfc,0x30,0x75,0xe8,3,0x18,0xfc,0x30,0x75};
		CHECK(profile(player, engine, &paths, bad_file, 8,1,44100,mono8,sizeof(mono8),3,1022,1022) == 0);
		CHECK(profile(player, engine, &paths, bad_file, 8,1,11127,mono8,sizeof(mono8),12,1022,1022) == 0);
		CHECK(profile(player, engine, &paths, bad_file, 16,1,44100,mono16,sizeof(mono16),4,998,998) == 0);
		CHECK(profile(player, engine, &paths, bad_file, 24,2,44100,stereo24,sizeof(stereo24),4,998,-999) == 0);
		CHECK(profile(player, engine, &paths, bad_file, 32,2,44100,stereo32,sizeof(stereo32),4,998,-999) == 0);
		CHECK(profile(player, engine, &paths, bad_file, 16,3,44100,three16,sizeof(three16),4,998,-999) == 0);
		CHECK(profile(player, engine, &paths, bad_file, 16,2,22050,wave+44,16,8,998,-999) == 0);
		struct consumer consumer = {.engine=engine};
		st_thread_t thread;
		int result;
		atomic_init(&consumer.stop, false);
		CHECK(st_mutex_init(&consumer.mutex, ST_MUTEX_PLAIN) == ST_THREAD_SUCCESS);
		CHECK(st_condition_init(&consumer.condition) == ST_THREAD_SUCCESS);
		CHECK(st_thread_create(&thread, consume, &consumer) == ST_THREAD_SUCCESS);
		CHECK(st_mutex_lock(&consumer.mutex) == ST_THREAD_SUCCESS);
		while (!consumer.ready)
			CHECK(st_condition_wait(&consumer.condition, &consumer.mutex) == ST_THREAD_SUCCESS);
		CHECK(st_mutex_unlock(&consumer.mutex) == ST_THREAD_SUCCESS);
		for (unsigned i = 0; i < 50; i++) {
			CHECK(st_rip3_audio_play(player, &paths, "tone.wav") == 0);
			CHECK(st_rip3_audio_stop(player) == 0);
		}
		/* Destroy quiesces its callback while the engine consumer stays alive. */
		st_rip3_audio_destroy(player); player = NULL;
		atomic_store_explicit(&consumer.stop, true, memory_order_release);
		CHECK(st_thread_join(thread, &result) == ST_THREAD_SUCCESS && result == 0);
		CHECK(!consumer.failed && consumer.pulls != 0);
		st_condition_destroy(&consumer.condition); st_mutex_destroy(&consumer.mutex);
		CHECK(st_rip3_audio_create(&player, engine, 0, 0) == 0);
		CHECK(st_rip3_audio_play(player, &paths, "tone.wav") == 0);
		audio_engine_stop(engine);
		CHECK(st_rip3_audio_play(player, &paths, "tone.wav") == IO_ERROR_CANCELLED);
		CHECK(st_rip3_audio_stop(player) == 0);
		puts("RIP3 WAV PCM and concurrent playback lifecycle verified");
	}
	else {
		CHECK(st_rip3_audio_play(player, &paths, "tone.wav") == IO_ERROR_NOT_SUPPORTED);
		CHECK(pull(engine, 8, 0, 0, 0) == 0);
		puts("RIP3 WAV decoder unavailable: rejection and format/lifecycle checks verified");
	}
	st_rip3_audio_destroy(player);
	audio_engine_destroy(engine);
	CHECK(fs_remove_file(file) == 0 && fs_remove_file(replacement) == 0 && fs_remove_file(bad_file) == 0);
	CHECK(fs_remove_directory(argv[1]) == 0);
	free(file); free(replacement); free(bad_file);
	return 0;
}
