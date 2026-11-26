/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_TERM_RIP3_PICK_LIST_H_
#define ST_TERM_RIP3_PICK_LIST_H_

#include <stddef.h>
#include <stdint.h>

#include "st_console.h"

struct st_rip3_frame;
struct st_rip3_pick_list;
struct st_term_rip3_pick_list;

int  st_term_rip3_pick_list_open(struct st_term_rip3_pick_list **result, st_console_t *console,
    const struct st_rip3_pick_list *list, const struct st_rip3_frame *frame);
int  st_term_rip3_pick_list_close(struct st_term_rip3_pick_list *dialog);
int  st_term_rip3_pick_list_key(struct st_term_rip3_pick_list *dialog, const struct st_console_event *event,
    uint32_t codepage_scalar, size_t *choice);
int  st_term_rip3_pick_list_mouse(struct st_term_rip3_pick_list *dialog, const struct st_console_event *event,
    size_t *choice);
void st_term_rip3_pick_list_disarm(struct st_term_rip3_pick_list *dialog);

#endif
