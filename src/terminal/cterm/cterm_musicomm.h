/* Owner-thread Musicomm protocol, with a mixer-owned PSG consumer. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef CTERM_MUSICOMM_H
#define CTERM_MUSICOMM_H
#include "st_cterm.h"
#include "st_igs_gist.h"
#include "st_igs_psg.h"
#include "st_threads.h"
struct cterm_musicomm {
	/* Owner alone changes protocol, routing, toggles and clock anchors.
	 * mutex protects gist/psg/credit against the mixer source. Detach the
	 * source without holding mutex before destroying this context. */
	enum st_cterm_musicomm_state state;
	bool on[128], midi, physical, warned;
	int channel[128];
	st_mutex_t mutex;
	struct st_igs_gist gist;
	struct st_igs_psg psg;
	unsigned credit, fraction;
	uint64_t local_ms;
};
void cterm_musicomm_feed(st_cterm_t *, const uint8_t *, size_t);
void cterm_musicomm_poll(st_cterm_t *);
void cterm_musicomm_cancel(st_cterm_t *);
void cterm_musicomm_destroy(st_cterm_t *);
#endif
