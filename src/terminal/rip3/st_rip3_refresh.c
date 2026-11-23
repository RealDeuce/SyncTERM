/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_refresh.h"
#include "st_rip3_query.h"
#include "st_rip3_query_internal.h"
#include "io_error.h"

int
st_rip3_refresh_define(struct st_rip3_refresh *refresh, const uint8_t *bytes, size_t length)
{
	if (refresh == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	if ((bytes == NULL && length != 0) || length == SIZE_MAX)
		return IO_ERROR_INVALID_ARGUMENT;
	static const char off[] = "$OFF$";
	bool clear = length == sizeof(off) - 1;
	for (size_t i = 0; clear && i < length; ++i) {
		uint8_t value = bytes[i];
		if (value >= 'a' && value <= 'z') value -= 'a' - 'A';
		clear = value == (uint8_t)off[i];
	}
	struct st_rip3_resident_query source = {(uint8_t *)bytes, clear ? 0 : length};
	return st_rip3_resident_query_copy(&refresh->expression, &source);
}

void
st_rip3_refresh_clear(struct st_rip3_refresh *refresh)
{
	if (refresh != NULL)
		st_rip3_resident_query_clear(&refresh->expression);
}

int
st_rip3_refresh_start(struct st_rip3_refresh *refresh)
{
	if (refresh == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	if (refresh->expression.length == 0)
		return 0;
	/* query_start rejects a recursive send while this evaluator is active. */
	return st_rip3_query_start(refresh->query, refresh->expression.bytes, refresh->expression.length);
}

bool
st_rip3_refresh_pending(const struct st_rip3_refresh *refresh)
{
	return refresh != NULL && st_rip3_query_pending(refresh->query);
}
