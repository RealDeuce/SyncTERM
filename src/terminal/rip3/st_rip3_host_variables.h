/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_HOST_VARIABLES_H_
#define ST_RIP3_HOST_VARIABLES_H_

#include "st_rip3_builtin_reference.h"

#include <stddef.h>

struct st_rip3_features;
struct st_rip3_frame;
struct st_rip3_table_host;

/* Borrowed current-session services; one session thread owns invocation and
 * state. A frame must have been initialized by frame_init. Missing services
 * are explicitly unsupported, never assumed to exist. No lifetime ownership
 * or terminal/application integration is implied by assembling these bindings. */
struct st_rip3_host_variables {
	const struct st_rip3_features *features;
	const struct st_rip3_frame *frame;
	const struct st_rip3_table_host *tables;
};

/* Execute one already expanded, ordinary built-in reference produced by
 * builtin_reference_parse. Supports NULL, IFS, RESX/RESY and the table host
 * variables. Recognized but unsupported/unimplemented names return
 * NOT_SUPPORTED. An ordinary name absent from the pinned built-in catalog
 * returns NOT_FOUND so the caller can perform user lookup separately. The
 * caller still owns framing, expansion, user policy, deadlines and delivery.
 * No version/full-compliance capability is inferred or advertised here.
 * External application/URL launch names always return unsupported.
 * Successful output is NUL terminated; short buffers and other failures
 * preserve the caller buffer and bound state. */
int st_rip3_host_variables_execute(const struct st_rip3_host_variables *host,
	const struct st_rip3_builtin_reference *reference, char *result, size_t capacity);

#endif
