/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_backup.h"
#include "st_rip3_ports.h"
#include "io_error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
		return 1; \
	} \
} while (0)

static unsigned created, destroyed;
static bool fail_clone, fail_restore;

static int
clone_value(const void *object, void **result)
{
	int *value;

	if (fail_clone)
		return IO_ERROR_OUT_OF_MEMORY;
	value = malloc(sizeof(*value));
	if (value == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	*value = *(const int *)object;
	*result = value;
	created++;
	return 0;
}

static int
restore_value(void *object, const void *saved)
{
	if (fail_restore)
		return IO_ERROR_IO;
	*(int *)object = *(const int *)saved;
	return 0;
}

static void
destroy_value(void *saved)
{
	free(saved);
	destroyed++;
}

static const struct st_rip3_backup_ops ops = {clone_value, restore_value, destroy_value};
static const struct st_rip3_backup_location base = {ST_RIP3_BACKUP_BASE,0};
static const struct st_rip3_backup_location stack = {ST_RIP3_BACKUP_STACK,0};
static const struct st_rip3_backup_location slot0 = {ST_RIP3_BACKUP_SLOT,0};
static const struct st_rip3_backup_location slot1 = {ST_RIP3_BACKUP_SLOT,1};
static const struct st_rip3_backup_location table = {ST_RIP3_BACKUP_TABLE,0};

static int
copy_saved(struct st_rip3_backup *backup, struct st_rip3_backup_location source,
    const struct st_rip3_backup_location *destinations, size_t count)
{
	return st_rip3_backup_copy(backup, source, destinations, count, NULL);
}

static int
single_copies(void)
{
	const struct st_rip3_backup_location sources[] = {base,slot0,stack};
	const struct st_rip3_backup_location destinations[] = {base,slot1,stack};

	for (size_t i = 0; i < 3; i++) {
		for (size_t j = 0; j < 3; j++) {
			struct st_rip3_backup *backup = NULL;
			struct st_rip3_backup_info info;
			int value = 73, live = 0;

			CHECK(st_rip3_backup_create(&backup, &ops) == 0);
			CHECK(st_rip3_backup_save(backup, sources[i].place, sources[i].index, &value) == 0);
			value = 99;
			if (i == j && i != 1) {
				CHECK(copy_saved(backup, sources[i], &destinations[j], 1) == IO_ERROR_INVALID_ARGUMENT);
			}
			else {
				CHECK(copy_saved(backup, sources[i], &destinations[j], 1) == 0);
				CHECK(st_rip3_backup_info(backup, &info) == 0);
				CHECK(info.stack == (j == 2 ? 1u : 0u));
				CHECK(st_rip3_backup_restore(backup, destinations[j].place, destinations[j].index, &live) == 0);
				CHECK(live == 73);
				/* Copying a numbered source into another backup never consumes it. */
				if (i != 2) {
					CHECK(st_rip3_backup_restore(backup, sources[i].place, sources[i].index, &live) == 0);
					CHECK(live == 73);
				}
			}
			st_rip3_backup_destroy(backup);
			CHECK(created == destroyed);
		}
	}
	return 0;
}

static int
multiple_and_aliases(void)
{
	struct st_rip3_backup *backup = NULL;
	struct st_rip3_backup_info info;
	const struct st_rip3_backup_location many[] = {base,slot0,slot1,slot1};
	const struct st_rip3_backup_location pushes[] = {stack,stack,stack};
	int value = 17, live;
	unsigned before;

	CHECK(st_rip3_backup_create(&backup, &ops) == 0);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_STACK, 0, &value) == 0);
	before = created;
	CHECK(copy_saved(backup, stack, many, 4) == 0);
	CHECK(created == before); /* No cast of snapshot into a live-object clone. */
	CHECK(st_rip3_backup_info(backup, &info) == 0 && info.base && info.slots == 2 && info.stack == 0);
	CHECK(copy_saved(backup, base, &slot0, 1) == 0); /* Already shares source. */
	CHECK(copy_saved(backup, slot0, &base, 1) == 0);
	CHECK(st_rip3_backup_protect(backup, 0, true) == 0);
	CHECK(copy_saved(backup, slot0, pushes, 3) == 0); /* Protected source is readable. */
	CHECK(st_rip3_backup_info(backup, &info) == 0 && info.stack == 3 && info.slot_protected[0]);
	/* Replace one owner and mutate a restored live object; other saves retain 17. */
	value = 88;
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_BASE, 0, &value) == 0);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_SLOT, 1, &live) == 0 && live == 17);
	live = 99;
	for (unsigned i = 0; i < 3; i++)
		CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_STACK, 0, &live) == 0 && live == 17);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_BASE, 0, &live) == 0 && live == 88);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_SLOT, 0, &live) == 0 && live == 17);
	CHECK(st_rip3_backup_clear_all(backup, false) == 0);
	CHECK(st_rip3_backup_info(backup, &info) == 0 && info.slots == 1 && info.slot_protected[0]);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_SLOT, 0, &live) == 0 && live == 17);
	st_rip3_backup_destroy(backup);
	CHECK(created == destroyed);
	return 0;
}

