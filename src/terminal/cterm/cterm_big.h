/* BIG pre-ANSI owner; protocol state stays outside the base ANSI parser. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef CTERM_BIG_H_
#define CTERM_BIG_H_
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
struct cterminal;
enum { CTERM_BIG_PALETTE_FIRST = 800 };
struct cterm_big;
struct cterm_key;
struct st_console_event;
bool cterm_big_pointer_active(const struct cterm_big *);
bool cterm_big_mouse(struct cterm_big *, const struct st_console_event *);
void cterm_big_mouse_observe(struct cterm_big *, const struct st_console_event *);
void cterm_big_mouse_sync(struct cterm_big *, uint32_t buttons);
bool cterm_big_handle_key(struct cterm_big *, const struct cterm_key *);
struct st_console_test_application;
void cterm_big_inspect(const struct cterm_big *, struct st_console_test_application *);
int cterm_big_create(struct cterminal *);
void cterm_big_destroy(struct cterm_big *);
void cterm_big_reset(struct cterm_big *);
int cterm_big_feed(struct cterm_big *, const void *, size_t, size_t *);
int cterm_big_poll(struct cterm_big *);
int cterm_big_finish(struct cterm_big *);
bool cterm_big_paused(const struct cterm_big *);
#endif
