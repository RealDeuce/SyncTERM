/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_backup.h"

#include "io_error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct st_rip3_backup {
	struct st_rip3_backup_ops ops;
	void *base;
	void *slots[10];
	bool protected[10];
	void *stack[10];
	unsigned depth;
};

static int
place_valid(enum st_rip3_backup_place place, unsigned index)
{
	if (place < ST_RIP3_BACKUP_BASE || place > ST_RIP3_BACKUP_STACK)
		return IO_ERROR_INVALID_ARGUMENT;
	if (place == ST_RIP3_BACKUP_SLOT ? index >= 10 : index != 0)
		return IO_ERROR_OUT_OF_RANGE;
	return 0;
}

static void
discard(struct st_rip3_backup *backup, void **object)
{
	if (*object != NULL)
		backup->ops.destroy(*object);
	*object = NULL;
}

static unsigned
slots_used(const struct st_rip3_backup *backup)
{
	unsigned count = 0;

	for (size_t i = 0; i < 10; i++)
		count += backup->slots[i] != NULL;
	return count;
}

int
st_rip3_backup_create(struct st_rip3_backup **result,
    const struct st_rip3_backup_ops *ops)
{
	struct st_rip3_backup *backup;

	if (result == NULL || ops == NULL || ops->clone == NULL
	    || ops->restore == NULL || ops->destroy == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	backup = calloc(1, sizeof(*backup));
	if (backup == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	backup->ops = *ops;
	*result = backup;
	return 0;
}

void
st_rip3_backup_destroy(struct st_rip3_backup *backup)
{
	if (backup == NULL)
		return;
	(void)st_rip3_backup_clear_all(backup, true);
	free(backup);
}

int
st_rip3_backup_save(struct st_rip3_backup *backup,
    enum st_rip3_backup_place place, unsigned index, const void *object)
{
	void *snapshot = NULL;
	void **destination;
	int status;

	if (backup == NULL || object == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = place_valid(place, index);
	if (status != 0)
		return status;
	if (place == ST_RIP3_BACKUP_SLOT && backup->protected[index])
		return IO_ERROR_PERMISSION_DENIED;
	if ((place == ST_RIP3_BACKUP_STACK
	    || (place == ST_RIP3_BACKUP_SLOT && backup->slots[index] == NULL))
	    && backup->depth + slots_used(backup) == 10)
		return IO_ERROR_NO_SPACE;
	status = backup->ops.clone(object, &snapshot);
	if (status != 0)
		return status;
	if (snapshot == NULL)
		return IO_ERROR_INVALID_FORMAT;
	destination = place == ST_RIP3_BACKUP_BASE ? &backup->base
	    : place == ST_RIP3_BACKUP_SLOT ? &backup->slots[index]
	    : &backup->stack[backup->depth];
	discard(backup, destination);
	*destination = snapshot;
	if (place == ST_RIP3_BACKUP_STACK)
		backup->depth++;
	return 0;
}

int
st_rip3_backup_restore(struct st_rip3_backup *backup,
    enum st_rip3_backup_place place, unsigned index, void *object)
{
	void **source;
	int status;

	if (backup == NULL || object == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = place_valid(place, index);
	if (status != 0)
		return status;
	if (place == ST_RIP3_BACKUP_STACK && backup->depth == 0)
		return IO_ERROR_NOT_FOUND;
	source = place == ST_RIP3_BACKUP_BASE ? &backup->base
	    : place == ST_RIP3_BACKUP_SLOT ? &backup->slots[index]
	    : &backup->stack[backup->depth - 1];
	if (*source == NULL)
		return IO_ERROR_NOT_FOUND;
	status = backup->ops.restore(object, *source);
	if (status != 0)
		return status;
	if (place == ST_RIP3_BACKUP_STACK) {
		discard(backup, source);
		backup->depth--;
	}
	else if (place == ST_RIP3_BACKUP_SLOT && !backup->protected[index])
		discard(backup, source);
	return 0;
}

int
st_rip3_backup_protect(struct st_rip3_backup *backup, unsigned index, bool protect)
{
	if (backup == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (index >= 10)
		return IO_ERROR_OUT_OF_RANGE;
	if (backup->slots[index] != NULL)
		backup->protected[index] = protect;
	return 0;
}

int
st_rip3_backup_clear(struct st_rip3_backup *backup,
    enum st_rip3_backup_place place, unsigned index)
{
	int status;

	if (backup == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = place_valid(place, index);
	if (status != 0)
		return status;
	if (place == ST_RIP3_BACKUP_BASE)
		discard(backup, &backup->base);
	else if (place == ST_RIP3_BACKUP_SLOT) {
		if (backup->protected[index])
			return IO_ERROR_PERMISSION_DENIED;
		discard(backup, &backup->slots[index]);
	}
	else {
		while (backup->depth != 0)
			discard(backup, &backup->stack[--backup->depth]);
	}
	return 0;
}

int
st_rip3_backup_clear_all(struct st_rip3_backup *backup, bool force)
{
	if (backup == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	discard(backup, &backup->base);
	while (backup->depth != 0)
		discard(backup, &backup->stack[--backup->depth]);
	for (size_t i = 0; i < 10; i++) {
		if (force || !backup->protected[i]) {
			discard(backup, &backup->slots[i]);
			backup->protected[i] = false;
		}
	}
	return 0;
}

int
st_rip3_backup_info(const struct st_rip3_backup *backup,
    struct st_rip3_backup_info *result)
{
	struct st_rip3_backup_info info = {0};

	if (backup == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	info.base = backup->base != NULL;
	info.stack = backup->depth;
	info.slots = slots_used(backup);
	info.free = 10 - info.stack - info.slots;
	for (size_t i = 0; i < 10; i++) {
		info.slot_used[i] = backup->slots[i] != NULL;
		info.slot_protected[i] = backup->protected[i];
	}
	*result = info;
	return 0;
}

int
st_rip3_backup_status(const struct st_rip3_backup *backup,
    bool protection, char *result, size_t capacity)
{
	struct st_rip3_backup_info info;
	char output[80];
	int length, status;

	if (result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_rip3_backup_info(backup, &info);
	if (status != 0)
		return status;
	if (protection) {
		length = snprintf(output, sizeof(output), "%u:%u:%u:%u:%u:%u:%u:%u:%u:%u",
		    (unsigned)info.slot_protected[0], (unsigned)info.slot_protected[1],
		    (unsigned)info.slot_protected[2], (unsigned)info.slot_protected[3],
		    (unsigned)info.slot_protected[4], (unsigned)info.slot_protected[5],
		    (unsigned)info.slot_protected[6], (unsigned)info.slot_protected[7],
		    (unsigned)info.slot_protected[8], (unsigned)info.slot_protected[9]);
	}
	else {
		length = snprintf(output, sizeof(output), "%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u",
		    (unsigned)info.base, info.stack, info.slots, info.free,
		    (unsigned)info.slot_used[0], (unsigned)info.slot_used[1],
		    (unsigned)info.slot_used[2], (unsigned)info.slot_used[3],
		    (unsigned)info.slot_used[4], (unsigned)info.slot_used[5],
		    (unsigned)info.slot_used[6], (unsigned)info.slot_used[7],
		    (unsigned)info.slot_used[8], (unsigned)info.slot_used[9]);
	}
	if (length < 0 || (size_t)length >= sizeof(output))
		return IO_ERROR_IO;
	if ((size_t)length >= capacity)
		return IO_ERROR_OUT_OF_RANGE;
	memcpy(result, output, (size_t)length + 1);
	return 0;
}
