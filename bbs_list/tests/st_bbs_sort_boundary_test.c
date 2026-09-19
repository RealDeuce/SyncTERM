/* Audit-only standalone test of the unchanged sort implementation.
 * Compile with undefined-behavior sanitization; no production fixes here.
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <limits.h>
#include <stdio.h>

#include "../st_bbs_sort.c"

/* Sorting entries is not exercised by this scalar-normalization test. */
size_t
st_bbs_list_count(const st_bbs_list_t *list)
{
	(void)list;
	return 0;
}

int
main(void)
{
	static const struct {
		int input;
		int expected;
	} cases[] = {
		{1, 1}, {-1, -1}, {30, 29}, {-30, -29}, {31, 31},
		{0, 0}, {32, 0}, {INT_MAX, 0}, {INT_MIN, 0},
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
		int actual = st_bbs_sort_field_normalize(cases[i].input);

		if (actual != cases[i].expected) {
			fprintf(stderr, "normalize(%d): %d, expected %d\n",
			    cases[i].input, actual, cases[i].expected);
			return 1;
		}
	}
	return 0;
}