static int
capacity_and_errors(void)
{
	struct st_rip3_backup *backup = NULL;
	struct st_rip3_backup_info info;
	struct st_rip3_backup_location dest[12];
	char before[80], after[80];
	int value = 33, live;

	CHECK(st_rip3_backup_create(&backup, &ops) == 0);
	CHECK(copy_saved(backup, base, &slot0, 1) == IO_ERROR_NOT_FOUND);
	CHECK(copy_saved(backup, stack, &slot0, 1) == IO_ERROR_NOT_FOUND);
	CHECK(copy_saved(backup, slot0, &base, 1) == IO_ERROR_NOT_FOUND);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_BASE, 0, &value) == 0);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_STACK, 0, &value) == 0);
	for (unsigned i = 1; i < 10; i++) {
		value = (int)i;
		CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_SLOT, i, &value) == 0);
	}
	CHECK(st_rip3_backup_status(backup, false, before, sizeof(before)) == 0);
	dest[0] = slot1; dest[1] = slot0;
	CHECK(copy_saved(backup, base, dest, 2) == IO_ERROR_NO_SPACE);
	CHECK(st_rip3_backup_status(backup, false, after, sizeof(after)) == 0 && strcmp(before, after) == 0);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_SLOT, 1, &live) == 0 && live == 1);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_SLOT, 1, &live) == 0);
	/* A POP-to-empty-slot atomically exchanges one of the ten occupied places. */
	CHECK(copy_saved(backup, stack, &slot0, 1) == 0);
	CHECK(st_rip3_backup_info(backup, &info) == 0 && info.free == 0 && info.stack == 0 && info.slots == 10);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_SLOT, 0, &live) == 0 && live == 33);
	CHECK(st_rip3_backup_protect(backup, 1, true) == 0);
	dest[0] = slot0; dest[1] = slot1;
	CHECK(copy_saved(backup, base, dest, 2) == IO_ERROR_PERMISSION_DENIED);
	CHECK(st_rip3_backup_info(backup, &info) == 0 && !info.slot_used[0]);
	CHECK(st_rip3_backup_clear_all(backup, true) == 0);
	value = 77;
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_STACK, 0, &value) == 0);
	dest[0] = base; dest[1] = (struct st_rip3_backup_location){ST_RIP3_BACKUP_SLOT,10};
	CHECK(copy_saved(backup, stack, dest, 2) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_backup_info(backup, &info) == 0 && !info.base && info.stack == 1);
	dest[1] = stack;
	CHECK(copy_saved(backup, stack, dest, 2) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_backup_info(backup, &info) == 0 && !info.base && info.stack == 1);
	CHECK(copy_saved(backup, stack, &base, 1) == 0);
	for (size_t i = 0; i < 12; i++) dest[i] = stack;
	CHECK(copy_saved(backup, base, dest, 12) == IO_ERROR_NO_SPACE);
	CHECK(st_rip3_backup_info(backup, &info) == 0 && info.stack == 0);
	CHECK(copy_saved(backup, base, dest, 10) == 0);
	CHECK(st_rip3_backup_info(backup, &info) == 0 && info.stack == 10);
	CHECK(copy_saved(NULL, base, &slot0, 1) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(copy_saved(backup, base, NULL, 1) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(copy_saved(backup, base, &slot0, 0) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(copy_saved(backup, (struct st_rip3_backup_location){ST_RIP3_BACKUP_BASE,1}, &slot0, 1) == IO_ERROR_OUT_OF_RANGE);
	st_rip3_backup_destroy(backup);
	CHECK(created == destroyed);
	return 0;
}

static int
live_tables(void)
{
	struct st_rip3_backup *backup = NULL;
	struct st_rip3_backup_info info;
	const struct st_rip3_backup_location saves[] = {base,slot0,stack};
	const struct st_rip3_backup_location restores[] = {table,base,slot1,table};
	const struct st_rip3_backup_location consume[] = {table,slot1,stack};
	const struct st_rip3_backup_location invalid[] = {base,{ST_RIP3_BACKUP_SLOT,10}};
	int value = 42, live = 99;
	unsigned before;
	char status_before[80], status_after[80];

	CHECK(st_rip3_backup_create(&backup, &ops) == 0);
	before = created;
	CHECK(st_rip3_backup_copy(backup, table, saves, 3, &value) == 0);
	CHECK(created == before + 1);
	value = 81;
	CHECK(st_rip3_backup_status(backup, false, status_before, sizeof(status_before)) == 0);
	CHECK(st_rip3_backup_copy(backup, table, invalid, 2, &value) == IO_ERROR_OUT_OF_RANGE);
	CHECK(created == before + 1); /* Validate even the last destination before cloning. */
	fail_clone = true;
	CHECK(st_rip3_backup_copy(backup, table, saves, 3, &value) == IO_ERROR_OUT_OF_MEMORY);
	fail_clone = false;
	fail_restore = true;
	CHECK(st_rip3_backup_copy(backup, stack, restores, 4, &live) == IO_ERROR_IO && live == 99);
	fail_restore = false;
	CHECK(st_rip3_backup_status(backup, false, status_after, sizeof(status_after)) == 0);
	CHECK(strcmp(status_before, status_after) == 0);
	CHECK(st_rip3_backup_copy(backup, stack, restores, 4, &live) == 0 && live == 42);
	CHECK(st_rip3_backup_info(backup, &info) == 0 && info.base && info.stack == 0 && info.slots == 2);
	CHECK(st_rip3_backup_copy(backup, slot0, consume, 3, &live) == 0 && live == 42);
	CHECK(st_rip3_backup_info(backup, &info) == 0 && !info.slot_used[0] && info.slot_used[1] && info.stack == 1);
	CHECK(st_rip3_backup_copy(backup, base, &slot0, 1, NULL) == 0);
	CHECK(st_rip3_backup_protect(backup, 0, true) == 0);
	CHECK(st_rip3_backup_copy(backup, slot0, &table, 1, &live) == 0 && live == 42);
	CHECK(st_rip3_backup_info(backup, &info) == 0 && info.slot_used[0] && info.slot_protected[0]);
	CHECK(st_rip3_backup_copy(backup, table, &table, 1, &live) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_backup_copy(backup, base, &table, 1, NULL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_backup_copy(backup, table, &base, 1, NULL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_backup_copy(backup, base,
	    &(struct st_rip3_backup_location){ST_RIP3_BACKUP_TABLE,1}, 1, &live) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_backup_clear_all(backup, true) == 0);
	/* Restore frees an unprotected numbered source only after every copy. */
	for (unsigned i = 0; i < 10; i++)
		CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_SLOT, i, &value) == 0);
	{
		const struct st_rip3_backup_location swap[] = {table,stack};
		CHECK(st_rip3_backup_copy(backup, slot0, swap, 2, &live) == 0 && live == 81);
	}
	CHECK(st_rip3_backup_info(backup, &info) == 0 && info.free == 0 && info.stack == 1 && info.slots == 9);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_STACK, 0, &live) == 0 && live == 81);
	st_rip3_backup_destroy(backup);
	CHECK(created == destroyed);
	return 0;
}

