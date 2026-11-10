/* Shared independent rounded-fill vectors for B and positive U. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef IGS_BOX_TEST_VECTORS_H
#define IGS_BOX_TEST_VECTORS_H

#include <stdint.h>

struct box_case {
	int resolution, mode, interior, index, flag, perimeter, clipping, initial_last, expected_last;
	int points[4], bounds[4];
	uint64_t hashes[2];
};
#include "data/igs_box_vectors.h"
_Static_assert(sizeof(box_cases)/sizeof(*box_cases) == 2688, "complete box matrix");
#endif
