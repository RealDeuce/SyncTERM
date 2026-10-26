/*
 * Copyright Rob Swindell - http://www.synchro.net/copyright.html
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "audio_internal.h"
#include "audio_config.h"

#include "io_error.h"
#include "st_threads.h"

#include <fcntl.h>
#if ST_AUDIO_HAVE_SPEAKER_TONE
#if ST_AUDIO_SPEAKER_HEADER_MACHINE_SPEAKER
#include <machine/speaker.h>
#else
#include <machine/spkr.h>
#endif
#endif
#if ST_AUDIO_HAVE_KIOCSOUND
#if ST_AUDIO_KIOCSOUND_HEADER_LINUX_KD
#include <linux/kd.h>
#else
#include <sys/kbio.h>
#endif
#endif
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static void
sleep_ms(uint32_t milliseconds)
{
	struct timespec duration;

	duration.tv_sec = (time_t)(milliseconds / 1000);
	duration.tv_nsec = (long)(milliseconds % 1000) * 1000000L;
	while (st_thread_sleep(&duration, &duration) != 0)
		;
}

int
audio_platform_beep(double frequency, uint32_t duration_ms)
{
#if ST_AUDIO_HAVE_SPEAKER_TONE
	int descriptor = open("/dev/speaker", O_WRONLY | O_APPEND);

	if (descriptor >= 0) {
		tone_t tone;

		tone.frequency = (int)(frequency + 0.5);
		tone.duration = (int)(duration_ms / 10);
		if (ioctl(descriptor, SPKRTONE, &tone) == 0) {
			(void)close(descriptor);
			return 0;
		}
		(void)close(descriptor);
	}
#endif
#if ST_AUDIO_HAVE_KIOCSOUND
	if (frequency > 0.0) {
		int descriptor = open("/dev/console", O_WRONLY | O_NOCTTY);

		if (descriptor >= 0) {
			int divisor = (int)(1193180.0 / frequency);

			if (ioctl(descriptor, KIOCSOUND, divisor) == 0) {
				sleep_ms(duration_ms);
				(void)ioctl(descriptor, KIOCSOUND, 0);
				(void)close(descriptor);
				return 0;
			}
			(void)close(descriptor);
		}
	}
#else
	(void)frequency;
	(void)duration_ms;
	(void)sleep_ms;
#endif
	return IO_ERROR_NOT_SUPPORTED;
}
