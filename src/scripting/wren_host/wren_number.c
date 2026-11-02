/* Lossless integer boundaries for Wren's binary64 Number. */

/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */

#include "wren_host_internal.h"

#include <math.h>
#include <stdint.h>

bool
st_wren_u64_exact(uint64_t value)
{
	return value <= ST_WREN_MAX_INTEGER;
}

bool
st_wren_i64_exact(int64_t value)
{
	return value >= -(int64_t)ST_WREN_MAX_INTEGER
	    && value <= (int64_t)ST_WREN_MAX_INTEGER;
}

bool
st_wren_number_i64(WrenVM *vm, int slot, int64_t *result)
{
	double value;

	if (wrenGetSlotType(vm, slot) != WREN_TYPE_NUM)
		return false;
	value = wrenGetSlotDouble(vm, slot);
	if (!isfinite(value) || trunc(value) != value
	    || value < -(double)ST_WREN_MAX_INTEGER
	    || value > (double)ST_WREN_MAX_INTEGER)
		return false;
	*result = (int64_t)value;
	return true;
}

bool
st_wren_number_u64(WrenVM *vm, int slot, uint64_t *result)
{
	int64_t value;

	if (!st_wren_number_i64(vm, slot, &value) || value < 0)
		return false;
	*result = (uint64_t)value;
	return true;
}

void
st_wren_return_u64(WrenVM *vm, uint64_t value)
{
	if (!st_wren_u64_exact(value))
		st_wren_abort(vm, "Native integer exceeds Wren's exact Number range");
	else
		wrenSetSlotDouble(vm, 0, (double)value);
}

void
st_wren_return_i64(WrenVM *vm, int64_t value)
{
	if (!st_wren_i64_exact(value))
		st_wren_abort(vm, "Native integer exceeds Wren's exact Number range");
	else
		wrenSetSlotDouble(vm, 0, (double)value);
}
