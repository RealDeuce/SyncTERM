/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>
#include <string.h>

#include "controller_internal.h"
#include "filesystem.h"
#include "path.h"

/* Four bytes per descriptor field. Store the evdev identity so changing the
 * presentation order or translating a label never invalidates a profile. */
static int
decode(const unsigned char *row)
{
	return row[0] ? pad_target(row[0], row[1] | ((unsigned)row[2] << 8)) : -1;
}

static bool
apply(struct st_pad *pad, const unsigned char *rows)
{
	for (size_t i = 0; i < pad->count; i++) {
		const unsigned char *row    = rows + 4 * i;
		int                  target = decode(row);
		if ((row[0] && target < 0) || !st_controller_compatible(pad, i, target) || row[3] > 1)
			return false;
		if (target >= 0)
			for (size_t j = 0; j < i; ++j)
				if (decode(rows + 4 * j) == target)
					return false;
	}
	for (size_t i = 0; i < pad->count; i++) {
		pad->fields[i].target   = decode(rows + 4 * i);
		pad->fields[i].inverted = rows[4 * i + 3] != 0;
	}
	pad->remapped = true;
	return true;
}

void
pad_load_mapping(struct st_controllers *hub, struct st_pad *pad)
{
	for (struct st_pad *other = hub->pads; other; other = other->next)
		if (other->count == pad->count && !strcmp(other->profile, pad->profile)) {
			for (size_t i = 0; i < pad->count; i++) {
				pad->fields[i].target   = other->fields[i].target;
				pad->fields[i].inverted = other->fields[i].inverted;
			}
			return;
		}
	char *path = NULL;
	FILE *file;
	if (!hub->directory || path_join_alloc(&path, hub->directory, pad->profile) != 0)
		return;
	if (fs_open(&file, path, "rb") == 0) {
		unsigned char *rows = malloc(pad->count * 4);
		if (rows && fread(rows, 4, pad->count, file) == pad->count && fgetc(file) == EOF && !ferror(file))
			(void)apply(pad, rows);
		free(rows);
		fclose(file);
	}
	free(path);
}

int
st_controller_bind(struct st_controllers *hub, struct st_pad *pad, size_t field, int target, bool invert,
    bool persist)
{
	if (!hub || !pad || pad->source != 1 || field >= pad->count
	    || !st_controller_compatible(pad, field, target))
		return IO_ERROR_INVALID_ARGUMENT;
	unsigned char *rows = calloc(pad->count, 4);
	if (!rows)
		return IO_ERROR_OUT_OF_MEMORY;
	for (size_t i = 0; i < pad->count; i++) {
		int t = i == field ? target : pad->fields[i].target;
		if (t < 0 || (i != field && t == target))
			continue;
		const struct st_control_target *binding = st_controller_target((size_t)t);
		rows[4 * i]                             = binding->kind == ST_BUTTON ? 1 : 3;
		rows[4 * i + 1]                         = binding->code & 255;
		rows[4 * i + 2]                         = binding->code >> 8;
		rows[4 * i + 3] = (binding->kind == ST_STICK || binding->kind == ST_TRIGGER)
		                  && (i == field ? invert : pad->fields[i].inverted);
	}
	int status = 0;
	if (persist) {
		char *path = NULL;
		status =
		    hub->directory ? path_join_alloc(&path, hub->directory, pad->profile) : IO_ERROR_READ_ONLY;
		if (!status)
			status = fs_create_parent_directories(path);
		if (!status)
			status = fs_replace_file(path, rows, pad->count * 4);
		free(path);
	}
	if (!status)
		for (struct st_pad *p = hub->pads; p; p = p->next)
			if (p->source == 1 && p->count == pad->count && !strcmp(pad->profile, p->profile))
				(void)apply(p, rows);
	free(rows);
	return status;
}
