/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "io_error.h"
#include "st_rip3_backup.h"
#include "st_rip3_palettes.h"

#define CHECK(expression)                                                                        \
	do {                                                                                     \
		if (!(expression)) {                                                             \
			fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
			return 1;                                                                \
		}                                                                                \
	} while (0)

static bool     fail_clone, fail_restore;
static unsigned created, destroyed;
static int     *cleared_values;

static int
clone_value(const void *object, void **result)
{
	int *copy;

	if (fail_clone)
		return IO_ERROR_OUT_OF_MEMORY;
	copy = malloc(sizeof(*copy));
	if (copy == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	*copy   = *(const int *)object;
	*result = copy;
	created++;
	return 0;
}

static int
restore_value(void *object, const void *snapshot)
{
	if (fail_restore)
		return IO_ERROR_IO;
	*(int *)object = *(const int *)snapshot;
	return 0;
}

static void
destroy_value(void *snapshot)
{
	if (cleared_values != NULL)
		*cleared_values++ = *(const int *)snapshot;
	destroyed++;
	free(snapshot);
}

static const struct st_rip3_backup_ops ops = {clone_value, restore_value, destroy_value};

static int
lifecycles(void)
{
	struct st_rip3_backup     *backup = NULL;
	struct st_rip3_backup_info info;
	int                        source = 1, live = 99;

	CHECK(st_rip3_backup_create(&backup, &ops) == 0);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_BASE, 0, &live) == IO_ERROR_NOT_FOUND);
	CHECK(live == 99);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_BASE, 0, &source) == 0);
	source = 2;
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_BASE, 0, &live) == 0 && live == 1);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_BASE, 0, &live) == 0 && live == 1);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_BASE, 0, &source) == 0);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_BASE, 0, &live) == 0 && live == 2);
	CHECK(st_rip3_backup_protect(backup, 5, true) == 0);
	CHECK(st_rip3_backup_info(backup, &info) == 0 && !info.slot_protected[5]);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_SLOT, 5, &source) == 0);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_SLOT, 5, &live) == 0 && live == 2);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_SLOT, 5, &live) == IO_ERROR_NOT_FOUND);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_SLOT, 5, &source) == 0);
	CHECK(st_rip3_backup_protect(backup, 5, true) == 0);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_SLOT, 5, &source) == IO_ERROR_PERMISSION_DENIED);
	CHECK(st_rip3_backup_clear(backup, ST_RIP3_BACKUP_SLOT, 5) == IO_ERROR_PERMISSION_DENIED);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_SLOT, 5, &live) == 0 && live == 2);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_SLOT, 5, &live) == 0 && live == 2);
	for (source = 10; source < 13; source++)
		CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_STACK, 0, &source) == 0);
	for (int expected = 12; expected >= 10; expected--)
		CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_STACK, 0, &live) == 0 && live == expected);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_STACK, 0, &live) == IO_ERROR_NOT_FOUND);
	CHECK(st_rip3_backup_info(backup, &info) == 0);
	CHECK(info.base && info.stack == 0 && info.slots == 1 && info.free == 9);
	st_rip3_backup_destroy(backup);
	CHECK(created == destroyed);
	return 0;
}

static int
capacity(void)
{
	struct st_rip3_backup     *backup = NULL;
	struct st_rip3_backup_info info;
	const unsigned             slots[] = {1, 3, 4, 6, 8, 9};
	int                        source  = 0, live;
	unsigned                   before;

	CHECK(st_rip3_backup_create(&backup, &ops) == 0);
	for (size_t i = 0; i < 6; i++) {
		source = (int)slots[i];
		CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_SLOT, slots[i], &source) == 0);
	}
	for (source = 10; source < 14; source++)
		CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_STACK, 0, &source) == 0);
	before = created;
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_STACK, 0, &source) == IO_ERROR_NO_SPACE);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_SLOT, 2, &source) == IO_ERROR_NO_SPACE);
	CHECK(created == before);
	CHECK(st_rip3_backup_info(backup, &info) == 0);
	CHECK(info.stack == 4 && info.slots == 6 && info.free == 0);
	/* Base does not compete with the ten numbered/stack positions. */
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_BASE, 0, &source) == 0);
	/* Overwrite occupied slot at capacity, then consume it and reuse capacity. */
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_SLOT, 3, &source) == 0);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_SLOT, 3, &live) == 0 && live == 14);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_SLOT, 2, &source) == 0);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_STACK, 0, &live) == 0 && live == 13);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_STACK, 0, &source) == 0);
	CHECK(st_rip3_backup_clear(backup, ST_RIP3_BACKUP_STACK, 0) == 0);
	CHECK(st_rip3_backup_info(backup, &info) == 0 && info.stack == 0 && info.slots == 6);
	CHECK(st_rip3_backup_clear_all(backup, true) == 0);
	for (unsigned i = 0; i < 10; i++)
		CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_STACK, 0, &source) == 0);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_STACK, 0, &source) == IO_ERROR_NO_SPACE);
	CHECK(st_rip3_backup_clear_all(backup, true) == 0);
	for (unsigned i = 0; i < 10; i++)
		CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_SLOT, i, &source) == 0);
	CHECK(st_rip3_backup_info(backup, &info) == 0 && info.free == 0 && info.slots == 10);
	st_rip3_backup_destroy(backup);
	CHECK(created == destroyed);
	return 0;
}

