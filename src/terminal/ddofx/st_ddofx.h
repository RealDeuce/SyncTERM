/* DDO F/X session wire interpreter. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_DDOFX_H_
#define ST_DDOFX_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "st_tracker.h"

#define ST_DDOFX_BODY_MAX 250

struct st_ddofx {
	enum {
		ST_DDO_TEXT,
		ST_DDO_PREFIX,
		ST_DDO_BODY,
		ST_DDO_DISCARD
	} phase;

	uint8_t            body[ST_DDOFX_BODY_MAX + 1];
	char               music_name[ST_DDOFX_BODY_MAX + 1];
	struct st_tracker *player;
	unsigned           volume;
	size_t             length;
	bool               ignoring;
};

struct st_ddofx_sink {
	void               *context;
	st_audio_runtime_t *audio;
	FILE               *(*open_music)(void *, const char *);
	int                 (*text)(void *, const uint8_t *, size_t, size_t *);
	void                (*reply)(void *, const uint8_t *, size_t);
};

void st_ddofx_reset(struct st_ddofx *);
int  st_ddofx_feed(struct st_ddofx *, const struct st_ddofx_sink *, const uint8_t *, size_t, size_t *);

#endif
