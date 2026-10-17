/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */
#include <stdatomic.h>
#include <stdint.h>

#if EXPECTED_STANDARD == 11
_Static_assert(__STDC_VERSION__ == 201112L, "C11 mode must actually be selected");
#else
_Static_assert(__STDC_VERSION__ >= 201710L, "C17 mode must actually be selected");
#endif

int
main(void)
{
	atomic_uint_fast64_t value = 1;

	return atomic_fetch_add(&value, UINT64_C(1)) == 1 ? 0 : 1;
}
