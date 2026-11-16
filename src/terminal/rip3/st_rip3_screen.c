/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_screen.h"
#include "st_rip3_ports.h"
#include "st_rip3_backup.h"
#include "io_error.h"

#include <stdlib.h>

struct screen_snapshot {
	struct st_rip3_surface *pixels;
	struct st_rip3_palette palette;
	bool status_bar;
};

static bool
bindings_valid(const struct st_rip3_screen *screen)
{
	return screen != NULL && screen->ports != NULL && screen->palette != NULL
	    && screen->status_bar != NULL;
}

static void
screen_destroy(void *object)
{
	struct screen_snapshot *snapshot = object;

	if (snapshot == NULL)
		return;
	st_rip3_surface_destroy(snapshot->pixels);
	free(snapshot);
}

static int
screen_clone(const void *object, void **result)
{
	const struct st_rip3_screen *screen = object;
	struct screen_snapshot *snapshot;
	int status;

	if (!bindings_valid(screen) || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	for (size_t i = 0; i < 256; i++) {
		if (screen->palette->rgb[i] > UINT32_C(0xffffff))
			return IO_ERROR_OUT_OF_RANGE;
	}
	snapshot = calloc(1, sizeof(*snapshot));
	if (snapshot == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	status = st_rip3_ports_screen_clone(screen->ports, &snapshot->pixels);
	if (status != 0) {
		screen_destroy(snapshot);
		return status;
	}
	snapshot->palette = *screen->palette;
	snapshot->status_bar = *screen->status_bar;
	*result = snapshot;
	return 0;
}

static int
screen_restore(void *object, const void *saved)
{
	struct st_rip3_screen *screen = object;
	const struct screen_snapshot *snapshot = saved;
	int status;

	if (!bindings_valid(screen) || snapshot == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	/* Replacement pixels are prepared before any live field changes. Palette
	 * and status assignment cannot fail after the pixel transaction commits. */
	status = st_rip3_ports_screen_restore(screen->ports, snapshot->pixels);
	if (status != 0)
		return status;
	*screen->palette = snapshot->palette;
	*screen->status_bar = snapshot->status_bar;
	return 0;
}

int
st_rip3_screen_backup_create(struct st_rip3_backup **result)
{
	static const struct st_rip3_backup_ops ops = {
		screen_clone, screen_restore, screen_destroy,
	};

	return st_rip3_backup_create(result, &ops);
}
