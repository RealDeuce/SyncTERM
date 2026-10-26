/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "audio.h"

#include <stdio.h>

int
main(void)
{
	audio_engine_t *engine = NULL;
	int status;

	status = audio_engine_create(&engine, AUDIO_BACKENDS_DEFAULT);
	if (status != 0) {
		fprintf(stderr, "could not open an audio backend: %d\n", status);
		return 1;
	}
	printf("backend mask: 0x%x\n", (unsigned)audio_engine_backend(engine));
	status = audio_beep(engine, 440.0, 250);
	audio_engine_destroy(engine);
	if (status != 0) {
		fprintf(stderr, "audio beep failed: %d\n", status);
		return 1;
	}
	return 0;
}
