/* Shared indexed fill fixtures and native initial word recipes. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef IGS_FILL_TEST_HELPERS_H
#define IGS_FILL_TEST_HELPERS_H

#include "igs_pixel_test_helpers.h"

static const uint16_t user_rows[16] = {0,0xffff,0xa531,0x8001,0x5555,0xaaaa,0x0010,0x7ffe,
    0xf00f,0x00ff,0xff00,0x1357,0x2468,0x1111,0x8888,0x5aa5};

static void
no_response(const char *bytes, size_t size, void *data)
{
	(void)bytes; (void)data; CHECK(size == 0);
}

static void
seed_words(struct fixture *f)
{
	struct cterm_igs *igs = f->terminal->igs_engine;
	unsigned planes = (unsigned[]){4,2,1}[igs->resolution];
	struct st_pixel_buffer *b = NULL; CHECK(st_pixel_buffer_create(&b,igs->width,igs->height,true) == 0);
	for (unsigned x = 0; x < igs->width; ++x) {
		unsigned primary = 0, alternate = 0;
		for (unsigned plane = 0; plane < planes; ++plane) {
			if ((0x5aa5u^(plane*0x1357u)) & (0x8000u >> (x%16))) primary |= 1u << plane;
			if ((0xa55au^(plane*0x2468u)) & (0x8000u >> (x%16))) alternate |= 1u << plane;
		}
		b->primary[x] = 768+primary; b->alternate[x] = 768+alternate;
	}
	for (unsigned y = 1; y < igs->height; ++y) {
		memcpy(b->primary+y*igs->width,b->primary,igs->width*sizeof(*b->primary));
		memcpy(b->alternate+y*igs->width,b->alternate,igs->width*sizeof(*b->alternate));
	}
	const struct st_pixel_blit blit = {.width = igs->width, .height = igs->height, .scale_x = 1, .scale_y = 1};
	CHECK(st_console_blit_pixels(f->console,b,NULL,&blit) == 0); st_pixel_buffer_destroy(b);
}

#endif