static int
status_and_clear(void)
{
	struct st_rip3_backup     *backup = NULL, *other = NULL;
	struct st_rip3_backup_info info;
	const unsigned             slots[]    = {3, 6, 7};
	int                        source     = 1;
	const int                  numbered[] = {3, 6, 7}, top = 5;
	char                       text[80], previous[80];
	const char                 expected[] = "0:2:3:5:0:0:0:1:0:0:1:1:0:0";

	CHECK(st_rip3_backup_create(&backup, &ops) == 0);
	CHECK(st_rip3_backup_create(&other, &ops) == 0);
	for (size_t i = 0; i < 3; i++)
		CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_SLOT, slots[i], &numbered[i]) == 0);
	for (unsigned i = 0; i < 2; i++)
		CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_STACK, 0, &top) == 0);
	/* Literal BACKSTAT USE example from the pinned reference. */
	CHECK(st_rip3_backup_status(backup, false, text, sizeof(text)) == 0);
	CHECK(strcmp(text, expected) == 0);
	memset(text, 'Q', sizeof(text));
	memcpy(previous, text, sizeof(text));
	CHECK(st_rip3_backup_status(backup, false, text, strlen(expected)) == IO_ERROR_OUT_OF_RANGE);
	CHECK(memcmp(text, previous, sizeof(text)) == 0);
	CHECK(st_rip3_backup_status(backup, false, text, strlen(expected) + 1) == 0);
	CHECK(strcmp(text, expected) == 0);
	CHECK(st_rip3_backup_protect(backup, 3, true) == 0);
	CHECK(st_rip3_backup_protect(backup, 7, true) == 0);
	CHECK(st_rip3_backup_status(backup, true, text, sizeof(text)) == 0);
	CHECK(strcmp(text, "0:0:0:1:0:0:0:1:0:0") == 0);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_BASE, 0, &source) == 0);
	CHECK(st_rip3_backup_save(other, ST_RIP3_BACKUP_STACK, 0, &source) == 0);
	int       order[7]         = {0};
	const int expected_order[] = {1, 6, 5, 5, 0, 0, 0};
	cleared_values             = order;
	CHECK(st_rip3_backup_clear_all(backup, false) == 0);
	cleared_values = NULL;
	CHECK(memcmp(order, expected_order, sizeof(order)) == 0);
	CHECK(st_rip3_backup_info(backup, &info) == 0);
	CHECK(!info.base && info.stack == 0 && info.slots == 2 && info.free == 8);
	CHECK(info.slot_used[3] && info.slot_protected[3] && info.slot_used[7]);
	CHECK(st_rip3_backup_info(other, &info) == 0 && info.stack == 1 && info.free == 9);
	CHECK(st_rip3_backup_clear_all(backup, true) == 0);
	CHECK(st_rip3_backup_status(backup, true, text, sizeof(text)) == 0);
	CHECK(strcmp(text, "0:0:0:0:0:0:0:0:0:0") == 0);
	CHECK(st_rip3_backup_info(backup, &info) == 0 && info.free == 10);
	st_rip3_backup_destroy(backup);
	st_rip3_backup_destroy(other);
	CHECK(created == destroyed);
	return 0;
}

