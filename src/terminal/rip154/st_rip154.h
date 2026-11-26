/* RIP graphics device attached to a text terminal. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP154_H
#define ST_RIP154_H
#include "st_cterm.h"
#include "st_rip_host.h"

struct st_rip154;

int  st_rip154_open(struct st_rip154 **, st_cterm_t **, const struct st_cterm_config *,
    const struct st_rip154_services *);
void st_rip154_destroy(struct st_rip154 *);
bool st_rip154_owns_bytes(const struct st_rip154 *);
int  st_rip154_feed(struct st_rip154 *, const void *, size_t, size_t *);
int  st_rip154_finish(struct st_rip154 *);
int  st_rip154_reset(struct st_rip154 *);
int  st_rip154_receive_begin(struct st_rip154 *rip);
int  st_rip154_receive_data(struct st_rip154 *rip, const uint8_t *bytes, size_t size);
int  st_rip154_receive_end(struct st_rip154 *rip, bool success);
bool st_rip154_text_key(struct st_rip154 *, uint32_t);
#endif
