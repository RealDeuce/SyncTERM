/* Internal fixed-format software audio mixer. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_AUDIO_MIXER_H_
#define ST_AUDIO_MIXER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct audio_mixer audio_mixer_t;
typedef struct audio_mixer_stream audio_mixer_stream_t;

struct audio_mixer_buffer_options {
	float volume_left_db;
	float volume_right_db;
	size_t fade_in_frames;
	size_t fade_out_frames;
	bool crossfade;
	bool loop;
};

int audio_mixer_create(audio_mixer_t **result);
void audio_mixer_destroy(audio_mixer_t *mixer);

int audio_mixer_stream_create(audio_mixer_stream_t **result,
    audio_mixer_t *mixer, float volume_left_db, float volume_right_db);
void audio_mixer_stream_destroy(audio_mixer_stream_t *stream);

int audio_mixer_stream_append(audio_mixer_stream_t *stream,
    const int16_t *frames, size_t frame_count,
    const struct audio_mixer_buffer_options *options);
int audio_mixer_stream_append_take(audio_mixer_stream_t *stream,
    int16_t *frames, size_t frame_count,
    const struct audio_mixer_buffer_options *options);
int audio_mixer_stream_clear(audio_mixer_stream_t *stream);
int audio_mixer_stream_drain(audio_mixer_stream_t *stream);
bool audio_mixer_stream_is_idle(audio_mixer_stream_t *stream);
size_t audio_mixer_stream_buffered_frames(audio_mixer_stream_t *stream);
int audio_mixer_stream_set_volume(audio_mixer_stream_t *stream,
    float volume_left_db, float volume_right_db);
int audio_mixer_stream_ramp_volume(audio_mixer_stream_t *stream,
    float volume_left_db, float volume_right_db, size_t frame_count);
int audio_mixer_stream_get_volume(audio_mixer_stream_t *stream,
    float *volume_left_db, float *volume_right_db);

/* Fills every requested frame and returns the non-silent prefix length. */
size_t audio_mixer_pull(audio_mixer_t *mixer, int16_t *output,
    size_t frame_count);
/* Blocking backends acknowledge only after the device accepts the frames. */
size_t audio_mixer_pull_pending(audio_mixer_t *mixer, int16_t *output,
    size_t frame_count, uint64_t *ticket);
void audio_mixer_acknowledge(audio_mixer_t *mixer, uint64_t ticket);

/* Used by blocking output backends. */
int audio_mixer_wait_for_data(audio_mixer_t *mixer);
void audio_mixer_stop(audio_mixer_t *mixer, int status);

#endif
