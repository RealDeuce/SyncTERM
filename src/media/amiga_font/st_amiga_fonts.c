/* Family packages are staged together; only complete packages are visible. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_amiga_fonts.h"
#include "amiga_directory_internal.h"
#include "filesystem.h"
#include "io_error.h"
#include "path.h"
#include "st_unicode_compare.h"
#include "st_unicode_text.h"
#include "string_compat.h"
#include <stdlib.h>
#include <string.h>

int amiga_names_append(struct amiga_names *names, const char *name)
{
	if (names->count == SIZE_MAX / sizeof(*names->items))
		return IO_ERROR_OUT_OF_MEMORY;
	char **items = realloc(names->items, (names->count + 1) * sizeof(*items));
	if (!items)
		return IO_ERROR_OUT_OF_MEMORY;
	names->items = items;
	items[names->count] = strdup(name);
	if (!items[names->count])
		return IO_ERROR_OUT_OF_MEMORY;
	++names->count;
	return 0;
}
void amiga_names_dispose(struct amiga_names *names)
{
	for (size_t i = 0; i < names->count; ++i)
		free(names->items[i]);
	free(names->items);
	*names = (struct amiga_names){0};
}
static int parent(char **out, const char *path)
{
	ptrdiff_t n = path_parent(NULL, 0, path);
	if (n < 0)
		return (int)n;
	char *p = malloc((size_t)n + 1);
	if (!p)
		return IO_ERROR_OUT_OF_MEMORY;
	int status = path_parent(p, (size_t)n + 1, path) < 0 ? IO_ERROR_INVALID_ARGUMENT : 0;
	if (status)
		free(p);
	else
		*out = p;
	return status;
}
int st_amiga_fonts_directory(char **out, const char *settings)
{
	if (!out || !settings)
		return IO_ERROR_INVALID_ARGUMENT;
	char *base = NULL;
	int status = parent(&base, settings);
	if (!status)
		status = path_join_alloc(out, base, "fonts/amiga");
	free(base);
	return status;
}
static bool family_name(const char *name)
{
	if (name == NULL)
		return false;
	size_t size = strlen(name);
	return size > 5 && name[size - 5] == '.' && (name[size - 4] == 'f' || name[size - 4] == 'F') &&
		   (name[size - 3] == 'o' || name[size - 3] == 'O') &&
		   (name[size - 2] == 'n' || name[size - 2] == 'N') &&
		   (name[size - 1] == 't' || name[size - 1] == 'T');
}
static int key(char **out, const char *name)
{
	if (!family_name(name) || !path_untrusted_component_valid(name) || name[0] == '.' ||
		!st_unicode_name_valid(name, 255, 255, ":\\/"))
		return IO_ERROR_INVALID_ARGUMENT;
	char *folded = NULL;
	size_t size = 0;
	int status = st_unicode_caseless_key(&folded, &size, name, strlen(name));
	if (!status)
		status = st_unicode_text_normalize(out, &size, folded, size, NULL);
	free(folded);
	return status;
}
static int package_path(char **out, const char *root, const char *name, const char *suffix)
{
	char *id = NULL;
	int status = key(&id, name);
	if (!status && suffix) {
		size_t a = strlen(id), b = strlen(suffix);
		char *p = realloc(id, a + b + 1);
		if (!p)
			status = IO_ERROR_OUT_OF_MEMORY;
		else {
			id = p;
			memcpy(id + a, suffix, b + 1);
		}
	}
	if (!status)
		status = path_join_alloc(out, root, id);
	free(id);
	return status;
}
static int regular_read(void **result, size_t *size, const char *path)
{
	struct fs_info info;
	int status = fs_get_link_info(&info, path);
	if (status)
		return status;
	if (info.kind != FS_ENTRY_FILE)
		return IO_ERROR_NOT_SUPPORTED;
	if (info.size > SIZE_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	FILE *f = NULL;
	status = fs_open(&f, path, "rb");
	if (status)
		return status;
	void *bytes = malloc(info.size ? (size_t)info.size : 1);
	if (!bytes) {
		fclose(f);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	if (fread(bytes, 1, (size_t)info.size, f) != (size_t)info.size)
		status = IO_ERROR_IO;
	if (fclose(f) != 0 && !status)
		status = IO_ERROR_IO;
	if (status)
		free(bytes);
	else {
		*result = bytes;
		*size = (size_t)info.size;
	}
	return status;
}
/* Every component must remain a directory/file, never a link or reparse point.
 * The relative path has already been validated by the descriptor decoder. */
