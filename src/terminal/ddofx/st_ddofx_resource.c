/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>
#include <string.h>

#include "filesystem.h"
#include "st_ddofx_resource.h"
#include "string_util.h"

bool
st_ddofx_dos_filename(const char *name)
{
	const char *dot  = strchr(name, '.');
	size_t      stem = dot ? (size_t)(dot - name) : strlen(name);
	if (!stem || stem > 8 || (dot && (!dot[1] || strlen(dot + 1) > 3)))
		return false;
	for (const char *p = name; *p; ++p)
		if (p != dot && ((unsigned char)*p <= ' ' || *p == 127 || strchr(".\"*+,/:;<=>?[\\]|", *p)))
			return false;
	return true;
}

int
st_ddofx_copy_bytes(FILE *output, FILE *input, uint64_t length)
{
	uint8_t bytes[16384];

	while (length) {
		size_t chunk = length < sizeof(bytes) ? (size_t)length : sizeof(bytes);
		if (fread(bytes, 1, chunk, input) != chunk || fwrite(bytes, 1, chunk, output) != chunk)
			return IO_ERROR_IO;
		length -= chunk;
	}
	return 0;
}

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

/* Rebuild the directory and payloads into the atomic replacement stream.
 * Close the source before the writer returns so Windows can replace it. */
struct resource_edit {
	const char *resource, *member, *addition;
	uint16_t    id;
};

struct entry {
	uint8_t record[24];
	FILE   *source;
};

static void
put_u32(uint8_t *p, uint32_t n)
{
	for (unsigned i = 0; i < 4; ++i)
		p[i] = (uint8_t)(n >> (i * 8));
}

static int
append_entry(struct entry **entries, size_t *count, size_t *capacity, const uint8_t *record, FILE *source)
{
	if (*count == *capacity) {
		if (*capacity > SIZE_MAX / 2 / sizeof(**entries))
			return IO_ERROR_OUT_OF_RANGE;
		size_t        next  = *capacity ? *capacity * 2 : 32;
		struct entry *grown = realloc(*entries, next * sizeof(*grown));
		if (!grown)
			return IO_ERROR_OUT_OF_MEMORY;
		*entries  = grown;
		*capacity = next;
	}
	struct entry *entry = &(*entries)[(*count)++];
	memcpy(entry->record, record, sizeof(entry->record));
	entry->source = source;
	return 0;
}

static int
read_entries(FILE *source, const char *remove, struct entry **entries, size_t *count, size_t *capacity)
{
	uint8_t block[792];
	int64_t size = fs_stream_size(source);
	if (size < 0 || (uint64_t)size > UINT32_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	uint32_t at = 0, checkpoint = 0;
	unsigned span = 1, steps = 0;
	for (int64_t remaining = size / sizeof(block); remaining > 0; --remaining) {
		if ((uint64_t)at + sizeof(block) > (uint64_t)size || fs_stream_seek(source, at)
		    || fread(block, 1, sizeof(block), source) != sizeof(block))
			return IO_ERROR_PROTOCOL;
		for (unsigned i = 0; i < 32; ++i) {
			const uint8_t *record = block + 24 * i;
			if (record[0] == 0xff || !record[0])
				continue;
			uint32_t offset = little_u32(record + 12), length = little_u32(record + 16);
			if (offset < sizeof(block) || (uint64_t)offset + length > (uint64_t)size)
				return IO_ERROR_PROTOCOL;
			char name[13] = {0};
			memcpy(name, record, 12);
			if (remove && !ascii_case_compare(remove, name))
				continue;
			int status = append_entry(entries, count, capacity, record, source);
			if (status)
				return status;
		}
		at = little_u32(block + 768 + 12);
		if (!at)
			return 0;
		if (at == checkpoint)
			break;
		if (++steps == span) {
			checkpoint = at;
			steps      = 0;
			span *= 2;
		}
	}
	return IO_ERROR_PROTOCOL;
}

static int
write_entries(FILE *out, struct entry *entries, size_t count)
{
	size_t   blocks = count ? (count + 31) / 32 : 1;
	uint64_t offset = (uint64_t)blocks * 792;
	for (size_t b = 0; b < blocks; ++b) {
		uint8_t block[792] = {0};
		for (unsigned i = 0; i <= 32; ++i)
			block[i * 24] = 0xff;
		for (unsigned i = 0; i < 32 && b * 32 + i < count; ++i) {
			struct entry *entry  = &entries[b * 32 + i];
			uint8_t      *record = block + i * 24;
			memcpy(record, entry->record, 24);
			put_u32(record + 12, (uint32_t)offset);
			offset += little_u32(record + 16);
			if (offset > UINT32_MAX - 3)
				return IO_ERROR_OUT_OF_RANGE;
		}
		if (b + 1 < blocks)
			put_u32(block + 768 + 12, (uint32_t)((b + 1) * 792));
		if (fwrite(block, 1, sizeof(block), out) != sizeof(block))
			return IO_ERROR_IO;
	}
	for (size_t i = 0; i < count; ++i) {
		struct entry *entry = &entries[i];
		if (fs_stream_seek(entry->source, little_u32(entry->record + 12)))
			return IO_ERROR_IO;
		int status = st_ddofx_copy_bytes(out, entry->source, little_u32(entry->record + 16));
		if (status)
			return status;
	}
	const uint8_t lock[] = {0xe5, 0xff, 0xe6};
	return fwrite(lock, 1, sizeof(lock), out) == sizeof(lock) ? 0 : IO_ERROR_IO;
}

static int
edit_contents(FILE *out, void *context)
{
	const struct resource_edit *edit   = context;
	FILE                       *source = NULL, *addition = NULL;
	struct entry               *entries = NULL;
	size_t                      count = 0, capacity = 0;
	int                         status = fs_open(&source, edit->resource, "rb");
	if (!status)
		status = read_entries(source, edit->addition ? NULL : edit->member, &entries, &count, &capacity);
	if (!status && edit->addition) {
		status = fs_open(&addition, edit->addition, "rb");
		if (!status) {
			int64_t size = fs_stream_size(addition);
			if (size < 0 || (uint64_t)size > UINT32_MAX)
				status = IO_ERROR_OUT_OF_RANGE;
			else {
				uint8_t record[24] = {0};
				for (size_t i = 0; edit->member[i]; ++i)
					record[i] = (uint8_t)ascii_to_upper((unsigned char)edit->member[i]);
				const char *ext = strrchr(edit->member, '.');
				record[20]      = ext && !ascii_case_compare(ext, ".PCX");
				record[22]      = (uint8_t)edit->id;
				record[23]      = (uint8_t)(edit->id >> 8);
				put_u32(record + 16, (uint32_t)size);
				status = append_entry(&entries, &count, &capacity, record, addition);
			}
		}
	}
	if (!status)
		status = write_entries(out, entries, count);
	free(entries);
	if (source)
		fclose(source);
	if (addition)
		fclose(addition);
	return status;
}

int
st_ddofx_edit_resource(const char *path, const char *member, const char *addition, uint16_t id)
{
	if (!*member || strlen(member) > 12)
		return IO_ERROR_INVALID_ARGUMENT;
	struct fs_info info;
	int            status = fs_get_link_info(&info, path);
	if (status || info.kind != FS_ENTRY_FILE)
		return status ? status : IO_ERROR_INVALID_ARGUMENT;
	struct resource_edit edit = {path, member, addition, id};
	status                    = fs_replace_file_with(path, edit_contents, &edit);
	if (!status)
		status = fs_set_modified_time(path, info.modified_time);
	return status;
}
