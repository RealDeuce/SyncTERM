/* Indexed observations shared by independent raster fixtures. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef IGS_PIXEL_TEST_HELPERS_H
#define IGS_PIXEL_TEST_HELPERS_H

#include "igs_test_fixture.h"

static uint64_t
native_hash(const struct st_pixel_buffer *b, unsigned planes, bool alternate)
{
	uint64_t hash = UINT64_C(14695981039346656037);
	const uint32_t *pixels = alternate ? b->alternate : b->primary;
	for (unsigned y = 0; y < b->height; ++y)
		for (unsigned group = 0; group < b->width/16; ++group) {
			uint16_t words[4] = {0};
			for (unsigned bit = 0; bit < 16; ++bit) {
				uint32_t colour = pixels[y*b->width+group*16+bit];
				CHECK(colour >= 768 && colour < 768+(1u << planes));
				for (unsigned p = 0; p < planes; ++p)
					if ((colour-768) & (1u << p)) words[p] |= (uint16_t)(0x8000u >> bit);
			}
			for (unsigned p = 0; p < planes; ++p) {
				hash = (hash ^ (words[p] >> 8))*UINT64_C(1099511628211);
				hash = (hash ^ (words[p] & 255))*UINT64_C(1099511628211);
			}
		}
	return hash;
}

#endif
