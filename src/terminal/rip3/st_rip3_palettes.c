/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_palettes.h"
#include "st_rip3_backup.h"

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
st_rip3_palettes_current_palette(const struct st_rip3_palettes *palettes,
    struct st_rip3_palette *result)
{
	if (palettes == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	*result = palettes->entries[palettes->current].palette;
	return 0;
}

int
st_rip3_palettes_reset_entry(struct st_rip3_palettes *palettes, unsigned number)
{
	if (palettes == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (number >= 36)
		return IO_ERROR_OUT_OF_RANGE;
	struct palette_entry *entry = &palettes->entries[number];
	if (entry->info.protected)
		return IO_ERROR_PERMISSION_DENIED;
	(void)st_rip3_palette_defaults(&entry->palette);
	entry->info.defined = number == 0;
	return 0;
}

int
st_rip3_palettes_reset(struct st_rip3_palettes *palettes, bool hard)
{
	if (palettes == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	for (unsigned i = 0; i < 36; i++) {
		if (hard) palettes->entries[i].info.protected = false;
		if (!palettes->entries[i].info.protected)
			(void)st_rip3_palettes_reset_entry(palettes,i);
	}
	if (hard) palettes->current = 0;
	return 0;
}

int
st_rip3_palettes_copy_entries(struct st_rip3_palettes *palettes, unsigned source,
    const unsigned *destinations, size_t count)
{
	if (palettes == NULL || destinations == NULL || count == 0)
		return IO_ERROR_INVALID_ARGUMENT;
	if (source >= 36)
		return IO_ERROR_OUT_OF_RANGE;
	if (!palettes->entries[source].info.defined)
		return IO_ERROR_NOT_FOUND;
	for (size_t i = 0; i < count; i++) {
		if (destinations[i] >= 36)
			return IO_ERROR_OUT_OF_RANGE;
		if (destinations[i] == source)
			return IO_ERROR_INVALID_ARGUMENT;
		if (palettes->entries[destinations[i]].info.protected)
			return IO_ERROR_PERMISSION_DENIED;
	}
	for (size_t i = 0; i < count; i++) {
		palettes->entries[destinations[i]] = palettes->entries[source];
		if (destinations[i] == 0) palettes->entries[0].info.protected = false;
	}
	return 0;
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
st_rip3_palettes_color_bits(const struct st_rip3_palettes *palettes,
    unsigned bits, uint32_t color, uint8_t *result)
{
	if (palettes==NULL || result==NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (bits>8) return IO_ERROR_OUT_OF_RANGE;
	if (bits==0) return st_rip3_palettes_color(palettes,false,color,result);
	uint32_t mask=(UINT32_C(1)<<bits)-1;
	if (color>UINT32_C(0xffffff)) return IO_ERROR_OUT_OF_RANGE;
	uint32_t red=(color>>(2*bits))&mask,green=(color>>bits)&mask,blue=color&mask;
	red=(red*255+mask/2)/mask;green=(green*255+mask/2)/mask;blue=(blue*255+mask/2)/mask;
	return st_rip3_palettes_color(palettes,true,(red<<16)|(green<<8)|blue,result);
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

static int
palette_backup_clone(const void *object, void **result)
{
	struct st_rip3_palettes *snapshot = NULL;
	int status = st_rip3_palettes_clone(&snapshot, object);

	if (status == 0)
		*result = snapshot;
	return status;
}

static int
palette_backup_restore(void *object, const void *snapshot)
{
	return st_rip3_palettes_restore(object, snapshot);
}

static void
palette_backup_destroy(void *snapshot)
{
	st_rip3_palettes_destroy(snapshot);
}

int
st_rip3_palettes_backup_create(struct st_rip3_backup **result)
{
	static const struct st_rip3_backup_ops ops = {
		palette_backup_clone, palette_backup_restore, palette_backup_destroy,
	};

	return st_rip3_backup_create(result, &ops);
}


int
st_rip3_palettes_restore_screen(struct st_rip3_palettes *palettes,const struct st_rip3_palette *source)
{
	if (palettes==NULL || source==NULL) return IO_ERROR_INVALID_ARGUMENT;
	for (unsigned i=0;i<256;i++) if (source->rgb[i]>0xffffff) return IO_ERROR_OUT_OF_RANGE;
	palettes->entries[palettes->current].palette=*source;return 0;
}

void
st_rip3_palettes_header_prune(struct st_rip3_palettes *palettes, unsigned keep)
{
	for (unsigned i = 1; i < 36; ++i)
		if (i != keep && !palettes->entries[i].info.protected)
			palettes->entries[i] = (struct palette_entry){0};
}
