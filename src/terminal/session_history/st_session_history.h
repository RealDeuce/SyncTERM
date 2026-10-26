/* Detached scrollback retained after a connected session ends. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_SESSION_HISTORY_H_
#define ST_SESSION_HISTORY_H_

#include "st_console.h"
#include "st_screen_mode.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct st_session_history st_session_history_t;

int st_session_history_create(st_session_history_t **result,
	size_t line_capacity);
void st_session_history_destroy(st_session_history_t *history);
int st_session_history_set_capacity(st_session_history_t *history,
	size_t line_capacity);
size_t st_session_history_capacity(st_session_history_t *history);

/* Copies the newest lines when count exceeds the configured capacity. */
int st_session_history_publish(st_session_history_t *history,
	const struct st_console_cell *cells, uint32_t width, size_t lines,
	enum st_screen_mode mode, bool has_status);
void st_session_history_clear(st_session_history_t *history);

/* The caller owns *cells and frees it with free(). */
int st_session_history_copy(st_session_history_t *history,
	struct st_console_cell **cells, uint32_t *width, size_t *lines,
	enum st_screen_mode *mode, bool *has_status);
int st_session_history_info(st_session_history_t *history, uint32_t *width,
	size_t *lines, enum st_screen_mode *mode, bool *has_status);

#endif
