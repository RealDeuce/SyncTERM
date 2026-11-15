/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_backup.h"

#include "io_error.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct saved_object {
	void *value;
	size_t references;
};

struct st_rip3_backup {
	struct st_rip3_backup_ops ops;
	struct saved_object *base;
	struct saved_object *slots[10];
	bool protected[10];
	struct saved_object *stack[10];
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
discard(struct st_rip3_backup *backup, struct saved_object **object)
{
	if (*object != NULL && --(*object)->references == 0) {
		backup->ops.destroy((*object)->value);
		free(*object);
	}
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
	struct saved_object *snapshot;
	struct saved_object **destination;
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
	snapshot = calloc(1, sizeof(*snapshot));
	if (snapshot == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	status = backup->ops.clone(object, &snapshot->value);
	if (status != 0 || snapshot->value == NULL) {
		free(snapshot);
		return status != 0 ? status : IO_ERROR_INVALID_FORMAT;
	}
	snapshot->references = 1;
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
	struct saved_object **source;
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
	status = backup->ops.restore(object, (*source)->value);
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
st_rip3_backup_copy(struct st_rip3_backup *backup,
    struct st_rip3_backup_location source,
    const struct st_rip3_backup_location *destinations, size_t count, void *object)
{
	struct saved_object *snapshot = NULL;
	bool occupied[10];
	bool table_destination = false, consume;
	size_t copies = 0;
	unsigned additional = 0, pushes = 0;
	int status;

	if (backup == NULL || destinations == NULL || count == 0)
		return IO_ERROR_INVALID_ARGUMENT;
	status = source.place == ST_RIP3_BACKUP_TABLE
	    ? (source.index == 0 ? 0 : IO_ERROR_OUT_OF_RANGE)
	    : place_valid(source.place, source.index);
	if (status != 0)
		return status;
	if (source.place == ST_RIP3_BACKUP_STACK && backup->depth == 0)
		return IO_ERROR_NOT_FOUND;
	if (source.place == ST_RIP3_BACKUP_TABLE) {
		if (object == NULL)
			return IO_ERROR_INVALID_ARGUMENT;
	}
	else {
		snapshot = source.place == ST_RIP3_BACKUP_BASE ? backup->base
		    : source.place == ST_RIP3_BACKUP_SLOT ? backup->slots[source.index]
		    : backup->stack[backup->depth - 1];
		if (snapshot == NULL)
			return IO_ERROR_NOT_FOUND;
	}
	for (size_t i = 0; i < 10; i++)
		occupied[i] = backup->slots[i] != NULL;
	for (size_t i = 0; i < count; i++) {
		struct st_rip3_backup_location dest = destinations[i];

		status = dest.place == ST_RIP3_BACKUP_TABLE
		    ? (dest.index == 0 ? 0 : IO_ERROR_OUT_OF_RANGE)
		    : place_valid(dest.place, dest.index);
		if (status != 0)
			return status;
		if (dest.place == source.place && dest.index == source.index)
			return IO_ERROR_INVALID_ARGUMENT;
		if (dest.place == ST_RIP3_BACKUP_TABLE) {
			if (object == NULL)
				return IO_ERROR_INVALID_ARGUMENT;
			table_destination = true;
			continue;
		}
		copies++;
		if (dest.place == ST_RIP3_BACKUP_SLOT) {
			if (backup->protected[dest.index])
				return IO_ERROR_PERMISSION_DENIED;
			if (!occupied[dest.index]) {
				occupied[dest.index] = true;
				additional++;
			}
		}
		else if (dest.place == ST_RIP3_BACKUP_STACK) {
			if (++pushes > 10)
				return IO_ERROR_NO_SPACE;
		}
	}
	consume = source.place == ST_RIP3_BACKUP_STACK
	    || (source.place == ST_RIP3_BACKUP_SLOT && table_destination
	    && !backup->protected[source.index]);
	if (backup->depth + slots_used(backup) + additional + pushes
	    - (consume ? 1u : 0u) > 10)
		return IO_ERROR_NO_SPACE;
	if (copies > SIZE_MAX - (snapshot == NULL ? 1 : snapshot->references))
		return IO_ERROR_OUT_OF_RANGE;
	if (source.place == ST_RIP3_BACKUP_TABLE) {
		snapshot = calloc(1, sizeof(*snapshot));
		if (snapshot == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		status = backup->ops.clone(object, &snapshot->value);
		if (status != 0 || snapshot->value == NULL) {
			free(snapshot);
			return status != 0 ? status : IO_ERROR_INVALID_FORMAT;
		}
		snapshot->references = 1;
	}
	if (table_destination) {
		status = backup->ops.restore(object, snapshot->value);
		if (status != 0)
			return status;
	}
	/* Reserve ownership before replacing destinations that may already share
	 * this snapshot. All remaining operations are infallible and unobservable
	 * outside the owner thread/callback contract. POP has no PUSH destination. */
	snapshot->references += copies;
	for (size_t i = 0; i < count; i++) {
		struct st_rip3_backup_location dest = destinations[i];
		if (dest.place == ST_RIP3_BACKUP_TABLE)
			continue;
		struct saved_object **target = dest.place == ST_RIP3_BACKUP_BASE ? &backup->base
		    : dest.place == ST_RIP3_BACKUP_SLOT ? &backup->slots[dest.index]
		    : &backup->stack[backup->depth++];

		discard(backup, target);
		*target = snapshot;
	}
	if (source.place == ST_RIP3_BACKUP_STACK)
		discard(backup, &backup->stack[--backup->depth]);
	else if (source.place == ST_RIP3_BACKUP_SLOT && consume)
		discard(backup, &backup->slots[source.index]);
	else if (source.place == ST_RIP3_BACKUP_TABLE)
		discard(backup, &snapshot); /* Release temporary ownership of the clone. */
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
