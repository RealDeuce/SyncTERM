/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_palettes.h"

#include "io_error.h"

#include <stdlib.h>

struct palette_entry {
	struct st_rip3_palette_info info;
	struct st_rip3_palette palette;
};

struct st_rip3_palettes {
	unsigned current;
	struct palette_entry entries[36];
};

int
st_rip3_palettes_create(struct st_rip3_palettes **result)
{
	struct st_rip3_palettes *palettes;

	if (result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	palettes = calloc(1, sizeof(*palettes));
	if (palettes == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	(void)st_rip3_palette_defaults(&palettes->entries[0].palette);
	palettes->entries[0].info.defined = true;
	*result = palettes;
	return 0;
}

void
st_rip3_palettes_destroy(struct st_rip3_palettes *palettes)
{
	free(palettes);
}

int
st_rip3_palettes_current(const struct st_rip3_palettes *palettes, unsigned *result)
{
	if (palettes == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	*result = palettes->current;
	return 0;
}

int
st_rip3_palettes_info(const struct st_rip3_palettes *palettes,
    unsigned number, struct st_rip3_palette_info *result)
{
	if (palettes == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (number >= 36)
		return IO_ERROR_OUT_OF_RANGE;
	*result = palettes->entries[number].info;
	return 0;
}

int
st_rip3_palettes_switch(struct st_rip3_palettes *palettes, unsigned number)
{
	if (palettes == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (number >= 36)
		return IO_ERROR_OUT_OF_RANGE;
	if (!palettes->entries[number].info.defined) {
		(void)st_rip3_palette_defaults(&palettes->entries[number].palette);
		palettes->entries[number].info.defined = true;
	}
	palettes->current = number;
	return 0;
}

int
st_rip3_palettes_protect(struct st_rip3_palettes *palettes,
    unsigned number, bool protect)
{
	if (palettes == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (number == 0 || number >= 36)
		return IO_ERROR_OUT_OF_RANGE;
	if (palettes->entries[number].info.defined)
		palettes->entries[number].info.protected = protect;
	return 0;
}

int
st_rip3_palettes_get(const struct st_rip3_palettes *palettes,
    unsigned number, struct st_rip3_palette *result)
{
	if (palettes == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (number >= 36)
		return IO_ERROR_OUT_OF_RANGE;
	if (!palettes->entries[number].info.defined)
		return IO_ERROR_NOT_FOUND;
	*result = palettes->entries[number].palette;
	return 0;
}

int
st_rip3_palettes_desktop(struct st_rip3_palettes *palettes, size_t start,
    const unsigned *master_colors, size_t count)
{
	struct palette_entry *entry;

	if (palettes == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	entry = &palettes->entries[palettes->current];
	if (entry->info.protected)
		return IO_ERROR_PERMISSION_DENIED;
	return st_rip3_palette_desktop(&entry->palette, start, master_colors, count);
}

int
st_rip3_palettes_rgb(struct st_rip3_palettes *palettes, size_t start,
    unsigned bits, const uint32_t *colors, size_t count)
{
	struct palette_entry *entry;

	if (palettes == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	entry = &palettes->entries[palettes->current];
	if (entry->info.protected)
		return IO_ERROR_PERMISSION_DENIED;
	return st_rip3_palette_rgb(&entry->palette, start, bits, colors, count);
}

int
st_rip3_palettes_defaults(struct st_rip3_palettes *palettes)
{
	struct palette_entry *entry;

	if (palettes == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	entry = &palettes->entries[palettes->current];
	if (entry->info.protected)
		return IO_ERROR_PERMISSION_DENIED;
	return st_rip3_palette_defaults(&entry->palette);
}

int
st_rip3_palettes_color(const struct st_rip3_palettes *palettes,
    bool direct, uint32_t color, uint8_t *result)
{
	if (palettes == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	return st_rip3_palette_color(&palettes->entries[palettes->current].palette,
	    direct, color, result);
}

int
st_rip3_palettes_clone(struct st_rip3_palettes **result,
    const struct st_rip3_palettes *palettes)
{
	struct st_rip3_palettes *copy;

	if (result == NULL || palettes == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	copy = malloc(sizeof(*copy));
	if (copy == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	*copy = *palettes;
	*result = copy;
	return 0;
}

int
st_rip3_palettes_restore(struct st_rip3_palettes *palettes,
    const struct st_rip3_palettes *snapshot)
{
	if (palettes == NULL || snapshot == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (palettes == snapshot)
		return 0;
	*palettes = *snapshot;
	return 0;
}
