/* X11 native-byte import at the indexed image boundary. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_igs.h"
#include "cterm_vt52.h"
#include "st_igs_image.h"
#include "io_error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

size_t
cterm_igs_bitmap_size(int section)
{
	return section < 0 ? 0 : section == 0 ? ST_IGS_IMAGE_BYTES : 4000;
}

static int
image(struct cterm_igs *igs)
{
	unsigned planes = igs->resolution == 0 ? 4 : igs->resolution == 1 ? 2 : 1;
	if (igs->memory[0] != NULL) return igs->memory[0]->planes == planes ? 0 : IO_ERROR_INVALID_FORMAT;
	return st_igs_image_create(&igs->memory[0],planes);
}

void
cterm_igs_bitmap_cancel(struct cterm_igs *igs)
{
	if (igs->bitmap_loading) igs->bitmap_errors = 0;
	igs->bitmap_loading = false;
	igs->bitmap_deadline = 0;
	igs->bitmap_offset = igs->bitmap_size = igs->bitmap_received = 0;
	igs->bitmap_operation = igs->bitmap_section = 0;
}

static uint64_t
deadline(uint64_t now)
{
	return now > UINT64_MAX-5000 ? UINT64_MAX : now+5000;
}

int
cterm_igs_bitmap_begin(struct cterm_igs *igs, int operation, int section)
{
	igs->bitmap_loading = true;
	igs->bitmap_operation = operation;
	igs->bitmap_section = section > 8 ? 8 : section;
	igs->bitmap_offset = igs->bitmap_section > 0 ? (size_t)(igs->bitmap_section-1)*4000 : 0;
	igs->bitmap_size = cterm_igs_bitmap_size(igs->bitmap_section);
	igs->bitmap_received = 0;
	igs->bitmap_deadline = 0;
	if (igs->bitmap_size == 0) return 0;
	int status = image(igs);
	uint64_t now;
	if (status == 0) status = igs->now_ms(&now);
	if (status == 0) igs->bitmap_deadline = deadline(now);
	return status;
}

int
cterm_igs_bitmap_bytes(struct cterm_igs *igs, const uint8_t *bytes, size_t size,
    size_t *accepted, bool *end)
{
	*accepted = 0; *end = false;
	if (!igs->bitmap_loading || size == 0 || size > igs->bitmap_size-igs->bitmap_received)
		return IO_ERROR_PROTOCOL;
	uint64_t now;
	int status = igs->now_ms(&now);
	if (status != 0) return status;
	bool expired = now >= igs->bitmap_deadline;
	if (expired || igs->bitmap_errors > 2) {
		uint8_t byte = bytes[0];
		if (expired) {
			byte = '*';
			/* Only the intended three-error threshold is retained; do not
			 * emulate foreign byte wrap or an absolute clock bucket. */
			if (igs->bitmap_errors < 3) ++igs->bitmap_errors;
			static const uint8_t bell = 7;
			status = cterm_vt52_output(igs->terminal,&bell,1);
			if (status != 0) return status;
			if (igs->reset_pending) return IO_ERROR_INTERRUPTED;
		}
		status = st_igs_image_import(igs->memory[0],igs->bitmap_offset+igs->bitmap_received,&byte,1);
		if (status != 0) return status;
		++igs->bitmap_received; ++*accepted;
		if (igs->bitmap_errors > 2) *end = true;
	}
	if (!*end && *accepted < size) {
		size_t remaining = size-*accepted;
		status = st_igs_image_import(igs->memory[0],igs->bitmap_offset+igs->bitmap_received,
		    bytes+*accepted,remaining);
		if (status != 0) return status;
		igs->bitmap_received += remaining;
		*accepted += remaining;
	}
	igs->bitmap_deadline = deadline(now);
	return 0;
}

int
cterm_igs_bitmap_complete(struct cterm_igs *igs, int operation, int section)
{
	if (section > 8) section = 8;
	if (operation == 1 || operation == 2) igs->bitmap_errors = 0;
	igs->bitmap_loading = false;
	igs->bitmap_deadline = 0;
	igs->bitmap_offset = igs->bitmap_size = igs->bitmap_received = 0;
	igs->bitmap_operation = igs->bitmap_section = 0;
	int band = igs->resolution == 2 ? 50 : 25;
	int first = section == 0 ? 0 : (section-1)*band;
	int last = section == 0 ? (int)igs->height-1 : section*band-1;
	int width = (int)igs->width-1;
	int *saved = igs->memory_rect[0];
	saved[0] = 0; saved[1] = first; saved[2] = width; saved[3] = last;
	igs->memory_view[0] = (struct cterm_igs_memory_view){width,last-first,width/16+1,
	    igs->resolution == 0 ? 4u : igs->resolution == 1 ? 2u : 1u,false};
	if (operation != 1) return 0;
	const int points[8] = {0,first,width,last,0,first,width,last};
	return cterm_igs_blit(igs,3,0,2,points);
}

int
cterm_igs_bitmap_wipe(struct cterm_igs *igs, int section, int fill)
{
	if (section > 8) section = 8;
	size_t size = cterm_igs_bitmap_size(section), offset = section > 0 ? (size_t)(section-1)*4000 : 0;
	int status = size == 0 ? 0 : image(igs);
	if (status != 0) return status;
	uint8_t bytes[4000];
	if (fill != 256) memset(bytes,(uint8_t)fill,sizeof(bytes));
	for (size_t done = 0; done < size; done += sizeof(bytes)) {
		struct cterm_igs random = *igs;
		if (fill == 256)
			for (size_t i = 0; i < sizeof(bytes); ++i) bytes[i] = (uint8_t)cterm_igs_random(&random);
		status = st_igs_image_import(igs->memory[0],offset+done,bytes,sizeof(bytes));
		if (status != 0) return status;
		if (fill == 256) igs->random_seed = random.random_seed;
	}
	return cterm_igs_bitmap_complete(igs,0,section);
}
