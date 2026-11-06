/* Retained scrollback rows and detached connection history. */

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

/* Capture a row only when its cells contain pixel graphics. Pixel colours
 * are frozen as RGB in both phases; equal phases share the primary plane.
 * A text-only row returns NULL. All outputs are unchanged on failure. */
int st_history_capture_pixels(struct st_pixel_buffer **result,
	st_console_t *console, const struct st_console_cell *cells, uint32_t columns,
	const struct st_console_rect *pixels);
void st_history_free_pixels(struct st_pixel_buffer **pixels, size_t lines);
/* Regrid a row by nearest-neighbour sampling within its original columns;
 * added columns are black. Source is borrowed; result is owned. */
int st_history_resize_pixels(struct st_pixel_buffer **result,
	const struct st_pixel_buffer *source, uint32_t source_columns,
	uint32_t columns, uint32_t cell_width, uint32_t cell_height);

int st_session_history_create(st_session_history_t **result,
	size_t line_capacity);
void st_session_history_destroy(st_session_history_t *history);
int st_session_history_set_capacity(st_session_history_t *history,
	size_t line_capacity);
size_t st_session_history_capacity(st_session_history_t *history);

/* Copies the newest cells when count exceeds capacity. On success, takes
 * the optional owned pixels array and its per-line buffers (including any
 * discarded prefix). On failure all inputs remain owned by the caller. */
int st_session_history_publish(st_session_history_t *history,
	const struct st_console_cell *cells, uint32_t width, size_t lines,
	enum st_screen_mode mode, bool has_status, struct st_pixel_buffer **pixels);
void st_session_history_clear(st_session_history_t *history);

/* The caller owns *cells and frees it with free(). */
int st_session_history_copy(st_session_history_t *history,
	struct st_console_cell **cells, uint32_t *width, size_t *lines,
	enum st_screen_mode *mode, bool *has_status);
int st_session_history_info(st_session_history_t *history, uint32_t *width,
	size_t *lines, enum st_screen_mode *mode, bool *has_status);
/* Clones only the requested row's optional pixels. Output unchanged on error. */
int st_session_history_copy_pixels(st_session_history_t *history, size_t row,
	struct st_pixel_buffer **result);

#endif
