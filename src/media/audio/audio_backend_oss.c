/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "audio_internal.h"
#include "audio_config.h"

#include "io_error.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#if ST_AUDIO_OSS_HEADER_SYS_SOUNDCARD
#include <sys/soundcard.h>
#elif ST_AUDIO_OSS_HEADER_MACHINE_SOUNDCARD
#include <machine/soundcard.h>
#endif
#include <unistd.h>

struct oss_state {
	int descriptor;
};

static int
oss_ioctl(int descriptor, unsigned long request, void *argument)
{
	return ioctl(descriptor, request, argument);
}

static bool
oss_available(void)
{
	return true;
}

static int
oss_open(void **result, const struct audio_backend_callbacks *callbacks)
{
	struct oss_state *state;
	int format = AFMT_S16_NE;
	int channels = AUDIO_CHANNELS;
	int rate = AUDIO_SAMPLE_RATE;

	(void)callbacks;
	state = malloc(sizeof(*state));
	if (state == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	state->descriptor = open("/dev/dsp", O_WRONLY);
	if (state->descriptor < 0
	    || oss_ioctl(state->descriptor, SNDCTL_DSP_SETFMT, &format) < 0
	    || format != AFMT_S16_NE
	    || oss_ioctl(state->descriptor, SNDCTL_DSP_CHANNELS, &channels) < 0
	    || channels != AUDIO_CHANNELS
	    || oss_ioctl(state->descriptor, SNDCTL_DSP_SPEED, &rate) < 0
	    || rate != AUDIO_SAMPLE_RATE) {
		if (state->descriptor >= 0)
			(void)close(state->descriptor);
		free(state);
		return IO_ERROR_IO;
	}
	*result = state;
	return 0;
}

static void
oss_close(void *opaque)
{
	struct oss_state *state = opaque;

	if (state == NULL)
		return;
	(void)close(state->descriptor);
	free(state);
}

static int
oss_write(void *opaque, const int16_t *frames, size_t frame_count)
{
	struct oss_state *state = opaque;
	const uint8_t *bytes = (const uint8_t *)frames;
	size_t remaining = frame_count * AUDIO_FRAME_SIZE;

	while (remaining > 0) {
		ssize_t written = write(state->descriptor, bytes, remaining);

		if (written < 0 && errno == EINTR)
			continue;
		if (written <= 0)
			return IO_ERROR_IO;
		bytes += written;
		remaining -= (size_t)written;
	}
	return 0;
}

static uint32_t
oss_latency_ms(void *opaque)
{
	struct oss_state *state = opaque;
	int bytes = 0;

#ifdef SNDCTL_DSP_GETODELAY
	if (oss_ioctl(state->descriptor, SNDCTL_DSP_GETODELAY, &bytes) == 0
	    && bytes > 0)
		return (uint32_t)((uint64_t)bytes * 1000
		    / (AUDIO_SAMPLE_RATE * AUDIO_FRAME_SIZE));
#else
	(void)state;
#endif
	return 0;
}

const struct audio_backend_descriptor audio_backend_oss = {
	AUDIO_BACKEND_OSS,
	oss_available,
	oss_open,
	oss_close,
	oss_write,
	oss_latency_ms,
};
