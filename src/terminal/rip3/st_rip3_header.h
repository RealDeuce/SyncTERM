/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_HEADER_H_
#define ST_RIP3_HEADER_H_

#include <stddef.h>
#include <stdint.h>

enum st_rip3_header_flag {
	ST_RIP3_HEADER_HARD = 1,
	ST_RIP3_HEADER_SOFT = 2,
	ST_RIP3_HEADER_LOCK_INPUT = 4,
	ST_RIP3_HEADER_SELECT_ZERO = 8,
	ST_RIP3_HEADER_CLEAR_REFRESH = 16,
};

struct st_rip3_header_segment {
	unsigned type, flags, slot, coordinate_width, rgb_bits;
};

struct st_rip3_header {
	unsigned revision, flags, count;
	struct st_rip3_header_segment segments[10];
};

#define ST_RIP3_HEADER_BASE_FIELDS "242"

/* Fixed UltraNum widths after the literal segment type. */
static inline const char *
st_rip3_header_segment_fields(unsigned type)
{
	static const char *const fields[] = {
		"21", "21", "41", "41", "21", "4111", "2", "2", "2", "2"
	};
	return type < sizeof(fields) / sizeof(*fields) ? fields[type] : NULL;
}

/* Decode the complete h command before any scene action. */
int st_rip3_header_decode(struct st_rip3_header *result, const uint8_t *command, size_t length);
#endif
