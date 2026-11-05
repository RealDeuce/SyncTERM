/* Decode font data, never execute an Amiga load segment. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "amiga_font_internal.h"
#include "filesystem.h"
#include "io_error.h"
#include "string_compat.h"
#include <stdlib.h>
#include <string.h>

static unsigned u16(const uint8_t *p) { return (unsigned)p[0] * 256 + p[1]; }
static uint32_t u32(const uint8_t *p) { return (uint32_t)u16(p) * 65536 + u16(p + 2); }
static int s16(const uint8_t *p)
{
	unsigned n = u16(p);
	return n < 32768 ? (int)n : (int)n - 65536;
}
static bool span(size_t size, size_t at, size_t count) { return at <= size && count <= size - at; }
static bool names_equal(const char *a, const char *b)
{
	while (*a && *b) {
		unsigned char x = (unsigned char)*a++, y = (unsigned char)*b++;
		if (x >= 'A' && x <= 'Z')
			x = (unsigned char)(x + 32);
		if (y >= 'A' && y <= 'Z')
			y = (unsigned char)(y + 32);
		if (x != y)
			return false;
	}
	return *a == *b;
}
const st_amiga_font_t *st_amiga_font_builtin(const char *name, unsigned height)
{
	if (name == NULL)
		return NULL;
	for (size_t i = 0; i < st_amiga_builtins_count; ++i)
		if (st_amiga_builtins[i].font->height == height &&
			names_equal(name, st_amiga_builtins[i].name))
			return st_amiga_builtins[i].font;
	return NULL;
}
size_t st_amiga_font_builtin_count(void) { return st_amiga_builtins_count; }
const char *st_amiga_font_builtin_name(size_t i, unsigned *height)
{
	if (i >= st_amiga_builtins_count)
		return NULL;
	if (height)
		*height = st_amiga_builtins[i].font->height;
	return st_amiga_builtins[i].name;
}
unsigned st_amiga_font_height(const st_amiga_font_t *font) { return font->height; }
unsigned st_amiga_font_baseline(const st_amiga_font_t *font) { return font->baseline; }
const struct st_amiga_glyph *st_amiga_font_glyph(const st_amiga_font_t *font, uint8_t ch)
{
	return &font->glyphs[ch];
}
void st_amiga_font_release(const st_amiga_font_t *font)
{
	if (font && font->storage) {
		free(font->storage);
		free((void *)font);
	}
}
struct hunk {
	const uint8_t *data;
	size_t size;
};
/* Only the four TextFont data references are needed. Relocations of executable
 * entry stubs, list links and OS bookkeeping are deliberately never applied. */
