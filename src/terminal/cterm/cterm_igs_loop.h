/* IGS & construction and stored-operand policy; no parser or worker of its own. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef CTERM_IGS_LOOP_H
#define CTERM_IGS_LOOP_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
struct cterm_igs;
struct st_graphics_command_view;
struct st_graphics_parameter;
struct cterm_igs_loop {
	int16_t operands[2048];
	uint8_t gang[128], target, command;
	int current, to, step, delay, original, saved, varying, first, pass;
	size_t count, loaded, index;
	unsigned work;
	uint64_t deadline;
	bool initializing, loading, ganged, no_parameters;
	bool run, next, initial, selecting, bad, did_flow, chained;
	bool waiting, delay_started, cooperative;
};
int cterm_igs_loop_next(void *,const struct st_graphics_command_view *,struct st_graphics_parameter *);
int cterm_igs_loop_field(struct cterm_igs *,const struct st_graphics_command_view *,const struct st_graphics_parameter *);
int cterm_igs_loop_begin(struct cterm_igs *,const struct st_graphics_command_view *);
int cterm_igs_loop_number(void *,const struct st_graphics_command_view *,const struct st_graphics_parameter *,int *,bool *);
int cterm_igs_loop_value(struct cterm_igs *,int *);
int cterm_igs_loop_end(struct cterm_igs *);
int cterm_igs_loop_cancel(struct cterm_igs *);
#endif
