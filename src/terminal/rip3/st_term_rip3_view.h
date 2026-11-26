/* A RIP3 TTY's native text grid at an absolute origin in the display raster. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_TERM_RIP3_VIEW_H_
#define ST_TERM_RIP3_VIEW_H_

#include "st_cterm.h"
#include "st_rip3_text_window.h"

struct st_term_rip3_view;
/* Construct completely before publishing. No parent pixels are changed. */
int         st_term_rip3_view_create(struct st_term_rip3_view **result, const struct st_cterm_config *text_config,
    const struct st_rip3_text_window_layout *layout, uint32_t device_width, uint32_t device_height);
void        st_term_rip3_view_destroy(struct st_term_rip3_view *view);
st_cterm_t *st_term_rip3_view_text(struct st_term_rip3_view *view);
st_console_t *st_term_rip3_view_console(struct st_term_rip3_view *view);
/* Borrowed immutable geometry, valid until the view is destroyed. */
const struct st_rip3_text_window_layout *st_term_rip3_view_layout(const struct st_term_rip3_view *view);
/* Adopt actual underlying artwork before an ANSI batch (including scrolling),
 * then publish damage in the shared raster. Factory/render statuses stay below the
 * owning parser's nonfatal command boundary. No physical display is opened. */
int st_term_rip3_view_begin(struct st_term_rip3_view *view, st_console_t *parent);
int st_term_rip3_view_present(struct st_term_rip3_view *view, st_console_t *parent, bool active);
int st_term_rip3_view_active(struct st_term_rip3_view *view, st_console_t *parent, bool active);

#endif