static int
failures(void)
{
	struct st_rip3_backup     *backup = NULL;
	struct st_rip3_backup_info info;
	int                        source = 1, live = 99;

	CHECK(st_rip3_backup_create(&backup, NULL) == IO_ERROR_INVALID_ARGUMENT && backup == NULL);
	CHECK(st_rip3_backup_create(&backup, &(struct st_rip3_backup_ops) {0}) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_backup_create(&backup, &ops) == 0);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_BASE, 1, &source) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_SLOT, 10, &source) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_backup_save(backup, (enum st_rip3_backup_place)3, 0, &source) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_backup_protect(backup, UINT_MAX, true) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_BASE, 0, NULL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_BASE, 0, &source) == 0);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_SLOT, 0, &source) == 0);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_STACK, 0, &source) == 0);
	CHECK(st_rip3_backup_restore_slot_retained(NULL, 0, &live) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_backup_restore_slot_retained(backup, 0, NULL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_backup_restore_slot_retained(backup, 10, &live) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_backup_restore_slot_retained(backup, 9, &live) == IO_ERROR_NOT_FOUND);
	CHECK(st_rip3_backup_restore_slot_retained(backup, 0, &live) == 0 && live == 1);
	CHECK(st_rip3_backup_info(backup, &info) == 0 && info.slot_used[0]);
	live       = 99;
	fail_clone = true;
	source     = 2;
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_BASE, 0, &source) == IO_ERROR_OUT_OF_MEMORY);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_SLOT, 0, &source) == IO_ERROR_OUT_OF_MEMORY);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_SLOT, 1, &source) == IO_ERROR_OUT_OF_MEMORY);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_STACK, 0, &source) == IO_ERROR_OUT_OF_MEMORY);
	fail_clone   = false;
	fail_restore = true;
	CHECK(st_rip3_backup_restore_slot_retained(backup, 0, &live) == IO_ERROR_IO && live == 99);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_BASE, 0, &live) == IO_ERROR_IO && live == 99);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_SLOT, 0, &live) == IO_ERROR_IO && live == 99);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_STACK, 0, &live) == IO_ERROR_IO && live == 99);
	CHECK(st_rip3_backup_info(backup, &info) == 0);
	CHECK(info.base && info.stack == 1 && info.slots == 1 && !info.slot_used[1]);
	fail_restore = false;
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_BASE, 0, &live) == 0 && live == 1);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_SLOT, 0, &live) == 0 && live == 1);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_STACK, 0, &live) == 0 && live == 1);
	st_rip3_backup_destroy(backup);
	st_rip3_backup_destroy(NULL);
	CHECK(created == destroyed);
	return 0;
}

static int
palette_tables(void)
{
	struct st_rip3_backup      *backup   = NULL;
	struct st_rip3_palettes    *palettes = NULL;
	struct st_rip3_palette      palette;
	struct st_rip3_palette_info entry;
	struct st_rip3_backup_info  info;
	const uint32_t              red = 0xff0000, green = 0x00ff00, blue = 0x0000ff;
	unsigned                    current;

	CHECK(st_rip3_palettes_create(&palettes) == 0);
	CHECK(st_rip3_palettes_backup_create(&backup) == 0);
	CHECK(st_rip3_palettes_rgb(palettes, 2, 8, &red, 1) == 0);
	CHECK(st_rip3_palettes_switch(palettes, 4) == 0);
	CHECK(st_rip3_palettes_rgb(palettes, 2, 8, &green, 1) == 0);
	CHECK(st_rip3_palettes_protect(palettes, 4, true) == 0);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_BASE, 0, palettes) == 0);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_SLOT, 3, palettes) == 0);
	CHECK(st_rip3_palettes_protect(palettes, 4, false) == 0);
	CHECK(st_rip3_palettes_rgb(palettes, 2, 8, &blue, 1) == 0);
	CHECK(st_rip3_palettes_protect(palettes, 4, true) == 0);
	CHECK(st_rip3_palettes_switch(palettes, 0) == 0);
	CHECK(st_rip3_palettes_defaults(palettes) == 0);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_BASE, 0, palettes) == 0);
	CHECK(st_rip3_palettes_current(palettes, &current) == 0 && current == 4);
	CHECK(st_rip3_palettes_get(palettes, 4, &palette) == 0 && palette.rgb[2] == green);
	CHECK(st_rip3_palettes_get(palettes, 0, &palette) == 0 && palette.rgb[2] == red);
	CHECK(st_rip3_palettes_info(palettes, 4, &entry) == 0 && entry.protected);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_SLOT, 3, palettes) == 0);
	CHECK(st_rip3_backup_info(backup, &info) == 0 && info.base && !info.slot_used[3]);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_STACK, 0, palettes) == 0);
	CHECK(st_rip3_palettes_switch(palettes, 2) == 0);
	CHECK(st_rip3_palettes_protect(palettes, 2, true) == 0);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_STACK, 0, palettes) == 0);
	CHECK(st_rip3_palettes_current(palettes, &current) == 0 && current == 4);
	CHECK(st_rip3_palettes_info(palettes, 2, &entry) == 0 && !entry.defined && !entry.protected);
	st_rip3_palettes_destroy(palettes);
	st_rip3_backup_destroy(backup);
	return 0;
}

int
main(void)
{
	CHECK(lifecycles() == 0);
	CHECK(capacity() == 0);
	CHECK(status_and_clear() == 0);
	CHECK(failures() == 0);
	CHECK(palette_tables() == 0);
	return 0;
}
