/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <zip.h>

#include "filesystem.h"
#include "path.h"
#include "st_ddofx_package.h"
#include "st_ddofx_resource.h"
#include "st_file_lock.h"
#include "string_compat.h"
#include "string_util.h"

static const char *
package_category(const char *filename)
{
	filename = path_untrusted_basename(filename);
	if (strlen(filename) != 12 || !st_ddofx_dos_filename(filename) || ascii_case_ncompare(filename, "$LI$", 4)
	    || ascii_case_compare(filename + 8, ".ZIP"))
		return NULL;
	switch (ascii_to_upper((unsigned char)filename[4])) {
		case 'M':
			return "MUSIC";
		case 'T':
		case 'R':
			return "BBSTHEME";
		case 'F':
			return "FONTS";
		case 'S':
			return "SCRIPTS";
		case 'P':
			return "THEMES";
		default:
			return NULL;
	}
}

struct member {
	zip_t       *archive;
	zip_uint64_t index, size;
};

static int
write_member(FILE *output, void *context)
{
	const struct member *member = context;
	zip_file_t          *file   = zip_fopen_index(member->archive, member->index, 0);
	if (!file)
		return IO_ERROR_PROTOCOL;
	uint8_t      bytes[16384];
	zip_uint64_t total  = 0;
	int          status = 0;
	for (;;) {
		zip_int64_t count = zip_fread(file, bytes, sizeof(bytes));
		if (count < 0 || (zip_uint64_t)count > member->size - total) {
			status = IO_ERROR_PROTOCOL;
			break;
		}
		if (!count)
			break;
		if (output && fwrite(bytes, 1, (size_t)count, output) != (size_t)count) {
			status = IO_ERROR_IO;
			break;
		}
		total += (zip_uint64_t)count;
	}
	if (zip_fclose(file) || total != member->size)
		status = IO_ERROR_PROTOCOL;
	return status;
}

static int
apply_line(const char *directory, st_ddofx_member_path resolve, char *line, char **resource,
    struct st_ddofx_autoplay *play)
{
	char *arg = strchr(line, '=');
	if (!arg)
		return 0;
	*arg++ = 0;
	if (!ascii_case_compare(line, "[RESOURCE]")) {
		free(*resource);
		*resource = NULL;
		if (st_ddofx_dos_filename(arg))
			(void)resolve(resource, directory, arg);
		return 0;
	}
	bool add  = !ascii_case_compare(line, "[ADD]");
	bool del  = !ascii_case_compare(line, "[DEL]");
	bool real = !ascii_case_compare(line, "[REALPLAY]");
	if (!add && !del && !real)
		return 0;
	char *number = strchr(arg, ',');
	if (number)
		*number++ = 0;
	if (!st_ddofx_dos_filename(arg))
		return 0;
	char *path   = NULL;
	int   status = resolve(&path, directory, arg);
	if (status)
		return status;
	if (real && !play->name[0] && number) {
		struct fs_info info;
		char          *end;
		long           rate = strtol(number, &end, 10);
		if (!*end && rate > 0 && rate <= INT_MAX && fs_get_link_info(&info, path) == 0
		    && info.kind == FS_ENTRY_FILE) {
			strcpy(play->name, arg);
			play->rate = (unsigned)rate;
		}
	}
	else if (del && *resource)
		status = st_ddofx_edit_resource(*resource, arg, NULL, 0);
	else if (add && *resource && ascii_case_compare(path, *resource)) {
		char *end;
		long  id = number ? strtol(number, &end, 10) : 0;
		if ((number && *end) || id < 0)
			id = 0;
		if (id > 64000)
			id = 64000;
		status = st_ddofx_edit_resource(*resource, arg, path, (uint16_t)id);
		/* The SDK also uses ADD with a missing resource for uninstall. */
		int removed = fs_remove_file(path);
		if (removed && removed != IO_ERROR_NOT_FOUND)
			status = removed;
	}
	free(path);
	return status == IO_ERROR_NOT_FOUND || status == IO_ERROR_PROTOCOL ? 0 : status;
}

/* Read only the INI from this archive. A stale loose INI in the cache must
 * never cause a subsequent package to repeat a resource update. */
static int
apply_ini(zip_t *archive, zip_uint64_t index, const char *directory, st_ddofx_member_path resolve,
    struct st_ddofx_autoplay *play)
{
	zip_file_t *file = zip_fopen_index(archive, index, 0);
	if (!file)
		return IO_ERROR_PROTOCOL;
	char   line[256], *resource = NULL;
	size_t used     = 0;
	bool   overflow = false;
	int    status   = 0;
	char   bytes[1024];
	for (;;) {
		zip_int64_t count = zip_fread(file, bytes, sizeof(bytes));
		if (count < 0) {
			status = IO_ERROR_PROTOCOL;
			break;
		}
		for (zip_int64_t i = 0; i < count; ++i) {
			char c = bytes[i];
			if (c == '\r' || c == '\n') {
				line[used] = 0;
				if (!overflow)
					status = apply_line(directory, resolve, line, &resource, play);
				used     = 0;
				overflow = false;
				if (status)
					break;
			}
			else if (!c || used == sizeof(line) - 1)
				overflow = true;
			else
				line[used++] = c;
		}
		if (status || !count)
			break;
	}
	if (!status && used && !overflow) {
		line[used] = 0;
		status     = apply_line(directory, resolve, line, &resource, play);
	}
	free(resource);
	if (zip_fclose(file))
		status = IO_ERROR_PROTOCOL;
	return status;
}

