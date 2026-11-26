/* Owner-thread Musicomm protocol, with a mixer-owned PSG consumer. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_MUSICOMM_INTERNAL_H
#define ST_MUSICOMM_INTERNAL_H
#include "st_igs_gist.h"
#include "st_igs_psg.h"
#include "st_musicomm.h"
#include "st_threads.h"

struct st_musicomm {
	struct st_musicomm_config config;
	/* Owner alone changes protocol, routing, toggles and clock anchors.
	 * mutex protects gist/psg/credit against the mixer source. Detach the
	 * source without holding mutex before destroying this context. */
	enum st_musicomm_state       state;
	bool                         on[128], midi, physical, warned;
	int                          channel[128];
	st_mutex_t                   mutex;
	struct st_igs_gist           gist;
	struct st_igs_psg            psg;
	unsigned                     credit, fraction;
	uint64_t                     local_ms;
};

#endif
