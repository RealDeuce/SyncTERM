/* Typed adapter for session-owned TW definitions and native metadata. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>

#include "io_error.h"
#include "st_rip3_backup.h"
#include "st_rip3_text_window.h"

struct window_save {
	void *object;
	void  (*destroy)(void *);
};

static void
destroy_window_save(void *object)
{
	struct window_save *save = object;
	if (save == NULL)
		return;
	if (save->object != NULL)
		save->destroy(save->object);
	free(save);
}

static int
capture_window_save(const void *object, void **result)
{
	const struct st_rip3_text_window_operations *ops = object;
	if (ops == NULL || ops->backup_capture == NULL || ops->backup_restore == NULL
	    || ops->backup_destroy == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	struct window_save *save = calloc(1, sizeof(*save));
	if (save == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	save->destroy = ops->backup_destroy;
	int status    = ops->backup_capture(ops->context, &save->object);
	if (status != 0 || save->object == NULL) {
		destroy_window_save(save);
		return status != 0 ? status : IO_ERROR_INVALID_FORMAT;
	}
	*result = save;
	return 0;
}

static int
restore_window_save(void *object, const void *saved)
{
	const struct st_rip3_text_window_operations *ops  = object;
	const struct window_save                    *save = saved;
	return ops == NULL || ops->backup_restore == NULL ? IO_ERROR_NOT_SUPPORTED
	                                                  : ops->backup_restore(ops->context, save->object);
}

int
st_rip3_text_window_backup_create(struct st_rip3_backup **result)
{
	static const struct st_rip3_backup_ops ops = {capture_window_save, restore_window_save,
	    destroy_window_save};
	return st_rip3_backup_create(result, &ops);
}