static int
unpack(zip_t *archive, const char *directory, st_ddofx_member_path resolve, struct st_ddofx_autoplay *play)
{
	zip_int64_t  ini   = -1;
	zip_int64_t  count = zip_get_num_entries(archive, 0);
	zip_uint64_t total = 0;
	if (count < 0)
		return IO_ERROR_PROTOCOL;
	for (zip_int64_t i = 0; i < count; ++i) {
		zip_stat_t st;
		if (zip_stat_index(archive, (zip_uint64_t)i, 0, &st) < 0)
			return IO_ERROR_PROTOCOL;
		const char *name = path_untrusted_basename(st.name);
		if (!name[0])
			continue;
		/* The SDK's installers discard archive directories. Only regular
		 * DOS files can be installed, using the BBS cache's case lookup. */
		if (!st_ddofx_dos_filename(name))
			return IO_ERROR_INVALID_ARGUMENT;
		zip_uint8_t  os;
		zip_uint32_t attributes;
		if (zip_file_get_external_attributes(archive, (zip_uint64_t)i, 0, &os, &attributes) < 0)
			return IO_ERROR_PROTOCOL;
		unsigned type = (attributes >> 16) & 0170000;
		if (os == ZIP_OPSYS_UNIX && type != 0 && type != 0100000)
			return IO_ERROR_PROTOCOL;
		if ((st.comp_method != ZIP_CM_STORE && st.comp_method != ZIP_CM_DEFLATE)
		    || st.encryption_method != ZIP_EM_NONE || st.size > UINT32_MAX - total)
			return IO_ERROR_NOT_SUPPORTED;
		total += st.size;
		if (play && !ascii_case_compare(name, "DDOFX.INI")) {
			/* Check the full instruction stream's size and CRC before it
			 * can change the resource directory. */
			struct member instructions = {archive, (zip_uint64_t)i, st.size};
			int           status       = write_member(NULL, &instructions);
			if (status)
				return status;
			ini = i;
			continue;
		}
		char *path   = NULL;
		int   status = resolve(&path, directory, name);
		if (!status) {
			struct member member = {archive, (zip_uint64_t)i, st.size};
			status               = fs_replace_file_with(path, write_member, &member);
			if (!status && (st.valid & ZIP_STAT_MTIME))
				status = fs_set_modified_time(path, st.mtime);
		}
		free(path);
		if (status)
			return status;
	}
	if (ini < 0)
		return 0;
	struct st_ddofx_autoplay next   = *play;
	int                      status = apply_ini(archive, (zip_uint64_t)ini, directory, resolve, &next);
	if (!status)
		*play = next;
	return status;
}

static int
unpack_package(const char *path, const char *directory, st_ddofx_member_path resolve,
    struct st_ddofx_autoplay *play)
{
	zip_t *archive = zip_open(path, ZIP_RDONLY | ZIP_CHECKCONS, NULL);
	if (!archive)
		return IO_ERROR_PROTOCOL;
	int status = unpack(archive, directory, resolve, play);
	zip_discard(archive);
	return status;
}

int
st_ddofx_remember_package(void *context, const char *filename, const char *path)
{
	struct st_ddofx_package_batch *batch    = context;
	const char                    *category = package_category(filename);
	unsigned                       slot     = batch->count;
	/* If the user overwrites a destination twice, only the last offer
	 * determines its contents and its position in the install order. */
	for (unsigned i = 0; i < slot; ++i) {
		if (!strcmp(batch->files[i].path, path)) {
			free(batch->files[i].path);
			memmove(&batch->files[i], &batch->files[i + 1], (slot - i - 1) * sizeof(batch->files[0]));
			slot = --batch->count;
			break;
		}
	}
	if (!category)
		return 0;
	if (slot == 50)
		return IO_ERROR_OUT_OF_RANGE;
	char *saved = strdup(path);
	if (!saved)
		return IO_ERROR_OUT_OF_MEMORY;
	batch->files[slot].path     = saved;
	batch->files[slot].category = category;
	++batch->count;
	return 0;
}

static int
install_download(struct st_ddofx_package_batch *batch, const char *category, const char *path)
{
	char *directory = NULL;
	int   status    = batch->directory(&directory, batch->root, category);
	bool theme = !strcmp(category, "BBSTHEME");
	st_file_lock_t *lock = NULL;
	if (!status && theme)
		status = st_file_lock_acquire(&lock, directory, true);
	if (!status)
		status = unpack_package(path, directory, batch->resolve, theme ? &batch->autoplay : NULL);
	if (lock)
		st_file_lock_release(lock);
	free(directory);
	return status ? status : fs_remove_file(path);
}

int
st_ddofx_install_packages(void *context, bool complete)
{
	struct st_ddofx_package_batch *batch       = context;
	int                            first_error = 0;
	for (unsigned i = 0; i < batch->count; ++i) {
		char *path = batch->files[i].path;
		if (complete) {
			int status = install_download(batch, batch->files[i].category, path);
			if (!first_error)
				first_error = status;
		}
		free(path);
	}
	batch->count = 0;
	return first_error;
}
