/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_input.h"
#include "io_error.h"

#include <stdint.h>
#include <stdlib.h>

struct st_rip3_input {
	struct st_rip3_input_source source;
	uint64_t started, duration;
};

int
st_rip3_input_create(struct st_rip3_input **result,
    const struct st_rip3_input_source *source)
{
	struct st_rip3_input *input;
	if (result == NULL || source == NULL || source->now_ns == NULL
	    || source->next == NULL || source->consume == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	input = calloc(1,sizeof(*input));
	if (input == NULL) return IO_ERROR_OUT_OF_MEMORY;
	input->source = *source;
	*result = input;
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
	if (input != NULL) input->duration = 0;
}

void
st_rip3_input_delay(struct st_rip3_input *input, uint32_t ticks)
{
	if (input == NULL) return;
	/* UINT32_MAX * 1e9 + 59 fits uint64_t. All decoded four-digit fields
	 * fit uint32_t, while the typed service supports its complete range. */
	input->duration = ((uint64_t)ticks*UINT64_C(1000000000)+59)/60;
	if (input->duration != 0) input->started = input->source.now_ns(input->source.context);
}

uint64_t
st_rip3_input_wait_ns(const struct st_rip3_input *input)
{
	uint64_t elapsed;
	if (input == NULL || input->duration == 0) return 0;
	/* Unsigned elapsed arithmetic retains a gate across counter rollover;
	 * it never constructs an overflowing absolute deadline. */
	elapsed = input->source.now_ns(input->source.context)-input->started;
	return elapsed >= input->duration ? 0 : input->duration-elapsed;
}

void
st_rip3_input_process(struct st_rip3_input *input, size_t budget)
{
	if (input == NULL) return;
	for (size_t i = 0; i < budget; i++) {
		uint8_t byte;
		if (st_rip3_input_wait_ns(input) != 0) return;
		input->duration = 0;
		if (input->source.prepare != NULL) {
			if (!input->source.prepare(input->source.context)) return;
			if (st_rip3_input_wait_ns(input) != 0) return;
		}
		if (!input->source.next(input->source.context,&byte)) return;
		input->source.consume(input->source.context,byte);
	}
}
