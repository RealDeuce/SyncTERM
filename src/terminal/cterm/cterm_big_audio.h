/* Private BIG sound ownership at the existing mixer/channel boundary. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef CTERM_BIG_AUDIO_H_
#define CTERM_BIG_AUDIO_H_
#include "st_big_speaker.h"
struct st_big_effect_player;
struct cterm_big;
struct cterm_big_audio;
struct st_console_test_application;
bool cterm_big_audio_needs_poll(const struct cterm_big *);
int  cterm_big_audio_poll(struct cterm_big *);
int  cterm_big_audio_frequency(struct cterm_big *, uint16_t);
int  cterm_big_audio_stop(struct cterm_big *);
int  cterm_big_audio_effect(struct cterm_big *, uint16_t selector);
int  cterm_big_audio_effect_done(struct cterm_big *, bool *done);
void cterm_big_audio_cancel_effect(struct cterm_big *);
int  cterm_big_effect_snapshot(const struct cterm_big *, struct st_big_effect_player *);
void cterm_big_audio_reset(struct cterm_big *);
void cterm_big_audio_destroy(struct cterm_big *);
int  cterm_big_speaker_snapshot(const struct cterm_big *, struct st_big_speaker *);
void cterm_big_audio_inspect(const struct cterm_big *, struct st_console_test_application *);
#endif
