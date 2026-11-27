/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <string.h>

#include "filesystem.h"
#include "st_ddofx_flic.h"

static unsigned
word(const uint8_t *p)
{
	return p[0] | (unsigned)p[1] << 8;
}

static uint32_t
dword(const uint8_t *p)
{
	return word(p) | (uint32_t)word(p + 2) << 16;
}

bool
st_ddofx_flic_header(struct st_ddofx_flic *f, FILE *file)
{
	uint8_t h[128];
	if (fread(h, 1, sizeof(h), file) != sizeof(h) || (word(h + 4) != 0xaf11 && word(h + 4) != 0xaf12)
	    || word(h + 12) != 8 || !word(h + 6) || !word(h + 8) || !word(h + 10))
		return false;
	f->end = dword(h);
	if (f->end < sizeof(h) || f->end > fs_stream_size(file))
		return false;
	f->file   = file;
	f->frames = word(h + 6);
	f->width  = word(h + 8);
	f->height = word(h + 10);
	return true;
}

struct chunk {
	FILE    *file;
	uint32_t remaining;
};

static bool
take(struct chunk *chunk, void *out, size_t n)
{
	if (n > chunk->remaining || fread(out, 1, n, chunk->file) != n)
		return false;
	chunk->remaining -= (uint32_t)n;
	return true;
}

static bool
read_word(struct chunk *c, unsigned *v)
{
	uint8_t buf[2];
	if (!take(c, buf, 2))
		return false;
	*v = word(buf);
	return true;
}

static bool
colours(struct chunk *c, bool six_bit, uint32_t *palette)
{
	unsigned packets, index = 0;
	if (!read_word(c, &packets))
		return false;
	for (unsigned i = 0; i < packets; ++i) {
		uint8_t pair[2];
		if (!take(c, pair, 2))
			return false;
		index += pair[0];
		unsigned count = pair[1] ? pair[1] : 256;
		if (index > 256 || count > 256 - index)
			return false;
		for (unsigned j = 0; j < count; ++j) {
			uint8_t rgb[3];
			if (!take(c, rgb, 3))
				return false;
			uint32_t value = 0;
			for (unsigned k = 0; k < 3; ++k) {
				unsigned dac = six_bit ? (rgb[k] & 63) : (rgb[k] >> 2);
				value        = value << 8 | dac << 2 | dac >> 4;
			}
			palette[index++] = value;
		}
	}
	return true;
}

/* LC and SS2 use signed packet counts in units of one or two pixels.
 * BRUN reverses the sign convention. All three must stay within one row. */
static bool
run(struct chunk *c, uint8_t *row, unsigned width, unsigned *x, unsigned unit, int count)
{
	unsigned n = (unsigned)(count < 0 ? -count : count) * unit;
	if (n > width - *x)
		return false;
	uint8_t *dest = row + *x;
	*x += n;
	if (count >= 0)
		return take(c, dest, n);
	uint8_t pair[2];
	if (!take(c, pair, unit))
		return false;
	if (unit == 1)
		memset(dest, pair[0], n);
	else
		for (unsigned i = 0; i < n; i += 2)
			memcpy(dest + i, pair, 2);
	return true;
}

static int
signed_byte(unsigned b)
{
	return b < 128 ? (int)b : (int)b - 256;
}

static bool
brun(struct st_ddofx_flic *f, struct chunk *c)
{
	for (unsigned y = 0; y < f->height; ++y) {
		uint8_t count;
		if (!take(c, &count, 1))
			return false;
		uint8_t *row = f->image + y * f->width;
		for (unsigned x = 0; x < f->width;) {
			if (!take(c, &count, 1) || !count || !run(c, row, f->width, &x, 1, -signed_byte(count)))
				return false;
		}
	}
	return true;
}

static bool
delta(struct st_ddofx_flic *f, struct chunk *c, bool words)
{
	unsigned y = 0, lines;
	if ((!words && !read_word(c, &y)) || !read_word(c, &lines) || y > f->height || lines > f->height - y)
		return false;
	for (unsigned line = 0; line < lines; ++line, ++y) {
		unsigned packets;
		if (words) {
			for (;;) {
				if (y >= f->height || !read_word(c, &packets))
					return false;
				if ((packets & 0xc000) == 0xc000)
					y += 0x10000 - packets;
				else if ((packets & 0xc000) == 0x8000)
					f->image[(y + 1) * f->width - 1] = (uint8_t)packets;
				else
					break;
			}
		}
		else {
			uint8_t n;
			if (!take(c, &n, 1))
				return false;
			packets = n;
		}
		unsigned x = 0;
		for (unsigned p = 0; p < packets; ++p) {
			uint8_t pair[2];
			if (!take(c, pair, 2) || pair[0] > f->width - x)
				return false;
			x += pair[0];
			if (!run(c, f->image + y * f->width, f->width, &x, words ? 2 : 1, signed_byte(pair[1])))
				return false;
		}
	}
	return true;
}

static bool
decode(struct st_ddofx_flic *f, struct chunk *c, unsigned type, uint32_t *palette)
{
	switch (type) {
		case 4:
		case 11:
			return !palette || colours(c, type == 11, palette);
		case 7:
		case 12:
			return delta(f, c, type == 7);
		case 13:
			memset(f->image, 0, f->width * f->height);
			return true;
		case 15:
			return brun(f, c);
		case 16:
			return take(c, f->image, f->width * f->height);
		default:
			return true;
	}
}

bool
st_ddofx_flic_frame(struct st_ddofx_flic *f, uint32_t palette[256])
{
	int64_t pos = fs_stream_position(f->file);
	uint8_t header[16];
	if (pos < 0)
		return false;
	uint32_t end;
	for (;;) {
		if (pos + 16 > f->end || fread(header, 1, 16, f->file) != 16)
			return false;
		uint32_t size = dword(header);
		if (size < 16 || size > f->end - pos)
			return false;
		end = (uint32_t)pos + size;
		if (word(header + 4) == 0xf1fa)
			break;
		if (word(header + 4) != 0xf100 || fs_stream_seek(f->file, end))
			return false;
		pos = end;
	}
	pos += 16;
	unsigned chunks = word(header + 6);
	for (unsigned i = 0; i < chunks; ++i) {
		uint8_t h[6];
		if (pos + 6 > end || fread(h, 1, 6, f->file) != 6)
			return false;
		uint32_t size = dword(h);
		if (size < 6 || size > end - pos)
			return false;
		struct chunk c = {f->file, size - 6};
		pos += size;
		if (!decode(f, &c, word(h + 4), palette) || (c.remaining && fs_stream_seek(f->file, pos)))
			return false;
	}
	return pos == end || !fs_stream_seek(f->file, end);
}