static int confined_path(char **out, const char *base, const char *relative)
{
	char *path = strdup(base), *parts = strdup(relative);
	if (!path || !parts) {
		free(path);
		free(parts);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	int status = 0;
	for (char *p = parts; p;) {
		char *next = strchr(p, '/');
		if (next)
			*next++ = 0;
		char *full = NULL;
		status = path_join_alloc(&full, path, p);
		free(path);
		path = full;
		struct fs_info info;
		if (!status)
			status = fs_get_link_info(&info, path);
		if (!status && info.kind != (next ? FS_ENTRY_DIRECTORY : FS_ENTRY_FILE))
			status = IO_ERROR_NOT_SUPPORTED;
		if (status)
			break;
		p = next;
	}
	free(parts);
	if (status)
		free(path);
	else
		*out = path;
	return status;
}
static int erase_tree(const char *path)
{
	struct fs_info info;
	int status = fs_get_link_info(&info, path);
	if (status == IO_ERROR_NOT_FOUND)
		return 0;
	if (status)
		return status;
	if (info.kind != FS_ENTRY_DIRECTORY)
		return fs_remove_file(path);
	struct amiga_names names = {0};
	status = amiga_directory_names(&names, path);
	for (size_t i = 0; !status && i < names.count; ++i) {
		char *child = NULL;
		status = path_join_alloc(&child, path, names.items[i]);
		if (!status)
			status = erase_tree(child);
		free(child);
	}
	amiga_names_dispose(&names);
	return status ? status : fs_remove_directory(path);
}
static int recover(const char *root, const char *name)
{
	char *live = NULL, *backup = NULL;
	int status = package_path(&live, root, name, NULL);
	if (!status)
		status = package_path(&backup, root, name, ".backup");
	struct fs_info info;
	int found_backup = status ? status : fs_get_link_info(&info, backup);
	if (!status && found_backup != 0 && found_backup != IO_ERROR_NOT_FOUND)
		status = found_backup;
	if (!status && found_backup == 0) {
		int found = fs_get_link_info(&info, live);
		if (found == IO_ERROR_NOT_FOUND)
			status = fs_rename(backup, live);
		else if (!found)
			(void)erase_tree(backup);
		else
			status = found;
	}
	free(live);
	free(backup);
	return status;
}
static int descriptor_read(struct st_amiga_family *family, const char *package, const char *name)
{
	char *file = NULL;
	void *bytes = NULL;
	size_t size = 0;
	struct fs_info info;
	int status = fs_get_link_info(&info, package);
	if (!status && info.kind != FS_ENTRY_DIRECTORY)
		status = IO_ERROR_NOT_SUPPORTED;
	if (!status)
		status = path_join_alloc(&file, package, "family.font");
	if (!status)
		status = regular_read(&bytes, &size, file);
	if (!status)
		status = st_amiga_family_decode(family, name, bytes, size);
	free(bytes);
	free(file);
	return status;
}
static int add_size(unsigned **, size_t *, unsigned);
void st_amiga_font_family_info_dispose(struct st_amiga_font_family_info *family)
{
	if (!family)
		return;
	free(family->name);
	free(family->sizes);
	free(family->installed_sizes);
	*family = (struct st_amiga_font_family_info){0};
}
int st_amiga_fonts_install(const char *root, const char *descriptor, bool replace,
						   struct st_amiga_font_family_info *installed)
{
	if (!root || !descriptor)
		return IO_ERROR_INVALID_ARGUMENT;
	const char *name = path_basename(descriptor);
	char *live = NULL, *backup = NULL, *stage = NULL, *source = NULL, *file = NULL;
	void *bytes = NULL;
	size_t size = 0;
	struct st_amiga_family family = {0};
	struct st_amiga_font_family_info info_result = {0};
	int status = package_path(&live, root, name, NULL);
	if (!status)
		status = package_path(&backup, root, name, ".backup");
	if (!status)
		status = package_path(&stage, root, name, ".stage");
	if (!status)
		status = regular_read(&bytes, &size, descriptor);
	if (!status)
		status = st_amiga_family_decode(&family, name, bytes, size);
	if (!status)
		status = parent(&source, descriptor);
	if (!status)
		status = fs_create_directories(root);
	if (!status)
		status = recover(root, name);
	struct fs_info info;
	bool exists = false;
	if (!status) {
		status = fs_get_link_info(&info, live);
		if (!status) {
			exists = true;
			if (info.kind != FS_ENTRY_DIRECTORY)
				status = IO_ERROR_NOT_SUPPORTED;
		} else if (status == IO_ERROR_NOT_FOUND)
			status = 0;
		if (!status && exists && !replace)
			status = IO_ERROR_ALREADY_EXISTS;
	}
	if (!status && installed) {
		info_result.name = strdup(name);
		info_result.installed = true;
		if (!info_result.name)
			status = IO_ERROR_OUT_OF_MEMORY;
		for (size_t i = 0; !status && i < st_amiga_font_builtin_count(); ++i) {
			unsigned height;
			const char *built = st_amiga_font_builtin_name(i, &height);
			bool equal = false;
			status = st_unicode_caseless_equal(&equal, name, strlen(name), built, strlen(built));
			if (!status && equal) {
				info_result.bundled = true;
				status = add_size(&info_result.sizes, &info_result.count, height);
			}
		}
		for (size_t i = 0; !status && i < family.count; ++i) {
			status = add_size(&info_result.sizes, &info_result.count, family.strikes[i].height);
			if (!status)
				status = add_size(&info_result.installed_sizes, &info_result.installed_count,
								  family.strikes[i].height);
		}
	}
	if (!status)
		status = erase_tree(stage);
	if (!status)
		status = fs_create_directory(stage);
	for (size_t i = 0; !status && i < family.count; ++i) {
		char *input = NULL, *output = NULL;
		st_amiga_font_t *font = NULL;
		status = confined_path(&input, source, family.strikes[i].path);
		if (!status)
			status = path_join_alloc(&output, stage, family.strikes[i].path);
		if (!status)
			status = fs_create_parent_directories(output);
		if (!status)
			status = fs_copy_file(input, output);
		/* Validate the bytes that will be published, even if the source
		 * changed while copying. No caller retains this temporary font. */
		if (!status)
			status = st_amiga_font_read(&font, output);
		if (!status && st_amiga_font_height(font) != family.strikes[i].height)
			status = IO_ERROR_INVALID_FORMAT;
		st_amiga_font_release(font);
		free(input);
		free(output);
	}
	if (!status)
		status = path_join_alloc(&file, stage, "family.font");
	if (!status)
		status = fs_replace_file(file, bytes, size);
	/* Keep the original display name separately from the case-folded key. */
	free(file);
	file = NULL;
	if (!status)
		status = path_join_alloc(&file, stage, "name");
	if (!status)
		status = fs_replace_file(file, name, strlen(name));
	bool moved = false;
	if (!status && exists) {
		status = fs_rename(live, backup);
		moved = status == 0;
	}
	if (!status) {
		status = fs_rename(stage, live);
		if (status && moved)
			(void)fs_rename(backup, live);
		if (!status && moved)
			(void)erase_tree(backup);
	}
	if (stage)
		(void)erase_tree(stage);
	if (!status && installed) {
		*installed = info_result;
		info_result = (struct st_amiga_font_family_info){0};
	}
	st_amiga_font_family_info_dispose(&info_result);
	st_amiga_family_dispose(&family);
	free(live);
	free(backup);
	free(stage);
	free(source);
	free(file);
	free(bytes);
	return status;
}
int st_amiga_fonts_remove(const char *root, const char *name)
{
	if (!root)
		return IO_ERROR_INVALID_ARGUMENT;
	char *live = NULL, *removed = NULL;
	int status = package_path(&live, root, name, NULL);
	if (!status)
		status = package_path(&removed, root, name, ".removed");
	if (!status)
		status = recover(root, name);
	if (!status)
		status = erase_tree(removed);
	if (!status)
		status = fs_rename(live, removed);
	if (!status)
		(void)erase_tree(removed);
	free(live);
	free(removed);
	return status;
}
int st_amiga_fonts_lookup(const st_amiga_font_t **result, const char *root, const char *name,
						  unsigned height)
{
	if (!result || !name)
		return IO_ERROR_INVALID_ARGUMENT;
	int status = IO_ERROR_NOT_FOUND;
	if (root) {
		char *package = NULL, *file = NULL;
		struct st_amiga_family family = {0};
		status = package_path(&package, root, name, NULL);
		if (!status)
			status = recover(root, name);
		if (!status)
			status = descriptor_read(&family, package, name);
		if (!status) {
			status = IO_ERROR_NOT_FOUND;
			size_t selected = family.count;
			for (size_t i = 0; i < family.count; ++i)
				if (family.strikes[i].height == height &&
					(selected == family.count || family.strikes[i].style == 0)) {
					selected = i;
					if (family.strikes[i].style == 0)
						break;
				}
			if (selected < family.count) {
				size_t i = selected;
				status = confined_path(&file, package, family.strikes[i].path);
				st_amiga_font_t *font = NULL;
				if (!status)
					status = st_amiga_font_read(&font, file);
				if (!status && st_amiga_font_height(font) != height) {
					st_amiga_font_release(font);
					status = IO_ERROR_INVALID_FORMAT;
				}
				if (!status) {
					*result = font;
				}
			}
		}
		st_amiga_family_dispose(&family);
		free(package);
		free(file);
		if (!status)
			return 0;
	}
	const st_amiga_font_t *font = st_amiga_font_builtin(name, height);
	if (font) {
		*result = font;
		return status == IO_ERROR_NOT_FOUND ? 0 : status;
	}
	*result = st_amiga_font_builtin("pearl.font", 8);
	return status;
}
static int add_size(unsigned **sizes, size_t *count, unsigned size)
{
	for (size_t i = 0; i < *count; ++i)
		if ((*sizes)[i] == size)
			return 0;
	unsigned *p = realloc(*sizes, (*count + 1) * sizeof(*p));
	if (!p)
		return IO_ERROR_OUT_OF_MEMORY;
	*sizes = p;
	size_t i = *count;
	while (i && p[i - 1] > size) {
		p[i] = p[i - 1];
		--i;
	}
	p[i] = size;
	++*count;
	return 0;
}
static int family_info(struct st_amiga_fonts_catalog *catalog, const char *name,
					   struct st_amiga_font_family_info **out)
{
	for (size_t i = 0; i < catalog->count; ++i) {
		bool equal = false;
		if (st_unicode_caseless_equal(&equal, catalog->families[i].name,
									  strlen(catalog->families[i].name), name, strlen(name)) == 0 &&
			equal) {
			*out = &catalog->families[i];
			return 0;
		}
	}
	if (catalog->count == SIZE_MAX / sizeof(*catalog->families))
		return IO_ERROR_OUT_OF_MEMORY;
	struct st_amiga_font_family_info *p =
		realloc(catalog->families, (catalog->count + 1) * sizeof(*p));
	if (!p)
		return IO_ERROR_OUT_OF_MEMORY;
	catalog->families = p;
	p += catalog->count;
	*p = (struct st_amiga_font_family_info){.name = strdup(name)};
	if (!p->name)
		return IO_ERROR_OUT_OF_MEMORY;
	++catalog->count;
	*out = p;
	return 0;
}
void st_amiga_fonts_catalog_dispose(struct st_amiga_fonts_catalog *c)
{
	for (size_t i = 0; i < c->count; ++i) {
		st_amiga_font_family_info_dispose(&c->families[i]);
	}
	free(c->families);
	*c = (struct st_amiga_fonts_catalog){0};
}
int st_amiga_fonts_catalog_read(struct st_amiga_fonts_catalog *result, const char *root)
{
	if (!result)
		return IO_ERROR_INVALID_ARGUMENT;
	struct st_amiga_fonts_catalog c = {0};
	struct amiga_names names = {0};
	int status = 0;
	for (size_t i = 0; !status && i < st_amiga_font_builtin_count(); ++i) {
		unsigned height;
		const char *name = st_amiga_font_builtin_name(i, &height);
		struct st_amiga_font_family_info *f = NULL;
		status = family_info(&c, name, &f);
		if (!status) {
			f->bundled = true;
			status = add_size(&f->sizes, &f->count, height);
		}
	}
	if (!status && root) {
		status = amiga_directory_names(&names, root);
		if (status == IO_ERROR_NOT_FOUND)
			status = 0;
	}
	for (size_t i = 0; !status && i < names.count; ++i) {
		const char *id = names.items[i];
		if (id[0] == '.')
			continue;
		size_t n = strlen(id);
		if ((n > 6 && !strcmp(id + n - 6, ".stage")) || (n > 8 && !strcmp(id + n - 8, ".removed")))
			continue;
		char *normal = strdup(id);
		if (!normal) {
			status = IO_ERROR_OUT_OF_MEMORY;
			break;
		}
		if (n > 7 && !strcmp(normal + n - 7, ".backup"))
			normal[n - 7] = 0;
		if (!family_name(normal)) {
			free(normal);
			continue;
		}
		status = recover(root, normal);
		char *package = NULL, *file = NULL;
		if (!status)
			status = package_path(&package, root, normal, NULL);
		void *namebytes = NULL;
		size_t namesize = 0;
		if (!status)
			status = path_join_alloc(&file, package, "name");
		if (!status && regular_read(&namebytes, &namesize, file) == 0 && namesize < 256 &&
			!memchr(namebytes, 0, namesize)) {
			char *display = malloc(namesize + 1);
			if (display) {
				memcpy(display, namebytes, namesize);
				display[namesize] = 0;
				char *display_key = NULL, *normal_key = NULL;
				if (key(&display_key, display) == 0 && key(&normal_key, normal) == 0 &&
					strcmp(display_key, normal_key) == 0) {
					free(normal);
					normal = display;
					display = NULL;
				}
				free(display_key);
				free(normal_key);
				free(display);
			} else
				status = IO_ERROR_OUT_OF_MEMORY;
		}
		free(file);
		free(namebytes);
		struct st_amiga_font_family_info *info = NULL;
		if (!status)
			status = family_info(&c, normal, &info);
		struct st_amiga_family family = {0};
		if (!status) {
			info->installed = true;
			info->error = descriptor_read(&family, package, normal);
			for (size_t j = 0; !status && j < family.count; ++j) {
				status = add_size(&info->sizes, &info->count, family.strikes[j].height);
				if (!status)
					status = add_size(&info->installed_sizes, &info->installed_count,
									  family.strikes[j].height);
			}
		}
		st_amiga_family_dispose(&family);
		free(package);
		free(normal);
	}
	amiga_names_dispose(&names);
	if (status)
		st_amiga_fonts_catalog_dispose(&c);
	else
		*result = c;
	return status;
}
