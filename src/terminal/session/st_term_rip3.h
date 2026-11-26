/* Connected RIP3 owner; the borrowed CTerm handles ordinary ANSI text only. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_TERM_RIP3_H_
#define ST_TERM_RIP3_H_

#include "st_term.h"

struct st_term_rip3;
int st_term_rip3_prepare_console(st_console_t *console);
int st_term_rip3_create(struct st_term_rip3 **result, const struct st_term_config *config, st_cterm_t *text,
    const struct st_cterm_config *text_config, st_term_session_t *owner);
st_cterm_t *st_term_rip3_text(const struct st_term_rip3 *rip);
bool        st_term_rip3_text_view(struct st_term_rip3 *rip, struct st_term_text_view *view);
void        st_term_rip3_flush(struct st_term_rip3 *rip);
void        st_term_rip3_set_transfer_protocols(struct st_term_rip3 *rip, uint32_t protocols);
int         st_term_rip3_receive_asset(struct st_term_rip3 *rip, const char *directory, const char *cache_root,
    const char *name, unsigned file_type);
void        st_term_rip3_autosense(struct st_term_rip3 *rip, uint16_t selector);
int         st_term_rip3_begin_local_text(struct st_term_rip3 *rip);
int         st_term_rip3_end_local_text(struct st_term_rip3 *rip);
void        st_term_rip3_destroy(struct st_term_rip3 *rip);
/* All incoming-data, continuation and service failures are nonfatal here. */
void st_term_rip3_write(struct st_term_rip3 *rip, const uint8_t *bytes, size_t length);
void st_term_rip3_poll(struct st_term_rip3 *rip);
void st_term_rip3_cancel(struct st_term_rip3 *rip);
/* The owner appends a modal press tail to its existing deferred-event FIFO. */
void st_term_rip3_bind_prompt_input(struct st_term_rip3 *rip,
    void (*retain)(void *context, uint64_t token, const struct st_console_event *event), void *context);
void st_term_rip3_observe_prompt_input(struct st_term_rip3 *rip, const struct st_console_event *event);
void st_term_rip3_note_queued_input(struct st_term_rip3 *rip, const struct st_console_event *event);
void st_term_rip3_captured_input(struct st_term_rip3 *rip, uint64_t token, const struct st_console_event *event);
bool st_term_rip3_graphical_pending(const struct st_term_rip3 *rip);
void st_term_rip3_graphical_failure(struct st_term_rip3 *rip);
bool st_term_rip3_paused(const struct st_term_rip3 *rip);
bool st_term_rip3_mouse_active(const struct st_term_rip3 *rip);
bool st_term_rip3_pointer_shape(const struct st_term_rip3 *rip, enum st_console_pointer_shape *result);
bool st_term_rip3_mouse(struct st_term_rip3 *rip, const struct st_console_event *event);
bool st_term_rip3_keyboard_locked(const struct st_term_rip3 *rip);
bool st_term_rip3_key(struct st_term_rip3 *rip, const struct st_console_event *event);
void st_term_rip3_mouse_cancel(struct st_term_rip3 *rip, bool keyboard_focus_lost);
void st_term_rip3_mouse_ignore(struct st_term_rip3 *rip, const struct st_console_event *event);
bool st_term_rip3_status_visible(const struct st_term_rip3 *rip);
void st_term_rip3_suspend_status(struct st_term_rip3 *rip, bool suspended);

#endif
