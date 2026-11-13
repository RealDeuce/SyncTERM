/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_random.h"
#include "st_big_number.h"
#include "io_error.h"

void
st_big_random_seed(uint32_t *state, uint16_t seed)
{
	if (state != NULL) *state = seed;
}

uint16_t
st_big_random_next(uint32_t *state)
{
	if (state == NULL) return 0;
	*state = *state*UINT32_C(0x015a4e35)+1;
	return (uint16_t)((*state>>16)&0x7fff);
}
int
st_big_random_repair(uint16_t *from, uint16_t *to)
{
	if (from==NULL || to==NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (st_big_number_signed(*from)<0) *from=0;
	if (st_big_number_signed(*to)<st_big_number_signed(*from)) *to=(uint16_t)(*from+1u);
	return 0;
}

int
st_big_random_range(uint32_t *state, uint16_t from, uint16_t to,
    size_t budget, size_t *attempts, uint16_t *value)
{
	if (attempts != NULL) *attempts = 0;
	if (state == NULL || attempts == NULL || value == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	int32_t lower = st_big_number_signed(from), upper = st_big_number_signed(to);
	for (size_t i = 0; i < budget; ++i) {
		int32_t candidate = (int32_t)st_big_random_next(state)*upper/32768;
		*attempts = i+1;
		if (candidate >= lower && candidate <= upper) {
			*value = (uint16_t)candidate;
			return 0;
		}
	}
	/* A scheduling yield is not a failed random request. Retain state for
	 * the next poll, including ranges with no possible accepted candidate. */
	return IO_ERROR_IN_PROGRESS;
}