static int
owned_port_values(void)
{
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_backup *backup = NULL;
	const struct st_rip3_rect bounds = {0,0,3,2};
	const struct st_rip3_backup_location destinations[] = {slot0,slot1,stack};
	uint8_t pixel;

	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_ports_define(ports, 2, &bounds, true) == 0);
	CHECK(st_rip3_ports_switch(ports, 2) == 0);
	CHECK(st_rip3_ports_erase(ports, 4) == 0);
	CHECK(st_rip3_ports_backup_create(&backup) == 0);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_BASE, 0, ports) == 0);
	CHECK(copy_saved(backup, base, destinations, 3) == 0);
	CHECK(st_rip3_backup_clear(backup, ST_RIP3_BACKUP_BASE, 0) == 0);
	st_rip3_ports_destroy(ports); ports = NULL;
	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_640_480) == 0);
	for (size_t i = 0; i < 3; i++) {
		CHECK(st_rip3_backup_restore(backup, destinations[i].place, destinations[i].index, ports) == 0);
		CHECK(st_rip3_ports_read(ports, 2, 0, 0, &pixel) == 0 && pixel == 4);
		CHECK(st_rip3_ports_plot(ports, 0, 0, 9, ST_RIP3_COPY) == 0);
	}
	st_rip3_ports_destroy(ports);
	st_rip3_backup_destroy(backup);
	return 0;
}

int
main(void)
{
	CHECK(single_copies() == 0);
	CHECK(multiple_and_aliases() == 0);
	CHECK(capacity_and_errors() == 0);
	CHECK(live_tables() == 0);
	CHECK(owned_port_values() == 0);
	return 0;
}
