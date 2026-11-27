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

struct st_ddofx_framer {
	enum {
		ST_DDO_TEXT,
		ST_DDO_PREFIX,
		ST_DDO_BODY,
		ST_DDO_DISCARD
	} phase;

	char   body[ST_DDOFX_BODY_MAX + 1];
	size_t length;
};

struct st_ddofx {
	struct st_ddofx_framer  wire;
	struct st_ddofx_script *script;
	uint8_t                *queued;
	size_t                  queued_start, queued_end;
	uint64_t                wait_until;
	char                    music_name[ST_DDOFX_BODY_MAX + 1];
	struct st_tracker      *player;
	unsigned                volume;
	bool                    ignoring;
};

struct st_ddofx_sink {
	void               *context;
	st_audio_runtime_t *audio;
	FILE               *(*open_asset)(void *, const char *category, const char *client, const char *name);
	bool                (*client_exists)(void *, const char *name);
	int                 (*text)(void *, const uint8_t *, size_t, size_t *);
	void                (*reply)(void *, const uint8_t *, size_t);
};

bool st_ddofx_busy(const struct st_ddofx *);
int  st_ddofx_poll(struct st_ddofx *, const struct st_ddofx_sink *);
void st_ddofx_reset(struct st_ddofx *);
int  st_ddofx_feed(struct st_ddofx *, const struct st_ddofx_sink *, const uint8_t *, size_t, size_t *);

#endif
