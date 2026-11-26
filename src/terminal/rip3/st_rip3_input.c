/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdint.h>
#include <stdlib.h>

#include "io_error.h"
#include "st_rip3_input.h"

struct st_rip3_input {
	struct st_rip3_input_source source;
	uint64_t                    started, duration;
	bool                        (*blocked)(void *context);
	void                       *blocked_context;
};

int
st_rip3_input_create(struct st_rip3_input **result, const struct st_rip3_input_source *source)
{
	struct st_rip3_input *input;
	if (result == NULL || source == NULL || source->now_ns == NULL || source->next == NULL
	    || source->consume == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	input = calloc(1, sizeof(*input));
	if (input == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	input->source = *source;
	*result       = input;
	return 0;
}

void
st_rip3_input_destroy(struct st_rip3_input *input)
{
	free(input);
}

void
st_rip3_input_reset(struct st_rip3_input *input)
{
	if (input != NULL)
		input->duration = 0;
}

void
st_rip3_input_delay(struct st_rip3_input *input, uint32_t ticks)
{
	if (input == NULL)
		return;
	input->blocked = NULL;
	/* Both RIP_DELAY and $D use whole milliseconds, truncated after scaling. */
	uint64_t milliseconds = (uint64_t)ticks * 1000 / 60;
	input->duration       = milliseconds * UINT64_C(1000000);
	if (input->duration != 0)
		input->started = input->source.now_ns(input->source.context);
}

void
st_rip3_input_wait_for(struct st_rip3_input *input, bool (*blocked)(void *), void *context)
{
	if (input == NULL)
		return;
	input->blocked         = blocked;
	input->blocked_context = context;
	input->duration        = blocked != NULL;
}

uint64_t
st_rip3_input_wait_ns(const struct st_rip3_input *input)
{
	uint64_t elapsed;
	if (input == NULL || input->duration == 0)
		return 0;
	if (input->blocked != NULL)
		return input->blocked(input->blocked_context) ? UINT64_C(1000000) : 0;
	/* Unsigned elapsed arithmetic retains a gate across counter rollover;
	 * it never constructs an overflowing absolute deadline. */
	elapsed = input->source.now_ns(input->source.context) - input->started;
	return elapsed >= input->duration ? 0 : input->duration - elapsed;
}

void
st_rip3_input_process(struct st_rip3_input *input, size_t budget)
{
	if (input == NULL)
		return;
	for (size_t i = 0; i < budget; i++) {
		uint8_t byte;
		if (st_rip3_input_wait_ns(input) != 0)
			return;
		input->duration = 0;
		if (input->source.prepare != NULL) {
			if (!input->source.prepare(input->source.context))
				return;
			if (st_rip3_input_wait_ns(input) != 0)
				return;
		}
		if (!input->source.next(input->source.context, &byte))
			return;
		input->source.consume(input->source.context, byte);
	}
}
