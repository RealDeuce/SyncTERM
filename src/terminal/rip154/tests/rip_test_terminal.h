/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef RIP_TEST_TERMINAL_H
#define RIP_TEST_TERMINAL_H
#include "cterm.h"
#include "st_rip154_internal.h"

/* The shared emulation fixtures exercise plain terminals and RIP devices. */
static inline struct st_rip154 *
rip_test_device(st_cterm_t *t)
{
	return t != NULL
	               && (t->graphics_mode == ST_CTERM_GRAPHICS_RIP_1
	                   || (t->graphics_mode == ST_CTERM_GRAPHICS_RIP_3 && !t->rip3_session_owner))
		   ? t->device.context
		   : NULL;
}

static inline int
rip_test_open(st_cterm_t **t, const struct st_cterm_config *cfg, const struct st_rip154_services *services)
{
	if (cfg->graphics_mode == ST_CTERM_GRAPHICS_RIP_1
	    || (cfg->graphics_mode == ST_CTERM_GRAPHICS_RIP_3 && !cfg->rip3_session_owner)) {
		struct st_rip154 *rip;
		return st_rip154_open(&rip, t, cfg, services);
	}
	return st_cterm_create(t, cfg);
}

static inline int
rip_test_create(st_cterm_t **t, const struct st_cterm_config *cfg)
{
	return rip_test_open(t, cfg, NULL);
}

static inline int
rip_test_write(st_cterm_t *t, const void *data, size_t size, size_t *used)
{
	struct st_rip154 *rip = rip_test_device(t);
	return rip != NULL ? st_rip154_feed(rip, data, size, used) : st_cterm_write(t, data, size, used);
}

static inline int
rip_test_finish(st_cterm_t *t)
{
	struct st_rip154 *rip    = rip_test_device(t);
	int               status = rip != NULL ? st_rip154_finish(rip) : 0;
	return status == 0 ? st_cterm_finish(t) : status;
}

static inline int
rip_test_reset(st_cterm_t *t)
{
	struct st_rip154 *rip = rip_test_device(t);
	return rip != NULL ? st_rip154_reset(rip) : st_cterm_reset_terminal(t);
}

static inline void
rip_test_destroy(st_cterm_t **t)
{
	if (t != NULL && *t != NULL) {
		(void)rip_test_finish(*t);
		st_rip154_destroy(rip_test_device(*t));
	}
	st_cterm_destroy(t);
}
#endif
