/* Musicomm raw connection receiver. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_MUSICOMM_H
#define ST_MUSICOMM_H
#include "st_music.h"

enum st_musicomm_state {
	ST_MUSICOMM_OFF,
	ST_MUSICOMM_WAITING,
	ST_MUSICOMM_START_DRAIN,
	ST_MUSICOMM_PLAYING,
	ST_MUSICOMM_END_DRAIN,
};

struct st_musicomm;

struct st_musicomm_config {
	st_audio_runtime_t *audio;
	st_music_t         *player;
	void                (*reply)(void *context, const uint8_t *bytes, size_t size);
	void                (*warning)(void *context, const char *message);
	void               *context;
};

/* The session owns the receiver and supplies the shared channel-0 player. */
int                    st_musicomm_start(struct st_musicomm **receiver, const struct st_musicomm_config *config);
enum st_musicomm_state st_musicomm_state(const struct st_musicomm *receiver);
void                   st_musicomm_feed(struct st_musicomm *receiver, const void *bytes, size_t size);
void                   st_musicomm_poll(struct st_musicomm *receiver);
/* Only the transport can establish that the entire receive queue is empty. */
void st_musicomm_idle(struct st_musicomm *receiver);
bool st_musicomm_stop(struct st_musicomm *receiver);
void st_musicomm_cancel(struct st_musicomm *receiver);
void st_musicomm_destroy(struct st_musicomm **receiver);
#endif
