/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>
#include <string.h>

#include "filesystem.h"
#include "st_ddofx_graphics_internal.h"
#include "st_ddofx_resource.h"
#include "st_ddofx_script.h"
#include "st_file_lock.h"
#include "string_util.h"

enum {
	RECORD_SIZE = 20
};

static unsigned
word(const uint8_t *p)
{
	return p[0] | (unsigned)p[1] << 8;
}

static void
put_word(uint8_t *p, unsigned value)
{
	p[0] = (uint8_t)value;
	p[1] = (uint8_t)(value >> 8);
}

static int
write_images(const struct st_ddofx_graphics *g, FILE *raw, uint8_t *table)
{
	for (unsigned i = 0; i < SPRITE_COUNT; ++i) {
		const struct st_ddofx_sprite *s = &g->sprites[i];
		if (!s->pixels)
			continue;
		uint8_t *record = table + i * RECORD_SIZE;
		record[0]       = s->active;
		record[1]       = (uint8_t)strlen(s->name);
		for (unsigned j = 0; j < record[1]; ++j)
			record[2 + j] = (uint8_t)ascii_to_upper((unsigned char)s->name[j]);
		/* Bytes 10..13 are a legacy far pointer, ignored by readers. */
		put_word(record + 14, s->allocation);
		put_word(record + 16, s->width);
		put_word(record + 18, s->height);
		for (unsigned y = s->height; y > 0; --y)
			if (fwrite(s->pixels + (y - 1) * s->width, 1, s->width, raw) != s->width)
				return IO_ERROR_IO;
		size_t area = (size_t)s->width * s->height;
		size_t tail = s->allocation - area;
		if (tail && fwrite(s->pixels + area, 1, tail, raw) != tail)
			return IO_ERROR_IO;
	}
	return 0;
}

static bool
read_images(struct st_ddofx_sprite *sprites, FILE *dat, FILE *raw)
{
	if (fs_stream_size(dat) != SPRITE_COUNT * RECORD_SIZE)
		return false;
	for (unsigned i = 0; i < SPRITE_COUNT; ++i) {
		uint8_t record[RECORD_SIZE];
		if (fread(record, 1, sizeof(record), dat) != sizeof(record))
			return false;
		if (!record[0])
			continue;
		struct st_ddofx_sprite *s = &sprites[i];
		s->active                 = record[0];
		s->allocation             = (uint16_t)word(record + 14);
		s->width                  = (uint16_t)word(record + 16);
		s->height                 = (uint16_t)word(record + 18);
		if (record[1] > 8 || memchr(record + 2, 0, record[1]) || !s->width || !s->height
		    || (uint32_t)s->width * s->height > s->allocation)
			return false;
		memcpy(s->name, record + 2, record[1]);
		s->pixels = malloc(s->allocation);
		if (!s->pixels || fread(s->pixels, 1, s->allocation, raw) != s->allocation)
			return false;
		st_ddofx_invert_sprite(s);
	}
	return fgetc(raw) == EOF && !ferror(raw);
}

/* Reserve the backup name exclusively before moving the old table. Holding
 * the bank lock lets a failed second-file operation put the old pair back. */
static int
set_aside(const char *path, char **backup)
{
	struct fs_info info;
	int            status = fs_get_link_info(&info, path);
	if (status == IO_ERROR_NOT_FOUND)
		return 0;
	if (status || info.kind != FS_ENTRY_FILE)
		return IO_ERROR_IO;

	FILE *file;
	status = fs_create_sibling_temp(backup, &file, path);
	if (status)
		return status;
	status = fclose(file) == 0 ? fs_rename(path, *backup) : IO_ERROR_IO;
	if (status) {
		(void)fs_remove_file(*backup);
		free(*backup);
		*backup = NULL;
	}
	return status;
}

static void
finish_backup(char *backup, const char *path, bool restore)
{
	if (backup) {
		if (restore)
			(void)fs_rename(backup, path);
		else
			(void)fs_remove_file(backup);
		free(backup);
	}
}

static void
store_bank(const struct st_ddofx_graphics *g, const char *dat, const char *raw)
{
	uint8_t *table = calloc(SPRITE_COUNT, RECORD_SIZE);
	if (!table)
		return;
	char *staged = NULL, *backup = NULL;
	FILE *file;
	int   status = fs_create_sibling_temp(&staged, &file, raw);
	if (!status) {
		status = write_images(g, file, table);
		if (fclose(file))
			status = IO_ERROR_IO;
	}
	if (!status)
		status = set_aside(dat, &backup);
	if (!status)
		status = fs_replace_file(dat, table, SPRITE_COUNT * RECORD_SIZE);
	if (!status) {
		status = fs_rename(staged, raw);
		if (status && !backup)
			(void)fs_remove_file(dat);
	}
	finish_backup(backup, dat, status != 0);
	if (status && staged)
		(void)fs_remove_file(staged);
	free(staged);
	free(table);
}

static void
load_bank(struct st_ddofx_graphics *g, const char *dat, const char *raw, bool erase)
{
	FILE *data, *images;
	if (fs_open(&data, dat, "rb") != 0)
		return;
	if (fs_open(&images, raw, "rb") != 0) {
		fclose(data);
		return;
	}
	struct st_ddofx_sprite *sprites = calloc(SPRITE_COUNT, sizeof(*sprites));
	bool                    loaded  = sprites && read_images(sprites, data, images);
	fclose(data);
	fclose(images);
	if (loaded) {
		st_ddofx_free_sprites(g);
		memcpy(g->sprites, sprites, sizeof(g->sprites));
		if (erase) {
			char *backup = NULL;
			if (!set_aside(dat, &backup))
				finish_backup(backup, dat, fs_remove_file(raw) != 0);
		}
	}
	else if (sprites) {
		for (unsigned i = 0; i < SPRITE_COUNT; ++i)
			free(sprites[i].pixels);
	}
	free(sprites);
}

void
st_ddofx_sprite_bank(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, char *args, bool load)
{
	int32_t erase = 0;
	if (load && (!st_ddofx_field_number(ddo, &args, &erase) || (erase != 0 && erase != 1)))
		return;
	if (!args || !st_ddofx_dos_filename(args) || strchr(args, '.') || !sink->theme_path)
		return;
	/* Serialize the pair of files, including name resolution, across sessions. */
	char *directory = sink->theme_path(sink->context, NULL);
	if (!directory)
		return;
	st_file_lock_t *lock   = NULL;
	int             status = st_file_lock_acquire(&lock, directory, !load || erase != 0);
	free(directory);
	if (status)
		return;

	char   filename[13];
	size_t length = strlen(args);
	for (size_t i = 0; i < length; ++i)
		filename[i] = (char)ascii_to_upper((unsigned char)args[i]);
	strcpy(filename + length, ".DAT");
	char *dat = sink->theme_path(sink->context, filename);
	strcpy(filename + length, ".RAW");
	char *raw = sink->theme_path(sink->context, filename);
	if (dat && raw) {
		if (load)
			load_bank(ddo->graphics, dat, raw, erase != 0);
		else
			store_bank(ddo->graphics, dat, raw);
	}
	free(raw);
	free(dat);
	st_file_lock_release(lock);
}
