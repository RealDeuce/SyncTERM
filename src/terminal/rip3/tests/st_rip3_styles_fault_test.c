/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool fail_calloc, fail_malloc;

static void *
test_calloc(size_t count, size_t size)
{
	if (fail_calloc) {
		fail_calloc = false;
		return NULL;
	}
	return calloc(count, size);
}

static void *
test_malloc(size_t size)
{
	if (fail_malloc) {
		fail_malloc = false;
		return NULL;
	}
	return malloc(size);
}

/* Test-local injection into the unchanged production implementation. */
#define calloc test_calloc
#define malloc test_malloc
#include "../st_rip3_styles.c"
#undef calloc
#undef malloc
#include "st_rip3_host_variables.h"
#include "st_rip3_table_host.h"

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
		return 1; \
	} \
} while (0)

struct observation { unsigned replies, errors; bool mismatch; };

static void
reply(void *context, const uint8_t *bytes, size_t length)
{
	struct observation *seen = context;
	seen->replies++;
	if (length != 1 || bytes[0] != '0') seen->mismatch = true;
}

static void
diagnostic(void *context, enum st_rip3_host_diagnostic kind)
{
	struct observation *seen = context;
	seen->errors++;
	if (kind != ST_RIP3_HOST_RESOURCE_FAILURE) seen->mismatch = true;
}

int
main(void)
{
	struct st_rip3_style initial = {.drawing_color=3,.background_color=1,.fill_color=4,
	    .pen={.pattern=0xffff,.thickness=1}, .font={0,0,1,0}, .write_mode=ST_RIP3_COPY};
	struct st_rip3_style changed, actual;
	struct st_rip3_styles *styles = NULL;
	struct st_rip3_backup *backup = NULL;
	struct st_rip3_backup_info info;
	struct st_rip3_table_host tables = {0};
	struct st_rip3_host_variables host = {.tables=&tables};
	struct observation seen = {0};
	const struct st_rip3_host_sink sink = {&seen,reply,diagnostic};
	const char *copy = "$COPY(STYLE,TBL,S0)$", *query = "$CUR(STYLE)$";

	CHECK(st_rip3_fill_solid(&initial.fill, true) == 0);
	fail_calloc = true;
	CHECK(st_rip3_styles_create(&styles, &initial) == IO_ERROR_OUT_OF_MEMORY && styles == NULL && !fail_calloc);
	CHECK(st_rip3_styles_create(&styles, &initial) == 0);
	CHECK(st_rip3_styles_backup_create(&backup) == 0);
	fail_malloc = true;
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_BASE, 0, styles) == IO_ERROR_OUT_OF_MEMORY && !fail_malloc);
	CHECK(st_rip3_backup_info(backup, &info) == 0 && !info.base && info.free == 10);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_BASE, 0, styles) == 0);
	changed = initial; changed.drawing_color = 7;
	CHECK(st_rip3_styles_set(styles, 0, &changed) == 0);
	fail_malloc = true;
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_BASE, 0, styles) == IO_ERROR_OUT_OF_MEMORY && !fail_malloc);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_BASE, 0, styles) == 0);
	CHECK(st_rip3_styles_get(styles, 0, &actual) == 0 && actual.drawing_color == 3);
	tables.tables[ST_RIP3_TABLE_STYLE] = (struct st_rip3_table_binding){styles,backup};
	fail_malloc = true;
	st_rip3_host_variables_process(&host, &sink, (const uint8_t *)copy, strlen(copy));
	CHECK(!fail_malloc && seen.errors == 1 && seen.replies == 0 && !seen.mismatch);
	CHECK(st_rip3_backup_info(backup, &info) == 0 && !info.slot_used[0]);
	st_rip3_host_variables_process(&host, &sink, (const uint8_t *)query, strlen(query));
	CHECK(seen.errors == 1 && seen.replies == 1 && !seen.mismatch);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_SLOT, 0, styles) == 0);
	st_rip3_styles_destroy(styles);
	st_rip3_backup_destroy(backup);
	return 0;
}
