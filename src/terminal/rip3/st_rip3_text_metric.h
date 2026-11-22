/* Parser-owned graphics metric continuation; statuses stay inside RIP3. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_TEXT_METRIC_H_
#define ST_RIP3_TEXT_METRIC_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct st_rip3_command_host;
struct st_rip3_host_sink;
struct st_rip3_metric_argument {unsigned mode,domain;bool retained,literal_empty;};
struct st_rip3_metric_state {
	uint8_t bytes[4*1022+1];
	size_t length;
};

/* Exact consumer sentinel after the raw C-string prefix; no trimming. */
bool st_rip3_metric_sentinel(const uint8_t *bytes,size_t length);

/* Decode before providers can change BASEMATH. Reserved bytes are consumed
 * without interpretation. Failure preserves argument and offset. */
int st_rip3_metric_argument_prepare(const struct st_rip3_command_host *host,
	const uint8_t *command,size_t length,struct st_rip3_metric_argument *argument,size_t *offset);
/* Expanded source is owned by the shared worker until this call completes.
 * The exact sentinel bypasses it. Commit a replacement before measuring, so
 * later metric/font failure cannot roll back already completed providers. */
int st_rip3_metric_complete(struct st_rip3_metric_state *state,
	const struct st_rip3_command_host *host,const struct st_rip3_metric_argument *argument,
	const uint8_t *bytes,size_t length,bool converted_codepage,const struct st_rip3_host_sink *sink);

#endif
