/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_DDOFX_SCRIPT_H_
#define ST_DDOFX_SCRIPT_H_

#include "st_ddofx.h"

struct st_ddofx_callback {
	char macro[101], reply[101];
};

void  st_ddofx_callback_define(const struct st_ddofx *, struct st_ddofx_callback *, const char *, const char *);
void  st_ddofx_callback_start(struct st_ddofx *, const struct st_ddofx_callback *);
char *st_ddofx_field(char **);
bool  st_ddofx_field_number(const struct st_ddofx *, char **, int32_t *);
void  st_ddofx_event_command(struct st_ddofx *, const char *, char *);
void  st_ddofx_event_poll(struct st_ddofx *);

bool st_ddofx_builtin(const struct st_ddofx *, const struct st_ddofx_sink *, const char *,
    char result[ST_DDOFX_BODY_MAX + 1], bool *numeric);
int  st_ddofx_parse(struct st_ddofx *, struct st_ddofx_framer *, const struct st_ddofx_sink *, const uint8_t *,
    size_t, size_t *);
bool st_ddofx_script_active(const struct st_ddofx *);
int  st_ddofx_script_step(struct st_ddofx *, const struct st_ddofx_sink *);
void st_ddofx_script_destroy(struct st_ddofx *);
void st_ddofx_script_command(struct st_ddofx *, const struct st_ddofx_sink *, char *);
bool st_ddofx_number(const struct st_ddofx *, const char *, int32_t *);
const char *st_ddofx_value(const struct st_ddofx *, const char *);
bool        st_ddofx_capture(struct st_ddofx *, const char *, bool numeric);

#endif
