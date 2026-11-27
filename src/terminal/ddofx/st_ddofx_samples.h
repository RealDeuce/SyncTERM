/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_DDOFX_SAMPLES_H_
#define ST_DDOFX_SAMPLES_H_

#include "st_ddofx.h"

uint32_t    st_ddofx_sample_wait_ms(const struct st_ddofx *);
void        st_ddofx_unload_term_theme(struct st_ddofx *);
void        st_ddofx_term_sound(struct st_ddofx *, const struct st_ddofx_sink *, unsigned number);
void st_ddofx_animation_sample(struct st_ddofx *, const struct st_ddofx_sink *, bool motion, int sample, int rate);
void        st_ddofx_samples_poll(struct st_ddofx *);
const char *st_ddofx_theme_name(const struct st_ddofx *);
bool        st_ddofx_samples_command(struct st_ddofx *, const struct st_ddofx_sink *, char *);
void        st_ddofx_clear_sample_profiles(struct st_ddofx *);
void        st_ddofx_play_realplay(struct st_ddofx *, const struct st_ddofx_sink *, const char *name,
    unsigned rate);
void        st_ddofx_samples_destroy(struct st_ddofx *);

#endif
