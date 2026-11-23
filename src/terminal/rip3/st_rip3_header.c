/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_header.h"
#include "st_rip3_number.h"
#include "io_error.h"

static int
field(unsigned *result, const uint8_t **bytes, size_t *remaining, size_t width)
{
	if (*remaining < width)
		return IO_ERROR_INVALID_FORMAT;
	uint64_t value;
	int status = st_rip3_number_decode(&value, *bytes, width, 64);
	if (status != 0)
		return status;
	*result = (unsigned)value;
	*bytes += width;
	*remaining -= width;
	return 0;
}

int
st_rip3_header_decode(struct st_rip3_header *result, const uint8_t *command, size_t length)
{
	static const unsigned flag_width[] = {2, 2, 4, 4, 2, 4, 2, 2, 2, 2};
	static const unsigned flag_max[] = {255, 255, 65535, 524287, 255, 65535, 31, 1, 15, 1};
	struct st_rip3_header header = {0};

	if (result == NULL || command == NULL || length == 0 || *command != 'h')
		return IO_ERROR_INVALID_ARGUMENT;
	++command;
	--length;
	int status = field(&header.revision, &command, &length, 2);
	if (status == 0) status = field(&header.flags, &command, &length, 4);
	if (status == 0) status = field(&header.count, &command, &length, 2);
	if (status != 0) return status;
	if (header.revision > 1 || header.flags > 31 || header.count > 10)
		return IO_ERROR_OUT_OF_RANGE;

	/* The original consumer walks decoded arguments, not byte descriptors.
	 * Keep the two passes so the type-9 stride mismatch cannot read past the
	 * argument list. A header declaring zero still has to frame valid fields. */
	unsigned decoded[50];
	size_t used = 0;
	while (length != 0) {
		if (*command < '0' || *command > '9')
			return IO_ERROR_INVALID_FORMAT;
		const unsigned type = *command++ - '0';
		--length;
		const size_t count = type == 5 ? 5 : type < 6 ? 3 : 2;
		if (header.count != 0 && count > sizeof(decoded) / sizeof(*decoded) - used)
			return IO_ERROR_INVALID_FORMAT;
		unsigned words[5] = {type};
		status = field(&words[1], &command, &length, flag_width[type]);
		if (status == 0 && type < 6) status = field(&words[2], &command, &length, 1);
		if (status == 0 && type == 5) status = field(&words[3], &command, &length, 1);
		if (status == 0 && type == 5) status = field(&words[4], &command, &length, 1);
		if (status != 0) return status;
		if (header.count != 0) {
			for (size_t i = 0; i < count; ++i)
				decoded[used++] = words[i];
		}
	}
	size_t position = 0;
	for (unsigned i = 0; i < header.count; ++i) {
		if (position == used || decoded[position] > 9)
			return IO_ERROR_INVALID_FORMAT;
		unsigned type = decoded[position];
		size_t stride = type == 5 ? 5 : type < 6 || type == 9 ? 3 : 2;
		if (stride > used - position)
			return IO_ERROR_INVALID_FORMAT;
		struct st_rip3_header_segment *segment = &header.segments[i];
		segment->type = type;
		segment->flags = decoded[position + 1];
		if (segment->flags > flag_max[type])
			return IO_ERROR_OUT_OF_RANGE;
		if (type < 6) {
			segment->slot = decoded[position + 2];
			if (segment->slot > 35)
				return IO_ERROR_OUT_OF_RANGE;
		}
		if (type == 5) {
			/* The shipped validator checks the slot instead of either field. */
			if (((segment->flags & 1024) && (segment->slot < 2 || segment->slot > 5))
			    || ((segment->flags & 4096) && (segment->slot < 1 || segment->slot > 8)))
				return IO_ERROR_OUT_OF_RANGE;
			segment->coordinate_width = decoded[position + 3];
			segment->rgb_bits = decoded[position + 4];
		}
		position += stride;
	}
	if (position != used)
		return IO_ERROR_INVALID_FORMAT;
	*result = header;
	return 0;
}
