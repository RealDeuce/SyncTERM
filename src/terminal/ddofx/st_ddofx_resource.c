/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <string.h>

#include "filesystem.h"
#include "st_ddofx_resource.h"
#include "string_util.h"

static uint32_t
little_u32(const uint8_t *p)
{
	return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

int
st_ddofx_resource_find(FILE *file, const char *member, int id, uint32_t *length, uint16_t *type)
{
	enum {
		RECORD_SIZE  = 24,
		MEMBER_SLOTS = 32
	};

	uint8_t block[(MEMBER_SLOTS + 1) * RECORD_SIZE];
	size_t  name_length = strlen(member);
	if (file == NULL || name_length == 0 || name_length > 12)
		return -1;
	int64_t  size = fs_stream_size(file);
	uint32_t at = 0, checkpoint = 0;
	unsigned steps = 0, span = 1;
	/* The link record contains an absolute offset. Check for cycles even
	 * when the next directory sits before the current block. */
	for (int64_t blocks = size / (int64_t)sizeof(block); blocks > 0; --blocks) {
		if ((int64_t)at > size - (int64_t)sizeof(block) || fs_stream_seek(file, at) != 0
		    || fread(block, 1, sizeof(block), file) != sizeof(block))
			break;
		for (unsigned i = 0; i < MEMBER_SLOTS; ++i) {
			const uint8_t *p = block + i * RECORD_SIZE;
			if (p[0] == 0xff || ascii_case_ncompare((const char *)p, member, name_length) != 0
			    || (name_length < 12 && p[name_length] != 0))
				continue;
			uint32_t offset = little_u32(p + 12), bytes = little_u32(p + 16);
			int      found = p[22] | (unsigned)p[23] << 8;
			if ((id >= 0 && found != id) || offset < sizeof(block)
			    || (uint64_t)offset + bytes > (uint64_t)size)
				continue;
			if (fs_stream_seek(file, offset) != 0)
				return -1;
			*length = bytes;
			if (type != NULL)
				*type = (uint16_t)(p[20] | (unsigned)p[21] << 8);
			return found;
		}
		at = little_u32(block + MEMBER_SLOTS * RECORD_SIZE + 12);
		if (at == 0 || at == checkpoint)
			break;
		if (++steps == span) {
			checkpoint = at;
			steps      = 0;
			span *= 2;
		}
	}
	return -1;
}