static const unsigned pointer_fields[4] = {92, 98, 102, 106};
static const uint8_t *font_pointer(const struct hunk *hunks, size_t count, const size_t targets[4],
								   unsigned field, size_t required)
{
	uint32_t offset = u32(hunks[0].data + pointer_fields[field]);
	if (offset == 0 && targets[field] == SIZE_MAX)
		return NULL;
	size_t index = targets[field] == SIZE_MAX ? 0 : targets[field];
	if (index >= count)
		return NULL;
	const struct hunk *target = &hunks[index];
	return target->data && span(target->size, offset, required) ? target->data + offset : NULL;
}
int st_amiga_font_decode(st_amiga_font_t **result, const void *bytes, size_t size)
{
	const uint8_t *input = bytes;
	struct hunk *hunks = NULL;
	st_amiga_font_t *font = NULL;
	size_t position = 4, count, current = 0, targets[4] = {SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX};
	int status = IO_ERROR_INVALID_FORMAT;
	if (!result || !input || !span(size, 0, 24) || u32(input) != 1011)
		return status;
	/* Skip the HUNK_HEADER resident-name table. */
	while (span(size, position, 4) && u32(input + position) != 0) {
		size_t n = u32(input + position);
		position += 4;
		if (n > (size - position) / 4)
			return status;
		position += n * 4;
	}
	if (!span(size, position, 16))
		return status;
	position += 4;
	count = u32(input + position);
	if (count == 0 || u32(input + position + 4) != 0 || u32(input + position + 8) != count - 1)
		return status;
	position += 12;
	if (count > (size - position) / 4 || count > SIZE_MAX / sizeof(*hunks))
		return status;
	position += count * 4;
	hunks = calloc(count, sizeof(*hunks));
	font = calloc(1, sizeof(*font));
	if (!hunks || !font) {
		status = IO_ERROR_OUT_OF_MEMORY;
		goto done;
	}
	font->storage = malloc(size);
	if (!font->storage) {
		status = IO_ERROR_OUT_OF_MEMORY;
		goto done;
	}
	memcpy(font->storage, input, size);
	input = font->storage;
	bool ended = true;
	while (position < size) {
		if (!span(size, position, 4))
			goto done;
		uint32_t type = u32(input + position) & UINT32_C(0x3fffffff);
		position += 4;
		if (type == 1001 || type == 1002 || type == 1003) {
			if (!ended || current >= count || !span(size, position, 4))
				goto done;
			size_t n = u32(input + position);
			position += 4;
			if (n > SIZE_MAX / 4)
				goto done;
			hunks[current].size = n * 4;
			if (type != 1003) {
				if (!span(size, position, n * 4))
					goto done;
				hunks[current].data = input + position;
				position += n * 4;
			}
			ended = false;
		} else if (type == 1004) {
			if (ended)
				goto done;
			for (;;) {
				if (!span(size, position, 4))
					goto done;
				size_t n = u32(input + position);
				position += 4;
				if (!n)
					break;
				if (!span(size, position, 4))
					goto done;
				size_t target = u32(input + position);
				position += 4;
				if (target >= count || n > (size - position) / 4)
					goto done;
				for (size_t i = 0; i < n; ++i) {
					uint32_t offset = u32(input + position);
					position += 4;
					if (!span(hunks[current].size, offset, 4))
						goto done;
					if (current == 0)
						for (unsigned f = 0; f < 4; ++f)
							if (offset == pointer_fields[f])
								targets[f] = target;
				}
			}
		} else if (type == 1010) {
			if (ended)
				goto done;
			ended = true;
			++current;
		} else if (type == 1009 || type == 1000) {
			if (!span(size, position, 4))
				goto done;
			size_t n = u32(input + position);
			position += 4;
			if (n > (size - position) / 4)
				goto done;
			position += n * 4;
		} else {
			status = IO_ERROR_NOT_SUPPORTED;
			goto done;
		}
	}
	if (!ended || current != count || !hunks[0].data || hunks[0].size < 110)
		goto done;
	const uint8_t *h = hunks[0].data;
	if (u16(h + 18) != 0x0f80)
		goto done;
	if (h[80] & 0x40) {
		status = IO_ERROR_NOT_SUPPORTED;
		goto done;
	}
	font->height = u16(h + 78);
	font->baseline = u16(h + 84);
	unsigned first = h[90], last = h[91], nominal = u16(h + 82), modulo = u16(h + 96);
	if (!font->height || font->baseline >= font->height || first > last || !modulo)
		goto done;
	size_t glyphs = last - first + 2;
	const uint8_t *bits = font_pointer(hunks, count, targets, 0, (size_t)modulo * font->height);
	const uint8_t *loc = font_pointer(hunks, count, targets, 1, glyphs * 4);
	const uint8_t *space = font_pointer(hunks, count, targets, 2, glyphs * 2);
	const uint8_t *kern = font_pointer(hunks, count, targets, 3, glyphs * 2);
	if (!bits || !loc || ((u32(h + 102) != 0 || targets[2] != SIZE_MAX) && !space) ||
		((u32(h + 106) != 0 || targets[3] != SIZE_MAX) && !kern))
		goto done;
	for (unsigned ch = 0; ch < 256; ++ch) {
		size_t i = ch >= first && ch <= last ? ch - first : glyphs - 1;
		unsigned offset = u16(loc + i * 4), width = u16(loc + i * 4 + 2);
		if (offset > (unsigned)modulo * 8 || width > (unsigned)modulo * 8 - offset)
			goto done;
		bool proportional = (h[81] & 0x20) != 0;
		int difference = (int)nominal - (int)width;
		int bearing = proportional		? (kern ? s16(kern + i * 2) : 0)
					  : difference >= 0 ? difference / 2
										: -((-difference + 1) / 2);
		font->glyphs[ch] = (struct st_amiga_glyph){
			.bits = bits + offset / 8,
			.stride = modulo,
			.width = width,
			.height = font->height,
			.bearing = bearing,
			.advance =
				proportional ? bearing + (space ? s16(space + i * 2) : (int)nominal) : (int)nominal,
			.top = (int)font->height - (int)font->baseline - 1,
			.bit_offset = offset % 8,
		};
	}
	*result = font;
	font = NULL;
	status = 0;
done:
	if (font) {
		free(font->storage);
		free(font);
	}
	free(hunks);
	return status;
}
void st_amiga_family_dispose(struct st_amiga_family *f)
{
	if (!f)
		return;
	for (size_t i = 0; i < f->count; ++i)
		free(f->strikes[i].path);
	free(f->strikes);
	free(f->name);
	*f = (struct st_amiga_family){0};
}
static int relative_name(char **out, const uint8_t *bytes, size_t limit)
{
	size_t length = 0;
	while (length < limit && bytes[length])
		++length;
	if (!length || length == limit || bytes[0] == '/' || bytes[0] == '\\')
		return IO_ERROR_INVALID_FORMAT;
	char *text = malloc(length * 2 + 1);
	if (!text)
		return IO_ERROR_OUT_OF_MEMORY;
	size_t used = 0, start = 0;
	for (size_t i = 0; i <= length; ++i) {
		unsigned c = bytes[i];
		if (i == length || c == '/') {
			size_t n = i - start;
			if (!n || (n == 1 && bytes[start] == '.') ||
				(n == 2 && bytes[start] == '.' && bytes[start + 1] == '.')) {
				free(text);
				return IO_ERROR_INVALID_FORMAT;
			}
			start = i + 1;
		}
		if (i == length)
			break;
		if (c < 32 || c == 127 || c == ':' || c == '\\') {
			free(text);
			return IO_ERROR_INVALID_FORMAT;
		}
		if (c >= 128) {
			text[used++] = (char)(0xc0 | (c >> 6));
			text[used++] = (char)(0x80 | (c & 63));
		} else
			text[used++] = (char)c;
	}
	text[used] = 0;
	*out = text;
	return 0;
}
int st_amiga_family_decode(struct st_amiga_family *result, const char *name, const void *bytes,
						   size_t size)
{
	const uint8_t *p = bytes;
	struct st_amiga_family f = {0};
	if (!result || !name || !p || size < 4)
		return IO_ERROR_INVALID_FORMAT;
	unsigned type = u16(p), count = u16(p + 2);
	if (type != 0x0f00 && type != 0x0f02)
		return type == 0x0f03 ? IO_ERROR_NOT_SUPPORTED : IO_ERROR_INVALID_FORMAT;
	if (!count || count > (size - 4) / 260)
		return IO_ERROR_INVALID_FORMAT;
	f.name = strdup(name);
	f.strikes = calloc(count, sizeof(*f.strikes));
	if (!f.name || !f.strikes) {
		st_amiga_family_dispose(&f);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	for (unsigned i = 0; i < count; ++i) {
		const uint8_t *entry = p + 4 + i * 260;
		char *path = NULL;
		int status = relative_name(&path, entry, type == 0x0f02 ? 254 : 256);
		unsigned height = u16(entry + 256);
		if (status || !height) {
			free(path);
			st_amiga_family_dispose(&f);
			return status ? status : IO_ERROR_INVALID_FORMAT;
		}
		f.strikes[f.count++] = (struct st_amiga_strike){height, path, entry[258]};
	}
	*result = f;
	return 0;
}
int st_amiga_font_read(st_amiga_font_t **result, const char *path)
{
	if (!result || !path)
		return IO_ERROR_INVALID_ARGUMENT;
	FILE *stream = NULL;
	int status = fs_open(&stream, path, "rb");
	if (status != 0)
		return status;
	int64_t length = fs_stream_size(stream);
	if (length < 0 || (uint64_t)length > SIZE_MAX) {
		fclose(stream);
		return length < 0 ? (int)length : IO_ERROR_OUT_OF_RANGE;
	}
	if (length == 0) {
		fclose(stream);
		return IO_ERROR_INVALID_FORMAT;
	}
	uint8_t *bytes = malloc((size_t)length);
	if (!bytes) {
		fclose(stream);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	status = fread(bytes, 1, (size_t)length, stream) == (size_t)length ? 0 : IO_ERROR_IO;
	if (fclose(stream) != 0 && status == 0)
		status = IO_ERROR_IO;
	if (status == 0)
		status = st_amiga_font_decode(result, bytes, (size_t)length);
	free(bytes);
	return status;
}
