/* Private BIG sound ownership at the existing mixer/channel boundary. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef CTERM_BIG_AUDIO_H_
#define CTERM_BIG_AUDIO_H_
#include "st_big_speaker.h"
struct cterm_big;
struct cterm_big_audio;
struct st_console_test_application;
int cterm_big_audio_poll(struct cterm_big *);
int cterm_big_audio_frequency(struct cterm_big *, uint16_t);
int cterm_big_audio_stop(struct cterm_big *);
void cterm_big_audio_reset(struct cterm_big *);
void cterm_big_audio_destroy(struct cterm_big *);
int cterm_big_speaker_snapshot(const struct cterm_big *, struct st_big_speaker *);
void cterm_big_audio_inspect(const struct cterm_big *, struct st_console_test_application *);
#endif
