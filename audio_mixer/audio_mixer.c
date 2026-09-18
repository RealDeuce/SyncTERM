/*
 * Copyright Rob Swindell - http://www.synchro.net/copyright.html
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "audio_mixer.h"

#include "io_error.h"
#include "st_threads.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define AUDIO_MIXER_CHANNELS 2
#define AUDIO_MIXER_FRAME_SIZE 4
#define AUDIO_MIXER_CHUNK_FRAMES 1024
#define AUDIO_MIXER_FADE_TABLE_SIZE 1024

struct audio_mixer_buffer {
	int16_t *frames;
	size_t frame_count;
	size_t consumed;
	float volume_left_db;
	float volume_right_db;
	size_t fade_in;
	size_t fade_out;
	bool loop;
	size_t loop_iteration;
	size_t overlay_total;
	size_t overlay_elapsed;
	uint64_t sequence;
	uint64_t pull_ticket;
	struct audio_mixer_buffer *next;
};

struct audio_mixer_stream {
	struct audio_mixer *mixer;
	struct audio_mixer_stream *next;
	struct audio_mixer_buffer *head;
	struct audio_mixer_buffer *tail;
	struct audio_mixer_buffer *retired_head;
	struct audio_mixer_buffer *retired_tail;
	st_mutex_t mutex;
	st_condition_t condition;
	float volume_left_db;
	float volume_right_db;
	float ramp_start_left;
	float ramp_start_right;
	float ramp_target_left;
	float ramp_target_right;
	float ramp_target_left_db;
	float ramp_target_right_db;
	size_t ramp_elapsed;
	size_t ramp_total;
	uint64_t next_sequence;
	uint64_t clear_generation;
	int status;
};

struct audio_mixer {
	st_mutex_t mutex;
	st_condition_t condition;
	struct audio_mixer_stream *streams;
	int64_t *accumulator;
	int16_t fade_table[AUDIO_MIXER_FADE_TABLE_SIZE];
	uint64_t next_pull_ticket;
	int status;
};

static bool
valid_volume(float value)
{
	return isfinite(value) && value >= -192.0f && value <= 96.0f;
}

static size_t
proportional_part(size_t whole, size_t part, size_t total)
{
	long double scaled;

	scaled = (long double)whole * (long double)part / (long double)total;
	if (scaled >= (long double)whole)
		return whole;
	return (size_t)scaled;
}

static void
free_buffer(struct audio_mixer_buffer *buffer)
{
	free(buffer->frames);
	free(buffer);
}

static void
drop_buffers_locked(audio_mixer_stream_t *stream)
{
	struct audio_mixer_buffer *buffer = stream->head;

	while (buffer != NULL) {
		struct audio_mixer_buffer *next = buffer->next;

		free_buffer(buffer);
		buffer = next;
	}
	stream->head = NULL;
	stream->tail = NULL;
	buffer = stream->retired_head;
	while (buffer != NULL) {
		struct audio_mixer_buffer *next = buffer->next;

		free_buffer(buffer);
		buffer = next;
	}
	stream->retired_head = NULL;
	stream->retired_tail = NULL;
}

static void
retire_buffer_locked(audio_mixer_stream_t *stream,
    struct audio_mixer_buffer *buffer, uint64_t pull_ticket)
{
	buffer->pull_ticket = pull_ticket;
	buffer->next = NULL;
	if (stream->retired_tail == NULL)
		stream->retired_head = buffer;
	else
		stream->retired_tail->next = buffer;
	stream->retired_tail = buffer;
}

int
audio_mixer_create(audio_mixer_t **result)
{
	audio_mixer_t *mixer;
	int status;
	int i;

	if (result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	mixer = calloc(1, sizeof(*mixer));
	if (mixer == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	mixer->accumulator = malloc(AUDIO_MIXER_CHUNK_FRAMES
	    * AUDIO_MIXER_CHANNELS * sizeof(*mixer->accumulator));
	if (mixer->accumulator == NULL) {
		free(mixer);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	status = st_mutex_init(&mixer->mutex, ST_MUTEX_PLAIN);
	if (status != ST_THREAD_SUCCESS) {
		free(mixer->accumulator);
		free(mixer);
		return IO_ERROR_IO;
	}
	status = st_condition_init(&mixer->condition);
	if (status != ST_THREAD_SUCCESS) {
		st_mutex_destroy(&mixer->mutex);
		free(mixer->accumulator);
		free(mixer);
		return IO_ERROR_IO;
	}
	for (i = 0; i < AUDIO_MIXER_FADE_TABLE_SIZE; i++) {
		double position = (double)i
		    / (double)(AUDIO_MIXER_FADE_TABLE_SIZE - 1);
		double gain = sin(position * 3.14159265358979323846 * 0.5);
		int value = (int)(gain * 32767.0 + 0.5);

		if (value > 32767)
			value = 32767;
		else if (value < 0)
			value = 0;
		mixer->fade_table[i] = (int16_t)value;
	}
	mixer->next_pull_ticket = 1;
	*result = mixer;
	return 0;
}

void
audio_mixer_destroy(audio_mixer_t *mixer)
{
	audio_mixer_stream_t *stream;

	if (mixer == NULL)
		return;
	audio_mixer_stop(mixer, IO_ERROR_CANCELLED);
	while ((stream = mixer->streams) != NULL)
		audio_mixer_stream_destroy(stream);
	st_condition_destroy(&mixer->condition);
	st_mutex_destroy(&mixer->mutex);
	free(mixer->accumulator);
	free(mixer);
}

int
audio_mixer_stream_create(audio_mixer_stream_t **result,
    audio_mixer_t *mixer, float volume_left_db, float volume_right_db)
{
	audio_mixer_stream_t *stream;
	int status;

	if (result == NULL || mixer == NULL || !valid_volume(volume_left_db)
	    || !valid_volume(volume_right_db))
		return IO_ERROR_INVALID_ARGUMENT;
	stream = calloc(1, sizeof(*stream));
	if (stream == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	status = st_mutex_init(&stream->mutex, ST_MUTEX_PLAIN);
	if (status != ST_THREAD_SUCCESS) {
		free(stream);
		return IO_ERROR_IO;
	}
	status = st_condition_init(&stream->condition);
	if (status != ST_THREAD_SUCCESS) {
		st_mutex_destroy(&stream->mutex);
		free(stream);
		return IO_ERROR_IO;
	}
	stream->mixer = mixer;
	stream->volume_left_db = volume_left_db;
	stream->volume_right_db = volume_right_db;
	stream->next_sequence = 1;
	if (st_mutex_lock(&mixer->mutex) != ST_THREAD_SUCCESS) {
		st_condition_destroy(&stream->condition);
		st_mutex_destroy(&stream->mutex);
		free(stream);
		return IO_ERROR_IO;
	}
	if (mixer->status != 0) {
		status = mixer->status;
		(void)st_mutex_unlock(&mixer->mutex);
		st_condition_destroy(&stream->condition);
		st_mutex_destroy(&stream->mutex);
		free(stream);
		return status;
	}
	stream->next = mixer->streams;
	mixer->streams = stream;
	(void)st_mutex_unlock(&mixer->mutex);
	*result = stream;
	return 0;
}

void
audio_mixer_stream_destroy(audio_mixer_stream_t *stream)
{
	audio_mixer_t *mixer;
	audio_mixer_stream_t **link;

	if (stream == NULL)
		return;
	mixer = stream->mixer;
	(void)st_mutex_lock(&mixer->mutex);
	for (link = &mixer->streams; *link != NULL; link = &(*link)->next) {
		if (*link == stream) {
			*link = stream->next;
			break;
		}
	}
	(void)st_mutex_lock(&stream->mutex);
	stream->status = IO_ERROR_CANCELLED;
	++stream->clear_generation;
	drop_buffers_locked(stream);
	(void)st_condition_broadcast(&stream->condition);
	(void)st_mutex_unlock(&stream->mutex);
	(void)st_mutex_unlock(&mixer->mutex);
	st_condition_destroy(&stream->condition);
	st_mutex_destroy(&stream->mutex);
	free(stream);
}

static int
prepare_buffer(struct audio_mixer_buffer **result, int16_t *frames,
    size_t frame_count, const struct audio_mixer_buffer_options *options)
{
	struct audio_mixer_buffer *buffer;
	size_t fade_in = 0;
	size_t fade_out = 0;

	if (result == NULL || frames == NULL || frame_count == 0)
		return IO_ERROR_INVALID_ARGUMENT;
	if (frame_count > SIZE_MAX / AUDIO_MIXER_FRAME_SIZE)
		return IO_ERROR_OUT_OF_RANGE;
	if (options != NULL) {
		if (!valid_volume(options->volume_left_db)
		    || !valid_volume(options->volume_right_db))
			return IO_ERROR_INVALID_ARGUMENT;
		fade_in = options->fade_in_frames < frame_count
		    ? options->fade_in_frames : frame_count;
		fade_out = options->fade_out_frames < frame_count
		    ? options->fade_out_frames : frame_count;
		if (fade_in > frame_count - fade_out) {
			size_t sum = fade_in + fade_out;

			fade_in = proportional_part(frame_count, fade_in, sum);
			fade_out = frame_count - fade_in;
		}
	}
	buffer = calloc(1, sizeof(*buffer));
	if (buffer == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	buffer->frames = frames;
	buffer->frame_count = frame_count;
	buffer->fade_in = fade_in;
	buffer->fade_out = fade_out;
	if (options != NULL) {
		buffer->volume_left_db = options->volume_left_db;
		buffer->volume_right_db = options->volume_right_db;
		buffer->loop = options->loop;
	}
	*result = buffer;
	return 0;
}

int
audio_mixer_stream_append_take(audio_mixer_stream_t *stream,
    int16_t *frames, size_t frame_count,
    const struct audio_mixer_buffer_options *options)
{
	struct audio_mixer_buffer *buffer;
	audio_mixer_t *mixer;
	int status;

	if (stream == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = prepare_buffer(&buffer, frames, frame_count, options);
	if (status != 0)
		return status;
	if (st_mutex_lock(&stream->mutex) != ST_THREAD_SUCCESS) {
		free(buffer);
		return IO_ERROR_IO;
	}
	if (stream->status != 0) {
		status = stream->status;
		(void)st_mutex_unlock(&stream->mutex);
		free(buffer);
		return status;
	}
	buffer->sequence = stream->next_sequence++;
	if (stream->tail != NULL) {
		stream->tail->loop = false;
		if (options != NULL && options->crossfade
		    && buffer->fade_in > 0) {
			stream->tail->overlay_total = buffer->fade_in;
			stream->tail->overlay_elapsed = 0;
		}
		stream->tail->next = buffer;
	}
	else
		stream->head = buffer;
	stream->tail = buffer;
	(void)st_mutex_unlock(&stream->mutex);
	mixer = stream->mixer;
	if (st_mutex_lock(&mixer->mutex) == ST_THREAD_SUCCESS) {
		(void)st_condition_signal(&mixer->condition);
		(void)st_mutex_unlock(&mixer->mutex);
	}
	return 0;
}

int
audio_mixer_stream_append(audio_mixer_stream_t *stream,
    const int16_t *frames, size_t frame_count,
    const struct audio_mixer_buffer_options *options)
{
	int16_t *copy;
	int status;

	if (frames == NULL || frame_count == 0)
		return IO_ERROR_INVALID_ARGUMENT;
	if (frame_count > SIZE_MAX / AUDIO_MIXER_FRAME_SIZE)
		return IO_ERROR_OUT_OF_RANGE;
	copy = malloc(frame_count * AUDIO_MIXER_FRAME_SIZE);
	if (copy == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	memcpy(copy, frames, frame_count * AUDIO_MIXER_FRAME_SIZE);
	status = audio_mixer_stream_append_take(stream, copy, frame_count,
	    options);
	if (status != 0)
		free(copy);
	return status;
}

int
audio_mixer_stream_clear(audio_mixer_stream_t *stream)
{
	if (stream == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (st_mutex_lock(&stream->mutex) != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	++stream->clear_generation;
	drop_buffers_locked(stream);
	(void)st_condition_broadcast(&stream->condition);
	(void)st_mutex_unlock(&stream->mutex);
	return 0;
}

static bool
pending_through(const audio_mixer_stream_t *stream, uint64_t target)
{
	const struct audio_mixer_buffer *buffer;

	for (buffer = stream->head; buffer != NULL; buffer = buffer->next) {
		if (buffer->sequence <= target)
			return true;
	}
	for (buffer = stream->retired_head; buffer != NULL;
	    buffer = buffer->next) {
		if (buffer->sequence <= target)
			return true;
	}
	return false;
}

int
audio_mixer_stream_drain(audio_mixer_stream_t *stream)
{
	uint64_t target;
	uint64_t clear_generation;
	struct audio_mixer_buffer *buffer;
	int status = 0;

	if (stream == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (st_mutex_lock(&stream->mutex) != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (stream->status != 0) {
		status = stream->status;
		goto done;
	}
	target = stream->next_sequence - 1;
	clear_generation = stream->clear_generation;
	for (buffer = stream->head; buffer != NULL; buffer = buffer->next) {
		if (buffer->sequence <= target && buffer->loop
		    && buffer->overlay_total == 0) {
			status = IO_ERROR_BUSY;
			goto done;
		}
	}
	while (pending_through(stream, target)) {
		int wait_status;

		wait_status = st_condition_wait(&stream->condition,
		    &stream->mutex);
		if (wait_status != ST_THREAD_SUCCESS) {
			status = IO_ERROR_IO;
			break;
		}
		if (stream->status != 0) {
			status = stream->status;
			break;
		}
		if (stream->clear_generation != clear_generation) {
			status = IO_ERROR_CANCELLED;
			break;
		}
	}
done:
	(void)st_mutex_unlock(&stream->mutex);
	return status;
}

bool
audio_mixer_stream_is_idle(audio_mixer_stream_t *stream)
{
	bool idle;

	if (stream == NULL)
		return true;
	if (st_mutex_lock(&stream->mutex) != ST_THREAD_SUCCESS)
		return true;
	idle = stream->head == NULL || stream->status != 0;
	(void)st_mutex_unlock(&stream->mutex);
	return idle;
}

int
audio_mixer_stream_set_volume(audio_mixer_stream_t *stream,
    float volume_left_db, float volume_right_db)
{
	if (stream == NULL || !valid_volume(volume_left_db)
	    || !valid_volume(volume_right_db))
		return IO_ERROR_INVALID_ARGUMENT;
	if (st_mutex_lock(&stream->mutex) != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	stream->volume_left_db = volume_left_db;
	stream->volume_right_db = volume_right_db;
	stream->ramp_total = 0;
	(void)st_mutex_unlock(&stream->mutex);
	return 0;
}

int
audio_mixer_stream_ramp_volume(audio_mixer_stream_t *stream,
    float volume_left_db, float volume_right_db, size_t frame_count)
{
	if (stream == NULL || !valid_volume(volume_left_db)
	    || !valid_volume(volume_right_db))
		return IO_ERROR_INVALID_ARGUMENT;
	if (frame_count == 0)
		return audio_mixer_stream_set_volume(stream, volume_left_db,
		    volume_right_db);
	if (st_mutex_lock(&stream->mutex) != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	stream->ramp_start_left = powf(10.0f,
	    stream->volume_left_db / 20.0f);
	stream->ramp_start_right = powf(10.0f,
	    stream->volume_right_db / 20.0f);
	stream->ramp_target_left = powf(10.0f, volume_left_db / 20.0f);
	stream->ramp_target_right = powf(10.0f, volume_right_db / 20.0f);
	if (!isfinite(stream->ramp_start_left)
	    || !isfinite(stream->ramp_start_right)
	    || !isfinite(stream->ramp_target_left)
	    || !isfinite(stream->ramp_target_right)) {
		(void)st_mutex_unlock(&stream->mutex);
		return IO_ERROR_OUT_OF_RANGE;
	}
	stream->ramp_target_left_db = volume_left_db;
	stream->ramp_target_right_db = volume_right_db;
	stream->ramp_elapsed = 0;
	stream->ramp_total = frame_count;
	(void)st_mutex_unlock(&stream->mutex);
	return 0;
}

int
audio_mixer_stream_get_volume(audio_mixer_stream_t *stream,
    float *volume_left_db, float *volume_right_db)
{
	if (stream == NULL || (volume_left_db == NULL && volume_right_db == NULL))
		return IO_ERROR_INVALID_ARGUMENT;
	if (st_mutex_lock(&stream->mutex) != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (volume_left_db != NULL)
		*volume_left_db = stream->volume_left_db;
	if (volume_right_db != NULL)
		*volume_right_db = stream->volume_right_db;
	(void)st_mutex_unlock(&stream->mutex);
	return 0;
}

static int16_t
fade_gain(const audio_mixer_t *mixer, size_t position, size_t total)
{
	double normalized;
	double table_position;
	int index;
	double fraction;

	if (total == 0 || position >= total)
		return 32767;
	normalized = (double)position / (double)total;
	table_position = normalized * (AUDIO_MIXER_FADE_TABLE_SIZE - 1);
	index = (int)table_position;
	if (index >= AUDIO_MIXER_FADE_TABLE_SIZE - 1)
		return mixer->fade_table[AUDIO_MIXER_FADE_TABLE_SIZE - 1];
	fraction = table_position - index;
	return (int16_t)(mixer->fade_table[index] * (1.0 - fraction)
	    + mixer->fade_table[index + 1] * fraction);
}

static bool
mix_buffer(audio_mixer_t *mixer, struct audio_mixer_buffer *buffer,
	int64_t *accumulator, size_t frames, float left_db, float right_db,
    size_t *written)
{
	float left_gain = powf(10.0f,
	    (left_db + buffer->volume_left_db) / 20.0f);
	float right_gain = powf(10.0f,
	    (right_db + buffer->volume_right_db) / 20.0f);
	size_t i;
	bool drop = false;

	for (i = 0; i < frames; i++) {
		int32_t gain = 32767;
		int32_t left;
		int32_t right;

		if (buffer->overlay_total > 0
		    && buffer->overlay_elapsed >= buffer->overlay_total) {
			drop = true;
			break;
		}
		if (buffer->consumed >= buffer->frame_count) {
			if (buffer->loop) {
				buffer->consumed = 0;
				++buffer->loop_iteration;
			}
			else {
				drop = true;
				break;
			}
		}
		if (buffer->loop_iteration == 0 && buffer->fade_in > 0
		    && buffer->consumed < buffer->fade_in)
			gain = (gain * fade_gain(mixer, buffer->consumed,
			    buffer->fade_in)) >> 15;
		if (!buffer->loop && buffer->fade_out > 0
		    && buffer->consumed + buffer->fade_out
		    >= buffer->frame_count)
			gain = (gain * fade_gain(mixer,
			    buffer->frame_count - 1 - buffer->consumed,
			    buffer->fade_out)) >> 15;
		if (buffer->overlay_total > 0)
			gain = (gain * fade_gain(mixer,
			    buffer->overlay_total - 1 - buffer->overlay_elapsed,
			    buffer->overlay_total)) >> 15;
		left = ((int32_t)buffer->frames[buffer->consumed * 2] * gain)
		    >> 15;
		right = ((int32_t)buffer->frames[buffer->consumed * 2 + 1]
		    * gain) >> 15;
		accumulator[i * 2] += (int64_t)((float)left * left_gain);
		accumulator[i * 2 + 1] += (int64_t)((float)right * right_gain);
		++buffer->consumed;
		if (buffer->overlay_total > 0)
			++buffer->overlay_elapsed;
	}
	*written = i;
	if (!drop && buffer->overlay_total > 0
	    && buffer->overlay_elapsed >= buffer->overlay_total)
		drop = true;
	if (!drop && buffer->consumed >= buffer->frame_count && !buffer->loop)
		drop = true;
	return drop;
}

static void
advance_ramp(audio_mixer_t *mixer, audio_mixer_stream_t *stream,
    size_t frames)
{
	size_t remaining;
	size_t step;
	float rising;
	float falling;
	float left;
	float right;

	if (stream->ramp_total == 0)
		return;
	remaining = stream->ramp_total - stream->ramp_elapsed;
	step = frames < remaining ? frames : remaining;
	stream->ramp_elapsed += step;
	if (stream->ramp_elapsed >= stream->ramp_total) {
		stream->volume_left_db = stream->ramp_target_left_db;
		stream->volume_right_db = stream->ramp_target_right_db;
		stream->ramp_total = 0;
		return;
	}
	rising = (float)fade_gain(mixer, stream->ramp_elapsed,
	    stream->ramp_total) / 32767.0f;
	falling = (float)fade_gain(mixer,
	    stream->ramp_total - 1 - stream->ramp_elapsed,
	    stream->ramp_total) / 32767.0f;
	if (stream->ramp_target_left >= stream->ramp_start_left)
		left = stream->ramp_start_left
		    + (stream->ramp_target_left - stream->ramp_start_left) * rising;
	else
		left = stream->ramp_target_left
		    + (stream->ramp_start_left - stream->ramp_target_left) * falling;
	if (stream->ramp_target_right >= stream->ramp_start_right)
		right = stream->ramp_start_right
		    + (stream->ramp_target_right - stream->ramp_start_right) * rising;
	else
		right = stream->ramp_target_right
		    + (stream->ramp_start_right - stream->ramp_target_right) * falling;
	stream->volume_left_db = left > 1e-6f ? 20.0f * log10f(left) : -60.0f;
	stream->volume_right_db = right > 1e-6f ? 20.0f * log10f(right) : -60.0f;
}

static int16_t
soft_clip(int64_t value)
{
	double normalized = (double)value / 32767.0;
	int32_t result = (int32_t)(tanh(normalized) * 32767.0);

	if (result > 32767)
		result = 32767;
	else if (result < -32768)
		result = -32768;
	return (int16_t)result;
}

static size_t
pull_chunk_locked(audio_mixer_t *mixer, int16_t *output, size_t frames,
    uint64_t pull_ticket)
{
	audio_mixer_stream_t *stream;
	size_t maximum = 0;
	size_t i;

	memset(mixer->accumulator, 0, frames * AUDIO_MIXER_CHANNELS
	    * sizeof(*mixer->accumulator));
	for (stream = mixer->streams; stream != NULL; stream = stream->next) {
		struct audio_mixer_buffer *head;
		size_t offset = 0;
		bool dropped = false;
		float left_db;
		float right_db;

		(void)st_mutex_lock(&stream->mutex);
		advance_ramp(mixer, stream, frames);
		if (stream->status != 0 || stream->head == NULL) {
			(void)st_mutex_unlock(&stream->mutex);
			continue;
		}
		left_db = stream->volume_left_db;
		right_db = stream->volume_right_db;
		while ((head = stream->head) != NULL && offset < frames) {
			struct audio_mixer_buffer *next = head->next;
			size_t written = 0;
			bool drop;

			if (head->overlay_total > 0 && next != NULL) {
				struct audio_mixer_buffer *after_next = next->next;
				bool drop_next;

				drop = mix_buffer(mixer, head,
				    mixer->accumulator + offset * 2, frames - offset,
				    left_db, right_db, &written);
				if (offset + written > maximum)
					maximum = offset + written;
				drop_next = mix_buffer(mixer, next,
				    mixer->accumulator + offset * 2, frames - offset,
				    left_db, right_db, &written);
				if (offset + written > maximum)
					maximum = offset + written;
				if (drop_next) {
					head->next = after_next;
					if (stream->tail == next)
						stream->tail = head;
					retire_buffer_locked(stream, next, pull_ticket);
					dropped = true;
				}
				if (drop) {
					stream->head = head->next;
					if (stream->tail == head)
						stream->tail = NULL;
					retire_buffer_locked(stream, head, pull_ticket);
					dropped = true;
				}
				break;
			}
			drop = mix_buffer(mixer, head,
			    mixer->accumulator + offset * 2, frames - offset,
			    left_db, right_db, &written);
			offset += written;
			if (offset > maximum)
				maximum = offset;
			if (!drop)
				break;
			stream->head = head->next;
			if (stream->tail == head)
				stream->tail = NULL;
			retire_buffer_locked(stream, head, pull_ticket);
			dropped = true;
		}
		if (dropped || stream->head == NULL)
			(void)st_condition_broadcast(&stream->condition);
		(void)st_mutex_unlock(&stream->mutex);
	}
	for (i = 0; i < frames * AUDIO_MIXER_CHANNELS; i++)
		output[i] = soft_clip(mixer->accumulator[i]);
	return maximum;
}

size_t
audio_mixer_pull_pending(audio_mixer_t *mixer, int16_t *output,
    size_t frame_count, uint64_t *ticket)
{
	size_t offset = 0;
	size_t populated = 0;
	uint64_t pull_ticket;

	if (mixer == NULL || output == NULL || ticket == NULL)
		return 0;
	if (frame_count > SIZE_MAX / AUDIO_MIXER_FRAME_SIZE)
		return 0;
	if (st_mutex_lock(&mixer->mutex) != ST_THREAD_SUCCESS) {
		memset(output, 0, frame_count * AUDIO_MIXER_FRAME_SIZE);
		return 0;
	}
	pull_ticket = mixer->next_pull_ticket++;
	if (mixer->next_pull_ticket == 0)
		mixer->next_pull_ticket = 1;
	while (offset < frame_count) {
		size_t count = frame_count - offset;
		size_t real;

		if (count > AUDIO_MIXER_CHUNK_FRAMES)
			count = AUDIO_MIXER_CHUNK_FRAMES;
		real = pull_chunk_locked(mixer, output + offset * 2, count,
		    pull_ticket);
		if (real > 0)
			populated = offset + real;
		offset += count;
	}
	(void)st_mutex_unlock(&mixer->mutex);
	*ticket = pull_ticket;
	return populated;
}

void
audio_mixer_acknowledge(audio_mixer_t *mixer, uint64_t ticket)
{
	audio_mixer_stream_t *stream;

	if (mixer == NULL || ticket == 0)
		return;
	if (st_mutex_lock(&mixer->mutex) != ST_THREAD_SUCCESS)
		return;
	for (stream = mixer->streams; stream != NULL; stream = stream->next) {
		struct audio_mixer_buffer **link;
		struct audio_mixer_buffer *previous = NULL;
		bool removed = false;

		(void)st_mutex_lock(&stream->mutex);
		link = &stream->retired_head;
		while (*link != NULL) {
			struct audio_mixer_buffer *buffer = *link;

			if (buffer->pull_ticket != ticket) {
				previous = buffer;
				link = &buffer->next;
				continue;
			}
			*link = buffer->next;
			if (stream->retired_tail == buffer)
				stream->retired_tail = previous;
			free_buffer(buffer);
			removed = true;
		}
		if (removed)
			(void)st_condition_broadcast(&stream->condition);
		(void)st_mutex_unlock(&stream->mutex);
	}
	(void)st_mutex_unlock(&mixer->mutex);
}

size_t
audio_mixer_pull(audio_mixer_t *mixer, int16_t *output, size_t frame_count)
{
	uint64_t ticket = 0;
	size_t populated;

	populated = audio_mixer_pull_pending(mixer, output, frame_count,
	    &ticket);
	audio_mixer_acknowledge(mixer, ticket);
	return populated;
}

static bool
has_data_locked(audio_mixer_t *mixer)
{
	audio_mixer_stream_t *stream;
	bool has_data = false;

	for (stream = mixer->streams; stream != NULL; stream = stream->next) {
		(void)st_mutex_lock(&stream->mutex);
		if (stream->head != NULL && stream->status == 0)
			has_data = true;
		(void)st_mutex_unlock(&stream->mutex);
		if (has_data)
			break;
	}
	return has_data;
}

int
audio_mixer_wait_for_data(audio_mixer_t *mixer)
{
	int status = 0;

	if (mixer == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (st_mutex_lock(&mixer->mutex) != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	while (mixer->status == 0 && !has_data_locked(mixer)) {
		if (st_condition_wait(&mixer->condition, &mixer->mutex)
		    != ST_THREAD_SUCCESS) {
			status = IO_ERROR_IO;
			break;
		}
	}
	if (status == 0)
		status = mixer->status;
	(void)st_mutex_unlock(&mixer->mutex);
	return status;
}

void
audio_mixer_stop(audio_mixer_t *mixer, int status)
{
	audio_mixer_stream_t *stream;

	if (mixer == NULL)
		return;
	if (status == 0)
		status = IO_ERROR_CANCELLED;
	if (st_mutex_lock(&mixer->mutex) != ST_THREAD_SUCCESS)
		return;
	mixer->status = status;
	for (stream = mixer->streams; stream != NULL; stream = stream->next) {
		(void)st_mutex_lock(&stream->mutex);
		stream->status = status;
		drop_buffers_locked(stream);
		(void)st_condition_broadcast(&stream->condition);
		(void)st_mutex_unlock(&stream->mutex);
	}
	(void)st_condition_broadcast(&mixer->condition);
	(void)st_mutex_unlock(&mixer->mutex);
}
