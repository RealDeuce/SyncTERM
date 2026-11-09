/* Compiled catalog plus user-owned disk-font packages. Owner-thread API. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_AMIGA_FONTS_H_
#define ST_AMIGA_FONTS_H_
#include "st_amiga_font.h"
struct st_amiga_font_family_info {
	char *name;
	unsigned *sizes, *installed_sizes;
	size_t count, installed_count;
	bool bundled, installed;
	int error;
};
struct st_amiga_fonts_catalog {
	struct st_amiga_font_family_info *families;
	size_t count;
};
int st_amiga_fonts_directory(char **result, const char *settings_file);
int st_amiga_fonts_catalog_read(struct st_amiga_fonts_catalog *result, const char *directory);
void st_amiga_font_family_info_dispose(struct st_amiga_font_family_info *family);
void st_amiga_fonts_catalog_dispose(struct st_amiga_fonts_catalog *catalog);
/* Import is explicit user consent for the selected descriptor's confined family.
 * Replacing a family publishes only after every strike validates. */
int st_amiga_fonts_install(const char *directory, const char *descriptor, bool replace,
						   struct st_amiga_font_family_info *installed);
int st_amiga_fonts_remove(const char *directory, const char *family);
/* Exact installed strike, exact compiled strike; missing/corrupt custom data is
 * reported even when a compiled fallback is returned. Loaded data is owned. */
int st_amiga_fonts_lookup(const st_amiga_font_t **result, enum st_amiga_font_set set, const char *directory, const char *name,
						  unsigned height);
#endif
