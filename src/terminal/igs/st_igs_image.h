/* Indexed IGS memory images with conversion at native-format boundaries. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_IGS_IMAGE_H_
#define ST_IGS_IMAGE_H_

#include "st_pixels.h"

#include <stddef.h>
#include <stdint.h>

/* Every ST native bitmap has this byte capacity, independent of resolution. */
#define ST_IGS_IMAGE_BYTES 32000

struct st_igs_image {
	/* Full decoded capacity, including padding/retained pixels. A flat row
	 * has 64000/128000/256000 physical indices for 4/2/1 planes. G's current
	 * descriptor supplies the row stride/view; it does not resize storage.
	 * The image owns this buffer. Consumers may edit indices in range but
	 * must not replace the buffer, geometry or plane count directly. */
	struct st_pixel_buffer *pixels;
	unsigned planes;
};

int st_igs_image_create(struct st_igs_image **result, unsigned planes);
void st_igs_image_destroy(struct st_igs_image *);

/* Native offsets/counts are bytes in ST interleaved big-endian plane words.
 * Partial imports preserve other bits; payloads need not be group-aligned.
 * Zero count accepts NULL bytes. The byte array must not alias the image.
 * Invalid operations leave image/output contents unchanged. */
int st_igs_image_import(struct st_igs_image *, size_t offset,
	const uint8_t *bytes, size_t count);
int st_igs_image_export(const struct st_igs_image *, size_t offset,
	uint8_t *bytes, size_t count);

/* Reinterpret the same native bytes in another plane layout once, at a mode
 * change. Drawing and blitting never invoke this conversion. Failure leaves
 * both storage and plane count unchanged. All pixel-buffer borrows end on a
 * successful layout change. No persistent planar copy is retained. */
int st_igs_image_replane(struct st_igs_image *, unsigned planes);

#endif
