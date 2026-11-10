/* Shared IGS line/Drawto fixtures and independent vector observations. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef IGS_LINE_TEST_HELPERS_H
#define IGS_LINE_TEST_HELPERS_H

#include "igs_pixel_test_helpers.h"

struct line_case {
	int resolution, style, width, begin, end, mode, clipping, initial_last;
	int x1, y1, x2, y2, left, top, right, bottom;
	uint64_t hashes[2];
};
#include "data/igs_line_vectors.h"
_Static_assert(sizeof(line_cases)/sizeof(*line_cases) == 3321, "complete source-derived line matrix");

#endif
